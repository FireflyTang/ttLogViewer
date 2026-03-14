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

### 文件编码支持

通过 **BOM 自动检测**，无需用户干预：

| BOM 字节 | 编码 | 处理方式 |
|----------|------|----------|
| `EF BB BF` | UTF-8 BOM | 剥掉 3 字节 BOM，内容按 UTF-8 处理 |
| `FF FE` | UTF-16LE | 整体解码为 UTF-8，存入 decoded 缓冲区 |
| `FE FF` | UTF-16BE | 整体解码为 UTF-8，存入 decoded 缓冲区 |
| 无 BOM | UTF-8 | 直接使用 mmap（现有行为，零拷贝）|

- 转换在 `open()` 中同步完成（UI 线程），完成后索引和行读取逻辑不感知编码
- 实时模式下非 UTF-8 文件若发生增长，触发全量重新加载（fileResetCb），因为 decoded 偏移无法增量映射到 raw mmap 偏移
- 不支持无 BOM 的 GBK / Latin-1 等 ANSI 编码（需用户显式指定或启发式检测）

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
- **全量预处理**：过滤器链变更时对整个日志文件进行完整过滤，显示进度条，完成后才可查看；超过 30 秒时弹 Y/N 确认（Y=继续等待，N=取消过滤）
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
| `o` | 输入文件路径切换文件，`Tab` 补全文件名/目录，回车确认，`Esc` 取消 |
| `s` | 切换到静态模式 |
| `r` | 切换到实时模式 |
| `f` | 强制检查追加 / 强制重新加载 |
| `w` | 导出过滤结果：自动生成文件名，底部显示路径供确认，回车保存，`Esc` 取消 |
| `l` | 切换显示/隐藏原始文件行号 |
| `z` | 切换当前高亮行的折叠/展开（超长行） |
| `←` / `→` | 当前窗格水平滚动（每次 4 字节，UTF-8 边界对齐） |
| `h` | 显示所有快捷键帮助（弹窗，任意键关闭） |
| `q` | 退出（弹窗 Y/N 确认） |

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

- **折叠超长行**：超长行截断显示，按 `z` 切换当前高亮行的折叠/展开；折叠行末尾显示 `…` 并保留颜色高亮
- **水平滚动**：`←`/`→` 键平移当前窗格的水平偏移，每次 4 字节（UTF-8 边界对齐），两窗格独立滚动
- **鼠标支持**：滚轮在悬停窗格内滚动（±1 行/次，Ctrl+滚轮透传终端），单击切换焦点并定位行；**拖拽** 字符级文本选择，拖出窗格边界时自动垂直和水平滚动（竖向 ±1 行/次，横向 ±hScrollStep 字节/次）
- **行号显示**：默认开启，按 `l` 切换显示/隐藏原始文件行号
- **过滤栏颜色指示**：每个过滤器标签后显示颜色圆点：`●`（启用，使用过滤器颜色）或 `○`（禁用，灰色）
- **搜索关键词提示**：搜索词激活时，底部输入行（`None` 模式）显示 `/keyword  (N/M)  n/N:跳转  Esc:清除`，无匹配时显示 `无结果`
- **过滤超时确认**：过滤重处理耗时超过 30 秒时，弹 Y/N 对话框（Y = 继续等待，N = 中止过滤并重置进度条）

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

    // 取消正在进行的 reprocess，等待线程退出
    void cancelReprocess();

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
        int         number;     // 1-based 显示编号
        std::string pattern;
        std::string color;
        bool        enabled;
        bool        exclude;
        bool        selected;
        bool        useRegex;   // true=正则模式，false=字符串模式
        size_t      matchCount; // 经本过滤器处理后存活的行数
    };
    std::vector<FilterTag> filterTags;

    // 底部输入行
    InputMode   inputMode;
    std::string inputPrompt;   // "Pattern> " / "Goto: " 等
    std::string inputBuffer;
    bool        inputValid;    // 正则信号灯

    // 搜索状态（inputMode=None 时底部显示用）
    std::string searchKeyword;        // 当前搜索词，空串=无激活搜索
    size_t      searchResultCount;    // 搜索结果总数
    size_t      searchResultIndex;    // 当前位置（1-based）

    // 弹窗
    bool        showDialog;
    std::string dialogTitle;
    std::string dialogBody;
    bool        dialogHasChoice;  // true = Y/N，false = 任意键关闭

    // 进度条
    bool   showProgress;
    double progress;

    // 文本选区
    bool hasSelection;  // 是否有活动选区

    // 文件路径 Tab 补全（InputMode::OpenFile 时有效）
    bool                     showCompletions;
    std::vector<std::string> completions;   // 候选文件名列表（仅文件名，不含目录前缀）
    size_t                   completionIndex;  // 当前高亮项（0-based）
    int                      completionCol;    // 补全弹窗文本起始列（用于对齐）
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

