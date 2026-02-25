#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>

#include <ftxui/component/screen_interactive.hpp>

#include "app_config.hpp"
#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "render.hpp"

// Return the path to the per-user session file.
static std::string sessionPath() {
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    const std::string base = appdata ? appdata : ".";
    return base + "/ttlogviewer/last_session.json";
#else
    const char* xdg = getenv("XDG_CONFIG_HOME");
    const std::string base = xdg
        ? std::string(xdg)
        : (std::string(getenv("HOME")) + "/.config");
    return base + "/ttlogviewer/last_session.json";
#endif
}

int main(int argc, char* argv[]) {
    // Load user config overrides before anything else.
    // A missing or unparseable config file is silently ignored (all defaults apply).
    AppConfig::loadGlobal();

    LogReader     reader;
    FilterChain   chain(reader);
    AppController controller(reader, chain);

    // Create screen before injecting PostFn so the lambda captures a valid ref
    auto screen = ftxui::ScreenInteractive::TerminalOutput();

    // Inject async post function so background threads can safely notify UI
    auto postFn = [&screen](std::function<void()> fn) {
        screen.Post(std::move(fn));
    };
    reader.setPostFn(postFn);
    chain.setPostFn(postFn);
    controller.setPostFn(postFn);

    // Load session (ignore failure – fresh start on first run or corrupt file)
    const std::string sPath = sessionPath();
    chain.load(sPath);

    // Open file: command-line argument takes priority over last session file
    if (argc >= 2) {
        const std::string path = argv[1];
        if (!reader.open(path)) {
            std::cerr << "ttLogViewer: cannot open '" << path << "'\n";
            return EXIT_FAILURE;
        }
    } else if (!chain.sessionLastFile().empty()) {
        const std::string lastFile{chain.sessionLastFile()};
        reader.open(lastFile);          // Ignore failure (file may be gone)
        reader.setMode(chain.sessionMode());
    }

    auto component = CreateMainComponent(controller, screen);
    screen.Loop(component);

    // Save session on exit
    chain.save(sPath, reader.filePath(), reader.mode());

    return EXIT_SUCCESS;
}
