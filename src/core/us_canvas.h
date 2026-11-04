/* Understory — canvas + rasteriser.
 *
 * Scenes draw in NORMALIZED units: y in [0,1] top->bottom, x in [0, aspect].
 * Sizes / radii / widths are in the same units (1.0 == canvas height).
 * A pixel holds `ink` (brightness 0..255) and `mat` (what it is, for Pixel palettes).
 * `crisp` canvases (Pixel / Mono-pixel styles) draw without anti-aliasing.
 */
#pragma once
#include <stdint.h>

enum us_mat {
	US_MAT_BG = 0,
	US_MAT_EARTH, /* soil, rock, ground, sand          */
	US_MAT_STEM,  /* stems, trunks, threads, hyphae    */
	US_MAT_LEAF,  /* foliage, grass, moss, coral        */
	US_MAT_BLOOM, /* flowers, fruiting bodies, accents  */
	US_MAT_WATER, /* water, rain, foam, ripples         */
	US_MAT_LIGHT, /* fireflies, aurora, lightning, sky  */
	US_MAT_ACCENT,/* birds, seeds, anything else        */
	US_MAT_COUNT
};

enum us_blend { US_BLEND_MAX = 0, US_BLEND_ADD = 1 };

typedef struct us_canvas {
	int w, h;
	float s;      /* pixels per normalized unit (== h) */
	float aspect; /* w / h */
	uint8_t *ink;
	uint8_t *mat;
	int crisp; /* 1: no anti-aliasing (pixel styles) */
	int blend; /* enum us_blend, set by scenes before drawing; host resets to MAX each frame */
} us_canvas;

int us_canvas_init(us_canvas *c, int w, int h, int crisp);
void us_canvas_free(us_canvas *c);
void us_canvas_clear(us_canvas *c);
/* multiply all ink by keep (0..1). Used by the host for trails. */
void us_canvas_fade(us_canvas *c, float keep);

/* v = brightness 0..1 for all primitives */
void us_point(us_canvas *c, float x, float y, float r, float v, uint8_t mat);
void us_disc(us_canvas *c, float x, float y, float r, float v, uint8_t mat);
void us_ring(us_canvas *c, float x, float y, float r, float width, float v, uint8_t mat);
void us_arc(us_canvas *c, float x, float y, float r, float a0, float a1, float width, float v, uint8_t mat);
void us_line(us_canvas *c, float x0, float y0, float x1, float y1, float width, float v, uint8_t mat);
/* xy = interleaved x,y pairs, n points */
void us_polyline(us_canvas *c, const float *xy, int n, float width, float v, uint8_t mat);
void us_tri(us_canvas *c, float x0, float y0, float x1, float y1, float x2, float y2, float v, uint8_t mat);
/* draw a scalar field f[fw*fh] (values ~0..1, times gain) bilinearly over the normalized rect */
void us_field(us_canvas *c, const float *f, int fw, int fh, float gain, uint8_t mat, float rx0, float ry0,
	      float rx1, float ry1);

/* raw pixel write, coverage-weighted, respects blend mode (exposed for scenes doing their own loops) */
void us_put(us_canvas *c, int px, int py, float v, uint8_t mat);
