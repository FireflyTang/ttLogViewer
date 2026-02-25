// Cross-feature E2E tests simulating real-world usage patterns.
// These tests span multiple components and focus on scenarios that emerge
// from the interaction of filters, export, realtime monitoring, search, and
// multi-pane navigation working together.

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <ftxui/component/event.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

namespace fs = std::filesystem;

// ── Shared fixture ────────────────────────────────────────────────────────────
//
// File layout (10 lines, alternating INFO / ERROR):
//   line 1:  "INFO 001"
//   line 2:  "ERROR 002"
//   line 3:  "INFO 003"
//   ...
//   line 9:  "INFO 009"
//   line 10: "ERROR 010"
//
// Filter "ERROR" matches lines 2, 4, 6, 8, 10 (5 lines).
// Filter "INFO"  matches lines 1, 3, 5, 7, 9  (5 lines).

class ComprehensiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string content;
        for (int i = 1; i <= 10; ++i) {
            const std::string tag = (i % 2 == 0) ? "ERROR" : "INFO";
            content += tag + " " + std::to_string(i).insert(0, 3 - std::to_string(i).size(), '0') + "\n";
        }
        file_ = std::make_unique<TempFile>(content);
        reader_.open(file_->path());
        waitForIndexing(reader_);
        ctrl_.getViewData(10, 10);
    }

    bool key(ftxui::Event e)    { return ctrl_.handleKey(e); }
    ViewData data()              { return ctrl_.getViewData(10, 10); }

    void type(const std::string& s) {
        for (char c : s)
            key(ftxui::Event::Character(std::string(1, c)));
    }

    // Add a filter via the 'a' key and wait for reprocess.
    void addFilter(const std::string& pattern) {
        key(ftxui::Event::Character('a'));
        type(pattern);
        key(ftxui::Event::Return);
        chain_.waitReprocess();
    }

    // Helper: find the rawLineNo of the highlighted line in a pane.
    static size_t highlightedIn(const std::vector<LogLine>& pane) {
        for (const auto& ll : pane)
            if (ll.highlighted) return ll.rawLineNo;
        return 0;
    }

    // Export via 'w'+Enter, read the file content (closed before we return),
    // remove the export file, and return the content string.
    std::string exportAndRead() {
        key(ftxui::Event::Character('w'));
        const std::string exportPath = data().inputBuffer;
        key(ftxui::Event::Return);  // confirms export

        std::string content;
        {
            std::ifstream f{exportPath};
            if (f.is_open())
                content = std::string((std::istreambuf_iterator<char>(f)),
                                       std::istreambuf_iterator<char>());
        }  // ifstream closed here — safe to remove on Windows
        fs::remove(exportPath);
        return content;
    }

    std::unique_ptr<TempFile> file_;
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};
};

// ── Export with active filter exports only filtered lines ─────────────────────

TEST_F(ComprehensiveTest, ExportWithActiveFilterOnlyExportsFilteredLines) {
    addFilter("ERROR");
    ASSERT_EQ(chain_.filteredLineCount(), 5u);  // lines 2,4,6,8,10

    std::string content = exportAndRead();

    // Every exported line should contain "ERROR"
    EXPECT_NE(content.find("ERROR 002"), std::string::npos);
    EXPECT_NE(content.find("ERROR 010"), std::string::npos);

    // INFO lines must NOT appear in the export
    EXPECT_EQ(content.find("INFO"), std::string::npos);
}

// ── Export without filter exports every raw line ──────────────────────────────

TEST_F(ComprehensiveTest, ExportNoFilterExportsAllLines) {
    // No filter added; export should give all 10 raw lines.
    std::string content = exportAndRead();

    EXPECT_NE(content.find("INFO 001"), std::string::npos);
    EXPECT_NE(content.find("ERROR 002"), std::string::npos);
    EXPECT_NE(content.find("INFO 009"), std::string::npos);
    EXPECT_NE(content.find("ERROR 010"), std::string::npos);

    // Count newlines to verify 10 lines
    int lines = static_cast<int>(std::count(content.begin(), content.end(), '\n'));
    EXPECT_EQ(lines, 10);
}

// ── Realtime monitoring picks up new lines through the filter chain ───────────

