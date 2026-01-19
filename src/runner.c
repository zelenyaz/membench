#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include "runner.h"
#include "memory.h"
#include "bench.h"

int workload_init(workload_ctx_t *wctx, const bench_desc_t *bench,
				  cli_args_t *args)
{
	memset(wctx, 0, sizeof(*wctx));
	wctx->bench = bench;
	wctx->args	= args;

	// Initialize stats
	if (stats_init(&wctx->stats, bench->name, args->threads) < 0) {
		return -1;
	}

	// Allocate buffer (64-byte aligned)
	wctx->buffer = mem_alloc_aligned(args->buffer_size, 64);
	if (!wctx->buffer) {
		stats_destroy(&wctx->stats);
		return -1;
	}

	// Touch pages and fill with pattern
	mem_touch_pages(wctx->buffer, args->buffer_size);
	mem_fill_pattern(wctx->buffer, args->buffer_size, args->seed);

	// Allocate thread structures
	wctx->threads	  = calloc((size_t)args->threads, sizeof(pthread_t));
	wctx->worker_ctxs = calloc((size_t)args->threads, sizeof(worker_ctx_t));
	if (!wctx->threads || !wctx->worker_ctxs) {
		mem_free_aligned(wctx->buffer);
		stats_destroy(&wctx->stats);
		return -1;
	}

	// Initialize barrier (+1 for main thread if needed, but we use thread
	// count)
	pthread_barrier_init(&wctx->barrier, NULL, (unsigned)args->threads);
	atomic_store(&wctx->stop_flag, 0);

	// Setup worker contexts
	// Round chunk_size down to 64-byte boundary for AVX-512 alignment
	size_t chunk_size = (args->buffer_size / (size_t)args->threads) &
						~(size_t)63;
	for (int i = 0; i < args->threads; i++) {
		worker_ctx_t *w = &wctx->worker_ctxs[i];
		w->thread_id	= i;
		w->thread_count = args->threads;
		w->buffer		= (char *)wctx->buffer + (size_t)i * chunk_size;
		w->buffer_size	= chunk_size;
		w->reuse_mode	= bench->reuse_mode;
		w->region_bytes = args->region_bytes;
		w->reuse_iter	= args->reuse_iter;
		w->stop_flag	= &wctx->stop_flag;
		w->stop_mode	= args->stop_mode;
		w->max_seconds	= args->seconds;
		w->max_iters	= args->iters / (uint64_t)args->threads; // Divide iters
															  // among threads
		w->stats	  = &wctx->stats.thread_stats[i];
		w->barrier	  = &wctx->barrier;
		w->start_time = &wctx->start_time;

		// Initialize PRNG with unique seed per thread
		prng_init(&w->prng, args->seed + (uint64_t)i * 0x9E3779B97F4A7C15ULL);
	}

	return 0;
}

// Thread entry point that calls the benchmark function
typedef struct {
	worker_ctx_t	  *ctx;
	bench_func_t	   func;
	struct timespec	  *shared_start;
	pthread_barrier_t *barrier;
} thread_entry_t;

static void *thread_entry(void *arg)
{
	thread_entry_t *entry = (thread_entry_t *)arg;
	worker_ctx_t   *ctx	  = entry->ctx;

	// Wait at barrier
	pthread_barrier_wait(entry->barrier);

	// First thread records start time
	if (ctx->thread_id == 0) {
		clock_gettime(CLOCK_MONOTONIC, entry->shared_start);
	}

	// Small barrier again to ensure start time is set
	pthread_barrier_wait(entry->barrier);

	// Run benchmark
	entry->func(ctx);

	return NULL;
}

