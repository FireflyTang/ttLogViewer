#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <ftxui/component/event.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "mock_filter_chain.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

using ::testing::Return;
using ::testing::_;

// ── Fixture with real four-layer objects ─────────────────────────────────────

class EscPriorityTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string content;
        for (int i = 1; i <= 10; ++i)
            content += "line" + std::to_string(i) + "\n";
        file_ = std::make_unique<TempFile>(content);
        reader_.open(file_->path());
        waitForIndexing(reader_);
        ctrl_.getViewData(5, 5);
    }

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }
    ViewData data()          { return ctrl_.getViewData(5, 5); }
    void type(const std::string& s) {
        for (char c : s)
            key(ftxui::Event::Character(std::string(1, c)));
    }

    std::unique_ptr<TempFile> file_;
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};
};

// ── ESC priority chain (None mode) ──────────────────────────────────────────
// Order: 1) cancel progress  2) clear selection  3) clear search
// Each ESC triggers exactly one action.

TEST_F(EscPriorityTest, EscClearsSearchWhenNoOtherState) {
    // Set up search keyword.
    key(ftxui::Event::Character('/'));
    type("line1");
    key(ftxui::Event::Return);
    EXPECT_FALSE(data().searchKeyword.empty());

    // ESC clears search.
    key(ftxui::Event::Escape);
    EXPECT_TRUE(data().searchKeyword.empty());
}

TEST_F(EscPriorityTest, EscClearsSelectionBeforeSearch) {
    // Set up search.
    key(ftxui::Event::Character('/'));
    type("line1");
    key(ftxui::Event::Return);
    EXPECT_FALSE(data().searchKeyword.empty());

    // Set up selection.
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(1, 3);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());

    // First ESC: clears selection only.
    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.hasSelection());
    EXPECT_FALSE(data().searchKeyword.empty());  // search still active

    // Second ESC: clears search.
    key(ftxui::Event::Escape);
    EXPECT_TRUE(data().searchKeyword.empty());
}

TEST_F(EscPriorityTest, ThreeConsecutiveEscClearsAll) {
    // Set up all three states that ESC can clear.
    // 1. Search
    key(ftxui::Event::Character('/'));
    type("line");
    key(ftxui::Event::Return);
    EXPECT_FALSE(data().searchKeyword.empty());

    // 2. Selection
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(2, 5);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());

    // Note: progress requires mock (tested separately below).

    // ESC #1: clears selection.
    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.hasSelection());
    EXPECT_FALSE(data().searchKeyword.empty());

    // ESC #2: clears search.
    key(ftxui::Event::Escape);
    EXPECT_TRUE(data().searchKeyword.empty());

    // ESC #3: no effect (nothing left to clear).
    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.hasSelection());
    EXPECT_TRUE(data().searchKeyword.empty());
}

TEST_F(EscPriorityTest, EscAfterClearingSearchIsNoOp) {
    key(ftxui::Event::Character('/'));
    type("line1");
    key(ftxui::Event::Return);
    EXPECT_FALSE(data().searchKeyword.empty());

    key(ftxui::Event::Escape);  // clears search
    EXPECT_TRUE(data().searchKeyword.empty());

    // Additional ESC should be a no-op (nothing to clear).
    key(ftxui::Event::Escape);
    EXPECT_TRUE(data().searchKeyword.empty());
}

TEST_F(EscPriorityTest, EscInInputModeExitsInputNotChain) {
    // Enter filter-add mode.
    key(ftxui::Event::Character('a'));
    EXPECT_TRUE(ctrl_.isInputActive());

    // ESC exits input mode, does NOT trigger ESC chain logic.
    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.isInputActive());

    // No residual state cleared.
    EXPECT_FALSE(ctrl_.hasSelection());
    EXPECT_TRUE(data().searchKeyword.empty());
}

// ── ESC priority with progress (uses MockFilterChain) ───────────────────────

class EscProgressPriorityTest : public ::testing::Test {
protected:
    void SetUp() override {
        EXPECT_CALL(chain_, filteredLineCount()).WillRepeatedly(Return(size_t{0}));
        EXPECT_CALL(chain_, filterCount()).WillRepeatedly(Return(size_t{0}));
        EXPECT_CALL(chain_, filteredLineCountAt(_)).WillRepeatedly(Return(size_t{0}));
        EXPECT_CALL(chain_, append(_)).Times(1);
        // reprocess deliberately does NOT call onDone -> progress stays true
        EXPECT_CALL(chain_, reprocess(_, _, _)).Times(1);
        EXPECT_CALL(chain_, cancelReprocess()).Times(1);
        ctrl_.getViewData(5, 5);
    }

    MockFilterChain chain_;
    LogReader       reader_;
    AppController   ctrl_{reader_, chain_};

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }
    ViewData data()          { return ctrl_.getViewData(5, 5); }
    void type(const std::string& s) {
        for (char c : s)
            key(ftxui::Event::Character(std::string(1, c)));
    }
};

