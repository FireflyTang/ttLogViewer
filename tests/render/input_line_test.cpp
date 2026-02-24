#include <gtest/gtest.h>
#include "render_test_base.hpp"

class InputLineTest : public RenderTestBase {};

TEST_F(InputLineTest, DefaultModeShowsHints) {
    EXPECT_NE(renderCtrl().find("↑↓"), std::string::npos);
}

TEST_F(InputLineTest, FilterAddShowsPrompt) {
    key(ftxui::Event::Character('a'));
    EXPECT_NE(renderCtrl().find("Pattern>"), std::string::npos);
}

TEST_F(InputLineTest, FilterAddTypingAppearsInBuffer) {
    key(ftxui::Event::Character('a'));
    key(ftxui::Event::Character('E'));
    key(ftxui::Event::Character('R'));
    key(ftxui::Event::Character('R'));
    EXPECT_NE(renderCtrl().find("ERR"), std::string::npos);
}

TEST_F(InputLineTest, SearchModeShowsPrompt) {
    key(ftxui::Event::Character('/'));
    EXPECT_NE(renderCtrl().find("Search>"), std::string::npos);
}

TEST_F(InputLineTest, GotoLineModeShowsPrompt) {
    key(ftxui::Event::Character('g'));
    EXPECT_NE(renderCtrl().find("Goto:"), std::string::npos);
}

TEST_F(InputLineTest, OpenFileModeShowsPrompt) {
    key(ftxui::Event::Character('o'));
    EXPECT_NE(renderCtrl().find("Open:"), std::string::npos);
}

TEST_F(InputLineTest, EscExitsFilterAddMode) {
    key(ftxui::Event::Character('a'));
    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.isInputActive());
}
