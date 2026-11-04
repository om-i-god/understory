/* Understory scene: Pollen & Seeds — drifting grains, spores and dandelion parachutes in warm air.
 *
 * Three depth layers of airborne particles ride a curl-noise wind (divergence-free, so they swirl rather
 * than bunch up): far motes are tiny and slow, near dandelion seeds are large, slow-turning starbursts.
 *   highs / air   -> thermals: lift everything, near seeds rise and sparkle
 *   sub / bass    -> the slow vortices: curl strength and eddy size swell with the low end
 *   level         -> overall breeze
 *   onsets        -> seed heads along the bottom release new parachutes (and pollen puffs from flowers)
 * By day, faint parallel sunbeam shafts slant down and particles inside them catch the light.
 * World: reads bloom (flowers vs seed heads), pollen (how much is airborne), daylight, season, wind;
 *        writes pollen. In winter the air holds slow snow motes instead and the heads are bare.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAXP 1000
#define NHEADS 9
#define NBLADE 120
#define CGX 36
#define CGY 20
#define NBEAM 3

enum { K_GRAIN = 0, K_SPORE, K_SEED, K_SNOW };

typedef struct {
	float x, y, vx, vy; /* x stored as fraction of width */
	float z;            /* depth 0 far .. 1 near */
	float rot, vr;
	float life;
	float age, ttl; /* seconds alive, lifespan */
	float seedv;        /* per-particle variation */
	int kind, alive;
} part_t;

typedef struct {
	float x;       /* fraction of width */
	float h;       /* stem height */
	float seeds;   /* 0..1 seeds left on the clock */
	float flower;  /* 1 = flower (bloom), 0 = seed clock */
	float ph;      /* sway phase */
	float hx, hy;  /* current head position (normalized, x already * aspect) */
	int petals;
} head_t;

typedef struct {
	us_rng rng;
	int inited;
	part_t p[MAXP];
	head_t hd[NHEADS];
	float blade_x[NBLADE], blade_h[NBLADE], blade_ph[NBLADE];
	float psi[(CGX + 1) * (CGY + 1)];
	float fvx[CGX * CGY], fvy[CGX * CGY];
	float bass_s, high_s, lvl_s, swirl_t, wind_t;
	float spawn_acc;
	float beam_x[NBEAM], beam_w[NBEAM], beam_ph[NBEAM];
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x706f6c6cu);
	return s;
}

static void destroy(void *p) { free(p); }

static float ground_y(float xf) { return 0.93f + 0.015f * us_noise2(xf * 2.3f, 8.1f); }

static void spawn(st_t *st, part_t *q, float winter, int anywhere, float A)
{
	us_rng *r = &st->rng;
	(void)A;
	memset(q, 0, sizeof(*q));
	q->alive = 1;
	q->z = us_rand(r);
	float u = us_rand(r);
	if (us_rand(r) < winter)
		q->kind = K_SNOW;
	else if (u < 0.03f + 0.04f * q->z)
		q->kind = K_SEED;
	else if (u < 0.35f)
		q->kind = K_SPORE;
	else
		q->kind = K_GRAIN;
	q->x = us_rand(r);
	if (anywhere)
		q->y = us_randr(r, 0.02f, 0.9f);
	else if (q->kind == K_SNOW)
		q->y = -0.02f; /* snow sifts down from above */
	else {
		/* enter from an edge the wind is blowing from, or from below (thermals) */
		float e = us_rand(r);
		if (e < 0.6f) {
			q->x = -0.02f;
			q->y = us_randr(r, 0.05f, 0.88f);
		} else if (e < 0.8f) {
			q->y = 0.95f;
		} else {
			q->y = -0.02f;
		}
	}
	q->rot = us_rand(r) * US_TAU;
	q->vr = us_randn(r) * 0.25f;
	q->life = 0.0f;
	q->seedv = us_rand(r);
	q->ttl = us_randr(r, 25.0f, 60.0f);
}

