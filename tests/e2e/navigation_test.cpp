#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <ftxui/component/event.hpp>

#include "app_config.hpp"
#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "mock_filter_chain.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

using ::testing::Return;
using ::testing::_;
using ::testing::AtLeast;

class NavigationTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string content;
        for (int i = 1; i <= 20; ++i)
            content += "line" + std::to_string(i) + "\n";
        file_ = std::make_unique<TempFile>(content);
        reader_.open(file_->path());
        waitForIndexing(reader_);
        ctrl_.getViewData(5, 5);
    }

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }

    size_t highlightedLine() {
        for (auto& ll : ctrl_.getViewData(5, 5).rawPane)
            if (ll.highlighted) return ll.rawLineNo;
        return 0;
    }

    std::unique_ptr<TempFile> file_;
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};

    void type(const std::string& s) {
        for (char c : s)
            key(ftxui::Event::Character(std::string(1, c)));
    }
};

// ── g goto line ───────────────────────────────────────────────────────────────

TEST_F(NavigationTest, GotoLineJumps) {
    key(ftxui::Event::Character('g'));
    type("10");
    key(ftxui::Event::Return);
    EXPECT_EQ(highlightedLine(), 10u);
}

TEST_F(NavigationTest, GotoLineClampsAtEnd) {
    key(ftxui::Event::Character('g'));
    type("999");
    key(ftxui::Event::Return);
    EXPECT_EQ(highlightedLine(), 20u);
}

TEST_F(NavigationTest, GotoLineEscCancels) {
    for (int i = 0; i < 5; ++i) key(ftxui::Event::ArrowDown);
    size_t before = highlightedLine();

    key(ftxui::Event::Character('g'));
    type("1");
    key(ftxui::Event::Escape);
    // Cursor should not have moved
    EXPECT_EQ(highlightedLine(), before);
}

TEST_F(NavigationTest, GotoLineZeroGoesToFirst) {
    key(ftxui::Event::ArrowDown);
    key(ftxui::Event::ArrowDown);
    key(ftxui::Event::Character('g'));
    type("0");
    key(ftxui::Event::Return);
    EXPECT_EQ(highlightedLine(), 1u);
}

// ── Two-pane independent scrolling ───────────────────────────────────────────

TEST_F(NavigationTest, TwoPanesScrollIndependently) {
    // Add a filter so filtered pane has content
    chain_.append({.pattern = "line1"});
    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    // Move raw pane cursor
    for (int i = 0; i < 5; ++i) key(ftxui::Event::ArrowDown);
    size_t rawLine = highlightedLine();

    // Switch to filtered pane
    key(ftxui::Event::Tab);
    // Move filtered pane cursor
    key(ftxui::Event::ArrowDown);

    // Switch back
    key(ftxui::Event::Tab);
    // Raw pane cursor should be unchanged
    EXPECT_EQ(highlightedLine(), rawLine);
}

// ── g disabled while indexing ─────────────────────────────────────────────────

TEST_F(NavigationTest, GotoNotAvailableWhileIndexing) {
    // isIndexing() always returns false in Phase 2 (synchronous indexing)
    // So this just verifies the g key works when not indexing
    EXPECT_FALSE(reader_.isIndexing());
    key(ftxui::Event::Character('g'));
    EXPECT_TRUE(ctrl_.isInputActive());
    key(ftxui::Event::Escape);
}

// ── Phase 3: l / z / h keys ───────────────────────────────────────────────────

TEST_F(NavigationTest, LKeyTogglesShowLineNumbers) {
    // Line numbers are ON by default; 'l' toggles the state.
    EXPECT_TRUE(ctrl_.getViewData(5, 5).showLineNumbers);
    key(ftxui::Event::Character('l'));
    EXPECT_FALSE(ctrl_.getViewData(5, 5).showLineNumbers);
    key(ftxui::Event::Character('l'));
    EXPECT_TRUE(ctrl_.getViewData(5, 5).showLineNumbers);
}

TEST_F(NavigationTest, ZKeyFoldsHighlightedLine) {
    // Cursor is on line 1
    auto d0 = ctrl_.getViewData(5, 5);
    bool foldedBefore = false;
    for (auto& ll : d0.rawPane) if (ll.highlighted) foldedBefore = ll.folded;
    EXPECT_FALSE(foldedBefore);

    key(ftxui::Event::Character('z'));
    auto d1 = ctrl_.getViewData(5, 5);
    bool foldedAfter = false;
    for (auto& ll : d1.rawPane) if (ll.highlighted) foldedAfter = ll.folded;
    EXPECT_TRUE(foldedAfter);
}

