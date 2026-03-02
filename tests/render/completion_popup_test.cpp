#include <gtest/gtest.h>
#include <ftxui/screen/screen.hpp>

#include <filesystem>
#include <fstream>

#include "render_test_base.hpp"

namespace fs = std::filesystem;

// ── Fixture ──────────────────────────────────────────────────────────────────

class CompletionPopupRenderTest : public RenderTestBase {
protected:
    void SetUp() override {
        RenderTestBase::SetUp();

        // Create a temp directory with test files for completion.
        tmpDir_ = fs::temp_directory_path() / ("ttlv_popup_render_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(tmpDir_);

        for (const char* name : {"alpha.log", "bravo.log", "charlie.log"}) {
            std::ofstream f(tmpDir_ / name);
            f << "test\n";
        }
        // Also create a subdirectory to test "/" suffix
        fs::create_directories(tmpDir_ / "subdir");
    }

    void TearDown() override {
        fs::remove_all(tmpDir_);
    }

    // Enter OpenFile mode and type a path prefix.
    void openFileMode(const std::string& prefix) {
        ctrl_.handleKey(ftxui::Event::Character('o'));
        for (char c : prefix)
            ctrl_.handleKey(ftxui::Event::Character(std::string(1, c)));
    }

    // Render to a Screen for pixel inspection.
    ftxui::Screen renderScreen(int w = 100, int h = 20) {
        auto comp = CreateMainComponent(ctrl_, screen_);
        ctrl_.onTerminalResize(w, h);
        auto scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(w),
                                          ftxui::Dimension::Fixed(h));
        ftxui::Render(scr, comp->Render());
        return scr;
    }

    fs::path tmpDir_;
};

// ── Popup border alignment test ──────────────────────────────────────────────
// The corner characters '┐' and '┘' must appear at the same x-column.
// FTXUI may merge the popup's '│' with underlying separators (producing '├'),
// so we only check the corners which are unambiguous.
// This catches the textPad double-counting bug that made content rows wider
// than the top/bottom border.

TEST_F(CompletionPopupRenderTest, RightBorderIsAligned) {
    // Type a prefix that matches multiple files to trigger popup.
    const std::string prefix = (tmpDir_ / "").string();  // all files
    openFileMode(prefix);
    ctrl_.handleKey(ftxui::Event::Tab);

    auto d = ctrl_.getViewData(10, 10);
    ASSERT_TRUE(d.showCompletions) << "Popup should be visible";
    ASSERT_GE(d.completions.size(), 3u);

    auto scr = renderScreen();

    // Find '┐' (top-right corner) and '┘' (bottom-right corner).
    int topRightX = -1, bottomRightX = -1;

    for (int y = 0; y < scr.dimy(); ++y) {
        for (int x = 0; x < scr.dimx(); ++x) {
            const auto& ch = scr.PixelAt(x, y).character;
            if (ch == "┐") topRightX = x;
            else if (ch == "┘") bottomRightX = x;
        }
    }

    ASSERT_NE(topRightX, -1) << "Top-right corner not found in rendered output";
    ASSERT_NE(bottomRightX, -1) << "Bottom-right corner not found in rendered output";

    // Both corners must be at the same x-column.
    EXPECT_EQ(topRightX, bottomRightX)
        << "Top-right corner at x=" << topRightX
        << " but bottom-right corner at x=" << bottomRightX;
}

// ── Left border alignment ────────────────────────────────────────────────────

TEST_F(CompletionPopupRenderTest, LeftBorderIsAligned) {
    const std::string prefix = (tmpDir_ / "").string();
    openFileMode(prefix);
    ctrl_.handleKey(ftxui::Event::Tab);

    ASSERT_TRUE(ctrl_.getViewData(10, 10).showCompletions);

    auto scr = renderScreen();

    int topLeftX = -1, bottomLeftX = -1;

    for (int y = 0; y < scr.dimy(); ++y) {
        for (int x = 0; x < scr.dimx(); ++x) {
            const auto& ch = scr.PixelAt(x, y).character;
            if (ch == "┌") topLeftX = x;
            else if (ch == "└") bottomLeftX = x;
        }
    }

    ASSERT_NE(topLeftX, -1) << "Top-left corner not found";
    ASSERT_NE(bottomLeftX, -1) << "Bottom-left corner not found";

    EXPECT_EQ(topLeftX, bottomLeftX)
        << "Top-left corner at x=" << topLeftX
        << " but bottom-left corner at x=" << bottomLeftX;
}

// ── Selected item has inverted styling ───────────────────────────────────────

TEST_F(CompletionPopupRenderTest, SelectedItemHasInvertedStyling) {
    const std::string prefix = (tmpDir_ / "").string();
    openFileMode(prefix);
    ctrl_.handleKey(ftxui::Event::Tab);

    auto d = ctrl_.getViewData(10, 10);
    ASSERT_TRUE(d.showCompletions);
    const std::string firstItem = d.completions[0];

    auto scr = renderScreen();

    // The selected item should have ">" prefix and inverted styling.
    bool foundInvertedInPopup = false;
    for (int y = 0; y < scr.dimy(); ++y) {
        for (int x = 0; x < scr.dimx(); ++x) {
            if (scr.PixelAt(x, y).character == ">" && scr.PixelAt(x, y).inverted) {
                foundInvertedInPopup = true;
                break;
            }
        }
        if (foundInvertedInPopup) break;
    }
    EXPECT_TRUE(foundInvertedInPopup)
        << "Selected completion item should have inverted '>' marker";
}

