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

// ── v0.9.4: search keyword display in None mode ──────────────────────────────

TEST_F(InputLineTest, SearchKeywordAppearsInNoneMode) {
    // After submitting a search, None mode shows the keyword and jump hint.
    // "n/N:跳转" only appears when a search keyword is active.
    key(ftxui::Event::Character('/'));
    for (char c : std::string("line1"))
        key(ftxui::Event::Character(c));
    key(ftxui::Event::Return);
    std::string out = renderCtrl();
    EXPECT_NE(out.find("line1"), std::string::npos);
    EXPECT_NE(out.find("n/N"), std::string::npos);
}

TEST_F(InputLineTest, SearchKeywordShowsResultCount) {
    // Fixture has 5 lines; "line1" matches exactly one → count "1/1"
    key(ftxui::Event::Character('/'));
    for (char c : std::string("line1"))
        key(ftxui::Event::Character(c));
    key(ftxui::Event::Return);
    EXPECT_NE(renderCtrl().find("1/1"), std::string::npos);
}

TEST_F(InputLineTest, SearchNoResultsShowsText) {
    // Searching for something that matches nothing must display "无结果"
    key(ftxui::Event::Character('/'));
    for (char c : std::string("XXXXNOTFOUND"))
        key(ftxui::Event::Character(c));
    key(ftxui::Event::Return);
    EXPECT_NE(renderCtrl().find("无结果"), std::string::npos);
}
