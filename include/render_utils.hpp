#pragma once
#include <string_view>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "i_filter_chain.hpp"  // ColorSpan

// ── UTF-8 helpers ──────────────────────────────────────────────────────────────

// Returns true if `pos` is at a valid UTF-8 codepoint start, or if pos >= s.size().
// A continuation byte (0x80–0xBF) returns false.
bool isUtf8Boundary(std::string_view s, size_t pos);

// Truncate `content` to at most `maxBytes` bytes, ensuring the result ends at
// a valid UTF-8 codepoint boundary (backs up if necessary).
std::string_view truncateUtf8(std::string_view content, size_t maxBytes);

// ── Color helper ───────────────────────────────────────────────────────────────

// Parse "#RRGGBB" hex string into an ftxui Color, falling back to white on error.
ftxui::Color parseHexColor(std::string_view hex);

// ── Search span ────────────────────────────────────────────────────────────────

// Byte-range of a search keyword match within a log line.
// Rendered with bold + underlined to distinguish from filter color spans.
struct SearchSpan {
    size_t start;
    size_t end;
};

// ── Line renderer ──────────────────────────────────────────────────────────────

// Render a single log line with optional ColorSpan and SearchSpan highlighting.
//
// ColorSpans apply foreground color to matched filter patterns.
// SearchSpans apply bold + underlined to searched keyword matches.
//
// If `folded` is true and `terminalWidth` > 2, the content is truncated to
// `terminalWidth - 2` bytes (UTF-8 boundary aligned) and "…" is appended.
// Spans are NOT applied to folded lines to keep the implementation simple.
//
// `terminalWidth` == 0 disables folding even when `folded` is true.
ftxui::Element renderColoredLine(std::string_view content,
                                  const std::vector<ColorSpan>& spans,
                                  const std::vector<SearchSpan>& searchSpans = {},
                                  bool folded = false,
                                  int terminalWidth = 0);
