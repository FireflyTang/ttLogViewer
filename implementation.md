# ttLogViewer 实现报告

> 版本：v0.9.14
> 测试：457 个，全部通过
> 最后更新：2026-03-07

本文档是 ttLogViewer 的"开发记忆文档"，面向维护者和二次开发者，记录实际实现细节、架构决策依据、以及扩展指南。功能需求和接口设计见 [design.md](design.md)。

---

## 1. 架构总览

```
┌─────────────────────────────────────────────────────────────┐
│                        渲染层 (render.cpp)                   │
│  CreateMainComponent()  ←  ViewData snapshot  ← getViewData │
└────────────────────────┬────────────────────────────────────┘
                         │ handleKey()
┌────────────────────────▼────────────────────────────────────┐
│                  AppController (app_controller.cpp)          │
│  PaneState×2 · InputMode状态机 · 搜索 · 折叠 · 对话框       │
└──────┬───────────────────────────────────────┬──────────────┘
       │                                       │
┌──────▼──────────────────┐    ┌───────────────▼──────────────┐
│   FilterChain            │    │        LogReader              │
│  (filter_chain.cpp)      │    │     (log_reader.cpp)          │
│  FilterNode×N · reprocess│    │  mmap · IndexThread           │
│  stage cache · JSON 持久  │    │  FileWatcher · 双模式         │
└──────────────────────────┘    └──────────────────────────────┘
```

### 数据流

**Pull 流（渲染时）**：`render.cpp` 调用 `controller.getViewData(h1, h2)` → controller 从 `reader_.getLine()` 和 `chain_.filteredLines()` 取数据 → 组装 `ViewData` 快照 → render 纯粹绘制。

**Push 流（文件变化时）**：`FileWatcher` 侦测到大小变化 → `PostEvent` 回 UI 线程 → `handleNewLines()` 触发 `chain_.processNewLines()` → `chain_` 增量追加输出 → 下一帧 `getViewData` 自动包含新行。

---

## 2. 文件组织

```
ttLogViewer/
├── include/                    # 所有头文件（接口优先）
│   ├── i_log_reader.hpp        # LogReader 抽象接口（测试用 mock 基类）
│   ├── i_filter_chain.hpp      # FilterChain 抽象接口
│   ├── log_reader.hpp          # LogReader 实现声明（111行）
│   ├── filter_chain.hpp        # FilterChain + FilterNode（108行）
│   ├── app_controller.hpp      # AppController + ViewData + PaneState（356行）
│   ├── app_config.hpp          # AppConfig 全局配置单例（75行）
│   ├── render.hpp              # CreateMainComponent 声明（11行）
│   └── render_utils.hpp        # renderColoredLine 等工具（37行）
│
├── src/                        # 实现文件
│   ├── main.cpp                # 入口：loadGlobal → 构造四层 → screen.Loop（74行）
│   ├── log_reader.cpp          # mmap + IndexThread + FileWatcher（397行）
│   ├── filter_chain.cpp        # FilterNode 链 + ReprocessThread（454行）
│   ├── app_controller.cpp      # 键盘分发 + 状态管理（1565行）
│   ├── render.cpp              # FTXUI 组件树组装（512行）
│   ├── render_utils.cpp        # UTF-8 颜色渲染工具（81行）
│   ├── app_config.cpp          # JSON 配置加载（68行）
│   └── CMakeLists.txt
│
├── tests/
│   ├── helpers/                # 共享测试基础设施
│   │   ├── mock_log_reader.hpp    # MockLogReader（GMock）
│   │   ├── mock_filter_chain.hpp  # MockFilterChain（GMock）
│   │   ├── render_test_base.hpp   # FTXUI Screen 渲染 helper
│   │   ├── temp_file.hpp          # RAII 临时文件
│   │   └── test_utils.hpp         # 通用断言 helper
│   ├── unit/                   # 单元测试（孤立类）
│   │   ├── log_reader_test.cpp
│   │   ├── filter_chain_test.cpp
│   │   ├── app_controller_test.cpp
│   │   ├── render_utils_test.cpp
│   │   └── app_config_test.cpp
│   ├── render/                 # 渲染快照测试
│   │   ├── log_pane_test.cpp
│   │   ├── status_bar_test.cpp
│   │   ├── filter_bar_test.cpp
│   │   ├── input_line_test.cpp
│   │   ├── dialog_overlay_test.cpp
│   │   └── progress_overlay_test.cpp
│   └── e2e/                    # 端到端测试（真实四层）
│       ├── file_open_test.cpp
│       ├── filter_workflow_test.cpp
│       ├── realtime_test.cpp
│       ├── navigation_test.cpp
│       ├── search_test.cpp
│       ├── input_flow_test.cpp
│       ├── session_test.cpp
│       ├── comprehensive_test.cpp
│       ├── selection_test.cpp      # 文本选区 + 拖拽自动滚动（#20/#22）
│       ├── completion_test.cpp     # 文件路径 Tab 补全（#10）
│       └── esc_priority_test.cpp   # ESC 优先级链 + GoTo 边界 + 排除过滤器
│
├── design.md                   # 完整设计文档（功能 + 接口 + 测试 + 配置）
├── implementation.md           # 本文件（实现报告）
├── README.md                   # 用户文档（构建、快捷键、配置）
├── CLAUDE.md                   # Claude 开发规范
├── examples/                   # 示例日志文件（测试用）
├── scripts/                    # 构建/发布脚本
└── CMakeLists.txt              # 根构建文件（含版本号）
```

---

## 3. 类报告

### 3.1 AppConfig

**职责**：全局可调参数的唯一来源，消除代码中散落的魔法数字。

**字段**（见 design.md → 配置系统）：8 个字段，涵盖 UI 布局、文件监控、搜索预分配、JSON 格式。

**设计要点**：
- 全局单例 `s_config`（`app_config.cpp` 文件作用域），通过 `AppConfig::global()` 只读访问
- `loadFromFile(path)` 是实例方法（不碰全局），测试可安全构造本地 `AppConfig` 对象
- 用 `j.value(key, currentValue)` 逐字段覆盖，缺失字段保持默认——**不会因 partial JSON 崩溃**
- 头文件不引入 `nlohmann/json.hpp`（避免传染依赖），JSON 解析全在 `.cpp` 文件内

### 3.2 LogReader

**职责**：mmap 文件访问、后台行索引建立、文件变更侦测、双模式（Static / Realtime）切换。

**关键实现**：

```
open(path)
  └─ mmap 文件 → MmapRegion (shared_ptr)
  └─ 启动 IndexThread → 扫描换行符 → lineOffsets_[]
  └─ 实时模式：启动 FileWatcher 线程
     └─ 每 watcherTickCount × watcherTickIntervalMs 检测文件大小
     └─ 大小变化 → PostEvent → UI线程 remap + handleNewLines()
```

