#include <gtest/gtest.h>
#include <ftxui/component/event.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

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

    // Open a file directly and wait for indexing to complete
    void openAndWait(const std::string& path) {
        reader_.open(path);
        waitForIndexing(reader_);
        ctrl_.getViewData(5, 5);
    }

    // Open a file via 'o' key and wait for indexing to complete
    void openViaKeyAndWait(const std::string& path) {
        key(ftxui::Event::Character('o'));
        type(path);
        key(ftxui::Event::Return);
        waitForIndexing(reader_);
    }
};

// ── Open via 'o' key ──────────────────────────────────────────────────────────

TEST_F(FileOpenTest, OpenEmptyFile) {
    TempFile f("");
    openAndWait(f.path());
    EXPECT_EQ(data().totalLines, 0u);
}

TEST_F(FileOpenTest, OpenFileWithContent) {
    TempFile f("line1\nline2\nline3\n");
    openAndWait(f.path());
    EXPECT_EQ(data().totalLines, 3u);
}

TEST_F(FileOpenTest, OpenFileWithChinese) {
    TempFile f("第一行\n第二行\n第三行\n");
    openAndWait(f.path());
    EXPECT_EQ(data().totalLines, 3u);
    auto sv = reader_.getLine(1);
    EXPECT_EQ(sv, "第一行");
}

TEST_F(FileOpenTest, OpenViaKeyO) {
    TempFile f("hello\nworld\n");
    key(ftxui::Event::Character('o'));
    EXPECT_TRUE(ctrl_.isInputActive());
    type(f.path());
    key(ftxui::Event::Return);
    waitForIndexing(reader_);
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
    openAndWait(f1.path());
    EXPECT_EQ(data().totalLines, 2u);

    openViaKeyAndWait(f2.path());
    EXPECT_EQ(data().totalLines, 1u);
}
