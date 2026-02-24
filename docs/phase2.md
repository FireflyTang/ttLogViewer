# 阶段二：过滤链 + 搜索 + 实时监控

## 目标

在阶段一的框架基础上，实现 ttLogViewer 的核心价值功能：正则过滤链、全文搜索、实时文件追踪。阶段结束时，程序具备日常使用所需的全部主要功能。

**交付价值**：可对超大日志文件进行链式正则过滤、关键词搜索、实时追踪新增内容——这是与 `tail -f | grep` 的核心差异。

---

## 范围

### 本阶段实现

**LogReader 后台线程补全**

- `IndexThread`：`open()` 后立即启动后台线程扫描行索引；`lineCount_` 改为 `std::atomic<size_t>`（release/acquire 语义），UI 线程实时可读已扫描行数
- `isIndexing()`：返回真实状态（索引线程运行期间为 true）
- 索引完成后通过 `PostEvent` 通知 UI 线程（StatusBar "索引中..." 提示消失）
- `FileWatcher` 线程（实时模式）：500ms 轮询 `stat()`，检测到变化后获取 `checkMutex_` 再调用 `doCheck()`
- `forceCheck()`：获取 `checkMutex_` 后同步执行 `doCheck()`，与 FileWatcher 线程互斥
- `doCheck()` 内部：
  - 文件变大 → 读新增部分，追加索引，触发 `onNewLines` 回调（经 `PostEvent` 在 UI 线程调用）
  - 文件缩小 / inode 变更 → 触发 `onFileReset` 回调（经 `PostEvent` 在 UI 线程调用）
- `MmapRegion` 改为 `shared_ptr<MmapRegion>` 管理，供 SearchThread 持有引用

**FilterChain 完整实现**

- `FilterNode.output` 阶段缓存完整实现：`filters_[k].output` 存经 0..k 过滤后存活的行号（1-based）
- `append()` / `edit()` / `moveUp()` / `moveDown()`：修改 filters_ 后调用 `reprocess(fromFilter, ...)`
- `remove()`：删除节点（output 缓存随之释放），调用 `reprocess(index, ...)`
- `processNewLines(firstLine, lastLine)`：在 UI 线程增量追加到各阶段末尾（含 `isReprocessing_` 检查，若重算中则缓存到 `pendingNewLines_`）
- `reprocess(fromFilter, onProgress, onDone)`：
  - 设 `cancelFlag_ = true`，join 上次线程（若有）
  - 重置 `cancelFlag_ = false`，`isReprocessing_ = true`
  - 启动 `ReprocessThread`：操作本地 `vector<FilterNode> local_filters`，定期检查 `cancelFlag_`
  - 完成后：mutex swap `filters_`，处理 `pendingNewLines_`，PostEvent，调用 `onDone`
- `computeColors(rawLineNo, content)`：遍历所有启用 filter，对 content 执行正则匹配，返回 `ColorSpan` 列表（后 filter 颜色覆盖前 filter）
- 正则无效（`std::regex` 抛出）：`append()` / `edit()` 返回错误，不修改 filters_
- `save(path)` / `load(path)`：完整 JSON 序列化（见 design.md 数据格式）；`load()` 失败时返回 false，不修改当前状态

**AppController 全功能实现**

*过滤器操作（FilterAdd / FilterEdit InputMode）*
- `a`：进入 `FilterAdd` 模式，底部显示 `Pattern> _  ●`
  - 逐字符更新 `inputBuffer_`，实时编译正则，`inputValid_` 驱动信号灯
  - `Tab`：进入颜色预览子状态（在调色板中循环，FilterBar 实时预览）
  - `Enter`（有效）：调用 `filterChain_.append()`，退出模式，清空 buffer
  - `Enter`（无效）：弹出编译错误 Dialog，返回 FilterAdd 继续编辑
  - `Esc`：取消，退出 FilterAdd 模式
- `[` / `]`：选中上/下一个过滤器（`selectedFilter_` 索引循环）
- `e`：进入 `FilterEdit` 模式，buffer 预填当前 filter pattern
  - 流程与 FilterAdd 相同，`Enter` 调用 `filterChain_.edit()`
