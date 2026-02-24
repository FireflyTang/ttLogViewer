cmake_minimum_required(VERSION 3.21)

project(ttLogViewer
    VERSION 0.1.0
    DESCRIPTION "A fast terminal log file viewer"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(FetchContent)

# ── FTXUI ──────────────────────────────────────────────────────────────────────
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG        v6.1.9
    GIT_SHALLOW    TRUE
)
option(FTXUI_BUILD_DOCS     "Build FTXUI docs"     OFF)
option(FTXUI_BUILD_EXAMPLES "Build FTXUI examples" OFF)
option(FTXUI_ENABLE_INSTALL "FTXUI install target" OFF)
FetchContent_MakeAvailable(ftxui)

# ── Google Test (Phase 1) ──────────────────────────────────────────────────────
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── nlohmann/json (Phase 2 – declared now, made available in phase 2) ─────────
# FetchContent_Declare(json
#     GIT_REPOSITORY https://github.com/nlohmann/json.git
#     GIT_TAG        v3.11.3
#     GIT_SHALLOW    TRUE
# )
# FetchContent_MakeAvailable(json)

# ── Platform-specific link libraries ──────────────────────────────────────────
set(PLATFORM_LIBS "")
if(WIN32)
    # MinGW/MSYS2: nothing extra needed for pthreads (built-in)
elseif(UNIX AND NOT APPLE)
    list(APPEND PLATFORM_LIBS pthread)
endif()

# ── Sub-directories ───────────────────────────────────────────────────────────
add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