**线程安全**：
- `lineOffsets_` 是 `vector<uint64_t>`，不加锁——通过 `atomic<size_t> lineCount_` 做 release/acquire 序列化：IndexThread 写完一行偏移后 `lineCount_.fetch_add(1, release)`；UI 线程读前先 `lineCount_.load(acquire)`，确保读到完整偏移。
- `getLine(n)` 返回 `string_view`，指向当前 `MmapRegion`；remap 在 UI 线程执行，所以与 `getLine` 调用不会并发。
- SearchThread 持有 `shared_ptr<MmapRegion>` 副本，防止搜索期间 remap 释放旧内存区域（dangling view）。

**mmap 平台差异**：
- POSIX：`mmap()` / `munmap()`
- Windows：`CreateFileMapping()` / `MapViewOfFile()` / `UnmapViewOfFile()`

### 3.3 FilterChain

**职责**：链式正则过滤，阶段缓存，后台 reprocess，JSON 持久化。

**FilterNode 结构**（三合一设计）：
```cpp
struct FilterNode {
    FilterDef             def;       // 用户定义（pattern, color, enabled, exclude, useRegex）
    std::regex            compiled;  // 预编译 regex（仅 useRegex=true 时有效）
    std::vector<uint32_t> output;    // 通过本阶段的1-based原始行号
};
```

**阶段缓存逻辑**：`filters_[k].output` 存放通过前 k+1 个过滤器的行号集合。修改第 k 个过滤器只需从第 k 阶段重新 buildStage，前 k-1 阶段缓存不受影响。

**字符串/正则双模式**（v0.9.2 新增）：
- `FilterDef.useRegex = false`（默认）：字面字符串匹配（`string_view::find()`），无 regex 编译，特殊字符按字面处理
- `FilterDef.useRegex = true`：`std::regex` 匹配，`compiled` 字段有效
- `x` 键调用 `toggleUseRegex(idx)`：翻转模式，切换到 regex 时尝试编译——失败则回退为字符串模式
- `filteredLineCountAt(idx)` 返回 `filters_[idx].output.size()`，供 filter bar 显示每个过滤器的匹配行数

**增量处理**（Push 路径）：
- `processNewLines(first, last)` → `processNewLinesImpl()`：新行依次通过每个 `FilterNode`，append 到 `output` 末尾。
- reprocess 期间收到新行 → 暂存 `pendingNewLines_`，reprocess 完成后由 `DoneCallback` 在 UI 线程一次性补处理。

**cancelFlag_ 取消机制**：
- 新 reprocess 开始时 `cancelFlag_.store(true)`，join 等待旧线程退出，然后 `cancelFlag_.store(false)` 再启动新线程。
- `buildStage()` 每处理一定行数检查 `cancel.load()`，提前返回。
- **v0.9.4 新增** `cancelReprocess()`：对外暴露显式取消接口（`IFilterChain` 纯虚方法）。实现：`cancelFlag_.store(true)` → join → `isReprocessing_=false`。用于过滤超时对话框选"否"时中止重处理。

**持久化**：
- `save(path)` 和 `save(path, lastFile, mode)` 两个重载，共用 `buildFiltersJson()` 内部函数（返回 `nlohmann::json` array）。
- `ensureParentDir()` 自动创建父目录（支持首次运行）。
- `load(path)` 重建 `FilterNode`（重新编译 regex），然后触发全量 reprocess。

### 3.4 AppController

**职责**：双窗格状态管理、键盘事件分发（InputMode 状态机）、搜索、折叠、对话框、进度条。

**状态机**：`InputMode` 枚举控制键盘分发路径：

```
None ──────────────────────────────────── 普通导航/过滤/模式键
  ├─ '/' → Search ─── Enter/Esc → None
  ├─ 'a'/'e' → FilterAdd/FilterEdit ── Enter/Esc → None
  ├─ 'g' → GotoLine ─── Enter/Esc → None
  ├─ 'o' → OpenFile ── Enter/Esc → None
  ├─ 'w' → ExportConfirm ── Enter/Esc → None
  └─ 'd' → (对话框) → handleKeyDialog → Yes/No → None
```

**PaneState**：
```cpp
struct PaneState {
    size_t cursor        = 0;
    size_t scrollOffset  = 0;
    size_t hScrollOffset = 0;   // v0.9.3: horizontal byte offset
};
```
每个窗格（Raw / Filtered）各一份，`clampScroll()` 在 `getViewData()` 时约束垂直范围；hScrollOffset 无上限（超范围时渲染返回空行）。

**getViewData() 职责**：
1. 计算可见行切片（`[scrollOffset, scrollOffset + paneHeight)`）
2. 构建 `LogLine` 列表（含 `rawLineNo`、`content string_view`、`colors`、`searchSpans`、`highlighted`、`folded`）
3. 组装 `ViewData`（过滤标签含 `useRegex`/`matchCount`、输入状态、对话框、进度）
4. 不修改任何业务状态——scroll clamp 是唯一副作用（`mutable` 成员）

**提取的私有方法**（重构后）：
- `clampSelectedFilter()`：替换 4 处重复的 selectedFilter_ 边界检查
- `stepSearch(int dir)`：统一 `n`/`p` 键的搜索跳转逻辑
- `toggleFoldCurrentLine()`：从 `handleModeKeys()` 提取的 17 行 `z` 键逻辑
- `kHelpText`（static constexpr string_view）：从 `h` 键 handler 内联提取
- `computeSearchSpans()`（v0.9.2 新增）：静态辅助函数，扫描行内 keyword 所有出现位置，返回 `vector<SearchSpan>`

**v0.9.2 新增私有字段**：
- `searchKeyword_`（`std::string`）：最后提交的搜索词，供 `buildRawPane`/`buildFilteredPane` 计算 searchSpans
- `searchInFiltered_`（`bool`）：记录搜索是在原始区（false）还是过滤区（true）发起

**焦点感知搜索**（v0.9.2）：
- 焦点在原始区时：`runSearch()` 扫描全量原始行（原有行为）
- 焦点在过滤区时：只扫描 `chain_.filteredLineAt(i)` 返回的行集合
- `jumpToSearchResult()` 同样感知：`searchInFiltered_=true` 时线性扫描过滤列表定位 cursor

**v0.9.3 新增公有方法**：
- `scrollPane(FocusArea area, int delta)`：滚动指定窗格（不改变焦点），由 render.cpp CatchEvent 的鼠标滚轮处理调用
- `setFocus(FocusArea area)`：切换焦点，由 render.cpp CatchEvent 的鼠标单击处理调用

