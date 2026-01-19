# AVX-512 Memory Microbenchmark Suite

A Linux C microbenchmark suite for memory read/write patterns using **AVX-512 intrinsics** and **pthreads**.

## Features

- **7 Benchmarks**: Sequential and random memory access patterns plus pointer chasing
- **3 Run Modes**: Single workload, sequential list, concurrent list
- **2 Stop Modes**: Time-based or iteration-based
- **Per-second stats**: Real-time throughput reporting
- **Reuse mode**: Cache locality testing with configurable region sizes

## Building

```bash
make
```

The executable is built at `bin/membench`.

**Requirements:**
- Linux with GCC or Clang
- CPU with AVX-512 support
- `-mavx512f -march=native` flags (included in Makefile)

```bash
make clean  # Clean build
```

## Benchmarks

| Name | Description | Bytes Read | Bytes Written |
|------|-------------|-----------|---------------|
| `seq_read` | Sequential AVX-512 loads | 64/op | 0 |
| `seq_write` | Sequential AVX-512 stores | 0 | 64/op |
| `seq_rw` | Sequential 1:1 load+store | 64/op | 64/op |
| `rand_read` | Random AVX-512 loads | 64/op | 0 |
| `rand_write` | Random AVX-512 stores | 0 | 64/op |
| `rand_rw` | Random 1:1 load+store | 64/op | 64/op |
| `ptr_chase` | Pointer chasing (latency-bound) | 8/op | 0 |

## Operation Definition

**1 operation = 1 cache line (64 bytes) processed**

For `seq_rw` and `rand_rw`: one op = one load + one store pair on the same cache line.
For `ptr_chase`: one op = one pointer dereference (8 bytes read).

## Usage

### Single Mode

Run one benchmark:

```bash
# Time-based (5 seconds)
./bin/membench --mode single --bench seq_read --size 64M --threads 4 --seconds 5

# Iteration-based (10 million ops)
./bin/membench --mode single --bench rand_write --size 32M --threads 2 --iters 10000000
```

### Sequential Mode

Run benchmarks one after another:

```bash
./bin/membench --mode seq --benches seq_read,seq_write,rand_read --size 64M --threads 4 --seconds 3
```

### Concurrent Mode

Run benchmarks simultaneously:

```bash
./bin/membench --mode concurrent --benches seq_read,rand_rw,ptr_chase --size 64M --threads 2 --seconds 5
```

### Reuse Mode

Test cache effects with limited working set:

```bash
./bin/membench --mode single --bench seq_read --size 256M --threads 4 --seconds 5 \
    --reuse 1 --region-bytes 2M --reuse-iter 50000
```

## CLI Options

| Option | Description | Default |
|--------|-------------|---------|
| `--mode` | `single`, `seq`, or `concurrent` | `single` |
| `--bench` | Benchmark name (single mode) | - |
| `--benches` | Comma-separated list (seq/concurrent) | - |
| `--size` | Buffer size per benchmark (e.g., `64M`) | 64M |
| `--threads` | Threads per benchmark | 4 |
| `--seconds` | Run duration (time-based stop) | 5.0 |
| `--iters` | Operation count (iteration-based stop) | - |
| `--reuse` | Enable reuse mode (0 or 1) | 0 |
| `--region-bytes` | Region size for reuse mode | 2M |
| `--reuse-iter` | Iterations per region | 50000 |
| `--seed` | PRNG seed | 0x12345678DEADBEEF |
| `--report-interval` | Stats interval in seconds | 1.0 |

**Note:** If both `--seconds` and `--iters` are specified, time-based stop takes precedence.

## Output Format

### Per-second Stats

```
t=1s bench=seq_read ops=156250000 rd_GBs=9.31 wr_GBs=0.00
t=2s bench=seq_read ops=157890625 rd_GBs=9.42 wr_GBs=0.00
```

### Final Stats

```
=== seq_read final ===
total_ops=1250000000
total_bytes_rd=80000000000
total_bytes_wr=0
elapsed_sec=8.00
mean_rd_GBs=9.31
mean_wr_GBs=0.00
checksum=0xDEADBEEF12345678
```

## Bandwidth Calculation

Bandwidth is calculated as:

```
rd_GBs = bytes_read / elapsed_seconds / 1e9
wr_GBs = bytes_written / elapsed_seconds / 1e9
```

Where:
- `bytes_read = ops × 64` (for read benchmarks)
- `bytes_written = ops × 64` (for write benchmarks)

## Architecture

```
src/
├── main.c        # Entry point
├── cli.c         # Argument parsing
├── runner.c      # Workload coordination
├── stats.c       # Per-second reporting
├── prng.c        # xoshiro256** PRNG
├── memory.c      # Aligned allocation
├── bench_seq.c   # Sequential benchmarks
├── bench_rand.c  # Random benchmarks
└── bench_ptr.c   # Pointer chase + registry
```

## License

Public domain / MIT
