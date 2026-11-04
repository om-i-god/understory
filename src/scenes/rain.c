/* Understory scene: Rain — rain falling over a quiet landscape; snow in winter.
 *
 * Layers, back to front: drifting rain clouds (low-res field), a far ridge, a near ridge with conifers,
 * a meadow, a still pool along the bottom with a trembling reflection of the ridge.
 * Every drop has a depth: far drops are short, faint and slow and vanish into the meadow; near drops are
 * long bright streaks that land in the pool and open elliptical ripple rings (or bounce up as tiny
 * splash droplets on the grass). In winter the same drops become slow drifting flakes (near ones are little
 * six-armed stars) that settle into a snow line along the shore and cap the ridge and the trees.
 *
 * Audio: level -> steady density; onsets -> bursts of drops; highs (+ world wind) -> wind slant + gusts;
 *        bass -> heavier, faster near drops. Silence keeps a light drizzle / a few flakes.
 * World: reads season (snow), wind, daylight; writes moisture + water_level (rain fills the world).
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAX_DROPS 1800
#define MAX_SPL 260
#define MAX_RINGS 140
#define NSNOW 192
#define MAX_TREES 22
#define MAX_TUFT 140
#define MAX_GRASS 200
#define MAX_GLINT 70
#define CFW 96
#define CFH 24
#define CLOUD_Y 0.36f
#define US_MAT_SNOW US_MAT_ACCENT /* cream-white at the bright end of most palettes */

typedef struct {
	float u, y, z;   /* u = x / aspect; z = depth 0 far .. 1 near */
	float vy, land, ph;
	uint8_t snow;
} drop_t;

typedef struct {
	float u, y, vx, vy, life;
} spl_t;

typedef struct {
	float u, y, r, rmax, age, life, z;
} ring_t;

