/* Understory scene: Coral reef — a sunlit reef seen from the side, as a diver hovering over the sand sees it.
 *
 * A water column: the bright, rippling underside of the surface at the top, sun shafts slanting down through
 * it, the blue deepening toward the sea floor. Depth comes in layers, each hazier than the one in front:
 * a far reef ridge on the horizon, mid-distance outcrops in silhouette (their fans and branches just shapes in
 * the haze), and in front the main reef — a lumpy rock outcrop rising from rippled, caustic-lit sand — carrying
 * distinct colonies drawn as an illustrator would: lacy gorgonian sea fans, branching staghorn thickets with
 * polyps at their tips, round brain corals with meandering ridges, clusters of tube sponges, and anemones
 * whose tentacles sway in the surge (one keeps a pair of clownfish). Kelp fronds frame the foreground corners,
 * seagrass tufts dot the sand. A school of small silver fish cruises the mid water (a deeper school behind),
 * a few butterflyfish potter around the reef, now and then a turtle swims through; bubbles rise to the
 * surface and marine snow drifts down.
 *
 * Audio: each drum has its own answer (from the analyser's kick / snare / hat detection):
 *   kick  — a surge of current: kelp, fans, anemones and seagrass are shoved over and swing back, the sun shafts
 *           and surface lurch, the fish schools wheel as one, the butterflyfish dart, a polyp-opening ripple
 *           crosses the reef, the turtle lands its power stroke;
 *   snare — the reef spawns: every polyp flares wide and bright, a colony or two release a puff of pink spawn
 *           bundles that drift up toward the light, a cloud of bubbles rises; the clownfish dart into their
 *           anemone;
 *   hats  — sunlight glints at a fresh scattering of points on the caustic sand and along the surface,
 *           marine snow twinkles, polyps flinch.
 *   Loudness is how much sunlight gets down (everything: louder = brighter, quiet = dim); swell fills the sun
 *   shafts; low-mids feed growth (colonies fill out); bass + sub drive the groundswell (the slow sway, sand
 *   ripple drift); the mids open the anemones (each its own band); air makes the marine snow sparkle; centroid
 *   sets the water's clarity (how far you see: the distant layers); stereo width swings the current.
 *   In silence the swell, the anemones' breathing, the fish and the turtle go on.
 * World: reads pollen + water_level (nutrients -> growth), reef (initial growth), daylight (sunlight),
 *        flock + pollen + reef (fish numbers); writes reef (follows the colonies' growth).
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define Y_SURF 0.05f /* mean surface line */
#define Y_HOR 0.64f  /* where the sand meets the haze */
#define WFW 80       /* water-column light field */
#define WFH 48
#define SFW 96 /* sand field */
#define SFH 32
#define NFAR 160 /* far ridge profile samples */
#define NRAY 7
#define MAX_COL 26
#define MAX_MIDC 16 /* silhouette colonies on the mid-distance outcrops */
#define MAX_MIDR 4
#define MAX_KELP 8
#define MAX_GRASS 28
#define MAX_PL 320
#define MAX_SP 160
#define MAX_BUB 128
#define MAX_GL 128
#define MAX_SCHOOL 3
#define MAX_FISH 28
#define MAX_RF 4
#define MAX_HOLE 6

enum { CT_FAN = 0, CT_SPONGE, CT_STAG, CT_BRAIN, CT_ANEM, CT_COUNT };

typedef struct {
	int type, rock; /* rock: 0 main, 1 side outcrop, 2 on the sand */
	float fx, u;    /* x as fraction of width; u = position across its rock (-1..1) */
	float face;     /* 0 on the crest, up to ~0.7 down the rock's face (nearer to us) */
	float size, grow, ph, glow, open, shy;
	uint32_t seed;
} col_t;

typedef struct {
	float cx, hw, base, h, nofs; /* cx, hw as fractions of width */
	int nh;
	float hu[MAX_HOLE], hd[MAX_HOLE], hr[MAX_HOLE]; /* crevices: across (-1..1), depth below the top (0..1), size */
} rock_t;

typedef struct {
	float fx, h, w, ph, lean;
	uint32_t seed;
} kelp_t;

typedef struct {
	float fx, y, h, ph;
	int n;
} grass_t;

typedef struct {
	float x, y, vx, vy, ph, size; /* x as fraction of width */
} plank_t;

typedef struct {
	float x, y, vx, vy, life, age; /* x in units */
} spawn_t;

typedef struct {
	float x, y, r, age, life, wob;
} bub_t;

typedef struct {
	float x, y, age, life, size;
} glint_t;

typedef struct {
	float ox, oy, vx, vy, sa, sc, tau, flash, dx, dy;
} fish_t;

typedef struct {
	float x, y, head, head_to, speed, burst, burst_to, scare, turn;
	int deep;
	fish_t f[MAX_FISH];
} school_t;

typedef struct {
	float x, y, home_y, face, face_to, L, ph, burst, wait;
	int kind;
} rfish_t;

typedef struct {
	int on, dir;
	float x, y, L, glide, fl, paddle, phase_t, wait, bob;
	int phase; /* 0 glide, 1 raising the flippers (waiting for a kick), 2 power stroke */
} turtle_t;

typedef struct {
	int on, dir, phase;
	float x, y, L, fl, glide, phase_t, wait, ph;
} ray_t;

typedef struct {
	int mode; /* 0 sitting in its den, 1 jetting away, 2 swimming back, settling */
	float x, y, vx, vy, hx, hy; /* mantle centre; den */
	float stream, blanch, band, band_x, mood, cool, ph, mt;
	float ink_x[6], ink_y[6], ink_r[6], ink_a[6];
} octo_t;

typedef struct {
	int g;          /* the seagrass tuft it holds on to */
	float tuck, ph, dir;
} horse_t;

typedef struct {
	float u, face, rot, size, ph;
} star_t;

typedef struct {
	us_rng rng;
	us_rng rng2; /* the reef dwellers' own dice, so they leave the rest of the reef's draws untouched */
	int inited;
	float A0;
	/* geography */
	rock_t rk[2]; /* 0 main, 1 side outcrop */
	rock_t mr[MAX_MIDR];
	int nmr;
	col_t col[MAX_COL];
	int ncol, nfixed;
	col_t mc[MAX_MIDC];
	int nmc;
	float far_h[NFAR];
	kelp_t kelp[MAX_KELP];
	int nkelp;
	grass_t grass[MAX_GRASS];
	float ray_x[NRAY], ray_w[NRAY], ray_ph[NRAY];
	/* fields */
	float wf[WFW * WFH], sf[SFW * SFH];
	/* lives */
	plank_t pl[MAX_PL];
	spawn_t sp[MAX_SP];
	int nsp;
	bub_t bub[MAX_BUB];
	int nbub;
	glint_t gl[MAX_GL];
	int ngl;
	school_t sc[MAX_SCHOOL];
	rfish_t rf[MAX_RF];
	turtle_t tu;
	ray_t ray;
	octo_t oc;
	horse_t hs[2];
	star_t ss[3];
	int mor_h; /* the crevice the moray lives in */
	float mor_side, mor_ext, mor_to, mor_ph;
	/* envelopes */
	float low_env, bass_env, hi_env, lvl_env, lvl_slow, swell_k, open_env, sub_env, mid_env, cent_env;
	float surge_ph, surge_amp, cur_ang, duck, duck_to;
	float kick_s, snare_s, hat_s, kick_wave, kick_wave_x, lurch, stir;
	float flow_t, caus_t, ray_t, rip_drift, intro_t, bub_cd, bub_idle, cover;
	uint32_t glint_seed;
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x636f7261u);
	us_rng_seed(&s->rng2, seed ^ 0x72656566u);
	return s;
}

static void destroy(void *p) { free(p); }

static inline uint32_t hash2(int x, int y)
{
	uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	return h ^ (h >> 15);
}
static inline float fast_sin(float x)
{
	x *= 1.0f / US_TAU;
	x -= floorf(x + 0.5f);
	float y = 8.0f * x - 16.0f * x * fabsf(x);
	return 0.775f * y + 0.225f * y * fabsf(y);
}
static inline float hashf(uint32_t s, int k) { return (hash2((int)s, k) & 0xffff) * (1.0f / 65535.0f); }

/* ---------------------------------------------------------------- drawing primitives ---- */

/* painter's compositing: lay v over what is there by coverage (so near things hide far ones, even darker) */
static inline void over(us_canvas *c, int px, int py, float v, float cov, uint8_t mat)
{
	if ((unsigned)px >= (unsigned)c->w || (unsigned)py >= (unsigned)c->h || !(cov > 0.0f))
		return;
	size_t i = (size_t)py * c->w + px;
	float o = c->ink[i];
	float n = o + (v * 255.0f - o) * (cov > 1.0f ? 1.0f : cov);
	c->ink[i] = (uint8_t)(n < 0 ? 0 : n > 255 ? 255 : n + 0.5f);
	if (cov >= 0.5f)
		c->mat[i] = mat;
}

/* cheap anti-aliased hairline (Xiaolin Wu), MAX blend: highlights, ribs, outlines */
static void wu_line(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
	float x0 = ax * c->s, y0 = ay * c->s, x1 = bx * c->s, y1 = by * c->s;
	int steep = fabsf(y1 - y0) > fabsf(x1 - x0);
	if (steep) {
		float t = x0; x0 = y0; y0 = t;
		t = x1; x1 = y1; y1 = t;
	}
	if (x0 > x1) {
		float t = x0; x0 = x1; x1 = t;
		t = y0; y0 = y1; y1 = t;
	}
	float dx = x1 - x0, g = dx > 1e-5f ? (y1 - y0) / dx : 0.0f;
	int i0 = (int)floorf(x0 + 0.5f), i1 = (int)floorf(x1 + 0.5f);
	int major = steep ? c->h : c->w, minor = steep ? c->w : c->h;
	int lo = i0 < 0 ? 0 : i0, hi = i1 >= major ? major - 1 : i1;
	if (lo > hi || !(v > 0.0f))
		return;
	size_t smaj = steep ? (size_t)c->w : 1, smin = steep ? 1 : (size_t)c->w;
	uint8_t *ink = c->ink, *mt = c->mat;
	float v255 = (v > 1.0f ? 1.0f : v) * 255.0f;
	for (int i = lo; i <= hi; i++) {
		float cx = i + 0.5f;
		float cov = v255;
		if (i == i0) cov *= us_sat(cx + 0.5f - x0);
		if (i == i1) cov *= us_sat(x1 - (cx - 0.5f));
		float y = y0 + g * (us_clampf(cx, x0, x1) - x0) - 0.5f;
		int iy = (int)floorf(y);
		float f = y - iy;
		int ia = (int)(cov * (1.0f - f) + 0.5f), ib = (int)(cov * f + 0.5f);
		size_t base = (size_t)i * smaj;
		if ((unsigned)iy < (unsigned)minor) {
			size_t k = base + (size_t)iy * smin;
			if (ia > ink[k]) ink[k] = (uint8_t)ia, mt[k] = mat;
		}
		if ((unsigned)(iy + 1) < (unsigned)minor) {
			size_t k = base + (size_t)(iy + 1) * smin;
			if (ib > ink[k]) ink[k] = (uint8_t)ib, mt[k] = mat;
		}
	}
}

/* 1-px crisp line (DDA) for pixel canvases */
static void px_line(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
	float x0 = ax * c->s, y0 = ay * c->s, x1 = bx * c->s, y1 = by * c->s;
	float dx = x1 - x0, dy = y1 - y0;
	int n = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
	if (n < 1)
		n = 1;
	if (n > 4096)
		return;
	float ix = dx / n, iy = dy / n;
	for (int k = 0; k <= n; k++)
		us_put(c, (int)floorf(x0 + ix * k), (int)floorf(y0 + iy * k), v, mat);
}

static inline void hair(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
	if (c->crisp)
		px_line(c, ax, ay, bx, by, v, mat);
	else
		wu_line(c, ax, ay, bx, by, v, mat);
}

/* a tapered capsule (radius ra at a, rb at b), painted over what is there */
static void cap_over(us_canvas *c, float ax, float ay, float bx, float by, float ra, float rb, float v, uint8_t mat)
{
	float s = c->s;
	ax *= s, ay *= s, bx *= s, by *= s, ra *= s, rb *= s;
	float rmin = c->crisp ? 0.5f : 0.6f;
	if (ra < rmin) ra = rmin;
	if (rb < rmin) rb = rmin;
	float rm = fmaxf(ra, rb) + 1.0f;
	int x0 = (int)floorf(fminf(ax, bx) - rm), x1 = (int)ceilf(fmaxf(ax, bx) + rm);
	int y0 = (int)floorf(fminf(ay, by) - rm), y1 = (int)ceilf(fmaxf(ay, by) + rm);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > c->w - 1) x1 = c->w - 1;
	if (y1 > c->h - 1) y1 = c->h - 1;
	if (x0 > x1 || y0 > y1 || (x1 - x0) * (y1 - y0) > 400000)
		return;
	float dx = bx - ax, dy = by - ay, il = 1.0f / (dx * dx + dy * dy + 1e-9f);
	uint8_t v8 = (uint8_t)(us_sat(v) * 255.0f + 0.5f);
	for (int py = y0; py <= y1; py++) {
		/* only the stretch of this row the capsule can reach */
		int xa = x0, xb = x1;
		if (fabsf(dy) > 1e-3f) {
			float yy = py + 0.5f;
			float t0 = us_sat((yy - rm - ay) / dy), t1 = us_sat((yy + rm - ay) / dy);
			float xa_f = ax + dx * fminf(t0, t1) - rm, xb_f = ax + dx * fmaxf(t0, t1) + rm;
			if (dx < 0) {
				float tt = ax + dx * fmaxf(t0, t1) - rm;
				xb_f = ax + dx * fminf(t0, t1) + rm;
				xa_f = tt;
			}
			if ((int)floorf(xa_f) > xa) xa = (int)floorf(xa_f);
			if ((int)ceilf(xb_f) < xb) xb = (int)ceilf(xb_f);
		}
		for (int px = xa; px <= xb; px++) {
			float qx = px + 0.5f - ax, qy = py + 0.5f - ay;
			float t = us_sat((qx * dx + qy * dy) * il);
			float ex = qx - t * dx, ey = qy - t * dy;
			float d2 = ex * ex + ey * ey;
			if (d2 > rm * rm)
				continue;
			float r = ra + (rb - ra) * t;
			if (d2 < (r - 0.5f) * (r - 0.5f) && r > 0.5f) { /* fully inside: plain write */
				size_t k = (size_t)py * c->w + px;
				c->ink[k] = v8, c->mat[k] = mat;
				continue;
			}
			float d = sqrtf(d2);
			float cov = c->crisp ? (d < r + 0.15f ? 1.0f : 0.0f) : us_sat(r - d + 0.5f);
			if (cov > 0.0f)
				over(c, px, py, v, cov, mat);
		}
	}
}

/* a filled ellipse painted over, optionally shaded as a dome lit from above-left (lit 0..1), optionally only
 * the part above cy + skirt (a dome sitting on something) */
static void ell_over(us_canvas *c, float cx, float cy, float a, float b, float v, uint8_t mat, float lit, int upper,
		     float skirt)
{
	float s = c->s;
	float minr = 0.5f / s;
	if (a < minr) a = minr;
	if (b < minr) b = minr;
	int x0 = (int)floorf((cx - a) * s - 1), x1 = (int)ceilf((cx + a) * s + 1);
	int y0 = (int)floorf((cy - b) * s - 1), y1 = (int)ceilf((cy + (upper ? fminf(skirt, b) : b)) * s + 1);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > c->w - 1) x1 = c->w - 1;
	if (y1 > c->h - 1) y1 = c->h - 1;
	if (x0 > x1 || y0 > y1 || (x1 - x0) * (y1 - y0) > 400000)
		return;
	float ia = 1.0f / a, ib = 1.0f / b, mr = fminf(a, b) * s, is = 1.0f / s;
	for (int py = y0; py <= y1; py++) {
		float dy = (py + 0.5f) * is - cy;
		if (upper && dy > skirt)
			continue;
		float ny = dy * ib;
		for (int px = x0; px <= x1; px++) {
			float nx = ((px + 0.5f) * is - cx) * ia;
			float q2 = nx * nx + ny * ny;
			if (q2 > 1.6f)
				continue;
			float q = sqrtf(q2);
			float cov = c->crisp ? (q <= 1.0f ? 1.0f : 0.0f) : us_sat((1.0f - q) * mr + 0.5f);
			if (cov <= 0.0f)
				continue;
			float vv = v;
			if (lit > 0.0f) {
				float z = sqrtf(fmaxf(0.0f, 1.0f - q2));
				float L = -0.45f * nx - 0.62f * ny + 0.64f * z;
				vv = v * (1.0f - lit + lit * us_sat(0.2f + 0.9f * L));
			}
			over(c, px, py, vv, cov, mat);
		}
	}
}

