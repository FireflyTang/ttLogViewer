#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "i_filter_chain.hpp"
#include "i_log_reader.hpp"

// Stage cache node: definition + precompiled regex + surviving line numbers.
// filters_[k].output holds 1-based raw line numbers that survive filters 0..k.
struct FilterNode {
    FilterDef             def;
    std::regex            compiled;
    std::vector<uint32_t> output;
};

// ── FilterChain ────────────────────────────────────────────────────────────────

// Maintains a chain of regex filters with per-stage output caches.
// All public methods must be called from the UI thread only.
class FilterChain : public IFilterChain {
public:
    explicit FilterChain(ILogReader& reader);
    ~FilterChain() override;

    FilterChain(const FilterChain&)            = delete;
    FilterChain& operator=(const FilterChain&) = delete;

    // Inject post-to-UI function (same PostFn as LogReader).
    void setPostFn(PostFn fn);

    // ── Filter management ────────────────────────────────────────────────────
    bool append(FilterDef def)               override;
    void remove(size_t index)                override;
    bool edit(size_t index, FilterDef def)   override;
    void moveUp(size_t index)                override;
    void moveDown(size_t index)              override;

    size_t           filterCount()            const override;
    const FilterDef& filterAt(size_t index)   const override;

    void   toggleUseRegex(size_t idx)           override;
    size_t filteredLineCountAt(size_t idx) const override;

    // ── Query ────────────────────────────────────────────────────────────────
    size_t              filteredLineCount()                          const override;
    size_t              filteredLineAt(size_t filteredIndex)         const override;
    std::vector<size_t> filteredLines(size_t from, size_t count)    const override;

    std::vector<ColorSpan> computeColors(size_t rawLineNo,
                                          std::string_view content) const override;

    // ── Push path ────────────────────────────────────────────────────────────
    void processNewLines(size_t firstLine, size_t lastLine) override;

    void reprocess(size_t fromFilter,
                   ProgressCallback onProgress,
                   DoneCallback     onDone) override;

    void reset() override;

    void save(std::string_view path) const override;
    bool load(std::string_view path) override;

    // Session-aware save: also records the last opened file and its mode.
    // Automatically creates the parent directory if it does not exist.
    void save(std::string_view path,
              std::string_view lastFile,
              FileMode         mode) const;

    // Session fields read back from the last successful load().
    std::string_view sessionLastFile() const { return sessionLastFile_; }
    FileMode         sessionMode()     const { return sessionMode_;     }

    // Block until the current reprocess thread finishes.
    // Intended for use in tests with a synchronous postFn.
    void waitReprocess();

private:
    ILogReader&             reader_;
    std::vector<FilterNode> filters_;
    PostFn                  postFn_;

    // Session fields (populated by load())
    std::string sessionLastFile_;
    FileMode    sessionMode_ = FileMode::Static;

    // Reprocess state (UI thread only)
    bool isReprocessing_ = false;
    std::vector<std::pair<size_t, size_t>> pendingNewLines_;

    // Background reprocess thread
    std::thread       reprocessThread_;
    std::atomic<bool> cancelFlag_{false};

    // Incremental helper (called in UI thread)
    void processNewLinesImpl(size_t firstLine, size_t lastLine);

    // Build output for filter at `stage` from the previous stage's output.
    // Called in background thread with a local copy.
    static void buildStage(std::vector<FilterNode>& nodes, size_t stage,
                           const std::vector<std::string_view>& lineViews,
                           const std::atomic<bool>& cancel);
};
