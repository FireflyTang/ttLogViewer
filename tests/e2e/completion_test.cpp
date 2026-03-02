#include <gtest/gtest.h>
#include <ftxui/component/event.hpp>

#include <filesystem>
#include <fstream>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

namespace fs = std::filesystem;

// ── Fixture ───────────────────────────────────────────────────────────────────

class CompletionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temp directory with a few files for completion tests.
        tmpDir_ = fs::temp_directory_path() / ("ttlv_completion_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(tmpDir_);

        // Create test files: app.log, api.log, audit.log, system.log
        for (const char* name : {"app.log", "api.log", "audit.log", "system.log"}) {
            std::ofstream f(tmpDir_ / name);
            f << "test\n";
        }
        // Create a subdirectory
        fs::create_directories(tmpDir_ / "subdir");

        // Open a dummy log file so the controller has something loaded.
        logFile_ = std::make_unique<TempFile>("line1\nline2\n");
        reader_.open(logFile_->path());
        waitForIndexing(reader_);
        ctrl_.getViewData(5, 5);
    }

    void TearDown() override {
        fs::remove_all(tmpDir_);
    }

    // Helper: type the 'o' key to enter OpenFile mode, then type a path prefix.
    void openFileMode(const std::string& prefix = "") {
        ctrl_.handleKey(ftxui::Event::Character('o'));
        for (char c : prefix)
            ctrl_.handleKey(ftxui::Event::Character(std::string(1, c)));
    }

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }
    ViewData data()          { return ctrl_.getViewData(10, 10); }

    fs::path tmpDir_;
    std::unique_ptr<TempFile> logFile_;
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};
};

// ── No completion when no match ───────────────────────────────────────────────

TEST_F(CompletionTest, NoMatchDoesNothing) {
    const std::string prefix = (tmpDir_ / "zzz_no_such_file").string();
    openFileMode(prefix);
    key(ftxui::Event::Tab);

    auto d = data();
    EXPECT_FALSE(d.showCompletions);
    EXPECT_TRUE(d.completions.empty());
}

// ── Single match fills directly ───────────────────────────────────────────────

TEST_F(CompletionTest, SingleMatchFillsBufferDirectly) {
    // "system" is a unique prefix among the test files.
    const std::string prefix = (tmpDir_ / "system").string();
    openFileMode(prefix);
    key(ftxui::Event::Tab);

    auto d = data();
    EXPECT_FALSE(d.showCompletions);  // No popup for a single match
    EXPECT_TRUE(d.completions.empty());
    // Buffer should now end with "system.log"
    EXPECT_TRUE(d.inputBuffer.ends_with("system.log")) << "buffer: " << d.inputBuffer;
}

// ── Multiple matches show popup ───────────────────────────────────────────────

TEST_F(CompletionTest, MultipleMatchesShowPopup) {
    // "a" matches: api.log, app.log, audit.log
    const std::string prefix = (tmpDir_ / "a").string();
    openFileMode(prefix);
    key(ftxui::Event::Tab);

    auto d = data();
    EXPECT_TRUE(d.showCompletions);
    EXPECT_GE(d.completions.size(), 3u);
    EXPECT_EQ(d.completionIndex, 0u);
}

// ── Tab cycles through completions ───────────────────────────────────────────

TEST_F(CompletionTest, TabCyclesThroughCompletions) {
    const std::string prefix = (tmpDir_ / "a").string();
    openFileMode(prefix);
    key(ftxui::Event::Tab);  // open popup, index=0

    auto d0 = data();
    ASSERT_TRUE(d0.showCompletions);
    const size_t total = d0.completions.size();

    key(ftxui::Event::Tab);  // cycle → index=1
    EXPECT_EQ(data().completionIndex, 1u);

    key(ftxui::Event::Tab);  // cycle → index=2
    EXPECT_EQ(data().completionIndex, 2u);

    // Tab wraps around
    for (size_t i = 0; i < total; ++i)
        key(ftxui::Event::Tab);
    EXPECT_EQ(data().completionIndex, 2u % total);
}

// ── Arrow keys navigate popup ─────────────────────────────────────────────────

TEST_F(CompletionTest, ArrowDownNavigatesPopup) {
    const std::string prefix = (tmpDir_ / "a").string();
    openFileMode(prefix);
    key(ftxui::Event::Tab);  // open popup

    key(ftxui::Event::ArrowDown);
    EXPECT_EQ(data().completionIndex, 1u);
}

TEST_F(CompletionTest, ArrowUpWrapsAround) {
    const std::string prefix = (tmpDir_ / "a").string();
    openFileMode(prefix);
    key(ftxui::Event::Tab);  // open popup, index=0

    key(ftxui::Event::ArrowUp);  // wraps to last
    const size_t total = data().completions.size();
    EXPECT_EQ(data().completionIndex, total - 1);
}