/* scanline polygon fill painted over (no AA: outlines go on top), brightness graded top -> bottom */
static void poly_over(us_canvas *c, const float *xy, int n, float vt, float vb, uint8_t mat)
{
	if (n < 3 || n > 64)
		return;
	float s = c->s, ymin = 1e9f, ymax = -1e9f;
	for (int i = 0; i < n; i++)
		ymin = fminf(ymin, xy[2 * i + 1]), ymax = fmaxf(ymax, xy[2 * i + 1]);
	int y0 = (int)floorf(ymin * s), y1 = (int)ceilf(ymax * s);
	if (y0 < 0) y0 = 0;
	if (y1 > c->h - 1) y1 = c->h - 1;
	float xs[64];
	for (int py = y0; py <= y1; py++) {
		float y = (py + 0.5f) / s;
		int nx = 0;
		for (int i = 0, j = n - 1; i < n; j = i++) {
			float yi = xy[2 * i + 1], yj = xy[2 * j + 1];
			if ((yi > y) != (yj > y))
				xs[nx++] = xy[2 * i] + (y - yi) / (yj - yi) * (xy[2 * j] - xy[2 * i]);
		}
		for (int i = 1; i < nx; i++)
			for (int k = i; k > 0 && xs[k] < xs[k - 1]; k--) {
				float t = xs[k]; xs[k] = xs[k - 1]; xs[k - 1] = t;
			}
		float v = us_lerp(vt, vb, us_sat((y - ymin) / (ymax - ymin + 1e-6f)));
		for (int i = 0; i + 1 < nx; i += 2) {
			int a = (int)floorf(xs[i] * s + 0.5f), b = (int)floorf(xs[i + 1] * s + 0.5f);
			if (a < 0) a = 0;
			if (b > c->w) b = c->w;
			for (int px = a; px < b; px++)
				over(c, px, py, v, 1.0f, mat);
		}
	}
}

/* a low-res field (values 0..1) stretched over the full width between rows y0..y1, written straight in
 * (it is the backdrop: overwrites). Much cheaper than us_field: one lerp per pixel, no per-pixel checks. */
static void backdrop(us_canvas *c, const float *f, int fw, int fh, uint8_t mat, float ry0, float ry1)
{
	int y0 = (int)floorf(ry0 * c->s), y1 = (int)ceilf(ry1 * c->s);
	if (y0 < 0) y0 = 0;
	if (y1 > c->h) y1 = c->h;
	float sh = (ry1 - ry0) * c->s;
	if (sh <= 0 || fw < 2 || fh < 2 || fw > 512)
		return;
	float row[512 + 1];
	/* pixel spans per field column: [px0[i], px0[i+1]) lies between samples i and i+1 */
	int px0[513];
	for (int i = 0; i <= fw; i++) {
		int p = (int)ceilf(((float)i + 0.5f) * c->w / fw - 0.5f);
		px0[i] = p < 0 ? 0 : p > c->w ? c->w : p;
	}
	for (int j = y0; j < y1; j++) {
		float fy = ((j + 0.5f) - ry0 * c->s) / sh * fh - 0.5f;
		int iy = (int)floorf(fy);
		float ty = fy - iy;
		int ia = iy < 0 ? 0 : iy >= fh ? fh - 1 : iy, ib = iy + 1 < 0 ? 0 : iy + 1 >= fh ? fh - 1 : iy + 1;
		for (int i = 0; i < fw; i++)
			row[i] = 255.0f * us_sat(f[ia * fw + i] + (f[ib * fw + i] - f[ia * fw + i]) * ty) + 0.5f;
		row[fw] = row[fw - 1];
		uint8_t *ink = c->ink + (size_t)j * c->w;
		/* left edge: flat */
		for (int p = 0; p < px0[0]; p++)
			ink[p] = (uint8_t)row[0];
		for (int i = 0; i < fw; i++) {
			int pa = px0[i], pb = i + 1 < fw ? px0[i + 1] : c->w;
			if (pb <= pa)
				continue;
			float v = row[i], dv = i + 1 < fw ? (row[i + 1] - row[i]) * fw / (float)c->w : 0.0f;
			v += dv * (((float)pa + 0.5f) * fw / c->w - 0.5f - i);
			for (int p = pa; p < pb; p++, v += dv)
				ink[p] = (uint8_t)v;
		}
		memset(c->mat + (size_t)j * c->w, mat, (size_t)c->w);
	}
}

/* ellipse outline (part of it: t from t0 to t1, 0 = right, pi/2 = down) */
static void ell_line(us_canvas *c, float cx, float cy, float a, float b, float t0, float t1, int n, float v, uint8_t mat)
{
	float px = cx + cosf(t0) * a, py = cy + sinf(t0) * b;
	for (int k = 1; k <= n; k++) {
		float t = t0 + (t1 - t0) * k / n;
		float x = cx + cosf(t) * a, y = cy + sinf(t) * b;
		hair(c, px, py, x, y, v, mat);
		px = x, py = y;
	}
}

/* --------------------------------------------------------------------- geography ---- */

static float rock_top(const rock_t *rk, float fx)
{
	float u = (fx - rk->cx) / rk->hw;
	if (fabsf(u) >= 1.0f)
		return 9.0f;
	float sh = powf(cosf(u * US_PI * 0.5f), 0.6f);
	float n = 0.16f * us_noise2(fx * 11.0f + rk->nofs, 0.5f) + 0.07f * us_noise2(fx * 37.0f, 3.1f + rk->nofs);
	return rk->base - rk->h * sh * (1.0f + n);
}

static float col_base_y(const st_t *st, const col_t *k)
{
	if (k->rock == 2)
		return 0.0f; /* set by caller */
	const rock_t *rk = &st->rk[k->rock];
	float top = rock_top(rk, k->fx);
	return top + 0.006f + (rk->base - top) * k->face;
}

static void add_col(st_t *st, int type, int rock, float u, float face, float size)
{
	if (st->ncol >= MAX_COL)
		return;
	us_rng *r = &st->rng;
	col_t *k = &st->col[st->ncol++];
	memset(k, 0, sizeof(*k));
	k->type = type, k->rock = rock, k->u = u, k->face = face;
	k->fx = rock < 2 ? st->rk[rock].cx + u * st->rk[rock].hw : u;
	k->size = size * us_randr(r, 0.88f, 1.12f);
	k->ph = us_rand(r) * US_TAU;
	k->seed = us_rng_u32(r);
	k->open = 0.5f;
}

static void init_scene(st_t *st, const us_ctx *ctx)
{
	us_rng *r = &st->rng, *r2 = &st->rng2;
	us_world *w = ctx->w;
	float A = ctx->aspect;
	st->A0 = A;
	int flip = us_rand(r) < 0.5f;
	float sgn = flip ? -1.0f : 1.0f;
	/* the main outcrop, a little off centre, and a smaller one further back on the other side */
	rock_t *m = &st->rk[0];
	m->cx = 0.5f + sgn * us_randr(r, 0.02f, 0.07f);
	m->hw = us_randr(r, 0.27f, 0.31f);
	m->base = 0.95f, m->h = us_randr(r, 0.2f, 0.23f), m->nofs = us_rand(r) * 50.0f;
	rock_t *s2 = &st->rk[1];
	s2->cx = 0.5f - sgn * us_randr(r, 0.33f, 0.37f);
	s2->hw = us_randr(r, 0.11f, 0.14f);
	s2->base = 0.8f, s2->h = us_randr(r, 0.08f, 0.1f), s2->nofs = us_rand(r) * 50.0f;
	for (int k = 0; k < 2; k++) {
		rock_t *rk = &st->rk[k];
		rk->nh = k ? 2 : 5;
		for (int i = 0; i < rk->nh; i++) {
			rk->hu[i] = us_randr(r, -0.7f, 0.7f);
			rk->hd[i] = us_randr(r, 0.35f, 0.75f);
			rk->hr[i] = us_randr(r, 0.010f, 0.020f) * (k ? 0.7f : 1.0f);
		}
	}
	/* colonies on the main outcrop: a composed arrangement (big fan behind, brain coral up front) */
	st->ncol = 0;
	add_col(st, CT_FAN, 0, sgn * -0.2f, 0.0f, 0.27f);
	add_col(st, CT_FAN, 0, sgn * 0.56f, 0.0f, 0.16f);
	add_col(st, CT_SPONGE, 0, sgn * 0.74f, 0.0f, 0.15f);
	add_col(st, CT_SPONGE, 0, sgn * -0.8f, 0.0f, 0.1f);
	add_col(st, CT_STAG, 0, sgn * -0.5f, 0.0f, 0.17f);
	add_col(st, CT_STAG, 0, sgn * 0.27f, 0.0f, 0.17f);
	add_col(st, CT_BRAIN, 0, sgn * 0.02f, 0.0f, 0.11f);
	add_col(st, CT_ANEM, 0, sgn * 0.44f, 0.0f, 0.06f);
	add_col(st, CT_BRAIN, 0, sgn * -0.3f, 0.42f, 0.10f);
	add_col(st, CT_STAG, 0, sgn * -0.66f, 0.45f, 0.09f);
	add_col(st, CT_BRAIN, 0, sgn * 0.5f, 0.5f, 0.075f);
	add_col(st, CT_ANEM, 0, sgn * 0.12f, 0.62f, 0.05f);
	/* the side outcrop */
	add_col(st, CT_FAN, 1, 0.05f, 0.0f, 0.13f);
	add_col(st, CT_STAG, 1, -0.4f, 0.0f, 0.1f);
	add_col(st, CT_BRAIN, 1, 0.35f, 0.15f, 0.065f);
	/* the clownfish anemone on the sand at the foot of the reef */
	add_col(st, CT_ANEM, 2, m->cx + sgn * (m->hw * 0.98f), 0.0f, 0.09f);
	st->nfixed = st->ncol;
	/* extras, shown with density */
	static const int extra[] = {CT_STAG, CT_BRAIN, CT_SPONGE, CT_FAN, CT_STAG, CT_ANEM, CT_BRAIN, CT_SPONGE,
				    CT_BRAIN, CT_STAG};
	for (int i = 0; i < 10; i++) {
		int rock = (i % 4 == 3) ? 1 : 0;
		int ty = extra[i];
		float sz = ty == CT_FAN ? 0.12f : ty == CT_BRAIN ? 0.07f : ty == CT_ANEM ? 0.045f : 0.1f;
		float face = ty == CT_FAN ? 0.0f : us_randr(r, 0.15f, 0.65f);
		add_col(st, ty, rock, us_randr(r, -0.8f, 0.8f), face, sz * (rock ? 0.75f : 1.0f));
	}
	/* memory: a reef that was well fed elsewhere opens grown in */
	for (int i = 0; i < st->ncol; i++)
		st->col[i].grow = us_sat(0.55f + 0.45f * w->reef + us_randr(r, -0.1f, 0.1f));
	/* mid-distance outcrops (silhouettes in the haze), avoiding the main reef */
	st->nmr = 0;
	st->nmc = 0;
	for (int k = 0; k < MAX_MIDR; k++) {
		rock_t *rk = &st->mr[st->nmr++];
		float cx = (k + 0.5f) / MAX_MIDR + us_randr(r, -0.08f, 0.08f);
		rk->cx = cx, rk->hw = us_randr(r, 0.06f, 0.11f), rk->base = Y_HOR + 0.045f, rk->h = us_randr(r, 0.05f, 0.09f);
		rk->nofs = us_rand(r) * 50.0f;
		for (int j = 0; j < 4 && st->nmc < MAX_MIDC; j++) {
			col_t *q = &st->mc[st->nmc++];
			memset(q, 0, sizeof(*q));
			static const int mt[4] = {CT_FAN, CT_STAG, CT_BRAIN, CT_SPONGE};
			q->type = mt[(j + k) & 3];
			q->rock = k;
			q->u = us_randr(r, -0.6f, 0.6f);
			q->fx = rk->cx + q->u * rk->hw;
			q->size = (q->type == CT_FAN ? 0.075f : q->type == CT_BRAIN ? 0.03f : 0.05f) * us_randr(r, 0.8f, 1.2f);
			q->ph = us_rand(r) * US_TAU;
			q->seed = us_rng_u32(r);
			q->grow = 1.0f;
		}
	}
	/* far ridge on the horizon */
	for (int i = 0; i < NFAR; i++) {
		float fx = (float)i / (NFAR - 1);
		float n = 0.5f + 0.5f * us_fbm2(fx * 4.0f + 9.0f, 1.3f, 3);
		float bumps = fmaxf(0.0f, us_noise2(fx * 46.0f, 7.0f)) * 0.012f;
		st->far_h[i] = 0.016f + 0.05f * n * n + bumps;
	}
	/* kelp at the foreground edges */
	st->nkelp = 0;
	for (int i = 0; i < MAX_KELP; i++) {
		kelp_t *k = &st->kelp[st->nkelp++];
		int right = i & 1;
		float fx = us_randr(r, 0.0f, 0.1f) + 0.012f * (i >> 1);
		k->fx = right ? 1.0f - fx : fx;
		k->h = us_randr(r, 0.5f, 0.8f) * (i < 2 ? 1.0f : 0.85f);
		k->w = us_randr(r, 0.8f, 1.2f);
		k->ph = us_rand(r) * US_TAU;
		k->lean = (right ? -1.0f : 1.0f) * us_randr(r, 0.02f, 0.06f);
		k->seed = us_rng_u32(r);
	}
	/* seagrass tufts on the near sand, kept clear of the reef's foot */
	for (int i = 0; i < MAX_GRASS; i++) {
		grass_t *g = &st->grass[i];
		for (int tries = 0; tries < 8; tries++) {
			g->fx = us_rand(r);
			g->y = us_randr(r, Y_HOR + 0.08f, 1.02f);
			if (fabsf(g->fx - m->cx) > m->hw * 0.9f || g->y > 0.97f)
				break;
		}
		float d = (g->y - Y_HOR) / (1.0f - Y_HOR);
		g->h = us_randr(r, 0.03f, 0.06f) * (0.4f + 0.7f * d);
		g->ph = us_rand(r) * US_TAU;
		g->n = 4 + (int)(us_rand(r) * 4);
	}
	for (int i = 0; i < NRAY; i++) {
		st->ray_x[i] = us_randr(r, -0.1f, 1.0f);
		st->ray_w[i] = us_randr(r, 0.012f, 0.04f);
		st->ray_ph[i] = us_rand(r) * US_TAU;
	}
	for (int i = 0; i < MAX_PL; i++) {
		plank_t *p = &st->pl[i];
		p->x = us_rand(r), p->y = us_randr(r, Y_SURF, 1.0f), p->ph = us_rand(r) * 50.0f, p->size = us_rand(r);
	}
	for (int k = 0; k < MAX_SCHOOL; k++) {
		school_t *s = &st->sc[k];
		s->deep = k == 2;
		s->x = us_rand(r) * A;
		s->y = s->deep ? us_randr(r, 0.38f, 0.5f) : us_randr(r, 0.2f, 0.42f);
		s->head = us_rand(r) < 0.5f ? us_randr(r, -0.2f, 0.2f) : US_PI + us_randr(r, -0.2f, 0.2f);
		s->head_to = s->head;
		s->speed = us_randr(r, 0.03f, 0.045f);
		for (int i = 0; i < MAX_FISH; i++) {
			fish_t *f = &s->f[i];
			f->sa = us_randn(r) * 0.04f;
			f->sc = us_randn(r) * 0.016f;
			f->ox = f->sa * cosf(s->head);
			f->oy = f->sa * sinf(s->head);
			f->vx = cosf(s->head) * s->speed;
			f->vy = sinf(s->head) * s->speed;
			f->tau = us_randr(r, 0.06f, 0.4f);
			f->dx = cosf(s->head), f->dy = sinf(s->head);
		}
	}
	for (int k = 0; k < MAX_RF; k++) {
		rfish_t *f = &st->rf[k];
		f->kind = k & 1;
		f->x = m->cx * A + us_randr(r, -0.5f, 0.5f);
		f->home_y = f->y = us_randr(r, 0.5f, 0.72f);
		f->face = f->face_to = us_rand(r) < 0.5f ? -1.0f : 1.0f;
		f->L = us_randr(r, 0.05f, 0.064f);
		f->ph = us_rand(r) * US_TAU;
		f->wait = us_randr(r, 2.0f, 8.0f);
	}
	st->tu.wait = us_randr(r, 3.0f, 9.0f);
	st->ray.wait = us_randr(r2, 14.0f, 26.0f);
	st->ray.ph = us_rand(r2) * 10.0f;
	/* the moray keeps the biggest crevice of the main outcrop; the octopus a den on the far side of it */
	{
		/* the crevice clearest of colonies (so the eel's head reads against bare rock) */
		int hb = 0, ho = 0;
		float clear_b = -1.0f;
		for (int h = 0; h < m->nh; h++) {
			float fx = m->cx + m->hu[h] * m->hw, top = rock_top(m, fx);
			float y = top + (m->base - top) * m->hd[h];
			float dmin = 9.0f, nside = 1.0f;
			for (int i = 0; i < st->ncol; i++) {
				col_t *k = &st->col[i];
				if (k->rock != 0)
					continue;
				float ky = col_base_y(st, k) - k->size * 0.3f;
				float ddx = (k->fx - fx) * A, ddy = ky - y;
				float d = sqrtf(ddx * ddx + ddy * ddy) - k->size * 0.5f;
				if (d < dmin)
					dmin = d, nside = k->fx > fx ? -1.0f : 1.0f;
			}
			dmin += 0.2f * m->hr[h];
			if (dmin > clear_b)
				clear_b = dmin, hb = h, st->mor_side = nside; /* it looks away from its nearest neighbour */
		}
		float best = -1.0f;
		for (int h = 0; h < m->nh; h++)
			if (h != hb && fabsf(m->hu[h] - m->hu[hb]) > best)
				best = fabsf(m->hu[h] - m->hu[hb]), ho = h;
		st->mor_h = hb;
		st->mor_ext = 0.5f, st->mor_to = 1.0f;
		octo_t *o = &st->oc;
		float fx = m->cx + m->hu[ho] * m->hw, top = rock_top(m, fx);
		o->hx = fx;
		o->hy = top + (m->base - top) * m->hd[ho] - 0.004f;
		o->x = fx * A, o->y = o->hy;
		o->ph = us_rand(r2) * 10.0f;
		o->band = 1.0f;
		o->cool = 3.0f;
	}
	for (int i = 0; i < 3; i++) {
		star_t *q = &st->ss[i];
		q->u = us_randr(r2, -0.8f, 0.8f);
		q->face = us_randr(r2, 0.35f, 0.75f);
		q->rot = us_rand(r2) * US_TAU;
		q->size = us_randr(r2, 0.014f, 0.019f);
		q->ph = us_rand(r2) * 10.0f;
	}
	/* seahorses, each holding a seagrass tuft out on the sand (not under the kelp, not by the anemone) */
	for (int k = 0; k < 2; k++) {
		horse_t *hh = &st->hs[k];
		hh->g = k;
		float bestv = -1.0f;
		for (int i = 0; i < MAX_GRASS; i++) {
			grass_t *g = &st->grass[i];
			float d = (g->y - Y_HOR) / (1.0f - Y_HOR);
			float side = k ? (g->fx > m->cx ? 1.0f : 0.0f) : (g->fx < m->cx ? 1.0f : 0.0f);
			float score = side + (g->fx > 0.16f && g->fx < 0.84f ? 2.5f : 0.0f) + (d > 0.3f && d < 0.75f ? 1.0f : 0.0f) +
				      (fabsf(g->fx - (m->cx + sgn * m->hw * 0.98f)) > 0.08f ? 1.0f : 0.0f) + 0.3f * hashf(0x4057u, i);
			if (k && i == st->hs[0].g)
				score = -9.0f;
			if (score > bestv)
				bestv = score, hh->g = i;
		}
		hh->dir = st->grass[hh->g].fx < 0.5f ? 1.0f : -1.0f;
		hh->ph = us_rand(r2) * 10.0f;
	}
	st->cur_ang = us_randr(r, -0.4f, 0.4f);
	st->surge_amp = 0.3f;
	st->inited = 1;
}

