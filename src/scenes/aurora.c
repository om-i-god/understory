/* Understory scene: Aurora — curtains of light over a still northern lake.
 *
 * Each curtain is a ribbon path through the sky whose travelling folds double it back on itself. Its
 * light is deposited into a low-res emission field (columns hanging up from a sharp lower rim: a
 * green body fading with height, turning to a dimmer pink fringe at the same fraction of every column,
 * so the colour change is one coherent band). The field is soft-saturated (folds seen edge-on glow, never
 * a slab), then drawn with a screen-space texture of fine vertical rays and a fine bright rim line.
 * Audio: kick -> the curtain's lower body flashes and a pulse of light runs outward along its folds;
 *        snare -> a corona flare: the pink fringe blazes and drops, the curtains stretch upward, and a
 *        sudden fold snaps into one curtain and relaxes; hats -> a fresh scatter of fine rays flares and
 *        stars glint; loudness -> brightness + curtain height (quiet sinks to a faint breathing arc);
 *        swell -> curtain count; bands[] -> each stretch of a curtain lights and lengthens with its own
 *        band; centroid -> curtain height; width/high/air -> ripple speed, ray drift and flicker; width ->
 *        curtain spread; sub -> a red lower border under the rim, and the lake ice booms (a crack glints);
 *        strong onsets -> the odd shooting star; lowmid -> the cabin hearth (window + smoke).
 * World: daylight (by day only a pale washed ghost remains), season (strongest in winter),
 *        storm_charge (a geomagnetic storm: more curtains, lower/brighter pink fringes, faster motion).
 * Silence: curtains keep folding slowly, a quiet arc breathes low over the ridge (about 9 s), rays drift,
 *        stars twinkle, the odd meteor falls, a fox sometimes trots along the shore past the lit cabin.
 * The lake mirrors the same light field about the shore (rows wobbling and broken by ripples, hidden
 * behind the mirrored ridge); the sky's glow lights the snowy crests, the foothills and a glimmer on the water.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAX_CURT 5
#define MAX_RAYS 520  /* fine rays in the screen-space ray texture */
#define GW 192        /* emission field: the sky as a low-res grid of light, x across the width */
#define GH 128        /* ... and y over [0, SKY_Y] */
#define SKY_Y 0.74f
#define NSAMP 240     /* samples along each curtain's ribbon */
#define MAXCOL 8192   /* per-column scratch (ray texture, ridge line) */
#define MAX_FOLD 3
#define MAX_STARS 500
#define MAX_TREES 110

#define SHORE 0.75f
#define MAX_CRACK 7
#define CRACK_PTS 9
#define MAX_PULSE 3

typedef struct {
	float seed;      /* noise offset */
	float y0;        /* lower edge altitude */
	float xc, span;  /* centre (fraction of aspect) + span (fraction) */
	float fold;      /* fold amplitude */
	float life;      /* 0..1 fade in/out */
	float target;    /* 1 = should exist */
	float ph;        /* ripple phase */
	float drift;
} curtain_t;

typedef struct {
	float x, y, m, tw; /* x fraction 0..1 */
} star_t;

typedef struct {
	float x, h, w; /* x fraction */
} spruce_t;

typedef struct {
	us_rng rng;
	curtain_t cu[MAX_CURT];
	star_t st[MAX_STARS];
	spruce_t tr[MAX_TREES];
	int ntr;
	float T;          /* slow time */
	float surge;      /* bass envelope */
	float shimmer;    /* highs/width envelope */
	float height;     /* centroid envelope */
	float flick_t;
	float twk; /* star twinkle phase: runs faster with air */
	float meteor_t, meteor_x, meteor_y, meteor_vx, meteor_vy, next_meteor;
	int inited;
	/* reactive envelopes */
	float bandv[US_NBANDS]; /* per-band envelopes: each band lights its own stretch of curtain */
	float sub_env, air_env, lm_env, spread;
	float pulse_age[MAX_PULSE]; /* kick pulses running outward along the curtains from pulse_u */
	float pulse_u[MAX_PULSE];
	int pulse_i;
	float kick_cool, met_cool, snare_cool;
	/* snare: a sudden fold snaps into one curtain and relaxes */
	float fold_age[MAX_FOLD], fold_u[MAX_FOLD], fold_amp[MAX_FOLD];
	int fold_c[MAX_FOLD], fold_i;
	float lvl_env, rayT;
	float snr; /* this frame's snare envelope (the pixel raster flares the pink fringe with it) */
	float lvlk; /* pixel: loudness fills in the curtains' faint body (dither density grows with it) */
	uint32_t hat_seed; /* each hat re-picks which rays flare */
	/* per frame: the emission field (light + pinkness), the ray texture, the ridge per column */
	float fld[GH * GW], pk[GH * GW];
	int gw, gh; /* the field's size this frame: full in ink, half on crisp (pixel) canvases */
	short rmin[GH], rmax[GH];
	float colR[MAXCOL], ridgeAt[MAXCOL];
	/* ice cracks along the near lake edge; sub hits make the ice boom and a crack glints */
	float crack[MAX_CRACK][2 * CRACK_PTS];
	float crack_g[MAX_CRACK];
	/* a lit cabin in a clearing on the far shore; a fox that sometimes trots along the shore */
	float cab_x;
	float fox_x, fox_dir, fox_ph, next_fox;
	int fox_on;
} st_t;

static float ridge_y(float u)
{
	/* far mountains: ridged noise */
	float n = 1.0f - fabsf(us_noise2(u * 3.1f, 11.3f));
	float n2 = 1.0f - fabsf(us_noise2(u * 7.7f, 2.9f));
	return 0.705f - 0.14f * n * n * (0.7f + 0.3f * n2) - 0.03f * n2 * n2 + 0.02f * us_noise2(u * 1.3f, 4.1f);
}

static float hash1(int k, float seed)
{
	uint32_t h = (uint32_t)k * 2654435761u ^ (uint32_t)(seed * 1000.0f) * 40503u;
	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return (h & 0xffffff) / 16777216.0f;
}

/* One column of curtain light, deposited into the emission field. The column hangs upward from the
 * lower rim at yb for ln: a sharp lower edge, a green body fading with height, turning to a dimmer pink
 * fringe above fr (a fraction of ln, the same for every column of a curtain, so the colour change is one
 * coherent band, never speckle). kick brightens the lower body, pinkb scales the fringe, and below the
 * rim hangs an optional red border (subd deep, subb bright). b is the column's brightness. */
static void deposit(st_t *s, float A, float x, float yb, float ln, float b, float fr, float pinkb, float kick,
		    float subd, float subb, float lean)
{
	if (b < 1e-4f || ln < 0.004f || !(x == x) || !(yb == yb))
		return;
	int GWr = s->gw, GHr = s->gh;
	float cw = A / GWr, ch = SKY_Y / GHr;
	float gx0 = x / cw - 0.5f, glean = lean * ch / cw; /* columns shifted per row of height */
	float gtop = gx0 + glean * (ln / ch + 1.0f);
	if ((gx0 < -3.0f && gtop < -3.0f) || (gx0 > GWr + 2.0f && gtop > GWr + 2.0f))
		return;
	int cx[4], nc = 0;
	float cwt[4];
	(void)gtop;
	int jr = (int)floorf(yb / ch - 0.5f); /* last row centre at or above the rim */
	int jt = (int)floorf((yb - ln) / ch);
	int jb = (int)ceilf((yb + subd) / ch + 0.5f);
	if (jt < 0) jt = 0;
	if (jb > GHr - 1) jb = GHr - 1;
	if (jr > GHr - 1) jr = GHr - 1;
	float il = 1.0f / ln, ich = 1.0f / ch;
	/* walk upward from the rim: exponentials advance by a constant factor per row */
	float h0 = yb - (jr + 0.5f) * ch;
	float eg = expf(-2.4f * h0 * il), ek = expf(-7.0f * h0 * il), em = expf(-1.1f * h0 * il);
	float kg = expf(-2.4f * ch * il), kk = expf(-7.0f * ch * il), km = expf(-1.1f * ch * il);
	float fa = fr - 0.14f, fb = fr + 0.14f;
	for (int j = jr; j >= jt; j--, eg *= kg, ek *= kk, em *= km) {
		if (j < 0)
			break;
		float h = yb - (j + 0.5f) * ch;
		float t = h * il;
		if (t >= 1.0f)
			break;
		float p = t > fa ? us_smooth(fa, fb, t) : 0.0f;
		float I = (1.0f - p) * eg * (1.0f + 1.6f * kick * ek) + p * 0.36f * pinkb * em;
		if (h < 0.5f * ch)
			I *= us_sat(0.5f + h * ich);
		if (t > 0.7f)
			I *= 1.0f - us_smooth(0.7f, 1.0f, t);
		/* a soft horizontal tent (2 cells each side), so folds seen edge-on glow without hard walls */
		float gx = gx0 + glean * (h > 0.0f ? h * ich : 0.0f);
		int c0 = (int)floorf(gx - 2.0f) + 1;
		float *fr_ = s->fld + j * GWr, *pr_ = s->pk + j * GWr, Ib = I * b, Ipb = Ib * p;
		for (int k = 0; k < 4; k++) {
			int ci = c0 + k;
			float wv = (1.0f - fabsf(ci - gx) * 0.5f) * 0.5f;
			if ((unsigned)ci < (unsigned)GWr && wv > 0.0f) {
				fr_[ci] += wv * Ib;
				pr_[ci] += wv * Ipb;
			}
		}
	}
	/* below the rim: the edge's anti-aliased fall-off, and the red lower border */
	for (int j = jr + 1; j <= jb; j++) {
		if (j < 0)
			continue;
		float h = yb - (j + 0.5f) * ch; /* negative */
		float I = us_sat(0.5f + h * ich), p = 0.0f;
		if (subd > 1e-4f) {
			float sI = subb * us_sat(1.0f + h / subd);
			if (sI > I) {
				p = 1.0f;
				I = sI;
			}
		}
		if (I <= 0.0f)
			continue;
		/* a soft horizontal tent (2 cells each side), so folds seen edge-on glow without hard walls */
		float gx = gx0 + glean * (h > 0.0f ? h * ich : 0.0f);
		nc = 0;
		int c0 = (int)floorf(gx - 2.0f) + 1;
		for (int k = 0; k < 4; k++) {
			int ci = c0 + k;
			float wv = (1.0f - fabsf(ci - gx) * 0.5f) * 0.5f;
			if (ci >= 0 && ci < GWr && wv > 0.0f) {
				cx[nc] = ci;
				cwt[nc++] = wv * b;
			}
		}
		float *fr_ = s->fld + j * GWr, *pr_ = s->pk + j * GWr, Ip = I * p;
		for (int k = 0; k < nc; k++) {
			fr_[cx[k]] += cwt[k] * I;
			pr_[cx[k]] += cwt[k] * Ip;
		}
	}
}

