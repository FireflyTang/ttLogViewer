#include "app_controller.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <regex>
#include <string>

#include "app_config.hpp"
#include "version.hpp"

using namespace ftxui;

// ── File-scope constants ───────────────────────────────────────────────────────

// Full help text shown by the 'h' key.  Extracted here to keep handleModeKeys()
// readable and to make it easy to update the text in one place.
static constexpr size_t kHScrollStep = 4;   // bytes per ArrowLeft/ArrowRight press

static constexpr std::string_view kHelpText =
    "↑↓: 移动光标\n"
    "PgUp/PgDn: 翻页\n"
    "Home/End: 首/末行\n"
    "Tab: 切换区域\n"
    "←/→: 水平滚动\n"
    "a: 添加过滤器\n"
    "e: 编辑过滤器\n"
    "d: 删除过滤器\n"
    "[/]: 选择过滤器\n"
    "+/-: 调整过滤器顺序\n"
    "Space: 启停过滤器\n"
    "x: 切换正则/字符串匹配模式\n"
    "/: 搜索 (输入时 Tab 切换正则/字符串)\n"
    "n/p: 下/上一搜索结果  Esc:清除搜索\n"
    "g: 跳转行号\n"
    "G: 跟随末尾\n"
    "o: 打开文件\n"
    "s/r: 静态/实时模式\n"
    "f: 强制检查\n"
    "l: 行号显示切换\n"
    "z: 折叠/展开超长行\n"
    "w: 导出过滤结果\n"
    "h: 帮助\n"
    "q: 退出";

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

    // Allow ESC to cancel an in-progress reprocess immediately (None mode only;
    // in input modes, ESC already exits the mode via handleCommonInputKeys).
    if (event == Event::Escape && showProgress_ && inputMode_ == InputMode::None) {
        chain_.cancelReprocess();
        showProgress_          = false;
        progress_              = 0.0;
        reprocessTimeoutShown_ = false;
        return true;
    }

    // ESC in None mode with an active search → clear search state.
    if (event == Event::Escape && !searchKeyword_.empty() && inputMode_ == InputMode::None) {
        searchResults_.clear();
        searchIndex_      = 0;
        searchKeyword_.clear();
        searchInFiltered_ = false;
        searchRegex_.reset();
        return true;
    }

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
        case InputMode::ExportConfirm:
            return handleKeyExportConfirm(event);
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

    // Focus switch — also clear any active search so old results don't persist
    if (event == Event::Tab || event == Event::TabReverse) {
        searchResults_.clear();
        searchIndex_      = 0;
        searchKeyword_.clear();
        searchInFiltered_ = false;
        searchRegex_.reset();
        focus_ = (focus_ == FocusArea::Raw) ? FocusArea::Filtered : FocusArea::Raw;
        return true;
    }

    // Horizontal scroll
    if (event == Event::ArrowLeft) {
        PaneState& ps = activeState();
        ps.hScrollOffset = (ps.hScrollOffset >= kHScrollStep)
                           ? ps.hScrollOffset - kHScrollStep : 0;
        return true;
    }
    if (event == Event::ArrowRight) {
        activeState().hScrollOffset += kHScrollStep;
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
        clampSelectedFilter();
        const FilterDef& def = chain_.filterAt(selectedFilter_);
        enterInputMode(InputMode::FilterEdit, "Edit> ", def.pattern);
        return true;
    }
    if (event == Event::Character('d')) {
        if (chain_.filterCount() == 0) return true;
        clampSelectedFilter();
        chain_.remove(selectedFilter_);
        clampSelectedFilter();
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
            clampSelectedFilter();
            FilterDef def = chain_.filterAt(selectedFilter_);
            def.enabled   = !def.enabled;
            chain_.edit(selectedFilter_, std::move(def));
            triggerReprocess(selectedFilter_);
        }
        return true;
    }
    if (event == Event::Character('x')) {
        if (chain_.filterCount() > 0) {
            clampSelectedFilter();
            chain_.toggleUseRegex(selectedFilter_);
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
        stepSearch(+1);
        return true;
    }
    if (event == Event::Character('p')) {
        stepSearch(-1);
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

    // ── Phase 3 keys ──────────────────────────────────────────────────────────

    if (event == Event::Character('l')) {
        showLineNumbers_ = !showLineNumbers_;
        return true;
    }
    if (event == Event::Character('z')) {
        toggleFoldCurrentLine();
        return true;
    }
    if (event == Event::Character('h')) {
        showDialog_      = true;
        dialogTitle_     = std::string("ttLogViewer v") + TTLOGVIEWER_VERSION + "  帮助";
        dialogBody_      = std::string(kHelpText);
        dialogHasChoice_ = false;
        return true;
    }
    if (event == Event::Character('w')) {
        // Generate export filename: <stem>_filtered_<timestamp>.txt
        std::string fp = std::string(reader_.filePath());
        if (fp.empty()) return true;  // No file open, nothing to export

        namespace fs = std::filesystem;
        const std::string stem = fs::path(fp).stem().string();
        const std::string dir  = fs::path(fp).parent_path().string();

        auto now = std::chrono::system_clock::now();
        const std::string ts = std::format("{:%Y%m%d_%H%M%S}", now);

        exportPath_ = (dir.empty() ? "." : dir) + "/" +
                      stem + "_filtered_" + ts + ".txt";
        enterInputMode(InputMode::ExportConfirm, "Export: ", exportPath_);
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
        searchIndex_      = 0;
        searchKeyword_.clear();
        searchInFiltered_ = false;
        searchRegex_.reset();
        exitInputMode();
        return true;
    }

    // Special handling for Return key
    if (event == Event::Return) {
        runSearch(inputBuffer_);
        exitInputMode();
        return true;
    }

    // Tab: toggle regex / literal search mode
    if (event == Event::Tab) {
        searchUseRegex_ = !searchUseRegex_;
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

// ── ExportConfirm mode ────────────────────────────────────────────────────────

bool AppController::handleKeyExportConfirm(const Event& event) {
    if (event == Event::Return) {
        const std::string path = inputBuffer_;
        exitInputMode();

        if (path.empty()) return true;

        namespace fs = std::filesystem;

        // Create parent directory if needed
        std::error_code ec;
        const auto parent = fs::path(path).parent_path();
        if (!parent.empty())
            fs::create_directories(parent, ec);
        if (ec) {
            showErrorDialog("导出失败", "无法创建目录:\n" + parent.string());
            return true;
        }

        std::ofstream out{path};
        if (!out) {
            showErrorDialog("导出失败", "无法写入文件:\n" + path);
            return true;
        }

        const size_t filteredCount = chain_.filteredLineCount();
        if (filteredCount > 0) {
            // Export filtered lines
            for (size_t i = 0; i < filteredCount; ++i) {
                size_t rawLineNo = chain_.filteredLineAt(i);
                out << reader_.getLine(rawLineNo) << '\n';
            }
        } else {
            // No active filter: export all raw lines
            const size_t total = reader_.lineCount();
            for (size_t i = 1; i <= total; ++i)
                out << reader_.getLine(i) << '\n';
        }

        showErrorDialog("已保存", "文件已导出到:\n" + path);
        return true;
    }

    // Allow editing the path, or press Escape to cancel
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

// Returns all byte-ranges in `content` where `keyword` occurs.
// When useRegex is true, uses the pre-compiled regex; falls back to literal on error.
static std::vector<SearchSpan> computeSearchSpans(
        std::string_view content,
        const std::string& keyword,
        bool useRegex,
        const std::optional<std::regex>& regex) {
    std::vector<SearchSpan> result;
    if (keyword.empty()) return result;
    if (useRegex && regex) {
        std::string s(content);
        auto it  = std::sregex_iterator(s.begin(), s.end(), *regex);
        auto end = std::sregex_iterator();
        for (; it != end; ++it)
            result.push_back({static_cast<size_t>(it->position()),
                              static_cast<size_t>(it->position() + it->length())});
    } else {
        size_t pos = 0;
        while ((pos = content.find(keyword, pos)) != std::string_view::npos) {
            result.push_back({pos, pos + keyword.size()});
            pos += keyword.size();
        }
    }
    return result;
}

void AppController::buildRawPane(ViewData& data) {
    const size_t total = reader_.lineCount();
    const size_t ph    = static_cast<size_t>(lastRawPaneHeight_);

    clampScroll(rawState_, total, lastRawPaneHeight_);

    const size_t first = rawState_.scrollOffset;
    const size_t count = (total > first) ? std::min(ph, total - first) : 0;

    data.rawPane.reserve(count);
    size_t maxContentLen = 0;
    for (size_t i = 0; i < count; ++i) {
        const size_t lineNo = first + i + 1;
        LogLine ll;
        ll.rawLineNo   = lineNo;
        ll.content     = reader_.getLine(lineNo);
        ll.colors      = {};   // raw pane does not show filter match colors
        ll.searchSpans = computeSearchSpans(ll.content, searchKeyword_,
                                            searchUseRegex_, searchRegex_);
        ll.highlighted = (first + i == rawState_.cursor);
        ll.folded      = (foldedLines_.count(lineNo) > 0);
        maxContentLen  = std::max(maxContentLen, ll.content.size());
        data.rawPane.push_back(std::move(ll));
    }

    // Clamp horizontal scroll: keep at least one byte of the longest visible line visible.
    // Skip when there are no visible lines (maxContentLen == 0) to avoid losing the offset.
    if (maxContentLen > 0 && rawState_.hScrollOffset >= maxContentLen)
        rawState_.hScrollOffset = maxContentLen - 1;
}

void AppController::buildFilteredPane(ViewData& data) {
    const size_t total = chain_.filteredLineCount();
    const size_t ph    = static_cast<size_t>(lastFilteredPaneHeight_);

    clampScroll(filteredState_, total, lastFilteredPaneHeight_);

    const size_t first = filteredState_.scrollOffset;
    const size_t count = (total > first) ? std::min(ph, total - first) : 0;

    data.filteredPane.reserve(count);
    size_t maxContentLenFilt = 0;
    for (size_t i = 0; i < count; ++i) {
        const size_t rawLineNo = chain_.filteredLineAt(first + i);
        LogLine ll;
        ll.rawLineNo   = rawLineNo;
        ll.content     = reader_.getLine(rawLineNo);
        ll.colors      = chain_.computeColors(rawLineNo, ll.content);
        ll.searchSpans = computeSearchSpans(ll.content, searchKeyword_,
                                            searchUseRegex_, searchRegex_);
        ll.highlighted = (first + i == filteredState_.cursor);
        ll.folded      = (foldedLines_.count(rawLineNo) > 0);
        maxContentLenFilt = std::max(maxContentLenFilt, ll.content.size());
        data.filteredPane.push_back(std::move(ll));
    }

    // Clamp horizontal scroll: keep at least one byte of the longest visible line visible.
    // Skip when there are no visible lines (maxContentLenFilt == 0) to avoid losing the offset.
    if (maxContentLenFilt > 0 && filteredState_.hScrollOffset >= maxContentLenFilt)
        filteredState_.hScrollOffset = maxContentLenFilt - 1;
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
    data.rawHScroll      = rawState_.hScrollOffset;
    data.filtHScroll     = filteredState_.hScrollOffset;
    data.inputMode       = inputMode_;
    data.inputPrompt     = inputPrompt_;
    data.inputBuffer     = inputBuffer_;
    data.inputValid      = inputValid_;

    data.showDialog      = showDialog_;
    data.dialogTitle     = dialogTitle_;
    data.dialogBody      = dialogBody_;
    data.dialogHasChoice = dialogHasChoice_;

    data.showProgress    = showProgress_;
    data.progress        = progress_;
    data.showLineNumbers = showLineNumbers_;
    data.terminalWidth   = lastTerminalWidth_;

    data.searchKeyword     = searchKeyword_;
    data.searchResultCount = searchResults_.size();
    data.searchResultIndex = searchResults_.empty() ? 0 : searchIndex_ + 1;
    data.searchActive      = !searchKeyword_.empty();
    data.searchUseRegex    = searchUseRegex_;

    buildRawPane(data);
    buildFilteredPane(data);

    // ── Filter tags ───────────────────────────────────────────────────────────
    data.filterTags.reserve(chain_.filterCount());
    for (size_t i = 0; i < chain_.filterCount(); ++i) {
        const FilterDef& def = chain_.filterAt(i);
        ViewData::FilterTag tag;
        tag.number     = static_cast<int>(i) + 1;
        tag.pattern    = def.pattern;
        tag.color      = def.color;
        tag.enabled    = def.enabled;
        tag.exclude    = def.exclude;
        tag.selected   = (i == selectedFilter_);
        tag.useRegex   = def.useRegex;
        tag.matchCount = chain_.filteredLineCountAt(i);
        data.filterTags.push_back(std::move(tag));
    }

    return data;
}

// ── Resize ────────────────────────────────────────────────────────────────────

void AppController::onTerminalResize(int width, int height) {
    if (width > 0) lastTerminalWidth_ = width;
    const int overhead  = AppConfig::global().uiOverheadRows;
    const int available = std::max(2, height - overhead);
    lastRawPaneHeight_      = available * 6 / 10;
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
    if (inputBuffer_.empty()) { inputValid_ = false; return; }

    // Determine whether the current filter uses regex mode.
    // FilterAdd: new filters always start as string mode (useRegex=false).
    // FilterEdit: inherit the filter's current mode.
    bool useRegex = false;
    if (inputMode_ == InputMode::FilterEdit && selectedFilter_ < chain_.filterCount())
        useRegex = chain_.filterAt(selectedFilter_).useRegex;

    if (!useRegex) {
        inputValid_ = true;  // Any non-empty literal string is valid
        return;
    }

    try {
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
    showProgress_          = true;
    progress_              = 0.0;
    reprocessStartTime_    = std::chrono::steady_clock::now();
    reprocessTimeoutShown_ = false;

    // The onProgress / onDone callbacks are invoked from FilterChain's postFn
    // context, which is already the UI thread. No additional wrapping needed.
    chain_.reprocess(fromFilter,
        [this](double p) {
            progress_ = p;
            // Show a one-time confirmation dialog if filtering takes too long.
            if (!reprocessTimeoutShown_ && !showDialog_) {
                using namespace std::chrono;
                auto elapsed = duration_cast<seconds>(
                    steady_clock::now() - reprocessStartTime_).count();
                if (elapsed >= kReprocessTimeoutSeconds) {
                    reprocessTimeoutShown_ = true;
                    showDialog_      = true;
                    dialogTitle_     = "过滤耗时较长";
                    dialogBody_      = "过滤已进行超过30秒，是否继续等待？";
                    dialogHasChoice_ = true;
                    dialogYesAction_ = [this] { closeDialog(); };
                    dialogNoAction_  = [this] {
                        chain_.cancelReprocess();
                        showProgress_          = false;
                        progress_              = 0.0;
                        reprocessTimeoutShown_ = false;
                        closeDialog();
                    };
                }
            }
        },
        [this]() {
            showProgress_          = false;
            progress_              = 1.0;
            reprocessTimeoutShown_ = false;
        });
}

void AppController::requestQuit(std::function<void()> exitFn) {
    if (showDialog_) return;   // another dialog already open, ignore
    showDialog_      = true;
    dialogTitle_     = "退出确认";
    dialogBody_      = "确认退出 ttLogViewer？";
    dialogHasChoice_ = true;
    dialogYesAction_ = std::move(exitFn);
    dialogNoAction_  = [this] { closeDialog(); };
}

bool AppController::isDialogOpen() const { return showDialog_; }

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
    searchIndex_      = 0;
    searchKeyword_    = keyword;
    searchInFiltered_ = (focus_ == FocusArea::Filtered);
    searchRegex_.reset();

    if (keyword.empty()) return;

    // Compile regex once if in regex mode (fall back to literal on error).
    if (searchUseRegex_) {
        try {
            searchRegex_ = std::regex(keyword);
        } catch (const std::regex_error&) {
            searchUseRegex_ = false;   // invalid pattern → silently use literal
        }
    }

    auto lineMatches = [&](std::string_view line) -> bool {
        if (searchUseRegex_ && searchRegex_) {
            std::string s(line);
            return std::regex_search(s, *searchRegex_);
        }
        return line.find(keyword) != std::string_view::npos;
    };

    if (!searchInFiltered_) {
        // Scan all raw lines
        const size_t total = reader_.lineCount();
        // Reserve space to avoid frequent reallocations during search.
        // Estimate: most searches match fewer than 1/searchReserveFraction of lines,
        // but we cap the allocation at searchReserveMax on very large files.
        const size_t reserveEstimate = total / static_cast<size_t>(
            AppConfig::global().searchReserveFraction);
        searchResults_.reserve(std::min(reserveEstimate,
                                        AppConfig::global().searchReserveMax));
        for (size_t i = 1; i <= total; ++i) {
            if (lineMatches(reader_.getLine(i)))
                searchResults_.push_back(i);
        }
    } else {
        // Scan only filtered lines
        const size_t total = chain_.filteredLineCount();
        for (size_t i = 0; i < total; ++i) {
            const size_t rawNo = chain_.filteredLineAt(i);
            if (lineMatches(reader_.getLine(rawNo)))
                searchResults_.push_back(rawNo);
        }
    }

    if (!searchResults_.empty()) {
        searchIndex_ = 0;
        jumpToSearchResult(0);
    }
}

void AppController::jumpToSearchResult(size_t idx) {
    if (idx >= searchResults_.size()) return;
    const size_t rawNo = searchResults_[idx];

    if (!searchInFiltered_) {
        jumpToRawLine(rawNo);
        return;
    }

    // Find the position of rawNo in the filtered list and move the cursor there
    const size_t total = chain_.filteredLineCount();
    for (size_t i = 0; i < total; ++i) {
        if (chain_.filteredLineAt(i) == rawNo) {
            filteredState_.cursor = i;
            clampScroll(filteredState_, total, lastFilteredPaneHeight_);
            return;
        }
    }
}

// ── stepSearch ────────────────────────────────────────────────────────────────

void AppController::stepSearch(int dir) {
    const size_t resultCount = searchResults_.size();
    if (resultCount == 0) return;

    if (searchIndex_ >= resultCount) {
        // Index became stale (e.g. after a new search with fewer results).
        // Reset to a valid boundary before stepping.
        searchIndex_ = (dir >= 0) ? 0 : resultCount - 1;
    } else if (dir >= 0) {
        searchIndex_ = (searchIndex_ + 1) % resultCount;
    } else {
        searchIndex_ = (searchIndex_ == 0) ? resultCount - 1 : searchIndex_ - 1;
    }
    jumpToSearchResult(searchIndex_);
}

// ── clampSelectedFilter ───────────────────────────────────────────────────────

void AppController::clampSelectedFilter() {
    if (chain_.filterCount() == 0) {
        selectedFilter_ = 0;
    } else if (selectedFilter_ >= chain_.filterCount()) {
        selectedFilter_ = chain_.filterCount() - 1;
    }
}

// ── toggleFoldCurrentLine ─────────────────────────────────────────────────────

void AppController::toggleFoldCurrentLine() {
    size_t rawLine = 0;
    if (focus_ == FocusArea::Raw) {
        const size_t total = reader_.lineCount();
        if (total > 0)
            rawLine = rawState_.cursor + 1;  // cursor is 0-based
    } else {
        const size_t total = chain_.filteredLineCount();
        if (total > 0 && filteredState_.cursor < total)
            rawLine = chain_.filteredLineAt(filteredState_.cursor);
    }
    if (rawLine > 0) {
        if (foldedLines_.count(rawLine))
            foldedLines_.erase(rawLine);
        else
            foldedLines_.insert(rawLine);
    }
}

// ── Mouse-support helpers ──────────────────────────────────────────────────────

void AppController::scrollPane(FocusArea area, int delta) {
    PaneState& ps    = (area == FocusArea::Raw) ? rawState_ : filteredState_;
    const size_t total = (area == FocusArea::Raw)
                         ? reader_.lineCount()
                         : chain_.filteredLineCount();
    const int ph = (area == FocusArea::Raw)
                   ? lastRawPaneHeight_
                   : lastFilteredPaneHeight_;
    if (total == 0) return;
    if (delta < 0) {
        const size_t step = static_cast<size_t>(-delta);
        ps.cursor = (ps.cursor >= step) ? ps.cursor - step : 0;
    } else {
        ps.cursor = std::min(ps.cursor + static_cast<size_t>(delta), total - 1);
    }
    clampScroll(ps, total, ph);
    followTail_ = false;
}

void AppController::setFocus(FocusArea area) {
    focus_ = area;
}
