#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <stdint.h>

// Allocate aligned memory (64-byte alignment for cache lines)
void *mem_alloc_aligned(size_t size, size_t alignment);

// Free aligned memory
void mem_free_aligned(void *ptr);

// Touch all pages to avoid first-touch noise
void mem_touch_pages(void *ptr, size_t size);

// Zero-fill buffer
void mem_zero(void *ptr, size_t size);

// Fill buffer with pattern
void mem_fill_pattern(void *ptr, size_t size, uint64_t seed);

// Bind memory region to a NUMA node via mbind()
void mem_set_numa_policy(void *ptr, size_t size, int node);

// Restore default NUMA memory policy on a memory region
void mem_restore_numa_policy(void *ptr, size_t size);

#endif // MEMORY_H
