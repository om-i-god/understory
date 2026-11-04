/* Understory scene: Rain — rain over a wet meadow at eye level; snow in winter.
 *
 * Back to front: drifting rain clouds (soft in Ink, ordered-dither in Pixel), a far ridge, a treeline of
 * conifers and round-headed trees along the horizon, then the meadow running toward us in perspective,
 * dotted with puddles. Every drop has a depth: far drops are short faint hairlines that vanish just under
 * the horizon, near drops are long bright streaks that fall the whole height of the frame. Where a drop
 * lands in a puddle it opens a ripple ring (two crests, flattened by perspective, clipped by the puddle);
 * on open ground it throws up a little crown of droplets. Sheets of heavier rain sweep across with the
 * gusts. The puddles grow and merge as the world gets wetter and shrink as it dries.
 * Winter turns the drops into slow flakes that whiten the meadow, freeze the puddles, cap the trees and
 * build a drift along the foreground that buries the grass.
 *
 * Audio: level -> steady rain; onsets -> bursts; highs (+ world wind) -> wind slant + gusts;
 *        bass -> heavier, faster near drops. Silence keeps a light drizzle / a few flakes.
 * Density: 0 = a sparse drizzle even at the loudest, 1 = a downpour.
 * World: reads season (snow), wind, moisture (puddle size), daylight; writes moisture + water_level.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAX_DROPS 2000
#define MAX_SPL 420
#define MAX_RINGS 240
#define MAX_PUD 10
#define MAX_TREES 46
#define MAX_TUFT 220
#define MAX_BLADES 110
#define NDRIFT 160
#define CFW 96
#define CFH 26
#define HZ 0.47f      /* horizon */
#define CLOUD_Y HZ    /* clouds (and a mist band) fill the sky down to the horizon */
#define SKY_WMAX 4096
#define US_MAT_SNOW US_MAT_ACCENT /* cream-white at the bright end of most palettes */

typedef struct {
	float u, y, z;   /* u = x / aspect; z = nearness 0 far .. 1 at the bottom edge */
	float vy, land, ph;
	float wx, wy, wr; /* where a flake was drawn last frame (wiped so flakes do not smear into comets) */
	uint8_t snow;
} drop_t;

typedef struct {
	float u, y, vx, vy, life, z;
} spl_t;

typedef struct {
	float u, y, rmax, age, life, z, flat;
} ring_t;

typedef struct {
	float u, z, size, ph; /* a hollow in the ground where water collects */
} pud_t;

typedef struct {
	float u, h, lean;
	uint8_t kind; /* 0 conifer, 1 round-headed */
} tree_t;

typedef struct {
	us_rng rng;
	int inited;
	float so; /* seed offset for terrain noise */
	drop_t d[MAX_DROPS];
	int nd;
	spl_t sp[MAX_SPL];
	int nsp;
	ring_t rg[MAX_RINGS];
	int nrg;
	pud_t pd[MAX_PUD];
	tree_t tr[MAX_TREES];
	float tu[MAX_TUFT], tz[MAX_TUFT];
	float bx[MAX_BLADES], bh[MAX_BLADES], bp[MAX_BLADES];
	float drift[NDRIFT];
	float cloud[CFW * CFH];
	float spawn_acc, rain_amt, wind_s, gust_t, cloud_t, snowfrac, snow_cov, heavy, wet, freeze;
	/* per-frame puddle geometry */
	float px[MAX_PUD], py[MAX_PUD], prx[MAX_PUD], pry[MAX_PUD];
	int npud;
	uint16_t sky_x[SKY_WMAX], sky_t[SKY_WMAX]; /* sky_blit column cache */
	uint16_t ridge_row[SKY_WMAX];              /* pixel clouds stop at the ridge */
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x7261696eu);
	s->so = us_rand(&s->rng) * 50.0f;
	return s;
}

static void destroy(void *p) { free(p); }

/* ---- fast hairlines (see river.c): Wu-style 1px AA in ink, Bresenham when crisp ---- */
static void hl(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
	if (c->crisp) {
		us_line(c, ax, ay, bx, by, 0.001f, v, mat);
		return;
	}
	float x0 = ax * c->s - 0.5f, y0 = ay * c->s - 0.5f, x1 = bx * c->s - 0.5f, y1 = by * c->s - 0.5f;
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
	float g = dx != 0.0f ? (y1 - y0) / (x1 - x0) : 0.0f;
	float vv = v * sqrtf(1.0f + g * g) * 0.92f;
	int W = steep ? c->h : c->w, H = steep ? c->w : c->h;
	int i0 = (int)floorf(x0 + 0.5f), i1 = (int)floorf(x1 + 0.5f);
	if (i0 < 0) i0 = 0;
	if (i1 > W - 1) i1 = W - 1;
	if (i1 < i0) {
		if (x1 - x0 < 1.0f && i1 + 1 == i0)
			i1 = i0;
		else
			return;
	}
	for (int i = i0; i <= i1; i++) {
		float y = y0 + g * (i - x0);
		int j = (int)floorf(y);
		float f = y - j;
		if (j < -1 || j >= H)
			continue;
		float e = vv;
		if (i == i0 && i == i1)
			e *= us_clampf(x1 - x0, 0.3f, 1.0f);
		if (steep) {
			us_put(c, j, i, e * (1.0f - f), mat);
			us_put(c, j + 1, i, e * f, mat);
		} else {
			us_put(c, i, j, e * (1.0f - f), mat);
			us_put(c, i, j + 1, e * f, mat);
		}
	}
}

