# ttLogViewer 设计文档

## 基本约定

- **目标平台**：Linux、macOS、Windows（Git Bash）
- **C++ 标准**：允许使用最新标准（C++23），以获得高效简洁的实现
- **文件编码**：只支持 UTF-8 编码的日志文件
- **TUI 框架**：FTXUI v6.1.9+（现代化、跨平台、成熟生态）

---

## 架构总览

采用四层架构，存在两种信息流动方向：

### Pull 流（显示驱动，自上而下）

用户导航/滚动时，由显示层主动向下请求数据：

```
用户操作（滚动/跳转）
   │
   ▼
[状态管理层] → 请求第 N~M 行数据（原始区/过滤区分别处理）
   │
   ▼
原始区路径：[读取与缓存层] → 按行号取行内容 → [渲染层]
过滤区路径：[链路过滤层] → 从缓存取行号列表 → [读取与缓存层] → 按行号取行内容 → [状态管理层] → 动态计算颜色标记 → [渲染层]
```

### Push 流（追加驱动，自下而上）

文件有新内容追加时，由读取层主动向上通知：

```
文件追加新内容
   │
   ▼
[读取与缓存层] → 通知新增行范围
   │
   ▼
[链路过滤层] → 对新增行过滤，追加到各缓存末尾
   │
   ▼
[状态管理层] → 收到更新通知，判断是否需要刷新视图
   │
   ▼
[渲染层] → 更新显示（如锁定跟随末尾则自动滚动）
```

### 整体结构

```
日志文件
   │
   ▼
[读取与缓存层] ──── 文件变更检测 ──→ 刷新弹窗
   ↕ (Push/Pull 双向)
[链路过滤层]
   ├── [过滤器1] ←→ 缓存1
   ├── [过滤器2] ←→ 缓存2
   └── [过滤器N] ←→ 缓存N
   ↕ (Push/Pull 双向)
[状态管理层] ←→ 用户输入（键盘事件）
   │
   ▼
[渲染层] → TUI 彩色渲染
```

---

## 第一层：日志读取与缓存层

### 核心假设

日志文件**只会被追加**，不会被编辑（中间内容不变）。

### 文件读取

- 使用 **mmap（内存映射）** 代替传统 `read()`，OS 按需加载页，避免用户态/内核态拷贝
- 建立**行索引表**（行号 → 文件偏移量），支持 O(1) 跳转任意行，增量追加更新

### 工作模式

| 模式 | 触发键 | 行为 |
|------|--------|------|
| 静态模式 | `s` | 加载完当前内容后不再监控 |
| 实时模式 | `r` | 每 500ms 轮询文件，检测并推送新增内容 |
| 强制检查 | `f` | 立即检测新增/变更并更新显示 |

启动时默认进入**实时模式**。

### 文件变更检测

每次检测时比较文件状态：

| 检测条件 | 判断 | 处理 |
|----------|------|------|
| 文件大小增大 | 有新内容追加 | 读取新增部分，推送到过滤层 |
| 文件大小不变 | 无变化 | 不操作 |
| 文件大小变小 / inode 变更 | 文件被重新写入 | 触发**文件刷新弹窗** |

### 文件刷新弹窗

```
┌──────────────────────────────┐
│  检测到文件已被重新写入       │
│                              │
│  是否重新加载？               │
│                              │
│  [Y] 重新加载   [N] 继续浏览  │
└──────────────────────────────┘
```

- 实时模式自动弹出提示
- 按 `f` 强制重新加载（跳过弹窗直接刷新）
- 重新加载时清空全部缓存，从头处理

---

## 第二层：链路过滤层

### 过滤器属性

| 属性 | 说明 |
|------|------|
| `pattern` | 正则表达式 |
| `color` | 匹配高亮颜色（新增时随机分配） |
| `enabled` | 启用/禁用开关 |
| `exclude` | false = 保留匹配行，true = 排除匹配行 |

过滤器没有名称，以**编号**标识，编号从 1 开始，始终连续递增（删除后重新编号）。

### 颜色分配

- 新增过滤器时从预设调色板随机选取未使用颜色
- 调色板颜色用尽后循环使用
- 用户可通过编辑过滤器手动覆盖颜色

### 默认行为

无任何过滤器时显示原始文件所有行。

### 处理机制

