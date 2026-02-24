#include <cstdlib>
#include <iostream>
#include <string>

#include <ftxui/component/screen_interactive.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "render.hpp"

int main(int argc, char* argv[]) {
    LogReader    reader;
    FilterChain  chain(reader);
    AppController controller(reader, chain);

    // Open file from command-line argument if provided
    if (argc >= 2) {
        const std::string path = argv[1];
        if (!reader.open(path)) {
            std::cerr << "ttLogViewer: cannot open '" << path << "'\n";
            return EXIT_FAILURE;
        }
    }

    auto screen    = ftxui::ScreenInteractive::TerminalOutput();
    auto component = CreateMainComponent(controller, screen);
    screen.Loop(component);

    return EXIT_SUCCESS;
}