**鼠标支持**（v0.9.3）：
- FTXUI v6 默认启用鼠标跟踪（`track_mouse_ = true`），无需显式开启
- `CatchEvent` lambda 检测 `event.is_mouse()`：按窗格 Y 边界判断悬停位置
  - `WheelUp`/`WheelDown`：调用 `scrollPane` 滚动悬停窗格（±3 行/次），不影响另一窗格
  - `Left + Pressed`：调用 `setFocus` 切换到点击窗格
- 布局行号（0-based）：status(0) + sep(1) + rawPane(2..1+rawH) + sep(2+rawH) + filtPane(3+rawH..2+rawH+filtH)

**v0.9.4 变更**：
- **原始窗格不染色**：`buildRawPane()` 将 `ll.colors` 设为空（不调用 `chain_.computeColors()`）；过滤颜色仅属于过滤窗格，原始窗格的搜索高亮（searchSpans）不受影响
- **行号默认开启**：`showLineNumbers_` 初始值改为 `true`
- **新增公有方法**：
  - `requestQuit(std::function<void()> exitFn)`：弹 Y/N 退出确认框；若另一对话框已开则静默忽略（防嵌套）
  - `isDialogOpen() const`：render.cpp 用于在弹窗期间屏蔽 `q` 键再次触发
- **ViewData 新增搜索字段**：`searchKeyword`（当前关键词）、`searchResultCount`（结果总数）、`searchResultIndex`（1-based 当前位置）；由 `getViewData()` 填充
- **过滤超时**：`triggerReprocess()` 在进度回调中检测已耗时 ≥ 30 秒（`kReprocessTimeoutSeconds`），首次触发时弹 Y/N 确认（Y = 继续等待，N = 调用 `chain_.cancelReprocess()` 中止）；字段 `reprocessStartTime_` 和 `reprocessTimeoutShown_` 追踪超时状态

### 3.5 渲染层

**职责**：纯 FTXUI 渲染，将 `ViewData` 转换为 FTXUI `Element` 树，无业务状态。

**组件层次**（`render.cpp`）：
```
CreateMainComponent(controller, screen)
  └─ CatchEvent → handleKey()
  └─ Renderer → getViewData() → BuildMainView(data)
       ├─ RenderStatusBar(data)
       ├─ RenderRawPane(data)          ← RenderLogLine 逐行
       ├─ separator
       ├─ RenderFilteredPane(data)     ← RenderLogLine 逐行
       ├─ RenderFilterBar(data)
       ├─ RenderInputLine(data)        （InputMode != None 时显示）
       ├─ RenderDialogOverlay(data)    （showDialog 时覆盖）
       └─ RenderProgressOverlay(data)  （showProgress 时覆盖）
```

**renderColoredLine() / render_utils.cpp**：
- 接收 `string_view content` + `vector<ColorSpan>` + `vector<SearchSpan>`，输出 FTXUI `Elements`
- UTF-8 安全：按字节扫描，`0x80`~`0xBF` 是续字节跳过，多字节序列整体处理
- ColorSpan 按字符（不是字节）位置计数，`computeColors()` 在 FilterChain 侧保证
- SearchSpan（v0.9.2 新增）：`{size_t start; size_t end;}` 字节区间，渲染为 **bold + underlined**，与过滤颜色叠加，两类 span 通过边界点合并算法同时处理
- **v0.9.4 重构**：统一截断路径——当 `terminalWidth > 0` 时，先用 `truncateUtf8()` 将 content 裁剪到可用宽度（折叠模式预留 1 列给 "…"），同步裁剪两类 span；折叠 early-return 分支删除，改为在段落构建完成后追加 `text("…") | dim`。修复了折叠行颜色全丢和窄窗口颜色截断两类问题

**v0.9.4 渲染层变更**（render.cpp）：
- `renderFilterBar`：每个过滤器标签后追加颜色指示圆点：`● ` + 过滤器颜色（启用），或 `○ ` + dim（禁用）
- `renderInputLine` None 分支：当 `data.searchKeyword` 非空时，底部显示 `/keyword  (N/M)  n/N:跳转  Esc:清除`
- `CatchEvent` q 键：改为调用 `controller.requestQuit(screen.ExitLoopClosure())`，并增加 `!controller.isDialogOpen()` 保护

**v0.9.5 变更**：
- `renderLogPane` 行号右对齐：从 `data.totalLines` 计算 `maxLineNoW`（总行数决定列宽），用 `std::format("{:>{}} ", rawLineNo, maxLineNoW)` 右对齐，内容列全文件一致
- `renderFilterBar` 禁用圆点：`○ ` 改为 `color(parseHexColor(tag.color))`（移除 dim），与启用圆点统一用过滤器颜色
- `onTerminalResize` 窗格比例：`available/2` → `available*6/10`（原始窗格 60%，过滤窗格 40%）
- `handleKey` ESC 取消：在 None 模式检查 `showProgress_ && inputMode_==None`，立即 `cancelReprocess()`（不等 30s 超时）
- `main.cpp`：删除启动时自动打开 `sessionLastFile` 的逻辑（过滤器仍持久化，只不再自动打开文件）
- `cmake/version.rc.in`：新增 Windows VERSIONINFO 资源模板，CMake configure_file 生成，`src/CMakeLists.txt` WIN32 条件引入，使"打开方式"显示正确的 ProductName

**v0.9.6 变更**：
- `render_utils.cpp` `renderColoredLine`：搜索匹配段由 `bold | underlined` 改为 `inverted`（反色更醒目）
- `render.cpp` `renderLogPane`：新增 `bool searchActive` 参数；激活搜索时 `▶` 标记和整行反色均被抑制，避免光标随搜索跳动
- `render.cpp` `renderFilterBar`：圆点从 `●`（U+25CF）改为 `⬤`（U+2B24 BLACK LARGE CIRCLE）；标签末尾去掉空格使圆点紧贴匹配数；label+dot 合为一个 `hbox` 元素再统一应用 `inverted`/`dim`，选中时整体高亮
- `render.cpp` `renderInputLine` Search 分支：显示 `[正则]`/`[字符串]` 模式指示和 `Tab:切换` 提示（取代原 `inputPrompt`）
- `render.cpp` 鼠标滚轮：`dir * 3` → `dir * 1`，每次滚动 1 行
- `app_controller.cpp` `computeSearchSpans`：增加 `bool useRegex, const std::optional<std::regex>& regex` 参数，支持正则匹配路径（`std::sregex_iterator`）
- `app_controller.cpp` `runSearch`：若 `searchUseRegex_` 为真则编译 regex（失败时回退为字面匹配），用闭包 `lineMatches` 统一两种扫描路径
- `app_controller.cpp` `handleKeySearch`：拦截 Tab → 切换 `searchUseRegex_`（不传递给 `handleCommonInputKeys`）
- `app_controller.cpp` `handleKey`：ESC in None 模式且 `!searchKeyword_.empty()` → 清除搜索状态
- `app_controller.cpp` `handleNavKeys` Tab：切换前先清除搜索状态（keyword / results / regex）
- `app_controller.cpp` `buildRawPane`/`buildFilteredPane`：移除基于 searchLine 的 `highlighted` 附加条件；追踪 `maxContentLen`，`getViewData` 后 clamp `hScrollOffset` 到 `maxContentLen - 1`（0 行时跳过）
- `app_controller.hpp`：`PaneState` 和 `ViewData` 新增 `searchActive`、`searchUseRegex` 字段；AppController 私有新增 `searchUseRegex_` 和 `std::optional<std::regex> searchRegex_`

