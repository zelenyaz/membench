#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "stats.h"

int stats_init(stats_ctx_t *ctx, const char *bench_name, int thread_count)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->bench_name	  = bench_name;
	ctx->thread_count = thread_count;

	ctx->thread_stats = aligned_alloc(
		CACHE_LINE_SIZE, (size_t)thread_count * sizeof(thread_stats_t));
	if (!ctx->thread_stats) {
		return -1;
	}
	memset(ctx->thread_stats, 0, (size_t)thread_count * sizeof(thread_stats_t));

	atomic_store(&ctx->running, 0);
	atomic_store(&ctx->done, 0);

	return 0;
}

void stats_destroy(stats_ctx_t *ctx)
{
	if (ctx->thread_stats) {
		free(ctx->thread_stats);
		ctx->thread_stats = NULL;
	}
}

void stats_start(stats_ctx_t *ctx, struct timespec *start_time)
{
	ctx->start_time = start_time;
	atomic_store(&ctx->running, 1);
}

double stats_elapsed(stats_ctx_t *ctx)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	double elapsed = (double)(now.tv_sec - ctx->start_time->tv_sec) +
					 (double)(now.tv_nsec - ctx->start_time->tv_nsec) / 1e9;
	return elapsed;
}

void stats_aggregate(stats_ctx_t *ctx)
{
	uint64_t ops = 0, bytes_rd = 0, bytes_wr = 0, checksum = 0;

	for (int i = 0; i < ctx->thread_count; i++) {
		ops += ctx->thread_stats[i].ops;
		bytes_rd += ctx->thread_stats[i].bytes_rd;
		bytes_wr += ctx->thread_stats[i].bytes_wr;
		checksum ^= ctx->thread_stats[i].checksum;
	}

	ctx->total_ops		= ops;
	ctx->total_bytes_rd = bytes_rd;
	ctx->total_bytes_wr = bytes_wr;
	ctx->total_checksum = checksum;
}

void stats_stop(stats_ctx_t *ctx)
{
	ctx->elapsed_sec = stats_elapsed(ctx);
	atomic_store(&ctx->running, 0);
	stats_aggregate(ctx);
}

void stats_print_interval(stats_ctx_t *ctx, double interval_sec)
{
	stats_aggregate(ctx);

	uint64_t delta_ops = ctx->total_ops - ctx->last_ops;
	uint64_t delta_rd  = ctx->total_bytes_rd - ctx->last_bytes_rd;
	uint64_t delta_wr  = ctx->total_bytes_wr - ctx->last_bytes_wr;

	double rd_gbs = (double)delta_rd / interval_sec / 1e9;
	double wr_gbs = (double)delta_wr / interval_sec / 1e9;

	double elapsed = stats_elapsed(ctx);

	printf("t=%.0fs bench=%s ops=%lu rd_GBs=%.2f wr_GBs=%.2f\n", elapsed,
		   ctx->bench_name, delta_ops, rd_gbs, wr_gbs);
	fflush(stdout);

	ctx->last_ops	   = ctx->total_ops;
	ctx->last_bytes_rd = ctx->total_bytes_rd;
	ctx->last_bytes_wr = ctx->total_bytes_wr;
}

void stats_print_final(stats_ctx_t *ctx)
{
	double rd_gbs = ctx->elapsed_sec > 0 ?
						(double)ctx->total_bytes_rd / ctx->elapsed_sec / 1e9 :
						0;
	double wr_gbs = ctx->elapsed_sec > 0 ?
						(double)ctx->total_bytes_wr / ctx->elapsed_sec / 1e9 :
						0;

	printf("\n=== %s final ===\n", ctx->bench_name);
	printf("total_ops=%lu\n", ctx->total_ops);
	printf("total_bytes_rd=%lu\n", ctx->total_bytes_rd);
	printf("total_bytes_wr=%lu\n", ctx->total_bytes_wr);
	printf("elapsed_sec=%.2f\n", ctx->elapsed_sec);
	printf("mean_rd_GBs=%.2f\n", rd_gbs);
	printf("mean_wr_GBs=%.2f\n", wr_gbs);
	printf("checksum=0x%016lX\n", ctx->total_checksum);
	fflush(stdout);
}

// Reporter thread function
static void *reporter_thread_func(void *arg)
{
	reporter_ctx_t *rctx = (reporter_ctx_t *)arg;

	struct timespec ts;
	ts.tv_sec  = (time_t)rctx->interval_sec;
	ts.tv_nsec = (long)((rctx->interval_sec - (double)ts.tv_sec) * 1e9);

	while (!atomic_load(rctx->stop_flag)) {
		nanosleep(&ts, NULL);

		if (atomic_load(rctx->stop_flag))
			break;

		for (int i = 0; i < rctx->context_count; i++) {
			if (atomic_load(&rctx->contexts[i]->running)) {
				stats_print_interval(rctx->contexts[i], rctx->interval_sec);
			}
		}
	}

	return NULL;
}

int reporter_start(reporter_ctx_t *rctx, stats_ctx_t **contexts, int count,
				   double interval_sec, atomic_int *stop_flag)
{
	rctx->contexts		= contexts;
	rctx->context_count = count;
	rctx->interval_sec	= interval_sec;
	rctx->stop_flag		= stop_flag;

	if (pthread_create(&rctx->thread, NULL, reporter_thread_func, rctx) != 0) {
		return -1;
	}

	return 0;
}

void reporter_stop(reporter_ctx_t *rctx)
{
	atomic_store(rctx->stop_flag, 1);
	pthread_join(rctx->thread, NULL);
}
