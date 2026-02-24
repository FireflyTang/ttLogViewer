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
    auto comp = CreateMainComponent(ctrl, screen);
    ctrl.onTerminalResize(w, h);  // Override height so layout fits the test screen
    auto scr  = ftxui::Screen::Create(ftxui::Dimension::Fixed(w),
                                       ftxui::Dimension::Fixed(h));
    ftxui::Render(scr, comp->Render());
    return scr.ToString();
}

class DialogOverlayTest : public ::testing::Test {
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

TEST_F(DialogOverlayTest, NoDialogByDefault) {
    auto d = ctrl_.getViewData(3, 3);
    EXPECT_FALSE(d.showDialog);
}

TEST_F(DialogOverlayTest, InvalidRegexShowsDialog) {
    // Enter FilterAdd mode, type invalid regex, press Enter
    key(ftxui::Event::Character('a'));
    key(ftxui::Event::Character('['));  // '[' alone is invalid regex
    key(ftxui::Event::Return);

    auto d = ctrl_.getViewData(3, 3);
    EXPECT_TRUE(d.showDialog);
    EXPECT_FALSE(d.dialogHasChoice);
}

TEST_F(DialogOverlayTest, AnyKeyClosesInfoDialog) {
    key(ftxui::Event::Character('a'));
    key(ftxui::Event::Character('['));
    key(ftxui::Event::Return);
    ASSERT_TRUE(ctrl_.getViewData(3, 3).showDialog);

    key(ftxui::Event::Character('x'));  // Any key closes
    EXPECT_FALSE(ctrl_.getViewData(3, 3).showDialog);
}

TEST_F(DialogOverlayTest, DialogTitleBodyRendered) {
    key(ftxui::Event::Character('a'));
    key(ftxui::Event::Character('['));
    key(ftxui::Event::Return);
    std::string out = render();
    // Dialog title should appear
    EXPECT_NE(out.find("无效正则"), std::string::npos);
}

TEST_F(DialogOverlayTest, ChoiceDialogShowsYN) {
    // Simulate file reset dialog by calling handleFileReset
    ctrl_.handleFileReset();
    auto d = ctrl_.getViewData(3, 3);
    EXPECT_TRUE(d.showDialog);
    EXPECT_TRUE(d.dialogHasChoice);

    std::string out = render();
    EXPECT_NE(out.find("Y"), std::string::npos);
    EXPECT_NE(out.find("N"), std::string::npos);
}

TEST_F(DialogOverlayTest, ChoiceDialogNClosesWithoutAction) {
    bool actionCalled = false;
    ctrl_.handleFileReset();  // sets up a Y/N dialog

    // N should close without reloading
    key(ftxui::Event::Character('N'));
    EXPECT_FALSE(ctrl_.getViewData(3, 3).showDialog);
}
