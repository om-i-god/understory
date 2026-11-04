/* Understory — the shared hidden ecosystem under every scene.
 *
 * All fields are 0..1 unless noted. Scenes READ fields to shape what they show and WRITE (add to) fields
 * through us_world_add() so contributions are dt-scaled and clamped. us_world_step() runs the couplings
 * (rain -> moisture -> water level, calm -> fireflies, bloom -> pollen -> reef/flock, energy -> seasons)
 * exactly once per video frame no matter how many sources are live.
 */
#pragma once
#include "us_audio.h"

typedef struct us_world {
	float moisture;     /* soil/air wetness. rain + storm raise it; sun dries it            */
	float water_level;  /* rivers, ponds, tide height. fed by moisture, drains slowly        */
	float storm_charge; /* builds with sustained energy, discharged by lightning             */
	float calm;         /* builds in quiet passages, drops in dense ones                     */
	float growth;       /* vegetation maturity (meadow, grass)                               */
	float bloom;        /* how much is flowering                                             */
	float mycelium;     /* underground network biomass; speeds growth                        */
	float pollen;       /* airborne pollen/seeds                                             */
	float insects;      /* firefly / insect population                                       */
	float flock;        /* birds in the area                                                 */
	float reef;         /* coral / lichen coverage                                           */
	float season;       /* 0..4 continuous: 0 spring, 1 summer, 2 autumn, 3 winter, wraps   */
	float daylight;     /* 0 night .. 1 noon (derived from day_phase)                        */
	float day_phase;    /* 0..1 continuous, 0 = midnight                                     */
	float wind;         /* prevailing wind strength, slow                                    */
	double age;         /* seconds this world has lived                                      */
	double energy;      /* cumulative musical energy                                          */
	/* bookkeeping */
	double last_step_t;
	int dirty;
} us_world;

void us_world_init(us_world *w);
/* add amount*dt to a field and clamp to 0..1 (pointer to a field of w) */
void us_world_add(us_world *w, float *field, float rate, float dt);
/* global couplings; t = monotonic seconds of the caller's clock; speed scales world time (1 = normal).
 * Returns 1 if a step happened (only once per distinct frame time). */
int us_world_step(us_world *w, const us_audio *a, double t, float dt, float speed);

/* season helpers: weights that sum to ~1, smoothly crossfading */
float us_world_season_w(const us_world *w, int season /*0..3*/);
/* winter-ness 0..1, handy for snow */
static inline float us_world_winter(const us_world *w) { return us_world_season_w(w, 3); }

int us_world_save(const us_world *w, const char *path);
int us_world_load(us_world *w, const char *path);

/* the process-wide shared world (lives in us_world.c) */
us_world *us_world_shared(void);
