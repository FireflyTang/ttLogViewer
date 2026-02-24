#include <gtest/gtest.h>
#include "render_test_base.hpp"

class StatusBarTest : public RenderTestBase {};

TEST_F(StatusBarTest, ShowsStaticMode) {
    reader_.setMode(FileMode::Static);
    EXPECT_NE(renderCtrl().find("静态"), std::string::npos);
}

TEST_F(StatusBarTest, ShowsRealtimeMode) {
    reader_.setMode(FileMode::Realtime);
    EXPECT_NE(renderCtrl().find("实时"), std::string::npos);
}

TEST_F(StatusBarTest, ShowsLineCount) {
    EXPECT_NE(renderCtrl().find("5"), std::string::npos);
}

TEST_F(StatusBarTest, ShowsFileName) {
    // The temp file path contains "ttlv_test_" as a prefix
    EXPECT_NE(renderCtrl().find("ttlv_test_"), std::string::npos);
}