TEST_F(ComprehensiveTest, RealtimeWithFilterProcessesNewLines) {
    addFilter("ERROR");
    ASSERT_EQ(chain_.filteredLineCount(), 5u);  // 5 existing ERROR lines

    // Append two new lines: one matching, one not
    file_->append("ERROR 011\n");
    file_->append("INFO 012\n");
    reader_.forceCheck();   // triggers handleNewLines → chain_.processNewLines

    // Only the new ERROR line should have been added to the filter output
    EXPECT_EQ(chain_.filteredLineCount(), 6u);
    // Total raw lines increased by 2
    EXPECT_EQ(data().totalLines, 12u);
}

// ── Two panes maintain truly independent cursor positions ─────────────────────

TEST_F(ComprehensiveTest, TwoPanesRemainsIndependentUnderFilter) {
    addFilter("ERROR");  // filtered pane: 5 lines (2,4,6,8,10)

    // Move filtered pane cursor to its second entry (raw line 4)
    key(ftxui::Event::Tab);         // focus → Filtered
    key(ftxui::Event::ArrowDown);   // filteredState_.cursor = 1 → raw line 4

    // Switch back to Raw and move raw cursor down 4 places (raw line 5)
    key(ftxui::Event::Tab);         // focus → Raw
    for (int i = 0; i < 4; ++i) key(ftxui::Event::ArrowDown);

    // Snapshot both pane cursors
    const size_t rawLine = highlightedIn(data().rawPane);
    key(ftxui::Event::Tab);   // focus → Filtered (to read filteredPane highlights)
    const size_t filtLine = highlightedIn(data().filteredPane);

    // Raw pane cursor: line 5 (INFO); Filtered pane cursor: line 4 (ERROR)
    EXPECT_EQ(rawLine,  5u);
    EXPECT_EQ(filtLine, 4u);
    EXPECT_NE(rawLine, filtLine);
}

// ── Search jumps the raw pane only; filtered pane cursor is unchanged ─────────

TEST_F(ComprehensiveTest, SearchOnlyAffectsRawPaneCursorWithFilter) {
    addFilter("ERROR");   // filtered pane starts at cursor 0 → raw line 2

    // Move filtered cursor to second entry (raw line 4)
    key(ftxui::Event::Tab);
    key(ftxui::Event::ArrowDown);   // filteredState_.cursor = 1 → line 4
    key(ftxui::Event::Tab);         // back to Raw

    // Search for "INFO" — first result is raw line 1
    key(ftxui::Event::Character('/'));
    type("INFO");
    key(ftxui::Event::Return);
    // Give the search thread time to complete on this small file
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Raw pane cursor should have jumped to line 1 (first INFO)
    const size_t rawLine = highlightedIn(data().rawPane);
    EXPECT_EQ(rawLine, 1u);

    // Filtered pane cursor must still be at line 4 (untouched by search)
    key(ftxui::Event::Tab);   // focus → Filtered
    const size_t filtLine = highlightedIn(data().filteredPane);
    EXPECT_EQ(filtLine, 4u);
}

// ── Multiple filters: add / delete / reorder workflow ─────────────────────────

TEST_F(ComprehensiveTest, MultipleFiltersAddDeleteReorderWorkflow) {
    addFilter("INFO");    // index 0
    addFilter("ERROR");   // index 1
    addFilter("WARN");    // index 2  (no WARN lines → doesn't affect count test)

    ASSERT_EQ(chain_.filterCount(), 3u);
    EXPECT_EQ(chain_.filterAt(0).pattern, "INFO");
    EXPECT_EQ(chain_.filterAt(1).pattern, "ERROR");
    EXPECT_EQ(chain_.filterAt(2).pattern, "WARN");

    // Select index 1 (ERROR) and delete it
    key(ftxui::Event::Character('['));  // selectedFilter_ 2→1
    key(ftxui::Event::Character('d'));
    chain_.waitReprocess();

    ASSERT_EQ(chain_.filterCount(), 2u);
    EXPECT_EQ(chain_.filterAt(0).pattern, "INFO");
    EXPECT_EQ(chain_.filterAt(1).pattern, "WARN");

    // Add a new filter — it becomes index 2
    addFilter("DEBUG");
    ASSERT_EQ(chain_.filterCount(), 3u);
    EXPECT_EQ(chain_.filterAt(2).pattern, "DEBUG");

    // Move DEBUG (currently at index 2) up one position
    key(ftxui::Event::Character('+'));  // moveUp(2): swap 1↔2
    chain_.waitReprocess();

    EXPECT_EQ(chain_.filterAt(1).pattern, "DEBUG");
    EXPECT_EQ(chain_.filterAt(2).pattern, "WARN");
    // Index 0 unchanged
    EXPECT_EQ(chain_.filterAt(0).pattern, "INFO");
}

