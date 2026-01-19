#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "memory.h"

void *mem_alloc_aligned(size_t size, size_t alignment)
{
	void *ptr = NULL;
	if (posix_memalign(&ptr, alignment, size) != 0) {
		return NULL;
	}
	return ptr;
}

void mem_free_aligned(void *ptr)
{
	free(ptr);
}

void mem_touch_pages(void *ptr, size_t size)
{
	volatile char *p		 = (volatile char *)ptr;
	size_t		   page_size = 4096;

	for (size_t i = 0; i < size; i += page_size) {
		p[i] = 0;
	}
}

void mem_zero(void *ptr, size_t size)
{
	memset(ptr, 0, size);
}

void mem_fill_pattern(void *ptr, size_t size, uint64_t seed)
{
	uint64_t *p		= (uint64_t *)ptr;
	size_t	  count = size / sizeof(uint64_t);

	// Simple pattern fill
	for (size_t i = 0; i < count; i++) {
		p[i] = seed ^ (uint64_t)i;
	}
}
