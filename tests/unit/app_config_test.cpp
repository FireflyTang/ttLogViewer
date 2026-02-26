#include <gtest/gtest.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include "app_config.hpp"

namespace fs = std::filesystem;

// Helper: write a temp JSON file and return its path
static std::string writeTempJson(const std::string& content) {
    static std::atomic<int> counter{0};
    const std::string path = (fs::temp_directory_path()
        / ("ttlv_cfg_" + std::to_string(counter++) + ".json")).string();
    std::ofstream f{path};
    f << content;
    return path;
}

// ── Default values ────────────────────────────────────────────────────────────

TEST(AppConfig, DefaultValues) {
    AppConfig cfg;  // freshly default-constructed, global not involved
    EXPECT_EQ(cfg.uiOverheadRows,             6);
    EXPECT_EQ(cfg.dialogMaxWidth,             60);
    EXPECT_EQ(cfg.defaultTerminalWidth,       80);
    EXPECT_EQ(cfg.watcherTickCount,           50);
    EXPECT_EQ(cfg.watcherTickIntervalMs,      10);
    EXPECT_EQ(cfg.searchReserveFraction,      10);
    EXPECT_EQ(cfg.searchReserveMax,           size_t(10000));
    EXPECT_EQ(cfg.jsonIndent,                 2);
    // Navigation / layout defaults added in refactor
    EXPECT_EQ(cfg.hScrollStep,               4);
    EXPECT_EQ(cfg.minLineNoWidth,            6);
    EXPECT_EQ(cfg.reprocessTimeoutSeconds,   30);
    EXPECT_DOUBLE_EQ(cfg.rawPaneFraction,    0.6);
}

// ── loadFromFile overrides specified fields ────────────────────────────────────

TEST(AppConfig, LoadFromJsonOverridesFields) {
    const std::string p = writeTempJson(R"({
        "dialogMaxWidth": 80,
        "jsonIndent": 4
    })");

    AppConfig cfg;
    ASSERT_TRUE(cfg.loadFromFile(p));

    EXPECT_EQ(cfg.dialogMaxWidth, 80);
    EXPECT_EQ(cfg.jsonIndent,      4);
    // Other fields keep their defaults
    EXPECT_EQ(cfg.uiOverheadRows, 6);

    fs::remove(p);
}

TEST(AppConfig, LoadNewNavigationAndLayoutFields) {
    const std::string p = writeTempJson(R"({
        "hScrollStep":             8,
        "minLineNoWidth":          4,
        "reprocessTimeoutSeconds": 60,
        "rawPaneFraction":         0.5
    })");

    AppConfig cfg;
    ASSERT_TRUE(cfg.loadFromFile(p));

    EXPECT_EQ(cfg.hScrollStep,              8);
    EXPECT_EQ(cfg.minLineNoWidth,           4);
    EXPECT_EQ(cfg.reprocessTimeoutSeconds,  60);
    EXPECT_DOUBLE_EQ(cfg.rawPaneFraction,   0.5);
    // Unrelated fields keep their defaults
    EXPECT_EQ(cfg.uiOverheadRows,           6);
    EXPECT_EQ(cfg.jsonIndent,               2);

    fs::remove(p);
}

// ── Partial JSON preserves unspecified defaults ────────────────────────────────

TEST(AppConfig, PartialJsonKeepsUnspecifiedDefaults) {
    const std::string p = writeTempJson(R"({"watcherTickCount": 100})");

    AppConfig cfg;
    ASSERT_TRUE(cfg.loadFromFile(p));

    EXPECT_EQ(cfg.watcherTickCount, 100);
    // Unspecified fields must still equal defaults
    EXPECT_EQ(cfg.uiOverheadRows,  6);
    EXPECT_EQ(cfg.dialogMaxWidth,  60);
    EXPECT_EQ(cfg.jsonIndent,      2);

    fs::remove(p);
}

// ── Corrupt JSON returns false and keeps defaults ──────────────────────────────

TEST(AppConfig, CorruptJsonKeepsDefaults) {
    const std::string p = writeTempJson("{ not valid json !!!");

    AppConfig cfg;
    EXPECT_FALSE(cfg.loadFromFile(p));
    // All fields must still equal defaults
    EXPECT_EQ(cfg.uiOverheadRows,  6);
    EXPECT_EQ(cfg.dialogMaxWidth,  60);

    fs::remove(p);
}

// ── Missing file returns false and keeps defaults ─────────────────────────────

TEST(AppConfig, MissingFileKeepsDefaults) {
    AppConfig cfg;
    EXPECT_FALSE(cfg.loadFromFile("/no/such/path/config.json"));
    EXPECT_EQ(cfg.uiOverheadRows,  6);
    EXPECT_EQ(cfg.dialogMaxWidth,  60);
    EXPECT_EQ(cfg.searchReserveMax, size_t(10000));
}
