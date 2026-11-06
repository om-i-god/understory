/* Understory — turns a canvas (ink + material) into RGBA for display. */
#pragma once
#include "us_canvas.h"
#include <stddef.h>

enum us_style { US_STYLE_INK = 0, US_STYLE_PIXEL = 1, US_STYLE_MONO = 2, US_STYLE_COLOR = 3, US_STYLE_COUNT };
/* full-resolution anti-aliased line styles (Ink, and Ink in colour) */
static inline int us_style_is_ink(int style) { return style == US_STYLE_INK || style == US_STYLE_COLOR; }

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

/* Soft bloom on the composed picture: adds a blurred copy of its bright parts back, in colour, in place
 * (in paper mode it bleeds the ink instead). scratch must hold us_bloom_scratch(w, h) floats. */
size_t us_bloom_scratch(int w, int h);
void us_bloom_rgba(uint8_t *rgba, int w, int h, int invert, float amount, float *scratch);
