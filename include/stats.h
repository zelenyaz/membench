#ifndef STATS_H
#define STATS_H

#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>

#define MAX_THREADS		256
#define CACHE_LINE_SIZE 64

// Per-thread stats (cache-line padded to avoid false sharing)
typedef struct {
	uint64_t ops;
	uint64_t bytes_rd;
	uint64_t bytes_wr;
	uint64_t checksum;
	char	 padding[CACHE_LINE_SIZE - 32];
} __attribute__((aligned(CACHE_LINE_SIZE))) thread_stats_t;

// Workload stats context
typedef struct {
	const char *bench_name;
	int			thread_count;

	// Per-thread local counters
	thread_stats_t *thread_stats;

	// Snapshot for interval reporting
	uint64_t last_ops;
	uint64_t last_bytes_rd;
	uint64_t last_bytes_wr;

	// Totals
	uint64_t total_ops;
	uint64_t total_bytes_rd;
	uint64_t total_bytes_wr;
	uint64_t total_checksum;

	// Timing (pointer to shared start time, owned by workload_ctx_t)
	struct timespec *start_time;
	double			 elapsed_sec;

	// Control
	atomic_int running;
	atomic_int done;
} stats_ctx_t;

// Initialize stats context for a workload
int stats_init(stats_ctx_t *ctx, const char *bench_name, int thread_count);

// Cleanup stats context
void stats_destroy(stats_ctx_t *ctx);

// Start timing (uses shared start_time pointer)
void stats_start(stats_ctx_t *ctx, struct timespec *start_time);

// Stop and compute final stats
void stats_stop(stats_ctx_t *ctx);

// Get current timestamp in seconds from start
double stats_elapsed(stats_ctx_t *ctx);

// Aggregate thread stats (called by reporter)
void stats_aggregate(stats_ctx_t *ctx);

// Print interval stats
void stats_print_interval(stats_ctx_t *ctx, double interval_sec);

// Print final stats
void stats_print_final(stats_ctx_t *ctx);

// Reporter thread context
typedef struct {
	stats_ctx_t **contexts; // Array of workload contexts
	int			  context_count;
	double		  interval_sec;
	atomic_int	 *stop_flag;
	pthread_t	  thread;
} reporter_ctx_t;

// Start reporter thread
int reporter_start(reporter_ctx_t *rctx, stats_ctx_t **contexts, int count,
				   double interval_sec, atomic_int *stop_flag);

// Stop reporter thread
void reporter_stop(reporter_ctx_t *rctx);

#endif // STATS_H
