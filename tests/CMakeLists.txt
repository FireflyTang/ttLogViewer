include(GoogleTest)

# ── Test executable ────────────────────────────────────────────────────────────
add_executable(ttLogViewer_tests
    # Phase 1 (unchanged)
    unit/log_reader_test.cpp
    unit/app_controller_test.cpp
    render/log_pane_test.cpp

    # Phase 2
    unit/filter_chain_test.cpp
    render/status_bar_test.cpp
    render/filter_bar_test.cpp
    render/input_line_test.cpp
    render/dialog_overlay_test.cpp
    render/progress_overlay_test.cpp
    e2e/file_open_test.cpp
    e2e/filter_workflow_test.cpp
    e2e/realtime_test.cpp
    e2e/navigation_test.cpp
    e2e/search_test.cpp
    e2e/input_flow_test.cpp

    # Phase 3
    unit/render_utils_test.cpp
    e2e/session_test.cpp
    e2e/comprehensive_test.cpp

    # Config system
    unit/app_config_test.cpp

    # Text selection + clipboard
    e2e/selection_test.cpp

    # Tab completion
    e2e/completion_test.cpp

    # Pre-1.0 quality: ESC priority, GoTo boundaries, exclude filter
    e2e/esc_priority_test.cpp

    # Render-layer tests (high + medium priority)
    render/completion_popup_test.cpp
    render/search_highlight_render_test.cpp
    render/selection_render_test.cpp
    render/input_cursor_render_test.cpp

    # Post-v0.9.9 bug fixes (#24-#32)
    e2e/misc_keys_test.cpp
    e2e/input_cursor_test.cpp
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
