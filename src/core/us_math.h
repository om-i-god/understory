/* Understory — small math / rng / noise helpers shared by every scene. */
#pragma once
#include <math.h>
#include <stdint.h>

#ifndef US_PI
#define US_PI 3.14159265358979323846f
#define US_TAU 6.28318530717958647692f
#endif

static inline float us_clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float us_sat(float x) { return us_clampf(x, 0.0f, 1.0f); }
static inline float us_lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float us_smooth(float e0, float e1, float x)
{
	float t = us_sat((x - e0) / (e1 - e0));
	return t * t * (3.0f - 2.0f * t);
}
/* frame-rate independent exponential approach: move cur toward target with time constant tau (s) */
static inline float us_approach(float cur, float target, float tau, float dt)
{
	if (tau <= 0.0f)
		return target;
	return target + (cur - target) * expf(-dt / tau);
}

/* ---- RNG: PCG32 ---------------------------------------------------- */
typedef struct us_rng {
	uint64_t state, inc;
} us_rng;

static inline uint32_t us_rng_u32(us_rng *r)
{
	uint64_t old = r->state;
	r->state = old * 6364136223846793005ULL + (r->inc | 1u);
	uint32_t xs = (uint32_t)(((old >> 18u) ^ old) >> 27u);
	uint32_t rot = (uint32_t)(old >> 59u);
	return (xs >> rot) | (xs << ((-rot) & 31));
}
static inline void us_rng_seed(us_rng *r, uint64_t seed)
{
	r->state = 0;
	r->inc = (seed << 1u) | 1u;
	us_rng_u32(r);
	r->state += seed * 0x9E3779B97F4A7C15ULL;
	us_rng_u32(r);
}
/* [0,1) */
static inline float us_rand(us_rng *r) { return (us_rng_u32(r) >> 8) * (1.0f / 16777216.0f); }
/* [a,b) */
static inline float us_randr(us_rng *r, float a, float b) { return a + (b - a) * us_rand(r); }
/* approx gaussian, mean 0 sd 1 */
static inline float us_randn(us_rng *r)
{
	return (us_rand(r) + us_rand(r) + us_rand(r) + us_rand(r) - 2.0f) * 1.7320508f;
}

/* ---- Noise --------------------------------------------------------- */
/* Gradient noise in [-1,1]. Deterministic, no init needed. */
float us_noise2(float x, float y);
float us_noise3(float x, float y, float z);
/* fractal sum, octaves 1..8, result roughly [-1,1] */
float us_fbm2(float x, float y, int octaves);
float us_fbm3(float x, float y, float z, int octaves);
