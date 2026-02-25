#include "filter_chain.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ── Default color palette ──────────────────────────────────────────────────────

static const char* kPalette[] = {
    "#FF5555",  // Red
    "#55FF55",  // Green
    "#5555FF",  // Blue
    "#FFFF55",  // Yellow
    "#FF55FF",  // Magenta
    "#55FFFF",  // Cyan
    "#FF8800",  // Orange
    "#FF55AA",  // Pink
};
static constexpr size_t kPaletteSize = std::size(kPalette);

// Declared in i_filter_chain.hpp.
const char* defaultColor(size_t filterIndex) {
    return kPalette[filterIndex % kPaletteSize];
}

// ── FilterChain ────────────────────────────────────────────────────────────────

FilterChain::FilterChain(ILogReader& reader)
    : reader_(reader)
    , postFn_([](std::function<void()> fn) { fn(); })  // synchronous default
{}

FilterChain::~FilterChain() {
    cancelFlag_.store(true);
    if (reprocessThread_.joinable())
        reprocessThread_.join();
}

void FilterChain::setPostFn(PostFn fn) {
    postFn_ = fn ? std::move(fn)
                 : [](std::function<void()> f) { f(); };
}

// ── Filter management ──────────────────────────────────────────────────────────

bool FilterChain::append(FilterDef def) {
    try {
        if (def.color.empty())
            def.color = defaultColor(filters_.size());
        FilterNode node;
        node.def      = std::move(def);
        node.compiled = std::regex(node.def.pattern);
        filters_.push_back(std::move(node));
        return true;
    } catch (const std::regex_error&) {
        return false;
    }
}

void FilterChain::remove(size_t index) {
    if (index < filters_.size())
        filters_.erase(filters_.begin() + static_cast<std::ptrdiff_t>(index));
}

bool FilterChain::edit(size_t index, FilterDef def) {
    if (index >= filters_.size()) return false;
    try {
        FilterNode node;
        node.def      = std::move(def);
        node.compiled = std::regex(node.def.pattern);
        node.output   = {};  // Invalidated; reprocess needed
        filters_[index] = std::move(node);
        return true;
    } catch (const std::regex_error&) {
        return false;
    }
}

void FilterChain::moveUp(size_t index) {
    if (index > 0 && index < filters_.size())
        std::swap(filters_[index], filters_[index - 1]);
}

void FilterChain::moveDown(size_t index) {
    if (index + 1 < filters_.size())
        std::swap(filters_[index], filters_[index + 1]);
}

size_t FilterChain::filterCount() const { return filters_.size(); }

const FilterDef& FilterChain::filterAt(size_t index) const {
    return filters_.at(index).def;
}

// ── Query ──────────────────────────────────────────────────────────────────────

size_t FilterChain::filteredLineCount() const {
    if (filters_.empty()) return reader_.lineCount();
    // Find the last enabled filter to use its output
    for (int i = static_cast<int>(filters_.size()) - 1; i >= 0; --i) {
        if (filters_[static_cast<size_t>(i)].def.enabled)
            return filters_[static_cast<size_t>(i)].output.size();
    }
    return reader_.lineCount();
}

size_t FilterChain::filteredLineAt(size_t filteredIndex) const {
    if (filters_.empty()) return filteredIndex + 1;
    for (int i = static_cast<int>(filters_.size()) - 1; i >= 0; --i) {
        if (filters_[static_cast<size_t>(i)].def.enabled) {
            const auto& out = filters_[static_cast<size_t>(i)].output;
            if (filteredIndex < out.size())
                return static_cast<size_t>(out[filteredIndex]);
            return 0;  // Out of range
        }
    }
    return filteredIndex + 1;
}

std::vector<size_t> FilterChain::filteredLines(size_t from, size_t count) const {
    std::vector<size_t> result;
    const size_t total = filteredLineCount();
    if (from >= total || count == 0) return result;

    const size_t end = std::min(from + count, total);
    result.reserve(end - from);
    for (size_t i = from; i < end; ++i)
        result.push_back(filteredLineAt(i));
    return result;
}

