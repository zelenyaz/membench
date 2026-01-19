# AVX-512 Memory Microbenchmark Suite
CC ?= gcc
CFLAGS = -O3 -pthread -mavx512f -march=native -Wall -Wextra -Wshadow -Wconversion -Wno-unused-parameter
LDFLAGS = -pthread -lm

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
TARGET = $(BIN_DIR)/membench

.PHONY: all clean dirs

all: dirs $(TARGET)

dirs:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Dependencies
$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c $(INC_DIR)/cli.h $(INC_DIR)/runner.h $(INC_DIR)/bench.h
$(BUILD_DIR)/cli.o: $(SRC_DIR)/cli.c $(INC_DIR)/cli.h
$(BUILD_DIR)/prng.o: $(SRC_DIR)/prng.c $(INC_DIR)/prng.h
$(BUILD_DIR)/stats.o: $(SRC_DIR)/stats.c $(INC_DIR)/stats.h
$(BUILD_DIR)/memory.o: $(SRC_DIR)/memory.c $(INC_DIR)/memory.h
$(BUILD_DIR)/runner.o: $(SRC_DIR)/runner.c $(INC_DIR)/runner.h $(INC_DIR)/bench.h $(INC_DIR)/stats.h
$(BUILD_DIR)/bench_seq.o: $(SRC_DIR)/bench_seq.c $(INC_DIR)/bench.h $(INC_DIR)/stats.h
$(BUILD_DIR)/bench_rand.o: $(SRC_DIR)/bench_rand.c $(INC_DIR)/bench.h $(INC_DIR)/stats.h $(INC_DIR)/prng.h
$(BUILD_DIR)/bench_ptr.o: $(SRC_DIR)/bench_ptr.c $(INC_DIR)/bench.h $(INC_DIR)/stats.h $(INC_DIR)/prng.h
