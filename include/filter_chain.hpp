#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "log_reader.hpp"

// ── Data structures ────────────────────────────────────────────────────────────

struct FilterDef {
    std::string pattern;
    std::string color;    // "#RRGGBB"
    bool        enabled = true;
    bool        exclude = false;
};

// Byte-offset color segment for a single log line.
// ASCII regex guarantees offsets land on valid UTF-8 character boundaries.
struct ColorSpan {
    size_t      start;
    size_t      end;
    std::string color;  // "#RRGGBB"
};

// Stage cache node: definition + precompiled regex + surviving line numbers.
// filters_[k].output holds 1-based raw line numbers that survive filters 0..k.
struct FilterNode {
    FilterDef             def;
    std::regex            compiled;
    std::vector<uint32_t> output;
};

using ProgressCallback = std::function<void(double)>;   // 0.0 ~ 1.0
using DoneCallback     = std::function<void()>;

// ── FilterChain ────────────────────────────────────────────────────────────────

// Maintains a chain of regex filters with per-stage output caches.
// All public methods must be called from the UI thread only.
class FilterChain {
public:
    explicit FilterChain(LogReader& reader);
    ~FilterChain();

    FilterChain(const FilterChain&)            = delete;
    FilterChain& operator=(const FilterChain&) = delete;

    // ── Filter management ───────────────────────────────────────────────────
    // Returns false if the pattern is not a valid regex.
    bool append(FilterDef def);
    void remove(size_t index);         // 0-based
    bool edit(size_t index, FilterDef def);
    void moveUp(size_t index);
    void moveDown(size_t index);

    size_t           filterCount()   const;
    const FilterDef& filterAt(size_t index) const;

    // ── Query ────────────────────────────────────────────────────────────────
    // Results from the last filter stage (or all raw lines when chain is empty).
    size_t              filteredLineCount()                              const;
    size_t              filteredLineAt(size_t filteredIndex)            const;  // 1-based raw line no.
    std::vector<size_t> filteredLines(size_t from, size_t count)        const;

    // Dynamic color computation – call only for visible lines, never cached.
    std::vector<ColorSpan> computeColors(size_t rawLineNo,
                                         std::string_view content)     const;

    // ── Push path ────────────────────────────────────────────────────────────
    // Called in UI thread when LogReader reports new lines (Phase 2).
    void processNewLines(size_t firstLine, size_t lastLine);

    // Async full recompute starting from fromFilter (0-based).
    // Runs in background; calls onDone via PostEvent when finished (Phase 2).
    void reprocess(size_t fromFilter,
                   ProgressCallback onProgress,
                   DoneCallback     onDone);

    void reset();   // Clear all caches; keep filter definitions

    void save(std::string_view path) const;
    bool load(std::string_view path);

private:
    LogReader&            reader_;
    std::vector<FilterNode> filters_;
};
