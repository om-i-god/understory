/* Understory scene: Meadow — plants grow & bloom with the bass, sway with the highs.
 *
 * Each plant has a seeded genome (a branching skeleton) and a life cycle:
 *   SPROUT/GROW -> BLOOM -> SEED (petals fall) -> WITHER -> reseed nearby with a new genome.
 * Bass drives growth and opens flowers; highs + world wind drive sway; kicks nudge blooms.
 * World: reads moisture, mycelium (growth speed), season (bare winter, red autumn leaf-fall), daylight;
 *        writes growth + bloom + pollen.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAX_PLANTS 16
#define MAX_SEGS 72
#define MAX_PARTS 400
#define MAX_BLADES 260

enum { GROW = 0, BLOOM, SEED, WITHER };

typedef struct {
	int parent;   /* -1 = root */
	float rel;    /* angle relative to parent */
	float len;    /* units of plant height */
	float birth;  /* growth threshold where this segment starts */
	float span;   /* growth range over which it extends */
	float width;
	uint8_t depth, leaf, tip;
	float leaf_side, petals, petal_rot, flower_size;
} seg_t;

typedef struct {
	float x;      /* 0..1 across width */
	float height; /* normalized */
	float g;      /* growth 0..1 */
	float open;   /* flower openness 0..1 */
	float seedness;
	int phase;
	float phase_t, bloom_len;
	float hue_bias; /* 0..1 picks ACCENT vs BLOOM for flowers */
	int nseg;
	seg_t seg[MAX_SEGS];
	/* scratch per frame */
	float ex[MAX_SEGS], ey[MAX_SEGS], ea[MAX_SEGS];
} plant_t;

typedef struct {
	float x, y, vx, vy, rot, vr, life, size;
	uint8_t mat, kind; /* kind 0 petal, 1 leaf, 2 seed */
} part_t;

typedef struct {
	us_rng rng;
	int nplants;
	plant_t pl[MAX_PLANTS];
	part_t parts[MAX_PARTS];
	int nparts;
	float blade_x[MAX_BLADES], blade_h[MAX_BLADES], blade_ph[MAX_BLADES];
	int inited;
	float bass_env;
	float wind_t;
} st_t;

/* ---- genome ---------------------------------------------------------- */

static int add_seg(plant_t *p, int parent, float rel, float len, float width, int depth)
{
	if (p->nseg >= MAX_SEGS)
		return -1;
	seg_t *s = &p->seg[p->nseg];
	memset(s, 0, sizeof(*s));
	s->parent = parent;
	s->rel = rel;
	s->len = len;
	s->width = width;
	s->depth = (uint8_t)depth;
	return p->nseg++;
}

static void grow_branch(plant_t *p, us_rng *r, int parent, float rel, float len, float width, int depth, int style)
{
	int chain = depth == 0 ? 5 + (int)(us_rand(r) * 4) : 3 + (int)(us_rand(r) * 3);
	int prev = parent;
	float curl = us_randr(r, -0.09f, 0.09f);
	for (int k = 0; k < chain; k++) {
		float a = (k == 0 ? rel : curl + us_randn(r) * 0.05f);
		int id = add_seg(p, prev, a, len / chain * us_randr(r, 0.8f, 1.2f), width * (1.0f - 0.18f * k), depth);
		if (id < 0)
			return;
		seg_t *s = &p->seg[id];
		s->leaf = us_rand(r) < (style == 2 ? 0.9f : 0.65f);
		s->leaf_side = us_rand(r) < 0.5f ? -1.0f : 1.0f;
		prev = id;
		/* side branches */
		if (depth < 3 && k > 0 && k < chain - 1 && us_rand(r) < (style == 1 ? 0.6f : 0.38f) * (depth == 0 ? 1.0f : 0.7f)) {
			float side = us_rand(r) < 0.5f ? -1.0f : 1.0f;
			grow_branch(p, r, id, side * us_randr(r, 0.35f, 0.9f), len * us_randr(r, 0.45f, 0.7f), width * 0.65f,
				    depth + 1, style);
		}
	}
	if (prev >= 0) {
		seg_t *t = &p->seg[prev];
		t->tip = 1;
		t->petals = (float)(4 + (int)(us_rand(r) * 5));
		t->petal_rot = us_rand(r) * US_TAU;
		t->flower_size = us_randr(r, 0.6f, 1.2f) * (depth == 0 ? 1.4f : 1.0f);
	}
}

