#include <gtest/gtest.h>
#include <ftxui/component/event.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

using namespace ftxui;

// ── Fixture ────────────────────────────────────────────────────────────────────

class AppControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 10-line file
        std::string content;
        for (int i = 1; i <= 10; ++i)
            content += "line" + std::to_string(i) + "\n";
        file_ = std::make_unique<TempFile>(content);
        reader_.open(file_->path());
        waitForIndexing(reader_);
        // Initialize stored pane heights before any key presses
        ctrl_.getViewData(kPH, kPH);
    }

    TempFile* file() { return file_.get(); }

    std::unique_ptr<TempFile> file_;
    LogReader      reader_;
    FilterChain    chain_{reader_};
    AppController  ctrl_{reader_, chain_};

    static constexpr int kPH = 5;  // pane height used in tests

    ViewData data() { return ctrl_.getViewData(kPH, kPH); }
    bool key(Event e) { return ctrl_.handleKey(e); }
};

// ── ArrowDown ──────────────────────────────────────────────────────────────────

TEST_F(AppControllerTest, ArrowDownMovesCursor) {
    EXPECT_TRUE(key(Event::ArrowDown));
    auto d = data();
    // cursor moves to index 1, so highlight is on the second row in the pane
    bool foundHighlight = false;
    for (auto& ll : d.rawPane)
        if (ll.highlighted) { foundHighlight = true; EXPECT_EQ(ll.rawLineNo, 2u); }
    EXPECT_TRUE(foundHighlight);
}

TEST_F(AppControllerTest, ArrowDownAtLastLine) {
    // Press Down 20 times; cursor must stay at last line (10)
    for (int i = 0; i < 20; ++i) key(Event::ArrowDown);
    auto d = data();
    size_t highlightedLine = 0;
    for (auto& ll : d.rawPane) if (ll.highlighted) highlightedLine = ll.rawLineNo;
    EXPECT_EQ(highlightedLine, 10u);
}

// ── ArrowUp ───────────────────────────────────────────────────────────────────

TEST_F(AppControllerTest, ArrowUpAtFirstLine) {
    key(Event::ArrowUp);  // Already at line 1; cursor must stay
    auto d = data();
    bool firstHighlighted = false;
    for (auto& ll : d.rawPane) if (ll.highlighted) firstHighlighted = (ll.rawLineNo == 1u);
    EXPECT_TRUE(firstHighlighted);
}

TEST_F(AppControllerTest, ArrowUpMovesCursor) {
    key(Event::ArrowDown);
    key(Event::ArrowDown);
    key(Event::ArrowUp);
    auto d = data();
    size_t hl = 0;
    for (auto& ll : d.rawPane) if (ll.highlighted) hl = ll.rawLineNo;
    EXPECT_EQ(hl, 2u);
}

// ── PageDown / PageUp ─────────────────────────────────────────────────────────

TEST_F(AppControllerTest, PageDownAdvances) {
    key(Event::PageDown);
    auto d = data();
    size_t hl = 0;
    for (auto& ll : d.rawPane) if (ll.highlighted) hl = ll.rawLineNo;
    // Should advance by paneHeight-1 = 4 rows → line 5
    EXPECT_EQ(hl, 5u);
}

TEST_F(AppControllerTest, PageDownClampsAtEnd) {
    for (int i = 0; i < 5; ++i) key(Event::PageDown);
    auto d = data();
    size_t hl = 0;
    for (auto& ll : d.rawPane) if (ll.highlighted) hl = ll.rawLineNo;
    EXPECT_EQ(hl, 10u);
}

TEST_F(AppControllerTest, PageUpAtBeginning) {
    key(Event::PageUp);
    auto d = data();
    size_t hl = 0;
    for (auto& ll : d.rawPane) if (ll.highlighted) hl = ll.rawLineNo;
    EXPECT_EQ(hl, 1u);
}

// ── Home / End ────────────────────────────────────────────────────────────────

