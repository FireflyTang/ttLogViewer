#include "app_controller.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <regex>
#include <string>

#include "app_config.hpp"
#include "clipboard.hpp"
#include "version.hpp"

using namespace ftxui;

// ── File-scope constants ───────────────────────────────────────────────────────

// Full help text shown by the 'h' key.  Extracted here to keep handleModeKeys()
// readable and to make it easy to update the text in one place.

static constexpr std::string_view kHelpText =
    "↑↓: 移动光标\n"
    "PgUp/PgDn: 翻页\n"
    "Home/End: 首/末行\n"
    "Tab: 切换区域\n"
    "←/→: 水平滚动\n"
    "a/e: 添加/编辑过滤器 (输入时 Tab 循环四模式: 字符串·匹配→字符串·排除→正则·匹配→正则·排除)\n"
    "d: 删除过滤器\n"
    "[/]: 选择过滤器\n"
    "+/-: 调整过滤器顺序\n"
    "Space: 启停过滤器\n"
    "/: 搜索 (输入时 Tab 切换正则/字符串)\n"
    "n/p: 下/上一搜索结果  Esc:清除搜索\n"
    "鼠标拖拽: 字符级文本选择  Ctrl+C:复制  Esc:取消选择\n"
    "输入模式下 Ctrl+V: 从剪贴板粘贴\n"
    "g: 跳转行号\n"
    "G: 跟随末尾\n"
    "o: 打开文件\n"
    "s: 切换静态/实时模式\n"
    "f: 强制检查\n"
    "l: 行号显示切换\n"
    "z: 折叠/展开超长行\n"
    "w: 导出过滤结果\n"
    "x: 跳转原始行（过滤窗格中）\n"
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
    // Move raw pane cursor to the given 1-based raw line and center it in the viewport.
    if (rawLineNo == 0 || rawLineNo > reader_.lineCount()) return;
    const size_t idx = rawLineNo - 1;
    rawState_.cursor = idx;
    if (lastRawPaneHeight_ > 0) {
        const size_t half = static_cast<size_t>(lastRawPaneHeight_ / 2);
        rawState_.scrollOffset = (idx >= half) ? idx - half : 0;
    }
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

    // ESC in None mode with an active selection → clear it (higher priority than search).
    if (event == Event::Escape && selection_.active && inputMode_ == InputMode::None) {
        clearSelection();
        return true;
    }

    // ESC in None mode with an active search → clear search state.
    if (event == Event::Escape && !searchKeyword_.empty() && inputMode_ == InputMode::None) {
        clearSearch();
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
        clearSearch();
        focus_ = (focus_ == FocusArea::Raw) ? FocusArea::Filtered : FocusArea::Raw;
        return true;
    }

    // Horizontal scroll
    if (event == Event::ArrowLeft) {
        PaneState& ps = activeState();
        const size_t step = static_cast<size_t>(AppConfig::global().hScrollStep);
        ps.hScrollOffset = (ps.hScrollOffset >= step) ? ps.hScrollOffset - step : 0;
        return true;
    }
    if (event == Event::ArrowRight) {
        activeState().hScrollOffset += static_cast<size_t>(AppConfig::global().hScrollStep);
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
        if (focus_ == FocusArea::Filtered) {
            const size_t total = chain_.filteredLineCount();
            if (filteredState_.cursor < total) {
                const size_t rawNo = chain_.filteredLineAt(filteredState_.cursor);
                jumpToRawLine(rawNo);
            }
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
        // Toggle between static and realtime mode.
        if (reader_.mode() == FileMode::Static) {
            reader_.setMode(FileMode::Realtime);
        } else {
            reader_.setMode(FileMode::Static);
            followTail_ = false;
        }
        return true;
    }
    if (event == Event::Character('f')) {
        reader_.forceCheck();
        return true;
    }
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
            def.pattern  = inputBuffer_;
            def.color    = nextPaletteColor(colorPaletteIdx_);
            def.useRegex = filterInputUseRegex_;
            def.exclude  = filterInputExclude_;
            chain_.append(std::move(def));
            selectedFilter_ = chain_.filterCount() - 1;
            triggerReprocess(selectedFilter_);
        } else {  // FilterEdit
            // Validate selectedFilter_ before access (defensive)
            if (selectedFilter_ < chain_.filterCount()) {
                FilterDef def = chain_.filterAt(selectedFilter_);
                def.pattern  = inputBuffer_;
                def.useRegex = filterInputUseRegex_;
                def.exclude  = filterInputExclude_;
                chain_.edit(selectedFilter_, std::move(def));
                triggerReprocess(selectedFilter_);
            }
        }
        exitInputMode();
        return true;
    }

    // Tab: cycle through 4 modes: str-include → str-exclude → regex-include → regex-exclude
    if (event == Event::Tab) {
        if (!filterInputUseRegex_ && !filterInputExclude_) {
            filterInputExclude_ = true;
        } else if (!filterInputUseRegex_ && filterInputExclude_) {
            filterInputUseRegex_ = true;
            filterInputExclude_  = false;
        } else if (filterInputUseRegex_ && !filterInputExclude_) {
            filterInputExclude_ = true;
        } else {
            filterInputUseRegex_ = false;
            filterInputExclude_  = false;
        }
        validateInputRegex();
        return true;
    }

    // Handle common input keys (Escape, Backspace, characters)
    return handleCommonInputKeys(event, true);
}

