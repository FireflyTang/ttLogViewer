#include <gtest/gtest.h>
#include <ftxui/screen/screen.hpp>

#include "render_test_base.hpp"

// ── Input cursor visualization render tests ──────────────────────────────────
// Verify that the block cursor is rendered at the correct position in input modes.

class InputCursorRenderTest : public RenderTestBase {
protected:
    ftxui::Screen renderScreen(int w = 80, int h = 20) {
        auto comp = CreateMainComponent(ctrl_, screen_);
        ctrl_.onTerminalResize(w, h);
        auto scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(w),
                                          ftxui::Dimension::Fixed(h));
        ftxui::Render(scr, comp->Render());
        return scr;
    }

    void enterSearch(const std::string& text) {
        ctrl_.handleKey(ftxui::Event::Character('/'));
        for (char c : text)
            ctrl_.handleKey(ftxui::Event::Character(std::string(1, c)));
    }

    void enterFilter(const std::string& text) {
        ctrl_.handleKey(ftxui::Event::Character('a'));
        for (char c : text)
            ctrl_.handleKey(ftxui::Event::Character(std::string(1, c)));
    }

    void enterOpenFile(const std::string& text) {
        ctrl_.handleKey(ftxui::Event::Character('o'));
        for (char c : text)
            ctrl_.handleKey(ftxui::Event::Character(std::string(1, c)));
    }

    void enterGoto(const std::string& text) {
        ctrl_.handleKey(ftxui::Event::Character('g'));
        for (char c : text)
            ctrl_.handleKey(ftxui::Event::Character(std::string(1, c)));
    }

    // Find an inverted pixel in a row range that matches the given character.
    // Returns {x, y} or {-1, -1}.
    std::pair<int,int> findInvertedChar(const ftxui::Screen& scr,
                                         int yMin, int yMax,
                                         const std::string& ch = "") {
        for (int y = yMin; y < yMax && y < scr.dimy(); ++y)
            for (int x = 0; x < scr.dimx(); ++x) {
                const auto& px = scr.PixelAt(x, y);
                if (px.inverted && (ch.empty() || px.character == ch))
                    return {x, y};
            }
        return {-1, -1};
    }
};

// ── Search mode cursor ──────────────────────────────────────────────────────

TEST_F(InputCursorRenderTest, SearchModeCursorAtEnd) {
    enterSearch("abc");
    auto scr = renderScreen();
    // Cursor at end of "abc" should render as inverted space (block cursor)
    // Look in the bottom area of the screen (input line)
    auto [x, y] = findInvertedChar(scr, scr.dimy() - 4, scr.dimy(), " ");
    EXPECT_NE(x, -1) << "Block cursor (inverted space) should be visible at end of input";
}

TEST_F(InputCursorRenderTest, SearchModeCursorInMiddle) {
    enterSearch("abc");
    ctrl_.handleKey(ftxui::Event::ArrowLeft);  // cursor on 'c'

    auto scr = renderScreen();
    // Cursor on 'c' should render as inverted 'c'
    auto [x, y] = findInvertedChar(scr, scr.dimy() - 4, scr.dimy(), "c");
    EXPECT_NE(x, -1) << "Block cursor should highlight 'c' after ArrowLeft";
}

TEST_F(InputCursorRenderTest, SearchModeCursorAtStart) {
    enterSearch("abc");
    ctrl_.handleKey(ftxui::Event::Home);  // cursor on 'a'

    auto scr = renderScreen();
    auto [x, y] = findInvertedChar(scr, scr.dimy() - 4, scr.dimy(), "a");
    EXPECT_NE(x, -1) << "Block cursor should highlight 'a' after Home";
}

// ── Filter mode cursor ──────────────────────────────────────────────────────

TEST_F(InputCursorRenderTest, FilterModeCursorVisible) {
    enterFilter("ERROR");
    auto scr = renderScreen();
    // Cursor at end: inverted space
    auto [x, y] = findInvertedChar(scr, scr.dimy() - 4, scr.dimy(), " ");
    EXPECT_NE(x, -1) << "Filter mode should show block cursor";
}

