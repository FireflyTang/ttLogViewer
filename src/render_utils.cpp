#include "render_utils.hpp"

#include <algorithm>
#include <vector>

using namespace ftxui;

// ── Control character sanitization ─────────────────────────────────────────────
// Replace ASCII control characters (0x00–0x1F, 0x7F) with '.' so they do not
// reach the terminal as raw bytes (e.g. \r moves the cursor to column 0,
// corrupting the display).  Bytes 0x80–0xFF are preserved as-is since they are
// valid UTF-8 continuation/lead bytes.  '.' replaces 1 byte with 1 byte so
// span byte offsets remain valid.
static std::string sanitizeControlChars(std::string_view s) {
    std::string out(s);
    for (auto& c : out) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F)
            c = '.';
    }
    return out;
}

// ── UTF-8 helpers ──────────────────────────────────────────────────────────────

bool isUtf8Boundary(std::string_view s, size_t pos) {
    if (pos >= s.size()) return true;
    // A continuation byte is 10xxxxxx (0x80–0xBF); anything else is a boundary.
    return (static_cast<unsigned char>(s[pos]) & 0xC0) != 0x80;
}

std::string_view truncateUtf8(std::string_view content, size_t maxBytes) {
    if (content.size() <= maxBytes) return content;
    size_t pos = maxBytes;
    // Back up until we land on a codepoint boundary
    while (pos > 0 && !isUtf8Boundary(content, pos))
        --pos;
    return content.substr(0, pos);
}

// ── Color helper ───────────────────────────────────────────────────────────────