- `d`：删除当前选中过滤器（调用 `filterChain_.remove()`）
- `+` / `-`：调用 `filterChain_.moveUp()` / `moveDown()`
- `Space`：切换当前 filter 的 `enabled`（调用 `filterChain_.edit()` 仅改 enabled）

*搜索（Search InputMode）*
- `/`：进入 `Search` 模式，底部显示 `Search> _`
- `Enter`：启动 `SearchThread`
  - SearchThread 持有 `shared_ptr<MmapRegion>`，调用 `searchLines(keyword, mmap)` 纯函数
  - 完成后 PostEvent，UI 线程设置 `searchResults_`、跳转到第一个结果
  - 搜索中显示 "搜索中..." 提示
- `Esc`：退出 Search 模式，清空 searchResults_
- `n` / `p`：跳转到下一个/上一个搜索结果（searchIndex_ 循环）；无结果时忽略

*行号跳转（GotoLine InputMode）*
- `g`（`isIndexing()` 为 false 时）：进入 `GotoLine` 模式，底部显示 `Goto: _`
- `Enter`：解析行号，跳转（若被过滤，跳最近可见行）；超出范围跳末行
- `Esc`：取消

*文件操作*
- `o`：进入 `OpenFile` 模式，底部显示 `Open: _`
  - `Enter`：`reader_.open(path)`，成功则 `filterChain_.reset()`，失败弹错误 Dialog
  - `Esc`：取消
- `s` / `r`：切换 `reader_.setMode()`，ViewData.mode 变化，FileWatcher 随之启停
- `f`：调用 `reader_.forceCheck()`

*G 锁定跟随*
- `G`（实时模式下）：设 `followTail_ = true`，跳到末行
- `G`（静态模式下）：忽略
- `onNewLines` 回调中：
  - `followTail_` 为 true → 自动将 cursor 和 scrollOffset 跟随到末行
  - `followTail_` 为 false → `newLineCount_` 累加，ViewData.newLineCount 显示提示
- 任意导航键（↑↓PgUp/PgDn/Home/End）：`followTail_ = false`

*输入状态中文件追加*
- `inputMode_ != None` 时，`onNewLines` 触发 → 只累加 `newLineCount_`，不刷新 cursor

*文件重置弹窗*
- `onFileReset` 回调：设 `showDialog_`，弹出 Y/N 弹窗
- 用户选 Y → `reader_.open(filePath)`（重新加载）、`filterChain_.reset()`、reprocess 从头
- 用户选 N → 继续浏览当前（已缓存）内容

*进度条*
- `reprocess()` 的 `onProgress` 回调：设 `progress_`，ViewData.showProgress = true
- 进度到 1.0 时隐藏进度条

**渲染层补全**

- `InputLine`：
  - FilterAdd / FilterEdit 模式：`Pattern> buffer  ●`（● 颜色由 `inputValid_` 驱动）
  - Search 模式：`Search> buffer`
  - GotoLine 模式：`Goto: buffer`
  - OpenFile 模式：`Open: buffer`
  - ExportConfirm 模式（阶段三实现）：占位
- `FilterBar`：选中过滤器高亮；disabled 过滤器视觉区分（灰色/删除线）；`●` 颜色方块
- `DialogOverlay`：title + body；Y/N 选项（`dialogHasChoice=true` 时）；任意键关闭
- `ProgressOverlay`：进度条宽度随 `progress` 线性变化；`showProgress=false` 时不渲染

**抽象接口（供 Mock 测试）**

```cpp
// include/i_log_reader.hpp
class ILogReader {
public:
    virtual ~ILogReader() = default;
    virtual bool open(std::string_view path) = 0;
    virtual void close() = 0;
    virtual std::string_view getLine(size_t lineNo) const = 0;
    virtual size_t lineCount() const = 0;
    virtual bool   isIndexing() const = 0;
    // ... 完整签名与 LogReader 一致
};

// include/i_filter_chain.hpp
class IFilterChain {
public:
    virtual ~IFilterChain() = default;
    // ... 完整签名与 FilterChain 一致
};
```