---

## 4. 线程模型

### 4.1 五条线程

| 线程 | 职责 | 生命周期 |
|------|------|----------|
| **UI 主线程** | FTXUI 事件循环、渲染、所有业务方法 | 全程 |
| **IndexThread** | 扫描文件换行符，填充 `lineOffsets_[]` | open() 启动，索引完成退出 |
| **FileWatcher** | 轮询文件大小变化 | 实时模式开启时运行 |
| **ReprocessThread** | 重建所有 FilterNode 的 output 缓存 | reprocess() 触发，完成退出 |
| **SearchThread** | 全文扫描关键字，收集匹配行 | '/' 搜索触发，完成退出 |

### 4.2 后台→UI 通信规则

**所有**后台线程通过 `postFn_(lambda)` 将结果投递回 UI 线程，lambda 在下一个 FTXUI 事件循环帧执行。唯一例外：`lineCount_` 是 `atomic<size_t>`，允许 UI 线程直接读取（但 `lineOffsets_` 的实际数据仍由 `lineCount_` release/acquire 序列化保护）。

```
IndexThread          FileWatcher        ReprocessThread     SearchThread
     │                   │                    │                   │
     │ lineCount_.fetch_add(1, release)        │                   │
     │ postFn_(onProgress/onDone)              │                   │
     │                   │ postFn_(handleNewLines/handleFileReset)  │
     │                   │                    │ postFn_(onProgress/onDone)
     │                   │                    │                   │ postFn_(onSearchDone)
     └───────────────────┴────────────────────┴───────────────────┘
                                UI 线程（所有 lambda 在此执行）
```

### 4.3 关键竞争点及解决

| 竞争点 | 解决方案 |
|--------|----------|
| `lineOffsets_` 读写 | `atomic<size_t> lineCount_` release/acquire 序列化 |
| reprocess 期间收到新行 | `pendingNewLines_` 缓存，done 回调后补处理 |
| 多次连续 reprocess | `cancelFlag_` + join 保证串行（旧线程先退出） |
| mmap remap vs SearchThread | SearchThread 持有 `shared_ptr<MmapRegion>` 防 dangling |
| forceCheck vs FileWatcher | `checkMutex_` 互斥 |

---

## 5. 关键设计决策

### 5.1 mmap + append-only 行索引

**选择原因**：
- mmap 让 OS 负责缓存和分页，对大文件几乎零拷贝
- `getLine()` 返回 `string_view`，指向 mmap 内存，避免字符串复制
- 行索引只追加（`lineOffsets_[]`），新行到来时增量扩展，不需要锁

**代价**：文件必须整体 mmap，极大文件（> 可用虚地址空间）理论上受限，但实践中 16EB 的 `uint64_t` 偏移足够。

### 5.2 FilterNode 三合一

将 `FilterDef`（用户定义）、`std::regex`（预编译）、`output`（阶段缓存）绑定为同一对象，避免三个独立 vector 之间的索引同步错误，也避免每次过滤都重新编译 regex。

### 5.3 PostFn 机制

后台线程持有 `PostFn`（`std::function<void(std::function<void()>)>`），测试中替换为同步版本（直接调用 lambda），使异步逻辑可以在单线程测试中确定性验证，无需 `sleep` 或复杂同步原语。

**重要：FTXUI 帧失效机制**

FTXUI 的 `HandleTask` 对三种任务类型的帧失效行为不同：

| 类型 | `frame_valid_ = false`？ | 触发 `Draw()`？ |
|------|--------------------------|----------------|
| **Event**（键盘/鼠标/Custom） | ✅ 总是 | ✅ |
| **Closure**（`screen.Post(lambda)`） | ❌ 不设置 | ❌ |
| **AnimationTask** | 仅当 `animation_requested_` | 条件触发 |

`Draw()` 在 `frame_valid_ == true` 时直接跳过。因此，**后台线程通过 `postFn_` 投递的 Closure 不会自动触发重绘**，UI 会停留在旧状态直到下一个用户输入事件。

**修复方案**（`main.cpp`）：在每次 `screen.Post(fn)` 之后立即调用 `screen.PostEvent(Event::Custom)`，将一个 Event 类型任务投入队列。Event 总是设置 `frame_valid_ = false`，从而确保 `Draw()` 在任务完成后执行。`Event::Custom`（= `Event::Special({0})`）是 FTXUI 官方指定的用于此目的的事件，CatchEvent 处理链不会误响应它（`handleKey` 不匹配，返回 false）。

### 5.4 getViewData() 纯快照

`getViewData()` 的合约是"构建快照"而非"修改状态"。滚动 clamp（`mutable` 成员）是唯一例外，且只为保证渲染的合法性，不影响业务逻辑正确性。这使渲染层完全是纯函数式的：同样的 `ViewData` 必然渲染出同样的画面。

### 5.5 接口隔离（ILogReader / IFilterChain）

`AppController` 只依赖抽象接口，使单元测试可以注入 GMock 对象，精确控制返回值并验证调用序列，不依赖真实文件 I/O 或 regex 引擎。

---

## 6. 二次开发指南

### 6.1 添加新快捷键

1. 确认触发时机（`InputMode::None` 还是某个输入模式）
2. 在对应的 `handleKeyXxx()` 方法中添加 `if (event == Event::Character('x')) { ... return true; }`
3. 更新 `kHelpText`（`app_controller.cpp` 顶部 static constexpr）
4. 添加 E2E 测试（`handleKey()` + `getViewData()` 断言）

### 6.2 添加新过滤类型

当前过滤器仅支持 `include`（保留匹配行）和 `exclude`（剔除匹配行）。若要添加新类型（例如"高亮但不过滤"）：

1. 在 `FilterDef`（`i_filter_chain.hpp`）中扩展枚举或字段
2. 在 `FilterChain::buildStage()` 中添加对应分支
3. 在 `FilterChain::computeColors()` 中添加颜色规则
4. 在 `render.cpp` 的过滤标签渲染处显示新类型标识
5. 更新 `FilterChain::save()`/`load()` 的 JSON 序列化

