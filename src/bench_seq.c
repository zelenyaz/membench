#include <stdio.h>
#include <string.h>
#include <time.h>
#ifndef USE_64BIT
#include <immintrin.h>
#endif
#include "bench.h"

#define CACHE_LINE_SIZE 64

#ifdef USE_64BIT
#define ACCESS_SIZE 8 // 64-bit = 8 bytes
#else
#define ACCESS_SIZE 64 // AVX-512 = 64 bytes (ZMM)
#endif

// How often to update stats (must be power of 2 - 1)
#define STATS_UPDATE_MASK 0xFFFF

// Software-prefetch lookahead for *_pf variants (in cache lines / bytes)
#define PREFETCH_DISTANCE 16
#define PREFETCH_BYTES	  (PREFETCH_DISTANCE * CACHE_LINE_SIZE)

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

// Sequential read
void bench_seq_read(worker_ctx_t *ctx)
{
	const char *buf	 = (const char *)ctx->buffer;
	size_t		size = ctx->buffer_size;
	uint64_t	ops	 = 0;
#ifdef USE_64BIT
	uint64_t checksum = 0;
#else
	__m512i checksum = _mm512_setzero_si512();
#endif

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
				for (size_t off = 0; off < region_size; off += ACCESS_SIZE) {
#ifdef USE_64BIT
					uint64_t v = *(const uint64_t *)(region + off);
					checksum ^= v;
#else
					__m512i v =
						_mm512_load_si512((const __m512i *)(region + off));
					checksum = _mm512_xor_si512(checksum, v);
#endif
					ops++;
					update_stats(ctx, ops, ops * ACCESS_SIZE, 0);
					if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
						break;
				}
			}
			region_idx++;
		}
	} else {
		while (!should_stop(ctx, ops)) {
			for (size_t off = 0; off < size; off += ACCESS_SIZE) {
#ifdef USE_64BIT
				uint64_t v = *(const uint64_t *)(buf + off);
				checksum ^= v;
#else
				__m512i v = _mm512_load_si512((const __m512i *)(buf + off));
				checksum  = _mm512_xor_si512(checksum, v);
#endif
				ops++;
				update_stats(ctx, ops, ops * ACCESS_SIZE, 0);
				if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
					break;
			}
		}
	}

#ifdef USE_64BIT
	ctx->stats->checksum = checksum;
#else
	uint64_t cs[8];
	_mm512_storeu_si512((__m512i *)cs, checksum);
	ctx->stats->checksum = cs[0] ^ cs[1] ^ cs[2] ^ cs[3] ^ cs[4] ^ cs[5] ^
						   cs[6] ^ cs[7];
#endif
	ctx->stats->ops		 = ops;
	ctx->stats->bytes_rd = ops * ACCESS_SIZE;
	ctx->stats->bytes_wr = 0;
}

// Sequential read with explicit software prefetch PREFETCH_DISTANCE lines
// ahead. Mirrors bench_seq_read; the only difference is a __builtin_prefetch
// (PREFETCHT0: read, high temporal locality) issued per cache line. The
// prefetch is a non-faulting hint, so the tail iterations targeting addresses
// just past the buffer end are harmless and statistically negligible.
void bench_seq_read_pf(worker_ctx_t *ctx)
{
	const char *buf	 = (const char *)ctx->buffer;
	size_t		size = ctx->buffer_size;
	uint64_t	ops	 = 0;
#ifdef USE_64BIT
	uint64_t checksum = 0;
#else
	__m512i checksum = _mm512_setzero_si512();
#endif

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
				for (size_t off = 0; off < region_size; off += ACCESS_SIZE) {
					__builtin_prefetch(region + off + PREFETCH_BYTES, 0, 3);
#ifdef USE_64BIT
					uint64_t v = *(const uint64_t *)(region + off);
					checksum ^= v;
#else
					__m512i v =
						_mm512_load_si512((const __m512i *)(region + off));
					checksum = _mm512_xor_si512(checksum, v);
#endif
					ops++;
					update_stats(ctx, ops, ops * ACCESS_SIZE, 0);
					if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
						break;
				}
			}
			region_idx++;
		}
	} else {
		while (!should_stop(ctx, ops)) {
			for (size_t off = 0; off < size; off += ACCESS_SIZE) {
				__builtin_prefetch(buf + off + PREFETCH_BYTES, 0, 3);
#ifdef USE_64BIT
				uint64_t v = *(const uint64_t *)(buf + off);
				checksum ^= v;
#else
				__m512i v = _mm512_load_si512((const __m512i *)(buf + off));
				checksum  = _mm512_xor_si512(checksum, v);
#endif
				ops++;
				update_stats(ctx, ops, ops * ACCESS_SIZE, 0);
				if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
					break;
			}
		}
	}