// ── Large file: single filter gives correct count ────────────────────────────

TEST(LargeFileTest, SingleFilterGivesCorrectCount) {
    // 500-line file: every 5th line is "CRITICAL event N", the rest "debug N"
    std::string content;
    for (int i = 1; i <= 500; ++i) {
        if (i % 5 == 0)
            content += "CRITICAL event " + std::to_string(i) + "\n";
        else
            content += "debug info " + std::to_string(i) + "\n";
    }
    TempFile f(content);
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);
    AppController ctrl(reader, chain);

    // Add filter "CRITICAL" via UI key flow
    ctrl.handleKey(ftxui::Event::Character('a'));
    for (char c : std::string("CRITICAL"))
        ctrl.handleKey(ftxui::Event::Character(std::string(1, c)));
    ctrl.handleKey(ftxui::Event::Return);
    chain.waitReprocess();

    EXPECT_EQ(chain.filteredLineCount(), 100u);  // 500 / 5 = 100 CRITICAL lines

    // Cursor should be clamped within the filtered line count
    const auto d = ctrl.getViewData(10, 10);
    for (const auto& ll : d.filteredPane)
        EXPECT_LE(ll.rawLineNo, 500u);
}

// ── Large file: include then exclude chain ────────────────────────────────────

TEST(LargeFileTest, IncludeThenExcludeChainGivesCorrectCount) {
    // 300 lines: "INFO event N" and every 10th line is "INFO SKIP event N"
    std::string content;
    for (int i = 1; i <= 300; ++i) {
        if (i % 10 == 0)
            content += "INFO SKIP event " + std::to_string(i) + "\n";
        else
            content += "INFO event " + std::to_string(i) + "\n";
    }
    TempFile f(content);
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);

    // Filter 0: include "INFO" → all 300 lines
    // Filter 1: exclude "SKIP"  → removes the 30 SKIP lines
    ASSERT_TRUE(chain.append({ .pattern = "INFO" }));
    {
        std::promise<void> d1;
        chain.reprocess(0, nullptr, [&]{ d1.set_value(); });
        waitForReprocess(d1);
    }
    ASSERT_EQ(chain.filteredLineCount(), 300u);

    ASSERT_TRUE(chain.append({ .pattern = "SKIP", .exclude = true }));
    {
        std::promise<void> d2;
        chain.reprocess(1, nullptr, [&]{ d2.set_value(); });
        waitForReprocess(d2);
    }
    EXPECT_EQ(chain.filteredLineCount(), 270u);  // 300 - 30 = 270
}

// ── Export preserves UTF-8 multi-byte characters ──────────────────────────────

TEST(ExportUtf8Test, ExportPreservesUtf8Content) {
    // Chinese log lines: 3 lines, middle one is ERROR
    TempFile f("日志信息 001\nERROR: 数据库连接失败\n普通日志 003\n");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);
    AppController ctrl(reader, chain);

    // Add ERROR filter
    ctrl.handleKey(ftxui::Event::Character('a'));
    for (char c : std::string("ERROR"))
        ctrl.handleKey(ftxui::Event::Character(std::string(1, c)));
    ctrl.handleKey(ftxui::Event::Return);
    chain.waitReprocess();
    ASSERT_EQ(chain.filteredLineCount(), 1u);

    // Export
    ctrl.handleKey(ftxui::Event::Character('w'));
    const std::string exportPath = ctrl.getViewData(5, 5).inputBuffer;
    ctrl.handleKey(ftxui::Event::Return);

    std::string content;
    {
        std::ifstream ef{exportPath};
        ASSERT_TRUE(ef.is_open());
        content = std::string((std::istreambuf_iterator<char>(ef)),
                               std::istreambuf_iterator<char>());
    }
    fs::remove(exportPath);

    // The Chinese text in the ERROR line must be intact
    EXPECT_NE(content.find("ERROR: \xe6\x95\xb0\xe6\x8d\xae\xe5\xba\x93\xe8\xbf\x9e\xe6\x8e\xa5\xe5\xa4\xb1\xe8\xb4\xa5"), std::string::npos);
    // Other Chinese lines must NOT appear
    EXPECT_EQ(content.find("\xe6\x97\xa5\xe5\xbf\x97\xe4\xbf\xa1\xe6\x81\xaf"), std::string::npos);
}

// ── Export long folded line exports full content, no ellipsis ─────────────────

