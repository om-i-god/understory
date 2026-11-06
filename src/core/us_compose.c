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
	if (us_style_is_ink(style)) {
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

	if (look->style == US_STYLE_COLOR) {
		/* Ink in colour: full-res AA lines, each material its palette hue. Faint values sit near the
		 * dark end of the ramp and fade into the ground; bright values reach the light end. */
		int pi = look->palette;
		if (pi < 0 || pi >= us_palette_count)
			pi = 0;
		const us_palette_def *p = &us_palettes[pi];
		uint8_t tab[US_MAT_COUNT][256][3]; /* 6 KB, rebuilt per frame (cheap, and thread-safe) */
		{
			float bg[3];
			for (int k = 0; k < 3; k++) /* a deeper ground than Pixel, so lines carry the colour */
				bg[k] = p->bg[k] * 0.55f;
			for (int m = 0; m < US_MAT_COUNT; m++)
				for (int i = 0; i < 256; i++) {
					float v = lut[i] / 255.0f;
					float cover = v * 2.2f > 1 ? 1 : v * 2.2f;
					for (int k = 0; k < 3; k++) {
						float hue = us_lerp(p->ramp[m][0][k], p->ramp[m][1][k], v);
						float o = bg[k] + (hue - bg[k]) * cover;
						if (look->invert) /* paper: light ground, coloured ink */
							o = 244 - (244 - hue * 0.7f) * cover;
						tab[m][i][k] = (uint8_t)(o < 0 ? 0 : o > 255 ? 255 : o);
					}
				}
		}
		for (size_t i = 0; i < n; i++) {
			int m = mat[i] < US_MAT_COUNT ? mat[i] : 0;
			const uint8_t *col = tab[m][ink[i]];
			uint8_t *o = rgba + i * 4;
			o[0] = col[0];
			o[1] = col[1];
			o[2] = col[2];
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

/* ---------------------------------------------------------------- bloom */

#define BLOOM_DS 4

size_t us_bloom_scratch(int w, int h)
{
	size_t bw = (size_t)(w + BLOOM_DS - 1) / BLOOM_DS, bh = (size_t)(h + BLOOM_DS - 1) / BLOOM_DS;
	/* tight + wide planes per channel (6), one blur temp, one interpolated row per channel */
	return bw * bh * 7 + bw * 3 + 16;
}

static void box_blur(float *a, float *tmp, int w, int h, int r)
{
	float inv = 1.0f / (2 * r + 1);
	for (int y = 0; y < h; y++) {
		float *row = a + (size_t)y * w, *o = tmp + (size_t)y * w;
		float acc = row[0] * (r + 1);
		for (int i = 1; i <= r; i++)
			acc += row[i < w ? i : w - 1];
		for (int x = 0; x < w; x++) {
			o[x] = acc * inv;
			int add = x + r + 1, sub = x - r;
			acc += row[add < w ? add : w - 1] - row[sub > 0 ? sub : 0];
		}
	}
	for (int x = 0; x < w; x++) {
		float acc = tmp[x] * (r + 1);
		for (int i = 1; i <= r; i++)
			acc += tmp[(size_t)(i < h ? i : h - 1) * w + x];
		for (int y = 0; y < h; y++) {
			a[(size_t)y * w + x] = acc * inv;
			int add = y + r + 1, sub = y - r;
			acc += tmp[(size_t)(add < h ? add : h - 1) * w + x] - tmp[(size_t)(sub > 0 ? sub : 0) * w + x];
		}
	}
}

/* Soft bloom in colour, after compose: a low-res copy of the bright parts of the picture is blurred at two
 * scales and added back with bilinear upsampling. Working on RGB means a glow takes the real colour of the
 * light that casts it and blends smoothly where two colours meet. In paper mode the same is done to the
 * darkness, so ink bleeds softly into the paper instead. */
void us_bloom_rgba(uint8_t *rgba, int w, int h, int invert, float amount, float *scratch)
{
	if (amount <= 0.001f || !rgba || w < 1 || h < 1)
		return;
	int bw = (w + BLOOM_DS - 1) / BLOOM_DS, bh = (h + BLOOM_DS - 1) / BLOOM_DS;
	size_t nb = (size_t)bw * bh;
	float *tight = scratch, *wide = scratch + nb * 3, *tmp = scratch + nb * 6, *ir = scratch + nb * 7;
	/* downsample: per block and channel, a mix of mean and max (1 px lines in a 4x4 block still glow) */
	for (int by = 0; by < bh; by++) {
		int y0 = by * BLOOM_DS, y1 = y0 + BLOOM_DS < h ? y0 + BLOOM_DS : h;
		for (int bx = 0; bx < bw; bx++) {
			int x0 = bx * BLOOM_DS, x1 = x0 + BLOOM_DS < w ? x0 + BLOOM_DS : w;
			int sum[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
			for (int y = y0; y < y1; y++) {
				const uint8_t *p = rgba + ((size_t)y * w + x0) * 4;
				for (int x = x0; x < x1; x++, p += 4)
					for (int k = 0; k < 3; k++) {
						int v = invert ? 255 - p[k] : p[k];
						sum[k] += v;
						mx[k] = v > mx[k] ? v : mx[k];
					}
			}
			float cnt = (float)((y1 - y0) * (x1 - x0));
			for (int k = 0; k < 3; k++) {
				float v = (0.5f * sum[k] / cnt + 0.5f * mx[k]) / 255.0f - 0.04f; /* the ground itself never glows */
				v = v > 0 ? v : 0;
				tight[nb * k + (size_t)by * bw + bx] = v * v;
			}
		}
	}
	/* two scales: a tight halo (point lights, lines) and a wide haze (big bright areas) */
	memcpy(wide, tight, sizeof(float) * nb * 3);
	int r = bw > 200 ? 4 : 2;
	for (int k = 0; k < 3; k++) {
		box_blur(tight + nb * k, tmp, bw, bh, 1);
		box_blur(tight + nb * k, tmp, bw, bh, 1);
		box_blur(wide + nb * k, tmp, bw, bh, r);
		box_blur(wide + nb * k, tmp, bw, bh, r);
	}
	for (size_t i = 0; i < nb * 3; i++)
		tight[i] = 0.55f * tight[i] + 1.1f * wide[i];
	/* upsample bilinear + add. With a 4x factor the 4 pixels of block k sample at fixed offsets:
	 * k-0.375, k-0.125 (between k-1 and k) and k+0.125, k+0.375 (between k and k+1). */
	float gain = 255.0f * 2.0f * amount;
	static const float TX[4] = {0.625f, 0.875f, 0.125f, 0.375f};
	for (int y = 0; y < h; y++) {
		float fy = (y + 0.5f) / BLOOM_DS - 0.5f;
		int y0 = (int)floorf(fy);
		float ty = fy - y0;
		int ya = y0 < 0 ? 0 : (y0 >= bh ? bh - 1 : y0), yb = y0 + 1 >= bh ? bh - 1 : (y0 + 1 < 0 ? 0 : y0 + 1);
		float rowmax = 0;
		for (int k = 0; k < 3; k++) {
			const float *ra = tight + nb * k + (size_t)ya * bw, *rb = tight + nb * k + (size_t)yb * bw;
			float *o = ir + (size_t)bw * k;
			for (int bx = 0; bx < bw; bx++) {
				o[bx] = (ra[bx] + (rb[bx] - ra[bx]) * ty) * gain;
				rowmax = o[bx] > rowmax ? o[bx] : rowmax;
			}
		}
		if (rowmax < 0.5f)
			continue;
		uint8_t *row = rgba + (size_t)y * w * 4;
		for (int bk = 0; bk < bw; bk++) {
			int x = bk * BLOOM_DS;
			for (int c = 0; c < 3; c++) {
				const float *o = ir + (size_t)bw * c;
				float l = o[bk > 0 ? bk - 1 : 0], m = o[bk], rr = o[bk + 1 < bw ? bk + 1 : bw - 1];
				for (int j = 0; j < BLOOM_DS && x + j < w; j++) {
					float v = j < 2 ? l + (m - l) * TX[j] : m + (rr - m) * TX[j];
					int ad = v > 255.0f ? 255 : (int)v;
					uint8_t *px = row + (size_t)(x + j) * 4 + c;
					int nv = invert ? (int)*px - ad : (int)*px + ad;
					*px = (uint8_t)(nv < 0 ? 0 : nv > 255 ? 255 : nv);
				}
			}
		}
	}
}
