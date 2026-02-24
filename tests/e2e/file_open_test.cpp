#include <gtest/gtest.h>
#include <ftxui/component/event.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"

class FileOpenTest : public ::testing::Test {
protected:
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }
    ViewData data()           { return ctrl_.getViewData(5, 5); }

    // Helper: type a string char by char
    void type(const std::string& s) {
        for (char c : s)
            key(ftxui::Event::Character(std::string(1, c)));
    }
};

// ── Open via 'o' key ──────────────────────────────────────────────────────────

TEST_F(FileOpenTest, OpenEmptyFile) {
    TempFile f("");
    reader_.open(f.path());
    ctrl_.getViewData(5, 5);
    EXPECT_EQ(ctrl_.getViewData(5, 5).totalLines, 0u);
}

TEST_F(FileOpenTest, OpenFileWithContent) {
    TempFile f("line1\nline2\nline3\n");
    reader_.open(f.path());
    ctrl_.getViewData(5, 5);
    EXPECT_EQ(ctrl_.getViewData(5, 5).totalLines, 3u);
}

TEST_F(FileOpenTest, OpenFileWithChinese) {
    TempFile f("第一行\n第二行\n第三行\n");
    reader_.open(f.path());
    ctrl_.getViewData(5, 5);
    EXPECT_EQ(ctrl_.getViewData(5, 5).totalLines, 3u);
    auto sv = reader_.getLine(1);
    EXPECT_EQ(sv, "第一行");
}

TEST_F(FileOpenTest, OpenViaKeyO) {
    TempFile f("hello\nworld\n");
    key(ftxui::Event::Character('o'));
    EXPECT_TRUE(ctrl_.isInputActive());
    type(f.path());
    key(ftxui::Event::Return);
    EXPECT_FALSE(ctrl_.isInputActive());
    EXPECT_EQ(ctrl_.getViewData(5, 5).totalLines, 2u);
}

TEST_F(FileOpenTest, OpenInvalidPathShowsDialog) {
    key(ftxui::Event::Character('o'));
    type("/nonexistent/path/file.log");
    key(ftxui::Event::Return);
    auto d = ctrl_.getViewData(5, 5);
    EXPECT_TRUE(d.showDialog);
    EXPECT_FALSE(d.dialogHasChoice);
}

TEST_F(FileOpenTest, EscCancelsOpenMode) {
    key(ftxui::Event::Character('o'));
    EXPECT_TRUE(ctrl_.isInputActive());
    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.isInputActive());
}

TEST_F(FileOpenTest, OpenViaKeyOSwitchesFile) {
    TempFile f1("file1_line1\nfile1_line2\n");
    TempFile f2("file2_line1\n");
    reader_.open(f1.path());
    ctrl_.getViewData(5, 5);
    EXPECT_EQ(ctrl_.getViewData(5, 5).totalLines, 2u);

    key(ftxui::Event::Character('o'));
    type(f2.path());
    key(ftxui::Event::Return);
    EXPECT_EQ(ctrl_.getViewData(5, 5).totalLines, 1u);
}
