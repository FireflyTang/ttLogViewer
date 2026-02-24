# Claude Development Guide for ttLogViewer

## Language Guidelines

**重要说明**：本项目语言使用规范
- **项目对话**: 使用中文进行讨论和交流
- **文档编写**: README、说明文档等使用中文编写
- **代码日志**: 代码中的日志输出使用英文
- **代码注释**: 源代码注释使用英文

## Current Status

**当前阶段：架构与功能设计阶段**

详细设计文档见 `docs/design.md`，该文档是功能纪要，持续更新。其中包含：
- 已确定的功能和交互设计
- 待定讨论的问题
- 已明确排除的功能

实现阶段尚未开始，不要生成代码，除非用户明确要求。

## Project Overview

ttLogViewer 是一个终端日志查看器，用 C++ 编写，目标是高效查看和过滤超大日志文件。

## Tech Stack

- **Language**: C++23（允许使用最新标准以获得高效简洁实现）
- **Build System**: CMake
- **UI Framework**: TUI - 待定（ncurses 或 FTXUI）
- **Configuration**: JSON（使用 nlohmann/json）
- **Target Platform**: Linux, macOS, Windows (Git Bash)
- **File Encoding**: UTF-8 only

## Architecture

三层架构，详见 `docs/design.md`：

1. **日志读取与缓存层**：mmap 文件读取、行索引、文件变更检测、双模式（静态/实时）
2. **链路过滤层**：链式正则过滤、颜色标记、增量缓存、JSON 持久化
3. **显示层**：TUI 渲染、键盘交互、虚拟列表

## Project Structure

```
ttLogViewer/
├── src/           # Source files
├── include/       # Header files
├── tests/         # Unit tests
├── docs/          # Documentation
│   └── design.md  # 功能设计纪要（持续更新）
├── examples/      # Example log files
└── CMakeLists.txt # Build configuration
```

## Development Guidelines

### Code Style
- Follow modern C++ best practices
- Use RAII for resource management
- Prefer standard library over custom implementations
- Use smart pointers for memory management
- Keep functions focused and testable
- Use `string_view` to avoid unnecessary copies

### Performance Considerations
- Memory-map large files instead of loading entirely
- Implement efficient line indexing
- Consider multithreading for filter processing
- Keep UI responsive at all times

### Error Handling
- Use exceptions for exceptional cases
- Validate file paths and permissions
- Provide meaningful error messages to user

## Dependencies

- **ncurses** or **FTXUI**: Terminal UI framework（待定）
- **nlohmann/json**: Configuration persistence
- **Catch2** or **Google Test**: Unit testing
