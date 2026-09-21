# ad-planner 构建脚本
#
#   make            编译 Agent 主程序与广告案例求解器
#   make test       跑全部测试（C++ 核心测试 + Python 测试）
#   make test-cpp   只跑 C++ 核心测试
#   make test-py    只跑 Python 测试
#   make clean      清理编译产物
#
# Windows 上如果没有 make，可以直接用 README「快速开始」里的单行 g++ 命令。

CXX      ?= g++
# -finput-charset / -fexec-charset：源码与运行时都用 UTF-8。
# MinGW / clang 在中文 Windows 上默认把字面量转成 GBK，会导致终端输出乱码，
# 这里显式固定为 UTF-8。用 MSVC 时请把这两个参数换成 /utf-8。
CXXFLAGS ?= -std=c++17 -Wall -O2 -finput-charset=UTF-8 -fexec-charset=UTF-8
PYTHON   ?= python

SRC_DIR  = src
TEST_DIR = tests

ifeq ($(OS),Windows_NT)
EXE = .exe
endif

# Agent 主程序：自然语言 -> 模型 -> 求解 -> 验证
AGENT_SRC = \
	$(SRC_DIR)/main.cpp \
	$(SRC_DIR)/llm_client.cpp \
	$(SRC_DIR)/model_parser.cpp \
	$(SRC_DIR)/solver_call.cpp \
	$(SRC_DIR)/verifier.cpp \
	$(SRC_DIR)/learning_log.cpp \
	$(SRC_DIR)/linear_expr.cpp

AGENT_OBJ = $(AGENT_SRC:.cpp=.o)
AGENT     = ad-planner$(EXE)

# 广告投放案例（独立入口，run.bat 会调用它）
LEGACY    = ip_solver$(EXE)

# C++ 核心测试：线性表达式解析 + 解验证
CORE_TEST = test_core$(EXE)
CORE_SRC  = $(TEST_DIR)/test_linear_expr.cpp $(SRC_DIR)/linear_expr.cpp $(SRC_DIR)/verifier.cpp

.PHONY: all clean test test-cpp test-py test-e2e example

all: $(AGENT) $(LEGACY)

$(AGENT): $(AGENT_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(AGENT_OBJ)

$(LEGACY): $(SRC_DIR)/ip_solver.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

$(CORE_TEST): $(CORE_SRC)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -o $@ $(CORE_SRC)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: test-cpp test-py test-e2e
	@echo ""
	@echo "==== 全部测试通过 ===="

test-cpp: $(CORE_TEST)
	@echo ""
	@echo "==== C++ 核心测试（线性表达式解析 + 解验证）===="
	./$(CORE_TEST)

test-py:
	@echo ""
	@echo "==== Python 表达式解析测试 ===="
	$(PYTHON) $(TEST_DIR)/test_parser.py
	@echo ""
	@echo "==== Python 端到端已知答案测试 ===="
	$(PYTHON) $(TEST_DIR)/test_known_cases.py

# 需要先编译出 ad-planner：把 LLM 网络调用换成桩，跑通真实 C++ 链路
test-e2e: $(AGENT)
	@echo ""
	@echo "==== 端到端集成测试（LLM 调用替换为桩）===="
	$(PYTHON) $(TEST_DIR)/test_agent_pipeline.py

# 不需要 API Key 的离线演示：直接跑求解层
example:
	$(PYTHON) $(TEST_DIR)/test_known_cases.py

clean:
	rm -f $(AGENT_OBJ) $(SRC_DIR)/*.o
	rm -f $(AGENT) $(LEGACY) $(CORE_TEST)