- **链式过滤**：有序处理，每个过滤器的输出作为下一个的输入
- **颜色叠加**：后续过滤器的颜色标记可覆盖前面过滤器的颜色
- **全量预处理**：过滤器链变更时对整个日志文件进行完整过滤，显示进度条，完成后才可查看
- **行号缓存**：每个过滤器只缓存匹配的行号列表，颜色信息动态计算以节省内存
- **正则预编译**：过滤器配置时编译正则，不在匹配时重复编译
- **并行过滤**：每个过滤器内部将行分段，线程池并行处理后合并

### 刷新策略

| 触发条件 | 刷新范围 |
|----------|----------|
| 某过滤器任意属性变更 | 从该过滤器重新处理后续所有链路 |
| 实时模式新内容到达 | 500ms 窗口内的新增行批量推送，通过完整过滤链处理后追加到结果 |
| 文件重新加载 | 清空全部缓存，从头处理 |

### 持久化

- **自动保存**：每次链路变更（增/删/改过滤器）立即保存到 JSON 文件
- **自动加载**：启动时加载上次保存的链路配置
- 保存路径：
  - Linux / macOS：`~/.config/ttlogviewer/last_session.json`
  - Windows：`%APPDATA%\ttlogviewer\last_session.json`

```json
{
  "version": 1,
  "last_file": "/var/log/app.log",
  "mode": "realtime",
  "filters": [
    { "pattern": "ERROR",     "color": "#FF5555", "enabled": true,  "exclude": false },
    { "pattern": "heartbeat", "color": "#AAAAAA", "enabled": true,  "exclude": true  }
  ]
}
```

---

## 第三层：状态管理层

**核心职责**：作为用户输入与数据之间的中间层，不直接参与渲染

- 维护当前应用状态：工作模式、焦点位置、选中过滤器、搜索关键词、行号显示开关等
- 接收并分发键盘事件，根据当前状态决定行为
- 动态计算颜色标记：根据过滤器配置对行内容实时计算颜色段信息
- 向渲染层提供完整显示数据：行内容 + 颜色信息
- 驱动过滤层刷新、触发文件检测等操作
- **Push 流处理**：
  - G 锁定状态：立即自动滚动到新内容
  - 非锁定状态：状态栏显示"新增 +N 行"提示，不打断当前浏览
  - 输入状态：缓存更新通知，输入完成后再刷新界面

---

## 第四层：渲染层

**核心职责**：纯显示，不含业务逻辑，只根据状态管理层提供的数据进行渲染

### 界面布局

```
┌──────────────────────────────────────────────────────────┐
│  app.log  │  实时模式  │  原始: 99,999 行  │  有更新  │  ← 顶部状态栏
├──────────────────────────────────────────────────────────┤
│                                                          │
│  [原始日志区 - 上半]                                      │  ← 焦点区域之一
│  1 2024-01-01 10:00:01 ERROR something failed here       │
│  2 2024-01-01 10:00:02 INFO  service started             │
│▶ 3 2024-01-01 10:00:03 ERROR another failure occurred    │
│                                                          │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  [过滤结果区 - 下半]                                      │  ← 焦点区域之一
│  1 2024-01-01 10:00:01 ERROR something failed here       │
│▶ 2 2024-01-01 10:00:03 ERROR another failure occurred    │
│                                                          │
├──────────────────────────────────────────────────────────┤
│  [1:ERROR ●红] [2:heartbeat ○灰 排除]                     │  ← 过滤器状态栏
│  >                                                       │  ← 交互/输入行
└──────────────────────────────────────────────────────────┘
```

`Shift` 切换当前焦点在上半（原始日志区）和下半（过滤结果区）之间，`↑`/`↓`/`PgUp`/`PgDn` 操作当前焦点区域。两个区域**独立滚动，互不联动**。

### 渲染优化

- **虚拟列表**：只渲染屏幕可见行，避免对所有过滤结果进行颜色计算和渲染

### 键盘交互

#### 日志内容导航

| 按键 | 动作 |
|------|------|
| `Shift` | 切换焦点：原始日志区 ↔ 过滤结果区 |
| `↑` / `↓` | 高亮上/下一行（当前焦点区域） |
| `PgUp` / `PgDn` | 上/下翻页（当前焦点区域） |
| `Home` | 跳到第一行（当前焦点区域） |
| `End` | 跳到最后一行（当前焦点区域） |
| `g` | 输入原始文件行号跳转（若该行被过滤掉则跳到最近的可见行） |
| `G` | 跳到末尾并锁定跟随最新行（仅实时模式有效，静态模式下忽略），按任意导航键自动解除锁定 |

