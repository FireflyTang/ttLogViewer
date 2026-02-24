cmake_minimum_required(VERSION 3.15)

# Project definition
project(ttLogViewer
    VERSION 0.1.0
    DESCRIPTION "A fast and efficient terminal log file viewer written in C++"
    LANGUAGES CXX
)

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler-specific options
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Include FetchContent module
include(FetchContent)

# Fetch FTXUI
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v6.1.9
)

# Configure FTXUI options
option(FTXUI_BUILD_DOCS "Set to ON to build tests" OFF)
option(FTXUI_BUILD_EXAMPLES "Set to ON to build examples" OFF)
option(FTXUI_ENABLE_INSTALL "Generate the install target" OFF)
FetchContent_MakeAvailable(ftxui)

# JSON library will be added later when needed
# FetchContent_Declare(json
#     GIT_REPOSITORY https://github.com/nlohmann/json.git
#     GIT_TAG v3.11.3
# )
# FetchContent_MakeAvailable(json)

# Project directories
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Create include directory if it doesn't exist
file(MAKE_DIRECTORY ${INCLUDE_DIR})
file(MAKE_DIRECTORY ${SOURCE_DIR})

# Main executable with sources
add_executable(ttLogViewer
    ${SOURCE_DIR}/main.cpp
)

# Include directories
target_include_directories(ttLogViewer PRIVATE
    ${INCLUDE_DIR}
    ${SOURCE_DIR}
)

# Link libraries
target_link_libraries(ttLogViewer PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    # nlohmann_json::nlohmann_json  # Add later when needed
)

# Platform-specific linking
if(WIN32)
    # Windows-specific libraries if needed
    target_link_libraries(ttLogViewer PRIVATE)
elseif(UNIX AND NOT APPLE)
    # Linux-specific libraries if needed
    target_link_libraries(ttLogViewer PRIVATE pthread)
elseif(APPLE)
    # macOS-specific libraries if needed
    target_link_libraries(ttLogViewer PRIVATE)
endif()

# Set output directory
set_target_properties(ttLogViewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Enable testing
enable_testing()

# Add subdirectories (create them later)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()