### 6.3 添加新 UI 区域

以"书签栏"为例：

1. 在 `ViewData` 中添加 `struct BookmarkItem` 和 `vector<BookmarkItem> bookmarks`
2. 在 `AppController::getViewData()` 的 `buildRawPane()` 或新增 `buildBookmarkBar()` 中填充
3. 在 `render.cpp` 中添加 `RenderBookmarkBar(data)` 并插入组件树
4. 在 `AppController` 中添加 `unordered_set<size_t> bookmarkedLines_` 和快捷键处理

### 6.4 添加新配置项

1. 在 `AppConfig`（`include/app_config.hpp`）结构体中添加字段和默认值
2. 在 `AppConfig::loadFromFile()`（`src/app_config.cpp`）中添加 `j.value("fieldName", currentValue)` 加载行
3. 在使用处改用 `AppConfig::global().newField`
4. 在 `design.md` → 配置系统章节的字段表中追加记录
5. 更新 `README.md` 的配置示例 JSON

### 6.5 扩展 Session JSON

`FilterChain::save()` 写出的 JSON 结构：

```json
{
  "filters": [
    { "pattern": "ERROR", "color": "red", "enabled": true, "exclude": false, "useRegex": false }
  ],
  "lastFile": "/path/to/log",
  "mode": "realtime"
}
```

添加新 session 字段：
1. 在 `FilterChain::save(path, lastFile, mode)` 中添加 `j["newKey"] = value`
2. 在 `FilterChain::load()` 中添加 `j.value("newKey", default)` 读取
3. 如需由 `AppController` 存取，在 `sessionLastFile()` / `sessionMode()` 旁边添加 accessor

---

## 7. 已知限制与未来方向

### 当前限制

| 限制 | 说明 |
|------|------|
| 仅支持 UTF-8 | 其他编码（GBK、UTF-16）未经测试，行为未定义 |
| 单文件查看 | 不支持多标签页或并排比较 |
| 无跨行匹配 | 正则过滤逐行匹配，不支持跨行模式 |
| 行数限制 | `uint32_t output` 限制单文件不超过约 42 亿行（实践中不构成限制） |
| 行索引内存 | 每行 8 字节偏移，1 千万行约 80MB 索引内存 |

### 未来方向

- **双区联动**：过滤结果区选中行时，原始区自动跳转对应行
- **命名捕获组高亮**：正则中 `(?P<level>ERROR|WARN)` 自动映射颜色
- **懒求值过滤**：按需计算，适应超大文件
- **多文件标签**：`AppController` 改为持有多个 `(LogReader, FilterChain)` 对

---

## 8. 测试策略速查

| 层级 | 框架 | 隔离方式 | 典型断言 |
|------|------|----------|----------|
| 单元（unit/） | GTest | GMock 注入接口 | 方法调用次数、返回值 |
| 渲染（render/） | GTest + FTXUI Screen | `RenderTestBase` 渲染到内存 buffer | buffer 中出现特定字符串 |
| 端到端（e2e/） | GTest | 真实四层 + `TempFile` | `getViewData()` 字段精确匹配 |
| 异步 | promise/future | `postFn` 替换为同步调用 | `waitReprocess()` 后断言 |

运行全部测试：
```bash
/c/msys64/mingw64/bin/ninja.exe -C build && build/bin/ttLogViewer_tests.exe
```

---

## 9. Post-v0.9.8 修复记录（待合入 v0.9.9）

### 9.1 进度弹窗不自动消失（根本原因定位与修复）

**问题**：添加过滤器后"过滤处理中"弹窗停留在 0%，必须按键才消失。此 bug 两次尝试修复未果（均基于错误假设：Windows ReadConsoleInputW 阻塞了事件循环）。

**根本原因**：FTXUI `HandleTask`（`screen_interactive.cpp:779`）处理 **Closure** 类型任务时不设置 `frame_valid_ = false`（仅 Event 类型会设置），而 `Draw()` 在 `frame_valid_ == true` 时直接返回不渲染。因此 `screen.Post(lambda)` 投递的回调执行后，UI 不重绘——直到下一个真实用户按键事件到来。

**修复**（`main.cpp`）：`postFn` 在每次 `screen.Post(fn)` 后立即调用 `screen.PostEvent(Event::Custom)` 注入一个 Event 类型任务，强制帧失效和 `Draw()` 执行。同时删除了 `app_controller.cpp` 中已无效的 `postFn_([] {})` hack。

### 9.2 ESC 优先级链（重要）

`handleKey` 在 None 模式下对 ESC 的处理是 **有序的短路链**：

```
ESC
 ├─1. showProgress_ && None mode → cancelReprocess()（中断过滤，最高优先）
 ├─2. selection_.active && None mode → clearSelection()（清除文本选区）
 └─3. !searchKeyword_.empty() && None mode → clearSearch()（清除搜索）
```

**优先级规则**：
- 进度取消 > 清除选区 > 清除搜索
- 每次 ESC 只触发一个动作，用户需要多次按 ESC 才能逐层退出
- 在输入模式（FilterAdd/FilterEdit/Search/GotoLine/OpenFile/ExportConfirm）下，ESC 由 `handleCommonInputKeys` 处理，直接退出输入模式，不经过上述链

### 9.3 跳转居中

**原行为**：`jumpToRawLine`（'x' 键、'g' goto、n/p 搜索跳转）使用 `clampScroll` 最小滚动——目标行在视口边缘显示。
**新行为**：跳转目标行尽量居中显示（`scrollOffset = cursor - paneHeight/2`），再经 `clampScroll` 修正边界。同样适用于过滤窗格的搜索结果跳转（`jumpToSearchResult`）。

### 9.4 过滤器圆点背景一致

**问题**：选中过滤器行时，label 反色高亮，dot 背景仍为默认色，视觉上不协调。
**修复**（双重反色技巧）：先对 dot 预施加 `| inverted`，再对整行施加 `| inverted`。两次反色相消——dot 保留原有前景色，同时与 label 共享相同的高亮背景。

---

## 10. 字符级文本选择与剪贴板（v0.9.9）

### 10.1 设计决策

**为什么不用终端原生选文？**
终端鼠标协议是全有全无的。启用 ANSI 鼠标跟踪（`\033[?1003h`）后，终端将所有鼠标事件路由给应用；关闭后，终端自行处理鼠标（原生选文），但应用收不到任何鼠标事件（包括滚轮）。由于滚轮更常用，保持 FTXUI 鼠标跟踪常开，在 TUI 层自己实现字符级选区。

