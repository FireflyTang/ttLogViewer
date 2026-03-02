#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

// ── Ctrl+C diagnostic log (writes to %TEMP%/ttlv_debug.log) ────────────────
static std::ofstream& debugLogStream() {
    static std::ofstream f([]() -> std::string {
#ifdef _WIN32
        const char* tmp = getenv("TEMP");
        return std::string(tmp ? tmp : ".") + "/ttlv_debug.log";
#else
        return "/tmp/ttlv_debug.log";
#endif
    }(), std::ios::app);
    return f;
}
static void debugLog(const char* msg) {
    debugLogStream() << msg << std::endl;  // flush immediately
}

#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "app_config.hpp"
#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "render.hpp"

#ifdef _WIN32
// Suppress the default CTRL_C_EVENT handler that terminates the process.
// FTXUI delivers Ctrl+C as a keyboard event; without this handler the OS
// kills the process before CatchEvent ever sees the key.
static BOOL WINAPI ctrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT) {
        debugLog("[PATH-A] ctrlHandler: CTRL_C_EVENT received, returning TRUE");
        return TRUE;
    }
    debugLog("[PATH-A] ctrlHandler: non-CtrlC event, returning FALSE");
    return FALSE;
}
#endif

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
#ifdef _WIN32
    SetConsoleCtrlHandler(ctrlHandler, TRUE);
#endif

    // Load user config overrides before anything else.
    // A missing or unparseable config file is silently ignored (all defaults apply).
    AppConfig::loadGlobal();

    LogReader     reader;
    FilterChain   chain(reader);
    AppController controller(reader, chain);

    // Create screen before injecting PostFn so the lambda captures a valid ref
    // Note: FTXUI v6 has mouse tracking enabled by default (track_mouse_ = true).
    auto screen = ftxui::ScreenInteractive::TerminalOutput();

    // FTXUI v6 defaults force_handle_ctrl_c_=true, which means FTXUI always calls
    // RecordSignal(SIGABRT) on Ctrl+C regardless of whether CatchEvent returns true.
    // Disabling this lets our CatchEvent handler suppress the exit (copy to clipboard).
    screen.ForceHandleCtrlC(false);
    debugLog("[INIT] ForceHandleCtrlC(false) called");

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

    // Override FTXUI's SIGINT handler AFTER it installs its own (inside Loop()).
    // On MSYS2/mintty (POSIX PTY), Ctrl+C sends SIGINT directly — not a Win32
    // console event.  FTXUI's default handler sets signal_ which exits the loop.
    // By posting SIG_IGN we ensure SIGINT is harmless; the Ctrl+C keyboard event
    // still reaches CatchEvent as Event::CtrlC for copy-to-clipboard handling.
    screen.Post([]() {
        std::signal(SIGINT, SIG_IGN);
        debugLog("[INIT] SIGINT handler overridden to SIG_IGN");
    });

    auto component = CreateMainComponent(controller, screen);
    debugLog("[INIT] Entering screen.Loop()");
    screen.Loop(component);
    debugLog("[EXIT] screen.Loop() returned normally");

    // Save session on exit
    chain.save(sPath, reader.filePath(), reader.mode());

    return EXIT_SUCCESS;
}
