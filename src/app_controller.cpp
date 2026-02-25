#include "app_controller.hpp"

#include <algorithm>
#include <regex>
#include <string>

using namespace ftxui;

// ── AppController ──────────────────────────────────────────────────────────────

AppController::AppController(ILogReader& reader, IFilterChain& chain)
    : reader_(reader)
    , chain_(chain)
    , postFn_([](std::function<void()> fn) { fn(); })
{
    // Register LogReader callbacks (called in UI thread via postFn_)
    reader_.onNewLines([this](size_t first, size_t last) {
        handleNewLines(first, last);
    });
    reader_.onFileReset([this]() {
        handleFileReset();
    });
}

AppController::~AppController() = default;

void AppController::setPostFn(PostFn fn) {
    postFn_ = fn ? std::move(fn)
                 : [](std::function<void()> f) { f(); };
}

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

void AppController::clampScroll(PaneState& ps, size_t totalLines, int paneHeight) {
    if (totalLines == 0) {
        ps.cursor       = 0;
        ps.scrollOffset = 0;
        return;
    }

    if (ps.cursor >= totalLines)
        ps.cursor = totalLines - 1;

    const size_t ph = static_cast<size_t>(std::max(1, paneHeight));

    if (ps.cursor < ps.scrollOffset)
        ps.scrollOffset = ps.cursor;

    if (ps.cursor >= ps.scrollOffset + ph)
        ps.scrollOffset = ps.cursor - ph + 1;

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

    followTail_ = false;  // Any navigation cancels tail-follow

    if (delta < 0) {
        size_t step = static_cast<size_t>(-delta);
        ps.cursor   = (ps.cursor >= step) ? ps.cursor - step : 0;
    } else {
        ps.cursor = std::min(ps.cursor + static_cast<size_t>(delta), total - 1);
    }

    clampScroll(ps, total, paneHeight);
}

void AppController::jumpToRawLine(size_t rawLineNo) {
    // Move raw pane cursor to the given 1-based raw line
    if (rawLineNo == 0 || rawLineNo > reader_.lineCount()) return;
    const size_t idx = rawLineNo - 1;
    rawState_.cursor = idx;
    clampScroll(rawState_, reader_.lineCount(), lastRawPaneHeight_);
}

// ── Key handling ───────────────────────────────────────────────────────────────

bool AppController::handleKey(const Event& event) {
    if (showDialog_)
        return handleKeyDialog(event);

    switch (inputMode_) {
        case InputMode::None:
            return handleKeyNone(event);
        case InputMode::FilterAdd:
        case InputMode::FilterEdit:
            return handleKeyFilterInput(event);
        case InputMode::Search:
            return handleKeySearch(event);
        case InputMode::GotoLine:
            return handleKeyGotoLine(event);
        case InputMode::OpenFile:
            return handleKeyOpenFile(event);
        default:
            return false;
    }
}

// ── None mode (sub-handlers) ───────────────────────────────────────────────────

bool AppController::handleNavKeys(const Event& event, int activePh) {
    if (event == Event::ArrowUp)   { moveCursor(-1, activePh); return true; }
    if (event == Event::ArrowDown) { moveCursor(+1, activePh); return true; }

    if (event == Event::PageUp) {
        moveCursor(-std::max(1, activePh - 1), activePh);
        return true;
    }
    if (event == Event::PageDown) {
        moveCursor(+std::max(1, activePh - 1), activePh);
        return true;
    }

    if (event == Event::Home) {
        PaneState& ps   = activeState();
        ps.cursor       = 0;
        ps.scrollOffset = 0;
        followTail_     = false;
        return true;
    }
    if (event == Event::End) {
        const size_t total = activeLineCount();
        if (total > 0) {
            PaneState& ps = activeState();
            ps.cursor     = total - 1;
            clampScroll(ps, total, activePh);
        }
        followTail_ = false;
        return true;
    }

    // Focus switch
    if (event == Event::Tab || event == Event::TabReverse) {
        focus_ = (focus_ == FocusArea::Raw) ? FocusArea::Filtered : FocusArea::Raw;
        return true;
    }

    return false;
}