#ifdef USE_64BIT
	ctx->stats->checksum = checksum;
#else
	uint64_t cs[8];
	_mm512_storeu_si512((__m512i *)cs, checksum);
	ctx->stats->checksum = cs[0] ^ cs[1] ^ cs[2] ^ cs[3] ^ cs[4] ^ cs[5] ^
						   cs[6] ^ cs[7];
#endif
	ctx->stats->ops		 = ops;
	ctx->stats->bytes_rd = ops * ACCESS_SIZE;
	ctx->stats->bytes_wr = 0;
}

// Sequential read using scalar 8-byte loads (no AVX)
void bench_seq_read_scalar(worker_ctx_t *ctx)
{
	const uint64_t *buf	  = (const uint64_t *)ctx->buffer;
	size_t			count = ctx->buffer_size / sizeof(uint64_t);
	uint64_t		ops	  = 0;
	uint64_t		sum	  = 0;

	if (ctx->reuse_mode && ctx->region_bytes > 0) {
		size_t region_count = ctx->region_bytes / sizeof(uint64_t);
		if (region_count == 0)
			region_count = 1;
		size_t num_regions = count / region_count;
		if (num_regions == 0)
			num_regions = 1;

		size_t region_idx = 0;
		while (!should_stop(ctx, ops)) {
			const uint64_t *region =
				buf + (region_idx % num_regions) * region_count;

			for (uint64_t iter = 0;
				 iter < ctx->reuse_iter && !should_stop(ctx, ops); iter++) {
				for (size_t i = 0; i < region_count; i++) {
					sum += region[i];
					ops++;
					update_stats(ctx, ops, ops * sizeof(uint64_t), 0);
					if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
						break;
				}
			}
			region_idx++;
		}
	} else {
		while (!should_stop(ctx, ops)) {
			for (size_t i = 0; i < count; i++) {
				sum += buf[i];
				ops++;
				update_stats(ctx, ops, ops * sizeof(uint64_t), 0);
				if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
					break;
			}
		}
	}

	ctx->stats->checksum = sum;
	ctx->stats->ops		 = ops;
	ctx->stats->bytes_rd = ops * sizeof(uint64_t);
	ctx->stats->bytes_wr = 0;
}

// Sequential write
void bench_seq_write(worker_ctx_t *ctx)
{
	char	*buf  = (char *)ctx->buffer;
	size_t	 size = ctx->buffer_size;
	uint64_t ops  = 0;
#ifdef USE_64BIT
	uint64_t val = (uint64_t)(ctx->thread_id + 1);
#else
	__m512i val = _mm512_set1_epi64((long long)(ctx->thread_id + 1));
#endif

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
				for (size_t off = 0; off < region_size; off += ACCESS_SIZE) {
#ifdef USE_64BIT
					*(uint64_t *)(region + off) = val;
#else
					_mm512_store_si512((__m512i *)(region + off), val);
#endif
					ops++;
					update_stats(ctx, ops, 0, ops * ACCESS_SIZE);
					if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
						break;
				}
#ifdef USE_64BIT
				val++;
#else
				val = _mm512_add_epi64(val, _mm512_set1_epi64(1));
#endif
			}
			region_idx++;
		}
	} else {
		while (!should_stop(ctx, ops)) {
			for (size_t off = 0; off < size; off += ACCESS_SIZE) {
#ifdef USE_64BIT
				*(uint64_t *)(buf + off) = val;
#else
				_mm512_store_si512((__m512i *)(buf + off), val);
#endif
				ops++;
				update_stats(ctx, ops, 0, ops * ACCESS_SIZE);
				if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
					break;
			}
#ifdef USE_64BIT
			val++;
#else
			val = _mm512_add_epi64(val, _mm512_set1_epi64(1));
#endif
		}
	}

