# 设计文档审查笔记

## 需要澄清的问题

### 1. Tab 键行为（优先级：低）
**现状**：Tab 在不同 InputMode 下有不同功能，无冲突
- None 模式：切换焦点区域
- FilterAdd/FilterEdit 模式：预览颜色

**建议**：文档中明确说明即可，无需修改

### 2. 接口签名变更（优先级：高）
**问题**：阶段一声称"接口锁定"，但阶段二改变 AppController 构造函数签名

**方案 A**：阶段一就使用抽象接口
```cpp
// phase1 就定义 ILogReader, IFilterChain
// AppController 构造函数从一开始就用接口
AppController(ILogReader& reader, IFilterChain& chain);
```

**方案 B**：承认这是唯一例外
- 在 phase2.md 明确标注为"唯一的接口签名变更"
- 解释原因：为了支持 Mock 测试

**推荐**：方案 B（更简单，避免阶段一过度设计）

### 3. 无参数启动行为（优先级：中）
**问题**：未明确无文件参数时的启动行为

**建议**：phase1.md 补充
- 无参数启动 → 显示 "请使用 o 键打开文件" 提示
- 或直接显示空白界面，状态栏显示 "No file"

### 4. nlohmann/json 引入时机（优先级：中）
**问题**：CLAUDE.md 说阶段二引入，phase1.md CMakeLists.txt 要配置所有依赖

**建议**：
- phase1.md：CMakeLists.txt 只配置 FTXUI 和 GoogleTest
- phase2.md：增加 nlohmann/json 的 FetchContent 配置
- 这样更符合"渐进式"原则

### 5. examples/ 目录内容（优先级：低）
**建议**：phase1.md 增加创建示例文件
```
examples/
├── simple.log      # 10 行简单日志
├── with_errors.log # 包含 ERROR/WARN 的日志
├── chinese.log     # 含中文内容
└── large.log       # 10000 行（用脚本生成）
```

## 不需要修改的部分

- ✅ mmap 技术方案可行
- ✅ 线程模型设计合理
- ✅ 测试方案基本完整（可在实现时补充细节）
- ✅ 三阶段划分合理
- ✅ 功能覆盖完整

## 总体评估

设计文档质量很高，只有少量细节需要澄清。主要问题是接口签名变更的说明，建议在实现阶段一之前决定采用哪种方案。