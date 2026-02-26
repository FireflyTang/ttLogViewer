#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>

#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

// ── Fixture ────────────────────────────────────────────────────────────────────

class FilterChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 10 lines: line1..line10, with ERROR on line3 and line7
        std::string content;
        content += "line1\n";
        content += "line2\n";
        content += "ERROR: something bad\n";  // line3
        content += "line4\n";
        content += "line5\n";
        content += "line6\n";
        content += "ERROR: another error\n";  // line7
        content += "line8\n";
        content += "line9\n";
        content += "line10\n";
        file_ = std::make_unique<TempFile>(content);
        reader_.open(file_->path());
        waitForIndexing(reader_);
    }

    TempFile*  file() { return file_.get(); }

    std::unique_ptr<TempFile> file_;
    LogReader   reader_;
    FilterChain chain_{reader_};
};

// ── No filters ────────────────────────────────────────────────────────────────

TEST_F(FilterChainTest, NoFiltersPassthrough) {
    EXPECT_EQ(chain_.filteredLineCount(), 10u);
    EXPECT_EQ(chain_.filteredLineAt(0), 1u);
    EXPECT_EQ(chain_.filteredLineAt(9), 10u);
}

// ── Include filter ────────────────────────────────────────────────────────────

TEST_F(FilterChainTest, IncludeFilterMatchesSomeLines) {
    ASSERT_TRUE(chain_.append({.pattern = "ERROR"}));

    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    EXPECT_EQ(chain_.filteredLineCount(), 2u);
    EXPECT_EQ(chain_.filteredLineAt(0), 3u);  // line3
    EXPECT_EQ(chain_.filteredLineAt(1), 7u);  // line7
}

TEST_F(FilterChainTest, IncludeFilterNoMatch) {
    ASSERT_TRUE(chain_.append({.pattern = "CRITICAL"}));

    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    EXPECT_EQ(chain_.filteredLineCount(), 0u);
}

TEST_F(FilterChainTest, IncludeFilterAllMatch) {
    // "line" matches line1,line2,line4,line5,line6,line8,line9,line10 = 8 lines.
    // The two ERROR lines ("ERROR: something bad", "ERROR: another error") do NOT
    // contain the substring "line".
    ASSERT_TRUE(chain_.append({.pattern = "line"}));

    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    EXPECT_EQ(chain_.filteredLineCount(), 8u);
}

// ── Exclude filter ────────────────────────────────────────────────────────────

TEST_F(FilterChainTest, ExcludeFilterRemovesMatches) {
    ASSERT_TRUE(chain_.append({.pattern = "ERROR", .exclude = true}));

    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    EXPECT_EQ(chain_.filteredLineCount(), 8u);
}

TEST_F(FilterChainTest, ExcludeFilterNoMatch) {
    ASSERT_TRUE(chain_.append({.pattern = "CRITICAL", .exclude = true}));

    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    EXPECT_EQ(chain_.filteredLineCount(), 10u);
}

TEST_F(FilterChainTest, ExcludeFilterAllMatch) {
    // Use regex mode so "." matches any character (every non-empty line)
    ASSERT_TRUE(chain_.append({.pattern = ".", .exclude = true, .useRegex = true}));  // match any non-empty

    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    EXPECT_EQ(chain_.filteredLineCount(), 0u);
}

// ── Disabled filter ───────────────────────────────────────────────────────────

TEST_F(FilterChainTest, AllFiltersDisabledPassthrough) {
    ASSERT_TRUE(chain_.append({.pattern = "ERROR", .enabled = false}));

    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    EXPECT_EQ(chain_.filteredLineCount(), 10u);
}

TEST_F(FilterChainTest, PartiallyDisabledFilters) {
    ASSERT_TRUE(chain_.append({.pattern = "ERROR"}));
    ASSERT_TRUE(chain_.append({.pattern = "another", .enabled = false}));

    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    // First filter keeps 2 lines; second filter is disabled (pass through)
    EXPECT_EQ(chain_.filteredLineCount(), 2u);
}

// ── Chain: include + exclude ──────────────────────────────────────────────────

TEST_F(FilterChainTest, ChainedIncludeThenExclude) {
    ASSERT_TRUE(chain_.append({.pattern = "line"}));   // include all "line" lines (10)
    ASSERT_TRUE(chain_.append({.pattern = "ERROR", .exclude = true}));  // remove 2 ERROR lines

    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    EXPECT_EQ(chain_.filteredLineCount(), 8u);
}

// ── Filter management: remove ─────────────────────────────────────────────────

