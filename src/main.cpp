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
static BOOL WINAPI ctrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT) {
        debugLog("[CTRL] ctrlHandler: CTRL_C_EVENT suppressed");
        return TRUE;
    }
    return FALSE;
}

// Disable ENABLE_PROCESSED_INPUT on the console so Ctrl+C is delivered as
// keyboard input (byte 0x03 → Event::CtrlC) instead of generating a
// CTRL_C_EVENT / SIGINT signal.  Must be called AFTER FTXUI's Install()
// sets up its own console mode (via screen.Post inside Loop).
static void disableProcessedInput() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hIn, &mode)) {
        const DWORD ENABLE_PROCESSED_INPUT_FLAG = 0x0001;
        if (mode & ENABLE_PROCESSED_INPUT_FLAG) {
            mode &= ~ENABLE_PROCESSED_INPUT_FLAG;
            SetConsoleMode(hIn, mode);
            debugLog("[INIT] ENABLE_PROCESSED_INPUT disabled on console input");
        } else {
            debugLog("[INIT] ENABLE_PROCESSED_INPUT was already off");
        }
    } else {
        // Not a real console (e.g. mintty PTY pipe) — console API unavailable.
        debugLog("[INIT] GetConsoleMode failed (not a real console)");
    }
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
    // Layer 1: Process-level Ctrl+C ignore (prevents default ExitProcess).
    SetConsoleCtrlHandler(NULL, TRUE);
    // Layer 2: Custom handler as fallback (returns TRUE for CTRL_C_EVENT).
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

    // ── Ctrl+C defense: runs AFTER FTXUI Install() inside Loop() ──────────
    // FTXUI's Install() registers std::signal(SIGINT, RecordSignal) and sets
    // console mode.  We must override BOTH after Install() runs.
    //
    // Root cause: FTXUI on _WIN32 keeps ENABLE_PROCESSED_INPUT enabled, so
    // Ctrl+C generates CTRL_C_EVENT → CRT raises SIGINT → RecordSignal →
    // g_signal_exit_count++ → ExecuteSignalHandlers → Exit().  This path
    // bypasses ForceHandleCtrlC and CatchEvent entirely.
    //
    // Fix: disable ENABLE_PROCESSED_INPUT so Ctrl+C becomes keyboard input
    // (byte 0x03).  FTXUI's VT parser converts it to Event::CtrlC, which
    // our CatchEvent handles for copy-to-clipboard.
    screen.Post([]() {
#ifdef _WIN32
        disableProcessedInput();
#endif
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