/* Ordered dither for crisp canvases. Pixel shows 4 shades per material (plus ground), cut at ink ~17/69/128/191
 * (at the default glow). A value between two shade centres picks the upper or lower shade by a 4x4 Bayer
 * threshold, and is only nudged to that shade's edge -- so Pixel gets a true dithered fall-off while Mono
 * (16 levels) keeps an almost smooth ramp with a faint grain. */
static const uint8_t bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static inline float bthr(int x, int y) { return (bayer4[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f; }
static int dith(float v, int x, int y)
{
	static const float lo[5] = {0, 17, 69, 128, 191}, ce[5] = {0, 40, 98, 159, 222};
	v *= 255.0f;
	if (v <= 0.5f)
		return 0;
	if (v >= 222.0f)
		return v > 255.0f ? 255 : (int)v;
	int k = v < 40 ? 0 : v < 98 ? 1 : v < 159 ? 2 : 3;
	float f = (v - ce[k]) / (ce[k + 1] - ce[k]);
	int st = k + (f > bthr(x, y));
	float a = lo[st], b = st < 4 ? lo[st + 1] - 1.0f : 255.0f;
	if (v < a) v = a;
	if (v > b) v = b;
	return (int)v;
}

/* Rasterise the emission field (bilinear) into the canvas with MAX blend, sky rows [py0, py1) mapped from
 * sky altitude ys(py) = ya + (py+0.5)/S * yk (yk = 1 for the sky, -2 for the mirrored lake), each row
 * shifted by xoff(py) px and scaled by rowv(py). Light is the field times the ray texture; colour is
 * green, or pink where the field says so. Columns whose sky point sits below the ridge are skipped. */
static void raster(st_t *s, us_canvas *c, float A, int py0, int py1, float ya, float yk, float gain, int mirror, double t, int crisp)
{
	int GWr = s->gw, GHr = s->gh;
	float S = c->s, cw = A / GWr, ch = SKY_Y / GHr;
	int W = c->w < MAXCOL ? c->w : MAXCOL;
	if (py0 < 0) py0 = 0;
	if (py1 > c->h) py1 = c->h;
	float cpx = cw * S; /* pixels per field cell */
	float rowV[GW + 1], rowP[GW + 1];
	for (int py = py0; py < py1; py++) {
		float y = ya + (py + 0.5f) / S * yk;
		if (y < 0.0f || y >= SKY_Y)
			continue;
		float fy = y / ch - 0.5f;
		int iy = (int)floorf(fy);
		float ty = fy - iy;
		int iy0 = iy < 0 ? 0 : iy, iy1 = iy + 1 > GHr - 1 ? GHr - 1 : iy + 1;
		if (iy0 > GHr - 1) iy0 = GHr - 1;
		int cmin = s->rmin[iy0] < s->rmin[iy1] ? s->rmin[iy0] : s->rmin[iy1];
		int cmax = s->rmax[iy0] > s->rmax[iy1] ? s->rmax[iy0] : s->rmax[iy1];
		if (cmax < cmin)
			continue;
		float rv = gain, xo = 0.0f;
		if (mirror) { /* the lake: rows wobble sideways and break into ripples, dimmer with depth */
			float yy = (py + 0.5f) / S;
			if (crisp) { /* pixel: the reflection breaks into horizontal ripple strips with dark gaps */
				float sn = us_noise2(yy * 95.0f, (float)t * 0.12f) + 0.35f * us_noise2(yy * 31.0f, (float)t * 0.06f + 4.0f);
				if (sn < -0.28f)
					continue;
				rv *= 0.6f + 0.4f * us_sat(sn * 2.0f + 0.3f);
			}
			xo =S * (0.0045f * us_noise2(yy * 70.0f, (float)t * 0.6f) + 0.0018f * us_noise2(yy * 210.0f, (float)t * 1.4f));
			rv *= (0.6f + 0.4f * us_sat(0.55f + 0.9f * us_noise2(yy * 130.0f, (float)t * 0.45f))) * (1.0f - 1.2f * (yy - SHORE));
			if (rv <= 0.0f)
				continue;
		}
		/* the two field rows, blended once per canvas row */
		const float *f0 = s->fld + iy0 * GWr, *f1 = s->fld + iy1 * GWr, *p0 = s->pk + iy0 * GWr, *p1 = s->pk + iy1 * GWr;
		int a0 = cmin > 0 ? cmin - 1 : 0, a1 = cmax + 1 < GWr - 1 ? cmax + 1 : GWr - 1;
		for (int i = a0; i <= a1; i++) {
			rowV[i] = (f0[i] + (f1[i] - f0[i]) * ty) * rv;
			rowP[i] = p0[i] + (p1[i] - p0[i]) * ty;
		}
		uint8_t *ink = c->ink + (size_t)py * c->w, *mat = c->mat + (size_t)py * c->w;
		/* walk cell to cell (centres i and i+1); skip spans that are dark at both ends */
		float icpx = 1.0f / cpx;
		int ixo = (int)floorf(xo + 0.5f);
		for (int i = a0; i < a1; i++) {
			float va = rowV[i], vb = rowV[i + 1];
			if (va < 0.004f && vb < 0.004f)
				continue;
			int px0 = (int)ceilf((i + 0.5f) * cpx - 0.5f - xo), px1 = (int)ceilf((i + 1.5f) * cpx - 0.5f - xo);
			if (i == 0 && px0 > 0) px0 = 0; /* the field's edge columns extend to the canvas edges */
			if (i + 1 == GWr - 1) px1 = W;
			if (px0 < 0) px0 = 0;
			if (px1 > W) px1 = W;
			if (px1 <= px0)
				continue;
			float pa = rowP[i], pb = rowP[i + 1];
			float tx = (px0 + 0.5f + xo) * icpx - 0.5f - i;
			float dV = (vb - va) * icpx, dP = (pb - pa) * icpx;
			float V = va + (vb - va) * tx, P = pa + (pb - pa) * tx;
			for (int px = px0; px < px1; px++, V += dV, P += dP) {
				int sx = px + ixo;
				if ((unsigned)sx >= (unsigned)W || y > s->ridgeAt[sx] || V < 0.004f)
					continue;
				float v = V * (1.0f + (s->colR[sx] - 1.0f) * (0.75f + 0.5f * P));
				int iv;
				uint8_t m;
				if (crisp) {
					/* break the ripple strips into dashes on the water */
					if (mirror && us_noise2(px * 0.09f + (float)t * 0.25f, py * 1.3f) < -0.42f)
						continue;
					/* lake: each strip one shade; sky: a deeper fall-off so the curtains fade up into the dark */
					iv = mirror ? dith(v, 1, py) : dith(v * us_sat(0.2f + 1.1f * V) * (0.6f + 0.5f * v) * (1.0f + 2.1f * P * s->snr) + s->lvlk * V, px, py);
					m = P > 0.3f + 0.5f * bthr(px + 1, py + 2) ? US_MAT_BLOOM : US_MAT_LEAF;
				} else {
					iv = (int)(v * 255.0f);
					if (iv > 255) iv = 255;
					m = P > 0.6f ? US_MAT_BLOOM : US_MAT_LEAF;
				}
				if (iv > ink[px]) {
					ink[px] = (uint8_t)iv;
					mat[px] = m;
				}
			}
		}
	}
}

/* thin additive line for the curtain rim (Wu spans: two pixels per step, respects the ADD blend) */
static void rim(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
	float x0 = ax * c->s - 0.5f, y0 = ay * c->s - 0.5f, x1 = bx * c->s - 0.5f, y1 = by * c->s - 0.5f;
	int steep = fabsf(y1 - y0) > fabsf(x1 - x0);
	if (steep) {
		float t = x0; x0 = y0; y0 = t;
		t = x1; x1 = y1; y1 = t;
	}
	if (x0 > x1) {
		float t = x0; x0 = x1; x1 = t;
		t = y0; y0 = y1; y1 = t;
	}
	float g = fabsf(x1 - x0) > 1e-6f ? (y1 - y0) / (x1 - x0) : 0.0f;
	int i0 = (int)floorf(x0 + 0.5f), i1 = (int)floorf(x1 + 0.5f);
	for (int i = i0; i <= i1 && i - i0 < 400; i++) {
		float yy = y0 + g * (i - x0);
		int j = (int)floorf(yy);
		float f = yy - j;
		if (steep) {
			us_put(c, j, i, v * (1.0f - f), mat);
			us_put(c, j + 1, i, v * f, mat);
		} else {
			us_put(c, i, j, v * (1.0f - f), mat);
			us_put(c, i, j + 1, v * f, mat);
		}
	}
}

/* the curtain's lower edge, as a fraction of the width: span + travelling folds (doubling back where
 * their sway outruns the span) */
static float curtain_u2x(const curtain_t *cu, float u, float T, float spread)
{
	float fa = u * US_TAU * 1.8f + T * 0.55f + cu->seed;
	float fb = u * US_TAU * 3.1f - T * 0.4f + cu->seed * 1.7f;
	float env = sinf(US_PI * us_sat(u)); /* the ends run straight out: no fold turns back at an end */
	float fold = cu->fold * env * (0.62f * sinf(fa) + 0.2f * sinf(fb) + 0.9f * us_fbm2(u * 1.8f + T * 0.3f, cu->seed, 2));
	return cu->xc + cu->span * spread * (u - 0.5f) + fold;
}

/* hairline polyline for ink (Wu spans, ~0.8 px): cheaper than us_polyline for the treeline */
static void hpoly(us_canvas *c, const float *xy, int n, float v, uint8_t mat)
{
	for (int i = 0; i + 1 < n; i++)
		rim(c, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], v * 0.8f, mat);
}

static void wline_(us_canvas *c, int crisp, float ax, float ay, float bx, float by, float v, uint8_t m)
{
	if (crisp)
		us_line(c, ax, ay, bx, by, 0.004f, v, m);
	else
		rim(c, ax, ay, bx, by, v, m);
}

static void new_curtain(st_t *s, curtain_t *c)
{
	us_rng *r = &s->rng;
	c->seed = us_randr(r, 0, 100);
	c->y0 = us_randr(r, 0.36f, 0.54f);
	c->xc = us_randr(r, 0.3f, 0.7f);
	c->span = us_randr(r, 0.9f, 1.6f);
	c->fold = us_randr(r, 0.16f, 0.3f);
	c->ph = us_rand(r) * US_TAU;
	c->drift = us_randr(r, -0.012f, 0.012f);
	c->life = 0;
	c->target = 1;
}

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng *r = &s->rng;
	us_rng_seed(r, seed ^ 0x6175726fu);
	for (int i = 0; i < MAX_STARS; i++) {
		star_t *t = &s->st[i];
		t->x = us_rand(r);
		t->y = us_randr(r, 0.0f, 0.68f);
		float m = us_rand(r);
		t->m = 0.08f + 0.92f * m * m * m * m;
		t->tw = us_rand(r) * 50;
	}
	s->ntr = MAX_TREES;
	for (int i = 0; i < MAX_TREES; i++) {
		spruce_t *t = &s->tr[i];
		t->x = us_rand(r);
		/* clustered stands: taller where a low noise says so */
		float stand = 0.5f + 0.5f * us_noise2(t->x * 6.0f, 1.7f);
		t->h = us_randr(r, 0.022f, 0.04f) + 0.035f * stand * us_rand(r);
		t->w = t->h * us_randr(r, 0.28f, 0.38f);
	}
	/* sort trees far-to-near is irrelevant (same depth); sort by height so tall ones draw last */
	for (int i = 1; i < MAX_TREES; i++) {
		spruce_t k = s->tr[i];
		int j = i - 1;
		while (j >= 0 && s->tr[j].h > k.h) {
			s->tr[j + 1] = s->tr[j];
			j--;
		}
		s->tr[j + 1] = k;
	}
	for (int i = 0; i < MAX_CURT; i++) {
		new_curtain(s, &s->cu[i]);
		s->cu[i].target = 0;
	}
	s->T = us_rand(r) * 100;
	s->next_meteor = us_randr(r, 5, 20);
	for (int i = 0; i < MAX_PULSE; i++)
		s->pulse_age[i] = 9.0f;
	for (int i = 0; i < MAX_FOLD; i++)
		s->fold_age[i] = 9.0f;
	s->spread = 1.0f;
	/* ice cracks: wandering, mostly shore-parallel lines in the near water at both edges */
	for (int i = 0; i < MAX_CRACK; i++) {
		float x = i & 1 ? us_randr(r, 0.0f, 0.28f) : us_randr(r, 0.72f, 1.0f);
		float y = us_randr(r, 0.84f, 0.985f);
		float ang = (x < 0.5f ? 0.0f : US_PI) + us_randr(r, -0.5f, 0.5f);
		float step = us_randr(r, 0.012f, 0.022f);
		for (int k = 0; k < CRACK_PTS; k++) {
			s->crack[i][2 * k] = x;
			s->crack[i][2 * k + 1] = y;
			ang += us_randr(r, -0.7f, 0.7f);
			x += cosf(ang) * step / 1.78f; /* x is a width fraction */
			y += sinf(ang) * step * 0.35f;  /* foreshortened on the flat lake */
		}
	}
	s->cab_x = us_randr(r, 0.58f, 0.8f);
	s->next_fox = us_randr(r, 15, 35);
	return s;
}

