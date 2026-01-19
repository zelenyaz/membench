#ifndef PRNG_H
#define PRNG_H

#include <stdint.h>

// xoshiro256** state
typedef struct {
	uint64_t s[4];
} prng_state_t;

// Initialize PRNG with a seed (uses splitmix64 internally)
void prng_init(prng_state_t *state, uint64_t seed);

// Get next 64-bit random number (xoshiro256**)
uint64_t prng_next(prng_state_t *state);

// Get random number in range [0, max)
static inline uint64_t prng_range(prng_state_t *state, uint64_t max)
{
	return prng_next(state) % max;
}

// splitmix64 for seeding
uint64_t splitmix64(uint64_t *state);

#endif // PRNG_H
