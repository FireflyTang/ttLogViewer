# Claude Development Guide for ttLogViewer

## Language Guidelines

**重要说明**：本项目语言使用规范
- **项目对话**：使用中文进行讨论和交流
- **文档编写**：README、说明文档等使用中文编写
- **代码注释**：源代码注释使用英文
- **代码日志**：代码中的日志输出使用英文

---

## Current Status

**v0.9.2 — 搜索增强 + 过滤器字符串/正则模式切换**

- 阶段一：框架搭建 + 静态文件浏览 ✓
- 阶段二：过滤链 + 搜索 + 实时监控 ✓
- 阶段三：辅助功能 + 会话持久化 + 配置系统 ✓
- 基础设施：版本管理 + 静态打包 ✓
- v0.9.2：搜索词 bold+underline 高亮、焦点感知搜索、过滤器字符串/正则切换（x 键）、过滤栏匹配数显示 ✓

当前测试：247 个，全部通过。

---

## Project Overview

ttLogViewer 是一个终端日志查看器，用 C++ 编写，目标是高效查看和过滤超大日志文件。

---

## Tech Stack

- **Language**: C++23
- **Build System**: CMake 3.21+ + Ninja
- **UI Framework**: FTXUI v6.1.9+（TUI 框架，静态链接）
- **Configuration**: nlohmann/json v3.11.3+（配置持久化）
- **Testing**: Google Test v1.14+ + Google Mock
- **Target Platform**: Linux, macOS, Windows (MSYS2 MinGW64)
- **File Encoding**: UTF-8 only

---

## Architecture

四层架构，详见 `design.md`：

1. **LogReader**：mmap 文件读取、后台行索引、文件变更检测、双模式（静态/实时）
2. **FilterChain**：链式正则过滤、FilterNode 阶段缓存、颜色标记、JSON 持久化
3. **AppController**：双窗格状态、键盘事件分发、搜索
4. **渲染层**：纯 FTXUI 渲染，`CreateMainComponent(AppController&)`，无业务状态

### 关键架构决策

- **线程模型**：后台线程只做纯计算（FileWatcher、reprocess、搜索），通过 `PostFn` 回 UI 线程
- **mmap 安全**：remap 在 UI 线程执行，`getLine()` 可安全返回 `string_view`
- **FilterNode**：`FilterDef`（规则）+ `std::regex`（预编译）+ `output`（缓存）三合一
- **虚拟列表**：`AppController::getViewData(paneHeight)` 裁剪可见切片，渲染层只做排列
- **AppConfig**：全局配置单例，所有可调参数的唯一来源，JSON 可覆盖
- **版本管理**：`CMakeLists.txt` 的 `VERSION` 字段是唯一来源，cmake configure_file 生成 version.hpp

---

## Project Structure

```
ttLogViewer/
├── design.md              # 功能设计 + 接口设计 + 配置系统 + 版本管理（功能规格文档）
├── implementation.md      # 实现细节 + 类报告 + 线程模型 + 二次开发指南（实现记忆文档）
├── README.md              # 用户文档（构建、快捷键、配置项）
├── CLAUDE.md              # 开发约定（本文件）
├── CMakeLists.txt         # 根构建文件（含版本号）
├── cmake/
│   └── version.hpp.in     # CMake 版本模板 → build/include/version.hpp
├── include/               # 所有头文件（接口优先）
├── src/                   # 实现文件 + CMakeLists.txt
├── tests/
│   ├── unit/              # 单元测试
│   ├── render/            # FTXUI 渲染快照测试
│   ├── e2e/               # 端到端测试（真实四层）
│   └── helpers/           # 共享测试基础设施
├── scripts/
│   └── build-release.sh   # 静态打包脚本（多平台）
└── examples/              # 示例日志文件
```

---

## Document Division of Labor

