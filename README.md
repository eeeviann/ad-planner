# AdPlanner - 整数规划智能规划 Agent

基于大语言模型的整数规划求解 Agent，支持自然语言输入 → 自动建模 → 求解 → 结果输出全链路自动化。

## 项目结构

```
ad-planner/
├── src/
│   ├── main.cpp           # Agent 主循环（任务分解、LLM调用、重试）
│   ├── llm_client.cpp     # LLM 调用（通过 Python subprocess）
│   ├── model_parser.cpp   # JSON 解析（从 LLM 响应提取模型）
│   ├── solver_call.cpp    # 求解器调用（C++ → Python → ortools）
│   ├── verifier.cpp       # 解验证
│   └── learning_log.cpp   # 学习日志
├── solver/
│   ├── solve.py           # 整数规划求解脚本（ortools）
│   └── llm_call.py        # LLM 调用脚本（requests）
└── README.md
```

## 架构

```
用户输入（自然语言问题）
    ↓
C++ Agent 主循环
    ├→ llm_client.cpp     调用 LLM，解析自然语言 → JSON 模型
    ├→ model_parser.cpp   提取变量、约束、目标函数
    ├→ solver_call.cpp    调用 Python 求解器
    │                        └→ solve.py → ortools → 结果
    ├→ verifier.cpp       验证解的有效性
    ├→ 失败 → 反馈 LLM 重试（最多3次）
    └→ 学习日志记录
    ↓
结果输出
```

**关键设计：**
- C++ 是"大脑"：负责 Agent 逻辑、LLM 调用、策略选择
- Python 是"计算器"：纯求解，无 Agent 逻辑
- 边界清晰，C++ 层可完整独立讲解

## 编译

```bash
make clean && make
```

依赖：
- `g++ -std=c++17`
- `python3` + `ortools` + `requests`

安装 Python 依赖：
```bash
pip install ortools requests
```

## 运行

```bash
export SILICONFLOW_API_KEY="your_api_key"
./ad-planner "你的整数规划问题"
```

示例：
```bash
./ad-planner "某工厂生产甲、乙两种产品。甲每件利润3元，乙每件利润5元。甲每件用设备2小时，乙用1小时，共12小时。甲每件用原料3单位，乙用1单位，共18单位。求最大利润。"
```

## 项目亮点

1. **全链路自动化**：自然语言 → 数学模型 → 求解 → 验证，无需人工干预
2. **自演化能力**：失败自动反馈 LLM 重试，最优解自动记录
3. **分层设计**：C++ 智能层 + Python 计算层，边界清晰可讲解
4. **零依赖外部库**：纯 C++ 标准库 + curl（HTTP），Python 仅依赖 ortools

## 技术栈

- **Agent 层**：C++17（HTTP 调用、JSON 解析、策略控制）
- **LLM**：SiliconFlow API（DeepSeek-V3）
- **求解引擎**：Google OR-Tools（SCIP 求解器）
- **接口**：stdin/stdout JSON 通信

## 对应赛道

**2026 Agentic 时代智算系统超级个体暑期学校 · Track 03**

本项目展示了：
- 多智能体协作（LLM Agent + 求解器 Agent）
- 工具调用（LLM → HTTP → Python 脚本 → ortools）
- 持续进化（失败重试 + 学习日志）
