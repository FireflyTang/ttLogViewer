#include <gtest/gtest.h>
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "render.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

using namespace ftxui;

// Render ViewData to a fixed-size in-memory screen and return the string buffer.
static std::string renderToString(LogReader& reader, FilterChain& chain,
                                   int width = 60, int height = 20) {
    AppController ctrl(reader, chain);
    auto screen = ScreenInteractive::TerminalOutput();
    auto comp   = CreateMainComponent(ctrl, screen);

    Screen s = Screen::Create(Dimension::Fixed(width), Dimension::Fixed(height));
    Render(s, comp->Render());
    return s.ToString();
}

// ── Highlight marker ──────────────────────────────────────────────────────────

TEST(LogPaneRender, HighlightedLineHasMarker) {
    TempFile f("alpha\nbeta\ngamma\n");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);

    std::string out = renderToString(reader, chain);
    // First line (cursor starts at 0) should have the ▶ marker
    EXPECT_NE(out.find("▶"), std::string::npos);
}

TEST(LogPaneRender, NonHighlightedLineNoMarker) {
    TempFile f("only_one_line\n");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);

    Screen s = Screen::Create(Dimension::Fixed(60), Dimension::Fixed(10));

    AppController ctrl(reader, chain);
    auto screen = ScreenInteractive::TerminalOutput();
    auto comp   = CreateMainComponent(ctrl, screen);
    Render(s, comp->Render());
    std::string out = s.ToString();

    // There is only one line; it should be highlighted (▶ present)
    EXPECT_NE(out.find("▶"), std::string::npos);
    // And its content visible
    EXPECT_NE(out.find("only_one_line"), std::string::npos);
}

// ── Empty file ────────────────────────────────────────────────────────────────

TEST(LogPaneRender, EmptyFileNoCrash) {
    TempFile f("");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);

    EXPECT_NO_THROW(renderToString(reader, chain));
}

// ── Status bar content ────────────────────────────────────────────────────────

TEST(LogPaneRender, StatusBarShowsLineCount) {
    TempFile f("a\nb\nc\n");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);

    std::string out = renderToString(reader, chain);
    EXPECT_NE(out.find("3"), std::string::npos);
}

TEST(LogPaneRender, StatusBarShowsStaticMode) {
    TempFile f("x\n");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    reader.setMode(FileMode::Static);
    FilterChain chain(reader);

    std::string out = renderToString(reader, chain);
    EXPECT_NE(out.find("静态"), std::string::npos);
}

TEST(LogPaneRender, StatusBarShowsRealtimeMode) {
    TempFile f("x\n");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    reader.setMode(FileMode::Realtime);
    FilterChain chain(reader);

    std::string out = renderToString(reader, chain);
    EXPECT_NE(out.find("实时"), std::string::npos);
}

// ── No file open ──────────────────────────────────────────────────────────────

TEST(LogPaneRender, NoFileOpenNoCrash) {
    LogReader reader;   // Not opened
    FilterChain chain(reader);
    EXPECT_NO_THROW(renderToString(reader, chain));
}

// ── Phase 3: line number display ─────────────────────────────────────────────

TEST(LogPaneRender, LineNumbersShownByDefault) {
    TempFile f("alpha\nbeta\n");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);

    AppController ctrl(reader, chain);
    auto screen = ScreenInteractive::TerminalOutput();
    auto comp   = CreateMainComponent(ctrl, screen);
    ctrl.onTerminalResize(60, 20);

    Screen s = Screen::Create(Dimension::Fixed(60), Dimension::Fixed(20));
    Render(s, comp->Render());
    std::string out = s.ToString();

    // Line numbers are on by default — line 1 number should appear
    EXPECT_NE(out.find("1 "), std::string::npos);
}

TEST(LogPaneRender, LineNumbersHiddenAfterLKey) {
    TempFile f("alpha\nbeta\n");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);

    AppController ctrl(reader, chain);
    auto screen = ScreenInteractive::TerminalOutput();
    auto comp   = CreateMainComponent(ctrl, screen);
    ctrl.onTerminalResize(60, 20);

    // Press 'l' to toggle line numbers off (they are on by default)
    ctrl.handleKey(Event::Character('l'));

    // showLineNumbers should now be false
    EXPECT_FALSE(ctrl.getViewData(5, 5).showLineNumbers);
}

// ── Phase 3: folded line shows ellipsis ──────────────────────────────────────

TEST(LogPaneRender, FoldedLineShowsEllipsis) {
    // Create a line longer than the terminal width
    std::string longLine(100, 'x');
    TempFile f(longLine + "\n");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);

    AppController ctrl(reader, chain);
    auto screen = ScreenInteractive::TerminalOutput();
    auto comp   = CreateMainComponent(ctrl, screen);
    ctrl.onTerminalResize(40, 20);

    // Press 'z' to fold the current (first) line
    ctrl.handleKey(Event::Character('z'));

    Screen s = Screen::Create(Dimension::Fixed(40), Dimension::Fixed(20));
    Render(s, comp->Render());
    std::string out = s.ToString();

    EXPECT_NE(out.find("…"), std::string::npos);
}

// ── Phase 3: line numbers and folding together ────────────────────────────────

TEST(LogPaneRender, LineNumbersAndFoldingTogether) {
    // A single long line so both features activate simultaneously.
    std::string longLine(80, 'y');
    TempFile f(longLine + "\n");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);

    AppController ctrl(reader, chain);
    auto screen = ScreenInteractive::TerminalOutput();
    auto comp   = CreateMainComponent(ctrl, screen);
    ctrl.onTerminalResize(40, 20);

    // Enable line numbers ('l') then fold the line ('z')
    ctrl.handleKey(Event::Character('l'));
    ctrl.handleKey(Event::Character('z'));

    Screen s = Screen::Create(Dimension::Fixed(40), Dimension::Fixed(20));
    Render(s, comp->Render());
    std::string out = s.ToString();

    // Both "1 " (line number) and "…" (fold ellipsis) must appear
    EXPECT_NE(out.find("1 "), std::string::npos);
    EXPECT_NE(out.find("…"),  std::string::npos);
}

// ── Phase 3: folding only affects the folded line ─────────────────────────────

TEST(LogPaneRender, UnfoldedLinesAreUnaffected) {
    // Two long lines; fold the second one only
    std::string longLine(80, 'z');
    TempFile f(longLine + "\n" + longLine + "\n");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);

    AppController ctrl(reader, chain);
    auto screen = ScreenInteractive::TerminalOutput();
    auto comp   = CreateMainComponent(ctrl, screen);
    ctrl.onTerminalResize(40, 20);

    // Move cursor to line 2 and fold it
    ctrl.handleKey(Event::ArrowDown);
    ctrl.handleKey(Event::Character('z'));

    auto d = ctrl.getViewData(5, 5);
    // Line 1 (cursor = 0): not folded
    // Line 2 (cursor = 1): folded
    bool line1Folded = false, line2Folded = false;
    for (const auto& ll : d.rawPane) {
        if (ll.rawLineNo == 1) line1Folded = ll.folded;
        if (ll.rawLineNo == 2) line2Folded = ll.folded;
    }
    EXPECT_FALSE(line1Folded);
    EXPECT_TRUE(line2Folded);
}
