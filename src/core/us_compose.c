#include "us_compose.h"
#include "us_math.h"
#include <string.h>

#define C3(r, g, b) {r, g, b}
#define R(m0, m1) {m0, m1}

/* Palettes: BG, then ramps for BG,EARTH,STEM,LEAF,BLOOM,WATER,LIGHT,ACCENT (dark -> bright). */
const us_palette_def us_palettes[] = {
	{"moss", "Moss",
	 C3(16, 20, 31),
	 {R(C3(16, 20, 31), C3(60, 70, 90)), R(C3(45, 58, 46), C3(120, 110, 80)), R(C3(70, 110, 72), C3(167, 201, 87)),
	  R(C3(91, 140, 90), C3(212, 224, 155)), R(C3(160, 50, 90), C3(255, 160, 190)), R(C3(40, 90, 140), C3(130, 210, 240)),
	  R(C3(200, 160, 60), C3(255, 236, 160)), R(C3(140, 120, 150), C3(242, 232, 207))}},
	{"dusk", "Dusk",
	 C3(24, 16, 36),
	 {R(C3(24, 16, 36), C3(70, 50, 90)), R(C3(60, 36, 60), C3(130, 80, 90)), R(C3(90, 50, 80), C3(200, 120, 120)),
	  R(C3(110, 70, 110), C3(230, 160, 150)), R(C3(200, 90, 90), C3(255, 200, 140)), R(C3(70, 80, 150), C3(160, 170, 240)),
	  R(C3(230, 150, 80), C3(255, 230, 170)), R(C3(160, 110, 160), C3(250, 220, 230))}},
	{"riverbed", "Riverbed",
	 C3(8, 22, 26),
	 {R(C3(8, 22, 26), C3(30, 60, 64)), R(C3(50, 60, 50), C3(140, 130, 100)), R(C3(40, 90, 80), C3(110, 180, 150)),
	  R(C3(50, 120, 100), C3(150, 220, 170)), R(C3(200, 120, 60), C3(255, 210, 120)), R(C3(30, 110, 140), C3(150, 240, 240)),
	  R(C3(150, 200, 200), C3(240, 255, 250)), R(C3(110, 140, 120), C3(220, 235, 220))}},
	{"ember", "Ember",
	 C3(18, 10, 8),
	 {R(C3(18, 10, 8), C3(60, 30, 20)), R(C3(70, 40, 25), C3(150, 90, 50)), R(C3(110, 50, 30), C3(220, 120, 60)),
	  R(C3(140, 70, 30), C3(250, 170, 70)), R(C3(200, 40, 40), C3(255, 140, 90)), R(C3(90, 60, 70), C3(200, 150, 140)),
	  R(C3(230, 160, 50), C3(255, 245, 190)), R(C3(160, 100, 80), C3(250, 220, 190))}},
	{"glacier", "Glacier",
	 C3(10, 14, 28),
	 {R(C3(10, 14, 28), C3(40, 50, 80)), R(C3(50, 60, 80), C3(140, 150, 170)), R(C3(60, 90, 130), C3(150, 190, 230)),
	  R(C3(70, 120, 150), C3(180, 230, 240)), R(C3(140, 110, 200), C3(230, 210, 255)), R(C3(40, 90, 170), C3(150, 210, 255)),
	  R(C3(170, 220, 255), C3(250, 255, 255)), R(C3(110, 130, 170), C3(230, 235, 250))}},
	{"lichen", "Lichen (4-shade)",
	 C3(15, 30, 20),
	 {R(C3(15, 30, 20), C3(48, 82, 52)), R(C3(48, 82, 52), C3(110, 150, 80)), R(C3(48, 82, 52), C3(140, 180, 90)),
	  R(C3(80, 120, 60), C3(170, 205, 110)), R(C3(110, 150, 80), C3(205, 230, 150)), R(C3(48, 82, 52), C3(155, 190, 100)),
	  R(C3(140, 180, 90), C3(225, 245, 180)), R(C3(80, 120, 60), C3(190, 220, 130))}},
};
const int us_palette_count = (int)(sizeof(us_palettes) / sizeof(us_palettes[0]));