/* ---------------------------------------------------------------- the colonies ---- */

typedef struct {
	float lum;   /* light reaching it */
	float fill;  /* body tone */
	float line;  /* highlights / ribs */
	float tip;   /* polyps */
	int sil;     /* silhouette in the haze: one flat tone + a faint rim */
	uint8_t mat; /* silhouettes use the water's material */
} look_t;

static void stag_branch(us_canvas *c, const look_t *lk, float x, float y, float ang, float len, float r, int depth,
			uint32_t seed, int idx, float tipv, float tipr, uint8_t mat)
{
	float ex = x + sinf(ang) * len, ey = y - cosf(ang) * len;
	float re = r * 0.8f;
	cap_over(c, x, y, ex, ey, r, re, lk->fill, mat);
	if (!lk->sil) {
		/* the sun-side flank catches the light */
		float nx = -cosf(ang), ny = -sinf(ang); /* perpendicular toward the left */
		if (nx > 0) nx = -nx, ny = -ny;
		hair(c, x + nx * r * 0.55f, y + ny * r * 0.55f, ex + nx * re * 0.55f, ey + ny * re * 0.55f, lk->line, mat);
	}
	int last = depth >= (lk->sil ? 2 : 3) || len < 0.016f;
	if (last) {
		if (!lk->sil) {
			us_disc(c, ex + sinf(ang) * re * 0.8f, ey - cosf(ang) * re * 0.8f, tipr, tipv, tipv > 0.75f ? US_MAT_LIGHT : mat);
		}
		return;
	}
	int nch = 2 + ((hash2((int)seed, idx * 7 + 3) & 7) == 0);
	for (int k = 0; k < nch; k++) {
		float h = hashf(seed, idx * 13 + k);
		float da = (nch == 2 ? (k ? 1.0f : -1.0f) : (k - 1.0f)) * (0.32f + 0.25f * h);
		/* staghorn grows up and outward: pull the angle a little back toward vertical */
		float na = ang * 0.82f + da;
		stag_branch(c, lk, ex, ey, na, len * (0.68f + 0.14f * h), re, depth + 1, seed, idx * 3 + k + 1, tipv, tipr, mat);
	}
}

static void draw_staghorn(us_canvas *c, const look_t *lk, float X, float Y, float S, const col_t *k, float tipv)
{
	uint8_t mat = lk->sil ? lk->mat : US_MAT_ACCENT;
	int nt = 3 + (int)(hashf(k->seed, 99) * 2.0f);
	for (int i = 0; i < nt; i++) {
		float h = hashf(k->seed, 200 + i);
		float ang = ((i + 0.5f) / nt - 0.5f) * 1.5f + (h - 0.5f) * 0.3f;
		float bx = X + ((i + 0.5f) / nt - 0.5f) * S * 0.35f;
		stag_branch(c, lk, bx, Y, ang, S * (0.33f + 0.1f * h), S * 0.055f, 0, k->seed + (uint32_t)i * 977u, 1, tipv,
			    S * (0.014f + 0.012f * k->open), mat);
	}
}

static void draw_brain(us_canvas *c, const look_t *lk, float X, float Y, float S, const col_t *k, float ridgev)
{
	float a = 0.55f * S, b = 0.43f * S;
	uint8_t mat = lk->sil ? lk->mat : US_MAT_LEAF;
	ell_over(c, X, Y, a, b, lk->fill, mat, lk->sil ? 0.0f : 0.75f, 1, 0.012f);
	if (lk->sil) {
		ell_line(c, X, Y, a, b, US_PI, US_TAU, 14, lk->line, mat);
		return;
	}
	/* meandering ridges (the grooves between them stay dark) */
	int nr = c->crisp ? 3 : 6;
	int np = c->crisp ? 20 : 56;
	for (int j = 1; j <= nr; j++) {
		float s = (j - 0.35f) / nr;
		float f = 15.0f + 6.0f * hashf(k->seed, j), p0 = hashf(k->seed, j + 50) * US_TAU;
		float amp = 0.045f + 0.02f * hashf(k->seed, j + 90);
		float px = 0, py = 0;
		for (int i = 0; i <= np; i++) {
			float t = US_PI * (0.04f + 0.92f * i / np);
			float rr = s + amp * sinf(t * f + p0) * (0.6f + 0.4f * sinf(t * 3.0f + j));
			float x = X - cosf(t) * a * rr, y = Y - sinf(t) * b * rr;
			if (i)
				hair(c, px, py, x, y, ridgev * (0.5f + 0.5f * sinf(t)), mat);
			px = x, py = y;
		}
	}
	/* rim catching the light on its upper-left */
	ell_line(c, X, Y, a, b, US_PI * 1.05f, US_PI * 1.6f, 10, ridgev * 0.9f, mat);
}

static void draw_sponge(us_canvas *c, const look_t *lk, float X, float Y, float S, const col_t *k, float sway)
{
	uint8_t mat = lk->sil ? lk->mat : US_MAT_STEM;
	int nt = 3 + (int)(hashf(k->seed, 7) * 2.0f);
	for (int i = 0; i < nt; i++) {
		float h = hashf(k->seed, 30 + i);
		float off = ((i + 0.5f) / nt - 0.5f) * S * 0.5f;
		float hh = S * (0.55f + 0.45f * h) * (i == nt / 2 ? 1.1f : 1.0f);
		float rw = S * (0.075f + 0.03f * hashf(k->seed, 60 + i));
		float lean = off * 0.6f + sway * 0.01f * hh / S;
		float bx = X + off, tx = bx + lean, ty = Y - hh;
		cap_over(c, bx, Y, tx, ty, rw * 0.8f, rw, lk->fill, mat);
		if (lk->sil)
			continue;
		/* lit flank, the open rim and its dark mouth */
		hair(c, bx - rw * 0.55f, Y, tx - rw * 0.6f, ty, lk->line * 0.8f, mat);
		ell_over(c, tx, ty, rw * 1.05f, rw * 0.36f, lk->fill * 1.2f, mat, 0.0f, 0, 0.0f);
		ell_over(c, tx, ty + rw * 0.03f, rw * 0.72f, rw * 0.2f, lk->fill * 0.25f, mat, 0.0f, 0, 0.0f);
		ell_line(c, tx, ty, rw * 1.05f, rw * 0.36f, 0.0f, US_TAU, c->crisp ? 8 : 16, lk->line, mat);
	}
}

/* a gorgonian: ribs fanning from a short trunk, laced together, the whole fan bending in the surge */
static void draw_fan(us_canvas *c, const look_t *lk, float X, float Y, float S, const col_t *k, float bend)
{
	enum { NR = 15, NM = 8 };
	float px[NR][NM + 1], py[NR][NM + 1];
	uint8_t mat = lk->sil ? lk->mat : US_MAT_BLOOM;
	float oy = Y - 0.1f * S;
	for (int i = 0; i < NR; i++) {
		float th = -1.2f + 2.4f * i / (NR - 1) + 0.07f * (hashf(k->seed, i) - 0.5f);
		float rl = S * (0.8f + 0.2f * cosf(th)) * (0.9f + 0.1f * hashf(k->seed, i + 40));
		px[i][0] = X, py[i][0] = oy;
		for (int j = 1; j <= NM; j++) {
			float r = rl * (j + 0.35f * (hashf(k->seed, i * 31 + j) - 0.5f) * (j < NM)) / NM;
			float an = th + 0.06f * sinf(j * 1.7f + i * 2.3f) + 0.05f * (hashf(k->seed, i * 17 + j + 500) - 0.5f);
			float q = (r / S) * (r / S);
			px[i][j] = X + sinf(an) * r * 1.08f + bend * S * 0.3f * q;
			py[i][j] = oy - cosf(an) * r + fabsf(bend) * S * 0.06f * q;
		}
	}
	cap_over(c, X, Y, X, oy, S * 0.03f, S * 0.022f, lk->fill * 1.3f, mat);
	/* the fan's body: a faint web between the ribs */
	for (int i = 0; i + 1 < NR; i++)
		for (int j = 1; j < NM; j++) {
			float q[8] = {px[i][j], py[i][j], px[i + 1][j], py[i + 1][j], px[i + 1][j + 1], py[i + 1][j + 1],
				      px[i][j + 1], py[i][j + 1]};
			poly_over(c, q, 4, lk->fill, lk->fill * 0.8f, mat);
		}
	if (lk->sil) {
		for (int i = 0; i < NR; i += NR - 1)
			for (int j = 0; j < NM; j++)
				hair(c, px[i][j], py[i][j], px[i][j + 1], py[i][j + 1], lk->line, mat);
		for (int i = 0; i + 1 < NR; i++)
			hair(c, px[i][NM], py[i][NM], px[i + 1][NM], py[i + 1][NM], lk->line, mat);
		return;
	}
	for (int i = 0; i < NR; i++)
		for (int j = 0; j < NM; j++)
			hair(c, px[i][j], py[i][j], px[i][j + 1], py[i][j + 1], lk->line * (j < 2 ? 1.0f : 0.9f), mat);
	/* lace: cross-links, slightly sagging, and an outer rim */
	int step = c->crisp ? 2 : 1;
	for (int j = 2; j <= NM; j += step)
		for (int i = 0; i + 1 < NR; i++) {
			uint32_t h = hash2((int)k->seed + i, j);
			if (j < NM && (h & 7) == 0)
				continue; /* an irregular net: a few links missing ... */
			int j2 = (j < NM && j > 2 && (h & 24) == 8) ? j - 1 : j; /* ... and some running on a slant */
			float mx = 0.5f * (px[i][j] + px[i + 1][j2]), my = 0.5f * (py[i][j] + py[i + 1][j2]) + S * 0.006f;
			float v = lk->line * (j == NM ? 0.95f : 0.5f);
			hair(c, px[i][j], py[i][j], mx, my, v, mat);
			hair(c, mx, my, px[i + 1][j2], py[i + 1][j2], v, mat);
		}
	/* polyps along the rim */
	for (int i = 0; i < NR; i++)
		us_point(c, px[i][NM], py[i][NM], c->crisp ? 0.003f : 0.0018f, lk->tip, lk->tip > 0.75f ? US_MAT_LIGHT : mat);
}

/* anemone: a short column, an oral disc and a crown of tentacles swaying in the surge */
static void draw_anemone(us_canvas *c, const look_t *lk, float X, float Y, float S, const col_t *k, float sway, float t)
{
	uint8_t mat = US_MAT_BLOOM;
	float top = Y - 0.32f * S;
	cap_over(c, X, Y, X + sway * 0.03f * S, top, 0.2f * S, 0.27f * S, lk->fill, mat);
	float open = 0.55f + 0.45f * k->glow + 0.06f * sinf(k->ph);
	int nt = c->crisp ? 11 : 19;
	for (int i = 0; i < nt; i++) {
		float u = (float)i / (nt - 1) * 2.0f - 1.0f;
		float h = hashf(k->seed, i);
		float th = u * 1.15f;
		float L = S * (0.42f + 0.3f * open) * (0.85f + 0.3f * h) * (1.0f - 0.25f * fabsf(u));
		float curl = -u * (1.0f - open) * 0.55f;
		float x = X + sway * 0.03f * S + u * 0.24f * S, y = top + fabsf(u) * 0.02f * S;
		float ang = th;
		for (int sgi = 0; sgi < 4; sgi++) {
			float f = (sgi + 1) / 4.0f;
			ang += curl * 0.5f + sway * 0.16f * f + 0.08f * sinf(t * 1.3f + i * 2.1f + sgi);
			float l = L / 4.0f;
			float nx = x + sinf(ang) * l, ny = y - cosf(ang) * l;
			float r0 = S * 0.032f * (1.0f - 0.18f * sgi), r1 = S * 0.032f * (1.0f - 0.18f * (sgi + 1));
			cap_over(c, x, y, nx, ny, r0, r1, lk->fill * (1.0f + 0.25f * f), mat);
			x = nx, y = ny;
		}
		us_disc(c, x, y, c->crisp ? 0.003f : S * 0.03f, lk->tip, US_MAT_LIGHT);
	}
	if (!c->crisp)
		ell_line(c, X + sway * 0.03f * S, top, 0.25f * S, 0.06f * S, 0.0f, US_TAU, 14, lk->line, mat);
}

/* ------------------------------------------------------------------- the lives ---- */

/* side-view clownfish / small fish body: an ellipse with a tail, facing dir (+1 right / -1 left) */
static void draw_clown(us_canvas *c, float x, float y, float dir, float L, float v)
{
	float a = 0.5f * L, b = 0.22f * L;
	cap_over(c, x - dir * a * 0.9f, y, x - dir * a * 1.45f, y - b * 0.9f, b * 0.25f, b * 0.5f, v * 0.8f, US_MAT_LIGHT);
	cap_over(c, x - dir * a * 0.9f, y, x - dir * a * 1.45f, y + b * 0.9f, b * 0.25f, b * 0.5f, v * 0.8f, US_MAT_LIGHT);
	ell_over(c, x, y, a, b, v * 0.85f, US_MAT_LIGHT, 0.5f, 0, 0.0f);
	if (!c->crisp) {
		/* the white bands */
		wu_line(c, x + dir * a * 0.35f, y - b * 0.85f, x + dir * a * 0.3f, y + b * 0.85f, 1.0f, US_MAT_ACCENT);
		wu_line(c, x - dir * a * 0.2f, y - b * 0.95f, x - dir * a * 0.25f, y + b * 0.95f, 1.0f, US_MAT_ACCENT);
		us_put(c, (int)((x + dir * a * 0.62f) * c->s), (int)((y - b * 0.2f) * c->s), 0.05f, US_MAT_BG);
	} else {
		us_put(c, (int)((x + dir * a * 0.1f) * c->s), (int)(y * c->s), 1.0f, US_MAT_ACCENT);
	}
}

/* a butterflyfish: deep oval body, dorsal fin, tail, eye stripe. face in [-1,1]: turning = squashing through 0 */
static void draw_rfish(us_canvas *c, const rfish_t *f, float v, float t)
{
	float dir = f->face >= 0 ? 1.0f : -1.0f, sx = fmaxf(0.18f, fabsf(f->face));
	float L = f->L, a = 0.4f * L * sx, b = 0.3f * L, x = f->x, y = f->y;
	uint8_t mb = f->kind ? US_MAT_LIGHT : US_MAT_ACCENT;
	float wag = 0.15f * sinf(t * (6.0f + 10.0f * f->burst) + f->ph);
	/* tail */
	float tx = x - dir * a * 0.95f;
	float tq[8] = {tx + dir * 0.04f * L * sx, y, tx - dir * 0.3f * L * sx, y - 0.19f * L + wag * L * 0.3f,
		       tx - dir * 0.24f * L * sx, y + wag * L * 0.3f, tx - dir * 0.3f * L * sx, y + 0.19f * L + wag * L * 0.3f};
	poly_over(c, tq, 4, v * 0.75f, v * 0.65f, mb);
	/* a pointed snout */
	float sq[6] = {x + dir * a * 0.8f, y - b * 0.35f, x + dir * (a + 0.13f * L * sx), y + b * 0.02f, x + dir * a * 0.8f,
		       y + b * 0.3f};
	poly_over(c, sq, 3, v, v, mb);
	/* dorsal + anal fins */
	float dq[6] = {x + dir * a * 0.5f, y - b * 0.8f, x - dir * a * 1.05f, y - b * 0.4f, x - dir * a * 0.7f, y - b * 1.45f};
	poly_over(c, dq, 3, v * 0.75f, v * 0.7f, mb);
	float aq[6] = {x + dir * a * 0.2f, y + b * 0.85f, x - dir * a * 0.95f, y + b * 0.5f, x - dir * a * 0.55f, y + b * 1.15f};
	poly_over(c, aq, 3, v * 0.7f, v * 0.65f, mb);
	ell_over(c, x, y, a, b, v, mb, 0.45f, 0, 0.0f);
	/* eye stripe (dark band through the eye) and a body stripe */
	float ex = x + dir * a * 0.55f;
	if (c->crisp) {
		px_line(c, ex, y - b * 0.7f, ex, y + b * 0.6f, 0.0f, mb);
		for (int py = (int)((y - b * 0.7f) * c->s); py <= (int)((y + b * 0.6f) * c->s); py++)
			over(c, (int)(ex * c->s), py, v * 0.25f, 1.0f, US_MAT_EARTH);
	} else {
		cap_over(c, ex, y - b * 0.8f, ex - dir * a * 0.05f, y + b * 0.7f, b * 0.13f, b * 0.13f, v * 0.2f, US_MAT_EARTH);
		ell_line(c, x, y, a, b, 0.0f, US_TAU, 18, us_sat(v * 1.3f), mb);
		us_disc(c, ex, y - b * 0.15f, b * 0.12f, 1.0f, US_MAT_LIGHT);
		wu_line(c, x - dir * a * 0.3f, y - b * 0.9f, x - dir * a * 0.45f, y + b * 0.8f, v * 0.45f, mb);
	}
}

