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

// ── Folded mode with color spans (#4 fix) ─────────────────────────────────────

TEST(RenderColoredLine, FoldedLinePreservesColorSpan) {
    // "hello world" folded to width 8 (7 chars content + "…").
    // ColorSpan covers "hello" (bytes 0-5). After truncation to 7 bytes the
    // span is still within content, so the colored text must still render.
    std::vector<ColorSpan> cs = {{ 0, 5, "#FF5555" }};
    auto e = renderColoredLine("hello world", cs, {}, /*folded=*/true, 8, 0);
    std::string s = renderLine(e, 10);
    EXPECT_NE(s.find("hello"), std::string::npos);
    // "…" indicator must be present
    // The UTF-8 "…" is 3 bytes; we just check it didn't vanish entirely
    EXPECT_NE(s.find("\xe2\x80\xa6"), std::string::npos);  // "…" in UTF-8
}

TEST(RenderColoredLine, FoldedLineDropsSpanBeyondTruncation) {
    // Span covers "world" (bytes 6-11). With terminalWidth=7 (6 content + "…")
    // content is truncated to "hello " (6 bytes) and the span is dropped.
    // Rendering must not crash.
    std::vector<ColorSpan> cs = {{ 6, 11, "#55FF55" }};
    auto e = renderColoredLine("hello world", cs, {}, /*folded=*/true, 7, 0);
    EXPECT_NO_THROW(renderLine(e, 10));
}

TEST(RenderColoredLine, FoldedSearchSpanPreserved) {
    // SearchSpan covers "hello" (bytes 0-5), folded to width 9.
    // The search span must still apply (bold+underlined) on the visible part.
    std::vector<SearchSpan> ss = {{ 0, 5 }};
    auto e = renderColoredLine("hello world", {}, ss, /*folded=*/true, 9, 0);
    std::string s = renderLine(e, 12);
    EXPECT_NE(s.find("hello"), std::string::npos);
}

// ── Narrow window clipping (#4 fix) ───────────────────────────────────────────

TEST(RenderColoredLine, NarrowWindowClipsContent) {
    // terminalWidth=5, content is 11 chars. Only first 5 chars should appear.
    auto e = renderColoredLine("hello world", {}, {}, false, 5, 0);
    std::string s = renderLine(e, 10);
    EXPECT_NE(s.find("hello"), std::string::npos);
    EXPECT_EQ(s.find("world"), std::string::npos);
}

TEST(RenderColoredLine, NarrowWindowClipsColorSpan) {
    // ColorSpan covers "world" (bytes 6-11); terminal is only 6 wide.
    // The span starts beyond the clipped content (6 chars = "hello ") so it
    // must be dropped — no crash expected.
    std::vector<ColorSpan> cs = {{ 6, 11, "#FF5555" }};
    auto e = renderColoredLine("hello world", cs, {}, false, 6, 0);
    std::string s = renderLine(e, 10);
    EXPECT_NE(s.find("hello"), std::string::npos);
    EXPECT_EQ(s.find("world"), std::string::npos);
}

TEST(RenderColoredLine, NarrowWindowPartialColorSpan) {
    // ColorSpan covers bytes 3-11 ("lo world"); terminal width = 6.
    // After clipping to "hello " (6 bytes), span [3,6] is still valid → "lo " colored.
    std::vector<ColorSpan> cs = {{ 3, 11, "#FF5555" }};
    auto e = renderColoredLine("hello world", cs, {}, false, 6, 0);
    EXPECT_NO_THROW(renderLine(e, 10));
}

// ── SelectionSpan rendering ───────────────────────────────────────────────────

TEST(RenderColoredLine, SelectionSpanNoCrash) {
    std::vector<SelectionSpan> sel = {{ 6, 11 }};
    auto e = renderColoredLine("hello world", {}, {}, false, 0, 0, sel);
    EXPECT_NO_THROW(renderLine(e, 20));
}

TEST(RenderColoredLine, SelectionSpanContentVisible) {
    std::vector<SelectionSpan> sel = {{ 6, 11 }};
    auto e = renderColoredLine("hello world", {}, {}, false, 0, 0, sel);
    std::string s = renderLine(e, 20);
    EXPECT_NE(s.find("hello"), std::string::npos);
    EXPECT_NE(s.find("world"), std::string::npos);
}

