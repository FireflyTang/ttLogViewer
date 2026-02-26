#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// ── Shared data structures ─────────────────────────────────────────────────────

struct FilterDef {
    std::string pattern;
    std::string color    = {};   // "#RRGGBB"; empty → filled by FilterChain
    bool        enabled  = true;
    bool        exclude  = false;
    bool        useRegex = false;  // false = literal string; true = regex
};

// Byte-offset color segment for a single log line.
struct ColorSpan {
    size_t      start;
    size_t      end;
    std::string color;  // "#RRGGBB"
};

using ProgressCallback = std::function<void(double)>;   // 0.0 ~ 1.0
using DoneCallback     = std::function<void()>;

// Size of the default color palette.
constexpr size_t kDefaultColorPaletteSize = 8;

// Returns the default "#RRGGBB" color string for a filter at the given index.
// Cycles through a palette of kDefaultColorPaletteSize colors.
// Defined in filter_chain.cpp.
const char* defaultColor(size_t filterIndex);

// ── Abstract interface ─────────────────────────────────────────────────────────

// Abstract interface for the filter chain.
// Thread safety: All public methods must be called from the UI thread only.
// NOT thread-safe for concurrent calls from multiple threads.
// The implementation uses background threads internally but synchronizes
// results back to the UI thread via PostFn callbacks.
class IFilterChain {
public:
    virtual ~IFilterChain() = default;

    // ── Filter management ────────────────────────────────────────────────────
    virtual bool append(FilterDef def) = 0;
    virtual void remove(size_t index)  = 0;
    virtual bool edit(size_t index, FilterDef def) = 0;
    virtual void moveUp(size_t index)   = 0;
    virtual void moveDown(size_t index) = 0;

    virtual size_t           filterCount()            const = 0;
    virtual const FilterDef& filterAt(size_t index)   const = 0;

    virtual void   toggleUseRegex(size_t idx)           = 0;
    virtual size_t filteredLineCountAt(size_t idx) const = 0;

    // ── Query ────────────────────────────────────────────────────────────────
    virtual size_t              filteredLineCount()                          const = 0;
    virtual size_t              filteredLineAt(size_t filteredIndex)         const = 0;
    virtual std::vector<size_t> filteredLines(size_t from, size_t count)    const = 0;

    virtual std::vector<ColorSpan> computeColors(size_t rawLineNo,
                                                  std::string_view content) const = 0;

    // ── Push path ────────────────────────────────────────────────────────────
    virtual void processNewLines(size_t firstLine, size_t lastLine) = 0;

    virtual void reprocess(size_t fromFilter,
                           ProgressCallback onProgress,
                           DoneCallback     onDone) = 0;

    virtual void reset() = 0;

    virtual void save(std::string_view path) const = 0;
    virtual bool load(std::string_view path) = 0;
};
