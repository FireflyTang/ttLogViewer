#include "app_controller.hpp"

#include <algorithm>
#include <string>

// ── AppController ──────────────────────────────────────────────────────────────

AppController::AppController(LogReader& reader, FilterChain& chain)
    : reader_(reader), chain_(chain) {}

AppController::~AppController() = default;

// ── Private helpers ────────────────────────────────────────────────────────────

PaneState& AppController::activeState() {
    return (focus_ == FocusArea::Raw) ? rawState_ : filteredState_;
}

const PaneState& AppController::activeState() const {
    return (focus_ == FocusArea::Raw) ? rawState_ : filteredState_;
}

size_t AppController::activeLineCount() const {
    if (focus_ == FocusArea::Raw)
        return reader_.lineCount();
    return chain_.filteredLineCount();
}

// Clamp scrollOffset so that cursor is always within the visible window.
void AppController::clampScroll(PaneState& ps, size_t totalLines, int paneHeight) {
    if (totalLines == 0) {
        ps.cursor       = 0;
        ps.scrollOffset = 0;
        return;
    }

    // Clamp cursor
    if (ps.cursor >= totalLines)
        ps.cursor = totalLines - 1;

    const size_t ph = static_cast<size_t>(std::max(1, paneHeight));

    // Scroll up if cursor is above viewport
    if (ps.cursor < ps.scrollOffset)
        ps.scrollOffset = ps.cursor;

    // Scroll down if cursor is below viewport
    if (ps.cursor >= ps.scrollOffset + ph)
        ps.scrollOffset = ps.cursor - ph + 1;

    // Clamp scrollOffset
    if (totalLines > ph) {
        if (ps.scrollOffset > totalLines - ph)
            ps.scrollOffset = totalLines - ph;
    } else {
        ps.scrollOffset = 0;
    }
}

void AppController::moveCursor(int delta, int paneHeight) {
    PaneState& ps  = activeState();
    size_t total   = activeLineCount();
    if (total == 0) return;

    if (delta < 0) {
        size_t step = static_cast<size_t>(-delta);
        ps.cursor   = (ps.cursor >= step) ? ps.cursor - step : 0;
    } else {
        ps.cursor = std::min(ps.cursor + static_cast<size_t>(delta), total - 1);
    }

    clampScroll(ps, total, paneHeight);
}

// ── Key handling ───────────────────────────────────────────────────────────────

bool AppController::handleKey(const ftxui::Event& event) {
    switch (inputMode_) {
        case InputMode::None:
            return handleKeyNone(event);
        default:
            // Phase 2 will add handlers for other modes
            return false;
    }
}

bool AppController::handleKeyNone(const ftxui::Event& event) {
    using E = ftxui::Event;

    const int rph = lastRawPaneHeight_;
    const int fph = lastFilteredPaneHeight_;
    const int activePh = (focus_ == FocusArea::Raw) ? rph : fph;

    // Navigation
    if (event == E::ArrowUp)   { moveCursor(-1, activePh); return true; }
    if (event == E::ArrowDown) { moveCursor(+1, activePh); return true; }

    if (event == E::PageUp) {
        moveCursor(-std::max(1, activePh - 1), activePh);
        return true;
    }
    if (event == E::PageDown) {
        moveCursor(+std::max(1, activePh - 1), activePh);
        return true;
    }

    if (event == E::Home) {
        PaneState& ps = activeState();
        ps.cursor     = 0;
        ps.scrollOffset = 0;
        return true;
    }
    if (event == E::End) {
        size_t total = activeLineCount();
        if (total > 0) {
            PaneState& ps   = activeState();
            ps.cursor       = total - 1;
            clampScroll(ps, total, activePh);
        }
        return true;
    }

    // Focus switch (Tab or Shift+Tab)
    if (event == E::Tab || event == E::TabReverse) {
        focus_ = (focus_ == FocusArea::Raw) ? FocusArea::Filtered : FocusArea::Raw;
        return true;
    }

    return false;
}

// ── getViewData ────────────────────────────────────────────────────────────────

