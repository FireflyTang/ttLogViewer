#include <gtest/gtest.h>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "render.hpp"
#include "temp_file.hpp"

// Helpers
static std::string renderToString(AppController& ctrl,
                                   ftxui::ScreenInteractive& screen,
                                   int w = 80, int h = 20)
{
    auto comp   = CreateMainComponent(ctrl, screen);
    ctrl.onTerminalResize(w, h);  // Override height so layout fits the test screen
    auto fscr   = ftxui::Screen::Create(ftxui::Dimension::Fixed(w),
                                         ftxui::Dimension::Fixed(h));
    ftxui::Render(fscr, comp->Render());
    return fscr.ToString();
}

// ── StatusBar tests ────────────────────────────────────────────────────────────

class StatusBarTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string content;
        for (int i = 1; i <= 5; ++i)
            content += "line" + std::to_string(i) + "\n";
        file_ = std::make_unique<TempFile>(content);
        reader_.open(file_->path());
        ctrl_.getViewData(3, 3);
    }

    std::unique_ptr<TempFile> file_;
    LogReader   reader_;
    FilterChain chain_{reader_};
    AppController ctrl_{reader_, chain_};
    ftxui::ScreenInteractive screen_ = ftxui::ScreenInteractive::TerminalOutput();

    std::string render() { return renderToString(ctrl_, screen_); }
};

TEST_F(StatusBarTest, ShowsStaticMode) {
    reader_.setMode(FileMode::Static);
    std::string out = render();
    EXPECT_NE(out.find("静态"), std::string::npos);
}

TEST_F(StatusBarTest, ShowsRealtimeMode) {
    reader_.setMode(FileMode::Realtime);
    std::string out = render();
    EXPECT_NE(out.find("实时"), std::string::npos);
}

TEST_F(StatusBarTest, ShowsLineCount) {
    std::string out = render();
    EXPECT_NE(out.find("5"), std::string::npos);
}

TEST_F(StatusBarTest, ShowsFileName) {
    std::string out = render();
    // The path contains the filename fragment
    EXPECT_NE(out.find("ttlv_test_"), std::string::npos);
}
