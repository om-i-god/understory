/* Understory — scene contract + engine (one engine per OBS source / harness run). */
#pragma once
#include "us_audio.h"
#include "us_canvas.h"
#include "us_compose.h"
#include "us_world.h"

/* The 3-macro contract every scene interprets, all 0..1, default 0.5. */
typedef struct us_params {
	float density;    /* how much stuff: counts, coverage, branching                  */
	float speed;      /* already folded into ctx->dt; scenes may also read it          */
	float reactivity; /* already folded into ctx->a; scenes may also read it           */
} us_params;

typedef struct us_ctx {
	float dt;        /* simulation dt (real dt * speed macro), seconds  */
	float real_dt;   /* wall-clock dt, seconds                          */
	double t;        /* simulation time, seconds (accumulated sim dt)   */
	const us_audio *a; /* audio features, already scaled by reactivity  */
	us_world *w;     /* shared (or private) world: read freely, write via us_world_add */
	const us_params *p;
	float aspect;    /* canvas width / height; x spans [0, aspect]       */
	int crisp;       /* 1 when drawing to a pixel-style canvas           */
} us_ctx;

typedef struct us_scene_vt {
	const char *id;    /* stable id, saved in OBS settings */
	const char *name;  /* shown in the dropdown            */
	float trails;      /* default persistence 0..1 (0 = clear each frame, 0.9 = long smears) */
	void *(*create)(uint32_t seed);
	void (*destroy)(void *s);
	/* simulate one step AND draw into c. The canvas has already been faded per trails.
	 * Must be resolution independent (normalized coords) and must tolerate aspect changes. */
	void (*frame)(void *s, const us_ctx *ctx, us_canvas *c);
} us_scene_vt;

extern const us_scene_vt *const us_scene_list[];
extern const int us_scene_count;
const us_scene_vt *us_scene_find(const char *id);

/* ---------------------------------------------------------------------- */

typedef struct us_engine {
	const us_scene_vt *vt;
	void *scene;
	uint32_t seed;

	us_canvas canvas;
	us_look look;
	int out_w, out_h, pixel_size;
	float ink_scale;
	uint8_t *rgba; /* canvas.w * canvas.h * 4 */

	int linked; /* 1 = use the shared world */
	us_world private_world;
	float world_speed;

	us_params params;
	float trails_bias; /* -0.5..0.5 added to the scene's default trails */
	double sim_t;
	double clock_t; /* wall clock used to de-duplicate world steps */

	/* scene-change dissolve */
	float xfade_len; /* seconds; 0 = hard cut */
	float xf_t;      /* 1 -> 0 while dissolving */
	uint8_t *xf_ink, *xf_mat;
	uint8_t *xf_thr; /* per-pixel dissolve order (organic noise), filled at each scene change */
	uint32_t xf_count;
	us_canvas disp;  /* composited display canvas used during a dissolve (and for bloom) */
	float *bloom_buf;
	float light_pulse; /* eased kick envelope that makes the bloom breathe with the music */
	size_t bloom_n;
} us_engine;

void us_engine_init(us_engine *e);
void us_engine_free(us_engine *e);
/* (re)create the scene if id or seed changed */
void us_engine_set_scene(us_engine *e, const char *id, uint32_t seed);
/* (re)allocate canvas if the style/size implies a new canvas size */
void us_engine_configure(us_engine *e, const us_look *look, int out_w, int out_h, int pixel_size, float ink_scale);
/* clock_t: monotonic wall seconds (same across sources so the shared world steps once per frame) */
void us_engine_frame(us_engine *e, const us_audio *a, float dt, double clock_t);
us_world *us_engine_world(us_engine *e);
