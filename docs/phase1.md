# 阶段一：框架搭建 + 静态文件浏览

## 目标

建立完整的项目框架，使后续阶段只需**填充实现内容**，不再进行结构性变动。阶段结束时，程序可打开日志文件并进行基础浏览，所有键盘交互框架已就位（即使部分功能尚未实现）。

**交付价值**：一个可实际使用的静态日志查看器——打开文件、上下翻行、翻页、跳首尾。

---

## 范围

### 本阶段实现

**构建系统**
- `CMakeLists.txt`（根目录）：完整配置，包含所有目标和依赖
- `src/CMakeLists.txt`、`tests/CMakeLists.txt`：子目录构建
- FetchContent 引入：FTXUI v6.1.9、Google Test v1.14
- 编译目标：`ttLogViewer`（可执行文件）、`ttLogViewer_tests`（测试）
- 编译标准：C++23

**头文件（所有接口在本阶段完全定义，后续阶段不再修改签名）**
- `include/log_reader.hpp`：完整 LogReader 接口（含所有枚举、回调类型）
- `include/filter_chain.hpp`：完整 FilterChain 接口（FilterDef / FilterNode / ColorSpan 数据结构）
- `include/app_controller.hpp`：完整 AppController 接口（ViewData / PaneState / LogLine / InputMode 枚举）
- `include/render.hpp`：`CreateMainComponent()` 声明

**LogReader 实现（静态模式子集）**
- `open()`：POSIX `mmap()` + 同步行索引扫描（无后台线程）
- `close()`：RAII 释放 mmap
- `getLine()` / `getLines()`：返回 `string_view`，O(1)
- `lineCount()`：返回已索引行数（同步索引，打开即完成）
- `isIndexing()`：始终返回 `false`（后台索引在阶段二实现）
- `setMode()` / `mode()`：存储模式枚举，FileWatcher 暂不启动
- `forceCheck()`：桩实现（空函数）
- `onNewLines()` / `onFileReset()`：存储回调，暂不触发
- 换行符兼容：LF / CRLF

**FilterChain 实现（透传桩）**
- `append()` / `remove()` / `edit()` / `moveUp()` / `moveDown()`：桩（空实现，后续阶段填充）
- `filteredLineCount()`：返回 `reader_.lineCount()`（无过滤器 = 全量通过）
- `filteredLineAt(i)`：返回 `i + 1`（1-based 原始行号，身份映射）
- `filteredLines()`：返回连续行号数组
- `computeColors()`：返回空 span 列表
- `processNewLines()` / `reprocess()`：桩
- `save()` / `load()`：桩

**AppController 实现（None 模式 + 基础导航）**
- 构造函数：初始化双窗格 PaneState，注册 LogReader 回调
- `handleKey()`：分发逻辑框架（switch(inputMode_) → 各私有方法），本阶段实现：
  - `↑` / `↓`：移动光标（边界保护）
  - `PgDn` / `PgUp`：翻页（边界保护）
  - `Home` / `End`：跳首/末
  - `Tab` / `Shift`：切换焦点区域（rawFocused ↔ filteredFocused）
  - `q`：退出（向 FTXUI ScreenInteractive 发 Quit 事件）
  - 其余键：忽略（返回 false）
- `getViewData(rawPaneHeight, filteredPaneHeight)`：
  - 按 PaneState 裁剪可见切片
  - 调用 `filterChain_.filteredLineAt()` 取行号，调用 `reader_.getLine()` 取内容
  - 调用 `filterChain_.computeColors()` 取颜色段（本阶段返回空）
  - 构建完整 ViewData 快照
- `onTerminalResize()`：重新校正 scrollOffset

**渲染层实现（完整布局框架）**
- `CreateMainComponent()`：装配全部子组件，返回 FTXUI Component
- `StatusBar`：文件名、模式（Static/Realtime）、总行数、"索引中..." 提示
- `LogPane`：虚拟列表（根据 paneHeight 裁剪），高亮行 `▶` 标记，colorSpan hbox 拼合（本阶段 span 为空，退化为单段 text）
- `FilterBar`：无过滤器时显示占位文字
- `InputLine`：None 模式下显示空白或提示栏
- `DialogOverlay`：组件框架就位，本阶段不触发
- `ProgressOverlay`：组件框架就位，本阶段不触发
- 整体布局与 design.md 草图一致（顶栏 / 上半 / 分隔线 / 下半 / 过滤栏 / 输入行）

**主程序**
- `src/main.cpp`：解析命令行参数（可选文件路径），创建四层对象，接线，启动 FTXUI ScreenInteractive 事件循环

