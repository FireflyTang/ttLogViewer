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

// ── Encoding: E2E tests ────────────────────────────────────────────────────────
// Verify that AppController + LogReader correctly expose decoded content from
// UTF-16LE / UTF-16BE / UTF-8 BOM files through getViewData().

// Build an ASCII string as UTF-16LE bytes (BOM included).
static std::string makeUtf16LeE2E(const char* ascii) {
    std::string out;
    out.push_back('\xFF'); out.push_back('\xFE');
    for (const char* p = ascii; *p; ++p) {
        out.push_back(*p);
        out.push_back('\x00');
    }
    return out;
}

// Build an ASCII string as UTF-16BE bytes (BOM included).
static std::string makeUtf16BeE2E(const char* ascii) {
    std::string out;
    out.push_back('\xFE'); out.push_back('\xFF');
    for (const char* p = ascii; *p; ++p) {
        out.push_back('\x00');
        out.push_back(*p);
    }
    return out;
}

TEST_F(FileOpenTest, Utf16LeFileLineCountAndContent) {
    TempFile f(makeUtf16LeE2E("alpha\nbeta\ngamma\n"));
    openAndWait(f.path());

    EXPECT_EQ(data().totalLines, 3u);
    EXPECT_EQ(reader_.getLine(1), "alpha");
    EXPECT_EQ(reader_.getLine(2), "beta");
    EXPECT_EQ(reader_.getLine(3), "gamma");
}

TEST_F(FileOpenTest, Utf16BeFileLineCountAndContent) {
    TempFile f(makeUtf16BeE2E("x\ny\n"));
    openAndWait(f.path());

    EXPECT_EQ(data().totalLines, 2u);
    EXPECT_EQ(reader_.getLine(1), "x");
    EXPECT_EQ(reader_.getLine(2), "y");
}

TEST_F(FileOpenTest, Utf8BomFileContentWithoutBom) {
    std::string content;
    content.push_back('\xEF'); content.push_back('\xBB'); content.push_back('\xBF');
    content += "first\nsecond\n";
    TempFile f(content);
    openAndWait(f.path());

    EXPECT_EQ(data().totalLines, 2u);
    // BOM must not appear in line 1.
    EXPECT_EQ(reader_.getLine(1), "first");
    EXPECT_EQ(reader_.getLine(2), "second");
}

TEST_F(FileOpenTest, Utf16LeNavigationWorks) {
    // Verify that navigation keys work correctly on a UTF-16LE file
    // (i.e. the cursor and scroll logic is not broken by the decoded buffer).
    TempFile f(makeUtf16LeE2E("row1\nrow2\nrow3\nrow4\nrow5\n"));
    openAndWait(f.path());

    EXPECT_EQ(data().totalLines, 5u);
    // Press ArrowDown to move cursor down: rawPane[1] (line 2) should be highlighted.
    key(ftxui::Event::ArrowDown);
    auto d = ctrl_.getViewData(5, 5);
    ASSERT_GE(d.rawPane.size(), 2u);
    EXPECT_FALSE(d.rawPane[0].highlighted)
        << "First row should not be highlighted after pressing ArrowDown";
    EXPECT_TRUE(d.rawPane[1].highlighted)
        << "Second row should be highlighted after pressing ArrowDown";
}