TEST_F(NavigationTest, ZKeyUnfoldsOnSecondPress) {
    key(ftxui::Event::Character('z'));
    key(ftxui::Event::Character('z'));
    auto d = ctrl_.getViewData(5, 5);
    for (auto& ll : d.rawPane) {
        if (ll.highlighted) {
            EXPECT_FALSE(ll.folded);
        }
    }
}

TEST_F(NavigationTest, HKeyShowsDialog) {
    key(ftxui::Event::Character('h'));
    auto d = ctrl_.getViewData(5, 5);
    EXPECT_TRUE(d.showDialog);
    EXPECT_FALSE(d.dialogHasChoice);
    EXPECT_NE(d.dialogBody.find("q"), std::string::npos);
}

TEST_F(NavigationTest, HKeyDialogClosesOnAnyKey) {
    key(ftxui::Event::Character('h'));
    EXPECT_TRUE(ctrl_.getViewData(5, 5).showDialog);
    key(ftxui::Event::Return);
    EXPECT_FALSE(ctrl_.getViewData(5, 5).showDialog);
}

// ── Horizontal scroll (ArrowLeft / ArrowRight) ────────────────────────────────

TEST_F(NavigationTest, ArrowRightIncreasesHScroll) {
    key(ftxui::Event::ArrowRight);
    EXPECT_GT(ctrl_.getViewData(5, 5).rawHScroll, 0u);
}

TEST_F(NavigationTest, ArrowRightScrollsByConfiguredStep) {
    // One right-arrow press must scroll by exactly AppConfig::hScrollStep bytes.
    key(ftxui::Event::ArrowRight);
    const size_t expected = static_cast<size_t>(AppConfig::global().hScrollStep);
    EXPECT_EQ(ctrl_.getViewData(5, 5).rawHScroll, expected);
}

TEST_F(NavigationTest, ArrowLeftDecreasesHScroll) {
    key(ftxui::Event::ArrowRight);
    key(ftxui::Event::ArrowRight);
    const size_t after2 = ctrl_.getViewData(5, 5).rawHScroll;
    key(ftxui::Event::ArrowLeft);
    EXPECT_LT(ctrl_.getViewData(5, 5).rawHScroll, after2);
}

TEST_F(NavigationTest, ArrowLeftAtZeroStaysZero) {
    key(ftxui::Event::ArrowLeft);
    EXPECT_EQ(ctrl_.getViewData(5, 5).rawHScroll, 0u);
}

TEST_F(NavigationTest, HScrollIsIndependentPerPane) {
    // Scroll raw pane right
    key(ftxui::Event::ArrowRight);
    auto d = ctrl_.getViewData(5, 5);
    EXPECT_GT(d.rawHScroll, 0u);
    EXPECT_EQ(d.filtHScroll, 0u);

    // Switch to filtered pane and scroll right
    key(ftxui::Event::Tab);
    key(ftxui::Event::ArrowRight);
    d = ctrl_.getViewData(5, 5);
    EXPECT_GT(d.filtHScroll, 0u);
    // Raw pane hScroll should be unchanged
    EXPECT_GT(d.rawHScroll, 0u);
}

// ── scrollPane / setFocus API ─────────────────────────────────────────────────

TEST_F(NavigationTest, ScrollPaneDoesNotChangeFocus) {
    // Default focus is raw; scrollPane on filtered must not change focus
    ctrl_.scrollPane(FocusArea::Filtered, 1);
    EXPECT_TRUE(ctrl_.getViewData(5, 5).rawFocused);
}

TEST_F(NavigationTest, SetFocusSwitchesToFilteredPane) {
    ctrl_.setFocus(FocusArea::Filtered);
    auto d = ctrl_.getViewData(5, 5);
    EXPECT_FALSE(d.rawFocused);
    EXPECT_TRUE(d.filteredFocused);
}

TEST_F(NavigationTest, SetFocusRoundTrip) {
    ctrl_.setFocus(FocusArea::Filtered);
    ctrl_.setFocus(FocusArea::Raw);
    EXPECT_TRUE(ctrl_.getViewData(5, 5).rawFocused);
}