// ── OpenFile mode cursor ────────────────────────────────────────────────────

TEST_F(InputCursorRenderTest, OpenFileModeCursorVisible) {
    enterOpenFile("test");
    auto scr = renderScreen();
    auto [x, y] = findInvertedChar(scr, scr.dimy() - 4, scr.dimy(), " ");
    EXPECT_NE(x, -1) << "OpenFile mode should show block cursor";
}

// ── Goto mode cursor ────────────────────────────────────────────────────────

TEST_F(InputCursorRenderTest, GotoModeCursorVisible) {
    enterGoto("5");
    auto scr = renderScreen();
    // Cursor should be visible as any inverted character in the input area
    auto [x, y] = findInvertedChar(scr, scr.dimy() - 4, scr.dimy());
    EXPECT_NE(x, -1) << "Goto mode should show block cursor";
}

// ── Cursor moves visually when arrow keys are pressed ───────────────────────

TEST_F(InputCursorRenderTest, CursorMovesLeftVisually) {
    enterSearch("abc");
    auto scr1 = renderScreen();
    auto [x1, y1] = findInvertedChar(scr1, scr1.dimy() - 4, scr1.dimy());

    ctrl_.handleKey(ftxui::Event::ArrowLeft);
    auto scr2 = renderScreen();
    auto [x2, y2] = findInvertedChar(scr2, scr2.dimy() - 4, scr2.dimy());

    ASSERT_NE(x1, -1);
    ASSERT_NE(x2, -1);
    EXPECT_LT(x2, x1) << "Cursor should move left visually after ArrowLeft";
}

// ── Mode tag renders correctly ──────────────────────────────────────────────

TEST_F(InputCursorRenderTest, SearchModeTagShown) {
    enterSearch("test");
    auto scr = renderScreen();
    std::string out = scr.ToString();
    EXPECT_NE(out.find("字符串"), std::string::npos)
        << "Search mode should show [字符串] tag by default";
}

TEST_F(InputCursorRenderTest, FilterModeTagShown) {
    enterFilter("ERROR");
    auto scr = renderScreen();
    std::string out = scr.ToString();
    EXPECT_NE(out.find("字符串"), std::string::npos)
        << "Filter mode should show mode tag";
}

// ── Regex validity dot ──────────────────────────────────────────────────────

TEST_F(InputCursorRenderTest, ValidRegexShowsGreenDot) {
    enterSearch("line");
    // Switch to regex mode with Tab
    ctrl_.handleKey(ftxui::Event::Tab);

    auto scr = renderScreen();
    // Find a green pixel (the validity dot ●)
    bool foundGreen = false;
    for (int y = scr.dimy() - 4; y < scr.dimy(); ++y) {
        for (int x = 0; x < scr.dimx(); ++x) {
            const auto& px = scr.PixelAt(x, y);
            if (px.character == "●" &&
                px.foreground_color == ftxui::Color::Green) {
                foundGreen = true;
                break;
            }
        }
        if (foundGreen) break;
    }
    EXPECT_TRUE(foundGreen)
        << "Valid regex should show green ● indicator";
}

TEST_F(InputCursorRenderTest, InvalidRegexShowsRedDot) {
    enterSearch("[invalid");  // unclosed bracket = invalid regex
    // Switch to regex mode with Tab
    ctrl_.handleKey(ftxui::Event::Tab);

    auto scr = renderScreen();
    bool foundRed = false;
    for (int y = scr.dimy() - 4; y < scr.dimy(); ++y) {
        for (int x = 0; x < scr.dimx(); ++x) {
            const auto& px = scr.PixelAt(x, y);
            if (px.character == "●" &&
                px.foreground_color == ftxui::Color::Red) {
                foundRed = true;
                break;
            }
        }
        if (foundRed) break;
    }
    EXPECT_TRUE(foundRed)
        << "Invalid regex should show red ● indicator";
}
