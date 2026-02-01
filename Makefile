# Kernel Exploit Visualizer - Makefile
# Phase 1 MVP Build System

CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 -g
CFLAGS += -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS += -fstack-protector-strong -fPIE
LDFLAGS = -pie
LIBS = -lncurses -lpthread

# Lua support (optional)
HAS_LUA := $(shell pkg-config --exists lua5.4 2>/dev/null && echo 1 || echo 0)
ifeq ($(HAS_LUA),1)
    CFLAGS += -DHAS_LUA $(shell pkg-config --cflags lua5.4)
    LIBS += $(shell pkg-config --libs lua5.4)
endif
# Always compile scripting sources (stubs provided when HAS_LUA is not defined)
SCRIPT_SRCS = $(wildcard $(SRC_DIR)/scripting/*.c)

# Directories
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
TEST_DIR = tests

# Source files
CORE_SRCS = $(wildcard $(SRC_DIR)/core/*.c)
INST_SRCS = $(wildcard $(SRC_DIR)/instrumentation/ptrace/*.c)
VIZ_SRCS = $(wildcard $(SRC_DIR)/visualization/*.c)
UI_SRCS = $(wildcard $(SRC_DIR)/ui/*.c)
EXPLOIT_SRCS = $(wildcard $(SRC_DIR)/exploits/api/*.c) $(wildcard $(SRC_DIR)/exploits/examples/*.c)
UTIL_SRCS = $(wildcard $(SRC_DIR)/utils/*.c)
EDUCATION_SRCS = $(wildcard $(SRC_DIR)/education/*.c)

ALL_SRCS = $(CORE_SRCS) $(INST_SRCS) $(VIZ_SRCS) $(UI_SRCS) $(EXPLOIT_SRCS) $(UTIL_SRCS) $(EDUCATION_SRCS) $(SCRIPT_SRCS)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(ALL_SRCS))

# Test sources and objects
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS = $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/tests/%.o,$(TEST_SRCS))
# All app objects except main.o (tests provide their own main)
TEST_APP_OBJS = $(filter-out $(OBJ_DIR)/core/main.o $(OBJ_DIR)/ui/tui.o,$(OBJS))

# Target
TARGET = $(BIN_DIR)/kexploit-viz
TEST_TARGET = $(BIN_DIR)/test-runner

.PHONY: all clean test run debug show-sources help

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJS) $(LDFLAGS) $(LIBS) -o $@
	@echo "Build complete: $@"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c $< -o $@

$(OBJ_DIR)/tests/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c $< -o $@

# Test target: link test files with app objects (excluding main.o and tui.o)
$(TEST_TARGET): $(TEST_OBJS) $(TEST_APP_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(TEST_OBJS) $(TEST_APP_OBJS) $(LDFLAGS) $(LIBS) -o $@
	@echo "Test build complete: $@"

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "Clean complete"

run: $(TARGET)
	./$(TARGET)

# Debug build
debug: CFLAGS += -DDEBUG -O0 -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean all

# Show sources (for debugging Makefile)
show-sources:
	@echo "CORE_SRCS: $(CORE_SRCS)"
	@echo "INST_SRCS: $(INST_SRCS)"
	@echo "VIZ_SRCS: $(VIZ_SRCS)"
	@echo "UI_SRCS: $(UI_SRCS)"
	@echo "EXPLOIT_SRCS: $(EXPLOIT_SRCS)"
	@echo "UTIL_SRCS: $(UTIL_SRCS)"
	@echo "EDUCATION_SRCS: $(EDUCATION_SRCS)"
	@echo "SCRIPT_SRCS: $(SCRIPT_SRCS)"
	@echo "ALL_SRCS: $(ALL_SRCS)"
	@echo "OBJS: $(OBJS)"
	@echo "TEST_SRCS: $(TEST_SRCS)"
	@echo "HAS_LUA: $(HAS_LUA)"

help:
	@echo "Kernel Exploit Visualizer - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build the visualizer"
	@echo "  clean   - Remove build artifacts"
	@echo "  run     - Build and run"
	@echo "  test    - Build and run test suite"
	@echo "  debug   - Build with sanitizers"
	@echo "  help    - Show this help"
