/* Understory scene: Fireflies — a bioluminescent swarm over a summer night meadow at the edge of a wood.
 *
 * Every firefly is an agent with its own flash oscillator (phase th, natural frequency w_i).
 * The oscillators are coupled Kuramoto-style through a coarse spatial grid (each firefly feels the mean
 * phase of its neighbourhood) plus a weak global field. Coupling strength follows the music's energy:
 *   quiet  -> weak coupling, wide frequency spread: a few fireflies blink independently;
 *   dense  -> strong coupling: neighbourhoods lock, and a slow drifting frequency gradient across the field
 *             turns the locked swarm into waves of synchronised flashing that roll across the meadow.
 * Beats nudge every phase toward its local mean (and gently entrain the swarm's mean frequency to the tempo).
 * Depth: near fireflies are few, large and soft (bokeh), far ones are tiny sparks along the treeline.
 * The drum kit has three distinct answers, in the swarm's own language:
 *   kick  -> a synchronised flash wave rolls out from a point through the near and middle swarm (with a steady
 *            beat the swarm holds its own blinking between kicks, like Photinus carolinus, and lets go on them);
 *            the wave lights the grass, the pool and the bark of the near tree;
 *   snare -> one cluster bursts: each member throws off a quick widening halo and sparks scatter upward;
 *   hats  -> tiny far fireflies twinkle as four-point glints.
 * Loudness decides how many join in and how bright they burn (louder = clearly more light, with a warm lantern
 * glow between flashes); in quiet passages and silence only a few leaders blink, slowly.
 * Each natural flash is a round hot core in a soft additive halo with a faint ring that widens as it fades, and every
 * firefly keeps a ~1.2 s flight-path history drawn as a long-exposure trail lit wherever it flashed. Most of them
 * drift around a few slowly wandering gatherings, so the swarm clusters. Every flash is mirrored in the still pond
 * below it, and flash light pools on the grass of the near bank (so sync waves sweep it). Louder music fills the air
 * over the water with a soft haze and lights the grass. A big tree frames the left edge as a silhouette whose rim
 * and leaf fringe catch the swarm's light.
 * The swarm is six colonies by depth, each listening to its own slice of the spectrum (sub low in the grass ..
 * air up by the treeline): a rise in its band makes the members that were about to flash flash now, and the
 * band's level tints how bright the colony burns. Around them: moths chase the light (highs quicken their wings),
 * an owl on a dead snag turns its head with the spectral centroid and its eyes catch the mids, kicks raise
 * fish rings on the pool and shiver an orb-web strung off a cattail, sub thickens the ground mist, air makes the
 * stars scintillate, glitters the moon path and (on a hit) sends a shooting star, swell brightens the milky way
 * and stereo width loosens the moths' orbits.
 * World: reads calm, insects (population at open), daylight (by day far fewer and dimmer), season;
 *        writes insects (calm, dark music breeds them).
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>


/* The place: a summer night meadow at the edge of a wood. A moonlit sky over three soft treeline layers (blue haze
 * far, darker nearer, mist pooled at their feet), a still pond that mirrors the wood and the swarm, and a near bank of
 * tall grass and seed heads, framed by one big tree on the left. The static backdrop (sky, moon glow, treelines, pond
 * mirror, bank) is painted once into a cache with ordered dithering (smooth gradients, no banding) and re-laid each
 * frame; the big tree's trunk and boughs are a cached anti-aliased mask with a rim-light edge. */
#define MAXF 520
#define TREE_N 320
#define NLAY 3    /* treeline depth layers: far, mid, near */
#define NBLADES 170
#define NSEED 7   /* seed heads on tall stems */
#define NCAT 7
#define NCLU 6    /* drifting gathering points the swarm clusters around */
#define NSTARS 140
#define NRIP 22
#define GX 14
#define GY 7
#define NHIST 18 /* flight-path history samples (long-exposure trails) */
#define NGB 48    /* ground-light bins across the width */
#define NMW 460   /* milky-way stars */
#define LGX 64    /* coarse flash-light grid (lights the near tree) */
#define LGY 36
#define NCOL 6    /* colonies: each listens to its own slice of the spectrum */
#define NMOTH 4
#define NPR 6     /* rising-fish rings on the pool (kicks) */
#define NDEW 7
#define NSPK 120  /* snare sparks */
#define NTSEG 360 /* near-tree capsules (boughs + foliage clumps) */
#define NLEAF 1000 /* swaying leaves on the near tree */
#define MFW 96    /* mist field */
#define MFH 14

#define W0 0.635f     /* far shore: the waterline under the wood */
#define POOL_Y 0.815f /* near shore (mean) */
#define FLY_TOP 0.38f
#define FLY_BOT 0.95f
#define MIST_Y0 0.52f
#define MIST_Y1 0.72f

typedef struct {
	float x, y;   /* x as 0..1 fraction of width, y normalized */
	float vx, vy; /* in the same units per second */
	float z;      /* depth 0 far .. 1 near */
	float th;     /* flash phase, flash at th == 0 (mod 2pi) */
	float dev;    /* natural-frequency deviation (gaussian) */
	float life;   /* 0..1 fade in/out */
	float px, py; /* previous drawn position (normalized, x already * aspect) */
	float nz;     /* noise offset */
	int state;    /* 0 dead, 1 alive, 2 leaving */
	int cell;
	int col;      /* colony 0 (sub, low in the grass) .. NCOL-1 (air, up by the treeline) */
	float hx[NHIST], hy[NHIST], hb[NHIST]; /* ring buffer: x (already * aspect), y, flash brightness */
	int hh;
	float rank;   /* 0..1: who joins in first as the music gets louder */
	float ke, ka, kd; /* kick flash: envelope, amplitude, delay until the wave front arrives (<0 none) */
	float se;     /* snare burst envelope */
	float he;     /* hat twinkle envelope */
	int clu;      /* gathering point it drifts around (-1: a loner) */
	/* this frame's draw parameters (simulated first, drawn in depth passes) */
	float dv, dsz, dba, dpw, dvis, dpart, dtwk, dbs, dlant;
} fly_t;

typedef struct {
	us_rng rng;
	int inited;
	fly_t f[MAXF];
	float tree[TREE_N]; /* skyline: the highest treeline top y per x fraction */
	float lay[NLAY][TREE_N]; /* each treeline layer's top */
	float blade_x[NBLADES], blade_h[NBLADES], blade_lean[NBLADES], blade_ph[NBLADES], blade_d[NBLADES];
	float seed_x[NSEED], seed_h[NSEED], seed_d[NSEED], seed_ph[NSEED];
	int seed_k[NSEED];
	float clu_ph[NCLU];
	float mist[MFW * MFH];
	/* the cached backdrop + the near tree's mask (reallocated only when the canvas size / style changes) */
	uint8_t *bd_ink, *bd_mat, *tm_cov, *tm_rim;
	int bd_w, bd_h, bd_crisp, bd_ok;
	float bd_mx, bd_my, bd_day, bd_moon;
	int tm_x0, tm_x1, tm_y0, tm_y1;
	float cat_x[NCAT], cat_h[NCAT], cat_ph[NCAT];
	float star_x[NSTARS], star_y[NSTARS], star_b[NSTARS], star_ph[NSTARS];
	float rip_x[NRIP], rip_y[NRIP], rip_len[NRIP], rip_sp[NRIP];
	float gc[GX * GY], gs[GX * GY], gn[GX * GY];
	float lc[GX * GY], ls[GX * GY];
	float energy;     /* smoothed musical energy 0..1 */
	float pulse;
	float omega;      /* swarm mean angular frequency */
	float beat_iv;    /* tracked beat interval (s) */
	float since_beat;
	float spawn_acc, kill_acc;
	float glow;       /* smoothed total flash light (lights the grass a touch) */
	float wind_t;
	float grad_t;
	float sync;       /* global order parameter (smoothed), for diagnostics + glow */
	float hist_acc;
	float gbin[NGB], gbin_s[NGB]; /* flash light falling on the bank, per x bin */
	float mw_x[NMW], mw_y[NMW], mw_b[NMW];
	float lg[LGX * LGY], lg_s[LGX * LGY];
	/* colonies: band envelope, slow average, refractory timer, phase kick for this frame */
	float col_env[NCOL], col_avg[NCOL], col_cd[NCOL], col_kick[NCOL];
	/* smoothed region features (each has its own job) */
	float sub_s, mid_s, air_s, high_s, swell_s, width_s, cent_s, web_sh;
	/* moths chasing the light */
	float mo_x[NMOTH], mo_y[NMOTH], mo_vx[NMOTH], mo_vy[NMOTH], mo_ph[NMOTH], mo_orb[NMOTH], mo_tt[NMOTH];
	int mo_tgt[NMOTH];
	/* the owl on the dead snag */
	float owl_xf, owl_turn, owl_look, owl_look_t, owl_blink, owl_blink_t;
	/* rising-fish rings */
	float pr_x[NPR], pr_y[NPR], pr_age[NPR], pr_amp[NPR];
	/* shooting star */
	float met_x, met_y, met_dx, met_dy, met_age, met_cd;
	/* the orb-web between the reeds */
	float web_x, web_y, web_r;
	/* drums: loudness, how strongly a beat is driving the swarm, the light thrown by the last kick wave */
	float lvl_s, drive, kglow, sglow, sg_x, sg_y, sg_r;
	/* snare sparks rising out of a bursting cluster */
	float sp_x[NSPK], sp_y[NSPK], sp_vx[NSPK], sp_vy[NSPK], sp_age[NSPK], sp_life[NSPK];
	int sp_next;
	/* the near tree framing the left edge: capsules (x as fraction of width, y, radius) */
	float nt_ax[NTSEG], nt_ay[NTSEG], nt_bx[NTSEG], nt_by[NTSEG], nt_ra[NTSEG], nt_rb[NTSEG];
	int nt_n;
	float lf_x[NLEAF], lf_y[NLEAF], lf_a[NLEAF], lf_l[NLEAF];
	uint8_t lf_hid[NLEAF]; /* buried inside the canopy silhouette: nothing to draw */
	int lf_n;
} st_t;

static void free_cache(st_t *s)
{
	free(s->bd_ink);
	free(s->bd_mat);
	free(s->tm_cov);
	free(s->tm_rim);
	s->bd_ink = s->bd_mat = s->tm_cov = s->tm_rim = NULL;
	s->bd_w = s->bd_h = 0;
	s->bd_ok = 0;
}

/* colony band slices (bands are log-spaced 30 Hz..16 kHz). The lowest bands can hold no FFT bin at all and read
 * as a flat 1, so the two bass colonies listen to the sub / bass features instead and the rest start at ~175 Hz */
static const int col_band[NCOL + 1] = {0, 0, 9, 15, 21, 26, 32};

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x66697265u);
	return s;
}

static void destroy(void *p)
{
	if (p)
		free_cache(p);
	free(p);
}

/* an axis-aligned hairline ellipse as splats (pool rings); crisp canvases get hard pixels */
static void hellipse(us_canvas *c, float x, float y, float rx, float ry, float v, uint8_t m)
{
	float rp = rx * c->s, rq = ry * c->s;
	int n = (int)(US_TAU * rp * 0.8f) + 8;
	if (n > 1500)
		n = 1500;
	float cx = x * c->s - 0.5f, cy = y * c->s - 0.5f;
	float ca = cosf(US_TAU / n), sa = sinf(US_TAU / n), dx = 1, dy = 0;
	for (int i = 0; i < n; i++) {
		float fx = cx + dx * rp, fy = cy + dy * rq;
		/* the ring is brightest at its near and far sides, like light on a real ripple */
		float vv = v * (0.45f + 0.55f * dy * dy);
		int ix = (int)floorf(fx), iy = (int)floorf(fy);
		if (c->crisp)
			us_put(c, (int)(fx + 0.5f), (int)(fy + 0.5f), vv, m);
		else {
			float ax = fx - ix, ay = fy - iy;
			us_put(c, ix, iy, vv * (1 - ax) * (1 - ay), m);
			us_put(c, ix + 1, iy, vv * ax * (1 - ay), m);
			us_put(c, ix, iy + 1, vv * (1 - ax) * ay, m);
			us_put(c, ix + 1, iy + 1, vv * ax * ay, m);
		}
		float ndx = dx * ca - dy * sa;
		dy = dx * sa + dy * ca;
		dx = ndx;
	}
}

/* knock ink out of a disc (silhouettes in front of the sky glow) */
static void carve_disc(us_canvas *c, float x, float y, float r)
{
	int x0 = (int)floorf((x - r) * c->s), x1 = (int)ceilf((x + r) * c->s);
	int y0 = (int)floorf((y - r) * c->s), y1 = (int)ceilf((y + r) * c->s);
	float r2 = r * c->s * r * c->s;
	float cx = x * c->s, cy = y * c->s;
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > c->w)
		x1 = c->w;
	if (y1 > c->h)
		y1 = c->h;
	for (int py = y0; py < y1; py++)
		for (int px = x0; px < x1; px++) {
			float ddx = px + 0.5f - cx, ddy = py + 0.5f - cy;
			if (ddx * ddx + ddy * ddy <= r2)
				c->ink[(size_t)py * c->w + px] = 0;
		}
}

static void carve_line(us_canvas *c, float ax, float ay, float bx, float by, float r)
{
	float len = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay)) * c->s;
	int n = (int)(len / 1.5f) + 1;
	for (int i = 0; i <= n; i++) {
		float u = (float)i / n;
		carve_disc(c, ax + (bx - ax) * u, ay + (by - ay) * u, r);
	}
}


/* a hairline circle drawn as bilinear splats around the circumference (much cheaper than us_ring) */
static void hring(us_canvas *c, float x, float y, float r, float v)
{
	float rp = r * c->s;
	int n = (int)(US_TAU * rp * 0.9f) + 8;
	if (n > 2000)
		n = 2000;
	float cx = x * c->s - 0.5f, cy = y * c->s - 0.5f;
	float ca = cosf(US_TAU / n), sa = sinf(US_TAU / n), dx = rp, dy = 0;
	for (int i = 0; i < n; i++) {
		float fx = cx + dx, fy = cy + dy;
		int ix = (int)floorf(fx), iy = (int)floorf(fy);
		float ax = fx - ix, ay = fy - iy;
		us_put(c, ix, iy, v * (1 - ax) * (1 - ay), US_MAT_LIGHT);
		us_put(c, ix + 1, iy, v * ax * (1 - ay), US_MAT_LIGHT);
		us_put(c, ix, iy + 1, v * (1 - ax) * ay, US_MAT_LIGHT);
		us_put(c, ix + 1, iy + 1, v * ax * ay, US_MAT_LIGHT);
		float ndx = dx * ca - dy * sa;
		dy = dx * sa + dy * ca;
		dx = ndx;
	}
}

