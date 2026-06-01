#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <numaif.h>
#include "numa_monitor.h"

#define NUMA_MON_INTERVAL_SEC 3
#define PAGE_SIZE			  4096

// Query NUMA distribution for a buffer using move_pages()
static void query_numa_dist(void *buffer, size_t buffer_size,
							long *out_node0, long *out_node1,
							long *out_others, long *out_errors)
{
	size_t nr_pages = buffer_size / PAGE_SIZE;
	if (nr_pages == 0) {
		*out_node0 = *out_node1 = *out_others = *out_errors = 0;
		return;
	}

	void **pages  = malloc(nr_pages * sizeof(void *));
	int   *status = malloc(nr_pages * sizeof(int));
	if (!pages || !status) {
		free(pages);
		free(status);
		*out_node0 = *out_node1 = *out_others = *out_errors = 0;
		return;
	}

	// Build page address array
	char *base = (char *)buffer;
	for (size_t i = 0; i < nr_pages; i++)
		pages[i] = base + i * PAGE_SIZE;

	// Query current NUMA placement (nodes=NULL means query only)
	long ret = move_pages(0, (unsigned long)nr_pages, pages, NULL, status, 0);
	if (ret != 0 && errno != 0) {
		free(pages);
		free(status);
		*out_node0	= 0;
		*out_node1	= 0;
		*out_others = 0;
		*out_errors = (long)nr_pages;
		return;
	}

	long n0 = 0, n1 = 0, others = 0, errors = 0;
	for (size_t i = 0; i < nr_pages; i++) {
		if (status[i] < 0) {
			errors++;
		} else if (status[i] == 0) {
			n0++;
		} else if (status[i] == 1) {
			n1++;
		} else {
			others++;
		}
	}

	*out_node0	= n0;
	*out_node1	= n1;
	*out_others = others;
	*out_errors = errors;

	free(pages);
	free(status);
}

static void *numa_monitor_thread(void *arg)
{
	numa_monitor_ctx_t *ctx = (numa_monitor_ctx_t *)arg;

	struct timespec ts;
	ts.tv_sec  = NUMA_MON_INTERVAL_SEC;
	ts.tv_nsec = 0;

	struct timespec t0;
	clock_gettime(CLOCK_MONOTONIC, &t0);

	while (!atomic_load(ctx->stop_flag)) {
		nanosleep(&ts, NULL);

		if (atomic_load(ctx->stop_flag))
			break;

		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		double elapsed = (double)(now.tv_sec - t0.tv_sec) +
						 (double)(now.tv_nsec - t0.tv_nsec) / 1e9;

		for (int i = 0; i < ctx->buffer_count; i++) {
			long n0, n1, others, errors;
			query_numa_dist(ctx->buffers[i].buffer,
							ctx->buffers[i].buffer_size,
							&n0, &n1, &others, &errors);
			fprintf(ctx->csv_file, "%.1f,%s,%ld,%ld,%ld,%ld\n",
					elapsed, ctx->buffers[i].bench_name,
					n0, n1, others, errors);
		}
		fflush(ctx->csv_file);
	}

	return NULL;
}

int numa_monitor_add_buffer(numa_monitor_ctx_t *ctx, void *buffer,
							size_t buffer_size, const char *bench_name)
{
	if (ctx->buffer_count >= NUMA_MON_MAX_BUFFERS)
		return -1;

	int idx					  = ctx->buffer_count;
	ctx->buffers[idx].buffer	  = buffer;
	ctx->buffers[idx].buffer_size = buffer_size;
	ctx->buffers[idx].bench_name  = bench_name;
	ctx->buffer_count++;
	return 0;
}

int numa_monitor_start(numa_monitor_ctx_t *ctx, const char *csv_path,
					   atomic_int *stop_flag, int write_header)
{
	ctx->stop_flag = stop_flag;

	ctx->csv_file = fopen(csv_path, write_header ? "w" : "a");
	if (!ctx->csv_file) {
		perror("numa_monitor: fopen");
		return -1;
	}

	if (write_header) {
		fprintf(ctx->csv_file,
				"elapsed_sec,bench_name,nr_pages_node0,"
				"nr_pages_node1,others,errors\n");
		fflush(ctx->csv_file);
	}

	if (pthread_create(&ctx->thread, NULL, numa_monitor_thread, ctx) != 0) {
		fclose(ctx->csv_file);
		ctx->csv_file = NULL;
		return -1;
	}

	return 0;
}

void numa_monitor_stop(numa_monitor_ctx_t *ctx)
{
	atomic_store(ctx->stop_flag, 1);
	pthread_join(ctx->thread, NULL);

	if (ctx->csv_file) {
		fclose(ctx->csv_file);
		ctx->csv_file = NULL;
	}
}