## 测试方案

### 测试框架与工具

- **测试框架**：Google Test + Google Mock
- **引入方式**：CMake FetchContent 自动下载
- **接口隔离**：`ILogReader`、`IFilterChain` 纯虚基类，供 Mock 使用
- **渲染层测试**：FTXUI `Screen::Create()` + `Render()` 渲染到内存 buffer，不依赖真实终端

**测试工具函数**（`tests/helpers/test_utils.hpp`，不进生产接口）：

```cpp
// 等待后台索引完成（带超时）
void waitForIndexing(ILogReader& reader,
                     std::chrono::milliseconds timeout = std::chrono::seconds(5));

// 等待 reprocess() 完成（配合 std::promise）
void waitForReprocess(std::promise<void>& done,
                      std::chrono::milliseconds timeout = std::chrono::seconds(5));
```

**RAII 临时文件**（`tests/helpers/temp_file.hpp`）：

```cpp
class TempFile {
public:
    explicit TempFile(std::string_view content);
    void append(std::string_view content);
    void truncateAndWrite(std::string_view content);  // 模拟文件重写
    std::string path() const;
    ~TempFile();  // 自动删除
};
```

---

### 单元测试

#### LogReader

| 测试点 | 边界场景 |
|--------|---------|
| 正常读取行内容 | 空文件、单行无末尾换行、只有换行符的文件 |
| `lineCount()` 正确 | 索引期间动态增长，完成后稳定 |
| `getLine()` 内容正确 | 含中文的 UTF-8 行不截断字符；超长单行（1MB+） |
| `getLine()` 越界 | lineNo=0（无效，1-based）；lineNo > lineCount |
| `getLines(from, to)` 范围 | from > to（无效）；to > lineCount（末尾截断） |
| 换行符兼容 | CRLF（\r\n）文件；混合换行符文件 |
| `forceCheck()` 检测追加 | 追加 0 字节（无变化）、追加 1 字节、追加多行 |
| `forceCheck()` 检测文件重写 | truncate 后重写、inode 变更、文件被删除 |
| `forceCheck()` 在未 open 时调用 | 不 crash，返回或忽略 |
| `onNewLines` 触发参数正确 | firstLine / lastLine 边界值 |
| `onFileReset` 触发 | 文件缩小 / 删除重建 |
| `close()` 时索引线程仍在运行 | 应安全 join 线程，不 hang |
| `open()` 二次调用（未先 close） | 自动关闭前一个，重新打开 |
| 无读权限文件 | `open()` 返回 false，不 crash |
| 静态/实时模式切换 | 切换后轮询行为变化；静态模式下 `forceCheck()` 仍能手动触发检查 |

#### FilterChain

| 测试点 | 边界场景 |
|--------|---------|
| 无过滤器 → 全量通过 | — |
| 所有过滤器 disabled → 全量通过 | 部分 disabled、全部 disabled |
| include filter 正确 | 无匹配行、全部匹配行 |
| exclude filter 正确 | 无匹配行（结果不变）、全部匹配行（结果为空） |
| 链式过滤结果正确 | 多个 include + exclude 混用；首 filter 为 exclude |
| `moveUp` / `moveDown` 后结果变化 | 调换顺序后验证结果与原链不同；moveUp 第一个 → 无效果；moveDown 最后一个 → 无效果 |
| `remove()` 后缓存清除 | 删中间节点、删首节点、删尾节点 |
| `processNewLines()` 追加正确 | 追加 0 行、追加 1 行、追加多行；`reset()` 后调用 |
| `reprocess(fromFilter)` 只重算后续 | spy 验证前段 output 对象未被重建 |
| `reprocess()` 期间调用变更操作 | `append/edit/remove/reset` 调用时取消当前 reprocess，重新开始 |
| 多次快速 `reprocess()` | 最后一次结果生效，中间结果不污染状态 |
| `computeColors()` 颜色段正确 | 无匹配（空 spans）、全行匹配、多处匹配 |
| `computeColors()` 颜色叠加 | 后 filter 颜色覆盖前 filter 颜色；同一行多 filter 各自着色不同段 |
| 越界访问 | `filteredLineAt(N)` N >= filteredLineCount；`filterAt(N)` N >= filterCount；`filteredLines(from, count)` 末尾部分范围 |
| 颜色调色板耗尽 | 超出调色板数量后循环复用，不 crash |
| 无效正则 | 构造 FilterDef 时抛出 / 返回错误，不 crash |
| 空字符串 pattern `""` | 明确定义行为（拒绝或匹配所有行） |
| `edit()` 传入相同值 | 仍触发 reprocess（保持一致性） |
| JSON save/load 往返一致 | 空链路、多 filter、含特殊字符的 pattern、版本字段 |
| `load()` 异常输入 | JSON schema 不匹配、version 字段未知、文件不存在 |
| `save()` 异常路径 | 目标目录不存在 → 创建目录或返回错误 |
| 并发：`reprocess()` 期间读 `filteredLineAt()` | 不崩溃，返回旧数据或新数据，不返回混合数据 |