static void hpoly(us_canvas *c, const float *xy, int n, float v, uint8_t mat)
{
	for (int i = 0; i + 1 < n; i++)
		hl(c, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], v, mat);
}

/* ellipse outline; only the arc between a0..a1 (radians) when partial */
static void ellipse(us_canvas *c, float x, float y, float rx, float ry, float a0, float a1, float v, uint8_t mat)
{
	float pts[2 * 33];
	int n = rx * c->s < 5.0f ? 8 : (rx * c->s < 20.0f ? 16 : 32);
	n = (int)(n * (a1 - a0) / US_TAU + 0.99f);
	if (n < 3)
		n = 3;
	for (int i = 0; i <= n; i++) {
		float a = a0 + (a1 - a0) * i / n;
		pts[2 * i] = x + cosf(a) * rx;
		pts[2 * i + 1] = y + sinf(a) * ry;
	}
	hpoly(c, pts, n + 1, v, mat);
}

/* bilinear blit of the low-res cloud field over the full width, rows 0..y1 (normalized). Row-separable
 * with a per-column cache and 8.8 fixed point: several times cheaper than the generic us_field over a
 * region this large (see pond.c's field_blit). */
static void sky_blit(us_canvas *c, const float *f, int fw, int fh, float gain, uint8_t mat, float y1n, uint16_t *bx,
		     uint16_t *bt)
{
	int tmp[CFW + 1];
	int H = (int)(y1n * c->s);
	if (H > c->h)
		H = c->h;
	int W = c->w < SKY_WMAX ? c->w : SKY_WMAX;
	float kx = (float)fw / c->w, ky = (float)fh / (y1n * c->s);
	for (int i = 0; i < W; i++) {
		float fx = (i + 0.5f) * kx - 0.5f;
		if (fx < 0)
			fx = 0;
		int ix = (int)fx;
		float tx = fx - ix;
		if (ix >= fw - 1) {
			ix = fw - 1;
			tx = 0;
		}
		bx[i] = (uint16_t)ix;
		bt[i] = (uint16_t)(tx * 256.0f + 0.5f);
	}
	for (int j = 0; j < H; j++) {
		float fy = (j + 0.5f) * ky - 0.5f;
		int iy = (int)floorf(fy);
		float ty = fy - iy;
		int y0 = iy < 0 ? 0 : (iy >= fh ? fh - 1 : iy), yb = iy + 1 >= fh ? fh - 1 : (iy + 1 < 0 ? 0 : iy + 1);
		const float *r0 = f + y0 * fw, *r1 = f + yb * fw;
		int any = 0;
		for (int i = 0; i < fw; i++) {
			tmp[i] = (int)((r0[i] + (r1[i] - r0[i]) * ty) * gain * 255.0f * 256.0f);
			any |= tmp[i] > 0;
		}
		if (!any)
			continue;
		tmp[fw] = tmp[fw - 1];
		uint8_t *ink = c->ink + (size_t)j * c->w, *mt = c->mat + (size_t)j * c->w;
		for (int i = 0; i < W; i++) {
			int ix = bx[i], a0 = tmp[ix];
			int iv = (a0 + (((tmp[ix + 1] - a0) * bt[i]) >> 8)) >> 8;
			if (iv > 255)
				iv = 255;
			if (iv > ink[i]) {
				ink[i] = (uint8_t)iv;
				mt[i] = mat;
			}
		}
	}
}

/* ---- landscape ---- */
static float ridge_y(const st_t *st, float u) { return HZ - 0.06f - 0.05f * us_fbm2(u * 2.2f + st->so, 1.3f, 3); }
static float ground_y(float z) { return HZ + (1.0f - HZ) * z; }
static float drift_at(const st_t *st, float u)
{
	float f = us_clampf(u, 0, 1) * (NDRIFT - 1);
	int i = (int)f;
	if (i >= NDRIFT - 1)
		return st->drift[NDRIFT - 1];
	return us_lerp(st->drift[i], st->drift[i + 1], f - i);
}

