#include <gtest/gtest.h>
#include <ftxui/component/event.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

// ── Fixture ──────────────────────────────────────────────────────────────────

class SelectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 5 lines, each "lineN<content>" so we know byte offsets precisely.
        // "line1abc\nline2def\nline3ghi\nline4jkl\nline5mno\n"
        file_ = std::make_unique<TempFile>(
            "line1abc\n"
            "line2def\n"
            "line3ghi\n"
            "line4jkl\n"
            "line5mno\n");
        reader_.open(file_->path());
        waitForIndexing(reader_);
        ctrl_.getViewData(5, 5);  // initialise pane heights
    }

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }

    std::unique_ptr<TempFile> file_;
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};
};

// ── Basic selection lifecycle ─────────────────────────────────────────────────

TEST_F(SelectionTest, DefaultNoSelection) {
    EXPECT_FALSE(ctrl_.hasSelection());
    EXPECT_FALSE(ctrl_.isSelectionDragging());
    EXPECT_FALSE(ctrl_.getViewData(5, 5).hasSelection);
}

TEST_F(SelectionTest, StartSelectionEntersDragging) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    EXPECT_TRUE(ctrl_.isSelectionDragging());
    EXPECT_FALSE(ctrl_.hasSelection());  // Not active until moved
}

TEST_F(SelectionTest, ExtendSelectionActivatesWhenMoved) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 4);  // Same line, different byte
    EXPECT_TRUE(ctrl_.hasSelection());
}

TEST_F(SelectionTest, ExtendSamePositionNoActivation) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 0);  // Same position
    EXPECT_FALSE(ctrl_.hasSelection());
}

TEST_F(SelectionTest, FinalizeKeepsSelection) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(1, 3);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());
    EXPECT_FALSE(ctrl_.isSelectionDragging());
}

TEST_F(SelectionTest, ClearResetsAllState) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(2, 5);
    ctrl_.clearSelection();
    EXPECT_FALSE(ctrl_.hasSelection());
    EXPECT_FALSE(ctrl_.isSelectionDragging());
    EXPECT_FALSE(ctrl_.getViewData(5, 5).hasSelection);
}

// ── selectionSpans in ViewData ────────────────────────────────────────────────

TEST_F(SelectionTest, SelectionSpansAppearsOnSelectedLines) {
    // Select bytes 0-4 of line 0 (raw pane, visible slice index 0).
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 4);
    ctrl_.finalizeSelection();

    auto data = ctrl_.getViewData(5, 5);
    // Line 0 is visible at rawPane[0]; it should have a selectionSpan.
    ASSERT_FALSE(data.rawPane.empty());
    EXPECT_FALSE(data.rawPane[0].selectionSpans.empty());
    EXPECT_EQ(data.rawPane[0].selectionSpans[0].start, 0u);
    EXPECT_EQ(data.rawPane[0].selectionSpans[0].end,   4u);
}

TEST_F(SelectionTest, SelectionSpansOnlyForSelectedLines) {
    // Select only line 0 (byte 0 to 3).
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 3);
    ctrl_.finalizeSelection();

    auto data = ctrl_.getViewData(5, 5);
    EXPECT_FALSE(data.rawPane[0].selectionSpans.empty());  // selected
    if (data.rawPane.size() > 1) {
        EXPECT_TRUE(data.rawPane[1].selectionSpans.empty());  // not selected
    }
}

TEST_F(SelectionTest, MultiLineSelectionSpansAllLines) {
    // Select from line 0 byte 0 to line 2 byte 3.
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(2, 3);
    ctrl_.finalizeSelection();

    auto data = ctrl_.getViewData(5, 5);
    // Lines 0, 1, 2 should all have selection spans.
    ASSERT_GE(data.rawPane.size(), 3u);
    EXPECT_FALSE(data.rawPane[0].selectionSpans.empty());
    EXPECT_FALSE(data.rawPane[1].selectionSpans.empty());
    EXPECT_FALSE(data.rawPane[2].selectionSpans.empty());
    // Line 3 should not be selected.
    if (data.rawPane.size() > 3) {
        EXPECT_TRUE(data.rawPane[3].selectionSpans.empty());
    }
}