#### AppController

| 测试点 | 边界场景 |
|--------|---------|
| `↑` / `↓` 更新 cursor | cursor 在第一行时按 `↑` → 停留在 0；cursor 在末行时按 `↓` → 停留在末行 |
| `PgDn` / `PgUp` 翻页 | 最后一页 `PgDn` → 停在末行；第一页 `PgUp` → 停在首行 |
| `Home` / `End` 跳转 | 任意位置按 Home → cursor=0；任意位置按 End → cursor=末行 |
| `Shift` 切换焦点 | raw ↔ filtered 各自独立滚动，切换后另一区 cursor 不变 |
| `G` 锁定后新行自动滚末尾 | 新增 1 行、新增 100 行 |
| `G` 在静态模式下 | 忽略，不锁定，不跳末尾 |
| 导航键取消 G 锁定 | `↑` 取消、`PgUp` 取消、`Home` 取消 |
| 非锁定时新行只增 `newLineCount` | cursor 不移动 |
| `s` / `r` 模式切换 | ViewData.mode 正确变化；静态→实时后轮询恢复 |
| `l` 切换行号显示 | ViewData 中行号显示标志正确翻转 |
| `z` 折叠/展开当前行 | 高亮行状态 toggle；其他行不受影响 |
| `h` 帮助弹窗 | showDialog=true，内容含所有快捷键；任意键关闭 |
| `[` / `]` 选中过滤器 | 无过滤器时无效果；到达首/末后循环 |
| `d` 删除过滤器 | 删最后一个 filter → filterCount=0；删后编号重排 |
| `+` / `-` 移动过滤器 | 首个上移 → 无效果；末个下移 → 无效果 |
| `Space` 切换 enabled | ViewData.filterTags[i].enabled 翻转；结果区即时变化 |
| `a` 完整流程 | 输入正则 → 信号灯绿 → Enter 保存；中途 Esc → 取消不创建 |
| `a` 无效正则 | 信号灯红；Enter → 弹出编译错误信息；filter 不创建 |
| `a` → Tab 预览颜色 | inputMode 进入颜色预览子状态 |
| `e` 编辑完整流程 | 修改 pattern → Enter 保存，reprocess 触发；Esc 取消，filter 不变 |
| `e` 修改为无效正则 | Enter → 弹出错误，filter 保持原值 |
| `o` 切换文件 | 有效路径 → 文件加载；无效路径 → 错误提示；Esc → 取消 |
| `w` 导出 | Enter 确认 → 文件写出；Esc → 取消；无 filter 时导出全量原始行 |
| `g` 完整流程 | 逐位输入数字 → Enter 跳转；Esc 取消；isIndexing 时禁用 |
| `g` 跳转到被过滤行 | 跳转到最近可见行，而非目标行 |
| `g` 跳转超出行数范围 | 跳转到末行 |
| `getViewData()` 裁剪正确 | paneHeight=0、paneHeight=1、paneHeight > 总行数 |
| `getViewData()` 双区均空 | 未打开文件时不 crash |
| 搜索无结果 | `n` / `p` 无响应或给出提示 |
| 搜索单个结果 | `n` 和 `p` 均停在同一行 |
| 搜索 `n` / `p` 循环 | 末尾结果按 `n` → 跳回首个结果；首个结果按 `p` → 跳到末尾 |
| 搜索 `Esc` | 退出搜索模式，高亮清除，inputMode=None |
| 搜索含中文关键词 | 正确匹配，不截断字符 |
| 输入状态中文件追加 | 新增行通知缓存，输入完成后再刷新界面 |
| `onTerminalResize()` | 滚动偏移重新校正，cursor 仍在可见区域 |
| InputMode 切换不串状态 | 多种模式进出后 buffer 清空、inputValid 复位 |

