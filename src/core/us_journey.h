/* Understory — the journey: "follow the music" scene director.
 *
 * Listens to the slow character of the music (energy, brightness, how percussive, how calm) together with
 * the shared world (storm charge, water, insects, season, time of day) and moves to the scene that suits
 * the moment. It changes only at musical turning points — a drop arriving, a breakdown, a long change of
 * mood — and never sooner than a minimum dwell, so scenes have time to grow.
 */
#pragma once
#include "us_audio.h"
#include "us_world.h"

typedef struct us_journey {
	float energy, bright, perc, calm; /* slow mood (~20 s) */
	float fast_energy;                /* phrase-scale (~3 s) */
	float onset_rate;                 /* hits per second, smoothed */
	float lo, hi;                     /* running range of loudness this set (relative mood) */
	float dwell;                      /* seconds in the current scene */
	float settle;                     /* mood energy when the current scene began */
	int recent[4];                    /* last scenes visited (indices), -1 = empty */
	uint32_t rng;
} us_journey;

void us_journey_init(us_journey *j, uint32_t seed);
/* Call once per frame. cur = current scene index. min_dwell / max_dwell in seconds.
 * Returns the index of the scene to switch to, or -1 to stay. */
int us_journey_step(us_journey *j, const us_audio *a, const us_world *w, float dt, int cur, float min_dwell,
		    float max_dwell);
/* Score of each scene for the current moment (for tests/diagnostics). */
float us_journey_score(const us_journey *j, const us_world *w, int scene_idx);