**已删除 `m` 键**：原先 `m` 键用于在"鼠标跟踪/原生选文"之间切换，现已删除。鼠标跟踪始终开启，选区由拖拽实现，ESC 清除。

### 10.2 架构

```
render.cpp CatchEvent
    Left+Pressed  → controller.startSelection(pane, row, byte)
    Left+Moved    → controller.extendSelection(row, byte)
    Left+Released → hasSelection ? finalizeSelection() : clickLine()
    Ctrl+C        → controller.copySelectionToClipboard()

app_controller.cpp
    SelectionState { anchor, current, active, dragging }
    buildRawPane/buildFilteredPane → 填充 ll.selectionSpans
    ESC (优先级2) → clearSelection()
    handleNewLines / handleFileReset / triggerReprocess.onDone / setFocus → clearSelection()

render_utils.cpp renderColoredLine
    SelectionSpan 渲染优先级最高（bgcolor Blue + White fg）

clipboard.hpp / clipboard.cpp
    Windows: CF_UNICODETEXT（UTF-8 ↔ UTF-16 via Win32 API）
    Linux/macOS: 存根，记录为未来功能
```

### 10.3 坐标转换：屏幕列 → 字节偏移

`screenColToByteOffset(FocusArea, lineIndex, screenCol)` 的转换链：

```
m.x（终端绝对列）
  → 减去前缀宽度（行号位数+1 + 2 for "▶ "/"  "）= contentCol
  → displayColToByteOffset(content, hScroll, contentCol) = 绝对字节偏移
```

关键点：
- `displayColToByteOffset` 返回 `content` 内的**绝对**字节偏移（不需要再加 hScroll）
- CJK 字符每个占 2 个显示列；ASCII 每个占 1 个
- 点击前缀区域（行号/箭头）返回 0（选区从行首开始）

### 10.4 SelectionState 生命周期

```
startSelection  → active=false, dragging=true, anchor=current=点击位置
extendSelection → active=(anchor≠current), current=鼠标位置
finalizeSelection → dragging=false, active 保持（用户手动 ESC 或单击清除）
clearSelection  → active=false, dragging=false
```

**自动清除时机**（内容变化时选区失效）：
- `handleNewLines`：实时模式追加新行
- `handleFileReset`：文件被截断/替换
- `triggerReprocess.onDone`：过滤器变更后重新计算
- `setFocus`：切换到不同窗格（anchor.pane 不同）
- `clickLine`：普通单击（无拖拽时 release 触发）

### 10.5 渲染优先级（span 覆盖规则）

`renderColoredLine` 用边界点算法渲染各种 span，优先级从高到低：

| 优先级 | 类型 | 来源 | 样式 |
|--------|------|------|------|
| 1（最高）| SelectionSpan | 鼠标拖拽选区 | `bgcolor(Blue)` + `color(White)` |
| 2 | SearchSpan | 搜索关键词当前结果 | `inverted` |
| 3 | ColorSpan | 过滤器颜色标记 | `color(parseHexColor(...))` |
| 4 | 无 | 未命中 | 终端默认样式 |

当 SelectionSpan 和其他 span 重叠时，选区高亮覆盖所有下层样式。

### 10.6 `buildSelectedText` 多行文本组装

- 按 `SelectionState::ordered()` 确定有序 (start, end) 区间
- 逐行从 reader_（或 filteredLineAt）取原始内容，不含行号、折叠省略号
- 第一行：`[anchor.byteOffset, content.size())`；中间行：完整行；最后行：`[0, current.byteOffset)`
- 各行用 `\n` 连接（不含行尾换行）

### 10.7 Ctrl+V 粘贴（输入模式）

`handleCommonInputKeys` 中拦截 `Event::CtrlV`，调用 `clipboardPaste`：
- `Ok` → 追加到 `inputBuffer_`，重新校验正则
- `MultiLine` → 弹出错误对话框"剪贴板内容含多行，无法粘贴"
- `NotText` → 弹出错误对话框"剪贴板内容不是文本"
- `Empty` / `Error` → 静默忽略

### 10.8 未来功能（已记录）

- Linux/macOS 剪贴板集成（xclip/xsel/wl-clipboard/pbcopy）
- Shift+点击扩展选区（已明确不做）

---

## 11. SelectionPoint 绝对行号重构 + 拖拽自动滚动（v0.9.9，Issue #22）

### 11.1 根本 Bug：lineIndex 语义歧义

**原有语义**：`SelectionPoint.lineIndex` 存储的是窗格可见区内的相对行号（0 = scrollOffset 处的行）。
这导致两个隐患：
1. **选区高亮漂移**：键盘滚动改变 `scrollOffset` 后，相对行号对应的绝对行变了，高亮出现在错误位置。
2. **自动滚动无法实现**：自动滚动会改变 `scrollOffset`，导致 lineIndex 指向的绝对行立即错位。

**修复**：`lineIndex` 语义改为**绝对行号**（0-based，与 `reader_.getLine(lineIndex+1)` 直接对应）。

### 11.2 同步修改的位置

| 位置 | 旧逻辑 | 新逻辑 |
|------|--------|--------|
| `CatchEvent Left+Pressed` | `startSelection(pane, row, byte)` where `row = m.y - paneTop` | `absIdx = scrollOff + row`; `startSelection(pane, absIdx, byte)` |
| `CatchEvent Left+Moved` | `extendSelection(clamp(row), byte)` | 自动滚动后 `extendSelection(newScrollOff + rowInPane, byte)` |
| `screenColToByteOffset` | `content = reader_.getLine(scrollOffset + lineIndex + 1)` | `content = reader_.getLine(lineIndex + 1)` (lineIndex is absolute) |
| `buildSelectedText` | `absIdx = scrollOff + li` | `absIdx = li` 直接用 |
| `selSpans` lambda | `sliceIdx == startPt.lineIndex` | `absIdx = first + sliceIdx`; 与 anchor/current 比较 |

### 11.3 新增 AppController 公有方法

```cpp
size_t paneScrollOffset(FocusArea area) const;   // 当前垂直滚动偏移
int    terminalWidth() const;                     // 最后已知终端宽度（inline）
int    prefixColWidth() const;                    // 行号+箭头前缀宽度
void   scrollHorizontal(FocusArea area, int deltaBytes);  // 横向滚动
```

`prefixColWidth()` 与 `screenColToByteOffset` 使用相同的计算逻辑（行号宽度 + "▶ "），确保点击精度。

### 11.4 自动滚动逻辑（render.cpp CatchEvent Left+Moved）