void us_style_canvas_size(int style, int out_w, int out_h, int pixel_size, float ink_scale, int *cw, int *ch)
{
	if (style == US_STYLE_INK) {
		if (ink_scale < 0.25f) ink_scale = 0.25f;
		if (ink_scale > 1.0f) ink_scale = 1.0f;
		*cw = (int)(out_w * ink_scale + 0.5f);
		*ch = (int)(out_h * ink_scale + 0.5f);
	} else {
		if (pixel_size < 1) pixel_size = 1;
		*cw = out_w / pixel_size;
		*ch = out_h / pixel_size;
	}
	if (*cw < 16) *cw = 16;
	if (*ch < 9) *ch = 9;
}

void us_compose(const us_canvas *c, const us_look *look, uint8_t *rgba)
{
	size_t n = (size_t)c->w * c->h;
	const uint8_t *ink = c->ink, *mat = c->mat;
	float glow = us_sat(look->glow);
	/* brightness LUT: gentle gamma lift so faint trails survive projectors */
	uint8_t lut[256];
	float gamma = 1.0f - 0.45f * glow;
	for (int i = 0; i < 256; i++)
		lut[i] = (uint8_t)(255.0f * powf(i / 255.0f, gamma) + 0.5f);

	if (look->style == US_STYLE_PIXEL) {
		int pi = look->palette;
		if (pi < 0 || pi >= us_palette_count)
			pi = 0;
		const us_palette_def *p = &us_palettes[pi];
		/* 4 shade steps per material ramp, plus background */
		uint8_t tab[US_MAT_COUNT][5][3];
		for (int m = 0; m < US_MAT_COUNT; m++) {
			for (int k = 0; k < 3; k++)
				tab[m][0][k] = p->bg[k];
			for (int s = 1; s <= 4; s++) {
				float t = (s - 1) / 3.0f;
				for (int k = 0; k < 3; k++)
					tab[m][s][k] = (uint8_t)us_lerp(p->ramp[m][0][k], p->ramp[m][1][k], t);
			}
		}
		for (size_t i = 0; i < n; i++) {
			int v = lut[ink[i]];
			int step = v < 24 ? 0 : 1 + (v - 24) * 4 / 232;
			if (step > 4) step = 4;
			int m = mat[i] < US_MAT_COUNT ? mat[i] : 0;
			const uint8_t *col = tab[m][step];
			uint8_t *o = rgba + i * 4;
			if (look->invert) {
				o[0] = 255 - col[0];
				o[1] = 255 - col[1];
				o[2] = 255 - col[2];
			} else {
				o[0] = col[0];
				o[1] = col[1];
				o[2] = col[2];
			}
			o[3] = 255;
		}
		return;
	}

	/* Ink / Mono: tint * level */
	uint8_t tab[256][3];
	for (int i = 0; i < 256; i++) {
		int v = lut[i];
		if (look->style == US_STYLE_MONO) {
			int lvl = (v * 15 + 127) / 255; /* 16 levels, like the norns OLED */
			v = lvl * 17;
		}
		static const int paper[3] = {242, 238, 226};
		for (int k = 0; k < 3; k++) {
			int t = look->tint[k];
			int out;
			if (look->invert) /* paper mode: light ground, ink is the tint pushed dark */
				out = paper[k] + ((t / 4) - paper[k]) * v / 255;
			else
				out = (t * v) / 255;
			tab[i][k] = (uint8_t)out;
		}
	}
	for (size_t i = 0; i < n; i++) {
		const uint8_t *col = tab[ink[i]];
		uint8_t *o = rgba + i * 4;
		o[0] = col[0];
		o[1] = col[1];
		o[2] = col[2];
		o[3] = 255;
	}
}