- `LogReader` 继承 `ILogReader`，`FilterChain` 继承 `IFilterChain`
- `AppController` 构造函数改为接受 `ILogReader&` / `IFilterChain&`（接口引用），便于注入 Mock

**测试（本阶段新增，全部通过）**
- `tests/helpers/mock_log_reader.hpp`：Google Mock MockLogReader
- `tests/helpers/mock_filter_chain.hpp`：Google Mock MockFilterChain
- `tests/helpers/test_utils.hpp`：填充 `waitForIndexing()` / `waitForReprocess()` 实现
- `tests/unit/filter_chain_test.cpp`：FilterChain 单元测试
- `tests/unit/app_controller_test.cpp`：扩充过滤/搜索/实时相关用例
- `tests/render/`：InputLine、FilterBar、DialogOverlay、ProgressOverlay、StatusBar 渲染测试
- `tests/e2e/file_open_test.cpp`
- `tests/e2e/filter_workflow_test.cpp`
- `tests/e2e/realtime_test.cpp`
- `tests/e2e/navigation_test.cpp`（补全 g/G 用例）
- `tests/e2e/search_test.cpp`
- `tests/e2e/input_flow_test.cpp`（a/e/o/g 完整输入流程）

### 本阶段不实现

- `w` 导出
- `z` 折叠超长行
- `l` 行号显示切换
- `h` 帮助弹窗
- JSON 会话自动保存/加载（启动时恢复过滤器链）
- `tests/e2e/session_test.cpp`

---

## 文件清单

### 新建文件

```
include/
├── i_log_reader.hpp            ← 新增抽象接口
└── i_filter_chain.hpp          ← 新增抽象接口
tests/
├── helpers/
│   ├── mock_log_reader.hpp
│   └── mock_filter_chain.hpp
├── unit/
│   └── filter_chain_test.cpp
├── render/
│   ├── status_bar_test.cpp
│   ├── filter_bar_test.cpp
│   ├── input_line_test.cpp
│   └── dialog_overlay_test.cpp
└── e2e/
    ├── file_open_test.cpp
    ├── filter_workflow_test.cpp
    ├── realtime_test.cpp
    ├── navigation_test.cpp
    ├── search_test.cpp
    └── input_flow_test.cpp
```

### 修改文件

```
include/log_reader.hpp          ← 继承 ILogReader
include/filter_chain.hpp        ← 继承 IFilterChain
include/app_controller.hpp      ← 构造函数参数改为接口引用
src/log_reader.cpp              ← 补全 IndexThread、FileWatcher、MmapRegion shared_ptr
src/filter_chain.cpp            ← 完整实现（替换所有桩）
src/app_controller.cpp          ← 补全所有 InputMode 处理
src/render.cpp                  ← 补全 InputLine、FilterBar、DialogOverlay、ProgressOverlay
tests/helpers/test_utils.hpp    ← 填充 waitForIndexing/waitForReprocess 实现
tests/unit/app_controller_test.cpp ← 扩充测试用例
tests/CMakeLists.txt            ← 新增测试目标
```

---

## 实现要点

### AppController 构造函数接口变更

阶段一的 `AppController(LogReader&, FilterChain&)` 改为 `AppController(ILogReader&, IFilterChain&)`，使测试可注入 Mock。这是本阶段唯一的接口**签名变更**（其余方法签名不变）。

### SearchThread 与 mmap 安全

```cpp
// AppController 内部
void startSearch(std::string keyword) {
    cancelSearch_.store(true);
    if (searchThread_.joinable()) searchThread_.join();
    cancelSearch_.store(false);

    auto region = reader_.mmapRegion();  // shared_ptr<MmapRegion>，增加引用计数
    searchThread_ = std::thread([=, this] {
        auto results = searchLines(keyword, region, cancelSearch_);
        if (!cancelSearch_.load())
            screen_.PostEvent(SearchDoneEvent{std::move(results)});
        // region shared_ptr 在此处析构，引用计数减一
    });
}
```

