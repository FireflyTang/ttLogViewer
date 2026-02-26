#include <gtest/gtest.h>
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include "render_utils.hpp"

using namespace ftxui;

// ── isUtf8Boundary ────────────────────────────────────────────────────────────

TEST(IsUtf8Boundary, ASCIIBytes) {
    std::string_view s = "hello";
    EXPECT_TRUE(isUtf8Boundary(s, 0));
    EXPECT_TRUE(isUtf8Boundary(s, 3));
}

TEST(IsUtf8Boundary, ChineseFirstByte) {
    // "你好" = \xe4\xbd\xa0\xe5\xa5\xbd (each char: 3 bytes)
    std::string s = "\xe4\xbd\xa0\xe5\xa5\xbd";
    EXPECT_TRUE(isUtf8Boundary(s, 0));  // First byte of 你 (0xE4, 11100100)
    EXPECT_TRUE(isUtf8Boundary(s, 3));  // First byte of 好 (0xE5, 11100101)
}

TEST(IsUtf8Boundary, ChineseContinuationBytes) {
    // "你" = \xe4\xbd\xa0; bytes at 1 and 2 are continuation bytes (0x80–0xBF)
    std::string s = "\xe4\xbd\xa0";
    EXPECT_FALSE(isUtf8Boundary(s, 1));  // 0xBD = 10111101 → continuation
    EXPECT_FALSE(isUtf8Boundary(s, 2));  // 0xA0 = 10100000 → continuation
}

TEST(IsUtf8Boundary, PosAtEnd) {
    std::string_view s = "hi";
    EXPECT_TRUE(isUtf8Boundary(s, 2));  // pos == size
    EXPECT_TRUE(isUtf8Boundary(s, 99)); // pos > size
}

// ── truncateUtf8 ──────────────────────────────────────────────────────────────

TEST(TruncateUtf8, ASCIIShort) {
    std::string_view s = "hello";
    EXPECT_EQ(truncateUtf8(s, 10), "hello");  // shorter than maxBytes
}

TEST(TruncateUtf8, ASCIIExact) {
    std::string_view s = "hello";
    EXPECT_EQ(truncateUtf8(s, 5), "hello");
}

TEST(TruncateUtf8, ASCIITruncate) {
    std::string_view s = "hello world";
    EXPECT_EQ(truncateUtf8(s, 5), "hello");
}

TEST(TruncateUtf8, ChineseBoundary) {
    // "你好" = 6 bytes; truncate to 4 should give "你" (3 bytes) not a cut mid-char
    std::string s = "\xe4\xbd\xa0\xe5\xa5\xbd";
    auto result = truncateUtf8(s, 4);
    EXPECT_EQ(result.size(), 3u);           // Backed up to 你 boundary
    EXPECT_EQ(result, s.substr(0, 3));      // "你"
}

TEST(TruncateUtf8, EmptyContent) {
    EXPECT_EQ(truncateUtf8("", 10), "");
}

// ── renderColoredLine – screen rendering ─────────────────────────────────────

// Helper: render an Element to a 1-row screen of given width.
static std::string renderLine(Element e, int width = 40) {
    auto scr = Screen::Create(Dimension::Fixed(width), Dimension::Fixed(1));
    Render(scr, e);
    return scr.ToString();
}

TEST(RenderColoredLine, NoSpansContentVisible) {
    auto e = renderColoredLine("hello world", {});
    std::string s = renderLine(e, 20);
    EXPECT_NE(s.find("hello world"), std::string::npos);
}

TEST(RenderColoredLine, EmptyContentNoCrash) {
    EXPECT_NO_THROW(renderColoredLine("", {}));
}

TEST(RenderColoredLine, OneSpanColorApplied) {
    // Span covers "world" in "hello world"; ANSI codes break the string between
    // "hello " and "world", so check each word separately.
    std::vector<ColorSpan> spans = {{ 6, 11, "#FF5555" }};
    auto e = renderColoredLine("hello world", spans);
    std::string s = renderLine(e, 20);
    EXPECT_NE(s.find("hello"), std::string::npos);
    EXPECT_NE(s.find("world"), std::string::npos);
}

TEST(RenderColoredLine, SpanCoversFullLine) {
    // Single span covering the entire content
    std::vector<ColorSpan> spans = {{ 0, 5, "#55FF55" }};
    auto e = renderColoredLine("hello", spans);
    std::string s = renderLine(e, 20);
    EXPECT_NE(s.find("hello"), std::string::npos);
}

TEST(RenderColoredLine, MultipleSpans) {
    // "aaa bbb ccc": 'a'=0-2, ' '=3, 'b'=4-6, ' '=7, 'c'=8-10
    // Span {0,3} covers "aaa", span {8,11} covers "ccc".
    // "bbb" (4-7) is uncolored gap between spans and must be findable as-is.
    std::vector<ColorSpan> spans = {
        { 0, 3, "#FF5555" },
        { 8, 11, "#55FF55" },
    };
    auto e = renderColoredLine("aaa bbb ccc", spans);
    std::string s = renderLine(e, 20);
    EXPECT_NE(s.find("bbb"), std::string::npos);
    EXPECT_NE(s.find("ccc"), std::string::npos);
}