/* the turtle, side view: domed shell with scutes, head, front flipper beating on the kicks, rear flipper */
static void draw_turtle(us_canvas *c, const turtle_t *tu, float v)
{
	float d = (float)tu->dir, L = tu->L, x = tu->x, y = tu->y + 0.004f * sinf(tu->bob);
	float sk = v * 0.72f;
	float psi = 0.4f - (tu->fl + 0.6f) * 0.82f; /* flipper angle below the horizontal, pointing back */
	/* far-side front flipper (dimmer, a little behind in phase) */
	float p2 = psi + 0.25f;
	float sx0 = x + d * 0.2f * L, sy0 = y + 0.05f * L;
	cap_over(c, sx0, sy0, sx0 - d * cosf(p2) * 0.5f * L, sy0 + sinf(p2) * 0.5f * L, 0.05f * L, 0.015f * L, sk * 0.35f,
		 US_MAT_STEM);
	/* rear flipper */
	cap_over(c, x - d * 0.4f * L, y + 0.06f * L, x - d * 0.62f * L, y + 0.12f * L, 0.05f * L, 0.02f * L, sk * 0.5f,
		 US_MAT_STEM);
	/* head and neck */
	float hx = x + d * 0.62f * L, hy = y + 0.02f * L;
	cap_over(c, x + d * 0.4f * L, y + 0.04f * L, hx, hy, 0.07f * L, 0.065f * L, sk * 0.55f, US_MAT_STEM);
	ell_over(c, hx + d * 0.04f * L, hy, 0.1f * L, 0.075f * L, sk * 0.6f, US_MAT_STEM, 0.5f, 0, 0.0f);
	us_disc(c, hx + d * 0.08f * L, hy - 0.02f * L, c->crisp ? 0.002f : 0.009f * L, 0.05f, US_MAT_BG);
	/* plastron + shell */
	ell_over(c, x, y + 0.02f * L, 0.47f * L, 0.08f * L, v * 0.45f, US_MAT_EARTH, 0.0f, 0, 0.0f);
	ell_over(c, x, y + 0.02f * L, 0.5f * L, 0.32f * L, v * 0.5f, US_MAT_EARTH, 0.7f, 1, 0.0f);
	if (!c->crisp) {
		float sy = y + 0.02f * L;
		ell_line(c, x, sy, 0.5f * L, 0.32f * L, US_PI, US_TAU, 18, us_sat(v * 1.1f), US_MAT_EARTH);
		ell_line(c, x, sy, 0.3f * L, 0.2f * L, US_PI * 1.08f, US_PI * 1.92f, 10, v * 0.8f, US_MAT_EARTH);
		for (int k = 0; k < 5; k++) {
			float th = US_PI * (1.1f + 0.2f * k);
			wu_line(c, x + cosf(th) * 0.3f * L, sy + sinf(th) * 0.2f * L, x + cosf(th) * 0.5f * L,
				sy + sinf(th) * 0.32f * L, v * 0.7f, US_MAT_EARTH);
		}
		wu_line(c, x - 0.48f * L, sy, x + 0.48f * L, sy, v * 0.8f, US_MAT_EARTH);
	}
	/* near front flipper: long, tapering, a slight bend */
	float ex = sx0 - d * cosf(psi) * 0.32f * L, ey = sy0 + sinf(psi) * 0.32f * L + 0.02f * L;
	float p3 = psi + 0.35f;
	cap_over(c, sx0, sy0 + 0.02f * L, ex, ey, 0.065f * L, 0.045f * L, sk * 0.6f, US_MAT_STEM);
	cap_over(c, ex, ey, ex - d * cosf(p3) * 0.3f * L, ey + sinf(p3) * 0.3f * L, 0.045f * L, 0.012f * L, sk * 0.6f,
		 US_MAT_STEM);
	if (!c->crisp)
		wu_line(c, sx0, sy0, ex, ey - 0.03f * L, sk * 0.9f, US_MAT_STEM);
}

/* a spotted eagle ray seen a little from above: pointed wings that rise and beat down (on the kicks), a
 * duck-bill snout, a long whip of a tail; fl is the wingtip height (+1 up, -1 down) */
static void ray_pt(const ray_t *ry, float u, float w, float *ox, float *oy)
{
	float L = ry->L;
	float lift = ry->fl * 0.2f * L * powf(fminf(1.0f, fabsf(w) / 0.5f), 1.5f);
	*ox = ry->x + ry->dir * u * L * (1.0f - 0.12f * fabsf(ry->fl) * fabsf(w) / 0.5f);
	*oy = ry->y + w * L * 0.42f - lift;
}

static void draw_ray(us_canvas *c, const ray_t *ry, float v, float t)
{
	enum { NO = 8 };
	static const float pu[NO] = {0.36f, 0.31f, 0.2f, 0.1f, -0.12f, -0.07f, -0.15f, -0.2f};
	static const float pw[NO] = {0.0f, 0.045f, 0.075f, 0.24f, 0.5f, 0.28f, 0.1f, 0.04f};
	float L = ry->L;
	/* the tail first, a slow wave travelling down it */
	{
		float px, py;
		ray_pt(ry, -0.2f, 0.0f, &px, &py);
		for (int k = 1; k <= 12; k++) {
			float f = k / 12.0f;
			float u = -0.2f - 0.95f * f;
			float qx, qy;
			ray_pt(ry, u, 0.0f, &qx, &qy);
			qy += 0.03f * L * f * sinf(t * 2.0f - f * 6.0f + ry->ph);
			hair(c, px, py, qx, qy, v * (0.45f - 0.25f * f), US_MAT_WATER);
			px = qx, py = qy;
		}
	}
	float xy[2 * (2 * NO)];
	int n = 0;
	for (int i = 0; i < NO; i++, n++)
		ray_pt(ry, pu[i], pw[i], &xy[2 * n], &xy[2 * n + 1]);
	for (int i = NO - 1; i >= 1; i--, n++)
		ray_pt(ry, pu[i], -pw[i], &xy[2 * n], &xy[2 * n + 1]);
	/* the far wing is drawn darker: the near wing and the body over it */
	float far[2 * (NO + 2)];
	int nf = 0;
	ray_pt(ry, pu[2], 0.0f, &far[0], &far[1]), nf++;
	for (int i = 2; i < NO; i++, nf++)
		ray_pt(ry, pu[i], -pw[i], &far[2 * nf], &far[2 * nf + 1]);
	ray_pt(ry, pu[NO - 1], 0.0f, &far[2 * nf], &far[2 * nf + 1]), nf++;
	/* a dark back against the bright water, a paler rim where the light catches the wing edges */
	poly_over(c, xy, n, v * 0.12f, v * 0.1f, US_MAT_WATER);
	poly_over(c, far, nf, v * 0.08f, v * 0.07f, US_MAT_WATER);
	for (int i = 0; i < n; i++) {
		int j = (i + 1) % n;
		hair(c, xy[2 * i], xy[2 * i + 1], xy[2 * j], xy[2 * j + 1], v * 0.5f, US_MAT_WATER);
	}
	/* the head lobe and the eye */
	{
		float hx, hy;
		ray_pt(ry, 0.29f, 0.0f, &hx, &hy);
		ell_over(c, hx, hy, 0.05f * L, 0.03f * L, v * 0.14f, US_MAT_WATER, 0.5f, 0, 0.0f);
		float ex, ey;
		ray_pt(ry, 0.24f, 0.06f, &ex, &ey);
		us_disc(c, ex, ey, c->crisp ? 0.002f : 0.006f * L, v * 0.6f, US_MAT_ACCENT);
	}
	/* white spots over the back */
	int ns = c->crisp ? 6 : 18;
	for (int k = 0; k < ns; k++) {
		float h1 = hashf(0x5eedu, k), h2 = hashf(0x5eedu, k + 50);
		float u = -0.12f + 0.3f * h1, w = (h2 - 0.5f) * 2.0f * (0.06f + 0.35f * (0.18f - fabsf(u - 0.06f)) / 0.18f);
		float sx, sy;
		ray_pt(ry, u, w, &sx, &sy);
		if (c->crisp)
			us_put(c, (int)(sx * c->s), (int)(sy * c->s), us_sat(v * 0.7f), US_MAT_ACCENT);
		else
			us_disc(c, sx, sy, 0.0045f * L, us_sat(v * 0.6f), US_MAT_ACCENT);
	}
}

/* a day octopus on the reef: a bulbous mantle over a crown of curling arms; it jets off trailing its arms */
static void draw_octo(us_canvas *c, const octo_t *o, float v, float t, float sway)
{
	float m = o->stream;
	/* the mantle axis: up and a little back while it sits, leading the way when it jets */
	float vl = sqrtf(o->vx * o->vx + o->vy * o->vy) + 1e-6f;
	float sit_a = -US_PI * 0.5f - 0.35f + 0.08f * sinf(o->ph * 0.3f);
	float jet_a = atan2f(o->vy / vl, o->vx / vl);
	float da = jet_a - sit_a;
	while (da > US_PI) da -= US_TAU;
	while (da < -US_PI) da += US_TAU;
	float ax = sit_a + da * m;
	float cx = cosf(ax), cy = sinf(ax), px = -cy, py = cx;
	float bx = o->x, by = o->y;
	uint8_t mat = o->blanch > 0.4f ? US_MAT_ACCENT : o->mood > 0.5f ? US_MAT_BLOOM : US_MAT_EARTH;
	float vb = us_sat(v * (0.62f + 0.6f * o->blanch));
	/* the arms */
	for (int k = 0; k < 8; k++) {
		float side = k < 4 ? -1.0f : 1.0f;
		float a_sit = ax + US_PI + (k - 3.5f) * 0.4f;
		float a_jet = ax + US_PI + (k - 3.5f) * 0.05f;
		float ang = us_lerp(a_sit, a_jet, m);
		float x = bx, y = by;
		const int N = c->crisp ? 6 : 9;
		float seg = (0.072f + 0.012f * hashf(0x0c70u, k)) / N * (1.0f + 0.25f * m);
		for (int i = 0; i < N; i++) {
			float f = (float)i / N;
			float curl = (1.0f - m) * ((0.06f + 0.2f * f) * sinf(o->ph * 0.8f + k * 1.9f + f * 3.0f) + (f > 0.6f ? side * 0.5f * (f - 0.6f) : 0.0f));
			ang += curl + m * 0.05f * sinf(t * 6.0f + k + f * 5.0f) - (1.0f - m) * 0.06f * sway * f * cosf(ang);
			float nx = x + cosf(ang) * seg, ny = y + sinf(ang) * seg;
			float r0 = 0.0042f * (1.0f - f) + 0.0007f, r1 = 0.0042f * (1.0f - f - 1.0f / N) + 0.0007f;
			cap_over(c, x, y, nx, ny, r0, r1, vb * (k == 3 || k == 4 ? 0.85f : 0.7f), mat);
			if (!c->crisp && (i & 1) && i < N - 2) /* suckers along the underside */
				us_point(c, nx - sinf(ang) * side * r1 * 0.8f, ny + cosf(ang) * side * r1 * 0.8f, 0.0007f, us_sat(vb * 1.3f), US_MAT_ACCENT);
			x = nx, y = ny;
		}
	}
	/* head and mantle */
	float breathe = 1.0f + 0.07f * sinf(o->ph * 1.6f);
	cap_over(c, bx, by, bx + cx * 0.012f, by + cy * 0.012f, 0.0105f, 0.0115f, vb * 0.95f, mat);
	cap_over(c, bx + cx * 0.02f, by + cy * 0.02f, bx + cx * 0.032f, by + cy * 0.032f, 0.015f * breathe, 0.013f * breathe, vb, mat);
	if (!c->crisp) {
		/* mottling and the passing cloud: dark bands that sweep down the mantle */
		for (int k = 0; k < 7; k++) {
			float h1 = hashf(0x0c71u, k), h2 = hashf(0x0c72u, k);
			float s = 0.016f + 0.02f * h1, w = (h2 - 0.5f) * 0.02f;
			float mv = 0.5f + 0.5f * us_noise2(k * 3.1f, o->mt);
			us_disc(c, bx + cx * s + px * w, by + cy * s + py * w, 0.0022f, us_sat(vb * (1.2f + 0.4f * mv)), mat);
		}
		if (o->band < 1.0f)
			for (int b = 0; b < 2; b++) {
				float s = 0.04f - 0.04f * o->band - 0.008f * b;
				if (s < 0.004f || s > 0.04f)
					continue;
				float w = 0.013f;
				cap_over(c, bx + cx * s - px * w, by + cy * s - py * w, bx + cx * s + px * w, by + cy * s + py * w, 0.0022f, 0.0022f,
					 vb * 0.35f, mat);
			}
	} else if (o->band < 1.0f) {
		float s = 0.04f - 0.04f * o->band;
		us_disc(c, bx + cx * s, by + cy * s, 0.004f, vb * 0.4f, mat);
	}
	/* eyes: bumps at the side of the head with a dark slit */
	for (int e = -1; e <= 1; e += 2) {
		float ex = bx + cx * 0.011f + px * e * 0.0085f, ey = by + cy * 0.011f + py * e * 0.0085f;
		us_disc(c, ex, ey, c->crisp ? 0.003f : 0.0034f, us_sat(vb * 1.25f), US_MAT_ACCENT);
		if (!c->crisp)
			wu_line(c, ex - 0.0018f, ey, ex + 0.0018f, ey, 0.05f, US_MAT_BG);
	}
}

/* a green moray looking out of its crevice: a thick neck, a deep head with a high brow, the jaw working
 * as it breathes (side: +1 looks right) */
