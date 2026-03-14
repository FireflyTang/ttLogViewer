# Claude Development Guide for ttLogViewer

## Language Guidelines

**重要说明**：本项目语言使用规范
- **项目对话**：使用中文进行讨论和交流
- **文档编写**：README、说明文档等使用中文编写
- **代码注释**：源代码注释使用英文
- **代码日志**：代码中的日志输出使用英文

---

## Current Status

**v0.9.16**

- 阶段一：框架搭建 + 静态文件浏览 ✓
- 阶段二：过滤链 + 搜索 + 实时监控 ✓
- 阶段三：辅助功能 + 会话持久化 + 配置系统 ✓
- 基础设施：版本管理 + 静态打包 ✓
- v0.9.2：搜索词高亮、焦点感知搜索、过滤栏匹配数显示 ✓
- v0.9.3：鼠标滚轮滚动 + 单击切焦点、←/→ 水平滚动（UTF-8 安全） ✓
- v0.9.4：退出确认弹窗、过滤器颜色圆点、搜索词状态栏、行号默认开启 ✓
- v0.9.5：行号右对齐（内容列固定）、禁用过滤器彩色圆点、窗格 6:4 比例、ESC 中断过滤 ✓
- v0.9.6：搜索词反色、ESC/Tab 清除搜索、搜索正则模式切换（Tab）、过滤器圆点优化 ✓
- v0.9.7：正则切换统一 Tab、输入行合法性圆点统一、s 键切换静态/实时、代码重构清理 ✓
- v0.9.8：动态底部行、过滤器圆点不反色、搜索只高亮当前结果、进度条重绘修复、x 跳转原始行、Ctrl+滚轮透传、单击移动光标、滚轮跟随焦点 ✓
- post-v0.9.8：进度弹窗根本修复（FTXUI frame_valid_）、ESC 退出选文模式、跳转居中、圆点背景一致 ✓
- post-v0.9.8：排除过滤器（Tab 4 模式）、字符级文本选择 + 剪贴板（拖拽/Ctrl+C/Ctrl+V）✓
- v0.9.9：文件路径 Tab 补全（向上弹窗+中文路径）、拖拽自动滚动（竖向+横向）、选区绝对行号修复 ✓
- v0.9.10：输入光标移动、补全弹窗 overlay、Ctrl+C 不退出（Windows 三层防御）、过滤器圆点颜色修复、横向自动滚动修复、补全弹窗滚动+宽度对齐 ✓
- v0.9.11：Ctrl+C SIGINT 修复（MSYS2/mintty）、补全弹窗边框对齐、Del 键支持、补全弹窗渲染测试 ✓
- v0.9.12：Ctrl+C 根本修复（禁用 ENABLE_PROCESSED_INPUT）、全面渲染层测试补充（+40 tests）✓
- v0.9.13：控制字符渲染修复（\r/\x01/\x7F→'.'）、Ctrl+C 复盘文档、控制字符渲染层测试（+8 tests）✓
- post-v0.9.13：渲染层测试覆盖补全（overlay/viewport/drag/highlight 各+1，共+4 tests）✓
- v0.9.14：Ctrl+C 复制修复（mintty/PTY 双路径：ctrlHandler→PostEvent）、debug log 清理、CatchEvent 路径渲染测试（+1 test）✓
- v0.9.15：文件编码自动识别（UTF-16LE/BE、UTF-8 BOM）、BOM 检测 + utf16ToUtf8 转换 + decoded_ 缓冲区、单元/渲染/E2E 测试（+16 tests）✓
- v0.9.16：水平滚动裁剪修复（truncateToDisplayWidth，按显示列截断）、块字符/全角字符滚动不再错位（+9 tests）✓

当前测试：482 个，全部通过。

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
   # 仅暂存与发版相关的文件（避免意外包含 .env 等敏感文件）
   git add CMakeLists.txt implementation.md README.md CLAUDE.md
   # 如有其他变更文件按需追加
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

### 工作流规则（重要）

- **版本号**：不得自行修改版本号。版本号变更必须由用户明确要求，再执行发版流程第1步。
- **开工前对齐**：所有 bug 修复和新功能，必须先向用户说明方案和最终效果，得到明确同意后才能开始修改代码。

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
