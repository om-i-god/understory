/* Understory scene: Misty Valley — a mountain valley in the manner of an ink-wash landscape (shan shui).
 *
 * Four ranges stand one behind the other: pale far spires, the towering main massif, the cliff the
 * waterfall drops from (with a pavilion on its ledge) and the near cliffs (an old pine reaching out over
 * the valley on the left, a bamboo grove on the right bank). Each range is a graded wash that is darkest
 * at its crest and dissolves downward into mist, textured with streaky "hemp-fibre" strokes; crest lines,
 * texture strokes, moss dots and far pines are drawn on top as fine single-weight lines. The washes are
 * resolved painter-style in a low-res grid (nearer ranges cover farther ones), mist is mixed into the same
 * grid, then the grid is upsampled once onto the canvas. A thin waterfall feeds a river that winds through
 * the valley floor toward the viewer; red-crowned cranes stand in the shallows and cross the sky.
 * Audio: kick -> the waterfall surges (a bright slug of water races down, more strands, spray thrown up)
 *        and a wave of mist billows out along the valley floor from its foot, the bamboo is shoved over;
 *        snare -> cranes lift off the shallows / rise out of the mist with big wingbeats, the old pine
 *        shakes and loses a burst of needles, the bamboo leaves shiver; hats -> spray glints at the fall's
 *        foot, glitter down the river and dew on the bamboo tips, a different handful each frame of the
 *        tick; loudness -> the mist thins and lifts to reveal the far peaks, the light warms (sun halo,
 *        gold-lit crests); swell -> mist height; bands[] -> ripple marks down the river (far = highs, near =
 *        lows); width -> mist drift; sub -> rings on the plunge pool; lowmid -> the pavilion lantern;
 *        air -> how much glitters. Pixel styles keep the mist and light it with the loudness instead.
 * World: daylight (sun / moon, stars, lantern), season (spring plum blossom, summer green, autumn maple
 *        and falling leaves, winter snow caps, a thinner half-frozen fall and snowfall), moisture (mist
 *        density), water_level (river width, fall volume), wind (mist drift, bamboo lean), flock (how many
 *        cranes); writes moisture (the fall's spray).
 * Silence: mist keeps drifting and breathing, water keeps falling, cranes preen and now and then one
 *          glides in to land or flies off; the scene never stops.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define NL 4         /* mountain ranges, far -> near */
#define RN 513       /* ridge samples per range over u in [0,1] */
#define GWM 480      /* wash grid max */
#define GHM 272
#define MW 96        /* mist grid */
#define MH 56
#define MAXCOL 4096
#define MAX_CUN 260
#define MAX_DOT 260
#define MAX_FARPINE 40
#define MAX_CRANE 10
#define MAX_PART 360
#define MAX_PUFF 10
#define MAX_BAMBOO 16
#define MAX_RIP 70
#define MAX_STAR 120
#define NRIV 64      /* river centreline samples */
#define MAX_FOLD 96
#define FOLD_N 45
#define PINE_SEG 48
#define PINE_PAD 14
#define NPINE 4

/* crisp-canvas ink values that land on palette shade steps 1..3 (after the compose gamma lift) */
#define STEP1 0.17f
#define STEP2 0.38f
#define STEP3 0.62f

#define U_FALL 0.700f /* the waterfall's column (fraction of width) */
#define Y_POOL 0.745f
#define U_PAV 0.583f  /* the pavilion's ledge */

typedef struct {
	float c, wl, wr, top, foot, pw;
} peak_t;

typedef struct {
	float u, len, bend, v; /* a texture stroke down a flank */
	int l;
} cun_t;

typedef struct {
	float u, dy, r;
	int l;
} dot_t;

typedef struct {
	float u, h;
	int l;
} fpine_t;

typedef struct {
	float x, y, vx, vy; /* x as a fraction of width */
	float ph, rate, sc, life, tgt_x, tgt_y, t, kick;
	int state; /* 0 off, 1 standing, 2 flying away, 3 gliding in to land, 4 crossing */
	float dir;
} crane_t;

typedef struct {
	float x, y, vx, vy, life, len, ang, spin; /* x in normalized units (not fraction) */
	uint8_t mat, kind; /* kind 0 needle, 1 spray, 2 petal/leaf, 3 snow */
} part_t;

typedef struct {
	float x, y, r, life, amp; /* x fraction of width */
} puff_t;

typedef struct {
	float u, h, lean, ph;
	int nodes;
} bamboo_t;

typedef struct {
	float t, s, len, ph; /* along the river (0 far .. 1 near), across (-1..1) */
	int band;
} rip_t;

typedef struct {
	/* a pine: trunk + branch segments (pairs of points) and needle pads, relative to its root, height 1 */
	float seg[PINE_SEG * 4];
	int nseg;
	float pad[PINE_PAD * 3]; /* dx, dy, r */
	int npad;
	float u, y, sc, flip;
} pine_t;

typedef struct {
	us_rng rng;
	float ridge[NL][RN];
	float occ[NL][RN]; /* top of the nearest range in front of range l */
	cun_t cun[MAX_CUN];
	int ncun;
	dot_t dot[MAX_DOT];
	int ndot;
	fpine_t fp[MAX_FARPINE];
	int nfp;
	pine_t pine[NPINE];
	crane_t cr[MAX_CRANE];
	part_t pt[MAX_PART];
	int pt_i;
	puff_t pf[MAX_PUFF];
	int pf_i;
	bamboo_t bb[MAX_BAMBOO];
	int nbb;
	rip_t rp[MAX_RIP];
	float star[MAX_STAR * 3];
	float riv[NRIV * 3]; /* centre u, y, half-width (normalized height units) */
	float fold_u[MAX_FOLD][FOLD_N], fold_y[MAX_FOLD][FOLD_N], fold_k[MAX_FOLD][FOLD_N]; /* k: ink weight, 0 = hidden */
	int fold_l[MAX_FOLD], nfold;

	/* static wash grid, rebuilt when the grid size / aspect changes */
	int gw, gh;
	float gA;
	uint8_t lay[GWM * GHM];   /* visible range (255 = sky) */
	float st[GWM * GHM];      /* static wash value (shape * texture) */
	float snow[GWM * GHM];    /* where snow would lie */
	uint8_t water[GWM * GHM]; /* river coverage */
	float fv[GWM * GHM];      /* per-frame composite value (land / water / snow, behind the mist) */
	float fq[GWM * GHM];      /* ... and the mist in front of it */
	uint8_t fm[GWM * GHM];    /* ... and material */
	float mist[MW * MH];
	int cx0[MAXCOL], cx1[MAXCOL];
	float ctx_[MAXCOL];
	float rowmax[GHM];
	/* two wash rows expanded to canvas width (land, mist, material), reused across canvas rows */
	float hv[2][MAXCOL], hq[2][MAXCOL];
	uint8_t hm[2][MAXCOL];
	int hrow[2];

	/* envelopes */
	float lvl, surge, billow, kage, flash, glint, hat_seedf, shake, warm, sub_env, lm_env, air_env, drift, mistT, fallT;
	float bsw, bvel; /* bamboo sway spring */
	float bandv[US_NBANDS];
	float snare_cool, kick_cool, land_t, cross_t, ring_t[3];
	int ring_i;
	uint32_t hat_seed;
	int inited;
} st_t;

/* ------------------------------------------------------------------ helpers */

static float hash1(int k, float seed)
{
	uint32_t h = (uint32_t)k * 2654435761u ^ (uint32_t)(seed * 1000.0f) * 40503u;
	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return (h & 0xffffff) / 16777216.0f;
}

