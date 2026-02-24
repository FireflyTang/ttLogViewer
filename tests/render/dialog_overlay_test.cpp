#include <gtest/gtest.h>
#include "render_test_base.hpp"

class DialogOverlayTest : public RenderTestBase {};

TEST_F(DialogOverlayTest, NoDialogByDefault) {
    EXPECT_FALSE(ctrl_.getViewData(3, 3).showDialog);
}

TEST_F(DialogOverlayTest, InvalidRegexShowsDialog) {
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
    EXPECT_NE(renderCtrl().find("无效正则"), std::string::npos);
}

TEST_F(DialogOverlayTest, ChoiceDialogShowsYN) {
    ctrl_.handleFileReset();
    auto d = ctrl_.getViewData(3, 3);
    EXPECT_TRUE(d.showDialog);
    EXPECT_TRUE(d.dialogHasChoice);

    std::string out = renderCtrl();
    EXPECT_NE(out.find("Y"), std::string::npos);
    EXPECT_NE(out.find("N"), std::string::npos);
}

TEST_F(DialogOverlayTest, ChoiceDialogNClosesWithoutAction) {
    ctrl_.handleFileReset();  // sets up a Y/N dialog
    key(ftxui::Event::Character('N'));
    EXPECT_FALSE(ctrl_.getViewData(3, 3).showDialog);
}