static void draw_moray(us_canvas *c, float hx, float hy, float hr, float side, float ext, float gape, float v)
{
	if (ext < 0.03f)
		return;
	float a = -0.25f;
	float dx = side * cosf(a), dy = sinf(a);
	float ux = dy * side, uy = -dx * side; /* the top of the head */
	if (uy > 0)
		ux = -ux, uy = -uy;
	float k = fmaxf(0.0105f, hr * 0.8f);
	float len = 0.004f + 0.065f * ext;
	float tx = hx + dx * len, ty = hy + dy * len;
	uint8_t mat = US_MAT_STEM;
#define MP(s_, o_, X, Y) (*(X) = tx + dx * (s_) * k + ux * (o_) * k, *(Y) = ty + dy * (s_) * k + uy * (o_) * k)
	/* the neck out of the hole, arching up a little like a snake's */
	{
		float mx = hx + dx * len * 0.5f + ux * len * 0.22f, my = hy + dy * len * 0.5f + uy * len * 0.22f;
		cap_over(c, hx, hy, mx, my, k * 1.05f, k * 1.02f, v * 0.42f, mat);
		cap_over(c, mx, my, tx, ty, k * 1.02f, k, v * 0.45f, mat);
	}
	/* upper head: brow, snout, the upper jaw's edge back to the corner of the mouth */
	static const float up[][2] = {{-0.3f, 1.0f}, {1.2f, 1.15f}, {2.4f, 0.8f}, {3.6f, 0.3f}, {3.9f, -0.05f}, {3.0f, -0.2f},
				      {1.8f, -0.25f}, {0.6f, -0.3f}, {-0.3f, -0.4f}};
	float xy[2 * 9];
	for (int i = 0; i < 9; i++)
		MP(up[i][0], up[i][1], &xy[2 * i], &xy[2 * i + 1]);
	/* lower jaw, hinged at the corner of the mouth */
	float ja = 0.08f + 0.42f * gape;
	float ca = cosf(ja), sa = sinf(ja);
	static const float lo[][2] = {{0.0f, 0.0f}, {3.2f, -0.05f}, {3.0f, -0.35f}, {1.5f, -0.6f}, {-0.6f, -0.75f}};
	float jw[2 * 5];
	for (int i = 0; i < 5; i++) {
		float ls = lo[i][0], lov = lo[i][1];
		float rs = ls * ca + lov * sa, ro = -ls * sa + lov * ca; /* rotate down about the hinge */
		MP(0.6f + rs, -0.3f + ro, &jw[2 * i], &jw[2 * i + 1]);
	}
	/* the open mouth */
	{
		float m[6] = {jw[0], jw[1], xy[8], xy[9], jw[2], jw[3]};
		poly_over(c, m, 3, v * 0.1f, v * 0.1f, US_MAT_EARTH);
	}
	poly_over(c, jw, 5, v * 0.45f, v * 0.4f, mat);
	poly_over(c, xy, 9, v * 0.58f, v * 0.48f, mat);
	if (!c->crisp) {
		for (int i = 0; i + 1 < 9; i++)
			wu_line(c, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], us_sat(v * (i < 4 ? 1.1f : 0.5f)), mat);
		for (int i = 0; i + 1 < 5; i++)
			wu_line(c, jw[2 * i], jw[2 * i + 1], jw[2 * i + 2], jw[2 * i + 3], us_sat(v * 0.6f), mat);
		/* fangs along the upper jaw */
		if (gape > 0.25f)
			for (int i = 0; i < 4; i++) {
				float px, py;
				MP(1.2f + 0.6f * i, -0.3f, &px, &py);
				wu_line(c, px, py, px, py + k * 0.22f, us_sat(v * 1.2f), US_MAT_ACCENT);
			}
		/* the fin ridge along the top of the neck */
		float nx0, ny0, nx1, ny1;
		MP(-len / k, 1.0f, &nx0, &ny0);
		MP(0.0f, 1.05f, &nx1, &ny1);
		wu_line(c, nx0, ny0, nx1, ny1, us_sat(v * 1.0f), mat);
	}
	/* the leopard spotting: pale spots over the neck and head */
	{
		int ns = c->crisp ? 4 : 16;
		for (int i = 0; i < ns; i++) {
			float s_ = (-len / k) * hashf(0x3011u, i) + 3.0f * hashf(0x3013u, i) * (i & 1);
			float o_ = (hashf(0x3012u, i) - 0.5f) * (s_ > 0 ? 1.0f : 1.5f), px, py;
			if (s_ > 2.4f && o_ < 0.0f)
				continue;
			MP(s_, o_, &px, &py);
			if (c->crisp)
				us_put(c, (int)(px * c->s), (int)(py * c->s), us_sat(v * 1.2f), US_MAT_LIGHT);
			else
				us_disc(c, px, py, k * 0.13f, us_sat(v * 1.15f), US_MAT_LIGHT);
		}
	}
	/* the eye, forward and high */
	float ex, ey;
	MP(2.3f, 0.45f, &ex, &ey);
	us_disc(c, ex, ey, c->crisp ? 0.002f : k * 0.28f, us_sat(v * 1.4f), US_MAT_LIGHT);
	if (!c->crisp)
		us_disc(c, ex, ey, k * 0.12f, 0.03f, US_MAT_BG);
#undef MP
}

/* a sea star on the rock: five arms, slightly foreshortened on the sloping face */
static void draw_star(us_canvas *c, float x, float y, float r, float rot, float v, float curl)
{
	float xy[2 * 10];
	for (int i = 0; i < 10; i++) {
		float a = rot + US_TAU * i / 10.0f;
		float rr = (i & 1) ? r * 0.36f : r * (1.0f - 0.12f * curl * ((i >> 1) & 1));
		xy[2 * i] = x + cosf(a) * rr;
		xy[2 * i + 1] = y + sinf(a) * rr * 0.72f;
	}
	poly_over(c, xy, 10, v * 0.75f, v * 0.62f, US_MAT_BLOOM);
	if (!c->crisp) {
		for (int i = 0; i < 10; i++) {
			int j = (i + 1) % 10;
			wu_line(c, xy[2 * i], xy[2 * i + 1], xy[2 * j], xy[2 * j + 1], us_sat(v * 1.05f), US_MAT_BLOOM);
		}
		for (int i = 0; i < 10; i += 2) /* a row of bumps down each arm */
			for (int k = 1; k <= 2; k++)
				us_point(c, us_lerp(x, xy[2 * i], k / 3.0f), us_lerp(y, xy[2 * i + 1], k / 3.0f), 0.0011f, us_sat(v * 1.2f), US_MAT_LIGHT);
	}
}

/* a seahorse, tail wrapped round a seagrass blade at (ax, ay); upright, facing dir; tuck bows the head */
static void draw_horse(us_canvas *c, float ax, float ay, float H, float dir, float lean, float tuck, float fin, float v, uint8_t mat)
{
	/* the spine from the tail's tip (curled round the blade) up to the back of the head (x facing, y up), radius */
	static const float P[][3] = {
		{-0.05f, 0.0f, 0.013f},  {-0.02f, 0.045f, 0.015f}, {0.04f, 0.03f, 0.017f}, {0.05f, -0.04f, 0.021f},
		{0.07f, -0.15f, 0.03f},  {0.09f, -0.28f, 0.048f},  {0.14f, -0.42f, 0.072f}, {0.13f, -0.56f, 0.08f},
		{0.07f, -0.68f, 0.062f}, {0.03f, -0.77f, 0.046f},  {0.07f, -0.86f, 0.054f},
	};
	enum { NP = 11, NECK = 9 };
	/* extra points: head front, snout tip, coronet, eye */
	static const float E[4][3] = {{0.14f, -0.88f, 0.042f}, {0.33f, -0.83f, 0.018f}, {0.05f, -0.96f, 0.024f}, {0.115f, -0.895f, 0.0f}};
	float xs[NP + 4], ys[NP + 4], rs[NP + 4];
	float cl = cosf(lean), sl = sinf(lean);
	float pvx = P[NECK][0], pvy = P[NECK][1], ct = cosf(tuck * 0.8f), st = sinf(tuck * 0.8f);
	for (int i = 0; i < NP + 4; i++) {
		const float *q = i < NP ? P[i] : E[i - NP];
		float lx = q[0], ly = q[1];
		rs[i] = q[2] * H;
		/* tuck: the head and neck bow down toward the chest */
		if (i >= NECK) {
			float qx = lx - pvx, qy = ly - pvy;
			lx = pvx + qx * ct - qy * st;
			ly = pvy + qx * st + qy * ct;
		}
		lx *= dir * H, ly *= H;
		xs[i] = ax + lx * cl - ly * sl;
		ys[i] = ay + lx * sl + ly * cl;
	}
	for (int i = 0; i + 1 < NP; i++)
		cap_over(c, xs[i], ys[i], xs[i + 1], ys[i + 1], rs[i], rs[i + 1], v * (0.55f + 0.025f * i), mat);
	cap_over(c, xs[NP - 1], ys[NP - 1], xs[NP], ys[NP], rs[NP - 1], rs[NP], v * 0.78f, mat);         /* head */
	cap_over(c, xs[NP], ys[NP], xs[NP + 1], ys[NP + 1], rs[NP + 1], rs[NP + 1] * 0.9f, v * 0.72f, mat); /* snout */
	cap_over(c, xs[NP - 1], ys[NP - 1], xs[NP + 2], ys[NP + 2], rs[NP - 1] * 0.6f, rs[NP + 2], v * 0.72f, mat); /* coronet */
	us_disc(c, xs[NP + 3], ys[NP + 3], c->crisp ? 0.0015f : 0.004f * H / 0.06f, 0.05f, US_MAT_BG);
	if (!c->crisp) {
		/* the bony rings down the trunk and tail */
		for (int i = 3; i < NECK; i++) {
			float dxs = xs[i + 1] - xs[i - 1], dys = ys[i + 1] - ys[i - 1];
			float d = sqrtf(dxs * dxs + dys * dys) + 1e-6f;
			float nx = -dys / d, ny = dxs / d;
			for (int h = 0; h < 2; h++) {
				float mx = us_lerp(xs[i], xs[i + 1], 0.5f * h), my = us_lerp(ys[i], ys[i + 1], 0.5f * h);
				float rr = us_lerp(rs[i], rs[i + 1], 0.5f * h) * 0.9f;
				wu_line(c, mx - nx * rr, my - ny * rr, mx + nx * rr, my + ny * rr, us_sat(v * 1.05f), mat);
			}
		}
		/* the dorsal fin on its back, a blur of fluttering rays */
		float fx = xs[6] - dir * rs[6] * 0.9f, fy = ys[6];
		for (int k = 0; k < 4; k++) {
			float o = (k - 1.5f) * 0.014f * H;
			wu_line(c, fx, fy + o * 0.6f, fx - dir * 0.08f * H, fy + o * 1.6f + 0.025f * H * fin * sinf(k * 2.3f), us_sat(v * 0.9f), mat);
		}
	}
}

/* --------------------------------------------------------------------- the frame ---- */

static float water_v(float y, float crisp)
{
	/* brighter under the surface, deepening toward the floor */
	float d = us_sat((y - Y_SURF) / (Y_HOR - Y_SURF));
	return (0.075f + 0.2f * expf(-d * 2.6f)) * (crisp ? 1.25f : 1.0f);
}

