#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

namespace fs = std::filesystem;

// Helper: unique temp path for session JSON (not a real file yet)
static std::string tempSessionPath() {
    static std::atomic<int> counter{0};
    return (fs::temp_directory_path()
            / ("ttlv_session_" + std::to_string(counter++) + ".json")).string();
}

// ── Save and load round-trip ──────────────────────────────────────────────────

TEST(SessionTest, FiltersRoundTrip) {
    TempFile logFile("a\nb\nc\n");
    LogReader reader;
    reader.open(logFile.path());
    waitForIndexing(reader);

    FilterChain chain(reader);
    chain.append({ .pattern = "a",  .color = "#FF5555", .enabled = true,  .exclude = false });
    chain.append({ .pattern = "b",  .color = "#55FF55", .enabled = false, .exclude = true  });

    const std::string sPath = tempSessionPath();
    chain.save(sPath, logFile.path(), FileMode::Static);

    // Load into a fresh chain
    LogReader  reader2;
    FilterChain chain2(reader2);
    ASSERT_TRUE(chain2.load(sPath));

    EXPECT_EQ(chain2.filterCount(), 2u);
    EXPECT_EQ(chain2.filterAt(0).pattern, "a");
    EXPECT_EQ(chain2.filterAt(0).color,   "#FF5555");
    EXPECT_TRUE(chain2.filterAt(0).enabled);
    EXPECT_FALSE(chain2.filterAt(0).exclude);

    EXPECT_EQ(chain2.filterAt(1).pattern, "b");
    EXPECT_EQ(chain2.filterAt(1).color,   "#55FF55");
    EXPECT_FALSE(chain2.filterAt(1).enabled);
    EXPECT_TRUE(chain2.filterAt(1).exclude);

    fs::remove(sPath);
}

TEST(SessionTest, EmptyChainRoundTrip) {
    LogReader  reader;
    FilterChain chain(reader);

    const std::string sPath = tempSessionPath();
    chain.save(sPath, "/some/file.log", FileMode::Realtime);

    LogReader  reader2;
    FilterChain chain2(reader2);
    ASSERT_TRUE(chain2.load(sPath));
    EXPECT_EQ(chain2.filterCount(), 0u);

    fs::remove(sPath);
}

TEST(SessionTest, LastFileRestoredFromSession) {
    LogReader  reader;
    FilterChain chain(reader);

    const std::string sPath = tempSessionPath();
    chain.save(sPath, "/path/to/my.log", FileMode::Static);

    LogReader  reader2;
    FilterChain chain2(reader2);
    ASSERT_TRUE(chain2.load(sPath));
    EXPECT_EQ(chain2.sessionLastFile(), "/path/to/my.log");

    fs::remove(sPath);
}

TEST(SessionTest, StaticModeRestoredFromSession) {
    LogReader  reader;
    FilterChain chain(reader);

    const std::string sPath = tempSessionPath();
    chain.save(sPath, "file.log", FileMode::Static);

    LogReader  reader2;
    FilterChain chain2(reader2);
    ASSERT_TRUE(chain2.load(sPath));
    EXPECT_EQ(chain2.sessionMode(), FileMode::Static);

    fs::remove(sPath);
}

TEST(SessionTest, RealtimeModeRestoredFromSession) {
    LogReader  reader;
    FilterChain chain(reader);

    const std::string sPath = tempSessionPath();
    chain.save(sPath, "file.log", FileMode::Realtime);

    LogReader  reader2;
    FilterChain chain2(reader2);
    ASSERT_TRUE(chain2.load(sPath));
    EXPECT_EQ(chain2.sessionMode(), FileMode::Realtime);

    fs::remove(sPath);
}

TEST(SessionTest, CorruptJsonReturnsFalse) {
    const std::string sPath = tempSessionPath();
    {
        std::ofstream f{sPath};
        f << "{ not valid json !!! ";
    }

    LogReader  reader;
    FilterChain chain(reader);
    EXPECT_FALSE(chain.load(sPath));
    EXPECT_EQ(chain.filterCount(), 0u);

    fs::remove(sPath);
}

TEST(SessionTest, NonExistentFileReturnsFalse) {
    LogReader  reader;
    FilterChain chain(reader);
    EXPECT_FALSE(chain.load("/no/such/path/session.json"));
    EXPECT_EQ(chain.filterCount(), 0u);
}