**测试辅助工具**
- `tests/helpers/temp_file.hpp`：TempFile RAII 类（创建 / 追加 / 截断重写 / 自动删除）
- `tests/helpers/test_utils.hpp`：`waitForIndexing()`、`waitForReprocess()` 桩声明（阶段二填充实现）

**测试（本阶段通过）**
- `tests/unit/log_reader_test.cpp`：基础读取
- `tests/unit/app_controller_test.cpp`：导航操作
- `tests/render/log_pane_test.cpp`：虚拟列表渲染

### 本阶段不实现

- 后台索引线程（IndexThread）
- 文件监控线程（FileWatcher）
- 实时模式（r/s/f 键无效）
- 过滤器操作（a/e/d/[]/]/+/-/Space 键无效）
- 搜索（/ n p Esc 无效）
- 行号跳转（g 键无效）
- G 锁定跟随末尾
- 文件切换（o 键无效）
- 进度条 / 弹窗（渲染框架存在但不激活）
- 导出（w 键无效）
- 折叠超长行（z 键无效）
- 行号显示切换（l 键无效）
- 帮助弹窗（h 键无效）
- JSON 会话持久化
- `ILogReader` / `IFilterChain` 抽象接口（Mock 在阶段二引入）

---

## 文件清单

### 新建文件

```
CMakeLists.txt                          ← 根构建配置
src/
├── CMakeLists.txt
├── main.cpp
├── log_reader.cpp
├── filter_chain.cpp
├── app_controller.cpp
└── render.cpp
include/
├── log_reader.hpp
├── filter_chain.hpp
├── app_controller.hpp
└── render.hpp
tests/
├── CMakeLists.txt
├── helpers/
│   ├── temp_file.hpp
│   └── test_utils.hpp
├── unit/
│   ├── log_reader_test.cpp
│   └── app_controller_test.cpp
└── render/
    └── log_pane_test.cpp
```

### 已有文件（不修改内容）

```
docs/design.md
docs/phase1.md（本文件）
docs/phase2.md
docs/phase3.md
CLAUDE.md
README.md
```

---

## 接口契约（本阶段锁定，后续阶段不改签名）

本阶段完成后，以下接口签名固定：

```cpp
// include/log_reader.hpp
class LogReader {
    bool             open(std::string_view path);
    void             close();
    std::string_view getLine(size_t lineNo) const;          // 1-based
    std::vector<std::string_view> getLines(size_t from, size_t to) const;
    size_t           lineCount() const;
    bool             isIndexing() const;
    void             setMode(FileMode mode);
    FileMode         mode() const;
    void             forceCheck();
    void             onNewLines(NewLinesCallback cb);
    void             onFileReset(FileResetCallback cb);
    std::string_view filePath() const;
};

// include/filter_chain.hpp
class FilterChain {
    size_t              filteredLineCount() const;
    size_t              filteredLineAt(size_t filteredIndex) const;
    std::vector<size_t> filteredLines(size_t from, size_t count) const;
    std::vector<ColorSpan> computeColors(size_t rawLineNo, std::string_view content) const;
    // ... 其余接口见 design.md
};

// include/app_controller.hpp
class AppController {
    bool     handleKey(const ftxui::Event& event);
    ViewData getViewData(int rawPaneHeight, int filteredPaneHeight) const;
    void     onTerminalResize(int width, int height);
};

// include/render.hpp
ftxui::Component CreateMainComponent(AppController& controller);
```

---

## 实现要点

### mmap 平台兼容

目标平台为 MSYS2 MinGW64（POSIX 兼容层），直接使用 `<sys/mman.h>` + `mmap()`。

```cpp
// 内部 RAII 封装，不暴露到公共接口
struct MmapRegion {
    void*  ptr  = MAP_FAILED;
    size_t size = 0;
    int    fd   = -1;
    MmapRegion(std::string_view path);
    ~MmapRegion();  // munmap + close
    // non-copyable, movable
};
```

若未来需要支持纯 Win32（无 MSYS2 运行时），在此类内部用 `#ifdef _WIN32` 切换到 `CreateFileMapping` / `MapViewOfFile`，公共接口无需变动。

### lineOffsets_ 数据结构

```cpp
// src/log_reader.cpp 内部
std::vector<uint64_t> lineOffsets_;  // lineOffsets_[i] = 第 i+1 行在 mmap 中的字节偏移
                                     // 阶段一：open() 同步扫描建立
                                     // 阶段二：改为 IndexThread 后台建立
```

阶段一直接在 `open()` 调用时扫描，无需 atomic/mutex（单线程）。阶段二重构为后台线程时，只改 `open()` 内部实现，公共接口 `lineCount()` / `getLine()` 签名不变。

### getViewData() 性能

