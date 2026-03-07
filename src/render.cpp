#include "render.hpp"
#include "render_utils.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <optional>
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include "app_config.hpp"

using namespace ftxui;

// ── File-scope rendering constants ────────────────────────────────────────────

// Display columns occupied by the cursor marker ("▶ " or "  ").
static constexpr int kCursorMarkerCols = 2;

// Maximum candidate rows shown in the Tab-completion popup (not counting borders/arrow).
static constexpr size_t kCompletionPopupMaxRows = 3;

// Format a number with thousands separators, e.g. 1234567 → "1,234,567".
static std::string fmtCount(size_t n) {
    std::string s = std::to_string(n);
    if (s.size() <= 3) return s;

    // Build result backwards to avoid repeated insertions
    std::string result;
    result.reserve(s.size() + s.size() / 3);

    int count = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if (count == 3) {
            result.push_back(',');
            count = 0;
        }
        result.push_back(*it);
        ++count;
    }

    std::reverse(result.begin(), result.end());
    return result;
}

// ── Sub-component renderers ────────────────────────────────────────────────────

static Element renderStatusBar(const ViewData& data) {
    std::string modeStr = (data.mode == AppMode::Realtime) ? "实时" : "静态";

    std::string lineInfo;
    if (data.isIndexing)
        lineInfo = "索引建立中...";
    else if (data.newLineCount > 0)
        lineInfo = "+" + fmtCount(data.newLineCount) + " 新行";
    else
        lineInfo = fmtCount(data.totalLines) + " 行";

    std::string fileName = data.fileName.empty() ? "(未打开文件)" : data.fileName;

    return hbox({
        text(" " + fileName)  | bold | flex,
        text(" │ ") | dim,
        text(modeStr),
        text(" │ ") | dim,
        text(lineInfo),
        text(" "),
    }) | bgcolor(Color::GrayDark);
}

static Element renderLogPane(const std::vector<LogLine>& lines,
                              bool focused,
                              bool showLineNumbers,
                              int terminalWidth,
                              size_t hScroll,
                              size_t totalLines) {
    if (lines.empty())
        return text("") | flex;

    // Use the total file line count to fix the line-number column width for the
    // entire file, with a configurable minimum (default 6 digits).
    // The minimum prevents layout jumps for the vast majority of real-world log
    // files and also covers realtime files that grow past a 10× boundary
    // (e.g., 999 → 1000 lines) without shifting the content column.
    int maxLineNoW = 0;
    if (showLineNumbers) {
        const int minW = AppConfig::global().minLineNoWidth;
        int digitCount = static_cast<int>(std::to_string(totalLines).size());
        maxLineNoW = std::max(digitCount, minW);
    }

    Elements rows;
    rows.reserve(lines.size());

    for (const auto& ll : lines) {
        Elements parts;
        // Compute the prefix width so renderColoredLine can reserve space for "…"
        int prefixCols = kCursorMarkerCols;  // "▶ " or "  "
        if (showLineNumbers)
            prefixCols += maxLineNoW + 1;
        int contentWidth = terminalWidth > prefixCols ? terminalWidth - prefixCols : 0;

        if (showLineNumbers)
            parts.push_back(
                text(std::format("{:>{}} ", ll.rawLineNo, maxLineNoW)) | dim);
        parts.push_back(text(ll.highlighted ? "▶ " : "  "));
        parts.push_back(
            renderColoredLine(ll.content, ll.colors, ll.searchSpans,
                              ll.folded, contentWidth, hScroll,
                              ll.selectionSpans) | flex);

        Element row = hbox(std::move(parts));

        if (ll.highlighted && focused)
            row = row | inverted;

        rows.push_back(row);
    }

    return vbox(std::move(rows)) | flex;
}

static Element renderFilterBar(const std::vector<ViewData::FilterTag>& tags) {
    if (tags.empty())
        return text(" (无过滤器) ") | dim;

    Elements items;
    items.reserve(tags.size());
    for (const auto& tag : tags) {
        // Format: " [1:pattern](N)", " [1R:pattern](N)", " [1!:pattern](N)", etc.
        // R = regex, ! = exclude; combined: "R!" = regex+exclude.
        std::string label = std::format(" [{}{}{}:{}]({})",
            tag.number,
            tag.useRegex ? "R" : "",
            tag.exclude  ? "!" : "",
            tag.pattern, tag.matchCount);
        // ⬤ (U+2B24) is a larger filled circle than ●; ○ stays for disabled state.
        Element dot = tag.enabled
            ? Element(text("⬤ ") | color(parseHexColor(tag.color)))
            : Element(text("○ ") | color(parseHexColor(tag.color)));
        // Selected state uses explicit bgcolor instead of inverted to preserve
        // the dot's filter color as foreground (inverted would swap fg/bg).
        Element labelElem = text(std::move(label));
        if (tag.selected) {
            labelElem = labelElem | color(Color::White) | bgcolor(Color::GrayDark);
            dot = dot | bgcolor(Color::GrayDark);
        }
        Element row = hbox({ std::move(labelElem), std::move(dot) });
        if (!tag.enabled) row = row | dim;
        items.push_back(std::move(row));
    }
    return hbox(std::move(items));
}