#### 渲染工具函数

| 测试点 | 边界场景 |
|--------|---------|
| `renderColoredLine()` 片段数和颜色 | 无 span（1 片段）、1 span、多 span、span 覆盖全行、空字符串内容 |
| `renderColoredLine()` CJK 内容 | 含中文字符 + 颜色段，hbox 宽度计算正确 |
| `isUtf8Boundary()` | ASCII 字节（边界）、中文首字节（边界）、中文续字节（非边界）、pos == size（边界） |

---

### 渲染层测试（FTXUI Screen）

利用 `Screen::Create()` + `Render()` 渲染到内存，检查字符内容：

| 子组件 | 测试内容 |
|--------|---------|
| `StatusBar` | 文件名显示（超长截断）、模式文字、总行数格式（千位分隔）、"索引建立中..." 提示、newLineCount>0 时的 "+N 行" 提示 |
| `FilterBar` | tag 数量与 filterCount 一致、编号从 1 开始连续、disabled 有视觉区分、selected 有高亮、0 个 filter 时不 crash |
| `LogPane` | paneHeight=5 时渲染恰好 5 行；高亮行有 `▶` 标记；行号显示/隐藏切换；超长折叠行显示截断标记 |
| `InputLine` | 各 InputMode 下 prompt 文字正确；buffer 内容显示；正则信号灯颜色（绿/红）；颜色预览状态（Tab 后） |
| `DialogOverlay` | title + body 出现在输出中；Y/N 提示仅在 `dialogHasChoice=true` 时出现；帮助弹窗含所有快捷键；错误弹窗含错误消息 |
| `ProgressOverlay` | 0% / 50% / 100% 时进度条宽度正确；showProgress=false 时不显示 |

---

### 端到端测试

使用真实文件 + 真实四层对象，`handleKey()` 驱动，`getViewData()` 断言，不需要真实终端。

| 场景 | 边界场景 |
|------|---------|
| 无参数启动 | 空状态不 crash；显示打开文件提示 |
| 打开文件，raw 区显示所有行 | 空文件（0 行）、1 行文件、含中文内容、超长单行 |
| `o` 切换文件 | 有效路径加载；无效路径错误提示；Esc 取消；切换后旧 filter 链保留 |
| 添加 include filter，过滤区正确 | 无匹配、全部匹配、pattern 含特殊字符 |
| 添加 exclude filter，过滤区正确 | 排除后结果为空 |
| `a` 输入无效正则 | 信号灯红，弹出编译错误，filter 不创建 |
| `a` → Tab 预览并确认颜色 | 颜色正确保存到 filter |
| 链式过滤：多 filter 叠加 | 调换顺序后结果不同（验证顺序语义） |
| 过滤器 `moveUp` / `moveDown` 后结果变化 | moveUp 第一个 → 无变化；moveDown 最后一个 → 无变化 |
| 删除过滤器后结果回退 | 删中间节点、删所有 filter → 全量显示；编号重排连续 |
| 编辑过滤器 pattern，结果重新计算 | 修改为无效正则 → 弹窗提示，filter 不更新；Esc 取消 → 原值保留 |
| 过滤器 `disable` / `enable` 切换（`Space`） | disable 后等同于无该 filter；re-enable 后结果恢复 |
| 过滤器 `+` / `-` 移动（键盘操作路径） | 首个上移 → 无变化；末个下移 → 无变化 |
| `s` / `r` 模式切换 | StatusBar 模式文字变化；切换到静态后文件追加不触发通知 |
| `l` 切换行号显示 | 行号出现/消失 |
| `z` 折叠/展开超长行 | 同一行反复切换状态正确 |
| `h` 帮助弹窗 | 显示后任意键关闭，inputMode 恢复 None |
| 实时模式：文件追加，新行出现 | 追加 1 行、追加 100 行、G 锁定跟随 |
| 实时模式：reprocess 进行中同时文件追加 | 两者结果均正确反映，不丢失新增行 |
| 文件重写，弹窗出现 | showDialog=true，选 Y 重载（所有缓存清空）、选 N 继续浏览 |
| `f` 强制重载 | 文件重写后 `f` 跳过弹窗直接刷新 |
| 导航：`↑↓ PgUp PgDn Home End` | 首行/末行边界；两区独立（raw 区操作不影响 filtered 区） |
| `Shift` 切换焦点 | 切换后各区 cursor 独立维护 |
| `g` 行号跳转 | 跳到已过滤行（跳最近可见行）、跳超范围；isIndexing 时禁用 |
| `G` 锁定 + 导航解锁 | 追加文件后确认自动滚；按 `↑` 后不再自动滚；静态模式下 `G` 无效 |
| 搜索 `/` + `n` / `p` 跳转 | 无结果、单结果、多结果循环；`n`/`p` 到达边界后循环 |
| 搜索结果含被过滤行 | 搜索跳转到该行，原始区高亮；过滤区不跳（该行不可见） |
| 搜索 `Esc` | 退出搜索模式，高亮清除 |
| `w` 导出 | Enter 确认后文件内容与 filteredPane 行内容一致；Esc 取消；无 filter 时导出全量 |
| JSON save/load 会话恢复 | 增删 filter 后关闭再打开，filterCount、pattern、color、enabled、exclude 与保存前一致 |
| 输入状态中文件追加 | 新增行通知缓存，输入完成后再刷新 |