// ── Search mode ───────────────────────────────────────────────────────────────

bool AppController::handleKeySearch(const Event& event) {
    // Special handling for Escape - also clear search results
    if (event == Event::Escape) {
        clearSearch();
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
        validateInputRegex();
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
        inputBuffer_.insert(inputCursorPos_, event.character());
        inputCursorPos_ += 1;
        return true;
    }

    // Handle common keys (Escape, Backspace) but don't accept all characters
    return handleCommonInputKeys(event, false);
}

// ── Tab completion helpers ─────────────────────────────────────────────────────

namespace {

// Maximum visible rows in the completion popup (must match render.cpp).
static constexpr size_t kCompletionMaxVisible = 3;

// Split a path into (directory_prefix, filename_prefix).
// The directory prefix is everything up to and including the last '/' or '\'.
// If no separator, dir = "" (meaning current directory).
// UTF-8 safe: '/' (0x2F) and '\' (0x5C) never appear as continuation bytes.
static std::pair<std::string, std::string> splitPathForCompletion(const std::string& input) {
    const auto pos = input.find_last_of("/\\");
    if (pos == std::string::npos)
        return {"", input};
    return {input.substr(0, pos + 1), input.substr(pos + 1)};
}

// Return sorted list of filenames (not full paths) in the given directory
// whose names start with filePrefix.  Directories have '/' appended.
// Empty filePrefix = list all entries.  Errors are silently ignored.
static std::vector<std::string> getFileCompletions(const std::string& dirStr,
                                                    const std::string& filePrefix) {
    namespace fs = std::filesystem;
    std::vector<std::string> results;

    // Use current directory when no directory part was given.
    fs::path dir = dirStr.empty() ? fs::current_path() : fs::path(dirStr);

    std::error_code ec;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        // u8string() returns std::u8string in C++20; convert to std::string via range ctor.
        // The byte content is identical (UTF-8), so this is a safe reinterpretation.
        const auto u8name = entry.path().filename().u8string();
        std::string name(u8name.begin(), u8name.end());
        if (filePrefix.empty() || name.starts_with(filePrefix)) {
            if (entry.is_directory(ec))
                name += '/';
            results.push_back(std::move(name));
        }
    }
    std::sort(results.begin(), results.end());
    return results;
}

}  // anonymous namespace

// ── OpenFile mode ─────────────────────────────────────────────────────────────

