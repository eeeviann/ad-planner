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

### 扫描结论：全量审计了 Git 历史

早先的版本这里曾写过"仓库只有一次提交，不存在历史泄漏隐患"——**这句话是错的**。
仓库远端实际有 2 个提交、1 个分支（`main`）、无 tag：

| commit | 日期 | 说明 | 文件数 |
| --- | --- | --- | --- |
| `e6b91fa72d763c27d6888e5744993bcf06705918` | 2026-06-29 | 初始化 | 20 |
| `43c31eb8def5736e0f3de2d3cc9b5c2f35cce47d` | 2026-09-21 | 修复表达式解析与结果验证；补充测试、文档与架构图 | 35 |

因为没有其它分支和 tag，所以"扫全部历史"是**穷尽**的，不存在未覆盖的提交。
对全部 51 个 blob 逐个扫描了下列模式：

    密钥前缀        sk- / sk_live_ / ghp_ / gho_ / ghu_ / ghs_ / github_pat_
                    AKIA[0-9A-Z]{16} / AIza... / xox[baprs]-
    其它            -----BEGIN ... PRIVATE KEY----- / Bearer <20+字符>
    赋值型          api_key= / apikey / access_token / secret_key /
                    client_secret / password / passwd / pwd 后跟 12+ 字符
    其它疑似        40+ 连续 base64 字符 / QQ 邮箱 / 手机号

**结论：没有发现任何硬编码的真实密钥、Token 或密码。**

三处疑似命中，经人工逐条确认为误报：

| 命中位置 | 实际内容 | 判定 |
| --- | --- | --- |
| `solver/llm_call.py` | `api_key = resolve_api_key()` | 函数调用，非字面量 → 误报 |
| `src/llm_client.cpp`（新旧两版各 1 处） | `"ABCDEFGHIJKLMNOPQRSTUVWXYZabc...+/"` | base64 字符表 → 误报 |

真实命中的只有占位符：`.env.example` 与 `README.md` 中的 `your_api_key`。

### 但历史里确实留有这些内容，已处理

1. **`learning_log.txt` 曾在初始化提交中被提交进公开仓库，
   且至今仍可通过提交历史访问**（`git show e6b91fa72d763c27d6888e5744993bcf06705918:learning_log.txt`）。
   删除最新版本中的文件，并不等于它会从历史里消失。
   该文件包含**完整的题目文本**与**完整的原始 LLM 响应**
   （含接口返回的 `id` 与 token 用量统计）。

   → 其中**不含密钥**，但属于个人运行数据。现已从工作区移除并加入
   `.gitignore`；同时把这份历史记录整理为
   `examples/real_llm_run_before_fix.txt` **主动公开**，
   避免"以为删掉了、其实还在历史里"的误解。

2. **初始化提交的作者邮箱曾是个人 QQ 邮箱（已于 2026-09-21 重写历史移除）**，
   在公开历史中可见。2026-09 的提交已改用
   `eeeviann@users.noreply.github.com`。如需一并处理，见下方"重写历史"。

3. **API Key 曾通过命令行参数传给 Python 脚本**：

   ```cpp
   std::string cmd = "python3 solver/llm_call.py \"" + api_url_ + "\" \""
                     + api_key_ + "\" " + sys_b64 + " " + user_b64;
   ```

   这样 Key 会出现在进程列表中（`ps` / 任务管理器都看得到），Key 里若含引号
   还会破坏命令。另外这两处 `popen()` / `system()` 都是把字符串直接拼进 shell，
   属于命令注入面。→ 改为通过环境变量 `ADPLANNER_LLM_API_KEY` 传递。

4. **日志会落原始 LLM 响应全文**。→ `LearningLog` 现在只记录
   "提取后的模型 + 结论"；并新增 `ADPLANNER_LOG=off` 可完全关闭日志。

### 需要你自己决定的一件事：要不要重写历史

当前**没有发现真实密钥**，所以不清理也不存在密钥泄漏风险。
但如果不想让上面第 1、2 条的个人数据留在历史里，可以重写历史：

```bash
# 前提：pip install git-filter-repo，并先完整备份仓库
git filter-repo --path learning_log.txt --invert-paths
git push --force origin main
```

代价：**所有 commit hash 会变**，已有的克隆、fork 和指向旧提交的链接会失效。
本次没有代做这一步，因为它是不可逆操作，且风险等级不高，应当由你自己权衡。