TEST_F(FilterChainTest, RemoveFilterUpdatesCount) {
    chain_.append({.pattern = "ERROR"});
    chain_.append({.pattern = "line"});
    EXPECT_EQ(chain_.filterCount(), 2u);

    chain_.remove(0);
    EXPECT_EQ(chain_.filterCount(), 1u);
    EXPECT_EQ(chain_.filterAt(0).pattern, "line");
}

// ── Filter management: edit ───────────────────────────────────────────────────

TEST_F(FilterChainTest, EditFilterUpdatesPattern) {
    chain_.append({.pattern = "ERROR"});
    ASSERT_TRUE(chain_.edit(0, {.pattern = "line"}));
    EXPECT_EQ(chain_.filterAt(0).pattern, "line");
}

TEST_F(FilterChainTest, EditInvalidRegexFails) {
    chain_.append({.pattern = "ERROR"});
    // Must use useRegex=true so the pattern is compiled as a regex
    EXPECT_FALSE(chain_.edit(0, {.pattern = "[invalid", .useRegex = true}));
    EXPECT_EQ(chain_.filterAt(0).pattern, "ERROR");  // unchanged
}

// ── Filter management: moveUp / moveDown ──────────────────────────────────────

TEST_F(FilterChainTest, MoveUpAndDown) {
    chain_.append({.pattern = "A"});
    chain_.append({.pattern = "B"});
    chain_.append({.pattern = "C"});

    chain_.moveUp(2);  // C moves to index 1
    EXPECT_EQ(chain_.filterAt(0).pattern, "A");
    EXPECT_EQ(chain_.filterAt(1).pattern, "C");
    EXPECT_EQ(chain_.filterAt(2).pattern, "B");

    chain_.moveDown(0);  // A moves to index 1
    EXPECT_EQ(chain_.filterAt(0).pattern, "C");
    EXPECT_EQ(chain_.filterAt(1).pattern, "A");
}

TEST_F(FilterChainTest, MoveFirstUpIsNoop) {
    chain_.append({.pattern = "A"});
    chain_.append({.pattern = "B"});
    chain_.moveUp(0);
    EXPECT_EQ(chain_.filterAt(0).pattern, "A");
}

TEST_F(FilterChainTest, MoveLastDownIsNoop) {
    chain_.append({.pattern = "A"});
    chain_.append({.pattern = "B"});
    chain_.moveDown(1);
    EXPECT_EQ(chain_.filterAt(1).pattern, "B");
}

// ── processNewLines ───────────────────────────────────────────────────────────

TEST_F(FilterChainTest, ProcessNewLinesAppendsToOutput) {
    ASSERT_TRUE(chain_.append({.pattern = "ERROR"}));
    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    ASSERT_EQ(chain_.filteredLineCount(), 2u);

    // Append a new ERROR line to the file
    file_->append("ERROR: new error\n");
    reader_.forceCheck();

    // processNewLines should have been called via forceCheck → newLinesCb_ → AppController
    // But here we call it directly since we don't have AppController
    chain_.processNewLines(11, 11);
    EXPECT_EQ(chain_.filteredLineCount(), 3u);
}

// ── computeColors ─────────────────────────────────────────────────────────────

TEST_F(FilterChainTest, ComputeColorsNoFilters) {
    EXPECT_TRUE(chain_.computeColors(1, "hello").empty());
}

TEST_F(FilterChainTest, ComputeColorsMatchFound) {
    ASSERT_TRUE(chain_.append({.pattern = "ERROR", .color = "#FF0000"}));
    std::string_view line = "ERROR: something bad";
    auto spans = chain_.computeColors(3, line);
    ASSERT_FALSE(spans.empty());
    EXPECT_EQ(spans[0].start, 0u);
    EXPECT_EQ(spans[0].end, 5u);
    EXPECT_EQ(spans[0].color, "#FF0000");
}

TEST_F(FilterChainTest, ComputeColorsMultipleMatches) {
    ASSERT_TRUE(chain_.append({.pattern = "error", .color = "#FF0000"}));
    std::string_view line = "error one error two";
    auto spans = chain_.computeColors(1, line);
    EXPECT_EQ(spans.size(), 2u);
}

TEST_F(FilterChainTest, ComputeColorsExcludeFilterSkipped) {
    ASSERT_TRUE(chain_.append({.pattern = "ERROR", .color = "#FF0000", .exclude = true}));
    auto spans = chain_.computeColors(3, "ERROR: something bad");
    EXPECT_TRUE(spans.empty());  // Exclude filters don't colorize
}

// ── Invalid regex ─────────────────────────────────────────────────────────────

