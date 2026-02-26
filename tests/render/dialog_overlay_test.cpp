#include <gtest/gtest.h>
#include "render_test_base.hpp"

class DialogOverlayTest : public RenderTestBase {};

TEST_F(DialogOverlayTest, NoDialogByDefault) {
    EXPECT_FALSE(ctrl_.getViewData(3, 3).showDialog);
}

// Sequence to trigger the "无效正则" dialog:
// Add "ERROR" as a string filter, enter edit mode, toggle to regex via Tab,
// replace the pattern with "[invalid", then press Return.
// Must be inlined in each test (protected base members not accessible from free fns).

TEST_F(DialogOverlayTest, InvalidRegexShowsDialog) {
    using E = ftxui::Event;
    key(E::Character('a'));
    for (char c : std::string("ERROR")) key(E::Character(std::string(1, c)));
    key(E::Return);
    chain_.waitReprocess();
    key(E::Character('e'));   // Enter FilterEdit (buffer pre-filled with "ERROR")
    key(E::Tab);              // Toggle to regex mode
    for (int i = 0; i < 5; ++i) key(E::Backspace);
    for (char c : std::string("[invalid")) key(E::Character(std::string(1, c)));
    key(E::Return);           // Triggers "无效正则" dialog

    auto d = ctrl_.getViewData(3, 3);
    EXPECT_TRUE(d.showDialog);
    EXPECT_FALSE(d.dialogHasChoice);
}

TEST_F(DialogOverlayTest, AnyKeyClosesInfoDialog) {
    using E = ftxui::Event;
    key(E::Character('a'));
    for (char c : std::string("ERROR")) key(E::Character(std::string(1, c)));
    key(E::Return);
    chain_.waitReprocess();
    key(E::Character('e'));   // Enter FilterEdit
    key(E::Tab);              // Toggle to regex mode
    for (int i = 0; i < 5; ++i) key(E::Backspace);
    for (char c : std::string("[invalid")) key(E::Character(std::string(1, c)));
    key(E::Return);
    ASSERT_TRUE(ctrl_.getViewData(3, 3).showDialog);

    key(E::Character('q'));  // Any key closes
    EXPECT_FALSE(ctrl_.getViewData(3, 3).showDialog);
}

TEST_F(DialogOverlayTest, DialogTitleBodyRendered) {
    using E = ftxui::Event;
    key(E::Character('a'));
    for (char c : std::string("ERROR")) key(E::Character(std::string(1, c)));
    key(E::Return);
    chain_.waitReprocess();
    key(E::Character('e'));   // Enter FilterEdit
    key(E::Tab);              // Toggle to regex mode
    for (int i = 0; i < 5; ++i) key(E::Backspace);
    for (char c : std::string("[invalid")) key(E::Character(std::string(1, c)));
    key(E::Return);
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
