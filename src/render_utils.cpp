#include "render_utils.hpp"

#include <algorithm>
#include <vector>

using namespace ftxui;

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

// ── Line renderer ──────────────────────────────────────────────────────────────

Element renderColoredLine(std::string_view content,
                           const std::vector<ColorSpan>& spans,
                           const std::vector<SearchSpan>& searchSpans,
                           bool folded,
                           int terminalWidth,
                           size_t hOffset) {
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
        return renderColoredLine(content, shifted, shiftedSS, folded, terminalWidth, 0);
    }

    // Folded mode: truncate content and append "…" (no spans applied)
    if (folded && terminalWidth > 2) {
        size_t maxBytes = static_cast<size_t>(terminalWidth - 2);
        std::string_view truncated = truncateUtf8(content, maxBytes);
        return hbox({ text(std::string(truncated)), text("…") | dim });
    }

    // Fast path: no spans at all
    if (spans.empty() && searchSpans.empty())
        return text(std::string(content));

    // Collect all boundary points from both span types, then render each segment
    // with the appropriate color / bold+underlined decorators.
    std::vector<size_t> pts;
    pts.reserve(2 + spans.size() * 2 + searchSpans.size() * 2);
    pts.push_back(0);
    pts.push_back(content.size());
    for (const auto& s : spans) {
        pts.push_back(s.start);
        pts.push_back(s.end);
    }
    for (const auto& s : searchSpans) {
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

        // Determine color from the first covering ColorSpan
        std::string_view col;
        for (const auto& s : spans)
            if (s.start <= a && b <= s.end) { col = s.color; break; }

        // Determine whether to apply bold+underlined from SearchSpan
        bool isMatch = false;
        for (const auto& s : searchSpans)
            if (s.start <= a && b <= s.end) { isMatch = true; break; }

        Element e = text(std::string(content.substr(a, len)));
        if (!col.empty())  e = e | color(parseHexColor(col));
        if (isMatch)       e = e | bold | underlined;
        parts.push_back(std::move(e));
    }

    if (parts.empty())
        return text(std::string(content));

    return hbox(std::move(parts));
}
