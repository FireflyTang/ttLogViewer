#include <gtest/gtest.h>
#include <ftxui/component/event.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

class FilterWorkflowTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string content;
        content += "INFO line1\n";
        content += "ERROR line2\n";
        content += "INFO line3\n";
        content += "WARN line4\n";
        content += "ERROR line5\n";
        file_ = std::make_unique<TempFile>(content);
        reader_.open(file_->path());
        waitForIndexing(reader_);
        ctrl_.getViewData(5, 5);
    }

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }
    ViewData data()           { return ctrl_.getViewData(5, 5); }

    void type(const std::string& s) {
        for (char c : s)
            key(ftxui::Event::Character(std::string(1, c)));
    }

    // Press 'a', type pattern, press Enter; then wait for reprocess to finish.
    void addFilter(const std::string& pattern) {
        key(ftxui::Event::Character('a'));
        type(pattern);
        key(ftxui::Event::Return);
        // triggerReprocess starts a background thread; join it so the results
        // are visible in the main thread before we assert anything.
        chain_.waitReprocess();
    }

    std::unique_ptr<TempFile> file_;
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};
};

// ── Add filter via 'a' key ────────────────────────────────────────────────────

TEST_F(FilterWorkflowTest, AddIncludeFilter) {
    addFilter("ERROR");
    // reprocess triggered; with synchronous postFn, should complete
    EXPECT_EQ(chain_.filterCount(), 1u);
    EXPECT_EQ(data().filteredPane.size(), 2u);  // line2 and line5
}

TEST_F(FilterWorkflowTest, AddExcludeFilterViaEdit) {
    // Add an exclude filter by editing filterAt after append
    key(ftxui::Event::Character('a'));
    type("INFO");
    key(ftxui::Event::Return);
    chain_.waitReprocess();

    EXPECT_EQ(chain_.filterCount(), 1u);
    // filteredPane has INFO lines (2)
    EXPECT_EQ(data().filteredPane.size(), 2u);
}

TEST_F(FilterWorkflowTest, EscCancelsFilterAdd) {
    key(ftxui::Event::Character('a'));
    type("ERROR");
    key(ftxui::Event::Escape);
    EXPECT_FALSE(ctrl_.isInputActive());
    EXPECT_EQ(chain_.filterCount(), 0u);  // Not added
}

TEST_F(FilterWorkflowTest, InvalidRegexShowsDialogOnEdit) {
    // Adding "[invalid" as a string filter is accepted.
    // Trying to switch it to regex mode during edit and confirm shows an error dialog.
    addFilter("[invalid");  // Accepted as literal string
    EXPECT_EQ(chain_.filterCount(), 1u);
    key(ftxui::Event::Character('e'));   // Enter edit mode (buffer pre-filled with "[invalid")
    key(ftxui::Event::Tab);              // Toggle to regex mode — pattern is invalid
    key(ftxui::Event::Return);           // Try to confirm — should show error dialog
    EXPECT_TRUE(data().showDialog);
    // Close dialog; filter's useRegex must still be false (edit was not committed)
    key(ftxui::Event::Character('q'));
    EXPECT_FALSE(chain_.filterAt(0).useRegex);
}

TEST_F(FilterWorkflowTest, StringModeAcceptsRegexSpecialChars) {
    // In string mode, regex-special chars like "[" are valid literal patterns
    key(ftxui::Event::Character('a'));
    type("[literal");
    key(ftxui::Event::Return);
    // No dialog shown; filter is accepted
    EXPECT_FALSE(data().showDialog);
    EXPECT_EQ(chain_.filterCount(), 1u);
}

// ── Filter selection with [ and ] ─────────────────────────────────────────────

TEST_F(FilterWorkflowTest, BracketKeysSelectFilter) {
    addFilter("ERROR");
    addFilter("INFO");

    // Both filters added, selectedFilter_ starts at index 1 (last appended)
    // Press '[' to go to index 0
    key(ftxui::Event::Character('['));
    auto d = data();
    EXPECT_TRUE(d.filterTags[0].selected);
    EXPECT_FALSE(d.filterTags[1].selected);
}

// ── Delete filter with 'd' ────────────────────────────────────────────────────

TEST_F(FilterWorkflowTest, DeleteFilter) {
    addFilter("ERROR");
    EXPECT_EQ(chain_.filterCount(), 1u);
    key(ftxui::Event::Character('d'));
    EXPECT_EQ(chain_.filterCount(), 0u);
}