| 文档 | 职责 | 修改时机 |
|------|------|----------|
| `design.md` | 功能清单 + 高层架构决策 + 接口规范 | 新增功能或架构变更时 |
| `implementation.md` | 具体实现细节、类报告、线程模型、二次开发指南 | 随时按需补充，是开发记忆文档 |
| `README.md` | 用户文档（构建、快捷键、配置项） | 发版时同步，功能变更时同步 |
| `CLAUDE.md` | 开发约定、规范、流程 | 约定变更时更新 |

**规则**：
- `design.md` 不作为实现细节的载体，不随意改动
- 每次新增功能，自动更新 `design.md`（若影响功能设计/接口）和 `implementation.md`（若有实现细节值得记录）
- 新增配置项时，同步更新 `design.md` → 配置系统字段表 + `README.md` JSON 示例

---

## Version Management

**版本号唯一来源**：`CMakeLists.txt` 的 `project(ttLogViewer VERSION X.Y.Z ...)`

CMake 构建时自动生成 `${CMAKE_BINARY_DIR}/include/version.hpp`，包含 `TTLOGVIEWER_VERSION` 宏。

**UI 版本显示**：按 `h` 帮助对话框标题格式为 `"ttLogViewer vX.Y.Z  帮助"`。

---

## Release Process（发版流程）

发版时按顺序执行以下步骤：

1. **更新版本号**：修改 `CMakeLists.txt` 的 `project(... VERSION X.Y.Z ...)`
2. **同步文档**：
   - `README.md`：更新"开发状态"的版本号和测试数量
   - `implementation.md`：更新顶部版本号和日期
3. **构建 + 测试**：
   ```bash
   cmake -B build -G Ninja -DCMAKE_C_COMPILER=... -DCMAKE_CXX_COMPILER=...
   /c/msys64/mingw64/bin/ninja.exe -C build
   build/bin/ttLogViewer_tests.exe   # 必须全部通过
   ```
4. **提交 + Tag**：
   ```bash
   git add -A
   git commit -m "Release vX.Y.Z: ..."
   git tag vX.Y.Z
   ```
5. **静态打包**：
   ```bash
   bash scripts/build-release.sh
   # 产物：dist/ttLogViewer-vX.Y.Z-{platform}.exe
   ```
6. **验证二进制**（Windows）：
   ```bash
   ldd dist/ttLogViewer-vX.Y.Z-windows-x64.exe
   # 只应列出 Windows 系统 DLL，无 libstdc++/libgcc/libwinpthread
   ```
7. **推送 + 发布**：
   ```bash
   git push && git push --tags
   gh release create vX.Y.Z \
     --title "vX.Y.Z — ..." \
     --notes "发版说明" \
     dist/ttLogViewer-vX.Y.Z-windows-x64.exe
   ```

---

## Build (Windows MSYS2 MinGW64)

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"

cmake -B build -G Ninja \
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe

/c/msys64/mingw64/bin/ninja.exe -C build
# 产物：build/bin/ttLogViewer.exe
```

---

## Development Guidelines

### Code Style
- Follow modern C++ best practices (C++23)
- Use RAII for resource management
- Prefer standard library over custom implementations
- Use smart pointers for memory management
- Keep functions focused and testable
- Use `string_view` to avoid unnecessary copies
- All tunable constants go through `AppConfig` — no magic numbers in source

### Error Handling
- Use exceptions for exceptional cases
- Validate file paths and permissions
- Provide meaningful error messages in English

### Testing
- Unit tests: inject mocks via `ILogReader` / `IFilterChain` interfaces
- Render tests: render to `ftxui::Screen` in-memory buffer
- E2E tests: real four-layer objects, drive via `handleKey()`, assert with `getViewData()`
- Async: replace `postFn` with synchronous lambda in tests

---

## Dependencies

所有依赖均通过 CMake FetchContent 自动下载，无需手动安装：
- **FTXUI v6.1.9+**: TUI framework（静态库）
- **nlohmann/json v3.11.3+**: JSON 配置持久化
- **Google Test v1.14+**: 单元测试 + Mock