static void compute_births(plant_t *p)
{
	/* path length from root to segment start, normalized */
	float start[MAX_SEGS], maxend = 1e-3f;
	for (int i = 0; i < p->nseg; i++) {
		seg_t *s = &p->seg[i];
		start[i] = s->parent < 0 ? 0 : start[s->parent] + p->seg[s->parent].len;
		float e = start[i] + s->len;
		if (e > maxend)
			maxend = e;
	}
	for (int i = 0; i < p->nseg; i++) {
		p->seg[i].birth = start[i] / maxend * 0.9f;
		p->seg[i].span = p->seg[i].len / maxend * 0.9f + 0.02f;
	}
}

static void new_plant(st_t *st, plant_t *p, float x, float g0)
{
	us_rng *r = &st->rng;
	memset(p, 0, sizeof(*p));
	p->x = x;
	p->height = us_randr(r, 0.32f, 0.62f);
	p->g = g0;
	p->phase = GROW;
	p->hue_bias = us_rand(r);
	p->bloom_len = us_randr(r, 45.0f, 95.0f);
	int style = (int)(us_rand(r) * 3);
	grow_branch(p, r, -1, us_randn(r) * 0.05f, 1.0f, 1.0f, 0, style);
	compute_births(p);
}

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x6d656164u);
	return s;
}

static void destroy(void *p) { free(p); }

static void spawn(st_t *st, float x, float y, float vx, float vy, float size, uint8_t mat, uint8_t kind)
{
	if (st->nparts >= MAX_PARTS)
		return;
	part_t *q = &st->parts[st->nparts++];
	q->x = x;
	q->y = y;
	q->vx = vx;
	q->vy = vy;
	q->rot = us_rand(&st->rng) * US_TAU;
	q->vr = us_randr(&st->rng, -3, 3);
	q->life = 1.0f;
	q->size = size;
	q->mat = mat;
	q->kind = kind;
}

static float ground_y(float x, float aspect)
{
	(void)aspect;
	return 0.88f + 0.025f * us_noise2(x * 1.3f, 4.2f) + 0.01f * us_noise2(x * 5.0f, 1.1f);
}