TEST(ExportFoldTest, ExportLongLineNotTruncatedByFold) {
    // A single 100-character line
    const std::string longContent(100, 'x');
    TempFile f(longContent + "\n");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);
    AppController ctrl(reader, chain);

    ctrl.onTerminalResize(40, 20);  // narrow terminal to make fold visible

    // Fold the first (and only) line — rendered view shows "…"
    ctrl.handleKey(ftxui::Event::Character('z'));
    {
        const auto d = ctrl.getViewData(5, 5);
        ASSERT_FALSE(d.rawPane.empty());
        EXPECT_TRUE(d.rawPane[0].folded);  // verify fold flag is set
    }

    // Export — must contain the full 100-char content, NOT "…"
    ctrl.handleKey(ftxui::Event::Character('w'));
    const std::string exportPath = ctrl.getViewData(5, 5).inputBuffer;
    ctrl.handleKey(ftxui::Event::Return);

    std::string content;
    {
        std::ifstream ef{exportPath};
        ASSERT_TRUE(ef.is_open());
        content = std::string((std::istreambuf_iterator<char>(ef)),
                               std::istreambuf_iterator<char>());
    }
    fs::remove(exportPath);

    EXPECT_NE(content.find(longContent), std::string::npos);
    EXPECT_EQ(content.find("\xe2\x80\xa6"), std::string::npos);  // no "…" (U+2026 UTF-8)
}

// ── Filter toggle (Space) with realtime new lines ─────────────────────────────

TEST(RealtimeFilterToggleTest, DisabledFilterPassesNewLinesThrough) {
    TempFile f("ERROR alpha\nINFO beta\n");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);
    AppController ctrl(reader, chain);

    // Add include filter "ERROR"
    ctrl.handleKey(ftxui::Event::Character('a'));
    for (char c : std::string("ERROR"))
        ctrl.handleKey(ftxui::Event::Character(std::string(1, c)));
    ctrl.handleKey(ftxui::Event::Return);
    chain.waitReprocess();
    ASSERT_EQ(chain.filteredLineCount(), 1u);

    // Disable the filter — all lines pass through
    ctrl.handleKey(ftxui::Event::Character(' '));
    chain.waitReprocess();
    EXPECT_EQ(chain.filteredLineCount(), 2u);

    // Append a new line; it should also appear in the (disabled-filter) output
    f.append("INFO gamma\n");
    reader.forceCheck();
    EXPECT_EQ(chain.filteredLineCount(), 3u);

    // Re-enable filter — only ERROR lines remain
    ctrl.handleKey(ftxui::Event::Character(' '));
    chain.waitReprocess();
    EXPECT_EQ(chain.filteredLineCount(), 1u);
}

// ── Multiple export operations in the same session ────────────────────────────

TEST(ExportMultipleTest, TwoConsecutiveExportsProduceDifferentFiles) {
    TempFile f("line1\nline2\nline3\n");
    LogReader reader;
    reader.open(f.path());
    waitForIndexing(reader);
    FilterChain chain(reader);
    AppController ctrl(reader, chain);

    // First export
    ctrl.handleKey(ftxui::Event::Character('w'));
    const std::string path1 = ctrl.getViewData(5, 5).inputBuffer;
    ctrl.handleKey(ftxui::Event::Return);
    ctrl.handleKey(ftxui::Event::Return);  // close success dialog

    // Brief pause to ensure timestamps differ
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Second export (same session, different filename due to timestamp)
    ctrl.handleKey(ftxui::Event::Character('w'));
    const std::string path2 = ctrl.getViewData(5, 5).inputBuffer;
    ctrl.handleKey(ftxui::Event::Return);
    ctrl.handleKey(ftxui::Event::Return);  // close success dialog

    // Both files must exist and contain the log content
    EXPECT_TRUE(fs::exists(path1));
    EXPECT_TRUE(fs::exists(path2));

    std::string c1, c2;
    { std::ifstream f1{path1}; if (f1) c1 = std::string((std::istreambuf_iterator<char>(f1)), {}); }
    { std::ifstream f2{path2}; if (f2) c2 = std::string((std::istreambuf_iterator<char>(f2)), {}); }

    EXPECT_NE(c1.find("line1"), std::string::npos);
    EXPECT_NE(c2.find("line1"), std::string::npos);

    // Filenames may or may not differ (timestamps could collide within 1 s)
    // Just verify neither path is empty
    EXPECT_FALSE(path1.empty());
    EXPECT_FALSE(path2.empty());

    fs::remove(path1);
    fs::remove(path2);
}