---

### 测试目录结构

```
tests/
├── unit/
│   ├── log_reader_test.cpp
│   ├── filter_chain_test.cpp
│   ├── app_controller_test.cpp
│   └── render_utils_test.cpp       # renderColoredLine, isUtf8Boundary
├── render/
│   ├── status_bar_test.cpp
│   ├── filter_bar_test.cpp
│   ├── log_pane_test.cpp
│   └── input_line_test.cpp
├── e2e/
│   ├── file_open_test.cpp          # 打开文件、切换文件、无参数启动
│   ├── filter_workflow_test.cpp    # 增删改、顺序调整、disable/enable
│   ├── realtime_test.cpp           # 追加、文件重写、G 锁定
│   ├── navigation_test.cpp         # ↑↓PgUp/PgDn/Home/End/g/G/Shift
│   ├── search_test.cpp             # 搜索、n/p 循环、Esc
│   ├── input_flow_test.cpp         # a/e/o/w/g 完整输入流程
│   └── session_test.cpp            # save/load 会话恢复、模式切换
└── helpers/
    ├── temp_file.hpp
    ├── mock_log_reader.hpp         # Google Mock
    ├── mock_filter_chain.hpp
    └── test_utils.hpp              # waitForIndexing, waitForReprocess
```

### 异步测试策略

| 场景 | 策略 |
|------|------|
| `reprocess()` 后台线程 | `DoneCallback` 里 `promise.set_value()`，`future.wait_for(5s)` 等待 |
| 全文搜索后台线程 | 核心逻辑提取为纯函数 `searchLines()` 同步测试；异步派发不测 |
| LogReader 后台索引 | `waitForIndexing()` 轮询 `isIndexing()` 带超时 |
| 文件变更检测 | `forceCheck()` 设计为同步（检测并触发回调后返回），绕过轮询计时器 |

---

## 线程交互分析

多线程是 bug 的主要来源，本节系统梳理各线程职责、共享数据保护机制和潜在竞争点。

### 线程总览

| 线程名 | 所属模块 | 生命周期 | 职责 |
|--------|----------|---------|------|
| **UI 主线程** | 全局 | 程序全程 | FTXUI 事件循环、所有 UI 状态读写、渲染驱动 |
| **IndexThread** | LogReader | `open()` 到索引完成后退出 | 扫描文件建立 `lineOffsets_` 行偏移表 |
| **FileWatcher** | LogReader | 实时模式常驻，静态模式无此线程 | 500ms 轮询 `stat()`，检测追加/重写 |
| **ReprocessThread** | FilterChain | 每次 `reprocess()` 新建，完成后退出 | 全量重算过滤链，atomic swap 结果 |
| **SearchThread** | AppController | 每次 `/` 搜索新建，完成后退出 | `searchLines()` 遍历全量行 |

---

### 线程交互全景图