TEST_F(AppControllerTest, HomeJumpsToFirst) {
    for (int i = 0; i < 5; ++i) key(Event::ArrowDown);
    key(Event::Home);
    auto d = data();
    size_t hl = 0;
    for (auto& ll : d.rawPane) if (ll.highlighted) hl = ll.rawLineNo;
    EXPECT_EQ(hl, 1u);
}

TEST_F(AppControllerTest, EndJumpsToLast) {
    key(Event::End);
    auto d = data();
    size_t hl = 0;
    for (auto& ll : d.rawPane) if (ll.highlighted) hl = ll.rawLineNo;
    EXPECT_EQ(hl, 10u);
}

// ── Focus switch ──────────────────────────────────────────────────────────────

TEST_F(AppControllerTest, TabSwitchesFocus) {
    auto d0 = data();
    EXPECT_TRUE(d0.rawFocused);
    EXPECT_FALSE(d0.filteredFocused);

    key(Event::Tab);
    auto d1 = data();
    EXPECT_FALSE(d1.rawFocused);
    EXPECT_TRUE(d1.filteredFocused);

    key(Event::Tab);
    auto d2 = data();
    EXPECT_TRUE(d2.rawFocused);
}

TEST_F(AppControllerTest, FocusSwitchDoesNotMoveCursor) {
    // Move raw cursor to row 3
    key(Event::ArrowDown);
    key(Event::ArrowDown);

    // Switch to filtered pane and move there
    key(Event::Tab);
    key(Event::ArrowDown);

    // Switch back; raw cursor should still be at row 3
    key(Event::Tab);
    auto d = data();
    size_t rawHL = 0;
    for (auto& ll : d.rawPane) if (ll.highlighted) rawHL = ll.rawLineNo;
    EXPECT_EQ(rawHL, 3u);
}

// ── getViewData pane height clipping ─────────────────────────────────────────

TEST_F(AppControllerTest, GetViewDataClipsToHeight) {
    auto d = ctrl_.getViewData(5, 5);
    EXPECT_LE(d.rawPane.size(), 5u);
    EXPECT_LE(d.filteredPane.size(), 5u);
}

TEST_F(AppControllerTest, GetViewDataHeightZero) {
    EXPECT_NO_THROW(ctrl_.getViewData(0, 0));
}

TEST_F(AppControllerTest, GetViewDataHeightLargerThanFile) {
    auto d = ctrl_.getViewData(100, 100);
    EXPECT_EQ(d.rawPane.size(), 10u);
}

// ── Empty file ────────────────────────────────────────────────────────────────

TEST_F(AppControllerTest, EmptyFileNoCrash) {
    TempFile emptyFile("");
    LogReader r2;
    r2.open(emptyFile.path());
    FilterChain c2(r2);
    AppController ctrl2(r2, c2);
    EXPECT_NO_THROW(ctrl2.getViewData(5, 5));
}

// ── isInputActive ─────────────────────────────────────────────────────────────

TEST_F(AppControllerTest, IsInputActiveDefaultFalse) {
    EXPECT_FALSE(ctrl_.isInputActive());
}

// ── Phase 3: getViewData both panes empty ─────────────────────────────────────

TEST(AppControllerBoundary, GetViewDataNoFileNoCrash) {
    LogReader   reader;  // Not opened
    FilterChain chain(reader);
    AppController ctrl(reader, chain);
    EXPECT_NO_THROW(ctrl.getViewData(5, 5));
}

TEST(AppControllerBoundary, GetViewDataBothPanesEmptyFields) {
    LogReader   reader;
    FilterChain chain(reader);
    AppController ctrl(reader, chain);
    auto d = ctrl.getViewData(5, 5);
    EXPECT_EQ(d.totalLines, 0u);
    EXPECT_TRUE(d.rawPane.empty());
    EXPECT_TRUE(d.filteredPane.empty());
}

// ── Phase 3: onTerminalResize clamps scroll ───────────────────────────────────

TEST_F(AppControllerTest, OnTerminalResizeCursorStaysVisible) {
    // Move cursor to end
    for (int i = 0; i < 20; ++i) key(Event::ArrowDown);

    // Simulate small terminal
    ctrl_.onTerminalResize(80, 8);

    auto d = ctrl_.getViewData(ctrl_.rawPaneHeight(), ctrl_.filtPaneHeight());
    // Cursor must be in the visible window
    bool found = false;
    for (auto& ll : d.rawPane) if (ll.highlighted) found = true;
    EXPECT_TRUE(found);
}

