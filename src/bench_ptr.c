#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bench.h"
#include "prng.h"

// How often to update stats (must be power of 2 - 1)
#define STATS_UPDATE_MASK 0x3FFF

// Check if we should stop
static inline int should_stop(worker_ctx_t *ctx, uint64_t ops)
{
	if (atomic_load(ctx->stop_flag))
		return 1;

	if (ctx->stop_mode == STOP_ITERS) {
		return ops >= ctx->max_iters;
	} else {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		double elapsed = (double)(now.tv_sec - ctx->start_time->tv_sec) +
						 (double)(now.tv_nsec - ctx->start_time->tv_nsec) / 1e9;
		return elapsed >= ctx->max_seconds;
	}
}

// Pointer chase node (cache-line sized)
typedef struct chase_node {
	struct chase_node *next;
	uint64_t		   pad[7]; // Pad to 64 bytes
} __attribute__((aligned(64))) chase_node_t;

// Fisher-Yates shuffle for creating random cycle
static void shuffle_indices(uint64_t *arr, size_t n, prng_state_t *prng)
{
	for (size_t i = n - 1; i > 0; i--) {
		size_t	 j	 = prng_next(prng) % (i + 1);
		uint64_t tmp = arr[i];
		arr[i]		 = arr[j];
		arr[j]		 = tmp;
	}
}

// Create a random single-cycle permutation
static void create_chase_cycle(chase_node_t *nodes, size_t count,
							   prng_state_t *prng)
{
	// Create array of indices
	uint64_t *indices = malloc(count * sizeof(uint64_t));
	if (!indices)
		return;

	for (size_t i = 0; i < count; i++) {
		indices[i] = i;
	}

	// Shuffle to create random permutation
	shuffle_indices(indices, count, prng);

	// Link nodes in permutation order to form single cycle
	for (size_t i = 0; i < count - 1; i++) {
		nodes[indices[i]].next = &nodes[indices[i + 1]];
	}
	nodes[indices[count - 1]].next = &nodes[indices[0]]; // Close the cycle

	free(indices);
}

// Pointer chasing benchmark
void bench_ptr_chase(worker_ctx_t *ctx)
{
	// Use buffer as node array
	chase_node_t *nodes = (chase_node_t *)ctx->buffer;
	size_t		  count = ctx->buffer_size / sizeof(chase_node_t);

	if (count < 2) {
		ctx->stats->ops		 = 0;
		ctx->stats->bytes_rd = 0;
		ctx->stats->bytes_wr = 0;
		ctx->stats->checksum = 0;
		return;
	}

	// Initialize nodes
	for (size_t i = 0; i < count; i++) {
		nodes[i].next = NULL;
		for (int j = 0; j < 7; j++) {
			nodes[i].pad[j] = (uint64_t)i ^ (uint64_t)j;
		}
	}

	// Create random cycle
	create_chase_cycle(nodes, count, &ctx->prng);

	// Chase pointers
	uint64_t			   ops		= 0;
	uint64_t			   checksum = 0;
	volatile chase_node_t *current	= &nodes[0];

	while (!should_stop(ctx, ops)) {
		// Chase the pointer
		current = current->next;
		checksum ^= (uint64_t)(uintptr_t)current;
		ops++;

		// Update stats and check stop condition periodically
		if ((ops & STATS_UPDATE_MASK) == 0) {
			ctx->stats->ops		 = ops;
			ctx->stats->bytes_rd = ops * 8; // 8 = sizeof(void*)
			ctx->stats->bytes_wr = 0;
			if (should_stop(ctx, ops))
				break;
		}
	}

	// Use volatile to prevent optimization
	__asm__ volatile("" : : "r"(current) : "memory");

	ctx->stats->ops		 = ops;
	ctx->stats->bytes_rd = ops * sizeof(void *); // Only reading pointer
	ctx->stats->bytes_wr = 0;
	ctx->stats->checksum = checksum;
}

// Benchmark registry
static const bench_desc_t benchmarks[] = {
	{ "seq_read",	  bench_seq_read,	  1, 0 },
	{ "seq_write",  bench_seq_write,	0, 1 },
	{ "seq_rw",		bench_seq_rw,	  1, 1 },
	{ "rand_read",  bench_rand_read,	1, 0 },
	{ "rand_write", bench_rand_write, 0, 1 },
	{ "rand_rw",	 bench_rand_rw,	1, 1 },
	{ "ptr_chase",  bench_ptr_chase,	1, 0 },
	{ NULL,			NULL,			  0, 0 }
};

const bench_desc_t *bench_lookup(const char *name)
{
	for (int i = 0; benchmarks[i].name != NULL; i++) {
		if (strcmp(benchmarks[i].name, name) == 0) {
			return &benchmarks[i];
		}
	}
	return NULL;
}

void bench_list_all(void)
{
	printf("Available benchmarks:\n");
	for (int i = 0; benchmarks[i].name != NULL; i++) {
		printf("  %s\n", benchmarks[i].name);
	}
}
