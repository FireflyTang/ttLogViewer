#include <gtest/gtest.h>
#include <ftxui/component/event.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

// ── Fixture ──────────────────────────────────────────────────────────────────

class MiscKeysTest : public ::testing::Test {
protected:
    void SetUp() override {
        file_ = std::make_unique<TempFile>(
            "line1\nline2\nline3\nline4\nline5\n");
        reader_.open(file_->path());
        waitForIndexing(reader_);
        ctrl_.getViewData(5, 5);
    }

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }
    ViewData data()          { return ctrl_.getViewData(5, 5); }

    std::unique_ptr<TempFile> file_;
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};
};

// ── #24: Ctrl+C in None mode does not change state ──────────────────────────

TEST_F(MiscKeysTest, CtrlCInNoneModeDoesNotChangeState) {
    auto before = data();
    key(ftxui::Event::Character("\x03"));  // Ctrl+C = 0x03
    auto after = data();
    // Should not exit or change input mode.
    EXPECT_EQ(after.inputMode, InputMode::None);
    EXPECT_EQ(after.totalLines, before.totalLines);
}

TEST_F(MiscKeysTest, CtrlCWithSelectionDoesNotExit) {
    // Ctrl+C with active selection should copy (not exit).
    // Selection stays active after copy.
    ctrl_.startSelection(FocusArea::Raw, 0, 0);
    ctrl_.extendSelection(0, 4);
    ctrl_.finalizeSelection();
    EXPECT_TRUE(ctrl_.hasSelection());

    key(ftxui::Event::Character("\x03"));  // Ctrl+C copies
    // Key should be consumed (no state change beyond clipboard).
    auto d = data();
    EXPECT_EQ(d.inputMode, InputMode::None);
    EXPECT_TRUE(ctrl_.hasSelection());  // selection preserved after copy
}

// ── Drag-and-drop: unrecognised printable char opens OpenFile mode ───────────

TEST_F(MiscKeysTest, DragDropPrintableCharEntersOpenFileMode) {
    // Simulates the first character of a dragged path (e.g. 'C' from C:\...).
    key(ftxui::Event::Character("C"));
    auto d = data();
    EXPECT_EQ(d.inputMode, InputMode::OpenFile);
    EXPECT_EQ(d.inputBuffer, "C");
}

TEST_F(MiscKeysTest, DragDropPathCharsPopulateBuffer) {
    // Simulate dragging "C:\log.txt" — characters arrive one by one.
    for (char c : std::string("C:\\log.txt"))
        key(ftxui::Event::Character(std::string(1, c)));
    auto d = data();
    EXPECT_EQ(d.inputMode, InputMode::OpenFile);
    EXPECT_EQ(d.inputBuffer, "C:\\log.txt");
}

TEST_F(MiscKeysTest, DragDropControlCharDoesNotEnterOpenFileMode) {
    // Control characters (< 0x20) must NOT trigger OpenFile mode.
    key(ftxui::Event::Character("\x01"));  // Ctrl+A
    EXPECT_EQ(data().inputMode, InputMode::None);
}

// ── #28: Search active still shows highlighted row ──────────────────────────

TEST_F(MiscKeysTest, SearchActiveStillShowsHighlightedRow) {
    // Enter search, type "line3", press Enter.
    key(ftxui::Event::Character('/'));
    for (char c : std::string("line3"))
        key(ftxui::Event::Character(std::string(1, c)));
    key(ftxui::Event::Return);

    auto d = data();
    EXPECT_TRUE(d.searchActive);
    // The raw pane should have a highlighted row (the search result).
    bool anyHighlighted = false;
    for (const auto& line : d.rawPane) {
        if (line.highlighted) { anyHighlighted = true; break; }
    }
    EXPECT_TRUE(anyHighlighted) << "Search active but no highlighted row in rawPane";
}