// Always-visible bottom hints row.
static Element renderHintsLine(const ViewData& data) {
    if (data.hasSelection) {
        return hbox({ text(" [已选择]") | color(Color::Yellow),
                      text("  Ctrl+C:复制  Esc:取消选择") | dim });
    }
    const bool filterInputActive = (data.inputMode == InputMode::FilterAdd
                                 || data.inputMode == InputMode::FilterEdit);
    const bool searchInputActive = (data.inputMode == InputMode::Search);
    if (filterInputActive) {
        return text(" Esc:取消  Enter:确认  Tab:切换模式  Ctrl+V:粘贴") | dim;
    }
    if (searchInputActive) {
        return text(" Esc:取消  Enter:确认  Tab:正则  Ctrl+V:粘贴") | dim;
    }
    return text(" q:退出  ↑↓:移动  PgUp/PgDn:翻页  Tab:切换区域  拖拽:选文") | dim;
}

// Render an input buffer with a block cursor at byte offset cursorPos.
// The character under the cursor is rendered inverted; a space is shown when at end-of-input.
static Element renderBufWithCursor(const std::string& buf, size_t cursorPos) {
    const std::string before(buf, 0, cursorPos);
    std::string cursorChar;
    size_t afterStart = cursorPos;
    if (cursorPos < buf.size()) {
        size_t p = cursorPos;
        decodeUtf8Codepoint(buf, p);
        cursorChar = buf.substr(cursorPos, p - cursorPos);
        afterStart = p;
    } else {
        cursorChar = " ";  // block cursor at end-of-input
    }
    const std::string after(buf, afterStart);
    return hbox({
        text(before)     | bold,
        text(cursorChar) | bold | inverted,
        text(after)      | bold,
    });
}

// Active input row — shown only when input/search is active.
static Element renderActiveInput(const ViewData& data) {
    switch (data.inputMode) {
        case InputMode::None:
            if (!data.searchKeyword.empty()) {
                std::string cnt = (data.searchResultCount > 0)
                    ? std::format("{}/{}", data.searchResultIndex, data.searchResultCount)
                    : "无结果";
                return hbox({ text(" /") | dim,
                              text(data.searchKeyword) | bold,
                              text("  (" + cnt + ")  n/p:跳转  Esc:清除") | dim });
            }
            return text("");

        case InputMode::Search: {
            std::string modeTag = data.inputUseRegex ? " [正则]" : " [字符串]";
            if (data.inputUseRegex) {
                Element dot = text(" ●") | color(data.inputValid ? Color::Green : Color::Red);
                return hbox({ text(modeTag) | dim, text("  "),
                              renderBufWithCursor(data.inputBuffer, data.inputCursorPos),
                              std::move(dot) });
            }
            return hbox({ text(modeTag) | dim, text("  "),
                          renderBufWithCursor(data.inputBuffer, data.inputCursorPos) });
        }

        case InputMode::FilterAdd:
        case InputMode::FilterEdit: {
            // Build 4-mode tag: str-include, str-exclude (yellow), regex-include, regex-exclude (yellow)
            std::string modeStr;
            bool        modeIsExclude = data.inputExclude;
            if (!data.inputUseRegex && !data.inputExclude) modeStr = " [字符串·匹配]";
            else if (!data.inputUseRegex &&  data.inputExclude) modeStr = " [字符串·排除]";
            else if ( data.inputUseRegex && !data.inputExclude) modeStr = " [正则·匹配]";
            else                                                modeStr = " [正则·排除]";
            Element modeElem = text(modeStr) | dim;
            if (modeIsExclude) modeElem = modeElem | color(Color::Yellow);
            if (data.inputUseRegex) {
                Element dot = text(" ●") | color(data.inputValid ? Color::Green : Color::Red);
                return hbox({ std::move(modeElem), text("  "),
                              text(data.inputPrompt),
                              renderBufWithCursor(data.inputBuffer, data.inputCursorPos),
                              std::move(dot) });
            }
            return hbox({ std::move(modeElem), text("  "),
                          text(data.inputPrompt),
                          renderBufWithCursor(data.inputBuffer, data.inputCursorPos) });
        }

        case InputMode::GotoLine:
        case InputMode::OpenFile:
        case InputMode::ExportConfirm:
            return hbox({ text(data.inputPrompt),
                          renderBufWithCursor(data.inputBuffer, data.inputCursorPos) });
    }
    return text("");
}