bool AppController::handleFilterKeys(const Event& event) {
    if (event == Event::Character('a')) {
        colorPaletteIdx_ = chain_.filterCount() % kDefaultColorPaletteSize;
        enterInputMode(InputMode::FilterAdd, "Pattern> ");
        return true;
    }
    if (event == Event::Character('e')) {
        if (chain_.filterCount() == 0) return true;
        // Ensure selectedFilter_ is in valid range
        if (selectedFilter_ >= chain_.filterCount()) {
            selectedFilter_ = chain_.filterCount() > 0 ? chain_.filterCount() - 1 : 0;
        }
        const FilterDef& def = chain_.filterAt(selectedFilter_);
        enterInputMode(InputMode::FilterEdit, "Edit> ", def.pattern);
        return true;
    }
    if (event == Event::Character('d')) {
        if (chain_.filterCount() == 0) return true;
        // Validate selectedFilter_ before deletion
        if (selectedFilter_ >= chain_.filterCount()) {
            selectedFilter_ = chain_.filterCount() - 1;
        }
        chain_.remove(selectedFilter_);
        // Adjust selection after deletion
        if (selectedFilter_ >= chain_.filterCount() && chain_.filterCount() > 0) {
            selectedFilter_ = chain_.filterCount() - 1;
        }
        triggerReprocess(0);
        return true;
    }
    if (event == Event::Character('[')) {
        if (chain_.filterCount() > 0 && selectedFilter_ > 0)
            --selectedFilter_;
        return true;
    }
    if (event == Event::Character(']')) {
        if (chain_.filterCount() > 0 && selectedFilter_ + 1 < chain_.filterCount())
            ++selectedFilter_;
        return true;
    }
    if (event == Event::Character('+')) {
        if (chain_.filterCount() > 1 && selectedFilter_ > 0) {
            chain_.moveUp(selectedFilter_);
            --selectedFilter_;
            triggerReprocess(selectedFilter_);
        }
        return true;
    }
    if (event == Event::Character('-')) {
        if (chain_.filterCount() > 1 && selectedFilter_ + 1 < chain_.filterCount()) {
            chain_.moveDown(selectedFilter_);
            ++selectedFilter_;
            triggerReprocess(selectedFilter_ - 1);
        }
        return true;
    }
    if (event == Event::Character(' ')) {
        if (chain_.filterCount() > 0) {
            // Validate selectedFilter_ before access
            if (selectedFilter_ >= chain_.filterCount()) {
                selectedFilter_ = chain_.filterCount() - 1;
            }
            FilterDef def = chain_.filterAt(selectedFilter_);
            def.enabled   = !def.enabled;
            chain_.edit(selectedFilter_, std::move(def));
            triggerReprocess(selectedFilter_);
        }
        return true;
    }
    return false;
}

bool AppController::handleSearchKeys(const Event& event) {
    if (event == Event::Character('/')) {
        enterInputMode(InputMode::Search, "Search> ");
        return true;
    }
    if (event == Event::Character('n')) {
        const size_t resultCount = searchResults_.size();
        if (resultCount > 0) {
            // Ensure searchIndex_ is valid before incrementing
            if (searchIndex_ >= resultCount) {
                searchIndex_ = 0;
            } else {
                searchIndex_ = (searchIndex_ + 1) % resultCount;
            }
            jumpToSearchResult(searchIndex_);
        }
        return true;
    }
    if (event == Event::Character('p')) {
        const size_t resultCount = searchResults_.size();
        if (resultCount > 0) {
            // Ensure searchIndex_ is valid before decrementing
            if (searchIndex_ >= resultCount) {
                searchIndex_ = resultCount - 1;
            } else {
                searchIndex_ = (searchIndex_ == 0) ? resultCount - 1 : searchIndex_ - 1;
            }
            jumpToSearchResult(searchIndex_);
        }
        return true;
    }
    if (event == Event::Character('g')) {
        if (!reader_.isIndexing())
            enterInputMode(InputMode::GotoLine, "Goto: ");
        return true;
    }
    if (event == Event::Character('G')) {
        if (reader_.mode() == FileMode::Realtime) {
            followTail_   = true;
            newLineCount_ = 0;
            const size_t total = reader_.lineCount();
            if (total > 0) {
                rawState_.cursor = total - 1;
                clampScroll(rawState_, total, lastRawPaneHeight_);
            }
        }
        return true;
    }
    return false;
}

bool AppController::handleModeKeys(const Event& event) {
    if (event == Event::Character('o')) {
        enterInputMode(InputMode::OpenFile, "Open: ");
        return true;
    }
    if (event == Event::Character('s')) {
        reader_.setMode(FileMode::Static);
        followTail_ = false;
        return true;
    }
    if (event == Event::Character('r')) {
        reader_.setMode(FileMode::Realtime);
        return true;
    }
    if (event == Event::Character('f')) {
        reader_.forceCheck();
        return true;
    }
    return false;
}

// ── None mode ─────────────────────────────────────────────────────────────────

