#include "prng.h"

// Initialize xorshift64 state from a single seed
void prng_init(prng_state_t *state, uint64_t seed)
{
	state->s[0] = seed ? seed : 1; // Ensure non-zero state
}

// xorshift64 generator
inline uint64_t prng_next(prng_state_t *state)
{
	uint64_t x = state->s[0];
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	state->s[0] = x;
	return x;
}