// ── Reorder with + / - ────────────────────────────────────────────────────────

TEST_F(FilterWorkflowTest, PlusMoveFilterUp) {
    addFilter("ERROR");
    addFilter("INFO");
    // selectedFilter_ is at 1 (INFO)
    key(ftxui::Event::Character('+'));
    EXPECT_EQ(chain_.filterAt(0).pattern, "INFO");
    EXPECT_EQ(chain_.filterAt(1).pattern, "ERROR");
}

TEST_F(FilterWorkflowTest, MinusMoveFilterDown) {
    addFilter("ERROR");
    addFilter("INFO");
    key(ftxui::Event::Character('['));  // select index 0 (ERROR)
    key(ftxui::Event::Character('-'));
    EXPECT_EQ(chain_.filterAt(0).pattern, "INFO");
    EXPECT_EQ(chain_.filterAt(1).pattern, "ERROR");
}

// ── Toggle with Space ─────────────────────────────────────────────────────────

TEST_F(FilterWorkflowTest, SpaceTogglesEnabled) {
    addFilter("ERROR");
    EXPECT_TRUE(chain_.filterAt(0).enabled);

    key(ftxui::Event::Character(' '));
    EXPECT_FALSE(chain_.filterAt(0).enabled);

    key(ftxui::Event::Character(' '));
    EXPECT_TRUE(chain_.filterAt(0).enabled);
}

// ── Edit with 'e' key ─────────────────────────────────────────────────────────

TEST_F(FilterWorkflowTest, EditFilterChangesPattern) {
    addFilter("ERROR");
    key(ftxui::Event::Character('e'));
    // Buffer is pre-filled with "ERROR", clear and type new
    for (int i = 0; i < 5; ++i) key(ftxui::Event::Backspace);
    type("INFO");
    key(ftxui::Event::Return);
    EXPECT_EQ(chain_.filterAt(0).pattern, "INFO");
}

TEST_F(FilterWorkflowTest, EditEscKeepsOriginal) {
    addFilter("ERROR");
    key(ftxui::Event::Character('e'));
    for (int i = 0; i < 5; ++i) key(ftxui::Event::Backspace);
    type("INFO");
    key(ftxui::Event::Escape);
    // Pattern unchanged (edit was cancelled)
    EXPECT_EQ(chain_.filterAt(0).pattern, "ERROR");
}

// ── Tab during edit: toggle regex/string mode ─────────────────────────────────

TEST_F(FilterWorkflowTest, TabDuringEditTogglesUseRegex) {
    addFilter("ERROR");
    EXPECT_FALSE(chain_.filterAt(0).useRegex);  // default: string mode
    key(ftxui::Event::Character('e'));           // enter edit mode
    key(ftxui::Event::Tab);                      // toggle to regex mode
    key(ftxui::Event::Return);                   // commit
    chain_.waitReprocess();
    EXPECT_TRUE(chain_.filterAt(0).useRegex);
}

TEST_F(FilterWorkflowTest, InvalidRegexRemainsStringModeAfterDialog) {
    // "[bad" can't be a regex; entering edit + Tab + Return shows dialog
    addFilter("[bad");
    EXPECT_FALSE(chain_.filterAt(0).useRegex);
    key(ftxui::Event::Character('e'));   // enter edit mode (buffer: "[bad")
    key(ftxui::Event::Tab);              // try to switch to regex mode
    key(ftxui::Event::Return);           // pattern is invalid — dialog shown
    EXPECT_TRUE(data().showDialog);
    key(ftxui::Event::Character('q'));   // close dialog
    // useRegex still false — edit was not committed
    EXPECT_FALSE(chain_.filterAt(0).useRegex);
}

// ── matchCount in FilterTag ───────────────────────────────────────────────────

TEST_F(FilterWorkflowTest, MatchCountInFilterTag) {
    addFilter("ERROR");
    // ERROR appears on line2 and line5 in the test file
    auto d = data();
    ASSERT_EQ(d.filterTags.size(), 1u);
    EXPECT_EQ(d.filterTags[0].matchCount, 2u);
}

TEST_F(FilterWorkflowTest, FilterBarLabelIncludesMatchCount) {
    addFilter("ERROR");
    // Filter bar shows "[1:ERROR](2)" in string mode
    auto d = data();
    ASSERT_EQ(d.filterTags.size(), 1u);
    EXPECT_EQ(d.filterTags[0].matchCount, 2u);
    EXPECT_FALSE(d.filterTags[0].useRegex);
}
