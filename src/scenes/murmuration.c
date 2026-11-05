/* Understory scene: Murmuration — a starling flock folding over a reed bed at dusk.
 *
 * A real murmuration is a thin, twisting 3-D sheet of birds seen from the ground: where the sheet turns
 * edge-on to us it collapses into a dark dense seam, where it faces us it thins to a haze. So the flock here
 * is a deforming ribbon surface in 3-D (a wavy spine, a width vector twisting along it, a little thickness),
 * rotated and projected each frame. Every bird owns a (u, v, n) spot on that sheet, drifts along it, and
 * chases its spot with a soft spring — so the sheet's folds read as density, and birds can be knocked off it
 * (falcon) and visibly re-form. A smaller satellite sheet splits away now and then and merges back.
 *   centroid (melody brightness) -> where the flock wanders (left .. right) and how high it rides
 *   mid                          -> how fast the sheet yaws and twists (the flock banks and wheels)
 *   swell                        -> how long the ribbon stretches across the sky
 *   level                        -> compaction: loud music squeezes the sheet into tight dense folds
 *   kicks / beats                -> agitation waves: dark bands of banking birds race along the ribbon
 *   big transients               -> a falcon stoops through: a flash-expansion hole, a split, then re-forming
 *   silence                      -> the flock funnels down and pours into the reeds to roost, a trickle cycling
 * World: reads flock (bird count at open, slowly followed), pollen + insects (food => more birds), season,
 *        daylight, wind; writes flock (lively music with food around draws birds in).
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAXB 3000
#define NS 72 /* spine samples per sheet */
#define NSHEET 2
#define NWAVE 10
#define NTREES 2
#define MAXTS 200
#define NREEDS 170

#define HORIZON 0.80f
#define MGX 64 /* coarse flock-coverage grid (for halo tinting) */
#define MGY 36
#define NFAR 3  /* distant strings of birds low over the horizon */
#define FARN 28

typedef struct {
	float x, y, vx, vy;    /* screen position + velocity */
	float u, v, n;         /* spot on the sheet */
	float ptx, pty;        /* previous target (for target velocity) */
	float hx, hy;          /* smoothed heading */
	float life, panic;
	float hash, ph, fq;    /* per-bird constants */
	float z;               /* last depth */
	int alive, leaving, sheet, snap;
} bird_t;

typedef struct {
	/* pose (integrated / smoothed) */
	float cx, cy, len, wid, thick;
	float yaw, roll, pitch, tw0, twk;
	float a1, f1, p1, a2, f2, p2;
	/* sampled each frame (screen space; z = depth, + toward viewer) */
	float sx[NS], sy[NS], sz[NS];
	float wx[NS], wy[NS], wz[NS];
	float nx[NS], ny[NS];
	float ws[NS];
} sheet_t;

typedef struct {
	float u, speed, str;
	int sheet, on;
} wave_t;

typedef struct {
	float x0, y0, x1, y1, w; /* x as fraction of width */
	int depth;
} tseg_t;

typedef struct {
	int n;
	tseg_t s[MAXTS];
} tree_t;

typedef struct {
	us_rng rng;
	int inited;
	float A;
	bird_t b[MAXB];
	sheet_t sh[NSHEET];
	wave_t wave[NWAVE];
	tree_t tree[NTREES];
	float reed_x[NREEDS], reed_h[NREEDS], reed_ph[NREEDS];
	/* audio envelopes */
	float cent_s, cent_mean, mid_s, lvl_s, swell_s, quiet, roost;
	float flow_dir, yaw_dir, twist_kick;
	float sat_frac, sat_boost;
	float wcool, fcool;
	/* falcon */
	float fx, fy, fvx, fvy, ft;
	float wind_t, pop;
	float sat_side;
	float wp; /* wander phase: the flock's slow tour of the whole sky */
	float mg[MGX * MGY], mcov[MGX * MGY];
	float mwt[MGX * MGY];
	/* far strings */
	float far_x[NFAR], far_y[NFAR], far_v[NFAR], far_ph[NFAR], far_wait[NFAR], far_amp[NFAR];
	int far_on[NFAR];
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x6d75726du);
	return s;
}

static void destroy(void *p) { free(p); }

/* ---- drawing helpers ------------------------------------------------- */

/* bilinear splat of one sample: AA without the cost of a full line rasteriser */
static inline void splat(us_canvas *c, float x, float y, float v, uint8_t m)
{
	float fx = x * c->s - 0.5f, fy = y * c->s - 0.5f;
	int ix = (int)floorf(fx), iy = (int)floorf(fy);
	float ax = fx - ix, ay = fy - iy;
	us_put(c, ix, iy, v * (1 - ax) * (1 - ay), m);
	us_put(c, ix + 1, iy, v * ax * (1 - ay), m);
	us_put(c, ix, iy + 1, v * (1 - ax) * ay, m);
	us_put(c, ix + 1, iy + 1, v * ax * ay, m);
}

/* a bird: a tiny dash along its heading */
static inline void dash(us_canvas *c, float x, float y, float hx, float hy, float L, float v, uint8_t m)
{
	if (c->crisp) {
		us_put(c, (int)(x * c->s), (int)(y * c->s), v, m);
		if (L * c->s > 1.3f)
			us_put(c, (int)((x - hx * L) * c->s), (int)((y - hy * L) * c->s), v * 0.6f, m);
		return;
	}
	int n = (int)(L * c->s) + 1;
	if (n > 12)
		n = 12;
	for (int i = 0; i <= n; i++) {
		float t = (float)i / n - 0.5f;
		splat(c, x + hx * L * t, y + hy * L * t, v, m);
	}
}

/* a fine horizontal hairline (cheap: two rows of coverage, no general line rasteriser) */
static void hline(us_canvas *c, float x0, float x1, float y, float v, uint8_t m)
{
	if (c->crisp) {
		int py = (int)(y * c->s);
		for (int px = (int)(x0 * c->s); px < (int)(x1 * c->s); px++)
			us_put(c, px, py, v, m);
		return;
	}
	float fy = y * c->s - 0.5f;
	int iy = (int)floorf(fy);
	float ay = fy - iy;
	int p0 = (int)(x0 * c->s), p1 = (int)(x1 * c->s);
	if (p0 < 0)
		p0 = 0;
	if (p1 > c->w)
		p1 = c->w;
	for (int px = p0; px < p1; px++) {
		/* soft ends */
		float e = us_sat((px - p0) * 0.08f) * us_sat((p1 - px) * 0.08f);
		us_put(c, px, iy, v * e * (1 - ay), m);
		us_put(c, px, iy + 1, v * e * ay, m);
	}
}

/* sky hatching: a horizontal hairline that thins away where the flock flew last frame (cov = coarse
 * coverage grid), so the engraving parts softly around the birds instead of striping their glow */
