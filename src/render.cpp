#include "render.hpp"

#include <algorithm>
#include <format>
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

using namespace ftxui;

// ── Helpers ────────────────────────────────────────────────────────────────────

// Parse "#RRGGBB" into an ftxui Color, falling back to white on error.
static Color parseColor(std::string_view hex) {
    if (hex.size() == 7 && hex[0] == '#') {
        auto hexByte = [&](size_t pos) -> uint8_t {
            unsigned v = 0;
            for (int i = 0; i < 2; ++i) {
                char c = hex[pos + i];
                v = v * 16 + (c >= '0' && c <= '9' ? c - '0'
                            : c >= 'a' && c <= 'f' ? c - 'a' + 10
                            : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
            }
            return static_cast<uint8_t>(v);
        };
        return Color::RGB(hexByte(1), hexByte(3), hexByte(5));
    }
    return Color::White;
}

// Render a single log line with optional ColorSpan highlighting.
static Element renderColoredLine(std::string_view content,
                                  const std::vector<ColorSpan>& spans) {
    if (spans.empty())
        return text(std::string(content));

    Elements parts;
    size_t pos = 0;

    for (const auto& span : spans) {
        if (span.start > pos)
            parts.push_back(text(std::string(content.substr(pos, span.start - pos))));

        size_t len = (span.end > span.start) ? span.end - span.start : 0;
        if (len > 0) {
            parts.push_back(
                text(std::string(content.substr(span.start, len)))
                | color(parseColor(span.color))
            );
        }
        pos = span.end;
    }

    if (pos < content.size())
        parts.push_back(text(std::string(content.substr(pos))));

    return hbox(std::move(parts));
}

// Format a number with thousands separators, e.g. 1234567 → "1,234,567".
static std::string fmtCount(size_t n) {
    std::string s = std::to_string(n);
    size_t insertPos = s.size();
    while (insertPos > 3) {
        insertPos -= 3;
        s.insert(insertPos, ",");
    }
    return s;
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
                              bool /*showLineNumbers*/) {
    if (lines.empty())
        return text("") | flex;

    Elements rows;
    rows.reserve(lines.size());

    for (const auto& ll : lines) {
        Element marker = text(ll.highlighted ? "▶ " : "  ");
        Element content = renderColoredLine(ll.content, ll.colors);

        Element row = hbox({ marker, content | flex });

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
    for (const auto& tag : tags) {
        std::string label = "[" + std::to_string(tag.number) + ":" + tag.pattern + "]";
        Element e = text(" " + label + " ");
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
                   | size(WIDTH, LESS_THAN, 60)
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
    auto renderer = Renderer([&] {
        auto [dimx, dimy] = Terminal::Size();

        // Layout row budget:
        //   1 status bar + 1 sep + raw pane + 1 sep + filtered pane
        //   + 1 sep + 1 filter bar + 1 input line = 6 fixed rows
        const int overhead = 6;
        const int paneTotal = std::max(2, dimy - overhead);
        const int rawH  = paneTotal / 2;
        const int filtH = paneTotal - rawH;

        controller.onTerminalResize(dimx, dimy);
        ViewData data = controller.getViewData(rawH, filtH);

        Element layout = vbox({
            renderStatusBar(data),
            separator(),
            renderLogPane(data.rawPane, data.rawFocused, data.showLineNumbers)
                | size(HEIGHT, EQUAL, rawH),
            separator(),
            renderLogPane(data.filteredPane, data.filteredFocused, data.showLineNumbers)
                | size(HEIGHT, EQUAL, filtH),
            separator(),
            renderFilterBar(data.filterTags),
            renderInputLine(data),
        });

        layout = renderDialogOverlay(data, layout);
        layout = renderProgressOverlay(data, layout);

        return layout;
    });

    // Intercept events before passing to AppController
    return CatchEvent(renderer, [&](Event event) {
        // Quit on 'q' when no text input is active
        if (event == Event::Character('q') && !controller.isInputActive()) {
            screen.ExitLoopClosure()();
            return true;
        }
        return controller.handleKey(event);
    });
}
