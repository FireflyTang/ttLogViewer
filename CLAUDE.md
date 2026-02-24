# Claude Development Guide for ttLogViewer

## Language Guidelines

**重要说明**：本项目语言使用规范
- **项目对话**: 使用中文进行讨论和交流
- **文档编写**: README、说明文档等使用中文编写
- **代码日志**: 代码中的日志输出使用英文
- **代码注释**: 源代码注释使用英文

## Current Status

**当前阶段：测试方案设计阶段**

已完成：
- 功能与交互设计（见 `docs/design.md`）
- 四层模块接口设计（见 `docs/design.md` → 模块接口设计章节）
- 构建环境验证（CMake + FTXUI Hello World 可编译运行）

进行中：
- 测试方案设计

## Project Overview

ttLogViewer 是一个终端日志查看器，用 C++ 编写，目标是高效查看和过滤超大日志文件。

## Tech Stack

- **Language**: C++23（允许使用最新标准以获得高效简洁实现）
- **Build System**: CMake + Ninja
- **UI Framework**: FTXUI v6.1.9+（现代化跨平台 TUI 库）
- **Configuration**: nlohmann/json（待引入）
- **Target Platform**: Linux, macOS, Windows (Git Bash / MSYS2)
- **File Encoding**: UTF-8 only

## Architecture

四层架构，完整设计见 `docs/design.md`：

1. **LogReader**（日志读取层）：mmap 文件读取、后台行索引、文件变更检测、双模式（静态/实时）
2. **FilterChain**（链路过滤层）：链式正则过滤、FilterNode 阶段缓存、颜色标记、JSON 持久化
3. **AppController**（状态管理层）：双窗格状态、键盘事件分发、搜索
4. **渲染层**：纯 FTXUI 渲染，`CreateMainComponent(AppController&)`，无业务状态

### 关键架构决策

- **线程模型**：后台线程只做纯计算（FileWatcher 侦测、reprocess、搜索），结果 atomic swap，通过 `PostEvent` 回 UI 线程
- **mmap 安全**：FileWatcher 只侦测变化，remap 在 UI 线程执行，`getLine()` 可安全返回 `string_view`
- **FilterNode**：`FilterDef`（规则）+ `std::regex`（预编译）+ `output`（阶段行号缓存）三者绑定为同一对象
- **虚拟列表**：`AppController::getViewData(paneHeight)` 负责裁剪可见切片，渲染层只做排列

## Project Structure

```
ttLogViewer/
├── src/           # Source files
├── include/       # Header files
├── tests/         # Unit tests
├── docs/
│   └── design.md  # 完整设计文档（功能设计 + 模块接口）
├── examples/      # Example log files
└── CMakeLists.txt
```

## Build (Windows MSYS2 MinGW64)

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"

cmake -B build -G Ninja \
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe

/c/msys64/mingw64/bin/ninja.exe -C build
```

## Development Guidelines

### Code Style
- Follow modern C++ best practices
- Use RAII for resource management
- Prefer standard library over custom implementations
- Use smart pointers for memory management
- Keep functions focused and testable
- Use `string_view` to avoid unnecessary copies

### Error Handling
- Use exceptions for exceptional cases
- Validate file paths and permissions
- Provide meaningful error messages in English

## Dependencies

- **FTXUI v6.1.9+**: TUI framework（CMake FetchContent 自动下载）
- **nlohmann/json**: Configuration persistence（待引入，CMake FetchContent）
- **Catch2 or Google Test**: Unit testing（测试框架待定）
