#include <gtest/gtest.h>
#include <ftxui/component/event.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

// ── Fixture ──────────────────────────────────────────────────────────────────

class InputCursorTest : public ::testing::Test {
protected:
    void SetUp() override {
        file_ = std::make_unique<TempFile>("line1\n");
        reader_.open(file_->path());
        waitForIndexing(reader_);
        ctrl_.getViewData(5, 5);
    }

    void enterSearch(const std::string& text) {
        ctrl_.handleKey(ftxui::Event::Character('/'));
        for (char c : text)
            ctrl_.handleKey(ftxui::Event::Character(std::string(1, c)));
    }

    void enterOpenFile(const std::string& text) {
        ctrl_.handleKey(ftxui::Event::Character('o'));
        for (char c : text)
            ctrl_.handleKey(ftxui::Event::Character(std::string(1, c)));
    }

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }
    ViewData data()          { return ctrl_.getViewData(5, 5); }

    std::unique_ptr<TempFile> file_;
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};
};

// ── #26: Arrow keys move inputCursorPos ─────────────────────────────────────

TEST_F(InputCursorTest, ArrowLeftMovesCursorBack) {
    enterSearch("abc");
    auto d1 = data();
    EXPECT_EQ(d1.inputCursorPos, 3u);  // at end

    key(ftxui::Event::ArrowLeft);
    EXPECT_EQ(data().inputCursorPos, 2u);

    key(ftxui::Event::ArrowLeft);
    EXPECT_EQ(data().inputCursorPos, 1u);
}

TEST_F(InputCursorTest, ArrowRightMovesCursorForward) {
    enterSearch("abc");
    key(ftxui::Event::ArrowLeft);
    key(ftxui::Event::ArrowLeft);
    EXPECT_EQ(data().inputCursorPos, 1u);

    key(ftxui::Event::ArrowRight);
    EXPECT_EQ(data().inputCursorPos, 2u);
}

TEST_F(InputCursorTest, HomeMovesCursorToStart) {
    enterSearch("abc");
    key(ftxui::Event::Home);
    EXPECT_EQ(data().inputCursorPos, 0u);
}

TEST_F(InputCursorTest, EndMovesCursorToEnd) {
    enterSearch("abc");
    key(ftxui::Event::Home);
    EXPECT_EQ(data().inputCursorPos, 0u);

    key(ftxui::Event::End);
    EXPECT_EQ(data().inputCursorPos, 3u);
}

TEST_F(InputCursorTest, ArrowLeftClampsAtZero) {
    enterSearch("a");
    key(ftxui::Event::ArrowLeft);
    EXPECT_EQ(data().inputCursorPos, 0u);
    key(ftxui::Event::ArrowLeft);  // should not underflow
    EXPECT_EQ(data().inputCursorPos, 0u);
}

TEST_F(InputCursorTest, ArrowRightClampsAtEnd) {
    enterSearch("ab");
    // cursor already at end (2)
    key(ftxui::Event::ArrowRight);  // should stay
    EXPECT_EQ(data().inputCursorPos, 2u);
}

TEST_F(InputCursorTest, InsertAtCursorPosition) {
    enterSearch("ac");
    key(ftxui::Event::ArrowLeft);  // cursor at 1
    key(ftxui::Event::Character("b"));
    auto d = data();
    EXPECT_EQ(d.inputBuffer, "abc");
    EXPECT_EQ(d.inputCursorPos, 2u);  // after 'b'
}

TEST_F(InputCursorTest, BackspaceAtCursorPosition) {
    enterSearch("abc");
    key(ftxui::Event::ArrowLeft);  // cursor at 2
    key(ftxui::Event::Backspace);  // deletes 'b'
    auto d = data();
    EXPECT_EQ(d.inputBuffer, "ac");
    EXPECT_EQ(d.inputCursorPos, 1u);
}