`searchLines()` 是可单元测试的纯函数：

```cpp
std::vector<size_t> searchLines(
    std::string_view keyword,
    const std::shared_ptr<MmapRegion>& region,
    const std::atomic<bool>& cancel);
```

### reprocess + processNewLines 并发安全

```cpp
// FilterChain 内部（均在 UI 线程访问，无锁）
bool                        isReprocessing_ = false;
std::vector<std::pair<size_t,size_t>> pendingNewLines_;  // (first, last)

void processNewLines(size_t firstLine, size_t lastLine) {
    if (isReprocessing_) {
        pendingNewLines_.push_back({firstLine, lastLine});
        return;
    }
    processNewLinesImpl(firstLine, lastLine);  // 实际追加
}

// reprocess 完成的 PostEvent 回调（UI 线程）
void onReprocessDone(vector<FilterNode> newFilters) {
    filters_ = std::move(newFilters);
    isReprocessing_ = false;
    for (auto [f, l] : pendingNewLines_)
        processNewLinesImpl(f, l);
    pendingNewLines_.clear();
    // 刷新视图
}
```

`isReprocessing_` 和 `pendingNewLines_` 都是 UI 线程私有变量，无需 atomic/mutex。

### 文件变更检测稳健性

`doCheck()` 应记录上次已处理到的文件大小，防止重复处理：

```cpp
void doCheck() {
    // 获取 checkMutex_ 保证互斥（见线程分析）
    auto st = stat(path_);
    if (st.size > processedSize_) {
        // 追加：索引新增行，触发 onNewLines
        processedSize_ = st.size;
    } else if (st.size < processedSize_ || st.inode != inode_) {
        // 重写：触发 onFileReset
    }
}
```

---

## 测试清单

以下为本阶段新增测试，引用 design.md 测试方案中对应条目。

### FilterChain 单元测试（对照 design.md FilterChain 测试表）

**基础过滤行为**（12 项）
- 无过滤器 → 全量通过
- 所有 filter disabled → 全量通过；部分 disabled
- include filter：无匹配、全部匹配、正常匹配
- exclude filter：无匹配（结果不变）、全部匹配（结果为空）
- 链式：多 include + exclude 混用；首 filter 为 exclude

**过滤器变更**（8 项）
- `moveUp` / `moveDown` 后结果变化；首个上移无效；末个下移无效
- `remove()` 后缓存清除：删中间/首/尾节点
- `edit()` 传入相同值仍触发 reprocess

**增量追加**（4 项）
- `processNewLines()` 追加 0/1/多行
- `reprocess()` 期间 `processNewLines()` → pendingNewLines_ 缓冲正确，新增行不丢失
- 多次快速 `reprocess()` → 只有最后一次结果生效

**颜色计算**（5 项）
- `computeColors()`：无匹配/全行匹配/多处匹配
- 颜色叠加：后 filter 覆盖前 filter；同行多 filter 各自着色不同段
- 含中文行的颜色 span 字节偏移在合法 UTF-8 边界

**边界与错误**（8 项）
- `filteredLineAt(N)` N >= filteredLineCount
- `filterAt(N)` N >= filterCount
- `filteredLines(from, count)` 末尾部分范围
- 颜色调色板耗尽 → 循环复用不 crash
- 无效正则 → `append()` / `edit()` 返回错误，filters_ 不变
- 空字符串 pattern → 明确行为（拒绝或匹配所有行）
- `reset()` 后 filteredLineCount = 全量行数

**并发读取**（1 项）
- `reprocess()` 期间读 `filteredLineAt()` → 不崩溃，返回旧数据或新数据（不返回混合数据）

**JSON 持久化**（5 项）
- save/load 往返一致：空链路、多 filter、含特殊字符 pattern
- version 字段正确
- `load()` 异常输入：schema 不匹配、version 未知、文件不存在
- `save()` 目标目录不存在 → 创建目录或返回错误

### AppController 补充测试

