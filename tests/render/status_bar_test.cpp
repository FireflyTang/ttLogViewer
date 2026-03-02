#include <gtest/gtest.h>
#include <string>

#include <ftxui/screen/screen.hpp>

#include "render_test_base.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

class StatusBarTest : public RenderTestBase {};

TEST_F(StatusBarTest, ShowsStaticMode) {
    reader_.setMode(FileMode::Static);
    EXPECT_NE(renderCtrl().find("静态"), std::string::npos);
}

TEST_F(StatusBarTest, ShowsRealtimeMode) {
    reader_.setMode(FileMode::Realtime);
    EXPECT_NE(renderCtrl().find("实时"), std::string::npos);
}

TEST_F(StatusBarTest, ShowsLineCount) {
    EXPECT_NE(renderCtrl().find("5"), std::string::npos);
}

TEST_F(StatusBarTest, ShowsFileName) {
    // The temp file path contains "ttlv_test_" as a prefix
    EXPECT_NE(renderCtrl().find("ttlv_test_"), std::string::npos);
}

// ── Phase 3: thousands separator ─────────────────────────────────────────────

TEST(StatusBarThousands, LargeLineCountFormatted) {
    // Create a 1001-line file and verify "1,001" appears in the status bar
    std::string content;
    content.reserve(1001 * 4);
    for (int i = 0; i < 1001; ++i)
        content += "x\n";

    TempFile f(content);
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);
    AppController ctrl(reader, chain);
    auto scr = ftxui::ScreenInteractive::TerminalOutput();
    auto comp = CreateMainComponent(ctrl, scr);
    ctrl.onTerminalResize(80, 20);

    auto s = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                    ftxui::Dimension::Fixed(20));
    ftxui::Render(s, comp->Render());
    std::string out = s.ToString();

    // 1001 lines should be formatted as "1,001"
    EXPECT_NE(out.find("1,001"), std::string::npos);
}

// ── Status bar background color ─────────────────────────────────────────────

TEST_F(StatusBarTest, StatusBarHasGrayDarkBackground) {
    auto comp = CreateMainComponent(ctrl_, screen_);
    ctrl_.onTerminalResize(80, 20);
    auto scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                      ftxui::Dimension::Fixed(20));
    ftxui::Render(scr, comp->Render());

    // The status bar is the first rendered row. Check for GrayDark bg.
    bool hasGrayDark = false;
    for (int x = 0; x < 80; ++x) {
        if (scr.PixelAt(x, 0).background_color == ftxui::Color::GrayDark) {
            hasGrayDark = true;
            break;
        }
    }
    EXPECT_TRUE(hasGrayDark)
        << "Status bar should have GrayDark background color";
}

// ── Status bar shows separator ──────────────────────────────────────────────

TEST_F(StatusBarTest, StatusBarShowsSeparator) {
    std::string out = renderCtrl();
    EXPECT_NE(out.find("│"), std::string::npos)
        << "Status bar should show │ separator between fields";
}

// ── No file open shows placeholder ──────────────────────────────────────────

TEST(StatusBarNoFile, ShowsPlaceholder) {
    LogReader reader;
    FilterChain chain(reader);
    AppController ctrl(reader, chain);
    auto scr = ftxui::ScreenInteractive::TerminalOutput();
    auto comp = CreateMainComponent(ctrl, scr);
    ctrl.onTerminalResize(80, 20);

    auto s = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                    ftxui::Dimension::Fixed(20));
    ftxui::Render(s, comp->Render());
    std::string out = s.ToString();
    EXPECT_NE(out.find("未打开文件"), std::string::npos);
}