static void hline(us_canvas *c, float x0, float x1, float y, float v, uint8_t m)
{
	float fy = y * c->s - 0.5f;
	int iy = (int)floorf(fy);
	float ay = fy - iy;
	int p0 = (int)(x0 * c->s), p1 = (int)(x1 * c->s);
	if (p0 < 0)
		p0 = 0;
	if (p1 > c->w)
		p1 = c->w;
	for (int px = p0; px < p1; px++) {
		float e = us_sat((px - p0) * 0.1f) * us_sat((p1 - px) * 0.1f);
		us_put(c, px, iy, v * e * (1 - ay), m);
		us_put(c, px, iy + 1, v * e * ay, m);
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

/* ---- the near tree: one big tree framing the left edge, in aspect units ---- */
static void nt_add(st_t *st, float ax, float ay, float bx, float by, float ra, float rb)
{
	if (st->nt_n >= NTSEG)
		return;
	int k = st->nt_n++;
	st->nt_ax[k] = ax, st->nt_ay[k] = ay, st->nt_bx[k] = bx, st->nt_by[k] = by;
	st->nt_ra[k] = ra, st->nt_rb[k] = rb;
}

static void nt_leaf(st_t *st, float x, float y, float ang)
{
	if (st->lf_n >= NLEAF)
		return;
	us_rng *r = &st->rng;
	int k = st->lf_n++;
	st->lf_x[k] = x;
	st->lf_y[k] = y;
	st->lf_a[k] = ang;
	st->lf_l[k] = us_randr(r, 0.010f, 0.017f);
}

static void build_neartree(st_t *st)
{
	us_rng *r = &st->rng;
	st->nt_n = 0;
	st->lf_n = 0;
	/* trunk: rooted below the frame on the near bank, a gentle S, flared at the root */
	const int NT = 8;
	float px = 0.215f, py = 1.08f;
	for (int i = 0; i < NT; i++) {
		float u = (i + 1.0f) / NT;
		float nx = us_lerp(0.215f, 0.175f, u) + 0.016f * sinf(u * 4.2f), ny = us_lerp(1.08f, 0.40f, u);
		float ra = us_lerp(0.034f, 0.02f, (float)i / NT) + (i == 0 ? 0.018f : 0.0f), rb = us_lerp(0.034f, 0.02f, u);
		nt_add(st, px, py, nx, ny, ra, rb);
		px = nx, py = ny;
	}
	/* major limbs out of the fork, arching up and out; the last sweeps along the top of the frame and one lower
	 * bough reaches over the meadow */
	enum { NL = 5, NP = 7 };
	static const float tg[NL][5] = {
		/* (unused), end x, y (relative to the frame), arch */
		{0.0f, 0.0f, -0.04f, -0.02f, 0.05f}, {0.0f, 0.0f, 0.12f, -0.10f, 0.03f}, {0.0f, 0.0f, 0.36f, 0.00f, 0.06f},
		{0.0f, 0.0f, 0.62f, 0.04f, 0.10f},   {0.0f, 0.0f, 0.86f, 0.02f, 0.13f}};
	static const float lr[NL] = {0.016f, 0.015f, 0.015f, 0.013f, 0.011f};
	float lx[NL][NP], ly[NL][NP];
	for (int k = 0; k < NL; k++) {
		float x0 = px, y0 = py, x1 = tg[k][2], y1 = tg[k][3];
		float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
		float nx = dy / len, ny = -dx / len; /* the side that arches upward */
		if (ny > 0)
			nx = -nx, ny = -ny;
		for (int i = 0; i < NP; i++) {
			float u = (float)i / (NP - 1), arch = tg[k][4] * 4.0f * u * (1.0f - u) * len;
			lx[k][i] = x0 + dx * u + nx * arch + (i ? 0.008f * us_randn(r) : 0.0f);
			ly[k][i] = y0 + dy * u + ny * arch + (i ? 0.008f * us_randn(r) : 0.0f);
		}
		for (int i = 0; i < NP - 1; i++)
			nt_add(st, lx[k][i], ly[k][i], lx[k][i + 1], ly[k][i + 1], lr[k] * (1.0f - 0.7f * i / (NP - 1)),
			       lr[k] * (1.0f - 0.7f * (i + 1) / (NP - 1)));
	}
	/* foliage: clumps along the outer two thirds of each limb, each hung on its own twig; noise leaves holes of
	 * sky through the crown, and leaves fringe every clump */
	int placed = 0;
	for (int tries = 0; tries < 1400 && placed < 130 && st->nt_n < NTSEG - 2; tries++) {
		int k = (int)(us_rand(r) * NL) % NL;
		float u = us_randr(r, 0.3f, 1.0f);
		float fi = u * (NP - 1);
		int i = (int)fi;
		if (i >= NP - 1)
			i = NP - 2;
		float t = fi - i, bx = us_lerp(lx[k][i], lx[k][i + 1], t), by = us_lerp(ly[k][i], ly[k][i + 1], t);
		float a = us_randr(r, -US_PI, 0.0f) + us_randn(r) * 0.7f, d = us_randr(r, 0.01f, 0.075f);
		float cx = bx + cosf(a) * d, cy = by + sinf(a) * d * 0.8f;
		if (us_fbm2(cx * 7.0f + 3.0f, cy * 7.0f, 2) < -0.22f)
			continue;
		float rr = us_randr(r, 0.018f, 0.04f) * (1.0f - 0.3f * u * (k >= 3));
		nt_add(st, bx, by, cx, cy, 0.0035f, 0.002f);
		nt_add(st, cx, cy, cx, cy, rr, rr);
		placed++;
		int nl = 4 + (int)(us_rand(r) * 4);
		for (int j = 0; j < nl; j++) {
			float b = us_rand(r) * US_TAU;
			if (sinf(b) < -0.5f && us_rand(r) < 0.5f) /* fewer on top, more hanging below */
				b = -b;
			nt_leaf(st, cx + cosf(b) * rr * 0.85f, cy + sinf(b) * rr * 0.85f, b + us_randn(r) * 0.35f);
		}
	}
}

static void build_treemask(st_t *st, int W, int H, float s, int crisp)
{
	uint8_t *cov = st->tm_cov, *rim = st->tm_rim;
	memset(cov, 0, (size_t)W * H);
	memset(rim, 0, (size_t)W * H);
	int bx0 = W, bx1 = 0, by0 = H, by1 = 0;
	for (int k = 0; k < st->nt_n; k++) {
		float ax = st->nt_ax[k] * s, ay = st->nt_ay[k] * s, bx = st->nt_bx[k] * s, by = st->nt_by[k] * s;
		float ra = st->nt_ra[k] * s, rb = st->nt_rb[k] * s, rm = (ra > rb ? ra : rb) + 1.0f;
		int x0 = (int)floorf(fminf(ax, bx) - rm), x1 = (int)ceilf(fmaxf(ax, bx) + rm);
		int y0 = (int)floorf(fminf(ay, by) - rm), y1 = (int)ceilf(fmaxf(ay, by) + rm);
		if (x0 < 0)
			x0 = 0;
		if (y0 < 0)
			y0 = 0;
		if (x1 > W)
			x1 = W;
		if (y1 > H)
			y1 = H;
		if (x0 >= x1 || y0 >= y1)
			continue;
		float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy + 1e-6f;
		for (int py = y0; py < y1; py++)
			for (int px = x0; px < x1; px++) {
				float qx = px + 0.5f - ax, qy = py + 0.5f - ay;
				float u = us_sat((qx * dx + qy * dy) / l2);
				float ex = qx - dx * u, ey = qy - dy * u;
				float d = sqrtf(ex * ex + ey * ey), rr = us_lerp(ra, rb, u);
				float cv = crisp ? (d <= rr ? 1.0f : 0.0f) : us_sat(rr - d + 0.5f);
				int iv = (int)(cv * 255.0f + 0.5f);
				uint8_t *o = cov + (size_t)py * W + px;
				if (iv > *o)
					*o = (uint8_t)iv;
			}
		if (x0 < bx0)
			bx0 = x0;
		if (x1 > bx1)
			bx1 = x1;
		if (y0 < by0)
			by0 = y0;
		if (y1 > by1)
			by1 = y1;
	}
	/* rim light: inside the silhouette where the pixel toward the swarm (right, a touch down) is outside it */
	int ox = crisp ? 1 : 2, oy = crisp ? 0 : 1;
	for (int py = by0; py < by1; py++)
		for (int px = bx0; px < bx1; px++) {
			size_t i = (size_t)py * W + px;
			if (!cov[i])
				continue;
			int qx = px + ox, qy = py + oy < H ? py + oy : H - 1;
			float o = qx < W ? cov[(size_t)qy * W + qx] / 255.0f : 0.0f;
			float o2 = px - ox >= 0 ? cov[(size_t)py * W + px - ox] / 255.0f : 1.0f; /* far side: faint */
			float rv = cov[i] / 255.0f * fmaxf(1.0f - o, 0.12f * (1.0f - o2));
			rim[i] = (uint8_t)(us_sat(rv) * 255.0f + 0.5f);
		}
	st->tm_x0 = bx0, st->tm_x1 = bx1, st->tm_y0 = by0, st->tm_y1 = by1;
	/* leaves whose whole sway range stays inside the silhouette are never seen */
	for (int k = 0; k < st->lf_n; k++) {
		int hid = 1;
		for (int q = -2; q <= 2 && hid; q++) {
			float a = st->lf_a[k] + 0.2f * q;
			for (int j = 1; j <= 3 && hid; j++) {
				float u = j / 3.0f + 0.15f;
				int px = (int)((st->lf_x[k] + cosf(a) * st->lf_l[k] * u) * s), py = (int)((st->lf_y[k] + sinf(a) * st->lf_l[k] * u) * s);
				if (px < 0 || py < 0 || px >= W || py >= H || cov[(size_t)py * W + px] < 250)
					hid = 0;
			}
		}
		st->lf_hid[k] = (uint8_t)hid;
	}
}

/* one leaf: a slender filled lens from stalk to tip, darkened into the silhouette, its lit edge catching light */
static void draw_leaf(us_canvas *c, float x0, float y0, float x1, float y1, float tone, float rimv, uint8_t m,
		      const uint8_t *mask)
{
	float ax = x0 * c->s, ay = y0 * c->s, bx = x1 * c->s, by = y1 * c->s;
	float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy + 1e-6f, len = sqrtf(l2);
	float hw = 0.21f * len + (c->crisp ? 0.35f : 0.25f);
	int px0 = (int)floorf(fminf(ax, bx) - hw - 1), px1 = (int)ceilf(fmaxf(ax, bx) + hw + 1);
	int py0 = (int)floorf(fminf(ay, by) - hw - 1), py1 = (int)ceilf(fmaxf(ay, by) + hw + 1);
	if (px0 < 0)
		px0 = 0;
	if (py0 < 0)
		py0 = 0;
	if (px1 > c->w)
		px1 = c->w;
	if (py1 > c->h)
		py1 = c->h;
	int tv = (int)(tone * 255.0f + 0.5f);
	/* the lit side faces right / down (toward the swarm) */
	float nx = -dy / len, ny = dx / len;
	if (nx + 0.5f * ny < 0)
		nx = -nx, ny = -ny;
	for (int py = py0; py < py1; py++)
		for (int px = px0; px < px1; px++) {
			float qx = px + 0.5f - ax, qy = py + 0.5f - ay;
			float u = (qx * dx + qy * dy) / l2;
			if (u < -0.05f || u > 1.05f)
				continue;
			float uu = us_sat(u), w = hw * 4.0f * uu * (1.0f - uu) + 0.3f;
			float d = qx * nx + qy * ny;
			float cv = c->crisp ? (fabsf(d) <= w ? 1.0f : 0.0f) : us_sat(w - fabsf(d) + 0.5f);
			if (cv <= 0)
				continue;
			size_t i = (size_t)py * c->w + px;
			int iv = (int)(c->ink[i] + (tv - c->ink[i]) * cv);
			/* rim: the outer edge on the lit side */
			float e = d > 0 && mask[i] < 160 ? us_sat(1.0f - fabsf(w - d) / (c->crisp ? 0.8f : 1.1f)) : 0.0f;
			int rv = (int)(rimv * e * cv * 255.0f);
			if (rv > iv)
				iv = rv;
			c->ink[i] = (uint8_t)(iv > 255 ? 255 : iv);
			if (rv >= iv && rv > 0)
				c->mat[i] = US_MAT_LEAF;
			else if (cv > 0.5f)
				c->mat[i] = m;
		}
}

static void draw_neartree(st_t *st, us_canvas *c, int crisp, float rim, float t, float wind)
{
	/* trunk + boughs from the cached mask */
	float tone = crisp ? 0.09f : 0.012f;
	for (int py = st->tm_y0; py < st->tm_y1; py++) {
		uint8_t *ink = c->ink + (size_t)py * c->w, *mat = c->mat + (size_t)py * c->w;
		const uint8_t *cv = st->tm_cov + (size_t)py * c->w, *rm = st->tm_rim + (size_t)py * c->w;
		for (int px = st->tm_x0; px < st->tm_x1; px++) {
			int a = cv[px];
			if (!a)
				continue;
			float iv = ink[px] + (tone * 255.0f - ink[px]) * (a / 255.0f);
			float rv = rm[px] * rim;
			if (rv > iv)
				iv = rv;
			ink[px] = (uint8_t)(iv > 255.0f ? 255 : (int)iv);
			if (a > 128)
				mat[px] = US_MAT_EARTH;
		}
	}
	/* leaves: fine lenses that sway, darker inside, lit on their edge */
	float sw = 0.10f + 0.30f * wind;
	for (int k = 0; k < st->lf_n; k++) {
		if (st->lf_hid[k])
			continue;
		float a = st->lf_a[k] + sw * sinf(t * (0.8f + 0.25f * (k % 5)) + k * 1.7f);
		float x0 = st->lf_x[k] + 0.0015f * sw * sinf(t * 0.7f + k * 0.37f), y0 = st->lf_y[k], l = st->lf_l[k];
		float x1 = x0 + cosf(a) * l, y1 = y0 + sinf(a) * l;
		float rv = rim * (0.75f + 0.25f * sinf(k * 2.1f));
		draw_leaf(c, x0, y0, x1, y1, crisp ? 0.09f : 0.012f, rv, US_MAT_EARTH, st->tm_cov);
	}
}

static float lay_at(const float *arr, float xf)
{
	float u = us_clampf(xf, 0.0f, 1.0f) * (TREE_N - 1);
	int i = (int)u;
	if (i >= TREE_N - 1)
		return arr[TREE_N - 1];
	float t = u - i;
	return arr[i] * (1 - t) + arr[i + 1] * t;
}

static float tree_at(const st_t *st, float xf) { return lay_at(st->tree, xf); }

static const float lay_base[NLAY] = {0.602f, 0.620f, 0.632f};

/* crowns on one treeline layer: rounded deciduous crowns (three lobes) and notched conifers */
static void crowns(st_t *st, int L, int n, float hmin, float hmax, float pcon, float gap)
{
	us_rng *r = &st->rng;
	for (int k = 0; k < n; k++) {
		float cx = us_randr(r, -0.03f, 1.03f);
		int conifer = us_rand(r) < pcon;
		float h = us_randr(r, hmin, hmax);
		if (gap > 0 && cx > 0.30f && cx < 0.42f) /* a dip in the wood makes the line breathe */
			h *= gap;
		float wdt = conifer ? h * us_randr(r, 0.30f, 0.42f) : h * us_randr(r, 0.55f, 0.8f);
		float base = lay_base[L], ph = us_rand(r) * 50.0f;
		float R = wdt * 0.62f, lob = us_randr(r, 0.45f, 0.65f);
		for (int i = 0; i < TREE_N; i++) {
			float xf = (float)i / (TREE_N - 1);
			float d = (xf - cx) * 1.7778f; /* roughly isotropic at 16:9 */
			float top = 2.0f;
			if (conifer) {
				float edge = fabsf(d) / wdt;
				if (edge > 1.0f)
					continue;
				float tier = 0.07f * fabsf(sinf(edge * 8.0f + ph)) * (0.3f + edge);
				top = base - h * (1.0f - edge) + h * tier;
			} else {
				/* a central lobe and two shoulders, edged with foliage texture */
				const float lx[3] = {0.0f, -0.62f, 0.62f};
				for (int j = 0; j < 3; j++) {
					float rl = j ? R * lob + R * 0.2f : R, cyl = base - h + R + (j ? R * 0.45f : 0.0f);
					float dl = d - lx[j] * wdt;
					if (fabsf(dl) < rl) {
						float tp = cyl - sqrtf(rl * rl - dl * dl);
						if (tp < top)
							top = tp;
					}
				}
				if (top > 1.5f)
					continue;
				top += 0.0035f * us_noise2(xf * 160.0f, ph) + 0.002f * us_noise2(xf * 420.0f, ph);
			}
			if (top < st->lay[L][i])
				st->lay[L][i] = top;
		}
	}
}

static void build_treeline(st_t *st)
{
	for (int L = 0; L < NLAY; L++)
		for (int i = 0; i < TREE_N; i++)
			st->lay[L][i] = lay_base[L] - (L == 0 ? 0.02f : 0.008f) * (0.5f + 0.5f * us_fbm2(i * 0.012f + L * 7.3f, 3.7f, 3));
	crowns(st, 0, 120, 0.035f, 0.085f, 0.45f, 0);    /* far: a long forest of small crowns */
	crowns(st, 1, 46, 0.045f, 0.115f, 0.40f, 0.45f); /* middle wood, with a dip */
	crowns(st, 2, 46, 0.008f, 0.028f, 0.15f, 0);     /* near bank: low brush .. */
	crowns(st, 2, 6, 0.07f, 0.14f, 0.5f, 0.3f);      /* .. broken by a few taller trees */
	for (int i = 0; i < TREE_N; i++) {
		float m = st->lay[0][i];
		for (int L = 1; L < NLAY; L++)
			if (st->lay[L][i] < m)
				m = st->lay[L][i];
		st->tree[i] = m;
	}
}

/* ---- the cached backdrop: sky + moon glow, treeline layers with haze, the pond's mirror, the near bank ---- */
typedef struct {
	float sky_top, sky_hor, glow, lay[NLAY], haze[NLAY], lglow[NLAY], ground, refl, dith;
	uint8_t msky, mlay[NLAY];
} tones_t;

static tones_t get_tones(int crisp, float day)
{
	tones_t T;
	if (crisp) {
		T = (tones_t){0.17f, 0.54f, 0.30f, {0.36f, 0.20f, 0.08f}, {0.10f, 0.07f, 0.03f}, {0.3f, 0.1f, 0.0f},
			      0.035f, 0.78f, 0.045f, US_MAT_BG, {US_MAT_BG, US_MAT_BG, US_MAT_EARTH}};
	} else {
		T = (tones_t){0.05f, 0.19f, 0.28f, {0.125f, 0.078f, 0.032f}, {0.06f, 0.04f, 0.018f}, {0.35f, 0.12f, 0.0f},
			      0.022f, 0.62f, 1.0f / 255.0f, US_MAT_WATER, {US_MAT_WATER, US_MAT_BG, US_MAT_EARTH}};
	}
	T.sky_top += 0.25f * day;
	T.sky_hor += 0.22f * day;
	T.glow *= 1.0f - day;
	for (int L = 0; L < NLAY; L++)
		T.lay[L] += 0.08f * day * (NLAY - L) / NLAY;
	return T;
}

static float shore_at(float xf)
{
	return POOL_Y + 0.012f * us_fbm2(xf * 5.0f, 1.3f, 2) + 0.004f * us_noise2(xf * 45.0f, 7.7f);
}

/* above the water: sky + glow, then each treeline layer over it (soft edges, haze toward its foot) */
static float bd_above(const tones_t *T, const float *tops, float x, float y, float mx, float my, float mv, float px1,
		      uint8_t *mat)
{
	float u = us_sat(y / W0);
	float v = T->sky_top + (T->sky_hor - T->sky_top) * u * u * u;
	float gl = 0;
	if (mv > 0) {
		float d2 = (x - mx) * (x - mx) + (y - my) * (y - my);
		gl = mv * T->glow * (0.45f / (1.0f + d2 * 500.0f) + 0.55f / (1.0f + d2 * 18.0f));
	}
	v += gl;
	uint8_t m = T->msky;
	for (int L = 0; L < NLAY; L++) {
		float soft = L == 0 ? 0.45f : L == 1 ? 0.7f : 1.0f; /* far edges are softer */
		float cv = us_sat((y - tops[L]) * px1 * soft + 0.5f);
		if (cv <= 0)
			continue;
		float lv = T->lay[L] + T->haze[L] * us_smooth(tops[L] + 0.01f, W0 + 0.005f, y) + T->lglow[L] * gl;
		v += (lv - v) * cv;
		if (cv > 0.5f)
			m = T->mlay[L];
	}
	*mat = m;
	return v;
}

static void build_backdrop(st_t *st, int W, int H, float s, int crisp, float day, float mx, float my, float mv)
{
	static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
	tones_t T = get_tones(crisp, day);
	float A = (float)W / H;
	float *col = malloc(sizeof(float) * (size_t)W * (NLAY + 1));
	if (!col)
		return;
	for (int px = 0; px < W; px++) {
		float xf = (px + 0.5f) / W;
		for (int L = 0; L < NLAY; L++)
			col[px * (NLAY + 1) + L] = lay_at(st->lay[L], xf);
		col[px * (NLAY + 1) + NLAY] = shore_at(xf);
	}
	for (int py = 0; py < H; py++) {
		float y = (py + 0.5f) / s;
		uint8_t *oi = st->bd_ink + (size_t)py * W, *om = st->bd_mat + (size_t)py * W;
		const uint8_t *bz = bayer + (py & 3) * 4;
		/* the water's mirror wobbles a little, more toward the near shore */
		float dpt = us_sat((y - W0) / (POOL_Y - W0));
		int wob = crisp ? 0 : (int)lrintf((0.6f + 1.4f * dpt) * sinf(py * 0.9f) * (y > W0));
		for (int px = 0; px < W; px++) {
			const float *cl = col + px * (NLAY + 1);
			float x = (px + 0.5f) / s, v;
			uint8_t m;
			float sh = cl[NLAY];
			if (y < W0) {
				v = bd_above(&T, cl, x, y, mx, my, mv, s, &m);
			} else if (y < sh + 0.5f / s) {
				int sx = px + wob;
				sx = sx < 0 ? 0 : sx >= W ? W - 1 : sx;
				float ys = W0 - (y - W0) * 0.97f;
				v = bd_above(&T, col + sx * (NLAY + 1), (sx + 0.5f) / s, ys, mx, my, mv, s, &m);
				v = v * T.refl * (1.0f - 0.2f * dpt) + 0.012f;
				/* a thin line of light where the wood meets the water */
				v += (crisp ? 0.08f : 0.05f) * expf(-(y - W0) * s / (crisp ? 0.8f : 1.5f));
				if (!crisp && m == US_MAT_BG)
					m = US_MAT_WATER;
				/* soft near shore */
				float cv = us_sat((sh - y) * s + 0.5f);
				v = T.ground + (v - T.ground) * cv;
				if (cv < 0.5f)
					m = US_MAT_EARTH;
			} else {
				v = T.ground * (1.0f + 0.4f * us_sat((y - sh) / 0.15f));
				m = US_MAT_EARTH;
			}
			(void)A;
			v += (bz[px & 3] / 16.0f - 0.47f) * T.dith * us_sat(v * 6.0f);
			int iv = (int)(v * 255.0f + 0.5f);
			oi[px] = (uint8_t)(iv < 0 ? 0 : iv > 255 ? 255 : iv);
			om[px] = m;
		}
	}
	free(col);
}

/* (re)build the caches when the canvas changes, and the backdrop when the moon or the daylight has moved */
static void ensure_cache(st_t *st, us_canvas *c, float day, float mx, float my, float mv)
{
	int W = c->w, H = c->h;
	if (W != st->bd_w || H != st->bd_h || c->crisp != st->bd_crisp || !st->bd_ink) {
		free_cache(st);
		size_t n = (size_t)W * H;
		st->bd_ink = malloc(n);
		st->bd_mat = malloc(n);
		st->tm_cov = malloc(n);
		st->tm_rim = malloc(n);
		if (!st->bd_ink || !st->bd_mat || !st->tm_cov || !st->tm_rim) {
			free_cache(st);
			return;
		}
		st->bd_w = W, st->bd_h = H, st->bd_crisp = c->crisp;
		build_treemask(st, W, H, c->s, c->crisp);
		st->bd_ok = 0;
	}
	float mv_q = mv > 0.02f ? mv : 0.0f;
	if (!st->bd_ok || fabsf(day - st->bd_day) > 0.015f || fabsf(mv_q - st->bd_moon) > 0.05f ||
	    (mv_q > 0 && (fabsf(mx - st->bd_mx) + fabsf(my - st->bd_my)) * c->s > 6.0f)) {
		build_backdrop(st, W, H, c->s, c->crisp, day, mx, my, mv_q);
		st->bd_ok = 1, st->bd_day = day, st->bd_moon = mv_q, st->bd_mx = mx, st->bd_my = my;
	}
}

static void blit_backdrop(const st_t *st, us_canvas *c)
{
	size_t n = (size_t)c->w * c->h;
	uint8_t *ink = c->ink, *mat = c->mat;
	const uint8_t *bi = st->bd_ink, *bm = st->bd_mat;
	for (size_t i = 0; i < n; i++)
		if (bi[i] >= ink[i]) {
			ink[i] = bi[i];
			mat[i] = bm[i];
		}
}

/* a round firefly: a hot anti-aliased core inside a soft quadratic halo (additive) */
static void glow_dot(us_canvas *c, float x, float y, float rc, float rh, float vc, float vh, uint8_t m)
{
	float cx = x * c->s, cy = y * c->s, rcp = rc * c->s, rhp = rh * c->s;
	if (rhp < rcp + 1.0f)
		rhp = rcp + 1.0f;
	int x0 = (int)floorf(cx - rhp), x1 = (int)ceilf(cx + rhp), y0 = (int)floorf(cy - rhp), y1 = (int)ceilf(cy + rhp);
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > c->w)
		x1 = c->w;
	if (y1 > c->h)
		y1 = c->h;
	float ih = 1.0f / rhp, r2 = rhp * rhp;
	for (int j = y0; j < y1; j++) {
		float dy = j + 0.5f - cy;
		for (int i = x0; i < x1; i++) {
			float dx = i + 0.5f - cx, d2 = dx * dx + dy * dy;
			if (d2 >= r2)
				continue;
			float d = sqrtf(d2), hq = 1.0f - d * ih;
			float v = vc * us_sat(rcp + 0.5f - d) + vh * hq * hq;
			if (v > 0.004f)
				us_put(c, i, j, v, m);
		}
	}
}

/* ---- drawing one firefly (additive): its long-exposure trail, a round glowing body, drum answers ---- */
static void draw_fly(us_canvas *c, const fly_t *f, float trail_k, float aamp)
{
	int crisp = c->crisp;
	float x = f->px, y = f->py, size = f->dsz, v = f->dv, vis = f->dvis;
	/* trail: the path remembers how bright it was where it was flown */
	if (f->z > 0.2f) {
		float px = x, py = y, pb = f->hb[f->hh];
		int idx = f->hh;
		for (int k = 0; k < NHIST - 1; k++) {
			float hb = f->hb[idx];
			float tv = 0.10f * trail_k * (pb < hb ? pb : hb) * (1.0f - (float)k / NHIST);
			if (tv > 0.015f) {
				if (crisp)
					us_line(c, px, py, f->hx[idx], f->hy[idx], 0.002f, tv * 0.7f, US_MAT_LIGHT);
				else
					wline(c, px, py, f->hx[idx], f->hy[idx], tv * (0.5f + 0.3f * size), US_MAT_LIGHT);
			}
			px = f->hx[idx], py = f->hy[idx], pb = hb;
			idx = (idx + NHIST - 1) % NHIST;
		}
	}
	if (v <= 0.012f) {
		/* between flashes: only a faint ember, warmer the louder the music */
		float gv = f->dlant;
		if (!crisp)
			us_point(c, x, y, 0.0009f * size, gv, US_MAT_LIGHT);
		else if (gv > 0.09f)
			us_put(c, (int)(x * c->s), (int)(y * c->s), 1.3f * gv, US_MAT_LIGHT);
		return;
	}
	float pw = f->dpw, fade = us_sat(pw / 1.1f), twk = f->dtwk;
	if (crisp) {
		us_disc(c, x, y, 0.0036f * size, v, US_MAT_LIGHT);
		if (f->z > 0.45f) /* a soft pixel halo */
			us_disc(c, x, y, 0.0036f * size + 1.2f / c->s, 0.1f * v, US_MAT_LIGHT);
		if (f->se > 0.15f) {
			float su = 1.0f - us_sat(f->se / 1.3f), rr = (0.008f + 0.025f * su) * (0.6f + 0.8f * f->z);
			hellipse(c, x, y, rr, rr, 0.95f * f->se * vis * (1.0f - su), US_MAT_ACCENT);
		}
		if (v > 0.6f || twk > 0.35f) /* a cross-shaped glint on the brightest flashes and twinkles */
			for (int k = -1; k <= 1; k += 2) {
				us_put(c, (int)(x * c->s) + k, (int)(y * c->s), 0.5f * v, US_MAT_LIGHT);
				us_put(c, (int)(x * c->s), (int)(y * c->s) + k, 0.5f * v, US_MAT_LIGHT);
			}
		return;
	}
	/* (the host's trails accumulate additive light over a few frames: the halo is kept faint) */
	float rc = 0.0022f * (0.75f + 0.85f * size), rh = rc * (4.0f + 3.0f * f->z);
	glow_dot(c, x, y, rc, rh, 1.0f * v, 0.13f * v, US_MAT_LIGHT);
	if (twk > 0.08f) { /* a hat twinkle: a tiny four-point glint far away */
		float gl = 0.0065f * twk, gv = 0.8f * v;
		wline(c, x - gl, y, x + gl, y, gv, US_MAT_LIGHT);
		wline(c, x, y - gl, x, y + gl, gv, US_MAT_LIGHT);
	}
	if (f->se > 0.06f) { /* a snare burst: each bursting firefly throws off a quick widening halo */
		float su = 1.0f - us_sat(f->se / 1.3f);
		hring(c, x, y, (0.006f + 0.03f * su) * (0.6f + 0.8f * f->z), 0.6f * f->se * vis * (1.0f - su));
	}
	if (pw > 0 && f->dba > 0.05f && f->z > 0.3f) { /* the afterglow ring of a natural flash */
		float rr = (0.008f + 0.026f * fade) * size;
		hring(c, x, y, rr, 0.16f * vis * aamp * f->dpart * (1.0f - fade) * (1.0f - fade));
	}
}

/* its reflection in the still pond: mirrored about the water surface beneath it, softened into a streak */
static void draw_refl(us_canvas *c, const fly_t *f, float t)
{
	if (f->dv <= 0.02f || f->z > 0.8f)
		return;
	float ys = us_lerp(W0 + 0.004f, POOL_Y - 0.01f, us_sat(f->z * 1.25f)); /* the water surface under it */
	if (f->py >= ys)
		return;
	float ry = ys + (ys - f->py) * 0.92f;
	if (ry > shore_at(f->x) - 0.004f)
		return;
	float dp = us_sat((ry - W0) / (POOL_Y - W0));
	float rv = f->dv * (c->crisp ? 0.3f : 0.55f) * (1.0f - 0.5f * dp);
	float x = f->px, size = f->dsz;
	float wob = 0.0025f * sinf(t * 2.3f + ry * 140.0f) + 0.0015f * us_noise2(x * 30.0f, t * 0.8f);
	float rl = (0.004f + 0.01f * us_sat(f->dbs)) * size;
	if (c->crisp) {
		us_line(c, x + wob, ry - rl, x - wob, ry + rl, 0.003f * size, rv, US_MAT_LIGHT);
		return;
	}
	float rc = 0.0009f * (0.7f + 0.9f * size);
	glow_dot(c, x + wob, ry, rc, rc * 4.0f, 0.6f * rv, 0.05f * rv, US_MAT_LIGHT);
	wline(c, x + wob, ry - rl, x - wob, ry + rl, 0.5f * rv, US_MAT_LIGHT);
}

/* add a low-res scalar field (bilinear, ordered-dithered so it never bands) onto the canvas, keeping materials */
static void field_add(us_canvas *c, const float *f, int fw, int fh, float y0, float y1, float dith, const uint8_t *bi,
		      const uint8_t *bm)
{
	static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
	int p0 = (int)floorf(y0 * c->s), p1 = (int)ceilf(y1 * c->s);
	if (p0 < 0)
		p0 = 0;
	if (p1 > c->h)
		p1 = c->h;
	float sh = (y1 - y0) * c->s;
	for (int j = p0; j < p1; j++) {
		float fy = ((j + 0.5f) - y0 * c->s) / sh * fh - 0.5f;
		int iy = (int)floorf(fy);
		float ty = fy - iy;
		int a0 = iy < 0 ? 0 : iy >= fh ? fh - 1 : iy, a1 = iy + 1 < 0 ? 0 : iy + 1 >= fh ? fh - 1 : iy + 1;
		const float *r0 = f + a0 * fw, *r1 = f + a1 * fw;
		uint8_t *ink = c->ink + (size_t)j * c->w;
		const uint8_t *bz = bayer + (j & 3) * 4;
		float gsc = (float)fw / c->w;
		for (int i = 0; i < c->w; i++) {
			float fx = (i + 0.5f) * gsc - 0.5f;
			int ix = (int)fx;
			if (fx < 0)
				ix = 0, fx = 0;
			if (ix >= fw - 1)
				ix = fw - 2, fx = (float)(fw - 1);
			float tx = fx - ix;
			float v = us_lerp(us_lerp(r0[ix], r0[ix + 1], tx), us_lerp(r1[ix], r1[ix + 1], tx), ty);
			if (v <= 0.002f)
				continue;
			/* over the backdrop, not over last frame's mist (the host's trails would pile it up) */
			size_t q = (size_t)j * c->w + i;
			int iv = (bi ? bi[q] : ink[i]) + (int)((v + (bz[i & 3] / 16.0f - 0.47f) * dith) * 255.0f + 0.5f);
			iv = iv < 0 ? 0 : iv > 255 ? 255 : iv;
			if (iv > ink[i]) {
				ink[i] = (uint8_t)iv;
				if (bm)
					c->mat[q] = c->crisp || v < 0.006f + 0.03f * bz[i & 3] / 16.0f ? bm[q] : US_MAT_WATER; /* cool haze */
			}
		}
	}
}

/* a tapered blade or stem, darkened into the bank as a silhouette, with a fine lit edge */
static void blade_fill(us_canvas *c, const float *pts, int n, float w0, float tone, float shore)
{
	int tv = (int)(tone * 255.0f + 0.5f);
	for (int q = 0; q < n - 1; q++) {
		if (pts[2 * q + 1] > shore + 0.01f && pts[2 * q + 3] > shore + 0.01f)
			continue; /* dark on the dark bank: nothing to carve */
		float ax = pts[2 * q] * c->s, ay = pts[2 * q + 1] * c->s, bx = pts[2 * q + 2] * c->s, by = pts[2 * q + 3] * c->s;
		float ra = w0 * c->s * (1.0f - (float)q / (n - 1)), rb = w0 * c->s * (1.0f - (float)(q + 1) / (n - 1));
		float rm = (ra > rb ? ra : rb) + 1.0f;
		int x0 = (int)floorf(fminf(ax, bx) - rm), x1 = (int)ceilf(fmaxf(ax, bx) + rm);
		int y0 = (int)floorf(fminf(ay, by) - rm), y1 = (int)ceilf(fmaxf(ay, by) + rm);
		if (x0 < 0)
			x0 = 0;
		if (y0 < 0)
			y0 = 0;
		if (x1 > c->w)
			x1 = c->w;
		if (y1 > c->h)
			y1 = c->h;
		float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy + 1e-6f;
		for (int py = y0; py < y1; py++)
			for (int px = x0; px < x1; px++) {
				float qx = px + 0.5f - ax, qy = py + 0.5f - ay;
				float u = us_sat((qx * dx + qy * dy) / l2);
				float ex = qx - dx * u, ey = qy - dy * u;
				float cv = us_sat(us_lerp(ra, rb, u) + 0.5f - sqrtf(ex * ex + ey * ey));
				if (cv <= 0)
					continue;
				size_t i = (size_t)py * c->w + px;
				if (c->ink[i] > tv)
					c->ink[i] = (uint8_t)(c->ink[i] + (tv - c->ink[i]) * cv);
				if (cv > 0.5f)
					c->mat[i] = US_MAT_EARTH;
			}
	}
}

/* the height band a firefly at depth z keeps to */
static void fly_band(float z, float *ylo, float *yhi)
{
	*ylo = us_lerp(0.46f, 0.70f, z);
	*yhi = us_lerp(0.625f, 0.885f, z);
}

static void draw_moon(us_canvas *c, float x, float y, float r, float mv)
{
	float cx = x * c->s, cy = y * c->s, rp = r * c->s;
	int x0 = (int)floorf(cx - rp - 1), x1 = (int)ceilf(cx + rp + 1), y0 = (int)floorf(cy - rp - 1), y1 = (int)ceilf(cy + rp + 1);
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > c->w)
		x1 = c->w;
	if (y1 > c->h)
		y1 = c->h;
	/* the terminator: an ellipse across the disc leaves a waxing gibbous */
	for (int j = y0; j < y1; j++)
		for (int i = x0; i < x1; i++) {
			float dx = i + 0.5f - cx, dy = j + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
			float cv = c->crisp ? (d <= rp ? 1.0f : 0.0f) : us_sat(rp - d + 0.5f);
			if (cv <= 0)
				continue;
			float tx = -0.45f * sqrtf(fmaxf(rp * rp - dy * dy, 0.0f));
			float lit = c->crisp ? (dx > tx ? 1.0f : 0.0f) : us_smooth(tx - 1.0f, tx + 1.0f, dx);
			float u = dx / rp, w = dy / rp;
			float maria = us_sat(0.5f + 0.9f * us_noise2(u * 1.8f + 3.1f, w * 1.8f + 1.7f));
			float v = lit * (0.88f - 0.2f * maria * (c->crisp ? 0.5f : 1.0f)) + (1.0f - lit) * (c->crisp ? 0.3f : 0.1f);
			us_put(c, i, j, v * cv * mv, US_MAT_ACCENT);
		}
}