bool AppController::handleKeyNone(const Event& event) {
    const int activePh = (focus_ == FocusArea::Raw)
                         ? lastRawPaneHeight_ : lastFilteredPaneHeight_;

    return handleNavKeys(event, activePh)
        || handleFilterKeys(event)
        || handleSearchKeys(event)
        || handleModeKeys(event);
}

// ── Filter input mode ─────────────────────────────────────────────────────────

bool AppController::handleKeyFilterInput(const Event& event) {
    // Special handling for Return key
    if (event == Event::Return) {
        if (!inputValid_) {
            showErrorDialog("无效正则", "正则表达式编译失败:\n" + inputBuffer_);
            return true;
        }
        if (inputMode_ == InputMode::FilterAdd) {
            FilterDef def;
            def.pattern = inputBuffer_;
            def.color   = nextPaletteColor(colorPaletteIdx_);
            chain_.append(std::move(def));
            selectedFilter_ = chain_.filterCount() - 1;
            triggerReprocess(selectedFilter_);
        } else {  // FilterEdit
            // Validate selectedFilter_ before access (defensive)
            if (selectedFilter_ < chain_.filterCount()) {
                FilterDef def = chain_.filterAt(selectedFilter_);
                def.pattern = inputBuffer_;
                chain_.edit(selectedFilter_, std::move(def));
                triggerReprocess(selectedFilter_);
            }
        }
        exitInputMode();
        return true;
    }

    // Tab: cycle color palette (FilterAdd only)
    if (event == Event::Tab && inputMode_ == InputMode::FilterAdd) {
        colorPaletteIdx_ = (colorPaletteIdx_ + 1) % kDefaultColorPaletteSize;
        return true;
    }

    // Handle common input keys (Escape, Backspace, characters)
    return handleCommonInputKeys(event, true);
}

// ── Search mode ───────────────────────────────────────────────────────────────

bool AppController::handleKeySearch(const Event& event) {
    // Special handling for Escape - also clear search results
    if (event == Event::Escape) {
        searchResults_.clear();
        searchIndex_ = 0;
        exitInputMode();
        return true;
    }

    // Special handling for Return key
    if (event == Event::Return) {
        runSearch(inputBuffer_);
        exitInputMode();
        return true;
    }

    // Handle common input keys (Backspace, characters)
    return handleCommonInputKeys(event, true);
}

// ── GotoLine mode ─────────────────────────────────────────────────────────────

bool AppController::handleKeyGotoLine(const Event& event) {
    // Special handling for Return key
    if (event == Event::Return) {
        try {
            size_t lineNo = std::stoul(inputBuffer_);
            if (lineNo == 0) lineNo = 1;
            const size_t total = reader_.lineCount();
            if (lineNo > total) lineNo = total;
            jumpToRawLine(lineNo);
        } catch (const std::invalid_argument&) {
            // Invalid number format - do nothing, just exit
        } catch (const std::out_of_range&) {
            // Number too large - do nothing, just exit
        }
        exitInputMode();
        // Note: We don't show error dialog as the input is already restricted to digits
        // This catch is only for edge cases like extremely large numbers
        return true;
    }

    // Only accept digits for GotoLine mode
    if (event.is_character() && event.character().size() == 1
        && event.character()[0] >= '0' && event.character()[0] <= '9') {
        inputBuffer_ += event.character();
        return true;
    }

    // Handle common keys (Escape, Backspace) but don't accept all characters
    return handleCommonInputKeys(event, false);
}

// ── OpenFile mode ─────────────────────────────────────────────────────────────

bool AppController::handleKeyOpenFile(const Event& event) {
    // Special handling for Return key
    if (event == Event::Return) {
        std::string path = inputBuffer_;
        exitInputMode();
        if (!path.empty()) {
            if (!reader_.open(path)) {
                showErrorDialog("打开失败", "无法打开文件:\n" + path);
            } else {
                chain_.reset();
                triggerReprocess(0);
                rawState_      = {};
                filteredState_ = {};
                newLineCount_  = 0;
                followTail_    = false;
            }
        }
        return true;
    }

    // Handle common input keys (Escape, Backspace, characters)
    return handleCommonInputKeys(event, true);
}

// ── Dialog mode ───────────────────────────────────────────────────────────────

bool AppController::handleKeyDialog(const Event& event) {
    if (dialogHasChoice_) {
        if (event == Event::Character('y') || event == Event::Character('Y')) {
            auto action = std::move(dialogYesAction_);
            closeDialog();
            if (action) action();
            return true;
        }
        if (event == Event::Character('n') || event == Event::Character('N')) {
            auto action = std::move(dialogNoAction_);
            closeDialog();
            if (action) action();
            return true;
        }
    } else {
        // Any key closes the dialog
        closeDialog();
        return true;
    }
    return false;
}