bool AppController::handleKeyOpenFile(const Event& event) {
    // Helper: ensure completionIndex_ is visible within the scroll window.
    auto ensureCompletionVisible = [this]() {
        const size_t showCount = std::min(completions_.size(), kCompletionMaxVisible);
        if (completionIndex_ < completionScrollOffset_)
            completionScrollOffset_ = completionIndex_;
        else if (completionIndex_ >= completionScrollOffset_ + showCount)
            completionScrollOffset_ = completionIndex_ - showCount + 1;
    };

    // ── Tab: trigger or cycle completions ─────────────────────────────────────
    if (event == Event::Tab) {
        if (showCompletions_ && !completions_.empty()) {
            completionIndex_ = (completionIndex_ + 1) % completions_.size();
            ensureCompletionVisible();
        } else {
            triggerCompletion();
        }
        return true;
    }

    // ── Completion popup navigation ────────────────────────────────────────────
    if (showCompletions_ && !completions_.empty()) {
        if (event == Event::ArrowDown) {
            completionIndex_ = (completionIndex_ + 1) % completions_.size();
            ensureCompletionVisible();
            return true;
        }
        if (event == Event::ArrowUp) {
            completionIndex_ = (completionIndex_ == 0)
                               ? completions_.size() - 1
                               : completionIndex_ - 1;
            ensureCompletionVisible();
            return true;
        }
        if (event == Event::Return) {
            acceptCompletion();
            return true;
        }
        if (event == Event::Escape) {
            showCompletions_ = false;
            completions_.clear();
            completionIndex_ = 0;
            completionScrollOffset_ = 0;
            recomputePaneHeights();
            return true;
        }
        // Any character typed closes the popup and falls through to normal input.
        if (event.is_character()) {
            showCompletions_ = false;
            completions_.clear();
            completionIndex_ = 0;
            completionScrollOffset_ = 0;
            recomputePaneHeights();
            // Fall through to handleCommonInputKeys below.
        }
    }

    // ── Return: open the file ─────────────────────────────────────────────────
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

// ── triggerCompletion ─────────────────────────────────────────────────────────

void AppController::triggerCompletion() {
    auto [dir, filePrefix] = splitPathForCompletion(inputBuffer_);
    completions_ = getFileCompletions(dir, filePrefix);
    completionIndex_ = 0;
    completionScrollOffset_ = 0;

    if (completions_.empty()) {
        showCompletions_ = false;
    } else if (completions_.size() == 1) {
        // Single match: fill in directly and hide popup.
        const std::string candidate = completions_[0];
        inputBuffer_ = dir + candidate;
        inputCursorPos_ = inputBuffer_.size();
        completions_.clear();
        showCompletions_ = false;
        recomputePaneHeights();
        // If it ended in '/', recurse to show the next level.
        if (candidate.back() == '/')
            triggerCompletion();
    } else {
        showCompletions_ = true;
        recomputePaneHeights();
    }
}

// ── acceptCompletion ──────────────────────────────────────────────────────────

void AppController::acceptCompletion() {
    if (completions_.empty()) return;
    const std::string candidate = completions_[completionIndex_];
    auto [dir, _unused] = splitPathForCompletion(inputBuffer_);
    inputBuffer_ = dir + candidate;
    inputCursorPos_ = inputBuffer_.size();
    showCompletions_ = false;
    completions_.clear();
    completionIndex_ = 0;
    completionScrollOffset_ = 0;
    recomputePaneHeights();
    // If user selected a directory, immediately show the next level.
    if (candidate.back() == '/')
        triggerCompletion();
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
    selection_.clear();  // Content changed — auto-clear selection

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
    selection_.clear();  // Content changed — auto-clear selection
    showDialog_      = true;
    dialogTitle_     = "文件已重置";
    dialogBody_      = "检测到文件被截断或替换。\n是否重新加载?";
    dialogHasChoice_ = true;

    std::string pathCopy{reader_.filePath()};

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

void AppController::buildPane(FocusArea area, ViewData& data) {
    const bool isRaw     = (area == FocusArea::Raw);
    PaneState& ps        = isRaw ? rawState_      : filteredState_;
    const int  paneH     = isRaw ? lastRawPaneHeight_ : lastFilteredPaneHeight_;
    const size_t total   = isRaw ? reader_.lineCount() : chain_.filteredLineCount();
    const size_t ph      = static_cast<size_t>(paneH);
    auto& output         = isRaw ? data.rawPane    : data.filteredPane;

    clampScroll(ps, total, paneH);

    const size_t first = ps.scrollOffset;
    const size_t count = (total > first) ? std::min(ph, total - first) : 0;

    // Only highlight the keyword on the current search result line (not every match).
    const size_t currentResultRawLine =
        (!searchKeyword_.empty() && !searchResults_.empty())
        ? searchResults_[searchIndex_] : 0;

    // Build selection span using ABSOLUTE pane index.
    auto selSpans = [&](size_t absIdx, size_t contentSize) -> std::vector<SelectionSpan> {
        if (!selection_.active || selection_.anchor.pane != area) return {};
        auto [startPt, endPt] = selection_.ordered();
        if (absIdx < startPt.lineIndex || absIdx > endPt.lineIndex) return {};
        const size_t bStart = (absIdx == startPt.lineIndex) ? startPt.byteOffset : 0;
        const size_t bEnd   = (absIdx == endPt.lineIndex)   ? endPt.byteOffset   : contentSize;
        if (bStart >= bEnd) return {};
        return {{ bStart, bEnd }};
    };

    output.reserve(count);
    size_t maxContentLen = 0;
    for (size_t i = 0; i < count; ++i) {
        const size_t rawLineNo = isRaw ? (first + i + 1) : chain_.filteredLineAt(first + i);
        LogLine ll;
        ll.rawLineNo      = rawLineNo;
        ll.content        = reader_.getLine(rawLineNo);
        ll.colors         = isRaw ? std::vector<ColorSpan>{}
                                  : chain_.computeColors(rawLineNo, ll.content);
        ll.searchSpans    = (rawLineNo == currentResultRawLine)
            ? computeSearchSpans(ll.content, searchKeyword_, searchUseRegex_, searchRegex_)
            : std::vector<SearchSpan>{};
        ll.selectionSpans = selSpans(first + i, ll.content.size());
        ll.highlighted    = (first + i == ps.cursor);
        ll.folded         = (foldedLines_.count(rawLineNo) > 0);
        maxContentLen     = std::max(maxContentLen, ll.content.size());
        output.push_back(std::move(ll));
    }

    // Clamp horizontal scroll: keep at least one byte of the longest visible line visible.
    if (maxContentLen > 0 && ps.hScrollOffset >= maxContentLen)
        ps.hScrollOffset = maxContentLen - 1;
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
    data.inputMode        = inputMode_;
    data.inputPrompt      = inputPrompt_;
    data.inputBuffer      = inputBuffer_;
    data.inputCursorPos   = inputCursorPos_;
    data.inputValid      = inputValid_;
    data.inputUseRegex   = (inputMode_ == InputMode::Search) ? searchUseRegex_
                                                              : filterInputUseRegex_;
    data.inputExclude    = filterInputExclude_;

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
    data.hasSelection      = selection_.active;

    // ── Completion popup ──────────────────────────────────────────────────────
    data.showCompletions       = showCompletions_;
    data.completions           = completions_;
    data.completionIndex       = completionIndex_;
    data.completionScrollOffset = completionScrollOffset_;
    if (showCompletions_ && !completions_.empty()) {
        // completionCol = display column where the filename part starts in the input line.
        // = len(inputPrompt_) + display width of directory prefix.
        auto [dir, _unused] = splitPathForCompletion(inputBuffer_);
        const size_t dirBytes = dir.empty() ? 0 : dir.size();
        const int dirDisplayCols = static_cast<int>(byteOffsetToDisplayCol(inputBuffer_, 0, dirBytes));
        data.completionCol = static_cast<int>(inputPrompt_.size()) + dirDisplayCols;
    }

    buildPane(FocusArea::Raw, data);
    buildPane(FocusArea::Filtered, data);

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
    if (width  > 0) lastTerminalWidth_  = width;
    if (height > 0) lastTerminalHeight_ = height;
    recomputePaneHeights();
    clampScroll(rawState_,      reader_.lineCount(),        lastRawPaneHeight_);
    clampScroll(filteredState_, chain_.filteredLineCount(), lastFilteredPaneHeight_);
}

void AppController::recomputePaneHeights() {
    if (lastTerminalHeight_ <= 0) return;   // not yet set (test environment)
    const int extra = (inputMode_ != InputMode::None || !searchKeyword_.empty()) ? 1 : 0;
    // Completion popup is rendered as a dbox overlay and does not consume pane height.
    const int avail = std::max(2, lastTerminalHeight_
                                    - AppConfig::global().uiOverheadRows - extra);
    const int rawH  = static_cast<int>(avail * AppConfig::global().rawPaneFraction);
    lastRawPaneHeight_      = std::max(1, rawH);
    lastFilteredPaneHeight_ = std::max(1, avail - rawH);
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
    inputCursorPos_ = inputBuffer_.size();  // place cursor at end of any prefill text
    // Initialize local regex/exclude toggles from filter state (FilterEdit) or defaults.
    const bool isEdit = (mode == InputMode::FilterEdit
                         && selectedFilter_ < chain_.filterCount());
    filterInputUseRegex_ = isEdit ? chain_.filterAt(selectedFilter_).useRegex  : false;
    filterInputExclude_  = isEdit ? chain_.filterAt(selectedFilter_).exclude   : false;
    inputValid_  = (mode == InputMode::FilterEdit && !inputBuffer_.empty());
    if (inputMode_ == InputMode::FilterAdd || inputMode_ == InputMode::FilterEdit)
        validateInputRegex();
    recomputePaneHeights();
}

void AppController::exitInputMode() {
    inputMode_          = InputMode::None;
    inputBuffer_.clear();
    inputPrompt_.clear();
    inputCursorPos_     = 0;
    inputValid_         = false;
    filterInputExclude_ = false;
    // Clear any active completion popup.
    showCompletions_ = false;
    completions_.clear();
    completionIndex_ = 0;
    completionScrollOffset_ = 0;
    recomputePaneHeights();
}

void AppController::validateInputRegex() {
    if (inputBuffer_.empty()) { inputValid_ = false; return; }

    // Determine regex mode from the local state of the active input.
    bool useRegex = false;
    if (inputMode_ == InputMode::FilterAdd || inputMode_ == InputMode::FilterEdit)
        useRegex = filterInputUseRegex_;
    else if (inputMode_ == InputMode::Search)
        useRegex = searchUseRegex_;

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

    // ── Cursor movement within the input buffer (UTF-8 safe) ──────────────────
    if (event == Event::ArrowLeft) {
        if (inputCursorPos_ > 0) {
            --inputCursorPos_;
            while (inputCursorPos_ > 0 && !isUtf8Boundary(inputBuffer_, inputCursorPos_))
                --inputCursorPos_;
        }
        return true;
    }
    if (event == Event::ArrowRight) {
        if (inputCursorPos_ < inputBuffer_.size())
            decodeUtf8Codepoint(inputBuffer_, inputCursorPos_);  // advances past one codepoint
        return true;
    }
    if (event == Event::Home) {
        inputCursorPos_ = 0;
        return true;
    }
    if (event == Event::End) {
        inputCursorPos_ = inputBuffer_.size();
        return true;
    }

    // Handle Ctrl+V — paste from system clipboard at cursor position
    if (event == Event::CtrlV) {
        std::string pasted;
        const ClipboardResult result = clipboardPaste(pasted);
        if (result == ClipboardResult::Ok) {
            inputBuffer_.insert(inputCursorPos_, pasted);
            inputCursorPos_ += pasted.size();
            if (inputMode_ == InputMode::FilterAdd || inputMode_ == InputMode::FilterEdit
                    || inputMode_ == InputMode::Search)
                validateInputRegex();
        } else if (result == ClipboardResult::MultiLine) {
            showErrorDialog("粘贴失败", "剪贴板内容含多行，无法粘贴");
        } else if (result == ClipboardResult::NotText) {
            showErrorDialog("粘贴失败", "剪贴板内容不是文本");
        }
        // ClipboardResult::Empty and ::Error are silently ignored
        return true;
    }

    // Handle Backspace key
    if (event == Event::Backspace) {
        return handleInputBackspace();
    }

    // Handle Delete key (delete character AFTER cursor)
    if (event == Event::Delete) {
        return handleInputDelete();
    }

    // Handle character input if allowed
    if (allowCharacters && event.is_character()) {
        inputBuffer_.insert(inputCursorPos_, event.character());
        inputCursorPos_ += event.character().size();
        // Validate for filter and search input modes
        if (inputMode_ == InputMode::FilterAdd || inputMode_ == InputMode::FilterEdit
                || inputMode_ == InputMode::Search) {
            validateInputRegex();
        }
        return true;
    }

    return false;
}

bool AppController::handleInputBackspace() {
    if (inputCursorPos_ == 0) return true;  // nothing before cursor
    // Find the start of the UTF-8 codepoint immediately before the cursor.
    size_t start = inputCursorPos_ - 1;
    while (start > 0 && !isUtf8Boundary(inputBuffer_, start))
        --start;
    inputBuffer_.erase(start, inputCursorPos_ - start);
    inputCursorPos_ = start;
    // Validate for filter and search input modes
    if (inputMode_ == InputMode::FilterAdd || inputMode_ == InputMode::FilterEdit
            || inputMode_ == InputMode::Search) {
        validateInputRegex();
    }
    return true;
}

bool AppController::handleInputDelete() {
    if (inputCursorPos_ >= inputBuffer_.size()) return true;  // nothing after cursor
    // Find the end of the UTF-8 codepoint at the cursor position.
    size_t end = inputCursorPos_ + 1;
    while (end < inputBuffer_.size() && !isUtf8Boundary(inputBuffer_, end))
        ++end;
    inputBuffer_.erase(inputCursorPos_, end - inputCursorPos_);
    // Cursor stays at the same position.
    // Validate for filter and search input modes
    if (inputMode_ == InputMode::FilterAdd || inputMode_ == InputMode::FilterEdit
            || inputMode_ == InputMode::Search) {
        validateInputRegex();
    }
    return true;
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
                if (elapsed >= AppConfig::global().reprocessTimeoutSeconds) {
                    reprocessTimeoutShown_ = true;
                    showDialog_      = true;
                    dialogTitle_     = "过滤耗时较长";
                    dialogBody_      = std::format("过滤已进行超过{}秒，是否继续等待？",
                                                   AppConfig::global().reprocessTimeoutSeconds);
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
            selection_.clear();  // Filter output changed — auto-clear selection
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
        const int fraction = AppConfig::global().searchReserveFraction;
        const size_t reserveEstimate = (fraction > 0)
            ? total / static_cast<size_t>(fraction) : 0;
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
    recomputePaneHeights();
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
            if (lastFilteredPaneHeight_ > 0) {
                const size_t half = static_cast<size_t>(lastFilteredPaneHeight_ / 2);
                filteredState_.scrollOffset = (i >= half) ? i - half : 0;
            }
            clampScroll(filteredState_, total, lastFilteredPaneHeight_);
            return;
        }
    }
}

// ── clearSearch ───────────────────────────────────────────────────────────────

void AppController::clearSearch() {
    searchResults_.clear();
    searchIndex_      = 0;
    searchKeyword_.clear();
    searchInFiltered_ = false;
    searchRegex_.reset();
    recomputePaneHeights();
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
    if (focus_ != area)
        selection_.clear();  // Pane switch — auto-clear selection
    focus_ = area;
}

FocusArea AppController::focusArea() const { return focus_; }

void AppController::clickLine(FocusArea area, int rowInPane) {
    if (rowInPane < 0) return;
    selection_.clear();  // Plain click clears selection
    PaneState&   ps    = (area == FocusArea::Raw) ? rawState_ : filteredState_;
    const int    ph    = (area == FocusArea::Raw) ? lastRawPaneHeight_ : lastFilteredPaneHeight_;
    const size_t total = (area == FocusArea::Raw) ? reader_.lineCount() : chain_.filteredLineCount();
    if (total == 0) return;
    ps.cursor = std::min(ps.scrollOffset + static_cast<size_t>(rowInPane), total - 1);
    clampScroll(ps, total, ph);
}

// ── SelectionState helpers ────────────────────────────────────────────────────

std::pair<AppController::SelectionPoint, AppController::SelectionPoint>
AppController::SelectionState::ordered() const {
    // Order by lineIndex first, then byteOffset.
    bool anchorFirst = (anchor.lineIndex < current.lineIndex)
                    || (anchor.lineIndex == current.lineIndex
                        && anchor.byteOffset <= current.byteOffset);
    return anchorFirst ? std::make_pair(anchor, current)
                       : std::make_pair(current, anchor);
}

// ── Text selection public methods ─────────────────────────────────────────────

void AppController::startSelection(FocusArea pane, size_t lineIndex, size_t byteOffset) {
    selection_.anchor  = {pane, lineIndex, byteOffset};
    selection_.current = {pane, lineIndex, byteOffset};
    selection_.dragging = true;
    selection_.active   = false;  // Not yet — needs at least one extend
}

void AppController::extendSelection(size_t lineIndex, size_t byteOffset) {
    if (!selection_.dragging) return;
    selection_.current.lineIndex  = lineIndex;
    selection_.current.byteOffset = byteOffset;
    // Consider a selection active when anchor != current (either line or offset differs)
    selection_.active = (selection_.anchor.lineIndex  != selection_.current.lineIndex
                      || selection_.anchor.byteOffset != selection_.current.byteOffset);
}

void AppController::finalizeSelection() {
    selection_.dragging = false;
    // Keep selection_.active unchanged — user must press Esc or click to dismiss.
}

void AppController::clearSelection() {
    selection_.clear();
}

bool AppController::hasSelection() const {
    return selection_.active;
}

bool AppController::isSelectionDragging() const {
    return selection_.dragging;
}

void AppController::copySelectionToClipboard() {
    if (!selection_.active) return;
    const std::string text = buildSelectedText();
    if (!text.empty())
        clipboardCopy(text);
}

// ── screenColToByteOffset ─────────────────────────────────────────────────────
// Maps a raw FTXUI mouse m.x (0-based terminal column) to a byte offset within
// the line content, accounting for the rendered prefix (line-number + cursor
// arrow) and horizontal scroll.  Returns 0 if the click falls within the prefix.

size_t AppController::screenColToByteOffset(FocusArea pane, size_t lineIndex, int screenCol) const {
    // Compute the prefix width: [lineNo digits + space] + ["▶ " or "  "].
    // Matches the formula used by renderLogPane() in render.cpp.
    int prefixCols = 2;  // cursor arrow "▶ " or "  "
    if (showLineNumbers_) {
        const int minW       = AppConfig::global().minLineNoWidth;
        const int digitCount = static_cast<int>(std::to_string(reader_.lineCount()).size());
        prefixCols += std::max(digitCount, minW) + 1;  // digits + trailing space
    }

    const int contentCol = screenCol - prefixCols;
    if (contentCol < 0) return 0;  // click landed on the prefix — treat as start of line

    // Determine the line content and horizontal scroll for the targeted pane.
    // lineIndex is an ABSOLUTE pane index (0-based), not viewport-relative.
    std::string_view content;
    size_t hScroll = 0;
    if (pane == FocusArea::Raw) {
        if (lineIndex >= reader_.lineCount()) return 0;
        content = reader_.getLine(lineIndex + 1);  // reader uses 1-based line numbers
        hScroll = rawState_.hScrollOffset;
    } else {
        if (lineIndex >= chain_.filteredLineCount()) return 0;
        const size_t rawLineNo = chain_.filteredLineAt(lineIndex);
        content = reader_.getLine(rawLineNo);
        hScroll = filteredState_.hScrollOffset;
    }

    // displayColToByteOffset returns an absolute byte offset within content.
    // Starting from hScroll, advance contentCol display columns.
    return displayColToByteOffset(content, hScroll, contentCol);
}

// ── buildSelectedText ─────────────────────────────────────────────────────────
// Collects the selected lines from the appropriate source, extracts the byte
// ranges, and joins them with '\n'.  Line numbers and cursor-arrow prefixes are
// NOT included.  When a line is folded, the full (unfolded) content is copied.

std::string AppController::buildSelectedText() const {
    if (!selection_.active) return {};

    auto [startPt, endPt] = selection_.ordered();
    const FocusArea pane = selection_.anchor.pane;

    // lineIndex values are now ABSOLUTE pane indices — iterate directly.
    std::string result;
    for (size_t absIdx = startPt.lineIndex; absIdx <= endPt.lineIndex; ++absIdx) {
        std::string_view content;
        if (pane == FocusArea::Raw) {
            if (absIdx >= reader_.lineCount()) break;
            content = reader_.getLine(absIdx + 1);
        } else {
            if (absIdx >= chain_.filteredLineCount()) break;
            content = reader_.getLine(chain_.filteredLineAt(absIdx));
        }

        // Determine the byte range within this line.
        const size_t byteStart = (absIdx == startPt.lineIndex) ? startPt.byteOffset : 0;
        const size_t byteEnd   = (absIdx == endPt.lineIndex)   ? endPt.byteOffset   : content.size();

        const size_t safeStart = std::min(byteStart, content.size());
        const size_t safeEnd   = std::min(byteEnd,   content.size());

        if (!result.empty()) result += '\n';
        if (safeStart < safeEnd)
            result += std::string(content.substr(safeStart, safeEnd - safeStart));
    }
    return result;
}

// ── #22 Auto-scroll accessors ──────────────────────────────────────────────────

size_t AppController::paneScrollOffset(FocusArea area) const {
    return (area == FocusArea::Raw) ? rawState_.scrollOffset : filteredState_.scrollOffset;
}

int AppController::prefixColWidth() const {
    // Matches the prefix computation in renderLogPane() and screenColToByteOffset().
    int w = 2;  // "▶ " or "  " (cursor arrow + space)
    if (showLineNumbers_) {
        const int minW       = AppConfig::global().minLineNoWidth;
        const int digitCount = static_cast<int>(std::to_string(reader_.lineCount()).size());
        w += std::max(digitCount, minW) + 1;  // digits + trailing space
    }
    return w;
}

void AppController::scrollHorizontal(FocusArea area, int deltaBytes) {
    PaneState& ps = (area == FocusArea::Raw) ? rawState_ : filteredState_;
    if (deltaBytes < 0) {
        const size_t step = static_cast<size_t>(-deltaBytes);
        ps.hScrollOffset = (ps.hScrollOffset >= step) ? ps.hScrollOffset - step : 0;
    } else {
        ps.hScrollOffset += static_cast<size_t>(deltaBytes);
    }
    followTail_ = false;
}
