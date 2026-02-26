#include <gtest/gtest.h>
#include <ftxui/component/event.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

class RealtimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string content;
        for (int i = 1; i <= 5; ++i)
            content += "line" + std::to_string(i) + "\n";
        file_ = std::make_unique<TempFile>(content);
        reader_.open(file_->path());
        waitForIndexing(reader_);
        ctrl_.getViewData(5, 5);
    }

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }
    ViewData data()           { return ctrl_.getViewData(5, 5); }

    std::unique_ptr<TempFile> file_;
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};
};

// ── forceCheck detects new lines ──────────────────────────────────────────────

TEST_F(RealtimeTest, ForceCheckPicksUpNewLines) {
    EXPECT_EQ(data().totalLines, 5u);
    file_->append("line6\nline7\n");
    reader_.forceCheck();
    EXPECT_EQ(data().totalLines, 7u);
}

TEST_F(RealtimeTest, ForceCheckEmptyAppend) {
    file_->append("");
    reader_.forceCheck();
    EXPECT_EQ(data().totalLines, 5u);
}

// ── newLineCount tracking ─────────────────────────────────────────────────────

TEST_F(RealtimeTest, NewLinesIncrementCounter) {
    file_->append("line6\n");
    reader_.forceCheck();
    // LogReader calls newLinesCb_ which calls AppController::handleNewLines
    // Since we manually registered the callback in AppController constructor,
    // we need to trigger it via forceCheck
    EXPECT_EQ(data().newLineCount, 1u);
}

TEST_F(RealtimeTest, GKeyResetsNewLineCounter) {
    reader_.setMode(FileMode::Realtime);
    file_->append("line6\n");
    reader_.forceCheck();
    EXPECT_GT(data().newLineCount, 0u);

    key(ftxui::Event::Character('G'));
    EXPECT_EQ(data().newLineCount, 0u);
}

// ── G tail-follow ─────────────────────────────────────────────────────────────

TEST_F(RealtimeTest, GKeyInRealtimeModeEnablesTailFollow) {
    reader_.setMode(FileMode::Realtime);
    key(ftxui::Event::Character('G'));

    file_->append("line6\nline7\nline8\n");
    reader_.forceCheck();

    auto d = data();
    // Cursor should be at the last line
    bool atLast = false;
    for (auto& ll : d.rawPane)
        if (ll.highlighted && ll.rawLineNo == d.totalLines)
            atLast = true;
    EXPECT_TRUE(atLast);
}

TEST_F(RealtimeTest, GKeyInStaticModeIgnored) {
    reader_.setMode(FileMode::Static);
    auto getCursor = [&]() -> size_t {
        for (auto& l : data().rawPane) if (l.highlighted) return l.rawLineNo;
        return size_t(0);
    };
    size_t cursorBefore = getCursor();

    key(ftxui::Event::Character('G'));
    // Cursor should not jump to end in static mode
    size_t cursorAfter = getCursor();
    EXPECT_EQ(cursorBefore, cursorAfter);
}

TEST_F(RealtimeTest, NavigationCancelsTailFollow) {
    reader_.setMode(FileMode::Realtime);
    key(ftxui::Event::Character('G'));

    file_->append("line6\n");
    reader_.forceCheck();

    // Navigate up cancels follow
    key(ftxui::Event::ArrowUp);
    file_->append("line7\n");
    reader_.forceCheck();

    // Cursor should NOT have moved to new last line
    size_t cursorLine = 0;
    for (auto& ll : data().rawPane)
        if (ll.highlighted) cursorLine = ll.rawLineNo;
    EXPECT_LT(cursorLine, data().totalLines);
}

// ── Mode switching ────────────────────────────────────────────────────────────

TEST_F(RealtimeTest, SKeyTogglesRealtimeToStatic) {
    reader_.setMode(FileMode::Realtime);
    key(ftxui::Event::Character('s'));
    EXPECT_EQ(reader_.mode(), FileMode::Static);
}

TEST_F(RealtimeTest, SKeyTogglesStaticToRealtime) {
    reader_.setMode(FileMode::Static);
    key(ftxui::Event::Character('s'));
    EXPECT_EQ(reader_.mode(), FileMode::Realtime);
    reader_.setMode(FileMode::Static);  // cleanup: stop FileWatcher
}

// ── File reset dialog ─────────────────────────────────────────────────────────

TEST_F(RealtimeTest, FileTruncationTriggersResetDialog) {
    // On Windows, a memory-mapped file cannot be truncated while the mapping is
    // active (SetEndOfFile fails with ERROR_USER_MAPPED_FILE).  We therefore
    // trigger the callback directly to test the dialog logic independently of
    // the OS-level detection mechanism.
    ctrl_.handleFileReset();

    auto d = data();
    EXPECT_TRUE(d.showDialog);
    EXPECT_TRUE(d.dialogHasChoice);
}

TEST_F(RealtimeTest, ResetDialogYReloadsFile) {
    // Trigger the dialog first (handleFileReset captures reader_.filePath() now).
    ctrl_.handleFileReset();
    ASSERT_TRUE(data().showDialog);

    // On Windows a memory-mapped file cannot be truncated while the map is
    // active.  Close the reader to release the map, then write the new content,
    // then re-engage the reader so that the dialog's Yes action reopens the
    // correct (2-line) file.
    reader_.close();
    file_->truncateAndWrite("new_line1\nnew_line2\n");
    reader_.open(file_->path());

    // 'Y' calls reader_.open(captured_path) which re-reads the 2-line file,
    // then triggers a reprocess (no filters → instant).
    key(ftxui::Event::Character('Y'));
    chain_.waitReprocess();
    EXPECT_FALSE(data().showDialog);
    EXPECT_EQ(data().totalLines, 2u);
}