// Completion popup: rendered as a dbox overlay above the active input line.
// Shows max kCompletionPopupMaxRows candidate filenames, with ▲ indicator when items are scrolled past.
// Returns nullopt when the popup should not be shown.
static std::optional<Element> renderCompletionPopup(const ViewData& data) {
    if (!data.showCompletions || data.completions.empty()) return std::nullopt;

    const size_t total     = data.completions.size();
    const size_t showCount = std::min(total, kCompletionPopupMaxRows);

    // Use controller-managed scroll offset for stable cursor-in-window behavior.
    size_t startIdx = data.completionScrollOffset;
    if (startIdx + showCount > total)
        startIdx = total - showCount;

    // The popup border '│ ' is 2 columns to the LEFT of the candidate text,
    // so the text first character lands exactly at completionCol.
    const int textIndent   = std::max(0, data.completionCol);
    const int borderIndent = std::max(0, textIndent - 2);
    const std::string borderPad(static_cast<size_t>(borderIndent), ' ');
    const std::string textPad  (static_cast<size_t>(textIndent - borderIndent), ' ');

    // Determine popup width from longest visible candidate in DISPLAY COLUMNS
    // (not bytes — CJK characters are 2 cols wide but 3 bytes in UTF-8).
    auto displayWidth = [](std::string_view s) -> int {
        return byteOffsetToDisplayCol(s, 0, s.size());
    };
    int maxCols = 0;
    for (size_t i = startIdx; i < startIdx + showCount; ++i)
        maxCols = std::max(maxCols, displayWidth(data.completions[i]));
    maxCols = std::max(maxCols, 1);  // minimum 1

    // Content between │ borders = textPad (2 cols) + ">" (1 col) + candidate (maxCols) = maxCols+3 cols.
    const int innerCols = maxCols + 3;

    // '─' (U+2500) is 3 bytes in UTF-8; build the border line by string concatenation.
    std::string hLine;
    hLine.reserve(static_cast<size_t>(innerCols) * 3);
    for (int i = 0; i < innerCols; ++i) hLine += "─";

    Elements rows;

    // ▲ indicator: shown when items exist above the visible window.
    if (startIdx > 0) {
        // " ▲" = 2 display cols, pad remainder with spaces.
        const std::string arrowLine = " ▲" + std::string(static_cast<size_t>(innerCols - 2), ' ');
        rows.push_back(hbox({ text(borderPad), text("│"), text(arrowLine), text("│") }));
    }

    // Content rows.
    // textPad is prepended inside the cell, so entry only needs to fill
    // (innerCols - textPadCols) display columns to keep total == innerCols.
    const int textPadCols = static_cast<int>(textPad.size());  // ASCII spaces = display cols
    const int entryTarget = innerCols - textPadCols;
    for (size_t i = startIdx; i < startIdx + showCount; ++i) {
        const bool selected = (i == data.completionIndex);
        std::string entry   = (selected ? ">" : " ") + data.completions[i];
        const int entryCols = 1 + displayWidth(data.completions[i]);
        if (entryCols < entryTarget)
            entry += std::string(static_cast<size_t>(entryTarget - entryCols), ' ');
        Element cell = text(textPad + entry) | (selected ? inverted : nothing);
        rows.push_back(hbox({ text(borderPad), text("│"), cell, text("│") }));
    }

    // Assemble all rows with top/bottom border into a single vbox element.
    Elements all;
    all.push_back(hbox({ text(borderPad), text("┌" + hLine + "┐") }));
    for (auto& r : rows) all.push_back(std::move(r));
    all.push_back(hbox({ text(borderPad), text("└" + hLine + "┘") }));
    return vbox(std::move(all));
}

static Element renderDialogOverlay(const ViewData& data, Element base) {
    if (!data.showDialog) return base;

    Element body = vbox({
        text(" " + data.dialogTitle + " ") | bold | center,
        separator(),
        paragraph(data.dialogBody) | center,
        separator(),
        data.dialogHasChoice
            ? hbox({ text(" [Y] 确认 ") | color(Color::Green),
                     text(" [N] 取消 ") | color(Color::Red)  }) | center
            : text("（按任意键关闭）") | dim | center,
    });

    Element dialog = window(text(""), body)
                   | size(WIDTH, LESS_THAN, AppConfig::global().dialogMaxWidth)
                   | clear_under | center;

    return dbox({ base, dialog });
}

