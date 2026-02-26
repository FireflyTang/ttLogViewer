#pragma once
#include <atomic>
#include <functional>
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
    size_t cursor       = 0;
    size_t scrollOffset = 0;
};

struct LogLine {
    size_t                  rawLineNo   = 0;
    std::string_view        content;
    std::vector<ColorSpan>  colors;
    std::vector<SearchSpan> searchSpans;   // bold+underlined search matches
    bool                    highlighted = false;
    bool                    folded      = false;  // Phase 3
};

struct ViewData {
    // ── Status bar ────────────────────────────────────────────────────────────
    std::string fileName;
    AppMode     mode         = AppMode::Static;
    size_t      totalLines   = 0;
    size_t      newLineCount = 0;
    bool        isIndexing   = false;
    bool        showLineNumbers = false;  // Phase 3
    int         terminalWidth   = 80;    // Phase 3: for fold truncation

    // ── Log panes ─────────────────────────────────────────────────────────────
    std::vector<LogLine> rawPane;
    bool                 rawFocused      = true;
    std::vector<LogLine> filteredPane;
    bool                 filteredFocused = false;

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
    InputMode   inputMode   = InputMode::None;
    std::string inputPrompt;
    std::string inputBuffer;
    bool        inputValid  = false;

    // ── Dialog overlay ────────────────────────────────────────────────────────
    bool        showDialog      = false;
    std::string dialogTitle;
    std::string dialogBody;
    bool        dialogHasChoice = false;

    // ── Progress overlay ──────────────────────────────────────────────────────
    bool   showProgress = false;
    double progress     = 0.0;
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

private:
    ILogReader&   reader_;
    IFilterChain& chain_;
    PostFn        postFn_;

    // ── Pane state ────────────────────────────────────────────────────────────
    PaneState  rawState_;
    PaneState  filteredState_;
    FocusArea  focus_     = FocusArea::Raw;

    // ── Input state ───────────────────────────────────────────────────────────
    InputMode   inputMode_   = InputMode::None;
    std::string inputBuffer_;
    std::string inputPrompt_;
    bool        inputValid_  = false;

    // ── Filter selection ──────────────────────────────────────────────────────
    size_t selectedFilter_  = 0;
    size_t colorPaletteIdx_ = 0;    // Cycles through palette on Tab in FilterAdd

    // ── Search state ──────────────────────────────────────────────────────────
    std::vector<size_t> searchResults_;       // 1-based raw line numbers
    size_t              searchIndex_      = 0;
    std::string         searchKeyword_;        // Last committed keyword (for spans)
    bool                searchInFiltered_ = false;  // Search was in filtered pane

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

    // ── Phase 3 state ─────────────────────────────────────────────────────────
    bool                       showLineNumbers_ = false;
    std::unordered_set<size_t> foldedLines_;       // rawLineNo values that are folded
    int                        lastTerminalWidth_ = 80;
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

    // ── Key dispatch ─────────────────────────────────────────────────────────
    bool handleKeyNone(const ftxui::Event& event);
    bool handleKeyFilterInput(const ftxui::Event& event);
    bool handleKeySearch(const ftxui::Event& event);
    bool handleKeyGotoLine(const ftxui::Event& event);
    bool handleKeyOpenFile(const ftxui::Event& event);
    bool handleKeyDialog(const ftxui::Event& event);
    bool handleKeyExportConfirm(const ftxui::Event& event);  // Phase 3

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

    // ── Phase 3 helpers ───────────────────────────────────────────────────────
    // Toggle fold state for the line currently under the cursor.
    void toggleFoldCurrentLine();

    // ── getViewData helpers ────────────────────────────────────────────────────
    void buildRawPane(ViewData& data);
    void buildFilteredPane(ViewData& data);
};
