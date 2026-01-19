#include "prng.h"

// splitmix64 for seeding xoshiro256**
uint64_t splitmix64(uint64_t *state)
{
	uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
	z		   = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z		   = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

// Initialize xoshiro256** state from a single seed
void prng_init(prng_state_t *state, uint64_t seed)
{
	uint64_t sm = seed;
	state->s[0] = splitmix64(&sm);
	state->s[1] = splitmix64(&sm);
	state->s[2] = splitmix64(&sm);
	state->s[3] = splitmix64(&sm);
}

// Rotate left helper
static inline uint64_t rotl(uint64_t x, int k)
{
	return (x << k) | (x >> (64 - k));
}

// xoshiro256** generator
uint64_t prng_next(prng_state_t *state)
{
	const uint64_t result = rotl(state->s[1] * 5, 7) * 9;
	const uint64_t t	  = state->s[1] << 17;

	state->s[2] ^= state->s[0];
	state->s[3] ^= state->s[1];
	state->s[1] ^= state->s[2];
	state->s[0] ^= state->s[3];

	state->s[2] ^= t;
	state->s[3] = rotl(state->s[3], 45);

	return result;
}