// ── Bug #3: raw pane must never show filter match colors ──────────────────────

TEST_F(NavigationTest, RawPaneHasNoFilterColors) {
    // Add a filter that matches "line" (every line in the fixture contains it).
    // computeColors() works without reprocess, so no waiting needed.
    chain_.append(FilterDef{"line", "#FF5555", true, false, false});

    auto d = ctrl_.getViewData(5, 5);
    for (const auto& ll : d.rawPane)
        EXPECT_TRUE(ll.colors.empty()) << "raw pane line " << ll.rawLineNo
                                       << " must not carry filter colors";
}

// ── Feature #2: quit confirmation dialog ─────────────────────────────────────

TEST_F(NavigationTest, IsDialogOpenReturnsFalseInitially) {
    EXPECT_FALSE(ctrl_.isDialogOpen());
}

TEST_F(NavigationTest, RequestQuitShowsDialog) {
    bool called = false;
    ctrl_.requestQuit([&called] { called = true; });
    EXPECT_TRUE(ctrl_.isDialogOpen());
    // The exit callback must NOT have been called yet (user hasn't confirmed)
    EXPECT_FALSE(called);
}

TEST_F(NavigationTest, RequestQuitNoopIfDialogAlreadyOpen) {
    // Open the help dialog first
    key(ftxui::Event::Character('h'));
    EXPECT_TRUE(ctrl_.isDialogOpen());
    // requestQuit must not override the existing dialog
    bool called = false;
    ctrl_.requestQuit([&called] { called = true; });
    auto d = ctrl_.getViewData(5, 5);
    // Dialog is still the help dialog (not a choice dialog)
    EXPECT_FALSE(d.dialogHasChoice);
    EXPECT_FALSE(called);
}

// ── Feature #7: search keyword exposed in ViewData ───────────────────────────

TEST_F(NavigationTest, SearchKeywordEmptyInitially) {
    EXPECT_TRUE(ctrl_.getViewData(5, 5).searchKeyword.empty());
}

TEST_F(NavigationTest, SearchKeywordSetAfterSearch) {
    key(ftxui::Event::Character('/'));
    type("line1");
    key(ftxui::Event::Return);
    auto d = ctrl_.getViewData(5, 5);
    EXPECT_EQ(d.searchKeyword, "line1");
    EXPECT_GT(d.searchResultCount, 0u);
    EXPECT_GE(d.searchResultIndex, 1u);
}

TEST_F(NavigationTest, SearchKeywordClearedOnEmptySearch) {
    // First do a real search
    key(ftxui::Event::Character('/'));
    type("line1");
    key(ftxui::Event::Return);
    EXPECT_FALSE(ctrl_.getViewData(5, 5).searchKeyword.empty());
    // Now clear by searching with empty string
    key(ftxui::Event::Character('/'));
    key(ftxui::Event::Return);
    EXPECT_TRUE(ctrl_.getViewData(5, 5).searchKeyword.empty());
}

// ── Feature #8: pane height ratio 6:4 ────────────────────────────────────────

TEST_F(NavigationTest, PaneHeightRatio6to4) {
    // With uiOverheadRows=6 (default), available = 26 - 6 = 20
    // raw = int(20 * 0.6) = 12,  filt = 20 - 12 = 8
    ctrl_.onTerminalResize(80, 26);
    EXPECT_EQ(ctrl_.rawPaneHeight(),  12);
    EXPECT_EQ(ctrl_.filtPaneHeight(),  8);
}

TEST_F(NavigationTest, PaneHeightRatio6to4SmallTerminal) {
    // With only 10 available rows: raw = int(10 * 0.6) = 6, filt = 4
    // uiOverheadRows=6, so height=16 → available=10
    ctrl_.onTerminalResize(80, 16);
    EXPECT_EQ(ctrl_.rawPaneHeight(),  6);
    EXPECT_EQ(ctrl_.filtPaneHeight(), 4);
}

// ── Text selection state ──────────────────────────────────────────────────────

TEST_F(NavigationTest, NoSelectionByDefault) {
    EXPECT_FALSE(ctrl_.hasSelection());
    EXPECT_FALSE(ctrl_.getViewData(5, 5).hasSelection);
}

