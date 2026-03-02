#include <gtest/gtest.h>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/color.hpp>

#include "render_test_base.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

// ── Search highlight render tests ────────────────────────────────────────────
// Verify that search results are visually distinct in the rendered output.

class SearchHighlightRenderTest : public RenderTestBase {
protected:
    ftxui::Screen renderScreen(int w = 80, int h = 20) {
        auto comp = CreateMainComponent(ctrl_, screen_);
        ctrl_.onTerminalResize(w, h);
        auto scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(w),
                                          ftxui::Dimension::Fixed(h));
        ftxui::Render(scr, comp->Render());
        return scr;
    }

    void doSearch(const std::string& term) {
        ctrl_.handleKey(ftxui::Event::Character('/'));
        for (char c : term)
            ctrl_.handleKey(ftxui::Event::Character(std::string(1, c)));
        ctrl_.handleKey(ftxui::Event::Return);
    }

    // Find the first pixel whose character starts with ch on a given row y.
    // Returns x or -1.
    int findCharOnRow(const ftxui::Screen& scr, int y, const std::string& ch) {
        for (int x = 0; x < scr.dimx(); ++x)
            if (scr.PixelAt(x, y).character == ch)
                return x;
        return -1;
    }
};

// Search active renders the search keyword in the bottom status area.
TEST_F(SearchHighlightRenderTest, SearchKeywordAppearsInStatusArea) {
    doSearch("line3");
    auto scr = renderScreen();
    std::string out = scr.ToString();
    EXPECT_NE(out.find("line3"), std::string::npos)
        << "Search keyword should appear in the rendered output";
}

// The highlighted (current result) row should have the ▶ marker.
TEST_F(SearchHighlightRenderTest, CurrentResultRowHasMarker) {
    doSearch("line3");
    auto scr = renderScreen();
    std::string out = scr.ToString();
    // ▶ marker should be present (cursor moves to match)
    EXPECT_NE(out.find("▶"), std::string::npos);
}

// The matched text should be rendered with inverted colors (search highlight).
// We check that the pixel at the search match position has inverted attribute
// by comparing its background to the default.
TEST_F(SearchHighlightRenderTest, MatchedTextHasInvertedDecoration) {
    doSearch("line2");

    auto scr = renderScreen();
    // Find "line2" in the rendered output. The "l" of "line2" should have
    // inverted decoration (non-default background).
    bool foundInverted = false;
    for (int y = 0; y < scr.dimy(); ++y) {
        for (int x = 0; x < scr.dimx() - 4; ++x) {
            // Check for "l" followed by "i", "n", "e", "2"
            if (scr.PixelAt(x, y).character == "l" &&
                scr.PixelAt(x+1, y).character == "i" &&
                scr.PixelAt(x+2, y).character == "n" &&
                scr.PixelAt(x+3, y).character == "e" &&
                scr.PixelAt(x+4, y).character == "2") {
                // Check if this "l" has inverted styling (non-default bg)
                const auto& px = scr.PixelAt(x, y);
                if (px.inverted) {
                    foundInverted = true;
                    break;
                }
            }
        }
        if (foundInverted) break;
    }
    EXPECT_TRUE(foundInverted)
        << "Matched text 'line2' should have inverted decoration";
}

// Search result count appears in rendered output.
TEST_F(SearchHighlightRenderTest, SearchResultCountDisplayed) {
    doSearch("line");
    auto scr = renderScreen();
    std::string out = scr.ToString();
    // Should show something like "1/5" (first result out of 5 lines)
    EXPECT_NE(out.find("/5"), std::string::npos)
        << "Search result count should be displayed";
}

// After pressing 'n' (next match), the result index changes in the display.
TEST_F(SearchHighlightRenderTest, NextMatchUpdatesResultIndex) {
    doSearch("line");
    ctrl_.handleKey(ftxui::Event::Character('n'));  // next match

    auto scr = renderScreen();
    std::string out = scr.ToString();
    // Should now show "2/5"
    EXPECT_NE(out.find("2/5"), std::string::npos)
        << "After pressing 'n', result index should update to 2/5";
}

// ── Search result scrolls into the viewport after 'n' ────────────────────────
// When the user presses 'n' to jump to the NEXT search result that is far
// off-screen, the raw pane must scroll so the new match and ▶ marker are
// visible in the rendered output.
//
// Test design: two matches far apart (line 10 and line 20).
// After the initial search, the viewport is at line 10 (first result).
// With a small screen (h=12), rawH ≈ 3 rows, so line 20 is off-screen.
// Pressing 'n' must scroll to line 20.

TEST(SearchHighlightRender, NextResultScrollsMatchIntoViewport) {
    // Build 20-line file with two search targets far apart.
    std::string content;
    for (int i = 1; i <= 20; ++i) {
        if      (i == 10) content += "target_alpha\n";  // first match
        else if (i == 20) content += "target_beta\n";   // second match (off-screen)
        else              content += "normalline" + std::to_string(i) + "\n";
    }

    TempFile f(content);
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);
    AppController ctrl(reader, chain);

    // Search for "target" — finds both matches; first result (line 10) becomes active
    // and the viewport immediately scrolls there via jumpToSearchResult(0).
    ctrl.handleKey(ftxui::Event::Character('/'));
    for (char c : std::string("target"))
        ctrl.handleKey(ftxui::Event::Character(std::string(1, c)));
    ctrl.handleKey(ftxui::Event::Return);

    // Small screen: h=12 → uiOverheadRows=6, extra=1 (search bar) → avail=5 → rawH=3.
    // Viewport shows 3 rows around line 10; line 20 is 10 rows below → off-screen.
    auto si   = ftxui::ScreenInteractive::TerminalOutput();
    auto comp = CreateMainComponent(ctrl, si);
    ctrl.onTerminalResize(80, 12);
    auto scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                      ftxui::Dimension::Fixed(12));
    ftxui::Render(scr, comp->Render());

    // After initial search: first result (line 10) is visible, second is not.
    // Note: FTXUI inserts ANSI escape codes between the highlighted "target" part
    // and the normal-style suffix.  We therefore search for the unique suffix that
    // renders without any mid-word style resets ("_alpha" / "_beta").
    EXPECT_NE(scr.ToString().find("_alpha"), std::string::npos)
        << "First search result (line 10, unique suffix '_alpha') should be visible";
    EXPECT_EQ(scr.ToString().find("_beta"), std::string::npos)
        << "Second search result (line 20, unique suffix '_beta') should be off-screen";

    // Press 'n' → viewport scrolls to the second result (line 20).
    ctrl.handleKey(ftxui::Event::Character('n'));
    ftxui::Render(scr, comp->Render());
    std::string out = scr.ToString();

    EXPECT_NE(out.find("_beta"), std::string::npos)
        << "After 'n', the second search result (line 20, '_beta') must scroll into viewport";
    EXPECT_NE(out.find("▶"), std::string::npos)
        << "The ▶ cursor marker must be visible at the second search result row";
}
