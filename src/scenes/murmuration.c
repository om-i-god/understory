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
 *   swell                        -> how long the ribbon stretches across the sky; the afterglow fans up
 *   level                        -> the light: louder = a bigger, broader, brighter flock, a warmer afterglow
 *                                   and brighter lit clouds; quiet = a dim, slim, calm ribbon
 *   kick                         -> the hawk-strike flinch: the whole flock snaps in toward its heart, rebounds
 *                                   and settles (a damped heartbeat, ~0.2 s), brightening; a ring of squeezed
 *                                   birds races out along the ribbon; sometimes a dark banking band follows
 *   snare / clap                 -> the ribbon tears open at one place, a burst of birds peels off the torn
 *                                   edge and swings back over the next second, and the sheet banks hard;
 *                                   a starling is startled off the wire and the wire thrums
 *   hats                         -> wing glints: a fresh scatter of bright cross-strokes along the flock's thin
 *                                   edges each tick, and a handful of glitter on the sun's path on the mere
 *   big transients               -> a falcon stoops through: a flash-expansion hole, a split, then re-forming
 *   silence                      -> the flock funnels down and pours into the reeds to roost, a trickle cycling
 *   high / air                   -> wind in the reeds and branches (smoothed, as weather); sitters flick wings;
 *                                   air scatters sparkles on the mere
 *   bass                         -> the sheet draws in on itself (narrower, shorter, denser)
 *   sub                          -> slow swells roll across the mere and waver the flock's reflection
 *   lowmid                       -> the church window glows (an organ somewhere inside)
 *   width                        -> the flock splits into two lobes
 * Context: a dusk sky (a soft rose afterglow, first stars overhead, lens-shaped cloud banks engraved in fine
 *          strokes with sun-lit undersides), a pylon line with sagging wires and perched starlings, a hedgerow
 *          and church on the horizon, a mere below it that holds a broken reflection of the flock.
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
#define NPYL 4    /* pylons marching off toward the sun (the first stands just off-screen left) */
#define NPERCH 18 /* starlings sitting out the show on the wires */
#define NGLINT 24 /* sparkles on the mere */
#define NHEDGE 384 /* hedgerow profile samples across the width */
#define NSTAR 60   /* first stars, high up */
#define NBANK 7    /* cloud banks lit from below by the low sun */

/* pylon line: x as a fraction of width, height (normalized); receding, so each is smaller */
static const float PYL_X[NPYL] = {-0.06f, 0.33f, 0.585f, 0.765f};
static const float PYL_H[NPYL] = {0.21f, 0.155f, 0.085f, 0.047f};

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
	/* extra listening: kick density pulse, spectral envelopes */
	float kick_s, kp_t, kp_amp, bass_s, sub_s, hi_s, wid_s, lm_s, air_s;
	/* perched birds + wire thrum */
	float pu[NPERCH], px[NPERCH], py[NPERCH], pvx[NPERCH], pvy[NPERCH], ptim[NPERCH], pflap[NPERCH];
	float pax[NPERCH], pay[NPERCH];
	int pw[NPERCH], pspan[NPERCH], pstate[NPERCH]; /* 0 sitting, 1 flying off, 2 away, 3 landing */
	float pcool;
	float thr[NPYL - 1], thr_ph[NPYL - 1];
	float glint_ph[NGLINT], glint_f[NGLINT];
	float hedge[NHEDGE]; /* hedgerow height above the horizon (0 = none) */
	/* drums: kick = the flock flinches inward (a hawk-strike contraction); snare = the ribbon tears open at
	 * one place and a burst of birds peels off; hats = wing glints flicker along the edges */
	float wind_s, kq, kb, k_age, k_amp, sn_env, sn_age, sn_u, hat_env, light_s;
	uint32_t frame_n;
	uint32_t hat_seed;
	float hx_col[257]; /* sky glow: horizontal sun factor, sampled across the width */
	float star_x[NSTAR], star_y[NSTAR], star_b[NSTAR];
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
		/* coverage split over the 2x2 pixels under the bird: the palette's shade steps still keep it crisp,
		 * but a bird gliding across a pixel edge fades over instead of hopping */
		splat(c, x, y, v, m);
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

/* The afterglow behind everything: a soft vertical gradient that warms toward the horizon, with a gold halo
 * around the low sun. amp = the band's brightness at the horizon, samp = the halo's. Written straight into
 * the canvas (MAX blend, drawn first); Pixel looks get an ordered-dither gradient instead of flat bands. */
