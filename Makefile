CXX      ?= g++
CXXFLAGS ?= -std=c++17 -g -O0 -Wall -Wextra -Wpedantic -Isrc
CXXFLAGS += -MMD -MP
LDFLAGS  ?=

SRC := $(wildcard src/*.cpp)
OBJ := $(patsubst src/%.cpp,build/%.o,$(SRC))
DEP := $(OBJ:.o=.d)
BIN := build/mini-ls

I10 ?= $(HOME)/eda/abc/i10.aig

.PHONY: all clean run

all: $(BIN)

$(BIN): $(OBJ) | build
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LDFLAGS)

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build:
	mkdir -p build

run: $(BIN)
	$(BIN) $(I10) -c balance

clean:
	rm -rf build

-include $(DEP)