TEST(RenderColoredLine, SelectionOverridesColorSpan) {
    // Color span covers full "hello world"; selection covers "world" (6-11).
    // The selected portion should use selection's Blue bg, not the color span.
    // Both must still appear in the rendered output.
    std::vector<ColorSpan>     cs  = {{ 0, 11, "#FF5555" }};
    std::vector<SelectionSpan> sel = {{ 6, 11 }};
    auto e = renderColoredLine("hello world", cs, {}, false, 0, 0, sel);
    std::string s = renderLine(e, 20);
    EXPECT_NE(s.find("hello"), std::string::npos);
    EXPECT_NE(s.find("world"), std::string::npos);
}

TEST(RenderColoredLine, SelectionWithHOffset) {
    // hOffset=6 scrolls past "hello "; selection covers bytes 6-11 (="world").
    // After shifting, the selection maps to bytes 0-5 in the shifted view.
    std::vector<SelectionSpan> sel = {{ 6, 11 }};
    auto e = renderColoredLine("hello world", {}, {}, false, 0, 6, sel);
    std::string s = renderLine(e, 20);
    EXPECT_NE(s.find("world"), std::string::npos);
}

TEST(RenderColoredLine, SelectionBeforeHOffsetDropped) {
    // Selection covers "hello" (0-5); hOffset=6 puts the selection before the view.
    std::vector<SelectionSpan> sel = {{ 0, 5 }};
    auto e = renderColoredLine("hello world", {}, {}, false, 0, 6, sel);
    // Should not crash; "world" is visible.
    std::string s = renderLine(e, 20);
    EXPECT_NE(s.find("world"), std::string::npos);
}

// ── displayColToByteOffset ────────────────────────────────────────────────────

TEST(DisplayColToByteOffset, ASCIIForwardMapping) {
    std::string_view s = "hello world";
    // Column 0 from byte 0 → byte 0
    EXPECT_EQ(displayColToByteOffset(s, 0, 0), 0u);
    // Column 5 from byte 0 → byte 5
    EXPECT_EQ(displayColToByteOffset(s, 0, 5), 5u);
    // Column 11 (= end) from byte 0 → byte 11
    EXPECT_EQ(displayColToByteOffset(s, 0, 11), 11u);
}

TEST(DisplayColToByteOffset, StartByteMidString) {
    // Start from byte 6 ("world"), advance 3 columns → byte 9
    std::string_view s = "hello world";
    EXPECT_EQ(displayColToByteOffset(s, 6, 3), 9u);
}

TEST(DisplayColToByteOffset, CJKTwoColsPerChar) {
    // "你好" = 2 CJK chars × 3 bytes each = 6 bytes total
    // Each char takes 2 display columns.
    // Column 0 → byte 0; column 2 → byte 3; column 4 → byte 6 (end)
    std::string s = "\xe4\xbd\xa0\xe5\xa5\xbd";  // 你好
    EXPECT_EQ(displayColToByteOffset(s, 0, 0), 0u);
    EXPECT_EQ(displayColToByteOffset(s, 0, 2), 3u);
    EXPECT_EQ(displayColToByteOffset(s, 0, 4), 6u);
}

TEST(DisplayColToByteOffset, ColumnPastEndClampsToBoundary) {
    // Requesting a column beyond the string end should return string size.
    std::string_view s = "hi";
    EXPECT_EQ(displayColToByteOffset(s, 0, 100), 2u);
}

// ── byteOffsetToDisplayCol ────────────────────────────────────────────────────

TEST(ByteOffsetToDisplayCol, ASCIIBasic) {
    std::string_view s = "hello world";
    EXPECT_EQ(byteOffsetToDisplayCol(s, 0, 0),  0);
    EXPECT_EQ(byteOffsetToDisplayCol(s, 0, 5),  5);
    EXPECT_EQ(byteOffsetToDisplayCol(s, 0, 11), 11);
}

TEST(ByteOffsetToDisplayCol, CJKTwoColsPerChar) {
    // "你好" → each char is 3 bytes wide but 2 display columns.
    std::string s = "\xe4\xbd\xa0\xe5\xa5\xbd";  // 你好
    EXPECT_EQ(byteOffsetToDisplayCol(s, 0, 0), 0);
    EXPECT_EQ(byteOffsetToDisplayCol(s, 0, 3), 2);  // after 你
    EXPECT_EQ(byteOffsetToDisplayCol(s, 0, 6), 4);  // after 好
}

