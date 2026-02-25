#include <gtest/gtest.h>
#include <future>
#include "render_test_base.hpp"

class ProgressOverlayTest : public RenderTestBase {};

// ── Default state ─────────────────────────────────────────────────────────────

TEST_F(ProgressOverlayTest, NoProgressByDefault) {
    EXPECT_FALSE(ctrl_.getViewData(3, 3).showProgress);
}

TEST_F(ProgressOverlayTest, NotRenderedWhenShowProgressFalse) {
    std::string out = renderCtrl();
    EXPECT_EQ(out.find("过滤处理中"), std::string::npos);
}

// ── After reprocess completes ──────────────────────────────────────────────────

TEST_F(ProgressOverlayTest, ShowProgressFalseAfterReprocessDone) {
    // Add a filter, trigger reprocess, wait for completion.
    key(ftxui::Event::Character('a'));
    for (char c : std::string("line"))
        key(ftxui::Event::Character(std::string(1, c)));
    key(ftxui::Event::Return);

    chain_.waitReprocess();

    EXPECT_FALSE(ctrl_.getViewData(3, 3).showProgress);
}

TEST_F(ProgressOverlayTest, NotRenderedAfterReprocessDone) {
    key(ftxui::Event::Character('a'));
    for (char c : std::string("line"))
        key(ftxui::Event::Character(std::string(1, c)));
    key(ftxui::Event::Return);

    chain_.waitReprocess();

    std::string out = renderCtrl();
    EXPECT_EQ(out.find("过滤处理中"), std::string::npos);
}