TEST_F(SelectionTest, MultiLineFirstLineBytesFromAnchor) {
    // Anchor at byte 2 of line 0.
    ctrl_.startSelection(FocusArea::Raw, 0, 2);
    ctrl_.extendSelection(1, 5);
    ctrl_.finalizeSelection();

    auto data = ctrl_.getViewData(5, 5);
    ASSERT_GE(data.rawPane.size(), 2u);
    // First line: bytes [2, content.size())
    EXPECT_EQ(data.rawPane[0].selectionSpans[0].start, 2u);
    // Second line: bytes [0, 5)
    EXPECT_EQ(data.rawPane[1].selectionSpans[0].start, 0u);
    EXPECT_EQ(data.rawPane[1].selectionSpans[0].end,   5u);
}

// ── copySelectionToClipboard builds correct text ──────────────────────────────

TEST_F(SelectionTest, CopyNoSelectionDoesNothing) {
    // Should not crash with no selection.
    EXPECT_NO_THROW(ctrl_.copySelectionToClipboard());
}

TEST_F(SelectionTest, SelectionSpansClearedAfterClearSelection) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(1, 4);
    ctrl_.finalizeSelection();

    ctrl_.clearSelection();
    auto data = ctrl_.getViewData(5, 5);
    for (const auto& ll : data.rawPane)
        EXPECT_TRUE(ll.selectionSpans.empty());
}

// ── Auto-clear on content change ──────────────────────────────────────────────

TEST_F(SelectionTest, AutoClearOnFileReset) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(1, 3);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());

    ctrl_.handleFileReset();
    EXPECT_FALSE(ctrl_.hasSelection());
}

TEST_F(SelectionTest, AutoClearOnNewLines) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(1, 3);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());

    ctrl_.handleNewLines(6, 8);  // Simulate new lines arriving
    EXPECT_FALSE(ctrl_.hasSelection());
}

// ── ESC priority ─────────────────────────────────────────────────────────────

TEST_F(SelectionTest, EscClearsSelection) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(1, 3);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());

    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.hasSelection());
}

TEST_F(SelectionTest, EscSelectionHigherPriorityThanSearch) {
    // Start a search.
    key(ftxui::Event::Character('/'));
    for (char c : std::string("line1"))
        key(ftxui::Event::Character(std::string(1, c)));
    key(ftxui::Event::Return);
    EXPECT_FALSE(ctrl_.getViewData(5, 5).searchKeyword.empty());

    // Establish a selection.
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 4);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());

    // First ESC: clears selection (higher priority).
    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.hasSelection());
    EXPECT_FALSE(ctrl_.getViewData(5, 5).searchKeyword.empty());  // search remains

    // Second ESC: clears search.
    key(ftxui::Event::Escape);
    EXPECT_TRUE(ctrl_.getViewData(5, 5).searchKeyword.empty());
}

// ── Pane switch auto-clear ────────────────────────────────────────────────────

TEST_F(SelectionTest, SetFocusClearsSelection) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 4);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());

    ctrl_.setFocus(FocusArea::Filtered);
    EXPECT_FALSE(ctrl_.hasSelection());
}

TEST_F(SelectionTest, SetFocusSamePaneDoesNotClear) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 4);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());

    ctrl_.setFocus(FocusArea::Raw);  // Same pane — should not clear
    EXPECT_TRUE(ctrl_.hasSelection());
}

// ── #22: lineIndex is absolute (not viewport-relative) ───────────────────────

TEST_F(SelectionTest, AbsoluteLineIndexWorksWithScrollOffset) {
    // Use pane height 3 with 5 lines so scrolling is possible.
    // scrollPane(+4) → cursor=4, clampScroll with ph=3 → scrollOffset=2.
    ctrl_.getViewData(3, 3);
    ctrl_.scrollPane(FocusArea::Raw, 4);
    ASSERT_EQ(ctrl_.paneScrollOffset(FocusArea::Raw), 2u) << "precondition: scrollOffset must be 2";

    // Select absolute lines 2..3 (which map to viewport rows 0..1 at scrollOffset=2).
    ctrl_.startSelection(FocusArea::Raw, 2, 0);
    ctrl_.extendSelection(3, 4);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());

    auto data = ctrl_.getViewData(3, 3);
    // rawPane[0] → absIdx=2: selected
    ASSERT_GE(data.rawPane.size(), 2u);
    EXPECT_FALSE(data.rawPane[0].selectionSpans.empty()) << "line at absIdx=2 should be selected";
    EXPECT_FALSE(data.rawPane[1].selectionSpans.empty()) << "line at absIdx=3 should be selected";
    // rawPane[2] → absIdx=4: not selected
    if (data.rawPane.size() > 2) {
        EXPECT_TRUE(data.rawPane[2].selectionSpans.empty()) << "line at absIdx=4 should not be selected";
    }
}