ViewData AppController::getViewData(int rawPaneHeight, int filteredPaneHeight) const {
    lastRawPaneHeight_      = std::max(1, rawPaneHeight);
    lastFilteredPaneHeight_ = std::max(1, filteredPaneHeight);

    ViewData data;
    data.fileName    = std::string(reader_.filePath());
    data.mode        = (reader_.mode() == FileMode::Static) ? AppMode::Static : AppMode::Realtime;
    data.totalLines  = reader_.lineCount();
    data.isIndexing  = reader_.isIndexing();
    data.rawFocused  = (focus_ == FocusArea::Raw);
    data.filteredFocused = !data.rawFocused;
    data.inputMode   = inputMode_;

    // ── Raw pane ─────────────────────────────────────────────────────────────
    {
        const size_t total = reader_.lineCount();
        const size_t ph    = static_cast<size_t>(lastRawPaneHeight_);

        // Ensure scroll is still valid
        PaneState& rps = const_cast<AppController*>(this)->rawState_;
        const_cast<AppController*>(this)->clampScroll(rps, total, lastRawPaneHeight_);

        const size_t first = rps.scrollOffset;
        const size_t count = (total > first) ? std::min(ph, total - first) : 0;

        data.rawPane.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const size_t lineNo = first + i + 1;  // 1-based
            LogLine ll;
            ll.rawLineNo   = lineNo;
            ll.content     = reader_.getLine(lineNo);
            ll.colors      = chain_.computeColors(lineNo, ll.content);
            ll.highlighted = (first + i == rps.cursor);
            data.rawPane.push_back(std::move(ll));
        }
    }

    // ── Filtered pane ─────────────────────────────────────────────────────────
    {
        const size_t total = chain_.filteredLineCount();
        const size_t ph    = static_cast<size_t>(lastFilteredPaneHeight_);

        PaneState& fps = const_cast<AppController*>(this)->filteredState_;
        const_cast<AppController*>(this)->clampScroll(fps, total, lastFilteredPaneHeight_);

        const size_t first = fps.scrollOffset;
        const size_t count = (total > first) ? std::min(ph, total - first) : 0;

        data.filteredPane.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const size_t rawLineNo = chain_.filteredLineAt(first + i);
            LogLine ll;
            ll.rawLineNo   = rawLineNo;
            ll.content     = reader_.getLine(rawLineNo);
            ll.colors      = chain_.computeColors(rawLineNo, ll.content);
            ll.highlighted = (first + i == fps.cursor);
            data.filteredPane.push_back(std::move(ll));
        }
    }

    // ── Filter tags ───────────────────────────────────────────────────────────
    data.filterTags.reserve(chain_.filterCount());
    for (size_t i = 0; i < chain_.filterCount(); ++i) {
        const FilterDef& def = chain_.filterAt(i);
        ViewData::FilterTag tag;
        tag.number   = static_cast<int>(i) + 1;
        tag.pattern  = def.pattern;
        tag.color    = def.color;
        tag.enabled  = def.enabled;
        tag.exclude  = def.exclude;
        tag.selected = false;  // Phase 2: selectedFilter_ tracking
        data.filterTags.push_back(std::move(tag));
    }

    return data;
}

// ── Resize ────────────────────────────────────────────────────────────────────

void AppController::onTerminalResize(int /*width*/, int height) {
    // Recalculate pane heights and re-clamp scroll offsets
    const int overhead   = 6;  // status + 3 separators + filter bar + input line
    const int available  = std::max(2, height - overhead);
    lastRawPaneHeight_      = available / 2;
    lastFilteredPaneHeight_ = available - lastRawPaneHeight_;

    const_cast<AppController*>(this)->clampScroll(rawState_,
        reader_.lineCount(), lastRawPaneHeight_);
    const_cast<AppController*>(this)->clampScroll(filteredState_,
        chain_.filteredLineCount(), lastFilteredPaneHeight_);
}

bool AppController::isInputActive() const {
    return inputMode_ != InputMode::None;
}