// ── Popup shows file names from candidates ───────────────────────────────────

TEST_F(CompletionPopupRenderTest, PopupShowsFileNames) {
    const std::string prefix = (tmpDir_ / "").string();
    openFileMode(prefix);
    ctrl_.handleKey(ftxui::Event::Tab);

    auto scr = renderScreen();
    std::string out = scr.ToString();

    // At least one of our test files should appear
    bool found = out.find("alpha.log") != std::string::npos
              || out.find("bravo.log") != std::string::npos
              || out.find("charlie.log") != std::string::npos;
    EXPECT_TRUE(found) << "Popup should show file names from candidates";
}

// ── Overlay: popup must not shift log pane content ───────────────────────────
// The completion popup is rendered as a dbox overlay. Opening it should NOT
// change the row at which log content appears (as a push-down layout would).

TEST_F(CompletionPopupRenderTest, PopupIsOverlayNotPush) {
    const std::string prefix = (tmpDir_ / "").string();

    // Enter OpenFile mode and type prefix WITHOUT triggering popup yet.
    // (openFileMode types the prefix but does not press Tab)
    openFileMode(prefix);
    ASSERT_FALSE(ctrl_.getViewData(10, 10).showCompletions);

    auto scr_before = renderScreen(100, 20);

    // Find the row where "line1" appears (first log line from RenderTestBase).
    auto findRow = [](const ftxui::Screen& scr, const std::string& text) {
        for (int y = 0; y < scr.dimy(); ++y) {
            std::string row;
            for (int x = 0; x < scr.dimx(); ++x)
                row += scr.PixelAt(x, y).character;
            if (row.find(text) != std::string::npos)
                return y;
        }
        return -1;
    };

    int row_before = findRow(scr_before, "line1");
    ASSERT_GE(row_before, 0) << "line1 must be visible in input mode before popup";

    // Now trigger popup (Tab).
    ctrl_.handleKey(ftxui::Event::Tab);
    ASSERT_TRUE(ctrl_.getViewData(10, 10).showCompletions);

    auto scr_after = renderScreen(100, 20);
    int row_after = findRow(scr_after, "line1");
    ASSERT_GE(row_after, 0) << "line1 must still be visible with popup active";

    // Overlay: the log content row must not have shifted.
    EXPECT_EQ(row_after, row_before)
        << "Completion popup (overlay/dbox) must not shift log pane content: "
        << "line1 was on row " << row_before << " but moved to row " << row_after;
}

// ── Navigation: ArrowDown moves the highlight row downward ──────────────────
// When the user presses ArrowDown in the popup, the inverted ">" marker must
// appear one row lower than it was before (next completion item selected).

TEST_F(CompletionPopupRenderTest, NavigationMovesHighlightDownward) {
    const std::string prefix = (tmpDir_ / "").string();
    openFileMode(prefix);
    ctrl_.handleKey(ftxui::Event::Tab);

    auto d = ctrl_.getViewData(10, 10);
    ASSERT_TRUE(d.showCompletions);
    ASSERT_GE(d.completions.size(), 2u) << "Need at least 2 completions for navigation test";

    // Helper: find the y-row of the inverted ">" marker (selected item in popup).
    auto findInvertedRow = [](const ftxui::Screen& scr) {
        for (int y = 0; y < scr.dimy(); ++y)
            for (int x = 0; x < scr.dimx(); ++x)
                if (scr.PixelAt(x, y).character == ">" && scr.PixelAt(x, y).inverted)
                    return y;
        return -1;
    };

    auto scr_before = renderScreen(100, 20);
    int row_before = findInvertedRow(scr_before);
    ASSERT_GE(row_before, 0) << "Initial selected item must have inverted '>' marker";

    // Navigate down: second item becomes selected.
    ctrl_.handleKey(ftxui::Event::ArrowDown);

    auto scr_after = renderScreen(100, 20);
    int row_after = findInvertedRow(scr_after);
    ASSERT_GE(row_after, 0) << "After ArrowDown, an inverted '>' must still exist";

    // The highlight row must have moved DOWN (to the next item).
    EXPECT_GT(row_after, row_before)
        << "ArrowDown must move the highlighted item to a lower row: "
        << "was row " << row_before << ", now row " << row_after;
}

// ── Scroll indicator ▲ appears when scrolled ─────────────────────────────────

TEST_F(CompletionPopupRenderTest, ScrollIndicatorAppearsWhenNeeded) {
    // With 4 items (alpha, bravo, charlie, subdir/) and maxVisible=3,
    // scrolling down should show ▲ indicator.
    const std::string prefix = (tmpDir_ / "").string();
    openFileMode(prefix);
    ctrl_.handleKey(ftxui::Event::Tab);

    auto d = ctrl_.getViewData(10, 10);
    ASSERT_TRUE(d.showCompletions);

    if (d.completions.size() <= 3) {
        // Not enough items to scroll, skip
        GTEST_SKIP() << "Need >3 completions for scroll test";
    }

    // Move down past the visible window
    for (size_t i = 0; i < 3; ++i)
        ctrl_.handleKey(ftxui::Event::ArrowDown);

    auto scr = renderScreen();
    std::string out = scr.ToString();
    EXPECT_NE(out.find("▲"), std::string::npos)
        << "Scroll up indicator should appear when items are hidden above";
}