static void destroy(void *p) { free(p); }

static void spruce(us_canvas *c, int crisp, float x, float y, float h, float w, float v)
{
	/* 4 tiers of drooping boughs */
	if (crisp) {
		for (int k = 0; k < 4; k++) {
			float t0 = k / 4.0f, t1 = (k + 1.4f) / 4.0f;
			float ty = y - h + h * t0;
			float by = y - h + h * (t1 > 1 ? 1 : t1) - h * 0.08f;
			float bw = w * (0.35f + 0.65f * t1);
			us_tri(c, x, ty, x - bw, by, x + bw, by, v, US_MAT_LEAF);
		}
		us_line(c, x, y - h * 0.1f, x, y, 0.004f, v, US_MAT_STEM);
		return;
	}
	float pts[2 * 11];
	int n = 0;
	pts[n++] = x - w * 0.15f;
	pts[n++] = y;
	for (int k = 3; k >= 0; k--) {
		float t = (k + 1) / 4.0f;
		pts[n++] = x - w * (0.3f + 0.7f * t);
		pts[n++] = y - h + h * t - h * 0.04f;
		pts[n++] = x - w * 0.2f * t;
		pts[n++] = y - h + h * t - h * 0.2f;
	}
	pts[n++] = x;
	pts[n++] = y - h;
	hpoly(c, pts, n / 2, v, US_MAT_LEAF);
	n = 0;
	pts[n++] = x;
	pts[n++] = y - h;
	for (int k = 0; k < 4; k++) {
		float t = (k + 1) / 4.0f;
		pts[n++] = x + w * 0.2f * t;
		pts[n++] = y - h + h * t - h * 0.2f;
		pts[n++] = x + w * (0.3f + 0.7f * t);
		pts[n++] = y - h + h * t - h * 0.04f;
	}
	pts[n++] = x + w * 0.15f;
	pts[n++] = y;
	hpoly(c, pts, n / 2, v, US_MAT_LEAF);
}

