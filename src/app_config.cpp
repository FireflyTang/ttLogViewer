#include "app_config.hpp"

#include <cstdlib>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ── Global singleton ──────────────────────────────────────────────────────────

static AppConfig s_config;

const AppConfig& AppConfig::global() {
    return s_config;
}

// ── Instance loading ──────────────────────────────────────────────────────────

bool AppConfig::loadFromFile(const std::string& path) {
    std::ifstream f{path};
    if (!f) return false;

    try {
        json j;
        f >> j;

        // Override only fields that are explicitly present in the JSON.
        // j.value(key, default) returns the default when the key is absent or
        // has the wrong type, so partial config files work correctly.
        uiOverheadRows        = j.value("uiOverheadRows",        uiOverheadRows);
        dialogMaxWidth        = j.value("dialogMaxWidth",         dialogMaxWidth);
        defaultTerminalWidth  = j.value("defaultTerminalWidth",   defaultTerminalWidth);
        watcherTickCount      = j.value("watcherTickCount",       watcherTickCount);
        watcherTickIntervalMs = j.value("watcherTickIntervalMs",  watcherTickIntervalMs);
        searchReserveFraction = j.value("searchReserveFraction",  searchReserveFraction);
        searchReserveMax      = j.value("searchReserveMax",       searchReserveMax);
        jsonIndent            = j.value("jsonIndent",             jsonIndent);

        return true;
    } catch (const nlohmann::json::exception&) {
        return false;  // Invalid JSON – leave this unchanged
    } catch (const std::exception&) {
        return false;  // Other I/O errors – leave this unchanged
    }
}

// ── Default config path ───────────────────────────────────────────────────────

static std::string defaultConfigPath() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
    const std::string base = home ? home : ".";
    return base + "\\.ttlogviewer.json";
#else
    const char* home = std::getenv("HOME");
    const std::string base = home ? home : ".";
    return base + "/.ttlogviewer.json";
#endif
}

// ── loadGlobal ────────────────────────────────────────────────────────────────

void AppConfig::loadGlobal(const std::string& path) {
    const std::string filePath = path.empty() ? defaultConfigPath() : path;
    s_config.loadFromFile(filePath);
}