/* Fast thin line for ink: Wu-style two-pixel antialiasing (falls back to us_line on crisp / wide lines). */
static void wline(us_canvas *c, float ax, float ay, float bx, float by, float width, float v, uint8_t mat)
{
	float wp = width * c->s;
	if (c->crisp || wp > 1.6f) {
		us_line(c, ax, ay, bx, by, width, v, mat);
		return;
	}
	if (!(v > 0.004f))
		return;
	v *= us_clampf(wp, 0.35f, 1.0f);
	float x0 = ax * c->s - 0.5f, y0 = ay * c->s - 0.5f, x1 = bx * c->s - 0.5f, y1 = by * c->s - 0.5f;
	if (!(x0 == x0) || !(y0 == y0) || !(x1 == x1) || !(y1 == y1))
		return;
	float dx = x1 - x0, dy = y1 - y0;
	int steep = fabsf(dy) > fabsf(dx);
	if (steep) {
		float t = x0; x0 = y0; y0 = t;
		t = x1; x1 = y1; y1 = t;
		t = dx; dx = dy; dy = t;
	}
	if (x0 > x1) {
		float t = x0; x0 = x1; x1 = t;
		t = y0; y0 = y1; y1 = t;
	}
	float g = fabsf(dx) > 1e-6f ? dy / dx : 0.0f;
	int i0 = (int)floorf(x0 + 0.5f), i1 = (int)floorf(x1 + 0.5f);
	int lim = steep ? c->h : c->w;
	if (i0 < 0) i0 = 0;
	if (i1 > lim - 1) i1 = lim - 1;
	if (i1 - i0 > 4000)
		return;
	for (int i = i0; i <= i1; i++) {
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

static void wpoly(us_canvas *c, const float *xy, int n, float width, float v, uint8_t mat)
{
	for (int i = 0; i + 1 < n; i++)
		wline(c, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], width, v, mat);
}

/* a crisp-aware stroke: fine in ink, one pixel in pixel styles */
static void stroke(us_canvas *c, float ax, float ay, float bx, float by, float wi, float v, uint8_t m)
{
	if (c->crisp)
		us_line(c, ax, ay, bx, by, 0.001f, v, m);
	else
		wline(c, ax, ay, bx, by, wi, v, m);
}

/* slender leaf / petal: filled lens when crisp, outlined lens in ink */
static void petal(us_canvas *c, float cx, float cy, float ang, float len, float wid, float v, uint8_t mat)
{
	float dx = cosf(ang), dy = sinf(ang);
	float tx = cx + dx * len, ty = cy + dy * len;
	float mx = cx + dx * len * 0.45f, my = cy + dy * len * 0.45f;
	float nx = -dy * wid, ny = dx * wid;
	if (c->crisp) {
		us_tri(c, cx, cy, mx + nx, my + ny, tx, ty, v, mat);
		us_tri(c, cx, cy, mx - nx, my - ny, tx, ty, v, mat);
	} else {
		float pts[] = {cx, cy, mx + nx, my + ny, tx, ty, mx - nx, my - ny, cx, cy};
		wpoly(c, pts, 5, 0.0011f, v, mat);
	}
}

/* a glint: a bright point with a small four-ray star */
static void sparkle(us_canvas *c, float x, float y, float r, float v, uint8_t m)
{
	if (v < 0.02f)
		return;
	if (c->crisp) {
		us_point(c, x, y, 0.003f, v, m);
		if (v > 0.5f) {
			us_line(c, x - r, y, x + r, y, 0.001f, v * 0.6f, m);
			us_line(c, x, y - r, x, y + r, 0.001f, v * 0.6f, m);
		}
		return;
	}
	us_disc(c, x, y, 0.0014f, v, m);
	wline(c, x - r, y, x + r, y, 0.001f, v * 0.7f, m);
	wline(c, x, y - r, x, y + r, 0.001f, v * 0.7f, m);
}

static float ridge_at(const st_t *s, int l, float u)
{
	float f = us_sat(u) * (RN - 1);
	int i = (int)f;
	if (i >= RN - 1)
		i = RN - 2;
	return us_lerp(s->ridge[l][i], s->ridge[l][i + 1], f - i);
}

static float occ_at(const st_t *s, int l, float u)
{
	float f = us_sat(u) * (RN - 1);
	int i = (int)f;
	if (i >= RN - 1)
		i = RN - 2;
	return us_lerp(s->occ[l][i], s->occ[l][i + 1], f - i);
}

static float mist_at(const st_t *s, float x, float y, float A)
{
	float fx = x / A * MW - 0.5f, fy = y * MH - 0.5f;
	if (!(fx == fx) || !(fy == fy))
		return 0;
	int ix = (int)floorf(fx), iy = (int)floorf(fy);
	float tx = fx - ix, ty = fy - iy;
	int x0 = ix < 0 ? 0 : ix >= MW ? MW - 1 : ix, x1 = ix + 1 < 0 ? 0 : ix + 1 >= MW ? MW - 1 : ix + 1;
	int y0 = iy < 0 ? 0 : iy >= MH ? MH - 1 : iy, y1 = iy + 1 < 0 ? 0 : iy + 1 >= MH ? MH - 1 : iy + 1;
	float a = us_lerp(s->mist[y0 * MW + x0], s->mist[y0 * MW + x1], tx);
	float b = us_lerp(s->mist[y1 * MW + x0], s->mist[y1 * MW + x1], tx);
	return us_lerp(a, b, ty);
}

/* ------------------------------------------------------------------ the ranges */

/* each peak stands on its own foot; beyond its half-width the flank keeps falling, so ranges never end in
 * a flat baseline: {centre, half-width left, right, top y, foot y, roundness (smaller = steeper, blunter)} */
static const peak_t P0[] = {{0.03f, 0.05f, 0.05f, 0.47f, 0.63f, 0.6f},  {0.11f, 0.045f, 0.04f, 0.40f, 0.63f, 0.5f},
			    {0.19f, 0.04f, 0.05f, 0.44f, 0.63f, 0.55f}, {0.28f, 0.05f, 0.04f, 0.41f, 0.63f, 0.5f},
			    {0.36f, 0.035f, 0.04f, 0.46f, 0.63f, 0.6f}, {0.62f, 0.04f, 0.045f, 0.33f, 0.63f, 0.5f},
			    {0.70f, 0.035f, 0.04f, 0.38f, 0.63f, 0.55f}, {0.785f, 0.05f, 0.04f, 0.30f, 0.63f, 0.45f},
			    {0.87f, 0.04f, 0.05f, 0.27f, 0.63f, 0.45f}, {0.955f, 0.05f, 0.05f, 0.36f, 0.63f, 0.55f}};
static const peak_t P1[] = {{0.47f, 0.045f, 0.075f, 0.10f, 0.74f, 0.4f},  {0.402f, 0.03f, 0.042f, 0.25f, 0.74f, 0.38f},
			    {0.552f, 0.026f, 0.05f, 0.215f, 0.74f, 0.45f}, {0.335f, 0.05f, 0.03f, 0.37f, 0.74f, 0.32f},
			    {0.64f, 0.035f, 0.05f, 0.32f, 0.74f, 0.5f},    {0.235f, 0.06f, 0.035f, 0.46f, 0.74f, 0.28f},
			    {0.12f, 0.07f, 0.05f, 0.5f, 0.74f, 0.6f},      {0.865f, 0.05f, 0.08f, 0.41f, 0.74f, 0.35f},
			    {0.745f, 0.035f, 0.05f, 0.45f, 0.74f, 0.55f}};
static const peak_t P2[] = {{0.785f, 0.115f, 0.24f, 0.37f, 0.80f, 0.3f}, {U_PAV, 0.026f, 0.03f, 0.655f, 0.765f, 0.5f},
			    {0.19f, 0.06f, 0.045f, 0.61f, 0.80f, 0.5f}, {0.95f, 0.06f, 0.06f, 0.33f, 0.80f, 0.4f},
			    {0.285f, 0.035f, 0.05f, 0.68f, 0.81f, 0.55f}};
static const peak_t P3[] = {{0.0f, 0.1f, 0.165f, 0.40f, 1.02f, 0.42f}, {1.0f, 0.2f, 0.2f, 0.80f, 1.05f, 0.6f},
			    {0.335f, 0.06f, 0.08f, 0.955f, 1.03f, 0.35f}};
static const int NPK[NL] = {10, 9, 5, 3};
static const float ROUGH[NL] = {0.007f, 0.014f, 0.015f, 0.014f};
static const float FADE[NL] = {0.07f, 0.15f, 0.17f, 0.45f};
static const float LV[NL] = {0.19f, 0.34f, 0.42f, 0.5f}; /* wash strength at the crest */

static const peak_t *peaks(int l)
{
	return l == 0 ? P0 : l == 1 ? P1 : l == 2 ? P2 : P3;
}

static float peak_y(const peak_t *p, float u)
{
	float d = u - p->c, w = d < 0 ? p->wl : p->wr;
	float t = fabsf(d) / w;
	float h = p->foot - p->top;
	if (t < 1.0f) /* |t|^1.4: crowns come to a point instead of a dome */
		return p->foot - h * powf(1.0f - powf(t, 1.4f + 0.6f * (p->pw > 0.35f)), p->pw);
	return p->foot + (t - 1.0f) * w * 2.5f;
}

static void build_ridges(st_t *s, float jit)
{
	for (int l = 0; l < NL; l++) {
		const peak_t *P = peaks(l);
		for (int i = 0; i < RN; i++) {
			float u = i / (float)(RN - 1);
			float y = 1.6f, top = 1.0f;
			for (int k = 0; k < NPK[l]; k++) {
				float py = peak_y(&P[k], u);
				if (py < y) {
					y = py;
					top = P[k].top;
				}
			}
			/* crags: ridged noise, stronger high up and on tall peaks */
			float hgt = us_sat((0.85f - y) / 0.6f);
			float rn = 1.0f - fabsf(us_fbm2(u * (16.0f + 5 * l), l * 7.3f + jit, 3));
			y -= ROUGH[l] * (0.5f + 2.0f * hgt) * rn * rn;
			/* shoulders and ledges down the flanks: a slow ridged wave, so no peak is a smooth bullet */
			float sh = 1.0f - fabsf(us_noise2(u * (7.0f + 2 * l), l * 5.9f + jit + 2.0f));
			y -= (l == 0 ? 0.012f : 0.034f) * sh * sh * sh * (0.4f + hgt);
			y += 0.004f * us_noise2(u * 70.0f, l * 3.1f + jit);
			(void)top;
			if (l == 2) /* the notch the waterfall pours through */
				y += 0.016f * expf(-(u - U_FALL) * (u - U_FALL) / (0.008f * 0.008f));
			s->ridge[l][i] = y;
		}
	}
	for (int l = 0; l < NL; l++)
		for (int i = 0; i < RN; i++) {
			float o = 2.0f;
			for (int k = l + 1; k < NL; k++)
				if (s->ridge[k][i] < o)
					o = s->ridge[k][i];
			s->occ[l][i] = o;
		}
}

static void build_folds(st_t *s)
{
	s->nfold = 0;
	for (int l = 1; l < NL; l++) {
		const peak_t *P = peaks(l);
		for (int k = 0; k < NPK[l]; k++) {
			const peak_t *pk = &P[k];
			float h = pk->foot - pk->top;
			if (h < 0.1f)
				continue;
			for (int f = 0; f < 3 && s->nfold < MAX_FOLD; f++) {
				peak_t q = *pk;
				float side = (f == 1) ? 1.0f : -1.0f;
				float sc = f == 2 ? 0.28f : 0.55f;
				q.c = pk->c + side * (f == 2 ? 0.45f : 0.3f) * (side > 0 ? pk->wr : pk->wl);
				q.wl = pk->wl * sc;
				q.wr = pk->wr * sc;
				q.top = pk->top + h * (f == 2 ? 0.3f : 0.14f + 0.06f * f);
				q.pw = pk->pw * 1.1f;
				int fi = s->nfold++;
				s->fold_l[fi] = l;
				for (int i = 0; i < FOLD_N; i++) {
					float u = q.c - q.wl + (q.wl + q.wr) * i / (FOLD_N - 1);
					float y = peak_y(&q, u) + 0.004f * us_noise2(u * 60.0f, k * 3.0f + f);
					float dy = y - ridge_at(s, l, u);
					int vis = u > 0 && u < 1 && dy > 0.008f && y < occ_at(s, l, u) - 0.003f && dy < FADE[l] * 1.4f;
					s->fold_u[fi][i] = u;
					s->fold_y[fi][i] = y;
					s->fold_k[fi][i] = vis ? expf(-dy / (FADE[l] * 0.9f)) : 0.0f;
				}
			}
		}
	}
}

/* river centreline: a winding path from the plunge pool toward the viewer */
static void build_river(st_t *s)
{
	static const float K[][3] = {{U_FALL, Y_POOL, 0.006f}, {0.635f, 0.765f, 0.009f}, {0.53f, 0.79f, 0.014f},
				     {0.45f, 0.83f, 0.022f},   {0.50f, 0.89f, 0.036f},  {0.60f, 0.95f, 0.055f},
				     {0.58f, 1.06f, 0.08f}};
	int nk = 7;
	for (int i = 0; i < NRIV; i++) {
		float f = i / (float)(NRIV - 1) * (nk - 1);
		int k = (int)f;
		if (k >= nk - 1)
			k = nk - 2;
		float t = f - k;
		/* Catmull-Rom */
		int k0 = k > 0 ? k - 1 : 0, k3 = k + 2 < nk ? k + 2 : nk - 1;
		for (int d = 0; d < 3; d++) {
			float p0 = K[k0][d], p1 = K[k][d], p2 = K[k + 1][d], p3 = K[k3][d];
			s->riv[i * 3 + d] = 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t +
						    (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
		}
	}
}

static void river_at(const st_t *s, float t, float *u, float *y, float *hw)
{
	float f = us_sat(t) * (NRIV - 1);
	int i = (int)f;
	if (i >= NRIV - 1)
		i = NRIV - 2;
	float k = f - i;
	*u = us_lerp(s->riv[i * 3], s->riv[i * 3 + 3], k);
	*y = us_lerp(s->riv[i * 3 + 1], s->riv[i * 3 + 4], k);
	*hw = us_lerp(s->riv[i * 3 + 2], s->riv[i * 3 + 5], k);
}

/* the static wash: which range shows where, how strongly, textured with hemp-fibre streaks */
static void build_grid(st_t *s, int gw, int gh, float A, int crisp)
{
	s->gw = gw;
	s->gh = gh;
	s->gA = A;
	for (int gx = 0; gx < gw; gx++) {
		float u = (gx + 0.5f) / gw, x = u * A;
		float r[NL];
		for (int l = 0; l < NL; l++)
			r[l] = ridge_at(s, l, u);
		/* river: nearest centreline distance per row is found below */
		for (int gy = 0; gy < gh; gy++) {
			float y = (gy + 0.5f) / gh;
			int i = gy * gw + gx;
			int L = -1;
			for (int l = NL - 1; l >= 0; l--)
				if (y >= r[l]) {
					L = l;
					break;
				}
			s->water[i] = 0;
			s->snow[i] = 0;
			if (L < 0) {
				s->lay[i] = 255;
				s->st[i] = 0;
				continue;
			}
			float dy = y - r[L];
			/* streaks run down the flanks: low frequency vertically, high across */
			float tex = 0.62f + 0.55f * us_fbm2(x * 34.0f + L * 13.0f, y * 5.5f + L * 3.0f, 3);
			tex += 0.25f * us_noise2(x * 9.0f + L, y * 14.0f);
			tex = us_clampf(tex, 0.15f, 1.3f);
			float prof = expf(-dy / FADE[L]) * (0.62f + 0.38f * expf(-dy / 0.018f));
			/* faces turned away from the light (to the right of each crest) hold more ink */
			float sl = ridge_at(s, L, u + 0.004f) - ridge_at(s, L, u - 0.004f);
			prof *= 0.85f + 0.3f * us_clampf(sl * 40.0f, -1.0f, 1.0f);
			if (crisp) /* pixel: flatter shapes, the steps do the shading */
				tex = 0.75f + 0.35f * (tex - 0.62f);
			s->st[i] = LV[L] * prof * tex;
			s->lay[i] = (uint8_t)L;
			if (L < 3) {
				float sn = us_smooth(0.035f, 0.004f, dy) * us_smooth(0.45f, 0.75f, tex + 0.15f * us_noise2(x * 60.0f, y * 60.0f));
				s->snow[i] = sn * (L == 0 ? 0.8f : 1.0f);
			}
			/* the waterfall's dark channel down the cliff */
			if (L == 2) {
				float d = fabsf(x - U_FALL * A);
				if (d < 0.012f && y < Y_POOL + 0.01f)
					s->st[i] *= 0.25f + 0.75f * us_smooth(0.004f, 0.012f, d);
			}
		}
	}
	/* river coverage */
	for (int k = 0; k < NRIV * 4; k++) {
		float t = k / (float)(NRIV * 4 - 1), u, y, hw;
		river_at(s, t, &u, &y, &hw);
		int gy = (int)(y * gh);
		if (gy < 0 || gy >= gh)
			continue;
		float x = u * A;
		int gx0 = (int)((x - hw) / A * gw), gx1 = (int)((x + hw) / A * gw);
		for (int gx = gx0; gx <= gx1; gx++) {
			if (gx < 0 || gx >= gw)
				continue;
			float xx = (gx + 0.5f) / gw * A;
			float e = 1.0f - fabsf(xx - x) / (hw + 1e-4f);
			int i = gy * gw + gx;
			int wv = (int)(255 * us_sat(e * 3.0f));
			if (s->lay[i] == 3 && y < ridge_at(s, 3, (gx + 0.5f) / gw) + 0.005f)
				continue;
			if (wv > s->water[i])
				s->water[i] = (uint8_t)wv;
		}
	}
}

/* ------------------------------------------------------------------ pines */

static void build_pine(pine_t *p, us_rng *r, float lean, int big)
{
	p->nseg = 0;
	p->npad = 0;
	/* gnarled trunk: bends left and right, leaning out */
	float x = 0, y = 0, ang = -US_PI / 2 + lean;
	int n = big ? 9 : 6;
	float step = 1.0f / n;
	float tx[12], ty[12];
	tx[0] = x;
	ty[0] = y;
	for (int i = 1; i <= n; i++) {
		ang += us_randr(r, -0.35f, 0.35f) + (big ? 0.06f * lean : 0.0f);
		x += cosf(ang) * step;
		y += sinf(ang) * step;
		tx[i] = x;
		ty[i] = y;
		if (p->nseg < PINE_SEG) {
			float *sg = &p->seg[p->nseg++ * 4];
			sg[0] = tx[i - 1];
			sg[1] = ty[i - 1];
			sg[2] = x;
			sg[3] = y;
		}
	}
	/* branches from the upper trunk, each ending in a flat needle pad */
	for (int i = n / 3; i <= n; i++) {
		if (i < n && us_rand(r) < 0.35f)
			continue;
		float side = (i & 1) ? 1.0f : -1.0f;
		if (big && us_rand(r) < 0.6f)
			side = lean > 0 ? 1.0f : -1.0f;
		float bl = us_randr(r, 0.18f, 0.42f) * (i == n ? 0.5f : 1.0f);
		float bx = tx[i], by = ty[i];
		float a = side > 0 ? us_randr(r, -0.5f, 0.1f) : US_PI + us_randr(r, -0.1f, 0.5f);
		float ex = bx + cosf(a) * bl, ey = by + sinf(a) * bl * 0.6f;
		float mx = (bx + ex) * 0.5f, my = (by + ey) * 0.5f + bl * 0.08f;
		if (p->nseg + 2 <= PINE_SEG) {
			float *sg = &p->seg[p->nseg++ * 4];
			sg[0] = bx; sg[1] = by; sg[2] = mx; sg[3] = my;
			sg = &p->seg[p->nseg++ * 4];
			sg[0] = mx; sg[1] = my; sg[2] = ex; sg[3] = ey;
		}
		if (p->npad < PINE_PAD) {
			float *pd = &p->pad[p->npad++ * 3];
			pd[0] = ex;
			pd[1] = ey;
			pd[2] = us_randr(r, 0.14f, 0.22f) * (big ? 1.0f : 1.15f);
		}
	}
}

/* one flat pine pad: a fan of needle strokes radiating up and out (ink) or stacked discs (pixel) */
static void draw_pad(us_canvas *c, float x, float y, float rr, float v, uint8_t m, int seed, float sw)
{
	if (c->crisp) {
		us_disc(c, x, y - rr * 0.1f, rr * 0.55f, v, m);
		us_disc(c, x - rr * 0.55f, y, rr * 0.38f, v, m);
		us_disc(c, x + rr * 0.55f, y, rr * 0.38f, v, m);
		us_line(c, x - rr * 0.9f, y + rr * 0.25f, x + rr * 0.9f, y + rr * 0.25f, 0.001f, v * 1.25f, m);
		return;
	}
	int n = 13;
	for (int k = 0; k < n; k++) {
		float a = US_PI + US_PI * (k + 0.5f) / n; /* upper half fan */
		float l = rr * (0.75f + 0.35f * hash1(k, (float)seed));
		float ex = x + cosf(a) * l * 1.35f + sw, ey = y + sinf(a) * l * 0.55f;
		wline(c, x, y + rr * 0.12f, ex, ey, 0.0011f, v * (0.75f + 0.25f * hash1(k + 7, (float)seed)), m);
	}
	wline(c, x - rr * 1.3f, y + rr * 0.12f, x + rr * 1.3f, y + rr * 0.12f, 0.0012f, v * 0.9f, m);
}

static void draw_pine(us_canvas *c, const pine_t *p, float A, float v, float shake, float t, float winter, float mistk,
		      int seed)
{
	float rx = p->u * A, ry = p->y, sc = p->sc, fl = p->flip;
	float tw = c->crisp ? 0.0f : 0.0026f * sc / 0.2f;
	for (int i = 0; i < p->nseg; i++) {
		const float *sg = &p->seg[i * 4];
		float w = i < 9 ? tw * (1.0f - 0.07f * i) : tw * 0.45f;
		if (c->crisp)
			us_line(c, rx + fl * sg[0] * sc, ry + sg[1] * sc, rx + fl * sg[2] * sc, ry + sg[3] * sc, i < 4 ? 0.006f : 0.001f,
				v * 0.8f * mistk, US_MAT_STEM);
		else
			wline(c, rx + fl * sg[0] * sc, ry + sg[1] * sc, rx + fl * sg[2] * sc, ry + sg[3] * sc, w < 0.0011f ? 0.0011f : w,
			      v * 0.85f * mistk, US_MAT_STEM);
	}
	for (int i = 0; i < p->npad; i++) {
		const float *pd = &p->pad[i * 3];
		float sw = shake * 0.02f * sinf(t * 31.0f + i * 1.7f) * sc / 0.2f;
		float px = rx + fl * pd[0] * sc + sw, py = ry + pd[1] * sc;
		draw_pad(c, px, py, pd[2] * sc, us_sat(v * mistk * (1.0f + 0.7f * shake)), US_MAT_LEAF, seed * 31 + i, sw * 0.5f);
		if (winter > 0.05f) /* snow resting on the pads */
			stroke(c, px - pd[2] * sc * 0.9f, py - pd[2] * sc * 0.18f, px + pd[2] * sc * 0.9f, py - pd[2] * sc * 0.18f, 0.0016f,
			       winter * 0.75f * mistk, US_MAT_ACCENT);
	}
}

/* ------------------------------------------------------------------ cranes */

static void draw_crane_fly(us_canvas *c, float x, float y, float sz, float ph, float dir, float v, float mistk)
{
	v *= mistk;
	if (v < 0.02f)
		return;
	int cr = c->crisp;
	float wi = cr ? 0.001f : 0.0012f;
	/* body: a slim lens along the flight line; neck straight forward, legs trailing straight back */
	float hx = x + dir * sz * 0.3f, hy = y - sz * 0.01f;
	float tx = x - dir * sz * 0.28f, ty = y + sz * 0.01f;
	petal(c, tx, ty, atan2f(hy - ty, hx - tx), sz * 0.58f, sz * 0.07f, v, US_MAT_ACCENT);
	float nx = hx + dir * sz * 0.42f, ny = hy - sz * 0.02f;
	stroke(c, hx, hy, nx, ny, wi, v, US_MAT_ACCENT);
	stroke(c, nx, ny, nx + dir * sz * 0.1f, ny + sz * 0.015f, wi, v * 0.8f, US_MAT_ACCENT); /* bill */
	us_point(c, nx, ny - sz * 0.005f, cr ? 0.003f : 0.0019f, v, US_MAT_BLOOM);               /* red crown */
	stroke(c, tx, ty, tx - dir * sz * 0.5f, ty + sz * 0.05f, wi, v * 0.7f, US_MAT_STEM);     /* legs */
	/* wings, seen three-quarter: the near wing sweeps from high over the back down through the beat, the far
	 * wing shows below/behind; gliding (mid-beat) they spread as a shallow V. Black primaries at the tips. */
	float sx = x + dir * sz * 0.05f, sy = y - sz * 0.01f;
	float fl = sinf(ph);
	for (int k = 1; k >= 0; k--) {
		float span = sz * (k ? 0.7f : 1.0f);
		/* angle measured from "straight back" (0) upward (+) */
		float th = k ? -0.15f - 0.35f * fl : 0.55f + 0.75f * fl;
		float bx = -dir * cosf(th), by = -sinf(th);
		float a = atan2f(by, bx);
		float wv = v * (k ? 0.6f : 1.0f);
		petal(c, sx, sy, a, span * 0.7f, sz * 0.12f, wv, US_MAT_ACCENT);
		float mx = sx + bx * span * 0.64f, my = sy + by * span * 0.64f;
		petal(c, mx, my, a, span * 0.38f, sz * 0.075f, wv * (cr ? 0.55f : 0.6f), US_MAT_ACCENT);
	}
}

static void draw_crane_stand(us_canvas *c, float x, float y, float sz, float ph, float dir, float v, float mistk, float bow)
{
	v *= mistk;
	if (v < 0.02f)
		return;
	int cr = c->crisp;
	float wi = cr ? 0.001f : 0.0012f;
	/* legs */
	float by = y - sz * 0.5f;
	stroke(c, x - sz * 0.05f, y, x - sz * 0.02f, by, wi, v * 0.6f, US_MAT_STEM);
	stroke(c, x + sz * 0.06f, y, x + sz * 0.02f, by, wi, v * 0.6f, US_MAT_STEM);
	/* body: an egg leaning forward, the black tail bustle behind */
	float bx = x, bcy = by - sz * 0.12f;
	if (cr) {
		us_disc(c, bx, bcy, sz * 0.14f, v, US_MAT_ACCENT);
		us_disc(c, bx - dir * sz * 0.12f, bcy + sz * 0.02f, sz * 0.09f, v * 0.85f, US_MAT_ACCENT);
	} else {
		float pts[14];
		for (int k = 0; k < 7; k++) {
			float a = US_TAU * k / 6.0f;
			pts[2 * k] = bx + dir * cosf(a) * sz * 0.2f;
			pts[2 * k + 1] = bcy + sinf(a) * sz * 0.1f - dir * cosf(a) * sz * 0.03f * dir;
		}
		wpoly(c, pts, 7, 0.0013f, v, US_MAT_ACCENT);
		wline(c, bx - dir * sz * 0.18f, bcy, bx - dir * sz * 0.3f, bcy + sz * 0.05f, 0.0014f, v * 0.5f, US_MAT_STEM);
	}
	/* neck: an S up to the head; bows to preen */
	float nx0 = bx + dir * sz * 0.16f, ny0 = bcy - sz * 0.04f;
	float hx = nx0 + dir * sz * (0.08f + 0.18f * bow), hy = ny0 - sz * (0.42f - 0.32f * bow);
	float mx = nx0 + dir * sz * 0.02f, my = (ny0 + hy) * 0.5f;
	stroke(c, nx0, ny0, mx, my, wi, v, US_MAT_ACCENT);
	stroke(c, mx, my, hx, hy, wi, v, US_MAT_ACCENT);
	stroke(c, hx, hy, hx + dir * sz * 0.12f, hy + sz * 0.02f + sz * 0.06f * bow, wi, v * 0.8f, US_MAT_ACCENT);
	us_point(c, hx, hy - sz * 0.01f, cr ? 0.003f : 0.0018f, v, US_MAT_BLOOM);
	(void)ph;
}

/* ------------------------------------------------------------------ particles */

static part_t *spawn(st_t *s)
{
	part_t *p = &s->pt[s->pt_i];
	s->pt_i = (s->pt_i + 1) % MAX_PART;
	memset(p, 0, sizeof(*p));
	return p;
}

static void puff(st_t *s, float x, float y, float r, float amp)
{
	puff_t *p = &s->pf[s->pf_i];
	s->pf_i = (s->pf_i + 1) % MAX_PUFF;
	p->x = x;
	p->y = y;
	p->r = r;
	p->life = 1.0f;
	p->amp = amp;
}

/* ------------------------------------------------------------------ create */

static void place_crane_stand(st_t *s, crane_t *k)
{
	us_rng *r = &s->rng;
	float t = us_randr(r, 0.42f, 0.62f), u, y, hw;
	river_at(s, t, &u, &y, &hw);
	float side = us_rand(r) < 0.6f ? -1.0f : 1.0f;
	k->x = u + side * hw * us_randr(r, 0.4f, 0.85f) / 1.78f;
	k->y = y + us_randr(r, -0.004f, 0.006f);
}

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(st_t));
	if (!s)
		return NULL;
	us_rng_seed(&s->rng, seed ^ 0x5A11E7u);
	us_rng *r = &s->rng;
	float jit = (seed % 97) * 0.37f;
	build_ridges(s, jit);
	build_folds(s);
	build_river(s);

	/* texture strokes down the flanks of the three farther-but-one ranges */
	s->ncun = 0;
	for (int i = 0; i < MAX_CUN; i++) {
		cun_t *k = &s->cun[s->ncun];
		int l = 1 + (int)(us_rand(r) * 3) % 3;
		k->l = l;
		k->u = us_rand(r);
		if (l == 3 && ridge_at(s, 3, k->u) > 0.9f) { /* the near ground: only the cliffs carry strokes */
			continue;
		}
		k->len = us_randr(r, 0.025f, 0.07f) * (l == 1 ? 0.8f : 1.0f);
		k->bend = us_randr(r, -0.4f, 0.4f);
		k->v = us_randr(r, 0.5f, 1.0f);
		s->ncun++;
	}
	/* moss dots and far pines on the crests */
	s->ndot = 0;
	for (int i = 0; i < MAX_DOT; i++) {
		dot_t *d = &s->dot[s->ndot];
		d->l = 1 + (int)(us_rand(r) * 3) % 3;
		d->u = us_rand(r);
		if (d->l == 3 && ridge_at(s, 3, d->u) > 0.9f)
			continue;
		d->dy = us_randr(r, 0.001f, 0.012f) * (us_rand(r) < 0.3f ? 3.0f : 1.0f);
		d->r = us_randr(r, 0.0012f, 0.0026f) * (d->l == 1 ? 0.8f : 1.0f);
		s->ndot++;
	}
	s->nfp = 0;
	for (int i = 0; i < MAX_FARPINE; i++) {
		fpine_t *f = &s->fp[s->nfp];
		f->l = us_rand(r) < 0.55f ? 1 : 2;
		f->u = us_rand(r);
		f->h = us_randr(r, 0.011f, 0.02f) * (f->l == 1 ? 0.8f : 1.1f);
		/* only on high ground (tops and shoulders) */
		if (ridge_at(s, f->l, f->u) > (f->l == 1 ? 0.5f : 0.62f))
			continue;
		if (f->l == 2 && fabsf(f->u - U_PAV) < 0.02f)
			continue;
		s->nfp++;
	}
	/* pines: the old pine on the near cliff, two by the pavilion, one at the fall's lip */
	build_pine(&s->pine[0], r, 0.75f, 1);
	s->pine[0].u = 0.105f;
	s->pine[0].y = ridge_at(s, 3, 0.105f) + 0.012f;
	s->pine[0].sc = 0.2f;
	s->pine[0].flip = 1.0f;
	build_pine(&s->pine[1], r, -0.15f, 0);
	s->pine[1].u = U_PAV - 0.024f;
	s->pine[1].y = ridge_at(s, 2, U_PAV - 0.024f) + 0.006f;
	s->pine[1].sc = 0.075f;
	s->pine[1].flip = -1.0f;
	build_pine(&s->pine[2], r, 0.3f, 0);
	s->pine[2].u = U_FALL - 0.018f;
	s->pine[2].y = ridge_at(s, 2, U_FALL - 0.018f) + 0.004f;
	s->pine[2].sc = 0.06f;
	s->pine[2].flip = -1.0f;
	build_pine(&s->pine[3], r, -0.4f, 0);
	s->pine[3].u = 0.19f;
	s->pine[3].y = ridge_at(s, 3, 0.19f) + 0.01f;
	s->pine[3].sc = 0.09f;
	s->pine[3].flip = 1.0f;

	/* bamboo on the right bank */
	s->nbb = 0;
	for (int i = 0; i < MAX_BAMBOO; i++) {
		bamboo_t *b = &s->bb[s->nbb++];
		b->u = 0.83f + 0.16f * (i + us_randr(r, -0.3f, 0.3f)) / MAX_BAMBOO;
		b->h = us_randr(r, 0.26f, 0.48f) * (i & 1 ? 0.85f : 1.0f);
		b->lean = us_randr(r, -0.12f, 0.06f);
		b->ph = us_rand(r) * US_TAU;
		b->nodes = 6 + (int)(us_rand(r) * 4);
	}
	/* river ripple marks, each tied to a band */
	for (int i = 0; i < MAX_RIP; i++) {
		rip_t *p = &s->rp[i];
		p->t = us_rand(r);
		p->s = us_randr(r, -0.75f, 0.75f);
		p->len = us_randr(r, 0.3f, 0.7f);
		p->ph = us_rand(r);
		p->band = 0;
	}
	for (int i = 0; i < MAX_STAR; i++) {
		s->star[3 * i] = us_rand(r);
		s->star[3 * i + 1] = us_randr(r, 0.01f, 0.45f);
		s->star[3 * i + 2] = us_randr(r, 0.2f, 1.0f);
	}
	/* cranes: some standing in the shallows, the rest off-screen */
	for (int i = 0; i < MAX_CRANE; i++) {
		crane_t *k = &s->cr[i];
		k->state = 0;
		k->ph = us_rand(r) * US_TAU;
		k->sc = us_randr(r, 0.9f, 1.1f);
	}
	s->land_t = 4.0f;
	s->cross_t = 6.0f;
	return s;
}