static void sky_glow(st_t *st, us_canvas *c, float A, float sun_x, float sun_y, float amp, float samp)
{
	static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
	int py0 = (int)(0.1f * c->s), py1 = (int)(HORIZON * c->s);
	if (py0 < 0)
		py0 = 0;
	if (py1 > c->h)
		py1 = c->h;
	amp = us_clampf(amp, 0.0f, 0.4f);
	samp = us_clampf(samp, 0.0f, 0.4f);
	for (int i = 0; i <= 256; i++) {
		float d = ((float)i / 256.0f * A - sun_x) / 0.5f;
		st->hx_col[i] = expf(-d * d);
	}
	float gsc = 256.0f / (float)(c->w > 0 ? c->w : 1);
	for (int py = py0; py < py1; py++) {
		float y = (py + 0.5f) / c->s, hgt = HORIZON - y;
		float base = amp * expf(-hgt / 0.13f);
		float dy = (y - sun_y) / 0.1f;
		float halo = samp * expf(-dy * dy);
		if (base + halo < 0.004f)
			continue;
		uint8_t *ink = c->ink + (size_t)py * c->w, *mat = c->mat + (size_t)py * c->w;
		const uint8_t *bz = bayer + (py & 3) * 4;
		for (int px = 0; px < c->w; px++) {
			float gx = px * gsc;
			int gi = (int)gx;
			if (gi > 255)
				gi = 255;
			float hv = halo * us_lerp(st->hx_col[gi], st->hx_col[gi + 1], gx - gi);
			float v = base + hv, th = (bz[px & 3] + 0.5f) * (1.0f / 16.0f);
			/* hue by ordered dither over wide zones, so no edge shows: rose low down fading into the night
			 * colour above (Pixel: gold too, in proportion to the sun's share; in Ink a faint gold reads muddy) */
			uint8_t m = c->crisp && hv > (base + hv) * (0.35f + 0.6f * th) ? US_MAT_LIGHT
					: (hgt < 0.06f + 0.22f * th ? US_MAT_BLOOM : US_MAT_BG);
			int iv;
			if (c->crisp) {
				if (v * 4.0f <= th)
					continue;
				iv = 70; /* one quiet palette step, dithered: density carries the gradient */
			} else {
				iv = (int)(v * 255.0f + 0.5f);
			}
			if (iv > ink[px])
				ink[px] = (uint8_t)iv, mat[px] = m;
		}
	}
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
	/* a dense core that thins out to wispy edges; a few stragglers trail beyond the sheet's rim */
	if (us_rand(r) < 0.88f)
		b->v = us_clampf(us_randn(r) * 0.42f, -1.0f, 1.0f);
	else
		b->v = (us_rand(r) < 0.5f ? -1.0f : 1.0f) * us_randr(r, 0.8f, 1.45f);
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

static void perch_place(st_t *st, int k);

static void init_extras(st_t *st)
{
	us_rng *r = &st->rng;
	for (int k = 0; k < NPERCH; k++) {
		perch_place(st, k);
		st->pstate[k] = 0;
		st->pflap[k] = 0;
	}
	for (int k = 0; k < NPYL - 1; k++)
		st->thr_ph[k] = us_rand(r) * US_TAU;
	for (int k = 0; k < NGLINT; k++) {
		st->glint_ph[k] = us_rand(r);
		st->glint_f[k] = us_randr(r, 0.35f, 0.9f);
	}
	st->kp_t = 10.0f;
	for (int k = 0; k < NSTAR; k++) {
		st->star_x[k] = us_rand(r);
		float f = us_rand(r);
		st->star_y[k] = 0.02f + 0.36f * f * f; /* thicker overhead, fading out into the afterglow */
		float b = us_rand(r);
		st->star_b[k] = 0.12f + 0.4f * b * b * b;
	}
	/* a hedgerow in two stretches either side of the low sun, a gap for the churchyard */
	const float hx0[2] = {0.37f, 0.745f}, hx1[2] = {0.6f, 0.875f};
	for (int i = 0; i < NHEDGE; i++) {
		float xf = (i + 0.5f) / NHEDGE, h = 0;
		for (int seg = 0; seg < 2; seg++) {
			float e = us_smooth(hx0[seg], hx0[seg] + 0.012f, xf) * us_smooth(hx1[seg], hx1[seg] - 0.012f, xf);
			if (xf < hx0[seg] || xf > hx1[seg])
				e = 0;
			h += e * (0.002f + 0.009f * us_sat(0.5f + 0.9f * us_noise2(xf * 23.0f, 3.3f)) +
				  0.004f * us_sat(us_noise2(xf * 83.0f, 7.7f)));
		}
		if (xf > 0.4666f && xf < 0.497f)
			h = h > 0 ? 0.002f : 0; /* the churchyard */
		st->hedge[i] = h;
	}
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
	float len_t = (k ? 0.22f + 0.3f * st->sat_frac : 0.5f) + (k ? 0.1f : 0.4f) * st->swell_s + 0.14f * us_noise2(t * 0.031f, 1.3f + ks);
	/* bass draws the flock in on itself: a narrower, shorter, denser sheet */
	float bs = st->bass_s;
	/* loud music opens the flock out: a bigger, broader sheet; quiet draws it into a slim ribbon */
	float wid_t = (0.03f + 0.07f * us_sat(0.5f + 0.7f * us_noise2(t * 0.043f, 4.1f + ks))) * (0.7f + 0.65f * lvl) *
			      (1.0f - 0.3f * bs) + 0.012f;
	len_t *= (0.85f + 0.3f * lvl) * (1.0f - 0.12f * bs);
	s->len = us_approach(s->len, len_t * (1.0f - 0.3f * roost), 2.0f, dt);
	s->wid = us_approach(s->wid, wid_t, 0.9f, dt);
	s->thick = us_approach(s->thick, 0.006f + 0.006f * lvl, 1.0f, dt);

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


/* ---- context: pylon line, wires, perched birds, hamlet, mere -------- */

/* where wire `wire` (0 / 1 = crossarm tips, 2 = earth wire on the peak) leaves pylon p */
static void wire_anchor(int p, int wire, float A, float *x, float *y)
{
	float h = PYL_H[p], X = PYL_X[p] * A;
	if (wire == 2) {
		*x = X, *y = HORIZON - h;
		return;
	}
	*x = X + (wire ? 0.27f : -0.27f) * h;
	*y = HORIZON - 0.80f * h + 0.05f * h; /* hangs below its insulator */
}

/* a point on a span (pylon span -> span+1), u in 0..1: parabolic sag, wind sway, plucked thrum */
static void wire_pt(const st_t *st, int span, int wire, float u, float A, float t, float wind, float *x, float *y)
{
	float ax, ay, bx, by;
	wire_anchor(span, wire, A, &ax, &ay);
	wire_anchor(span + 1, wire, A, &bx, &by);
	float sag = 0.16f * (PYL_H[span] + PYL_H[span + 1]) * 0.5f * (wire == 2 ? 0.8f : 1.0f);
	float bell = sinf(US_PI * u);
	*x = us_lerp(ax, bx, u);
	*y = us_lerp(ay, by, u) + sag * 4.0f * u * (1.0f - u) +
	     bell * (0.0015f * wind * us_noise2(t * 0.5f, span * 3.0f + wire) +
		     st->thr[span] * (wire == 1 ? 1.0f : 0.6f) * sinf(t * 19.0f + st->thr_ph[span] + wire));
}

static void draw_pylon(us_canvas *c, int p, float A, float v)
{
	float h = PYL_H[p], X = PYL_X[p] * A, H = HORIZON;
	if (X + 0.3f * h < 0)
		return;
	/* the lattice: legs taper to a waist, then a peak; zig-zag bracing between them */
	float lx[4] = {-0.17f, -0.05f, -0.035f, 0.0f}, ly[4] = {0.0f, 0.62f, 0.80f, 1.0f};
	for (int s = -1; s <= 1; s += 2)
		for (int k = 0; k < 3; k++)
			wline(c, X + s * lx[k] * h, H - ly[k] * h, X + s * lx[k + 1] * h, H - ly[k + 1] * h, v, US_MAT_EARTH);
	int nb = h > 0.1f ? 5 : 3;
	for (int k = 0; k < nb; k++) {
		float f0 = (float)k / nb, f1 = (float)(k + 1) / nb;
		float y0 = H - 0.62f * h * f0, y1 = H - 0.62f * h * f1;
		float w0 = us_lerp(0.17f, 0.05f, f0) * h, w1 = us_lerp(0.17f, 0.05f, f1) * h;
		float sg = (k & 1) ? 1.0f : -1.0f;
		wline(c, X - sg * w0, y0, X + sg * w1, y1, v * 0.7f, US_MAT_EARTH);
		if (c->crisp && h < 0.1f)
			continue;
		wline(c, X + sg * w0, y0, X - sg * w1, y1, v * 0.45f, US_MAT_EARTH);
	}
	/* crossarm + its underside struts + hanging insulators */
	float ya = H - 0.80f * h;
	wline(c, X - 0.27f * h, ya, X + 0.27f * h, ya, v, US_MAT_EARTH);
	wline(c, X - 0.27f * h, ya, X - 0.04f * h, ya + 0.09f * h, v * 0.6f, US_MAT_EARTH);
	wline(c, X + 0.27f * h, ya, X + 0.04f * h, ya + 0.09f * h, v * 0.6f, US_MAT_EARTH);
	if (!c->crisp || h > 0.1f) {
		wline(c, X - 0.27f * h, ya, X - 0.27f * h, ya + 0.05f * h, v, US_MAT_EARTH);
		wline(c, X + 0.27f * h, ya, X + 0.27f * h, ya + 0.05f * h, v, US_MAT_EARTH);
	}
}

/* a starling sitting on a wire (x, y = its feet); flap opens its wings */
static void draw_perched(us_canvas *c, float x, float y, float flap, float v)
{
	if (c->crisp) {
		us_put(c, (int)(x * c->s), (int)(y * c->s) - 1, v, US_MAT_ACCENT);
		if (flap > 0.3f) {
			us_put(c, (int)(x * c->s) - 1, (int)(y * c->s) - 2, v * 0.8f, US_MAT_ACCENT);
			us_put(c, (int)(x * c->s) + 1, (int)(y * c->s) - 2, v * 0.8f, US_MAT_ACCENT);
		}
		return;
	}
	wline(c, x, y - 0.0008f, x + 0.0008f, y - 0.0058f, v, US_MAT_ACCENT);               /* body */
	wline(c, x + 0.0004f, y - 0.0012f, x + 0.0012f, y - 0.0054f, v * 0.7f, US_MAT_ACCENT); /* breast */
	splat(c, x + 0.0012f, y - 0.0066f, v, US_MAT_ACCENT);                                   /* head */
	wline(c, x - 0.0002f, y - 0.0012f, x - 0.0016f, y + 0.0016f, v * 0.6f, US_MAT_ACCENT);  /* tail */
	if (flap > 0.05f) {
		float s = 0.0045f * flap;
		wline(c, x + 0.0003f, y - 0.0036f, x - s, y - 0.0036f - s * 0.9f, v * flap, US_MAT_ACCENT);
		wline(c, x + 0.0003f, y - 0.0036f, x + s * 1.1f, y - 0.0036f - s * 0.8f, v * flap, US_MAT_ACCENT);
	}
}

/* a flying starling: a body dash and two wing strokes that beat */
static void draw_flyer(us_canvas *c, float x, float y, float hx, float hy, float beat, float v)
{
	if (c->crisp) {
		us_put(c, (int)(x * c->s), (int)(y * c->s), v, US_MAT_ACCENT);
		return;
	}
	float L = 0.0035f, wy = 0.004f * beat;
	wline(c, x - hx * L, y - hy * L, x + hx * L, y + hy * L, v, US_MAT_ACCENT);
	wline(c, x, y, x - hy * 0.004f - hx * 0.002f, y + hx * 0.004f - wy, v * 0.8f, US_MAT_ACCENT);
	wline(c, x, y, x + hy * 0.004f - hx * 0.002f, y - hx * 0.004f - wy, v * 0.8f, US_MAT_ACCENT);
}

static inline uint32_t hash2(uint32_t a, uint32_t b)
{
	uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu;
	h ^= h >> 15;
	h *= 0x2c1b3c6du;
	h ^= h >> 12;
	return h;
}

static void perch_place(st_t *st, int k)
{
	us_rng *r = &st->rng;
	/* small groups: most sit on the near span, some on the next, a few on the far one */
	st->pspan[k] = k < 6 ? 0 : (k < 15 ? 1 : 2);
	st->pw[k] = us_rand(r) < 0.75f ? (us_rand(r) < 0.5f ? 0 : 1) : 2;
	float grp = (float)((k * 7) % 5) / 5.0f;
	st->pu[k] = us_clampf(0.15f + 0.7f * grp + us_randr(r, -0.06f, 0.06f), 0.1f, 0.9f);
	if (st->pspan[k] == 0)
		st->pu[k] = 0.35f + 0.6f * st->pu[k]; /* the visible part of the off-screen span */
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
		init_extras(st);
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
	/* wind in the reeds and branches follows the top end, but as weather: smoothed over most of a second */
	st->wind_s = us_approach(st->wind_s, us_sat(0.2f * w->wind + 0.7f * a->high + 0.3f * a->air), 0.9f, rdt);
	float wind = st->wind_s;
	st->wind_t += dt * (0.15f + 0.6f * wind);
	st->twist_kick = 0;
	{
		/* fast envelopes: quick attack, eased release (no strobing) */
		float hi_in = us_sat(0.75f * a->high + 0.35f * a->air);
		st->hi_s = us_approach(st->hi_s, hi_in, hi_in > st->hi_s ? 0.04f : 0.3f, rdt);
		st->air_s = us_approach(st->air_s, us_sat(a->air), a->air > st->air_s ? 0.08f : 0.5f, rdt);
		st->kick_s = us_approach(st->kick_s, us_sat(a->kick), a->kick > st->kick_s ? 0.03f : 0.25f, rdt);
		st->bass_s = us_approach(st->bass_s, us_sat(a->bass), 0.35f, rdt);
		st->sub_s = us_approach(st->sub_s, us_sat(a->sub), 0.6f, rdt);
		st->lm_s = us_approach(st->lm_s, us_sat(a->lowmid), 0.5f, rdt);
		st->wid_s = us_approach(st->wid_s, us_sat(a->width), 1.5f, rdt);
		/* kick: a density pulse rings out from the heart of the flock */
		if (a->kick_hit && st->kp_t > 0.2f) {
			st->kp_t = 0;
			st->kp_amp = 0.55f + 0.45f * us_sat(a->kick);
		}
		st->kp_t += dt;
		st->kp_amp *= expf(-dt * 3.0f);
		/* kick: the whole flock flinches in on itself, then breathes back out */
		if (a->kick_hit)
			st->k_age = 0, st->k_amp = 0.65f + 0.35f * us_sat(a->kick);
		else
			st->k_age += rdt;
		if (st->k_age > 1.0f)
			st->k_age = 1.0f, st->k_amp = 0;
		/* a damped heartbeat: in hard on the hit, a rebound outward, settled within a quarter second */
		st->kq = st->k_amp * expf(-st->k_age / 0.12f) * cosf(US_TAU * 6.0f * st->k_age);
		st->kb = st->k_amp * expf(-st->k_age / 0.12f);
		/* snare: the ribbon tears open somewhere along its length */
		if (a->snare_hit) {
			st->sn_age = 0;
			st->sn_u = us_randr(r, 0.22f, 0.78f);
			st->twist_kick += 0.6f * (us_rand(r) < 0.5f ? 1.0f : -1.0f); /* and the sheet banks hard */
		}
		else
			st->sn_age = fminf(st->sn_age + rdt, 2.0f);
		/* the tear opens over two frames and heals over a few hundred ms */
		st->sn_env = st->sn_age >= 2.0f ? 0.0f : us_sat(0.45f + st->sn_age / 0.05f) * expf(-st->sn_age / 0.25f);
		/* hats: a fresh scatter of wing glints for every tick */
		if (a->hat_hit) {
			st->hat_env = 1.0f;
			st->hat_seed = us_rng_u32(r);
		}
		st->hat_env *= expf(-rdt / 0.07f);
		st->frame_n++;
		/* loudness lights the sky: quick to brighten, slow to fade */
		float l_in = us_sat(a->level);
		st->light_s = us_approach(st->light_s, l_in, l_in > st->light_s ? 0.35f : 1.2f, rdt);
	}

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
	if (a->kick_hit && st->wcool <= 0 && us_rand(r) < 0.6f)
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
		/* a wide stereo image splits the flock into two lobes */
		float sat_t = 0.3f * us_smooth(0.0f, 0.6f, us_noise2(t * 0.021f, 31.0f)) + 0.28f * st->sat_boost +
			      0.3f * us_smooth(0.35f, 0.85f, st->wid_s);
		if (sat_t > 0.5f)
			sat_t = 0.5f;
		st->sat_frac = us_approach(st->sat_frac, sat_t * (1.0f - roost), 1.5f, dt);
	}

	/* ---- the sheets ---- */
	float fun_x = A * (0.3f + 0.08f * us_noise2(t * 0.02f, 40.0f));
	{
		/* the flock tours the whole sky on a slow swing (never parks at one side); melody leans it */
		st->wp += dt * (0.21f + 0.2f * st->mid_s);
		float bias = us_clampf((st->cent_s - st->cent_mean) * 4.0f, -1.0f, 1.0f);
		float tx = A * (0.5f + 0.21f * sinf(st->wp) + 0.08f * bias) + 0.07f * A * us_noise2(t * 0.027f, 1.7f);
		/* brighter melody lifts the flock, darker sinks it toward the reeds */
		float ty = 0.35f + 0.06f * sinf(st->wp * 0.71f + 1.3f) + 0.04f * us_noise2(t * 0.037f, 7.3f) -
			   0.05f * st->lvl_s - 0.09f * bias;
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
		float swl = st->swell_s, L = st->light_s;
		/* the afterglow: a soft gradient, rose low down and gold around the sun; loudness lights it up */
		sky_glow(st, c, A, sun_x, sun_y, (0.018f + 0.05f * L + 0.015f * swl) * glow, (0.02f + 0.07f * L + 0.015f * swl) * glow);
		/* first stars overhead, paling as the afterglow brightens */
		{
			float sv = (1.0f - 0.5f * day) * (1.0f - 0.45f * L);
			for (int k = 0; k < NSTAR; k++) {
				float v = st->star_b[k] * sv * (0.8f + 0.2f * sinf(t * (0.5f + st->star_b[k]) + k * 2.1f));
				float x = st->star_x[k] * A, y = st->star_y[k];
				if (ctx->crisp) {
					if (st->star_b[k] > 0.3f)
						us_put(c, (int)(x * c->s), (int)(y * c->s), 0.3f + 0.4f * v, US_MAT_LIGHT);
				} else {
					splat(c, x, y, v, US_MAT_LIGHT);
				}
			}
		}
		/* cloud banks: stacked fine strokes in lens shapes, their undersides lit by the low sun */
		for (int b = 0; b < NBANK; b++) {
			float fb = (b + 0.5f) / NBANK; /* 0 = low .. 1 = high */
			float yb = HORIZON - 0.045f - 0.52f * powf(fb, 1.25f) + 0.012f * us_noise2(b * 3.1f, t * 0.01f);
			int nl = ctx->crisp ? 3 : 6 + (b & 1);
			float sp = ctx->crisp ? 2.0f / c->s : 0.0036f + 0.0022f * fb;
			float drift = t * (0.0035f + 0.003f * fb) * ((b & 1) ? 1.0f : -0.7f);
			float fq = 1.4f + 1.6f * fb, lo = 1.0f - fb;
			/* how much of this bank there is breathes slowly */
			float cover = 0.12f * us_noise2(t * 0.013f, b * 5.3f + 1.0f) + 0.08f * fb - 0.05f;
			float step = ctx->crisp ? 1.0f / c->s : 0.012f;
			for (int j = 0; j < nl; j++) {
				float fj = (float)j / (nl - 1); /* 0 = top stroke .. 1 = the lit underside */
				float y = yb + (j - 0.5f * (nl - 1)) * sp;
				float thr = cover + 0.32f * fabsf(fj - 0.55f) * 2.0f;
				float shear = (fj - 0.5f) * 0.05f;
				float base = ctx->crisp ? 0.24f + 0.22f * fj * fj
						      : (0.1f + 0.12f * lo) * (0.45f + 0.9f * fj * fj) * (0.75f + 0.6f * L) * glow;
				float x = 0;
				while (x < A) {
					float x0 = x;
					while (x < A && us_noise2((x + shear) * fq - drift * 12.0f, b * 1.37f + 5.0f) > thr)
						x += step;
					if (x > x0 + 0.004f) {
						float xc = 0.5f * (x0 + x), d = (xc - sun_x) / 0.45f;
						float near = expf(-d * d) * (0.35f + 0.65f * lo) * (0.3f + 0.7f * fj);
						uint8_t m;
						if (ctx->crisp)
							m = near > 0.4f && fj > 0.5f ? US_MAT_LIGHT : US_MAT_BG;
						else
							m = near > 0.42f ? US_MAT_LIGHT : (fj > 0.6f && fb < 0.65f ? US_MAT_BLOOM : US_MAT_WATER);
						float vv = base * (1.0f + (ctx->crisp ? 0.4f : 1.6f) * near);
						hline_sky(c, x0, x, y, vv, m, st->mcov, ctx->crisp && fj < 0.5f ? 2 : 1);
					}
					x += step;
				}
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
			float hw = sun_r * (1.4f + 0.8f * us_noise2(k * 1.3f, t * 0.02f)) + (0.05f + 0.05f * st->swell_s) * k;
			float xo = 0.03f * us_noise2(k * 2.1f, t * 0.015f);
			hline(c, sun_x - hw + xo, sun_x + hw * 0.7f + xo, yy, (0.14f + 0.1f * day + 0.1f * st->swell_s) * glow,
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

	/* ---- distant hamlet: a hedgerow and a church on the horizon, its window lit by the low-mids ---- */
	{
		float chx = 0.47f * A;
		for (int i = 1; i < NHEDGE; i++) {
			float h0 = st->hedge[i - 1], h1 = st->hedge[i];
			if (h0 <= 0 && h1 <= 0)
				continue;
			float x0 = (i - 0.5f) / NHEDGE * A, x1 = (i + 0.5f) / NHEDGE * A;
			if (ctx->crisp)
				us_put(c, (int)(x1 * c->s), (int)((HORIZON - h1) * c->s), 0.26f, US_MAT_STEM);
			else
				wline(c, x0, HORIZON - h0, x1, HORIZON - h1, 0.26f, US_MAT_STEM);
		}
		float tw = 0.011f, th = 0.034f, H = HORIZON;
		wline(c, chx, H, chx, H - th, 0.24f, US_MAT_EARTH);
		wline(c, chx + tw, H, chx + tw, H - th, 0.24f, US_MAT_EARTH);
		wline(c, chx, H - th, chx + tw, H - th, 0.24f, US_MAT_EARTH);
		wline(c, chx, H - th, chx + tw * 0.5f, H - th - 0.026f, 0.24f, US_MAT_EARTH);
		wline(c, chx + tw, H - th, chx + tw * 0.5f, H - th - 0.026f, 0.24f, US_MAT_EARTH);
		wline(c, chx + tw * 0.5f, H - th - 0.026f, chx + tw * 0.5f, H - th - 0.032f, 0.2f, US_MAT_EARTH);
		wline(c, chx + tw, H - 0.017f, chx + tw + 0.038f, H - 0.017f, 0.22f, US_MAT_EARTH); /* nave roof */
		wline(c, chx + tw + 0.038f, H - 0.017f, chx + tw + 0.038f, H, 0.22f, US_MAT_EARTH);
		/* belfry slit + a warm window that glows with the low-mids (an organ somewhere inside) */
		float wv = 0.12f + 0.6f * st->lm_s;
		if (ctx->crisp) {
			us_put(c, (int)((chx + tw * 0.5f) * c->s), (int)((H - th * 0.75f) * c->s), 0.3f, US_MAT_LIGHT);
			us_put(c, (int)((chx + tw + 0.019f) * c->s), (int)((H - 0.008f) * c->s), wv, US_MAT_BLOOM);
		} else {
			wline(c, chx + tw * 0.5f, H - th * 0.82f, chx + tw * 0.5f, H - th * 0.66f, 0.3f, US_MAT_LIGHT);
			for (int k = 0; k < 3; k++)
				wline(c, chx + tw + 0.01f + 0.009f * k, H - 0.011f, chx + tw + 0.01f + 0.009f * k, H - 0.005f,
				      wv * (k == 1 ? 1.0f : 0.7f), US_MAT_BLOOM);
		}
	}

	/* ---- the pylon line, its wires, and the starlings sitting on them ---- */
	{
		float pv = ctx->crisp ? 0.42f : 0.3f;
		for (int p = 0; p < NPYL; p++)
			draw_pylon(c, p, A, pv * (p == 0 ? 1.0f : 1.0f - 0.12f * p));
		for (int sp = 0; sp < NPYL - 1; sp++) {
			st->thr[sp] *= expf(-dt * 2.5f);
			int nseg = ctx->crisp ? 8 : 24;
			for (int wi = 0; wi < 3; wi++) {
				float px = 0, py = 0;
				for (int q = 0; q <= nseg; q++) {
					float x, y;
					wire_pt(st, sp, wi, (float)q / nseg, A, t, wind, &x, &y);
					if (q)
						wline(c, px, py, x, y, ctx->crisp ? (wi == 2 ? 0.3f : 0.4f) : (wi == 2 ? 0.16f : 0.22f),
						      ctx->crisp ? US_MAT_BG : US_MAT_EARTH);
					px = x, py = y;
				}
			}
		}
		/* sharp transients (claps, snares, hats on a hit) startle one off the wire to join the flock */
		st->pcool -= rdt;
		if (a->snare_hit && st->pcool <= 0 && roost < 0.3f && us_rand(r) < 0.55f) {
			for (int tries = 0; tries < 6; tries++) {
				int k = (int)(us_rand(r) * NPERCH);
				if (k < NPERCH && st->pstate[k] == 0) {
					st->pstate[k] = 1;
					st->ptim[k] = 0;
					st->pvx[k] = us_randr(r, -0.1f, 0.1f);
					st->pvy[k] = -us_randr(r, 0.12f, 0.2f);
					st->thr[st->pspan[k]] += 0.0035f;
					st->pcool = us_randr(r, 0.5f, 1.4f);
					break;
				}
			}
		}
		float fbx = st->sh[0].cx * A, fby = st->sh[0].cy;
		for (int k = 0; k < NPERCH; k++) {
			int sp = st->pspan[k];
			float wx, wy;
			wire_pt(st, sp, st->pw[k], st->pu[k], A, t, wind, &wx, &wy);
			/* hats ruffle the sitters: a wing-flick here and there */
			if (st->pstate[k] == 0 && us_rand(r) < dt * (0.04f + 2.2f * st->hi_s * st->hi_s))
				st->pflap[k] = 1.0f;
			st->pflap[k] = us_approach(st->pflap[k], 0, 0.18f, dt);
			float pv2 = ctx->crisp ? 0.6f : 0.55f;
			switch (st->pstate[k]) {
			case 0:
				draw_perched(c, wx, wy, st->pflap[k], pv2);
				break;
			case 1: { /* flying off toward the flock */
				st->ptim[k] += dt;
				float dx = fbx - st->px[k], dy = fby - st->py[k];
				if (st->ptim[k] < dt * 1.5f)
					st->px[k] = wx, st->py[k] = wy - 0.004f, dx = fbx - wx, dy = fby - wy;
				float dl = sqrtf(dx * dx + dy * dy) + 1e-4f;
				st->pvx[k] += (dx / dl * 0.35f - st->pvx[k]) * (1.0f - expf(-dt * 1.5f));
				st->pvy[k] += (dy / dl * 0.35f - st->pvy[k]) * (1.0f - expf(-dt * 1.5f));
				st->px[k] += st->pvx[k] * dt;
				st->py[k] += st->pvy[k] * dt;
				float sp2 = sqrtf(st->pvx[k] * st->pvx[k] + st->pvy[k] * st->pvy[k]) + 1e-5f;
				float fade = us_sat(1.0f - (st->ptim[k] - 1.8f) / 1.0f);
				draw_flyer(c, st->px[k], st->py[k], st->pvx[k] / sp2, st->pvy[k] / sp2,
					   sinf(st->ptim[k] * 38.0f), pv2 * fade);
				if (st->ptim[k] > 2.8f || dl < 0.03f)
					st->pstate[k] = 2, st->ptim[k] = us_randr(r, 3.0f, 10.0f);
				break;
			}
			case 2: /* away with the flock; a while later another one drops back in */
				st->ptim[k] -= dt;
				if (st->ptim[k] <= 0) {
					perch_place(st, k);
					wire_pt(st, st->pspan[k], st->pw[k], st->pu[k], A, t, wind, &wx, &wy);
					st->pax[k] = wx + us_randr(r, -0.25f, 0.25f);
					st->pay[k] = wy - us_randr(r, 0.15f, 0.25f);
					st->pstate[k] = 3, st->ptim[k] = 0;
				}
				break;
			case 3: { /* gliding down to land, braking with a flutter */
				st->ptim[k] += dt / 1.8f;
				float e = us_smooth(0.0f, 1.0f, st->ptim[k]);
				float x = us_lerp(st->pax[k], wx, e), y = us_lerp(st->pay[k], wy - 0.004f, e) +
								      0.03f * sinf(US_PI * e) * (1.0f - e);
				float hx = wx - st->pax[k], hy = wy - st->pay[k];
				float hl = sqrtf(hx * hx + hy * hy) + 1e-5f;
				draw_flyer(c, x, y, hx / hl, hy / hl, sinf(st->ptim[k] * (20.0f + 40.0f * e)), pv2 * us_sat(st->ptim[k] * 4.0f));
				if (st->ptim[k] >= 1.0f) {
					st->pstate[k] = 0;
					st->pflap[k] = 1.0f;
					st->thr[sp] += 0.0025f;
				}
				break;
			}
			}
		}
	}

	/* ---- the mere below the horizon: sub-bass rolls slow swells, air scatters sparkles ---- */
	{
		int nrip = ctx->crisp ? 5 : 9;
		for (int k = 0; k < nrip; k++) {
			float f = (k + 0.5f) / nrip;
			float y = HORIZON + 0.006f + 0.085f * powf(f, 1.4f);
			float v = (ctx->crisp ? 0.3f : 0.13f) * (1.0f - 0.4f * f) * glow;
			float x = 0, stp = ctx->crisp ? 0.04f : 0.028f, dr = t * (0.01f + 0.02f * st->sub_s);
			while (x < A) {
				float x0 = x;
				while (x < A && us_noise2(x * (3.0f + 2.0f * f) - dr * 9.0f, 20.0f + k * 0.77f) > -0.05f)
					x += stp;
				if (x > x0 + 0.001f) {
					float xc = 0.5f * (x0 + x);
					float yy = y + (0.0015f + 0.005f * st->sub_s) * (0.4f + f) * sinf(t * 1.1f + k * 0.9f + xc * 4.0f);
					hline_sky(c, x0, x, yy, v, US_MAT_WATER, NULL, ctx->crisp ? 2 : 1);
				}
				x += stp;
			}
		}
		float ga = 0.06f + 0.8f * st->air_s;
		for (int k = 0; k < NGLINT; k++) {
			float ph = t * st->glint_f[k] + st->glint_ph[k];
			float cyc = floorf(ph), fr = ph - cyc;
			uint32_t h = hash2((uint32_t)k, (uint32_t)(int)cyc);
			float gx = (h & 0xffffu) / 65536.0f * A;
			float gy = HORIZON + 0.004f + ((h >> 16) & 0xffu) / 255.0f * 0.075f;
			float sb = sinf(US_PI * fr);
			float gv = ga * sb * sb * sb * sb * (ctx->crisp ? 0.8f : 0.6f);
			if (gv < (ctx->crisp ? 0.15f : 0.02f))
				continue;
			float gl = 0.002f + 0.003f * sb;
			hline(c, gx - gl, gx + gl, gy, gv, US_MAT_LIGHT);
		}
		/* hats: each tick throws a fresh handful of glitter across the sun's path on the water */
		if (st->hat_env > 0.1f) {
			int ng = ctx->crisp ? 10 : 18;
			for (int k = 0; k < ng; k++) {
				uint32_t h = hash2((uint32_t)k + 977u, st->hat_seed);
				float u1 = (h & 0xffffu) / 65536.0f, u2 = ((h >> 16) & 0xffu) / 255.0f, u3 = (h >> 24) / 255.0f;
				float gy = HORIZON + 0.003f + 0.07f * u2 * u2;
				/* clustered under the sun, widening toward the viewer */
				float gx = sun_x + (u1 - 0.5f) * (0.25f + 1.2f * u2);
				float gv = st->hat_env * (0.45f + 0.4f * u3) * (1.0f - 0.4f * u2);
				float gl = 0.003f + 0.008f * u3 * (0.5f + u2);
				wline(c, gx - gl, gy, gx + gl, gy, gv, US_MAT_LIGHT);
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
	/* louder = a brighter flock (quiet keeps it a dim, calm ribbon, never gone) */
	float bv = (ctx->crisp ? 0.28f : 0.75f) * (0.62f + 0.6f * st->lvl_s);
	float idt = dt > 1e-4f ? 1.0f / dt : 0.0f;
	/* kick flinch: every bird is drawn pulled toward the heart of its sheet, then released */
	float sq = (ctx->crisp ? 0.36f : 0.2f) * st->kq * (1.0f - roost);
	/* Pixel birds hop whole pixels as they fly, so the drums answer a touch harder there to read as clearly */
	float tear = ctx->crisp ? 1.8f : 1.0f;
	float fcx[NSHEET], fcy[NSHEET];
	for (int k = 0; k < NSHEET; k++)
		fcx[k] = st->sh[k].sx[NS / 2], fcy[k] = st->sh[k].sy[NS / 2];
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
		/* kick pulse: a ring of squeezed, brightened birds running out from the middle of the sheet */
		float kp = 0;
		if (st->kp_amp > 0.02f && b->sheet == 0) {
			float d = (st->kp_t * 2.4f - fabsf(b->u - 0.5f)) * (1.0f / 0.08f);
			if (d > -2.5f && d < 2.5f)
				kp = st->kp_amp * expf(-d * d);
		}
		float vv = (b->v + 0.07f * sinf(t * 0.7f * b->fq + b->ph)) * (1.0f - 0.55f * kp);
		float nn = b->n + 0.4f * sinf(t * 0.9f * b->fq + b->ph * 1.7f) + band * 1.6f;
		float wxx = us_lerp(s->wx[i0], s->wx[i1], tu), wyy = us_lerp(s->wy[i0], s->wy[i1], tu);
		float nxx = us_lerp(s->nx[i0], s->nx[i1], tu), nyy = us_lerp(s->ny[i0], s->ny[i1], tu);
		float tx = px + wxx * vv * wsc + nxx * nn * s->thick;
		float ty = py + wyy * vv * wsc + nyy * nn * s->thick;
		float tz = pz + us_lerp(s->wz[i0], s->wz[i1], tu) * vv * wsc;
		/* snare: the sheet tears open around sn_u, the two halves thrown apart across its width */
		float seg = 0;
		if (st->sn_env > 0.02f && b->sheet == 0 && !b->leaving) {
			float du = (b->u - st->sn_u) * (1.0f / 0.13f);
			if (du > -3.0f && du < 3.0f)
				seg = expf(-du * du);
			if (a->snare_hit && seg > 0.2f && roost < 0.5f) {
				/* the torn edge peels off as a burst of birds and swings back over the next second */
				float sg = vv < 0 ? -1.0f : 1.0f, kk = seg * (0.5f + 0.5f * b->fq);
				b->vx += (sg * wxx * 0.9f + 0.25f * (b->hash - 0.5f)) * kk;
				b->vy += (sg * wyy * 0.9f - 0.15f) * kk;
				b->panic = fmaxf(b->panic, 0.75f * seg);
			}
		}

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
		/* the stragglers beyond the rim are fainter */
		float av = fabsf(vv), wisp = 1.0f - 0.55f * us_smooth(0.85f, 1.5f, av);
		float v = bv * depth * us_sat(b->life) * endf * wisp *
			  (1.0f + 1.6f * band + 0.6f * b->panic + 1.5f * kp + 1.2f * st->kb + 1.4f * st->sn_env * seg);
		float L = 0.003f * depth * (1.0f + 0.9f * band + 0.6f * b->panic + 0.5f * kp + 0.5f * st->kb);
		/* where it is drawn: the kick's flinch toward the sheet's heart, the snare's tear across its width */
		float dxp = b->x, dyp = b->y;
		if (sq > 0.001f && !b->leaving) {
			dxp += (fcx[b->sheet] - b->x) * sq;
			dyp += (fcy[b->sheet] - b->y) * sq;
		}
		if (seg > 0.01f) {
			float sg = vv < 0 ? -1.0f : 1.0f, op = st->sn_env * seg * (1.0f * wsc + 0.03f) * (1.0f - roost) * tear;
			dxp += sg * wxx * op;
			dyp += sg * wyy * op;
		}
		/* true motion blur instead of trail ghosts: a fast bird stretches along its path, dimmer per sample */
		float mb = spd * dt * 1.1f;
		if (mb > L) {
			if (mb > 0.008f)
				mb = 0.008f;
			v *= 0.55f + 0.45f * L / mb;
			L = mb;
		}
		dash(c, dxp, dyp, b->hx, b->hy, L, v, US_MAT_ACCENT);
		/* hats: wing glints, a fresh scatter for each tick, gathered toward the sheet's thin edges */
		if (st->hat_env > 0.08f) {
			float edge = us_smooth(0.35f, 1.0f, av);
			uint32_t h = hash2((uint32_t)i, st->hat_seed + st->frame_n * 0x9e37u);
			if ((float)(h & 1023u) < 1024.0f * (0.07f + 0.45f * edge) * us_sat(st->hat_env * 1.4f)) {
				float g = st->hat_env * us_sat(b->life) * endf * (0.7f + 0.3f * depth);
				if (c->crisp) {
					int gx = (int)(dxp * c->s), gy = (int)(dyp * c->s);
					us_put(c, gx - 1, gy - 1, 0.5f * g, US_MAT_LIGHT);
					us_put(c, gx + 1, gy - 1, 0.5f * g, US_MAT_LIGHT);
				} else {
					/* both wings up: a bright stroke across the heading */
					dash(c, dxp, dyp, -b->hy, b->hx, 0.008f * depth, g, US_MAT_ACCENT);
				}
			}
		}
		/* a broken, wavering reflection in the mere (every third bird is plenty) */
		if ((i % 3) == 0) {
			float ry = HORIZON + 0.005f + (HORIZON - dyp) * 0.17f;
			if (ry < HORIZON + 0.095f) {
				float rx = dxp + (0.002f + 0.005f * st->sub_s) * sinf(ry * 700.0f + t * 2.3f);
				float rv = v * 0.42f * (0.6f + 0.4f * sinf(ry * 1900.0f - t * 3.0f));
				if (c->crisp)
					us_put(c, (int)(rx * c->s), (int)(ry * c->s), rv, US_MAT_WATER);
				else
					splat(c, rx, ry, rv, US_MAT_WATER);
			}
		}
		{
			int gx = (int)(dxp / A * MGX), gy = (int)(dyp * MGY);
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
