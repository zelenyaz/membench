#ifndef PRNG_H
#define PRNG_H

#include <stdint.h>

// xorshift64 state
typedef struct {
	uint64_t s[1];
} prng_state_t;

// Initialize PRNG with a seed
static inline void prng_init(prng_state_t *state, uint64_t seed)
{
	state->s[0] = seed ? seed : 1; // Ensure non-zero state
}

// Get next 64-bit random number (xorshift64)
static inline uint64_t prng_next(prng_state_t *state)
{
	uint64_t x = state->s[0];
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	state->s[0] = x;
	return x;
}

#endif // PRNG_H