static float surf_y(float x, float t, float lurch)
{
	return Y_SURF + 0.006f * sinf(x * 7.0f + t * 0.7f) + 0.003f * sinf(x * 19.0f - t * 1.3f) +
	       0.014f * lurch * sinf(x * 4.0f - t * 2.0f);
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *st = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	us_rng *r = &st->rng, *r2 = &st->rng2;
	float dt = ctx->dt, rdt = ctx->real_dt, A = ctx->aspect, dens = ctx->p->density;
	float t = (float)ctx->t;
	if (!st->inited)
		init_scene(st, ctx);
	int crisp = ctx->crisp;

	/* ---- envelopes ---- */
	st->low_env = us_approach(st->low_env, us_sat(a->lowmid * 0.8f + a->mid * 0.3f), 1.5f, rdt);
	st->bass_env = us_approach(st->bass_env, us_sat(a->bass * 0.7f + a->kick * 0.7f), 0.1f, rdt);
	st->hi_env = us_approach(st->hi_env, us_sat(a->high + 0.6f * a->air), 0.2f, rdt);
	st->lvl_env = us_approach(st->lvl_env, us_sat(a->level), 0.35f, rdt);
	/* the light follows loudness more slowly: a crescendo brightens the water, single beats do not pulse it */
	st->lvl_slow = us_approach(st->lvl_slow, us_sat(a->level), 0.5f, rdt);
	st->swell_k = us_approach(st->swell_k, us_sat(0.5f * a->swell + 0.7f * a->level), 1.2f, rdt);
	st->open_env = us_approach(st->open_env, us_sat(0.15f + 0.55f * a->bass + 0.45f * a->level), 0.8f, rdt);
	int kick_ev = a->kick_hit, snare_ev = a->snare_hit, hat_ev = a->hat_hit;
	if (kick_ev) {
		st->kick_wave = us_sat(0.4f + a->kick);
		st->kick_wave_x = -0.1f;
		st->kick_s = 1.0f;
	}
	st->kick_s = us_approach(st->kick_s, 0.0f, 0.35f, rdt);
	if (snare_ev)
		st->snare_s = 1.0f;
	st->snare_s = us_approach(st->snare_s, 0.0f, 0.18f, rdt);
	if (hat_ev) {
		st->hat_s = us_sat(0.75f + 0.5f * a->hat);
		st->glint_seed = us_rng_u32(r);
	}
	st->hat_s = us_approach(st->hat_s, 0.0f, 0.07f, rdt);
	float hat = st->hat_s;
	float flinch = 1.4f * us_sat((0.5f * (a->high + a->air) - 0.45f) * 2.5f) * a->active;
	st->sub_env = us_approach(st->sub_env, us_sat(a->sub * 1.1f), 0.4f, rdt);
	st->surge_amp = us_approach(st->surge_amp, 0.3f + 0.9f * us_sat(0.7f * a->bass + 0.5f * a->sub), 0.7f, rdt);
	st->surge_ph += dt * (0.8f + 0.6f * st->bass_env);
	/* the kick's surge rides on the groundswell: a shove that eases back */
	float swell_s = 0.6f * sinf(st->surge_ph) * st->surge_amp, shove = 1.6f * st->kick_s;
	float surge = swell_s + shove;
	st->mid_env = us_approach(st->mid_env, us_sat(a->mid * 1.1f), a->mid > st->mid_env ? 0.12f : 0.5f, rdt);
	st->cent_env = us_approach(st->cent_env, a->centroid, 2.0f, rdt);
	st->cur_ang = us_approach(st->cur_ang, 0.6f * us_noise2(7.7f, t * 0.008f) + 1.6f * (a->width - 0.35f), 3.0f, rdt);
	float cur = us_clampf(st->cur_ang, -1.2f, 1.2f); /* signed current across the screen */
	st->rip_drift += dt * (0.05f + 0.25f * st->sub_env);
	st->lurch = st->kick_s; /* a sharp shove, a slow easing back */
	st->stir = us_approach(st->stir, us_sat(st->kick_s * (0.6f + 0.6f * a->kick)), st->kick_s > st->stir ? 0.04f : 0.35f, rdt);
	if (snare_ev)
		st->duck_to = 1.0f;
	st->duck_to = us_approach(st->duck_to, 0.0f, 0.25f, rdt);
	st->duck = us_approach(st->duck, st->duck_to, 0.06f, rdt);
	st->kick_wave_x += rdt * 1.6f * A;
	st->kick_wave = us_approach(st->kick_wave, 0.0f, 0.9f, rdt);
	float nutrients = us_sat(0.4f * w->pollen + 0.6f * w->water_level);
	st->flow_t += dt * (0.02f + 0.05f * a->level);
	st->caus_t += dt * (0.08f + 0.1f * st->lvl_env + 0.08f * st->hi_env);
	st->ray_t += dt * (1.0f + 0.8f * st->lvl_env);
	st->intro_t += rdt;
	float fa = us_smooth(0.0f, 1.5f, st->intro_t);

	/* ---- light ---- */
	float day = 0.5f + 0.5f * w->daylight;
	float lum = (crisp ? 0.34f + 0.9f * st->lvl_slow : 0.42f + 0.78f * st->lvl_slow) * day * fa; /* loudness lets the sunlight down */
	float rays = (0.12f + 0.2f * w->daylight) * (0.3f + 1.1f * st->swell_k);
	float clear = 0.6f + 0.6f * st->cent_env; /* water clarity: how far you see */
	float fl = crisp ? 1.25f : 1.0f;

	/* ---- colonies: growth, polyps ---- */
	int ncol = st->nfixed + (int)(dens * (st->ncol - st->nfixed) + 0.5f);
	if (ncol > st->ncol)
		ncol = st->ncol;
	{
		float gt = us_sat(0.6f + 0.3f * st->low_env + 0.2f * nutrients);
		float sum = 0;
		for (int i = 0; i < st->ncol; i++) {
			col_t *k = &st->col[i];
			k->grow = us_approach(k->grow, gt, 40.0f, dt * (0.5f + st->low_env));
			sum += k->grow;
			k->ph += dt * 0.7f;
			if ((hat_ev && us_rand(r) < 0.25f + 0.3f * hat) || us_rand(r) < flinch * dt)
				k->shy = 1.0f;
			k->shy = us_approach(k->shy, 0.0f, 0.3f, rdt);
			float fx = k->fx * A;
			float wave = st->kick_wave * expf(-fabsf(fx - st->kick_wave_x) * 6.0f);
			float target = us_sat(st->open_env * (0.8f + 0.2f * sinf(k->ph)) + wave) * (1.0f - 0.85f * k->shy);
			k->open = us_approach(k->open, target, target > k->open ? 0.12f : 0.4f, rdt);
			if (k->type == CT_ANEM) {
				float band = a->bands[9 + (i * 5) % 14];
				k->glow = us_approach(k->glow, us_sat(0.55f * st->mid_env + 0.6f * band), band > k->glow ? 0.1f : 0.45f,
						      rdt);
			}
		}
		st->cover = sum / (float)st->ncol;
		us_world_add(w, &w->reef, 0.02f * (us_sat(st->cover * 0.9f) - w->reef), dt);
	}

	/* ---- spawning: each snare, a colony or two release a puff of spawn that drifts up toward the light ---- */
	if (snare_ev) {
		for (int puff = 0; puff < 2; puff++) {
			col_t *k = &st->col[(int)(us_rand(r) * ncol) % ncol];
			if (k->type == CT_ANEM || k->type == CT_SPONGE)
				k = &st->col[puff ? 5 : 6]; /* a staghorn / the brain coral */
			float X = k->fx * A, Y = col_base_y(st, k);
			if (k->rock == 2)
				Y = 0.95f;
			float S = k->size * (0.6f + 0.4f * k->grow);
			int nb = 8 + (int)(dens * 8.0f + 4.0f * st->lvl_env);
			for (int b = 0; b < nb && st->nsp < MAX_SP; b++) {
				spawn_t *q = &st->sp[st->nsp++];
				q->x = X + us_randn(r) * S * 0.25f;
				q->y = Y - S * us_randr(r, 0.2f, 0.6f);
				float an = us_randr(r, -2.4f, -0.7f);
				float spd = us_randr(r, 0.03f, 0.08f);
				q->vx = cosf(an) * spd;
				q->vy = sinf(an) * spd;
				q->life = us_randr(r, 4.0f, 8.0f);
				q->age = 0;
			}
		}
	}

	/* ================================================================ draw ======== */

	/* ---- the water column: depth gradient + sun shafts ---- */
	{
		float lurch = 0.05f * st->lurch;
		for (int j = 0; j < WFH; j++) {
			float y = (j + 0.5f) / WFH * Y_HOR;
			float base = water_v(y, crisp);
			float fade = expf(-(y - Y_SURF) * 1.6f);
			for (int i = 0; i < WFW; i++) {
				float x = (i + 0.5f) / WFW; /* fraction of width */
				float ray = 0;
				for (int k = 0; k < NRAY; k++) {
					float x0 = st->ray_x[k] + 0.02f * sinf(st->ray_t * 0.11f + st->ray_ph[k]) + lurch;
					float off = (x - x0 - (y - Y_SURF) * 0.32f / A) * A;
					float wd = st->ray_w[k] * (1.0f + 1.4f * y) * (crisp ? 1.7f : 1.0f);
					float pr = off * off / (wd * wd);
					if (pr < 9.0f)
						ray += expf(-pr) * (0.6f + 0.4f * sinf(st->ray_t * 0.3f + st->ray_ph[k] * 3.0f));
				}
				float wv = (base + rays * 0.28f * (1.0f + 4.8f * st->lurch) * ray * fade) * lum;
				/* pixel palettes: keep the open water off the background shade, so silhouettes read against it */
				st->wf[j * WFW + i] = wv;
			}
		}
		backdrop(c, st->wf, WFW, WFH, US_MAT_WATER, 0.0f, Y_HOR + 0.002f);
	}
	/* ---- the surface seen from below: a bright rippling ceiling ---- */
	{
		int n = crisp ? 64 : 160;
		float px = 0, py = 0;
		for (int i = 0; i <= n; i++) {
			float x = A * i / n;
			float y = surf_y(x, t, st->lurch);
			float sun = 0.75f + 0.35f * expf(-(x - 0.35f * A) * (x - 0.35f * A) * 3.0f);
			if (i) {
				/* fill the sliver above the line */
				int x0p = (int)(px * c->s), x1p = (int)(x * c->s);
				for (int xp = x0p; xp < x1p; xp++) {
					float yy = us_lerp(py, y, (float)(xp - x0p) / (float)(x1p - x0p + 1));
					int ye = (int)(yy * c->s);
					for (int yp = 0; yp < ye; yp++)
						over(c, xp, yp, 0.4f * fl * lum, 1.0f, US_MAT_WATER);
				}
				hair(c, px, py, x, y, us_sat(0.75f * sun * lum + 0.2f * st->lurch + 0.6f * hat), US_MAT_LIGHT);
			}
			px = x, py = y;
		}
		/* the underside's ripple pattern just below the line */
		if (!crisp)
			for (int row = 1; row <= 3; row++) {
				float yo = 0.008f * row;
				float qx = 0, qy = 0;
				for (int i = 0; i <= 80; i++) {
					float x = A * i / 80.0f;
					float y = surf_y(x, t, st->lurch) + yo + 0.003f * sinf(x * (30.0f + 9.0f * row) + t * (1.0f + 0.3f * row));
					if (i)
						wu_line(c, qx, qy, x, y, 0.28f * lum / row, US_MAT_WATER);
					qx = x, qy = y;
				}
			}
	}

	/* ---- the sand floor: brighter toward us, caustic light net, ripples ---- */
	{
		float lurch = 0.7f * st->lurch;
		/* hats: the sunlight net on the sand glitters; a kick's swell overhead focuses it brighter */
		float glint = 1.0f + 1.3f * hat + 2.5f * st->lurch;
		for (int j = 0; j < SFH; j++) {
			float d = (j + 0.5f) / SFH; /* 0 far .. 1 near */
			float zz = 1.0f / (d + 0.12f);
			float base = (0.13f + 0.2f * d) * fl;
			for (int i = 0; i < SFW; i++) {
				float x = ((i + 0.5f) / SFW - 0.5f) * A;
				float u = x * zz * 0.9f;
				float n1 = 1.0f - fabsf(us_noise3(u * 2.2f + lurch, zz * 2.4f, st->caus_t));
				float n2 = 1.0f - fabsf(us_noise3(u * 3.7f + 5.0f, zz * 4.0f + lurch, st->caus_t * 1.3f));
				float cs = n1 * n1 * n1 * n1 * n1 * n1 + 0.5f * n2 * n2 * n2 * n2 * n2 * n2;
				/* the kick's surge stirs a cloud of sand off the floor */
				float stir = st->stir * d * us_sat(0.5f + 0.8f * us_noise3(u * 0.8f, zz * 0.9f, t * 0.5f));
				float sv = (base + cs * (0.08f + 0.22f * d) * (0.4f + 0.6f * clear) * glint + 0.4f * stir) * lum;
				st->sf[j * SFW + i] = sv;
			}
		}
		backdrop(c, st->sf, SFW, SFH, US_MAT_EARTH, Y_HOR, 1.0f);
		/* ripple crests, closer together toward the horizon */
		int nl = crisp ? 9 : 16;
		for (int k = 1; k <= nl; k++) {
			float y0 = Y_HOR + (1.05f - Y_HOR) / (1.0f + 0.42f * k);
			float d = (y0 - Y_HOR) / (1.0f - Y_HOR);
			float amp = 0.006f * d, f = 9.0f / (0.25f + d);
			int n = crisp ? 40 : 96;
			float qx = 0, qy = 0;
			for (int i = 0; i <= n; i++) {
				float x = A * i / n;
				float y = y0 + amp * sinf(x * f + st->rip_drift + k * 1.7f) + 0.5f * amp * us_noise2(x * 5.0f, k * 3.3f);
				if (i)
					hair(c, qx, qy, x, y, (0.1f + 0.22f * d) * fl * lum, US_MAT_EARTH);
				qx = x, qy = y;
			}
		}
	}

	/* ---- the far ridge on the horizon, nearly lost in the haze ---- */
	{
		float hz = us_sat(0.45f + 0.4f * clear);
		int x0 = 0, x1 = c->w;
		float py = 0;
		for (int xp = x0; xp < x1; xp++) {
			float fx = (xp + 0.5f) / c->w * (NFAR - 1);
			int i = (int)fx;
			if (i >= NFAR - 1)
				i = NFAR - 2;
			float h = us_lerp(st->far_h[i], st->far_h[i + 1], fx - i);
			float top = Y_HOR - h * hz;
			float v = water_v(top, crisp) * (crisp ? 0.86f : 0.72f) * lum;
			int yt = (int)(top * c->s), yb = (int)((Y_HOR + 0.008f) * c->s);
			for (int yp = yt; yp <= yb; yp++)
				over(c, xp, yp, v, 1.0f, US_MAT_WATER);
			if (xp)
				hair(c, (xp - 0.5f) / c->s, py, (xp + 0.5f) / c->s, top, v * 1.6f, US_MAT_WATER);
			py = top;
		}
	}

	/* ---- mid-distance outcrops: silhouettes with their fans and branches ---- */
	{
		int nm = 2 + (int)(dens * 2.0f + 0.5f);
		for (int k = 0; k < st->nmr && k < nm; k++) {
			rock_t *rk = &st->mr[k];
			float yt0 = rk->base - rk->h;
			float vs = water_v(yt0, crisp) * (crisp ? 0.74f : 0.55f) * lum * (1.15f - 0.25f * clear);
			float rim = crisp ? fmaxf(vs * 1.9f, 0.13f * fa) : vs * 1.9f; /* pixel: keep the rim off the background shade */
			look_t lk = {lum, vs, rim, 0, 1, US_MAT_WATER};
			for (int j = 0; j < st->nmc; j++) {
				col_t *q = &st->mc[j];
				if (q->rock != k)
					continue;
				float X = q->fx * A, Y = rock_top(rk, q->fx) + 0.004f;
				if (q->type == CT_FAN)
					draw_fan(c, &lk, X, Y, q->size, q, 0.3f * surge * 0.5f + 0.15f * cur);
				else if (q->type == CT_STAG)
					draw_staghorn(c, &lk, X, Y, q->size, q, 0.0f);
				else if (q->type == CT_BRAIN)
					draw_brain(c, &lk, X, Y, q->size, q, 0.0f);
				else
					draw_sponge(c, &lk, X, Y, q->size, q, 0.0f);
			}
			int xa = (int)((rk->cx - rk->hw) * A * c->s), xb = (int)((rk->cx + rk->hw) * A * c->s);
			float py = 9.0f; /* no previous column yet */
			for (int xp = xa; xp <= xb; xp++) {
				float top = rock_top(rk, (xp + 0.5f) / (A * c->s));
				if (top > 2.0f)
					continue;
				int yt = (int)(top * c->s), yb = (int)(rk->base * c->s);
				for (int yp = yt; yp <= yb; yp++)
					over(c, xp, yp, vs, 1.0f, US_MAT_WATER);
				if (xp > xa && py < 2.0f)
					hair(c, (xp - 0.5f) / c->s, py, (xp + 0.5f) / c->s, top, rim, US_MAT_WATER);
				py = top;
			}
		}
	}

	/* ---- the deep school, behind the reef ---- */
	float life = us_sat(0.35f + 0.4f * w->flock + 0.3f * w->pollen + 0.3f * w->reef);
	int nf = 10 + (int)((MAX_FISH - 10) * (0.3f + 0.7f * dens) * life);
	if (nf > MAX_FISH)
		nf = MAX_FISH;
	int ns = 2 + (dens > 0.3f);
	float SW = A + 0.3f;
	for (int k = 0; k < ns; k++) {
		school_t *s = &st->sc[k];
		if (kick_ev && us_rand(r) < 0.8f) {
			float side = us_rand(r) < 0.5f ? -1.0f : 1.0f;
			s->head_to += side * us_randr(r, 0.7f, 1.6f) * (0.5f + 0.5f * a->onset);
			s->burst_to = us_sat(s->burst_to + 0.45f + 0.4f * a->onset);
			s->scare = us_sat(s->scare + 0.5f * a->onset);
		}
		s->burst_to = us_approach(s->burst_to, 0.0f, 0.6f, rdt);
		s->burst = us_approach(s->burst, s->burst_to, 0.12f, rdt);
		s->scare = us_approach(s->scare, 0.0f, 1.2f, rdt);
		s->turn = us_noise2(k * 13.7f, st->flow_t * 4.0f + k) * 0.7f;
		float hx = cosf(s->head), hy = sinf(s->head);
		float yc = s->deep ? 0.44f : 0.32f;
		float wx = hx >= 0 ? 1.0f : -1.0f;
		float wy = (yc - s->y) * (fabsf(s->y - yc) > 0.12f ? 5.0f : 1.2f);
		float wl = sqrtf(wx * wx + wy * wy);
		s->head_to += (s->turn + 1.8f * (hx * wy - hy * wx) / wl) * dt;
		s->head = us_approach(s->head, s->head_to, 0.22f, rdt);
		float spd = s->speed * (1.0f + 0.6f * st->lvl_env + 1.8f * s->burst) * (s->deep ? 0.65f : 1.0f);
		float cvx = cosf(s->head) * spd, cvy = sinf(s->head) * spd;
		s->x += cvx * dt;
		s->y += cvy * dt;
		if (s->y < 0.12f) { s->y = 0.12f; s->head = -s->head; s->head_to = -s->head_to; }
		if (s->y > 0.6f) { s->y = 0.6f; s->head = -s->head; s->head_to = -s->head_to; }
		if (s->x > A + 0.15f) s->x -= SW;
		if (s->x < -0.15f) s->x += SW;
		float ca = cosf(s->head), sa = sinf(s->head);
		float spread = 1.0f + 2.2f * s->scare;
		for (int i = 0; i < nf; i++) {
			fish_t *f = &s->f[i];
			float tx = (f->sa * ca - f->sc * sa) * spread, ty = (f->sa * sa + f->sc * ca) * spread;
			float jit = us_noise2(i * 3.1f + k * 17.0f, st->flow_t * 9.0f);
			float wvx = cvx + (tx - f->ox) * 1.6f + jit * 0.01f * -sa;
			float wvy = cvy + (ty - f->oy) * 1.6f + jit * 0.01f * ca;
			float tau = f->tau * (s->burst > 0.3f ? 0.6f : 1.0f);
			f->vx = us_approach(f->vx, wvx, tau, dt);
			f->vy = us_approach(f->vy, wvy, tau, dt);
			f->ox += (f->vx - cvx) * dt;
			f->oy += (f->vy - cvy) * dt;
			float vl = sqrtf(f->vx * f->vx + f->vy * f->vy) + 1e-6f;
			float ndx = f->vx / vl, ndy = f->vy / vl;
			float turn = fabsf(ndx * f->dy - ndy * f->dx);
			f->dx = ndx, f->dy = ndy;
			f->flash = us_approach(f->flash, us_sat(turn * 6.0f), 0.15f, dt);
		}
	}
	/* fish drawing, shared by the deep school (now) and the near ones (after the reef) */
#define DRAW_SCHOOL(K)                                                                                                  \
	do {                                                                                                            \
		school_t *s = &st->sc[K];                                                                               \
		for (int i = 0; i < nf; i++) {                                                                          \
			fish_t *f = &s->f[i];                                                                           \
			float x = s->x + f->ox, y = s->y + f->oy;                                                       \
			if (x > A + 0.15f) x -= SW;                                                                     \
			if (x < -0.15f) x += SW;                                                                        \
			if (x < -0.03f || x > A + 0.03f)                                                                \
				continue;                                                                               \
			float sz = s->deep ? 0.6f : 1.0f;                                                               \
			float L = (crisp ? 0.017f : 0.013f) * sz;                                                       \
			float vv = us_sat(0.55f + 0.4f * f->flash + 0.1f * s->burst) * (s->deep ? 0.5f : 1.0f) * fa *    \
				   (0.55f + 0.45f * lum);                                                               \
			uint8_t fm = s->deep ? US_MAT_WATER : US_MAT_ACCENT;                                            \
			float ndx = f->dx, ndy = f->dy;                                                                 \
			float bx = x - ndx * L, by = y - ndy * L;                                                       \
			if (crisp) {                                                                                    \
				if (s->deep)                                                                            \
					px_line(c, x, y, bx, by, vv, fm);                                               \
				else                                                                                    \
					us_line(c, x, y, bx, by, 0.0045f, vv, fm);                                      \
			} else {                                                                                        \
				float pxn = -ndy, pyn = ndx;                                                            \
				float mx = x - ndx * L * 0.4f, my = y - ndy * L * 0.4f;                                 \
				float bw = 0.0022f * sz;                                                                \
				cap_over(c, x - ndx * L * 0.1f, y - ndy * L * 0.1f, bx, by, bw, bw * 0.3f, vv * 0.55f, fm); \
				wu_line(c, x, y, mx + pxn * bw, my + pyn * bw, vv, fm);                                 \
				wu_line(c, x, y, mx - pxn * bw, my - pyn * bw, vv * 0.8f, fm);                          \
				wu_line(c, mx + pxn * bw, my + pyn * bw, bx, by, vv * 0.85f, fm);                       \
				wu_line(c, mx - pxn * bw, my - pyn * bw, bx, by, vv * 0.7f, fm);                        \
				float bt = sinf(t * (14.0f + 20.0f * s->burst) + i) * 0.0012f * sz;                     \
				float tl = 0.004f * sz, tw = 0.0026f * sz;                                              \
				wu_line(c, bx, by, bx - ndx * tl + pxn * (tw + bt), by - ndy * tl + pyn * (tw + bt), vv * 0.7f, fm); \
				wu_line(c, bx, by, bx - ndx * tl - pxn * (tw - bt), by - ndy * tl - pyn * (tw - bt), vv * 0.7f, fm); \
			}                                                                                               \
		}                                                                                                       \
	} while (0)
	for (int k = 0; k < ns; k++)
		if (st->sc[k].deep)
			DRAW_SCHOOL(k);

	/* ---- the reef: side outcrop, then the main one, each with its colonies (back to front) ---- */
	float sn = st->snare_s * (crisp ? 1.7f : 1.0f); /* pixel palettes need a bigger step to show */
	for (int ri = 1; ri >= 0; ri--) {
		rock_t *rk = &st->rk[ri];
		float near = ri == 0 ? 1.0f : 0.8f;
		float vtop = 0.5f * fl * lum * near, vbot = 0.22f * fl * lum * near;
		int xa = (int)((rk->cx - rk->hw) * A * c->s), xb = (int)((rk->cx + rk->hw) * A * c->s);
		float py = 0;
		int have = 0;
		for (int xp = xa; xp <= xb; xp++) {
			float fx = (xp + 0.5f) / (A * c->s);
			float top = rock_top(rk, fx);
			if (top > 2.0f) {
				have = 0;
				continue;
			}

			float cofs = 0.012f * us_noise2(fx * 24.0f, rk->nofs + 2.0f);
			/* the foot is buried in sand: an uneven lower edge */
			float bot = rk->base - 0.012f - 0.01f * us_noise2(fx * 18.0f, rk->nofs + 7.0f);
			int yt = (int)(top * c->s), yb = (int)(bot * c->s);
			if (yt < 0) yt = 0;
			if (yb > c->h - 1) yb = c->h - 1;
			if (xp < 0 || xp >= c->w)
				continue;
			/* lit top, shadowed lower face; lumpy bands of encrusting growth (a gentle ripple down the face) */
			float iq = 1.0f / ((bot - top) * c->s + 1e-3f), q0 = ((yt + 0.5f) - top * c->s) * iq;
			float ph = (yt + 0.5f) / c->s + cofs, dph = 1.0f / c->s;
			for (int yp = yt; yp <= yb; yp++, q0 += iq, ph += dph) {
				float q = us_sat(q0);
				q = q * (2.0f - q); /* ~sqrt: the shade falls off fast below the crest */
				float band = crisp ? 1.0f : 0.9f + 0.1f * fast_sin(ph * 150.0f);
				float v = us_sat((vtop + (vbot - vtop) * q) * band);
				size_t k = (size_t)yp * c->w + xp;
				c->ink[k] = (uint8_t)(v * 255.0f + 0.5f), c->mat[k] = US_MAT_EARTH;
			}
			if (have)
				hair(c, (xp - 0.5f) / c->s, py, (xp + 0.5f) / c->s, top, 0.55f * lum * near, US_MAT_EARTH);
			py = top, have = 1;
		}
		/* crevices (dark holes with a lit lower lip) */
		for (int h = 0; h < rk->nh; h++) {
			float fx = rk->cx + rk->hu[h] * rk->hw;
			float top = rock_top(rk, fx);
			if (top > 2.0f)
				continue;
			float y = top + (rk->base - top) * rk->hd[h], X = fx * A;
			float hr = rk->hr[h];
			ell_over(c, X, y, hr * 1.4f, hr * 0.8f, vbot * 0.6f, US_MAT_EARTH, 0.0f, 0, 0.0f);
			ell_line(c, X, y, hr * 1.4f, hr * 0.8f, 0.15f, US_PI - 0.15f, 8, vtop * 1.3f, US_MAT_EARTH);
		}
		/* encrusting life on the rock face: small knobs of coralline algae and button polyps */
		if (!crisp || ri == 0) {
			int nd = crisp ? 24 : 90;
			for (int d = 0; d < nd; d++) {
				float u = hashf((uint32_t)(ri * 7919 + 17), d) * 1.8f - 0.9f;
				float fx = rk->cx + u * rk->hw;
				float top = rock_top(rk, fx);
				if (top > 2.0f)
					continue;
				float dd = hashf((uint32_t)(ri * 7919 + 18), d);
				float y = top + (rk->base - top - 0.02f) * dd * dd + 0.006f;
				float rr = (crisp ? 0.004f : 0.0028f) * (0.6f + 0.8f * hashf((uint32_t)(ri + 99), d));
				int pink = (d % 3) == 0;
				float v = (pink ? 0.42f : 0.36f) * fl * lum * near * (1.0f - 0.4f * dd);
				ell_over(c, fx * A, y, rr * 1.3f, rr, v, pink ? US_MAT_BLOOM : US_MAT_LEAF, 0.6f, 0, 0.0f);
			}
		}
		/* colonies, back to front by kind */
		static const int order[CT_COUNT] = {CT_FAN, CT_SPONGE, CT_STAG, CT_BRAIN, CT_ANEM};
		for (int o = 0; o < CT_COUNT + 8; o++)
			for (int i = 0; i < ncol; i++) {
				col_t *k = &st->col[i];
				if (k->rock != ri)
					continue;
				if (o < CT_COUNT ? (k->face > 0.0f || k->type != order[o])
						 : (k->face <= 0.0f || (int)(k->face * 8.0f) != o - CT_COUNT))
					continue;
				float X = k->fx * A, Y = col_base_y(st, k);
				if (Y > 2.0f)
					continue;
				float S = k->size * (0.6f + 0.4f * k->grow) * (ri ? 0.9f : 1.0f);
				float shine = lum * near;
				float tipv = us_sat((0.4f + 0.5f * k->open) * shine + 0.6f * sn);
				look_t lk = {shine, 0.0f, 0.0f, tipv, 0, US_MAT_LEAF};
				float sway = surge * (1.0f + 0.2f * sinf(k->ph)) + 0.4f * cur;
				switch (k->type) {
				case CT_FAN:
					lk.fill = (0.17f + 0.18f * sn) * fl * shine;
					lk.line = us_sat((0.58f + 0.25f * sn) * shine);
					draw_fan(c, &lk, X, Y, S, k, 0.65f * sway);
					break;
				case CT_SPONGE:
					lk.fill = (0.3f + 0.1f * sn) * fl * shine;
					lk.line = us_sat(0.62f * shine);
					draw_sponge(c, &lk, X, Y, S, k, sway);
					break;
				case CT_STAG:
					lk.fill = (0.3f + 0.25f * sn + 0.06f * k->open) * fl * shine;
					lk.line = us_sat((0.62f + 0.25f * sn) * shine);
					draw_staghorn(c, &lk, X, Y, S, k, tipv);
					break;
				case CT_BRAIN:
					lk.fill = (0.3f + 0.2f * sn) * fl * shine;
					draw_brain(c, &lk, X, Y, S, k, us_sat((0.68f + 0.25f * k->open) * shine + 0.4f * sn));
					break;
				default:
					lk.fill = (0.26f + 0.18f * k->glow + 0.15f * sn) * fl * shine;
					lk.line = us_sat(0.5f * shine);
					lk.tip = us_sat((0.45f + 0.45f * k->glow) * shine + 0.5f * sn);
					draw_anemone(c, &lk, X, Y, S, k, 1.4f * sway, t);
					break;
				}
			}
	}
	/* the clownfish anemone on the sand, with its pair */
	for (int i = 0; i < ncol; i++) {
		col_t *k = &st->col[i];
		if (k->rock != 2)
			continue;
		float X = k->fx * A, Y = 0.955f, S = k->size * (0.6f + 0.4f * k->grow);
		float shine = lum;
		look_t lk = {shine, (0.28f + 0.18f * k->glow) * fl * shine, us_sat(0.5f * shine),
			     us_sat((0.45f + 0.45f * k->glow) * shine + 0.5f * sn), 0, US_MAT_BLOOM};
		float sway = 1.4f * (surge * (1.0f + 0.2f * sinf(k->ph)) + 0.4f * cur);
		/* contact shadow on the sand */
		ell_over(c, X, Y + 0.004f, S * 0.45f, S * 0.06f, 0.05f * lum, US_MAT_EARTH, 0.0f, 0, 0.0f);
		for (int f = 0; f < 2; f++) {
			float th = t * (0.6f + 0.15f * f) + f * 2.6f + 0.6f * us_noise2(f * 5.3f, t * 0.3f);
			float ro = S * (0.75f + 0.2f * sinf(t * 0.8f + f * 1.9f)) * (1.0f - 0.75f * st->duck);
			float cy = Y - S * (0.75f - 0.35f * st->duck);
			float fx = X + cosf(th) * ro, fy = cy + sinf(th) * ro * 0.35f;
			float dir = -sinf(th) >= 0 ? 1.0f : -1.0f;
			float L = crisp ? 0.026f : 0.022f;
			/* the one behind the anemone is drawn first, then the crown hides it */
			if (sinf(th) < 0)
				draw_clown(c, fx, fy, dir, L, (0.85f - 0.25f * st->duck) * fa * (0.6f + 0.4f * lum));
		}
		draw_anemone(c, &lk, X, Y, S, k, sway, t);
		for (int f = 0; f < 2; f++) {
			float th = t * (0.6f + 0.15f * f) + f * 2.6f + 0.6f * us_noise2(f * 5.3f, t * 0.3f);
			float ro = S * (0.75f + 0.2f * sinf(t * 0.8f + f * 1.9f)) * (1.0f - 0.75f * st->duck);
			float cy = Y - S * (0.75f - 0.35f * st->duck);
			float fx = X + cosf(th) * ro, fy = cy + sinf(th) * ro * 0.35f;
			float dir = -sinf(th) >= 0 ? 1.0f : -1.0f;
			float L = crisp ? 0.026f : 0.022f;
			if (sinf(th) >= 0)
				draw_clown(c, fx, fy, dir, L, (0.85f - 0.25f * st->duck) * fa * (0.6f + 0.4f * lum));
		}
	}

	/* ---- reef dwellers: sea stars on the rock, the moray in its crevice, the octopus by its den ---- */
	{
		rock_t *m = &st->rk[0];
		int nss = 1 + (int)(dens * 2.0f + 0.5f);
		for (int i = 0; i < nss && i < 3; i++) {
			star_t *q = &st->ss[i];
			q->ph += dt;
			q->u += 0.0015f * sinf(q->ph * 0.05f + i) * dt; /* creeping, imperceptibly */
			float fx = m->cx + q->u * m->hw, top = rock_top(m, fx);
			if (top > 2.0f)
				continue;
			float y = top + (m->base - top) * q->face * 0.8f;
			draw_star(c, fx * A, y, q->size, q->rot, us_sat(lum * 0.95f * (1.0f + 0.2f * sn)), 0.5f + 0.5f * sinf(q->ph * 0.3f) + st->kick_s);
		}
		/* the moray: breathing, lunging out a little on the kicks, snapping back in on a snare */
		{
			int h = st->mor_h;
			float fx = m->cx + m->hu[h] * m->hw, top = rock_top(m, fx);
			if (top < 2.0f) {
				float hy = top + (m->base - top) * m->hd[h];
				if (snare_ev)
					st->mor_to = 0.0f;
				st->mor_to = us_approach(st->mor_to, 1.0f, 2.5f, dt);
				float target = st->mor_to * st->mor_to * (0.55f + 0.25f * sinf(t * 0.17f + 1.0f));
				if (kick_ev)
					st->mor_ext = us_sat(st->mor_ext + 0.35f * st->mor_to);
				st->mor_ext = us_approach(st->mor_ext, target, target < st->mor_ext - 0.15f ? 0.05f : 0.5f, rdt);
				st->mor_ph += dt * (2.0f + 1.5f * st->lvl_env);
				float gape = 0.5f + 0.5f * sinf(st->mor_ph);
				float side = st->mor_side;
				draw_moray(c, fx * A, hy, m->hr[h], side, st->mor_ext, gape * gape, us_sat(lum * 0.9f * fl));
			}
		}
		/* the octopus: settles in its den, a dark band sweeps it on every kick; a snare may send it jetting off
		 * behind a puff of ink, after which it swims back and settles in again */
		{
			octo_t *o = &st->oc;
			float dx_ = o->hx * A, dy_ = o->hy;
			o->ph += dt;
			o->mt += dt * (0.15f + 0.8f * st->lvl_env);
			o->mood = us_approach(o->mood, us_sat(1.4f * st->lvl_env + 0.6f * o->blanch), 2.0f, dt);
			o->cool -= dt;
			if (kick_ev)
				o->band = 0.0f;
			o->band += rdt * 2.2f;
			o->blanch = us_approach(o->blanch, 0.0f, 0.45f, rdt);
			if (snare_ev) {
				if (o->mode == 0 && o->cool <= 0 && us_rand(r2) < 0.55f) {
					float away = dx_ > st->rk[0].cx * A ? 1.0f : -1.0f;
					float an = -0.55f - 0.3f * us_rand(r2);
					o->vx = away * cosf(an) * 0.32f;
					o->vy = sinf(an) * 0.32f;
					o->mode = 1;
					o->cool = us_randr(r2, 7.0f, 12.0f);
					for (int i = 0; i < 6; i++) {
						o->ink_x[i] = o->x + us_randr(r2, -0.008f, 0.008f) - o->vx * 0.02f * i;
						o->ink_y[i] = o->y + us_randr(r2, -0.008f, 0.004f) - o->vy * 0.02f * i;
						o->ink_r[i] = us_randr(r2, 0.006f, 0.011f);
						o->ink_a[i] = 1.0f;
					}
				}
				o->blanch = 1.0f;
			}
			if (o->mode == 1) {
				o->stream = us_approach(o->stream, 1.0f, 0.06f, rdt);
				float drag = expf(-dt * 1.7f);
				o->vx *= drag, o->vy *= drag;
				if (o->vx * o->vx + o->vy * o->vy < 0.03f * 0.03f)
					o->mode = 2;
			} else if (o->mode == 2) {
				/* swimming home in slow pulses, then letting the arms down onto the rock */
				float ex = dx_ - o->x, ey = dy_ - o->y, d = sqrtf(ex * ex + ey * ey) + 1e-6f;
				float pulse = 0.5f + 0.5f * sinf(o->ph * 3.0f);
				float sp = 0.045f * (0.4f + 0.6f * pulse) * us_sat(d / 0.03f);
				o->vx = us_approach(o->vx, ex / d * sp, 0.4f, dt);
				o->vy = us_approach(o->vy, ey / d * sp, 0.4f, dt);
				o->stream = us_approach(o->stream, us_sat(d / 0.05f) * 0.75f, 0.5f, dt);
				if (d < 0.006f)
					o->mode = 0;
			} else {
				o->vx = us_approach(o->vx, (dx_ - o->x) * 2.0f, 0.3f, dt);
				o->vy = us_approach(o->vy, (dy_ - o->y) * 2.0f, 0.3f, dt);
				o->stream = us_approach(o->stream, 0.0f, 0.5f, dt);
			}
			o->x += o->vx * dt;
			o->y += o->vy * dt;
			if (o->y < 0.14f)
				o->y = 0.14f, o->vy = 0.0f;
			/* the ink: dark puffs swelling and thinning into the water */
			for (int i = 0; i < 6; i++) {
				if (o->ink_a[i] < 0.02f)
					continue;
				o->ink_a[i] = us_approach(o->ink_a[i], 0.0f, 2.2f, dt);
				o->ink_r[i] += dt * 0.012f;
				o->ink_x[i] += 0.006f * cur * dt;
				o->ink_y[i] -= 0.004f * dt;
				float wv = water_v(o->ink_y[i], crisp) * lum;
				float iv = wv * (1.0f - 0.75f * o->ink_a[i]);
				ell_over(c, o->ink_x[i], o->ink_y[i], o->ink_r[i], o->ink_r[i] * 0.8f, iv, US_MAT_WATER, 0.0f, 0, 0.0f);
			}
			draw_octo(c, o, us_sat((0.45f + 0.55f * lum) * fa * fl), t, surge);
		}
	}

	/* ---- butterflyfish pottering about the reef ---- */
	{
		int nrf = 2 + (int)(dens * 2.0f + 0.5f);
		for (int k = 0; k < nrf && k < MAX_RF; k++) {
			rfish_t *f = &st->rf[k];
			f->wait -= dt;
			float lo = st->rk[0].cx * A - 0.6f, hi = st->rk[0].cx * A + 0.6f;
			if (f->wait <= 0 || (f->x < lo && f->face_to < 0) || (f->x > hi && f->face_to > 0)) {
				if (f->wait <= 0 || f->x < lo || f->x > hi)
					f->face_to = f->x < lo ? 1.0f : f->x > hi ? -1.0f : -f->face_to;
				f->wait = us_randr(r, 3.0f, 9.0f);
			}
			if (kick_ev && us_rand(r) < 0.5f)
				f->burst = 1.0f;
			f->burst = us_approach(f->burst, 0.0f, 0.35f, rdt);
			f->face = us_approach(f->face, f->face_to, 0.35f, dt);
			f->ph += dt;
			float spd = (0.012f + 0.05f * f->burst) * f->face;
			f->x += spd * dt;
			f->y = us_approach(f->y, f->home_y + 0.03f * us_noise2(k * 3.7f, t * 0.15f), 1.0f, dt);
			draw_rfish(c, f, us_sat((0.5f + 0.15f * f->burst) * (0.45f + 0.55f * lum) * fa * fl), t);
		}
	}

	/* ---- now and then a turtle swims through; its power strokes land on the kicks ---- */
	{
		turtle_t *tu = &st->tu;
		if (!tu->on) {
			tu->wait -= dt;
			if (tu->wait <= 0) {
				tu->on = 1;
				tu->dir = us_rand(r) < 0.5f ? 1 : -1;
				tu->L = crisp ? 0.12f : 0.11f;
				tu->x = tu->dir > 0 ? -0.12f : A + 0.12f;
				tu->y = us_randr(r, 0.22f, 0.38f);
				tu->phase = 0, tu->phase_t = 0, tu->fl = -0.6f, tu->glide = 0.5f;
			}
		}
		if (tu->on) {
			tu->phase_t += dt;
			if (tu->phase == 0 && tu->phase_t > 1.1f)
				tu->phase = 1, tu->phase_t = 0;
			else if (tu->phase == 1 && ((kick_ev && tu->phase_t > 0.25f) || tu->phase_t > 1.6f))
				tu->phase = 2, tu->phase_t = 0, tu->glide = 1.0f;
			else if (tu->phase == 2 && tu->phase_t > 0.35f)
				tu->phase = 0, tu->phase_t = 0;
			float fl_to = tu->phase == 1 ? 0.75f : (tu->phase == 2 ? -1.1f : -0.6f);
			tu->fl = us_approach(tu->fl, fl_to, tu->phase == 2 ? 0.1f : 0.35f, dt);
			tu->glide = us_approach(tu->glide, 0.0f, 1.4f, dt);
			tu->bob += dt * 1.3f;
			tu->x += tu->dir * (0.02f + 0.035f * tu->glide) * dt;
			tu->y += 0.004f * us_noise2(3.3f, st->flow_t * 2.0f) * dt;
			if (tu->x < -0.25f || tu->x > A + 0.25f) {
				tu->on = 0;
				tu->wait = us_randr(r, 25.0f, 55.0f);
			} else {
				draw_turtle(c, tu, us_sat((0.45f + 0.55f * lum) * fa * fl));
			}
		}
	}

	/* ---- now and then an eagle ray glides over the reef; its wingbeats come down on the kicks ---- */
	{
		ray_t *ry = &st->ray;
		if (!ry->on) {
			ry->wait -= dt * (0.6f + 0.8f * st->lvl_env);
			if (ry->wait <= 0) {
				ry->on = 1;
				ry->dir = us_rand(r2) < 0.5f ? 1 : -1;
				ry->L = crisp ? 0.28f : 0.26f;
				ry->x = ry->dir > 0 ? -0.25f : A + 0.25f;
				ry->y = us_randr(r2, 0.15f, 0.32f);
				if (st->tu.on && fabsf(ry->y - st->tu.y) < 0.12f) /* keep clear of the turtle */
					ry->y = st->tu.y > 0.27f ? st->tu.y - 0.13f : st->tu.y + 0.13f;
				ry->phase = 0, ry->phase_t = 0, ry->fl = 0.2f, ry->glide = 0.4f;
			}
		}
		if (ry->on) {
			ry->phase_t += dt;
			if (ry->phase == 0 && ry->phase_t > 1.4f)
				ry->phase = 1, ry->phase_t = 0;
			else if (ry->phase == 1 && ((kick_ev && ry->phase_t > 0.35f) || ry->phase_t > 4.5f))
				ry->phase = 2, ry->phase_t = 0, ry->glide = 1.0f;
			else if (ry->phase == 2 && ry->phase_t > 0.4f)
				ry->phase = 0, ry->phase_t = 0;
			float fl_to = ry->phase == 1 ? 1.0f : ry->phase == 2 ? -0.9f : 0.15f;
			ry->fl = us_approach(ry->fl, fl_to, ry->phase == 2 ? 0.06f : ry->phase == 1 ? 0.7f : 0.9f, dt);
			ry->glide = us_approach(ry->glide, 0.0f, 1.8f, dt);
			ry->ph += dt;
			ry->x += ry->dir * (0.02f + 0.05f * ry->glide) * dt;
			ry->y += (0.006f * us_noise2(5.1f, ry->ph * 0.2f) + 0.004f * ry->fl) * dt;
			if (ry->x < -0.3f || ry->x > A + 0.3f) {
				ry->on = 0;
				ry->wait = us_randr(r2, 30.0f, 60.0f);
			} else {
				draw_ray(c, ry, us_sat((0.45f + 0.55f * lum) * fa * fl), t);
			}
		}
	}

	/* ---- the near school ---- */
	for (int k = 0; k < ns; k++)
		if (!st->sc[k].deep)
			DRAW_SCHOOL(k);
#undef DRAW_SCHOOL

	/* ---- seagrass tufts on the sand ---- */
	{
		int ng = 8 + (int)(dens * (MAX_GRASS - 8));
		for (int i = 0; i < ng; i++) {
			grass_t *g = &st->grass[i];
			float X = g->fx * A, d = (g->y - Y_HOR) / (1.0f - Y_HOR);
			float v = (0.25f + 0.35f * d) * lum * fl;
			for (int b = 0; b < g->n; b++) {
				float u = (b + 0.5f) / g->n - 0.5f;
				float hh = g->h * (0.7f + 0.5f * hashf((uint32_t)i, b));
				float bend = (u * 1.2f + 0.5f * surge + 0.25f * cur + 0.1f * sinf(t * 0.9f + g->ph + b)) * (0.4f + 0.6f * d);
				float x = X + u * g->h * 0.3f, y = g->y;
				for (int sgi = 0; sgi < 3; sgi++) {
					float f = (sgi + 1) / 3.0f;
					float nx = X + u * g->h * 0.3f + bend * hh * 0.45f * f * f, ny = g->y - hh * f;
					if (crisp)
						px_line(c, x, y, nx, ny, v, US_MAT_LEAF);
					else
						cap_over(c, x, y, nx, ny, 0.0016f * (1.0f - 0.3f * sgi) * (0.5f + d), 0.0016f * (0.7f - 0.3f * sgi) * (0.5f + d),
							 v, US_MAT_LEAF);
					x = nx, y = ny;
				}
			}
		}
	}

	/* ---- seahorses holding on to the seagrass: they sway with it, fan their dorsal fins (faster on the
	 *      hats) and tuck their heads down when a snare startles them ---- */
	{
		int nh = 1 + (dens > 0.35f);
		for (int k = 0; k < nh; k++) {
			horse_t *hh = &st->hs[k];
			grass_t *g = &st->grass[hh->g];
			hh->ph += dt;
			if (snare_ev)
				hh->tuck = 1.0f;
			hh->tuck = us_approach(hh->tuck, 0.0f, 0.7f, rdt);
			float X = g->fx * A, d = (g->y - Y_HOR) / (1.0f - Y_HOR);
			int b = g->n / 2;
			float u = (b + 0.5f) / g->n - 0.5f;
			float hb = g->h * (0.7f + 0.5f * hashf((uint32_t)hh->g, b));
			float bend = (u * 1.2f + 0.5f * surge + 0.25f * cur + 0.1f * sinf(t * 0.9f + g->ph + b)) * (0.4f + 0.6f * d);
			float f = 0.5f;
			float ax = X + u * g->h * 0.3f + bend * hb * 0.45f * f * f, ay = g->y - hb * f;
			float H = (crisp ? 0.1f : 0.09f) * (0.65f + 0.5f * d);
			float fin = sinf(t * (6.0f + 26.0f * hat) + k * 2.0f) * (0.12f + 0.9f * hat);
			float v = us_sat((0.62f + 0.12f * hat) * (0.45f + 0.55f * lum) * fa * fl);
			draw_horse(c, ax, ay, H, hh->dir, 0.22f * bend + 0.04f * sinf(hh->ph * 0.5f), hh->tuck, fin, v,
				   k ? US_MAT_BLOOM : US_MAT_LIGHT);
		}
	}

	/* ---- kelp fronds framing the foreground ---- */
	{
		int nk = 2 + (int)(dens * (MAX_KELP - 2) + 0.5f);
		for (int i = 0; i < nk && i < st->nkelp; i++) {
			kelp_t *k = &st->kelp[i];
			k->ph += dt * 0.3f;
			enum { NS = 13 };
			float xs[NS + 1], ys[NS + 1];
			float bx = k->fx * A, by = 1.03f, H = k->h;
			for (int j = 0; j <= NS; j++) {
				float s = (float)j / NS;
				/* the stipe: a slow travelling wave up its length, the surge bending the top over */
				float sway = (0.03f * swell_s + 0.04f * shove + 0.03f * cur) * powf(s, 1.6f) + 0.008f * sinf(k->ph - s * 4.0f) * s;
				xs[j] = bx + k->lean * s * s + sway;
				ys[j] = by - H * s;
			}
			float vf = 0.24f * lum * fl, vl = us_sat(0.52f * lum);
			for (int j = 0; j < NS; j++) {
				float r0 = 0.0024f * k->w, r1 = 0.0024f * k->w;
				cap_over(c, xs[j], ys[j], xs[j + 1], ys[j + 1], r0, r1, vf * 1.3f, US_MAT_STEM);
			}
			/* blades: long ruffled straps streaming up and out, alternating sides, a gas float at each root */
			for (int j = 1; j <= NS; j++) {
				float side = (j & 1) ? 1.0f : -1.0f;
				float s = (float)j / NS;
				float h = hashf(k->seed, j);
				float bl = (0.06f + 0.035f * h) * (0.75f + 0.35f * s);
				float stip = atan2f(xs[j] - xs[j - 1], ys[j - 1] - ys[j]);
				float ang = stip + side * (0.45f + 0.25f * h) + 0.2f * cur;
				float x = xs[j], y = ys[j];
				if (!crisp)
					us_disc(c, x + sinf(ang) * 0.004f, y - cosf(ang) * 0.004f, 0.0034f, vl * 0.75f, US_MAT_STEM);
				float bxp[4], byp[4];
				bxp[0] = x, byp[0] = y;
				for (int sg = 0; sg < 3; sg++) {
					float f0 = sg / 3.0f, f1 = (sg + 1) / 3.0f;
					/* each blade curls back toward the stipe and flutters */
					ang -= side * 0.27f;
					ang += 0.07f * sinf(k->ph * 1.6f + j * 1.3f + sg * 0.9f) + (0.13f * swell_s + 0.13f * shove) * s;
					float l = bl / 3.0f;
					float nx = x + sinf(ang) * l, ny = y - cosf(ang) * l;
					float w0 = 0.0058f * sinf(US_PI * (0.15f + 0.85f * f0)) + 0.0012f;
					float w1 = 0.0058f * sinf(US_PI * (0.15f + 0.85f * f1)) + 0.0012f;
					cap_over(c, x, y, nx, ny, w0, w1, vf, US_MAT_LEAF);
					x = nx, y = ny;
					bxp[sg + 1] = x, byp[sg + 1] = y;
				}
				if (!crisp) /* midrib */
					for (int sg = 0; sg < 3; sg++)
						wu_line(c, bxp[sg], byp[sg], bxp[sg + 1], byp[sg + 1], vl * (0.75f - 0.15f * sg), US_MAT_LEAF);
			}
			if (!crisp)
				for (int j = 0; j < NS; j++)
					wu_line(c, xs[j] - 0.0016f, ys[j], xs[j + 1] - 0.0016f, ys[j + 1], vl * 0.9f, US_MAT_STEM);
		}
	}

	/* ---- spawn bundles: released bright, then soft pink beads drifting up toward the light ---- */
	for (int i = 0; i < st->nsp;) {
		spawn_t *q = &st->sp[i];
		float fx = us_noise3(q->x * 2.0f, q->y * 2.0f, st->flow_t * 3.0f);
		q->vx += (fx * 0.02f + 0.004f * cur) * dt;
		q->vy += -0.01f * dt;
		q->vx *= expf(-dt * 0.8f);
		q->vy *= expf(-dt * 0.5f);
		if (q->vy > -0.01f)
			q->vy = -0.01f;
		q->x += q->vx * dt;
		q->y += q->vy * dt;
		q->age += dt;
		if (q->age >= q->life || q->y < Y_SURF + 0.01f) {
			if (us_rand(r) < 0.25f) { /* a few settle and recruit to a colony */
				col_t *k = &st->col[(int)(us_rand(r) * st->ncol) % st->ncol];
				k->grow = us_sat(k->grow + 0.08f);
			}
			*q = st->sp[--st->nsp];
			continue;
		}
		float fresh = us_sat(1.0f - q->age * 2.0f);
		float fade = us_sat(q->age * 12.0f) * us_sat((q->life - q->age) * 0.5f);
		float v = us_sat((0.5f + 0.25f * st->lvl_env + 0.5f * fresh) * fade) * fa;
		float rad = (crisp ? 0.0045f : 0.003f) * (1.0f + 0.9f * fresh);
		us_disc(c, q->x, q->y, rad, v, fresh > 0.5f ? US_MAT_LIGHT : US_MAT_BLOOM);
		i++;
	}

	/* ---- marine snow / plankton ---- */
	{
		int n = 60 + (int)(dens * (MAX_PL - 60));
		float sparkle = st->hi_env;
		for (int i = 0; i < n; i++) {
			plank_t *p = &st->pl[i];
			float px = p->x * A;
			float fx = us_noise3(px * 1.4f, p->y * 1.4f, st->flow_t);
			float fy = us_noise3(px * 1.4f + 31.7f, p->y * 1.4f, st->flow_t);
			float spd = 0.01f + 0.025f * a->level;
			float push = 0.006f + 0.05f * st->kick_s;
			p->x += (fx * spd + push * (cur >= 0 ? 1.0f : -1.0f) * (0.3f + 0.7f * fabsf(cur))) * dt / A;
			p->y += (fy * spd + 0.004f) * dt;
			p->x -= floorf(p->x);
			if (p->y > 1.0f)
				p->y = Y_SURF + 0.01f;
			if (p->y < Y_SURF)
				p->y = 0.99f;
			p->ph += dt * (0.5f + 3.0f * sparkle);
			float tw = 0.5f + 0.5f * sinf(p->ph + p->size * 9.0f);
			int g = ((hash2(i, (int)(st->glint_seed & 0xffff)) & 1) == 0);
			float v = (0.14f + 0.18f * p->size) * (0.3f + 0.9f * st->lvl_env) + (0.15f + 0.3f * sparkle) * tw * tw * tw +
				  (g ? 1.0f * hat : 0.0f);
			us_point(c, px, p->y, (crisp ? 0.002f : 0.0011f) * (0.8f + 0.6f * p->size), us_sat(v) * fa,
				 p->size > 0.85f || g ? US_MAT_LIGHT : US_MAT_WATER);
		}
	}

	/* ---- bubbles: a snare releases a cloud from the anemone and a crevice; a lone stream in quiet ---- */
	{
		st->bub_cd -= rdt;
		st->bub_idle -= dt;
		int puff = 0, cloud = 0;
		if (snare_ev && st->bub_cd <= 0)
			puff = 7 + (int)(5.0f * st->lvl_env), cloud = 1, st->bub_cd = 0.12f;
		else if (st->bub_idle <= 0)
			puff = 3, st->bub_idle = us_randr(r, 2.0f, 5.0f);
		for (int src = 0; src < 1 + cloud && puff; src++) {
			rock_t *rk = &st->rk[0];
			int h = (int)(us_rand(r) * rk->nh) % rk->nh;
			float fx = rk->cx + rk->hu[h] * rk->hw;
			float top = rock_top(rk, fx);
			float bx = fx * A, by = top + (rk->base - top) * rk->hd[h];
			if (src == 1 || (cloud && us_rand(r) < 0.5f)) {
				for (int i = 0; i < ncol; i++)
					if (st->col[i].rock == 2) {
						bx = st->col[i].fx * A, by = 0.92f;
						break;
					}
			}
			for (int k = 0; k < puff && st->nbub < MAX_BUB; k++) {
				bub_t *b = &st->bub[st->nbub++];
				float spr = cloud ? 0.015f : 0.003f;
				b->x = bx + us_randr(r, -spr, spr);
				b->y = by + us_randr(r, -spr, spr) * 0.5f;
				b->r = cloud ? us_randr(r, 1.0f, 2.2f) : us_randr(r, 0.7f, 1.3f);
				b->age = cloud ? -k * 0.015f : -k * 0.25f;
				b->life = 9.0f;
				b->wob = us_rand(r) * US_TAU;
			}
		}
		for (int i = 0; i < st->nbub;) {
			bub_t *b = &st->bub[i];
			b->age += dt;
			if (b->age >= b->life || b->y < surf_y(b->x, t, st->lurch) + 0.004f) {
				*b = st->bub[--st->nbub];
				continue;
			}
			i++;
			if (b->age < 0)
				continue;
			b->wob += dt * 6.0f;
			float rise = 0.06f + 0.05f * b->r;
			b->y -= rise * dt;
			b->x += (0.01f * cur + 0.012f * sinf(b->wob)) * dt;
			float rad = (0.0022f + 0.0006f * (1.0f - b->y)) * b->r * (crisp ? 1.4f : 1.0f);
			float v = us_sat(0.6f * (0.5f + 0.5f * lum) + 0.2f) * fa * us_sat(b->age * 8.0f);
			if (crisp) {
				if (rad * c->s < 1.3f)
					us_put(c, (int)(b->x * c->s), (int)(b->y * c->s), v, US_MAT_WATER);
				else
					us_ring(c, b->x, b->y, rad, 1.0f / c->s, v, US_MAT_WATER);
			} else {
				us_ring(c, b->x, b->y, rad, 0.0009f, v, US_MAT_WATER);
				us_put(c, (int)((b->x - rad * 0.35f) * c->s), (int)((b->y - rad * 0.35f) * c->s), v, US_MAT_LIGHT);
			}
		}
	}

	/* ---- hats: sunlight glints on the caustic sand and along the surface ---- */
	{
		if (hat_ev) {
			int nsand = (24 + (int)(16.0f * dens)) * (crisp ? 2 : 1), nsurf = crisp ? 16 : 12;
			for (int k = 0; k < nsand + nsurf && st->ngl < MAX_GL; k++) {
				glint_t *g = &st->gl[st->ngl++];
				g->x = us_rand(r) * A;
				if (k < nsand) {
					float d = us_rand(r);
					g->y = Y_HOR + 0.01f + (1.0f - Y_HOR - 0.01f) * d * d;
					g->size = 0.5f + 0.7f * d;
				} else {
					g->y = surf_y(g->x, t, st->lurch) + us_randr(r, 0.0f, 0.012f);
					g->size = 0.9f;
				}
				g->age = 0;
				g->life = us_randr(r, 0.12f, 0.3f);
			}
		}
		for (int i = 0; i < st->ngl;) {
			glint_t *g = &st->gl[i];
			g->age += rdt;
			if (g->age >= g->life) {
				*g = st->gl[--st->ngl];
				continue;
			}
			i++;
			float k = 1.0f - g->age / g->life;
			float v = us_sat((0.6f + 0.5f * lum) * k) * fa;
			if (crisp) {
				int px = (int)(g->x * c->s), py = (int)(g->y * c->s);
				us_put(c, px, py, v, US_MAT_LIGHT);
				if (k > 0.4f) {
					us_put(c, px - 1, py, v * 0.7f, US_MAT_LIGHT);
					us_put(c, px + 1, py, v * 0.7f, US_MAT_LIGHT);
					us_put(c, px, py - 1, v * 0.7f, US_MAT_LIGHT);
					us_put(c, px, py + 1, v * 0.7f, US_MAT_LIGHT);
				}
			} else {
				float L = 0.008f * g->size * (0.5f + 0.5f * k);
				wu_line(c, g->x - L, g->y, g->x + L, g->y, v, US_MAT_LIGHT);
				wu_line(c, g->x, g->y - L * 0.8f, g->x, g->y + L * 0.8f, v, US_MAT_LIGHT);
				us_disc(c, g->x, g->y, 0.0012f * g->size, v, US_MAT_LIGHT);
			}
		}
	}
	(void)nutrients;
}

const us_scene_vt us_scene_coral = {"coral", "Coral", 0.0f, create, destroy, frame};