static void hline_sky(us_canvas *c, float x0, float x1, float y, float v, uint8_t m, const float *cov, int dither)
{
	if (c->crisp && dither > 1) {
		/* pixel looks: a dithered gradient (sparser dots higher up) instead of hard ruled lines */
		int py = (int)(y * c->s), off = py * 7;
		for (int px = (int)(x0 * c->s); px < (int)(x1 * c->s); px++)
			if ((px + off) % dither == 0)
				us_put(c, px, py, v, m);
		return;
	}
	if (c->crisp || !cov) {
		hline(c, x0, x1, y, v, m);
		return;
	}
	float fy = y * c->s - 0.5f;
	int iy = (int)floorf(fy);
	float ay = fy - iy;
	int p0 = (int)(x0 * c->s), p1 = (int)(x1 * c->s);
	if (p0 < 0)
		p0 = 0;
	if (p1 > c->w)
		p1 = c->w;
	float gyf = us_clampf(y * MGY - 0.5f, 0, MGY - 1.001f);
	int gj = (int)gyf;
	float ty = gyf - gj;
	const float *r0 = cov + gj * MGX, *r1 = cov + (gj + 1) * MGX;
	float gsc = (float)MGX / c->w;
	for (int px = p0; px < p1; px++) {
		float e = us_sat((px - p0) * 0.08f) * us_sat((p1 - px) * 0.08f);
		float gxf = us_clampf((px + 0.5f) * gsc - 0.5f, 0, MGX - 1.001f);
		int gi = (int)gxf;
		float tx = gxf - gi;
		float k = us_lerp(us_lerp(r0[gi], r0[gi + 1], tx), us_lerp(r1[gi], r1[gi + 1], tx), ty);
		float vv = v * e * (1.0f - us_sat(k));
		if (vv <= 0.002f)
			continue;
		us_put(c, px, iy, vv * (1 - ay), m);
		us_put(c, px, iy + 1, vv * ay, m);
	}
}

/* cheap anti-aliased hairline (Wu-style: two pixels per step along the major axis) */
static void wline(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t m)
{
	if (c->crisp) {
		us_line(c, ax, ay, bx, by, 0.002f, v, m);
		return;
	}
	float x0 = ax * c->s - 0.5f, y0 = ay * c->s - 0.5f, x1 = bx * c->s - 0.5f, y1 = by * c->s - 0.5f;
	float dx = x1 - x0, dy = y1 - y0;
	int steep = fabsf(dy) > fabsf(dx);
	float len = steep ? fabsf(dy) : fabsf(dx);
	int n = (int)len + 1;
	if (n > 4000)
		return;
	float sx = dx / n, sy = dy / n;
	for (int i = 0; i <= n; i++) {
		float x = x0 + sx * i, y = y0 + sy * i;
		if (steep) {
			int ix = (int)floorf(x);
			float f = x - ix;
			int iy = (int)(y + 0.5f);
			us_put(c, ix, iy, v * (1 - f), m);
			us_put(c, ix + 1, iy, v * f, m);
		} else {
			int iy = (int)floorf(y);
			float f = y - iy;
			int ix = (int)(x + 0.5f);
			us_put(c, ix, iy, v * (1 - f), m);
			us_put(c, ix, iy + 1, v * f, m);
		}
	}
}

/* Ink bloom keeps each pixel's material, so a glow spreading over faint pixels of other materials comes out
 * patchy or striped. Recolour faint pixels toward `to` with a coarse weight field (bilinear between cell
 * centres, ordered dither at partial weight), so a halo is one even hue with no visible cell edges.
 * Works quad by quad between cell centres: empty quads are skipped, saturated quads take a fast path. */
static void tint_soft(us_canvas *c, const float *wgt, int gw, int gh, uint8_t to, int ink_max, unsigned from_mask)
{
	static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
	float cw = (float)c->w / gw, ch = (float)c->h / gh;
	for (int j = -1; j < gh; j++) {
		int ja = j < 0 ? 0 : j, jb = j + 1 >= gh ? gh - 1 : j + 1;
		int y0 = j < 0 ? 0 : (int)((j + 0.5f) * ch), y1 = j + 1 >= gh ? c->h : (int)((j + 1.5f) * ch);
		if (y1 > c->h)
			y1 = c->h;
		for (int i = -1; i < gw; i++) {
			int ia = i < 0 ? 0 : i, ib = i + 1 >= gw ? gw - 1 : i + 1;
			float w00 = wgt[ja * gw + ia], w10 = wgt[ja * gw + ib], w01 = wgt[jb * gw + ia], w11 = wgt[jb * gw + ib];
			float mx = fmaxf(fmaxf(w00, w10), fmaxf(w01, w11));
			if (mx < 0.03f)
				continue;
			int x0 = i < 0 ? 0 : (int)((i + 0.5f) * cw), x1 = i + 1 >= gw ? c->w : (int)((i + 1.5f) * cw);
			if (x1 > c->w)
				x1 = c->w;
			float mn = fminf(fminf(w00, w10), fminf(w01, w11));
			float iy = 1.0f / (y1 - y0 > 0 ? y1 - y0 : 1), ix = 1.0f / (x1 - x0 > 0 ? x1 - x0 : 1);
			for (int y = y0; y < y1; y++) {
				const uint8_t *ink = c->ink + (size_t)y * c->w;
				uint8_t *m = c->mat + (size_t)y * c->w;
				if (mn >= 1.0f) { /* branchless: this runs over large areas */
					for (int x = x0; x < x1; x++) {
						unsigned mm = m[x], ok = (unsigned)(ink[x] <= ink_max) & (from_mask >> (mm & 31u));
						m[x] = (uint8_t)(ok & 1u ? to : mm);
					}
					continue;
				}
				float ty = (y - y0 + 0.5f) * iy;
				float wl = (w00 + (w01 - w00) * ty) * 16.0f, wr = (w10 + (w11 - w10) * ty) * 16.0f;
				float k = wl + (wr - wl) * 0.5f * ix, dk = (wr - wl) * ix;
				const uint8_t *bz = bayer + (y & 3) * 4;
				for (int x = x0; x < x1; x++, k += dk) {
					unsigned mm = m[x];
					unsigned ok = (unsigned)(k > bz[x & 3] + 0.5f) & (unsigned)(ink[x] <= ink_max) & (from_mask >> (mm & 31u));
					m[x] = (uint8_t)(ok & 1u ? to : mm);
				}
			}
		}
	}
}

/* spread a coarse accumulation grid into a 0..1 tint weight (cone kernel of radius `rad` cells, rad <= 3) */
static void tint_weight(const float *acc, float *wgt, int gw, int gh, int rad, float gain)
{
	float kern[7][7];
	for (int dy = -rad; dy <= rad; dy++)
		for (int dx = -rad; dx <= rad; dx++) {
			float d = sqrtf((float)(dx * dx + dy * dy)) / (rad + 0.5f);
			kern[dy + 3][dx + 3] = d < 1.0f ? (1.0f - d) * gain : 0.0f;
		}
	memset(wgt, 0, sizeof(float) * (size_t)gw * gh);
	/* scatter: only occupied cells cost anything */
	for (int gy = 0; gy < gh; gy++)
		for (int gx = 0; gx < gw; gx++) {
			float a = acc[gy * gw + gx];
			if (a <= 0)
				continue;
			for (int dy = -rad; dy <= rad; dy++) {
				int yy = gy + dy;
				if (yy < 0 || yy >= gh)
					continue;
				for (int dx = -rad; dx <= rad; dx++) {
					int xx = gx + dx;
					if (xx >= 0 && xx < gw)
						wgt[yy * gw + xx] += a * kern[dy + 3][dx + 3];
				}
			}
		}
	for (int i = 0; i < gw * gh; i++)
		wgt[i] = us_sat(wgt[i]);
}

