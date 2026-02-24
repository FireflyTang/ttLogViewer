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
