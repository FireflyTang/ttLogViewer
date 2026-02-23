# Claude Development Guide for ttLogViewer

## Language Guidelines

**重要说明**：本项目语言使用规范
- **项目对话**: 使用中文进行讨论和交流
- **文档编写**: README、说明文档等使用中文编写
- **代码日志**: 代码中的日志输出使用英文
- **代码注释**: 源代码注释使用英文

## Project Overview

ttLogViewer is a terminal-based log viewer application written in C++ that provides efficient viewing and filtering of large log files.

## Tech Stack

- **Language**: C++17 or later
- **Build System**: CMake
- **UI Framework**: Terminal UI (TUI) - exact library TBD (ncurses, FTXUI, etc.)
- **Target Platform**: macOS, Linux, (potentially Windows)

## Project Structure

```
ttLogViewer/
├── src/           # Source files
├── include/       # Header files
├── tests/         # Unit tests
├── docs/          # Documentation
├── examples/      # Example log files and configs
└── CMakeLists.txt # Build configuration
```

## Key Components

### 1. File Reader
- Efficient memory-mapped file reading for large files
- Lazy loading to handle files larger than available RAM
- Line indexing for fast navigation

### 2. Filter Engine
- Chain filter architecture: filters can be stacked
- Each filter processes output from previous filter
- Support for:
  - Regex patterns
  - Keyword matching
  - Time range filtering
  - Log level filtering

### 3. Terminal UI
- Display filtered log entries
- Navigation controls (scroll, jump, search)
- Status bar showing filter state
- Configuration for colors and formatting

### 4. Configuration
- YAML/JSON configuration file support
- Predefined filter chains
- Custom key bindings
- Color schemes

## Development Guidelines

### Code Style
- Follow modern C++ best practices
- Use RAII for resource management
- Prefer standard library over custom implementations
- Use smart pointers for memory management
- Keep functions focused and testable

### Performance Considerations
- Memory-map large files instead of loading entirely
- Implement efficient line indexing
- Use string_view to avoid unnecessary copies
- Consider multithreading for filter processing
- Profile before optimizing

### Error Handling
- Use exceptions for exceptional cases
- Validate file paths and permissions
- Handle corrupted or binary log files gracefully
- Provide meaningful error messages

## Building and Testing

```bash
# Debug build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Release build
cmake -DCMAKE_BUILD_TYPE=Release ..
make

# Run tests
ctest
```

## Dependencies to Consider

- **ncurses** or **FTXUI**: Terminal UI framework
- **spdlog**: Logging (for the viewer's own logging)
- **yaml-cpp** or **nlohmann/json**: Configuration parsing
- **Catch2** or **Google Test**: Unit testing
- **Boost.Filesystem** or `std::filesystem`: File operations

## Current Status

Project is in initial setup phase. Core components need to be implemented.

## Notes for Claude

- When implementing features, prioritize performance and memory efficiency
- Test with large files (>1GB) to ensure scalability
- Keep the UI responsive even when processing large datasets
- Document design decisions in code comments
- Consider cross-platform compatibility from the start
