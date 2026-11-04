/* Understory scene: Murmuration — a starling flock wheeling over a reed bed at dusk.
 *
 * Several hundred boids (separation / alignment / cohesion, topological-ish neighbour cap, spatial grid)
 * chase a slow roaming attractor, folding into the dense shape-shifting ribbons of a real murmuration.
 *   centroid (melody brightness)  -> where the attractor wanders (left .. right), its rise and fall
 *   mid                           -> how fast the attractor wheels around its orbit (the flock banks and turns)
 *   level                         -> cohesion: loud music tightens the flock into dense knots
 *   big transients                -> a falcon stoops through the flock; a panic wave ripples outward from it,
 *                                    birds burst away and then re-form
 *   quiet / swell low             -> the flock descends and roosts in the bare trees and reeds (perched dots)
 * World: reads flock (bird count at open, slowly followed), pollen + insects (food => more birds), season,
 *        daylight; writes flock (lively music with food around draws birds in).
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAXB 820
#define GXN 72
#define GYN 36
#define NTREES 3
#define MAXTS 260 /* tree segments per tree */
#define NREEDS 150
#define MAXPERCH (NTREES * 128)

#define HORIZON 0.80f

enum { FLY = 0, LANDING, PERCHED, GONE };

typedef struct {
	float x, y, vx, vy;
	float z;     /* size / brightness jitter (pseudo depth) */
	float flap;  /* wing phase */
	float panic; /* 0..1 recently scattered */
	int state;
	int perch;
	float perch_u;
	int wave_id;
	float life; /* fade in/out */
	int leaving;
} bird_t;

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
	float A; /* aspect the positions are stored at */
	bird_t b[MAXB];
	int head[GXN * GYN];
	int next[MAXB];
	tree_t tree[NTREES];
	int nperch;
	short perch_tree[MAXPERCH], perch_seg[MAXPERCH];
	float reed_x[NREEDS], reed_h[NREEDS], reed_ph[NREEDS];
	/* attractor */
	float ax, ay, orbit_a, orbit_r, cx_s, cy_s;
	float cent_s, cent_mean, mid_s, lvl_s, quiet, roost;
	/* scatter wave + falcon */
	int wave_id;
	float wave_x, wave_y, wave_r, wave_str;
	float falcon_x, falcon_y, falcon_vx, falcon_vy, falcon_t;
	float cooldown;
	float spawn_acc;
	float wind_t;
	float pop; /* smoothed population fraction */
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x6d75726du);
	return s;
}

static void destroy(void *p) { free(p); }

/* bare tree: recursive forks; tips become perches */
static void grow_tree(st_t *st, tree_t *t, float x, float y, float ang, float len, float w, int depth)
{
	us_rng *r = &st->rng;
	if (t->n >= MAXTS || depth > 7 || len < 0.006f)
		return;
	/* gently curved limb in 2 pieces */
	float a1 = ang + us_randn(r) * (depth < 2 ? 0.04f : 0.12f);
	float xm = x + cosf(a1) * len * 0.5f / 1.7778f, ym = y + sinf(a1) * len * 0.5f;
	float a2 = a1 + us_randn(r) * (depth < 2 ? 0.04f : 0.15f);
	float xe = xm + cosf(a2) * len * 0.5f / 1.7778f, ye = ym + sinf(a2) * len * 0.5f;
	tseg_t *s = &t->s[t->n++];
	*s = (tseg_t){x, y, xm, ym, w, depth};
	if (t->n >= MAXTS)
		return;
	s = &t->s[t->n++];
	*s = (tseg_t){xm, ym, xe, ye, w * 0.85f, depth};
	int forks = depth == 0 ? 1 : (depth < 3 ? 2 + (us_rand(r) < 0.4f) : 2);
	for (int k = 0; k < forks; k++) {
		float spread = forks == 1 ? us_randn(r) * 0.08f
					  : us_randr(r, 0.25f, 0.6f) * (k == 0 ? -1.0f : 1.0f) * (k == 2 ? 0.1f : 1.0f);
		/* branches lean upward */
		float na = a2 + spread;
		na = us_lerp(na, -US_PI * 0.5f, 0.12f);
		grow_tree(st, t, xe, ye, na, len * (forks == 1 ? 0.85f : us_randr(r, 0.66f, 0.8f)), w * (forks == 1 ? 0.8f : 0.68f), depth + 1);
	}
}

