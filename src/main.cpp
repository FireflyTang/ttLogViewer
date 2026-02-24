#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

int main() {
    using namespace ftxui;

    // Simple button component
    auto button = Button("点击我!", []{});

    // Simple application
    auto component = Renderer(button, [&] {
        return vbox({
            text("FTXUI Hello World!") | bold | center,
            separator(),
            text("🎉 ttLogViewer 开发环境测试") | center,
            separator(),
            button->Render() | center,
            separator(),
            text("按 ESC 退出") | dim | center
        }) | border;
    });

    // Handle ESC key to quit
    component |= CatchEvent([&](Event event) {
        if (event == Event::Escape) {
            return true;  // Exit
        }
        return false;
    });

    auto screen = ScreenInteractive::TerminalOutput();
    screen.Loop(component);

    return 0;
}