TEST_F(SelectionTest, SelectionDoesNotDriftOnScroll) {
    // Select absolute line 0 bytes 0-4, then scroll so line 0 leaves the viewport.
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 4);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());

    // Use pane height 3; scrollPane(+4) forces scrollOffset=2.
    ctrl_.getViewData(3, 3);
    ctrl_.scrollPane(FocusArea::Raw, 4);
    ASSERT_EQ(ctrl_.paneScrollOffset(FocusArea::Raw), 2u) << "precondition: scrollOffset must be 2";

    // Now the viewport shows absolute lines 2,3,4 — none of which are selected (line 0 was).
    auto data = ctrl_.getViewData(3, 3);
    ASSERT_GE(data.rawPane.size(), 1u);
    for (size_t i = 0; i < data.rawPane.size(); ++i) {
        EXPECT_TRUE(data.rawPane[i].selectionSpans.empty())
            << "viewport row " << i << " (absIdx=" << (2+i) << ") must not show stale selection";
    }
}

// ── #22: paneScrollOffset accessor ───────────────────────────────────────────

TEST_F(SelectionTest, PaneScrollOffsetReflectsState) {
    // Use pane height 3 with 5 lines so scrollPane actually changes scrollOffset.
    ctrl_.getViewData(3, 3);
    EXPECT_EQ(ctrl_.paneScrollOffset(FocusArea::Raw), 0u);
    ctrl_.scrollPane(FocusArea::Raw, 4);  // cursor=4, clampScroll → scrollOffset=2
    EXPECT_EQ(ctrl_.paneScrollOffset(FocusArea::Raw), 2u);
}

// ── #22: scrollHorizontal accessor ───────────────────────────────────────────

TEST_F(SelectionTest, ScrollHorizontalIncreasesOffset) {
    auto data0 = ctrl_.getViewData(5, 5);
    EXPECT_EQ(data0.rawHScroll, 0u);

    ctrl_.scrollHorizontal(FocusArea::Raw, 4);
    auto data1 = ctrl_.getViewData(5, 5);
    EXPECT_EQ(data1.rawHScroll, 4u);
}

TEST_F(SelectionTest, ScrollHorizontalClampsAtZero) {
    ctrl_.scrollHorizontal(FocusArea::Raw, 4);
    ctrl_.scrollHorizontal(FocusArea::Raw, -10);  // Should clamp at 0
    auto data = ctrl_.getViewData(5, 5);
    EXPECT_EQ(data.rawHScroll, 0u);
}

// ── #22: screenColToByteOffset uses absolute index ───────────────────────────

TEST_F(SelectionTest, ScreenColToByteOffsetAbsoluteIndex) {
    // With scrollOffset=0, absolute index 0 = first line "line1abc"
    // prefixColWidth() with line numbers (default): at least 2 (arrow) + digits + 1 (space)
    // We just verify it doesn't crash and returns a reasonable value.
    const size_t byte = ctrl_.screenColToByteOffset(FocusArea::Raw, 0, 10);
    EXPECT_LT(byte, 20u);  // "line1abc" is 8 bytes + hScroll handling
}

TEST_F(SelectionTest, ScreenColToByteOffsetScrolledPane) {
    // Scroll to line 2 (absIdx=2 = "line3ghi").
    ctrl_.scrollPane(FocusArea::Raw, 2);
    // With absolute index 2, should return byte offset for that line.
    const size_t byte = ctrl_.screenColToByteOffset(FocusArea::Raw, 2, 10);
    EXPECT_LT(byte, 20u);  // "line3ghi" is 8 bytes
}

TEST_F(SelectionTest, ScreenColToByteOffsetOutOfBoundsReturnsZero) {
    // Absolute index beyond total lines should return 0 safely.
    const size_t byte = ctrl_.screenColToByteOffset(FocusArea::Raw, 9999, 10);
    EXPECT_EQ(byte, 0u);
}