```
                    ┌─────────────────────────────────────────┐
                    │             UI 主线程                    │
                    │  FTXUI 事件循环                          │
                    │  ├── handleKey()                        │
                    │  ├── onNewLines()    ◄── PostEvent ──┐  │
                    │  ├── onFileReset()   ◄── PostEvent ──┼──┤
                    │  ├── onIndexDone()   ◄── PostEvent ──┼──┤
                    │  ├── onReprocessDone()◄─ PostEvent ──┼──┤
                    │  ├── onSearchDone()  ◄── PostEvent ──┼──┤
                    │  ├── forceCheck()  ──┐               │  │
                    │  └── getViewData()   │               │  │
                    └──────────┬──────────┼───────────────┘  │
                               │          │                   │
          ┌────────────────────┼──────────┼───────────────────┘
          │                    │          │
          ▼                    ▼          ▼
 ┌─────────────────┐  ┌───────────────┐  ┌─────────────────────┐
 │   IndexThread   │  │  FileWatcher  │  │   ReprocessThread   │
 │                 │  │               │  │                     │
 │ atomic 写       │  │ stat() 轮询   │  │ 操作本地副本        │
 │ lineCount_      │  │ 检测到追加 →  │  │ local_filters       │
 │                 │  │ PostEvent     │  │ 完成后 atomic swap  │
 │ 完成后          │  │ 检测到重写 →  │  │ filters_            │
 │ PostEvent       │  │ PostEvent     │  │ 再 PostEvent        │
 └─────────────────┘  └───────────────┘  └─────────────────────┘

                    ┌─────────────────────┐
                    │    SearchThread     │
                    │                    │
                    │ 持有 MmapRegion     │
                    │ shared_ptr         │
                    │ searchLines() 遍历 │
                    │ 完成后 PostEvent   │
                    └─────────────────────┘
```

**核心原则**：所有后台线程只做纯计算，**不触碰任何 UI 状态**；结果通过 `PostEvent` 携带数据传回 UI 线程，或先 atomic swap 再 PostEvent 通知刷新。

---

### 共享数据详解

#### 1. `LogReader::lineOffsets_`（行偏移索引表）

```
IndexThread ──写──→ lineOffsets_[n], 然后 atomic 递增 lineCount_
UI 线程     ──读──→ 先读 lineCount_ 确认范围，再读 lineOffsets_[0..lineCount_-1]
```

**保护机制**：`lineCount_` 声明为 `std::atomic<size_t>`。IndexThread 规则：**先完整写入 `lineOffsets_[n]`，再原子递增 `lineCount_`**（释放语义 `memory_order_release`）。UI 线程读 `lineCount_`（获取语义 `memory_order_acquire`），之后读 `lineOffsets_[0..lineCount_-1]` 均已有效。无需 mutex，热路径无锁。

**不变量**：`lineOffsets_` 只追加，不修改已写入的槽位，因此 UI 线程读老槽位不存在写后读竞争。

---

#### 2. `LogReader::mmapPtr_` / `mmapSize_`（内存映射区域）

```
UI 线程     ──读──→ getLine() 返回 string_view（指向 mmap 内存）
UI 线程     ──写──→ onFileReset 后 remap（重新映射）
SearchThread──读──→ searchLines() 遍历行内容（持有旧映射引用）
```

**保护机制**：LogReader 内部用 `std::shared_ptr<MmapRegion>` 管理映射区域。SearchThread 启动时拷贝一份 `shared_ptr`（增加引用计数）。文件重置时 UI 线程创建新的 `MmapRegion` 替换，旧区域引用计数减一；SearchThread 持有引用期间旧映射不析构，不出现 dangling pointer。SearchThread 另持有 atomic 取消标志，文件重置时 UI 线程置位取消，SearchThread 提前退出并释放引用。

---

#### 3. `FilterChain::filters_`（过滤器链 + 阶段缓存）

```
UI 线程          ──读──→ filteredLineAt(), filteredLineCount()
UI 线程          ──写──→ processNewLines()（追加到各阶段末尾）
ReprocessThread  ──写──→ 操作本地 local_filters，完成后 swap
```

**保护机制**：ReprocessThread **全程操作本地副本** `vector<FilterNode> local_filters`，不触碰 `filters_`。计算完成后：

```cpp
// ReprocessThread 完成时序
{
    std::lock_guard lock(mutex_);
    filters_.swap(local_filters);   // 或 atomic pointer swap
    isReprocessing_.store(false);
}
screen_.PostEvent(Event::Custom);  // 通知 UI 线程刷新
```

UI 线程在 PostEvent 回调里处理 `pendingNewLines_`（见下节），始终单线程写 `filters_`，无竞争。

---

#### 4. `reprocess` 与 `processNewLines` 并发

**场景**：ReprocessThread 运行期间，文件有新增行，FileWatcher PostEvent → UI 线程触发 `onNewLines` → 调用 `processNewLines()`。

**问题**：此时 filters_ 正被 ReprocessThread 计算（写本地副本），processNewLines 若直接追加到 filters_ 会被 swap 覆盖，导致**新增行丢失**。

**解决方案**：`pendingNewLines_` 缓冲（UI 线程私有变量）：

