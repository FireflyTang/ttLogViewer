#include "filter_chain.hpp"

#include <algorithm>
#include <stdexcept>

// ── FilterChain ────────────────────────────────────────────────────────────────

FilterChain::FilterChain(LogReader& reader) : reader_(reader) {}
FilterChain::~FilterChain() = default;

// ── Filter management ──────────────────────────────────────────────────────────

bool FilterChain::append(FilterDef def) {
    try {
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
        filters_[index].def      = std::move(def);
        filters_[index].compiled = std::regex(filters_[index].def.pattern);
        filters_[index].output.clear();
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

// Phase 1: no filters implemented – identity pass-through.
// Phase 2: return filters_.back().output.size() (or reader lineCount when empty).
size_t FilterChain::filteredLineCount() const {
    return reader_.lineCount();
}

size_t FilterChain::filteredLineAt(size_t filteredIndex) const {
    // Phase 1: identity mapping; filteredIndex is 0-based, return 1-based raw line no.
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

// Phase 1: no color computation; returns empty span list.
std::vector<ColorSpan> FilterChain::computeColors(size_t /*rawLineNo*/,
                                                    std::string_view /*content*/) const {
    return {};
}

// ── Push path ──────────────────────────────────────────────────────────────────

void FilterChain::processNewLines(size_t /*firstLine*/, size_t /*lastLine*/) {
    // Phase 1: no-op. Phase 2: incremental filter pass.
}

void FilterChain::reprocess(size_t /*fromFilter*/,
                             ProgressCallback /*onProgress*/,
                             DoneCallback     /*onDone*/) {
    // Phase 1: no-op. Phase 2: background thread implementation.
}

// ── Persistence ────────────────────────────────────────────────────────────────

void FilterChain::reset() {
    for (auto& f : filters_)
        f.output.clear();
}

void FilterChain::save(std::string_view /*path*/) const {
    // Phase 2: JSON serialization.
}

bool FilterChain::load(std::string_view /*path*/) {
    // Phase 2: JSON deserialization.
    return false;
}
