/* Understory — turns a canvas (ink + material) into RGBA for display. */
#pragma once
#include "us_canvas.h"
#include <stddef.h>

enum us_style { US_STYLE_INK = 0, US_STYLE_PIXEL = 1, US_STYLE_MONO = 2, US_STYLE_COUNT };

typedef struct us_look {
	int style;         /* enum us_style */
	uint8_t tint[3];   /* Ink + Mono-pixel colour */
	int palette;       /* Pixel palette index */
	int invert;        /* paper mode: dark ink on light ground */
	float glow;        /* 0..1 gamma lift so faint trails read on projectors */
	float bloom;       /* 0..1 soft light bleed around bright lines (Ink only) */
} us_look;

typedef struct us_palette_def {
	const char *id, *name;
	uint8_t bg[3];
	/* per material: dark end + bright end of its ramp */
	uint8_t ramp[US_MAT_COUNT][2][3];
} us_palette_def;

extern const us_palette_def us_palettes[];
extern const int us_palette_count;

/* rgba must hold w*h*4 bytes */
void us_compose(const us_canvas *c, const us_look *look, uint8_t *rgba);
/* Canvas size each style wants for a given output size. pixel_size = output px per canvas px (pixel styles),
 * ink_scale = canvas/output ratio for Ink. */
void us_style_canvas_size(int style, int out_w, int out_h, int pixel_size, float ink_scale, int *cw, int *ch);

/* Soft bloom: adds a blurred copy of the bright parts back onto c->ink, in place.
 * scratch must hold at least us_bloom_scratch(c->w, c->h) floats. */
size_t us_bloom_scratch(int w, int h);
void us_bloom(us_canvas *c, float amount, float *scratch);
