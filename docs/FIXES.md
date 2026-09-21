# 问题分析与修复记录

本文记录 2026-09 对 ad-planner 仓库的一次完整自查与修复。所有结论都有可复现的
证据，所有修复都有对应测试。

---

## 1. 表达式解析错误：目标函数被静默截断

### 现象

`learning_log.txt` 中，投资规划示例（总资金 500 万，三档收益率 8% / 12% / 9%）
的最终目标函数值记录为 **0.08**。

这道题的正确最优收益应当是 **60 万**（即 60 万元）。把 0.08 当作"最大收益"
显然与题目规模不符。

### 根因

`solver/solve.py` 原来的 `parse_expr()` 用一条正则逐段匹配表达式：

```python
term_regex = r'([+-]?\d*\.?\d*)\s*\*?\s*([a-zA-Z_]\w*)|([+-]?\d+\.?\d*)'
```

当 LLM 输出目标函数 `0.08*2*a + 0.12*5*b + 0.09*3*c` 时，正则的**第一条命中**
并不是 `2*a` 这样的变量项，而是第三分支匹配到的裸数字 `0.08`：

```
表达式: 0.08*2*a + 0.12*5*b + 0.09*3*c
  #1 span=(0, 4)   groups=(None, None, '0.08')  text='0.08'   ← 第一条就命中裸数字
  #2 span=(5, 8)   groups=('2', 'a', None)      text='2*a'
  #3 span=(11, 15) groups=(None, None, '0.12')  text='0.12'
  #4 span=(16, 19) groups=('5', 'b', None)      text='5*b'
  #5 span=(22, 26) groups=(None, None, '0.09')  text='0.09'
  #6 span=(27, 30) groups=('3', 'c', None)      text='3*c'
```

而代码里处理裸数字的分支是**直接 return**：

```python
if number is not None and var_name is None:
    return float(number)      # ← 拿着 0.08 就返回了
```

于是累计到一半的结果被丢弃，目标函数退化成常数 0.08。求解器"成功"地把
一个常数最大化，报告 0.08 并标记 `success`。

> 这是一个典型的**静默错误**：没有抛异常、没有报错，只是答案错了。

### 修复

把正则替换成基于 `ast` 的安全解析：

1. 用 `ast.parse(expr, mode="eval")` 得到语法树；
2. 做白名单校验——只允许 `Constant`（数字）、`Name`（变量）、
   `BinOp`（`+ - * /`）、`UnaryOp`（一元正负号）；
3. 在语法树上按线性代数规则求值，累加成 `{变量: 系数} + 常数项`。

新实现同时修掉了几个附带问题：

| 旧行为 | 新行为 |
| --- | --- |
| 裸数字直接 `return`，丢弃已累计项 | 正确累加常数项 |
| `eval(rhs_str, {"__builtins__": {}})` 执行 LLM 文本 | 完全不用 `eval`，无代码注入面 |
| 未声明变量抛 `ValueError` → traceback 打到 stdout → C++ 只看到 `ERROR` | 返回结构化 `{"status":"ERROR","message":"..."}`，可直接反馈给 LLM 重试 |
| 不支持括号、一元负号处理脆弱 | 括号、一元正负号、`x/2`、科学计数法均正确 |
| 变量×变量 会被算成错误结果 | 明确拒绝并说明"仅支持线性模型" |

### 验证

`tests/test_parser.py` 第 1 节就是这条回归测试：

```
[1] 回归：混合系数的目标函数不再被截断
  [PASS] a 的系数 = 0.16
  [PASS] b 的系数 = 0.6
  [PASS] c 的系数 = 0.27
  [PASS] 常数项为 0
```

`tests/test_known_cases.py` 案例 3 用的是**日志里 LLM 原样输出的字符串**
（`0.08*2*a + 0.12*5*b + 0.09*3*c`），现在结果是：

```
求解器结果: 目标值 = 60.0, 解 = {'a': 0.0, 'b': 100.0, 'c': 0.0}
暴力枚举:   目标值 = 60.0, 解 = {'a': 0,   'b': 100,   'c': 0}
期望值:     目标值 = 60.0, 解 = {'a': 0.0, 'b': 100.0, 'c': 0.0}
```

60 万正好对应"500 万全部投入收益率最高的 B 类（12%）"这一直觉结论，
并且由独立的暴力枚举交叉验证通过。

### 附带发现：每条日志记录开头那一行莫名的 "n"

历史日志里每一成功记录都长这样：