Color parseHexColor(std::string_view hex) {
    if (hex.size() == 7 && hex[0] == '#') {
        auto hexByte = [&](size_t p) -> uint8_t {
            unsigned v = 0;
            for (int i = 0; i < 2; ++i) {
                char c = hex[p + i];
                v = v * 16 + (c >= '0' && c <= '9' ? c - '0'
                            : c >= 'a' && c <= 'f' ? c - 'a' + 10
                            : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
            }
            return static_cast<uint8_t>(v);
        };
        return Color::RGB(hexByte(1), hexByte(3), hexByte(5));
    }
    return Color::White;
}

// ── UTF-8 display width ───────────────────────────────────────────────────────

char32_t decodeUtf8Codepoint(std::string_view s, size_t& pos) {
    if (pos >= s.size()) return U'\xFFFD';
    const auto b0 = static_cast<unsigned char>(s[pos]);

    // Single byte (ASCII)
    if (b0 < 0x80) { pos += 1; return static_cast<char32_t>(b0); }

    // Determine sequence length
    int seqLen;
    char32_t cp;
    if      ((b0 & 0xE0) == 0xC0) { seqLen = 2; cp = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { seqLen = 3; cp = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { seqLen = 4; cp = b0 & 0x07; }
    else { pos += 1; return U'\xFFFD'; } // invalid lead byte

    if (pos + static_cast<size_t>(seqLen) > s.size()) { pos += 1; return U'\xFFFD'; }

    for (int i = 1; i < seqLen; ++i) {
        const auto b = static_cast<unsigned char>(s[pos + static_cast<size_t>(i)]);
        if ((b & 0xC0) != 0x80) { pos += 1; return U'\xFFFD'; }
        cp = (cp << 6) | (b & 0x3F);
    }
    pos += static_cast<size_t>(seqLen);
    return cp;
}

int codepointDisplayWidth(char32_t cp) {
    if (cp < 0x20 || cp == 0x7F) return 0;  // control characters

    // East Asian Wide / Fullwidth ranges (simplified)
    if ((cp >= 0x1100  && cp <= 0x115F)  ||  // Hangul Jamo
        (cp >= 0x2329  && cp <= 0x232A)  ||  // Angle brackets
        (cp >= 0x2E80  && cp <= 0x303E)  ||  // CJK Radicals, Kangxi
        (cp >= 0x3040  && cp <= 0x33FF)  ||  // Hiragana..CJK Compatibility
        (cp >= 0x3400  && cp <= 0x4DBF)  ||  // CJK Extension A
        (cp >= 0x4E00  && cp <= 0xA4CF)  ||  // CJK Unified..Yi
        (cp >= 0xA960  && cp <= 0xA97F)  ||  // Hangul Jamo Extended-A
        (cp >= 0xAC00  && cp <= 0xD7FF)  ||  // Hangul Syllables
        (cp >= 0xF900  && cp <= 0xFAFF)  ||  // CJK Compatibility Ideographs
        (cp >= 0xFE10  && cp <= 0xFE6F)  ||  // Vertical forms + CJK Compat
        (cp >= 0xFF01  && cp <= 0xFF60)  ||  // Fullwidth Forms
        (cp >= 0xFFE0  && cp <= 0xFFE6)  ||  // Fullwidth Signs
        (cp >= 0x1F000 && cp <= 0x1FAFF) ||  // Emoji/Mahjong/Domino
        (cp >= 0x20000 && cp <= 0x2FA1F))    // CJK Extensions B-G
        return 2;

    return 1;
}

size_t displayColToByteOffset(std::string_view s, size_t startByte, int targetCol) {
    if (targetCol <= 0) return startByte;
    size_t pos = startByte;
    int col = 0;
    while (pos < s.size() && col < targetCol) {
        size_t prevPos = pos;
        char32_t cp = decodeUtf8Codepoint(s, pos);
        int w = codepointDisplayWidth(cp);
        if (col + w > targetCol) { return prevPos; }  // would overshoot
        col += w;
    }
    return pos;
}

int byteOffsetToDisplayCol(std::string_view s, size_t startByte, size_t targetByte) {
    int col = 0;
    size_t pos = startByte;
    while (pos < s.size() && pos < targetByte) {
        char32_t cp = decodeUtf8Codepoint(s, pos);
        col += codepointDisplayWidth(cp);
    }
    return col;
}

std::string_view truncateToDisplayWidth(std::string_view content, int maxCols) {
    if (maxCols <= 0) return content.substr(0, 0);
    size_t pos = 0;
    int col = 0;
    while (pos < content.size()) {
        size_t prevPos = pos;
        char32_t cp = decodeUtf8Codepoint(content, pos);
        // Control chars render as '.' → 1 column; use max(1, width) for rendering
        int w = (cp < 0x20 || cp == 0x7F) ? 1 : codepointDisplayWidth(cp);
        if (col + w > maxCols) return content.substr(0, prevPos);
        col += w;
    }
    return content;
}

// ── Line renderer ──────────────────────────────────────────────────────────────

Element renderColoredLine(std::string_view content,
                           const std::vector<ColorSpan>& spans,
                           const std::vector<SearchSpan>& searchSpans,
                           bool folded,
                           int terminalWidth,
                           size_t hOffset,
                           const std::vector<SelectionSpan>& selectionSpans) {
    // Horizontal scroll: shift content and spans by hOffset bytes (UTF-8 safe).
    // Folded lines are not shifted (the fold truncation already implies short content).
    if (hOffset > 0 && !folded) {
        // Advance hOffset to the next UTF-8 codepoint boundary if needed
        while (hOffset < content.size() && !isUtf8Boundary(content, hOffset))
            ++hOffset;
        if (hOffset >= content.size())
            return text("");
        content = content.substr(hOffset);
        // Shift ColorSpans: drop those that end at or before the offset
        std::vector<ColorSpan> shifted;
        for (const auto& s : spans) {
            if (s.end <= hOffset) continue;
            shifted.push_back({s.start > hOffset ? s.start - hOffset : 0,
                               s.end - hOffset, s.color});
        }
        // Shift SearchSpans similarly
        std::vector<SearchSpan> shiftedSS;
        for (const auto& s : searchSpans) {
            if (s.end <= hOffset) continue;
            shiftedSS.push_back({s.start > hOffset ? s.start - hOffset : 0,
                                 s.end - hOffset});
        }
        // Shift SelectionSpans similarly
        std::vector<SelectionSpan> shiftedSel;
        for (const auto& s : selectionSpans) {
            if (s.end <= hOffset) continue;
            shiftedSel.push_back({s.start > hOffset ? s.start - hOffset : 0,
                                  s.end - hOffset});
        }
        return renderColoredLine(content, shifted, shiftedSS,
                                 folded, terminalWidth, 0, shiftedSel);
    }

    // Clip content to terminal width (UTF-8 safe) so that rendering stays within
    // the visible area regardless of FTXUI's clipping behaviour.
    // For folded mode, reserve 1 column for the "…" indicator.
    // Make local mutable copies of spans so we can clip them.
    std::vector<ColorSpan>     clippedSpans(spans);
    std::vector<SearchSpan>    clippedSearchSpans(searchSpans);
    std::vector<SelectionSpan> clippedSelSpans(selectionSpans);

    if (terminalWidth > 0) {
        int avail = (folded && terminalWidth > 1) ? terminalWidth - 1 : terminalWidth;
        content = truncateToDisplayWidth(content, avail);
        // Drop spans that start at or beyond the new content size; clip span ends.
        const size_t sz = content.size();
        auto clipVec = [sz](auto& vec) {
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [sz](const auto& s) { return s.start >= sz; }), vec.end());
            for (auto& s : vec) s.end = std::min(s.end, sz);
        };
        clipVec(clippedSpans);
        clipVec(clippedSearchSpans);
        clipVec(clippedSelSpans);
    }

    // Handle empty content after clipping.
    if (content.empty())
        return folded ? Element(text("…") | dim) : text("");

    // Fast path: no spans at all
    if (clippedSpans.empty() && clippedSearchSpans.empty() && clippedSelSpans.empty()) {
        if (folded && terminalWidth > 1)
            return hbox({ text(sanitizeControlChars(content)), text("…") | dim });
        return text(sanitizeControlChars(content));
    }

    // Collect all boundary points from all span types, then render each segment
    // with the appropriate style based on priority.
    std::vector<size_t> pts;
    pts.reserve(2 + clippedSpans.size() * 2
                  + clippedSearchSpans.size() * 2
                  + clippedSelSpans.size() * 2);
    pts.push_back(0);
    pts.push_back(content.size());
    for (const auto& s : clippedSpans) {
        pts.push_back(s.start);
        pts.push_back(s.end);
    }
    for (const auto& s : clippedSearchSpans) {
        pts.push_back(s.start);
        pts.push_back(s.end);
    }
    for (const auto& s : clippedSelSpans) {
        pts.push_back(s.start);
        pts.push_back(s.end);
    }
    std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());

    Elements parts;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const size_t a = pts[i];
        const size_t b = pts[i + 1];
        if (a >= content.size()) break;
        const size_t len = std::min(b, content.size()) - a;

        // Priority: Selection > Search > Filter color
        bool isSelected = false;
        for (const auto& s : clippedSelSpans)
            if (s.start <= a && b <= s.end) { isSelected = true; break; }

        Element e = text(sanitizeControlChars(content.substr(a, len)));

        if (isSelected) {
            e = e | bgcolor(Color::Blue) | color(Color::White);
        } else {
            // Determine color from the first covering ColorSpan
            std::string_view col;
            for (const auto& s : clippedSpans)
                if (s.start <= a && b <= s.end) { col = s.color; break; }

            // Determine whether to apply inverted color from SearchSpan
            bool isMatch = false;
            for (const auto& s : clippedSearchSpans)
                if (s.start <= a && b <= s.end) { isMatch = true; break; }

            if (!col.empty())  e = e | color(parseHexColor(col));
            if (isMatch)       e = e | inverted;
        }
        parts.push_back(std::move(e));
    }

    if (parts.empty())
        return text(sanitizeControlChars(content));

    // Append "…" indicator for folded lines
    if (folded && terminalWidth > 1)
        parts.push_back(text("…") | dim);

    return hbox(std::move(parts));
}