int workload_start(workload_ctx_t *wctx)
{
	// Re-init barrier for 2 phases
	pthread_barrier_destroy(&wctx->barrier);
	pthread_barrier_init(&wctx->barrier, NULL, (unsigned)wctx->args->threads);

	// Create thread entries
	thread_entry_t *entries =
		calloc((size_t)wctx->args->threads, sizeof(thread_entry_t));
	if (!entries)
		return -1;

	// Start stats timing (shares start_time with workers)
	stats_start(&wctx->stats, &wctx->start_time);

	for (int i = 0; i < wctx->args->threads; i++) {
		entries[i].ctx			= &wctx->worker_ctxs[i];
		entries[i].func			= wctx->bench->func;
		entries[i].shared_start = &wctx->start_time;
		entries[i].barrier		= &wctx->barrier;

		if (pthread_create(&wctx->threads[i], NULL, thread_entry,
						   &entries[i]) != 0) {
			// Cleanup on failure
			atomic_store(&wctx->stop_flag, 1);
			for (int j = 0; j < i; j++) {
				pthread_join(wctx->threads[j], NULL);
			}
			free(entries);
			return -1;
		}
	}

	// Store entries for later cleanup (hack: store in unused field or just free
	// after join) We'll wait immediately after this, so it's okay

	// Wait for all threads
	for (int i = 0; i < wctx->args->threads; i++) {
		pthread_join(wctx->threads[i], NULL);
	}

	free(entries);

	// Stop stats
	stats_stop(&wctx->stats);

	return 0;
}

void workload_wait(workload_ctx_t *wctx)
{
	// Already waited in workload_start
}

void workload_destroy(workload_ctx_t *wctx)
{
	pthread_barrier_destroy(&wctx->barrier);

	if (wctx->buffer) {
		mem_free_aligned(wctx->buffer);
		wctx->buffer = NULL;
	}
	if (wctx->threads) {
		free(wctx->threads);
		wctx->threads = NULL;
	}
	if (wctx->worker_ctxs) {
		free(wctx->worker_ctxs);
		wctx->worker_ctxs = NULL;
	}

	stats_destroy(&wctx->stats);
}

// Run single workload
int run_single(cli_args_t *args)
{
	const bench_desc_t *bench = bench_lookup(args->bench_name);
	if (!bench) {
		fprintf(stderr, "Unknown benchmark: %s\n", args->bench_name);
		return -1;
	}

	printf("Running benchmark: %s\n", bench->name);
	printf("Buffer size: %zu bytes, Threads: %d\n", args->buffer_size,
		   args->threads);
	if (args->stop_mode == STOP_TIME) {
		printf("Stop mode: time (%.1f seconds)\n", args->seconds);
	} else {
		printf("Stop mode: iterations (%lu ops)\n", args->iters);
	}
	printf("\n");

	workload_ctx_t wctx;
	if (workload_init(&wctx, bench, args) < 0) {
		fprintf(stderr, "Failed to initialize workload\n");
		return -1;
	}

	// Start reporter
	atomic_int	   reporter_stop_flag = ATOMIC_VAR_INIT(0);
	reporter_ctx_t reporter;
	stats_ctx_t	  *stats_arr[1] = { &wctx.stats };
	reporter_start(&reporter, stats_arr, 1, args->report_interval,
				   &reporter_stop_flag);

	// Run workload
	workload_start(&wctx);

	// Stop reporter
	atomic_store(&reporter_stop_flag, 1);
	reporter_stop(&reporter);

	// Print final stats
	stats_print_final(&wctx.stats);

	workload_destroy(&wctx);
	return 0;
}

// Run sequential workload list
int run_sequential(cli_args_t *args)
{
	printf("Running %d benchmarks sequentially\n\n", args->bench_count);

	for (int i = 0; i < args->bench_count; i++) {
		const bench_desc_t *bench = bench_lookup(args->bench_list[i]);
		if (!bench) {
			fprintf(stderr, "Unknown benchmark: %s\n", args->bench_list[i]);
			continue;
		}

		printf("=== Starting benchmark %d/%d: %s ===\n", i + 1,
			   args->bench_count, bench->name);
		printf("Buffer size: %zu bytes, Threads: %d\n", args->buffer_size,
			   args->threads);
		printf("\n");

		workload_ctx_t wctx;
		if (workload_init(&wctx, bench, args) < 0) {
			fprintf(stderr, "Failed to initialize workload: %s\n", bench->name);
			continue;
		}

		// Start reporter
		atomic_int	   reporter_stop_flag = ATOMIC_VAR_INIT(0);
		reporter_ctx_t reporter;
		stats_ctx_t	  *stats_arr[1] = { &wctx.stats };
		reporter_start(&reporter, stats_arr, 1, args->report_interval,
					   &reporter_stop_flag);

		// Run workload
		workload_start(&wctx);

		// Stop reporter
		atomic_store(&reporter_stop_flag, 1);
		reporter_stop(&reporter);

		// Print final stats
		stats_print_final(&wctx.stats);

		workload_destroy(&wctx);
		printf("\n");
	}

	return 0;
}