TEST_F(NavigationTest, StartExtendFinalizeSelection) {
    // Simulate a drag: press at line 0 byte 0, extend to line 1 byte 3.
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    EXPECT_FALSE(ctrl_.hasSelection());   // not active yet, needs extend
    EXPECT_TRUE(ctrl_.isSelectionDragging());

    ctrl_.extendSelection(1, 3);
    EXPECT_TRUE(ctrl_.hasSelection());    // now active (anchor != current)

    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());    // still active (user must ESC or click)
    EXPECT_FALSE(ctrl_.isSelectionDragging());
}

TEST_F(NavigationTest, ClearSelectionResetsState) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(1, 3);
    EXPECT_TRUE(ctrl_.hasSelection());

    ctrl_.clearSelection();
    EXPECT_FALSE(ctrl_.hasSelection());
    EXPECT_FALSE(ctrl_.isSelectionDragging());
}

TEST_F(NavigationTest, ViewDataHasSelectionFlag) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 3);
    EXPECT_TRUE(ctrl_.getViewData(5, 5).hasSelection);

    ctrl_.clearSelection();
    EXPECT_FALSE(ctrl_.getViewData(5, 5).hasSelection);
}

TEST_F(NavigationTest, MKeyTypedInInputMode) {
    // 'm' in filter-add input should still go to the input buffer (no special mode)
    key(ftxui::Event::Character('a'));   // enter FilterAdd
    key(ftxui::Event::Character('m'));   // typed into buffer
    EXPECT_EQ(ctrl_.getViewData(5, 5).inputBuffer, "m");
}

// ── Feature #6: ESC cancels reprocess when progress is shown ─────────────────

// Fixture that uses MockFilterChain so reprocess() never calls onDone,
// keeping showProgress_ == true until ESC is pressed.
class EscCancelReprocessTest : public ::testing::Test {
protected:
    static FilterDef dummyDef() { FilterDef d; d.pattern = "x"; return d; }

    void SetUp() override {
        // filteredLineCount / filterCount: 0 before append, 1 after
        EXPECT_CALL(chain_, filteredLineCount()).WillRepeatedly(Return(size_t{0}));
        EXPECT_CALL(chain_, filterCount())
            .WillRepeatedly(Return(size_t{0}));           // start: no filters
        EXPECT_CALL(chain_, filteredLineCountAt(_))
            .WillRepeatedly(Return(size_t{0}));

        // append() is called once; code ignores the return value
        EXPECT_CALL(chain_, append(_)).Times(1);

        // reprocess() deliberately does NOT call onDone → showProgress_ stays true
        EXPECT_CALL(chain_, reprocess(_, _, _)).Times(1);

        // ESC must trigger exactly one cancelReprocess() call
        EXPECT_CALL(chain_, cancelReprocess()).Times(1);

        ctrl_.getViewData(5, 5);  // initialise stored pane heights
    }

    MockFilterChain chain_;
    LogReader       reader_;
    AppController   ctrl_{reader_, chain_};

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }
    void type(const std::string& s) {
        for (char c : s)
            key(ftxui::Event::Character(std::string(1, c)));
    }
};

TEST_F(EscCancelReprocessTest, EscDuringProgressCancelsReprocess) {
    // Trigger reprocess: 'a' → type pattern → Enter
    key(ftxui::Event::Character('a'));
    type("pattern");
    key(ftxui::Event::Return);

    // Reprocess is now "in progress" (mock never called onDone)
    EXPECT_TRUE(ctrl_.getViewData(5, 5).showProgress);

    // ESC must cancel and clear the progress flag
    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.getViewData(5, 5).showProgress);
    // cancelReprocess() expectation is verified by GoogleMock on TearDown
}

// ── Dynamic bottom row: input/search active shrinks available pane height ─────

TEST_F(NavigationTest, DynamicBottomRow_InputModeShrinksPaneHeight) {
    // Without input: available = 26 - 6 = 20, raw = int(20 * 0.6) = 12
    ctrl_.onTerminalResize(80, 26);
    EXPECT_EQ(ctrl_.rawPaneHeight(), 12);

    // Enter FilterAdd (input mode active): extra = 1, available = 19
    // raw = int(19 * 0.6) = 11
    key(ftxui::Event::Character('a'));
    EXPECT_EQ(ctrl_.rawPaneHeight(), 11);

    // Exit input mode: heights restored to no-extra layout
    key(ftxui::Event::Escape);
    EXPECT_EQ(ctrl_.rawPaneHeight(), 12);
}