**过滤器操作**（10 项，对照 design.md AppController 测试表）
- `a` 完整流程：输入正则 → 信号灯绿 → Enter 保存；Esc 取消
- `a` 无效正则：信号灯红；Enter → 弹出编译错误；filter 不创建
- `a` → Tab 预览颜色
- `e` 修改 pattern → Enter 保存；Esc 取消，filter 保持原值
- `e` 修改为无效正则 → 弹窗，filter 不更新
- `[` / `]` 到达首/末后循环；无 filter 时无效果
- `d` 删最后一个；编号重排连续
- `+` / `-` 首个上移无效；末个下移无效
- `Space` 切换 enabled；结果区即时变化
- `s` / `r` 切换模式；静态模式 `G` 无效

**搜索**（6 项）
- 无结果：n/p 无响应或提示
- 单结果：n/p 停在同一行
- 多结果：n 循环到首；p 循环到末
- Esc 退出，高亮清除
- 含中文关键词正确匹配
- 搜索含被过滤行：原始区高亮，过滤区不跳

**实时相关**（7 项）
- G 锁定后新行自动滚末尾（追加 1/100 行）
- G 在静态模式下忽略
- 导航键取消 G 锁定
- 非锁定时新行只增 newLineCount，cursor 不动
- onNewLines 在输入状态中 → 缓存不刷新
- `g` 完整流程：逐字输入 → Enter 跳转；Esc 取消；isIndexing 时禁用
- `g` 跳转到被过滤行 → 最近可见行；超出范围 → 末行

**文件操作**（4 项）
- `o` 有效路径 → 加载；无效路径 → 错误提示；Esc 取消
- 文件重写弹窗：选 Y → 重载（缓存清空）；选 N → 继续浏览
- `f` 强制检查

### 渲染层新增测试

| 子组件 | 测试用例（对照 design.md 渲染层测试表） |
|--------|---------------------------------------|
| StatusBar | 索引中提示；+N 行提示；千位分隔格式 |
| FilterBar | 选中 filter 高亮；disabled 视觉区分；0 filter 不 crash |
| InputLine | 各 InputMode prompt 正确；信号灯绿/红；颜色预览状态 |
| DialogOverlay | title+body 出现；Y/N 仅在 dialogHasChoice=true 时出现 |
| ProgressOverlay | 0%/50%/100% 进度条宽度；showProgress=false 不显示 |

### E2E 测试

| 文件 | 关键场景 |
|------|---------|
| file_open_test.cpp | 空文件、含中文内容、超长单行；`o` 切换文件；无效路径错误提示 |
| filter_workflow_test.cpp | include/exclude filter；链式过滤；moveUp/moveDown 结果变化；disable/enable；无效正则弹窗 |
| realtime_test.cpp | 文件追加新行出现；G 锁定跟随；文件重写弹窗（Y/N）；f 强制重载 |
| navigation_test.cpp | g 跳转被过滤行；G 锁定解锁；两区独立滚动 |
| search_test.cpp | 无结果/单结果/多结果循环；被过滤行的搜索跳转；Esc 退出 |
| input_flow_test.cpp | a/e/o/g 完整键盘输入流程（含 Tab 颜色预览、错误弹窗） |

---

## 完成标准（Definition of Done）

1. **编译无警告**：以 `-Wall -Wextra` 通过
2. **测试全绿**：本阶段全部新增测试 + 阶段一测试均通过
3. **功能验证**（手工测试）：
   - 添加正则过滤器，过滤区正确显示匹配行，颜色高亮正确
   - 链式叠加多个 filter，顺序调整后结果变化
   - 实时模式下追加内容到文件，新行出现在原始区和过滤区
   - `G` 锁定追踪末行；导航键解锁
   - `/` 搜索关键词，`n`/`p` 跳转
   - 文件被重写后弹窗正确出现，Y/N 行为正确
4. **线程安全**：Valgrind / ThreadSanitizer 无 data race（在 Linux 上运行，Windows 可选）

---

## 依赖关系

- 依赖阶段一：所有头文件接口、框架代码、mmap 实现
- 阶段三依赖本阶段：完整的过滤链 + AppController 键盘框架
