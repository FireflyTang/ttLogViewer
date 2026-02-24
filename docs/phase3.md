# 阶段三：辅助功能 + 会话持久化 + 完善

## 目标

完成全部剩余功能，覆盖所有边界场景测试，确保程序在极端条件下稳定运行。阶段结束即为功能完整的 v1.0 可发布版本。

**交付价值**：会话自动恢复（下次启动无需重新配置过滤器）、导出过滤结果、折叠超长行、完整的边界场景测试覆盖。

---

## 范围

### 本阶段实现

**辅助功能键**

- `l`（行号显示切换）：
  - `AppController` 维护 `showLineNumbers_` 布尔状态，按 `l` 翻转
  - `ViewData.LogLine.rawLineNo` 已在阶段一传入；渲染层根据 `showLineNumbers` 字段决定是否渲染行号列
  - 测试：切换后 StatusBar / LogPane buffer 中行号出现/消失

- `z`（折叠超长行）：
  - `AppController` 维护 `foldedLines_`（`std::unordered_set<size_t>`，存放 rawLineNo）
  - 按 `z` → 当前高亮行的 rawLineNo 在 foldedLines_ 中 toggle
  - `ViewData.LogLine` 增加 `bool folded` 字段
  - 渲染层：folded=true 时将行内容截断到终端宽度，末尾显示 `…` 标记
  - 测试：同一行反复 z 状态正确切换；其他行不受影响；折叠行宽度不超出终端

- `h`（帮助弹窗）：
  - `AppController` 按 `h` → 设 `showDialog_`，title="快捷键帮助"，body=完整快捷键列表（中文），dialogHasChoice=false
  - 任意键关闭
  - 测试：弹窗出现；body 包含所有快捷键；任意键关闭后 inputMode=None

- `w`（导出过滤结果）：
  - `AppController` 按 `w` → 进入 `ExportConfirm` InputMode
  - 自动生成导出文件名（`<原始文件名>_filtered_<时间戳>.txt`），bottom 显示完整路径
  - `Enter`：写出 filteredPane 所有行（调用 `filterChain_.filteredLines()` + `reader_.getLine()` 逐行写入）
  - `Esc`：取消
  - 写出成功后底部短暂显示 "已保存到 <path>"（可用 Dialog 实现）
  - 无过滤器时导出全量原始行
  - 测试：导出内容与 filteredPane 行内容一致；Esc 取消不创建文件；目标目录不存在时报错

**会话自动保存/加载**

- 启动时：从 `last_session.json` 加载过滤器链（路径见 design.md 持久化章节）；若文件不存在或解析失败，以空链路启动
- 退出时：`FilterChain::save()` 写出当前过滤器链 + 上次打开的文件路径 + 工作模式
- 阶段二的 `save()` / `load()` 已实现，本阶段在 `main.cpp` 中接线：
  ```cpp
  // 启动
  filterChain.load(sessionPath);
  if (!lastFile.empty()) reader.open(lastFile);

  // 退出（q 键 / FTXUI 析构前）
  filterChain.save(sessionPath);
  ```
- 测试（`tests/e2e/session_test.cpp`）：
  - 增删 filter → 退出 → 重启 → filterCount、pattern、color、enabled、exclude 与保存前一致
  - 上次文件路径恢复
  - 模式恢复（Static / Realtime）
  - 损坏的 last_session.json → 以空链路启动，不 crash
  - 不存在的 last_session.json → 正常启动

**边界场景与错误处理完善**

以下场景在阶段一/二中已有桩或部分实现，本阶段补全测试并修复发现的问题：

*LogReader 边界*
- `close()` 时 IndexThread 仍在运行 → 安全 join，不 hang
- 文件被删除（`stat()` 失败）→ 触发 `onFileReset`，不 crash
- `forceCheck()` 在未 `open()` 时调用 → 忽略，不 crash
- 静态模式下 `forceCheck()` 仍能手动触发一次检查

*FilterChain 边界*
- `reprocess()` 期间调用 `append` / `edit` / `remove` / `reset` → 取消当前 reprocess，重新开始
- `save()` 路径的父目录不存在 → 自动创建，或返回错误并提示用户

*AppController 边界*
- `getViewData()` 双区均空（未打开文件）→ 不 crash
- `onTerminalResize()` → scrollOffset 重新校正，cursor 仍在可见区域
- InputMode 切换不串状态：多种模式进出后 buffer 清空、inputValid 复位

*渲染边界*
- `renderColoredLine()` 覆盖全行的单 span
- 状态栏文件名超长时截断显示
- StatusBar 行数千位分隔格式（如 "1,234,567 行"）

**大文件性能验证**

非自动化，手工操作并记录结果：
- 用 1GB 日志文件（可用脚本生成）测试：启动后界面立即可用（索引进行中）、滚动流畅、过滤器重算进度条显示
- 100 个过滤器时，reprocess 速度可接受（< 30 秒对 1GB 文件）
- 无明显内存泄漏（Valgrind 基本测试）