TEST_F(AppControllerTest, OnTerminalResizeStoresWidth) {
    ctrl_.onTerminalResize(120, 30);
    auto d = ctrl_.getViewData(5, 5);
    EXPECT_EQ(d.terminalWidth, 120);
}

// ── Phase 3: InputMode state no bleed ────────────────────────────────────────

TEST_F(AppControllerTest, InputModeBufferClearedOnExit) {
    key(Event::Character('a'));
    for (char c : std::string("hello"))
        key(Event::Character(std::string(1, c)));
    key(Event::Escape);

    auto d = data();
    EXPECT_EQ(d.inputMode, InputMode::None);
    EXPECT_TRUE(d.inputBuffer.empty());
}

TEST_F(AppControllerTest, MultipleInputModeSwitchesNoBleed) {
    // Enter filter add, type something, escape
    key(Event::Character('a'));
    key(Event::Character('x'));
    key(Event::Escape);

    // Enter goto line, type something, escape
    key(Event::Character('g'));
    key(Event::Character('5'));
    key(Event::Escape);

    // Enter open file, type something, escape
    key(Event::Character('o'));
    key(Event::Character('z'));
    key(Event::Escape);

    auto d = data();
    EXPECT_EQ(d.inputMode, InputMode::None);
    EXPECT_TRUE(d.inputBuffer.empty());
}

// ── Phase 3: l / z / h key handlers ──────────────────────────────────────────

TEST_F(AppControllerTest, LKeyTogglesLineNumbers) {
    // Line numbers are ON by default; 'l' toggles the state.
    EXPECT_TRUE(data().showLineNumbers);
    key(Event::Character('l'));
    EXPECT_FALSE(data().showLineNumbers);
    key(Event::Character('l'));
    EXPECT_TRUE(data().showLineNumbers);
}

TEST_F(AppControllerTest, ZKeyFoldsCurrentLine) {
    // Cursor is on line 1
    auto d0 = data();
    bool foldedBefore = false;
    for (auto& ll : d0.rawPane) if (ll.highlighted) foldedBefore = ll.folded;
    EXPECT_FALSE(foldedBefore);

    key(Event::Character('z'));
    auto d1 = data();
    bool foldedAfter = false;
    for (auto& ll : d1.rawPane) if (ll.highlighted) foldedAfter = ll.folded;
    EXPECT_TRUE(foldedAfter);

    // Second z unfolds
    key(Event::Character('z'));
    auto d2 = data();
    bool foldedAgain = false;
    for (auto& ll : d2.rawPane) if (ll.highlighted) foldedAgain = ll.folded;
    EXPECT_FALSE(foldedAgain);
}

TEST_F(AppControllerTest, ZKeyDoesNotAffectOtherLines) {
    // Move to line 3, fold it
    key(Event::ArrowDown);
    key(Event::ArrowDown);
    key(Event::Character('z'));

    // Move to line 1 – should NOT be folded
    key(Event::ArrowUp);
    key(Event::ArrowUp);
    auto d = data();
    for (auto& ll : d.rawPane)
        if (ll.rawLineNo == 1) { EXPECT_FALSE(ll.folded); }
}

TEST_F(AppControllerTest, HKeyShowsHelpDialog) {
    key(Event::Character('h'));
    auto d = data();
    EXPECT_TRUE(d.showDialog);
    EXPECT_FALSE(d.dialogHasChoice);
    // Dialog body should contain key descriptions
    EXPECT_NE(d.dialogBody.find("↑↓"), std::string::npos);
    EXPECT_NE(d.dialogBody.find("q"), std::string::npos);
}

TEST_F(AppControllerTest, HKeyDialogClosesOnAnyKey) {
    key(Event::Character('h'));
    EXPECT_TRUE(data().showDialog);
    key(Event::Character(' '));  // Any key closes the dialog
    EXPECT_FALSE(data().showDialog);
}