TEST_F(NavigationTest, DynamicBottomRow_SearchKeywordShrinksPaneHeight) {
    ctrl_.onTerminalResize(80, 26);
    EXPECT_EQ(ctrl_.rawPaneHeight(), 12);

    // Commit a search: searchKeyword_ becomes non-empty → active row appears
    key(ftxui::Event::Character('/'));
    for (char c : std::string("line1")) key(ftxui::Event::Character(c));
    key(ftxui::Event::Return);
    EXPECT_EQ(ctrl_.rawPaneHeight(), 11);

    // ESC in None mode clears search keyword → heights restored
    key(ftxui::Event::Escape);
    EXPECT_EQ(ctrl_.rawPaneHeight(), 12);
}

// ── clickLine: moves cursor in the pane ──────────────────────────────────────

TEST_F(NavigationTest, ClickLineMovesRawCursor) {
    // Raw pane starts at cursor=0 (rawLine 1), scrollOffset=0.
    // Clicking row 3 (0-based within pane) → cursor = scrollOffset + 3 = 3 → rawLine 4.
    ctrl_.clickLine(FocusArea::Raw, 3);
    EXPECT_EQ(highlightedLine(), 4u);
}

TEST_F(NavigationTest, ClickLineClampedToLastLine) {
    // Requesting a row beyond the last line must clamp to the last line.
    ctrl_.clickLine(FocusArea::Raw, 999);
    EXPECT_EQ(highlightedLine(), 20u);
}

// ── 'x' key in filtered pane jumps raw pane cursor to the raw line ───────────

TEST_F(NavigationTest, XKeyInFilteredPaneJumpsToRawLine) {
    // Add filter "line5": matches rawLine 5 ("line5") and rawLine 15 ("line15").
    key(ftxui::Event::Character('a'));
    for (char c : std::string("line5")) key(ftxui::Event::Character(c));
    key(ftxui::Event::Return);
    // Wait for the background reprocess thread to finish before asserting.
    chain_.waitReprocess();

    // Switch focus to filtered pane; cursor=0 → filteredLineAt(0) = rawLine 5.
    key(ftxui::Event::Tab);

    // 'x' must jump the raw pane cursor to that raw line without changing focus.
    key(ftxui::Event::Character('x'));

    // highlightedLine() reads rawPane — cursor should now be at rawLine 5.
    EXPECT_EQ(highlightedLine(), 5u);
}

TEST_F(NavigationTest, XKeyInRawPaneIsNoOp) {
    // 'x' while focused on the raw pane must not crash or change state.
    // Focus starts on Raw by default.
    const size_t lineBefore = highlightedLine();
    key(ftxui::Event::Character('x'));
    EXPECT_EQ(highlightedLine(), lineBefore);
}

// ── ESC clears selection (higher priority than clearing search) ───────────────

TEST_F(NavigationTest, EscClearsActiveSelection) {
    // Start and commit a selection.
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(1, 3);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());

    // ESC must clear the selection.
    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.hasSelection());
}

TEST_F(NavigationTest, EscPrioritySelectionBeforeSearch) {
    // Set up a search keyword.
    key(ftxui::Event::Character('/'));
    type("line1");
    key(ftxui::Event::Return);
    EXPECT_FALSE(ctrl_.getViewData(5, 5).searchKeyword.empty());

    // Also establish a selection.
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(1, 3);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());

    // First ESC: clears selection (higher priority than search).
    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.hasSelection());
    EXPECT_FALSE(ctrl_.getViewData(5, 5).searchKeyword.empty()); // search still active

    // Second ESC: clears search.
    key(ftxui::Event::Escape);
    EXPECT_TRUE(ctrl_.getViewData(5, 5).searchKeyword.empty());
}

// ── Jump centering ────────────────────────────────────────────────────────────

TEST_F(NavigationTest, JumpToRawLineCentersViewport) {
    // 20-line file, pane height 5 (set by SetUp via getViewData(5, 5)).
    // Jump to line 15: idx=14, half=5/2=2, scrollOffset=14-2=12.
    // Viewport shows rawLines 13..17; first visible line = 13.
    key(ftxui::Event::Character('g'));
    type("15");
    key(ftxui::Event::Return);
    EXPECT_EQ(highlightedLine(), 15u);
    auto d = ctrl_.getViewData(5, 5);
    ASSERT_FALSE(d.rawPane.empty());
    EXPECT_EQ(d.rawPane.front().rawLineNo, 13u);
}
