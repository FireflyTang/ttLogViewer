#include <gtest/gtest.h>
#include <ftxui/screen/screen.hpp>

#include "render_test_base.hpp"

class FilterBarTest : public RenderTestBase {
protected:
    // Render to an in-memory screen and return the Screen object for pixel inspection.
    ftxui::Screen renderScreen(int w = 80, int h = 20) {
        auto comp = CreateMainComponent(ctrl_, screen_);
        ctrl_.onTerminalResize(w, h);
        auto scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(w),
                                          ftxui::Dimension::Fixed(h));
        ftxui::Render(scr, comp->Render());
        return scr;
    }

    // Find the (x,y) of the first pixel whose character matches the given string.
    // Returns {-1,-1} if not found.
    std::pair<int,int> findPixel(const ftxui::Screen& scr, const std::string& ch) {
        for (int y = 0; y < scr.dimy(); ++y)
            for (int x = 0; x < scr.dimx(); ++x)
                if (scr.PixelAt(x, y).character == ch)
                    return {x, y};
        return {-1, -1};
    }
};

TEST_F(FilterBarTest, NoFiltersShowsPlaceholder) {
    EXPECT_NE(renderCtrl().find("无过滤器"), std::string::npos);
}

TEST_F(FilterBarTest, FilterPatternAppearsInBar) {
    chain_.append({.pattern = "ERROR"});
    EXPECT_NE(renderCtrl().find("ERROR"), std::string::npos);
}

TEST_F(FilterBarTest, MultipleFiltersShowNumbers) {
    chain_.append({.pattern = "A"});
    chain_.append({.pattern = "B"});
    std::string out = renderCtrl();
    EXPECT_NE(out.find("1"), std::string::npos);
    EXPECT_NE(out.find("2"), std::string::npos);
}

// ── v0.9.4: color dot indicators ─────────────────────────────────────────────

TEST_F(FilterBarTest, EnabledFilterShowsFilledDot) {
    // An enabled filter must render ⬤ (U+2B24, UTF-8: E2 AC A4)
    chain_.append({.pattern = "ERROR", .color = "#FF5555", .enabled = true});
    EXPECT_NE(renderCtrl().find("\xe2\xac\xa4"), std::string::npos);
}

TEST_F(FilterBarTest, DisabledFilterShowsEmptyDot) {
    // A disabled filter must render ○ (U+25CB, UTF-8: E2 97 8B)
    chain_.append({.pattern = "ERROR", .color = "#FF5555", .enabled = false});
    EXPECT_NE(renderCtrl().find("\xe2\x97\x8b"), std::string::npos);
}

// ── #29: Selected filter dot preserves foreground color ─────────────────────

TEST_F(FilterBarTest, SelectedFilterDotPreservesForegroundColor) {
    // Add a filter and select it (first filter is auto-selected).
    chain_.append({.pattern = "ERROR", .color = "#FF5555", .enabled = true});

    auto scr = renderScreen();
    // Find the ⬤ character in the rendered screen.
    auto [x, y] = findPixel(scr, "⬤");
    ASSERT_GE(x, 0) << "⬤ not found in rendered output";

    const auto& pixel = scr.PixelAt(x, y);
    // The dot's foreground must be the filter color (Red-ish), NOT default/white.
    // #FF5555 → Color::RGB(255, 85, 85)
    EXPECT_EQ(pixel.foreground_color, ftxui::Color::RGB(255, 85, 85))
        << "Selected filter dot should keep the filter color as foreground";
    // The background should be GrayDark (selected state).
    EXPECT_EQ(pixel.background_color, ftxui::Color::GrayDark)
        << "Selected filter dot should have GrayDark background";
}

TEST_F(FilterBarTest, UnselectedFilterDotHasDefaultBackground) {
    // Add two filters; second one is NOT selected.
    chain_.append({.pattern = "A", .color = "#FF5555", .enabled = true});
    chain_.append({.pattern = "B", .color = "#55FF55", .enabled = true});

    auto scr = renderScreen();
    // Find all ⬤ characters.  The second one belongs to the unselected filter.
    int found = 0;
    int lastX = -1, lastY = -1;
    for (int y = 0; y < scr.dimy(); ++y)
        for (int x = 0; x < scr.dimx(); ++x)
            if (scr.PixelAt(x, y).character == "⬤") {
                lastX = x; lastY = y;
                ++found;
            }
    ASSERT_GE(found, 2) << "Expected at least 2 ⬤ dots";

    // The last dot (unselected) should have the second filter's color as foreground.
    const auto& pixel = scr.PixelAt(lastX, lastY);
    EXPECT_EQ(pixel.foreground_color, ftxui::Color::RGB(85, 255, 85));
    // Unselected: background should be default (not GrayDark).
    EXPECT_NE(pixel.background_color, ftxui::Color::GrayDark);
}