#ifdef USE_64BIT
	ctx->stats->checksum = val;
#else
	uint64_t cs[8];
	_mm512_storeu_si512((__m512i *)cs, val);
	ctx->stats->checksum = cs[0];
#endif
	ctx->stats->ops		 = ops;
	ctx->stats->bytes_rd = 0;
	ctx->stats->bytes_wr = ops * ACCESS_SIZE;
}

// Sequential read+write (1:1)
void bench_seq_rw(worker_ctx_t *ctx)
{
	char	*buf  = (char *)ctx->buffer;
	size_t	 size = ctx->buffer_size;
	uint64_t ops  = 0;
#ifdef USE_64BIT
	uint64_t checksum = 0;
#else
	__m512i checksum = _mm512_setzero_si512();
	__m512i add_val	 = _mm512_set1_epi64(1);
#endif

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
				for (size_t off = 0; off < region_size; off += ACCESS_SIZE) {
#ifdef USE_64BIT
					uint64_t v = *(uint64_t *)(region + off);
					v++;
					*(uint64_t *)(region + off) = v;
					checksum ^= v;
#else
					__m512i v =
						_mm512_load_si512((const __m512i *)(region + off));
					v = _mm512_add_epi64(v, add_val);
					_mm512_store_si512((__m512i *)(region + off), v);
					checksum = _mm512_xor_si512(checksum, v);
#endif
					ops++;
					uint64_t bytes = ops * ACCESS_SIZE;
					update_stats(ctx, ops, bytes, bytes);
					if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
						break;
				}
			}
			region_idx++;
		}
	} else {
		while (!should_stop(ctx, ops)) {
			for (size_t off = 0; off < size; off += ACCESS_SIZE) {
#ifdef USE_64BIT
				uint64_t v = *(uint64_t *)(buf + off);
				v++;
				*(uint64_t *)(buf + off) = v;
				checksum ^= v;
#else
				__m512i v = _mm512_load_si512((const __m512i *)(buf + off));
				v		  = _mm512_add_epi64(v, add_val);
				_mm512_store_si512((__m512i *)(buf + off), v);
				checksum = _mm512_xor_si512(checksum, v);
#endif
				ops++;
				uint64_t bytes = ops * ACCESS_SIZE;
				update_stats(ctx, ops, bytes, bytes);
				if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
					break;
			}
		}
	}

#ifdef USE_64BIT
	ctx->stats->checksum = checksum;
#else
	uint64_t cs[8];
	_mm512_storeu_si512((__m512i *)cs, checksum);
	ctx->stats->checksum = cs[0] ^ cs[1] ^ cs[2] ^ cs[3] ^ cs[4] ^ cs[5] ^
						   cs[6] ^ cs[7];
#endif
	ctx->stats->ops		 = ops;
	ctx->stats->bytes_rd = ops * ACCESS_SIZE;
	ctx->stats->bytes_wr = ops * ACCESS_SIZE;
}