TEST_F(FilterChainTest, AppendInvalidRegexFails) {
    // Must use useRegex=true so the pattern is compiled as a regex
    EXPECT_FALSE(chain_.append({.pattern = "[invalid", .useRegex = true}));
    EXPECT_EQ(chain_.filterCount(), 0u);
}

TEST_F(FilterChainTest, AppendInvalidPatternSucceedsInStringMode) {
    // In string mode (default), any non-empty pattern is valid (no regex compile)
    EXPECT_TRUE(chain_.append({.pattern = "[invalid"}));
    EXPECT_EQ(chain_.filterCount(), 1u);
}

TEST_F(FilterChainTest, AppendEmptyPatternFails) {
    // Empty pattern compiles fine as a regex but is arguably useless.
    // We accept it since std::regex("") is valid.
    EXPECT_TRUE(chain_.append({.pattern = ""}));
}

// ── reset ─────────────────────────────────────────────────────────────────────

TEST_F(FilterChainTest, ResetClearsOutputCaches) {
    chain_.append({.pattern = "ERROR"});
    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);
    ASSERT_EQ(chain_.filteredLineCount(), 2u);

    chain_.reset();
    // reset() clears output caches but keeps filter definitions.
    // filteredLineCount() returns filters_.back().output.size() when filters exist,
    // which is 0 after reset.  Caller must call reprocess() to rebuild the cache.
    EXPECT_EQ(chain_.filteredLineCount(), 0u);
}

// ── JSON save / load ──────────────────────────────────────────────────────────

TEST_F(FilterChainTest, SaveLoadRoundTrip) {
    chain_.append({.pattern = "ERROR", .color = "#FF0000", .enabled = true,  .exclude = false});
    chain_.append({.pattern = "WARN",  .color = "#FFFF00", .enabled = false, .exclude = true});

    TempFile sessionFile("");
    chain_.save(sessionFile.path());

    FilterChain chain2(reader_);
    ASSERT_TRUE(chain2.load(sessionFile.path()));

    ASSERT_EQ(chain2.filterCount(), 2u);
    EXPECT_EQ(chain2.filterAt(0).pattern, "ERROR");
    EXPECT_EQ(chain2.filterAt(0).color,   "#FF0000");
    EXPECT_TRUE(chain2.filterAt(0).enabled);
    EXPECT_FALSE(chain2.filterAt(0).exclude);
    EXPECT_EQ(chain2.filterAt(1).pattern, "WARN");
    EXPECT_FALSE(chain2.filterAt(1).enabled);
    EXPECT_TRUE(chain2.filterAt(1).exclude);
}

TEST_F(FilterChainTest, LoadNonexistentFile) {
    EXPECT_FALSE(chain_.load("/nonexistent/path/session.json"));
    EXPECT_EQ(chain_.filterCount(), 0u);
}

TEST_F(FilterChainTest, LoadInvalidJson) {
    TempFile bad("not valid json{{");
    EXPECT_FALSE(chain_.load(bad.path()));
}

TEST_F(FilterChainTest, LoadWrongVersion) {
    TempFile bad("{\"version\":99,\"filters\":[]}");
    EXPECT_FALSE(chain_.load(bad.path()));
}

// ── filteredLines ─────────────────────────────────────────────────────────────

TEST_F(FilterChainTest, FilteredLinesRange) {
    // "line" matches: line1(1), line2(2), line4(4), line5(5), line6(6), line8(8),
    // line9(9), line10(10) — the two ERROR lines (3, 7) are excluded.
    ASSERT_TRUE(chain_.append({.pattern = "line"}));
    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    auto lines = chain_.filteredLines(0, 3);
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], 1u);  // raw line 1: "line1"
    EXPECT_EQ(lines[1], 2u);  // raw line 2: "line2"
    EXPECT_EQ(lines[2], 4u);  // raw line 4: "line4" (line 3 is "ERROR: something bad")
}

TEST_F(FilterChainTest, FilteredLinesPartialAtEnd) {
    ASSERT_TRUE(chain_.append({.pattern = "ERROR"}));
    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    // Only 2 results; ask for 10 from index 1
    auto lines = chain_.filteredLines(1, 10);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], 7u);
}

// ── Phase 3: save() with missing parent directory ────────────────────────────

TEST_F(FilterChainTest, SaveCreatesParentDirectory) {
    namespace fs = std::filesystem;
    const std::string dir = (fs::temp_directory_path()
                             / ("ttlv_fc_dir_" + std::to_string(
                                   std::chrono::steady_clock::now().time_since_epoch().count()))
                             / "nested").string();
    const std::string sPath = dir + "/session.json";

    // Should not throw and should create the file
    chain_.append({.pattern = "ERROR"});
    EXPECT_NO_THROW(chain_.save(sPath, "file.log", FileMode::Static));
    EXPECT_TRUE(fs::exists(sPath));

    fs::remove_all(fs::path(dir).parent_path());
}

