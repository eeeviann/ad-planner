CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2

SRC = src/main.cpp src/llm_client.cpp src/model_parser.cpp src/solver_call.cpp src/verifier.cpp src/learning_log.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = ad-planner

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) -o $@ $(OBJ)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