#### 内容搜索

搜索在**原始文件所有行**中进行，不受过滤器影响，结果可能包含当前被过滤掉的行。

| 按键 | 动作 |
|------|------|
| `/` | 进入搜索模式，输入关键词回车 |
| `n` | 跳转到下一个搜索结果 |
| `p` | 跳转到上一个搜索结果 |
| `Esc` | 退出搜索模式 |

#### 过滤器操作

| 按键 | 动作 |
|------|------|
| `a` | 追加过滤器 → 输入正则 → 回车生效，`Esc` 取消 |
| `[` / `]` | 选中上/下一个过滤器 |
| `e` | 编辑当前选中过滤器 → 回车生效，`Esc` 取消 |
| `d` | 删除当前选中过滤器 |
| `+` / `-` | 将当前选中过滤器在链路中上移/下移一位 |
| `Space` | 切换当前选中过滤器启用/禁用 |

#### 全局操作

| 按键 | 动作 |
|------|------|
| `o` | 输入文件路径切换文件，回车确认，`Esc` 取消 |
| `s` | 切换到静态模式 |
| `r` | 切换到实时模式 |
| `f` | 强制检查追加 / 强制重新加载 |
| `w` | 导出过滤结果：自动生成文件名，底部显示路径供确认，回车保存，`Esc` 取消 |
| `l` | 切换显示/隐藏原始文件行号 |
| `z` | 切换当前高亮行的折叠/展开（超长行） |
| `h` | 显示所有快捷键帮助（弹窗，任意键关闭） |
| `q` | 退出 |

### 新增过滤器输入流程

```
按 a
  → 底部显示: Pattern> _  ●  （● 为实时信号灯：绿色=编译通过，红色=编译失败）
  → 输入正则表达式，信号灯实时更新
  → Tab 预览并修改自动分配的颜色
  → 回车：
      编译通过 → 确认保存，自动保存链路配置
      编译失败 → 弹窗显示错误信息，返回输入框继续编辑
  → Esc 取消
```


### 其他显示功能

- **折叠超长行**：超长行截断显示，按 `z` 切换当前高亮行的折叠/展开
- **行号显示**：按 `l` 切换显示/隐藏原始文件行号

---

## 模块接口设计

### 跨层约定

- **UI 线程安全**：所有 UI 状态变更必须在 FTXUI 事件循环线程执行
- **后台线程原则**：后台线程只做纯计算，不触碰 UI 状态；计算完成后 atomic swap 结果，通过 `PostEvent` 通知 UI 线程刷新
- **回调线程**：`LogReader` 的所有回调均在 UI 线程触发（FileWatcher 后台线程只侦测，内部通过 `PostEvent` 转发）
- **数据流**：Pull（渲染驱动，自上而下）+ Push（文件追加，经 `PostEvent` 回 UI 线程）

---

### 第一层：LogReader

```cpp
enum class FileMode { Static, Realtime };

using NewLinesCallback = std::function<void(size_t firstLine, size_t lastLine)>;
using FileResetCallback = std::function<void()>;

class LogReader {
public:
    bool open(std::string_view path);
    void close();

    // 零拷贝；view 在下次 forceCheck() 或文件变更前有效
    // 仅在 UI 线程持有，不跨 forceCheck() 使用
    std::string_view getLine(size_t lineNo) const;        // 1-based
    std::vector<std::string_view> getLines(size_t from, size_t to) const;

    size_t lineCount() const;   // 当前已索引行数（后台索引期间动态增长）
    bool   isIndexing() const;

    void     setMode(FileMode mode);
    FileMode mode() const;

    void forceCheck();

    // 回调均在 UI 线程触发
    void onNewLines(NewLinesCallback cb);
    void onFileReset(FileResetCallback cb);

    std::string_view filePath() const;
};
```

**关键决策**：
- 不加额外块缓存，mmap + OS 页缓存已足够
- `getLine()` 返回 `string_view`（零拷贝）；FileWatcher 后台线程只侦测变化，remap 在 UI 线程执行保证安全
- `lineCount()` 索引期间返回已扫描行数，界面立即可用，`g` 跳转在 `isIndexing()` 期间禁用

---

### 第二层：FilterChain