static void init_scene(st_t *st, const us_ctx *ctx)
{
	us_rng *r = &st->rng;
	float xs[NTREES] = {0.1f, 0.78f, 0.93f};
	float hs[NTREES] = {0.17f, 0.12f, 0.09f};
	for (int i = 0; i < NTREES; i++) {
		st->tree[i].n = 0;
		grow_tree(st, &st->tree[i], xs[i] + us_randr(r, -0.02f, 0.02f), HORIZON + 0.01f, -US_PI * 0.5f, hs[i],
			  0.006f * hs[i] / 0.14f, 0);
	}
	st->nperch = 0;
	for (int i = 0; i < NTREES; i++)
		for (int k = 0; k < st->tree[i].n && st->nperch < MAXPERCH; k++)
			if (st->tree[i].s[k].depth >= 3) {
				st->perch_tree[st->nperch] = (short)i;
				st->perch_seg[st->nperch] = (short)k;
				st->nperch++;
			}
	for (int k = 0; k < NREEDS; k++) {
		st->reed_x[k] = us_rand(r);
		float h = us_rand(r);
		st->reed_h[k] = 0.04f + 0.09f * h * h;
		st->reed_ph[k] = us_rand(r) * 40.0f;
	}
	st->A = ctx->aspect;
	st->ax = ctx->aspect * 0.5f;
	st->ay = 0.35f;
	st->cx_s = ctx->aspect * 0.5f;
	st->cy_s = 0.35f;
	st->orbit_r = 0.12f;
	st->cent_s = 0.5f;
	st->cent_mean = 0.5f;
}

static void spawn_bird(st_t *st, bird_t *b, float A, int from_edge)
{
	us_rng *r = &st->rng;
	memset(b, 0, sizeof(*b));
	if (from_edge) {
		int left = us_rand(r) < 0.5f;
		b->x = left ? -0.03f : A + 0.03f;
		b->y = us_randr(r, 0.1f, 0.5f);
		b->vx = left ? 0.22f : -0.22f;
		b->vy = us_randr(r, -0.03f, 0.03f);
		b->life = 1.0f;
	} else {
		float ang = us_rand(r) * US_TAU, rad = sqrtf(us_rand(r)) * 0.12f;
		b->x = st->ax + cosf(ang) * rad * 1.8f;
		b->y = st->ay + sinf(ang) * rad * 0.6f;
		b->vx = 0.18f + us_randn(r) * 0.02f;
		b->vy = us_randn(r) * 0.02f;
		b->life = 1.0f;
	}
	b->z = us_randr(r, 0.6f, 1.25f);
	b->flap = us_rand(r) * US_TAU;
	b->state = FLY;
	b->wave_id = st->wave_id;
}