TEST(RenderColoredLine, FoldedShowsEllipsis) {
    std::string long_line(40, 'x');
    auto e = renderColoredLine(long_line, {}, {}, /*folded=*/true, /*terminalWidth=*/20);
    std::string s = renderLine(e, 30);
    // Should contain the ellipsis marker
    EXPECT_NE(s.find("…"), std::string::npos);
    // Content should be shorter than the original
    EXPECT_EQ(s.find(long_line), std::string::npos);
}

TEST(RenderColoredLine, FoldedZeroWidthNoTruncation) {
    // terminalWidth <= 2 means no truncation
    std::string line = "hello world";
    auto e = renderColoredLine(line, {}, {}, /*folded=*/true, /*terminalWidth=*/0);
    std::string s = renderLine(e, 20);
    EXPECT_NE(s.find("hello world"), std::string::npos);
}

TEST(RenderColoredLine, CJKContentVisible) {
    // "你好世界" = 4 × 3 bytes = 12 bytes
    std::string chinese = "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c";
    auto e = renderColoredLine(chinese, {});
    // Should not crash; content should appear in some form
    EXPECT_NO_THROW(renderLine(e, 30));
}

// ── SearchSpan rendering ──────────────────────────────────────────────────────

TEST(RenderColoredLine, SearchSpanBoldUnderlineNoCrash) {
    std::vector<SearchSpan> ss = {{ 6, 11 }};
    auto e = renderColoredLine("hello world", {}, ss);
    std::string s = renderLine(e, 20);
    // Both parts should appear in the output
    EXPECT_NE(s.find("hello"), std::string::npos);
    EXPECT_NE(s.find("world"), std::string::npos);
}

TEST(RenderColoredLine, SearchSpanAndColorSpanCoexist) {
    std::vector<ColorSpan>  cs = {{ 0, 5, "#FF5555" }};
    std::vector<SearchSpan> ss = {{ 6, 11 }};
    auto e = renderColoredLine("hello world", cs, ss);
    std::string s = renderLine(e, 20);
    EXPECT_NE(s.find("hello"), std::string::npos);
    EXPECT_NE(s.find("world"), std::string::npos);
}

TEST(RenderColoredLine, MultipleSearchSpansAllVisible) {
    // "apple pie apple": two "apple" matches at [0,5) and [10,15)
    std::vector<SearchSpan> ss = {{ 0, 5 }, { 10, 15 }};
    auto e = renderColoredLine("apple pie apple", {}, ss);
    EXPECT_NO_THROW(renderLine(e, 30));
}

// ── hOffset (horizontal scroll) ───────────────────────────────────────────────

TEST(RenderColoredLine, HOffsetZeroShowsFullContent) {
    auto e = renderColoredLine("hello world", {}, {}, false, 0, 0);
    std::string s = renderLine(e, 20);
    EXPECT_NE(s.find("hello"), std::string::npos);
    EXPECT_NE(s.find("world"), std::string::npos);
}

TEST(RenderColoredLine, HOffsetShiftsContentLeft) {
    // hOffset=6 skips "hello " — only "world" should be visible
    auto e = renderColoredLine("hello world", {}, {}, false, 0, 6);
    std::string s = renderLine(e, 20);
    EXPECT_NE(s.find("world"), std::string::npos);
    EXPECT_EQ(s.find("hello "), std::string::npos);
}

TEST(RenderColoredLine, HOffsetBeyondContentRendersEmpty) {
    // Offset past end of content should not crash and render nothing visible
    auto e = renderColoredLine("hi", {}, {}, false, 0, 100);
    EXPECT_NO_THROW(renderLine(e, 20));
}

TEST(RenderColoredLine, HOffsetAdjustsColorSpan) {
    // ColorSpan covers "world" (bytes 6-11); with hOffset=6 the span maps to [0,5)
    std::vector<ColorSpan> cs = {{ 6, 11, "#FF5555" }};
    auto e = renderColoredLine("hello world", cs, {}, false, 0, 6);
    std::string s = renderLine(e, 20);
    EXPECT_NE(s.find("world"), std::string::npos);
}

TEST(RenderColoredLine, HOffsetDropsSpanBeforeOffset) {
    // ColorSpan covers "hello" (bytes 0-5); with hOffset=6 that span is dropped
    std::vector<ColorSpan> cs = {{ 0, 5, "#FF5555" }};
    auto e = renderColoredLine("hello world", cs, {}, false, 0, 6);
    // Should not crash; "world" should still be visible
    std::string s = renderLine(e, 20);
    EXPECT_NE(s.find("world"), std::string::npos);
}
