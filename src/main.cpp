#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "app_config.hpp"
#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "render.hpp"

#ifdef _WIN32
// Global pointer to the FTXUI screen used by ctrlHandler.
// Set before screen.Loop() so the handler can post events.
static ftxui::ScreenInteractive* g_screen_ptr = nullptr;

// Intercepts CTRL_C_EVENT from the Windows console subsystem.
//
// Two Ctrl+C paths exist on Windows:
//
//  Path A — real Windows console (Windows Terminal, cmd, …):
//    disableProcessedInput() clears ENABLE_PROCESSED_INPUT, so Ctrl+C arrives
//    as a KEY_EVENT with UnicodeChar=0x03.  FTXUI parses it → Event::CtrlC.
//    CTRL_C_EVENT is NOT generated, so this handler is never called for Ctrl+C.
//
//  Path B — PTY-based consoles (mintty, MSYS2):
//    GetConsoleMode() may fail or the console mode cannot be changed, so
//    ENABLE_PROCESSED_INPUT remains set.  Ctrl+C generates a CTRL_C_EVENT
//    which reaches this handler; no KEY_EVENT is placed in the input buffer,
//    so FTXUI never sees byte 0x03.
//    Fix: post Event::CtrlC explicitly so CatchEvent can copy the selection.
//
static BOOL WINAPI ctrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT) {
        if (g_screen_ptr)
            g_screen_ptr->PostEvent(ftxui::Event::CtrlC);
        return TRUE;  // suppress SIGINT
    }
    return FALSE;
}

// Disable ENABLE_PROCESSED_INPUT on the console so Ctrl+C is delivered as
// keyboard input (byte 0x03 → Event::CtrlC) instead of generating a
// CTRL_C_EVENT / SIGINT signal.  Harmless no-op on PTY-based consoles
// (mintty) where GetConsoleMode fails; in that case ctrlHandler posts
// Event::CtrlC directly when CTRL_C_EVENT arrives.
static void disableProcessedInput() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hIn, &mode)) {
        mode &= ~DWORD{0x0001};  // clear ENABLE_PROCESSED_INPUT
        SetConsoleMode(hIn, mode);
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
    // Layer 1: Console control handler.
    //   - On PTY consoles (mintty): intercepts CTRL_C_EVENT and posts
    //     Event::CtrlC to FTXUI so CatchEvent can copy the selection.
    //   - Returns TRUE to suppress SIGINT in all cases.
    SetConsoleCtrlHandler(ctrlHandler, TRUE);

    // Layer 2: Clear ENABLE_PROCESSED_INPUT so Ctrl+C becomes a KEY_EVENT
    // (real Windows console path).  FTXUI's Install() does not touch this bit,
    // so the setting survives through screen.Loop().
    disableProcessedInput();
#endif

    // Load user config overrides before anything else.
    // A missing or unparseable config file is silently ignored (all defaults apply).
    AppConfig::loadGlobal();

    LogReader     reader;
    FilterChain   chain(reader);
    AppController controller(reader, chain);

    // Create screen before injecting PostFn so the lambda captures a valid ref.
    // Note: FTXUI v6 has mouse tracking enabled by default (track_mouse_ = true).
    auto screen = ftxui::ScreenInteractive::TerminalOutput();

    // FTXUI v6 defaults force_handle_ctrl_c_=true, which means FTXUI always calls
    // RecordSignal(SIGABRT) on Ctrl+C regardless of whether CatchEvent returns true.
    // Disabling this lets our CatchEvent handler suppress the exit (copy to clipboard).
    screen.ForceHandleCtrlC(false);

#ifdef _WIN32
    // Expose screen to ctrlHandler (Layer 1) for PostEvent.
    g_screen_ptr = &screen;
#endif

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

    // Layer 3 (belt-and-suspenders): override FTXUI's SIGINT handler to SIG_IGN
    // after Install() runs inside Loop().  A detached thread with a short delay
    // ensures we run after Install() sets RecordSignal(SIGINT).
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::signal(SIGINT, SIG_IGN);
    }).detach();

    auto component = CreateMainComponent(controller, screen);
    screen.Loop(component);

    // Save session on exit
    chain.save(sPath, reader.filePath(), reader.mode());

    return EXIT_SUCCESS;
}