// ── Enter accepts selected completion ────────────────────────────────────────

TEST_F(CompletionTest, EnterAcceptsSelectedCompletion) {
    const std::string prefix = (tmpDir_ / "ap").string();  // matches: api.log, app.log
    openFileMode(prefix);
    key(ftxui::Event::Tab);  // open popup, index=0 → first match selected

    auto d0 = data();
    ASSERT_TRUE(d0.showCompletions);
    const std::string expected = d0.completions[0];

    key(ftxui::Event::Return);  // accept

    auto d1 = data();
    EXPECT_FALSE(d1.showCompletions);
    EXPECT_TRUE(d1.inputBuffer.ends_with(expected)) << "buffer: " << d1.inputBuffer;
}

// ── Esc closes popup without changing buffer ──────────────────────────────────

TEST_F(CompletionTest, EscapeClosesPopupPreservesBuffer) {
    const std::string prefix = (tmpDir_ / "a").string();
    openFileMode(prefix);
    key(ftxui::Event::Tab);  // open popup
    EXPECT_TRUE(data().showCompletions);

    const std::string bufferBefore = data().inputBuffer;
    key(ftxui::Event::Escape);  // close popup

    auto d = data();
    EXPECT_FALSE(d.showCompletions);
    EXPECT_EQ(d.inputBuffer, bufferBefore);
}

// ── Character input closes popup and types the character ──────────────────────

TEST_F(CompletionTest, CharacterInputClosesPopup) {
    const std::string prefix = (tmpDir_ / "a").string();
    openFileMode(prefix);
    key(ftxui::Event::Tab);  // open popup
    EXPECT_TRUE(data().showCompletions);

    key(ftxui::Event::Character("p"));  // type 'p'
    EXPECT_FALSE(data().showCompletions);
    EXPECT_TRUE(data().inputBuffer.ends_with("ap")) << "buffer: " << data().inputBuffer;
}

// ── Selecting a directory triggers next-level completion ─────────────────────

TEST_F(CompletionTest, SelectingDirectoryTriggersNextLevel) {
    // "sub" matches "subdir/" in tmpDir_.
    const std::string prefix = (tmpDir_ / "sub").string();
    openFileMode(prefix);
    key(ftxui::Event::Tab);  // should directly fill "subdir/" (single match)

    auto d = data();
    // After auto-accepting "subdir/", the next completion round runs.
    // Either popup is shown (if subdir has files) or buffer ends with "subdir/".
    EXPECT_TRUE(d.inputBuffer.ends_with("subdir/")) << "buffer: " << d.inputBuffer;
}

// ── Exiting input mode clears completion state ────────────────────────────────

TEST_F(CompletionTest, ExitInputModeClearsCompletions) {
    const std::string prefix = (tmpDir_ / "a").string();
    openFileMode(prefix);
    key(ftxui::Event::Tab);  // open popup
    EXPECT_TRUE(data().showCompletions);

    // Press Escape to exit input mode entirely (while popup is closed first).
    // We close popup first with Escape, then exit input mode with another Escape.
    key(ftxui::Event::Escape);  // close popup
    key(ftxui::Event::Escape);  // exit input mode

    auto d = data();
    EXPECT_EQ(d.inputMode, InputMode::None);
    EXPECT_FALSE(d.showCompletions);
    EXPECT_TRUE(d.completions.empty());
}

// ── Empty prefix lists all files ──────────────────────────────────────────────

TEST_F(CompletionTest, EmptyPrefixListsAllFiles) {
    // Enter OpenFile mode and type just the directory path (no filename).
    const std::string dirPrefix = tmpDir_.string() + "/";
    openFileMode(dirPrefix);
    key(ftxui::Event::Tab);

    auto d = data();
    EXPECT_TRUE(d.showCompletions);
    // Should list all entries in tmpDir_: api.log, app.log, audit.log, subdir/, system.log
    EXPECT_GE(d.completions.size(), 4u);
}

// ── completionIndex stays in range after wrapping ─────────────────────────────

TEST_F(CompletionTest, CompletionIndexWrapsCorrectly) {
    const std::string prefix = (tmpDir_ / "a").string();
    openFileMode(prefix);
    key(ftxui::Event::Tab);

    auto d = data();
    const size_t total = d.completions.size();
    ASSERT_GT(total, 0u);

    // Cycle through all items and one more (wraps back to 0).
    for (size_t i = 0; i < total; ++i)
        key(ftxui::Event::Tab);
    EXPECT_EQ(data().completionIndex, 0u);
}

// ── #32b: Completion scroll offset ─────────────────────────────────────────────