/* ---- context: bare trees -------------------------------------------- */

static void grow_tree(st_t *st, tree_t *t, float x, float y, float ang, float len, float w, int depth)
{
	us_rng *r = &st->rng;
	if (t->n >= MAXTS || depth > 7 || len < 0.006f)
		return;
	float a1 = ang + us_randn(r) * (depth < 2 ? 0.04f : 0.12f);
	float xm = x + cosf(a1) * len * 0.5f / 1.7778f, ym = y + sinf(a1) * len * 0.5f;
	float a2 = a1 + us_randn(r) * (depth < 2 ? 0.04f : 0.15f);
	float xe = xm + cosf(a2) * len * 0.5f / 1.7778f, ye = ym + sinf(a2) * len * 0.5f;
	t->s[t->n++] = (tseg_t){x, y, xm, ym, w, depth};
	if (t->n >= MAXTS)
		return;
	t->s[t->n++] = (tseg_t){xm, ym, xe, ye, w * 0.85f, depth};
	int forks = depth == 0 ? 1 : (depth < 3 ? 2 + (us_rand(r) < 0.4f) : 2);
	for (int k = 0; k < forks; k++) {
		float spread = forks == 1 ? us_randn(r) * 0.08f
					  : us_randr(r, 0.25f, 0.6f) * (k == 0 ? -1.0f : 1.0f) * (k == 2 ? 0.1f : 1.0f);
		float na = us_lerp(a2 + spread, -US_PI * 0.5f, 0.12f);
		grow_tree(st, t, xe, ye, na, len * (forks == 1 ? 0.85f : us_randr(r, 0.66f, 0.8f)),
			  w * (forks == 1 ? 0.8f : 0.68f), depth + 1);
	}
}

static void place_bird(st_t *st, bird_t *b)
{
	us_rng *r = &st->rng;
	b->u = us_rand(r);
	float v = us_randr(r, -1.0f, 1.0f);
	/* slightly crowded edges: real flocks have sharp boundaries */
	b->v = (v < 0 ? -1.0f : 1.0f) * powf(fabsf(v), 0.8f);
	b->n = us_clampf(us_randn(r) * 0.5f, -1.0f, 1.0f);
	b->hash = us_rand(r);
	b->ph = us_rand(r) * US_TAU;
	b->fq = us_randr(r, 0.4f, 1.3f);
	b->panic = 0;
	b->leaving = 0;
}

static void init_scene(st_t *st, const us_ctx *ctx)
{
	us_rng *r = &st->rng;
	float xs[NTREES] = {0.07f, 0.9f};
	float hs[NTREES] = {0.15f, 0.1f};
	for (int i = 0; i < NTREES; i++) {
		st->tree[i].n = 0;
		grow_tree(st, &st->tree[i], xs[i] + us_randr(r, -0.02f, 0.02f), HORIZON + 0.01f, -US_PI * 0.5f, hs[i],
			  0.005f * hs[i] / 0.14f, 0);
	}
	for (int k = 0; k < NREEDS; k++) {
		st->reed_x[k] = us_rand(r);
		float h = us_rand(r);
		st->reed_h[k] = 0.035f + 0.08f * h * h;
		st->reed_ph[k] = us_rand(r) * 40.0f;
	}
	st->A = ctx->aspect;
	for (int k = 0; k < NSHEET; k++) {
		sheet_t *s = &st->sh[k];
		s->cx = 0.5f;
		s->cy = 0.36f;
		s->len = k ? 0.25f : 0.6f;
		s->wid = 0.12f;
		s->thick = 0.012f;
		s->yaw = us_randr(r, 0, US_TAU);
		s->tw0 = us_randr(r, 0, US_TAU);
		s->twk = 3.0f;
		s->a1 = 0.07f, s->f1 = 0.8f, s->p1 = us_rand(r) * US_TAU;
		s->a2 = 0.1f, s->f2 = 0.6f, s->p2 = us_rand(r) * US_TAU;
	}
	st->cent_s = st->cent_mean = 0.5f;
	st->wp = us_randr(r, 0, US_TAU);
	for (int k = 0; k < NFAR; k++)
		st->far_wait[k] = us_randr(r, 0.0f, 8.0f) * k;
	st->flow_dir = 1.0f;
	st->yaw_dir = 1.0f;
}