```
// 垂直自动滚动
if (m.y < paneTop)              → scrollPane(pane, -1); rowInPane = 0
else if (m.y >= paneTop + paneH) → scrollPane(pane, +1); rowInPane = paneH-1
else                             → rowInPane = m.y - paneTop

// 水平自动滚动
if (m.x < prefixColWidth())      → scrollHorizontal(pane, -hScrollStep)
else if (m.x >= terminalWidth()) → scrollHorizontal(pane, +hScrollStep)

// 延伸选区（使用滚动后的绝对索引）
newScrollOff = paneScrollOffset(pane)
absIdx = newScrollOff + rowInPane
extendSelection(absIdx, screenColToByteOffset(pane, absIdx, m.x))
```

`hScrollStep` 来自 `AppConfig::global().hScrollStep`（默认 4 字节），与键盘水平滚动步长一致。

### 11.5 测试覆盖

新增 8 个测试（`tests/e2e/selection_test.cpp`）：

| 测试 | 验证内容 |
|------|---------|
| `AbsoluteLineIndexWorksWithScrollOffset` | scrollOffset=2 时，绝对行 2,3 正确高亮到 viewport[0,1] |
| `SelectionDoesNotDriftOnScroll` | 选中行 0 后滚动到 scrollOffset=2，viewport 无选区残留 |
| `PaneScrollOffsetReflectsState` | scrollPane 后 paneScrollOffset 返回正确值 |
| `ScrollHorizontalIncreasesOffset` | scrollHorizontal 正确增加 hScrollOffset |
| `ScrollHorizontalClampsAtZero` | scrollHorizontal 不会使 hScrollOffset 变为负值 |
| `ScreenColToByteOffsetAbsoluteIndex` | 绝对行 0 处列转字节正常 |
| `ScreenColToByteOffsetScrolledPane` | 绝对行 2 处列转字节正常 |
| `ScreenColToByteOffsetOutOfBoundsReturnsZero` | 越界行返回 0，不崩溃 |

**测试技巧**：fixture 有 5 行，默认 paneHeight=5 时 clampScroll 不改变 scrollOffset。
需先调用 `ctrl_.getViewData(3, 3)` 将 paneHeight 设为 3，再 `scrollPane(+4)` 才能得到 scrollOffset=2。

---

## 12. 文件路径 Tab 补全（v0.9.9，Issue #10）

### 12.1 功能描述

按 `o` 进入 `InputMode::OpenFile` 后，按 `Tab` 键触发路径补全：
- **0 个匹配**：无动作
- **1 个匹配**：直接填入输入框；若选中的是目录，自动追加 `/` 并再次触发补全
- **N>1 个匹配**：在输入行上方弹出最多 3 条候选列表，超出时显示 `▲` 上翻指示

**弹窗键盘操作**：
- `Tab` / `↓`：下移高亮（循环）
- `↑`：上移高亮（循环）
- `Enter`：确认当前项，填入输入框，关闭弹窗（不提交文件，可继续编辑）
- `Esc`：关闭弹窗，保留当前输入
- 任意字符输入：先关闭弹窗，再追加字符

### 12.2 路径解析

`splitPathForCompletion(input)` 将输入分成目录前缀和文件名前缀：
```
"C:/Users/foo/bar" → {"C:/Users/foo/", "bar"}
"C:/Users/"        → {"C:/Users/",     ""}
"log"              → {"",              "log"}
```

`getFileCompletions(dir, prefix)` 用 `std::filesystem::directory_iterator` 列举目录内容：
- 目录项名称末尾自动追加 `/`
- 结果按字母排序
- 异常（无权限、路径不存在）静默忽略
- 中文路径通过 `u8string()` 转换（C++20 返回 `std::u8string`，需 `string(u8.begin(), u8.end())` 转换）

### 12.3 弹窗对齐

`completionCol`（ViewData 字段）= `len(inputPrompt_) + displayColWidth(inputBuffer 的目录前缀部分)`

渲染时：
- 边框左侧 (`│>`) 在 `completionCol - 2` 列
- 候选文件名文本在 `completionCol` 列开始，与输入框中正在补全的前缀字母对齐

示例（输入 `Open: /logs/ap_`，`a` 在列 12）：
```
          ┌─────────────┐
          │> api.log    │
          │  app.log    │
          │  apr.log  ▲ │
          └─────────────┘
Open: /logs/ap_
```
（`│>` 在列 10，`a` 在列 12）

### 12.4 paneHeight 联动

`recomputePaneHeights()` 中，当 `showCompletions_` 为 true 时，从可用高度中减去弹窗行数（最多 3 行内容 + 2 行边框 = 5 行），防止弹窗遮挡日志内容。

### 12.5 测试覆盖

新增 13 个测试（`tests/e2e/completion_test.cpp`）：

| 测试 | 验证内容 |
|------|---------|
| `NoMatchDoesNothing` | 无匹配时不显示弹窗 |
| `SingleMatchFillsBufferDirectly` | 单一匹配直接填入缓冲区，不弹窗 |
| `MultipleMatchesShowPopup` | 多个匹配弹出补全窗口 |
| `TabCyclesThroughCompletions` | Tab 依次循环候选项 |
| `ArrowDownNavigatesPopup` | `↓` 下移高亮 |
| `ArrowUpWrapsAround` | `↑` 从首项跳到末项 |
| `EnterAcceptsSelectedCompletion` | Enter 确认并关闭弹窗 |
| `EscapeClosesPopupPreservesBuffer` | Esc 关闭弹窗，输入缓冲不变 |
| `CharacterInputClosesPopup` | 字符输入关闭弹窗并追加字符 |
| `SelectingDirectoryTriggersNextLevel` | 选目录自动触发下一级补全 |
| `ExitInputModeClearsCompletions` | 退出输入模式后补全状态清除 |
| `EmptyPrefixListsAllFiles` | 无前缀 Tab 列举目录全部文件 |
| `CompletionIndexWrapsCorrectly` | 补全索引正确循环 |

---

## 13. Windows Ctrl+C 处理（v0.9.10–v0.9.13 教训总结）

### 13.1 问题描述

在 MSYS2 MinGW + mintty 环境下，用户按 Ctrl+C 时程序直接退出，而期望行为是：有文本选区时复制到剪贴板，无选区时不做任何操作（绝不退出）。

### 13.2 失败修复史（每次修复失败的根因）

**第一次（v0.9.10）**：添加 `SetConsoleCtrlHandler(ctrlHandler, TRUE)` + `ForceHandleCtrlC(false)`

- 失败原因：`SetConsoleCtrlHandler` 拦截了 Win32 Console 的 `CTRL_C_EVENT` 处理链，但 MSYS2 MinGW CRT 在更早阶段已经通过独立机制 `raise(SIGINT)` — SIGINT 根本没经过我们的 handler。`ForceHandleCtrlC(false)` 只影响键盘事件（`Event::CtrlC`）路径，对信号路径毫无作用。

