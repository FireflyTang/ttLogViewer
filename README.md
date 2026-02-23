# ttLogViewer

一个用 C++ 编写的快速高效的终端日志文件查看器。

## 特性

- 📖 **大文件支持**: 高效查看和导航大型日志文件
- 🔍 **链式过滤**: 配置级联过滤器，专注于相关的日志条目
- ⚡ **终端界面**: 运行在终端中的轻量级界面
- 🎯 **高性能**: 用 C++ 编写，追求最大速度和效率

## 安装

```bash
# 从源码构建
mkdir build
cd build
cmake ..
make
```

## 使用方法

```bash
# 查看日志文件
ttLogViewer /path/to/logfile.log

# 应用过滤器（示例）
ttLogViewer /path/to/logfile.log --filter "ERROR" --filter "WARNING"
```

## 配置

链式过滤器可以配置为逐步缩小日志条目范围：

```bash
# 示例：只显示特定服务的 ERROR 日志
ttLogViewer app.log -f "service:auth" -f "ERROR"
```

## 构建

### 环境要求

- C++17 或更高版本
- CMake 3.15+
- （根据需要添加其他依赖）

### 构建步骤

```bash
git clone <repository-url>
cd ttLogViewer
mkdir build && cd build
cmake ..
make
```

## 贡献

欢迎贡献代码！请随时提交 Pull Request。

## 许可证

[在此添加您的许可证信息]

## 路线图

- [ ] 基础日志文件查看功能
- [ ] 语法高亮
- [ ] 链式过滤器实现
- [ ] 搜索功能
- [ ] 书签支持
- [ ] 配置文件支持
