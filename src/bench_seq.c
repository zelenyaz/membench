#include <stdio.h>
#include <string.h>
#include <time.h>
#include <immintrin.h>
#include "bench.h"

#define CACHE_LINE_SIZE 64
#define ZMM_SIZE		64

// How often to update stats (must be power of 2 - 1)
#define STATS_UPDATE_MASK 0xFFFF

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

// Inline stats update
static inline void update_stats(worker_ctx_t *ctx, uint64_t ops,
								uint64_t bytes_rd, uint64_t bytes_wr)
{
	if ((ops & STATS_UPDATE_MASK) == 0) {
		ctx->stats->ops		 = ops;
		ctx->stats->bytes_rd = bytes_rd;
		ctx->stats->bytes_wr = bytes_wr;
	}
}

// Sequential read using AVX-512
void bench_seq_read(worker_ctx_t *ctx)
{
	const char *buf		 = (const char *)ctx->buffer;
	size_t		size	 = ctx->buffer_size;
	uint64_t	ops		 = 0;
	__m512i		checksum = _mm512_setzero_si512();

	if (ctx->reuse_mode && ctx->region_bytes > 0) {
		size_t region_size = ctx->region_bytes < size ? ctx->region_bytes :
														size;
		size_t num_regions = size / region_size;
		if (num_regions == 0)
			num_regions = 1;

		size_t region_idx = 0;
		while (!should_stop(ctx, ops)) {
			const char *region = buf + (region_idx % num_regions) * region_size;

			for (uint64_t iter = 0;
				 iter < ctx->reuse_iter && !should_stop(ctx, ops); iter++) {
				for (size_t off = 0; off < region_size; off += ZMM_SIZE) {
					__m512i v =
						_mm512_load_si512((const __m512i *)(region + off));
					checksum = _mm512_xor_si512(checksum, v);
					ops++;
					update_stats(ctx, ops, ops * CACHE_LINE_SIZE, 0);
					if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
						break;
				}
			}
			region_idx++;
		}
	} else {
		while (!should_stop(ctx, ops)) {
			for (size_t off = 0; off < size; off += ZMM_SIZE) {
				__m512i v = _mm512_load_si512((const __m512i *)(buf + off));
				checksum  = _mm512_xor_si512(checksum, v);
				ops++;
				update_stats(ctx, ops, ops * CACHE_LINE_SIZE, 0);
				if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
					break;
			}
		}
	}

	uint64_t cs[8];
	_mm512_storeu_si512((__m512i *)cs, checksum);
	ctx->stats->checksum = cs[0] ^ cs[1] ^ cs[2] ^ cs[3] ^ cs[4] ^ cs[5] ^
						   cs[6] ^ cs[7];
	ctx->stats->ops		 = ops;
	ctx->stats->bytes_rd = ops * CACHE_LINE_SIZE;
	ctx->stats->bytes_wr = 0;
}

// Sequential write using AVX-512
void bench_seq_write(worker_ctx_t *ctx)
{
	char	*buf  = (char *)ctx->buffer;
	size_t	 size = ctx->buffer_size;
	uint64_t ops  = 0;
	__m512i	 val  = _mm512_set1_epi64((long long)(ctx->thread_id + 1));

	if (ctx->reuse_mode && ctx->region_bytes > 0) {
		size_t region_size = ctx->region_bytes < size ? ctx->region_bytes :
														size;
		size_t num_regions = size / region_size;
		if (num_regions == 0)
			num_regions = 1;

		size_t region_idx = 0;
		while (!should_stop(ctx, ops)) {
			char *region = buf + (region_idx % num_regions) * region_size;

			for (uint64_t iter = 0;
				 iter < ctx->reuse_iter && !should_stop(ctx, ops); iter++) {
				for (size_t off = 0; off < region_size; off += ZMM_SIZE) {
					_mm512_store_si512((__m512i *)(region + off), val);
					ops++;
					update_stats(ctx, ops, 0, ops * CACHE_LINE_SIZE);
					if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
						break;
				}
				val = _mm512_add_epi64(val, _mm512_set1_epi64(1));
			}
			region_idx++;
		}
	} else {
		while (!should_stop(ctx, ops)) {
			for (size_t off = 0; off < size; off += ZMM_SIZE) {
				_mm512_store_si512((__m512i *)(buf + off), val);
				ops++;
				update_stats(ctx, ops, 0, ops * CACHE_LINE_SIZE);
				if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
					break;
			}
			val = _mm512_add_epi64(val, _mm512_set1_epi64(1));
		}
	}

	uint64_t cs[8];
	_mm512_storeu_si512((__m512i *)cs, val);
	ctx->stats->checksum = cs[0];
	ctx->stats->ops		 = ops;
	ctx->stats->bytes_rd = 0;
	ctx->stats->bytes_wr = ops * CACHE_LINE_SIZE;
}

// Sequential read+write (1:1)
void bench_seq_rw(worker_ctx_t *ctx)
{
	char	*buf	  = (char *)ctx->buffer;
	size_t	 size	  = ctx->buffer_size;
	uint64_t ops	  = 0;
	__m512i	 checksum = _mm512_setzero_si512();
	__m512i	 add_val  = _mm512_set1_epi64(1);

	if (ctx->reuse_mode && ctx->region_bytes > 0) {
		size_t region_size = ctx->region_bytes < size ? ctx->region_bytes :
														size;
		size_t num_regions = size / region_size;
		if (num_regions == 0)
			num_regions = 1;

		size_t region_idx = 0;
		while (!should_stop(ctx, ops)) {
			char *region = buf + (region_idx % num_regions) * region_size;

			for (uint64_t iter = 0;
				 iter < ctx->reuse_iter && !should_stop(ctx, ops); iter++) {
				for (size_t off = 0; off < region_size; off += ZMM_SIZE) {
					__m512i v =
						_mm512_load_si512((const __m512i *)(region + off));
					v = _mm512_add_epi64(v, add_val);
					_mm512_store_si512((__m512i *)(region + off), v);
					checksum = _mm512_xor_si512(checksum, v);
					ops++;
					uint64_t bytes = ops * CACHE_LINE_SIZE;
					update_stats(ctx, ops, bytes, bytes);
					if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
						break;
				}
			}
			region_idx++;
		}
	} else {
		while (!should_stop(ctx, ops)) {
			for (size_t off = 0; off < size; off += ZMM_SIZE) {
				__m512i v = _mm512_load_si512((const __m512i *)(buf + off));
				v		  = _mm512_add_epi64(v, add_val);
				_mm512_store_si512((__m512i *)(buf + off), v);
				checksum = _mm512_xor_si512(checksum, v);
				ops++;
				uint64_t bytes = ops * CACHE_LINE_SIZE;
				update_stats(ctx, ops, bytes, bytes);
				if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
					break;
			}
		}
	}

	uint64_t cs[8];
	_mm512_storeu_si512((__m512i *)cs, checksum);
	ctx->stats->checksum = cs[0] ^ cs[1] ^ cs[2] ^ cs[3] ^ cs[4] ^ cs[5] ^
						   cs[6] ^ cs[7];
	ctx->stats->ops		 = ops;
	ctx->stats->bytes_rd = ops * CACHE_LINE_SIZE;
	ctx->stats->bytes_wr = ops * CACHE_LINE_SIZE;
}