typedef struct {
	float u, h, lean;
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
	float snow[NSNOW];
	tree_t tr[MAX_TREES];
	float gx[MAX_GRASS], gh[MAX_GRASS], gp[MAX_GRASS];
	float mu[MAX_TUFT], mv[MAX_TUFT];
	float glu[MAX_GLINT], gly[MAX_GLINT], gll[MAX_GLINT];
	float cloud[CFW * CFH];
	float spawn_acc, rain_amt, wind_s, gust_t, cloud_t, snowfrac, ridge_snow, heavy;
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

/* ---- terrain (u = x / aspect) ---- */
static float far_y(const st_t *st, float u) { return 0.5f + 0.045f * us_fbm2(u * 2.2f + st->so, 1.3f, 3); }
static float near_y(const st_t *st, float u)
{
	return 0.6f + 0.05f * us_fbm2(u * 3.1f + st->so, 7.7f, 3) + 0.015f * sinf(u * 5.0f + st->so);
}
static float shore_y(const st_t *st, float u) { return 0.8f + 0.012f * us_noise2(u * 4.0f + st->so, 3.3f); }

static float snow_at(const st_t *st, float u)
{
	float f = us_clampf(u, 0, 1) * (NSNOW - 1);
	int i = (int)f;
	if (i >= NSNOW - 1)
		return st->snow[NSNOW - 1];
	return us_lerp(st->snow[i], st->snow[i + 1], f - i);
}

static void init_scene(st_t *st, const us_ctx *ctx)
{
	us_rng *r = &st->rng;
	float winter = us_world_winter(ctx->w);
	/* memory: open onto whatever snow the season has laid down */
	for (int i = 0; i < NSNOW; i++)
		st->snow[i] = us_smooth(0.5f, 0.95f, winter) * 0.018f * (0.75f + 0.25f * us_noise2(i * 0.11f, 4.0f));
	st->ridge_snow = us_smooth(0.5f, 0.95f, winter);
	st->snowfrac = us_smooth(0.35f, 0.65f, winter);
	st->rain_amt = us_sat(0.2f + ctx->w->moisture * 0.4f);
	/* conifers in a few groves plus scattered singles */
	float grove[3] = {us_randr(r, 0.05f, 0.3f), us_randr(r, 0.4f, 0.6f), us_randr(r, 0.7f, 0.95f)};
	for (int i = 0; i < MAX_TREES; i++) {
		float gc = grove[i % 3];
		st->tr[i].u = (i % 4 == 3) ? us_rand(r) : us_clampf(gc + us_randn(r) * 0.04f, 0.0f, 1.0f);
		st->tr[i].h = us_randr(r, 0.03f, 0.095f) * (i % 4 == 3 ? 0.7f : 1.0f);
		st->tr[i].lean = us_randr(r, -0.04f, 0.04f);
	}
	for (int i = 0; i < MAX_TUFT; i++) {
		st->mu[i] = us_rand(r);
		st->mv[i] = us_rand(r);
	}
	for (int i = 0; i < MAX_GRASS; i++) {
		st->gx[i] = us_rand(r);
		st->gh[i] = us_randr(r, 0.35f, 1.0f);
		st->gp[i] = us_rand(r) * 20.0f;
	}
	for (int i = 0; i < MAX_GLINT; i++) {
		st->glu[i] = us_rand(r);
		st->gly[i] = us_rand(r);
		st->gll[i] = us_randr(r, 0.01f, 0.05f);
	}
	st->inited = 1;
}

static void spawn_drop(st_t *st, float A, float slant, int top)
{
	if (st->nd >= MAX_DROPS)
		return;
	us_rng *r = &st->rng;
	drop_t *d = &st->d[st->nd++];
	d->snow = us_rand(r) < st->snowfrac;
	d->z = powf(us_rand(r), 0.8f);
	d->ph = us_rand(r) * US_TAU;
	/* where it lands: far drops in the meadow behind, near drops in the pool */
	/* start upwind so the slanted fall still covers the frame */
	float x = us_randr(r, -slant * (d->snow ? 0.5f : 1.0f) - 0.02f, A + 0.02f);
	d->u = x / A;
	d->y = top ? us_randr(r, -0.12f, -0.01f) : us_randr(r, -0.05f, 0.95f);
	float nu = us_clampf(d->u, 0, 1);
	float ridge = near_y(st, nu), shore = shore_y(st, nu);
	float land = us_lerp(ridge + 0.004f, 1.0f, d->z);
	if (d->snow && d->z > 0.62f && d->z < 0.8f)
		land = shore - snow_at(st, nu); /* settle on the snow line */
	d->land = land;
	if (d->snow)
		d->vy = (0.05f + 0.08f * d->z) * us_randr(r, 0.8f, 1.2f);
	else
		d->vy = (0.75f + 1.15f * d->z) * us_randr(r, 0.9f, 1.1f) * (1.0f + 0.35f * st->heavy);
	if (!top && d->y > land)
		d->y = us_randr(r, -0.05f, land);
}

static void add_ring(st_t *st, float u, float y, float z)
{
	if (st->nrg >= MAX_RINGS)
		return;
	ring_t *g = &st->rg[st->nrg++];
	g->u = u;
	g->y = y;
	g->z = z;
	g->r = 0;
	g->rmax = (0.012f + 0.04f * z * z) * us_randr(&st->rng, 0.7f, 1.2f);
	g->age = 0;
	g->life = 0.9f + 0.9f * z;
}

static void ellipse(us_canvas *c, float x, float y, float rx, float ry, float v, uint8_t mat)
{
	float pts[2 * 29];
	int n = rx * c->s < 6.0f ? 12 : 28;
	for (int i = 0; i <= n; i++) {
		float a = US_TAU * i / n;
		pts[2 * i] = x + cosf(a) * rx;
		pts[2 * i + 1] = y + sinf(a) * ry;
	}
	hpoly(c, pts, n + 1, v, mat);
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
	float wind_t = us_sat(0.25f * w->wind + 0.8f * a->high + 0.35f * a->air + 0.15f * autumn);
	st->wind_s = us_approach(st->wind_s, wind_t, 1.2f, dt);
	st->gust_t += dt * (0.2f + 0.8f * st->wind_s);
	float gust = 0.5f + 0.5f * us_noise2(st->gust_t * 0.7f, st->so);
	float slant = (0.05f + 0.55f * st->wind_s * (0.6f + 0.8f * gust));
	st->heavy = us_approach(st->heavy, us_sat(0.7f * a->bass + 0.5f * a->kick), 0.3f, ctx->real_dt);
	float target = us_sat(0.08f + 0.85f * a->level + 0.25f * a->swell);
	st->rain_amt = us_approach(st->rain_amt, target, target > st->rain_amt ? 0.8f : 3.0f, dt);
	st->cloud_t += dt * (0.02f + 0.06f * st->wind_s);

	us_world_add(w, &w->moisture, 0.006f * st->rain_amt * (1.0f - 0.6f * st->snowfrac), dt);
	us_world_add(w, &w->water_level, 0.0025f * st->rain_amt * (1.0f - 0.8f * st->snowfrac), dt);

	/* ---- spawn: steady density from level, bursts from onsets ---- */
	float dscale = 0.25f + 1.5f * dens;
	float rate = (22.0f + 520.0f * st->rain_amt * st->rain_amt + 120.0f * st->rain_amt) * dscale;
	rate *= 1.0f - 0.65f * st->snowfrac; /* flakes live much longer, fewer per second */
	st->spawn_acc += rate * dt;
	while (st->spawn_acc >= 1.0f) {
		spawn_drop(st, A, slant, 1);
		st->spawn_acc -= 1.0f;
	}
	if (a->beat) {
		int burst = (int)((25.0f + 110.0f * a->onset) * dscale * (1.0f - 0.6f * st->snowfrac));
		for (int i = 0; i < burst; i++)
			spawn_drop(st, A, slant, 1);
	}
	/* when the scene opens, start mid-shower instead of an empty sky */
	if (st->nd == 0)
		for (int i = 0; i < (int)(rate * 0.6f); i++)
			spawn_drop(st, A, slant, 0);

	/* ---- clouds: a slow low-res field at the top ---- */
	{
		float cov = 0.25f + 0.75f * st->rain_amt;
		for (int j = 0; j < CFH; j++) {
			float y = (j + 0.5f) / CFH;
			float vf = us_smooth(1.0f, 0.25f, y) * us_smooth(0.0f, 0.15f, y);
			for (int i = 0; i < CFW; i++) {
				float u = (i + 0.5f) / CFW;
				float n = us_fbm3(u * 3.0f * A - st->cloud_t * 2.0f, y * 2.2f, st->cloud_t * 0.5f + st->so, 4);
				float v = us_smooth(-0.15f + 0.3f * (1 - cov), 0.55f, n) * vf;
				st->cloud[j * CFW + i] = v;
			}
		}
		us_field(c, st->cloud, CFW, CFH, ctx->crisp ? 0.26f : 0.22f, US_MAT_WATER, 0, 0, A, CLOUD_Y);
	}

	/* ---- landscape ---- */
	const int NT = 96;
	float far[2 * 97], nr[2 * 97], sh[2 * 97];
	for (int i = 0; i <= NT; i++) {
		float u = (float)i / NT;
		far[2 * i] = nr[2 * i] = sh[2 * i] = u * A;
		far[2 * i + 1] = far_y(st, u);
		nr[2 * i + 1] = near_y(st, u);
		sh[2 * i + 1] = shore_y(st, u);
	}
	float ground_v = 0.14f + 0.05f * winter;
	uint8_t gm = winter > 0.6f ? US_MAT_SNOW : US_MAT_EARTH;
	if (ctx->crisp) {
		for (int i = 0; i < NT; i++) {
			float x0 = far[2 * i], x1 = far[2 * i + 2];
			/* far hills */
			us_tri(c, x0, far[2 * i + 1], x1, far[2 * i + 3], x0, nr[2 * i + 1], 0.1f, US_MAT_EARTH);
			us_tri(c, x1, far[2 * i + 3], x1, nr[2 * i + 3], x0, nr[2 * i + 1], 0.1f, US_MAT_EARTH);
			/* meadow */
			us_tri(c, x0, nr[2 * i + 1], x1, nr[2 * i + 3], x0, sh[2 * i + 1], ground_v, gm);
			us_tri(c, x1, nr[2 * i + 3], x1, sh[2 * i + 3], x0, sh[2 * i + 1], ground_v, gm);
			/* pool */
			us_tri(c, x0, sh[2 * i + 1], x1, sh[2 * i + 3], x0, 1.01f, 0.08f, US_MAT_WATER);
			us_tri(c, x1, sh[2 * i + 3], x1, 1.01f, x0, 1.01f, 0.08f, US_MAT_WATER);
		}
	}
	/* a faint middle ridge for depth */
	{
		float mid[2 * 97];
		for (int i = 0; i <= NT; i++) {
			float u = (float)i / NT;
			mid[2 * i] = u * A;
			mid[2 * i + 1] = 0.56f + 0.035f * us_fbm2(u * 2.7f + st->so, 4.4f, 3);
		}
		hpoly(c, mid, NT + 1, 0.14f + 0.12f * st->ridge_snow, st->ridge_snow > 0.5f ? US_MAT_SNOW : US_MAT_EARTH);
	}
	/* meadow texture: short strokes, smaller with distance */
	{
		int nm = (int)(MAX_TUFT * (0.3f + 0.7f * dens));
		for (int i = 0; i < nm; i++) {
			float u = st->mu[i], x = u * A;
			float y0 = near_y(st, u) + 0.01f, y1 = shore_y(st, u) - 0.01f;
			float f = st->mv[i] * st->mv[i];
			float y = us_lerp(y0, y1, f);
			float h = 0.002f + 0.007f * f;
			float sw = st->wind_s * h * 0.8f;
			hl(c, x, y, x + sw, y - h, 0.16f + 0.12f * f + 0.15f * winter, winter > 0.6f ? US_MAT_SNOW : US_MAT_LEAF);
		}
	}
	hpoly(c, far, NT + 1, 0.22f + 0.2f * st->ridge_snow, st->ridge_snow > 0.5f ? US_MAT_SNOW : US_MAT_EARTH);
	us_polyline(c, nr, NT + 1, 0.0015f, 0.45f + 0.25f * st->ridge_snow, st->ridge_snow > 0.5f ? US_MAT_SNOW : US_MAT_EARTH);
	st->ridge_snow = us_approach(st->ridge_snow, us_smooth(0.5f, 0.95f, winter), 20.0f, dt);

	/* conifers on the near ridge (snow-laden in winter) */
	{
		int ntr = 8 + (int)(dens * (MAX_TREES - 8));
		for (int i = 0; i < ntr; i++) {
			tree_t *tr = &st->tr[i];
			float x = tr->u * A, base = near_y(st, tr->u) + 0.004f;
			float sway = 0.006f * st->wind_s * us_noise2(st->gust_t * 1.5f, i * 1.7f);
			float tx = x + tr->lean * tr->h + sway, ty = base - tr->h;
			hl(c, x, base, tx, ty, 0.42f, US_MAT_STEM);
			int tiers = 4 + (int)(tr->h * 60.0f);
			for (int k = 0; k < tiers; k++) {
				float f = (k + 1.0f) / (tiers + 1.0f); /* 0 top .. 1 bottom */
				float px = us_lerp(tx, x, f), py = us_lerp(ty, base, f);
				float bw = tr->h * 0.36f * f;
				float droop = tr->h * 0.18f * (1.0f + 0.5f * st->ridge_snow);
				if (ctx->crisp) {
					us_tri(c, px, py - droop * 0.4f, px - bw, py + droop * 0.5f, px + bw, py + droop * 0.5f, 0.32f, US_MAT_LEAF);
				} else {
					hl(c, px, py - droop * 0.3f, px - bw, py + droop * 0.5f, 0.38f, US_MAT_LEAF);
					hl(c, px, py - droop * 0.3f, px + bw, py + droop * 0.5f, 0.38f, US_MAT_LEAF);
				}
				if (st->ridge_snow > 0.2f)
					hl(c, px - bw * 0.7f, py + droop * 0.25f, px + bw * 0.7f, py + droop * 0.25f, 0.55f * st->ridge_snow, US_MAT_SNOW);
			}
		}
	}

	/* the pool: shoreline, reflected ridge (trembling with the rain), glints */
	{
		us_polyline(c, sh, NT + 1, 0.0016f, 0.5f, US_MAT_EARTH);
		float refl[2 * 97];
		float tremble = 0.002f + 0.006f * st->rain_amt * (1.0f - st->snowfrac);
		for (int i = 0; i <= NT; i++) {
			float u = (float)i / NT;
			float s = sh[2 * i + 1];
			refl[2 * i] = u * A;
			refl[2 * i + 1] = s + (s - nr[2 * i + 1]) * 0.55f + tremble * us_noise2(u * 40.0f, t * 3.0f);
		}
		/* dashed: draw every other segment */
		for (int i = 0; i < NT; i += 2)
			hl(c, refl[2 * i], refl[2 * i + 1], refl[2 * i + 2], refl[2 * i + 3], 0.18f + 0.1f * st->ridge_snow, US_MAT_WATER);
		int ng = (int)(MAX_GLINT * (0.4f + 0.6f * dens));
		for (int i = 0; i < ng; i++) {
			float u = st->glu[i];
			float s = shore_y(st, u);
			float y = s + 0.012f + st->gly[i] * (0.99f - s - 0.012f);
			float sh_ = us_noise2(i * 3.1f, t * (0.4f + 1.5f * st->rain_amt));
			float v = us_smooth(0.1f, 0.6f, sh_) * (0.15f + 0.25f * st->gly[i]);
			if (v < 0.02f)
				continue;
			float L = st->gll[i] * (0.5f + st->gly[i]);
			hl(c, u * A - L * 0.5f, y, u * A + L * 0.5f, y, v, US_MAT_WATER);
		}
	}

	/* ---- snow line along the shore: melts slowly out of winter ---- */
	{
		float melt = 0.0009f * (1.0f - winter) * (1.0f + 2.0f * st->rain_amt * (1.0f - st->snowfrac));
		float maxd = 0.03f;
		float any = 0;
		for (int i = 0; i < NSNOW; i++) {
			st->snow[i] = us_clampf(st->snow[i] - melt * dt, 0, maxd);
			/* gentle settling: diffuse a little so it reads as drifts, not spikes */
			if (i > 0 && i < NSNOW - 1)
				st->snow[i] = us_lerp(st->snow[i], 0.5f * (st->snow[i - 1] + st->snow[i + 1]), dt * 0.6f);
			any += st->snow[i];
		}
		if (any > 0.002f) {
			float top[2 * NSNOW];
			for (int i = 0; i < NSNOW; i++) {
				float u = (float)i / (NSNOW - 1);
				top[2 * i] = u * A;
				top[2 * i + 1] = shore_y(st, u) - st->snow[i];
			}
			if (ctx->crisp) {
				for (int i = 0; i < NSNOW - 1; i++) {
					float u0 = (float)i / (NSNOW - 1), u1 = (float)(i + 1) / (NSNOW - 1);
					float s0 = shore_y(st, u0), s1 = shore_y(st, u1);
					us_tri(c, top[2 * i], top[2 * i + 1], top[2 * i + 2], top[2 * i + 3], top[2 * i], s0 + 0.002f, 0.55f, US_MAT_SNOW);
					us_tri(c, top[2 * i + 2], top[2 * i + 3], top[2 * i + 2], s1 + 0.002f, top[2 * i], s0 + 0.002f, 0.55f, US_MAT_SNOW);
				}
			} else {
				for (int i = 0; i < NSNOW - 1; i++) {
					float dpth = st->snow[i];
					if (dpth < 0.0008f)
						continue;
					hl(c, top[2 * i], top[2 * i + 1], top[2 * i + 2], top[2 * i + 3], us_sat(0.35f + 30.0f * dpth), US_MAT_SNOW);
					/* faint inner strata */
					hl(c, top[2 * i], top[2 * i + 1] + dpth * 0.45f, top[2 * i + 2], top[2 * i + 3] + st->snow[i + 1] * 0.45f,
					   0.2f, US_MAT_SNOW);
				}
			}
		}
	}

	/* ---- shore grass, swaying with the wind, buried by snow ---- */
	{
		int ng = (int)(MAX_GRASS * (0.3f + 0.7f * dens));
		for (int i = 0; i < ng; i++) {
			float u = st->gx[i], x = u * A;
			float base = shore_y(st, u) + 0.002f;
			float h = 0.03f * st->gh[i] * (1.0f - 0.45f * winter) - snow_at(st, u) * 0.8f;
			if (h < 0.003f)
				continue;
			float sw = (0.15f + 1.1f * st->wind_s * (0.5f + gust)) * (0.6f + 0.4f * us_noise2(x * 3.0f - st->gust_t, st->gp[i]));
			float pts[8];
			for (int k = 0; k < 4; k++) {
				float f = k / 3.0f;
				pts[2 * k] = x + sw * h * f * f * 0.9f;
				pts[2 * k + 1] = base - h * f * (1.0f - 0.25f * sw * f);
			}
			hpoly(c, pts, 4, 0.3f + 0.25f * st->gh[i], winter > 0.6f ? US_MAT_STEM : US_MAT_LEAF);
		}
	}

	/* ---- rain + snow ---- */
	for (int i = 0; i < st->nd;) {
		drop_t *d = &st->d[i];
		float x = d->u * A;
		float vx;
		if (d->snow) {
			float flutter = sinf(t * (1.2f + d->z) + d->ph) * (0.02f + 0.02f * d->z);
			vx = slant * 0.35f * (0.4f + 0.6f * d->z) + flutter + 0.03f * us_noise2(x * 2.0f, t * 0.3f + d->ph);
		} else {
			vx = slant * d->vy;
		}
		x += vx * dt;
		d->y += d->vy * dt;
		d->u = x / A;
		if (d->y >= d->land && (d->u < 0.0f || d->u > 1.0f)) { /* landed off-frame */
			*d = st->d[--st->nd];
			continue;
		}
		if (d->y >= d->land) {
			float nu = d->u;
			float shore = shore_y(st, nu);
			if (d->snow) {
				if (d->land < shore + 0.001f && d->z > 0.6f) {
					int k = (int)(nu * (NSNOW - 1) + 0.5f);
					st->snow[k] = us_sat(st->snow[k] + 0.0007f * (0.5f + d->z));
				}
			} else if (d->land > shore + 0.004f) {
				if (us_rand(r) < 0.35f + 0.65f * d->z)
					add_ring(st, nu, d->land, d->z);
			} else if (d->z > 0.3f && st->nsp + 2 <= MAX_SPL) {
				for (int k = 0; k < 2; k++) {
					spl_t *s = &st->sp[st->nsp++];
					s->u = nu;
					s->y = d->land;
					s->vx = us_randr(r, -0.12f, 0.12f) * d->z + vx * 0.1f;
					s->vy = -us_randr(r, 0.12f, 0.3f) * d->z;
					s->life = 1.0f;
				}
			}
			*d = st->d[--st->nd];
			continue;
		}
		if (x < -0.6f || x > A + 0.6f) {
			*d = st->d[--st->nd];
			continue;
		}
		if (d->y > -0.01f) {
			if (d->snow) {
				float sz = 0.0012f + 0.0035f * d->z * d->z;
				float v = 0.35f + 0.6f * d->z;
				if (!ctx->crisp && d->z > 0.78f) {
					/* little six-armed star, turning slowly */
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
				/* streak length ~ distance travelled in a short exposure */
				float ex = 0.012f + 0.02f * d->z;
				float len_x = vx * ex, len_y = d->vy * ex;
				float v = 0.2f + 0.6f * d->z * d->z;
				if (d->z > 0.85f && !ctx->crisp)
					us_line(c, x - len_x, d->y - len_y, x, d->y, 0.0014f, v, US_MAT_WATER);
				else
					hl(c, x - len_x, d->y - len_y, x, d->y, v, US_MAT_WATER);
			}
		}
		i++;
	}

	/* ---- splash droplets ---- */
	for (int i = 0; i < st->nsp;) {
		spl_t *s = &st->sp[i];
		s->vy += 1.6f * dt;
		s->u += s->vx * dt / A;
		s->y += s->vy * dt;
		s->life -= dt * 3.5f;
		if (s->life <= 0) {
			*s = st->sp[--st->nsp];
			continue;
		}
		us_point(c, s->u * A, s->y, 0.0011f, 0.6f * s->life, US_MAT_WATER);
		i++;
	}

	/* ---- ripple rings in the pool (perspective ellipses) ---- */
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
		float v = (1.0f - f) * (0.3f + 0.55f * g->z);
		float squash = 0.18f + 0.1f * g->z;
		float x = g->u * A;
		ellipse(c, x, g->y, rr, rr * squash, v, US_MAT_WATER);
		if (g->z > 0.55f && rr > 0.008f)
			ellipse(c, x, g->y, rr * 0.6f, rr * 0.6f * squash, v * 0.55f, US_MAT_WATER);
		/* first instant: a tiny crown */
		if (g->age < 0.08f && g->z > 0.5f)
			hl(c, x, g->y, x, g->y - 0.008f * g->z, 0.7f, US_MAT_WATER);
		i++;
	}
}

const us_scene_vt us_scene_rain = {"rain", "Rain", 0.06f, create, destroy, frame};
