#pragma once
// Common fixture for render tests: sets up a 5-line temp file, opens it in
// LogReader, and provides a renderCtrl() helper that renders to a fixed-size
// in-memory screen.
//
// Usage:
//   class MyRenderTest : public RenderTestBase { ... };

#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/screen.hpp>

#include "app_controller.hpp"
#include "filter_chain.hpp"
#include "log_reader.hpp"
#include "render.hpp"
#include "temp_file.hpp"
#include "test_utils.hpp"

class RenderTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        std::string content;
        for (int i = 1; i <= 5; ++i)
            content += "line" + std::to_string(i) + "\n";
        file_ = std::make_unique<TempFile>(content);
        reader_.open(file_->path());
        waitForIndexing(reader_);
        ctrl_.getViewData(3, 3);  // Initialize pane-height cache
    }

    // Render the controller to a fixed-size screen and return the text output.
    // After CreateMainComponent() sets heights from Terminal::Size(), override
    // with the test dimensions so the layout fits within the fixed screen.
    std::string renderCtrl(int w = 80, int h = 20) {
        auto comp = CreateMainComponent(ctrl_, screen_);
        ctrl_.onTerminalResize(w, h);
        auto scr = ftxui::Screen::Create(ftxui::Dimension::Fixed(w),
                                          ftxui::Dimension::Fixed(h));
        ftxui::Render(scr, comp->Render());
        return scr.ToString();
    }

    bool key(ftxui::Event e) { return ctrl_.handleKey(e); }

    std::unique_ptr<TempFile> file_;
    LogReader     reader_;
    FilterChain   chain_{reader_};
    AppController ctrl_{reader_, chain_};
    ftxui::ScreenInteractive screen_ = ftxui::ScreenInteractive::TerminalOutput();
};