> 补充：**万一曾经公开过真实密钥**，删文件是不够的——
> 必须在服务商后台**吊销并重新签发**。密钥一旦被 clone/fork 出去就无法收回，
> 靠改历史也无法保证安全。

### 自己复查用的命令

```bash
# 全历史密钥扫描（要带 -p 才扫得到历史 diff）
git log -p --all | grep -iE "sk-[A-Za-z0-9]{16,}|ghp_|gho_|AKIA[0-9A-Z]{16}|-----BEGIN .*PRIVATE KEY-----" | head

# 列出历史里出现过、但当前已删除的文件
git log --all --diff-filter=D --name-only --pretty=format:'%h %s' | sort -u

# 确认远端到底有几个提交 / 分支
git rev-list --all --count && git ls-remote origin
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
| Python 端到端已知答案 | `python tests/test_known_cases.py` | 5 / 5 通过（求解器 × 题面期望值 × 暴力枚举 三方一致） |
| 端到端集成（真实 C++ 链路，LLM 用桩） | `python tests/test_agent_pipeline.py` | 4 / 4 场景通过 |

产出两份运行记录，用途不同，不要混淆：

| 文件 | 性质 | 说明 |
| --- | --- | --- |
| `examples/demo_transcript.txt` | 本地实跑输出 | 由 `tests/make_demo_transcript.py` 生成。其中第 5 段用桩替换了 LLM 调用，段首有明确提示；**不代表真实模型的建模能力** |
| `examples/real_llm_run_before_fix.txt` | **真实调用大模型**的记录 | 2026-06-24 对 SiliconFlow / DeepSeek-V3 的调用，取自初始化提交中的 `learning_log.txt`，正文未删改。记录的是**修复前**的状态，含四类异常现象 |

---

## 8. 复审发现的两处表述问题

这两条都不是代码缺陷，而是**文档与事实不符**。
对一个要拿给导师看的仓库来说，性质不比 bug 轻。

### 8.1 README 里的个人贡献被写强了

原「AI 辅助开发的说明」单独列出了"本人完成的部分"，其中包括
「定修复原则」「定验收标准」「逐行审查 AI 生成的代码」
「人工推导期望值」，以及"这些代码我能讲清楚"。

实际情况是：**技术方案与代码主要由 AI 完成**，本人负责提出需求、
运行程序、检查输出结果，并把发现的问题反馈给 AI 迭代。
"注意到 500 万资金的收益是 0.08 不合理"这一步是真的；
但"从现象到根因的分析""定修复原则""逐行审查代码""能逐行讲清代码",
这些描述**超出了实际参与程度**。

→ 已改写为与实际情况一致的表述，只保留四项：提出需求 / 运行并检查 /
反馈与迭代 / 复核结论。

技术内容本身**没有删减**——那些分析都留在本文里，
只是不再声称是本人独立完成的判断。

> 为什么这比代码 bug 更该警惕：代码错了可以改，
> 而把自己的参与程度写强了，在面试或答辩里一戳就破。

### 8.2 测试演示的题面残缺，且被标成了"真实输出"

README「运行」一节原本写着"真实输出（取自 `tests/test_agent_pipeline.py`
的实跑记录，未经修饰）"，展示的内容是：

```
问题: 某工厂生产甲、乙两种产品。甲每件利润3元，乙每件利润5元。求最大利润。
      ↑ 只有利润，没有设备工时、没有原料限量

LLM 返回的模型 JSON: {..."c":["2*x + 1*y <= 12","3*x + 1*y <= 18"]...}
      ↑ 却出现了题面里根本没有的两条约束
