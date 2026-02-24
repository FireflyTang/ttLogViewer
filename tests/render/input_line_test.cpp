#include <gtest/gtest.h>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "render.hpp"
#include "temp_file.hpp"

static std::string renderCtrl(AppController& ctrl,
                               ftxui::ScreenInteractive& screen,
                               int w = 80, int h = 20)
{
    // CreateMainComponent initializes heights from Terminal::Size().
    // Override with the test's fixed size AFTER construction so the renderer
    // uses our dimensions and the layout fits within the fixed-size screen.
    auto comp = CreateMainComponent(ctrl, screen);
    ctrl.onTerminalResize(w, h);
    auto scr  = ftxui::Screen::Create(ftxui::Dimension::Fixed(w),
                                       ftxui::Dimension::Fixed(h));
    ftxui::Render(scr, comp->Render());
    return scr.ToString();
}

class InputLineTest : public ::testing::Test {
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
    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }
};

TEST_F(InputLineTest, DefaultModeShowsHints) {
    std::string out = render();
    EXPECT_NE(out.find("↑↓"), std::string::npos);
}

TEST_F(InputLineTest, FilterAddShowsPrompt) {
    key(ftxui::Event::Character('a'));
    std::string out = render();
    EXPECT_NE(out.find("Pattern>"), std::string::npos);
}

TEST_F(InputLineTest, FilterAddTypingAppearsInBuffer) {
    key(ftxui::Event::Character('a'));
    key(ftxui::Event::Character('E'));
    key(ftxui::Event::Character('R'));
    key(ftxui::Event::Character('R'));
    std::string out = render();
    EXPECT_NE(out.find("ERR"), std::string::npos);
}

TEST_F(InputLineTest, SearchModeShowsPrompt) {
    key(ftxui::Event::Character('/'));
    std::string out = render();
    EXPECT_NE(out.find("Search>"), std::string::npos);
}

TEST_F(InputLineTest, GotoLineModeShowsPrompt) {
    key(ftxui::Event::Character('g'));
    std::string out = render();
    EXPECT_NE(out.find("Goto:"), std::string::npos);
}

TEST_F(InputLineTest, OpenFileModeShowsPrompt) {
    key(ftxui::Event::Character('o'));
    std::string out = render();
    EXPECT_NE(out.find("Open:"), std::string::npos);
}

TEST_F(InputLineTest, EscExitsFilterAddMode) {
    key(ftxui::Event::Character('a'));
    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.isInputActive());
}
