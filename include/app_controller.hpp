#pragma once
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/event.hpp>

#include "filter_chain.hpp"
#include "log_reader.hpp"

// ── Enumerations ───────────────────────────────────────────────────────────────

enum class FocusArea { Raw, Filtered };

enum class AppMode { Static, Realtime };

enum class InputMode {
    None,
    Search,
    FilterAdd,
    FilterEdit,
    GotoLine,
    OpenFile,
    ExportConfirm,
};

// ── State and view structures ──────────────────────────────────────────────────

struct PaneState {
    size_t cursor       = 0;   // Highlighted row, absolute index into the line list
    size_t scrollOffset = 0;   // Index of the top visible row; driven by cursor
};

struct LogLine {
    size_t           rawLineNo  = 0;
    std::string_view content;
    std::vector<ColorSpan> colors;
    bool             highlighted = false;
    bool             folded      = false;  // Phase 3: fold long lines
};

struct ViewData {
    // ── Status bar ────────────────────────────────────────────────────────────
    std::string fileName;
    AppMode     mode         = AppMode::Static;
    size_t      totalLines   = 0;
    size_t      newLineCount = 0;    // Unread appended lines (non-G-lock mode)
    bool        isIndexing   = false;
    bool        showLineNumbers = false;  // Phase 3: toggled by 'l'

    // ── Log panes (already clipped to pane height by AppController) ───────────
    std::vector<LogLine> rawPane;
    bool                 rawFocused      = true;
    std::vector<LogLine> filteredPane;
    bool                 filteredFocused = false;

    // ── Filter bar ────────────────────────────────────────────────────────────
    struct FilterTag {
        int         number;    // 1-based display number
        std::string pattern;
        std::string color;
        bool        enabled;
        bool        exclude;
        bool        selected;
    };
    std::vector<FilterTag> filterTags;

    // ── Input line ────────────────────────────────────────────────────────────
    InputMode   inputMode   = InputMode::None;
    std::string inputPrompt;
    std::string inputBuffer;
    bool        inputValid  = false;   // Regex signal light

    // ── Dialog overlay ────────────────────────────────────────────────────────
    bool        showDialog      = false;
    std::string dialogTitle;
    std::string dialogBody;
    bool        dialogHasChoice = false;  // true = Y/N, false = any key to close

    // ── Progress overlay ──────────────────────────────────────────────────────
    bool   showProgress = false;
    double progress     = 0.0;
};

// ── AppController ──────────────────────────────────────────────────────────────

// Manages dual-pane state and dispatches keyboard events.
// All methods must be called from the UI thread.
class AppController {
public:
    AppController(LogReader& reader, FilterChain& chain);
    ~AppController();

    AppController(const AppController&)            = delete;
    AppController& operator=(const AppController&) = delete;

    // Process one keyboard event. Returns true if the event was consumed.
    bool handleKey(const ftxui::Event& event);

    // Build a full ViewData snapshot clipped to the given pane heights.
    ViewData getViewData(int rawPaneHeight, int filteredPaneHeight) const;

    // Recalculate scroll offsets after terminal resize.
    void onTerminalResize(int width, int height);

    // True while any text-input mode is active (search, filter edit, etc.).
    bool isInputActive() const;

private:
    LogReader&   reader_;
    FilterChain& chain_;

    PaneState  rawState_;
    PaneState  filteredState_;
    FocusArea  focus_     = FocusArea::Raw;
    InputMode  inputMode_ = InputMode::None;

    // ── Navigation helpers ────────────────────────────────────────────────────
    PaneState& activeState();
    const PaneState& activeState() const;
    size_t    activeLineCount() const;

    void moveCursor(int delta, int paneHeight);
    void clampScroll(PaneState& ps, size_t totalLines, int paneHeight);

    // ── Key dispatch ─────────────────────────────────────────────────────────
    bool handleKeyNone(const ftxui::Event& event);

    // Stored last-known pane heights for clamp on resize
    mutable int lastRawPaneHeight_      = 20;
    mutable int lastFilteredPaneHeight_ = 20;
};