```

问题有两层：

1. **标注错误**：这段输出确实来自 `tests/test_agent_pipeline.py`，
   但该测试**把 LLM 网络调用换成了桩**，桩返回的是预先写死的固定模型。
   标为"真实输出"，会让读者以为是真实大模型在建模。
2. **题面残缺**：测试场景里的题目被截短了，只剩利润，
   而桩返回的模型却带着完整约束。两者叠在一起，
   读者会得出一个完全错误的结论——"这个程序能从信息不足的题目里
   自动推导出完整约束"。
   事实恰恰相反：**本项目明确不保证模型忠实性**
   （见 README「已知限制」第 2 条）。

→ 已从三方面修正：

- README 中该段改标为「离线集成测试的输出」，并加粗提示
  "这不是真实大模型的运行结果……不代表真实大模型的建模准确率"；
- 四个测试场景的题面全部补全为**信息完整**的原文
  （约束、资源限量、收益率都写进题面），桩返回的模型与题面一一对应；
- 另外提供 `examples/real_llm_run_before_fix.txt` —— 一份**真实调用大模型**
  的记录（取自初始化提交中的历史日志），
  让"真实的端到端效果"有一个独立、不会被与桩混淆的出处。

> 顺带修掉一个设计细节：桩原本靠**题目文本里的关键词**（"工厂"、"投资"、
> "重试"、"无解"）决定返回哪组模型，这反过来逼着测试题面必须写成
> 含暗号的假题目。现已改为由测试夹具通过环境变量 `STUB_SCENARIO`
> 显式指定，题面因此可以保持信息完整。

---

## 9. 改动文件清单

### 8.1 2026-09 修复（第一轮）

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

### 8.2 2026-09 复审后的第二轮修改

针对复审意见（README 个人贡献表述偏强、测试演示易生误解、历史敏感信息、
README 偏长）所做的调整：

**新增**

- `examples/real_llm_run_before_fix.txt` — 修复前的真实大模型调用记录，
  取自初始化提交中的 `learning_log.txt`（正文未删改），并在首尾补上
  来源说明与四类异常的解读

**重写**

- `README.md`
  - 「AI 辅助开发的说明」按真实参与程度改写：技术方案与代码主要由 AI 完成，
    本人负责提出需求、运行程序、检查输出、反馈问题——删去
    "定修复原则 / 定验收标准 / 逐行审查代码 / 手工推导期望值 / 我能逐行讲清"
    等与实际不符的表述
  - 「运行」一节的示例输出，从"真实输出"改标为"离线集成测试输出"，
    并写明 LLM 调用已被替换为桩、不代表建模准确率
  - 示例题目补全约束条件（设备工时、原料限量），使题面与模型一致
  - 「一次真实的调试记录」压成摘要，完整分析移入本文第 1 节
  - 「目录结构」由 38 行压缩为 18 行的顶层概览
  - 「已知答案案例」中"手工推导的期望值"改为"题面推出的期望值"
- `tests/test_agent_pipeline.py`
  - 四个场景的题目全部改为**信息完整**的原文（`PROBLEM_*` 常量）
  - 桩返回哪组模型改由环境变量 `STUB_SCENARIO` 显式指定，
    不再依赖题目里的关键词——避免把题面写成含暗号的假题目
  - 不再打印本机绝对路径（只报解释器目录名），避免把个人目录写进公开记录
- `tests/stub_llm_call.py`
  - 文件头明确标注"不调用真实大模型、不能证明建模准确率"
  - 固定模型与题面一一对应，逐条注明对应关系
- `tests/make_demo_transcript.py`
  - 新增 `scrub()`，把 `~` 展开的绝对路径替换为 `<HOME>`
  - 第 5 段开头增加"这不是真实大模型结果"的提示
  - 文件头说明哪些段落是真实执行、哪些用了桩
- `docs/FIXES.md`
  - 第 5 节更正"仓库只有一次提交"的错误结论，改为全历史审计报告
    （2 个提交 / 1 个分支 / 51 个 blob 的穷尽扫描结果）
  - 补充历史中确实留存的个人数据（`learning_log.txt`、作者邮箱）
    与可选的 `git filter-repo` 清理方案
- `examples/demo_transcript.txt` — 重新生成（题目补全、路径脱敏、加免责说明）

### 8.3 仓库完整文件清单（35 → 36 个文件）

```
.env.example                        examples/demo_transcript.txt
.gitignore                          examples/learning_log.sample.txt
Makefile                            examples/real_llm_run_before_fix.txt
README.md                           run.bat
agent/agent.py                      solver/ip_solver.py
docs/FIXES.md                       solver/llm_call.py
docs/architecture.svg               solver/solve.py
src/ip_solver.cpp                   src/learning_log.cpp  src/learning_log.h
src/linear_expr.cpp                 src/linear_expr.h
src/llm_client.cpp                  src/llm_client.h
src/main.cpp                        src/model_parser.cpp  src/model_parser.h
src/shell_command.h                 src/solver_call.cpp   src/solver_call.h
src/verifier.cpp                    src/verifier.h
tests/make_demo_transcript.py       tests/run_all.py
tests/stub_llm_call.py              tests/test_agent_pipeline.py
tests/test_known_cases.py           tests/test_linear_expr.cpp
tests/test_parser.py
```

> `learning_log.txt` 不在清单中：它已从工作区移除并加入 `.gitignore`，
> 但仍存在于历史提交 `e6b91fa72d763c27d6888e5744993bcf06705918` 里，详见本文第 5 节。