std::vector<ColorSpan> FilterChain::computeColors(size_t /*rawLineNo*/,
                                                    std::string_view content) const {
    if (filters_.empty()) return {};

    std::vector<ColorSpan> spans;

    for (const auto& node : filters_) {
        if (!node.def.enabled || node.def.exclude) continue;

        const auto& pat = node.compiled;
        auto it = std::cregex_iterator(content.data(),
                                       content.data() + content.size(), pat);
        auto end = std::cregex_iterator{};
        for (; it != end; ++it) {
            const auto& m = *it;
            if (m.length() == 0) continue;
            ColorSpan span;
            span.start = static_cast<size_t>(m.position());
            span.end   = span.start + static_cast<size_t>(m.length());
            span.color = node.def.color;
            spans.push_back(span);
        }
    }

    // Sort by start position; later filters' spans overwrite earlier ones
    std::sort(spans.begin(), spans.end(),
              [](const ColorSpan& a, const ColorSpan& b) {
                  return a.start < b.start;
              });

    return spans;
}

// ── Internal reprocess helpers ─────────────────────────────────────────────────

// Build output cache for filter `stage` from the previous stage's output.
// Called in background thread with a local copy of the filter nodes.
void FilterChain::buildStage(std::vector<FilterNode>& nodes, size_t stage,
                              const std::vector<std::string_view>& lineViews,
                              const std::atomic<bool>& cancel) {
    auto& node = nodes[stage];
    node.output.clear();

    // Input set: previous stage output, or all lines if first stage
    const std::vector<uint32_t>* inputSet = nullptr;
    std::vector<uint32_t> allLines;

    if (stage == 0) {
        allLines.resize(lineViews.size());
        for (size_t i = 0; i < lineViews.size(); ++i)
            allLines[i] = static_cast<uint32_t>(i + 1);
        inputSet = &allLines;
    } else {
        inputSet = &nodes[stage - 1].output;
    }

    const bool isExclude = node.def.exclude;
    const bool isEnabled = node.def.enabled;

    if (!isEnabled) {
        // Disabled filter: pass through unchanged
        node.output = *inputSet;
        return;
    }

    for (uint32_t rawLineNo : *inputSet) {
        if (cancel.load(std::memory_order_relaxed)) return;

        const size_t idx = rawLineNo - 1;
        if (idx >= lineViews.size()) continue;

        std::string_view line = lineViews[idx];
        bool matched = std::regex_search(line.data(), line.data() + line.size(),
                                          node.compiled);

        if (isExclude) {
            if (!matched) node.output.push_back(rawLineNo);
        } else {
            if (matched) node.output.push_back(rawLineNo);
        }
    }
}

// ── Push path ──────────────────────────────────────────────────────────────────

void FilterChain::processNewLines(size_t firstLine, size_t lastLine) {
    if (isReprocessing_) {
        pendingNewLines_.emplace_back(firstLine, lastLine);
        return;
    }
    processNewLinesImpl(firstLine, lastLine);
}

void FilterChain::processNewLinesImpl(size_t firstLine, size_t lastLine) {
    if (filters_.empty()) return;  // No filters: nothing to cache

    for (size_t lineNo = firstLine; lineNo <= lastLine; ++lineNo) {
        std::string_view content = reader_.getLine(lineNo);

        bool alive = true;
        for (size_t k = 0; k < filters_.size(); ++k) {
            auto& node = filters_[k];
            if (!node.def.enabled) {
                // Pass through: add to this stage if it survived previous stage
                if (alive)
                    node.output.push_back(static_cast<uint32_t>(lineNo));
                continue;
            }

            bool matched = std::regex_search(content.data(),
                                             content.data() + content.size(),
                                             node.compiled);
            if (node.def.exclude) {
                if (matched) { alive = false; break; }
            } else {
                if (!matched) { alive = false; break; }
            }
            if (alive)
                node.output.push_back(static_cast<uint32_t>(lineNo));
        }
    }
}