```cpp
struct FilterDef {
    std::string pattern;
    std::string color;    // "#RRGGBB"
    bool enabled = true;
    bool exclude = false;
};

// 行内颜色段，start/end 为字节偏移
// ASCII 正则保证偏移落在合法 UTF-8 字符边界
struct ColorSpan {
    size_t      start;
    size_t      end;
    std::string color;
};

// filter 定义与其阶段输出缓存绑定为同一对象
struct FilterNode {
    FilterDef             def;
    std::regex            compiled;  // 预编译，def 变更时同步更新
    std::vector<uint32_t> output;    // 经本 filter 及之前所有 filter 处理后存活的行号（1-based）
};

using ProgressCallback = std::function<void(double)>;  // 0.0~1.0
using DoneCallback     = std::function<void()>;

class FilterChain {
public:
    explicit FilterChain(const LogReader& reader);

    void append(FilterDef def);
    void remove(size_t index);       // 0-based
    void edit(size_t index, FilterDef def);
    void moveUp(size_t index);
    void moveDown(size_t index);

    size_t           filterCount() const;
    const FilterDef& filterAt(size_t index) const;

    // 最终过滤结果（filters_.back().output）
    size_t              filteredLineCount() const;
    size_t              filteredLineAt(size_t filteredIndex) const;  // 返回原始行号（1-based）
    std::vector<size_t> filteredLines(size_t from, size_t count) const;

    // 动态计算行内颜色段（渲染时按可见行调用，不缓存）
    std::vector<ColorSpan> computeColors(size_t rawLineNo, std::string_view content) const;

    // Push 路径：UI 线程直接调用（每批行数少）
    void processNewLines(size_t firstLine, size_t lastLine);

    // filter 变更时异步重算，从 fromFilter 级开始
    // 后台线程计算，atomic swap 结果，PostEvent 通知刷新，期间保持显示旧数据
    void reprocess(size_t fromFilter, ProgressCallback onProgress, DoneCallback onDone);

    void reset();

    void save(std::string_view path) const;
    bool load(std::string_view path);
};
```

**关键决策**：
- 按阶段缓存：`FilterNode.output` 存该阶段存活行号，`filters_[k].output` 的输入是 `filters_[k-1].output`
- filter 删除时缓存随之销毁，`reprocess(fromFilter)` 只重算受影响的后续阶段
- 内部数据：`std::vector<FilterNode> filters_`，公开接口只暴露最终结果

---

### 第三层：AppController

```cpp
enum class FocusArea { Raw, Filtered };
enum class AppMode   { Static, Realtime };
enum class InputMode {
    None, Search, FilterAdd, FilterEdit, GotoLine, OpenFile, ExportConfirm
};

struct PaneState {
    size_t cursor;       // 高亮行在全量列表中的绝对位置
    size_t scrollOffset; // 视口顶行的绝对位置，由 cursor 驱动保证高亮行可见
};

struct LogLine {
    size_t                 rawLineNo;
    std::string_view       content;
    std::vector<ColorSpan> colors;
    bool                   highlighted;
};

struct ViewData {
    // 顶部状态栏
    std::string fileName;
    AppMode     mode;
    size_t      totalLines;
    size_t      newLineCount;   // 非 G 锁定时累积的未读新增行数
    bool        isIndexing;

    // 两个独立窗格的可见行切片（已由 AppController 按窗格高度裁剪）
    std::vector<LogLine> rawPane;
    bool                 rawFocused;
    std::vector<LogLine> filteredPane;
    bool                 filteredFocused;

    // 过滤器栏
    struct FilterTag {
        int         number;    // 1-based 显示编号
        std::string pattern;
        std::string color;
        bool        enabled;
        bool        exclude;
        bool        selected;
    };
    std::vector<FilterTag> filterTags;

    // 底部输入行
    InputMode   inputMode;
    std::string inputPrompt;   // "Pattern> " / "Goto: " 等
    std::string inputBuffer;
    bool        inputValid;    // 正则信号灯

    // 弹窗
    bool        showDialog;
    std::string dialogTitle;
    std::string dialogBody;
    bool        dialogHasChoice;  // true = Y/N，false = 任意键关闭

    // 进度条
    bool   showProgress;
    double progress;
};

class AppController {
public:
    AppController(LogReader& reader, FilterChain& chain);

    bool     handleKey(const ftxui::Event& event);
    ViewData getViewData(int rawPaneHeight, int filteredPaneHeight) const;
    void     onTerminalResize(int width, int height);
};
```