// ── Callbacks from LogReader ──────────────────────────────────────────────────

void AppController::handleNewLines(size_t firstLine, size_t lastLine) {
    chain_.processNewLines(firstLine, lastLine);
    newLineCount_ += lastLine - firstLine + 1;

    if (followTail_ && inputMode_ == InputMode::None) {
        newLineCount_ = 0;
        const size_t total = reader_.lineCount();
        if (total > 0) {
            rawState_.cursor = total - 1;
            clampScroll(rawState_, total, lastRawPaneHeight_);
        }
    }
}

void AppController::handleFileReset() {
    showDialog_      = true;
    dialogTitle_     = "文件已重置";
    dialogBody_      = "检测到文件被截断或替换。\n是否重新加载?";
    dialogHasChoice_ = true;

    std::string pathCopy = std::string(reader_.filePath());

    dialogYesAction_ = [this, pathCopy]() {
        if (reader_.open(pathCopy)) {
            chain_.reset();
            triggerReprocess(0);
            rawState_      = {};
            filteredState_ = {};
            newLineCount_  = 0;
            followTail_    = false;
        }
    };
    dialogNoAction_ = []() {};
}

// ── getViewData helpers ────────────────────────────────────────────────────────

void AppController::buildRawPane(ViewData& data) {
    const size_t total = reader_.lineCount();
    const size_t ph    = static_cast<size_t>(lastRawPaneHeight_);

    clampScroll(rawState_, total, lastRawPaneHeight_);

    const size_t first = rawState_.scrollOffset;
    const size_t count = (total > first) ? std::min(ph, total - first) : 0;

    // Pre-compute the highlighted search line (0 = none)
    // Cache size to avoid potential race conditions
    const size_t resultCount = searchResults_.size();
    const size_t searchLine = (resultCount > 0 && searchIndex_ < resultCount)
                              ? searchResults_[searchIndex_] : 0;

    data.rawPane.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t lineNo = first + i + 1;
        LogLine ll;
        ll.rawLineNo   = lineNo;
        ll.content     = reader_.getLine(lineNo);
        ll.colors      = chain_.computeColors(lineNo, ll.content);
        ll.highlighted = (first + i == rawState_.cursor) || (lineNo == searchLine);
        data.rawPane.push_back(std::move(ll));
    }
}

void AppController::buildFilteredPane(ViewData& data) {
    const size_t total = chain_.filteredLineCount();
    const size_t ph    = static_cast<size_t>(lastFilteredPaneHeight_);

    clampScroll(filteredState_, total, lastFilteredPaneHeight_);

    const size_t first = filteredState_.scrollOffset;
    const size_t count = (total > first) ? std::min(ph, total - first) : 0;

    data.filteredPane.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t rawLineNo = chain_.filteredLineAt(first + i);
        LogLine ll;
        ll.rawLineNo   = rawLineNo;
        ll.content     = reader_.getLine(rawLineNo);
        ll.colors      = chain_.computeColors(rawLineNo, ll.content);
        ll.highlighted = (first + i == filteredState_.cursor);
        data.filteredPane.push_back(std::move(ll));
    }
}

// ── getViewData ────────────────────────────────────────────────────────────────

ViewData AppController::getViewData(int rawPaneHeight, int filteredPaneHeight) {
    lastRawPaneHeight_      = std::max(1, rawPaneHeight);
    lastFilteredPaneHeight_ = std::max(1, filteredPaneHeight);

    ViewData data;
    data.fileName        = std::string(reader_.filePath());
    data.mode            = (reader_.mode() == FileMode::Static) ? AppMode::Static
                                                                 : AppMode::Realtime;
    data.totalLines      = reader_.lineCount();
    data.newLineCount    = newLineCount_;
    data.isIndexing      = reader_.isIndexing();
    data.rawFocused      = (focus_ == FocusArea::Raw);
    data.filteredFocused = !data.rawFocused;
    data.inputMode       = inputMode_;
    data.inputPrompt     = inputPrompt_;
    data.inputBuffer     = inputBuffer_;
    data.inputValid      = inputValid_;

    data.showDialog      = showDialog_;
    data.dialogTitle     = dialogTitle_;
    data.dialogBody      = dialogBody_;
    data.dialogHasChoice = dialogHasChoice_;

    data.showProgress = showProgress_;
    data.progress     = progress_;

    buildRawPane(data);
    buildFilteredPane(data);

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
        tag.selected = (i == selectedFilter_);
        data.filterTags.push_back(std::move(tag));
    }

    return data;
}

// ── Resize ────────────────────────────────────────────────────────────────────