static Element renderProgressOverlay(const ViewData& data, Element base) {
    if (!data.showProgress) return base;

    int pct = static_cast<int>(data.progress * 100.0);
    Element bar = hbox({
        text("过滤处理中: " + std::to_string(pct) + "% "),
        gauge(data.progress) | flex,
    }) | border;

    return dbox({ base, bar | center });
}

// ── Layout row constants ───────────────────────────────────────────────────────
//
// The two pane areas occupy fixed row slots within the terminal:
//   row 0          : status bar
//   row 1          : separator
//   rows 2..1+rawH : raw pane       (rawTop = 2)
//   row  2+rawH    : separator
//   rows 3+rawH..2+rawH+filtH : filtered pane  (filtTop = 3 + rawH)
//
// These values are used by both the renderer and the mouse-event handler.
// Keep them in sync with the vbox element order in CreateMainComponent.

static constexpr int kStatusBarRows = 2;  // status bar + separator above raw pane

// ── Mouse event handler ───────────────────────────────────────────────────────
//
// Extracted from CatchEvent lambda to keep CreateMainComponent readable.
// Returns true if the event was consumed.

static bool handleMouseEvent(AppController& controller, Event event) {
    auto& m = event.mouse();
    const int   rawH = controller.rawPaneHeight();
    const int   filtH = controller.filtPaneHeight();

    const int rawTop  = kStatusBarRows;
    const int rawBot  = rawTop + rawH - 1;
    const int filtTop = rawTop + rawH + 1;  // +1 for separator
    const int filtBot = filtTop + filtH - 1;

    const bool overRaw  = (m.y >= rawTop  && m.y <= rawBot);
    const bool overFilt = (m.y >= filtTop && m.y <= filtBot);

    // ── Scroll wheel ──────────────────────────────────────────────────────────
    if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
        if (m.control) return false;  // pass Ctrl+scroll to terminal (font resize)
        const int dir = (m.button == Mouse::WheelDown) ? 1 : -1;
        controller.scrollPane(controller.focusArea(), dir);
        return true;
    }

    // ── Left button press: start a potential drag/selection ───────────────────
    if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
        if (overRaw) {
            const int    row    = m.y - rawTop;
            const size_t absIdx = controller.paneScrollOffset(FocusArea::Raw)
                                  + static_cast<size_t>(row);
            const size_t byte   = controller.screenColToByteOffset(
                                      FocusArea::Raw, absIdx, m.x);
            controller.setFocus(FocusArea::Raw);
            controller.startSelection(FocusArea::Raw, absIdx, byte);
        } else if (overFilt) {
            const int    row    = m.y - filtTop;
            const size_t absIdx = controller.paneScrollOffset(FocusArea::Filtered)
                                  + static_cast<size_t>(row);
            const size_t byte   = controller.screenColToByteOffset(
                                      FocusArea::Filtered, absIdx, m.x);
            controller.setFocus(FocusArea::Filtered);
            controller.startSelection(FocusArea::Filtered, absIdx, byte);
        } else {
            controller.clearSelection();
        }
        return true;
    }

    // ── Left button move (drag): extend selection + auto-scroll ───────────────
    if (m.button == Mouse::Left && m.motion == Mouse::Moved
            && controller.isSelectionDragging()) {
        const FocusArea anchorPane = controller.focusArea();
        const int paneTop = (anchorPane == FocusArea::Raw) ? rawTop : filtTop;
        const int paneH   = (anchorPane == FocusArea::Raw) ? rawH   : filtH;

        // ── Vertical auto-scroll ──────────────────────────────────────────────
        int rowInPane;
        if (m.y < paneTop) {
            controller.scrollPane(anchorPane, -1);
            rowInPane = 0;
        } else if (m.y >= paneTop + paneH) {
            controller.scrollPane(anchorPane, +1);
            rowInPane = paneH - 1;
        } else {
            rowInPane = m.y - paneTop;
        }

        // ── Horizontal auto-scroll ────────────────────────────────────────────
        const int prefixCols = controller.prefixColWidth();
        const int termW      = controller.terminalWidth();
        const int hStep      = AppConfig::global().hScrollStep;
        if (m.x < prefixCols) {
            controller.scrollHorizontal(anchorPane, -hStep);
        } else if (m.x >= termW - 1) {
            controller.scrollHorizontal(anchorPane, +hStep);
        }

        // ── Extend selection using post-scroll absolute index ─────────────────
        const size_t newScrollOff = controller.paneScrollOffset(anchorPane);
        const size_t absIdx       = newScrollOff + static_cast<size_t>(rowInPane);
        const size_t byte         = controller.screenColToByteOffset(
                                        anchorPane, absIdx, m.x);
        controller.extendSelection(absIdx, byte);
        return true;
    }

    // ── Left button release: finalize or treat as click ───────────────────────
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
        if (controller.hasSelection()) {
            controller.finalizeSelection();
        } else if (controller.isSelectionDragging()) {
            controller.clearSelection();
            FocusArea pane = controller.focusArea();
            int row = (pane == FocusArea::Raw) ? m.y - rawTop : m.y - filtTop;
            if (overRaw || overFilt)
                controller.clickLine(pane, row);
        }
        return true;
    }

    return false;
}