```
Status: success
Attempts: 1
LLM Response: n
{"v":["x:甲产品数量","y:乙产品数量"],"c":["2*x + 1*y <= 12","3*x + 1*y <= 18"],"o":"3*x + 5*y","t":"maximize"}
```

那个孤零零的 `n` 来自 `main.cpp` 里剥代码块围栏时的**差一错误**：

```cpp
size_t b1 = content.find("```json");
if (b1 != std::string::npos) {
    size_t start = b1 + 6;    // ← "```json" 是 7 个字符，不是 6
```

`b1 + 6` 正好指在 `json` 的 `n` 上，于是模型字符串的开头永远是 `n`。
它没造成解析失败（后续查找关键字时会跳过），但属于确凿的偏差。
已改为 `b1 + 7`，并顺带补上 `\r` 与"没有 `json` 标注的裸 ```` ``` ```` 围栏"
两种情况。修复后同一场景输出的第一行是：

```
LLM 返回的模型 JSON: {"v":["x:甲产品数量","y":乙产品数量"],"c":["2*x + 1*y <= 12","3*x + 1*y <= 18"],"o":"3*x + 5*y","t":"maximize"}
```

---

## 2. 解验证器形同虚设

### 现象

README 曾宣称项目具备"求解结果验证"，但 `src/verifier.cpp` 的实现是：

```cpp
bool Verifier::verify(const std::vector<std::string>& variables,
                      const std::vector<std::string>& constraints,
                      const std::vector<double>& values,
                      double tolerance) {
    (void)variables;
    (void)constraints;                       // ← 约束被显式忽略
    if (values.size() != variables.size()) return false;
    for (double v : values) {
        if (std::isnan(v) || std::isinf(v)) return false;
    }
    return true;
}
```

它只检查了"解向量长度对不对、有没有 NaN/Inf"，**完全没有把解代入约束**。
一个明显违反约束的解（例如 (0, 200, 0)，超出资金约束）也会被判为"验证通过"。
`verify_integer()` 虽然写得正确，但 `main.cpp` 里从未调用。

### 修复

新增 `src/linear_expr.h/.cpp`（线性表达式解析），并把 `Verifier` 重写为
真正的四步验证链：

```
1. 结构检查    变量齐全、取值有限
2. 整数性检查  integer_vars 的取值必须是整数
3. 约束检查    把解代入每一条约束，逐条判断；记录违约约束的序号
4. 目标值检查  用解重算目标函数，与求解器报告的值比对
```

第 3、4 步是关键：第 3 步能抓住"不可行却被当成可行"，第 4 步能兜住第 1 节
那类"目标函数被静默截断"的错误——如果报告值 0.08 与代入解重算的 60 不一致，
验证会立刻失败并把原因回填给 LLM 重试。

`main.cpp` 的失败分支也一并改正：现在把求解层给出的**可读原因**（如
"变量 z 未声明"）作为反馈喂给 LLM，而不是原来那句无信息的"求解失败: ERROR"。

### 验证

`tests/test_linear_expr.cpp` 第 5 节：

```
[PASS] 正确解 (0,100,0) 通过
[PASS] 重算目标值 = 60
[PASS] 不可行解 (0,200,0) 被拒绝        ← 修复前会被误判为通过
[PASS] 指出了违约约束序号 1
[PASS] 目标值被截断成 0.08 时被拒绝      ← 第 1 节那类错误的兜底
[PASS] 非整数解被拒绝
[PASS] 缺少变量取值被拒绝
```

---

## 3. README 中未经证实的描述

| 原描述 | 实际情况 | 处理 |
| --- | --- | --- |
| "全链路自动化，无需人工干预" | 流程确实自动跑通，但正确性无保障 | 改为"自动跑通流程"，并新增"已知限制"一节 |
| "自演化能力" | 只是失败后把错误信息回填重试（最多 3 次），没有模型学习或能力提升 | 改为"基于错误反馈的重试机制"，并说明它**不是**自演化 |
| "求解结果验证" | `verify()` 没检查约束 | 先修实现，再保留该描述（现在名副其实） |
| "多智能体协作（LLM Agent + 求解器 Agent）" | 实际是"一个 LLM + 一个求解器"的工具调用，不是多智能体 | 改为"LLM 建模 + 求解器求解的工具调用流程" |
| "零依赖外部库：纯 C++ 标准库 + curl（HTTP）" | C++ 侧没有用 curl，HTTP 是 Python `requests` 发的 | 改为如实描述：C++ 只用标准库 + `popen`；HTTP 由 Python 侧完成 |
| 项目结构图未包含 `agent/`、`solver/ip_solver.py`、`run.bat`、`src/ip_solver.cpp` | 仓库里实际存在两套代码 | 结构图补全，并在"仓库构成"一节说明两套代码的关系 |

---

## 4. 跨平台缺陷（Windows 上根本跑不起来）

| 位置 | 问题 | 修复 |
| --- | --- | --- |
| `src/solver_call.cpp` | 临时文件写死 `/tmp/ip_input.json`，Windows 没有 `/tmp` | 改用 `TMPDIR`/`TEMP`/`TMP` 环境变量定位临时目录 |
| `src/learning_log.cpp` | `#include <sys/time.h>` + `gettimeofday()`，POSIX 专有 | 改用 C++17 `std::chrono` |
| `src/llm_client.cpp` / `src/main.cpp` | 默认解释器写死 `python3` | Windows 下默认 `python` |
| `run.bat` | 调用 `src\ip_solver`，但 Makefile 从未构建过这个二进制 | Makefile 新增 `ip_solver` 目标，`run.bat` 改为调用根目录的可执行文件 |
| `Makefile` | 未包含 `linear_expr.cpp`；无 `ip_solver` 目标；无测试目标 | 全部补齐，并新增 `make test` |
| `run.bat` | 只读 `SILICONFLOW_KEY`，与 README 里的 `SILICONFLOW_API_KEY` 不一致 | 统一为同时接受两个变量名 |

### 4.1 更深一层的问题：中文输入在 Windows 上会被转成非法 UTF-8

这一条是在给项目补端到端集成测试时才暴露出来的，值得单独说明。

`main(int argc, char* argv[])` 拿到的 `argv` 是 C 运行库按**当前 ANSI 代码页**
转换来的。在中文 Windows 上，一长串中文问题会被转成 **GBK 字节**。
而程序随后把这些字节按 UTF-8 编码进 JSON 发给 LLM ——
请求体里出现非法 UTF-8，接口直接拒绝。

这正好解释了仓库历史日志里**第一条**失败记录：

```
Status: failed
Error: LLM HTTP 错误: {"code":20015,"message":"The parameter is invalid. Please check again.","data":null}
Attempts: 1
```

当时只看到"参数无效"，很容易误以为是提示词写错了，其实是编码问题。

**修复**：在 Windows 上改从 `GetCommandLineW()` + `CommandLineToArgvW()` 重新取
参数并转成 UTF-8，同时用 `SetConsoleOutputCP(CP_UTF8)` 把控制台输出代码页设为
UTF-8；再给编译器加上 `-finput-charset=UTF-8 -fexec-charset=UTF-8`
（MinGW / clang 在中文 Windows 上默认会把字面量转成 GBK）。

顺带还踩到 cmd.exe 的一个历史怪癖：当 `/c` 后面的命令以引号开头时，它会先
剥掉首尾两个引号，于是 `"python" "solver/llm_call.py" ...` 被理解成程序名
`python" "solver/llm_call.py"`，报
`'python" "solver' is not recognized as an internal or external command`。
规避办法是在最外层再套一对引号（见 `src/shell_command.h`）。

### 4.2 仍然存在的限制

如果显式传入的 Python 解释器路径本身含非 ASCII 字符（例如把仓库放在
`C:\用户\...` 下并指定该目录里的解释器），窄字符 `system()` / `_popen()`
传给 cmd.exe 的路径会被破坏。目前的推荐做法是让 `python` 在 PATH 上即可
（默认行为），需要彻底解决则要改用宽字符版本的进程创建接口。

---

## 5. 敏感信息与运行日志

### 扫描结论

对全部 20 个文件做了密钥扫描（`sk-` 前缀、`api_key=`、`token=`、`password`、
`secret`、`Bearer <长串>`），**没有发现任何硬编码的真实密钥或 Token**。
唯一命中的是 `README.md` 里的占位符 `your_api_key`。

仓库只有一次提交（"初始化"），不存在"提交过密钥后又删除"的历史泄漏隐患。

### 但仍有三处风险，已处理

1. **`learning_log.txt` 被提交进了公开仓库**，里面包含完整的原始 LLM 响应
   （含接口返回的 `id`、token 用量统计）以及真实的题目文本。
   → 已从仓库移除，加入 `.gitignore`，并提供脱敏示例
   `examples/learning_log.sample.txt`。

2. **API Key 通过命令行参数传给 Python 脚本**：

   ```cpp
   std::string cmd = "python3 solver/llm_call.py \"" + api_url_ + "\" \""
                     + api_key_ + "\" " + sys_b64 + " " + user_b64;
   ```

   这样 Key 会出现在进程列表中（`ps` / 任务管理器都看得到），Key 里若含引号
   还会破坏命令。另外这两处 `popen()` / `system()` 都是把字符串直接拼进 shell，
   属于命令注入面。

   → 改为通过环境变量 `ADPLANNER_LLM_API_KEY` 传递，命令行上不再出现密钥；
   同时给路径加了引号包裹，并对写回 JSON 的错误文本做了转义。

3. **日志会落原始 LLM 响应全文**。
   → `LearningLog` 现在只记录"提取后的模型 + 结论"，不再写原始响应；
   并新增 `ADPLANNER_LOG=off` 环境变量可完全关闭日志。

### 建议你自己再确认一遍

```bash
# 万一以后本地跑过再提交，用这条兜一下
git log -p | grep -iE "sk-[A-Za-z0-9]{16,}|api[_-]?key|token" | head
```

---

## 6. 仓库构成：两套代码并存的问题

仓库里其实有**两个互不相干的项目**：

| 目录 / 文件 | 属于哪个项目 | 说明 |
| --- | --- | --- |
| `src/main.cpp`, `src/llm_client.cpp`, `src/model_parser.cpp`, `src/solver_call.cpp`, `src/verifier.cpp`, `src/learning_log.cpp`, `src/linear_expr.*` | **通用 NLP→整数规划 Agent** | 自然语言建模，OR-Tools 求解，与具体案例无关 |
| `solver/solve.py`, `solver/llm_call.py` | 同上（求解层 / LLM 调用层） | |
| `src/ip_solver.cpp`, `solver/ip_solver.py`, `agent/agent.py`, `run.bat` | **广告投放规划案例** | 暴力枚举求解"优格麦片"广告投放问题，与上面的通用 Agent 没有代码关系 |

两套代码没有互相调用。原 README 只描述了第一套，导致打开仓库的人会困惑：
入口到底是 `./ad-planner` 还是 `run.bat`？

本次处理方式是**在 README 中如实说明两套代码各自是什么**，而不是强行合并。
更彻底的做法是拆成两个仓库（通用 Agent 是简历里的主要作品，广告案例作为
课程/竞赛作品单独放），后续可以再做。

---

## 7. 测试结果汇总

| 测试 | 命令 | 结果 |
| --- | --- | --- |
| C++ 线性解析 + 解验证 | `make test-cpp` | 37 / 37 通过 |
| Python 表达式解析 | `python tests/test_parser.py` | 31 / 31 通过 |
| Python 端到端已知答案 | `python tests/test_known_cases.py` | 5 / 5 通过（求解器 × 手工期望值 × 暴力枚举 三方一致） |
| 端到端集成（真实 C++ 链路） | `python tests/test_agent_pipeline.py` | 4 / 4 场景通过 |

完整的真实运行输出见 `examples/demo_transcript.txt`。

---

## 8. 本次改动文件清单

**新增**

- `src/linear_expr.h` / `src/linear_expr.cpp` — 线性表达式解析（C++ 侧验证器的基础）
- `src/shell_command.h` — 规避 cmd.exe 引号怪癖的小工具
- `tests/test_linear_expr.cpp` — C++ 核心测试
- `tests/test_parser.py` — Python 解析单元测试
- `tests/test_known_cases.py` — 5 个已知答案的端到端测试
- `tests/test_agent_pipeline.py` + `tests/stub_llm_call.py` — 真实 C++ 链路的集成测试
- `tests/make_demo_transcript.py` — 生成 `examples/demo_transcript.txt`
- `.gitignore`、`.env.example`
- `docs/FIXES.md`（本文）、`docs/architecture.svg`
- `examples/learning_log.sample.txt`、`examples/demo_transcript.txt`

**重写**

- `solver/solve.py` — 解析层整体重构
- `src/verifier.h` / `src/verifier.cpp` — 真正的四步验证
- `src/main.cpp` — 接入真实验证、修正重试反馈
- `src/solver_call.cpp` — 跨平台临时文件、错误原因回传
- `src/llm_client.cpp` — 密钥改走环境变量
- `src/learning_log.cpp` — 跨平台时间戳、不再落原始响应
- `solver/llm_call.py` — 密钥改走环境变量、HTTP 错误返回非零退出码
- `Makefile`、`README.md`

**删除**

- `learning_log.txt`（移出公开仓库）
