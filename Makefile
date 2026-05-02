CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lm

SRC_DIR = src
BUILD_DIR = bin

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

TARGET = mdcomp

INPUT ?= README.md
OUTPUT ?= output.pdf
SKIP_OBSIDIAN ?=

all: $(BUILD_DIR) $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET) -i $(INPUT) -o $(OUTPUT) $(if $(SKIP_OBSIDIAN),-s,)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

rebuild: clean all

.PHONY: all run clean rebuild
