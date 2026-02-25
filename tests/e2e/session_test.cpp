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