/* petal: filled lens in crisp mode, outlined lens in ink mode */
static void petal(us_canvas *c, int crisp, float cx, float cy, float ang, float len, float wid, float v, uint8_t mat)
{
	float dx = cosf(ang), dy = sinf(ang);
	float tx = cx + dx * len, ty = cy + dy * len;
	float mx = cx + dx * len * 0.5f, my = cy + dy * len * 0.5f;
	float nx = -dy * wid, ny = dx * wid;
	if (crisp) {
		us_tri(c, cx, cy, mx + nx, my + ny, tx, ty, v, mat);
		us_tri(c, cx, cy, mx - nx, my - ny, tx, ty, v, mat);
	} else {
		float pts[] = {cx, cy, mx + nx, my + ny, tx, ty, mx - nx, my - ny, cx, cy};
		us_polyline(c, pts, 5, 0.0012f, v, mat);
	}
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *st = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, A = ctx->aspect;
	float dens = ctx->p->density;
	float winter = us_world_season_w(w, 3), autumn = us_world_season_w(w, 2);
	float spring = us_world_season_w(w, 0), summer = us_world_season_w(w, 1);

	int want = 5 + (int)(dens * 10.0f);
	if (want > MAX_PLANTS)
		want = MAX_PLANTS;
	if (!st->inited || want != st->nplants) {
		for (int i = st->inited ? st->nplants : 0; i < want; i++) {
			float x = (i + 0.5f) / want + us_randr(&st->rng, -0.3f, 0.3f) / want;
			/* stagger initial growth from the world's vegetation so returning to the meadow remembers it */
			float g0 = us_sat(w->growth * us_randr(&st->rng, 0.3f, 2.2f));
			new_plant(st, &st->pl[i], x, g0);
			/* staggered life cycles: some plants are already flowering when the scene opens */
			if (g0 >= 1.0f || us_rand(&st->rng) < w->bloom) {
				st->pl[i].g = 1.0f;
				st->pl[i].phase = BLOOM;
				st->pl[i].phase_t = us_randr(&st->rng, 0, st->pl[i].bloom_len * 0.6f);
			}
		}
		if (!st->inited) {
			for (int b = 0; b < MAX_BLADES; b++) {
				st->blade_x[b] = us_rand(&st->rng);
				st->blade_h[b] = us_randr(&st->rng, 0.3f, 1.0f);
				st->blade_ph[b] = us_rand(&st->rng) * 10;
			}
		}
		st->nplants = want;
		st->inited = 1;
	}

	st->bass_env = us_approach(st->bass_env, us_sat(a->bass * 0.8f + a->kick * 0.6f), 0.08f, ctx->real_dt);
	float wind = us_sat(0.15f * w->wind + 0.9f * a->high + 0.4f * a->air);
	st->wind_t += dt * (0.4f + 1.6f * wind);

	/* world contributions */
	us_world_add(w, &w->growth, 0.004f * a->bass * (1 - winter), dt);

	/* ---- ground ---- */
	{
		float pts[2 * 65];
		for (int i = 0; i <= 64; i++) {
			float x = A * i / 64.0f;
			pts[2 * i] = x;
			pts[2 * i + 1] = ground_y(x / A, A);
		}
		us_polyline(c, pts, 65, 0.0016f, 0.55f + 0.25f * winter, US_MAT_EARTH);
		if (ctx->crisp) /* solid earth below the line reads better in pixel styles */
			for (int i = 0; i < 64; i++) {
				float x0 = pts[2 * i], y0 = pts[2 * i + 1], x1 = pts[2 * i + 2], y1 = pts[2 * i + 3];
				us_tri(c, x0, y0, x1, y1, x0, 1.01f, 0.18f, US_MAT_EARTH);
				us_tri(c, x1, y1, x1, 1.01f, x0, 1.01f, 0.18f, US_MAT_EARTH);
			}
	}

	/* ---- sun / moon (world day) ---- */
	{
		float ph = w->day_phase;
		float cx = A * (0.08f + 0.84f * ph);
		float cy;
		cy = 0.78f + cosf(ph * US_TAU) * 0.55f;
		if (cy < 0.78f) {
			if (w->daylight > 0.35f)
				us_ring(c, cx, cy, 0.035f + 0.004f * st->bass_env, 0.0015f, 0.55f, US_MAT_LIGHT);
			else {
				/* crescent: ring arc pair */
				us_arc(c, cx, cy, 0.028f, -1.9f, 1.9f, 0.0015f, 0.5f, US_MAT_LIGHT);
				us_arc(c, cx + 0.01f, cy, 0.024f, -1.6f, 1.6f, 0.0012f, 0.35f, US_MAT_LIGHT);
			}
		}
	}

	/* ---- grass ---- */
	{
		int nb = (int)(MAX_BLADES * (0.25f + 0.75f * dens) * (0.2f + 0.8f * us_sat(w->growth + 0.2f)));
		float gh = 0.035f * (0.4f + 0.8f * w->growth) * (1.0f - 0.5f * winter);
		for (int b = 0; b < nb; b++) {
			float x = st->blade_x[b] * A;
			float gy = ground_y(st->blade_x[b], A);
			float h = gh * st->blade_h[b] * (1.0f + 0.25f * st->bass_env);
			float sw = (us_noise2(x * 2.0f - st->wind_t * 0.8f, st->blade_ph[b]) * 0.7f + 0.5f) * (0.15f + 1.3f * wind);
			float pts[8];
			for (int k = 0; k < 4; k++) {
				float t = k / 3.0f;
				pts[2 * k] = x + sw * h * t * t * 0.9f;
				pts[2 * k + 1] = gy - h * t;
			}
			us_polyline(c, pts, 4, 0.001f, 0.35f + 0.2f * st->blade_h[b], US_MAT_LEAF);
		}
	}

	/* ---- plants ---- */
	float grow_k = 0.06f * (0.35f + w->moisture) * (0.6f + 1.2f * w->mycelium) * (1.0f - 0.85f * winter);
	for (int i = 0; i < st->nplants; i++) {
		plant_t *p = &st->pl[i];
		p->phase_t += dt;
		float drive = 0.12f + 1.1f * a->bass + 0.6f * a->kick + 0.2f * a->level;
		switch (p->phase) {
		case GROW:
			p->g += dt * grow_k * drive;
			if (p->g >= 1.0f) {
				p->g = 1.0f;
				p->phase = BLOOM;
				p->phase_t = 0;
			}
			if (winter > 0.7f && p->phase_t > 20.0f && us_rand(&st->rng) < dt * 0.01f) {
				p->phase = WITHER;
				p->phase_t = 0;
			}
			break;
		case BLOOM: {
			float season_ok = spring + summer + 0.4f * autumn;
			float target = us_sat((0.25f + 0.85f * st->bass_env) * (0.3f + 0.7f * season_ok));
			p->open = us_approach(p->open, target, target > p->open ? 0.25f : 1.2f, ctx->real_dt);
			us_world_add(w, &w->bloom, 0.002f * p->open, dt);
			if (p->phase_t > p->bloom_len * (1.2f - 0.5f * a->swell)) {
				p->phase = SEED;
				p->phase_t = 0;
			}
			break;
		}
		case SEED:
			p->open = us_approach(p->open, 0, 3.0f, dt);
			p->seedness = us_sat(p->phase_t / 8.0f);
			us_world_add(w, &w->pollen, 0.004f, dt);
			if (p->phase_t > 12.0f) {
				p->phase = WITHER;
				p->phase_t = 0;
			}
			break;
		case WITHER:
			p->g -= dt * 0.06f;
			p->open = 0;
			if (p->g <= 0.0f) {
				float nx = us_sat(p->x + us_randr(&st->rng, -0.06f, 0.06f));
				new_plant(st, p, nx, 0.0f);
			}
			break;
		}

		float gx = p->x * A;
		float gy = ground_y(p->x, A);
		float H = p->height;
		float wither = p->phase == WITHER ? us_sat(1.0f - p->g) : 0.0f;
		float vbase = 0.85f - 0.4f * wither;

		for (int k = 0; k < p->nseg; k++) {
			seg_t *s = &p->seg[k];
			float vis = us_sat((p->g - s->birth) / s->span);
			float px, py, pa;
			if (s->parent < 0) {
				px = gx;
				py = gy;
				pa = -US_PI * 0.5f;
			} else {
				px = p->ex[s->parent];
				py = p->ey[s->parent];
				pa = p->ea[s->parent];
			}
			/* sway: noise wind field, stronger higher up; withering plants droop */
			float hgt = us_sat((gy - py) / (H + 1e-3f));
			float sway = us_noise2(gx * 1.7f - st->wind_t * 0.6f, k * 0.31f + i) * (0.03f + 0.22f * wind) * (0.3f + hgt);
			float droop = wither * 0.25f * (s->rel >= 0 ? 1 : -1);
			float ang = pa + s->rel + sway + droop;
			float L = s->len * H * vis;
			float ex = px + cosf(ang) * L, ey = py + sinf(ang) * L;
			p->ex[k] = ex;
			p->ey[k] = ey;
			p->ea[k] = ang;
			if (vis <= 0.0f)
				continue;
			float wdt = 0.0024f * s->width * (0.5f + 0.5f * p->g);
			us_line(c, px, py, ex, ey, wdt, vbase * (0.6f + 0.4f * vis), US_MAT_STEM);

			/* leaves: gone in winter, falling in autumn */
			float leafy = us_sat(1.0f - winter * 1.2f) * (1.0f - wither);
			if (s->leaf && vis > 0.6f && leafy > 0.05f) {
				float la = ang + s->leaf_side * (0.9f + 0.2f * us_noise2(st->wind_t + k, i * 3.1f));
				float ll = 0.034f * H * 2.0f * leafy * us_sat((vis - 0.6f) * 2.5f);
				float lx = px + (ex - px) * 0.6f, ly = py + (ey - py) * 0.6f;
				uint8_t lm = autumn > 0.5f ? US_MAT_ACCENT : US_MAT_LEAF;
				petal(c, ctx->crisp, lx, ly, la, ll, ll * 0.3f, 0.7f, lm);
				if (s->depth == 0)
					petal(c, ctx->crisp, lx, ly, ang - s->leaf_side * 0.9f, ll * 0.8f, ll * 0.26f, 0.6f, lm);
				if (autumn > 0.3f && us_rand(&st->rng) < dt * 0.04f * autumn * (0.3f + wind))
					spawn(st, lx, ly, 0.02f * wind, 0.0f, ll, US_MAT_ACCENT, 1);
			}

			/* flowers at tips */
			if (s->tip && vis >= 1.0f && (p->open > 0.02f || p->seedness > 0)) {
				float fr = 0.028f * H * 2.0f * s->flower_size;
				int np = (int)s->petals;
				uint8_t fm = p->hue_bias < 0.65f ? US_MAT_BLOOM : US_MAT_LIGHT;
				if (p->phase == SEED || p->seedness > 0) {
					/* seed head: a little puff of dots, shedding */
					float sh = p->seedness;
					for (int q = 0; q < 7; q++) {
						float qa = s->petal_rot + q * 0.9f;
						us_point(c, ex + cosf(qa) * fr * 0.35f, ey + sinf(qa) * fr * 0.35f, 0.0016f, 0.7f * (1 - sh), US_MAT_ACCENT);
					}
					if (us_rand(&st->rng) < dt * 2.0f * (0.3f + wind))
						spawn(st, ex, ey, us_randr(&st->rng, 0.01f, 0.05f), us_randr(&st->rng, -0.03f, 0.0f), 0.004f, US_MAT_ACCENT, 2);
				} else {
					float op = p->open;
					float rot = s->petal_rot + 0.3f * sway;
					for (int q = 0; q < np; q++) {
						float qa = rot + US_TAU * q / np;
						petal(c, ctx->crisp, ex, ey, qa, fr * (0.25f + 0.75f * op), fr * 0.22f * (0.4f + 0.6f * op), 0.6f + 0.4f * op, fm);
					}
					us_disc(c, ex, ey, fr * 0.12f, 1.0f, US_MAT_LIGHT);
					/* petals shed on hard hits when fully open */
					if (a->beat && op > 0.6f && us_rand(&st->rng) < 0.25f * a->onset)
						spawn(st, ex, ey, us_randr(&st->rng, 0.0f, 0.06f), us_randr(&st->rng, -0.05f, 0.0f), fr * 0.5f, fm, 0);
				}
			}
		}
	}

	/* ---- drifting petals / leaves / seeds ---- */
	for (int i = 0; i < st->nparts;) {
		part_t *q = &st->parts[i];
		float gust = us_noise2(q->x * 3 + st->wind_t, q->y * 3) * (0.05f + 0.25f * wind);
		q->vx += (gust + 0.03f * wind) * dt;
		q->vy += (q->kind == 2 ? 0.005f : 0.03f) * dt;
		q->vx *= expf(-dt * 0.8f);
		q->vy *= expf(-dt * 0.8f);
		q->x += q->vx * dt;
		q->y += q->vy * dt + (q->kind == 2 ? -0.004f * dt * (1 + wind) : 0);
		q->rot += q->vr * dt;
		q->life -= dt * (q->kind == 2 ? 0.08f : 0.12f);
		float gy = ground_y(us_sat(q->x / A), A);
		if (q->y > gy) {
			q->y = gy;
			q->vx = q->vy = 0;
			q->life -= dt * 0.5f;
		}
		if (q->life <= 0 || q->x < -0.1f || q->x > A + 0.1f) {
			*q = st->parts[--st->nparts];
			continue;
		}
		if (q->kind == 2)
			us_point(c, q->x, q->y, 0.0014f, 0.6f * q->life, q->mat);
		else
			petal(c, ctx->crisp, q->x, q->y, q->rot, q->size, q->size * 0.3f, 0.75f * q->life, q->mat);
		i++;
	}
}

const us_scene_vt us_scene_meadow = {"meadow", "Meadow", 0.0f, create, destroy, frame};
