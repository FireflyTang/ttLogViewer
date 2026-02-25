#pragma once
#include <string>

// ── AppConfig ─────────────────────────────────────────────────────────────────
//
// Central home for every tunable constant in ttLogViewer.
// All fields carry sensible defaults and can be overridden at startup by
// a JSON config file (see loadGlobal()).
//
// JSON schema example (~/.ttlogviewer.json or %USERPROFILE%\.ttlogviewer.json):
//
//   {
//     "uiOverheadRows":        6,
//     "dialogMaxWidth":        60,
//     "defaultTerminalWidth":  80,
//     "watcherTickCount":      50,
//     "watcherTickIntervalMs": 10,
//     "searchReserveFraction": 10,
//     "searchReserveMax":      10000,
//     "jsonIndent":            2
//   }
//
// Only the fields present in the file are overridden; absent fields keep their
// default values.  An absent or unparseable file is silently ignored.

struct AppConfig {
    // ── UI layout ─────────────────────────────────────────────────────────────
    // Number of non-log rows consumed by the TUI chrome:
    //   status bar (1) + filter bar (1) + input line (1) + dividers (3) = 6
    int uiOverheadRows = 6;

    // Maximum column width of the modal dialog box.
    int dialogMaxWidth = 60;

    // Assumed terminal width before the first resize event is received.
    int defaultTerminalWidth = 80;

    // ── File watcher (realtime mode) ──────────────────────────────────────────
    // The watcher thread sleeps in fine-grained ticks so it can stop promptly.
    // Total poll interval ≈ watcherTickCount × watcherTickIntervalMs ms.
    int watcherTickCount      = 50;  // iterations per check cycle
    int watcherTickIntervalMs = 10;  // ms per tick  (50 × 10 ms = 500 ms)

    // ── Search ────────────────────────────────────────────────────────────────
    // Reserve 1/searchReserveFraction of total lines for the search result
    // vector, capped at searchReserveMax to avoid over-allocation on huge files.
    int    searchReserveFraction = 10;
    size_t searchReserveMax      = 10'000;

    // ── Session / export ──────────────────────────────────────────────────────
    // Number of spaces used for JSON pretty-printing.
    int jsonIndent = 2;

    // ── Instance loading ──────────────────────────────────────────────────────

    // Loads the config file at `path` and overrides fields that are present in
    // the JSON.  Fields absent from the JSON are left unchanged (defaults kept).
    // Returns true if the file was read and parsed successfully.
    // Returns false (and leaves this unchanged) if the file is missing or
    // contains invalid JSON.
    bool loadFromFile(const std::string& path);

    // ── Global singleton access ───────────────────────────────────────────────

    // Returns the process-wide AppConfig instance.
    static const AppConfig& global();

    // Loads the config file at `path` and applies it to the global instance.
    // If `path` is empty the default per-user config path is used:
    //   Windows : %USERPROFILE%\.ttlogviewer.json
    //   Others  : $HOME/.ttlogviewer.json
    // A missing file or JSON parse error leaves the global instance unchanged
    // (all defaults are preserved).
    static void loadGlobal(const std::string& path = "");
};