### 本阶段补全的测试

**`tests/unit/log_reader_test.cpp` 补充**
- `close()` 时 IndexThread 仍运行 → 安全退出
- 文件被删除 → onFileReset 触发
- `forceCheck()` 未 open → 不 crash
- 静态模式 `forceCheck()` 手动触发

**`tests/unit/filter_chain_test.cpp` 补充**
- `reprocess()` 期间调用各变更操作 → 取消重启
- `save()` 父目录不存在 → 行为明确

**`tests/unit/app_controller_test.cpp` 补充**
- `getViewData()` 双区空 → 不 crash
- `onTerminalResize()` → cursor 在可见区
- InputMode 切换不串状态

**`tests/unit/render_utils_test.cpp`（新增）**
- `renderColoredLine()`：0 span / 1 span / 多 span / 覆盖全行 / 空内容
- `renderColoredLine()` CJK 内容：含中文字符 + 颜色段，hbox 宽度正确
- `isUtf8Boundary()`：ASCII / 中文首字节 / 中文续字节 / pos==size

**`tests/render/` 补充**
- `LogPane`：超长折叠行显示截断标记；行号显示/隐藏切换
- `StatusBar`：文件名超长截断；千位分隔行数格式

**`tests/e2e/session_test.cpp`（新建）**
- 完整会话保存/恢复流程（见上文）

**`tests/e2e/input_flow_test.cpp` 补充**
- `w` 导出完整流程（Enter 确认 / Esc 取消）

**`tests/e2e/navigation_test.cpp` 补充**
- `l` 切换行号显示
- `z` 折叠/展开超长行
- `h` 帮助弹窗

---

## 文件清单

### 新建文件

```
tests/
├── unit/
│   └── render_utils_test.cpp
└── e2e/
    └── session_test.cpp
```

### 修改文件

```
src/main.cpp                        ← 接入 session save/load
src/app_controller.cpp              ← 实现 l / z / h / w 键处理
src/render.cpp                      ← 行号列渲染、折叠截断标记
include/app_controller.hpp          ← ViewData.LogLine 增加 folded 字段（若阶段二未加）
tests/unit/log_reader_test.cpp      ← 补充边界用例
tests/unit/filter_chain_test.cpp    ← 补充边界用例
tests/unit/app_controller_test.cpp  ← 补充边界用例
tests/render/log_pane_test.cpp      ← 补充折叠行、行号切换用例
tests/render/status_bar_test.cpp    ← 补充截断、千位分隔用例
tests/e2e/input_flow_test.cpp       ← 补充 w 导出流程
tests/e2e/navigation_test.cpp       ← 补充 l/z/h 用例
tests/CMakeLists.txt                ← 新增测试目标
```

---

## 实现要点

### 行号显示列的渲染

行号显示不影响 LogLine 内容，只影响渲染时是否在 `▶ content` 前插入行号：

```cpp
// 渲染层（render.cpp）
Element renderLogLine(const LogLine& line, bool showLineNumbers) {
    Elements parts;
    if (showLineNumbers)
        parts.push_back(text(std::to_string(line.rawLineNo) + " ") | dim);
    parts.push_back(line.highlighted ? text("▶ ") : text("  "));
    parts.push_back(renderColoredLine(line.content, line.colors));
    return hbox(std::move(parts));
}
```

`showLineNumbers` 从 `ViewData` 传入（AppController 维护状态，getViewData 返回）。

### 折叠超长行的实现

折叠截断在渲染层执行（不修改原始 string_view）：

```cpp
Element renderColoredLine(std::string_view content,
                          const std::vector<ColorSpan>& spans,
                          bool folded,
                          int terminalWidth) {
    if (folded) {
        // 按字节截断到 terminalWidth - 2（预留 "…" 2 列）
        // 注意：截断点必须在合法 UTF-8 边界（用 isUtf8Boundary 校验）
        auto truncated = truncateToWidth(content, terminalWidth - 2);
        return hbox({text(std::string(truncated)), text("…") | dim});
    }
    // 正常 ColorSpan 渲染
    // ...
}
```

`ViewData.LogLine` 增加 `bool folded` 字段；`AppController` 在 `getViewData()` 中根据 `foldedLines_` 填充。

### 会话文件路径

```cpp
std::string sessionPath() {
#ifdef _WIN32
    return std::string(getenv("APPDATA")) + "/ttlogviewer/last_session.json";
#else
    const char* xdg = getenv("XDG_CONFIG_HOME");
    std::string base = xdg ? xdg : (std::string(getenv("HOME")) + "/.config");
    return base + "/ttlogviewer/last_session.json";
#endif
}
```

目录不存在时 `save()` 自动创建（`std::filesystem::create_directories`）。

### 导出文件名生成