**第二次（v0.9.11）**：在 `screen.Post()` 里追加 `std::signal(SIGINT, SIG_IGN)`

- 失败原因：**`screen.Post()` 在 `screen.Loop()` 之前调用是静默 no-op。** FTXUI 源码（`screen_interactive.cpp`）：
  ```cpp
  void ScreenInteractive::Post(Task task) {
      if (!task_sender_) {   // ← task_sender_ 在 Install() 前为 null
          return;            // ← 静默丢弃！
      }
      task_sender_->Send(std::move(task));
  }
  ```
  `task_sender_` 在 `Install()` 内部才初始化，而 `Install()` 在 `Loop()` 开始时调用。因此在 `Loop()` 前的所有 `Post()` 调用全部被无声丢弃，`SIG_IGN` 从未生效。

**第三次（v0.9.12）**：添加 `SetConsoleCtrlHandler(NULL, TRUE)` + 通过 `screen.Post()` 调用 `disableProcessedInput()`

- 部分成功：`SetConsoleCtrlHandler(NULL, TRUE)` 确实阻止了退出（进程级忽略 Ctrl+C）。
- 但 `disableProcessedInput()` 依然通过 `screen.Post()` 调用，仍然被丢弃。
- 根本问题：`NULL + TRUE` 太激进——它完全吞掉了 Ctrl+C，没有任何键盘事件（`Event::CtrlC`）产生，CatchEvent 无法触发复制。

### 13.3 根本修复（v0.9.13）

**核心：在 `screen.Loop()` 之前直接调用 `disableProcessedInput()`，不通过 `screen.Post()`。**

```cpp
// main() 中，screen.Loop() 之前：
SetConsoleCtrlHandler(ctrlHandler, TRUE);   // 备用拦截层
disableProcessedInput();                    // 在 Install() 之前禁用 ENABLE_PROCESSED_INPUT
// ...
screen.Loop(component);
```

**完整信号/事件链**（修复后）：

```
Ctrl+C 按下
  ↓
Console 检查 ENABLE_PROCESSED_INPUT（已被我们清除为 0）
  ↓
不生成 CTRL_C_EVENT，不 raise(SIGINT)
  ↓
生成普通 KEY_EVENT，UnicodeChar = 0x03
  ↓
FTXUI EventListener 线程（ReadConsoleInput）
  ↓
TerminalInputParser.Add('\x03') → Event::CtrlC
  ↓
RunOnceBlocking → HandleTask → OnEvent(Event::CtrlC)
  ↓
CatchEvent 返回 true → 复制到剪贴板（或无操作）
  ↓
ForceHandleCtrlC(false) + handled=true → RecordSignal 不被调用 → 不退出
```

**为何 FTXUI Install() 不会覆盖我们的设置**：FTXUI 的 `Install()` 对 console 输入模式只修改 `echo_input`/`line_input`/`virtual_terminal_input`/`window_input` 几个 bit，**从不设置 bit 0x0001（ENABLE_PROCESSED_INPUT）**，因此我们在 `Install()` 之前清除的 bit 在 Install 后仍保持清除状态。

### 13.4 关键教训

| 教训 | 具体内容 |
|------|---------|
| **`screen.Post()` 在 `Loop()` 前是 no-op** | `task_sender_` 在 `Install()` 前为 null，所有 Post 静默丢弃。初始化代码必须直接调用，不能 Post |
| **信号路径与键盘事件路径完全独立** | `ForceHandleCtrlC`/`CatchEvent` 只处理 `Event::CtrlC`（键盘事件）；SIGINT 走独立的 `RecordSignal → g_signal_exit_count → Exit()` 路径，两者互不干扰 |
| **`SetConsoleCtrlHandler(NULL, TRUE)` 太激进** | 进程级忽略 Ctrl+C，连键盘事件也不产生，不能用于"拦截并自定义 Ctrl+C 行为"的场景 |
| **ENABLE_PROCESSED_INPUT 是关键开关** | 此 bit 决定 Ctrl+C 是走"信号路径"（CTRL_C_EVENT → SIGINT）还是"键盘路径"（KEY_EVENT 0x03 → Event::CtrlC）。FTXUI 在 Windows 上不清除它，必须手动处理 |
| **诊断日志必须覆盖"修复代码是否执行"** | 每次修复都要在修复代码本身里加日志，而不只在出口处加日志。这次教训：v0.9.11 的日志只有 `[EXIT]` 没有 `[INIT] SIGINT overridden`，说明修复代码根本没跑，而不是修复失效 |

### 13.5 可测试性说明

ENABLE_PROCESSED_INPUT 的设置是 OS 级别行为，无法在 gtest 中模拟。已有的 `MiscKeysTest.CtrlCWithSelectionDoesNotExit` 和 `MiscKeysTest.CtrlCInNoneModeDoesNotChangeState` 覆盖了 `Event::CtrlC` 的键盘事件路径（CatchEvent 行为），这是单元测试能达到的最深层。OS 级别的 Ctrl+C 行为需要手动集成测试验证。

---

## 14. 控制字符渲染修复（v0.9.13）

### 14.1 问题描述

打开含二进制内容的日志文件（如 LevelDB 日志），向右滚动后第一行开头和第二行结尾出现画面错乱——内容互相覆盖。

### 14.2 根本原因

FTXUI 的 `Screen::ToString()` 直接输出 `pixel.character`，**不对控制字符做任何转义**：

```cpp
// FTXUI screen.cpp
ss << pixel.character;  // 直接输出，\r 会被终端解释为 CR
```

当日志行内含 `\r`（0x0D）时，FTXUI 将其放入 Screen 像素格，输出时终端收到裸 `\r`，将光标移至列 0，后续字符覆盖当前行开头，导致视觉混乱。

### 14.3 修复

在 `renderColoredLine`（`render_utils.cpp`）中，所有 `text(segment)` 调用之前先过 `sanitizeControlChars()`：

```cpp
static std::string sanitizeControlChars(std::string_view s) {
    std::string out(s);
    for (auto& c : out) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F)
            c = '.';   // 1 byte → 1 byte，span 字节偏移不变
    }
    return out;
}
```

选用 `'.'` 替换原则：
- 1 byte → 1 byte：ColorSpan/SearchSpan/SelectionSpan 的字节偏移完全不受影响
- 显示宽度 1 列：不改变行的可视宽度，hScroll 对齐不受影响
- 0x80–0xFF 保留：合法 UTF-8 续字节和首字节不被误替换

### 14.4 测试覆盖

见 `tests/render/control_char_render_test.cpp`：验证含 `\r`、`\0`、`\x01` 的行渲染后在对应位置显示 `'.'`，不出现原始控制字符。

