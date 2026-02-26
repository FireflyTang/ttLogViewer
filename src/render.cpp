#include "render.hpp"
#include "render_utils.hpp"

#include <algorithm>
#include <format>
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include "app_config.hpp"

using namespace ftxui;

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
                              size_t totalLines,
                              bool searchActive) {
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
        int prefixCols = 2;  // "▶ " or "  "
        if (showLineNumbers)
            prefixCols += maxLineNoW + 1;
        int contentWidth = terminalWidth > prefixCols ? terminalWidth - prefixCols : 0;

        if (showLineNumbers)
            parts.push_back(
                text(std::format("{:>{}} ", ll.rawLineNo, maxLineNoW)) | dim);
        // During an active search, suppress the ▶ cursor marker so it does not
        // jump around as the search scrolls through results.
        parts.push_back(text((ll.highlighted && !searchActive) ? "▶ " : "  "));
        parts.push_back(
            renderColoredLine(ll.content, ll.colors, ll.searchSpans,
                              ll.folded, contentWidth, hScroll) | flex);

        Element row = hbox(std::move(parts));

        // Also suppress the whole-line inverted background during active search.
        if (ll.highlighted && focused && !searchActive)
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
        // Format: " [1:pattern](N)" or " [1R:pattern](N)" — no trailing space,
        // the dot follows immediately so there is no gap between count and dot.
        std::string label = std::format(" [{}{}:{}]({})",
            tag.number, tag.useRegex ? "R" : "", tag.pattern, tag.matchCount);
        // ⬤ (U+2B24) is a larger filled circle than ●; ○ stays for disabled state.
        Element dot = tag.enabled
            ? Element(text("⬤ ") | color(parseHexColor(tag.color)))
            : Element(text("○ ") | color(parseHexColor(tag.color)));
        // Combine label + dot into one element so selection highlight covers both.
        Element row = hbox({ text(std::move(label)), std::move(dot) });
        if (tag.selected) row = row | inverted;
        if (!tag.enabled) row = row | dim;
        items.push_back(std::move(row));
    }
    return hbox(std::move(items));
}

static Element renderInputLine(const ViewData& data) {
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
            return text(" q:退出  ↑↓:移动  PgUp/PgDn:翻页  Tab:切换区域") | dim;

        case InputMode::Search: {
            std::string modeTag = data.searchUseRegex ? " [正则] " : " [字符串] ";
            return hbox({ text(modeTag) | dim,
                          text(data.inputBuffer) | bold,
                          text("_"),
                          text("  Tab:切换") | dim });
        }

        case InputMode::FilterAdd:
        case InputMode::FilterEdit: {
            Element light = text(data.inputValid ? " ●" : " ●")
                          | color(data.inputValid ? Color::Green : Color::Red);
            return hbox({ text(data.inputPrompt), text(data.inputBuffer) | bold, light });
        }

        case InputMode::GotoLine:
        case InputMode::OpenFile:
        case InputMode::ExportConfirm:
            return hbox({ text(data.inputPrompt), text(data.inputBuffer) | bold, text("_") });
    }
    return text("");
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

        Element layout = vbox({
            renderStatusBar(data),
            separator(),
            renderLogPane(data.rawPane, data.rawFocused,
                           data.showLineNumbers, data.terminalWidth,
                           data.rawHScroll, data.totalLines, data.searchActive)
                | size(HEIGHT, EQUAL, rawH),
            separator(),
            renderLogPane(data.filteredPane, data.filteredFocused,
                           data.showLineNumbers, data.terminalWidth,
                           data.filtHScroll, data.totalLines, data.searchActive)
                | size(HEIGHT, EQUAL, filtH),
            separator(),
            renderFilterBar(data.filterTags),
            renderInputLine(data),
        });

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
        // Update heights on every event to pick up terminal resize
        {
            auto [dimx, dimy] = Terminal::Size();
            controller.onTerminalResize(dimx, dimy);
        }

        // Mouse events: wheel scroll and click-to-focus
        if (event.is_mouse()) {
            const auto& m     = event.mouse();
            const int   rawH  = controller.rawPaneHeight();
            const int   filtH = controller.filtPaneHeight();

            // Layout row boundaries (0-based):
            //   row 0          : status bar
            //   row 1          : separator
            //   rows 2..1+rawH : raw pane
            //   row  2+rawH    : separator
            //   rows 3+rawH..2+rawH+filtH : filtered pane
            const int rawTop  = 2;
            const int rawBot  = 1 + rawH;
            const int filtTop = 3 + rawH;
            const int filtBot = 2 + rawH + filtH;

            const bool overRaw  = (m.y >= rawTop  && m.y <= rawBot);
            const bool overFilt = (m.y >= filtTop && m.y <= filtBot);

            if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
                const int dir = (m.button == Mouse::WheelDown) ? 1 : -1;
                if (overRaw)
                    controller.scrollPane(FocusArea::Raw,      dir);
                else if (overFilt)
                    controller.scrollPane(FocusArea::Filtered, dir);
                return true;
            }
            if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
                if (overRaw)
                    controller.setFocus(FocusArea::Raw);
                else if (overFilt)
                    controller.setFocus(FocusArea::Filtered);
                return true;
            }
            return false;
        }

        return controller.handleKey(event);
    });
}
