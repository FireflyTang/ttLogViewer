#include <gtest/gtest.h>
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
