#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>

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

#endif // MEMORY_H
