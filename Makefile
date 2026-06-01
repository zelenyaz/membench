# Memory Microbenchmark Suite
# Supports both 64-bit scalar and AVX-512 (512-bit) modes

CC ?= gcc
CFLAGS_COMMON = -O3 -pthread -march=native -Wall -Wextra -Wshadow -Wconversion -Wno-unused-parameter
CFLAGS_512BIT = $(CFLAGS_COMMON) -mavx512f
CFLAGS_64BIT = $(CFLAGS_COMMON) -DUSE_64BIT
LDFLAGS = -pthread -lm -lnuma

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin

BUILD_DIR_512BIT = $(BUILD_DIR)/512bit
BUILD_DIR_64BIT = $(BUILD_DIR)/64bit

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS_512BIT = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR_512BIT)/%.o,$(SRCS))
OBJS_64BIT = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR_64BIT)/%.o,$(SRCS))

TARGET_512BIT = $(BIN_DIR)/membench
TARGET_64BIT = $(BIN_DIR)/membench-64

.PHONY: all clean dirs 512bit 64bit

all: dirs $(TARGET_512BIT) $(TARGET_64BIT)

512bit: dirs $(TARGET_512BIT)

64bit: dirs $(TARGET_64BIT)

dirs:
	@mkdir -p $(BUILD_DIR_512BIT) $(BUILD_DIR_64BIT) $(BIN_DIR)

# 512-bit (AVX-512) version
$(TARGET_512BIT): $(OBJS_512BIT)
	$(CC) $(OBJS_512BIT) -o $@ $(LDFLAGS)

$(BUILD_DIR_512BIT)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS_512BIT) -I$(INC_DIR) -c $< -o $@

# 64-bit scalar version
$(TARGET_64BIT): $(OBJS_64BIT)
	$(CC) $(OBJS_64BIT) -o $@ $(LDFLAGS)

$(BUILD_DIR_64BIT)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS_64BIT) -I$(INC_DIR) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Dependencies for 512-bit
$(BUILD_DIR_512BIT)/main.o: $(SRC_DIR)/main.c $(INC_DIR)/cli.h $(INC_DIR)/runner.h $(INC_DIR)/bench.h
$(BUILD_DIR_512BIT)/cli.o: $(SRC_DIR)/cli.c $(INC_DIR)/cli.h
$(BUILD_DIR_512BIT)/stats.o: $(SRC_DIR)/stats.c $(INC_DIR)/stats.h
$(BUILD_DIR_512BIT)/memory.o: $(SRC_DIR)/memory.c $(INC_DIR)/memory.h
$(BUILD_DIR_512BIT)/runner.o: $(SRC_DIR)/runner.c $(INC_DIR)/runner.h $(INC_DIR)/bench.h $(INC_DIR)/stats.h $(INC_DIR)/numa_monitor.h
$(BUILD_DIR_512BIT)/bench_seq.o: $(SRC_DIR)/bench_seq.c $(INC_DIR)/bench.h $(INC_DIR)/stats.h
$(BUILD_DIR_512BIT)/bench_rand.o: $(SRC_DIR)/bench_rand.c $(INC_DIR)/bench.h $(INC_DIR)/stats.h $(INC_DIR)/prng.h
$(BUILD_DIR_512BIT)/bench_ptr.o: $(SRC_DIR)/bench_ptr.c $(INC_DIR)/bench.h $(INC_DIR)/stats.h $(INC_DIR)/prng.h
$(BUILD_DIR_512BIT)/numa_monitor.o: $(SRC_DIR)/numa_monitor.c $(INC_DIR)/numa_monitor.h

# Dependencies for 64-bit
$(BUILD_DIR_64BIT)/main.o: $(SRC_DIR)/main.c $(INC_DIR)/cli.h $(INC_DIR)/runner.h $(INC_DIR)/bench.h
$(BUILD_DIR_64BIT)/cli.o: $(SRC_DIR)/cli.c $(INC_DIR)/cli.h
$(BUILD_DIR_64BIT)/stats.o: $(SRC_DIR)/stats.c $(INC_DIR)/stats.h
$(BUILD_DIR_64BIT)/memory.o: $(SRC_DIR)/memory.c $(INC_DIR)/memory.h
$(BUILD_DIR_64BIT)/runner.o: $(SRC_DIR)/runner.c $(INC_DIR)/runner.h $(INC_DIR)/bench.h $(INC_DIR)/stats.h $(INC_DIR)/numa_monitor.h
$(BUILD_DIR_64BIT)/bench_seq.o: $(SRC_DIR)/bench_seq.c $(INC_DIR)/bench.h $(INC_DIR)/stats.h
$(BUILD_DIR_64BIT)/bench_rand.o: $(SRC_DIR)/bench_rand.c $(INC_DIR)/bench.h $(INC_DIR)/stats.h $(INC_DIR)/prng.h
$(BUILD_DIR_64BIT)/bench_ptr.o: $(SRC_DIR)/bench_ptr.c $(INC_DIR)/bench.h $(INC_DIR)/stats.h $(INC_DIR)/prng.h
$(BUILD_DIR_64BIT)/numa_monitor.o: $(SRC_DIR)/numa_monitor.c $(INC_DIR)/numa_monitor.h
