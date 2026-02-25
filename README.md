# ttLogViewer

一个用 C++ 编写的快速高效的终端日志文件查看器，专为查看和过滤超大日志文件设计。

## 特性

- **大文件支持**：内存映射（mmap）+ 行索引，O(1) 跳转任意行，无需完整加载
- **链式过滤**：多个正则过滤器顺序叠加，每层缓存中间结果，修改后层无需重算前层
- **实时追踪**：实时模式监控文件追加，`G` 键锁定跟随最新行
- **颜色标记**：每个过滤器对应独立颜色，匹配内容高亮显示
- **双区视图**：上半显示原始日志，下半显示过滤结果，两区独立滚动
- **会话持久化**：过滤链 + 上次打开文件自动保存，下次启动自动恢复
- **搜索**：关键字搜索，`n`/`p` 前后跳转，匹配行高亮
- **行号 & 折叠**：`l` 切换行号，`z` 折叠/展开超长行
- **导出**：`w` 将当前过滤结果导出为文件
- **用户配置**：`~/.ttlogviewer.json` 可覆盖 UI 布局、搜索、Watcher 等参数

## 使用方法

```bash
ttlogviewer [文件路径]
```

进入后按 `h` 显示所有快捷键帮助。

## 构建

### 环境要求

- C++23 兼容编译器（GCC 13+、Clang 16+、MSVC 2022+）
- CMake 3.15+
- Ninja（推荐）

依赖项由 CMake FetchContent 自动下载，无需手动安装：
- [FTXUI v6.1.9](https://github.com/ArthurSonzogni/FTXUI)（TUI 框架）
- [nlohmann/json](https://github.com/nlohmann/json)（配置持久化）

### 构建步骤

**Linux / macOS**

```bash
cmake -B build -G Ninja
cmake --build build
```

**Windows（MSYS2 MinGW64）**

```bash
cmake -B build -G Ninja \
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe
/c/msys64/mingw64/bin/ninja.exe -C build
```

编译产物：`build/bin/ttLogViewer`（Windows 为 `ttLogViewer.exe`）

## 快捷键速查

| 键 | 功能 |
|----|------|
| `h` | 显示帮助 |
| `↑` / `↓` | 上下移动光标 |
| `PgUp` / `PgDn` | 翻页 |
| `g` | 跳转到指定行号 |
| `G` | 跳转到末尾（实时模式下锁定跟随） |
| `Tab` | 切换原始 / 过滤 窗格焦点 |
| `a` | 添加过滤器 |
| `e` | 编辑当前过滤器 |
| `d` | 删除当前过滤器 |
| `Space` | 启用 / 禁用当前过滤器 |
| `[` / `]` | 上 / 下选择过滤器 |
| `/` | 搜索关键字 |
| `n` / `p` | 跳转到下 / 上一个搜索结果 |
| `r` | 切换实时模式 |
| `o` | 打开文件 |
| `l` | 切换行号显示 |
| `z` | 折叠 / 展开当前行 |
| `w` | 导出过滤结果 |
| `q` | 退出 |

## 用户配置

启动时从以下路径加载可选 JSON 配置文件（文件不存在时静默跳过）：

- **Windows**：`%USERPROFILE%\.ttlogviewer.json`
- **Linux / macOS**：`$HOME/.ttlogviewer.json`

所有字段均可选，只需覆盖需要修改的值：

```json
{
  "uiOverheadRows":       6,
  "dialogMaxWidth":       60,
  "defaultTerminalWidth": 80,
  "watcherTickCount":     50,
  "watcherTickIntervalMs":10,
  "searchReserveFraction":10,
  "searchReserveMax":     10000,
  "jsonIndent":           2
}
```

## 开发状态

**v0.9.1 — 全部三阶段实现完成**（226 个测试，全部通过）

- 阶段一：框架搭建 + 静态文件浏览 ✓
- 阶段二：过滤链 + 搜索 + 实时监控 ✓
- 阶段三：辅助功能 + 会话持久化 + 配置系统 ✓
- 基础设施：版本管理 + 静态打包 ✓

详细设计文档见 [design.md](design.md)；实现报告见 [implementation.md](implementation.md)。