```
processNewLines() 检查 isReprocessing_
├── true  → 缓存到 pendingNewLines_ = { firstLine, lastLine }
└── false → 直接追加到 filters_（正常路径）

ReprocessThread 完成 → PostEvent → UI 线程：
  1. swap(filters_, local_filters)
  2. for (auto& r : pendingNewLines_)
         processNewLinesImpl(r.first, r.last);  // 追加到新 filters_
  3. pendingNewLines_.clear()
  4. isReprocessing_ = false
```

`pendingNewLines_` 和 `isReprocessing_`（非 atomic，纯 UI 线程变量）无需同步。

---

#### 5. AppController 搜索结果

SearchThread 不直接写任何 AppController 成员。搜索结果通过 PostEvent 的 Custom Event 携带（`std::vector<size_t>` 拷贝）传回 UI 线程，UI 线程收到后赋值到 `searchResults_`。**零共享内存**，无竞争。

---

### 多次快速 reprocess 取消机制

用户连续修改过滤器，每次修改触发一次 reprocess，需要取消上一次：

```cpp
std::atomic<bool>  cancelFlag_{false};
std::thread        reprocessThread_;

void reprocess(size_t fromFilter, ProgressCB onProg, DoneCB onDone) {
    cancelFlag_.store(true);         // 通知上次线程取消
    if (reprocessThread_.joinable())
        reprocessThread_.join();     // 等上次安全退出
    cancelFlag_.store(false);        // 重置取消标志
    reprocessThread_ = std::thread([this, fromFilter, onProg, onDone] {
        // 内部定期检查 cancelFlag_
        for (size_t i = fromFilter; i < local_filters.size(); ++i) {
            if (cancelFlag_.load()) return;  // 提前退出，不 swap，不 PostEvent
            // ... 处理 local_filters[i] ...
        }
        // 未被取消 → swap + PostEvent
    });
}
```

**不变量**：同一时刻最多一个 ReprocessThread 存活。

---

### forceCheck 与 FileWatcher 竞争

**场景**：用户按 `f` 触发 `forceCheck()`（UI 线程），FileWatcher 同时在后台线程执行 `stat()` 检查。两者可能同时处理同一批新增行，导致 `onNewLines` 被触发两次。

**保护机制**：`checkMutex_`（LogReader 内部 mutex）。FileWatcher 和 `forceCheck()` 均先获取该锁再执行检测逻辑，保证互斥。

```cpp
void forceCheck() {
    std::lock_guard lock(checkMutex_);
    doCheck();   // stat() + 处理追加/重写 + PostEvent
}

// FileWatcher 线程循环
while (!stopped_) {
    std::this_thread::sleep_for(500ms);
    std::lock_guard lock(checkMutex_);
    doCheck();
}
```

`doCheck()` 内部记录上次已处理到的文件偏移，两次调用不会重复处理同一字节范围。

---

### 竞争点汇总

| 竞争点 | 场景 | 防范措施 |
|--------|------|---------|
| `lineOffsets_` 读写 | IndexThread 追加 vs UI 读 | `lineCount_` atomic release/acquire |
| `getLine()` 返回的 string_view 失效 | SearchThread 持有 vs UI remap | SearchThread 持有 `shared_ptr<MmapRegion>` |
| `filters_` 读写 | UI 线程读 vs ReprocessThread 写 | ReprocessThread 操作本地副本，atomic/mutex swap |
| 新增行丢失 | reprocess 期间 FileWatcher 推新行 | `pendingNewLines_` 缓冲，swap 后补处理 |
| 多次 reprocess 结果互相覆盖 | 用户连续改 filter | `cancelFlag_` + join 确保串行 |
| `forceCheck` 与 FileWatcher 重复处理 | 同时触发 | `checkMutex_` 互斥 |
| SearchThread 读已重置的文件 | 文件重写后旧 SearchThread 未退出 | `cancelFlag_` 置位 + `shared_ptr` 保活旧映射 |

---

### 原则总结

1. **UI 线程单写 UI 状态**：所有可见状态（`filters_`、滚动位置、`inputMode` 等）只在 UI 线程写，后台线程完全不触碰
2. **后台线程只做纯计算**：操作本地 / 只读数据，计算结果通过 PostEvent 或 atomic 交还 UI 线程
3. **PostEvent 是唯一跨线程通知通道**：除 `lineCount_` atomic 外，后台线程不直接修改任何共享状态
4. **hot-path 无锁**：`lineCount_` atomic 替代 mutex，频繁读取无阻塞
5. **可取消后台任务**：所有后台线程检查 `cancelFlag_`，支持及时退出，避免堆积
6. **资源生命周期 shared_ptr 管理**：mmap 区域通过引用计数管理，后台线程持有期间不会被析构

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