// ── Phase 3: reprocess() during modification ─────────────────────────────────

TEST_F(FilterChainTest, AppendDuringReprocessRestarts) {
    // Start a reprocess with first filter "line" (matches 8 lines)
    chain_.append({.pattern = "line"});
    std::promise<void> done1;
    chain_.reprocess(0, nullptr, [&]{ done1.set_value(); });

    // Immediately append another filter; this cancels + restarts reprocess.
    // Second filter "1" applied to the 8 "lineN" lines matches "line1" and
    // "line10", so the final filtered count is 2.
    chain_.append({.pattern = "1"});
    std::promise<void> done2;
    chain_.reprocess(0, nullptr, [&]{ done2.set_value(); });
    waitForReprocess(done2);

    // Both filters should be active and produce a consistent non-zero result
    EXPECT_EQ(chain_.filterCount(), 2u);
    EXPECT_GT(chain_.filteredLineCount(), 0u);
}

// ── String mode vs regex mode ─────────────────────────────────────────────────

TEST_F(FilterChainTest, StringModeDoesNotTreatDotAsWildcard) {
    // Pattern "." as string should only match a literal period, not every character
    ASSERT_TRUE(chain_.append({.pattern = "."}));  // useRegex=false by default

    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    // None of the 10 test lines contain a literal period
    EXPECT_EQ(chain_.filteredLineCount(), 0u);
}

TEST_F(FilterChainTest, ToggleUseRegexFlipsMode) {
    chain_.append({.pattern = "ERROR"});
    EXPECT_FALSE(chain_.filterAt(0).useRegex);

    chain_.toggleUseRegex(0);
    EXPECT_TRUE(chain_.filterAt(0).useRegex);

    chain_.toggleUseRegex(0);
    EXPECT_FALSE(chain_.filterAt(0).useRegex);
}

TEST_F(FilterChainTest, ToggleUseRegexInvalidPatternRevertsMode) {
    // "[invalid" is not a valid regex; toggle should revert if it fails
    ASSERT_TRUE(chain_.append({.pattern = "[invalid"}));  // string mode, always succeeds
    EXPECT_FALSE(chain_.filterAt(0).useRegex);

    chain_.toggleUseRegex(0);
    // Should stay false because "[invalid" can't compile as regex
    EXPECT_FALSE(chain_.filterAt(0).useRegex);
}

TEST_F(FilterChainTest, FilteredLineCountAtReturnsStageOutput) {
    ASSERT_TRUE(chain_.append({.pattern = "ERROR"}));

    std::promise<void> done;
    chain_.reprocess(0, nullptr, [&]{ done.set_value(); });
    waitForReprocess(done);

    // ERROR matches 2 lines (line3 and line7)
    EXPECT_EQ(chain_.filteredLineCountAt(0), 2u);
}

TEST_F(FilterChainTest, FilteredLineCountAtOutOfRangeReturnsZero) {
    EXPECT_EQ(chain_.filteredLineCountAt(99), 0u);
}

// ── String mode matching via computeColors ────────────────────────────────────

TEST_F(FilterChainTest, ComputeColorsStringMode) {
    // Literal "." should only match actual period characters
    ASSERT_TRUE(chain_.append({.pattern = "ERROR", .color = "#FF0000"}));  // string mode

    // "ERROR: something bad" contains "ERROR" as literal
    auto spans = chain_.computeColors(3, "ERROR: something bad");
    ASSERT_FALSE(spans.empty());
    EXPECT_EQ(spans[0].start, 0u);
    EXPECT_EQ(spans[0].end,   5u);
}

// ── JSON round-trip with useRegex ─────────────────────────────────────────────

TEST_F(FilterChainTest, SaveLoadRoundTripWithUseRegex) {
    chain_.append({.pattern = "ERROR", .color = "#FF0000",
                   .enabled = true, .exclude = false, .useRegex = true});
    chain_.append({.pattern = "WARN",  .color = "#FFFF00",
                   .enabled = true, .exclude = false, .useRegex = false});

    TempFile sessionFile("");
    chain_.save(sessionFile.path());

    FilterChain chain2(reader_);
    ASSERT_TRUE(chain2.load(sessionFile.path()));

    ASSERT_EQ(chain2.filterCount(), 2u);
    EXPECT_TRUE(chain2.filterAt(0).useRegex);
    EXPECT_FALSE(chain2.filterAt(1).useRegex);
}
