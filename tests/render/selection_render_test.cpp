#include <gtest/gtest.h>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/color.hpp>

#include "render_test_base.hpp"

// ── Text selection render tests ──────────────────────────────────────────────
// Verify that text selection is visually rendered (blue background highlight).

class SelectionRenderTest : public RenderTestBase {
protected:
    ftxui::Screen renderScreen(int w = 80, int h = 20) {
        auto comp = CreateMainComponent(ctrl_, screen_);
        ctrl_.onTerminalResize(w, h);
        auto scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(w),
                                          ftxui::Dimension::Fixed(h));
        ftxui::Render(scr, comp->Render());
        return scr;
    }
};

// Selection active shows "[已选择]" hint in the bottom hints row.
TEST_F(SelectionRenderTest, SelectionShowsHintText) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 4);
    ctrl_.finalizeSelection();

    auto scr = renderScreen();
    std::string out = scr.ToString();
    EXPECT_NE(out.find("已选择"), std::string::npos)
        << "Selection active should show '已选择' in hints";
}

// Selection active shows copy/cancel hints.
TEST_F(SelectionRenderTest, SelectionShowsCopyHint) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 4);
    ctrl_.finalizeSelection();

    auto scr = renderScreen();
    std::string out = scr.ToString();
    EXPECT_NE(out.find("Ctrl+C"), std::string::npos)
        << "Selection hints should mention Ctrl+C for copy";
}

// Selected text should have blue background (bgcolor::Blue).
TEST_F(SelectionRenderTest, SelectedTextHasBlueBackground) {
    // Select "line1" on the first line (chars 0-4)
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 4);
    ctrl_.finalizeSelection();

    auto scr = renderScreen();

    // Find pixels with Blue background color — these are the selection highlight.
    bool foundBlue = false;
    for (int y = 0; y < scr.dimy(); ++y) {
        for (int x = 0; x < scr.dimx(); ++x) {
            const auto& px = scr.PixelAt(x, y);
            if (px.background_color == ftxui::Color::Blue) {
                foundBlue = true;
                break;
            }
        }
        if (foundBlue) break;
    }
    EXPECT_TRUE(foundBlue)
        << "Selected text should have Blue background color";
}

// Selected text has white foreground on blue background.
TEST_F(SelectionRenderTest, SelectedTextHasWhiteForeground) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 4);
    ctrl_.finalizeSelection();

    auto scr = renderScreen();

    bool foundWhiteOnBlue = false;
    for (int y = 0; y < scr.dimy(); ++y) {
        for (int x = 0; x < scr.dimx(); ++x) {
            const auto& px = scr.PixelAt(x, y);
            if (px.background_color == ftxui::Color::Blue &&
                px.foreground_color == ftxui::Color::White) {
                foundWhiteOnBlue = true;
                break;
            }
        }
        if (foundWhiteOnBlue) break;
    }
    EXPECT_TRUE(foundWhiteOnBlue)
        << "Selected text should have White foreground on Blue background";
}

// No selection → no blue background pixels in the log pane area.
TEST_F(SelectionRenderTest, NoSelectionNoBlueBackground) {
    ASSERT_FALSE(ctrl_.hasSelection());

    auto scr = renderScreen();

    bool foundBlue = false;
    for (int y = 0; y < scr.dimy(); ++y) {
        for (int x = 0; x < scr.dimx(); ++x) {
            const auto& px = scr.PixelAt(x, y);
            if (px.background_color == ftxui::Color::Blue &&
                !px.character.empty() && px.character != " ") {
                foundBlue = true;
                break;
            }
        }
        if (foundBlue) break;
    }
    EXPECT_FALSE(foundBlue)
        << "Without selection, no text should have Blue background";
}

// After clearing selection, blue background should disappear.
TEST_F(SelectionRenderTest, ClearSelectionRemovesBlueBackground) {
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 4);
    ctrl_.finalizeSelection();
    ASSERT_TRUE(ctrl_.hasSelection());

    // Clear selection via ESC
    ctrl_.handleKey(ftxui::Event::Escape);
    ASSERT_FALSE(ctrl_.hasSelection());

    auto scr = renderScreen();

    bool foundBlue = false;
    for (int y = 0; y < scr.dimy(); ++y) {
        for (int x = 0; x < scr.dimx(); ++x) {
            const auto& px = scr.PixelAt(x, y);
            if (px.background_color == ftxui::Color::Blue &&
                !px.character.empty() && px.character != " ") {
                foundBlue = true;
                break;
            }
        }
        if (foundBlue) break;
    }
    EXPECT_FALSE(foundBlue)
        << "After clearing selection, Blue background should disappear";
}

// ── #30: Drag past right edge triggers horizontal auto-scroll ────────────────
// When the user holds Left button and drags the mouse past the right edge of
// the terminal (x >= terminalWidth()), render.cpp must call scrollHorizontal()
// to advance the horizontal scroll offset of the focused pane.
//
// Layout reminder (from render.cpp):
//   kStatusBarRows = 2  →  rawTop = 2
//   with h=20, uiOverheadRows=6, rawPaneFraction=0.6:
//     avail = 14, rawH = 8  →  rawBot = 9
//   y=5 is safely inside the raw pane [2, 9].

TEST_F(SelectionRenderTest, DragPastRightEdgeScrollsHorizontally) {
    const int w = 80, h = 20;

    auto comp = CreateMainComponent(ctrl_, screen_);
    ctrl_.onTerminalResize(w, h);

    // Render once so the component establishes internal layout state.
    {
        auto scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(w),
                                          ftxui::Dimension::Fixed(h));
        ftxui::Render(scr, comp->Render());
    }

    ASSERT_EQ(ctrl_.getViewData(8, 6).rawHScroll, 0u);

    // Helper to construct a mouse event.
    auto mouseEvent = [](int x, int y, ftxui::Mouse::Button btn,
                         ftxui::Mouse::Motion mot) {
        ftxui::Mouse m;
        m.button  = btn;
        m.motion  = mot;
        m.x       = x;
        m.y       = y;
        m.shift   = m.meta = m.control = false;
        return ftxui::Event::Mouse("", m);
    };

    // Press inside the raw pane (y=5, which is in [rawTop=2, rawBot=9]).
    // This calls controller.startSelection() → sets selection_.dragging = true.
    comp->OnEvent(mouseEvent(10, 5, ftxui::Mouse::Left, ftxui::Mouse::Pressed));

    ASSERT_TRUE(ctrl_.isSelectionDragging())
        << "Mouse press in raw pane should start selection drag";

    // Drag past the right edge (x=85 > termW=80).
    // handleMouseEvent checks: m.x >= controller.terminalWidth() → scrollHorizontal(+step).
    comp->OnEvent(mouseEvent(85, 5, ftxui::Mouse::Left, ftxui::Mouse::Moved));

    EXPECT_GT(ctrl_.getViewData(8, 6).rawHScroll, 0u)
        << "Dragging past the right edge (x=85 > termW=80) should increase rawHScroll";
}