/* a log cabin in its clearing: walls, roof, a lit window (lowmid = the hearth's warmth) and curling smoke */
static void cabin(st_t *s, const us_ctx *ctx, us_canvas *c, float day)
{
	float A = ctx->aspect, x = s->cab_x * A, y = SHORE + 0.001f;
	float w = 0.026f, h = 0.014f, rh = 0.012f;
	float lv = 0.30f + 0.25f * day;
	int cr = ctx->crisp;
	if (cr) {
		us_tri(c, x - w * 0.5f, y - h, x + w * 0.5f, y - h, x - w * 0.5f, y, lv, US_MAT_EARTH);
		us_tri(c, x + w * 0.5f, y - h, x + w * 0.5f, y, x - w * 0.5f, y, lv, US_MAT_EARTH);
		us_tri(c, x - w * 0.62f, y - h, x + w * 0.62f, y - h, x, y - h - rh, lv, US_MAT_STEM);
	} else {
		float pts[] = {x - w * 0.5f, y, x - w * 0.5f, y - h, x - w * 0.62f, y - h, x, y - h - rh, x + w * 0.62f, y - h,
			       x + w * 0.5f, y - h, x + w * 0.5f, y};
		hpoly(c, pts, 7, lv * 1.2f, US_MAT_STEM);
		rim(c, x - w * 0.5f, y - h, x + w * 0.5f, y - h, lv * 0.7f, US_MAT_STEM);
		for (int k = 1; k < 3; k++) /* log courses */
			rim(c, x - w * 0.5f, y - h * k / 3.0f, x + w * 0.5f, y - h * k / 3.0f, lv * 0.35f, US_MAT_STEM);
	}
	/* chimney */
	float chx = x + w * 0.28f, chy = y - h - rh * 0.45f;
	wline_(c, cr, chx, chy, chx, chy - rh * 0.5f, lv, US_MAT_STEM);
	/* window: warm, flickering gently with the hearth (lowmid), dimmer by day */
	float fire = 0.55f + 0.3f * s->lm_env + 0.08f * us_noise2((float)ctx->t * 3.1f, 5.0f);
	float wv = fire * (1.0f - 0.6f * us_smooth(0.3f, 0.8f, day));
	float wx = x - w * 0.18f, wy = y - h * 0.5f;
	if (cr) {
		us_disc(c, wx, wy, 0.0045f, wv, US_MAT_LIGHT);
	} else {
		us_disc(c, wx, wy, 0.008f, wv * 0.07f, US_MAT_LIGHT); /* glow on the snow */
		us_line(c, wx - 0.0028f, wy, wx + 0.0028f, wy, 0.0042f, wv, US_MAT_LIGHT);
	}
	/* its reflection: a broken warm streak on the water */
	for (int k = 0; k < (cr ? 2 : 4); k++) {
		float ry = SHORE + 0.008f + 0.0085f * k + 0.002f * us_noise2(k * 3.0f, (float)ctx->t * 0.8f);
		float hw = 0.0035f * (1.0f + 0.4f * us_noise2(k * 1.7f, (float)ctx->t * 1.1f));
		wline_(c, cr, wx - hw, ry, wx + hw, ry, wv * (0.5f - 0.1f * k), US_MAT_LIGHT);
	}
	/* smoke: rises, leans with the wind and the stereo spread, curls; thicker as the hearth warms */
	float px = chx, py = chy - rh * 0.5f;
	float lean = 0.25f + 0.6f * ctx->w->wind + 0.3f * (s->spread - 0.8f);
	for (int k = 1; k <= 9; k++) {
		float t = k / 9.0f;
		float nx = chx + lean * 0.05f * t * t + 0.006f * t * us_noise2(t * 3.0f - (float)ctx->t * 0.4f, 9.0f);
		float ny = chy - rh * 0.5f - 0.055f * t;
		wline_(c, cr, px, py, nx, ny, (0.16f + 0.1f * s->lm_env) * (1.0f - t), US_MAT_WATER);
		px = nx;
		py = ny;
	}
}