static void spawn_fly(st_t *st, fly_t *f, float A, float life0)
{
	us_rng *r = &st->rng;
	memset(f, 0, sizeof(*f));
	/* most fireflies are out in the field; only a few drift close to the viewer */
	f->z = powf(us_rand(r), 1.1f);
	f->x = us_rand(r);
	f->rank = us_rand(r);
	f->kd = -1.0f;
	/* far ones hug the treeline, near ones roam low over the water and the near bank */
	float ylo, yhi;
	fly_band(f->z, &ylo, &yhi);
	f->y = us_randr(r, ylo, yhi);
	f->clu = us_rand(r) < 0.7f ? (int)(us_rand(r) * NCLU) % NCLU : -1;
	/* colony by depth: the bass colonies hang low and near in the grass, the treble ones far and high */
	f->col = (int)((1.0f - f->z) * (NCOL - 1) + 0.5f);
	if (us_rand(r) < 0.15f) { /* a few up in the trees: the air colony */
		f->y = us_randr(r, FLY_TOP + 0.04f, 0.6f);
		f->col = NCOL - 1;
	}
	f->th = us_randr(r, -US_PI, US_PI);
	f->dev = us_randn(r);
	f->life = life0;
	f->state = 1;
	f->nz = us_rand(r) * 100.0f;
	f->px = f->x * A;
	f->py = f->y;
	for (int k = 0; k < NHIST; k++) {
		f->hx[k] = f->px;
		f->hy[k] = f->py;
		f->hb[k] = 0;
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
	float day = us_smooth(0.35f, 0.85f, w->daylight); /* 0 dusk/night .. 1 broad day */
	float dayf = 1.0f - 0.8f * day;
	float nmax = 80.0f + 440.0f * dens;
	if (nmax > MAXF)
		nmax = MAXF;

	/* ---- energy + tempo ---- */
	/* "pulse" = how much rhythmic drive there is (kicks averaged over a bar-ish window) */
	st->pulse = us_approach(st->pulse, us_sat(a->kick * 1.2f + 0.3f * a->onset), 1.2f, rdt);
	float e_in = us_sat(0.35f * a->level + 0.2f * a->swell + 0.75f * st->pulse);
	st->energy = us_approach(st->energy, e_in, e_in > st->energy ? 1.2f : 3.5f, rdt);
	float E = st->energy;

	if (!st->inited) {
		build_treeline(st);
		build_neartree(st);
		us_rng *r = &st->rng;
	/* tall grass on the near bank, back to front (depth = draw order) */
		for (int b = 0; b < NBLADES; b++) {
			st->blade_d[b] = (b + us_rand(r)) / NBLADES;
			st->blade_x[b] = us_rand(r);
			float hh = us_rand(r);
			st->blade_h[b] = (0.06f + 0.2f * powf(hh, 1.5f)) * (0.8f + 0.5f * st->blade_d[b]);
			st->blade_lean[b] = us_randn(r) * 0.14f;
			st->blade_ph[b] = us_rand(r) * 50.0f;
		}
		/* seed heads on tall stems: dandelion clocks and grass panicles, kept clear of the big tree */
		for (int k = 0; k < NSEED; k++) {
			st->seed_x[k] = us_randr(r, 0.22f, 0.97f);
			st->seed_d[k] = us_randr(r, 0.35f, 0.9f);
			st->seed_h[k] = us_randr(r, 0.17f, 0.27f) * (0.8f + 0.4f * st->seed_d[k]);
			st->seed_ph[k] = us_rand(r) * 30.0f;
			st->seed_k[k] = k % 3 == 0;
		}
		for (int k = 0; k < NCLU; k++)
			st->clu_ph[k] = us_rand(r) * 100.0f;
		for (int k = 0; k < NCAT; k++) {
			st->cat_x[k] = us_rand(r) < 0.45f ? us_randr(r, 0.15f, 0.34f) : us_randr(r, 0.70f, 0.98f);
			st->cat_h[k] = us_randr(r, 0.10f, 0.19f);
			st->cat_ph[k] = us_rand(r) * 20.0f;
		}
		for (int k = 0; k < NSTARS; k++) {
			st->star_x[k] = us_rand(r);
			st->star_y[k] = 0.02f + 0.55f * us_rand(r) * us_rand(r);
			st->star_b[k] = 0.25f + 0.6f * us_rand(r) * us_rand(r);
			st->star_ph[k] = us_rand(r) * US_TAU;
		}
		/* the milky way: a soft clumpy band arcing across the upper sky */
		for (int k = 0; k < NMW; k++) {
			float u = us_rand(r);
			float off = us_randn(r) * (0.035f + 0.03f * us_rand(r));
			st->mw_x[k] = u;
			st->mw_y[k] = 0.06f + 0.30f * u - 0.16f * u * u + off; /* tilted, gently bowed */
			float clump = us_sat(0.5f + 0.9f * us_fbm2(u * 7.0f, off * 30.0f, 3));
			st->mw_b[k] = (0.12f + 0.5f * us_rand(r) * us_rand(r)) * (0.3f + 0.7f * clump) *
				      (1.0f - us_sat(fabsf(off) / 0.09f) * 0.6f);
		}
		for (int k = 0; k < NRIP; k++) {
			st->rip_x[k] = us_rand(r);
			st->rip_y[k] = us_randr(r, W0 + 0.006f, POOL_Y - 0.012f);
			st->rip_len[k] = us_randr(r, 0.02f, 0.09f);
			st->rip_sp[k] = us_randr(r, 0.004f, 0.012f) * (us_rand(r) < 0.5f ? -1 : 1);
		}
		/* population at open remembers the world's insects */
		float frac = (0.38f + 0.3f * w->insects) * dayf * (1.0f - 0.85f * winter);
		int n0 = (int)(nmax * frac);
		for (int i = 0; i < n0 && i < MAXF; i++)
			spawn_fly(st, &st->f[i], A, 1.0f);
		for (int k = 0; k < NMOTH; k++) {
			st->mo_x[k] = us_randr(r, 0.1f, 0.9f) * A;
			st->mo_y[k] = us_randr(r, 0.5f, 0.8f);
			st->mo_ph[k] = us_rand(r) * US_TAU;
			st->mo_tgt[k] = -1;
		}
		for (int k = 0; k < NPR; k++)
			st->pr_age[k] = -1;
		/* the owl's snag stands in the lowest stretch of canopy on the right, so it reads against the sky */
		{
			float best = -1;
			for (int k = 0; k <= 24; k++) {
				float xf = 0.70f + 0.22f * k / 24.0f;
				float tp = tree_at(st, xf);
				if (tp > best)
					best = tp, st->owl_xf = xf;
			}
		}
		st->owl_look_t = 2.0f;
		st->owl_blink_t = 3.0f;
		st->met_age = -1;
		st->met_cd = 4.0f;
		/* the orb-web hangs off the first cattail, toward the open water */
		st->web_r = 0.026f;
		st->web_x = st->cat_x[0] + (st->cat_x[0] < 0.5f ? 0.03f : -0.03f); /* fraction of width */
		st->web_y = POOL_Y - st->cat_h[0] * 0.5f;
		st->omega = US_TAU / 1.7f;
		st->beat_iv = 0.5f;
		st->inited = 1;
	}

	st->since_beat += rdt;
	if (a->beat) {
		if (st->since_beat > 0.22f && st->since_beat < 1.3f)
			st->beat_iv = us_lerp(st->beat_iv, st->since_beat, 0.25f);
		st->since_beat = 0;
	}
	{
		float P = st->beat_iv * 2.0f;
		while (P < 1.05f)
			P *= 2.0f;
		while (P > 2.3f)
			P *= 0.5f;
		float w_nat = US_TAU / 1.7f;
		float target = us_lerp(w_nat, US_TAU / P, us_sat(E * 1.4f));
		st->omega = us_approach(st->omega, target, 4.0f, dt);
	}

	/* ---- the music, region by region ---- */
	{
		st->sub_s = us_approach(st->sub_s, a->sub, a->sub > st->sub_s ? 0.15f : 0.9f, rdt);
		st->mid_s = us_approach(st->mid_s, a->mid, a->mid > st->mid_s ? 0.12f : 0.6f, rdt);
		st->high_s = us_approach(st->high_s, a->high, a->high > st->high_s ? 0.06f : 0.35f, rdt);
		st->air_s = us_approach(st->air_s, a->air, a->air > st->air_s ? 0.06f : 0.4f, rdt);
		st->swell_s = us_approach(st->swell_s, a->swell, 1.5f, rdt);
		st->width_s = us_approach(st->width_s, a->width, 1.2f, rdt);
		st->cent_s = us_approach(st->cent_s, a->centroid, 0.9f, rdt);
		/* the web shivers with each kick and settles in ~0.4 s */
		st->web_sh = a->kick > st->web_sh ? a->kick : us_approach(st->web_sh, 0, 0.18f, rdt);
		/* colonies: each one's slice of the spectrum. A rise above its own recent average is that colony's
		 * "hit": fireflies that were about to flash flash now (pulse-coupled, like the real ones) */
		for (int k = 0; k < NCOL; k++) {
			float e = 0;
			if (k == 0)
				e = a->sub;
			else if (k == 1)
				e = a->bass;
			else {
				for (int b = col_band[k]; b < col_band[k + 1]; b++)
					e += a->bands[b];
				e /= (float)(col_band[k + 1] - col_band[k]);
			}
			st->col_env[k] = us_approach(st->col_env[k], e, e > st->col_env[k] ? 0.04f : 0.45f, rdt);
			float rise = e - st->col_avg[k];
			st->col_avg[k] = us_approach(st->col_avg[k], e, 0.22f, rdt);
			st->col_cd[k] -= rdt;
			st->col_kick[k] = 0;
			if (rise > 0.09f && st->col_cd[k] <= 0) {
				st->col_kick[k] = us_sat(0.3f + rise * 3.0f);
				st->col_cd[k] = 0.14f;
			}
		}
	}

	/* ---- drums: the swarm answers each part of the kit in its own way ----
	 *   kick  -> a synchronised flash wave rolls out through the near and middle swarm (Photinus-style:
	 *            with a steady beat the swarm holds its light between kicks and lets it all go on them)
	 *   snare -> one cluster bursts and throws sparks upward
	 *   hats  -> tiny far fireflies along the treeline twinkle
	 *   loudness -> how many join in and how bright they burn */
	st->lvl_s = us_approach(st->lvl_s, a->level * (0.4f + 0.6f * a->active), a->level > st->lvl_s ? 0.12f : 0.7f, rdt);
	float LV = us_sat(st->lvl_s);
	if (a->kick_hit)
		st->drive += 0.4f * (1.0f - st->drive);
	st->drive = us_sat(st->drive - st->drive * rdt / 3.0f);
	float lit_frac = 0.22f + 0.78f * us_smooth(0.04f, 1.0f, LV);
	{
		int nal = 0;
		for (int i = 0; i < MAXF; i++)
			nal += st->f[i].state == 1;
		if (a->kick_hit && nal > 0) {
			float ox = us_randr(&st->rng, 0.15f, 0.85f) * A, oy = us_randr(&st->rng, 0.6f, 0.78f);
			float ka = 1.0f + 0.5f * us_sat(a->kick);
			float kfrac = 0.3f + 0.7f * us_smooth(0.05f, 1.0f, LV);
			for (int i = 0; i < MAXF; i++) {
				fly_t *f = &st->f[i];
				if (f->state != 1 || f->z < 0.15f || f->rank > kfrac)
					continue;
				float dx = f->x * A - ox, dy = f->y - oy;
				/* the front crosses the whole field in ~0.1 s: one wave, still a single beat */
				f->kd = 0.075f * us_sat(sqrtf(dx * dx + dy * dy) / (0.8f * A));
				f->ka = ka * (0.75f + 0.35f * f->z);
			}
			st->kglow = 1.0f;
		}
		if (a->snare_hit && nal > 0) {
			/* the burst centres on a living firefly in the middle distance so the cluster is never empty */
			int k0 = (int)(us_rand(&st->rng) * MAXF), ci = -1;
			for (int j = 0; j < MAXF; j++) {
				const fly_t *f = &st->f[(k0 + j) % MAXF];
				if (f->state == 1 && f->z > 0.2f && f->y > 0.5f && f->x > 0.12f && f->x < 0.88f) {
					ci = (k0 + j) % MAXF;
					break;
				}
			}
			if (ci >= 0) {
				float cx = st->f[ci].x * A, cy = st->f[ci].y, rad = 0.2f + 0.08f * LV;
				int ns = 0;
				for (int i = 0; i < MAXF; i++) {
					fly_t *f = &st->f[i];
					if (f->state != 1)
						continue;
					float dx = f->x * A - cx, dy = (f->y - cy) * 1.6f;
					float d = sqrtf(dx * dx + dy * dy) / rad;
					if (d >= 1.0f)
						continue;
					float e = 1.3f * (1.0f - 0.6f * d * d);
					if (e > f->se)
						f->se = e;
					/* the closest few throw sparks */
					if (ns < 80 && d < 0.85f) {
						for (int q = 0; q < 2; q++) {
							int k = st->sp_next;
							st->sp_next = (st->sp_next + 1) % NSPK;
							st->sp_x[k] = f->x * A;
							st->sp_y[k] = f->y;
							st->sp_vx[k] = us_randn(&st->rng) * 0.05f + dx * 0.4f;
							st->sp_vy[k] = -us_randr(&st->rng, 0.18f, 0.45f) * (0.6f + 0.6f * f->z);
							st->sp_age[k] = 0;
							st->sp_life[k] = us_randr(&st->rng, 0.3f, 0.55f);
							ns++;
						}
					}
				}
				st->sglow = 1.0f;
				st->sg_x = cx;
				st->sg_y = cy;
				st->sg_r = rad;
			}
		}
		if (a->hat_hit) {
			float hp = 0.3f + 0.3f * us_sat(a->hat);
			for (int i = 0; i < MAXF; i++) {
				fly_t *f = &st->f[i];
				if (f->state == 1 && (f->z < 0.3f || f->y < 0.55f) && us_rand(&st->rng) < hp)
					f->he = 1.0f;
			}
		}
	}
	st->kglow *= expf(-rdt / 0.12f);
	st->sglow *= expf(-rdt / 0.1f);

	/* ---- world ---- */
	us_world_add(w, &w->insects, 0.004f * w->calm * (1.0f - day) * (1.0f - winter), dt);

	/* ---- population ---- */
	int alive = 0;
	for (int i = 0; i < MAXF; i++)
		if (st->f[i].state == 1)
			alive++;
	float frac = (0.38f + 0.3f * w->insects + 0.2f * E) * dayf * (1.0f - 0.85f * winter);
	int target = (int)(nmax * us_sat(frac));
	if (alive < target) {
		st->spawn_acc += dt * (12.0f + 30.0f * E);
		while (st->spawn_acc >= 1.0f) {
			st->spawn_acc -= 1.0f;
			for (int i = 0; i < MAXF; i++)
				if (st->f[i].state == 0) {
					spawn_fly(st, &st->f[i], A, 0.0f);
					break;
				}
		}
	} else
		st->spawn_acc = 0;
	if (alive > target + 4) {
		st->kill_acc += dt * (1.5f + 0.05f * (alive - target));
		while (st->kill_acc >= 1.0f) {
			st->kill_acc -= 1.0f;
			int k = (int)(us_rand(&st->rng) * MAXF);
			for (int j = 0; j < MAXF; j++) {
				fly_t *f = &st->f[(k + j) % MAXF];
				if (f->state == 1) {
					f->state = 2;
					break;
				}
			}
		}
	} else
		st->kill_acc = 0;

	/* ---- Kuramoto mean fields on a coarse grid ---- */
	memset(st->gc, 0, sizeof(st->gc));
	memset(st->gs, 0, sizeof(st->gs));
	memset(st->gn, 0, sizeof(st->gn));
	float Cg = 0, Sg = 0, Ng = 0;
	for (int i = 0; i < MAXF; i++) {
		fly_t *f = &st->f[i];
		if (!f->state)
			continue;
		int gx = (int)(us_sat(f->x) * (GX - 0.001f));
		int gy = (int)(us_sat((f->y - FLY_TOP) / (FLY_BOT - FLY_TOP)) * (GY - 0.001f));
		f->cell = gy * GX + gx;
		float cw = cosf(f->th), sw = sinf(f->th);
		st->gc[f->cell] += cw * f->life;
		st->gs[f->cell] += sw * f->life;
		st->gn[f->cell] += f->life;
		Cg += cw * f->life;
		Sg += sw * f->life;
		Ng += f->life;
	}
	for (int gy = 0; gy < GY; gy++)
		for (int gx = 0; gx < GX; gx++) {
			float C = 0, S = 0, N = 0;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++) {
					int xx = gx + dx, yy = gy + dy;
					if (xx < 0 || yy < 0 || xx >= GX || yy >= GY)
						continue;
					float k = (dx == 0 && dy == 0) ? 1.0f : 0.5f;
					C += st->gc[yy * GX + xx] * k;
					S += st->gs[yy * GX + xx] * k;
					N += st->gn[yy * GX + xx] * k;
				}
			st->lc[gy * GX + gx] = C / (N + 1.5f);
			st->ls[gy * GX + gx] = S / (N + 1.5f);
		}
	float Rg = Ng > 0 ? sqrtf(Cg * Cg + Sg * Sg) / Ng : 0;
	st->sync = us_approach(st->sync, Rg, 1.0f, rdt);
	if (Ng > 0) {
		Cg /= Ng + 4.0f;
		Sg /= Ng + 4.0f;
	}

	float K = 0.15f + 7.0f * E * E;                 /* local coupling */
	float Kg = 0.4f * E * E;                        /* weak global coupling */
	float spread = 0.17f - 0.08f * E;               /* natural-frequency spread */
	st->grad_t += dt * 0.021f;
	float grad = 0.07f * sinf(st->grad_t * US_TAU); /* slowly reversing wave direction */
	float gradv = 0.03f * sinf(st->grad_t * 4.1f + 1.0f);
	/* travelling-wave lag: synced fireflies flash in a front that sweeps across the field */
	float wang = st->grad_t * 2.3f + 0.6f * sinf(st->grad_t * 5.0f);
	float wdx = cosf(wang), wdy = 0.5f * sinf(wang);
	float wave_k = US_TAU * 1.05f * us_smooth(0.15f, 0.6f, E);
	float nudge = a->beat ? (0.25f + 0.9f * E) * us_sat(0.4f + a->onset) : 0.0f;

	float wind = us_sat(0.2f * w->wind + 0.6f * st->high_s + 0.2f * st->air_s);
	st->wind_t += dt * (0.12f + 0.6f * wind);
	float agit = 0.6f + 0.3f * st->high_s + 0.25f * E;

	/* ================= the backdrop: sky, moon glow, the wood, the pond's mirror, the near bank ============= */
	/* the moon rides a slow arc across the night, rising on the right */
	float moon_x = -1, moon_y = 0, moon_v = 0;
	{
		float u = fmodf(w->day_phase + 0.25f, 1.0f) / 0.5f; /* 0..1 across the night */
		if (u >= 0.0f && u <= 1.0f) {
			moon_x = A * (0.86f - 0.5f * u);
			moon_y = 0.36f - 0.24f * sinf(US_PI * u);
			moon_v = (1.0f - day) * us_smooth(0.0f, 0.06f, u) * us_smooth(1.0f, 0.94f, u);
		}
	}
	ensure_cache(st, c, day, moon_x, moon_y, moon_v);
	if (st->bd_ok)
		blit_backdrop(st, c);

	/* stars (only above the treeline, fade by day) */
	{
		float sv = (1.0f - us_smooth(0.15f, 0.55f, w->daylight));
		if (sv > 0.01f)
			for (int k = 0; k < NSTARS; k++) {
				float xf = st->star_x[k];
				if (st->star_y[k] > tree_at(st, xf) - 0.01f)
					continue;
				float twa = (0.3f + 0.45f * st->air_s) * (ctx->crisp ? 0.5f : 1.0f); /* air (cymbals, breath) makes the stars scintillate */
				float tw = 0.75f + twa * sinf(t * (0.7f + k % 5 * 0.3f) * (1.0f + 2.0f * st->air_s) + st->star_ph[k]);
				float sb = sv * st->star_b[k] * tw * (ctx->crisp ? 0.6f : 1.0f);
				if (ctx->crisp && st->star_b[k] < 0.4f)
					continue;
				us_point(c, xf * A, st->star_y[k], ctx->crisp ? 0.0025f : 0.0011f, sb,
					 US_MAT_ACCENT);
			}
	}

	/* the milky way: fills the high sky with a slow, clumpy river of faint stars */
	{
		float sv = 1.0f - us_smooth(0.12f, 0.5f, w->daylight);
		if (sv > 0.01f) {
			float rot = t * 0.0008f; /* the sky turns, imperceptibly */
			for (int k = 0; k < NMW; k++) {
				float xf = st->mw_x[k] + rot;
				xf -= floorf(xf);
				float y = st->mw_y[k] + 0.03f * sinf(xf * 3.0f);
				if (y > tree_at(st, xf) - 0.015f || y < 0.005f)
					continue;
				float b = 2.4f * st->mw_b[k] * sv * (0.65f + 0.7f * st->swell_s) * (0.85f + 0.15f * sinf(t * (0.5f + (k % 7) * 0.2f) + k));
				if (ctx->crisp) {
					if (st->mw_b[k] < 0.2f)
						continue;
					us_put(c, (int)(xf * A * c->s), (int)(y * c->s), b * 1.2f, US_MAT_ACCENT);
				} else {
					float fx = xf * A * c->s - 0.5f, fy = y * c->s - 0.5f;
					int ix = (int)floorf(fx), iy = (int)floorf(fy);
					float ax = fx - ix, ay = fy - iy;
					us_put(c, ix, iy, b * (1 - ax) * (1 - ay), US_MAT_ACCENT);
					us_put(c, ix + 1, iy, b * ax * (1 - ay), US_MAT_ACCENT);
					us_put(c, ix, iy + 1, b * (1 - ax) * ay, US_MAT_ACCENT);
					us_put(c, ix + 1, iy + 1, b * ax * ay, US_MAT_ACCENT);
				}
			}
		}
	}

	/* the moon: a pale gibbous disc with soft maria and a dim earthshine side (its glow lives in the backdrop) */
	if (moon_v > 0.02f && moon_y < tree_at(st, moon_x / A) - 0.03f)
		draw_moon(c, moon_x, moon_y, ctx->crisp ? 0.03f : 0.024f, moon_v);
	else
		moon_v *= 0.5f; /* behind the wood: only its glow and a faint path on the water */


	float night = 1.0f - us_smooth(0.15f, 0.55f, w->daylight);

	/* a shooting star now and then, when the air (crashes, breath) is open on a hit */
	{
		st->met_cd -= rdt;
		if (st->met_age < 0 && a->beat && st->air_s > 0.45f && st->met_cd <= 0 && night > 0.3f &&
		    us_rand(&st->rng) < 0.5f) {
			st->met_age = 0;
			st->met_x = us_randr(&st->rng, 0.15f, 0.85f) * A;
			st->met_y = us_randr(&st->rng, 0.04f, 0.2f);
			float ang = us_randr(&st->rng, 0.25f, 0.6f);
			float dir = us_rand(&st->rng) < 0.5f ? -1.0f : 1.0f;
			st->met_dx = dir * cosf(ang) * 0.55f;
			st->met_dy = sinf(ang) * 0.55f;
			st->met_cd = us_randr(&st->rng, 7.0f, 14.0f);
		}
		if (st->met_age >= 0) {
			st->met_age += rdt;
			const float dur = 0.65f;
			if (st->met_age > dur)
				st->met_age = -1;
			else {
				float u = st->met_age / dur;
				float hx = st->met_x + st->met_dx * st->met_age, hy = st->met_y + st->met_dy * st->met_age;
				float tl = 0.05f + 0.12f * us_smooth(0.0f, 0.4f, u);
				float tx = hx - st->met_dx * tl / 0.55f, ty = hy - st->met_dy * tl / 0.55f;
				float mv = night * 0.7f * us_smooth(0.0f, 0.1f, u) * (1.0f - u) * (1.0f - u);
				if (hy < tree_at(st, hx / A) - 0.01f) {
					if (ctx->crisp)
						us_line(c, tx, ty, hx, hy, 0.003f, mv, US_MAT_ACCENT);
					else {
						/* a fading streak: brighter toward the head */
						float mx = (tx + hx) * 0.5f, my = (ty + hy) * 0.5f;
						wline(c, tx, ty, mx, my, mv * 0.35f, US_MAT_ACCENT);
						wline(c, mx, my, hx, hy, mv, US_MAT_ACCENT);
						us_point(c, hx, hy, 0.0012f, mv, US_MAT_ACCENT);
					}
				}
			}
		}
	}

	/* the owl on a dead snag above the trees: turns its head to follow the melody's brightness, its eyes
	 * catch the light with the mids, and it blinks */
	{
		float xf = st->owl_xf, sx = xf * A;
		float top = tree_at(st, xf);
		for (int k = -3; k <= 3; k++) {
			float tk = tree_at(st, xf + k * 0.006f);
			if (tk < top)
				top = tk;
		}
		float yb = tree_at(st, xf) + 0.02f, yt = top - 0.075f;
		float kx = sx + 0.003f, ky = us_lerp(yb, yt, 0.45f);
		float py = yt + 0.022f;                       /* perch branch height */
		float bx0 = sx, bx1 = sx - 0.034f, by1 = py - 0.005f;
		float u = ctx->crisp ? 0.016f : 0.0125f;      /* owl scale */
		float ox = sx - 0.02f, oy = by1 + 0.0025f - 1.6f * u; /* owl centre, feet on the branch */
		/* behaviour */
		st->owl_look_t -= rdt;
		if (st->owl_look_t <= 0) {
			st->owl_look = us_randr(&st->rng, -1.0f, 1.0f);
			st->owl_look_t = us_randr(&st->rng, 2.5f, 7.0f);
		}
		float tgt = us_clampf(0.45f * st->owl_look + 2.6f * (st->cent_s - 0.45f) * a->active, -1.0f, 1.0f);
		st->owl_turn = us_approach(st->owl_turn, tgt, 0.35f, rdt);
		st->owl_blink_t -= rdt;
		if (st->owl_blink_t <= 0) {
			st->owl_blink = 1;
			st->owl_blink_t = us_randr(&st->rng, 3.0f, 8.0f);
		}
		st->owl_blink = us_approach(st->owl_blink, 0, 0.08f, rdt);
		float tn = st->owl_turn;
		/* silhouette: knock the sky out, then draw the edge */
		float cr = ctx->crisp ? 1.0f / c->s : 0.0016f;
		carve_line(c, sx, yb, kx, ky, cr);
		carve_line(c, kx, ky, sx - 0.001f, yt, cr);
		carve_line(c, bx0, py, bx1, by1, cr * 0.8f);
		carve_disc(c, ox, oy + 0.35f * u, 0.95f * u);
		carve_disc(c, ox + 0.1f * tn * u, oy - 0.65f * u, 0.8f * u);
		float sv = 0.15f + 0.1f * day;
		uint8_t sm = US_MAT_STEM;
		if (ctx->crisp) {
			/* pixel: a dim solid shape against the sky glow */
			us_line(c, sx, yb, kx, ky, 0.004f, 0.16f, sm);
			us_line(c, kx, ky, sx, yt, 0.004f, 0.16f, sm);
			us_line(c, bx0, py, bx1, by1, 0.004f, 0.14f, sm);
			us_disc(c, ox, oy + 0.35f * u, 0.95f * u, 0.2f, US_MAT_EARTH);
			us_disc(c, ox + 0.1f * tn * u, oy - 0.65f * u, 0.8f * u, 0.2f, US_MAT_EARTH);
			for (int k = -1; k <= 1; k += 2) /* ear tufts */
				us_put(c, (int)((ox + (0.6f * k + 0.3f * tn) * u) * c->s), (int)((oy - 1.5f * u) * c->s), 0.2f, US_MAT_EARTH);
		} else {
			wline(c, sx, yb, kx, ky, sv, sm);
			wline(c, kx, ky, sx - 0.001f, yt, sv, sm);
			wline(c, bx0, py, bx1, by1, sv, sm);
			wline(c, sx - 0.001f, yt + 0.035f, sx + 0.016f, yt + 0.022f, sv * 0.8f, sm);
			wline(c, sx + 0.016f, yt + 0.022f, sx + 0.022f, yt + 0.014f, sv * 0.6f, sm);
			/* owl outline in owl units: egg body, shoulders, ear tufts that swing with the head */
			static const float ol[17][2] = {{0, 1.6f}, {0.75f, 1.4f}, {1.0f, 0.8f}, {0.95f, 0.0f}, {0.8f, -0.6f},
							{0.75f, -1.0f}, {0.85f, -1.55f}, {0.45f, -1.25f}, {0, -1.2f}, {-0.45f, -1.25f},
							{-0.85f, -1.55f}, {-0.75f, -1.0f}, {-0.8f, -0.6f}, {-0.95f, 0.0f},
							{-1.0f, 0.8f}, {-0.75f, 1.4f}, {0, 1.6f}};
			float pts[34];
			for (int k = 0; k < 17; k++) {
				float hx = ol[k][0], hy = ol[k][1];
				if (hy < -0.7f) /* head points follow the turn */
					hx = hx * (1.0f - 0.25f * fabsf(tn)) + 0.3f * tn;
				pts[2 * k] = ox + hx * u;
				pts[2 * k + 1] = oy + hy * u;
			}
			us_polyline(c, pts, 17, 0.0011f, 0.24f + 0.1f * day, US_MAT_ACCENT);
		}
		/* eyes: two points that shift (and one hides) as the head turns */
		float ev = (0.1f + 0.75f * st->mid_s) * (1.0f - st->owl_blink) * (0.4f + 0.6f * night);
		if (ev > 0.02f)
			for (int k = -1; k <= 1; k += 2) {
				float ex = k * 0.38f * (1.0f - 0.3f * fabsf(tn)) + 0.42f * tn;
				if (fabsf(ex) > 0.62f)
					continue; /* turned away */
				if (ctx->crisp)
					us_put(c, (int)((ox + ex * u) * c->s), (int)((oy - 0.75f * u) * c->s), ev, US_MAT_LIGHT);
				else
					us_point(c, ox + ex * u, oy - 0.75f * u, 0.0011f, ev, US_MAT_LIGHT);
			}
	}

	/* ================= fireflies: simulate ================= */
	float gl_sum = 0;
	st->hist_acc += dt;
	int push_hist = 0;
	if (st->hist_acc >= 1.0f / 15.0f) {
		st->hist_acc -= 1.0f / 15.0f;
		if (st->hist_acc > 0.2f)
			st->hist_acc = 0;
		push_hist = 1;
	}
	memset(st->gbin, 0, sizeof(st->gbin));
	memset(st->lg, 0, sizeof(st->lg));
	/* at high density the long-exposure trails thin out so the swarm stays legible */
	float trail_k = 1.0f;
	{
		float pop = 0;
		for (int i = 0; i < MAXF; i++)
			pop += st->f[i].state != 0;
		if (pop > 120.0f)
			trail_k = sqrtf(120.0f / pop);
	}
	float clu_x[NCLU], clu_y[NCLU];
	for (int k = 0; k < NCLU; k++) {
		clu_x[k] = 0.5f + 0.5f * us_noise2(t * 0.011f + st->clu_ph[k], 1.7f + k);
		clu_y[k] = us_sat(0.5f + 0.8f * us_noise2(t * 0.013f + st->clu_ph[k], 9.3f + k));
	}
	float kdec = expf(-rdt / 0.08f), sdec = expf(-rdt / 0.11f), hdec = expf(-rdt / 0.05f);
	float aamp = (0.8f + 0.35f * LV) * (1.0f - 0.85f * st->drive);
	float dimv = (0.6f + 0.7f * LV) * (1.0f - 0.65f * day);
	for (int i = 0; i < MAXF; i++) {
		fly_t *f = &st->f[i];
		if (!f->state)
			continue;
		if (f->state == 1)
			f->life = us_sat(f->life + dt * 0.5f);
		else {
			f->life -= dt * 0.35f;
			if (f->life <= 0) {
				f->state = 0;
				continue;
			}
		}
		/* phase dynamics */
		float lc = st->lc[f->cell], ls = st->ls[f->cell];
		float cw = cosf(f->th), sw = sinf(f->th);
		float om = st->omega * (1.0f + spread * f->dev + grad * (f->x - 0.5f) + gradv * (f->y - 0.62f) * 4.0f);
		float dth = om + K * (ls * cw - lc * sw) + Kg * (Sg * cw - Cg * sw);
		f->th += dth * dt;
		if (nudge > 0) {
			float rl = sqrtf(lc * lc + ls * ls) + 1e-4f;
			f->th += nudge * (ls * cw - lc * sw) / rl * 0.35f;
		}
		if (f->th > US_PI)
			f->th -= US_TAU;
		else if (f->th < -US_PI)
			f->th += US_TAU;

		/* flight: steer along a slow noise flow, stay over the field */
		float ang = us_noise3(f->x * 2.2f, f->y * 3.0f, t * 0.05f + f->nz) * US_TAU;
		float spd = (0.012f + 0.02f * (0.4f + f->z)) * agit;
		float tvx = cosf(ang) * spd + 0.01f * wind * (0.5f + 0.5f * us_noise2(t * 0.1f, 3.0f));
		float tvy = sinf(ang) * spd * 0.6f;
		float ylo = us_lerp(FLY_TOP + 0.04f, 0.68f, f->z), yhi = us_lerp(0.63f, 0.895f, f->z);
		/* most belong to a loose gathering that drifts slowly over the meadow: the swarm clusters */
		if (f->clu >= 0) {
			/* each keeps its own place in the gathering, so they never stack up in pairs */
			float ox = f->nz * 7.31f, oy = f->nz * 3.17f;
			ox -= floorf(ox), oy -= floorf(oy);
			float cx = clu_x[f->clu] + 0.12f * (ox - 0.5f);
			float cy = us_lerp(ylo, yhi, us_clampf(clu_y[f->clu] + 0.6f * (oy - 0.5f), 0.05f, 0.95f));
			tvx += (cx - f->x) * A * 0.11f;
			tvy += (cy - f->y) * 0.2f;
		}
		if (f->y < ylo)
			tvy += (ylo - f->y) * 1.0f;
		if (f->y > yhi)
			tvy -= (f->y - yhi) * 1.0f;
		f->vx = us_approach(f->vx, tvx, 0.8f, dt);
		f->vy = us_approach(f->vy, tvy, 0.8f, dt);
		f->x += f->vx * dt / A;
		f->y += f->vy * dt + 0.004f * sinf(t * 1.3f + f->nz) * dt;
		int wrapped = 0;
		if (f->x < -0.02f)
			f->x += 1.04f, wrapped = 1;
		if (f->x > 1.02f)
			f->x -= 1.04f, wrapped = 1;

		/* flash: a bright pulse around th ~ 0 (lagged across the field so synced swarms flash in a
		 * travelling front), a hint of body glow otherwise */
		float lag = wave_k * ((f->x - 0.5f) * A * wdx + (f->y - 0.62f) * wdy);
		float ph = f->th - lag;
		/* this colony's band just rose: whoever was close to flashing flashes now */
		{
			float ck = st->col_kick[f->col];
			if (ck > 0) {
				float pw0 = ph;
				while (pw0 > US_PI)
					pw0 -= US_TAU;
				while (pw0 < -US_PI)
					pw0 += US_TAU;
				if (pw0 < 0 && pw0 > -1.7f) {
					float j = -pw0 * ck * 0.85f;
					f->th += j;
					ph += j;
					if (f->th > US_PI)
						f->th -= US_TAU;
				}
			}
		}
		float cth = cosf(ph);
		float b = cth > 0 ? cth * cth : 0;
		b = b * b * b; /* cos^6 */
		/* drum envelopes */
		if (f->kd >= 0) {
			f->kd -= rdt;
			if (f->kd < 0 && f->ka > f->ke)
				f->ke = f->ka;
		}
		f->ke *= kdec;
		f->se *= sdec;
		f->he *= hdec;
		float x = f->x * A, y = f->y;
		/* loudness decides who joins in; a steady kick makes the swarm hold its light for the beat */
		float part = us_sat((lit_frac - f->rank) * 10.0f);
		float ba = b * part * aamp;
		float far = 1.0f - us_smooth(0.15f, 0.4f, f->z);
		float bd = f->ke + 1.1f * f->se + 1.3f * f->he * far;
		float bs = ba + bd;
		if (bs > 1.5f)
			bs = 1.5f;
		float vis = f->life * dimv * (0.85f + 0.3f * st->col_env[f->col]);
		float size = 0.45f + 2.0f * f->z * f->z;
		float v = bs * vis;

		/* long-exposure flight trail: the path remembers how bright it was where it was flown (the beat's
		 * flashes leave only a faint mark, so the wave reads as a pulse, not a smear) */
		float vt = (ba + 0.3f * (f->ke + f->se)) * vis;
		if (wrapped)
			for (int k = 0; k < NHIST; k++)
				f->hx[k] = x, f->hy[k] = y, f->hb[k] = 0;
		if (push_hist) {
			f->hh = (f->hh + 1) % NHIST;
			f->hx[f->hh] = x;
			f->hy[f->hh] = y;
			f->hb[f->hh] = vt;
		} else if (vt > f->hb[f->hh])
			f->hb[f->hh] = vt;
		{
			float pw = ph;
			while (pw > US_PI)
				pw -= US_TAU;
			while (pw < -US_PI)
				pw += US_TAU;
			f->dv = v, f->dsz = size, f->dba = ba, f->dpw = pw, f->dvis = vis, f->dpart = part;
			f->dtwk = f->he * far, f->dbs = bs;
			f->dlant = (0.03f + 0.95f * part * LV * LV) * vis;
		}
		if (v > 0.012f) {
			gl_sum += v;
			{
				int gx = (int)(f->x * LGX), gy = (int)(y * LGY);
				if (gx >= 0 && gy >= 0 && gx < LGX && gy < LGY)
					st->lg[gy * LGX + gx] += v;
			}
			{
				int gb = (int)(f->x * NGB);
				if (gb >= 0 && gb < NGB)
					st->gbin[gb] += v * (0.3f + f->z);
			}
		}
		f->px = x;
		f->py = y;
	}
	st->glow = us_approach(st->glow, us_sat(gl_sum / 30.0f), 0.12f, rdt);
	/* light pooling on the bank: blur the bins a little and let them decay softly */
	for (int k = 0; k < NGB; k++) {
		float l = st->gbin[k > 0 ? k - 1 : 0], m = st->gbin[k], r = st->gbin[k < NGB - 1 ? k + 1 : NGB - 1];
		float g = us_sat((0.25f * l + 0.5f * m + 0.25f * r) * 0.12f);
		st->gbin_s[k] = g > st->gbin_s[k] ? g : us_approach(st->gbin_s[k], g, 0.09f, rdt);
	}


	/* ground mist: soft and drifting, pooled along the foot of the wood and over the water; sub thickens it */
	{
		/* (louder music: the air over the water fills with the swarm's warm haze) */
		float amp = (ctx->crisp ? 2.6f : 1.0f) * (0.015f + 0.015f * w->calm + 0.03f * st->glow + 0.04f * st->sub_s + 0.28f * LV) *
			    (1.0f - 0.5f * day);
		for (int j = 0; j < MFH; j++) {
			float y = MIST_Y0 + (MIST_Y1 - MIST_Y0) * (j + 0.5f) / MFH;
			float band = expf(-(y - 0.628f) * (y - 0.628f) / (0.045f * 0.045f));
			for (int i = 0; i < MFW; i++) {
				float xf = (i + 0.5f) / MFW;
				float n = us_fbm2(xf * 4.0f - t * 0.010f * (1.0f + 0.3f * band), y * 12.0f + t * 0.003f, 2);
				st->mist[j * MFW + i] = amp * band * us_smooth(0.2f, 0.8f, 0.5f + n);
			}
		}
		field_add(c, st->mist, MFW, MFH, MIST_Y0, MIST_Y1, ctx->crisp ? 0.04f : 1.0f / 255.0f, st->bd_ok ? st->bd_ink : NULL,
			  st->bd_ok ? st->bd_mat : NULL);
	}


	/* ================= the pond: still water, ripples, rising fish, the moon path ================= */
	{
	{
		/* still-water ripples: slow drifting dashes, a little livelier with highs */
		for (int k = 0; k < NRIP; k++) {
			st->rip_x[k] += st->rip_sp[k] * dt * (0.5f + wind);
			if (st->rip_x[k] < -0.2f)
				st->rip_x[k] += 1.4f;
			if (st->rip_x[k] > 1.2f)
				st->rip_x[k] -= 1.4f;
			if (ctx->crisp && k % 3)
				continue;
			float yy = st->rip_y[k] + 0.0015f * sinf(t * 0.7f + k);
			float x0 = st->rip_x[k] * A, len = st->rip_len[k] * (0.6f + 0.4f * sinf(t * 0.23f + k * 1.7f));
			float depth = (yy - W0) / (POOL_Y - W0);
			if (ctx->crisp)
				us_line(c, x0, yy, x0 + len, yy, 0.003f, (0.08f + 0.08f * depth) * (1.0f + st->glow), US_MAT_WATER);
			else
				hline(c, x0, x0 + len, yy, (0.035f + 0.045f * depth) * (1.0f + 1.5f * st->glow), US_MAT_WATER);
		}
		/* a fish rising (or a frog) on the kick: slow widening rings on the still water; now and then in
		 * silence too, so the pool keeps breathing */
		{
			int spawn = (a->beat && a->kick > 0.45f) ||
				    us_rand(&st->rng) < dt * 0.12f; /* ~ one every 8 s on its own */
			if (spawn)
				for (int k = 0; k < NPR; k++)
					if (st->pr_age[k] < 0) {
						st->pr_age[k] = 0;
						st->pr_x[k] = us_randr(&st->rng, 0.06f, 0.94f) * A;
						st->pr_y[k] = us_randr(&st->rng, W0 + 0.02f, POOL_Y - 0.03f);
						st->pr_amp[k] = 0.45f + 0.55f * (a->beat ? a->kick : 0.4f);
						break;
					}
			for (int k = 0; k < NPR; k++) {
				if (st->pr_age[k] < 0)
					continue;
				st->pr_age[k] += dt;
				const float dur = 2.4f;
				float u = st->pr_age[k] / dur;
				if (u >= 1.0f) {
					st->pr_age[k] = -1;
					continue;
				}
				float depth = (st->pr_y[k] - W0) / (POOL_Y - W0);
				float rx = (0.004f + 0.07f * sqrtf(u)) * (0.5f + 0.7f * depth);
				float rv = st->pr_amp[k] * (0.2f + 0.25f * LV + 0.3f * st->glow) * (1.0f - u) * (1.0f - u) * us_smooth(0.0f, 0.04f, u);
				float ry = rx * 0.14f;
				if (st->pr_y[k] + ry > shore_at(st->pr_x[k] / A) - 0.002f)
					continue;
				hellipse(c, st->pr_x[k], st->pr_y[k], rx, ry, rv, US_MAT_WATER);
				if (u > 0.1f && !ctx->crisp)
					hellipse(c, st->pr_x[k], st->pr_y[k], rx * 0.62f, ry * 0.62f, rv * 0.55f, US_MAT_WATER);
			}
		}
		/* moon glitter: sparks on the moon's path that come and go with the air band */
		if (moon_v > 0.02f && moon_x > 0 && st->air_s > 0.05f)
			for (int k = 0; k < 9; k++) {
				float slot = floorf(t * 6.0f + k * 0.37f);
				float hsh = sinf(slot * 12.9898f + k * 78.233f) * 43758.5453f;
				hsh -= floorf(hsh);
				float hs2 = sinf(slot * 39.346f + k * 11.135f) * 24634.6345f;
				hs2 -= floorf(hs2);
				float ph = t * 6.0f + k * 0.37f - slot; /* 0..1 within its slot */
				float gv = st->air_s * moon_v * 0.8f * sinf(ph * US_PI) * (hsh > 0.35f);
				float gy = W0 + 0.008f + hs2 * (POOL_Y - W0 - 0.02f);
				float gx = moon_x + (hsh - 0.5f) * (0.03f + 0.25f * (gy - W0));
				if (gv > 0.02f) {
					if (ctx->crisp)
						us_put(c, (int)(gx * c->s), (int)(gy * c->s), gv, US_MAT_WATER);
					else
						us_point(c, gx, gy, 0.0011f, gv, US_MAT_WATER);
				}
			}
		/* moon reflection */
		if (moon_v > 0.02f && moon_x > 0)
			for (int k = 0; k < 8; k++) {
				float yy = W0 + 0.008f + k * 0.019f;
				float hw = (0.008f + 0.004f * k) * (0.7f + 0.3f * sinf(t * 1.1f + k * 2.0f));
				float off = 0.004f * sinf(t * 0.8f + k * 1.3f) + (k & 1 ? 0.006f : -0.004f);
				us_line(c, moon_x - hw + off, yy, moon_x + hw + off, yy, ctx->crisp ? 0.003f : 0.0012f,
					(ctx->crisp ? 0.5f : 0.3f) * moon_v * (1.0f - k * 0.09f), US_MAT_WATER);
			}
	}
	}

	/* the far and middle swarm (and every reflection), in front of the wood, behind the grass */
	c->blend = US_BLEND_ADD;
	for (int i = 0; i < MAXF; i++)
		if (st->f[i].state)
			draw_refl(c, &st->f[i], t);
	for (int i = 0; i < MAXF; i++)
		if (st->f[i].state && st->f[i].z < 0.55f)
			draw_fly(c, &st->f[i], trail_k, aamp);
	c->blend = US_BLEND_MAX;


	/* cattails at the pool edge */
	for (int k = 0; k < NCAT; k++) {
		float x0 = st->cat_x[k] * A, h = st->cat_h[k];
		float sw = (us_noise2(st->cat_ph[k], st->wind_t * 0.5f) * 0.6f + 0.25f) * (0.02f + 0.1f * wind) * h;
		float tx = x0 + sw, ty = POOL_Y - h;
		us_line(c, x0, shore_at(st->cat_x[k]) + 0.004f, tx, ty, ctx->crisp ? 0.004f : 0.0016f, 0.32f + 0.25f * st->glow, US_MAT_STEM);
		float hx = x0 + sw * 0.82f, hy = POOL_Y - h * 0.82f;
		us_line(c, hx, hy, x0 + sw * 0.96f, POOL_Y - h * 0.96f, ctx->crisp ? 0.009f : 0.0055f,
			0.28f + 0.2f * st->glow, US_MAT_EARTH);
		/* a leaf blade curving off */
		float lp[8];
		for (int q = 0; q < 4; q++) {
			float u = q / 3.0f;
			lp[2 * q] = x0 + (0.02f * u * u + sw * 0.4f * u) * (k & 1 ? 1 : -1);
			lp[2 * q + 1] = POOL_Y - h * 0.7f * u;
		}
		us_polyline(c, lp, 4, ctx->crisp ? 0.003f : 0.0012f, 0.3f + 0.2f * st->glow, US_MAT_LEAF);
	}

	/* an orb-web strung off the first cattail: lit by nearby flashes, dew beads glinting, shivering on kicks */
	if (!ctx->crisp) {
		enum { NSP = 9, NRING = 6 };
		float wx = st->web_x * A, wy = st->web_y, wr = st->web_r;
		int gb = (int)(st->web_x * NGB);
		float lit = st->gbin_s[gb < 0 ? 0 : (gb >= NGB ? NGB - 1 : gb)];
		float sh = st->web_sh * (1.0f - winter);
		float wv = (0.06f + 0.45f * lit + 0.08f * sh) * (1.0f - 0.5f * day);
		float vx[NRING + 1][NSP], vy[NRING + 1][NSP];
		for (int j = 0; j <= NRING; j++) {
			float rr = j == 0 ? wr * 0.08f : wr * (0.22f + 0.78f * (float)(j - 1) / (NRING - 1));
			for (int k = 0; k < NSP; k++) {
				float an = k * (US_TAU / NSP) + 0.3f;
				float wob = 0.06f * sinf(k * 2.7f + j * 1.3f); /* hand-spun, not machine-perfect */
				float q = rr * (1.0f + wob);
				float d = sh * 0.0035f * sinf(t * 34.0f + k * 1.9f + j * 0.8f) * (0.3f + (float)j / NRING);
				vx[j][k] = wx + cosf(an) * q;
				vy[j][k] = wy + sinf(an) * q * 1.08f + d + 0.0015f * sinf(t * 0.6f) * (float)j / NRING;
			}
		}
		for (int k = 0; k < NSP; k++)
			wline(c, vx[0][k], vy[0][k], vx[NRING][k], vy[NRING][k], wv * 0.8f, US_MAT_ACCENT);
		for (int j = 1; j <= NRING; j++)
			for (int k = 0; k < NSP; k++) {
				int k1 = (k + 1) % NSP;
				wline(c, vx[j][k], vy[j][k], vx[j][k1], vy[j][k1], wv * (j == NRING ? 0.9f : 0.6f), US_MAT_ACCENT);
			}
		/* anchor threads: one to the cattail stem, one up and one down into the grass */
		float sx = st->cat_x[0] * A;
		int ks = st->web_x > st->cat_x[0] ? NSP / 2 : 0;
		wline(c, vx[NRING][ks], vy[NRING][ks], sx, wy - wr * 0.2f, wv * 0.7f, US_MAT_ACCENT);
		wline(c, vx[NRING][2], vy[NRING][2], wx + wr * 0.4f, POOL_Y - 0.004f, wv * 0.6f, US_MAT_ACCENT);
		wline(c, vx[NRING][6], vy[NRING][6], wx - wr * 0.3f, wy - wr * 2.4f, wv * 0.5f, US_MAT_ACCENT);
		for (int k = 0; k < NDEW; k++) {
			int j = 2 + (k * 3) % (NRING - 1), kk = (k * 4 + 1) % NSP;
			float dv = (0.1f + 1.2f * lit) * (0.6f + 0.4f * sinf(t * 0.9f + k * 2.3f)) * (1.0f - 0.5f * day);
			us_point(c, vx[j][kk], vy[j][kk], 0.0011f, dv, US_MAT_LIGHT);
		}
	}

	/* tall grass on the near bank: dark tapered blades back to front, their edges lit from above by whichever
	 * fireflies are flashing over this stretch of bank */
	{
		float keep = (0.45f + 0.55f * dens) * (ctx->crisp ? 0.8f : 1.0f);
		float gv = (ctx->crisp ? 0.2f : 0.12f) + 0.2f * st->glow + (ctx->crisp ? 0.45f : 0.25f) * LV;
		for (int b = 0; b < NBLADES; b++) {
			float hk = b * 0.618034f;
			if (hk - floorf(hk) >= keep)
				continue;
			float xf = st->blade_x[b], dd = st->blade_d[b];
			float x0 = xf * A, h = st->blade_h[b] * (1.0f - 0.35f * winter);
			float yb = shore_at(xf) + 0.004f + 0.16f * dd;
			float sw = (us_noise2(x0 * 1.6f - st->wind_t * 0.7f, st->blade_ph[b]) * 0.7f + 0.35f) * (0.1f + 0.5f * wind);
			float pts[10];
			for (int q = 0; q < 5; q++) {
				float u = q / 4.0f;
				pts[2 * q] = x0 + (st->blade_lean[b] + sw * 0.5f) * h * u * u;
				pts[2 * q + 1] = yb - h * u;
			}
			int gb = (int)(xf * NGB);
			float lit = st->gbin_s[gb < 0 ? 0 : (gb >= NGB ? NGB - 1 : gb)];
			float bvv = (gv + (ctx->crisp ? 1.1f : 0.7f) * lit) * (0.55f + 0.45f * dd);
			if (!ctx->crisp)
				blade_fill(c, pts, 5, 0.0011f + 0.0024f * dd, 0.012f, yb - 0.16f * dd - 0.004f);
			for (int q = 0; q < 4; q++)
				wline(c, pts[2 * q], pts[2 * q + 1], pts[2 * q + 2], pts[2 * q + 3], bvv * (1.0f - 0.15f * q),
				      ctx->crisp ? US_MAT_EARTH : US_MAT_LEAF);
		}
	}

	/* seed heads on tall stems: dandelion clocks and grass panicles, catching the swarm's light */
	{
		int ns = (int)(NSEED * (0.4f + 0.6f * dens) + 0.5f);
		for (int k = 0; k < ns; k++) {
			float xf = st->seed_x[k], dd = st->seed_d[k], x0 = xf * A, h = st->seed_h[k] * (1.0f - 0.35f * winter);
			float yb = shore_at(xf) + 0.01f + 0.2f * dd;
			float sw = (us_noise2(x0 * 1.6f - st->wind_t * 0.7f, st->seed_ph[k]) * 0.7f + 0.35f) * (0.1f + 0.5f * wind);
			float pts[12];
			for (int q = 0; q < 6; q++) {
				float u = q / 5.0f;
				pts[2 * q] = x0 + sw * 0.35f * h * u * u;
				pts[2 * q + 1] = yb - h * u;
			}
			int gb = (int)(xf * NGB);
			float lit = st->gbin_s[gb < 0 ? 0 : (gb >= NGB ? NGB - 1 : gb)];
			float sv = (ctx->crisp ? 0.4f : 0.3f) + 0.8f * lit + 0.2f * st->glow + 0.2f * LV;
			if (!ctx->crisp)
				blade_fill(c, pts, 6, 0.0012f, 0.012f, yb - 0.2f * dd - 0.01f);
			for (int q = 0; q < 5; q++)
				wline(c, pts[2 * q], pts[2 * q + 1], pts[2 * q + 2], pts[2 * q + 3], sv * 0.7f, US_MAT_STEM);
			float hx = pts[10], hy = pts[11];
			float dx = pts[10] - pts[8], dy = pts[11] - pts[9], dl = sqrtf(dx * dx + dy * dy) + 1e-6f;
			dx /= dl, dy /= dl;
			if (st->seed_k[k]) {
				/* a dandelion clock: fine rays, each tipped with a seed */
				float r = 0.019f * (0.8f + 0.4f * dd);
				int nr = ctx->crisp ? 10 : 26;
				for (int q = 0; q < nr; q++) {
					float an = q * (US_TAU / nr) + 0.3f * sinf(q * 2.7f + k);
					float ex = hx + cosf(an) * r, ey = hy + sinf(an) * r * 0.95f;
					if (ctx->crisp)
						us_put(c, (int)(ex * c->s), (int)(ey * c->s), sv, US_MAT_ACCENT);
					else {
						wline(c, hx, hy, ex, ey, sv * 0.35f, US_MAT_ACCENT);
						us_point(c, ex, ey, 0.0012f, sv * 0.9f, US_MAT_ACCENT);
					}
				}
				us_disc(c, hx, hy, ctx->crisp ? 0.004f : 0.0018f, sv, US_MAT_ACCENT);
			} else {
				/* a grass panicle: spikelets angled off a short nodding axis */
				const int NSPI = 9;
				float L = 0.05f * (0.8f + 0.4f * dd);
				for (int q = 0; q < NSPI; q++) {
					float u = (float)q / (NSPI - 1);
					float bx = hx + dx * L * u + sw * 0.01f * u * u, by = hy + dy * L * u;
					float sl = 0.008f * (1.0f - 0.6f * u);
					for (int sd = -1; sd <= 1; sd += 2) {
						float ca = cosf(0.5f * sd), sa = sinf(0.5f * sd);
						float ex = bx + (dx * ca - dy * sa) * sl, ey = by + (dx * sa + dy * ca) * sl;
						if (ctx->crisp)
							us_put(c, (int)(ex * c->s), (int)(ey * c->s), sv * 0.9f, US_MAT_ACCENT);
						else {
							wline(c, bx, by, ex, ey, sv * 0.8f, US_MAT_ACCENT);
							us_point(c, ex, ey, 0.0011f, sv * 0.8f, US_MAT_ACCENT);
						}
					}
				}
				if (ctx->crisp)
					us_line(c, hx, hy, hx + dx * L, hy + dy * L, 0.003f, sv, US_MAT_ACCENT);
				else
					wline(c, hx, hy, hx + dx * L, hy + dy * L, sv * 0.8f, US_MAT_ACCENT);
			}
		}
	}

	/* the near swarm: big soft fireflies in front of the grass, and the snare's sparks */
	c->blend = US_BLEND_ADD;
	for (int i = 0; i < MAXF; i++)
		if (st->f[i].state && st->f[i].z >= 0.55f)
			draw_fly(c, &st->f[i], trail_k, aamp);

	/* snare sparks: thrown up out of the bursting cluster, slowing and fading as they climb */
	for (int k = 0; k < NSPK; k++) {
		if (st->sp_life[k] <= 0)
			continue;
		st->sp_age[k] += rdt;
		float u = st->sp_age[k] / st->sp_life[k];
		if (u >= 1.0f) {
			st->sp_life[k] = 0;
			continue;
		}
		float drag = expf(-rdt * 2.5f);
		st->sp_vx[k] *= drag;
		st->sp_vy[k] = st->sp_vy[k] * drag - 0.02f * rdt;
		st->sp_x[k] += st->sp_vx[k] * rdt;
		st->sp_y[k] += st->sp_vy[k] * rdt;
		float sx = st->sp_x[k], sy = st->sp_y[k];
		float sv = (1.0f - u) * (0.75f + 0.25f * (1.0f - u)) * (1.0f - 0.4f * day);
		float tl = 0.09f; /* streak length in seconds of travel */
		float tx = sx - st->sp_vx[k] * tl, ty = sy - st->sp_vy[k] * tl;
		if (ctx->crisp) {
			us_put(c, (int)(sx * c->s), (int)(sy * c->s), sv, US_MAT_ACCENT);
			us_put(c, (int)(tx * c->s), (int)(ty * c->s), 0.5f * sv, US_MAT_ACCENT);
		} else {
			wline(c, tx, ty, sx, sy, 0.8f * sv, US_MAT_ACCENT);
			us_disc(c, sx, sy, 0.0016f, sv, US_MAT_ACCENT);
		}
	}
	c->blend = US_BLEND_MAX;

	/* moths: each chases a firefly's light for a few seconds, wings beating faster and flight more
	 * skittish with the hats; stereo width loosens their orbits */
	if (night > 0.05f)
		for (int m = 0; m < NMOTH; m++) {
			st->mo_tt[m] -= dt;
			int tg = st->mo_tgt[m];
			if (st->mo_tt[m] <= 0 || tg < 0 || st->f[tg].state != 1) {
				int k0 = (int)(us_rand(&st->rng) * MAXF);
				st->mo_tgt[m] = -1;
				for (int j = 0; j < MAXF; j++) {
					const fly_t *ff = &st->f[(k0 + j) % MAXF];
					if (ff->state == 1 && ff->life > 0.5f && ff->y > 0.45f) {
						st->mo_tgt[m] = (k0 + j) % MAXF;
						break;
					}
				}
				st->mo_tt[m] = us_randr(&st->rng, 2.5f, 6.0f);
				st->mo_orb[m] = us_randr(&st->rng, 0.015f, 0.04f);
				tg = st->mo_tgt[m];
			}
			float tx = tg >= 0 ? st->f[tg].x * A : 0.5f * A, ty = tg >= 0 ? st->f[tg].y : 0.62f;
			float ph2 = t * (1.4f + 0.37f * m) + m * 2.1f;
			float orb = st->mo_orb[m] * (0.7f + 0.9f * st->width_s);
			tx += cosf(ph2) * orb;
			ty += sinf(ph2 * 1.3f) * orb * 0.6f;
			float jit = 0.06f + 0.3f * st->high_s;
			float nx = us_noise2(t * 2.7f + m * 7.1f, 1.3f), ny = us_noise2(t * 2.9f + m * 3.7f, 8.1f);
			st->mo_vx[m] = us_approach(st->mo_vx[m], (tx - st->mo_x[m]) * 1.2f + jit * nx, 0.25f, dt);
			st->mo_vy[m] = us_approach(st->mo_vy[m], (ty - st->mo_y[m]) * 1.2f + jit * ny, 0.25f, dt);
			{ /* moths flutter, they don't dart: cap the cruising speed */
				float sp2 = st->mo_vx[m] * st->mo_vx[m] + st->mo_vy[m] * st->mo_vy[m], vmax = 0.09f + 0.08f * st->high_s;
				if (sp2 > vmax * vmax) {
					float k = vmax / sqrtf(sp2);
					st->mo_vx[m] *= k;
					st->mo_vy[m] *= k;
				}
			}
			st->mo_x[m] += st->mo_vx[m] * dt;
			st->mo_y[m] = us_clampf(st->mo_y[m] + st->mo_vy[m] * dt, 0.3f, POOL_Y - 0.01f);
			if (st->mo_x[m] < -0.05f || st->mo_x[m] > A + 0.05f)
				st->mo_x[m] = us_clampf(st->mo_x[m], -0.05f, A + 0.05f);
			st->mo_ph[m] += dt * (28.0f + 50.0f * st->high_s);
			float mx = st->mo_x[m], my = st->mo_y[m];
			int gb = (int)(mx / A * NGB);
			float lit = st->gbin_s[gb < 0 ? 0 : (gb >= NGB ? NGB - 1 : gb)];
			float mv = night * (0.11f + 0.6f * lit);
			float fl = sinf(st->mo_ph[m]);
			float sp = 0.0048f;
			if (ctx->crisp) {
				int ix = (int)(mx * c->s), iy = (int)(my * c->s);
				us_put(c, ix, iy, mv * 1.3f, US_MAT_ACCENT);
				if (fl > 0) {
					us_put(c, ix - 1, iy - 1, mv, US_MAT_ACCENT);
					us_put(c, ix + 1, iy - 1, mv, US_MAT_ACCENT);
				} else {
					us_put(c, ix - 1, iy, mv * 0.8f, US_MAT_ACCENT);
					us_put(c, ix + 1, iy, mv * 0.8f, US_MAT_ACCENT);
				}
			} else {
				/* a bow-tie of two soft wings whose width beats (seen from behind), a speck of body */
				float wd = sp * (0.2f + 0.8f * fabsf(fl));
				for (int sd = -1; sd <= 1; sd += 2) {
					float ax = mx + sd * wd, ay = my - sp * 0.45f, bx = mx + sd * wd * 0.75f, by = my + sp * 0.5f;
					wline(c, mx, my - sp * 0.1f, ax, ay, mv, US_MAT_ACCENT);
					wline(c, ax, ay, bx, by, mv * 0.8f, US_MAT_ACCENT);
					wline(c, bx, by, mx, my + sp * 0.15f, mv * 0.6f, US_MAT_ACCENT);
				}
				us_point(c, mx, my, 0.001f, mv, US_MAT_ACCENT);
			}
		}

	/* the near tree in front of everything, its bark lit by the swarm (and each kick's wave) */
	{
		float lt = 0;
		for (int gy = LGY / 3; gy < LGY; gy++)
			for (int gx = 0; gx < LGX / 4; gx++)
				lt += st->lg_s[gy * LGX + gx];
		float rim = (0.12f + 0.08f * night + 0.12f * LV + us_sat(lt * 0.08f) * 0.35f + 0.25f * st->kglow * night) * (1.0f - 0.3f * day);
		if (st->tm_cov)
			draw_neartree(st, c, ctx->crisp, rim, t, wind);
	}

	/* flash light warms the dark wood, water and bank around each flash (smoothed so it never pops) */
	for (int i = 0; i < LGX * LGY; i++) {
		float tgt = st->lg[i];
		st->lg_s[i] = tgt > st->lg_s[i] ? us_approach(st->lg_s[i], tgt, 0.05f, rdt) : us_approach(st->lg_s[i], tgt, 0.4f, rdt);
	}
}

const us_scene_vt us_scene_fireflies = {"fireflies", "Fireflies", 0.15f, create, destroy, frame};