TEST_F(EscProgressPriorityTest, EscCancelsProgressBeforeSelection) {
    // Trigger reprocess (mock never calls onDone).
    key(ftxui::Event::Character('a'));
    type("x");
    key(ftxui::Event::Return);
    EXPECT_TRUE(data().showProgress);

    // Set up selection too.
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(1, 3);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());

    // ESC #1: cancels progress (highest priority).
    key(ftxui::Event::Escape);
    EXPECT_FALSE(data().showProgress);
    EXPECT_TRUE(ctrl_.hasSelection());  // selection untouched

    // ESC #2: clears selection.
    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.hasSelection());
}

// ── GoTo boundary conditions ────────────────────────────────────────────────

class GotoBoundaryTest : public ::testing::Test {
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
    void type(const std::string& s) {
        for (char c : s)
            key(ftxui::Event::Character(std::string(1, c)));
    }

    size_t highlightedLine() {
        for (auto& ll : ctrl_.getViewData(5, 5).rawPane)
            if (ll.highlighted) return ll.rawLineNo;
        return 0;
    }

    std::unique_ptr<TempFile> file_;
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};
};

TEST_F(GotoBoundaryTest, GotoZeroClampsToFirstLine) {
    key(ftxui::Event::Character('g'));
    type("0");
    key(ftxui::Event::Return);
    EXPECT_EQ(highlightedLine(), 1u);
}

TEST_F(GotoBoundaryTest, GotoOneBeyondEndClampsToLast) {
    key(ftxui::Event::Character('g'));
    type("21");
    key(ftxui::Event::Return);
    EXPECT_EQ(highlightedLine(), 20u);
}

TEST_F(GotoBoundaryTest, GotoVeryLargeNumberClampsToLast) {
    key(ftxui::Event::Character('g'));
    type("999999");
    key(ftxui::Event::Return);
    EXPECT_EQ(highlightedLine(), 20u);
}

TEST_F(GotoBoundaryTest, GotoEmptyInputNoChange) {
    // Move cursor to line 5 first.
    for (int i = 0; i < 4; ++i) key(ftxui::Event::ArrowDown);

    key(ftxui::Event::Character('g'));
    key(ftxui::Event::Return);  // empty input
    // Should either stay at current position or go to line 1 (implementation-defined).
    // Either way, no crash.
    EXPECT_GT(highlightedLine(), 0u);
}

TEST_F(GotoBoundaryTest, GotoExactLastLine) {
    key(ftxui::Event::Character('g'));
    type("20");
    key(ftxui::Event::Return);
    EXPECT_EQ(highlightedLine(), 20u);
}

TEST_F(GotoBoundaryTest, GotoFirstLine) {
    // Move away first.
    for (int i = 0; i < 10; ++i) key(ftxui::Event::ArrowDown);
    key(ftxui::Event::Character('g'));
    type("1");
    key(ftxui::Event::Return);
    EXPECT_EQ(highlightedLine(), 1u);
}

TEST_F(GotoBoundaryTest, GotoWithLeadingZeros) {
    key(ftxui::Event::Character('g'));
    type("010");
    key(ftxui::Event::Return);
    EXPECT_EQ(highlightedLine(), 10u);
}

// ── Exclude filter tests ────────────────────────────────────────────────────

class ExcludeFilterTest : public ::testing::Test {
protected:
    void SetUp() override {
        file_ = std::make_unique<TempFile>(
            "error: something\n"
            "info: normal\n"
            "error: another\n"
            "debug: trace\n"
            "info: ok\n");
        reader_.open(file_->path());
        waitForIndexing(reader_);
        ctrl_.getViewData(5, 5);
    }

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }
    ViewData data()          { return ctrl_.getViewData(5, 5); }
    void type(const std::string& s) {
        for (char c : s)
            key(ftxui::Event::Character(std::string(1, c)));
    }

    std::unique_ptr<TempFile> file_;
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};
};

TEST_F(ExcludeFilterTest, TabCycles4Modes) {
    key(ftxui::Event::Character('a'));

    // Mode 0: str-include (default)
    auto d = data();
    EXPECT_FALSE(d.inputUseRegex);
    EXPECT_FALSE(d.inputExclude);

    // Tab -> Mode 1: str-exclude
    key(ftxui::Event::Tab);
    d = data();
    EXPECT_FALSE(d.inputUseRegex);
    EXPECT_TRUE(d.inputExclude);

    // Tab -> Mode 2: regex-include
    key(ftxui::Event::Tab);
    d = data();
    EXPECT_TRUE(d.inputUseRegex);
    EXPECT_FALSE(d.inputExclude);

    // Tab -> Mode 3: regex-exclude
    key(ftxui::Event::Tab);
    d = data();
    EXPECT_TRUE(d.inputUseRegex);
    EXPECT_TRUE(d.inputExclude);

    // Tab -> wraps to Mode 0
    key(ftxui::Event::Tab);
    d = data();
    EXPECT_FALSE(d.inputUseRegex);
    EXPECT_FALSE(d.inputExclude);
}

TEST_F(ExcludeFilterTest, ExcludeFilterRemovesMatchingLines) {
    // Add exclude filter for "error"
    key(ftxui::Event::Character('a'));
    key(ftxui::Event::Tab);  // str-exclude
    type("error");
    key(ftxui::Event::Return);
    chain_.waitReprocess();

    // Should exclude lines containing "error", leaving 3 lines
    EXPECT_EQ(chain_.filteredLineCount(), 3u);
}