```cpp
std::string exportPath(std::string_view filePath) {
    namespace fs = std::filesystem;
    auto stem = fs::path(filePath).stem().string();
    auto now = std::chrono::system_clock::now();
    auto ts = std::format("{:%Y%m%d_%H%M%S}", now);
    return stem + "_filtered_" + ts + ".txt";
}
```

导出与原始文件同目录，或使用当前工作目录（可讨论）。

---

## 测试清单

（对应 design.md 测试方案中标注"阶段三"的所有条目，以下为补充明细）

### render_utils_test.cpp

| 测试用例 | 断言 |
|---------|------|
| `renderColoredLine()` 无 span | 返回 1 个 text 片段，内容完整 |
| `renderColoredLine()` 1 span（中间段） | 返回 3 片段（前/中/后），颜色正确 |
| `renderColoredLine()` 多 span | 片段数 = span 数 + 间隔数 |
| `renderColoredLine()` span 覆盖全行 | 返回 1 个着色片段 |
| `renderColoredLine()` 空内容 | 不 crash，返回空或 1 个空 text |
| `renderColoredLine()` CJK + span | hbox 宽度 = span 字节边界对应的列宽，无错位 |
| `isUtf8Boundary()` ASCII 字节 | 返回 true |
| `isUtf8Boundary()` 中文首字节 | 返回 true |
| `isUtf8Boundary()` 中文续字节（0x80–0xBF） | 返回 false |
| `isUtf8Boundary()` pos == size | 返回 true |

### session_test.cpp

| 场景 | 断言 |
|------|------|
| 增删 filter 后 save → load | filterCount、pattern、color、enabled、exclude 完全一致 |
| 无 filter 的空链路 save → load | load 后 filterCount = 0 |
| 上次文件路径恢复 | load 后 lastFile 字段正确 |
| 模式恢复 | Static/Realtime 模式标志正确恢复 |
| 损坏 JSON → load | 返回 false，不 crash，链路保持初始状态 |
| 不存在的 session 文件 → load | 返回 false，不 crash |
| `save()` 目标目录不存在 | 自动创建目录，save 成功 |

---

## 完成标准（Definition of Done）

1. **编译无警告**：`-Wall -Wextra` 全部通过
2. **测试全绿**：全部三阶段所有测试（unit + render + e2e）均通过
3. **功能验证**（手工测试）：
   - `l` 切换行号显示/隐藏
   - `z` 折叠超长行，显示 `…` 截断标记
   - `h` 显示完整快捷键帮助弹窗，任意键关闭
   - `w` 导出，确认文件内容与过滤区一致
   - 退出后重新启动，过滤器链自动恢复
4. **功能完整性核查**：对照 design.md「键盘交互」表格，每个按键均已实现且行为符合设计
5. **大文件手工测试**：1GB 文件下启动 / 滚动 / 过滤操作无明显卡顿

---

## 依赖关系

- 依赖阶段二：完整的 LogReader（含后台线程）、FilterChain（含 save/load）、AppController（含所有 InputMode 框架）
- 本阶段不引入新的架构变动，只填充剩余功能和测试

---

## 完整功能覆盖矩阵

下表为对照设计文档的全功能核查，确保三个阶段合计覆盖所有功能：

| 功能 | 阶段 |
|------|------|
| mmap 文件读取 + 行索引 | 一 |
| 静态文件浏览（↑↓/PgUp/PgDn/Home/End） | 一 |
| 双窗格独立滚动（Shift） | 一 |
| q 退出 | 一 |
| 状态栏（文件名/模式/行数） | 一 |
| 虚拟列表渲染 | 一 |
| 后台行索引（IndexThread）+ isIndexing | 二 |
| 实时模式（FileWatcher）+ r/s/f | 二 |
| 文件追加通知（onNewLines） | 二 |
| 文件重写弹窗（onFileReset）+ Y/N | 二 |
| G 锁定跟随末尾 | 二 |
| g 行号跳转（GotoLine mode） | 二 |
| 过滤器增删改（a/e/d） | 二 |
| 过滤器选择（[ / ]） | 二 |
| 过滤器顺序调整（+ / -） | 二 |
| 过滤器启停（Space） | 二 |
| 过滤器颜色（Tab 预览）| 二 |
| 过滤器正则信号灯 | 二 |
| 链式过滤结果（FilterChain） | 二 |
| 颜色高亮渲染（ColorSpan） | 二 |
| 阶段缓存 + reprocess 后台线程 | 二 |
| 进度条（ProgressOverlay） | 二 |
| 全文搜索（SearchThread）/ n / p / Esc | 二 |
| 文件切换（o） | 二 |
| JSON 过滤链 save/load | 二 |
| l 行号显示切换 | 三 |
| z 折叠超长行 | 三 |
| h 帮助弹窗 | 三 |
| w 导出过滤结果 | 三 |
| 会话自动保存（退出时）| 三 |
| 会话自动恢复（启动时）| 三 |
| 全部边界场景测试 | 三 |
