include(GoogleTest)

# ── Test executable ────────────────────────────────────────────────────────────
add_executable(ttLogViewer_tests
    unit/log_reader_test.cpp
    unit/app_controller_test.cpp
    render/log_pane_test.cpp
)

target_include_directories(ttLogViewer_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/helpers
)

target_link_libraries(ttLogViewer_tests PRIVATE
    ttLogViewer_lib
    GTest::gtest_main
    GTest::gmock
)

set_target_properties(ttLogViewer_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

gtest_discover_tests(ttLogViewer_tests)
