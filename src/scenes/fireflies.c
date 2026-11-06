/* Understory scene: Fireflies — a bioluminescent swarm at the edge of a dusk field.
 *
 * Every firefly is an agent with its own flash oscillator (phase th, natural frequency w_i).
 * The oscillators are coupled Kuramoto-style through a coarse spatial grid (each firefly feels the mean
 * phase of its neighbourhood) plus a weak global field. Coupling strength follows the music's energy:
 *   quiet  -> weak coupling, wide frequency spread: a few fireflies blink independently;
 *   dense  -> strong coupling: neighbourhoods lock, and a slow drifting frequency gradient across the field
 *             turns the locked swarm into waves of synchronised flashing that roll across the meadow.
 * Beats nudge every phase toward its local mean (and gently entrain the swarm's mean frequency to the tempo).
 * Each flash glows in additive halos with a faint ring that widens as it fades, and every firefly keeps a
 * ~1.2 s flight-path history drawn as a long-exposure trail lit wherever it flashed. Low fireflies are mirrored
 * as broken streaks in a still pool, and flash light pools on the grass below (so sync waves sweep the bank).
 * The swarm is six colonies by depth, each listening to its own slice of the spectrum (sub low in the grass ..
 * air up by the treeline): a rise in its band makes the members that were about to flash flash now, and the
 * band's level sets how bright the colony burns. Around them: moths chase the light (hats quicken their wings),
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

#define MAXF 520
#define TREE_N 320
#define NBLADES 170
#define NCAT 7
#define NSTARS 140
#define NRIP 22
#define GX 14
#define GY 7
#define FW 112
#define FH 64
#define NHIST 18 /* flight-path history samples (long-exposure trails) */
#define NGB 48    /* ground-light bins across the width */
#define NMW 460   /* milky-way stars */
#define LGX 64    /* coarse flash-light grid (halo tinting) */
#define LGY 36
#define NCOL 6    /* colonies: each listens to its own slice of the spectrum */
#define NMOTH 4
#define NPR 6     /* rising-fish rings on the pool (kicks) */
#define NDEW 7

#define POOL_Y 0.885f
#define FLY_TOP 0.36f
#define FLY_BOT 0.875f
#define FIELD_Y0 0.0f
#define FIELD_Y1 0.80f

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
} fly_t;

typedef struct {
	us_rng rng;
	int inited;
	fly_t f[MAXF];
	float tree[TREE_N]; /* treeline top y per x fraction */
	float blade_x[NBLADES], blade_h[NBLADES], blade_lean[NBLADES], blade_ph[NBLADES];
	float cat_x[NCAT], cat_h[NCAT], cat_ph[NCAT];
	float star_x[NSTARS], star_y[NSTARS], star_b[NSTARS], star_ph[NSTARS];
	float rip_x[NRIP], rip_y[NRIP], rip_len[NRIP], rip_sp[NRIP];
	float field[FW * FH];
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
	float lg[LGX * LGY], lg_s[LGX * LGY], lcov[LGX * LGY];
	float lwt[LGX * LGY];
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
} st_t;

/* colony band slices (bands are log-spaced 30 Hz..16 kHz). The lowest bands can hold no FFT bin at all and read
 * as a flat 1, so the two bass colonies listen to the sub / bass features instead and the rest start at ~175 Hz */
static const int col_band[NCOL + 1] = {0, 0, 9, 15, 21, 26, 32};

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x66697265u);
	return s;
}

static void destroy(void *p) { free(p); }

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

/* a fine horizontal hairline (cheap: two rows of coverage, soft ends); with `cov` (a coarse LGX x LGY light
 * grid) the line thins away where fireflies are flashing, so their glare washes the engraving out softly */
