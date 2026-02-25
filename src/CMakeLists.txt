# ── Core library (all layers except main) ─────────────────────────────────────
add_library(ttLogViewer_lib STATIC
    app_config.cpp
    log_reader.cpp
    filter_chain.cpp
    app_controller.cpp
    render_utils.cpp
    render.cpp
)

target_include_directories(ttLogViewer_lib
    PUBLIC  ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_BINARY_DIR}/include   # Generated version.hpp
)

target_link_libraries(ttLogViewer_lib PUBLIC
    ftxui::screen
    ftxui::dom
    ftxui::component
    nlohmann_json::nlohmann_json
    ${PLATFORM_LIBS}
)

# ── Main executable ────────────────────────────────────────────────────────────
add_executable(ttLogViewer main.cpp)

target_link_libraries(ttLogViewer PRIVATE ttLogViewer_lib)

set_target_properties(ttLogViewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(STATIC_RUNTIME AND DEFINED STATIC_RUNTIME_FLAGS)
    target_link_options(ttLogViewer PRIVATE ${STATIC_RUNTIME_FLAGS})
endif()