// ── CreateMainComponent ────────────────────────────────────────────────────────

Component CreateMainComponent(AppController& controller,
                               ScreenInteractive& screen) {
    // Initialize pane heights from the current terminal size once.
    // Tests can override by calling controller.onTerminalResize(w, h) AFTER this
    // call but BEFORE Render().  The renderer reads the stored heights from the
    // controller so it never calls Terminal::Size() during a render frame.
    {
        auto [dimx, dimy] = Terminal::Size();
        controller.onTerminalResize(dimx, dimy);
    }

    auto renderer = Renderer([&] {
        // Use stored pane heights (set above or updated by resize events).
        const int rawH  = controller.rawPaneHeight();
        const int filtH = controller.filtPaneHeight();

        ViewData data = controller.getViewData(rawH, filtH);

        // Build layout: hints row is always visible; active-input row appears only
        // when an input mode is active or a search keyword is set.
        const bool inputRowActive = (data.inputMode != InputMode::None
                                  || !data.searchKeyword.empty());
        Elements elems = {
            renderStatusBar(data),
            separator(),
            renderLogPane(data.rawPane, data.rawFocused,
                           data.showLineNumbers, data.terminalWidth,
                           data.rawHScroll, data.totalLines)
                | size(HEIGHT, EQUAL, rawH),
            separator(),
            renderLogPane(data.filteredPane, data.filteredFocused,
                           data.showLineNumbers, data.terminalWidth,
                           data.filtHScroll, data.totalLines)
                | size(HEIGHT, EQUAL, filtH),
            separator(),
            renderFilterBar(data.filterTags),
        };
        if (inputRowActive) elems.push_back(renderActiveInput(data));
        elems.push_back(renderHintsLine(data));

        Element layout = vbox(std::move(elems));

        // Completion popup: dbox overlay floating above the input line.
        // The popup sits in a layer that does not affect pane heights.
        // Bottom spacer = 2 rows (active-input + hints), always present when popup is shown.
        if (auto popup = renderCompletionPopup(data)) {
            Element overlay = vbox({
                filler(),
                std::move(*popup),
                // 2-line spacer to align the popup bottom with the top of the input row.
                size(HEIGHT, EQUAL, 2)(text("")),
            });
            layout = dbox({ std::move(layout), std::move(overlay) });
        }

        layout = renderDialogOverlay(data, layout);
        layout = renderProgressOverlay(data, layout);

        return layout;
    });

    // Intercept events before passing to AppController.
    // On each event, refresh the pane heights in case the terminal was resized.
    return CatchEvent(renderer, [&](Event event) {
        // Quit on 'q' when no text input is active and no dialog is open
        if (event == Event::Character('q')
                && !controller.isInputActive()
                && !controller.isDialogOpen()) {
            controller.requestQuit(screen.ExitLoopClosure());
            return true;
        }

        // Ctrl+C: copy selection to clipboard (only in normal mode, no dialog).
        // Always consume CtrlC here so it does not fall through to FTXUI's
        // RecordSignal(SIGABRT) which would exit the loop.
        if (event == Event::CtrlC) {
            if (!controller.isInputActive() && !controller.isDialogOpen()) {
                if (controller.hasSelection())
                    controller.copySelectionToClipboard();
            }
            return true;
        }

        // Update heights on every event to pick up terminal resize
        {
            auto [dimx, dimy] = Terminal::Size();
            controller.onTerminalResize(dimx, dimy);
        }

        // Mouse events: wheel scroll, click-to-focus, and text-selection drag
        if (event.is_mouse())
            return handleMouseEvent(controller, event);

        return controller.handleKey(event);
    });
}