void AppController::onTerminalResize(int /*width*/, int height) {
    const int overhead  = 6;
    const int available = std::max(2, height - overhead);
    lastRawPaneHeight_      = available / 2;
    lastFilteredPaneHeight_ = available - lastRawPaneHeight_;

    clampScroll(rawState_,      reader_.lineCount(),        lastRawPaneHeight_);
    clampScroll(filteredState_, chain_.filteredLineCount(), lastFilteredPaneHeight_);
}

bool AppController::isInputActive() const {
    return inputMode_ != InputMode::None;
}

// ── Input helpers ─────────────────────────────────────────────────────────────

void AppController::enterInputMode(InputMode mode, std::string prompt,
                                    std::string prefill) {
    inputMode_   = mode;
    inputPrompt_ = std::move(prompt);
    inputBuffer_ = std::move(prefill);
    inputValid_  = (mode == InputMode::FilterEdit && !inputBuffer_.empty());
    if (inputMode_ == InputMode::FilterAdd || inputMode_ == InputMode::FilterEdit)
        validateInputRegex();
}

void AppController::exitInputMode() {
    inputMode_   = InputMode::None;
    inputBuffer_.clear();
    inputPrompt_.clear();
    inputValid_  = false;
}

void AppController::validateInputRegex() {
    try {
        if (inputBuffer_.empty()) { inputValid_ = false; return; }
        std::regex r(inputBuffer_);
        (void)r;
        inputValid_ = true;
    } catch (const std::regex_error&) {
        inputValid_ = false;
    }
}

// ── Common input helpers ──────────────────────────────────────────────────────

bool AppController::handleCommonInputKeys(const Event& event, bool allowCharacters) {
    // Handle Escape key - exit input mode
    if (event == Event::Escape) {
        exitInputMode();
        return true;
    }

    // Handle Backspace key
    if (event == Event::Backspace) {
        return handleInputBackspace();
    }

    // Handle character input if allowed
    if (allowCharacters && event.is_character()) {
        inputBuffer_ += event.character();
        // Validate regex if in filter input mode
        if (inputMode_ == InputMode::FilterAdd || inputMode_ == InputMode::FilterEdit) {
            validateInputRegex();
        }
        return true;
    }

    return false;
}

bool AppController::handleInputBackspace() {
    if (!inputBuffer_.empty()) {
        inputBuffer_.pop_back();
        // Validate regex if in filter input mode
        if (inputMode_ == InputMode::FilterAdd || inputMode_ == InputMode::FilterEdit) {
            validateInputRegex();
        }
        return true;
    }
    return false;
}

// ── Filter helpers ────────────────────────────────────────────────────────────

const char* AppController::nextPaletteColor(size_t idx) {
    return defaultColor(idx);
}

void AppController::triggerReprocess(size_t fromFilter) {
    showProgress_ = true;
    progress_     = 0.0;

    // The onProgress / onDone callbacks are invoked from FilterChain's postFn
    // context, which is already the UI thread. No additional wrapping needed.
    chain_.reprocess(fromFilter,
        [this](double p) {
            progress_ = p;
        },
        [this]() {
            showProgress_ = false;
            progress_     = 1.0;
        });
}

void AppController::showErrorDialog(std::string title, std::string body) {
    showDialog_      = true;
    dialogTitle_     = std::move(title);
    dialogBody_      = std::move(body);
    dialogHasChoice_ = false;
    dialogYesAction_ = {};
    dialogNoAction_  = {};
}

void AppController::closeDialog() {
    showDialog_      = false;
    dialogTitle_.clear();
    dialogBody_.clear();
    dialogHasChoice_ = false;
    dialogYesAction_ = {};
    dialogNoAction_  = {};
}

// ── Search ────────────────────────────────────────────────────────────────────

void AppController::runSearch(const std::string& keyword) {
    searchResults_.clear();
    searchIndex_ = 0;

    if (keyword.empty()) return;

    const size_t total = reader_.lineCount();
    // Reserve space to avoid frequent reallocations during search
    // Estimate: most searches match <10% of lines, but reserve conservatively
    searchResults_.reserve(std::min(total / 10, size_t(10000)));

    for (size_t i = 1; i <= total; ++i) {
        std::string_view line = reader_.getLine(i);
        // Simple substring search (case-sensitive)
        if (line.find(keyword) != std::string_view::npos)
            searchResults_.push_back(i);
    }

    if (!searchResults_.empty()) {
        searchIndex_ = 0;
        jumpToSearchResult(0);
    }
}

void AppController::jumpToSearchResult(size_t idx) {
    if (idx >= searchResults_.size()) return;
    jumpToRawLine(searchResults_[idx]);
}
