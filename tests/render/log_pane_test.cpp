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
