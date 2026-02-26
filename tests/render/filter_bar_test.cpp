#include <gtest/gtest.h>
#include "render_test_base.hpp"

class FilterBarTest : public RenderTestBase {};

TEST_F(FilterBarTest, NoFiltersShowsPlaceholder) {
    EXPECT_NE(renderCtrl().find("无过滤器"), std::string::npos);
}

TEST_F(FilterBarTest, FilterPatternAppearsInBar) {
    chain_.append({.pattern = "ERROR"});
    EXPECT_NE(renderCtrl().find("ERROR"), std::string::npos);
}

TEST_F(FilterBarTest, MultipleFiltersShowNumbers) {
    chain_.append({.pattern = "A"});
    chain_.append({.pattern = "B"});
    std::string out = renderCtrl();
    EXPECT_NE(out.find("1"), std::string::npos);
    EXPECT_NE(out.find("2"), std::string::npos);
}

// ── v0.9.4: color dot indicators ─────────────────────────────────────────────

TEST_F(FilterBarTest, EnabledFilterShowsFilledDot) {
    // An enabled filter must render ⬤ (U+2B24, UTF-8: E2 AC A4)
    chain_.append({.pattern = "ERROR", .color = "#FF5555", .enabled = true});
    EXPECT_NE(renderCtrl().find("\xe2\xac\xa4"), std::string::npos);
}

TEST_F(FilterBarTest, DisabledFilterShowsEmptyDot) {
    // A disabled filter must render ○ (U+25CB, UTF-8: E2 97 8B)
    chain_.append({.pattern = "ERROR", .color = "#FF5555", .enabled = false});
    EXPECT_NE(renderCtrl().find("\xe2\x97\x8b"), std::string::npos);
}
