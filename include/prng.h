#ifndef PRNG_H
#define PRNG_H

#include <stdint.h>

// xorshift64 state
typedef struct {
	uint64_t s[1];
} prng_state_t;

// Initialize PRNG with a seed
void prng_init(prng_state_t *state, uint64_t seed);

// Get next 64-bit random number (xorshift64)
uint64_t prng_next(prng_state_t *state);

// Get random number in range [0, max)
static inline uint64_t prng_range(prng_state_t *state, uint64_t max)
{
	return prng_next(state) % max;
}

#endif // PRNG_H
