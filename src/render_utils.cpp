#include "render_utils.hpp"

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
                           bool folded,
                           int terminalWidth) {
    // Folded mode: truncate content and append "…"
    if (folded && terminalWidth > 2) {
        size_t maxBytes = static_cast<size_t>(terminalWidth - 2);
        std::string_view truncated = truncateUtf8(content, maxBytes);
        return hbox({ text(std::string(truncated)), text("…") | dim });
    }

    // Normal rendering with optional ColorSpan highlighting
    if (spans.empty())
        return text(std::string(content));

    Elements parts;
    size_t pos = 0;

    for (const auto& span : spans) {
        if (span.start > pos) {
            parts.push_back(
                text(std::string(content.substr(pos, span.start - pos))));
        }
        if (span.end > span.start) {
            parts.push_back(
                text(std::string(content.substr(span.start, span.end - span.start)))
                | color(parseHexColor(span.color)));
        }
        pos = span.end;
    }

    if (pos < content.size())
        parts.push_back(text(std::string(content.substr(pos))));

    if (parts.empty())
        return text(std::string(content));

    return hbox(std::move(parts));
}