static void hline_cov(us_canvas *c, float x0, float x1, float y, float v, uint8_t m, const float *cov)
{
	float fy = y * c->s - 0.5f;
	int iy = (int)floorf(fy);
	float ay = fy - iy;
	int p0 = (int)(x0 * c->s), p1 = (int)(x1 * c->s);
	if (p0 < 0)
		p0 = 0;
	if (p1 > c->w)
		p1 = c->w;
	float gyf = us_clampf(y * LGY - 0.5f, 0, LGY - 1.001f);
	int gj = (int)gyf;
	float ty = gyf - gj;
	const float *r0 = cov + gj * LGX, *r1 = cov + (gj + 1) * LGX;
	float gsc = (float)LGX / c->w;
	for (int px = p0; px < p1; px++) {
		float e = us_sat((px - p0) * 0.1f) * us_sat((p1 - px) * 0.1f);
		float gxf = us_clampf((px + 0.5f) * gsc - 0.5f, 0, LGX - 1.001f);
		int gi = (int)gxf;
		float tx = gxf - gi;
		float k = us_lerp(us_lerp(r0[gi], r0[gi + 1], tx), us_lerp(r1[gi], r1[gi + 1], tx), ty);
		float vv = v * e * (1.0f - us_sat(k * 3.0f));
		if (vv <= 0.002f)
			continue;
		us_put(c, px, iy, vv * (1 - ay), m);
		us_put(c, px, iy + 1, vv * ay, m);
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

static float tree_at(const st_t *st, float xf)
{
	float u = us_clampf(xf, 0.0f, 1.0f) * (TREE_N - 1);
	int i = (int)u;
	if (i >= TREE_N - 1)
		return st->tree[TREE_N - 1];
	float t = u - i;
	return st->tree[i] * (1 - t) + st->tree[i + 1] * t;
}

static void build_treeline(st_t *st)
{
	us_rng *r = &st->rng;
	for (int i = 0; i < TREE_N; i++)
		st->tree[i] = 0.64f - 0.03f * us_fbm2(i * 0.012f, 3.7f, 3);
	/* conifers (triangles) and rounded crowns (domes) */
	int ntrees = 64;
	for (int k = 0; k < ntrees; k++) {
		float cx = us_rand(r);
		int conifer = us_rand(r) < 0.55f;
		float h = conifer ? us_randr(r, 0.08f, 0.2f) : us_randr(r, 0.06f, 0.15f);
		/* a gap in the middle-left makes the line breathe */
		if (cx > 0.3f && cx < 0.42f)
			h *= 0.45f;
		float wdt = conifer ? h * us_randr(r, 0.32f, 0.45f) : h * us_randr(r, 0.6f, 1.0f);
		for (int i = 0; i < TREE_N; i++) {
			float xf = (float)i / (TREE_N - 1);
			float d = fabsf(xf - cx) * 1.7778f; /* roughly isotropic at 16:9 */
			float base = 0.64f;
			float top;
			if (conifer) {
				/* jagged conifer: triangle with branch-tier notches */
				float edge = d / wdt;
				if (edge > 1.0f)
					continue;
				float tier = 0.06f * fabsf(sinf(edge * 9.0f + cx * 40.0f)) * (0.3f + edge);
				top = base - h * (1.0f - edge) + h * tier;
			} else {
				float e = d / wdt;
				if (e > 1.0f)
					continue;
				top = base - h * 0.55f - h * 0.45f * sqrtf(1.0f - e * e) +
				      0.012f * us_noise2(xf * 90.0f, cx * 13.0f);
			}
			if (top < st->tree[i])
				st->tree[i] = top;
		}
	}
}

static void spawn_fly(st_t *st, fly_t *f, float A, float life0)
{
	us_rng *r = &st->rng;
	memset(f, 0, sizeof(*f));
	f->z = us_rand(r);
	f->x = us_rand(r);
	/* far ones hug the treeline, near ones roam low over the field */
	float ylo = us_lerp(0.50f, 0.66f, f->z), yhi = us_lerp(0.70f, FLY_BOT, f->z);
	f->y = us_randr(r, ylo, yhi);
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
		us_rng *r = &st->rng;
		for (int b = 0; b < NBLADES; b++) {
			st->blade_x[b] = us_rand(r);
			float hh = us_rand(r);
			st->blade_h[b] = 0.03f + 0.15f * hh * hh;
			st->blade_lean[b] = us_randn(r) * 0.12f;
			st->blade_ph[b] = us_rand(r) * 50.0f;
		}
		for (int k = 0; k < NCAT; k++) {
			st->cat_x[k] = us_rand(r) < 0.5f ? us_randr(r, 0.02f, 0.25f) : us_randr(r, 0.72f, 0.98f);
			st->cat_h[k] = us_randr(r, 0.13f, 0.24f);
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
			st->rip_y[k] = us_randr(r, POOL_Y + 0.012f, 0.995f);
			st->rip_len[k] = us_randr(r, 0.03f, 0.16f);
			st->rip_sp[k] = us_randr(r, 0.004f, 0.012f) * (us_rand(r) < 0.5f ? -1 : 1);
		}
		/* population at open remembers the world's insects */
		float frac = (0.22f + 0.6f * w->insects) * dayf * (1.0f - 0.8f * winter);
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

	/* ---- world ---- */
	us_world_add(w, &w->insects, 0.004f * w->calm * (1.0f - day) * (1.0f - winter), dt);

	/* ---- population ---- */
	int alive = 0;
	for (int i = 0; i < MAXF; i++)
		if (st->f[i].state == 1)
			alive++;
	float frac = (0.2f + 0.35f * w->insects + 0.9f * E * E) * dayf * (1.0f - 0.85f * winter);
	int target = (int)(nmax * us_sat(frac));
	if (alive < target) {
		st->spawn_acc += dt * (6.0f + 40.0f * E);
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

	float wind = us_sat(0.2f * w->wind + 0.8f * a->high + 0.3f * a->air);
	st->wind_t += dt * (0.12f + 0.6f * wind);
	float agit = 0.6f + 0.8f * a->high + 0.5f * E;
	float lum = (0.72f + 0.28f * E) * (1.0f - 0.65f * day);

	/* ================= drawing: sky glow + mist ================= */
	{
		/* afterglow at dusk, a moonlit glow at night, a pale sky by day */
		float dusk = 0.16f + 0.26f * (1.0f - day) * us_smooth(0.02f, 0.3f, w->daylight) + 0.3f * day;
		if (ctx->crisp) {
			/* pixel styles: one low-res field (cheap at this size), sky masked by the treeline */
			float y0 = FIELD_Y0, y1 = FIELD_Y1;
			for (int j = 0; j < FH; j++) {
				float y = y0 + (y1 - y0) * (j + 0.5f) / FH;
				for (int i = 0; i < FW; i++) {
					float xf = (i + 0.5f) / FW;
					float top = tree_at(st, xf);
					float v = 0;
					if (y < top) {
						float h = 0.64f - y;
						/* pixel palettes quantise hard: lift the glow so it lands on real ramp steps */
						v = (0.5f + 0.7f * dusk) * (0.22f + 0.78f * expf(-h / 0.22f));
						v *= 0.8f + 0.2f * us_noise2(xf * 3.0f + t * 0.004f, y * 9.0f);
					} else {
						float band = (y > top + 0.01f) * us_smooth(0.57f, 0.645f, y) * (1.0f - us_smooth(0.68f, 0.79f, y));
						float n = us_fbm2(xf * 5.0f - t * 0.012f, y * 14.0f + t * 0.004f, 2);
						v = band * (0.05f + 0.25f * us_sat(0.5f + n)) * (0.5f + 0.8f * w->calm + 0.9f * st->sub_s);
					}
					st->field[j * FW + i] = v;
				}
			}
			us_field(c, st->field, FW, FH, 1.0f, US_MAT_BG, 0, y0, A, y1);
		} else {
			/* ink: engraved horizontal hatching, denser and brighter toward the horizon, clipped by the
			 * treeline and broken where slow clouds drift across */
			const int NH = 34;
			for (int k = 0; k < NH; k++) {
				float u = (k + 0.5f) / NH;
				float y = 0.64f - 0.56f * powf(u, 1.7f);
				float v = 2.3f * dusk * (0.07f + 0.93f * expf(-(0.64f - y) / 0.15f)) * (u < 0.28f ? 1.35f : 1.0f);
				if (v < 0.02f)
					continue;
				/* a rose afterglow low behind the trees, cool night blue overhead */
				uint8_t hm = u < 0.42f ? US_MAT_BLOOM : US_MAT_WATER;
				if (u >= 0.28f)
					v *= 0.8f;
				int run0 = -1;
				float cl = 0;
				for (int i = 0; i <= TREE_N; i++) {
					float xf = (float)i / (TREE_N - 1);
					int open = 0;
					if (i < TREE_N) {
						if (!(i & 3))
							cl = us_noise2(xf * 3.2f - t * 0.006f, y * 11.0f + k * 0.13f);
						open = st->tree[i] > y + 0.004f && cl > -0.28f;
					}
					if (open && run0 < 0)
						run0 = i;
					else if (!open && run0 >= 0) {
						float xa = (float)run0 / (TREE_N - 1) * A, xb = (float)(i - 1) / (TREE_N - 1) * A;
						if (xb > xa)
							hline_cov(c, xa, xb, y, v * 0.75f, hm, st->lcov);
						run0 = -1;
					}
				}
			}
			/* ground mist: drifting engraved lines whose brightness swells and thins along their length */
			enum { ML = 14, MS = 48 };
			for (int j = 0; j < ML; j++) {
				float u = (j + 0.5f) / ML;
				float y = 0.585f + 0.19f * u + 0.004f * sinf(t * 0.13f + j * 1.7f);
				float band = us_smooth(0.0f, 0.3f, u) * (1.0f - us_smooth(0.6f, 1.0f, u));
				float prof[MS];
				for (int i = 0; i < MS; i++) {
					float xf = (float)i / (MS - 1);
					float n = us_fbm2(xf * 5.0f - t * 0.012f * (1.0f + 0.2f * u), y * 14.0f + t * 0.004f, 2);
					float open = y > tree_at(st, xf) + 0.01f;
					/* the field's air, faintly lit by the swarm */
					prof[i] = open * band * us_smooth(0.25f, 0.75f, 0.5f + n) * (0.04f + 0.05f * w->calm + 0.14f * st->glow + 0.11f * st->sub_s);
				}
				float fy = y * c->s - 0.5f;
				int iy = (int)floorf(fy);
				float ay = fy - iy;
				for (int px = 0; px < c->w; px++) {
					float q = (float)px / c->w * (MS - 1);
					int qi = (int)q;
					if (qi >= MS - 1)
						qi = MS - 2;
					float v = us_lerp(prof[qi], prof[qi + 1], q - qi);
					if (v > 0.006f) {
						us_put(c, px, iy, v * (1 - ay), US_MAT_LIGHT);
						us_put(c, px, iy + 1, v * ay, US_MAT_LIGHT);
					}
				}
			}
		}
	}

	/* stars (only above the treeline, fade by day) */
	{
		float sv = (1.0f - us_smooth(0.15f, 0.55f, w->daylight));
		if (sv > 0.01f)
			for (int k = 0; k < NSTARS; k++) {
				float xf = st->star_x[k];
				if (st->star_y[k] > tree_at(st, xf) - 0.01f)
					continue;
				float twa = 0.3f + 0.45f * st->air_s; /* air (cymbals, breath) makes the stars scintillate */
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

	/* moon on a night arc */
	float moon_x = -1, moon_v = 0;
	{
		float u = fmodf(w->day_phase + 0.25f, 1.0f) / 0.5f; /* 0..1 across the night */
		if (u >= 0.0f && u <= 1.0f) {
			moon_x = A * (0.12f + 0.76f * u);
			float my = 0.42f - 0.32f * sinf(US_PI * u);
			moon_v = (1.0f - day) * us_smooth(0.0f, 0.08f, u) * us_smooth(1.0f, 0.92f, u);
			if (moon_v > 0.02f && my < tree_at(st, moon_x / A) - 0.03f) {
				float rr = 0.026f;
				float mw = ctx->crisp ? 0.007f : 0.0016f;
				us_arc(c, moon_x, my, rr, 1.25f, 5.0f, mw, 0.8f * moon_v, US_MAT_LIGHT);
				us_arc(c, moon_x - rr * 0.32f, my, rr * 0.8f, 1.35f, 4.93f, mw * 0.8f, 0.45f * moon_v, US_MAT_LIGHT);
			} else
				moon_v = 0;
		}
	}

	/* treeline contour (silhouette edge) */
	{
		float pts[2 * 161];
		for (int i = 0; i <= 160; i++) {
			float xf = i / 160.0f;
			pts[2 * i] = xf * A;
			pts[2 * i + 1] = tree_at(st, xf);
		}
		if (!ctx->crisp)
			us_polyline(c, pts, 161, 0.0011f, 0.16f + 0.1f * day, US_MAT_LEAF);
	}

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
	c->blend = US_BLEND_ADD;
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
		float ylo = us_lerp(FLY_TOP, 0.62f, f->z), yhi = us_lerp(0.66f, FLY_BOT, f->z);
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
		float x = f->x * A, y = f->y;
		float vis = f->life * lum * (0.7f + 0.55f * st->col_env[f->col]);
		float size = 0.65f + 0.75f * f->z;
		float v = b * vis;

		/* long-exposure flight trail: the path remembers how bright it was where it was flown */
		if (wrapped)
			for (int k = 0; k < NHIST; k++)
				f->hx[k] = x, f->hy[k] = y, f->hb[k] = 0;
		if (push_hist) {
			f->hh = (f->hh + 1) % NHIST;
			f->hx[f->hh] = x;
			f->hy[f->hh] = y;
			f->hb[f->hh] = v;
		} else if (v > f->hb[f->hh])
			f->hb[f->hh] = v;
		{
			float px = x, py = y, pb = v;
			int idx = f->hh;
			for (int k = 0; k < NHIST - 1; k++) {
				float hb = f->hb[idx];
				float tv = 0.36f * trail_k * (pb < hb ? pb : hb) * (1.0f - (float)k / NHIST);
				if (tv > 0.015f) {
					if (ctx->crisp)
						us_line(c, px, py, f->hx[idx], f->hy[idx], 0.002f, tv * 0.7f, US_MAT_LIGHT);
					else
						wline(c, px, py, f->hx[idx], f->hy[idx], tv * (0.6f + 0.4f * size), US_MAT_LIGHT);
				}
				px = f->hx[idx], py = f->hy[idx], pb = hb;
				idx = (idx + NHIST - 1) % NHIST;
			}
		}

		if (b > 0.01f) {
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
			/* the afterglow ring: a faint halo that widens as each flash fades */
			float pw = ph;
			while (pw > US_PI)
				pw -= US_TAU;
			while (pw < -US_PI)
				pw += US_TAU;
			float fade = us_sat(pw / 1.1f); /* 0 at the peak .. 1 as it dies */
			if (ctx->crisp) {
				us_disc(c, x, y, 0.0036f * size, v, US_MAT_LIGHT);
				if (v > 0.6f) /* a cross-shaped glint on the brightest flashes */
					for (int k = -1; k <= 1; k += 2) {
						us_put(c, (int)(x * c->s) + k, (int)(y * c->s), 0.45f * v, US_MAT_LIGHT);
						us_put(c, (int)(x * c->s), (int)(y * c->s) + k, 0.45f * v, US_MAT_LIGHT);
					}
			} else {
				/* a tight core and a small inner glow: the Ink bloom supplies the wide halo */
				us_disc(c, x, y, 0.0055f * size, 0.07f * v, US_MAT_LIGHT);
				us_disc(c, x, y, 0.003f * size, 0.38f * v, US_MAT_LIGHT);
				us_disc(c, x, y, 0.0015f * size, 1.1f * v, US_MAT_LIGHT);
				if (pw > 0 && b > 0.05f) {
					float rr = (0.008f + 0.026f * fade) * size;
					hring(c, x, y, rr, 0.22f * vis * (1.0f - fade) * (1.0f - fade));
					if (f->z > 0.5f)
						hring(c, x, y, rr * 1.7f, 0.09f * vis * (1.0f - fade) * (1.0f - fade));
				}
			}
			/* reflection in the pool: mirrored, compressed, broken into a shimmering streak */
			float hgt = POOL_Y - y;
			if (hgt > 0 && hgt < 0.32f) {
				float ry = POOL_Y + hgt * 0.42f + 0.004f;
				float rv = v * (ctx->crisp ? 0.35f : 0.6f) * (1.0f - hgt / 0.32f);
				float wob = 0.003f * sinf(t * 2.3f + ry * 140.0f) + 0.002f * us_noise2(x * 30.0f, t * 0.8f);
				float rl = (0.005f + 0.016f * b) * size;
				if (ctx->crisp)
					us_line(c, x + wob, ry - rl, x - wob, ry + rl, 0.004f * size, rv, US_MAT_WATER);
				else {
					wline(c, x + wob, ry - rl, x - wob, ry + rl, rv, US_MAT_WATER);
					us_disc(c, x, ry, 0.005f * size, 0.06f * rv, US_MAT_WATER);
				}
			}
		} else if (!ctx->crisp) {
			us_point(c, x, y, 0.0009f * size, 0.07f * vis, US_MAT_LIGHT);
		}
		f->px = x;
		f->py = y;
	}
	c->blend = US_BLEND_MAX;
	st->glow = us_approach(st->glow, us_sat(gl_sum / 30.0f), 0.12f, rdt);
	/* light pooling on the bank: blur the bins a little and let them decay softly */
	for (int k = 0; k < NGB; k++) {
		float l = st->gbin[k > 0 ? k - 1 : 0], m = st->gbin[k], r = st->gbin[k < NGB - 1 ? k + 1 : NGB - 1];
		float g = us_sat((0.25f * l + 0.5f * m + 0.25f * r) * 0.12f);
		st->gbin_s[k] = g > st->gbin_s[k] ? us_approach(st->gbin_s[k], g, 0.05f, rdt)
						  : us_approach(st->gbin_s[k], g, 0.35f, rdt);
	}

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

	/* ================= foreground: pool, cattails, grass ================= */
	{
		/* shoreline */
		float pts[2 * 97];
		for (int i = 0; i <= 96; i++) {
			float xf = i / 96.0f;
			pts[2 * i] = xf * A;
			pts[2 * i + 1] = POOL_Y + 0.003f * us_noise2(xf * 12.0f, 0.5f);
		}
		us_polyline(c, pts, 97, ctx->crisp ? 0.003f : 0.0012f, 0.32f + 0.25f * st->glow, US_MAT_WATER);
		/* still-water ripples: slow drifting dashes, a little livelier with highs */
		for (int k = 0; k < NRIP; k++) {
			st->rip_x[k] += st->rip_sp[k] * dt * (0.5f + wind);
			if (st->rip_x[k] < -0.2f)
				st->rip_x[k] += 1.4f;
			if (st->rip_x[k] > 1.2f)
				st->rip_x[k] -= 1.4f;
			if (ctx->crisp && (k & 1))
				continue;
			float yy = st->rip_y[k] + 0.0015f * sinf(t * 0.7f + k);
			float x0 = st->rip_x[k] * A, len = st->rip_len[k] * (0.6f + 0.4f * sinf(t * 0.23f + k * 1.7f));
			float depth = (yy - POOL_Y) / (1.0f - POOL_Y);
			if (ctx->crisp)
				us_line(c, x0, yy, x0 + len, yy, 0.003f, (0.10f + 0.12f * depth) * (1.0f + st->glow), US_MAT_WATER);
			else
				hline(c, x0, x0 + len, yy, (0.10f + 0.12f * depth) * (1.0f + st->glow), US_MAT_WATER);
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
						st->pr_y[k] = us_randr(&st->rng, POOL_Y + 0.025f, 0.975f);
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
				float depth = (st->pr_y[k] - POOL_Y) / (1.0f - POOL_Y);
				float rx = (0.004f + 0.075f * sqrtf(u)) * (0.6f + 0.6f * depth);
				float rv = st->pr_amp[k] * (0.32f + 0.3f * st->glow) * (1.0f - u) * (1.0f - u) * us_smooth(0.0f, 0.04f, u);
				float ry = rx * 0.14f;
				if (st->pr_y[k] + ry > 0.998f)
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
				float gy = POOL_Y + 0.012f + hs2 * 0.09f;
				float gx = moon_x + (hsh - 0.5f) * (0.03f + 0.25f * (gy - POOL_Y));
				if (gv > 0.02f) {
					if (ctx->crisp)
						us_put(c, (int)(gx * c->s), (int)(gy * c->s), gv, US_MAT_WATER);
					else
						us_point(c, gx, gy, 0.0011f, gv, US_MAT_WATER);
				}
			}
		/* moon reflection */
		if (moon_v > 0.02f && moon_x > 0)
			for (int k = 0; k < 6; k++) {
				float yy = POOL_Y + 0.015f + k * 0.017f;
				float hw = (0.012f + 0.006f * k) * (0.7f + 0.3f * sinf(t * 1.1f + k * 2.0f));
				float off = 0.004f * sinf(t * 0.8f + k * 1.3f);
				us_line(c, moon_x - hw + off, yy, moon_x + hw + off, yy, ctx->crisp ? 0.003f : 0.0012f,
					0.35f * moon_v * (1.0f - k * 0.12f), US_MAT_WATER);
			}
	}

	/* cattails at the pool edge */
	for (int k = 0; k < NCAT; k++) {
		float x0 = st->cat_x[k] * A, h = st->cat_h[k];
		float sw = (us_noise2(st->cat_ph[k], st->wind_t * 0.5f) * 0.6f + 0.25f) * (0.02f + 0.1f * wind) * h;
		float tx = x0 + sw, ty = POOL_Y - h;
		us_line(c, x0, POOL_Y + 0.004f, tx, ty, ctx->crisp ? 0.004f : 0.0016f, 0.32f + 0.25f * st->glow, US_MAT_STEM);
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

	/* grass silhouettes along the near bank */
	{
		int nb = (int)(NBLADES * (0.45f + 0.55f * dens) * (ctx->crisp ? 0.6f : 1.0f));
		float gv = (ctx->crisp ? 0.3f : 0.26f) + 0.2f * st->glow;
		for (int b = 0; b < nb; b++) {
			float xf = st->blade_x[b];
			float x0 = xf * A, h = st->blade_h[b] * (1.0f - 0.35f * winter);
			float sw = (us_noise2(x0 * 1.6f - st->wind_t * 0.7f, st->blade_ph[b]) * 0.7f + 0.35f) *
				   (0.1f + 0.5f * wind);
			float pts[10];
			for (int q = 0; q < 5; q++) {
				float u = q / 4.0f;
				pts[2 * q] = x0 + (st->blade_lean[b] + sw * 0.5f) * h * u * u;
				pts[2 * q + 1] = POOL_Y + 0.002f - h * u;
			}
			/* lit from above by whichever fireflies are flashing over this stretch of bank */
			int gb = (int)(xf * NGB);
			float lit = st->gbin_s[gb < 0 ? 0 : (gb >= NGB ? NGB - 1 : gb)];
			float bvv = (gv + 0.7f * lit) * (0.7f + 0.3f * h / 0.18f);
			for (int q = 0; q < 4; q++)
				wline(c, pts[2 * q], pts[2 * q + 1], pts[2 * q + 2], pts[2 * q + 3], bvv,
				      ctx->crisp ? US_MAT_EARTH : US_MAT_LEAF);
		}
	}

	/* flash light colours the faint mist, sky hatching and halo around it (smoothed so it never pops) */
	if (!ctx->crisp) {
		for (int i = 0; i < LGX * LGY; i++) {
			float tgt = st->lg[i];
			st->lg_s[i] = tgt > st->lg_s[i] ? us_approach(st->lg_s[i], tgt, 0.05f, rdt) : us_approach(st->lg_s[i], tgt, 0.4f, rdt);
		}
		for (int gy = 0; gy < LGY; gy++)
			for (int gx = 0; gx < LGX; gx++) {
				float sum = 0;
				for (int dy = -1; dy <= 1; dy++)
					for (int dx = -1; dx <= 1; dx++) {
						int xx = gx + dx, yy = gy + dy;
						if (xx >= 0 && yy >= 0 && xx < LGX && yy < LGY)
							sum += st->lg_s[yy * LGX + xx] * (dx || dy ? 0.5f : 1.0f);
					}
				st->lcov[gy * LGX + gx] = sum;
			}
		tint_weight(st->lg_s, st->lwt, LGX, LGY, 1, 3.0f);
		tint_soft(c, st->lwt, LGX, LGY, US_MAT_LIGHT, 20,
			   (1u << US_MAT_BG) | (1u << US_MAT_BLOOM) | (1u << US_MAT_WATER));
	}
}

const us_scene_vt us_scene_fireflies = {"fireflies", "Fireflies", 0.3f, create, destroy, frame};