// Concurrent mode worker thread
typedef struct {
	workload_ctx_t	  *wctx;
	pthread_barrier_t *global_barrier;
} concurrent_workload_t;

static void *concurrent_workload_thread(void *arg)
{
	concurrent_workload_t *cw	= (concurrent_workload_t *)arg;
	workload_ctx_t		  *wctx = cw->wctx;

	// Wait at global barrier
	pthread_barrier_wait(cw->global_barrier);

	// Run workload (this uses its own internal barrier)
	workload_start(wctx);

	return NULL;
}

// Run concurrent workload list
int run_concurrent(cli_args_t *args)
{
	printf("Running %d benchmarks concurrently\n", args->bench_count);
	int total_threads = args->bench_count * args->threads;
	printf("Total threads: %d (warning if > CPU cores)\n\n", total_threads);

	// Initialize all workloads
	workload_ctx_t *wctxs =
		calloc((size_t)args->bench_count, sizeof(workload_ctx_t));
	stats_ctx_t **stats_arr =
		calloc((size_t)args->bench_count, sizeof(stats_ctx_t *));
	pthread_t *workload_threads =
		calloc((size_t)args->bench_count, sizeof(pthread_t));
	concurrent_workload_t *cws =
		calloc((size_t)args->bench_count, sizeof(concurrent_workload_t));

	if (!wctxs || !stats_arr || !workload_threads || !cws) {
		fprintf(stderr, "Memory allocation failed\n");
		free(wctxs);
		free(stats_arr);
		free(workload_threads);
		free(cws);
		return -1;
	}

	pthread_barrier_t global_barrier;
	pthread_barrier_init(&global_barrier, NULL, (unsigned)args->bench_count);

	int active_count = 0;
	for (int i = 0; i < args->bench_count; i++) {
		const bench_desc_t *bench = bench_lookup(args->bench_list[i]);
		if (!bench) {
			fprintf(stderr, "Unknown benchmark: %s\n", args->bench_list[i]);
			continue;
		}

		if (workload_init(&wctxs[active_count], bench, args) < 0) {
			fprintf(stderr, "Failed to initialize workload: %s\n", bench->name);
			continue;
		}

		stats_arr[active_count]			 = &wctxs[active_count].stats;
		cws[active_count].wctx			 = &wctxs[active_count];
		cws[active_count].global_barrier = &global_barrier;
		active_count++;
	}

	if (active_count == 0) {
		fprintf(stderr, "No valid benchmarks to run\n");
		pthread_barrier_destroy(&global_barrier);
		free(wctxs);
		free(stats_arr);
		free(workload_threads);
		free(cws);
		return -1;
	}

	// Reinit barrier with actual count
	pthread_barrier_destroy(&global_barrier);
	pthread_barrier_init(&global_barrier, NULL, (unsigned)active_count);

	// Start reporter
	atomic_int	   reporter_stop_flag = ATOMIC_VAR_INIT(0);
	reporter_ctx_t reporter;
	reporter_start(&reporter, stats_arr, active_count, args->report_interval,
				   &reporter_stop_flag);

	// Launch all workload threads
	for (int i = 0; i < active_count; i++) {
		cws[i].global_barrier = &global_barrier;
		pthread_create(&workload_threads[i], NULL, concurrent_workload_thread,
					   &cws[i]);
	}

	// Wait for all to complete
	for (int i = 0; i < active_count; i++) {
		pthread_join(workload_threads[i], NULL);
	}

	// Stop reporter
	atomic_store(&reporter_stop_flag, 1);
	reporter_stop(&reporter);

	// Print final stats for each
	printf("\n=== Concurrent Results ===\n");
	for (int i = 0; i < active_count; i++) {
		stats_print_final(&wctxs[i].stats);
		workload_destroy(&wctxs[i]);
	}

	pthread_barrier_destroy(&global_barrier);
	free(wctxs);
	free(stats_arr);
	free(workload_threads);
	free(cws);

	return 0;
}
