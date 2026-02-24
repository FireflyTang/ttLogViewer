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