/* evolve one sheet's pose and sample its spine (screen space) */
static void build_sheet(st_t *st, int k, const us_ctx *ctx, float tx, float ty, float roost, float fun_x)
{
	sheet_t *s = &st->sh[k];
	float dt = ctx->dt, t = (float)ctx->t, A = ctx->aspect;
	float ks = k * 17.3f;
	float lvl = st->lvl_s, mid = st->mid_s;

	/* centre */
	s->cx = us_approach(s->cx, tx / A, k ? 1.8f : 3.0f, dt);
	s->cy = us_approach(s->cy, ty, k ? 1.8f : 3.0f, dt);

	/* shape targets */
	float len_t = (k ? 0.22f : 0.5f) + (k ? 0.1f : 0.4f) * st->swell_s + 0.14f * us_noise2(t * 0.031f, 1.3f + ks);
	float wid_t = (0.035f + 0.07f * us_sat(0.5f + 0.7f * us_noise2(t * 0.043f, 4.1f + ks))) * (1.0f - 0.45f * lvl) + 0.015f;
	s->len = us_approach(s->len, len_t * (1.0f - 0.3f * roost), 2.5f, dt);
	s->wid = us_approach(s->wid, wid_t, 2.0f, dt);
	s->thick = us_approach(s->thick, 0.008f * (1.0f - 0.5f * lvl) + 0.003f, 1.0f, dt);

	/* wheeling: yaw spins the sheet through edge-on; twist rolls along its length */
	if (us_noise2(t * 0.013f, 9.0f + ks) < -0.25f)
		st->yaw_dir = -1.0f;
	else if (us_noise2(t * 0.013f, 9.0f + ks) > 0.25f)
		st->yaw_dir = 1.0f;
	float wheel = 0.06f + 0.3f * mid + 0.1f * us_noise2(t * 0.09f, 3.1f + ks);
	s->yaw += dt * wheel * st->yaw_dir * (k ? -1.3f : 1.0f);
	s->tw0 += dt * (0.08f + 0.3f * mid) * (us_noise2(t * 0.021f, 5.0f + ks) > 0 ? 1.0f : -1.0f) + st->twist_kick * (k ? -0.5f : 1.0f);
	s->twk = us_approach(s->twk, 2.0f + 3.0f * us_sat(0.5f + 0.8f * us_noise2(t * 0.019f, 2.2f + ks)) + 1.5f * lvl, 3.0f, dt);
	s->roll = 0.35f * us_noise2(t * 0.037f, 6.6f + ks);
	s->pitch = 0.5f + 0.4f * us_noise2(t * 0.027f, 8.8f + ks);
	s->a1 = 0.04f + 0.09f * us_sat(0.5f + 0.8f * us_noise2(t * 0.029f, 11.0f + ks));
	s->f1 = 0.6f + 0.7f * us_sat(0.5f + 0.7f * us_noise2(t * 0.017f, 12.0f + ks));
	s->p1 += dt * (0.12f + 0.25f * mid);
	s->a2 = 0.06f + 0.12f * us_sat(0.5f + 0.8f * us_noise2(t * 0.023f, 13.0f + ks));
	s->f2 = 0.5f + 0.6f * us_sat(0.5f + 0.7f * us_noise2(t * 0.015f, 14.0f + ks));
	s->p2 += dt * (0.1f + 0.2f * mid);

	float cyw = cosf(s->yaw), syw = sinf(s->yaw);
	float cp = cosf(s->pitch), sp = sinf(s->pitch);
	float cr = cosf(s->roll), sr = sinf(s->roll);
	float cx = s->cx * A, cy = s->cy;
	/* roost funnel geometry (screen space) */
	float ftop = HORIZON - 0.40f, fbot = HORIZON + 0.06f;
	float fspin = t * 1.7f;
	for (int i = 0; i < NS; i++) {
		float u = (float)i / (NS - 1);
		float X = s->len * (u - 0.5f) * 1.2f;
		float Y = s->a1 * sinf(US_TAU * s->f1 * u + s->p1);
		float Z = s->a2 * sinf(US_TAU * s->f2 * u + s->p2) + 0.15f * s->len * (u - 0.5f) * (u - 0.5f) * 4.0f;
		float tau = s->tw0 + s->twk * (u - 0.5f);
		float WX = 0, WY = cosf(tau), WZ = sinf(tau);
		float NY = -sinf(tau), NZ = cosf(tau);
		float taper = powf(sinf(US_PI * us_clampf(u, 0.001f, 0.999f)), 0.65f);
		float wsc = s->wid * taper * (1.0f + 0.35f * us_noise2(u * 3.0f + ks, t * 0.07f));
		/* yaw (about vertical) */
		float x1 = X * cyw + Z * syw, z1 = -X * syw + Z * cyw, y1 = Y;
		float wx1 = WX * cyw + WZ * syw, wz1 = -WX * syw + WZ * cyw, wy1 = WY;
		float nx1 = NZ * syw, nz1 = NZ * cyw, ny1 = NY;
		/* pitch (about screen x) */
		float y2 = y1 * cp - z1 * sp, z2 = y1 * sp + z1 * cp;
		float wy2 = wy1 * cp - wz1 * sp, wz2 = wy1 * sp + wz1 * cp;
		float ny2 = ny1 * cp - nz1 * sp;
		/* roll (in screen) */
		float x3 = x1 * cr - y2 * sr, y3 = x1 * sr + y2 * cr;
		float wx3 = wx1 * cr - wy2 * sr, wy3 = wx1 * sr + wy2 * cr;
		float nx3 = nx1 * cr - ny2 * sr, ny3 = nx1 * sr + ny2 * cr;
		/* mild perspective */
		float persp = 1.0f / (1.0f - 0.35f * z2);
		float px = cx + x3 * persp, py = cy - y3 * persp;
		float pwx = wx3 * persp, pwy = -wy3 * persp;
		if (roost > 0.001f) {
			/* a tornado pouring into the reeds: u = 0 at the top, u = 1 below the horizon */
			float fy = us_lerp(ftop, fbot, u);
			float rad = 0.16f * powf(1.0f - u, 1.4f) + 0.012f;
			float ang = fspin + u * 9.0f + k * 2.0f;
			float fxp = fun_x + rad * cosf(ang) * 1.3f + 0.05f * (1.0f - u) * us_noise2(t * 0.1f, u * 2.0f);
			float fz = rad * sinf(ang);
			px = us_lerp(px, fxp, roost);
			py = us_lerp(py, fy, roost);
			z2 = us_lerp(z2, fz * 3.0f, roost);
			pwx = us_lerp(pwx, -sinf(ang) * 0.6f, roost);
			pwy = us_lerp(pwy, 0.15f, roost);
			wsc = us_lerp(wsc, rad * 0.9f + 0.01f, roost);
		}
		s->sx[i] = px;
		s->sy[i] = py;
		s->sz[i] = z2;
		s->wx[i] = pwx;
		s->wy[i] = pwy;
		s->wz[i] = wz2;
		s->nx[i] = nx3;
		s->ny[i] = -ny3;
		s->ws[i] = wsc;
	}
	/* keep the whole ribbon in the sky: drift the centre back when a fold pokes out of frame */
	if (roost < 0.5f) {
		float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
		for (int i = 0; i < NS; i += 3) {
			mnx = fminf(mnx, s->sx[i]), mxx = fmaxf(mxx, s->sx[i]);
			mny = fminf(mny, s->sy[i]), mxy = fmaxf(mxy, s->sy[i]);
		}
		float shx = 0, shy = 0;
		if (mnx < 0.05f * A)
			shx += 0.05f * A - mnx;
		if (mxx > 0.95f * A)
			shx -= mxx - 0.95f * A;
		if (mny < 0.05f)
			shy += 0.05f - mny;
		if (mxy > HORIZON - 0.06f)
			shy -= mxy - (HORIZON - 0.06f);
		float k = (1.0f - expf(-dt / 1.2f)) * (1.0f - 2.0f * roost);
		s->cx += shx / A * k;
		s->cy += shy * k;
	}
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *st = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, A = ctx->aspect, rdt = ctx->real_dt;
	float dens = ctx->p->density;
	float t = (float)ctx->t;
	float winter = us_world_winter(w);
	us_rng *r = &st->rng;
	if (dt > 0.05f)
		dt = 0.05f;

	if (!st->inited) {
		init_scene(st, ctx);
		st->pop = us_sat(0.4f + 0.75f * w->flock);
		int n0 = (int)((800.0f + 2200.0f * dens) * st->pop);
		for (int i = 0; i < MAXB; i++) {
			place_bird(st, &st->b[i]);
			/* the flock is already here when the scene opens */
			st->b[i].alive = st->b[i].snap = i < n0;
			st->b[i].life = 1.0f;
			st->b[i].ptx = -1e9f;
		}
		st->inited = 1;
	}
	if (fabsf(A - st->A) > 1e-4f) {
		float k = A / st->A;
		for (int i = 0; i < MAXB; i++) {
			st->b[i].x *= k;
			st->b[i].ptx *= k;
		}
		st->fx *= k;
		st->A = A;
	}

	/* ---- audio shaping ---- */
	st->cent_s = us_approach(st->cent_s, a->centroid, 1.5f, rdt);
	if (a->active > 0.3f)
		st->cent_mean = us_approach(st->cent_mean, a->centroid, 20.0f, rdt);
	st->mid_s = us_approach(st->mid_s, us_sat(a->mid * 0.8f + a->lowmid * 0.3f), 0.8f, rdt);
	st->lvl_s = us_approach(st->lvl_s, us_sat(a->level), 0.6f, rdt);
	st->swell_s = us_approach(st->swell_s, us_sat(a->swell), 2.0f, rdt);
	float q_in = us_sat(a->silence / 3.0f) * (1.0f - us_sat(a->level * 3.0f));
	st->quiet = us_approach(st->quiet, q_in, q_in > st->quiet ? 3.0f : 0.8f, rdt);
	st->roost = us_approach(st->roost, us_smooth(0.25f, 0.85f, st->quiet), 2.5f, rdt);
	float roost = st->roost;
	float wind = us_sat(0.2f * w->wind + 0.7f * a->high + 0.3f * a->air);
	st->wind_t += dt * (0.15f + 0.6f * wind);
	st->twist_kick = 0;

	/* ---- world ---- */
	float food = us_sat(0.6f * w->pollen + 0.6f * w->insects);
	us_world_add(w, &w->flock, 0.004f * a->active * st->lvl_s * (0.3f + food) * (1.0f - winter), dt);

	/* ---- population follows the world's flock + food, slowly ---- */
	float nmax = 800.0f + 2200.0f * dens;
	if (nmax > MAXB)
		nmax = MAXB;
	float pop_t = us_sat((0.4f + 0.75f * w->flock + 0.2f * food) * (1.0f - 0.45f * winter));
	st->pop = us_approach(st->pop, pop_t, 20.0f, dt);
	int N = (int)(nmax * st->pop);

	/* ---- agitation waves: kicks send dark banking bands along the ribbon ---- */
	st->wcool -= rdt;
	int want_wave = 0;
	float wstr = 0;
	if (a->beat && a->kick > 0.25f && st->wcool <= 0 && us_rand(r) < 0.75f)
		want_wave = 1, wstr = 0.55f + 0.45f * us_sat(a->kick);
	else if (us_rand(r) < dt * 0.2f) /* restless even in quiet */
		want_wave = 1, wstr = 0.45f;
	if (want_wave) {
		for (int k = 0; k < NWAVE; k++)
			if (!st->wave[k].on) {
				wave_t *wv = &st->wave[k];
				int from0 = us_rand(r) < 0.5f;
				wv->on = 1;
				wv->u = from0 ? -0.05f : 1.05f;
				wv->speed = (from0 ? 1.0f : -1.0f) * us_randr(r, 0.45f, 0.75f);
				wv->str = wstr;
				wv->sheet = us_rand(r) < 0.8f ? 0 : 1;
				st->wcool = 0.35f;
				break;
			}
	}
	for (int k = 0; k < NWAVE; k++) {
		wave_t *wv = &st->wave[k];
		if (!wv->on)
			continue;
		wv->u += wv->speed * dt;
		wv->str *= expf(-dt * 0.25f);
		if (wv->u < -0.1f || wv->u > 1.1f)
			wv->on = 0;
	}

	/* ---- falcon: biggest hits ---- */
	st->fcool -= rdt;
	if (a->beat && a->onset > 0.6f && st->fcool <= 0 && st->lvl_s > 0.25f && roost < 0.4f &&
	    us_rand(r) < 0.3f + 0.5f * a->kick) {
		/* strike at a live bird */
		int pick = -1;
		for (int tries = 0; tries < 24 && pick < 0; tries++) {
			int i = (int)(us_rand(r) * N);
			if (i < MAXB && st->b[i].alive && !st->b[i].leaving && st->b[i].sheet == 0)
				pick = i;
		}
		if (pick >= 0) {
			bird_t *pb = &st->b[pick];
			float ang = US_PI * 0.5f + us_randr(r, 0.35f, 1.1f) * (us_rand(r) < 0.5f ? 1.0f : -1.0f);
			float spd = 0.85f;
			st->fvx = cosf(ang) * spd;
			st->fvy = sinf(ang) * spd * 0.7f;
			st->fx = pb->x - st->fvx * 0.45f;
			st->fy = pb->y - st->fvy * 0.45f;
			st->ft = 1.6f;
			st->fcool = us_randr(r, 3.0f, 6.0f);
			st->sat_boost = 1.0f; /* the flock splits around the strike, then heals */
			st->twist_kick = 0.6f * (us_rand(r) < 0.5f ? 1.0f : -1.0f);
			/* the flinch runs along the sheet both ways from the strike */
			for (int dir = -1, made = 0; dir <= 1 && made < 2; dir += 2)
				for (int k = 0; k < NWAVE; k++)
					if (!st->wave[k].on) {
						st->wave[k] = (wave_t){pb->u, dir * 0.7f, 1.0f, 0, 1};
						made++;
						break;
					}
		}
	}
	st->sat_boost = us_approach(st->sat_boost, 0, 3.0f, dt);
	{
		float sat_t = 0.3f * us_smooth(0.0f, 0.6f, us_noise2(t * 0.021f, 31.0f)) + 0.28f * st->sat_boost;
		st->sat_frac = us_approach(st->sat_frac, sat_t * (1.0f - roost), 1.5f, dt);
	}

	/* ---- the sheets ---- */
	float fun_x = A * (0.3f + 0.08f * us_noise2(t * 0.02f, 40.0f));
	{
		/* the flock tours the whole sky on a slow swing (never parks at one side); melody leans it */
		st->wp += dt * (0.21f + 0.2f * st->mid_s);
		float bias = us_clampf((st->cent_s - st->cent_mean) * 4.0f, -1.0f, 1.0f);
		float tx = A * (0.5f + 0.21f * sinf(st->wp) + 0.08f * bias) + 0.07f * A * us_noise2(t * 0.027f, 1.7f);
		float ty = 0.35f + 0.07f * sinf(st->wp * 0.71f + 1.3f) + 0.05f * us_noise2(t * 0.037f, 7.3f) -
			   0.06f * st->lvl_s - 0.03f * bias;
		tx = us_clampf(tx, A * 0.24f, A * 0.76f);
		ty = us_clampf(ty, 0.2f, 0.5f);
		build_sheet(st, 0, ctx, tx, ty, roost, fun_x);
		float ox = 0.32f * us_noise2(t * 0.025f, 50.0f), oy = 0.12f * us_noise2(t * 0.03f, 51.0f);
		/* the satellite peels off toward open sky (the side away from the screen edge) */
		if (st->sh[0].cx > 0.58f)
			st->sat_side = -1.0f;
		else if (st->sh[0].cx < 0.42f)
			st->sat_side = 1.0f;
		else if (st->sat_side == 0)
			st->sat_side = ox >= 0 ? 1.0f : -1.0f;
		float sx = st->sh[0].cx * A + st->sat_side * (0.22f + fabsf(ox)), sy = st->sh[0].cy + oy - 0.04f;
		/* a merging satellite homes back onto the main sheet */
		float m = us_smooth(0.0f, 0.12f, st->sat_frac);
		sx = us_lerp(st->sh[0].cx * A, us_clampf(sx, 0.1f * A, 0.9f * A), m);
		sy = us_lerp(st->sh[0].cy, us_clampf(sy, 0.12f, 0.6f), m);
		build_sheet(st, 1, ctx, sx, sy, roost, fun_x + 0.03f);
	}
	float flow = (0.05f + 0.05f * st->lvl_s) * (us_noise2(t * 0.011f, 60.0f) > 0 ? 1.0f : -1.0f);
	flow = us_lerp(flow, 0.13f, roost);

	/* ---- sky: engraved hatching, denser and warmer toward a glowing horizon ---- */
	float day = w->daylight;
	float glow = 0.55f + 0.45f * (1.0f - fabsf(day - 0.35f) * 1.6f);
	float sun_x = A * 0.66f, sun_y = HORIZON - 0.045f - 0.1f * day, sun_r = 0.07f;
	{
		int nl = ctx->crisp ? 17 : 42;
		for (int k = 0; k < nl; k++) {
			float f = (k + 0.5f) / nl; /* 0 at the horizon .. 1 at the top */
			float y = HORIZON - 0.012f - 0.7f * powf(f, 1.55f);
			float lo = powf(1.0f - f, 1.7f);
			float v = (ctx->crisp ? 0.4f + 0.2f * powf(1.0f - f, 1.5f) : 0.06f + 0.15f * lo) * glow;
			/* broken into drifting cloud runs; the runs nearest the sun burn brighter */
			float x = 0, step = 0.03f;
			float drift = t * 0.006f * (1.0f + 0.3f * f);
			while (x < A) {
				float x0 = x;
				while (x < A && us_noise2(x * 2.3f - drift * 12.0f, k * 0.61f) > -0.35f + 0.25f * (1.0f - f))
					x += step;
				if (x > x0 + 0.001f) {
					float xc = 0.5f * (x0 + x), d = (xc - sun_x) / 0.5f;
					float near = expf(-d * d) * lo;
					uint8_t m = US_MAT_BG;
					if (!ctx->crisp)
						/* cool night blue overhead, a rose afterglow low down, gold around the sun */
						m = near > 0.45f ? US_MAT_LIGHT : (f < 0.4f ? US_MAT_BLOOM : US_MAT_WATER);
					float vv = v * (1.0f + (ctx->crisp ? 0.3f : 0.7f) * near);
					hline_sky(c, x0, x, y, vv, m, st->mcov, f > 0.55f ? 3 : (f > 0.2f ? 2 : 1));
				}
				x += step;
			}
		}
		/* low sun / moon: an engraved disc of horizontal strokes, cloud bars drifting across it */
		float sv = (0.5f + 0.25f * day) * glow;
		float rowh = ctx->crisp ? 1.0f / c->s : 0.0042f;
		for (float yy = sun_y - sun_r + rowh * 0.5f; yy < sun_y + sun_r; yy += rowh) {
			if (yy > HORIZON - 0.002f)
				break;
			float dy = (yy - sun_y) / sun_r;
			float hw = sun_r * sqrtf(us_sat(1.0f - dy * dy));
			/* cloud bars: slow bands of shadow across the disc */
			float cb = us_noise2(yy * 38.0f, t * 0.03f + 3.0f);
			float cut = us_smooth(0.15f, 0.4f, cb);
			float vv = sv * (0.75f + 0.25f * (1.0f - fabsf(dy))) * (1.0f - 0.85f * cut);
			if (vv < 0.03f)
				continue;
			if (ctx->crisp)
				for (int px = (int)((sun_x - hw) * c->s); px <= (int)((sun_x + hw) * c->s); px++)
					us_put(c, px, (int)(yy * c->s), vv, US_MAT_LIGHT);
			else
				hline(c, sun_x - hw, sun_x + hw, yy, vv, US_MAT_LIGHT);
		}
		if (!ctx->crisp)
			us_ring(c, sun_x, sun_y, sun_r, 0.0012f, 0.3f * glow, US_MAT_LIGHT);
		/* long thin cloud streaks in front, lit from below */
		for (int k = 0; k < 6; k++) {
			float yy = sun_y - sun_r * 0.6f + k * sun_r * 0.32f + 0.003f * sinf(t * 0.1f + k);
			if (yy > HORIZON - 0.004f)
				continue;
			float hw = sun_r * (1.4f + 0.8f * us_noise2(k * 1.3f, t * 0.02f)) + 0.05f * k;
			float xo = 0.03f * us_noise2(k * 2.1f, t * 0.015f);
			hline(c, sun_x - hw + xo, sun_x + hw * 0.7f + xo, yy, (0.16f + 0.1f * day) * glow,
			      ctx->crisp ? US_MAT_BG : US_MAT_BLOOM);
		}
		/* horizon + water glints under the sun */
		hline(c, 0, A, HORIZON, 0.45f, US_MAT_EARTH);
		for (int k = 1; k < 7; k++) {
			float yy = HORIZON + k * 0.0065f;
			float hw = 0.13f / (1.0f + k * 0.35f) * (0.7f + 0.3f * us_noise2(k * 3.0f, t * 0.3f));
			float xo = 0.006f * us_noise2(k * 5.0f, t * 0.4f);
			hline(c, sun_x - hw + xo, sun_x + hw + xo, yy, (0.36f - 0.035f * k) * glow, US_MAT_WATER);
		}
	}

	/* ---- far strings of birds, low over the horizon: depth, and life in the lower sky ---- */
	{
		int nf = 1 + (int)(2.0f * dens + 0.5f);
		for (int k = 0; k < NFAR; k++) {
			if (!st->far_on[k]) {
				st->far_wait[k] -= dt;
				if (st->far_wait[k] > 0 || k >= nf)
					continue;
				int left = us_rand(r) < 0.5f;
				st->far_on[k] = 1;
				st->far_v[k] = (left ? 1.0f : -1.0f) * us_randr(r, 0.035f, 0.06f);
				st->far_x[k] = left ? -0.25f : A + 0.25f;
				st->far_y[k] = us_randr(r, 0.52f, 0.7f);
				st->far_ph[k] = us_rand(r) * US_TAU;
				st->far_amp[k] = us_randr(r, 0.006f, 0.02f);
			}
			st->far_x[k] += st->far_v[k] * dt;
			st->far_y[k] += 0.004f * us_noise2(t * 0.2f, k * 7.0f) * dt;
			float dir = st->far_v[k] > 0 ? 1.0f : -1.0f;
			if ((dir > 0 && st->far_x[k] > A + 0.3f) || (dir < 0 && st->far_x[k] < -0.3f)) {
				st->far_on[k] = 0;
				st->far_wait[k] = us_randr(r, 3.0f, 12.0f);
				continue;
			}
			for (int i = 0; i < FARN; i++) {
				float u = (float)i / FARN;
				float bx = st->far_x[k] - dir * (u * 0.22f + 0.012f * us_noise2(i * 3.1f, t * 0.3f + k));
				float by = st->far_y[k] + st->far_amp[k] * sinf(st->far_ph[k] + t * 0.9f - u * 5.0f) +
					   0.006f * us_noise2(i * 1.7f, t * 0.25f + k * 3.0f);
				float fv = (0.3f - 0.12f * u) * (0.8f + 0.2f * sinf(t * 9.0f + i * 2.3f));
				dash(c, bx, by, dir, 0.0f, 0.0018f, fv, US_MAT_ACCENT);
			}
		}
	}

	/* ---- trees ---- */
	float tree_sway = (0.003f + 0.01f * wind) * us_noise2(st->wind_t * 0.7f, 2.0f);
	for (int i = 0; i < NTREES; i++) {
		tree_t *tr = &st->tree[i];
		for (int k = 0; k < tr->n; k++) {
			tseg_t *s = &tr->s[k];
			float h0 = (HORIZON - s->y0) / 0.15f, h1 = (HORIZON - s->y1) / 0.15f;
			float x0 = s->x0 * A + tree_sway * h0 * h0, x1 = s->x1 * A + tree_sway * h1 * h1;
			float wd = s->w;
			if (ctx->crisp && wd < 0.003f)
				wd = 0.002f;
			if (!ctx->crisp && wd < 0.0016f)
				wline(c, x0, s->y0, x1, s->y1, 0.34f - 0.02f * s->depth, US_MAT_STEM);
			else
				us_line(c, x0, s->y0, x1, s->y1, wd, 0.36f - 0.02f * s->depth, US_MAT_STEM);
		}
	}

	/* ---- birds ---- */
	c->blend = US_BLEND_ADD;
	memset(st->mg, 0, sizeof(st->mg));
	float bv = ctx->crisp ? 0.28f : 0.75f;
	float idt = dt > 1e-4f ? 1.0f / dt : 0.0f;
	for (int i = 0; i < MAXB; i++) {
		bird_t *b = &st->b[i];
		int want = i < N;
		if (!b->alive) {
			if (!want)
				continue;
			/* a newcomer streams in from an edge */
			place_bird(st, b);
			int left = us_rand(r) < 0.5f;
			b->x = left ? -0.03f : A + 0.03f;
			b->y = us_randr(r, 0.12f, 0.55f);
			b->vx = left ? 0.4f : -0.4f;
			b->vy = 0;
			b->hx = left ? 1.0f : -1.0f, b->hy = 0;
			b->ptx = -1e9f;
			b->alive = 1;
			b->life = 0;
		}
		if (!want && !b->leaving) {
			b->leaving = 1;
			b->vx += b->x < A * 0.5f ? -0.2f : 0.2f;
		}

		/* sheet assignment (satellite split / merge) */
		b->sheet = b->hash < st->sat_frac ? 1 : 0;
		sheet_t *s = &st->sh[b->sheet];
		b->u += flow * dt * (0.8f + 0.4f * b->fq);
		int wrapped = 0;
		if (b->u > 1.0f)
			b->u -= 1.0f, wrapped = 1;
		else if (b->u < 0.0f)
			b->u += 1.0f, wrapped = 1;

		/* agitation band at this bird */
		float band = 0;
		for (int k = 0; k < NWAVE; k++) {
			wave_t *wv = &st->wave[k];
			if (!wv->on || wv->sheet != b->sheet)
				continue;
			float d = (b->u - wv->u) * (1.0f / 0.04f);
			if (d > -2.0f && d < 2.0f) {
				float q = 1.0f - d * d * 0.25f;
				band += wv->str * q * q;
			}
		}
		if (band > 1.2f)
			band = 1.2f;

		/* target on the sheet */
		float fu = b->u * (NS - 1);
		int i0 = (int)fu;
		if (i0 >= NS - 1)
			i0 = NS - 2;
		float tu = fu - i0;
		int i1 = i0 + 1;
		float px = us_lerp(s->sx[i0], s->sx[i1], tu), py = us_lerp(s->sy[i0], s->sy[i1], tu);
		float pz = us_lerp(s->sz[i0], s->sz[i1], tu);
		float wsc = us_lerp(s->ws[i0], s->ws[i1], tu);
		float vv = b->v + 0.07f * sinf(t * 0.7f * b->fq + b->ph);
		float nn = b->n + 0.4f * sinf(t * 0.9f * b->fq + b->ph * 1.7f) + band * 1.6f;
		float wxx = us_lerp(s->wx[i0], s->wx[i1], tu), wyy = us_lerp(s->wy[i0], s->wy[i1], tu);
		float nxx = us_lerp(s->nx[i0], s->nx[i1], tu), nyy = us_lerp(s->ny[i0], s->ny[i1], tu);
		float tx = px + wxx * vv * wsc + nxx * nn * s->thick;
		float ty = py + wyy * vv * wsc + nyy * nn * s->thick;
		float tz = pz + us_lerp(s->wz[i0], s->wz[i1], tu) * vv * wsc;

		float tvx = 0, tvy = 0;
		if (b->ptx > -1e8f && !wrapped) {
			tvx = (tx - b->ptx) * idt;
			tvy = (ty - b->pty) * idt;
			float tv = sqrtf(tvx * tvx + tvy * tvy);
			if (tv > 1.5f)
				tvx *= 1.5f / tv, tvy *= 1.5f / tv;
		}
		b->ptx = tx;
		b->pty = ty;
		if (b->snap) {
			b->x = tx, b->y = ty, b->vx = b->vy = 0, b->snap = 0;
			b->hx = s->sx[i1] - s->sx[i0], b->hy = s->sy[i1] - s->sy[i0];
			float hl = sqrtf(b->hx * b->hx + b->hy * b->hy) + 1e-6f;
			b->hx /= hl, b->hy /= hl;
		} else if (wrapped && b->life > 0.5f) {
			/* re-entering at the far end of the ribbon: the end fades hide the hop */
			b->x = tx, b->y = ty, b->vx = tvx, b->vy = tvy;
		}

		/* falcon: flash expansion away from it */
		if (st->ft > 0) {
			float dx = b->x - st->fx, dy = b->y - st->fy;
			float d2 = dx * dx + dy * dy;
			const float RF = 0.07f;
			if (d2 < RF * RF) {
				float d = sqrtf(d2) + 1e-4f;
				float k = (1.0f - d / RF) * 4.0f * dt * 10.0f;
				b->vx += dx / d * k * 0.12f;
				b->vy += dy / d * k * 0.12f;
				b->panic = 1.0f;
			}
		}

		float ax, ay;
		if (b->leaving) {
			ax = (b->x < A * 0.5f ? -0.6f : 0.6f);
			ay = -0.15f;
			b->life -= dt * 0.5f;
		} else {
			float K = 16.0f * (1.0f - 0.93f * b->panic) + 0.6f;
			float D = 2.0f * sqrtf(K);
			ax = K * (tx - b->x) + D * (tvx - b->vx);
			ay = K * (ty - b->y) + D * (tvy - b->vy);
			float am = sqrtf(ax * ax + ay * ay);
			float amax = 2.5f + 10.0f * (1.0f - b->panic) * us_sat(b->life);
			if (am > amax)
				ax *= amax / am, ay *= amax / am;
			b->life = us_approach(b->life, 1.0f, 0.6f, dt);
		}
		b->panic = us_approach(b->panic, 0, 1.3f, dt);
		b->vx += ax * dt;
		b->vy += ay * dt;
		float spd = sqrtf(b->vx * b->vx + b->vy * b->vy);
		if (spd > 1.6f)
			b->vx *= 1.6f / spd, b->vy *= 1.6f / spd, spd = 1.6f;
		b->x += b->vx * dt;
		b->y += b->vy * dt;
		b->z = tz;
		if (b->leaving && (b->life <= 0 || b->x < -0.06f || b->x > A + 0.06f || b->y < -0.06f)) {
			b->alive = 0;
			continue;
		}

		/* heading: actual flight direction, smoothed; a stalled bird keeps its last heading */
		if (spd > 0.01f) {
			float k = 1.0f - expf(-dt / 0.12f);
			b->hx += (b->vx / spd - b->hx) * k;
			b->hy += (b->vy / spd - b->hy) * k;
			float hl = sqrtf(b->hx * b->hx + b->hy * b->hy) + 1e-5f;
			b->hx /= hl, b->hy /= hl;
		}

		/* draw */
		if (b->y > HORIZON + 0.004f)
			continue; /* dropped into the reeds */
		float endf = b->leaving ? 1.0f : us_smooth(0.0f, 0.05f, b->u) * us_smooth(1.0f, 0.95f, b->u);
		float depth = us_clampf(1.0f + 1.2f * b->z, 0.55f, 1.4f);
		float flick = 0.8f + 0.2f * sinf(t * 13.0f * b->fq + b->ph);
		float v = bv * depth * us_sat(b->life) * endf * (1.0f + 1.6f * band + 0.6f * b->panic) * flick;
		float L = 0.003f * depth * (1.0f + 0.9f * band + 0.6f * b->panic);
		/* true motion blur instead of trail ghosts: a fast bird stretches along its path, dimmer per sample */
		float mb = spd * dt * 1.1f;
		if (mb > L) {
			if (mb > 0.008f)
				mb = 0.008f;
			v *= 0.55f + 0.45f * L / mb;
			L = mb;
		}
		dash(c, b->x, b->y, b->hx, b->hy, L, v, US_MAT_ACCENT);
		{
			int gx = (int)(b->x / A * MGX), gy = (int)(b->y * MGY);
			if (gx >= 0 && gy >= 0 && gx < MGX && gy < MGY)
				st->mg[gy * MGX + gx] += v;
		}
	}
	c->blend = US_BLEND_MAX;

	/* ---- the flock's halo takes the flock's hue (Ink bloom keeps per-pixel material) ---- */
	if (!ctx->crisp) {
		/* next frame's hatching parts around where the flock is now */
		for (int i = 0; i < MGX * MGY; i++) {
			int gx = i % MGX, gy = i / MGX;
			float sum = 0;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++) {
					int xx = gx + dx, yy = gy + dy;
					if (xx >= 0 && yy >= 0 && xx < MGX && yy < MGY)
						sum += st->mg[yy * MGX + xx] * (dx || dy ? 0.6f : 1.0f);
				}
			st->mcov[i] = us_sat(sum * 0.35f);
		}
		tint_weight(st->mg, st->mwt, MGX, MGY, 2, 0.8f);
		tint_soft(c, st->mwt, MGX, MGY, US_MAT_ACCENT, 24,
			   (1u << US_MAT_BG) | (1u << US_MAT_BLOOM) | (1u << US_MAT_LIGHT));
	}

	/* ---- falcon ---- */
	if (st->ft > 0) {
		st->ft -= dt;
		st->fvy -= dt * 0.15f; /* pulls up out of the stoop */
		st->fx += st->fvx * dt;
		st->fy += st->fvy * dt;
		float spd = sqrtf(st->fvx * st->fvx + st->fvy * st->fvy) + 1e-5f;
		float hx = st->fvx / spd, hy = st->fvy / spd;
		float L = 0.016f, fx = st->fx, fy = st->fy;
		float fv = 0.9f * us_sat(st->ft * 2.0f) * us_sat((1.6f - st->ft) * 6.0f);
		float w1x = fx - hx * L + -hy * L * 0.6f, w1y = fy - hy * L + hx * L * 0.6f;
		float w2x = fx - hx * L - -hy * L * 0.6f, w2y = fy - hy * L - hx * L * 0.6f;
		if (ctx->crisp) {
			us_tri(c, fx, fy, w1x, w1y, fx - hx * L * 0.5f, fy - hy * L * 0.5f, fv, US_MAT_BLOOM);
			us_tri(c, fx, fy, w2x, w2y, fx - hx * L * 0.5f, fy - hy * L * 0.5f, fv, US_MAT_BLOOM);
		} else {
			us_line(c, fx, fy, w1x, w1y, 0.0018f, fv, US_MAT_BLOOM);
			us_line(c, fx, fy, w2x, w2y, 0.0018f, fv, US_MAT_BLOOM);
			us_line(c, fx, fy, fx - hx * L * 1.3f, fy - hy * L * 1.3f, 0.0016f, fv, US_MAT_BLOOM);
		}
	}

	/* ---- reed bed in front ---- */
	{
		int nr = (int)(NREEDS * (0.5f + 0.5f * dens));
		for (int k = 0; k < nr; k++) {
			float x0 = st->reed_x[k] * A, h = st->reed_h[k];
			float sw = (us_noise2(x0 * 1.3f - st->wind_t * 0.8f, st->reed_ph[k]) * 0.6f + 0.3f) * (0.15f + 0.5f * wind) +
				   0.25f * us_noise2(st->reed_ph[k], 0.3f);
			float base = 1.01f, top = 0.96f - h * 1.3f;
			float pts[8];
			for (int q = 0; q < 4; q++) {
				float u = q / 3.0f;
				pts[2 * q] = x0 + sw * h * u * u * 2.5f;
				pts[2 * q + 1] = base - (base - top) * u;
			}
			for (int q = 0; q < 3; q++)
				wline(c, pts[2 * q], pts[2 * q + 1], pts[2 * q + 2], pts[2 * q + 3], 0.24f + 0.14f * (h / 0.12f),
				      ctx->crisp ? US_MAT_EARTH : US_MAT_LEAF);
			if (k % 3 == 0) {
				float px = pts[6], py = pts[7];
				for (int q = 0; q < 4; q++) {
					float aa = -1.2f + 0.45f * q + sw * 2.0f;
					float ll = 0.008f + 0.002f * q;
					wline(c, px, py, px + sinf(aa) * ll * 0.6f, py - cosf(aa) * ll * 0.4f + ll * 0.5f, 0.26f,
					      US_MAT_EARTH);
				}
			}
		}
	}
}

const us_scene_vt us_scene_murmuration = {"murmuration", "Murmuration", 0.0f, create, destroy, frame};