/* 4x4 Bayer matrix for dithered pixel clouds */
static const uint8_t bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static void init_scene(st_t *st, const us_ctx *ctx)
{
	us_rng *r = &st->rng;
	float winter = us_world_winter(ctx->w);
	/* memory: open onto whatever snow the season has laid down, and puddles as wet as the world is */
	st->snow_cov = us_smooth(0.5f, 0.95f, winter) * 0.85f;
	for (int i = 0; i < NDRIFT; i++)
		st->drift[i] = st->snow_cov * 0.07f * (0.7f + 0.3f * us_noise2(i * 0.09f, 4.0f + st->so));
	st->snowfrac = us_smooth(0.35f, 0.65f, winter);
	st->freeze = us_smooth(0.6f, 0.9f, winter);
	st->rain_amt = us_sat(0.15f + ctx->w->moisture * 0.4f);
	st->wet = us_sat(ctx->w->moisture);
	/* treeline: groves of conifers and round-headed trees along the horizon */
	float grove[4] = {us_randr(r, 0.02f, 0.22f), us_randr(r, 0.3f, 0.48f), us_randr(r, 0.55f, 0.75f), us_randr(r, 0.8f, 0.98f)};
	for (int i = 0; i < MAX_TREES; i++) {
		tree_t *t = &st->tr[i];
		float gc = grove[i % 4];
		int lone = i % 5 == 4;
		t->u = lone ? us_rand(r) : us_clampf(gc + us_randn(r) * 0.05f, 0.0f, 1.0f);
		t->kind = us_rand(r) < 0.3f;
		t->h = us_randr(r, 0.035f, 0.1f) * (lone ? 0.7f : 1.0f) * (t->kind ? 0.8f : 1.0f);
		t->lean = us_randr(r, -0.04f, 0.04f);
	}
	/* puddles: spread across the meadow, more toward the front */
	float A = ctx->aspect;
	for (int i = 0; i < MAX_PUD; i++) {
		pud_t *p = &st->pd[i];
		p->size = us_randr(r, 0.6f, 1.15f);
		p->ph = us_rand(r) * 10.0f;
		/* keep hollows apart even at their wettest, so rims never cross */
		for (int tries = 0; tries < 40; tries++) {
			float gu = 0.23f + 0.618034f * (i + tries * 0.37f);
			p->u = 0.06f + 0.88f * (gu - floorf(gu));
			p->z = us_randr(r, 0.12f, 0.92f);
			float rx = (0.045f + 0.25f * p->z) * p->size * 1.3f, ry = rx * (0.08f + 0.26f * p->z);
			int ok = 1;
			for (int k = 0; k < i && ok; k++) {
				const pud_t *q = &st->pd[k];
				float qrx = (0.045f + 0.25f * q->z) * q->size * 1.3f, qry = qrx * (0.08f + 0.26f * q->z);
				float dx = fabsf(p->u - q->u) * A, dy = fabsf(ground_y(p->z) - ground_y(q->z));
				if (dx < (rx + qrx) * 1.05f && dy < (ry + qry) * 1.4f)
					ok = 0;
			}
			if (ok)
				break;
		}
	}
	for (int i = 0; i < MAX_TUFT; i++) {
		st->tu[i] = us_rand(r);
		st->tz[i] = us_rand(r);
	}
	for (int i = 0; i < MAX_BLADES; i++) {
		st->bx[i] = us_rand(r);
		st->bh[i] = us_randr(r, 0.25f, 1.0f);
		st->bp[i] = us_rand(r) * 20.0f;
	}
	st->inited = 1;
}

static void spawn_drop(st_t *st, float A, float slant, float sheet_t, int top)
{
	if (st->nd >= MAX_DROPS)
		return;
	us_rng *r = &st->rng;
	drop_t *d = &st->d[st->nd];
	d->snow = us_rand(r) < st->snowfrac;
	/* nearness: uniform in distance, biased a little toward the front */
	float dist = 1.0f + 22.0f * powf(us_rand(r), 1.6f);
	d->z = 1.0f / dist;
	d->ph = us_rand(r) * US_TAU;
	/* sheets of heavier rain sweep across with the gusts: thin out the gaps between them */
	float x = 0;
	for (int k = 0; k < 4; k++) {
		x = us_randr(r, -slant * (d->snow ? 0.4f : 1.0f) - 0.02f, A + 0.02f);
		float sh = 0.5f + 0.5f * us_noise2(x * 1.3f - sheet_t, st->so + 2.0f);
		if (us_rand(r) < 0.35f + 0.65f * sh)
			break;
	}
	d->u = x / A;
	d->wr = 0;
	d->land = ground_y(d->z);
	d->y = top ? us_randr(r, -0.12f, -0.01f) : us_randr(r, -0.05f, d->land);
	if (d->snow)
		d->vy = (0.04f + 0.11f * d->z) * us_randr(r, 0.8f, 1.2f);
	else
		d->vy = (0.55f + 1.6f * d->z) * us_randr(r, 0.9f, 1.1f) * (1.0f + 0.35f * st->heavy);
	st->nd++;
}

static void add_ring(st_t *st, float u, float y, float z, float rmax)
{
	if (st->nrg >= MAX_RINGS || rmax < 0.002f)
		return;
	ring_t *g = &st->rg[st->nrg++];
	g->u = u;
	g->y = y;
	g->z = z;
	g->rmax = rmax;
	g->flat = 0.1f + 0.3f * z;
	g->age = 0;
	g->life = 0.7f + 0.9f * z;
}