**关键决策**：
- 两窗格各自维护 `PaneState`，`scrollOffset` 由 `cursor` 驱动
- `handleKey()` 按 `InputMode` 分发到独立私有方法，每个模式一个函数
- 搜索在后台线程执行，结果通过 `PostEvent` 回 UI 线程

---

### 第四层：渲染层

```cpp
// 唯一对外接口
ftxui::Component CreateMainComponent(AppController& controller);
```

**内部子组件**：`StatusBar`、`LogPane`（虚拟列表）、`FilterBar`、`InputLine`、`DialogOverlay`、`ProgressOverlay`

**关键决策**：
- 虚拟列表由 `AppController::getViewData(paneHeight)` 负责裁剪，渲染层只排列可见切片
- 行内颜色：按 `ColorSpan` 拆成多个 `text()` 片段，`hbox()` 拼合
- 完全无业务状态，每次渲染调用 `getViewData()` 取最新快照

---

### UTF-8 多字节字符处理

`ColorSpan` 使用字节偏移。UTF-8 保证 ASCII 字节（0x00–0x7F）不会出现在多字节字符的中间（中间字节固定为 0x80–0xBF），因此 ASCII 正则的匹配边界必然落在合法 UTF-8 字符边界上。FTXUI 的 `string_width()` 正确处理 CJK fullwidth 字符（2 列宽），`hbox()` 拼合颜色片段不会错位。

防御性校验：

```cpp
bool isUtf8Boundary(std::string_view s, size_t pos) {
    if (pos >= s.size()) return true;
    return (s[pos] & 0xC0) != 0x80;  // 不是 continuation byte
}
```

---

## 待定讨论

以下问题尚未确定，需要后续讨论：

1. ~~**如何打开文件**~~ **已确定**：`ttlogviewer [文件路径]` 支持一个可选位置参数；进入后可按 `o` 键输入文件路径切换文件。
2. ~~**`n` 键冲突**~~ **已确定**：过滤器选择改为 `[` / `]` 键，`n`/`N` 专用于搜索跳转，彻底避免冲突。
3. ~~**`G` 锁定跟随的解除**~~ **已确定**：按任意导航键（↑↓PgUp/PgDn 等）自动解除锁定。
4. ~~**`g` 行号跳转的作用域**~~ **已确定**：跳转到原始文件的第 N 行，若该行被过滤掉则跳到最近的可见行。
5. ~~**`w` 导出的交互流程**~~ **已确定**：自动生成文件名，底部显示完整路径供确认，回车保存，`Esc` 取消。
6. ~~**折叠超长行的触发键**~~ **已确定**：按 `z` 切换当前高亮行的折叠/展开。
7. ~~**行号显示开关的触发键**~~ **已确定**：按 `l` 切换显示/隐藏行号。
8. ~~**新增过滤器的名称**~~ **已确定**：过滤器无名称，仅有从 1 开始的连续编号，删除后重新编号。
9. ~~**无效正则的处理**~~ **已确定**：输入框末尾实时信号灯（绿=通过/红=失败），编译失败时禁止保存，按回车弹窗显示错误信息。
10. ~~**行索引建立耗时**~~ **已确定**：后台异步扫描，界面立即可用；状态栏显示"索引建立中..."，完成前 `g` 键不可用。

---

## 未来功能

当前版本不实现，但值得在未来考虑：

- **双区联动**：在过滤结果区选中某行时，原始日志区自动跳转到对应的原始行
- **懒求值过滤**：按需计算过滤结果，而非全量预处理，适用于超大文件
- **智能缓存淘汰**：LRU 策略管理过滤缓存，平衡内存占用和响应速度
- **动态过滤率预估**：根据历史数据智能估算所需读取行数

---

## 已排除功能

以下功能明确不在本项目范围内：

- **命令行选项**：不支持 `--filter` 等选项参数，仅支持一个文件路径位置参数
- **多标签页**：不支持同时查看多个文件
- **上下文行**：不支持显示匹配行前后 N 行
- **多行匹配**：过滤器不支持跨行正则匹配
- **过滤器分组/预设**：不支持保存多套过滤链配置
- **书签**：不支持标记和跳转行
- **结构化列解析**：不支持将日志解析为结构化字段（时间戳、级别等列）