`getViewData()` 在 FTXUI 每次重绘时调用（约 60fps），需控制开销。本阶段 `computeColors()` 返回空列表，性能无压力。阶段二引入颜色计算后，只计算可见行（不超过 paneHeight 行），每行仅遍历 filterCount 次正则匹配。

### 渲染层无状态

`CreateMainComponent()` 返回的 Component 只持有 `AppController&` 引用，不缓存任何业务数据。每次 `Render()` 调用 `getViewData()` 取快照，确保显示永远与状态一致。

---

## 测试清单

### LogReader 单元测试（`tests/unit/log_reader_test.cpp`）

| 测试用例 | 断言 |
|---------|------|
| 打开普通文本文件 | `open()` 返回 true；`lineCount()` 与实际行数一致 |
| `getLine(1)` 内容正确 | 第一行字节内容与预期相符 |
| `getLine(lineCount())` 内容正确 | 末行内容正确（含/不含末尾换行） |
| `getLine()` 越界 | `lineNo=0` 和 `lineNo > lineCount()` 不 crash |
| `getLines(1, 3)` 批量读取 | 返回正确的 3 个 string_view |
| `getLines(from > to)` | 返回空 vector |
| 空文件 | `open()` 成功；`lineCount()=0` |
| 单行无末尾换行 | `lineCount()=1`；内容不含换行符 |
| 只有一个换行符的文件 | 行为明确（0 行或 1 空行，取决于约定） |
| CRLF 文件 | `getLine()` 返回内容不含 `\r` |
| 混合换行符（LF + CRLF）| 每行内容正确，无 `\r` |
| 含中文 UTF-8 的行 | `getLine()` 返回字节完整，不截断多字节字符 |
| 超长单行（1MB+） | 正常返回 string_view，不崩溃 |
| 无读权限文件 | `open()` 返回 false |
| `open()` 未先 `close()` 二次调用 | 自动关闭前一个，重新打开成功 |
| `close()` 后 `lineCount()` | 返回 0 或 undefined but no crash |

### AppController 单元测试（`tests/unit/app_controller_test.cpp`）

| 测试用例 | 断言 |
|---------|------|
| `↓` 移动光标 | `cursor` 从 0 增到 1 |
| `↓` 在末行 | `cursor` 保持在末行，不越界 |
| `↑` 在首行 | `cursor` 保持在 0 |
| `↑` 移动光标 | `cursor` 减一 |
| `PgDn` 翻页 | `cursor` 前进 paneHeight 行 |
| `PgDn` 末页 | `cursor` 停在末行 |
| `PgUp` 首页 | `cursor` 停在 0 |
| `Home` | `cursor=0` 无论当前位置 |
| `End` | `cursor` = 末行 |
| `Tab`/`Shift` 切换焦点 | `ViewData.rawFocused` 和 `filteredFocused` 互斥翻转 |
| 切换焦点后另一区 cursor 不变 | `rawPane.cursor` 不受 filtered 操作影响 |
| `getViewData()` 裁剪正确 | `paneHeight=5` → `rawPane.size()=5`（行数足够时） |
| `paneHeight=0` | 返回空 pane，不 crash |
| `paneHeight > totalLines` | 返回全部行 |
| 文件为空 | `getViewData()` 双区均返回空切片，不 crash |

### LogPane 渲染测试（`tests/render/log_pane_test.cpp`）

使用 `ftxui::Screen::Create(Dimension::Fixed(80, 10))` + `Render()` 渲染到内存 buffer：

| 测试用例 | 断言 |
|---------|------|
| 高亮行含 `▶` 标记 | screen buffer 对应行包含 `▶` 字符 |
| 非高亮行不含 `▶` | 其余行不含 `▶` |
| paneHeight=5 时恰好 5 行 | buffer 中有效行数为 5 |
| 空 pane（0 行）| 渲染不 crash，不含 `▶` |
| 含颜色 span 的行 | 渲染后内容字符正确（颜色属性暂不断言） |

---

## 完成标准（Definition of Done）

1. **编译无警告**：`cmake --build build` 以 `-Wall -Wextra` 通过
2. **测试全绿**：`ctest` 运行上述所有测试用例，全部通过
3. **功能验证**：
   - `./ttLogViewer <日志文件>` 可启动
   - 上下键移动高亮行，PgUp/PgDn 翻页，Home/End 跳首末
   - Tab/Shift 切换焦点区域
   - q 退出
   - 状态栏显示文件名、模式（Static）、总行数
4. **接口稳定**：四个头文件中所有公共接口签名与 design.md 完全一致
5. **桩编译通过**：所有阶段二/三的键盘路径（未实现的功能键）已有占位处理（忽略该键，不 crash）

---

## 依赖关系

- 无前置阶段（本阶段为起点）
- 阶段二依赖本阶段建立的接口契约
