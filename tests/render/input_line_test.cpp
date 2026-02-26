#include <gtest/gtest.h>
#include "render_test_base.hpp"

class InputLineTest : public RenderTestBase {};

TEST_F(InputLineTest, DefaultModeShowsHints) {
    EXPECT_NE(renderCtrl().find("↑↓"), std::string::npos);
}

TEST_F(InputLineTest, FilterAddShowsPrompt) {
    key(ftxui::Event::Character('a'));
    std::string out = renderCtrl();
    EXPECT_NE(out.find("Pattern>"), std::string::npos);
    // Filter input now shows [字符串] mode tag by default
    EXPECT_NE(out.find("\xe5\xad\x97\xe7\xac\xa6\xe4\xb8\xb2"), std::string::npos)
        << "Expected '字符串' mode tag in filter add input line";
}

TEST_F(InputLineTest, FilterAddTabShowsRegexTag) {
    key(ftxui::Event::Character('a'));
    key(ftxui::Event::Tab);  // toggle to regex mode
    std::string out = renderCtrl();
    EXPECT_NE(out.find("\xe6\xad\xa3\xe5\x88\x99"), std::string::npos)
        << "Expected '正则' mode tag after Tab in filter add";
}

TEST_F(InputLineTest, FilterAddTypingAppearsInBuffer) {
    key(ftxui::Event::Character('a'));
    key(ftxui::Event::Character('E'));
    key(ftxui::Event::Character('R'));
    key(ftxui::Event::Character('R'));
    EXPECT_NE(renderCtrl().find("ERR"), std::string::npos);
}

TEST_F(InputLineTest, SearchModeShowsModeIndicator) {
    // Search input now shows [字符串] or [正则] instead of "Search>"
    key(ftxui::Event::Character('/'));
    std::string out = renderCtrl();
    // Default literal mode indicator must appear
    EXPECT_NE(out.find("\xe5\xad\x97\xe7\xac\xa6\xe4\xb8\xb2"), std::string::npos)
        << "Expected '字符串' in search input line";
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
    // "n/p:跳转" only appears when a search keyword is active.
    // "n/N" must NOT appear (it was the old — incorrect — hint).
    key(ftxui::Event::Character('/'));
    for (char c : std::string("line1"))
        key(ftxui::Event::Character(c));
    key(ftxui::Event::Return);
    std::string out = renderCtrl();
    EXPECT_NE(out.find("line1"), std::string::npos);
    EXPECT_NE(out.find("n/p"),   std::string::npos);
    EXPECT_EQ(out.find("n/N"),   std::string::npos);  // old incorrect hint must be gone
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
