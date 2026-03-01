#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include <ftxui/component/event.hpp>

#include "i_filter_chain.hpp"
#include "i_log_reader.hpp"
#include "render_utils.hpp"

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
    size_t cursor        = 0;
    size_t scrollOffset  = 0;
    size_t hScrollOffset = 0;   // horizontal byte offset (for wide-line viewing)
};

struct LogLine {
    size_t                      rawLineNo   = 0;
    std::string_view            content;
    std::vector<ColorSpan>      colors;
    std::vector<SearchSpan>     searchSpans;      // inverted-color search matches
    std::vector<SelectionSpan>  selectionSpans;   // character-level text selection
    bool                        highlighted = false;
    bool                        folded      = false;
};

struct ViewData {
    // ── Status bar ────────────────────────────────────────────────────────────
    std::string fileName;
    AppMode     mode         = AppMode::Static;
    size_t      totalLines   = 0;
    size_t      newLineCount = 0;
    bool        isIndexing   = false;
    bool        showLineNumbers = false;
    int         terminalWidth   = 80;

    // ── Log panes ─────────────────────────────────────────────────────────────
    std::vector<LogLine> rawPane;
    bool                 rawFocused      = true;
    std::vector<LogLine> filteredPane;
    bool                 filteredFocused = false;
    size_t               rawHScroll      = 0;   // horizontal scroll offset for raw pane
    size_t               filtHScroll     = 0;   // horizontal scroll offset for filtered pane

    // ── Filter bar ────────────────────────────────────────────────────────────
    struct FilterTag {
        int         number;
        std::string pattern;
        std::string color;
        bool        enabled;
        bool        exclude;
        bool        selected;
        bool        useRegex   = false;
        size_t      matchCount = 0;
    };
    std::vector<FilterTag> filterTags;

    // ── Input line ────────────────────────────────────────────────────────────
    InputMode   inputMode      = InputMode::None;
    std::string inputPrompt;
    std::string inputBuffer;
    size_t      inputCursorPos = 0;     // byte offset of the text cursor within inputBuffer
    bool        inputValid     = false;
    bool        inputUseRegex  = false;  // regex mode for the active input (filter or search)
    bool        inputExclude   = false;  // exclude mode for filter add/edit (Ctrl+R toggle)

    // ── Dialog overlay ────────────────────────────────────────────────────────
    bool        showDialog      = false;
    std::string dialogTitle;
    std::string dialogBody;
    bool        dialogHasChoice = false;

    // ── Progress overlay ──────────────────────────────────────────────────────
    bool   showProgress = false;
    double progress     = 0.0;

    // ── Search status ─────────────────────────────────────────────────────────
    std::string searchKeyword;        // active search keyword; empty if none
    size_t      searchResultCount = 0;
    size_t      searchResultIndex = 0;  // 1-based current result position
    bool        searchActive      = false;  // true while a search keyword is set
    bool        searchUseRegex    = false;  // current search mode (regex or literal)

    // ── Text selection ───────────────────────────────────────────────────────
    bool        hasSelection      = false;  // true when character-level selection is active

    // ── Tab completion popup (OpenFile mode) ─────────────────────────────────
    bool                     showCompletions  = false;
    std::vector<std::string> completions;      // filename-only candidates
    size_t                   completionIndex  = 0;    // 0-based index of highlighted item
    int                      completionCol    = 0;    // terminal column where filename text starts
};

// ── AppController ──────────────────────────────────────────────────────────────

// Manages dual-pane state and dispatches keyboard events.
// All methods must be called from the UI thread.
class AppController {
public:
    // Phase 2: accepts abstract interfaces so tests can inject mocks.
    AppController(ILogReader& reader, IFilterChain& chain);
    ~AppController();

    AppController(const AppController&)            = delete;
    AppController& operator=(const AppController&) = delete;

    // Inject post-to-UI function (needed for background callbacks).
    void setPostFn(PostFn fn);

    // Process one keyboard event. Returns true if consumed.
    bool handleKey(const ftxui::Event& event);

    // Build a full ViewData snapshot clipped to the given pane heights.
    // NOTE: not const — may clamp scroll positions of both panes.
    ViewData getViewData(int rawPaneHeight, int filteredPaneHeight);

    void onTerminalResize(int width, int height);

    bool isInputActive() const;

    // Expose cached pane heights for the renderer to use
    // (avoids polling Terminal::Size() on every render frame).
    int rawPaneHeight()  const { return lastRawPaneHeight_;      }
    int filtPaneHeight() const { return lastFilteredPaneHeight_; }