/* now and then a fox trots along the shore in front of the spruces, with its reflection */
static void fox(st_t *s, const us_ctx *ctx, us_canvas *c, float day)
{
	float A = ctx->aspect, rdt = ctx->real_dt;
	if (!s->fox_on) {
		s->next_fox -= ctx->dt;
		if (s->next_fox > 0)
			return;
		s->fox_on = 1;
		s->fox_dir = us_rand(&s->rng) < 0.5f ? 1.0f : -1.0f;
		s->fox_x = s->fox_dir > 0 ? -0.04f : A + 0.04f;
	}
	/* a steady trot; a beat-quick lope in loud passages */
	float sp = 0.045f * (1.0f + 0.4f * ctx->a->level);
	s->fox_x += s->fox_dir * sp * ctx->dt;
	s->fox_ph += ctx->dt * 9.0f * (1.0f + 0.4f * ctx->a->level);
	(void)rdt;
	if (s->fox_x < -0.06f || s->fox_x > A + 0.06f) {
		s->fox_on = 0;
		s->next_fox = us_randr(&s->rng, 45, 110);
		return;
	}
	float d = s->fox_dir, x = s->fox_x, y = SHORE - 0.0005f;
	float L = 0.016f, H = 0.006f, leg = 0.0045f;
	float v = 0.42f + 0.2f * day;
	int cr = ctx->crisp;
	for (int m = 0; m < 2; m++) { /* m = 1: the reflection, flipped and dim */
		float sg = m ? -1.0f : 1.0f, vv = m ? v * 0.35f : v;
		float by = y - sg * leg; /* body line */
		float bob = 0.0006f * sinf(s->fox_ph * 2.0f);
		by -= sg * bob;
		/* body + head + ears + snout */
		float hx = x + d * L * 0.55f, hy = by - sg * H * 0.6f;
		if (cr) {
			us_line(c, x - d * L * 0.5f, by, x + d * L * 0.4f, by, 0.004f, vv, US_MAT_ACCENT);
			us_line(c, hx, hy, hx + d * 0.004f, hy + sg * 0.001f, 0.004f, vv, US_MAT_ACCENT);
		} else {
			float body[] = {x - d * L * 0.5f, by, x - d * L * 0.1f, by - sg * H * 0.35f, x + d * L * 0.35f, by - sg * H * 0.3f,
					hx, hy, hx + d * 0.0045f, hy + sg * 0.0012f, hx, hy + sg * 0.0016f, x + d * L * 0.35f, by + sg * 0.0006f,
					x - d * L * 0.5f, by + sg * 0.0005f};
			hpoly(c, body, 8, vv * 1.2f, US_MAT_ACCENT);
			rim(c, hx - d * 0.0008f, hy, hx - d * 0.0005f, hy - sg * 0.0035f, vv, US_MAT_ACCENT); /* ear */
		}
		/* bushy tail, low behind */
		float tx = x - d * L * 0.5f;
		float tw = 0.0015f * sinf(s->fox_ph * 1.0f);
		wline_(c, cr, tx, by, tx - d * 0.009f, by + sg * (0.001f + tw), vv, US_MAT_ACCENT);
		wline_(c, cr, tx - d * 0.003f, by - sg * 0.0012f, tx - d * 0.011f, by + sg * (0.0002f + tw), vv * 0.8f, US_MAT_ACCENT);
		/* legs: a diagonal-pair trot */
		for (int l = 0; l < 4; l++) {
			float lx = x + d * (l < 2 ? 0.3f : -0.38f) * L + (l & 1) * d * 0.002f;
			float sw = 0.0022f * sinf(s->fox_ph + ((l == 0 || l == 3) ? 0.0f : US_PI));
			wline_(c, cr, lx, by, lx + d * sw, y, vv * 0.9f, US_MAT_ACCENT);
		}
	}
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *s = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, rdt = ctx->real_dt, A = ctx->aspect;
	float dens = ctx->p->density;
	us_rng *r = &s->rng;
	float winter = us_world_season_w(w, 3), summer = us_world_season_w(w, 1);
	float day = w->daylight, storm = w->storm_charge;
	float night = 1.0f - 0.6f * us_smooth(0.15f, 0.8f, day);
	float kick = us_sat(a->kick), snare = us_sat(a->snare), hat = us_sat(a->hat);
	s->snr = snare;

	/* ---- envelopes ---- */
	s->lvl_env = us_approach(s->lvl_env, us_sat(a->level), a->level > s->lvl_env ? 0.12f : 0.4f, rdt);
	float sh = us_sat(0.45f * a->width + 0.35f * a->high + 0.5f * a->air);
	s->shimmer = us_approach(s->shimmer, sh, 0.4f, rdt);
	s->height = us_approach(s->height, a->active > 0.2f ? a->centroid : 0.4f, 2.0f, rdt);
	s->T += dt * (0.075f + 0.06f * storm + 0.05f * a->swell + 0.03f * s->lvl_env);
	s->flick_t += rdt * (2.2f + 4.0f * s->shimmer);
	s->rayT += dt * (0.9f + 1.6f * s->shimmer + storm);
	s->twk += rdt * (1.7f + 5.0f * s->air_env);
	/* each band: quick to light (~0.06 s), slow to let go (~0.35 s) */
	for (int i = 0; i < US_NBANDS; i++) {
		float bv = a->bands[i];
		s->bandv[i] = us_approach(s->bandv[i], bv, bv > s->bandv[i] ? 0.06f : 0.35f, rdt);
	}
	s->sub_env = us_approach(s->sub_env, a->sub, a->sub > s->sub_env ? 0.08f : 0.45f, rdt);
	s->air_env = us_approach(s->air_env, a->air, 0.25f, rdt);
	s->lm_env = us_approach(s->lm_env, a->lowmid, 0.8f, rdt);
	/* stereo width spreads the curtains across the sky (slowly) */
	s->spread = us_approach(s->spread, 0.82f + 0.4f * a->width, 2.5f, rdt);
	/* hats: the rays shimmer -- each hat re-picks which fine rays flare, and the flicker jumps on */
	if (a->hat_hit) {
		s->hat_seed = s->hat_seed * 1664525u + 1013904223u;
		s->flick_t += 0.5f;
	}
	/* kick: a pulse of light runs outward along every curtain's folds from a point on it */
	s->kick_cool -= rdt;
	for (int i = 0; i < MAX_PULSE; i++)
		s->pulse_age[i] += rdt;
	if (a->kick_hit && s->kick_cool <= 0) {
		s->pulse_i = (s->pulse_i + 1) % MAX_PULSE;
		s->pulse_age[s->pulse_i] = 0;
		s->pulse_u[s->pulse_i] = us_randr(r, 0.25f, 0.75f);
		s->kick_cool = 0.12f;
	}
	float pamp[MAX_PULSE], pdist[MAX_PULSE];
	for (int i = 0; i < MAX_PULSE; i++) {
		float ag = s->pulse_age[i];
		/* eases in over ~30 ms, out over ~0.4 s; the front slows as it spreads */
		pamp[i] = ag < 1.5f ? us_smooth(0.0f, 0.03f, ag) * expf(-ag / 0.3f) : 0.0f;
		pdist[i] = 0.55f * (1.0f - expf(-ag / 0.35f));
	}
	/* snare: a sudden fold snaps into one curtain (a corona-like kink) and relaxes */
	s->snare_cool -= rdt;
	for (int i = 0; i < MAX_FOLD; i++)
		s->fold_age[i] += rdt;
	if (a->snare_hit && s->snare_cool <= 0) {
		s->fold_i = (s->fold_i + 1) % MAX_FOLD;
		s->fold_age[s->fold_i] = 0;
		s->fold_u[s->fold_i] = us_randr(r, 0.2f, 0.8f);
		s->fold_amp[s->fold_i] = us_randr(r, 0.035f, 0.06f) * (us_rand(r) < 0.5f ? -1.0f : 1.0f);
		s->fold_c[s->fold_i] = (int)(us_rand(r) * MAX_CURT) % MAX_CURT;
		s->snare_cool = 0.1f;
	}
	float famp[MAX_FOLD];
	for (int i = 0; i < MAX_FOLD; i++) {
		float ag = s->fold_age[i];
		famp[i] = ag < 2.0f ? us_smooth(0.0f, 0.035f, ag) * expf(-ag / 0.45f) * cosf(ag * 7.0f) : 0.0f;
	}
	/* sub: the ice booms; one crack glints and fades */
	for (int i = 0; i < MAX_CRACK; i++)
		s->crack_g[i] = us_approach(s->crack_g[i], 0.0f, 0.5f, rdt);
	if (a->kick_hit && a->sub > 0.55f) {
		int ci = (int)(us_rand(r) * MAX_CRACK) % MAX_CRACK;
		s->crack_g[ci] = us_sat(s->crack_g[ci] + 0.5f + 0.5f * a->sub);
	}
	/* quiet passages: the sky still breathes, slowly (about a 9 s breath) */
	float quiet = 1.0f - us_sat(1.6f * s->lvl_env);
	float breath = 0.5f + 0.5f * sinf((float)ctx->t * (US_TAU / 9.0f));

	/* ---- how many curtains: density + geomagnetic storm + loudness ---- */
	float want_f = 1.2f + 1.6f * dens + 2.2f * storm + 1.2f * a->swell;
	int want = (int)want_f;
	if (want > MAX_CURT)
		want = MAX_CURT;
	if (!s->inited) {
		for (int i = 0; i < want; i++) {
			s->cu[i].target = 1;
			s->cu[i].life = 1;
		}
		s->inited = 1;
	}
	for (int i = 0; i < MAX_CURT; i++) {
		curtain_t *cu = &s->cu[i];
		int on = i < want;
		if (on && cu->target == 0 && cu->life <= 0.01f)
			new_curtain(s, cu);
		cu->target = on ? 1.0f : 0.0f;
		cu->life = us_approach(cu->life, cu->target, 4.0f, dt);
		cu->xc += cu->drift * dt;
		if (cu->xc < -0.2f || cu->xc > 1.2f)
			cu->drift = -cu->drift;
	}

	/* pixel styles quantise a faint ghost away against the banded day sky: keep more of it there */
	float strength = (0.55f + 0.45f * winter - 0.15f * summer) * (ctx->crisp ? (night > 0.95f ? night : 0.95f) : night * night);

	/* the ridge line, per canvas column (the aurora hangs behind the mountains, and so does its reflection) */
	int W = c->w < MAXCOL ? c->w : MAXCOL;
	{
		float rt[97];
		for (int i = 0; i <= 96; i++)
			rt[i] = ridge_y(i / 96.0f);
		for (int px = 0; px < W; px++) {
			float uu = us_sat((px + 0.5f) / c->s / A) * 96.0f;
			int i = (int)uu;
			if (i > 95) i = 95;
			s->ridgeAt[px] = us_lerp(rt[i], rt[i + 1], uu - i) - 0.002f;
		}
	}

	/* ---- stars: hats make a scatter of them glint ---- */
	{
		int ns = (int)(MAX_STARS * (0.25f + 0.75f * dens));
		float sv = us_sat(1.0f - day * 1.6f);
		if (sv > 0.01f)
			for (int i = 0; i < ns; i++) {
				star_t *t = &s->st[i];
				int px = (int)(t->x * A * c->s);
				if (px < 0 || px >= W || t->y > s->ridgeAt[px] - 0.008f)
					continue;
				float tw = 0.75f + (0.25f + 0.3f * s->air_env) * us_noise2(t->tw, s->twk);
				if (hat > 0.02f && hash1(i, (float)(s->hat_seed & 0xffff)) < 0.22f)
					tw += 1.6f * hat;
				float v = t->m * tw * sv * (ctx->crisp ? 0.9f : 1.1f);
				us_point(c, t->x * A, t->y, t->m > 0.7f ? 0.0016f : 0.0011f, v, US_MAT_LIGHT);
			}
	}

	/* ---- meteor (rare, in quiet) ---- */
	s->next_meteor -= rdt * (1.0f - 0.7f * a->level);
	s->met_cool -= rdt;
	/* a strong onset can throw a shooting star (rarely: a hit is a chance, not a rule) */
	int met_hit = a->beat && a->onset > 0.8f && s->met_cool <= 0 && s->meteor_t <= 0 && us_rand(r) < 0.2f;
	if ((s->next_meteor <= 0 || met_hit) && s->meteor_t <= 0) {
		s->met_cool = 5.0f;
		s->meteor_t = 1.0f;
		s->meteor_x = us_randr(r, 0.1f, 0.9f) * A;
		s->meteor_y = us_randr(r, 0.03f, 0.25f);
		float ang = us_randr(r, 0.25f, 0.7f) * (us_rand(r) < 0.5f ? 1.0f : -1.0f);
		s->meteor_vx = sinf(ang) * 0.6f;
		s->meteor_vy = cosf(fabsf(ang)) * 0.25f;
		s->next_meteor = us_randr(r, 12, 40);
	}
	if (s->meteor_t > 0) {
		s->meteor_t -= rdt * 2.2f;
		s->meteor_x += s->meteor_vx * rdt;
		s->meteor_y += s->meteor_vy * rdt;
		float v = us_sat(s->meteor_t) * (1.0f - day);
		us_line(c, s->meteor_x, s->meteor_y, s->meteor_x - s->meteor_vx * 0.12f, s->meteor_y - s->meteor_vy * 0.12f,
			0.0012f, v * 0.8f, US_MAT_LIGHT);
	}

	/* ---- the ray texture: fine vertical rays across the sky, drifting and shimmering ---- */
	{
		float base = ctx->crisp ? 0.42f : 0.62f;
		for (int px = 0; px < W; px++)
			s->colR[px] = base;
		int nray = (int)(160 + 360 * dens);
		if (nray > MAX_RAYS)
			nray = MAX_RAYS;
		float hs = (float)(s->hat_seed & 0xffff);
		float amp_k = ctx->crisp ? 0.95f : 1.1f;
		for (int i = 0; i < nray; i++) {
			float h1 = hash1(i, 1.3f), h2 = hash1(i + 5003, 1.3f), h3 = hash1(i + 9001, 1.3f);
			/* pixel: gentler sway, so rays step across whole pixels less often (less churn between the beats) */
			float xr = h1 + s->rayT * (ctx->crisp ? 0.012f : 0.03f) * (h2 - 0.5f) + (ctx->crisp ? 0.0015f : 0.007f) * sinf(s->rayT * (0.7f + h3) + h1 * 40.0f);
			xr -= floorf(xr);
			float amp = (0.2f + 0.8f * h3 * h3) * (0.6f + 0.5f * us_noise2(i * 0.71f, s->flick_t));
			if (hash1(i, hs) < 0.3f)
				amp += (ctx->crisp ? 1.75f : 1.6f) * hat;
			amp *= amp_k;
			float px = xr * W - 0.5f;
			int ix = (int)floorf(px);
			float fx = px - ix;
			if (ix >= 0 && ix < W)
				s->colR[ix] += amp * (1.0f - fx);
			if (ix + 1 >= 0 && ix + 1 < W)
				s->colR[ix + 1] += amp * fx;
		}
		/* pixel: few columns per ray, so group them into broader drifting streaks that read as rays */
		if (ctx->crisp)
			for (int px = 0; px < W; px++) {
				float u = (px + 0.5f) / W;
				float st = 0.5f + 0.5f * us_noise2(u * 55.0f + s->rayT * 0.012f, 3.3f) + 0.25f * us_noise2(u * 130.0f - s->rayT * 0.01f, 8.1f);
				s->colR[px] *= 0.45f + 0.95f * us_sat(st);
			}
	}

	/* ---- curtains: deposited into the emission field ---- */
	s->gw = ctx->crisp ? GW / 2 : GW;
	s->gh = ctx->crisp ? GH / 2 : GH;
	memset(s->fld, 0, sizeof(float) * s->gw * s->gh);
	memset(s->pk, 0, sizeof(float) * s->gw * s->gh);
	float L0 = (0.11f + 0.16f * s->lvl_env + 0.12f * s->height + 0.06f * storm) * (1.0f + 0.3f * snare);
	float lift = 0.07f * (s->height - 0.4f);
	float rip_speed = 1.2f + 4.5f * s->shimmer + 2.0f * storm;
	float nact = 0;
	for (int ci = 0; ci < MAX_CURT; ci++)
		nact += s->cu[ci].life;
	float share = 1.0f / powf(nact > 1.5f ? nact / 1.5f : 1.0f, 0.5f);
	/* the pink fringe: lower with a storm; a snare flares it and pulls it down the curtain */
	float fringe = us_clampf(0.6f - 0.2f * storm - 0.12f * snare, 0.2f, 0.9f);
	float pinkb = 1.0f + 2.2f * snare + 0.6f * storm;
	float subf = us_sat(1.3f * s->sub_env - 0.05f);
	float rimx[MAX_CURT][NSAMP / 2 + 1], rimy[MAX_CURT][NSAMP / 2 + 1], rimb[MAX_CURT][NSAMP / 2 + 1];
	int rimn[MAX_CURT];
	for (int ci = 0; ci < MAX_CURT; ci++) {
		curtain_t *cu = &s->cu[ci];
		rimn[ci] = 0;
		if (cu->life < 0.01f)
			continue;
		cu->ph += dt * rip_speed;
		float T = s->T + cu->seed;
		/* depth: curtains low over the horizon are farther away -> shorter, fainter */
		float near = us_sat((0.56f - cu->y0) / 0.2f);
		float lifev = cu->life * (0.65f + 0.45f * near) * share;
		float lscale = 0.6f + 0.7f * near;
		float xprev = curtain_u2x(cu, 0.0f, T, s->spread);
		for (int k = 0; k < NSAMP; k++) {
			float u = (k + 0.5f) / NSAMP;
			float fa = u * US_TAU * 1.8f + T * 0.55f + cu->seed;
			float xf = curtain_u2x(cu, u, T, s->spread);
			/* snare folds: a sharp kink snaps in sideways and lifts the rim there */
			float kink = 0.0f;
			for (int fi = 0; fi < MAX_FOLD; fi++)
				if (s->fold_c[fi] == ci && famp[fi] != 0.0f) {
					float d = (u - s->fold_u[fi]) * 14.0f;
					kink += famp[fi] * s->fold_amp[fi] * expf(-d * d);
				}
			/* where the ribbon turns edge-on its light piles up: let a fold glow, but not into a slab */
			float slope = fabsf(xf - xprev) * NSAMP / (cu->span * s->spread);
			xprev = xf;
			float pile = us_clampf(0.3f + 0.9f * slope, 0.3f, 1.0f);
			xf += kink;
			float ripple = 0.006f * sinf(u * 70.0f - cu->ph) + 0.004f * sinf(u * 23.0f - cu->ph * 0.6f + 1.0f);
			float x = xf * A + ripple * (0.4f + s->shimmer);
			float yb = cu->y0 - lift + 0.07f * us_noise2(u * 2.4f + T * 0.2f, cu->seed + 7.0f) +
				   0.03f * us_noise2(u * 5.0f + T * 0.5f, cu->seed + 9.0f) - 0.09f * cu->fold * cosf(fa) * sinf(US_PI * u) +
				   0.01f * sinf(u * 9.0f - cu->ph * 0.3f) + 0.045f * sinf(u * US_TAU * 0.9f + T * 0.25f + cu->seed * 2.3f) -
				   1.2f * fabsf(kink);
			float ends = us_smooth(0.0f, 0.3f, u) * us_smooth(1.0f, 0.7f, u);
			float ln = L0 * lscale * (0.45f + 0.75f * us_sat(0.55f + 0.7f * us_noise2(u * 5.0f - T * 0.4f, cu->seed + 3.0f)) +
						  0.25f * us_noise2(u * 17.0f + T * 0.3f, cu->seed + 5.0f));
			ln *= 0.25f + 0.75f * ends; /* the curtain's ends shrink away, no hard vertical edge */
			float patch = us_sat(0.45f + 1.1f * us_noise2(u * 6.0f - T * 0.9f, cu->seed + 13.0f) +
					     0.35f * us_noise2(u * 19.0f - T * 1.7f, cu->seed + 17.0f));
			/* rays lean a little, varying along the curtain and swaying slowly: folds curve, never walls */
			float lean = 0.22f * us_noise2(u * 3.0f + T * 0.15f, cu->seed + 29.0f) + 0.08f * (x / A - 0.5f);
			/* the spectrum laid along the ribbon (mids in the middle): each band lights and lifts its stretch */
			float bf = 4.0f + 23.0f * (ci & 1 ? 1.0f - u : u);
			int bi = (int)bf;
			float bandk = us_lerp(s->bandv[bi], s->bandv[bi + 1], bf - bi);
			/* kick pulses travelling out along the ribbon */
			float pul = 0;
			for (int pi = 0; pi < MAX_PULSE; pi++)
				if (pamp[pi] > 0.01f) {
					float d = fabsf(u - s->pulse_u[pi]) - pdist[pi];
					pul += pamp[pi] * expf(-d * d * 140.0f);
				}
			ln *= 0.85f + 0.4f * bandk + 0.15f * pul;
			float b = lifev * (0.25f + 0.75f * patch) * ends * pile * (0.7f + 0.6f * bandk) * (1.0f + 1.5f * pul + 1.2f * fabsf(kink) / 0.06f);
			float fln = subf > 0.05f ? (0.003f + 0.018f * subf) * lscale : 0.0f;
			deposit(s, A, x, yb, ln, b * 0.62f, fringe, pinkb, kick, fln, 0.45f * subf, lean);
			if (!(k & 1) && rimn[ci] < NSAMP / 2 + 1) {
				rimx[ci][rimn[ci]] = x;
				rimy[ci][rimn[ci]] = yb;
				rimb[ci][rimn[ci]] = b * (1.0f + 0.5f * pul);
				rimn[ci]++;
			}
		}
	}

	/* ---- the quiet arc: a faint, homogeneous band low over the ridge, the aurora's resting form, breathing ---- */
	{
		int nq = 120;
		float qb = 0.16f + (ctx->crisp ? 0.07f : 0.14f) * quiet * (0.6f + 0.8f * breath) + 0.06f * a->swell;
		float qT = s->T * 0.35f;
		for (int k = 0; k < nq; k++) {
			float u = (k + 0.5f) / nq;
			float y = 0.555f - 0.035f * sinf(US_PI * u) + 0.012f * us_noise2(u * 2.2f + qT, 21.0f) - 0.03f * (s->height - 0.4f);
			float patch = us_sat(0.6f + 0.8f * us_noise2(u * 6.0f - qT * 1.6f, 23.0f));
			float ln = 0.035f + 0.03f * patch;
			deposit(s, A, u * A, y, ln, qb * (0.4f + 0.6f * patch), 2.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
		}
	}

	/* tone: soft saturation (folds glow but never clip into a slab); pinkness = pink share of the light */
	float glow[64];
	memset(glow, 0, sizeof(glow));
	for (int j = 0; j < s->gh; j++) {
		int mn = s->gw, mx = -1;
		for (int i = 0; i < s->gw; i++) {
			int q = j * s->gw + i;
			float f = s->fld[q];
			if (f > 1e-4f) {
				s->pk[q] = us_sat(s->pk[q] / f);
				float V = 1.0f - expf(-0.9f * f);
				s->fld[q] = V;
				if (i < mn) mn = i;
				mx = i;
				int gi = i * 64 / s->gw;
				if (V > glow[gi])
					glow[gi] = V;
			} else {
				s->fld[q] = 0.0f;
				s->pk[q] = 0.0f;
			}
		}
		s->rmin[j] = (short)mn;
		s->rmax[j] = (short)mx;
	}

	/* brightness: loudness lifts the whole sky; quiet sinks to a faint breathing glow; the kick flashes it */
	float gain = strength * 1.25f * (0.42f + 1.0f * s->lvl_env + 0.1f * quiet * breath) * (1.0f + 0.35f * kick) * (0.95f + 0.2f * storm);
	/* pixel's dithered fall-off compresses the light: let loudness and the kick push it harder there */
	s->lvlk = 0.5f * strength * us_sat(a->level);
	if (ctx->crisp)
		gain *= (0.2f + 1.35f * s->lvl_env) * (1.0f + 0.95f * kick + 0.05f * snare);
	for (int g = 0; g < 64; g++)
		glow[g] *= gain * 1.6f;
	raster(s, c, A, 0, (int)ceilf(SKY_Y * c->s), 0.0f, 1.0f, gain, 0, ctx->t, ctx->crisp);
	/* pixel: a dithered airglow low over the ridge, under the curtains -- the sky's light scattered toward the
	 * horizon, brighter as the music swells (its dot density grows evenly with loudness) */
	if (ctx->crisp) {
		float S = c->s, ag = strength * (0.01f + 0.11f * s->lvl_env + 0.1f * kick);
		for (int px = 0; px < W; px++) {
			float rg = s->ridgeAt[px], h = 0.24f;
			int y0 = (int)((rg - h) * S), y1 = (int)(rg * S);
			for (int py = y0 < 0 ? 0 : y0; py < y1 && py < c->h; py++) {
				float d = rg - (py + 0.5f) / S; /* height above the ridge */
				float f = 1.0f - us_smooth(0.0f, h, d);
				float v = ag * f * f;
				int iv = dith(v, px, py);
				size_t ii = (size_t)py * c->w + px;
				if (iv > c->ink[ii]) {
					c->ink[ii] = (uint8_t)iv;
					c->mat[ii] = US_MAT_LEAF;
				}
			}
		}
	}
	/* the lower rim: a fine bright line along each curtain's base, where its light is sharpest */
	for (int ci = 0; ci < MAX_CURT; ci++)
		for (int k = 1; k < rimn[ci]; k++) {
			float x0 = rimx[ci][k - 1], y0 = rimy[ci][k - 1], x1 = rimx[ci][k], y1 = rimy[ci][k];
			float dx = x1 - x0, dy = y1 - y0;
			if (dx * dx + dy * dy > 0.0009f)
				continue;
			int px = (int)(x1 * c->s);
			if (px < 0 || px >= W || y1 > s->ridgeAt[px] || y0 > s->ridgeAt[px])
				continue;
			float rv = us_sat(gain * 0.75f * us_sat(0.5f * (rimb[ci][k] + rimb[ci][k - 1])) * (1.0f + 0.6f * kick));
			if (rv < 0.03f)
				continue;
			if (ctx->crisp)
				us_line(c, x0, y0, x1, y1, 0.001f, rv, US_MAT_LEAF);
			else
				rim(c, x0, y0, x1, y1, rv, US_MAT_LEAF);
		}
	/* reflection in the still lake: the same light, mirrored about the shore, rippled and dimmer */
	raster(s, c, A, (int)(SHORE * c->s) + 2, c->h, 3.0f * SHORE + 0.01f, -2.0f, gain * (ctx->crisp ? 0.75f : 0.7f), 1, ctx->t, ctx->crisp);
	c->blend = US_BLEND_MAX;

	/* ---- pixel styles by day: banded pale-blue sky behind (MAX) the ghost aurora; ink stays black ---- */
	{
		float dayk = us_smooth(0.22f, 0.7f, day); /* none at night/twilight: mats would linger under the trails */
		uint8_t sm = ctx->crisp ? US_MAT_WATER : US_MAT_BG;
		(void)sm;
		float f[2 * 8];
		for (int j = 0; j < 8; j++) {
			float y = j / 7.0f;
			float v = (ctx->crisp ? 0.06f + 0.34f * y : 0.03f + 0.16f * y * y) * dayk;
			f[j * 2] = f[j * 2 + 1] = v;
		}
		if (dayk > 0.0f && ctx->crisp) {
			us_field(c, f, 2, 8, 1.0f, sm, 0, 0, A, SHORE);
			/* the lake mirrors the pale sky, darker */
			float g[2 * 4];
			for (int j = 0; j < 4; j++)
				g[j * 2] = g[j * 2 + 1] = f[14 - 2 * j] * 0.6f;
			us_field(c, g, 2, 4, 1.0f, sm, 0, SHORE, A, 1.0f);
		}
	}

	/* ---- mountains ---- */
	{
		float pts[2 * 97];
		for (int i = 0; i <= 96; i++) {
			float u = i / 96.0f;
			pts[2 * i] = u * A;
			pts[2 * i + 1] = ridge_y(u);
		}
		float mv = 0.32f + 0.15f * day;
		if (!ctx->crisp) { /* overwrite the mountain mass so faint stars do not show through it */
			for (int px = 0; px < c->w; px++) {
				float uu = (px + 0.5f) / c->s / A * 96.0f;
				int i = (int)uu;
				if (i > 95) i = 95;
				int y0 = (int)ceilf(us_lerp(pts[2 * i + 1], pts[2 * i + 3], uu - i) * c->s) + 1, y1 = (int)(SHORE * c->s);
				for (int py = y0 < 0 ? 0 : y0; py < y1 && py < c->h; py++) {
					size_t ii = (size_t)py * c->w + px;
					if (c->ink[ii] < 3) {
						c->ink[ii] = 3;
						c->mat[ii] = US_MAT_EARTH;
					}
				}
			}
		}
		if (!ctx->crisp)
			us_polyline(c, pts, 97, 0.0013f, mv, US_MAT_EARTH);
		/* aurora light on the snowy crests */
		for (int i = 0; i < 96; i++) {
			float g = glow[(int)(i / 96.0f * 63.0f)] * strength;
			float gv = us_sat(g * 0.5f);
			if (gv > 0.02f)
				(ctx->crisp ? us_line : us_line)(c, pts[2 * i], pts[2 * i + 1] + (ctx->crisp ? 0.0f : 0.0025f), pts[2 * i + 2],
							  pts[2 * i + 3] + (ctx->crisp ? 0.0f : 0.0025f), ctx->crisp ? 0.004f : 0.0011f, gv, US_MAT_LEAF);
		}
		if (ctx->crisp) {
			/* pixel: a blue night range lit from above -- snow caps on the high peaks (dithered into the rock),
			 * the rock fading by dither into darkness toward its feet behind the foothills */
			/* (day terms gated: the dither would show every slow drift of the night's light) */
			float dk = us_smooth(0.25f, 0.7f, day);
			float S = c->s, body = 0.15f + 0.2f * dk, snowv = 0.5f + 0.15f * winter + 0.2f * dk;
			for (int px = 0; px < W; px++) {
				float u = (px + 0.5f) / S / A;
				float r = s->ridgeAt[px] + 0.002f, hgt = 0.705f - r;
				float g = us_sat(strength * s->lvl_env); /* the louder sky lights the snow */
				float gul = fabsf(us_noise2(u * 70.0f, 7.7f)) + 0.5f * fabsf(us_noise2(u * 170.0f, 2.1f)); /* gullies hold snow lower */
				float sd = us_sat((hgt - 0.01f) / 0.07f) * (0.01f + 0.065f * gul * gul);
				int y0 = (int)ceilf(r * S), y1 = (int)(SHORE * S);
				for (int py = y0 < 0 ? 0 : y0; py < y1 && py < c->h; py++) {
					float d = (py + 0.5f) / S - r;
					float bv = body * (1.0f - 0.2f * us_smooth(0.03f, 0.14f, d));
					float sn = sd > 0.0f ? 1.0f - us_smooth(sd * 0.45f, sd, d) : 0.0f;
					float v = bv + (snowv * (0.75f + 0.6f * g) - bv) * sn;
					int iv = dith(v, px, py);
					size_t ii = (size_t)py * c->w + px;
					if (iv >= c->ink[ii]) {
						c->ink[ii] = (uint8_t)iv;
						c->mat[ii] = sn > 0.2f ? US_MAT_ACCENT : US_MAT_WATER;
					}
				}
			}
		} else {
			/* snow on the highest ridges: short hatch strokes down the flanks */
			for (int i = 0; i < 96; i += 2) {
				float u = i / 96.0f, y = pts[2 * i + 1];
				float g = us_sat(glow[(int)(u * 63.0f)] * strength * 0.35f);
				if (y < 0.65f) {
					float sl = pts[2 * i + 3] > y ? 1.0f : -1.0f; /* streak down the slope */
					us_line(c, u * A, y + 0.004f, u * A + 0.006f * sl, y + 0.02f + (0.65f - y) * 0.45f, 0.0009f,
						0.12f + 0.22f * winter + g, g > 0.08f ? US_MAT_LEAF : US_MAT_WATER);
				}
			}
		}
	}

	/* ---- foothills: a nearer, darker range between the peaks and the shore ---- */
	{
		float fp[2 * 65];
		for (int i = 0; i <= 64; i++) {
			float u = i / 64.0f;
			fp[2 * i] = u * A;
			fp[2 * i + 1] = SHORE - 0.022f - 0.022f * (0.5f + 0.5f * us_noise2(u * 4.2f, 6.3f)) - 0.004f * us_noise2(u * 19.0f, 2.0f);
		}
		for (int px = 0; px < c->w; px++) {
			float uu = (px + 0.5f) / c->s / A * 64.0f;
			int i = (int)uu;
			if (i > 63) i = 63;
			int y0 = (int)ceilf(us_lerp(fp[2 * i + 1], fp[2 * i + 3], uu - i) * c->s) + 1, y1 = (int)(SHORE * c->s);
			for (int py = y0 < 0 ? 0 : y0; py < y1 && py < c->h; py++) {
				size_t ii = (size_t)py * c->w + px;
				c->ink[ii] = ctx->crisp ? (uint8_t)(255 * (0.09f + 0.2f * us_smooth(0.25f, 0.7f, day))) : 2;
				c->mat[ii] = US_MAT_EARTH;
			}
		}
		for (int i = 0; i < 64; i++) {
			float g = us_sat(glow[(int)(i / 64.0f * 63.0f)] * strength * 0.2f);
			wline_(c, ctx->crisp, fp[2 * i], fp[2 * i + 1], fp[2 * i + 2], fp[2 * i + 3], (ctx->crisp ? 0.09f : 0.24f) + 0.1f * day + g, US_MAT_EARTH);
		}
	}

	/* ---- shore + spruce treeline ---- */
	{
		int nt = (int)(s->ntr * (0.35f + 0.65f * dens));
		float tv = 0.30f + 0.25f * day;
		if (ctx->crisp) {
			/* pixel: one solid dark stand -- pointed spruces with stepped bough tiers over a low undergrowth band,
			 * and its dark reflection under the shore; drawn into a per-column top line, then filled */
			float S = c->s;
			int sy = (int)(SHORE * S);
			float *top = s->colR; /* the ray texture is spent by now: reuse it as the treeline's top per column */
			for (int px = 0; px < W; px++) {
				float u = (px + 0.5f) / S / A;
				top[px] = SHORE - 0.007f - 0.004f * (0.5f + 0.5f * us_noise2(u * 23.0f, 1.9f));
			}
			for (int i = 0; i < s->ntr; i++) {
				spruce_t *t = &s->tr[i];
				if ((int)(t->x * 997.0f) % s->ntr >= nt || fabsf(t->x - s->cab_x) * A < 0.03f)
					continue;
				/* row by row from the tip: a slender cone notched into bough tiers (each tier flares, then steps in) */
				float tx = t->x * A * S, th = t->h * 1.6f * S, tw = t->w * 1.15f * S;
				int r0 = (int)floorf((SHORE + 0.002f) * S - th), r1 = (int)(SHORE * S);
				for (int py = r0 < 0 ? 0 : r0; py <= r1; py++) {
					float tt = (py + 0.5f - ((SHORE + 0.002f) * S - th)) / th;
					float tier = tt * 3.3f + 0.15f;
					float hw = tw * tt * (0.62f + 0.38f * (tier - floorf(tier))) + 0.15f;
					int x0 = (int)floorf(tx - hw + 0.5f), x1 = (int)floorf(tx + hw - 0.5f);
					if (x1 < x0)
						x1 = x0 = (int)floorf(tx);
					for (int px = x0 < 0 ? 0 : x0; px <= x1 && px < W; px++)
						if (py / S < top[px])
							top[px] = py / S;
				}
			}
			for (int px = 0; px < W; px++) {
				int y0 = (int)floorf(top[px] * S + 0.01f);
				int ry = sy + (int)((SHORE - top[px]) * 0.75f * S + 0.5f); /* the reflection, a little shortened */
				for (int py = y0 < 0 ? 0 : y0; py <= ry && py < c->h; py++) {
					size_t ii = (size_t)py * c->w + px;
					c->ink[ii] = 0;
					c->mat[ii] = US_MAT_BG;
				}
			}
		}
		for (int i = 0; i < (ctx->crisp ? 0 : s->ntr); i++) {
			spruce_t *t = &s->tr[i];
			/* keep the full stand ordering stable: skip by hash, not by index */
			if ((int)(t->x * 997.0f) % s->ntr >= nt)
				continue;
			if (fabsf(t->x - s->cab_x) * A < 0.03f) /* the cabin's clearing */
				continue;
			spruce(c, ctx->crisp, t->x * A, SHORE + 0.002f, t->h, t->w, tv);
			/* mirrored in the still water, dim and shortened */
			spruce(c, ctx->crisp, t->x * A, SHORE + 0.004f, -t->h * 0.7f, t->w * 0.9f, tv * (ctx->crisp ? 0.5f : 0.32f));
		}
		us_line(c, 0, SHORE, A, SHORE, 0.0012f, ctx->crisp ? 0.1f : 0.35f, ctx->crisp ? US_MAT_WATER : US_MAT_EARTH);
		cabin(s, ctx, c, day);
		fox(s, ctx, c, day);
		/* the far ridge, reflected and broken by slow ripples */
		if (!ctx->crisp) {
			float px = 0, py = 0;
			for (int i = 0; i <= 96; i++) {
				float u = i / 96.0f;
				float y = SHORE + (SHORE - ridge_y(u)) * 0.5f;
				float x = u * A + 0.003f * us_noise2(u * 40.0f, (float)ctx->t * 0.5f);
				if (i > 0 && us_noise2(u * 23.0f, (float)ctx->t * 0.3f) > -0.15f)
					rim(c, px, py, x, y, 0.12f + 0.05f * day, US_MAT_WATER);
				px = x;
				py = y;
			}
		}
		/* lake: where the sky glows, a broken glimmer lies on the water just off the shore */
		for (int g = 0; g < 64; g++) {
			float gv = us_sat(glow[g] * strength * 0.3f);
			if (gv < 0.03f)
				continue;
			float x0 = g / 63.0f * A;
			for (int k = 0; k < (ctx->crisp ? 2 : 3); k++) {
				float y = SHORE + 0.02f + 0.022f * k + 0.003f * us_noise2(g * 0.7f, k + (float)ctx->t * 0.3f);
				float off = 0.01f * us_noise2(g * 1.3f + k * 5.0f, (float)ctx->t * 0.4f);
				float len = A / 63.0f * (0.5f + 0.4f * us_noise2(g * 2.1f, k * 3.0f + (float)ctx->t * 0.5f));
				if (ctx->crisp)
					us_line(c, x0 + off, y, x0 + off + len, y, 0.004f, gv * (1.0f - 0.25f * k), US_MAT_WATER);
				else
					rim(c, x0 + off, y, x0 + off + len, y, gv * (1.0f - 0.25f * k), US_MAT_WATER);
			}
		}
		/* lake: a few faint still-water lines (ink only) */
		for (int k = 0; k < (ctx->crisp ? 0 : 5); k++) {
			float y = SHORE + 0.03f + k * k * 0.009f;
			float x0 = (0.1f + 0.5f * (0.5f + 0.5f * us_noise2(k * 3.1f, (float)ctx->t * 0.02f))) * A;
			us_line(c, x0, y, x0 + A * (0.1f + 0.06f * k), y, 0.0009f, 0.10f + 0.05f * day, US_MAT_WATER);
		}
		/* the brighter low stars, mirrored and trembling on the water */
		float sv = us_sat(1.0f - day * 1.6f);
		if (sv > 0.01f)
			for (int i = 0; i < MAX_STARS / 2; i++) {
				star_t *t = &s->st[i];
				if (t->m < 0.3f || t->y < 0.25f)
					continue;
				float ry = SHORE + (SHORE - t->y) * 0.5f;
				if (ry > 0.995f || t->y > ridge_y(t->x) - 0.01f)
					continue;
				float wob = 0.0025f * us_noise2(t->tw, (float)ctx->t * 0.9f);
				float v = t->m * sv * (0.3f + 0.15f * us_noise2(t->tw + 7.0f, s->twk));
				us_point(c, t->x * A + wob, ry, 0.0011f, v, US_MAT_WATER);
			}
		/* ice along the near edge: fine cracks, glinting when the sub makes the lake boom */
		float icev = 0.06f + 0.1f * winter;
		for (int i = 0; i < MAX_CRACK; i++) {
			float g = s->crack_g[i];
			float v = icev + 0.45f * g;
			float *p = s->crack[i];
			for (int k = 0; k + 1 < CRACK_PTS; k++)
				wline_(c, ctx->crisp, p[2 * k] * A, p[2 * k + 1], p[2 * k + 2] * A, p[2 * k + 3],
				       v * (1.0f - 0.08f * k), g > 0.4f ? US_MAT_LIGHT : US_MAT_WATER);
			/* a short side branch */
			int bk = 3 + i % 3;
			wline_(c, ctx->crisp, p[2 * bk] * A, p[2 * bk + 1], p[2 * bk] * A + 0.01f, p[2 * bk + 1] + (i & 2 ? 0.008f : -0.006f),
			       v * 0.7f, US_MAT_WATER);
		}
	}
}

const us_scene_vt us_scene_aurora = {"aurora", "Aurora", 0.25f, create, destroy, frame};