void FilterChain::reprocess(size_t fromFilter,
                             ProgressCallback onProgress,
                             DoneCallback     onDone) {
    if (fromFilter >= filters_.size() && !filters_.empty()) return;

    // Cancel previous reprocess
    cancelFlag_.store(true);
    if (reprocessThread_.joinable())
        reprocessThread_.join();
    cancelFlag_.store(false);
    isReprocessing_ = true;

    // Snapshot input data in UI thread (safe)
    const size_t totalLines = reader_.lineCount();
    auto anchor = reader_.mmapAnchor();  // Keep mmap alive in background thread

    std::vector<std::string_view> lineViews;
    lineViews.reserve(totalLines);
    for (size_t i = 1; i <= totalLines; ++i)
        lineViews.push_back(reader_.getLine(i));

    // Snapshot filters up to fromFilter (reuse existing caches where possible)
    std::vector<FilterNode> localFilters = filters_;
    // Invalidate caches from fromFilter onward
    for (size_t i = fromFilter; i < localFilters.size(); ++i)
        localFilters[i].output.clear();

    reprocessThread_ = std::thread(
        [this,
         fromFilter,
         totalLines,
         anchor       = std::move(anchor),
         lineViews    = std::move(lineViews),
         localFilters = std::move(localFilters),
         onProgress,
         onDone]() mutable
    {
        // Build each stage from fromFilter onward
        const size_t nFilters = localFilters.size();
        for (size_t k = fromFilter; k < nFilters; ++k) {
            if (cancelFlag_.load()) return;

            buildStage(localFilters, k, lineViews, cancelFlag_);

            if (cancelFlag_.load()) return;

            if (onProgress) {
                double p = (nFilters > 0)
                    ? static_cast<double>(k + 1) / static_cast<double>(nFilters)
                    : 1.0;
                postFn_([onProgress, p]() { onProgress(p); });
            }
        }

        if (cancelFlag_.load()) return;

        // Post results to UI thread
        postFn_([this,
                 localFilters = std::move(localFilters),
                 totalLines,
                 onDone]() mutable
        {
            filters_ = std::move(localFilters);
            isReprocessing_ = false;

            // Process any lines that arrived while we were reprocessing
            for (auto [f, l] : pendingNewLines_)
                processNewLinesImpl(f, l);
            pendingNewLines_.clear();

            if (onDone) onDone();
        });
    });
}

// ── Persistence ────────────────────────────────────────────────────────────────

void FilterChain::reset() {
    for (auto& f : filters_)
        f.output.clear();
}

void FilterChain::waitReprocess() {
    if (reprocessThread_.joinable())
        reprocessThread_.join();
}

void FilterChain::save(std::string_view path) const {
    json j;
    j["version"] = 1;
    json arr = json::array();
    for (const auto& node : filters_) {
        json f;
        f["pattern"] = node.def.pattern;
        f["color"]   = node.def.color;
        f["enabled"] = node.def.enabled;
        f["exclude"] = node.def.exclude;
        arr.push_back(std::move(f));
    }
    j["filters"] = std::move(arr);

    std::string pathStr{path};
    std::ofstream out{pathStr};
    if (out) out << j.dump(2);
}

bool FilterChain::load(std::string_view path) {
    try {
        std::string pathStr{path};
        std::ifstream in{pathStr};
        if (!in) return false;

        json j;
        in >> j;

        if (!j.contains("version") || j["version"] != 1) return false;
        if (!j.contains("filters") || !j["filters"].is_array()) return false;

        std::vector<FilterNode> newFilters;
        for (const auto& f : j["filters"]) {
            FilterDef def;
            def.pattern = f.value("pattern", "");
            def.color   = f.value("color",   "#FF5555");
            def.enabled = f.value("enabled", true);
            def.exclude = f.value("exclude", false);

            FilterNode node;
            node.def      = std::move(def);
            node.compiled = std::regex(node.def.pattern);  // May throw
            newFilters.push_back(std::move(node));
        }

        filters_ = std::move(newFilters);

        // Read optional session fields
        sessionLastFile_ = j.value("lastFile", "");
        const std::string modeStr = j.value("mode", "static");
        sessionMode_ = (modeStr == "realtime") ? FileMode::Realtime : FileMode::Static;

        return true;
    } catch (const std::regex_error&) {
        // Invalid regex pattern in saved filters
        return false;
    } catch (const nlohmann::json::exception&) {
        // JSON parsing error
        return false;
    } catch (const std::exception&) {
        // Other standard exceptions (file I/O, etc.)
        return false;
    }
}

void FilterChain::save(std::string_view path,
                        std::string_view lastFile,
                        FileMode         mode) const {
    // Ensure parent directory exists
    namespace fs = std::filesystem;
    const fs::path filePath{std::string{path}};
    const auto parent = filePath.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        // Continue even if create_directories fails (e.g., path already exists)
    }

    json j;
    j["version"]  = 1;
    j["lastFile"] = std::string{lastFile};
    j["mode"]     = (mode == FileMode::Realtime) ? "realtime" : "static";

    json arr = json::array();
    for (const auto& node : filters_) {
        json f;
        f["pattern"] = node.def.pattern;
        f["color"]   = node.def.color;
        f["enabled"] = node.def.enabled;
        f["exclude"] = node.def.exclude;
        arr.push_back(std::move(f));
    }
    j["filters"] = std::move(arr);

    std::ofstream out{std::string{path}};
    if (out) out << j.dump(2);
}