    // Called by LogReader callbacks (registered in open()) to handle new lines
    // or file reset in the UI thread.
    void handleNewLines(size_t firstLine, size_t lastLine);
    void handleFileReset();

    // Scroll a specific pane by delta rows without changing the active focus.
    // Positive delta = scroll down, negative = scroll up.
    void scrollPane(FocusArea area, int delta);

    // Switch the active focus to the given pane.
    void setFocus(FocusArea area);

    // Returns the currently focused pane.
    FocusArea focusArea() const;

    // Move the cursor in a pane to the clicked row (0-based offset within pane).
    void clickLine(FocusArea area, int rowInPane);

    // Show a Y/N quit confirmation dialog.  exitFn is called when the user
    // confirms.  No-op if a dialog is already open.
    void requestQuit(std::function<void()> exitFn);

    // Returns true when a modal dialog (info or choice) is currently visible.
    bool isDialogOpen() const;

    // ── Text selection (character-level mouse drag) ────────────────────────
    void startSelection(FocusArea pane, size_t lineIndex, size_t byteOffset);
    void extendSelection(size_t lineIndex, size_t byteOffset);
    void finalizeSelection();
    void clearSelection();
    bool hasSelection() const;
    bool isSelectionDragging() const;
    void copySelectionToClipboard();

    // Convert a raw terminal screen-column (m.x from FTXUI Mouse event) within
    // the given pane/row to a byte offset within the line content.
    // lineIndex is 0-based ABSOLUTE index in the pane (not viewport-relative).
    // Accounts for the line-number prefix and horizontal scroll internally.
    size_t screenColToByteOffset(FocusArea pane, size_t lineIndex, int screenCol) const;

    // Return the current vertical scroll offset for the given pane.
    size_t paneScrollOffset(FocusArea area) const;

    // Return the last known terminal width.
    int terminalWidth() const { return lastTerminalWidth_; }

    // Return the column width of the rendered line prefix (line-number + cursor arrow).
    // Used by render.cpp to determine horizontal auto-scroll thresholds.
    int prefixColWidth() const;

    // Scroll the given pane horizontally by deltaBytes (positive = right, negative = left).
    void scrollHorizontal(FocusArea area, int deltaBytes);

private:
    ILogReader&   reader_;
    IFilterChain& chain_;
    PostFn        postFn_;

    // ── Pane state ────────────────────────────────────────────────────────────
    PaneState  rawState_;
    PaneState  filteredState_;
    FocusArea  focus_     = FocusArea::Raw;

    // ── Input state ───────────────────────────────────────────────────────────
    InputMode   inputMode_           = InputMode::None;
    std::string inputBuffer_;
    std::string inputPrompt_;
    size_t      inputCursorPos_      = 0;  // byte offset of cursor within inputBuffer_
    bool        inputValid_          = false;
    bool        filterInputUseRegex_ = false;  // local regex mode during filter add/edit
    bool        filterInputExclude_  = false;  // local exclude mode during filter add/edit

    // ── Filter selection ──────────────────────────────────────────────────────
    size_t selectedFilter_  = 0;
    size_t colorPaletteIdx_ = 0;    // Cycles through palette on 'a' (FilterAdd)

    // ── Search state ──────────────────────────────────────────────────────────
    std::vector<size_t>       searchResults_;       // 1-based raw line numbers
    size_t                    searchIndex_      = 0;
    std::string               searchKeyword_;       // Last committed keyword (for spans)
    bool                      searchInFiltered_ = false;  // Search was in filtered pane
    bool                      searchUseRegex_   = false;  // Use regex matching for search
    std::optional<std::regex> searchRegex_;         // Compiled regex (when searchUseRegex_)

    // ── Real-time / tail-follow ───────────────────────────────────────────────
    bool   followTail_   = false;
    size_t newLineCount_ = 0;    // Unread new lines (non-follow mode)

    // ── Dialog ────────────────────────────────────────────────────────────────
    bool              showDialog_      = false;
    std::string       dialogTitle_;
    std::string       dialogBody_;
    bool              dialogHasChoice_ = false;
    std::function<void()> dialogYesAction_;
    std::function<void()> dialogNoAction_;

    // ── Progress ──────────────────────────────────────────────────────────────
    bool   showProgress_ = false;
    double progress_     = 0.0;

    // ── Reprocess timeout ─────────────────────────────────────────────────────
    std::chrono::steady_clock::time_point reprocessStartTime_;
    bool reprocessTimeoutShown_ = false;