TEST(SessionTest, SaveCreatesParentDirectory) {
    // Use a nested path that does not exist yet
    const std::string dir = (fs::temp_directory_path()
                             / ("ttlv_sess_dir_" + std::to_string(
                                   std::chrono::steady_clock::now().time_since_epoch().count()))
                             / "sub").string();
    const std::string sPath = dir + "/session.json";

    LogReader  reader;
    FilterChain chain(reader);
    // Should not throw, should create the directory
    EXPECT_NO_THROW(chain.save(sPath, "file.log", FileMode::Static));
    EXPECT_TRUE(fs::exists(sPath));

    // Cleanup
    fs::remove_all(fs::path(dir).parent_path());
}

// ── E2E: session restore → continue working ───────────────────────────────────
//
// Simulate a two-session workflow:
//   Session A: open file, add 2 filters, save session, exit
//   Session B: load session, verify filters, add a 3rd filter, reprocess
//
// This validates that the loaded chain can still be used for filtering after
// session restore — not just round-trip serialisation.

TEST(SessionTest, SessionRestoreAndContinueWorking) {
    // --- Session A ---
    TempFile logFile("DEBUG init\nINFO start\nERROR fail\nINFO ok\nERROR done\n");
    LogReader readerA;
    readerA.open(logFile.path());
    waitForIndexing(readerA);

    FilterChain chainA(readerA);
    ASSERT_TRUE(chainA.append({ .pattern = "ERROR" }));
    {
        std::promise<void> done;
        chainA.reprocess(0, nullptr, [&]{ done.set_value(); });
        waitForReprocess(done);
    }
    ASSERT_EQ(chainA.filteredLineCount(), 2u);  // lines 3 and 5

    const std::string sPath = tempSessionPath();
    chainA.save(sPath, logFile.path(), FileMode::Static);

    // --- Session B ---
    LogReader readerB;
    readerB.open(logFile.path());
    waitForIndexing(readerB);

    FilterChain chainB(readerB);
    ASSERT_TRUE(chainB.load(sPath));

    // Restored filters and file path
    ASSERT_EQ(chainB.filterCount(), 1u);
    EXPECT_EQ(chainB.filterAt(0).pattern, "ERROR");
    EXPECT_EQ(chainB.sessionLastFile(), logFile.path());

    // Reprocess with the loaded filter — same results as session A
    {
        std::promise<void> done;
        chainB.reprocess(0, nullptr, [&]{ done.set_value(); });
        waitForReprocess(done);
    }
    EXPECT_EQ(chainB.filteredLineCount(), 2u);

    // Add a second filter (exclude "done") and reprocess — chain now: ERROR ∩ ¬done
    ASSERT_TRUE(chainB.append({ .pattern = "done", .exclude = true }));
    {
        std::promise<void> done;
        chainB.reprocess(1, nullptr, [&]{ done.set_value(); });
        waitForReprocess(done);
    }
    // "ERROR fail" and "ERROR done" → exclude "done" removes "ERROR done"
    EXPECT_EQ(chainB.filteredLineCount(), 1u);

    // Save updated session and verify it round-trips correctly
    chainB.save(sPath, logFile.path(), FileMode::Static);
    {
        LogReader readerC;
        FilterChain chainC(readerC);
        ASSERT_TRUE(chainC.load(sPath));
        EXPECT_EQ(chainC.filterCount(), 2u);
        EXPECT_EQ(chainC.filterAt(0).pattern, "ERROR");
        EXPECT_EQ(chainC.filterAt(1).pattern, "done");
        EXPECT_TRUE(chainC.filterAt(1).exclude);
    }

    fs::remove(sPath);
}

// ── E2E: session with empty filter chain still restores file info ─────────────

TEST(SessionTest, EmptyChainSessionRestoresFileOnly) {
    TempFile logFile("alpha\nbeta\n");
    LogReader reader;
    reader.open(logFile.path());
    waitForIndexing(reader);

    FilterChain chain(reader);
    // Save with no filters
    const std::string sPath = tempSessionPath();
    chain.save(sPath, logFile.path(), FileMode::Realtime);

    LogReader reader2;
    FilterChain chain2(reader2);
    ASSERT_TRUE(chain2.load(sPath));

    EXPECT_EQ(chain2.filterCount(), 0u);
    EXPECT_EQ(chain2.sessionLastFile(), logFile.path());
    EXPECT_EQ(chain2.sessionMode(), FileMode::Realtime);

    fs::remove(sPath);
}