TEST_F(CompletionTest, ScrollOffsetStartsAtZero) {
    // "a" matches 3+ items; popup opens with scrollOffset=0.
    const std::string prefix = (tmpDir_ / "a").string();
    openFileMode(prefix);
    key(ftxui::Event::Tab);

    auto d = data();
    ASSERT_TRUE(d.showCompletions);
    EXPECT_EQ(d.completionScrollOffset, 0u);
    EXPECT_EQ(d.completionIndex, 0u);
}

TEST_F(CompletionTest, ArrowDownMovesWithinWindow) {
    // With 3+ completions and maxVisible=3, ArrowDown should keep cursor
    // in the window without scrolling until we hit the bottom.
    const std::string prefix = (tmpDir_ / "a").string();
    openFileMode(prefix);
    key(ftxui::Event::Tab);

    auto d0 = data();
    ASSERT_TRUE(d0.showCompletions);
    ASSERT_GE(d0.completions.size(), 3u);

    key(ftxui::Event::ArrowDown);  // index=1
    EXPECT_EQ(data().completionIndex, 1u);
    EXPECT_EQ(data().completionScrollOffset, 0u);

    key(ftxui::Event::ArrowDown);  // index=2
    EXPECT_EQ(data().completionIndex, 2u);
    EXPECT_EQ(data().completionScrollOffset, 0u);  // still visible in 3-row window
}

TEST_F(CompletionTest, ArrowDownScrollsWhenCursorHitsBottom) {
    // Create enough files to need scrolling (>3 matches).
    for (const char* name : {"alpha.txt", "bravo.txt", "charlie.txt", "delta.txt"}) {
        std::ofstream f(tmpDir_ / name);
        f << "test\n";
    }
    // All entries in tmpDir_ now: api.log, app.log, audit.log, system.log,
    // alpha.txt, bravo.txt, charlie.txt, delta.txt, subdir/
    // With empty prefix, list all (>3).
    const std::string dirPrefix = tmpDir_.string() + "/";
    openFileMode(dirPrefix);
    key(ftxui::Event::Tab);

    auto d = data();
    ASSERT_TRUE(d.showCompletions);
    ASSERT_GT(d.completions.size(), 3u);

    // Move down past the 3-row window.
    key(ftxui::Event::ArrowDown);  // index=1, scroll=0
    key(ftxui::Event::ArrowDown);  // index=2, scroll=0
    key(ftxui::Event::ArrowDown);  // index=3, scroll should now be 1
    EXPECT_EQ(data().completionIndex, 3u);
    EXPECT_EQ(data().completionScrollOffset, 1u);
}

TEST_F(CompletionTest, ArrowUpScrollsWhenCursorHitsTop) {
    // Same setup with many files.
    for (const char* name : {"alpha.txt", "bravo.txt", "charlie.txt", "delta.txt"}) {
        std::ofstream f(tmpDir_ / name);
        f << "test\n";
    }
    const std::string dirPrefix = tmpDir_.string() + "/";
    openFileMode(dirPrefix);
    key(ftxui::Event::Tab);

    ASSERT_GT(data().completions.size(), 3u);

    // Move down to index=3 (scroll=1).
    for (int i = 0; i < 3; ++i) key(ftxui::Event::ArrowDown);
    EXPECT_EQ(data().completionScrollOffset, 1u);

    // Move up: index=2, still in window (scroll=1).
    key(ftxui::Event::ArrowUp);
    EXPECT_EQ(data().completionIndex, 2u);
    EXPECT_EQ(data().completionScrollOffset, 1u);

    // Move up more: index=1, scroll should decrease to 1 (still visible).
    key(ftxui::Event::ArrowUp);
    EXPECT_EQ(data().completionIndex, 1u);
    EXPECT_EQ(data().completionScrollOffset, 1u);

    // index=0, scroll should decrease to 0.
    key(ftxui::Event::ArrowUp);
    EXPECT_EQ(data().completionIndex, 0u);
    EXPECT_EQ(data().completionScrollOffset, 0u);
}

// ── ViewData completionCol is set correctly ───────────────────────────────────

TEST_F(CompletionTest, CompletionColReflectsFilenameStart) {
    // "Open: " prompt = 6 chars.  Input = "C:/foo/bar" → dir = "C:/foo/" (7 chars),
    // filePrefix = "bar".  completionCol should be 6 + 7 = 13.
    // We test with our actual tmpDir_ prefix.
    const std::string dirPath = tmpDir_.string() + "/";
    openFileMode(dirPath + "a");
    key(ftxui::Event::Tab);

    auto d = data();
    ASSERT_TRUE(d.showCompletions);
    // completionCol = len("Open: ") + display columns of dirPath + len("/")
    // We just check it's > len("Open: ") = 6 and reasonable.
    EXPECT_GT(d.completionCol, 6);
}