    // ── Text selection ────────────────────────────────────────────────────────
    struct SelectionPoint {
        FocusArea pane       = FocusArea::Raw;
        size_t    lineIndex  = 0;   // 0-based absolute index in the pane
        size_t    byteOffset = 0;   // byte offset within the line content
    };
    struct SelectionState {
        bool active   = false;  // true if at least one char is selected
        bool dragging = false;  // true while mouse button is held down
        SelectionPoint anchor;  // where the drag started
        SelectionPoint current; // where the drag currently is
        void clear() { active = dragging = false; }
        // Return ordered (start, end) pair by line index then byte offset.
        std::pair<SelectionPoint, SelectionPoint> ordered() const;
    };
    SelectionState selection_;

    // ── Display state ─────────────────────────────────────────────────────────
    bool                       showLineNumbers_ = true;
    std::unordered_set<size_t> foldedLines_;       // rawLineNo values that are folded
    int                        lastTerminalWidth_  = 80;
    int                        lastTerminalHeight_ = 0;   // 0 = not yet set (tests)
    std::string                exportPath_;        // Generated on 'w' press

    // Cached pane heights for rendering. Marked mutable because getViewData()
    // updates these values even though it's logically a query operation.
    // This allows the renderer to get consistent pane heights without calling
    // Terminal::Size() on every frame. Thread safety: single-threaded UI only.
    mutable int lastRawPaneHeight_      = 20;
    mutable int lastFilteredPaneHeight_ = 20;

    // ── Navigation helpers ────────────────────────────────────────────────────
    PaneState&       activeState();
    const PaneState& activeState() const;
    size_t           activeLineCount() const;

    void moveCursor(int delta, int paneHeight);
    void clampScroll(PaneState& ps, size_t totalLines, int paneHeight);
    void jumpToRawLine(size_t rawLineNo);  // Jump raw pane to a specific line

    // Recompute pane heights from lastTerminalHeight_, accounting for the extra
    // input row when input mode is active.  No-op when lastTerminalHeight_ == 0.
    void recomputePaneHeights();

    // ── Key dispatch ─────────────────────────────────────────────────────────
    bool handleKeyNone(const ftxui::Event& event);
    bool handleKeyFilterInput(const ftxui::Event& event);
    bool handleKeySearch(const ftxui::Event& event);
    bool handleKeyGotoLine(const ftxui::Event& event);
    bool handleKeyOpenFile(const ftxui::Event& event);
    bool handleKeyDialog(const ftxui::Event& event);
    bool handleKeyExportConfirm(const ftxui::Event& event);

    // Sub-handlers called by handleKeyNone()
    bool handleNavKeys(const ftxui::Event& event, int activePh);
    bool handleFilterKeys(const ftxui::Event& event);
    bool handleSearchKeys(const ftxui::Event& event);
    bool handleModeKeys(const ftxui::Event& event);

    // ── Input helpers ─────────────────────────────────────────────────────────
    void enterInputMode(InputMode mode, std::string prompt, std::string prefill = "");
    void exitInputMode();
    void validateInputRegex();

    // Common input handling patterns
    bool handleCommonInputKeys(const ftxui::Event& event, bool allowCharacters = true);
    bool handleInputBackspace();

    // ── Filter helpers ────────────────────────────────────────────────────────
    static const char* nextPaletteColor(size_t idx);
    void triggerReprocess(size_t fromFilter = 0);
    void showErrorDialog(std::string title, std::string body);
    void closeDialog();
    // Clamp selectedFilter_ to [0, filterCount()-1].  Called after any
    // operation that might leave selectedFilter_ pointing past the end.
    void clampSelectedFilter();

    // ── Search helpers ────────────────────────────────────────────────────────
    void runSearch(const std::string& keyword);
    void jumpToSearchResult(size_t idx);
    // Step through search results by direction (+1 forward, -1 backward).
    void stepSearch(int dir);
    // Clear all active search state (results, keyword, regex, pane flag).
    void clearSearch();

    // Toggle fold state for the line currently under the cursor.
    void toggleFoldCurrentLine();

    // ── Selection helpers ─────────────────────────────────────────────────────
    std::string buildSelectedText() const;

    // ── Completion state (OpenFile mode) ─────────────────────────────────────
    std::vector<std::string> completions_;
    size_t                   completionIndex_ = 0;
    bool                     showCompletions_ = false;

    void triggerCompletion();
    void acceptCompletion();

    // ── getViewData helpers ────────────────────────────────────────────────────
    void buildPane(FocusArea area, ViewData& data);
};
