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

// ── UTF-8 display width ───────────────────────────────────────────────────────

// Decode one UTF-8 codepoint starting at s[pos]. Advances pos past the codepoint.
// Returns U+FFFD on invalid sequences.
char32_t decodeUtf8Codepoint(std::string_view s, size_t& pos);

// Display width of a single codepoint: 0 for control, 2 for CJK/fullwidth, 1 otherwise.
int codepointDisplayWidth(char32_t cp);

// Map a display-column offset (0-based) to a byte offset within s,
// starting from byte `startByte`.  Returns byte offset at or just before `targetCol`.
size_t displayColToByteOffset(std::string_view s, size_t startByte, int targetCol);

// Inverse: compute display columns from `startByte` to `targetByte`.
int byteOffsetToDisplayCol(std::string_view s, size_t startByte, size_t targetByte);

// ── Search span ────────────────────────────────────────────────────────────────

// Byte-range of a search keyword match within a log line.
// Rendered with inverted color to distinguish from filter color spans.
struct SearchSpan {
    size_t start;
    size_t end;
};

// ── Selection span ─────────────────────────────────────────────────────────────

// Byte-range of selected text within a log line.
// Rendering priority (highest first): Selection > Search > Filter color.
struct SelectionSpan {
    size_t start;
    size_t end;
};

// ── Line renderer ──────────────────────────────────────────────────────────────

// Render a single log line with optional ColorSpan, SearchSpan, and
// SelectionSpan highlighting.
//
// Rendering priority (highest first):
//   1. SelectionSpan  -> bgcolor Blue + white foreground
//   2. SearchSpan     -> inverted
//   3. ColorSpan      -> custom foreground color
//   4. Default foreground
//
// If `folded` is true and `terminalWidth` > 2, the content is truncated to
// `terminalWidth - 2` bytes (UTF-8 boundary aligned) and "…" is appended.
// Spans are NOT applied to folded lines to keep the implementation simple.
//
// `terminalWidth` == 0 disables folding even when `folded` is true.
//
// `hOffset` (default 0) skips the first `hOffset` bytes of content (UTF-8
// boundary aligned), enabling horizontal scrolling.  Spans are shifted
// accordingly; spans that end before the offset are dropped.
ftxui::Element renderColoredLine(std::string_view content,
                                  const std::vector<ColorSpan>& spans,
                                  const std::vector<SearchSpan>& searchSpans = {},
                                  bool folded = false,
                                  int terminalWidth = 0,
                                  size_t hOffset = 0,
                                  const std::vector<SelectionSpan>& selectionSpans = {});