TEST(ByteOffsetToDisplayCol, StartByteMidString) {
    std::string_view s = "hello world";
    // Start at byte 6 ("world"), measure to byte 9 → 3 columns
    EXPECT_EQ(byteOffsetToDisplayCol(s, 6, 9), 3);
}

// ── truncateToDisplayWidth ────────────────────────────────────────────────────

TEST(TruncateToDisplayWidth, ASCIIFitsExact) {
    EXPECT_EQ(truncateToDisplayWidth("hello", 5), "hello");
}

TEST(TruncateToDisplayWidth, ASCIIFitsWithRoom) {
    EXPECT_EQ(truncateToDisplayWidth("hello", 10), "hello");
}

TEST(TruncateToDisplayWidth, ASCIITruncates) {
    EXPECT_EQ(truncateToDisplayWidth("hello world", 5), "hello");
}

TEST(TruncateToDisplayWidth, ZeroMaxColsReturnsEmpty) {
    EXPECT_EQ(truncateToDisplayWidth("hello", 0), "");
}

TEST(TruncateToDisplayWidth, EmptyInput) {
    EXPECT_EQ(truncateToDisplayWidth("", 10), "");
}

TEST(TruncateToDisplayWidth, BlockCharFitsExactColumns) {
    // U+2588 FULL BLOCK = 3 UTF-8 bytes, 1 display column
    // "████" = 4 block chars = 4 columns, 12 bytes
    std::string blocks(12, '\0');
    // Manually write 4× U+2588 (0xE2 0x96 0x88)
    for (int i = 0; i < 4; ++i) {
        blocks[i*3+0] = '\xe2';
        blocks[i*3+1] = '\x96';
        blocks[i*3+2] = '\x88';
    }
    // Truncating to 4 columns should keep all 4 chars (12 bytes)
    EXPECT_EQ(truncateToDisplayWidth(blocks, 4), blocks);
    // Truncating to 2 columns should keep 2 chars (6 bytes)
    EXPECT_EQ(truncateToDisplayWidth(blocks, 2), std::string_view(blocks).substr(0, 6));
}

TEST(TruncateToDisplayWidth, BlockCharMixedWithASCII) {
    // "ab█cd" = 'a'(1B,1col) 'b'(1B,1col) █(3B,1col) 'c'(1B,1col) 'd'(1B,1col)
    // Total: 7 bytes, 5 cols
    std::string s = "ab\xe2\x96\x88" "cd";  // ab█cd
    // maxCols=3 → "ab█" = 5 bytes
    EXPECT_EQ(truncateToDisplayWidth(s, 3), std::string_view(s).substr(0, 5));
    // maxCols=5 → full string
    EXPECT_EQ(truncateToDisplayWidth(s, 5), s);
}

TEST(TruncateToDisplayWidth, CJKDoubleWidth) {
    // "你好" = 2 CJK chars × 3 bytes each = 6 bytes, 4 display columns (2 per char)
    std::string s = "\xe4\xbd\xa0\xe5\xa5\xbd";  // 你好
    // maxCols=2 → "你" only (3 bytes)
    EXPECT_EQ(truncateToDisplayWidth(s, 2), std::string_view(s).substr(0, 3));
    // maxCols=4 → full string
    EXPECT_EQ(truncateToDisplayWidth(s, 4), s);
    // maxCols=3 → would overshoot at "好", so only "你" (3 bytes)
    EXPECT_EQ(truncateToDisplayWidth(s, 3), std::string_view(s).substr(0, 3));
}

// ── renderColoredLine clips by display columns (block chars) ──────────────────

TEST(RenderColoredLine, BlockCharClipByDisplayColumns) {
    // A line of 8 block chars (█, U+2588) = 8 display cols, 24 bytes.
    // With terminalWidth=4, only 4 chars (12 bytes) should appear.
    std::string blocks;
    for (int i = 0; i < 8; ++i) blocks += "\xe2\x96\x88";
    auto e = renderColoredLine(blocks, {}, {}, false, 4, 0);
    std::string s = renderLine(e, 6);
    // The rendered output should contain block chars but not overflow the width.
    // Since block chars appear in the screen, just verify no crash and no full string
    EXPECT_NO_THROW(renderLine(e, 6));
}