/* a dandelion pappus: short beak + radial hairs; crisp = fewer, bolder strokes */
static void pappus(us_canvas *c, int crisp, float x, float y, float sz, float rot, float v, uint8_t mat)
{
	float cx = x + sinf(rot) * sz * 0.9f, cy = y - cosf(rot) * sz * 0.9f; /* crown above the seed */
	if (crisp) {
		us_line(c, x, y, cx, cy, 0.002f, v * 0.7f, mat);
		int n = sz > 0.012f ? 6 : 4;
		for (int k = 0; k < n; k++) {
			float a = rot - US_PI * 0.5f + (k + 0.5f) * US_PI / n - US_PI * 0.5f * 0.0f;
			float ax = cx + cosf(a - US_PI * 0.5f) * sz * 0.8f, ay = cy + sinf(a - US_PI * 0.5f) * sz * 0.8f;
			us_line(c, cx, cy, ax, ay, 0.002f, v, mat);
		}
		return;
	}
	us_line(c, x, y, cx, cy, 0.0008f, v * 0.6f, mat);
	us_disc(c, x, y, sz * 0.09f, v * 0.8f, mat);
	const int n = 11;
	for (int k = 0; k < n; k++) {
		/* hairs fan over the upper hemisphere of the crown, tips curling slightly */
		float a = rot - US_PI + (k + 0.5f) * US_PI / n;
		float ex = cx + cosf(a) * sz, ey = cy + sinf(a) * sz;
		us_line(c, cx, cy, ex, ey, 0.0007f, v * 0.75f, mat);
		us_point(c, ex, ey, 0.0009f, v, mat);
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
	float winter = us_world_winter(w), autumn = us_world_season_w(w, 2);
	float day = us_smooth(0.25f, 0.75f, w->daylight);
	if (dt > 0.05f)
		dt = 0.05f;

	if (!st->inited) {
		us_rng *r = &st->rng;
		for (int i = 0; i < NHEADS; i++) {
			head_t *h = &st->hd[i];
			h->x = (i + 0.5f) / NHEADS + us_randr(r, -0.35f, 0.35f) / NHEADS;
			h->h = us_randr(r, 0.10f, 0.22f);
			h->seeds = us_randr(r, 0.4f, 1.0f);
			h->flower = us_rand(r) < w->bloom ? 1.0f : 0.0f;
			h->ph = us_rand(r) * 30.0f;
			h->petals = 11 + (int)(us_rand(r) * 8);
		}
		for (int b = 0; b < NBLADE; b++) {
			st->blade_x[b] = us_rand(r);
			st->blade_h[b] = us_randr(r, 0.015f, 0.06f);
			st->blade_ph[b] = us_rand(r) * 20.0f;
		}
		for (int k = 0; k < NBEAM; k++) {
			st->beam_x[k] = us_randr(r, 0.1f, 1.1f);
			st->beam_w[k] = us_randr(r, 0.04f, 0.11f);
			st->beam_ph[k] = us_rand(r) * 10.0f;
		}
		/* airborne amount at open remembers the world's pollen */
		float nmax = 250.0f + 700.0f * dens;
		int n0 = (int)(nmax * us_sat(0.4f + 0.8f * w->pollen + 0.2f * w->bloom));
		for (int i = 0; i < n0 && i < MAXP; i++) {
			spawn(st, &st->p[i], winter, 1, A);
			st->p[i].life = 1.0f;
		}
		st->inited = 1;
	}

	/* ---- audio shaping ---- */
	st->bass_s = us_approach(st->bass_s, us_sat(0.7f * a->sub + 0.5f * a->bass + 0.3f * a->kick), 0.6f, rdt);
	st->high_s = us_approach(st->high_s, us_sat(0.8f * a->high + 0.6f * a->air), 0.35f, rdt);
	st->lvl_s = us_approach(st->lvl_s, a->level, 1.0f, rdt);
	float wind = us_sat(0.25f * w->wind + 0.5f * st->lvl_s);
	st->wind_t += dt * (0.2f + wind);
	st->swirl_t += dt * (0.05f + 0.12f * st->bass_s);

	/* ---- world ---- */
	us_world_add(w, &w->pollen, 0.004f * (0.2f + st->high_s + a->onset * 0.5f) * (0.3f + w->bloom) * (1.0f - winter), dt);

	/* ---- curl-noise wind on a coarse grid ---- */
	float eddy = 1.6f + 1.4f * (1.0f - st->bass_s); /* big slow eddies when the low end swells */
	float swirl = 0.010f + 0.035f * st->bass_s;
	for (int j = 0; j <= CGY; j++)
		for (int i = 0; i <= CGX; i++) {
			float x = (float)i / CGX * A, y = (float)j / CGY;
			st->psi[j * (CGX + 1) + i] = us_noise3(x * eddy, y * eddy, st->swirl_t) +
						     0.4f * us_noise3(x * eddy * 2.3f, y * eddy * 2.3f, st->swirl_t * 1.7f + 5.0f);
		}
	{
		float hx = A / CGX, hy = 1.0f / CGY;
		for (int j = 0; j < CGY; j++)
			for (int i = 0; i < CGX; i++) {
				float p00 = st->psi[j * (CGX + 1) + i], p10 = st->psi[j * (CGX + 1) + i + 1];
				float p01 = st->psi[(j + 1) * (CGX + 1) + i], p11 = st->psi[(j + 1) * (CGX + 1) + i + 1];
				float dpdy = ((p01 + p11) - (p00 + p10)) * 0.5f / hy;
				float dpdx = ((p10 + p11) - (p00 + p01)) * 0.5f / hx;
				st->fvx[j * CGX + i] = dpdy * swirl / eddy;
				st->fvy[j * CGX + i] = -dpdx * swirl / eddy;
			}
	}

	/* ---- sunbeams (day): faint parallel slanted hairlines ---- */
	const float bdx = 0.42f; /* beam slant: x shift per unit of y */
	float beam_v[NBEAM];
	for (int k = 0; k < NBEAM; k++) {
		float flick = 0.5f + 0.5f * us_noise2(t * 0.07f, st->beam_ph[k]);
		beam_v[k] = day * (1.0f - winter * 0.5f) * us_smooth(0.2f, 0.8f, flick) * (0.6f + 0.4f * st->high_s);
		st->beam_x[k] += dt * 0.004f;
		if (st->beam_x[k] > 1.3f)
			st->beam_x[k] -= 1.5f;
		if (beam_v[k] < 0.02f)
			continue;
		float x0 = st->beam_x[k] * A, wdt = st->beam_w[k];
		int nl = ctx->crisp ? 4 : 9;
		for (int l = 0; l < nl; l++) {
			float off = (l / (float)(nl - 1) - 0.5f) * wdt;
			float edge = 1.0f - fabsf(off) / (wdt * 0.5f + 1e-4f) * 0.6f;
			float yb = 0.88f;
			/* pixel palettes: the BG ramp gives a soft slate shaft instead of a hard gold wire */
			us_line(c, x0 + off, -0.02f, x0 + off + bdx * (yb + 0.02f), yb, ctx->crisp ? 0.004f : 0.001f,
				(ctx->crisp ? 0.7f : 0.26f) * beam_v[k] * edge, ctx->crisp ? US_MAT_BG : US_MAT_LIGHT);
		}
	}

	/* ---- ground: low grass line ---- */
	{
		float pts[2 * 65];
		for (int i = 0; i <= 64; i++) {
			float xf = i / 64.0f;
			pts[2 * i] = xf * A;
			pts[2 * i + 1] = ground_y(xf);
		}
		us_polyline(c, pts, 65, ctx->crisp ? 0.003f : 0.0012f, 0.35f + 0.2f * winter, US_MAT_EARTH);
		int nb = (int)(NBLADE * (0.4f + 0.6f * dens) * (1.0f - 0.6f * winter));
		for (int b = 0; b < nb; b++) {
			float xf = st->blade_x[b], x0 = xf * A, gy = ground_y(xf), h = st->blade_h[b];
			float sw = (us_noise2(x0 * 1.5f - st->wind_t * 0.5f, st->blade_ph[b]) * 0.6f + 0.3f) * (0.2f + 0.8f * wind);
			float bp[8];
			for (int q = 0; q < 4; q++) {
				float u = q / 3.0f;
				bp[2 * q] = x0 + sw * h * u * u;
				bp[2 * q + 1] = gy - h * u;
			}
			us_polyline(c, bp, 4, ctx->crisp ? 0.002f : 0.001f, 0.3f, autumn > 0.5f ? US_MAT_EARTH : US_MAT_LEAF);
		}
	}

	/* ---- seed heads / flowers ---- */
	int beat_ok = a->beat && a->onset > 0.35f && winter < 0.8f;
	int nseeds = 0;
	for (int i = 0; i < MAXP; i++)
		nseeds += st->p[i].alive && st->p[i].kind == K_SEED;
	int seed_cap = 25 + (int)(70.0f * dens);
	if (nseeds >= seed_cap)
		beat_ok = 0;
	for (int i = 0; i < NHEADS; i++) {
		head_t *h = &st->hd[i];
		/* flowers follow the world's bloom slowly; spent flowers turn to clocks */
		if (h->flower > 0.5f && us_rand(&st->rng) < dt * 0.01f * (1.2f - w->bloom))
			h->flower = 0, h->seeds = 1.0f;
		else if (h->flower < 0.5f && h->seeds < 0.05f && us_rand(&st->rng) < dt * 0.02f * (0.2f + w->bloom))
			h->flower = 1;
		if (h->flower < 0.5f)
			h->seeds = us_sat(h->seeds + dt * 0.03f * (1.0f - winter));

		/* each head answers a hit with its own chance, so releases ripple along the row */
		int release = beat_ok && us_rand(&st->rng) < 0.3f + 0.4f * a->onset ? 1 + (us_rand(&st->rng) < a->onset * 0.5f) : 0;
		float xf = h->x, x0 = xf * A, gy = ground_y(xf);
		float H = h->h * (1.0f - 0.4f * winter);
		float sw = (us_noise2(st->wind_t * 0.4f, h->ph) * 0.8f + 0.3f) * (0.04f + 0.12f * wind + 0.06f * st->high_s);
		float hx = x0 + sw * H, hy = gy - H;
		h->hx = hx;
		h->hy = hy;
		float sp[8];
		for (int q = 0; q < 4; q++) {
			float u = q / 3.0f;
			sp[2 * q] = x0 + sw * H * u * u;
			sp[2 * q + 1] = gy - H * u;
		}
		us_polyline(c, sp, 4, ctx->crisp ? 0.002f : 0.0013f, 0.45f, US_MAT_STEM);
		if (winter > 0.8f)
			continue;
		if (h->flower > 0.5f) {
			/* composite flower: ray petals + disc */
			float fr = 0.018f + 0.01f * (H / 0.22f);
			for (int k = 0; k < h->petals; k++) {
				float ang = US_TAU * k / h->petals + h->ph;
				float ex = hx + cosf(ang) * fr, ey = hy + sinf(ang) * fr * 0.45f;
				us_line(c, hx + cosf(ang) * fr * 0.3f, hy + sinf(ang) * fr * 0.14f, ex, ey,
					ctx->crisp ? 0.003f : 0.0013f, 0.7f, US_MAT_BLOOM);
			}
			us_disc(c, hx, hy, fr * 0.28f, 0.85f, US_MAT_LIGHT);
			/* onsets puff pollen out of open flowers */
			if (release)
				for (int k = 0; k < release * 2; k++)
					for (int j = 0; j < MAXP; j++)
						if (!st->p[j].alive) {
							part_t *q = &st->p[j];
							spawn(st, q, 0, 1, A);
							q->kind = K_GRAIN;
							q->x = hx / A;
							q->y = hy;
							q->z = us_randr(&st->rng, 0.5f, 1.0f);
							q->vx = us_randn(&st->rng) * 0.03f;
							q->vy = -us_randr(&st->rng, 0.02f, 0.08f);
							q->life = 0.6f;
							break;
						}
		} else {
			/* dandelion clock: a sphere of radial hairs, thinning as seeds blow away */
			float cr = 0.022f + 0.012f * (H / 0.22f);
			int nh = ctx->crisp ? 14 : 34;
			int keep = (int)(nh * h->seeds + 0.5f);
			for (int k = 0; k < keep; k++) {
				float ang = US_TAU * (k * 0.618034f) + h->ph * 0.1f;
				float rr = cr * (0.85f + 0.15f * sinf(k * 3.7f));
				float ex = hx + cosf(ang) * rr, ey = hy + sinf(ang) * rr;
				us_line(c, hx, hy, ex, ey, ctx->crisp ? 0.002f : 0.0007f, 0.32f, US_MAT_ACCENT);
				us_point(c, ex, ey, ctx->crisp ? 0.002f : 0.0012f, 0.7f, US_MAT_ACCENT);
			}
			us_disc(c, hx, hy, cr * 0.12f, 0.6f, US_MAT_EARTH);
			/* onsets (and strong thermals) blow seeds free */
			int n = release + (us_rand(&st->rng) < dt * (0.05f + 0.5f * st->high_s * st->high_s) ? 1 : 0);
			for (int k = 0; k < n && h->seeds > 0.03f && nseeds < seed_cap + 4; k++, nseeds++)
				for (int j = 0; j < MAXP; j++)
					if (!st->p[j].alive) {
						part_t *q = &st->p[j];
						spawn(st, q, 0, 1, A);
						q->kind = K_SEED;
						float ang = us_randr(&st->rng, -US_PI, 0);
						q->x = (hx + cosf(ang) * cr) / A;
						q->y = hy + sinf(ang) * cr;
						q->z = us_randr(&st->rng, 0.55f, 1.0f);
						q->vx = cosf(ang) * 0.05f + 0.03f;
						q->vy = sinf(ang) * 0.05f - 0.03f;
						q->life = 1.0f;
						q->ttl = us_randr(&st->rng, 12.0f, 26.0f);
						h->seeds -= 1.0f / nh;
						break;
					}
		}
	}

	/* ---- population ---- */
	float nmax = 250.0f + 700.0f * dens;
	if (nmax > MAXP)
		nmax = MAXP;
	float amount = us_sat(0.4f + 0.8f * w->pollen + 0.2f * w->bloom + 0.3f * winter);
	int target = (int)(nmax * amount);
	int alive = 0;
	for (int i = 0; i < MAXP; i++)
		alive += st->p[i].alive;
	if (alive < target) {
		st->spawn_acc += dt * (8.0f + 0.3f * (target - alive));
		while (st->spawn_acc >= 1.0f) {
			st->spawn_acc -= 1.0f;
			for (int i = 0; i < MAXP; i++)
				if (!st->p[i].alive) {
					spawn(st, &st->p[i], winter, 0, A);
					break;
				}
		}
	} else
		st->spawn_acc = 0;

	/* ---- particles ---- */
	float lift = 0.004f + 0.03f * st->high_s;
	float breeze = 0.008f + 0.025f * wind;
	float light = (0.45f + 0.55f * day) * (0.75f + 0.25f * st->high_s);
	if (!ctx->crisp)
		c->blend = US_BLEND_ADD;
	for (int i = 0; i < MAXP; i++) {
		part_t *q = &st->p[i];
		if (!q->alive)
			continue;
		float par = 0.35f + 0.65f * q->z; /* parallax: near things move more */
		float X = q->x * A;
		/* sample wind field */
		float gxf = us_clampf(q->x * CGX - 0.5f, 0, CGX - 1.001f), gyf = us_clampf(q->y * CGY - 0.5f, 0, CGY - 1.001f);
		int gi = (int)gxf, gj = (int)gyf;
		float tx = gxf - gi, ty = gyf - gj;
		int i1 = gi + 1 < CGX ? gi + 1 : gi, j1 = gj + 1 < CGY ? gj + 1 : gj;
		float fvx = us_lerp(us_lerp(st->fvx[gj * CGX + gi], st->fvx[gj * CGX + i1], tx),
				    us_lerp(st->fvx[j1 * CGX + gi], st->fvx[j1 * CGX + i1], tx), ty);
		float fvy = us_lerp(us_lerp(st->fvy[gj * CGX + gi], st->fvy[gj * CGX + i1], tx),
				    us_lerp(st->fvy[j1 * CGX + gi], st->fvy[j1 * CGX + i1], tx), ty);
		float fall, buoy;
		switch (q->kind) {
		case K_SEED: fall = 0.010f; buoy = 1.3f; break;
		case K_SPORE: fall = 0.004f; buoy = 0.8f; break;
		case K_SNOW: fall = 0.022f; buoy = 0.25f; break;
		default: fall = 0.006f; buoy = 1.0f; break;
		}
		float tvx = (fvx + breeze * (0.6f + 0.4f * q->seedv)) * par;
		float tvy = (fvy + fall - lift * buoy) * par;
		/* thermals are patchy: noisy updraft columns */
		tvy -= lift * buoy * 0.8f * us_sat(us_noise2(X * 2.0f + t * 0.05f, 3.3f)) * par;
		float resp = q->kind == K_SEED ? 1.2f : 0.5f;
		q->vx = us_approach(q->vx, tvx, resp, dt);
		q->vy = us_approach(q->vy, tvy, resp, dt);
		q->x += q->vx * dt / A;
		q->y += q->vy * dt;
		q->rot += (q->vr + q->vx * 6.0f) * dt;
		q->rot = us_lerp(q->rot, 0.0f, dt * 0.3f); /* pappus parachutes right themselves */
		q->age += dt;
		if (q->age > q->ttl)
			q->life -= dt * 0.4f; /* settles out of the air */
		else
			q->life = us_sat(q->life + dt * 0.5f);
		int out = q->x < -0.05f || q->x > 1.05f || q->y < -0.06f || q->y > 1.02f;
		if (q->kind == K_SNOW && q->y > ground_y(q->x))
			out = 1;
		if (out || q->life < 0 || (alive > target + 20 && us_rand(&st->rng) < dt * 0.05f)) {
			q->alive = 0;
			alive--;
			continue;
		}
		X = q->x * A;
		/* sunbeam catch */
		float glint = 0;
		for (int k = 0; k < NBEAM; k++) {
			if (beam_v[k] < 0.02f)
				continue;
			float bx = st->beam_x[k] * A + bdx * (q->y + 0.02f);
			float d = fabsf(X - bx) / (st->beam_w[k] * 0.6f);
			if (d < 1.0f)
				glint += beam_v[k] * (1.0f - d * d);
		}
		float v = q->life * light * (0.35f + 0.65f * q->z) * (1.0f + 1.6f * glint);
		uint8_t mat = q->kind == K_SNOW ? US_MAT_WATER : (q->kind == K_SPORE ? US_MAT_LEAF : US_MAT_ACCENT);
		if (q->kind == K_GRAIN && glint > 0.2f)
			mat = US_MAT_LIGHT;
		switch (q->kind) {
		case K_SEED: {
			float sz = 0.006f + 0.016f * q->z * q->z;
			if (q->z < 0.3f || (ctx->crisp && sz < 0.008f))
				us_point(c, X, q->y, ctx->crisp ? 0.003f : 0.0014f, us_sat(v * 0.9f), US_MAT_ACCENT);
			else
				pappus(c, ctx->crisp, X, q->y, sz, q->rot * 0.4f, us_sat(v * (ctx->crisp ? 0.8f : 0.55f)), US_MAT_ACCENT);
			break;
		}
		case K_SPORE: {
			float r = 0.0012f + 0.0035f * q->z;
			if (ctx->crisp)
				us_point(c, X, q->y, 0.003f, us_sat(v * 0.6f), mat);
			else {
				us_ring(c, X, q->y, r, 0.0007f, us_sat(v * 0.5f), mat);
				us_point(c, X, q->y, 0.0007f, us_sat(v * 0.6f), mat);
			}
			break;
		}
		case K_SNOW: {
			float r = 0.0012f + 0.004f * q->z;
			if (ctx->crisp)
				us_disc(c, X, q->y, r * 0.8f, us_sat(v * 0.8f), mat);
			else
				us_disc(c, X, q->y, r, us_sat(v * 0.55f), mat);
			break;
		}
		default: {
			float r = 0.0009f + 0.0026f * q->z;
			if (ctx->crisp)
				us_point(c, X, q->y, 0.003f, us_sat(v * 0.55f), mat);
			else
				us_disc(c, X, q->y, r, us_sat(v * 0.85f), mat);
			break;
		}
		}
	}
	c->blend = US_BLEND_MAX;
}

const us_scene_vt us_scene_pollen = {"pollen", "Pollen & Seeds", 0.2f, create, destroy, frame};