## 近期待开发功能

计划在后续版本实现：

- **行内容展开**：按 `Enter` 在弹窗中展示当前高亮行的完整内容，弹窗内可用方向键翻页，按 `Esc` 关闭

---

## 未来功能

当前版本不实现，但值得在未来考虑：

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

---

## 配置系统

### 概述

`AppConfig` 是全局单例结构体，收录所有可调参数（原代码中散落的魔法数字）。
程序在 `main()` 入口处调用 `AppConfig::loadGlobal()` 加载配置，随后所有模块通过 `AppConfig::global()` 读取。

### 加载优先级

```
编译期默认值  <  配置文件（JSON 字段覆盖）
```

- 配置文件不存在 → 全部使用默认值，静默跳过
- 配置文件存在但 JSON 无效 → 全部使用默认值，不崩溃
- 配置文件中缺失的字段 → 该字段保持默认值

### 配置文件路径

| 平台 | 默认路径 |
|------|----------|
| Windows | `%USERPROFILE%\.ttlogviewer.json` |
| Linux / macOS | `$HOME/.ttlogviewer.json` |

`loadGlobal(path)` 也接受自定义路径，供测试或高级用户使用。

### 字段表

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `uiOverheadRows` | `int` | `6` | 状态栏 + 过滤栏 + 分隔线 + 输入行 + 分割线占用的行数 |
| `dialogMaxWidth` | `int` | `60` | 对话框最大列宽 |
| `defaultTerminalWidth` | `int` | `80` | 首次 resize 事件到来前的终端宽度回退值 |
| `watcherTickCount` | `int` | `50` | 每次文件检测轮询的 tick 次数 |
| `watcherTickIntervalMs` | `int` | `10` | 每个 tick 的间隔（毫秒）；默认 50×10ms = 500ms 检测周期 |
| `searchReserveFraction` | `int` | `10` | 搜索结果预分配：预留 `总行数 / N` 的容量 |
| `searchReserveMax` | `size_t` | `10000` | 搜索结果预分配上限 |
| `jsonIndent` | `int` | `2` | 会话 JSON 文件的缩进空格数 |

### JSON Schema 示例

```json
{
  "uiOverheadRows":        6,
  "dialogMaxWidth":        60,
  "defaultTerminalWidth":  80,
  "watcherTickCount":      50,
  "watcherTickIntervalMs": 10,
  "searchReserveFraction": 10,
  "searchReserveMax":      10000,
  "jsonIndent":            2
}
```

### API

```cpp
// 读取全局配置（只读引用）
const AppConfig& cfg = AppConfig::global();

// 程序启动时初始化（main() 首行调用）
AppConfig::loadGlobal();                     // 默认路径
AppConfig::loadGlobal("/path/to/cfg.json");  // 自定义路径

// 实例方法（测试用，不影响全局单例）
AppConfig cfg;
bool ok = cfg.loadFromFile("/path/to/cfg.json");
```

---

## 版本管理

### 版本号单一来源

版本号的唯一来源是根 `CMakeLists.txt` 的 `project()` 声明：

```cmake
project(ttLogViewer VERSION X.Y.Z LANGUAGES CXX)
```

CMake 在构建时通过 `configure_file(cmake/version.hpp.in ...)` 自动生成
`${CMAKE_BINARY_DIR}/include/version.hpp`，包含以下宏：

```cpp
#define TTLOGVIEWER_VERSION       "X.Y.Z"
#define TTLOGVIEWER_VERSION_MAJOR X
#define TTLOGVIEWER_VERSION_MINOR Y
#define TTLOGVIEWER_VERSION_PATCH Z
```

应用代码 `#include "version.hpp"` 即可使用，**不需要手动维护版本字符串**。

### UI 版本显示

按 `h` 键打开帮助对话框时，对话框标题格式为：

```
ttLogViewer vX.Y.Z  帮助
```

快捷键列表本身不含版本信息，版本仅显示在标题行。

### 发版手动同步清单

CMakeLists.txt 的 VERSION 字段变更后，以下位置需手动同步（详见 CLAUDE.md 发版流程）：

| 位置 | 内容 |
|------|------|
| `README.md` | "开发状态"行的版本号和测试数量 |
| `implementation.md` | 顶部版本号和最后更新日期 |
| git tag | `git tag vX.Y.Z` |
| GitHub Release | `gh release create vX.Y.Z ...` |

以下位置**自动联动**，无需手动修改：
- 编译进二进制的版本号（通过 version.hpp 宏）
- 帮助对话框标题中的版本号