static void destroy(void *p) { free(p); }

/* one wash row expanded (linearly) to canvas width into cache slot `slot` */
static void expand_row(st_t *s, int g, int slot, int gw, int W)
{
	const float *rv = s->fv + g * gw, *rq = s->fq + g * gw;
	const uint8_t *rm = s->fm + g * gw;
	float *ov = s->hv[slot], *oq = s->hq[slot];
	uint8_t *om = s->hm[slot];
	for (int px = 0; px < W; px++) {
		float tx = s->ctx_[px];
		int a0 = s->cx0[px], a1 = s->cx1[px];
		ov[px] = 255.0f * (rv[a0] + (rv[a1] - rv[a0]) * tx);
		oq[px] = 255.0f * (rq[a0] + (rq[a1] - rq[a0]) * tx);
		om[px] = rm[tx < 0.5f ? a0 : a1];
	}
	s->hrow[slot] = g;
}

/* ------------------------------------------------------------------ frame */

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *s = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	us_rng *r = &s->rng;
	float dt = ctx->dt, rdt = ctx->real_dt, A = ctx->aspect;
	float dens = ctx->p->density;
	float t = (float)ctx->t;
	int crisp = ctx->crisp;
	float spring = us_world_season_w(w, 0), summer = us_world_season_w(w, 1);
	float autumn = us_world_season_w(w, 2), winter = us_world_season_w(w, 3);
	float day = w->daylight;
	float dayk = us_smooth(0.05f, 0.6f, day);
	float kick = us_sat(a->kick), snare = us_sat(a->snare), hat = us_sat(a->hat);

	/* ---- envelopes ---- */
	s->lvl = us_approach(s->lvl, us_sat(a->level), a->level > s->lvl ? 0.15f : 0.6f, rdt);
	s->warm = us_approach(s->warm, us_sat(0.6f * a->swell + 0.6f * s->lvl), 1.5f, rdt);
	s->sub_env = us_approach(s->sub_env, a->sub, a->sub > s->sub_env ? 0.06f : 0.4f, rdt);
	s->lm_env = us_approach(s->lm_env, a->lowmid, 0.6f, rdt);
	s->air_env = us_approach(s->air_env, a->air, 0.3f, rdt);
	s->drift = us_approach(s->drift, 0.6f * w->wind + 0.8f * a->width, 3.0f, rdt);
	for (int i = 0; i < US_NBANDS; i++) {
		float bv = a->bands[i];
		s->bandv[i] = us_approach(s->bandv[i], bv, bv > s->bandv[i] ? 0.05f : 0.3f, rdt);
	}
	s->kick_cool -= rdt;
	s->snare_cool -= rdt;
	/* kick: the fall surges, mist billows from its foot, the bamboo is shoved */
	s->surge = us_approach(s->surge, 0.0f, 0.28f, rdt);
	s->flash = us_approach(s->flash, 0.0f, 0.12f, rdt);
	s->kage += rdt;
	if (a->kick_hit && s->kick_cool <= 0) {
		s->surge = us_sat(s->surge + 0.55f + 0.45f * kick);
		s->billow = us_sat(0.4f * s->billow * expf(-s->kage / 0.22f) + 0.7f + 0.3f * kick);
		s->kage = 0.0f;
		s->flash = 1.0f;
		s->bvel += 2.2f * (0.6f + 0.6f * kick);
		puff(s, U_FALL + us_randr(r, -0.01f, 0.01f), Y_POOL - 0.012f, 0.04f, 0.7f + 0.4f * kick);
		for (int k = 0; k < 22; k++) { /* spray thrown up and out */
			part_t *p = spawn(s);
			p->kind = 1;
			p->x = U_FALL * A + us_randr(r, -0.01f, 0.01f);
			p->y = Y_POOL - 0.004f;
			p->vx = us_randr(r, -0.2f, 0.2f);
			p->vy = us_randr(r, -0.24f, -0.06f);
			p->life = us_randr(r, 0.5f, 1.0f);
			p->mat = US_MAT_WATER;
		}
		s->kick_cool = 0.1f;
		if (a->sub > 0.5f) {
			s->ring_i = (s->ring_i + 1) % 3;
			s->ring_t[s->ring_i] = 0.0f;
		}
	}
	for (int i = 0; i < 3; i++)
		s->ring_t[i] += rdt;
	/* bamboo spring: wind lean + kick shove */
	{
		float target = 0.05f * w->wind + 0.03f * sinf(t * 0.6f);
		float acc = -45.0f * (s->bsw - target) - 8.0f * s->bvel;
		s->bvel += acc * rdt;
		s->bsw += s->bvel * rdt;
		s->bsw = us_clampf(s->bsw, -0.5f, 0.5f);
	}
	/* snare: cranes lift off, the pine shakes needles loose */
	s->shake = us_approach(s->shake, 0.0f, 0.15f, rdt);
	if (a->snare_hit && s->snare_cool <= 0) {
		s->shake = 1.0f;
		s->snare_cool = 0.12f;
		int nn = 56 + (int)(24 * snare);
		for (int k = 0; k < nn; k++) {
			const pine_t *pn = &s->pine[k % 5 == 4 ? 1 + (k / 5) % 3 : 0];
			int pi = (int)(us_rand(r) * pn->npad) % (pn->npad > 0 ? pn->npad : 1);
			const float *pd = &pn->pad[pi * 3];
			part_t *p = spawn(s);
			p->kind = 0;
			p->x = pn->u * A + pn->flip * pd[0] * pn->sc + us_randr(r, -1, 1) * pd[2] * pn->sc;
			p->y = pn->y + pd[1] * pn->sc + us_randr(r, -0.3f, 0.2f) * pd[2] * pn->sc;
			float ba = us_rand(r) * US_TAU, bs = us_randr(r, 0.12f, 0.4f);
			p->vx = cosf(ba) * bs + 0.03f;
			p->vy = sinf(ba) * bs * 0.7f - 0.02f;
			p->life = us_randr(r, 0.4f, 0.9f);
			p->ang = us_rand(r) * US_TAU;
			p->spin = us_randr(r, -6, 6);
			p->len = us_randr(r, 0.006f, 0.011f);
			p->mat = autumn > 0.5f ? US_MAT_BLOOM : US_MAT_LEAF;
		}
		/* launch one or two standing cranes; if none stand, the flyers beat hard */
		int launched = 0;
		for (int i = 0; i < MAX_CRANE && launched < 2; i++) {
			crane_t *k = &s->cr[i];
			if (k->state == 1) {
				k->state = 2;
				k->dir = k->x * A < 0.9f ? -1.0f : 1.0f;
				if (us_rand(r) < 0.35f)
					k->dir = -k->dir;
				k->vx = k->dir * us_randr(r, 0.05f, 0.08f);
				k->vy = -us_randr(r, 0.06f, 0.1f);
				k->rate = 9.0f;
				k->kick = 1.0f;
				launched++;
			}
		}
		for (int i = 0; i < MAX_CRANE; i++)
			if (s->cr[i].state >= 2)
				s->cr[i].kick = 1.0f;
		{ /* and a pair rises out of the mist on the valley floor */
			for (int i = 0, n = 0; i < MAX_CRANE && n < (launched ? 2 : 3); i++) {
				crane_t *k = &s->cr[i];
				if (k->state)
					continue;
				k->state = 2;
				k->x = us_randr(r, 0.3f, 0.66f);
				k->y = us_randr(r, 0.64f, 0.74f);
				k->dir = us_rand(r) < 0.5f ? -1.0f : 1.0f;
				k->vx = k->dir * us_randr(r, 0.05f, 0.08f);
				k->vy = -us_randr(r, 0.05f, 0.08f);
				k->rate = 9.0f;
				k->kick = 1.0f;
				k->ph = 0;
				n++;
			}
		}
	}
	/* hats: glints at the fall's foot and dew on the bamboo */
	s->glint = us_approach(s->glint, 0.0f, 0.07f, rdt);
	if (a->hat_hit) {
		s->hat_seed = s->hat_seed * 1664525u + 1013904223u;
		s->glint = 1.0f;
	}
	if (s->glint > 0.3f)
		s->hat_seed = s->hat_seed * 1664525u + 1013904223u;
	s->hat_seedf = (float)(s->hat_seed & 0xffff);

	/* ---- light ---- */
	float G = (0.76f + 0.44f * s->lvl) * (0.84f + 0.16f * dayk);
	float lg[NL];
	lg[0] = G * (0.55f + 0.75f * s->lvl + 0.25f * s->warm);
	lg[1] = G * (0.85f + 0.2f * s->lvl);
	lg[2] = G;
	lg[3] = G * 1.02f;

	/* ---- mist: bands that drift and breathe; loudness thins and lifts them ---- */
	s->mistT += dt * (0.025f + 0.05f * s->drift);
	/* the kick's mist wave: races out along the valley floor, then fades */
	float bamp = s->billow * expf(-s->kage / 0.22f) * us_smooth(0.0f, 0.04f, s->kage + 0.01f);
	float brad = 0.12f + 0.6f * (1.0f - expf(-s->kage / 0.1f));
	{
		float md = (1.05f + 0.6f * w->moisture) * (1.0f - (crisp ? 0.15f : 0.38f) * s->lvl) * (1.0f - 0.2f * summer);
		float lift = 0.035f * a->swell + 0.02f * s->lvl;
		static const float BY[4] = {0.31f, 0.545f, 0.675f, 0.80f}, BH[4] = {0.028f, 0.045f, 0.035f, 0.05f},
				   BD[4] = {0.55f, 0.95f, 1.0f, 0.55f}, BS[4] = {0.8f, 1.0f, 1.2f, 0.7f};
		float br[4];
		for (int b = 0; b < 4; b++)
			br[b] = 0.86f + 0.14f * sinf(t * (US_TAU / 11.0f) + b * 1.9f);
		/* each band wanders in height, swells and thins, and breaks into separate banks along its length */
		float byc[4][MW], bth[4][MW], bam[4][MW];
		for (int b = 0; b < 4; b++)
			for (int i = 0; i < MW; i++) {
				float x = (i + 0.5f) / MW * A;
				byc[b][i] = BY[b] - lift * (b == 3 ? 0.5f : 1.0f) + 0.035f * us_noise2(x * 0.8f + s->mistT * 0.25f, b * 4.0f);
				bth[b][i] = BH[b] * (0.55f + 0.9f * us_smooth(-0.5f, 0.7f, us_noise2(x * 1.3f - s->mistT * 0.4f, b * 7.0f + 1.0f)));
				bam[b][i] = us_smooth(-0.5f, 0.3f, us_noise2(x * 0.9f - s->mistT * BS[b] * 0.6f, b * 9.0f + 2.0f));
			}
		int mstep = crisp ? 2 : 1; /* pixel canvases: every other column, filled in below */
		for (int j = 0; j < MH; j++) {
			float y = (j + 0.5f) / MH;
			for (int i = 0; i < MW; i += mstep) {
				float x = (i + 0.5f) / MW * A;
				float m = 0;
				for (int b = 0; b < 4; b++) {
					float d = (y - byc[b][i]) / bth[b][i];
					if (d * d > 9.0f || bam[b][i] < 0.01f)
						continue;
					float wsp = us_fbm2(x * 1.5f - s->mistT * BS[b] * 2.0f, y * (crisp ? 6.0f : 13.0f) + b * 5.1f + s->mistT * 0.2f, 2);
					float sh = us_smooth(-0.4f, 0.6f, wsp);
					m += BD[b] * br[b] * bam[b][i] * expf(-d * d) * (0.25f + 0.85f * sh);
				}
				m *= md;
				/* kick: the fall's thunder pushes a wave of mist out along the valley floor */
				if (bamp > 0.01f) {
					float dx = (x - U_FALL * A) / brad;
					float dy = (y - (Y_POOL + 0.005f)) / 0.04f;
					m += 0.75f * bamp * expf(-dx * dx - dy * dy) * (0.5f + 0.6f * us_smooth(-0.3f, 0.5f, us_noise2(x * 4.0f, y * 12.0f + t)));
				}
				s->mist[j * MW + i] = m;
			}
		}
		if (mstep == 2)
			for (int j = 0; j < MH; j++)
				for (int i = 1; i < MW; i += 2)
					s->mist[j * MW + i] = i + 1 < MW ? 0.5f * (s->mist[j * MW + i - 1] + s->mist[j * MW + i + 1]) : s->mist[j * MW + i - 1];
		/* billows from the waterfall's foot */
		for (int k = 0; k < MAX_PUFF; k++) {
			puff_t *p = &s->pf[k];
			if (p->life <= 0)
				continue;
			p->life -= rdt / 2.2f;
			p->r += rdt * (0.02f + 0.12f * p->life * p->life * p->life); /* bursts out, then slows */
			p->y -= rdt * 0.008f;
			p->x += rdt * 0.004f * (0.5f + s->drift);
			float px = p->x * A, R = p->r;
			int i0 = (int)((px - 3 * R) / A * MW), i1 = (int)((px + 3 * R) / A * MW) + 1;
			int j0 = (int)((p->y - 1.4f * R) * MH), j1 = (int)((p->y + 1.4f * R) * MH) + 1;
			float amp = p->amp * us_sat(p->life) * us_smooth(1.0f, 0.97f, p->life);
			for (int j = j0 < 0 ? 0 : j0; j <= j1 && j < MH; j++)
				for (int i = i0 < 0 ? 0 : i0; i <= i1 && i < MW; i++) {
					float dx = ((i + 0.5f) / MW * A - px) / (2.2f * R), dy = ((j + 0.5f) / MH - p->y) / (0.8f * R);
					s->mist[j * MW + i] += amp * expf(-(dx * dx + dy * dy));
				}
		}
		/* the fall's own constant haze at its foot */
		{
			float px = U_FALL * A;
			float amp = 0.1f + 0.1f * (0.5f + w->water_level) * (1.0f - 0.6f * winter);
			for (int j = (int)((Y_POOL - 0.06f) * MH); j <= (int)((Y_POOL + 0.03f) * MH) && j < MH; j++)
				for (int i = (int)((px - 0.07f) / A * MW); i <= (int)((px + 0.07f) / A * MW) + 1 && i < MW; i++) {
					if (i < 0 || j < 0)
						continue;
					float dx = ((i + 0.5f) / MW * A - px) / 0.06f, dy = ((j + 0.5f) / MH - Y_POOL + 0.012f) / 0.018f;
					s->mist[j * MW + i] += amp * expf(-(dx * dx + dy * dy)) * (0.8f + 0.2f * sinf(t * 1.3f + i));
				}
		}
		for (int i = 0; i < MW * MH; i++)
			s->mist[i] = 1.0f - expf(-1.4f * s->mist[i]); /* soft-saturate: no flat plateaus */
	}

	/* ---- the wash grid ---- */
	int gw, gh;
	if (crisp) {
		gw = c->w < GWM ? c->w : GWM;
		gh = c->h < GHM ? c->h : GHM;
	} else {
		gw = (c->w + 2) / 3;
		gh = (c->h + 2) / 3;
		if (gw > GWM) gw = GWM;
		if (gh > GHM) gh = GHM;
	}
	if (gw < 2) gw = 2;
	if (gh < 2) gh = 2;
	if (gw != s->gw || gh != s->gh || fabsf(A - s->gA) > 1e-4f)
		build_grid(s, gw, gh, A, crisp);
	{
		/* pixel: the mist stays, lit by the loudness (its quantised bands would otherwise darken a loud frame) */
		float mv = (crisp ? 0.14f : 0.2f) + (crisp ? 0.08f : 0.06f) * dayk + (crisp ? 0.22f : 0.03f) * s->lvl;
		float wv = crisp ? 0.30f + 0.08f * dayk : 0.07f + 0.03f * dayk;
		float snowk = winter * (crisp ? 0.75f : 0.5f) * (0.8f + 0.3f * s->lvl);
		float skyv[GHM];
		for (int gy = 0; gy < gh; gy++) {
			float y = (gy + 0.5f) / gh;
			skyv[gy] = crisp ? (0.22f + 0.4f * y) * dayk : 0.0f;
			if (skyv[gy] < 0.12f)
				skyv[gy] = 0.0f;
		}
		/* sample the mist grid per wash cell (bilinear) */
		for (int gy = 0; gy < gh; gy++) {
			float y = (gy + 0.5f) / gh;
			float fy = y * MH - 0.5f;
			int iy = (int)floorf(fy);
			float ty = fy - iy;
			int y0 = iy < 0 ? 0 : iy >= MH ? MH - 1 : iy, y1 = iy + 1 >= MH ? MH - 1 : (iy + 1 < 0 ? 0 : iy + 1);
			float rmax = 0.0f;
			for (int gx = 0; gx < gw; gx++) {
				int i = gy * gw + gx;
				float fx = (gx + 0.5f) / gw * MW - 0.5f;
				int ix = (int)floorf(fx);
				float tx = fx - ix;
				int x0 = ix < 0 ? 0 : ix >= MW ? MW - 1 : ix, x1 = ix + 1 >= MW ? MW - 1 : (ix + 1 < 0 ? 0 : ix + 1);
				float ma = us_lerp(us_lerp(s->mist[y0 * MW + x0], s->mist[y0 * MW + x1], tx),
						   us_lerp(s->mist[y1 * MW + x0], s->mist[y1 * MW + x1], tx), ty);
				int L = s->lay[i];
				float vm = 0, vs = 0;
				uint8_t m = US_MAT_EARTH;
				if (L < NL) {
					vm = s->st[i] * lg[L];
					if (crisp) { /* pixel: firmer masses whose feet dissolve in an ordered dither */
						static const uint8_t BAYL[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
						vm = vm * (1.1f + 0.4f * s->lvl) + ((BAYL[(gy & 3) * 4 + (gx & 3)] + 0.5f) / 16.0f - 0.5f) * 0.12f;
						if (L == 3 && vm < 0.2f)
							vm = 0.2f;
						if (vm < 0)
							vm = 0;
					}
					vs = s->snow[i] * snowk * (L == 0 ? lg[0] / G : 1.0f);
				}
				if (s->water[i]) {
					float wvv = wv * s->water[i] / 255.0f;
					if (wvv > vm) {
						vm = wvv;
						m = US_MAT_WATER;
					}
				}
				if (vs > vm) {
					vm = vs;
					m = US_MAT_ACCENT;
				}
				if (L < NL) /* the mist lies behind the near cliffs, and only partly over the cliff of the fall */
					ma *= L == 3 ? 0.15f : 1.0f;
				float hide = 1.0f - 0.88f * ma;
				float v1 = vm * hide, v2 = ma * mv;
				if (crisp) { /* pixel: resolve the cell here (drawn 1:1); mist edges are ordered-dithered */
					static const uint8_t BAY[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
					float th = (BAY[(gy & 3) * 4 + (gx & 3)] + 0.5f) / 16.0f;
					float v;
					if (ma > 0.14f + 0.32f * th) {
						v = mv * (ma > 0.6f ? 1.0f : 0.7f) + 0.3f * vm;
						m = US_MAT_ACCENT;
					} else
						v = vm;
					if (L == 255 && m != US_MAT_ACCENT && skyv[gy] > v) {
						v = skyv[gy];
						m = US_MAT_WATER;
					}
					v1 = v;
					v2 = 0;
				}
				s->fv[i] = v1;
				s->fq[i] = v2;
				s->fm[i] = m;
				if (v1 + v2 > rmax)
					rmax = v1 + v2;
			}
			s->rowmax[gy] = rmax;
		}
		/* upsample onto the canvas (bilinear in ink, 1:1 / nearest in pixel) */
		int W = c->w < MAXCOL ? c->w : MAXCOL;
		for (int px = 0; px < W; px++) {
			float fx = (px + 0.5f) / c->w * gw - 0.5f;
			int ix = (int)floorf(fx);
			s->ctx_[px] = fx - ix;
			s->cx0[px] = ix < 0 ? 0 : ix >= gw ? gw - 1 : ix;
			s->cx1[px] = ix + 1 < 0 ? 0 : ix + 1 >= gw ? gw - 1 : ix + 1;
		}
		s->hrow[0] = s->hrow[1] = -1;
		for (int py = 0; py < c->h; py++) {
			float fy = (py + 0.5f) / c->h * gh - 0.5f;
			int iy = (int)floorf(fy);
			float ty = fy - iy;
			int y0 = iy < 0 ? 0 : iy >= gh ? gh - 1 : iy, y1 = iy + 1 < 0 ? 0 : iy + 1 >= gh ? gh - 1 : iy + 1;
			if (s->rowmax[y0] < 0.003f && s->rowmax[y1] < 0.003f)
				continue; /* empty sky */
			uint8_t *ink = c->ink + (size_t)py * c->w, *mat = c->mat + (size_t)py * c->w;
			if (crisp) {
				int yy = ty < 0.5f ? y0 : y1;
				const float *r0 = s->fv + yy * gw;
				const uint8_t *m0 = s->fm + yy * gw;
				for (int px = 0; px < W; px++) {
					int nx = s->ctx_[px] < 0.5f ? s->cx0[px] : s->cx1[px];
					int iv = (int)(r0[nx] * 255.0f + 0.5f);
					if (iv > 255) iv = 255;
					if (iv > ink[px]) {
						ink[px] = (uint8_t)iv;
						mat[px] = m0[nx];
					}
				}
				continue;
			}
			/* expand the two wash rows to canvas width once (cached), then blend vertically */
			int sl[2];
			sl[0] = s->hrow[0] == y0 ? 0 : s->hrow[1] == y0 ? 1 : -1;
			sl[1] = s->hrow[0] == y1 ? 0 : s->hrow[1] == y1 ? 1 : -1;
			if (sl[0] < 0) {
				sl[0] = sl[1] == 0 ? 1 : 0;
				expand_row(s, y0, sl[0], gw, W);
			}
			if (sl[1] < 0) {
				sl[1] = sl[0] == 0 ? 1 : 0;
				expand_row(s, y1, sl[1], gw, W);
			}
			const float *restrict v0 = s->hv[sl[0]], *restrict v1 = s->hv[sl[1]], *restrict q0 = s->hq[sl[0]],
				    *restrict q1 = s->hq[sl[1]];
			const uint8_t *mm = s->hm[ty < 0.5f ? sl[0] : sl[1]];
			float ty1 = 1.0f - ty;
			for (int px = 0; px < W; px++) {
				/* land and mist upsampled separately: the material follows the smooth boundary */
				float vl = v0[px] * ty1 + v1[px] * ty;
				float vq = q0[px] * ty1 + q1[px] * ty;
				int iv = (int)(vl + vq + 0.5f);
				if (iv > 255) iv = 255;
				if (iv > ink[px]) {
					ink[px] = (uint8_t)iv;
					mat[px] = vq > vl ? US_MAT_ACCENT : mm[px];
				}
			}
		}
	}

	/* ---- sky: sun or moon, stars ---- */
	{
		float sx = 0.2f * A, sy = 0.15f, sr = 0.034f;
		float mk = 1.0f - 0.85f * mist_at(s, sx, sy, A);
		float sv = (0.45f + 0.35f * s->warm) * mk;
		if (dayk < 0.5f) { /* moon: a pale ring with a soft fill */
			us_disc(c, sx, sy, sr, (crisp ? 0.4f : 0.1f) * mk, US_MAT_LIGHT);
			if (!crisp)
				for (int k = 0; k < 64; k++) {
					float a0 = US_TAU * k / 64, a1 = US_TAU * (k + 1) / 64;
					wline(c, sx + cosf(a0) * sr, sy + sinf(a0) * sr, sx + cosf(a1) * sr, sy + sinf(a1) * sr, 0.0012f, sv, US_MAT_LIGHT);
				}
			else
				us_ring(c, sx, sy, sr, 0.004f, sv, US_MAT_LIGHT);
		} else { /* sun: a red-gold disc, a halo that warms with loudness */
			us_disc(c, sx, sy, sr * 0.8f, (crisp ? 0.75f : 0.3f) * mk * (0.8f + 0.3f * s->warm), US_MAT_BLOOM);
			if (!crisp)
				for (int k = 0; k < 48; k++) {
					float a0 = US_TAU * k / 48, a1 = US_TAU * (k + 1) / 48, rr = sr * 0.8f;
					wline(c, sx + cosf(a0) * rr, sy + sinf(a0) * rr, sx + cosf(a1) * rr, sy + sinf(a1) * rr, 0.0012f, sv, US_MAT_BLOOM);
				}
		}
		/* halo rings: loudness warms the light */
		float hv = (0.06f + 0.22f * s->warm) * mk;
		for (int k = 1; k <= 3; k++) {
			float rr = sr * (1.0f + 0.45f * k + 0.08f * s->warm);
			if (crisp) {
				if (k == 1 && s->warm > 0.25f)
					us_ring(c, sx, sy, rr, 0.003f, hv * 1.4f, US_MAT_LIGHT);
			} else {
				int n = 72;
				for (int q = 0; q < n; q++) {
					if (((q + k * 5) % 9) < 3) /* broken rings, like brushed haloes */
						continue;
					float a0 = US_TAU * q / n, a1 = US_TAU * (q + 1) / n;
					wline(c, sx + cosf(a0) * rr, sy + sinf(a0) * rr, sx + cosf(a1) * rr, sy + sinf(a1) * rr, 0.001f,
					      hv * (1.0f - 0.25f * k), US_MAT_LIGHT);
				}
			}
		}
		float stv = 1.0f - us_smooth(0.05f, 0.35f, day);
		if (stv > 0.01f) {
			int ns = (int)(MAX_STAR * (0.4f + 0.6f * dens));
			for (int i = 0; i < ns; i++) {
				float x = s->star[3 * i] * A, y = s->star[3 * i + 1];
				float u = s->star[3 * i];
				if (y > ridge_at(s, 0, u) - 0.01f || y > ridge_at(s, 1, u) - 0.01f)
					continue;
				float tw = 0.7f + 0.3f * us_noise2(i * 1.7f, t * 1.5f);
				us_point(c, x, y, 0.0011f, stv * s->star[3 * i + 2] * tw * 0.55f * (1.0f - mist_at(s, x, y, A)), US_MAT_LIGHT);
			}
		}
	}

	/* ---- crest lines, texture strokes, moss dots, far pines ---- */
	{
		static const float CW[NL] = {0.0009f, 0.0012f, 0.0014f, 0.0018f};
		static const float CV[NL] = {0.26f, 0.5f, 0.6f, 0.7f};
		for (int l = 0; l < NL; l++) {
			int step = crisp ? 2 : 1;
			float cv = CV[l] * lg[l] * (crisp ? 1.25f : 1.0f);
			for (int i = 0; i + step < RN; i += step) {
				float u0 = i / (float)(RN - 1), u1 = (i + step) / (float)(RN - 1);
				float y0 = s->ridge[l][i], y1 = s->ridge[l][i + step];
				if (y0 > 0.995f && y1 > 0.995f)
					continue;
				if (y0 > s->occ[l][i] - 0.002f || y1 > s->occ[l][i + step] - 0.002f)
					continue;
				float x0 = u0 * A, x1 = u1 * A;
				float mk = 1.0f - 0.9f * mist_at(s, x0, y0, A);
				/* warm light catches the high crests when it's loud */
				float hi = us_smooth(0.36f, 0.12f, y0);
				float warm = s->warm * hi * (l < 2 ? 1.0f : 0.5f);
				uint8_t m = warm > 0.45f ? US_MAT_LIGHT : US_MAT_EARTH;
				float v = cv * mk * (1.0f + 0.5f * warm);
				if (winter > 0.3f && l < 3 && hi > 0.1f)
					m = US_MAT_ACCENT, v *= 1.1f + 0.4f * winter;
				if (crisp)
					us_line(c, x0, y0, x1, y1, 0.001f, us_sat(v), m);
				else
					wline(c, x0, y0, x1, y1, CW[l], v, m);
			}
		}
		/* inner folds (cached at create): nested contours on each tall face, so it reads as stacked rock */
		for (int f = 0; f < s->nfold; f++) {
			int l = s->fold_l[f];
			const float *fu = s->fold_u[f], *fy = s->fold_y[f], *fk = s->fold_k[f];
			for (int i = 1; i < FOLD_N; i++) {
				if (fk[i] <= 0 || fk[i - 1] <= 0)
					continue;
				float x0 = fu[i - 1] * A, x1 = fu[i] * A;
				float mk = 1.0f - 0.9f * mist_at(s, x1, fy[i], A);
				float v = CV[l] * 0.62f * lg[l] * mk * fk[i];
				if (crisp)
					us_line(c, x0, fy[i - 1], x1, fy[i], 0.001f, us_sat(v * 1.1f), US_MAT_EARTH);
				else
					wline(c, x0, fy[i - 1], x1, fy[i], 0.0011f, v, US_MAT_EARTH);
			}
		}
		if (!crisp) {
			int nc = (int)(s->ncun * (0.4f + 0.6f * dens));
			for (int i = 0; i < nc; i++) {
				const cun_t *k = &s->cun[i];
				float u = k->u, y = ridge_at(s, k->l, u) + 0.003f;
				if (y > occ_at(s, k->l, u) - 0.004f)
					continue;
				float sl = ridge_at(s, k->l, u + 0.006f) - ridge_at(s, k->l, u - 0.006f);
				float dir = sl > 0 ? 1.0f : -1.0f; /* fall away down the slope */
				float steep = us_clampf(fabsf(sl) * 30.0f, 0.1f, 1.0f);
				float L = k->len;
				float pts[8];
				for (int q = 0; q < 4; q++) {
					float f = q / 3.0f;
					pts[2 * q] = u * A - dir * L * (0.15f + 0.45f * (1.0f - steep)) * f + k->bend * L * 0.25f * f * f;
					pts[2 * q + 1] = y + L * f;
				}
				float ey = pts[7], ex = pts[6];
				if (ey > occ_at(s, k->l, ex / A) - 0.002f)
					continue;
				float mk = 1.0f - 0.9f * mist_at(s, pts[2], pts[3], A);
				wpoly(c, pts, 4, 0.0011f, 0.42f * k->v * lg[k->l] * mk * (0.8f + 0.1f * k->l), US_MAT_EARTH);
			}
		}
		int nd = (int)(s->ndot * (0.35f + 0.65f * dens));
		float dotm = autumn > 0.55f ? 0 : 1; /* autumn: some dots turn to maple red */
		for (int i = 0; i < nd; i++) {
			const dot_t *d = &s->dot[i];
			float y = ridge_at(s, d->l, d->u) + d->dy;
			if (y > occ_at(s, d->l, d->u) - 0.003f)
				continue;
			float x = d->u * A;
			float mk = 1.0f - 0.9f * mist_at(s, x, y, A);
			uint8_t m = US_MAT_LEAF;
			if (!dotm && (i % 3) == 0)
				m = US_MAT_BLOOM;
			if (winter > 0.6f && (i % 2) == 0)
				m = US_MAT_ACCENT;
			float v = (0.42f + 0.12f * d->l) * lg[d->l] * mk * (1.0f + 0.2f * summer);
			if (crisp)
				us_point(c, x, y, 0.003f, v * 1.2f, m);
			else
				us_disc(c, x, y, d->r, v, m);
		}
		int nf = (int)(s->nfp * (0.4f + 0.6f * dens));
		for (int i = 0; i < nf; i++) {
			const fpine_t *f = &s->fp[i];
			float x = f->u * A, y = ridge_at(s, f->l, f->u) + 0.003f;
			if (y > occ_at(s, f->l, f->u) - 0.002f)
				continue;
			float mk = 1.0f - 0.9f * mist_at(s, x, y - f->h * 0.5f, A);
			float v = (0.45f + 0.1f * f->l) * lg[f->l] * mk;
			stroke(c, x, y, x + 0.002f, y - f->h, 0.0011f, v, US_MAT_STEM);
			for (int q = 0; q < 3; q++) {
				float py = y - f->h * (0.45f + 0.25f * q), pw = f->h * (0.42f - 0.1f * q);
				stroke(c, x - pw, py + pw * 0.15f, x + pw, py, 0.0011f, v, US_MAT_LEAF);
			}
		}
	}

	/* ---- the waterfall ---- */
	float vol = (0.6f + 0.6f * w->water_level) * (1.0f - 0.55f * winter);
	{
		float xw = U_FALL * A, yt = ridge_at(s, 2, U_FALL) + 0.004f, yb = Y_POOL;
		s->fallT += dt * (0.7f + 0.9f * s->surge);
		int ns = 3 + (int)(3 * vol + 5 * s->surge);
		if (ns > 11) ns = 11;
		float half = 0.002f + 0.0022f * vol + 0.005f * s->surge;
		float fv = (0.65f + 0.25f * s->lvl) * (0.8f + 0.2f * dayk) + 0.4f * s->surge + 0.5f * s->flash;
		for (int k = 0; k < ns; k++) {
			float off = (ns > 1 ? (k / (float)(ns - 1)) * 2.0f - 1.0f : 0.0f) * half;
			float hk = hash1(k, 3.3f);
			float pts[2 * 25];
			int np = 0;
			for (int q = 0; q <= 24; q++) {
				float f = q / 24.0f;
				float y = yt + (yb - yt) * f;
				/* the water arcs out over the lip, then drops straight and spreads at the foot */
				float x = xw + off * (1.0f + 1.6f * f * f) + 0.004f * (1.0f - f) * (1.0f - f) * 0.0f +
					  0.0012f * sinf(f * 18.0f + hk * 6.0f + t * 2.0f);
				pts[2 * np] = x;
				pts[2 * np + 1] = y;
				np++;
			}
			float mk = 1.0f - 0.5f * mist_at(s, xw, (yt + yb) * 0.5f, A);
			if (crisp)
				us_polyline(c, pts, np, 0.001f, us_sat(fv * 0.75f) * mk, US_MAT_WATER);
			else
				wpoly(c, pts, np, 0.0011f, fv * (0.45f + 0.25f * hk) * mk, US_MAT_WATER);
			/* bright dashes running down the strand */
			int nd = 4;
			for (int d = 0; d < nd; d++) {
				float f0 = fmodf(s->fallT * (0.8f + 0.4f * hk) + d / (float)nd + hk, 1.0f);
				float f1 = f0 + 0.06f + 0.06f * s->surge;
				if (f1 > 1.0f)
					continue;
				int q0 = (int)(f0 * 24), q1 = (int)(f1 * 24);
				if (q1 <= q0)
					q1 = q0 + 1;
				if (q1 > 24)
					q1 = 24;
				float dv = us_sat(fv * 1.15f) * mk;
				if (crisp)
					us_polyline(c, &pts[2 * q0], q1 - q0 + 1, 0.001f, dv, US_MAT_WATER);
				else
					wpoly(c, &pts[2 * q0], q1 - q0 + 1, 0.0013f, dv, US_MAT_WATER);
			}
		}
		/* kick: a bright slug of water races down the fall */
		if (s->kage < 0.3f) {
			float f0 = us_sat(s->kage / 0.18f), f1 = us_sat(f0 + 0.25f);
			float ya = yt + (yb - yt) * f0, yb2 = yt + (yb - yt) * f1;
			float sv = us_sat(1.2f * (1.0f - s->kage / 0.3f)) * (0.6f + 0.4f * s->billow);
			for (int k = -2; k <= 2; k++) {
				float xo = xw + k * half * 0.5f;
				stroke(c, xo, ya, xo + k * half * 0.3f, yb2, crisp ? 0.001f : 0.0016f, sv, US_MAT_WATER);
			}
		}
		/* winter: icicles hang at the lip */
		if (winter > 0.2f)
			for (int k = 0; k < 7; k++) {
				float x = xw + (k - 3) * 0.004f;
				float l = 0.012f + 0.02f * hash1(k, 9.0f);
				stroke(c, x, yt, x, yt + l * winter, 0.0011f, 0.55f * winter, US_MAT_ACCENT);
			}
		/* plunge pool: an ellipse of ripples; sub thumps send rings out */
		for (int k = 0; k < 3; k++) {
			float rr = 0.012f + 0.009f * k + 0.004f * sinf(t * 1.5f + k);
			float pv = (0.28f - 0.06f * k) * G + 0.2f * s->surge;
			for (int q = 0; q < 24; q++) {
				if ((q + k * 3) % 6 == 0)
					continue;
				float a0 = US_TAU * q / 24, a1 = US_TAU * (q + 1) / 24;
				stroke(c, xw + cosf(a0) * rr * 1.8f, yb + 0.006f + sinf(a0) * rr * 0.35f, xw + cosf(a1) * rr * 1.8f,
				       yb + 0.006f + sinf(a1) * rr * 0.35f, 0.001f, pv, US_MAT_WATER);
			}
		}
		for (int k = 0; k < 3; k++) {
			float ag = s->ring_t[k];
			if (ag > 1.5f)
				continue;
			float rr = 0.012f + 0.05f * ag;
			float pv = 0.6f * (1.0f - ag / 1.5f);
			for (int q = 0; q < 32; q++) {
				float a0 = US_TAU * q / 32, a1 = US_TAU * (q + 1) / 32;
				stroke(c, xw + cosf(a0) * rr * 1.9f, yb + 0.008f + sinf(a0) * rr * 0.3f, xw + cosf(a1) * rr * 1.9f,
				       yb + 0.008f + sinf(a1) * rr * 0.3f, 0.001f, pv, US_MAT_WATER);
			}
		}
		/* hats: spray glints around the foot */
		if (s->glint > 0.02f) {
			int ng = 20 + (int)(16 * dens);
			float gv = us_sat(s->glint * (0.7f + 0.3f * hat) * 1.2f);
			for (int k = 0; k < ng; k++) {
				float h1 = hash1(k, s->hat_seedf), h2 = hash1(k + 101, s->hat_seedf), h3 = hash1(k + 202, s->hat_seedf);
				float gx = xw + (h1 - 0.5f) * 0.09f, gy = yb - 0.05f * h2 * h2 + 0.006f;
				sparkle(c, gx, gy, (0.003f + 0.004f * h3) * (0.6f + 0.4f * gv), gv * (0.6f + 0.4f * h3), US_MAT_WATER);
			}
			/* and the river glitters down its length */
			int nrg = 20 + (int)(20 * dens + 12 * s->air_env);
			for (int k = 0; k < nrg; k++) {
				float h1 = hash1(k + 300, s->hat_seedf), h2 = hash1(k + 400, s->hat_seedf), h3 = hash1(k + 500, s->hat_seedf);
				float u, y, hw;
				river_at(s, 0.08f + 0.9f * h1, &u, &y, &hw);
				float x = u * A + (h2 * 2.0f - 1.0f) * hw * 0.8f;
				if (y > 0.995f || y > ridge_at(s, 3, x / A) - 0.002f)
					continue;
				sparkle(c, x, y, (0.0025f + 0.0035f * h3) * (0.5f + h1), gv * (0.55f + 0.45f * h3), US_MAT_WATER);
			}
		}
		us_world_add(w, &w->moisture, 0.0015f * vol * (0.4f + s->surge), dt);
	}

	/* ---- the river ---- */
	{
		/* banks */
		for (int side = -1; side <= 1; side += 2) {
			float px = 0, py = 0;
			for (int i = 0; i < NRIV; i++) {
				float u = s->riv[i * 3], y = s->riv[i * 3 + 1], hw = s->riv[i * 3 + 2];
				float x = u * A + side * hw * 1.05f;
				if (i > 0 && y < ridge_at(s, 3, x / A) - 0.002f && py < ridge_at(s, 3, px / A) - 0.002f) {
					float mk = 1.0f - 0.8f * mist_at(s, x, y, A);
					float v = (0.3f + 0.25f * (i / (float)NRIV)) * G * mk;
					stroke(c, px, py, x, y, 0.0012f, v, US_MAT_EARTH);
				}
				px = x;
				py = y;
			}
		}
		/* ripple marks: drift toward the viewer, each lit by its own band (far = highs, near = lows) */
		int nr = (int)(MAX_RIP * (0.45f + 0.55f * dens));
		float spd = 0.02f + 0.03f * vol + 0.03f * s->lvl;
		for (int i = 0; i < nr; i++) {
			rip_t *p = &s->rp[i];
			p->t += dt * spd * (0.6f + 0.8f * p->t);
			if (p->t > 1.0f) {
				p->t -= 1.0f;
				p->s = us_randr(r, -0.75f, 0.75f);
			}
			float u, y, hw;
			river_at(s, p->t, &u, &y, &hw);
			if (y > 0.998f)
				continue;
			int bi = 2 + (int)((1.0f - p->t) * 26.0f);
			if (bi > 29) bi = 29;
			float bk = us_sat(s->bandv[bi] * 1.2f);
			float x = u * A + p->s * hw;
			float len = hw * p->len * (0.6f + 0.6f * bk);
			float mk = 1.0f - 0.85f * mist_at(s, x, y, A);
			float fade = us_smooth(0.0f, 0.06f, p->t) * us_smooth(1.0f, 0.9f, p->t);
			float v = (0.2f + 0.45f * bk + 0.15f * s->lvl) * G * mk * fade;
			if (y > ridge_at(s, 3, x / A) - 0.002f)
				continue;
			stroke(c, x - len, y, x + len, y + 0.0015f * sinf(p->ph * 9.0f + t), 0.0011f, v, US_MAT_WATER);
		}
		/* reflection of the waterfall's light under the pool */
		(void)0;
	}

	/* ---- pavilion on its ledge ---- */
	{
		float xp = U_PAV * A, yp = ridge_at(s, 2, U_PAV) + 0.002f;
		float mk = 1.0f - 0.45f * mist_at(s, xp, yp - 0.02f, A);
		float v = (0.62f + 0.25f * s->lvl) * mk;
		float pw = 0.022f, ph = 0.02f;
		/* platform + posts + railing */
		if (crisp) {
			us_line(c, xp - pw * 1.1f, yp, xp + pw * 1.1f, yp, 0.004f, v * 0.7f, US_MAT_EARTH);
			for (int k = -1; k <= 1; k += 2)
				us_line(c, xp + k * pw * 0.75f, yp, xp + k * pw * 0.75f, yp - ph, 0.001f, v * 0.75f, US_MAT_STEM);
			us_line(c, xp, yp, xp, yp - ph, 0.001f, v * 0.6f, US_MAT_STEM);
		} else {
			wline(c, xp - pw * 1.15f, yp, xp + pw * 1.15f, yp, 0.0016f, v * 0.75f, US_MAT_EARTH);
			for (int k = -2; k <= 2; k++)
				if (k != 0)
					wline(c, xp + k * pw * 0.38f, yp, xp + k * pw * 0.38f, yp - ph, 0.0012f, v * (abs(k) == 2 ? 0.8f : 0.55f),
					      US_MAT_STEM);
			wline(c, xp - pw * 0.9f, yp - ph * 0.35f, xp + pw * 0.9f, yp - ph * 0.35f, 0.001f, v * 0.5f, US_MAT_STEM);
		}
		/* lantern: night and the low mids */
		float lan = us_sat((1.0f - dayk) * 0.7f + 0.8f * s->lm_env) * mk;
		if (lan > 0.03f) {
			us_disc(c, xp, yp - ph * 0.62f, crisp ? 0.005f : 0.0035f, lan, US_MAT_LIGHT);
			if (!crisp)
				us_disc(c, xp, yp - ph * 0.62f, 0.011f, lan * 0.18f, US_MAT_LIGHT);
		}
		/* the roof: sweeping eaves that turn up at the tips, a finial on top */
		float ey = yp - ph, ax = xp, ay = ey - 0.026f;
		float rp[2 * 9];
		for (int k = 0; k <= 8; k++) {
			float f = k / 8.0f; /* left tip -> apex -> right tip */
			float side = f < 0.5f ? -1.0f : 1.0f;
			float g = fabsf(f - 0.5f) * 2.0f; /* 0 at apex .. 1 at tips */
			rp[2 * k] = ax + side * pw * 1.45f * g;
			rp[2 * k + 1] = ay + (ey - ay) * powf(g, 0.6f) - 0.007f * us_smooth(0.8f, 1.0f, g) + 0.004f * g * (1.0f - g) * 4.0f;
		}
		float rv = v * (0.9f + 0.2f * s->warm);
		if (crisp) {
			for (int k = 0; k < 8; k++)
				us_tri(c, rp[2 * k], rp[2 * k + 1], rp[2 * k + 2], rp[2 * k + 3], ax, ey - 0.002f, rv, US_MAT_BLOOM);
			us_line(c, xp - pw * 1.2f, ey, xp + pw * 1.2f, ey, 0.001f, rv, US_MAT_BLOOM);
		} else {
			wpoly(c, rp, 9, 0.0016f, rv, US_MAT_BLOOM);
			wline(c, xp - pw * 1.2f, ey + 0.001f, xp + pw * 1.2f, ey + 0.001f, 0.0014f, rv * 0.9f, US_MAT_BLOOM);
			/* tile lines down the roof */
			for (int k = 1; k < 8; k++) {
				float f = k / 8.0f;
				float bx = xp + (f - 0.5f) * 2.0f * pw * 1.15f;
				float tx = ax + (bx - ax) * 0.25f, ty = ay + 0.006f;
				wline(c, tx, ty, bx, ey, 0.001f, rv * 0.45f, US_MAT_BLOOM);
			}
		}
		stroke(c, ax, ay, ax, ay - 0.008f, 0.0012f, rv, US_MAT_BLOOM);
		if (winter > 0.2f)
			wpoly(c, rp + 4, 5, 0.002f, winter * 0.7f * mk, US_MAT_ACCENT);
	}

	/* ---- the flowering / maple tree beside the pavilion (season's colour) ---- */
	{
		float xt = (U_PAV + 0.03f) * A, yt = ridge_at(s, 2, U_PAV + 0.03f) + 0.004f;
		float mk = 1.0f - 0.8f * mist_at(s, xt, yt - 0.03f, A);
		float v = 0.55f * G * mk;
		float bl[6][4] = {{0, 0, 0.004f, -0.03f},       {0.004f, -0.03f, 0.016f, -0.048f}, {0.004f, -0.03f, -0.008f, -0.05f},
				  {0.002f, -0.018f, 0.018f, -0.024f}, {0.016f, -0.048f, 0.022f, -0.06f}, {-0.008f, -0.05f, -0.004f, -0.062f}};
		for (int k = 0; k < 6; k++)
			stroke(c, xt + bl[k][0], yt + bl[k][1], xt + bl[k][2], yt + bl[k][3], k ? 0.0011f : 0.0016f, v, US_MAT_STEM);
		float bloom = spring + autumn * 1.1f + summer * 0.8f;
		uint8_t bm = summer > 0.5f ? US_MAT_LEAF : US_MAT_BLOOM;
		int nb = (int)(34 * (0.5f + 0.5f * dens) * us_sat(bloom));
		for (int k = 0; k < nb; k++) {
			int e = 1 + k % 5;
			float f = hash1(k, 4.4f);
			float bx = xt + us_lerp(bl[e][0], bl[e][2], f) + (hash1(k, 5.5f) - 0.5f) * 0.012f;
			float by = yt + us_lerp(bl[e][1], bl[e][3], f) + (hash1(k, 6.6f) - 0.5f) * 0.01f;
			float bv = v * 1.3f * (0.7f + 0.3f * hash1(k, 7.7f)) * (1.0f + 0.3f * s->warm);
			us_point(c, bx, by, crisp ? 0.003f : 0.0019f, us_sat(bv), bm);
		}
		/* falling petals (spring) / leaves (autumn) drift off on the wind */
		float fallr = (spring * 0.8f + autumn * 1.5f) * (0.3f + 0.7f * s->drift + 0.5f * s->lvl);
		if (us_rand(r) < fallr * dt * 3.0f) {
			part_t *p = spawn(s);
			p->kind = 2;
			p->x = xt + us_randr(r, -0.015f, 0.02f);
			p->y = yt - us_randr(r, 0.03f, 0.06f);
			p->vx = us_randr(r, 0.01f, 0.04f);
			p->vy = us_randr(r, 0.005f, 0.015f);
			p->life = us_randr(r, 3.0f, 6.0f);
			p->ang = us_rand(r) * US_TAU;
			p->spin = us_randr(r, -3, 3);
			p->len = 0.003f;
			p->mat = US_MAT_BLOOM;
		}
	}

	/* ---- pines ---- */
	{
		for (int i = 0; i < NPINE; i++) {
			const pine_t *p = &s->pine[i];
			float x = p->u * A, y = p->y - 0.6f * p->sc;
			float mk = 1.0f - 0.85f * mist_at(s, x, y, A);
			float v = (i == 0 ? 0.72f : 0.6f) * G;
			draw_pine(c, p, A, v, i == 0 ? s->shake : s->shake * 0.3f, t, winter, mk, i);
		}
	}

	/* ---- cranes ---- */
	{
		int want = 2 + (int)(3.0f * w->flock + 1.5f * dens);
		int stand = 0, active = 0;
		for (int i = 0; i < MAX_CRANE; i++) {
			if (s->cr[i].state == 1) stand++;
			if (s->cr[i].state) active++;
		}
		if (!s->inited) { /* start with a few in the shallows */
			for (int i = 0; i < want && i < MAX_CRANE; i++) {
				crane_t *k = &s->cr[i];
				k->state = 1;
				place_crane_stand(s, k);
				k->dir = us_rand(r) < 0.5f ? -1.0f : 1.0f;
			}
			s->inited = 1;
		}
		/* now and then one glides in to land; one crosses the sky */
		s->land_t -= dt;
		s->cross_t -= dt;
		if (s->land_t <= 0 && stand < want) {
			for (int i = 0; i < MAX_CRANE; i++)
				if (!s->cr[i].state) {
					crane_t *k = &s->cr[i];
					k->state = 3;
					place_crane_stand(s, k);
					k->tgt_x = k->x;
					k->tgt_y = k->y;
					k->dir = us_rand(r) < 0.5f ? -1.0f : 1.0f;
					k->x = k->tgt_x - k->dir * 0.35f;
					k->y = k->tgt_y - 0.3f;
					k->rate = 2.5f;
					break;
				}
			s->land_t = us_randr(r, 5.0f, 11.0f);
		}
		if (s->cross_t <= 0) {
			for (int i = 0; i < MAX_CRANE; i++)
				if (!s->cr[i].state) {
					crane_t *k = &s->cr[i];
					k->state = 4;
					k->dir = us_rand(r) < 0.6f ? -1.0f : 1.0f;
					k->x = k->dir < 0 ? 1.04f : -0.04f;
					k->y = us_randr(r, 0.12f, 0.3f);
					k->vx = k->dir * us_randr(r, 0.025f, 0.035f);
					k->vy = us_randr(r, -0.003f, 0.003f);
					k->rate = 1.8f;
					break;
				}
			s->cross_t = us_randr(r, 9.0f, 20.0f) * (1.2f - 0.5f * w->flock);
		}
		(void)active;
		for (int i = 0; i < MAX_CRANE; i++) {
			crane_t *k = &s->cr[i];
			if (!k->state)
				continue;
			k->kick = us_approach(k->kick, 0.0f, 0.22f, rdt);
			float sz = 0.034f * k->sc;
			if (k->state == 1) {
				/* standing: preen now and then */
				k->t += dt;
				float bow = us_smooth(0.3f, 0.7f, 0.5f + 0.5f * sinf(k->t * 0.35f + k->ph * 3.0f)) * 0.9f;
				float x = k->x * A;
				float mk = 1.0f - 0.7f * mist_at(s, x, k->y - 0.02f, A);
				draw_crane_stand(c, x, k->y, sz, k->ph, k->dir, 0.85f * G * (1.0f + 0.3f * s->warm), mk, bow);
				/* reflection: a faint broken stroke below */
				stroke(c, x - 0.006f, k->y + 0.004f, x + 0.006f, k->y + 0.004f, 0.001f, 0.25f * G * mk, US_MAT_WATER);
				continue;
			}
			if (k->state == 3) {
				/* gliding in: ease toward the landing spot, legs down */
				float dx = k->tgt_x - k->x, dy = k->tgt_y - k->y;
				k->x += dx * us_sat(dt * 0.45f) + k->dir * 0.002f * dt;
				k->y += dy * us_sat(dt * 0.45f);
				k->ph += dt * k->rate * (0.3f + 0.7f * us_smooth(0.0f, 0.05f, fabsf(dy)));
				if (fabsf(dx) * A < 0.004f && fabsf(dy) < 0.004f) {
					k->state = 1;
					k->x = k->tgt_x;
					k->y = k->tgt_y;
					k->t = 0;
					continue;
				}
			} else {
				/* flying: beats faster after a snare; launched birds climb then level */
				k->ph += dt * k->rate * (1.0f + 2.0f * k->kick);
				k->x += k->vx * dt * (1.0f + 0.6f * k->kick) / A * 1.78f;
				k->y += k->vy * dt * (1.0f + 0.6f * k->kick);
				if (k->state == 2) {
					k->vy = us_approach(k->vy, -0.012f, 2.5f, dt);
					k->vx = us_approach(k->vx, k->dir * 0.1f, 2.0f, dt);
					k->rate = us_approach(k->rate, 1.6f, 0.8f, dt); /* then glide on slow, deep beats */
				}
				k->y += 0.0015f * sinf(k->ph * 0.5f) * dt;
				if (k->x < -0.08f || k->x > 1.08f || k->y < -0.06f) {
					k->state = 0;
					continue;
				}
			}
			float x = k->x * A;
			float mk = 1.0f - 0.4f * mist_at(s, x, k->y, A);
			float persp = k->state == 4 ? 0.8f : 1.0f;
			draw_crane_fly(c, x, k->y, sz * persp * 1.2f, k->ph, k->dir, (0.8f + 0.35f * k->kick) * G * (1.0f + 0.3f * s->warm),
				       mk);
		}
	}

	/* ---- bamboo grove on the right bank ---- */
	{
		int nb = (int)(s->nbb * (0.5f + 0.5f * dens));
		for (int i = 0; i < nb; i++) {
			const bamboo_t *b = &s->bb[i];
			float x0 = b->u * A, y0 = ridge_at(s, 3, b->u) + 0.03f;
			float sway = s->bsw * (0.7f + 0.6f * hash1(i, 2.2f)) + 0.012f * sinf(t * 0.9f + b->ph) + b->lean;
			int n = b->nodes;
			float px = x0, py = y0;
			float v = (0.55f + 0.2f * hash1(i, 1.1f)) * G;
			for (int q = 1; q <= n; q++) {
				float f = q / (float)n;
				float bend = sway * powf(f, 1.6f);
				float x = x0 + sinf(bend) * b->h * f, y = y0 - cosf(bend) * b->h * f;
				if (crisp)
					us_line(c, px, py, x, y, 0.001f, v * 0.8f, US_MAT_STEM);
				else
					wline(c, px, py, x, y, 0.0022f - 0.001f * f, v * 0.85f, US_MAT_STEM);
				/* node tick */
				stroke(c, x - 0.003f, y, x + 0.003f, y, 0.001f, v, US_MAT_STEM);
				/* leaves from the upper nodes: slender, drooping, swaying more */
				if (f > 0.4f) {
					int nl = 2 + (q + i) % 3;
					for (int L = 0; L < nl; L++) {
						float side = ((L + q + i) & 1) ? 1.0f : -1.0f;
						float ang = (side > 0 ? 0.35f : US_PI - 0.35f) + side * 0.25f * L + 0.6f * sway + 0.1f * sinf(t * 1.3f + q + L) +
								    0.5f * s->shake * sinf(t * 37.0f + q * 2.1f + L * 1.3f + i); /* snare: leaves shiver */
						float ll = 0.02f + 0.01f * hash1(q * 7 + L, (float)i);
						uint8_t lm = US_MAT_LEAF;
						if (winter > 0.6f && (L & 1))
							lm = US_MAT_ACCENT;
						petal(c, x, y, ang, ll, ll * 0.14f, v * (0.85f + 0.15f * summer), lm);
						/* hats: dew glints at leaf tips */
						if (s->glint > 0.02f && hash1(i * 64 + q * 8 + L, s->hat_seedf) < 0.22f + 0.25f * s->air_env)
							sparkle(c, x + cosf(ang) * ll, y + sinf(ang) * ll, 0.0045f,
								us_sat(s->glint * (0.75f + 0.3f * hat)), US_MAT_LIGHT);
					}
				}
				px = x;
				py = y;
			}
		}
		/* grasses + rocks along the near bank */
		for (int k = 0; k < 9; k++) {
			float u = 0.75f + 0.03f * k, y = ridge_at(s, 3, u) + 0.004f;
			float x = u * A;
			float gl = 0.012f + 0.01f * hash1(k, 8.0f);
			stroke(c, x, y, x - 0.004f + s->bsw * 0.02f, y - gl, 0.001f, 0.4f * G, US_MAT_LEAF);
			stroke(c, x + 0.002f, y, x + 0.006f + s->bsw * 0.02f, y - gl * 0.7f, 0.001f, 0.35f * G, US_MAT_LEAF);
		}
	}

	/* ---- particles: needles, spray, petals / leaves, snow ---- */
	{
		/* winter: snow drifts down */
		float snowr = winter * (40.0f + 60.0f * dens) * (0.6f + 0.4f * s->lvl);
		int nsn = (int)(snowr * dt + us_rand(r));
		for (int k = 0; k < nsn && winter > 0.3f; k++) {
			part_t *p = spawn(s);
			p->kind = 3;
			p->x = us_rand(r) * A;
			p->y = -0.01f;
			p->vx = us_randr(r, -0.01f, 0.01f) + 0.02f * s->drift;
			p->vy = us_randr(r, 0.03f, 0.06f);
			p->life = 30.0f;
			p->mat = US_MAT_ACCENT;
		}
		for (int i = 0; i < MAX_PART; i++) {
			part_t *p = &s->pt[i];
			if (p->life <= 0)
				continue;
			p->life -= dt;
			if (p->kind == 1) { /* spray: thrown up, falls back */
				p->vy += 0.35f * dt;
				p->x += p->vx * dt;
				p->y += p->vy * dt;
				p->life -= dt * 0.8f;
				us_point(c, p->x, p->y, crisp ? 0.003f : 0.0014f, us_sat(p->life * 1.2f) * 0.8f, US_MAT_WATER);
				continue;
			}
			if (p->kind == 3) {
				p->x += (p->vx + 0.006f * sinf(t + i)) * dt;
				p->y += p->vy * dt;
				if (p->y > 1.01f)
					p->life = 0;
				us_point(c, p->x, p->y, crisp ? 0.003f : 0.0015f, 0.6f * (1.0f - 0.6f * mist_at(s, p->x, p->y, A)), US_MAT_ACCENT);
				continue;
			}
			/* needles / petals / leaves: flutter down on the wind (a burst of needles slows quickly) */
			float drag = p->kind == 0 ? 0.18f : 1.2f;
			p->vy = us_approach(p->vy, 0.035f, drag, dt);
			p->vx = us_approach(p->vx, 0.03f * s->drift + 0.01f, drag * 1.2f, dt);
			p->x += (p->vx + 0.01f * sinf(t * 2.0f + i)) * dt;
			p->y += p->vy * dt;
			p->ang += p->spin * dt;
			if (p->y > 1.02f)
				p->life = 0;
			float v = us_sat(p->life * 1.5f) * (p->kind == 0 ? 0.85f : 0.75f);
			float ca = cosf(p->ang) * p->len, sa = sinf(p->ang) * p->len;
			if (p->kind == 2 && !crisp)
				petal(c, p->x - ca * 0.5f, p->y - sa * 0.5f, p->ang, p->len * 1.2f, p->len * 0.45f, v, p->mat);
			else
				stroke(c, p->x - ca, p->y - sa, p->x + ca, p->y + sa, 0.0011f, v, p->mat);
		}
	}
}

const us_scene_vt us_scene_valley = {"valley", "Misty Valley", 0.0f, create, destroy, frame};
