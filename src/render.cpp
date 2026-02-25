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
                              int terminalWidth) {
    if (lines.empty())
        return text("") | flex;

    Elements rows;
    rows.reserve(lines.size());

    for (const auto& ll : lines) {
        Elements parts;
        // Compute the prefix width so renderColoredLine can reserve space for "…"
        int prefixCols = 2;  // "▶ " or "  "
        if (showLineNumbers)
            prefixCols += static_cast<int>(std::to_string(ll.rawLineNo).size()) + 1;
        int contentWidth = terminalWidth > prefixCols ? terminalWidth - prefixCols : 0;

        if (showLineNumbers)
            parts.push_back(text(std::to_string(ll.rawLineNo) + " ") | dim);
        parts.push_back(text(ll.highlighted ? "▶ " : "  "));
        parts.push_back(
            renderColoredLine(ll.content, ll.colors, ll.folded, contentWidth) | flex);

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
        // Use std::format for efficient string formatting (C++20)
        std::string label = std::format(" [{}:{}] ", tag.number, tag.pattern);
        Element e = text(std::move(label));
        if (tag.selected)  e = e | inverted;
        if (!tag.enabled)  e = e | dim;
        items.push_back(e);
    }
    return hbox(std::move(items));
}

static Element renderInputLine(const ViewData& data) {
    switch (data.inputMode) {
        case InputMode::None:
            return text(" q:退出  ↑↓:移动  PgUp/PgDn:翻页  Tab:切换区域") | dim;

        case InputMode::Search:
            return hbox({ text(data.inputPrompt), text(data.inputBuffer) | bold, text("_") });

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
                           data.showLineNumbers, data.terminalWidth)
                | size(HEIGHT, EQUAL, rawH),
            separator(),
            renderLogPane(data.filteredPane, data.filteredFocused,
                           data.showLineNumbers, data.terminalWidth)
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
        // Quit on 'q' when no text input is active
        if (event == Event::Character('q') && !controller.isInputActive()) {
            screen.ExitLoopClosure()();
            return true;
        }
        // Update heights on every event to pick up terminal resize
        {
            auto [dimx, dimy] = Terminal::Size();
            controller.onTerminalResize(dimx, dimy);
        }
        return controller.handleKey(event);
    });
}
