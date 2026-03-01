#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>

#include <ftxui/component/event.hpp>
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
    const char* home = getenv("HOME");
    const std::string base = xdg
        ? std::string(xdg)
        : (home ? std::string(home) + "/.config" : std::string("."));
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
    // Note: FTXUI v6 has mouse tracking enabled by default (track_mouse_ = true).
    auto screen = ftxui::ScreenInteractive::TerminalOutput();

    // Inject async post function so background threads can safely notify UI.
    // After executing the task, post Event::Custom to invalidate the frame and
    // force Draw() — FTXUI only sets frame_valid_=false for Event tasks, not
    // for plain Closure tasks, so without this the UI would not redraw until
    // the next user input event.
    auto postFn = [&screen](std::function<void()> fn) {
        screen.Post(std::move(fn));
        screen.PostEvent(ftxui::Event::Custom);
    };
    reader.setPostFn(postFn);
    chain.setPostFn(postFn);
    controller.setPostFn(postFn);

    // Load session (ignore failure – fresh start on first run or corrupt file)
    const std::string sPath = sessionPath();
    chain.load(sPath);

    // Open file: command-line argument only (no auto-reopen of last session file)
    if (argc >= 2) {
        const std::string path = argv[1];
        if (!reader.open(path)) {
            std::cerr << "ttLogViewer: cannot open '" << path << "'\n";
            return EXIT_FAILURE;
        }
    }

    auto component = CreateMainComponent(controller, screen);
    screen.Loop(component);

    // Save session on exit
    chain.save(sPath, reader.filePath(), reader.mode());

    return EXIT_SUCCESS;
}