static void perch_pos(const st_t *st, int p, float u, float A, float sway, float *x, float *y)
{
	const tseg_t *s = &st->tree[st->perch_tree[p]].s[st->perch_seg[p]];
	float h = (HORIZON - s->y1) / 0.15f;
	*x = us_lerp(s->x0, s->x1, u) * A + sway * h * h;
	*y = us_lerp(s->y0, s->y1, u) - 0.0025f;
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
	if (dt > 0.05f)
		dt = 0.05f;

	if (!st->inited) {
		init_scene(st, ctx);
		/* flock size at open remembers the world */
		float nmax = 300.0f + 520.0f * dens;
		int n0 = (int)(nmax * us_sat(0.35f + 0.8f * w->flock));
		for (int i = 0; i < n0 && i < MAXB; i++)
			spawn_bird(st, &st->b[i], A, 0);
		for (int i = n0; i < MAXB; i++)
			st->b[i].state = GONE;
		st->pop = us_sat(0.35f + 0.8f * w->flock);
		st->inited = 1;
	}
	/* aspect changed: rescale stored x so the flock keeps its place */
	if (fabsf(A - st->A) > 1e-4f) {
		float k = A / st->A;
		for (int i = 0; i < MAXB; i++)
			st->b[i].x *= k;
		st->ax *= k;
		st->cx_s *= k;
		st->falcon_x *= k;
		st->wave_x *= k;
		st->A = A;
	}

	/* ---- audio shaping ---- */
	st->cent_s = us_approach(st->cent_s, a->centroid, 1.5f, rdt);
	/* self-centring: the melody steers relative to the track's own register */
	if (a->active > 0.3f)
		st->cent_mean = us_approach(st->cent_mean, a->centroid, 20.0f, rdt);
	st->mid_s = us_approach(st->mid_s, us_sat(a->mid * 0.8f + a->lowmid * 0.3f), 0.8f, rdt);
	st->lvl_s = us_approach(st->lvl_s, us_sat(a->level), 0.5f, rdt);
	float q_in = 1.0f - us_sat(a->level * 2.0f + a->swell * 0.25f + a->onset * 0.3f);
	st->quiet = us_approach(st->quiet, q_in, q_in > st->quiet ? 2.5f : 1.0f, rdt);
	float roost_t = us_smooth(0.3f, 0.7f, st->quiet);
	st->roost = us_approach(st->roost, roost_t, 2.0f, rdt);
	float roost = st->roost;
	float wind = us_sat(0.2f * w->wind + 0.7f * a->high + 0.3f * a->air);
	st->wind_t += dt * (0.15f + 0.6f * wind);

	/* ---- world ---- */
	float food = us_sat(0.6f * w->pollen + 0.6f * w->insects);
	us_world_add(w, &w->flock, 0.004f * a->active * st->lvl_s * (0.3f + food) * (1.0f - winter), dt);

	/* ---- population follows the world's flock + food, slowly ---- */
	float nmax = 300.0f + 520.0f * dens;
	if (nmax > MAXB)
		nmax = MAXB;
	float pop_t = us_sat((0.35f + 0.8f * w->flock + 0.25f * food) * (1.0f - 0.5f * winter));
	st->pop = us_approach(st->pop, pop_t, 20.0f, dt);
	int target = (int)(nmax * st->pop);
	int count = 0, grounded = 0;
	for (int i = 0; i < MAXB; i++) {
		if (st->b[i].state != GONE && !st->b[i].leaving)
			count++;
		if (st->b[i].state == PERCHED || st->b[i].state == LANDING)
			grounded++;
	}
	/* even at full roost a restless knot keeps wheeling over the trees */
	int may_land = grounded < (int)(count * 0.82f);
	if (count < target) {
		st->spawn_acc += dt * (4.0f + 0.1f * (target - count));
		while (st->spawn_acc >= 1.0f) {
			st->spawn_acc -= 1.0f;
			for (int i = 0; i < MAXB; i++)
				if (st->b[i].state == GONE) {
					/* small groups stream in from the edges */
					spawn_bird(st, &st->b[i], A, 1);
					break;
				}
		}
	} else if (count > target + 6) {
		st->spawn_acc += dt * (2.0f + 0.05f * (count - target));
		while (st->spawn_acc >= 1.0f) {
			st->spawn_acc -= 1.0f;
			for (int i = 0; i < MAXB; i++)
				if (st->b[i].state == FLY && !st->b[i].leaving) {
					st->b[i].leaving = 1;
					break;
				}
		}
	} else
		st->spawn_acc = 0;

	/* ---- attractor: wheels on an orbit whose centre wanders with the melody ---- */
	{
		float tx = A * (0.22f + 0.56f * us_sat(0.5f + (st->cent_s - st->cent_mean) * 4.0f)) + 0.22f * A * us_noise2(t * 0.03f, 1.7f);
		float ty = 0.30f + 0.12f * us_noise2(t * 0.04f, 7.3f) - 0.08f * st->lvl_s;
		/* roosting: the flock sinks toward the trees on the left */
		tx = us_lerp(tx, A * 0.16f, roost * 0.7f);
		ty = us_lerp(ty, HORIZON - 0.17f, roost);
		st->cx_s = us_approach(st->cx_s, tx, 3.0f, dt);
		st->cy_s = us_approach(st->cy_s, ty, 3.0f, dt);
		float wheel = 0.35f + 1.6f * st->mid_s + 0.6f * us_noise2(t * 0.11f, 3.1f);
		st->orbit_a += dt * wheel * (us_noise2(t * 0.017f, 9.0f) > -0.2f ? 1.0f : -1.0f);
		st->orbit_r = us_lerp(0.05f, 0.22f, us_sat(0.5f + 0.5f * us_noise2(t * 0.05f, 5.5f))) * (1.0f - 0.6f * roost);
		st->ax = st->cx_s + cosf(st->orbit_a) * st->orbit_r * 1.6f;
		st->ay = st->cy_s + sinf(st->orbit_a * 1.3f) * st->orbit_r * 0.55f;
	}

	/* ---- scatter waves from big transients ---- */
	st->cooldown -= rdt;
	if (a->beat && a->onset > 0.6f && st->cooldown <= 0 && st->lvl_s > 0.25f && roost < 0.5f &&
	    us_rand(&st->rng) < 0.35f + 0.5f * a->kick) {
		/* pick a bird near the flock edge as the strike point */
		int pick = -1;
		for (int tries = 0; tries < 20 && pick < 0; tries++) {
			int i = (int)(us_rand(&st->rng) * MAXB);
			if (st->b[i].state == FLY)
				pick = i;
		}
		if (pick >= 0) {
			st->wave_id++;
			st->wave_x = st->b[pick].x;
			st->wave_y = st->b[pick].y;
			st->wave_r = 0;
			st->wave_str = us_sat(0.5f + 0.6f * a->onset * (0.5f + a->kick));
			float fa = us_randr(&st->rng, 0.3f, 1.2f) * (us_rand(&st->rng) < 0.5f ? 1 : -1) + US_PI * 0.5f;
			st->falcon_vx = cosf(fa) * 0.75f;
			st->falcon_vy = fabsf(sinf(fa)) * 0.55f;
			st->falcon_x = st->wave_x - st->falcon_vx * 0.35f;
			st->falcon_y = st->wave_y - st->falcon_vy * 0.35f;
			st->falcon_t = 1.4f;
			st->cooldown = us_randr(&st->rng, 2.5f, 5.0f);
		}
	}
	if (st->wave_r < 2.0f)
		st->wave_r += dt * 0.55f;

	/* ---- spatial grid ---- */
	const float R = 0.045f;  /* neighbour radius */
	const float RS = 0.022f; /* separation radius */
	float cw = A / GXN, ch = 1.0f / GYN;
	if (cw < R)
		cw = R;
	if (ch < R)
		ch = R;
	for (int k = 0; k < GXN * GYN; k++)
		st->head[k] = -1;
	for (int i = 0; i < MAXB; i++) {
		bird_t *b = &st->b[i];
		if (b->state != FLY)
			continue;
		int gx = (int)(b->x / cw), gy = (int)(b->y / ch);
		gx = gx < 0 ? 0 : (gx >= GXN ? GXN - 1 : gx);
		gy = gy < 0 ? 0 : (gy >= GYN ? GYN - 1 : gy);
		int k = gy * GXN + gx;
		st->next[i] = st->head[k];
		st->head[k] = i;
	}

	float coh_w = 0.25f + 1.4f * st->lvl_s;
	float ali_w = 2.2f;
	float sep_w = 0.4f;
	float att_w = 0.55f + 0.5f * st->lvl_s;
	float vmax = 0.24f + 0.12f * st->lvl_s - 0.08f * roost, vmin = 0.11f - 0.04f * roost;

	/* ---- sky + horizon (drawn first, birds go over) ---- */
	float day = w->daylight;
	{
		/* low sun / moon disc near the horizon, cloud bars across it */
		float sx = A * 0.66f, sy = HORIZON - 0.06f - 0.12f * day;
		float sr = 0.075f;
		float sv = 0.18f + 0.25f * day;
		us_ring(c, sx, sy, sr, ctx->crisp ? 0.006f : 0.0014f, sv * 1.6f, US_MAT_LIGHT);
		for (int k = 0; k < 7; k++) {
			float yy = sy - sr * 0.8f + k * sr * 0.32f + 0.004f * sinf(t * 0.1f + k);
			float hw = sr * (1.6f + 0.9f * us_noise2(k * 1.3f, t * 0.02f)) + 0.06f * k;
			float xo = 0.03f * us_noise2(k * 2.1f, t * 0.015f);
			us_line(c, sx - hw + xo, yy, sx + hw * 0.7f + xo, yy, ctx->crisp ? 0.003f : 0.0009f,
				0.12f + 0.12f * day, US_MAT_BG);
		}
		/* horizon + distant hatching of the far bank */
		us_line(c, 0, HORIZON, A, HORIZON, ctx->crisp ? 0.003f : 0.0012f, 0.38f, US_MAT_EARTH);
		for (int k = 1; k < (ctx->crisp ? 3 : 4); k++) {
			float yy = HORIZON + k * k * 0.006f;
			us_line(c, 0, yy, A, yy, ctx->crisp ? 0.003f : 0.0008f, 0.1f + 0.03f * k, US_MAT_WATER);
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
			us_line(c, x0, s->y0, x1, s->y1, wd, 0.5f - 0.03f * s->depth, US_MAT_STEM);
		}
	}

	/* ---- birds ---- */
	c->blend = US_BLEND_ADD;
	float bv = ctx->crisp ? 0.3f : 0.4f;
	for (int i = 0; i < MAXB; i++) {
		bird_t *b = &st->b[i];
		if (b->state == GONE)
			continue;
		if (b->state == PERCHED) {
			if (us_rand(&st->rng) < dt * 0.6f * (1.0f - roost) * (1.0f - roost) + (st->wave_r < 0.1f ? 0.02f : 0))
				b->state = FLY, b->vy = -0.15f, b->vx = us_randr(&st->rng, -0.1f, 0.1f);
			float px, py;
			perch_pos(st, b->perch, b->perch_u, A, tree_sway, &px, &py);
			b->x = px;
			b->y = py;
			if (ctx->crisp)
				us_point(c, px, py - 0.003f, 0.003f, 0.6f, US_MAT_ACCENT);
			else {
				us_disc(c, px, py - 0.0022f, 0.0017f * b->z, 0.55f, US_MAT_ACCENT);
				us_line(c, px, py - 0.002f, px + 0.0035f, py - 0.0005f, 0.0008f, 0.3f, US_MAT_ACCENT);
			}
			continue;
		}

		float ax = 0, ay = 0;
		if (b->state == FLY) {
			int gx = (int)(b->x / cw), gy = (int)(b->y / ch);
			float sx = 0, sy = 0, avx = 0, avy = 0, cxs = 0, cys = 0;
			int n = 0;
			for (int dy = -1; dy <= 1 && n < 14; dy++)
				for (int dx = -1; dx <= 1 && n < 14; dx++) {
					int xx = gx + dx, yy = gy + dy;
					if (xx < 0 || yy < 0 || xx >= GXN || yy >= GYN)
						continue;
					for (int j = st->head[yy * GXN + xx]; j >= 0 && n < 14; j = st->next[j]) {
						if (j == i)
							continue;
						bird_t *o = &st->b[j];
						float ddx = o->x - b->x, ddy = o->y - b->y;
						float d2 = ddx * ddx + ddy * ddy;
						if (d2 > R * R)
							continue;
						n++;
						avx += o->vx;
						avy += o->vy;
						cxs += ddx;
						cys += ddy;
						if (d2 < RS * RS) {
							float d = sqrtf(d2) + 1e-4f;
							float k = (RS - d) / (RS * d);
							sx -= ddx * k;
							sy -= ddy * k;
						}
					}
				}
			float calm_k = 1.0f - 0.85f * b->panic;
			if (n > 0) {
				avx /= n;
				avy /= n;
				ax += (avx - b->vx) * ali_w * calm_k;
				ay += (avy - b->vy) * ali_w * calm_k;
				ax += cxs / n * coh_w * 8.0f * calm_k;
				ay += cys / n * coh_w * 8.0f * calm_k;
			}
			ax += sx * sep_w * 8.0f;
			ay += sy * sep_w * 8.0f;
			/* attractor (global shape) */
			float tx = st->ax - b->x, ty = st->ay - b->y;
			float td = sqrtf(tx * tx + ty * ty) + 1e-3f;
			/* free inside the flock's home range, pulled back hard outside it: the shape stays loose */
			float home = 0.10f + 0.08f * (1.0f - st->lvl_s);
			float pull = att_w * (0.15f + 2.2f * us_smooth(home * 0.5f, home * 2.2f, td)) * calm_k;
			ax += tx / td * pull;
			ay += ty / td * pull * 1.1f;
			/* leavers head for the nearest edge */
			if (b->leaving) {
				ax += (b->x < A * 0.5f ? -1.0f : 1.0f) * 1.2f;
				ay -= 0.2f;
			} else {
				/* soft walls */
				if (b->x < 0.04f * A)
					ax += (0.04f * A - b->x) * 12.0f;
				if (b->x > 0.96f * A)
					ax -= (b->x - 0.96f * A) * 12.0f;
				if (b->y < 0.05f)
					ay += (0.05f - b->y) * 14.0f;
				if (b->y > HORIZON - 0.04f)
					ay -= (b->y - (HORIZON - 0.04f)) * 14.0f;
			}
			/* scatter wave: hit once per wave as the front passes */
			if (b->wave_id != st->wave_id) {
				float wx = b->x - st->wave_x, wy = b->y - st->wave_y;
				float wd = sqrtf(wx * wx + wy * wy) + 1e-4f;
				if (wd < st->wave_r) {
					b->wave_id = st->wave_id;
					float k = st->wave_str * 0.30f * expf(-wd * 3.0f);
					b->vx += wx / wd * k;
					b->vy += wy / wd * k;
					b->panic = us_sat(b->panic + st->wave_str * expf(-wd * 2.5f));
				}
			}
			/* roost: some birds peel off and land in the trees */
			if (roost > 0.35f && st->nperch > 0 && !b->leaving && may_land &&
			    us_rand(&st->rng) < dt * 0.45f * roost * roost) {
				b->state = LANDING;
				b->perch = (int)(us_rand(&st->rng) * st->nperch);
				b->perch_u = us_randr(&st->rng, 0.2f, 1.0f);
			}
		} else { /* LANDING */
			float px, py;
			perch_pos(st, b->perch, b->perch_u, A, tree_sway, &px, &py);
			float tx = px - b->x, ty = py - b->y;
			float td = sqrtf(tx * tx + ty * ty) + 1e-4f;
			float want = us_clampf(td * 2.5f, 0.02f, 0.2f);
			ax += (tx / td * want - b->vx) * 3.0f;
			ay += (ty / td * want - b->vy) * 3.0f;
			if (td < 0.006f)
				b->state = PERCHED;
			if (roost < 0.2f)
				b->state = FLY;
		}
		b->panic = us_approach(b->panic, 0, 0.9f, dt);
		b->vx += ax * dt;
		b->vy += ay * dt;
		float sp = sqrtf(b->vx * b->vx + b->vy * b->vy) + 1e-5f;
		float hi = vmax * (1.0f + 0.8f * b->panic), lo = b->state == LANDING ? 0.0f : vmin;
		if (sp > hi) {
			b->vx *= hi / sp;
			b->vy *= hi / sp;
		} else if (sp < lo) {
			b->vx *= lo / sp;
			b->vy *= lo / sp;
		}
		float ox = b->x, oy = b->y;
		b->x += b->vx * dt;
		b->y += b->vy * dt;
		if (b->leaving && (b->x < -0.05f || b->x > A + 0.05f)) {
			b->state = GONE;
			continue;
		}

		/* draw: a tiny chevron pointing along the velocity, wings flicking */
		b->flap += dt * (14.0f + 10.0f * b->panic) * (0.8f + 0.4f * b->z);
		sp = sqrtf(b->vx * b->vx + b->vy * b->vy) + 1e-5f;
		float hx = b->vx / sp, hy = b->vy / sp;
		float L = 0.0045f * b->z;
		float open = 0.55f + 0.35f * sinf(b->flap);
		float v = bv * (0.7f + 0.3f * b->z);
		if (ctx->crisp) {
			us_point(c, b->x, b->y, 0.003f, v, US_MAT_ACCENT);
		} else {
			float bx = b->x - hx * L * 0.3f, by = b->y - hy * L * 0.3f;
			float wx1 = b->x - hx * L * 0.9f + -hy * L * open, wy1 = b->y - hy * L * 0.9f + hx * L * open;
			float wx2 = b->x - hx * L * 0.9f - -hy * L * open, wy2 = b->y - hy * L * 0.9f - hx * L * open;
			us_line(c, wx1, wy1, bx, by, 0.0011f, v, US_MAT_ACCENT);
			us_line(c, bx, by, wx2, wy2, 0.0011f, v, US_MAT_ACCENT);
			/* faint motion streak */
			if (fabsf(ox - b->x) + fabsf(oy - b->y) < 0.05f)
				us_line(c, ox, oy, b->x, b->y, 0.0008f, v * 0.25f, US_MAT_ACCENT);
		}
	}
	c->blend = US_BLEND_MAX;

	/* ---- falcon ---- */
	if (st->falcon_t > 0) {
		st->falcon_t -= dt;
		st->falcon_x += st->falcon_vx * dt;
		st->falcon_y += st->falcon_vy * dt;
		float sp = sqrtf(st->falcon_vx * st->falcon_vx + st->falcon_vy * st->falcon_vy) + 1e-5f;
		float hx = st->falcon_vx / sp, hy = st->falcon_vy / sp;
		float L = 0.014f, fx = st->falcon_x, fy = st->falcon_y;
		float fv = 0.85f * us_sat(st->falcon_t * 2.0f);
		/* swept-back wings of a stooping falcon */
		float w1x = fx - hx * L + -hy * L * 0.55f, w1y = fy - hy * L + hx * L * 0.55f;
		float w2x = fx - hx * L - -hy * L * 0.55f, w2y = fy - hy * L - hx * L * 0.55f;
		if (ctx->crisp) {
			us_tri(c, fx, fy, w1x, w1y, fx - hx * L * 0.5f, fy - hy * L * 0.5f, fv, US_MAT_BLOOM);
			us_tri(c, fx, fy, w2x, w2y, fx - hx * L * 0.5f, fy - hy * L * 0.5f, fv, US_MAT_BLOOM);
		} else {
			us_line(c, fx, fy, w1x, w1y, 0.0016f, fv, US_MAT_BLOOM);
			us_line(c, fx, fy, w2x, w2y, 0.0016f, fv, US_MAT_BLOOM);
			us_line(c, fx, fy, fx - hx * L * 1.2f, fy - hy * L * 1.2f, 0.0014f, fv, US_MAT_BLOOM);
		}
	}

	/* ---- reed bed in front ---- */
	{
		int nr = (int)(NREEDS * (0.5f + 0.5f * dens));
		for (int k = 0; k < nr; k++) {
			float x0 = st->reed_x[k] * A, h = st->reed_h[k];
			float sw = (us_noise2(x0 * 1.3f - st->wind_t * 0.8f, st->reed_ph[k]) * 0.6f + 0.3f) * (0.15f + 0.5f * wind) +
				   0.25f * us_noise2(st->reed_ph[k], 0.3f);
			float base = 1.01f, top = 0.95f - h * 1.3f;
			float pts[8];
			for (int q = 0; q < 4; q++) {
				float u = q / 3.0f;
				pts[2 * q] = x0 + sw * h * u * u * 2.5f;
				pts[2 * q + 1] = base - (base - top) * u;
			}
			us_polyline(c, pts, 4, ctx->crisp ? 0.002f : 0.001f, 0.3f + 0.15f * (h / 0.13f),
				    ctx->crisp ? US_MAT_EARTH : US_MAT_LEAF);
			if (k % 3 == 0) { /* feathery seed plume: a few short hairs fanning off the tip */
				float px = pts[6], py = pts[7];
				for (int q = 0; q < 4; q++) {
					float aa = -1.2f + 0.45f * q + sw * 2.0f;
					float ll = 0.008f + 0.002f * q;
					us_line(c, px, py, px + sinf(aa) * ll * 0.6f, py - cosf(aa) * ll * 0.4f + ll * 0.5f,
						ctx->crisp ? 0.002f : 0.0009f, 0.3f, US_MAT_EARTH);
				}
			}
		}
	}
}

const us_scene_vt us_scene_murmuration = {"murmuration", "Murmuration", 0.18f, create, destroy, frame};
