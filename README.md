# ttLogViewer

一个用 C++ 编写的快速高效的终端日志文件查看器，专为查看和过滤超大日志文件设计。

## 特性

- **大文件支持**：内存映射（mmap）+ 行索引，O(1) 跳转任意行，无需完整加载
- **链式过滤**：多个正则过滤器顺序叠加，每层缓存中间结果，修改后层无需重算前层
- **实时追踪**：实时模式监控文件追加，`G` 键锁定跟随最新行
- **颜色标记**：每个过滤器对应独立颜色，匹配内容高亮显示
- **双区视图**：上半显示原始日志，下半显示过滤结果，两区独立滚动
- **配置持久化**：过滤链自动保存，下次启动自动恢复

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

## 开发状态

当前阶段：接口设计完成，测试方案设计中。

详细设计文档见 [docs/design.md](docs/design.md)。