// Sequential dual-buffer: rw on buf1 then wr on buf2 at matching offsets per iter
void bench_seq_2buf_rw_wr(worker_ctx_t *ctx)
{
	char	*buf1 = (char *)ctx->buffer;
	char	*buf2 = (char *)ctx->buffer2;
	size_t	 size = ctx->buffer_size;
	uint64_t ops  = 0;
#ifdef USE_64BIT
	uint64_t val	  = (uint64_t)(ctx->thread_id + 1);
	uint64_t checksum = 0;
#else
	__m512i val		 = _mm512_set1_epi64((long long)(ctx->thread_id + 1));
	__m512i checksum = _mm512_setzero_si512();
	__m512i add_val	 = _mm512_set1_epi64(1);
#endif

	while (!should_stop(ctx, ops)) {
		for (size_t off = 0; off < size; off += ACCESS_SIZE) {
#ifdef USE_64BIT
			// rw on buf1
			uint64_t v1 = *(uint64_t *)(buf1 + off);
			v1++;
			*(uint64_t *)(buf1 + off) = v1;
			checksum ^= v1;
			// wr on buf2
			*(uint64_t *)(buf2 + off) = val;
			uint64_t v2				  = *(uint64_t *)(buf2 + off);
			checksum ^= v2;
			val++;
#else
			// rw on buf1
			__m512i v1 = _mm512_load_si512((const __m512i *)(buf1 + off));
			v1		   = _mm512_add_epi64(v1, add_val);
			_mm512_store_si512((__m512i *)(buf1 + off), v1);
			checksum = _mm512_xor_si512(checksum, v1);
			// wr on buf2
			_mm512_store_si512((__m512i *)(buf2 + off), val);
			__m512i v2 = _mm512_load_si512((const __m512i *)(buf2 + off));
			checksum   = _mm512_xor_si512(checksum, v2);
			val		   = _mm512_add_epi64(val, add_val);
#endif
			ops++;
			uint64_t bytes = ops * ACCESS_SIZE * 2;
			update_stats(ctx, ops, bytes, bytes);
			if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
				break;
		}
	}

#ifdef USE_64BIT
	ctx->stats->checksum = checksum;
#else
	uint64_t cs[8];
	_mm512_storeu_si512((__m512i *)cs, checksum);
	ctx->stats->checksum = cs[0] ^ cs[1] ^ cs[2] ^ cs[3] ^ cs[4] ^ cs[5] ^
						   cs[6] ^ cs[7];
#endif
	ctx->stats->ops		 = ops;
	ctx->stats->bytes_rd = ops * ACCESS_SIZE * 2;
	ctx->stats->bytes_wr = ops * ACCESS_SIZE * 2;
}

// Sequential write+read (1:1) — mirror of seq_rw: store before load at same addr
void bench_seq_wr(worker_ctx_t *ctx)
{
	char	*buf  = (char *)ctx->buffer;
	size_t	 size = ctx->buffer_size;
	uint64_t ops  = 0;
#ifdef USE_64BIT
	uint64_t val	  = (uint64_t)(ctx->thread_id + 1);
	uint64_t checksum = 0;
#else
	__m512i val		 = _mm512_set1_epi64((long long)(ctx->thread_id + 1));
	__m512i checksum = _mm512_setzero_si512();
	__m512i add_val	 = _mm512_set1_epi64(1);
#endif

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
				for (size_t off = 0; off < region_size; off += ACCESS_SIZE) {
#ifdef USE_64BIT
					*(uint64_t *)(region + off) = val;
					uint64_t v					= *(uint64_t *)(region + off);
					checksum ^= v;
					val++;
#else
					_mm512_store_si512((__m512i *)(region + off), val);
					__m512i v =
						_mm512_load_si512((const __m512i *)(region + off));
					checksum = _mm512_xor_si512(checksum, v);
					val		 = _mm512_add_epi64(val, add_val);
#endif
					ops++;
					uint64_t bytes = ops * ACCESS_SIZE;
					update_stats(ctx, ops, bytes, bytes);
					if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
						break;
				}
			}
			region_idx++;
		}
	} else {
		while (!should_stop(ctx, ops)) {
			for (size_t off = 0; off < size; off += ACCESS_SIZE) {
#ifdef USE_64BIT
				*(uint64_t *)(buf + off) = val;
				uint64_t v				 = *(uint64_t *)(buf + off);
				checksum ^= v;
				val++;
#else
				_mm512_store_si512((__m512i *)(buf + off), val);
				__m512i v = _mm512_load_si512((const __m512i *)(buf + off));
				checksum  = _mm512_xor_si512(checksum, v);
				val		  = _mm512_add_epi64(val, add_val);
#endif
				ops++;
				uint64_t bytes = ops * ACCESS_SIZE;
				update_stats(ctx, ops, bytes, bytes);
				if ((ops & STATS_UPDATE_MASK) == 0 && should_stop(ctx, ops))
					break;
			}
		}
	}

#ifdef USE_64BIT
	ctx->stats->checksum = checksum;
#else
	uint64_t cs[8];
	_mm512_storeu_si512((__m512i *)cs, checksum);
	ctx->stats->checksum = cs[0] ^ cs[1] ^ cs[2] ^ cs[3] ^ cs[4] ^ cs[5] ^
						   cs[6] ^ cs[7];
#endif
	ctx->stats->ops		 = ops;
	ctx->stats->bytes_rd = ops * ACCESS_SIZE;
	ctx->stats->bytes_wr = ops * ACCESS_SIZE;
}
