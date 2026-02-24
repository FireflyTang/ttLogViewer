#pragma once
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include "app_controller.hpp"

// Create the main TUI component.
// - controller must outlive the returned component.
// - screen is used for terminal-size queries and quit handling.
ftxui::Component CreateMainComponent(AppController& controller,
                                     ftxui::ScreenInteractive& screen);
