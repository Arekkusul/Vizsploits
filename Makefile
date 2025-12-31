# Kernel Exploit Visualizer - Makefile
# Phase 1 MVP Build System

CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 -g
CFLAGS += -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS += -fstack-protector-strong -fPIE
LDFLAGS = -pie
LIBS = -lncurses -lpthread

# Directories
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# Source files
CORE_SRCS = $(wildcard $(SRC_DIR)/core/*.c)
INST_SRCS = $(wildcard $(SRC_DIR)/instrumentation/ptrace/*.c)
VIZ_SRCS = $(wildcard $(SRC_DIR)/visualization/*.c)
UI_SRCS = $(wildcard $(SRC_DIR)/ui/*.c)
EXPLOIT_SRCS = $(wildcard $(SRC_DIR)/exploits/api/*.c) $(wildcard $(SRC_DIR)/exploits/examples/*.c)
UTIL_SRCS = $(wildcard $(SRC_DIR)/utils/*.c)

ALL_SRCS = $(CORE_SRCS) $(INST_SRCS) $(VIZ_SRCS) $(UI_SRCS) $(EXPLOIT_SRCS) $(UTIL_SRCS)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(ALL_SRCS))

# Target
TARGET = $(BIN_DIR)/kexploit-viz

.PHONY: all clean test run

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJS) $(LDFLAGS) $(LIBS) -o $@
	@echo "Build complete: $@"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c $< -o $@

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
	@echo "ALL_SRCS: $(ALL_SRCS)"
	@echo "OBJS: $(OBJS)"

help:
	@echo "Kernel Exploit Visualizer - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build the visualizer"
	@echo "  clean   - Remove build artifacts"
	@echo "  run     - Build and run"
	@echo "  debug   - Build with sanitizers"
	@echo "  help    - Show this help"
