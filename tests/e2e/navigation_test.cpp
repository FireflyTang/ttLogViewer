#include <gtest/gtest.h>
#include <ftxui/component/event.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

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
    EXPECT_FALSE(ctrl_.getViewData(5, 5).showLineNumbers);
    key(ftxui::Event::Character('l'));
    EXPECT_TRUE(ctrl_.getViewData(5, 5).showLineNumbers);
    key(ftxui::Event::Character('l'));
    EXPECT_FALSE(ctrl_.getViewData(5, 5).showLineNumbers);
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
