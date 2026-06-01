#ifndef RUNNER_H
#define RUNNER_H

#include "cli.h"
#include "bench.h"
#include "stats.h"

// Workload context
typedef struct {
	const bench_desc_t *bench;
	cli_args_t		   *args;
	stats_ctx_t			stats;
	int					nthreads;

	void		 *buffer;
	void		 *buffer2;
	pthread_t	 *threads;
	worker_ctx_t *worker_ctxs;

	pthread_barrier_t barrier;
	atomic_int		  stop_flag;
	struct timespec	  start_time;
} workload_ctx_t;

// Run single workload
int run_single(cli_args_t *args);

// Run sequential workload list
int run_sequential(cli_args_t *args);

// Run concurrent workload list
int run_concurrent(cli_args_t *args);

// Initialize workload context.
// core_offset is the first CPU core to pin this workload's threads to when
// args->pin is set; thread i is pinned to (core_offset + i) % online_cpus.
int workload_init(workload_ctx_t *wctx, const bench_desc_t *bench,
				  cli_args_t *args, int nthreads, int core_offset);

// Start workload threads
int workload_start(workload_ctx_t *wctx);

// Wait for workload completion
void workload_wait(workload_ctx_t *wctx);

// Cleanup workload
void workload_destroy(workload_ctx_t *wctx);

#endif // RUNNER_H
