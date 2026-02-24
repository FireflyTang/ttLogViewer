#include <gtest/gtest.h>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "render.hpp"
#include "temp_file.hpp"

static std::string renderCtrl(AppController& ctrl,
                               ftxui::ScreenInteractive& screen,
                               int w = 80, int h = 20)
{
    auto comp = CreateMainComponent(ctrl, screen);
    ctrl.onTerminalResize(w, h);  // Override height so layout fits the test screen
    auto scr  = ftxui::Screen::Create(ftxui::Dimension::Fixed(w),
                                       ftxui::Dimension::Fixed(h));
    ftxui::Render(scr, comp->Render());
    return scr.ToString();
}

class FilterBarTest : public ::testing::Test {
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

    std::string render() { return renderCtrl(ctrl_, screen_); }
};

TEST_F(FilterBarTest, NoFiltersShowsPlaceholder) {
    std::string out = render();
    EXPECT_NE(out.find("无过滤器"), std::string::npos);
}

TEST_F(FilterBarTest, FilterPatternAppearsInBar) {
    chain_.append({.pattern = "ERROR"});
    std::string out = render();
    EXPECT_NE(out.find("ERROR"), std::string::npos);
}

TEST_F(FilterBarTest, MultipleFiltersShowNumbers) {
    chain_.append({.pattern = "A"});
    chain_.append({.pattern = "B"});
    std::string out = render();
    EXPECT_NE(out.find("1"), std::string::npos);
    EXPECT_NE(out.find("2"), std::string::npos);
}