static void splash(st_t *st, float u, float y, float z, float vx, int n)
{
	us_rng *r = &st->rng;
	for (int k = 0; k < n && st->nsp < MAX_SPL; k++) {
		spl_t *s = &st->sp[st->nsp++];
		s->u = u;
		s->y = y;
		s->z = z;
		s->vx = us_randr(r, -0.16f, 0.16f) * z + vx * 0.08f;
		s->vy = -us_randr(r, 0.12f, 0.32f) * z;
		s->life = 1.0f;
	}
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *st = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, A = ctx->aspect, t = (float)ctx->t;
	float dens = ctx->p->density;
	float winter = us_world_winter(w), autumn = us_world_season_w(w, 2);
	us_rng *r = &st->rng;

	if (!st->inited)
		init_scene(st, ctx);

	/* ---- drivers ---- */
	st->snowfrac = us_approach(st->snowfrac, us_smooth(0.35f, 0.65f, winter), 4.0f, dt);
	st->freeze = us_approach(st->freeze, us_smooth(0.6f, 0.9f, winter), 8.0f, dt);
	float wind_t = us_sat(0.25f * w->wind + 0.8f * a->high + 0.35f * a->air + 0.15f * autumn);
	st->wind_s = us_approach(st->wind_s, wind_t, 1.2f, dt);
	st->gust_t += dt * (0.2f + 0.8f * st->wind_s);
	float gust = 0.5f + 0.5f * us_noise2(st->gust_t * 0.7f, st->so);
	float slant = 0.04f + 0.6f * st->wind_s * (0.55f + 0.8f * gust);
	st->heavy = us_approach(st->heavy, us_sat(0.7f * a->bass + 0.5f * a->kick), 0.3f, ctx->real_dt);
	float target = us_sat(0.06f + 0.85f * a->level + 0.25f * a->swell);
	st->rain_amt = us_approach(st->rain_amt, target, target > st->rain_amt ? 0.8f : 3.0f, dt);
	st->cloud_t += dt * (0.02f + 0.06f * st->wind_s);
	float liquid = 1.0f - st->snowfrac;
	/* puddles fill with the rain falling here and with how wet the world already is */
	float wet_t = us_sat(0.25f + 0.75f * w->moisture + 0.35f * st->rain_amt * liquid * (0.4f + dens));
	st->wet = us_approach(st->wet, wet_t, wet_t > st->wet ? 12.0f : 40.0f, dt);
	/* snow cover builds while it snows, melts out of winter (faster in rain) */
	st->snow_cov = us_clampf(st->snow_cov + dt * (0.03f * st->snowfrac * (0.2f + st->rain_amt) * (0.3f + dens) -
						      0.012f * (1.0f - winter) * (1.0f + 2.0f * st->rain_amt * liquid)),
				 0.0f, 1.0f);

	us_world_add(w, &w->moisture, 0.006f * st->rain_amt * (1.0f - 0.6f * st->snowfrac), dt);
	us_world_add(w, &w->water_level, 0.0025f * st->rain_amt * (1.0f - 0.8f * st->snowfrac), dt);

	/* ---- spawn: density sets the ceiling (0 = a drizzle even in a drop), level fills toward it ---- */
	float maxrate = 16.0f + 1150.0f * powf(dens, 1.5f);
	float rate = maxrate * (0.06f + 0.94f * st->rain_amt * st->rain_amt);
	rate *= 1.0f - 0.7f * st->snowfrac; /* flakes live much longer, fewer per second */
	float sheet_t = st->gust_t * 1.4f;
	st->spawn_acc += rate * dt;
	while (st->spawn_acc >= 1.0f) {
		spawn_drop(st, A, slant, sheet_t, 1);
		st->spawn_acc -= 1.0f;
	}
	if (a->beat) {
		int burst = (int)(maxrate * (0.03f + 0.1f * a->onset) * (1.0f - 0.6f * st->snowfrac));
		for (int i = 0; i < burst; i++)
			spawn_drop(st, A, slant, sheet_t, 1);
	}
	/* when the scene opens, start mid-shower instead of an empty sky */
	if (st->nd == 0)
		for (int i = 0; i < (int)(rate * (st->snowfrac > 0.5f ? 4.0f : 0.6f)); i++)
			spawn_drop(st, A, slant, sheet_t, 0);

	/* ---- flakes: wipe last frame's drawing (only snow-coloured pixels) ---- */
	for (int i = 0; i < st->nd; i++) {
		drop_t *d = &st->d[i];
		if (!d->snow || d->wr <= 0)
			continue;
		int x0 = (int)floorf((d->wx - d->wr) * c->s), x1 = (int)ceilf((d->wx + d->wr) * c->s);
		int y0 = (int)floorf((d->wy - d->wr) * c->s), y1 = (int)ceilf((d->wy + d->wr) * c->s);
		for (int py = y0 < 0 ? 0 : y0; py <= y1 && py < c->h; py++)
			for (int px = x0 < 0 ? 0 : x0; px <= x1 && px < c->w; px++) {
				size_t o = (size_t)py * c->w + px;
				if (c->mat[o] == US_MAT_SNOW)
					c->ink[o] = 0;
			}
	}

	/* ---- clouds: a slow low-res field over the sky ---- */
	{
		float cov = 0.3f + 0.7f * st->rain_amt;
		for (int j = 0; j < CFH; j++) {
			float y = (j + 0.5f) / CFH;
			float vf = us_smooth(0.95f, 0.35f, y) * us_smooth(0.0f, 0.1f, y);
			float mist = 0.3f * us_smooth(0.7f, 1.0f, y) * (0.4f + 0.6f * st->rain_amt);
			for (int i = 0; i < CFW; i++) {
				float u = (i + 0.5f) / CFW;
				float n = us_fbm3(u * 3.0f * A - st->cloud_t * 2.0f, y * 2.4f, st->cloud_t * 0.5f + st->so, 4);
				st->cloud[j * CFW + i] = us_smooth(-0.2f + 0.3f * (1 - cov), 0.6f, n) * vf + mist * (0.8f + 0.2f * n);
			}
		}
		float gain = 0.2f + 0.08f * w->daylight;
		if (ctx->crisp) {
			/* ordered dither: soft cloud edges without blobby 4-step bands; stops at the ridge */
			int yh = (int)(CLOUD_Y * c->s);
			int W = c->w < SKY_WMAX ? c->w : SKY_WMAX;
			for (int px = 0; px < W; px++)
				st->ridge_row[px] = (uint16_t)(ridge_y(st, (px + 0.5f) / c->w) * c->s);
			for (int py = 0; py < yh && py < c->h; py++) {
				float fy = (py + 0.5f) / (CLOUD_Y * c->s) * CFH - 0.5f;
				int j = (int)us_clampf(fy, 0, CFH - 2);
				float ty = us_clampf(fy - j, 0, 1);
				for (int px = 0; px < W; px++) {
					if (py >= st->ridge_row[px])
						continue;
					float fx = (px + 0.5f) / c->w * CFW - 0.5f;
					int i = (int)us_clampf(fx, 0, CFW - 2);
					float tx = us_clampf(fx - i, 0, 1);
					const float *q = st->cloud + j * CFW + i;
					float v = us_lerp(us_lerp(q[0], q[1], tx), us_lerp(q[CFW], q[CFW + 1], tx), ty);
					float th = (bayer4[(py & 3) * 4 + (px & 3)] + 0.5f) / 16.0f;
					float lv = v * 4.0f; /* 0..~3 shade levels */
					int base = (int)lv;
					if (lv - base > th)
						base++;
					if (base > 0)
						us_put(c, px, py, 0.14f + 0.18f * (base > 3 ? 3 : base), US_MAT_BG);
				}
			}
		} else {
			sky_blit(c, st->cloud, CFW, CFH, gain, US_MAT_LIGHT, CLOUD_Y, st->sky_x, st->sky_t);
		}
	}

	float snowy = us_smooth(0.15f, 0.6f, st->snow_cov);

	/* ---- far ridge ---- */
	{
		const int NT = 64;
		float pts[2 * 65];
		for (int i = 0; i <= NT; i++) {
			float u = (float)i / NT;
			pts[2 * i] = u * A;
			pts[2 * i + 1] = ridge_y(st, u);
		}
		uint8_t rm = snowy > 0.5f ? US_MAT_SNOW : US_MAT_STEM;
		if (ctx->crisp)
			for (int i = 0; i < NT; i++) {
				float x0 = pts[2 * i], x1 = pts[2 * i + 2];
				us_tri(c, x0, pts[2 * i + 1], x1, pts[2 * i + 3], x0, HZ, 0.2f, rm);
				us_tri(c, x1, pts[2 * i + 3], x1, HZ, x0, HZ, 0.2f, rm);
			}
		hpoly(c, pts, NT + 1, 0.2f + 0.25f * snowy, snowy > 0.5f ? US_MAT_SNOW : US_MAT_EARTH);
	}

	/* ---- the meadow: ground fill (crisp), perspective texture ---- */
	/* in snow the tufts are dry stalks poking through */
	uint8_t gm = snowy > 0.5f ? US_MAT_STEM : US_MAT_LEAF;
	if (snowy > 0.05f && !ctx->crisp) {
		/* snowfield contours: wind-shaped ridges receding in perspective */
		for (int k = 0; k < 14; k++) {
			float z = powf((k + 0.6f) / 14.0f, 1.7f);
			float pts[2 * 49];
			for (int i = 0; i <= 48; i++) {
				float u = i / 48.0f;
				pts[2 * i] = u * A;
				pts[2 * i + 1] = ground_y(z) + 0.012f * z * us_noise2(u * (3.0f + 4.0f * (1.0f - z)) + k * 3.1f, st->so);
			}
			hpoly(c, pts, 49, (0.06f + 0.2f * snowy) * (0.45f + 0.55f * z), US_MAT_SNOW);
		}
	}
	if (ctx->crisp) {
		int y0 = (int)(HZ * c->s);
		for (int py = y0; py < c->h; py++) {
			float z = ((py + 0.5f) / c->s - HZ) / (1.0f - HZ);
			for (int px = 0; px < c->w; px++) {
				if (snowy > 0.5f) {
					/* snowfield: dithered from a bright distance down to a shaded foreground,
					 * dark enough that the falling flakes still read against it */
					float th = (bayer4[(py & 3) * 4 + (px & 3)] + 0.5f) / 16.0f;
					float g = us_sat(1.0f - z * 1.6f) * (0.6f + 0.4f * snowy);
					us_put(c, px, py, g > th ? 0.36f : 0.2f, US_MAT_SNOW);
				} else {
					us_put(c, px, py, 0.12f + 0.04f * z, US_MAT_EARTH);
				}
			}
		}
	}
	{
		int nt = (int)(MAX_TUFT * (0.35f + 0.65f * dens));
		float green = 1.0f - 0.6f * snowy;
		for (int i = 0; i < nt; i++) {
			float z = 0.02f + 0.98f * st->tz[i] * st->tz[i];
			float x = st->tu[i] * A, y = ground_y(z);
			float h = 0.003f + 0.03f * z * z * green;
			float sw = (0.2f + st->wind_s) * h * 0.7f * (0.6f + 0.4f * us_noise2(x * 3.0f - st->gust_t, i * 0.3f));
			float v = 0.16f + 0.2f * z;
			hl(c, x, y, x + sw, y - h, v, gm);
			if (z > 0.3f) {
				hl(c, x + h * 0.25f, y, x + h * 0.25f + sw * 1.2f, y - h * 0.8f, v * 0.85f, gm);
				hl(c, x - h * 0.25f, y, x - h * 0.25f + sw * 0.8f, y - h * 0.7f, v * 0.85f, gm);
			}
		}
	}

	/* ---- treeline along the horizon (snow-laden in winter) ---- */
	{
		int ntr = 16 + (int)(dens * (MAX_TREES - 16));
		for (int i = 0; i < ntr; i++) {
			tree_t *tr = &st->tr[i];
			float x = tr->u * A, base = HZ + 0.002f;
			float sway = 0.005f * st->wind_s * us_noise2(st->gust_t * 1.5f, i * 1.7f);
			float tx = x + tr->lean * tr->h + sway, ty = base - tr->h;
			if (tr->kind == 0) {
				hl(c, x, base, tx, ty, 0.38f, US_MAT_STEM);
				int tiers = 4 + (int)(tr->h * 55.0f);
				for (int k = 0; k < tiers; k++) {
					float f = (k + 1.0f) / (tiers + 1.0f);
					float px = us_lerp(tx, x, f), py = us_lerp(ty, base, f);
					float bw = tr->h * 0.34f * f;
					float droop = tr->h * 0.16f * (1.0f + 0.5f * snowy);
					if (ctx->crisp)
						us_tri(c, px, py - droop * 0.5f, px - bw, py + droop * 0.5f, px + bw, py + droop * 0.5f, 0.32f, US_MAT_LEAF);
					else {
						hl(c, px, py - droop * 0.3f, px - bw, py + droop * 0.5f, 0.36f, US_MAT_LEAF);
						hl(c, px, py - droop * 0.3f, px + bw, py + droop * 0.5f, 0.36f, US_MAT_LEAF);
					}
					if (snowy > 0.1f)
						hl(c, px - bw * 0.7f, py + droop * 0.15f, px + bw * 0.7f, py + droop * 0.15f, 0.6f * snowy, US_MAT_SNOW);
				}
			} else {
				/* round-headed tree: trunk + a lumpy crown */
				float cr = tr->h * 0.42f;
				float cx = tx, cy = ty + cr;
				hl(c, x, base, cx, cy, 0.4f, US_MAT_STEM);
				if (ctx->crisp) {
					us_disc(c, cx, cy, cr, 0.3f, US_MAT_LEAF);
					us_disc(c, cx - cr * 0.3f, cy - cr * 0.3f, cr * 0.5f, 0.42f, US_MAT_LEAF);
				} else {
					float pts[2 * 17];
					for (int k = 0; k <= 16; k++) {
						float aa = US_TAU * k / 16;
						float rr = cr * (1.0f + 0.12f * sinf(aa * 5.0f + i));
						pts[2 * k] = cx + cosf(aa) * rr;
						pts[2 * k + 1] = cy + sinf(aa) * rr * 0.85f;
					}
					hpoly(c, pts, 17, 0.36f, US_MAT_LEAF);
				}
				if (snowy > 0.1f)
					ellipse(c, cx, cy, cr * 0.9f, cr * 0.75f, US_PI * 1.15f, US_PI * 1.85f, 0.65f * snowy, US_MAT_SNOW);
			}
		}
	}

	/* ---- puddles: hollows that fill with the wet; frozen in deep winter ---- */
	{
		int np = 3 + (int)(dens * (MAX_PUD - 3) + 0.5f);
		st->npud = np;
		float grow = 0.25f + 1.0f * st->wet;
		for (int i = 0; i < np; i++) {
			pud_t *p = &st->pd[i];
			float x = p->u * A, y = ground_y(p->z);
			float rx = (0.045f + 0.25f * p->z) * p->size * grow;
			float ry = rx * (0.08f + 0.26f * p->z);
			st->px[i] = x;
			st->py[i] = y;
			st->prx[i] = rx;
			st->pry[i] = ry;
			if (ctx->crisp) {
				/* filled water, rim of mud */
				int y0 = (int)floorf((y - ry) * c->s), y1 = (int)ceilf((y + ry) * c->s);
				for (int py = y0; py <= y1; py++) {
					float fy = ((py + 0.5f) / c->s - y) / ry;
					if (fy * fy >= 1.0f)
						continue;
					float hw = rx * sqrtf(1.0f - fy * fy);
					int x0 = (int)floorf((x - hw) * c->s + 0.5f), x1 = (int)floorf((x + hw) * c->s - 0.5f);
					for (int px = x0; px <= x1; px++)
						if ((unsigned)px < (unsigned)c->w && (unsigned)py < (unsigned)c->h) {
							size_t o = (size_t)py * c->w + px;
							c->ink[o] = (uint8_t)(255.0f * (st->freeze > 0.5f ? 0.55f : 0.2f));
							c->mat[o] = US_MAT_WATER;
						}
				}
			} else {
				/* far rim only (the near rim is hidden by the lip of the hollow) + the near lip */
				ellipse(c, x, y, rx, ry, US_PI, US_TAU, 0.3f + 0.15f * st->freeze, US_MAT_WATER);
				ellipse(c, x, y, rx, ry, 0.15f, US_PI - 0.15f, 0.18f, US_MAT_EARTH);
			}
			/* sky reflected in the water: a few trembling horizontal strokes */
			int nref = 2 + (int)(p->z * 7.0f);
			float trem = (0.4f + st->rain_amt * liquid) * (1.0f - st->freeze);
			for (int k = 0; k < nref; k++) {
				float h1 = sinf(k * 12.9898f + i * 78.233f) * 43758.5f;
				h1 -= floorf(h1);
				float fy = -0.75f + 1.5f * (k + 0.5f) / nref;
				float hw = sqrtf(us_sat(1.0f - fy * fy)) * rx;
				float len = hw * (0.25f + 0.5f * h1) * (0.7f + 0.3f * sinf(t * (1.0f + 2.0f * trem) + h1 * 9.0f + p->ph));
				float off = (h1 - 0.5f) * (hw - len) * 1.6f + 0.004f * trem * us_noise2(t * 3.0f, k + i * 5.0f);
				float yy = y + fy * ry;
				float v = (0.16f + 0.2f * (1.0f - fabsf(fy))) * (1.0f + 0.8f * st->freeze);
				hl(c, x + off - len, yy, x + off + len, yy, v, US_MAT_WATER);
			}
			/* ice: a couple of crack lines */
			if (st->freeze > 0.3f && rx * c->s > 6.0f) {
				for (int k = 0; k < 3; k++) {
					float h1 = sinf(k * 7.13f + i * 3.7f) * 43758.5f;
					h1 -= floorf(h1);
					float a0 = h1 * US_TAU;
					float cx0 = x + cosf(a0) * rx * 0.3f, cy0 = y + sinf(a0) * ry * 0.3f;
					float cx1 = x + cosf(a0 + 2.5f) * rx * 0.85f, cy1 = y + sinf(a0 + 2.5f) * ry * 0.85f;
					hl(c, cx0, cy0, cx1, cy1, 0.8f * st->freeze, US_MAT_WATER);
				}
			}
		}
	}

	/* ---- ripple rings in the puddles (perspective ellipses, clipped to their puddle) ---- */
	for (int i = 0; i < st->nrg;) {
		ring_t *g = &st->rg[i];
		g->age += dt;
		float f = g->age / g->life;
		if (f >= 1.0f) {
			*g = st->rg[--st->nrg];
			continue;
		}
		float e = 1.0f - (1.0f - f) * (1.0f - f);
		float rr = g->rmax * e;
		float v = (1.0f - f) * (0.5f + 0.5f * g->z);
		float x = g->u * A;
		uint8_t m = US_MAT_WATER;
		if (ctx->crisp && rr * c->s < 1.0f)
			us_point(c, x, g->y, 0.001f, v, m);
		else
			ellipse(c, x, g->y, rr, rr * g->flat, 0, US_TAU, v, m);
		if (g->z > 0.35f && rr > 0.006f)
			ellipse(c, x, g->y, rr * 0.62f, rr * 0.62f * g->flat, 0, US_TAU, v * 0.55f, m);
		/* first instant: the drop's crown, then a little jet */
		if (g->age < 0.09f && g->z > 0.3f) {
			float hh = 0.012f * g->z * (1.0f - g->age / 0.09f);
			hl(c, x - hh * 0.4f, g->y, x - hh * 0.6f, g->y - hh, 0.75f, m);
			hl(c, x + hh * 0.4f, g->y, x + hh * 0.6f, g->y - hh, 0.75f, m);
		} else if (g->age < 0.25f && g->z > 0.6f) {
			float jh = 0.014f * g->z * sinf(US_PI * (g->age - 0.09f) / 0.16f);
			us_point(c, x, g->y - jh, 0.0012f + 0.001f * g->z, 0.8f, m);
		}
		i++;
	}

	/* ---- rain + snow ---- */
	for (int i = 0; i < st->nd;) {
		drop_t *d = &st->d[i];
		float x = d->u * A;
		float vx;
		if (d->snow) {
			float flutter = sinf(t * (1.2f + d->z) + d->ph) * (0.015f + 0.03f * d->z);
			vx = slant * 0.3f * (0.3f + 0.7f * d->z) + flutter + 0.03f * us_noise2(x * 2.0f, t * 0.3f + d->ph);
		} else {
			vx = slant * d->vy;
		}
		x += vx * dt;
		d->y += d->vy * dt;
		d->u = x / A;
		if (d->y >= d->land) {
			float nu = d->u;
			if (nu >= -0.02f && nu <= 1.02f) {
				int hit = -1;
				for (int k = 0; k < st->npud; k++) {
					float qx = (x - st->px[k]) / st->prx[k], qy = (d->land - st->py[k]) / st->pry[k];
					if (qx * qx + qy * qy < 1.0f) {
						hit = k;
						break;
					}
				}
				if (d->snow) {
					if (d->z > 0.55f) {
						int k = (int)(us_clampf(nu, 0, 1) * (NDRIFT - 1) + 0.5f);
						st->drift[k] = us_clampf(st->drift[k] + 0.0016f * d->z * (0.5f + st->snow_cov), 0, 0.1f);
					}
				} else if (hit >= 0 && st->freeze < 0.5f) {
					/* the ring may not run past the puddle's edge */
					float qx = (x - st->px[hit]) / st->prx[hit], qy = (d->land - st->py[hit]) / st->pry[hit];
					float room = (1.0f - sqrtf(qx * qx + qy * qy)) * st->prx[hit];
					float rmax = fminf((0.012f + 0.055f * d->z) * us_randr(r, 0.7f, 1.2f), room + 0.004f * d->z);
					add_ring(st, nu, d->land, d->z, rmax);
				} else if (d->z > 0.25f && us_rand(r) < 0.5f + 0.5f * d->z) {
					splash(st, nu, d->land, d->z, vx, d->z > 0.6f ? 3 : 2);
				}
			}
			*d = st->d[--st->nd];
			continue;
		}
		if (x < -0.7f || x > A + 0.7f) {
			*d = st->d[--st->nd];
			continue;
		}
		if (d->y > -0.02f) {
			if (d->snow) {
				float sz = 0.001f + 0.004f * d->z * d->z;
				float v = 0.3f + 0.65f * d->z;
				d->wx = x;
				d->wy = d->y;
				d->wr = sz * 1.8f + 1.5f / c->s;
				if (!ctx->crisp && d->z > 0.7f) {
					/* a six-armed star, turning slowly */
					float rot = d->ph + t * 0.5f;
					for (int k = 0; k < 3; k++) {
						float aa = rot + k * US_PI / 3.0f;
						float cx = cosf(aa) * sz * 1.6f, cy = sinf(aa) * sz * 1.6f;
						hl(c, x - cx, d->y - cy, x + cx, d->y + cy, v, US_MAT_SNOW);
					}
				} else {
					us_point(c, x, d->y, sz, v, US_MAT_SNOW);
				}
			} else {
				/* streak = distance travelled in a short exposure, along the wind */
				float ex = 0.014f + 0.022f * d->z;
				float len_x = vx * ex, len_y = d->vy * ex;
				float v = 0.18f + 0.65f * d->z * d->z;
				if (d->z > 0.7f && !ctx->crisp)
					us_line(c, x - len_x, d->y - len_y, x, d->y, 0.0011f + 0.0009f * d->z, v, US_MAT_WATER);
				else
					hl(c, x - len_x, d->y - len_y, x, d->y, v, US_MAT_WATER);
			}
		}
		i++;
	}

	/* ---- splash droplets ---- */
	for (int i = 0; i < st->nsp;) {
		spl_t *s = &st->sp[i];
		s->vy += 1.8f * dt;
		s->u += s->vx * dt / A;
		s->y += s->vy * dt;
		s->life -= dt * 3.2f;
		if (s->life <= 0) {
			*s = st->sp[--st->nsp];
			continue;
		}
		float v = (0.4f + 0.5f * s->z) * s->life;
		if (ctx->crisp)
			us_point(c, s->u * A, s->y, 0.001f, v, US_MAT_WATER);
		else
			hl(c, s->u * A, s->y, s->u * A - s->vx * 0.02f, s->y - s->vy * 0.02f, v, US_MAT_WATER);
		i++;
	}

	/* ---- the snow drift along the foreground: settles, smooths, melts out of winter ---- */
	{
		float melt = 0.0012f * (1.0f - winter) * (1.0f + 2.0f * st->rain_amt * liquid);
		float any = 0;
		for (int i = 0; i < NDRIFT; i++) {
			st->drift[i] = us_clampf(st->drift[i] - melt * dt, 0, 0.1f);
			if (i > 0 && i < NDRIFT - 1)
				st->drift[i] = us_lerp(st->drift[i], 0.5f * (st->drift[i - 1] + st->drift[i + 1]), dt * 0.8f);
			any += st->drift[i];
		}
		if (any > 0.002f) {
			float top[2 * NDRIFT];
			for (int i = 0; i < NDRIFT; i++) {
				float u = (float)i / (NDRIFT - 1);
				top[2 * i] = u * A;
				top[2 * i + 1] = 1.0f - st->drift[i];
			}
			if (ctx->crisp) {
				for (int i = 0; i < NDRIFT - 1; i++) {
					us_tri(c, top[2 * i], top[2 * i + 1], top[2 * i + 2], top[2 * i + 3], top[2 * i], 1.01f, 0.62f, US_MAT_SNOW);
					us_tri(c, top[2 * i + 2], top[2 * i + 3], top[2 * i + 2], 1.01f, top[2 * i], 1.01f, 0.62f, US_MAT_SNOW);
				}
			} else {
				for (int i = 0; i < NDRIFT - 1; i++) {
					float dp = st->drift[i];
					if (dp < 0.0008f)
						continue;
					hl(c, top[2 * i], top[2 * i + 1], top[2 * i + 2], top[2 * i + 3], us_sat(0.4f + 20.0f * dp), US_MAT_SNOW);
					/* wind-carved strata in the drift */
					hl(c, top[2 * i], 1.0f - dp * 0.55f, top[2 * i + 2], 1.0f - st->drift[i + 1] * 0.55f, 0.22f, US_MAT_SNOW);
				}
			}
		}
	}

	/* ---- foreground grass at the bottom edge, swaying with the wind, buried by the drift ---- */
	{
		int nb = (int)(MAX_BLADES * (0.3f + 0.7f * dens));
		uint8_t bm = winter > 0.6f ? US_MAT_STEM : US_MAT_LEAF;
		for (int i = 0; i < nb; i++) {
			float u = st->bx[i], x = u * A;
			float base = 1.005f;
			float h = (0.03f + 0.09f * st->bh[i]) * (1.0f - 0.4f * winter) - drift_at(st, u) * 1.1f;
			if (h < 0.006f)
				continue;
			float sw = (0.12f + 1.1f * st->wind_s * (0.5f + gust)) * (0.6f + 0.4f * us_noise2(x * 3.0f - st->gust_t, st->bp[i]));
			float pts[10];
			for (int k = 0; k < 5; k++) {
				float f = k / 4.0f;
				pts[2 * k] = x + sw * h * f * f * 0.8f;
				pts[2 * k + 1] = base - h * f * (1.0f - 0.2f * sw * f);
			}
			float v = 0.3f + 0.25f * st->bh[i];
			if (ctx->crisp)
				us_polyline(c, pts, 5, 0.001f, v * 0.8f, bm);
			else
				hpoly(c, pts, 5, v, bm);
		}
	}
}

const us_scene_vt us_scene_rain = {"rain", "Rain", 0.06f, create, destroy, frame};
