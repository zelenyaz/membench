#ifndef BENCH_H
#define BENCH_H

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include "stats.h"
#include "prng.h"
#include "cli.h"

// Worker thread context
typedef struct {
	int thread_id;
	int thread_count;

	// Buffer region for this thread
	void  *buffer;
	size_t buffer_size;

	// Reuse mode parameters
	int		 reuse_mode;
	size_t	 region_bytes;
	uint64_t reuse_iter;

	// Stop control
	atomic_int *stop_flag;

	// Stop condition
	stop_mode_t stop_mode;
	double		max_seconds;
	uint64_t	max_iters;

	// Stats
	thread_stats_t *stats;

	// PRNG state
	prng_state_t prng;

	// Barrier for synchronized start
	pthread_barrier_t *barrier;

	// Start time reference
	struct timespec *start_time;
} worker_ctx_t;

// Benchmark function type
typedef void (*bench_func_t)(worker_ctx_t *ctx);

// Benchmark descriptor
typedef struct {
	const char	*name;
	bench_func_t func;
	int			 reads;		 // 1 if benchmark reads
	int			 writes;	 // 1 if benchmark writes
	int			 reuse_mode; // 1 if benchmark uses reuse pattern
} bench_desc_t;

// Get benchmark by name
const bench_desc_t *bench_lookup(const char *name);

// List all benchmarks
void bench_list_all(void);

// Sequential benchmarks
void bench_seq_read(worker_ctx_t *ctx);
void bench_seq_write(worker_ctx_t *ctx);
void bench_seq_rw(worker_ctx_t *ctx);

// Random benchmarks
void bench_rand_read(worker_ctx_t *ctx);
void bench_rand_write(worker_ctx_t *ctx);
void bench_rand_rw(worker_ctx_t *ctx);

// Pointer chase
void bench_ptr_chase(worker_ctx_t *ctx);

#endif // BENCH_H
