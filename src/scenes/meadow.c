/* Understory scene: Meadow — plants grow & bloom with the bass, sway with the highs.
 *
 * Three depth layers of rolling meadow (far hills faint and small, near bank tall and bright); the
 * nearer hill occludes the bases of the plants behind it. Six species, each with a seeded genome and a
 * life cycle SPROUT/GROW -> BLOOM -> SEED -> WITHER -> reseed nearby:
 *   wildflower (branching skeleton, star flowers)   umbel (Queen Anne's lace: dome -> bird's-nest seed head)
 *   fern (fiddleheads that unfurl with the bass)     oat grass (drooping panicles, anthers dangle in bloom)
 *   foxglove spike (bells open bottom-up)            vine (bindweed climbing last year's stalk, trumpets)
 * Butterflies and bees visit open blooms when the world has insects / pollen.
 * Bass drives growth and opens flowers; highs + world wind drive sway; kicks nudge blooms.
 * World: reads moisture, mycelium (growth speed), season (autumn colour + leaf fall, bare snow-capped
 *        winter stems and snowfall), daylight, insects; writes growth + bloom + pollen.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define NL 3
#define MAX_PL 22 /* per layer */
#define MAX_SEGS 72
#define MAX_PARTS 420
#define MAX_BLADES 260
#define MAX_ANCH 160
#define MAX_FLY 8
#define MAX_BEE 10

enum { GROW = 0, BLOOM, SEED, WITHER };
enum { SP_WILD = 0, SP_UMBEL, SP_FERN, SP_GRASS, SP_SPIKE, SP_VINE, SP_COUNT };

static const float LSCALE[NL] = {0.34f, 0.6f, 1.08f};
static const float LBRIGHT[NL] = {0.40f, 0.64f, 1.0f};

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
	float height; /* normalized (already layer-scaled) */
	float g;      /* growth 0..1 */
	float open;   /* flower openness 0..1 */
	float seedness;
	int phase, species, layer;
	float phase_t, bloom_len;
	float hue_bias; /* 0..1 picks flower material */
	uint32_t gene;
	int nseg;
	seg_t seg[MAX_SEGS];
	float ex[MAX_SEGS], ey[MAX_SEGS], ea[MAX_SEGS]; /* scratch per frame */
} plant_t;

typedef struct {
	float x, y, vx, vy, rot, vr, life, size;
	uint8_t mat, kind; /* kind 0 petal, 1 leaf, 2 seed/pollen */
} part_t;

typedef struct {
	float x, y, open;
} anchor_t;

typedef struct {
	float x, y, vx, vy, ph, size, rest;
	int tgt, mode; /* mode 0 fly, 1 perch */
	uint8_t mat;
	int alive;
} fly_t;

typedef struct {
	us_rng rng;
	int npl[NL];
	plant_t pl[NL][MAX_PL];
	part_t parts[MAX_PARTS];
	int nparts;
	float blade_x[MAX_BLADES], blade_h[MAX_BLADES], blade_ph[MAX_BLADES];
	anchor_t anch[MAX_ANCH];
	int nanch;
	fly_t fly[MAX_FLY];
	fly_t bee[MAX_BEE];
	int inited;
	float bass_env;
	float wind_t;
} st_t;

/* per-frame drawing context */
typedef struct {
	us_canvas *c;
	st_t *st;
	const us_audio *a;
	int crisp, L;
	float A, wind, wind_t, dt, t;
	float spring, summer, autumn, winter;
	float lb, lw; /* layer brightness + line-width scale */
} dc_t;

/* ---- helpers ----------------------------------------------------------- */

static float ground_y(float u, int L)
{
	if (L == 0)
		{
		float r = us_smooth(-0.35f, 0.35f, us_noise2(u * 3.5f + 3.0f, 17.3f)); /* rolling far hills */
		return 0.715f - 0.09f * r - 0.04f * us_noise2(u * 1.2f, 4.4f) + 0.006f * us_noise2(u * 12.0f, 2.2f);
	}
	if (L == 1)
		return 0.78f + 0.07f * us_noise2(u * 3.0f, 9.1f) + 0.008f * us_noise2(u * 9.0f, 5.5f);
	return 0.885f + 0.05f * us_noise2(u * 2.6f, 4.2f) + 0.012f * us_noise2(u * 8.0f, 1.1f);
}

static float hashf(uint32_t h)
{
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	h ^= h >> 16;
	return (h & 0xffffff) / 16777216.0f;
}

/* overwrite (not max) everything below a layer's ground line: the nearer hill hides what is behind it */
static void fill_ground(us_canvas *c, int L, float A, float v, uint8_t mat)
{
	int iv = (int)(us_sat(v) * 255.0f + 0.5f);
	for (int px = 0; px < c->w; px++) {
		float u = (px + 0.5f) / c->s / A;
		int y0 = (int)ceilf(ground_y(u, L) * c->s);
		if (y0 < 0)
			y0 = 0;
		/* the next (nearer) layer overwrites everything below its own crest anyway */
		int y1 = L < 2 ? (int)ceilf(ground_y(u, L + 1) * c->s) + 2 : c->h;
		if (y1 > c->h)
			y1 = c->h;
		for (int py = y0; py < y1; py++) {
			size_t i = (size_t)py * c->w + px;
			c->ink[i] = (uint8_t)iv;
			c->mat[i] = mat;
		}
	}
}

/* Fast thin line for ink: Wu-style two-pixel antialiasing along the major axis (no sqrt per pixel).
 * Falls back to us_line for crisp canvases and lines wider than ~1.6 px. */
static void wline(us_canvas *c, float ax, float ay, float bx, float by, float width, float v, uint8_t mat)
{
	float wp = width * c->s;
	if (c->crisp || wp > 1.6f) {
		us_line(c, ax, ay, bx, by, width, v, mat);
		return;
	}
	v *= us_clampf(wp, 0.35f, 1.0f);
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
	float g = fabsf(dx) > 1e-6f ? dy / dx : 0.0f;
	int i0 = (int)floorf(x0 + 0.5f), i1 = (int)floorf(x1 + 0.5f);
	int lim = steep ? c->h : c->w;
	if (i0 < 0) i0 = 0;
	if (i1 > lim - 1) i1 = lim - 1;
	if (i1 - i0 > 4000)
		return;
	for (int i = i0; i <= i1; i++) {
		float yy = y0 + g * (i - x0);
		int j = (int)floorf(yy);
		float f = yy - j;
		if (steep) {
			us_put(c, j, i, v * (1.0f - f), mat);
			us_put(c, j + 1, i, v * f, mat);
		} else {
			us_put(c, i, j, v * (1.0f - f), mat);
			us_put(c, i, j + 1, v * f, mat);
		}
	}
}

static void wpoly(us_canvas *c, const float *xy, int n, float width, float v, uint8_t mat)
{
	for (int i = 0; i + 1 < n; i++)
		wline(c, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], width, v, mat);
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
		wpoly(c, pts, 5, 0.0012f, v, mat);
	}
}

static float lw(const dc_t *d, float w)
{
	float r = w * d->lw;
	return r < 0.0009f ? 0.0009f : r;
}

static float sway_n(const dc_t *d, float gx, float k)
{
	return us_noise2(gx * 1.7f - d->wind_t * 0.6f, k) * (0.03f + 0.22f * d->wind);
}

static uint8_t leaf_mat(const dc_t *d, uint32_t h)
{
	if (d->autumn > 0.5f) {
		float r = hashf(h);
		return r < 0.45f ? US_MAT_LIGHT : (r < 0.8f ? US_MAT_BLOOM : US_MAT_EARTH);
	}
	if (d->winter > 0.5f)
		return US_MAT_EARTH;
	return US_MAT_LEAF;
}

static float leafy(const dc_t *d) { return us_sat(1.0f - d->winter * 1.25f); }

/* a dab of snow resting on top of a point */
static void snowcap(const dc_t *d, float x, float y, float r)
{
	if (d->winter < 0.35f)
		return;
	float v = us_sat((d->winter - 0.35f) * 2.0f) * (0.55f + 0.4f * d->lb);
	r *= LSCALE[d->L] < 0.5f ? 0.7f : 1.0f;
	if (d->crisp)
		us_disc(d->c, x, y - r * 0.4f, r, v, US_MAT_WATER);
	else {
		us_disc(d->c, x, y - r * 0.35f, r * 0.6f, v * 0.9f, US_MAT_WATER);
		wline(d->c, x - r * 0.8f, y - r * 0.1f, x + r * 0.8f, y - r * 0.1f, 0.0012f, v * 0.7f, US_MAT_WATER);
	}
}

static void add_anchor(st_t *st, float x, float y, float open)
{
	if (st->nanch >= MAX_ANCH || open < 0.25f)
		return;
	anchor_t *a = &st->anch[st->nanch++];
	a->x = x;
	a->y = y;
	a->open = open;
}

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

/* a stalk from the ground, n segments, growing to frac of its length; returns points (n+1) */
static float stalk(const dc_t *d, float gx, float gy, float H, float frac, float lean, float curve, float salt, int n,
		   float *pts)
{
	float ang = -US_PI * 0.5f + lean;
	float x = gx, y = gy, seg = H * frac / n;
	pts[0] = x;
	pts[1] = y;
	float sw = sway_n(d, gx, salt);
	for (int k = 0; k < n; k++) {
		float hgt = (float)k / n;
		ang += curve + sw * (0.25f + hgt) * 0.55f + 0.02f * us_noise2(salt * 3.0f, k * 0.7f);
		x += cosf(ang) * seg;
		y += sinf(ang) * seg;
		pts[2 * k + 2] = x;
		pts[2 * k + 3] = y;
	}
	return ang;
}

/* ---- wildflower genome ------------------------------------------------- */

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

static int pick_species(us_rng *r, int L)
{
	static const float wnear[SP_COUNT] = {0.27f, 0.16f, 0.13f, 0.16f, 0.14f, 0.14f};
	static const float wfar[SP_COUNT] = {0.36f, 0.26f, 0.0f, 0.24f, 0.14f, 0.0f};
	const float *wt = L == 0 ? wfar : wnear;
	float u = us_rand(r), acc = 0;
	for (int i = 0; i < SP_COUNT; i++) {
		acc += wt[i];
		if (u < acc)
			return i;
	}
	return SP_WILD;
}

static void new_plant(st_t *st, plant_t *p, float x, float g0, int L)
{
	us_rng *r = &st->rng;
	memset(p, 0, sizeof(*p));
	p->x = x;
	p->layer = L;
	p->species = pick_species(r, L);
	static const float hlo[SP_COUNT] = {0.32f, 0.40f, 0.20f, 0.30f, 0.40f, 0.36f};
	static const float hhi[SP_COUNT] = {0.62f, 0.64f, 0.32f, 0.52f, 0.68f, 0.58f};
	p->height = us_randr(r, hlo[p->species], hhi[p->species]) * LSCALE[L];
	p->g = g0;
	p->phase = GROW;
	p->hue_bias = us_rand(r);
	p->gene = us_rng_u32(r);
	p->bloom_len = us_randr(r, 45.0f, 95.0f);
	if (p->species == SP_WILD) {
		int style = (int)(us_rand(r) * 3);
		grow_branch(p, r, -1, us_randn(r) * 0.05f, 1.0f, 1.0f, 0, style);
		compute_births(p);
	}
}

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x6d656164u);
	return s;
}

static void destroy(void *p) { free(p); }

static uint8_t flower_mat(const plant_t *p)
{
	return p->hue_bias < 0.6f ? US_MAT_BLOOM : US_MAT_LIGHT;
}

/* ---- species drawing ---------------------------------------------------- */

static void draw_wild(dc_t *d, plant_t *p, int pi, float gx, float gy, float vbase)
{
	us_canvas *c = d->c;
	st_t *st = d->st;
	float H = p->height;
	float wither = p->phase == WITHER ? us_sat(1.0f - p->g) : 0.0f;
	float lf = leafy(d) * (1.0f - wither);
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
		float hgt = us_sat((gy - py) / (H + 1e-3f));
		float sway = us_noise2(gx * 1.7f - d->wind_t * 0.6f, k * 0.31f + pi) * (0.03f + 0.22f * d->wind) * (0.3f + hgt);
		float droop = (wither + 0.25f * d->winter) * 0.25f * (s->rel >= 0 ? 1 : -1);
		float ang = pa + s->rel + sway + droop;
		float Lg = s->len * H * vis;
		float ex = px + cosf(ang) * Lg, ey = py + sinf(ang) * Lg;
		p->ex[k] = ex;
		p->ey[k] = ey;
		p->ea[k] = ang;
		if (vis <= 0.0f)
			continue;
		float wdt = lw(d, 0.0024f * s->width * (0.5f + 0.5f * p->g));
		wline(c, px, py, ex, ey, wdt, vbase * (0.6f + 0.4f * vis), d->winter > 0.6f || d->autumn > 0.65f ? US_MAT_EARTH : US_MAT_STEM);

		if (s->leaf && vis > 0.6f && lf > 0.05f) {
			float la = ang + s->leaf_side * (0.9f + 0.2f * us_noise2(d->wind_t + k, pi * 3.1f));
			float ll = 0.068f * H * lf * us_sat((vis - 0.6f) * 2.5f);
			float lx = px + (ex - px) * 0.6f, ly = py + (ey - py) * 0.6f;
			uint8_t lm = leaf_mat(d, p->gene + k);
			petal(c, d->crisp, lx, ly, la, ll, ll * 0.3f, 0.7f * vbase, lm);
			if (s->depth == 0)
				petal(c, d->crisp, lx, ly, ang - s->leaf_side * 0.9f, ll * 0.8f, ll * 0.26f, 0.6f * vbase, lm);
			if (d->autumn > 0.3f && us_rand(&st->rng) < d->dt * 0.04f * d->autumn * (0.3f + d->wind))
				spawn(st, lx, ly, 0.02f * d->wind, 0.0f, ll, lm, 1);
		}
		if (s->tip && d->winter > 0.35f && vis >= 1.0f)
			snowcap(d, ex, ey, 0.006f * (0.5f + H * 1.5f));

		if (s->tip && vis >= 1.0f && (p->open > 0.02f || p->seedness > 0)) {
			float fr = 0.056f * H * s->flower_size;
			int np = (int)s->petals;
			uint8_t fm = flower_mat(p);
			if (p->phase == SEED || p->seedness > 0) {
				float sh = p->seedness;
				for (int q = 0; q < 7; q++) {
					float qa = s->petal_rot + q * 0.9f;
					us_point(c, ex + cosf(qa) * fr * 0.35f, ey + sinf(qa) * fr * 0.35f, 0.0016f, 0.7f * (1 - sh) * vbase,
						 US_MAT_ACCENT);
				}
				if (d->L > 0 && us_rand(&st->rng) < d->dt * 2.0f * (0.3f + d->wind))
					spawn(st, ex, ey, us_randr(&st->rng, 0.01f, 0.05f), us_randr(&st->rng, -0.03f, 0.0f), 0.004f,
					      US_MAT_ACCENT, 2);
			} else {
				float op = p->open;
				float rot = s->petal_rot + 0.3f * sway;
				for (int q = 0; q < np; q++) {
					float qa = rot + US_TAU * q / np;
					petal(c, d->crisp, ex, ey, qa, fr * (0.25f + 0.75f * op), fr * 0.22f * (0.4f + 0.6f * op),
					      (0.6f + 0.4f * op) * vbase, fm);
				}
				us_disc(c, ex, ey, fr * 0.12f, vbase, US_MAT_LIGHT);
				if (d->L > 0)
					add_anchor(st, ex, ey, op);
				if (d->L > 0 && d->a->beat && op > 0.6f && us_rand(&st->rng) < 0.25f * d->a->onset)
					spawn(st, ex, ey, us_randr(&st->rng, 0.0f, 0.06f), us_randr(&st->rng, -0.05f, 0.0f), fr * 0.5f, fm, 0);
			}
		}
	}
}

/* Queen Anne's lace: tall stalk, ferny basal leaves, a flat dome of umbellets that curls into a nest */
static void draw_umbel(dc_t *d, plant_t *p, int pi, float gx, float gy, float vbase)
{
	us_canvas *c = d->c;
	st_t *st = d->st;
	us_rng gr;
	us_rng_seed(&gr, p->gene);
	float H = p->height;
	int nst = 1 + (us_rand(&gr) < 0.6f) + (us_rand(&gr) < 0.3f);
	float lf = leafy(d);
	uint8_t sm = d->winter > 0.6f || d->autumn > 0.6f ? US_MAT_EARTH : US_MAT_STEM;
	/* basal leaves: finely cut, low */
	if (lf > 0.05f)
		for (int k = 0; k < 4; k++) {
			float la = -US_PI * 0.5f + (k - 1.5f) * 0.55f + 0.1f * sway_n(d, gx, k + 2.0f);
			float ll = H * 0.22f * us_sat(p->g * 3.0f) * lf;
			float x1 = gx + cosf(la) * ll, y1 = gy + sinf(la) * ll;
			wline(c, gx, gy, x1, y1, lw(d, 0.0012f), 0.55f * vbase, leaf_mat(d, p->gene + k));
			for (int j = 1; j <= 3; j++) {
				float t = j / 4.0f;
				float mx = gx + (x1 - gx) * t, my = gy + (y1 - gy) * t;
				petal(c, d->crisp, mx, my, la - 0.8f, ll * 0.22f * (1.1f - t), ll * 0.05f, 0.5f * vbase, leaf_mat(d, p->gene + k));
				petal(c, d->crisp, mx, my, la + 0.8f, ll * 0.22f * (1.1f - t), ll * 0.05f, 0.5f * vbase, leaf_mat(d, p->gene + k));
			}
		}
	for (int sidx = 0; sidx < nst; sidx++) {
		float h = H * (sidx == 0 ? 1.0f : us_randr(&gr, 0.6f, 0.85f));
		float lean = sidx == 0 ? us_randn(&gr) * 0.05f : (sidx == 1 ? -1.0f : 1.0f) * us_randr(&gr, 0.15f, 0.35f);
		float curve = us_randn(&gr) * 0.02f - lean * 0.08f;
		int nr = 9 + (int)(us_rand(&gr) * 6);
		float pts[2 * 9];
		float frac = us_sat(p->g * 1.15f - 0.08f * sidx);
		if (frac <= 0.02f)
			continue;
		float ang = stalk(d, gx, gy, h, frac, lean, curve, pi + sidx * 1.7f, 8, pts);
		wpoly(c, pts, 9, lw(d, 0.0022f), vbase * 0.85f, sm);
		float tx = pts[16], ty = pts[17];
		if (frac < 0.9f)
			continue;
		float bud = d->winter > 0.5f ? 1.0f : us_sat((frac - 0.9f) * 10.0f);
		float op = p->open, seed = p->seedness;
		int dry = d->winter > 0.5f || p->phase == WITHER;
		if (dry) {
			op = 0;
			seed = 1;
		}
		float rl = h * 0.12f * (0.45f + 0.55f * fmaxf(op, seed * 0.7f)) * bud;
		float spread = (0.35f + 0.9f * op) * (1.0f - 0.55f * seed);
		uint8_t fm = d->autumn > 0.55f || dry ? US_MAT_EARTH : US_MAT_ACCENT;
		float fv = (0.55f + 0.45f * op) * vbase * (dry ? 0.9f : 1.0f);
		for (int k = 0; k < nr; k++) {
			float f = nr > 1 ? (float)k / (nr - 1) : 0.5f;
			float phi = (f - 0.5f) * 2.0f * spread + (ang + US_PI * 0.5f) * 0.6f;
			/* rays rise then bow outward; when seeding they curl back in over the centre (bird's nest) */
			float ex = tx + sinf(phi) * rl * (1.0f - 0.5f * seed);
			float ey = ty - cosf(phi) * rl * (0.75f + 0.25f * fabsf(cosf(phi))) - seed * rl * 0.25f;
			float cx = ex - sinf(phi) * rl * 0.45f * seed;
			wline(c, tx, ty, ex, ey, lw(d, 0.001f), fv * 0.6f, sm);
			if (seed > 0.3f)
				wline(c, ex, ey, cx, ey - rl * 0.15f * seed, lw(d, 0.001f), fv * 0.6f, fm);
			/* umbellet: a tiny dome of florets */
			float ur = rl * 0.22f * (0.4f + 0.6f * op);
			int nf = d->L == 0 ? 2 : 4;
			for (int q = 0; q < nf; q++) {
				float qa = -US_PI * 0.5f + (q - (nf - 1) * 0.5f) * 0.55f;
				float fx = ex + cosf(qa) * ur, fy = ey + sinf(qa) * ur;
				if (d->crisp)
					us_put(c, (int)(fx * c->s), (int)(fy * c->s), fv, fm);
				else
					us_point(c, fx, fy, 0.0012f + 0.0008f * LSCALE[d->L], fv, fm);
			}
		}
		if (!dry && op > 0.2f) {
			us_disc(c, tx, ty - rl * 0.7f, 0.0018f * LSCALE[d->L] + 0.0006f, vbase * 0.8f, US_MAT_BLOOM); /* the one dark floret */
			if (d->L > 0)
				add_anchor(st, tx, ty - rl * 0.75f, op);
		}
		if (p->phase == SEED && d->L > 0 && us_rand(&st->rng) < d->dt * 1.5f * (0.3f + d->wind))
			spawn(st, tx, ty - rl, us_randr(&st->rng, 0.01f, 0.05f), us_randr(&st->rng, -0.03f, 0.0f), 0.004f,
			      US_MAT_ACCENT, 2);
		snowcap(d, tx, ty - rl * 0.6f, rl * 0.18f + 0.003f);
	}
}

/* fern: fronds unroll from fiddleheads as the plant grows; the bass keeps the tips breathing */
static void draw_fern(dc_t *d, plant_t *p, int pi, float gx, float gy, float vbase)
{
	us_canvas *c = d->c;
	us_rng gr;
	us_rng_seed(&gr, p->gene);
	float H = p->height;
	int nfr = 4 + (int)(us_rand(&gr) * 4);
	uint8_t m = US_MAT_LEAF;
	if (d->autumn > 0.5f)
	{
		m = hashf(p->gene) < 0.5f ? US_MAT_LIGHT : US_MAT_EARTH; /* bracken gold / rust */
		vbase *= m == US_MAT_LIGHT ? 0.62f : 1.0f;
	}
	if (d->winter > 0.5f)
		m = US_MAT_EARTH;
	float dead = us_sat((d->winter - 0.3f) * 2.0f);
	for (int f = 0; f < nfr; f++) {
		float side = (f & 1) ? 1.0f : -1.0f;
		float spread = (f + 0.5f) / nfr;
		float a0 = -US_PI * 0.5f + side * (0.1f + 0.75f * spread) + us_randn(&gr) * 0.06f;
		float Lf = H * us_randr(&gr, 0.7f, 1.1f) * (1.0f - 0.3f * spread);
		/* younger fronds lag; open (bass) relaxes the last curl */
		float uf = us_sat(p->g * 1.35f - 0.35f * f / nfr);
		if (p->phase == BLOOM || p->phase == SEED)
			uf = us_sat(0.82f + 0.18f * p->open);
		if (uf <= 0.01f)
			continue;
		enum { N = 20 };
		float pts[2 * (N + 1)];
		float x = gx, y = gy, ang = a0;
		float sw = sway_n(d, gx + f * 0.05f, pi + f * 0.9f);
		float seg = Lf / N;
		pts[0] = x;
		pts[1] = y;
		int last = N;
		for (int i = 0; i < N; i++) {
			float s = (float)i / N;
			float arch = side * (0.045f + 0.03f * dead) * (0.5f + s);
			float coil = 0;
			float ds = seg;
			if (s > uf) {
				float k = (s - uf) * N;
				coil = side * 0.55f;
				ds = seg * powf(0.86f, k);
			}
			ang += arch + coil + sw * 0.35f * (0.3f + s) + dead * side * 0.06f;
			x += cosf(ang) * ds;
			y += sinf(ang) * ds;
			if (y > gy + 0.004f && dead > 0.3f) { /* dead fronds lie on the ground */
				y = gy + 0.004f;
				last = i + 1;
			}
			pts[2 * i + 2] = x;
			pts[2 * i + 3] = y;
		}
		wpoly(c, pts, last + 1, lw(d, 0.0016f), vbase * 0.8f, m == US_MAT_LEAF ? US_MAT_STEM : m);
		/* pinnae along the unrolled part */
		float pv = vbase * 0.7f * (1.0f - 0.7f * dead);
		if (pv > 0.04f)
			for (int i = 2; i < last; i++) {
				float s = (float)i / N;
				if (s > uf - 0.04f)
					break;
				float x0 = pts[2 * i], y0 = pts[2 * i + 1];
				float dx = pts[2 * i + 2] - x0, dy = pts[2 * i + 3] - y0;
				float ha = atan2f(dy, dx);
				float env = us_smooth(0.0f, 0.25f, s) * (1.0f - 0.85f * s) * (1.0f - 0.6f * dead);
				float pl = Lf * 0.2f * env;
				if (pl < 0.002f)
					continue;
				for (int sd = -1; sd <= 1; sd += 2) {
					float pa = ha + sd * (1.05f - 0.25f * dead) + (sd * side < 0 ? 0.15f : 0) + dead * 0.6f;
					if (d->crisp || d->L == 0)
						wline(c, x0, y0, x0 + cosf(pa) * pl, y0 + sinf(pa) * pl, lw(d, 0.0011f), pv, m);
					else
						petal(c, 0, x0, y0, pa, pl, pl * 0.16f, pv, m);
				}
			}
		if ((f & 1) == 0)
			snowcap(d, pts[2 * (last / 2)], pts[2 * (last / 2) + 1] - 0.002f, 0.004f);
	}
}

/* wild oat: arching basal blades, culms with drooping panicles; anthers dangle while it flowers */
static void draw_grass(dc_t *d, plant_t *p, int pi, float gx, float gy, float vbase)
{
	us_canvas *c = d->c;
	st_t *st = d->st;
	us_rng gr;
	us_rng_seed(&gr, p->gene);
	float H = p->height;
	float lf = 0.4f + 0.6f * leafy(d);
	uint8_t bm = d->autumn > 0.5f ? US_MAT_LIGHT : (d->winter > 0.5f ? US_MAT_EARTH : US_MAT_LEAF);
	int nb = 5 + (int)(us_rand(&gr) * 5);
	for (int b = 0; b < nb; b++) {
		float side = us_randr(&gr, -1, 1);
		float h = H * us_randr(&gr, 0.35f, 0.65f) * us_sat(p->g * 2.0f) * lf;
		float pts[2 * 6];
		stalk(d, gx + side * 0.004f, gy, h, 1.0f, side * 0.45f, side * 0.07f + 0.04f * d->winter * (side < 0 ? -1 : 1),
		      pi + b * 0.37f, 5, pts);
		wpoly(c, pts, 6, lw(d, 0.0013f), 0.6f * vbase, bm);
	}
	int nc = 1 + (int)(us_rand(&gr) * 3);
	uint8_t spm = d->summer > 0.55f || d->autumn > 0.3f || p->phase >= SEED ? US_MAT_ACCENT : US_MAT_LEAF;
	if (d->winter > 0.5f)
		spm = US_MAT_EARTH;
	for (int k = 0; k < nc; k++) {
		float h = H * us_randr(&gr, 0.8f, 1.05f);
		float lean = us_randn(&gr) * 0.12f;
		float frac = us_sat(p->g * 1.1f - 0.05f * k);
		if (frac < 0.05f)
			continue;
		float pts[2 * 9];
		stalk(d, gx, gy, h, frac, lean, lean * 0.1f + 0.012f, pi + 5.0f + k, 8, pts);
		wpoly(c, pts, 9, lw(d, 0.0013f), 0.75f * vbase, d->winter > 0.5f ? US_MAT_EARTH : US_MAT_STEM);
		if (frac < 0.75f)
			continue;
		/* panicle on the top three segments: spikelets on hair pedicels, nodding downwind */
		int ns = d->L == 0 ? 4 : 9;
		for (int q = 0; q < ns; q++) {
			float t = 5.0f + 3.0f * q / ns;
			int i0 = (int)t;
			float ft = t - i0;
			float nx = us_lerp(pts[2 * i0], pts[2 * i0 + 2], ft), ny = us_lerp(pts[2 * i0 + 1], pts[2 * i0 + 3], ft);
			float sd = (q & 1) ? 1.0f : -1.0f;
			float sw = sway_n(d, gx, pi + q * 0.4f);
			float pa = -US_PI * 0.5f + sd * (0.9f + 0.6f * (1.0f - p->open)) + sw * 2.0f;
			float pl = H * 0.07f * (1.0f - 0.4f * q / ns);
			float px = nx + cosf(pa) * pl, py = ny + sinf(pa) * pl * 0.5f;
			float hang = US_PI * 0.5f + sd * 0.35f + sw * 1.5f; /* spikelets droop */
			wline(c, nx, ny, px, py, lw(d, 0.0009f), 0.5f * vbase, US_MAT_STEM);
			float sl = H * 0.045f;
			petal(c, d->crisp, px, py, hang, sl, sl * 0.28f, 0.8f * vbase, spm);
			/* anthers: pale threads dangling while it flowers */
			if (p->phase == BLOOM && p->open > 0.45f && d->L > 0 && d->winter < 0.4f) {
				float ax = px + cosf(hang) * sl, ay = py + sinf(hang) * sl;
				wline(c, ax, ay, ax + 0.002f * sw * 30.0f, ay + sl * 0.5f * p->open, lw(d, 0.0008f),
					0.7f * vbase * p->open, US_MAT_LIGHT);
				us_point(c, ax + 0.002f * sw * 30.0f, ay + sl * 0.5f * p->open, 0.0013f, 0.9f * vbase * p->open, US_MAT_LIGHT);
			}
			if (q == ns - 1)
				snowcap(d, px, py, 0.003f);
		}
		if (d->L > 0 && (p->phase == SEED || d->autumn > 0.5f) &&
		    us_rand(&st->rng) < d->dt * 1.2f * (0.2f + d->wind))
			spawn(st, pts[14], pts[15], us_randr(&st->rng, 0.02f, 0.06f), us_randr(&st->rng, -0.02f, 0.0f), 0.003f,
			      US_MAT_ACCENT, 2);
	}
}

/* foxglove: a basal rosette, one tall raceme; bells open from the bottom up as the bass opens it */
static void draw_spike(dc_t *d, plant_t *p, int pi, float gx, float gy, float vbase)
{
	us_canvas *c = d->c;
	st_t *st = d->st;
	us_rng gr;
	us_rng_seed(&gr, p->gene);
	float H = p->height;
	float lf = leafy(d);
	if (lf > 0.05f)
		for (int k = 0; k < 4; k++) {
			float la = -US_PI * 0.5f + (k < 2 ? -1.0f : 1.0f) * (0.75f + 0.35f * (k & 1)) + 0.12f * sway_n(d, gx, k + 9.0f);
			float ll = H * 0.16f * us_sat(p->g * 2.5f) * lf;
			petal(c, d->crisp, gx, gy, la, ll, ll * 0.17f, 0.65f * vbase, leaf_mat(d, p->gene + k));
		}
	float pts[2 * 13];
	float frac = us_sat(p->g * 1.1f);
	if (frac < 0.05f)
		return;
	stalk(d, gx, gy, H, frac, us_randn(&gr) * 0.05f, us_randn(&gr) * 0.012f, pi + 0.3f, 12, pts);
	wpoly(c, pts, 13, lw(d, 0.0022f), 0.8f * vbase, d->winter > 0.5f ? US_MAT_EARTH : US_MAT_STEM);
	/* small stem leaves on the lower half */
	if (lf > 0.05f)
		for (int i = 2; i < 6; i++) {
			float dx = pts[2 * i + 2] - pts[2 * i], dy = pts[2 * i + 3] - pts[2 * i + 1];
			float ha = atan2f(dy, dx);
			petal(c, d->crisp, pts[2 * i], pts[2 * i + 1], ha + ((i & 1) ? 0.9f : -0.9f), H * 0.07f * lf,
			      H * 0.018f * lf, 0.6f * vbase, leaf_mat(d, p->gene + 31 + i));
		}
	if (frac < 0.6f)
		return;
	int nbell = d->L == 0 ? 6 : 12;
	uint8_t fm = flower_mat(p);
	float op = p->open;
	int dry = d->winter > 0.5f || p->phase == WITHER;
	for (int b = 0; b < nbell; b++) {
		float s = 0.5f + 0.48f * b / nbell; /* position up the stem */
		float t = s * 12.0f;
		int i0 = (int)t;
		if (i0 > 11)
			i0 = 11;
		float ft = t - i0;
		float nx = us_lerp(pts[2 * i0], pts[2 * i0 + 2], ft), ny = us_lerp(pts[2 * i0 + 1], pts[2 * i0 + 3], ft);
		if (s > frac)
			break;
		float sd = (b & 1) ? 1.0f : -1.0f;
		float bf = (float)b / nbell;
		/* opening wave climbs the spike */
		float ob = us_sat(op * 1.6f - bf * 0.9f);
		if (p->phase == SEED || dry) {
			float cap = us_sat(1.0f - bf * 1.2f);
			if (cap > 0.1f)
				us_disc(c, nx + sd * 0.004f * LSCALE[d->L], ny, 0.0022f * LSCALE[d->L] + 0.0008f, 0.6f * vbase,
					dry ? US_MAT_EARTH : US_MAT_ACCENT);
			continue;
		}
		float bl = H * (0.06f - 0.025f * bf);
		if (ob < 0.15f) {
			us_point(c, nx + sd * 0.003f, ny, 0.0012f + 0.001f * LSCALE[d->L], 0.6f * vbase, US_MAT_LEAF);
			continue;
		}
		/* a nodding bell on a short pedicel: tube hanging out and down, flared mouth */
		float ba = US_PI * 0.5f - sd * (0.75f - 0.3f * ob);
		float x0 = nx + sd * bl * 0.25f, y0 = ny + bl * 0.08f;
		wline(c, nx, ny, x0, y0, lw(d, 0.0009f), 0.6f * vbase, US_MAT_STEM);
		float L2 = bl * (0.5f + 0.5f * ob);
		float ux = cosf(ba), uy = sinf(ba), vx = -uy, vy = ux;
		float w0 = bl * 0.13f, w1 = bl * (0.17f + 0.13f * ob);
		float mx = x0 + ux * L2, my = y0 + uy * L2;
		float bv = (0.55f + 0.45f * ob) * vbase;
		if (d->crisp) {
			us_tri(c, x0 + vx * w0, y0 + vy * w0, x0 - vx * w0, y0 - vy * w0, mx + vx * w1, my + vy * w1, bv, fm);
			us_tri(c, x0 - vx * w0, y0 - vy * w0, mx - vx * w1, my - vy * w1, mx + vx * w1, my + vy * w1, bv, fm);
		} else {
			float q[] = {mx + vx * w1 * 1.25f + ux * bl * 0.05f, my + vy * w1 * 1.25f + uy * bl * 0.05f,
				     mx + vx * w1, my + vy * w1, x0 + vx * w0, y0 + vy * w0, x0 - vx * w0, y0 - vy * w0,
				     mx - vx * w1, my - vy * w1, mx - vx * w1 * 1.25f + ux * bl * 0.05f, my - vy * w1 * 1.25f + uy * bl * 0.05f};
			wpoly(c, q, 6, lw(d, 0.0011f), bv, fm);
			us_point(c, x0 + ux * L2 * 0.6f, y0 + uy * L2 * 0.6f, 0.0009f, bv * 0.8f, US_MAT_LIGHT); /* freckles in the throat */
		}
		if (d->L > 0 && b % 3 == 0)
			add_anchor(st, mx, my, ob);
		if (d->L > 0 && d->a->beat && ob > 0.8f && us_rand(&st->rng) < 0.04f * d->a->onset)
			spawn(st, mx, my, us_randr(&st->rng, 0.0f, 0.05f), 0.0f, bl * 0.5f, fm, 0);
	}
	snowcap(d, pts[24], pts[25], 0.004f);
}

/* bindweed climbing last year's dead stalk; tendrils search, trumpets open with the bass */
static void draw_vine(dc_t *d, plant_t *p, int pi, float gx, float gy, float vbase)
{
	us_canvas *c = d->c;
	st_t *st = d->st;
	us_rng gr;
	us_rng_seed(&gr, p->gene);
	float H = p->height;
	float pts[2 * 11];
	float lean = us_randn(&gr) * 0.06f;
	stalk(d, gx, gy, H, 1.0f, lean, us_randn(&gr) * 0.01f, pi + 0.11f, 10, pts);
	wpoly(c, pts, 11, lw(d, 0.002f), 0.38f * vbase, US_MAT_EARTH); /* the dead support */
	snowcap(d, pts[20], pts[21], 0.004f);
	float climb = us_sat(p->g * 1.05f);
	if (climb < 0.02f || d->winter > 0.75f)
		return;
	enum { N = 64 };
	float turns = us_randr(&gr, 4.0f, 6.5f), ph = us_rand(&gr) * US_TAU;
	float R = 0.006f + 0.01f * LSCALE[d->L];
	float prevx = gx, prevy = gy;
	int prev_front = 1;
	float lf = leafy(d);
	uint8_t fm = hashf(p->gene + 5) < 0.55f ? US_MAT_WATER : US_MAT_BLOOM;
	float op = p->open;
	int nflow = 0;
	for (int i = 1; i <= N; i++) {
		float s = (float)i / N;
		if (s > climb)
			break;
		float t = s * 10.0f;
		int i0 = (int)t;
		if (i0 > 9)
			i0 = 9;
		float ft = t - i0;
		float sx = us_lerp(pts[2 * i0], pts[2 * i0 + 2], ft), sy = us_lerp(pts[2 * i0 + 1], pts[2 * i0 + 3], ft);
		float th = s * turns * US_TAU + ph;
		float r = R * (1.0f - 0.3f * s);
		/* the growing tip leaves the stalk and circles, searching */
		float tipk = us_smooth(climb - 0.08f, climb, s) * (p->phase == GROW ? 1.0f : 0.0f);
		float x = sx + sinf(th) * r + tipk * 0.02f * LSCALE[d->L] * sinf(d->t * 0.8f + pi);
		float y = sy - tipk * 0.01f;
		int front = cosf(th) > 0;
		wline(c, prevx, prevy, x, y, lw(d, 0.0013f), (front ? 0.85f : 0.4f) * vbase,
			d->autumn > 0.5f ? US_MAT_LIGHT : US_MAT_LEAF);
		prevx = x;
		prevy = y;
		(void)prev_front;
		prev_front = front;
		/* heart leaves on alternate nodes */
		if ((i % 6) == 3 && lf > 0.05f && s < climb - 0.03f) {
			float sd = sinf(th) > 0 ? 1.0f : -1.0f;
			float la = -US_PI * 0.5f + sd * 1.1f + 0.4f * sway_n(d, gx, i * 0.3f);
			float ll = 0.022f * LSCALE[d->L] * lf;
			uint8_t lm = leaf_mat(d, p->gene + i);
			petal(c, d->crisp, x, y, la - 0.18f, ll, ll * 0.42f, 0.7f * vbase, lm);
			petal(c, d->crisp, x, y, la + 0.18f, ll, ll * 0.42f, 0.7f * vbase, lm);
		}
		/* tendril curls */
		if ((i % 11) == 7 && !d->crisp && d->L > 0) {
			float sd = sinf(th) > 0 ? 1.0f : -1.0f;
			float cr = 0.004f * LSCALE[d->L];
			us_arc(c, x + sd * cr, y, cr, sd > 0 ? US_PI : 0, sd > 0 ? US_PI + 4.5f : 4.5f, 0.001f, 0.5f * vbase, US_MAT_LEAF);
		}
		/* trumpets */
		if ((i % 13) == 9 && s > 0.3f && (p->phase == BLOOM || p->phase == SEED) && nflow < 4) {
			nflow++;
			float sd = sinf(th) > 0 ? 1.0f : -1.0f;
			float fo = p->phase == SEED ? us_sat(op) : op;
			float tl = 0.03f * LSCALE[d->L] * (0.6f + 0.4f * fo);
			float ta = -US_PI * 0.5f + sd * (0.7f + 0.3f * sway_n(d, gx, i * 0.2f));
			float mx = x + cosf(ta) * tl, my = y + sinf(ta) * tl;
			float mr = tl * (0.12f + 0.42f * fo);
			float nx = -sinf(ta), ny = cosf(ta);
			float bv = (0.5f + 0.5f * fo) * vbase;
			if (d->crisp) {
				us_tri(c, x, y, mx + nx * mr, my + ny * mr, mx - nx * mr, my - ny * mr, bv, fm);
			} else {
				wline(c, x, y, mx + nx * mr, my + ny * mr, lw(d, 0.0011f), bv, fm);
				wline(c, x, y, mx - nx * mr, my - ny * mr, lw(d, 0.0011f), bv, fm);
				/* the mouth: a flattened ellipse seen at an angle */
				float q[2 * 9];
				for (int e = 0; e <= 8; e++) {
					float ea = e * US_TAU / 8;
					q[2 * e] = mx + nx * mr * cosf(ea) + cosf(ta) * mr * 0.35f * sinf(ea);
					q[2 * e + 1] = my + ny * mr * cosf(ea) + sinf(ta) * mr * 0.35f * sinf(ea);
				}
				wpoly(c, q, 9, lw(d, 0.0011f), bv, fm);
			}
			us_point(c, mx, my, 0.0012f, bv, US_MAT_LIGHT);
			if (d->L > 0)
				add_anchor(st, mx, my, fo);
		}
	}
}

/* ---- insects ------------------------------------------------------------ */

static void draw_butterfly(us_canvas *c, int crisp, float x, float y, float sz, float ph, float v, uint8_t m, float tilt)
{
	float fx = 0.12f + 0.88f * fabsf(cosf(ph));
	float ct = cosf(tilt), st = sinf(tilt);
#define RX(dx, dy) (x + (dx) * ct - (dy) * st)
#define RY(dx, dy) (y + (dx) * st + (dy) * ct)
	for (int s = -1; s <= 1; s += 2) {
		float a1x = s * sz * 1.0f * fx, a1y = -sz * 0.95f, a2x = s * sz * 0.85f * fx, a2y = sz * 0.05f;
		float b1x = s * sz * 0.7f * fx, b1y = sz * 0.2f, b2x = s * sz * 0.35f * fx, b2y = sz * 0.7f;
		if (crisp) {
			us_tri(c, RX(0, -sz * 0.15f), RY(0, -sz * 0.15f), RX(a1x, a1y), RY(a1x, a1y), RX(a2x, a2y), RY(a2x, a2y), v, m);
			us_tri(c, RX(0, sz * 0.05f), RY(0, sz * 0.05f), RX(b1x, b1y), RY(b1x, b1y), RX(b2x, b2y), RY(b2x, b2y), v * 0.8f, m);
		} else {
			float p1[] = {RX(0, -sz * 0.2f), RY(0, -sz * 0.2f), RX(a1x, a1y), RY(a1x, a1y), RX(a2x, a2y), RY(a2x, a2y),
				      RX(0, sz * 0.05f), RY(0, sz * 0.05f)};
			wpoly(c, p1, 4, 0.0011f, v, m);
			float p2[] = {RX(0, sz * 0.05f), RY(0, sz * 0.05f), RX(b1x, b1y), RY(b1x, b1y), RX(b2x, b2y), RY(b2x, b2y),
				      RX(0, sz * 0.3f), RY(0, sz * 0.3f)};
			wpoly(c, p2, 4, 0.0011f, v * 0.8f, m);
		}
	}
	wline(c, RX(0, -sz * 0.35f), RY(0, -sz * 0.35f), RX(0, sz * 0.45f), RY(0, sz * 0.45f), crisp ? 0.003f : 0.0014f, v,
		US_MAT_STEM);
#undef RX
#undef RY
}

static int pick_anchor(st_t *st)
{
	if (st->nanch <= 0)
		return -1;
	int best = (int)(us_rand(&st->rng) * st->nanch);
	for (int k = 0; k < 3; k++) {
		int i = (int)(us_rand(&st->rng) * st->nanch);
		if (st->anch[i].open > st->anch[best].open)
			best = i;
	}
	return best;
}

static void insects(dc_t *d, us_world *w, float dens)
{
	st_t *st = d->st;
	us_canvas *c = d->c;
	us_rng *r = &st->rng;
	float A = d->A, dt = d->dt;
	float warm = (1.0f - d->winter) * (1.0f - 0.6f * d->autumn);
	float dayk = 0.35f + 0.65f * us_smooth(0.1f, 0.5f, w->daylight);
	int nf = (int)((w->insects * 6.0f + w->bloom * 4.0f) * warm * dayk * (0.5f + dens) + 0.5f);
	int nb = (int)((w->pollen * 6.0f + w->bloom * 5.0f) * warm * dayk * (0.5f + dens) + 0.5f);
	if (st->nanch < 2) {
		nf = nf > 1 ? 1 : nf;
		nb = 0;
	}
	if (nf > MAX_FLY)
		nf = MAX_FLY;
	if (nb > MAX_BEE)
		nb = MAX_BEE;
	float lift = d->a->kick + 0.6f * d->a->bass; /* the bass sends them up from the flowers */

	for (int i = 0; i < MAX_FLY; i++) {
		fly_t *f = &st->fly[i];
		if (!f->alive) {
			if (i >= nf)
				continue;
			f->alive = 1;
			f->x = us_rand(r) < 0.5f ? -0.05f : A + 0.05f;
			f->y = us_randr(r, 0.35f, 0.7f);
			f->vx = f->vy = 0;
			f->ph = us_rand(r) * 10;
			f->size = us_randr(r, 0.013f, 0.021f);
			f->mode = 0;
			f->tgt = pick_anchor(st);
			f->rest = 0;
			float h = us_rand(r);
			f->mat = h < 0.4f ? US_MAT_LIGHT : (h < 0.7f ? US_MAT_ACCENT : US_MAT_BLOOM);
		}
		int leaving = i >= nf;
		float tx, ty;
		if (f->tgt >= 0 && f->tgt < st->nanch && !leaving) {
			tx = st->anch[f->tgt].x;
			ty = st->anch[f->tgt].y - f->size * 0.4f;
		} else {
			tx = leaving ? (f->x < A * 0.5f ? -0.2f : A + 0.2f) : A * (0.5f + 0.4f * us_noise2(f->ph, d->t * 0.05f));
			ty = 0.4f + 0.15f * us_noise2(f->ph + 3, d->t * 0.07f);
		}
		if (f->mode == 1) {
			f->x = tx;
			f->y = ty;
			f->rest -= dt * (1.0f + 3.0f * lift);
			f->ph += dt * (1.2f + 0.8f * us_noise2(f->ph, d->t)); /* slow wing fanning */
			if (f->rest <= 0 || leaving || f->tgt >= st->nanch) {
				f->mode = 0;
				f->tgt = pick_anchor(st);
				f->vy = -0.1f;
			}
		} else {
			float dx = tx - f->x, dy = ty - f->y;
			float dl = sqrtf(dx * dx + dy * dy) + 1e-5f;
			float sp = 0.16f + 0.05f * d->wind;
			/* fluttering, wandering flight */
			float wx = us_noise2(f->ph * 0.3f, d->t * 1.3f + i), wy = us_noise2(f->ph * 0.3f + 7, d->t * 1.7f + i);
			f->vx += ((dx / dl) * sp + wx * 0.2f + 0.04f * d->wind - f->vx) * dt * 3.0f;
			f->vy += ((dy / dl) * sp + wy * 0.25f - f->vy) * dt * 3.0f;
			f->x += f->vx * dt;
			f->y += f->vy * dt + 0.006f * sinf(f->ph * 0.5f) * dt * 10.0f;
			f->ph += dt * 14.0f;
			if (dl < 0.012f && f->tgt >= 0 && !leaving) {
				f->mode = 1;
				f->rest = us_randr(r, 2.0f, 6.0f);
			}
			if (f->tgt < 0 || f->tgt >= st->nanch)
				f->tgt = pick_anchor(st);
			if (leaving && (f->x < -0.1f || f->x > A + 0.1f))
				f->alive = 0;
		}
		float tilt = f->mode ? 0.0f : us_clampf(f->vx * 2.0f, -0.5f, 0.5f);
		draw_butterfly(c, d->crisp, f->x, f->y, f->size, f->ph, 0.9f, f->mat, tilt);
	}

	for (int i = 0; i < MAX_BEE; i++) {
		fly_t *b = &st->bee[i];
		if (!b->alive) {
			if (i >= nb)
				continue;
			b->alive = 1;
			b->x = us_rand(r) * A;
			b->y = -0.02f;
			b->mode = 0;
			b->tgt = pick_anchor(st);
			b->ph = us_rand(r) * 10;
			b->vx = b->vy = 0;
		}
		int leaving = i >= nb;
		float tx, ty;
		if (b->tgt >= 0 && b->tgt < st->nanch && !leaving) {
			tx = st->anch[b->tgt].x;
			ty = st->anch[b->tgt].y - 0.006f;
		} else {
			tx = b->x + (leaving ? 0.0f : 0.1f);
			ty = leaving ? -0.2f : 0.4f;
		}
		b->ph += dt;
		if (b->mode == 1) {
			/* working the flower: tiny jittering hover */
			b->x = tx + 0.004f * us_noise2(b->ph * 6.0f, i * 3.0f);
			b->y = ty + 0.003f * us_noise2(b->ph * 6.0f, i * 3.0f + 5.0f);
			b->rest -= dt * (1.0f + 4.0f * d->a->kick);
			if (us_rand(r) < dt * 0.6f)
				spawn(st, b->x, b->y, us_randr(r, -0.01f, 0.01f), us_randr(r, -0.01f, 0.0f), 0.002f, US_MAT_LIGHT, 2);
			if (b->rest <= 0 || leaving) {
				b->mode = 0;
				b->tgt = pick_anchor(st);
			}
		} else {
			float dx = tx - b->x, dy = ty - b->y;
			float dl = sqrtf(dx * dx + dy * dy) + 1e-5f;
			float sp = 0.35f;
			b->vx += ((dx / dl) * sp + 0.15f * us_noise2(b->ph * 2.0f, i) - b->vx) * dt * 6.0f;
			b->vy += ((dy / dl) * sp + 0.15f * us_noise2(b->ph * 2.0f, i + 9.0f) - b->vy) * dt * 6.0f;
			b->x += b->vx * dt;
			b->y += b->vy * dt;
			if (dl < 0.01f && !leaving) {
				b->mode = 1;
				b->rest = us_randr(r, 0.8f, 2.5f);
			}
			if (b->tgt < 0 || b->tgt >= st->nanch)
				b->tgt = pick_anchor(st);
			if (leaving && b->y < -0.1f)
				b->alive = 0;
		}
		float bl = 0.0045f;
		float ang = atan2f(b->vy, b->vx + 1e-4f);
		if (b->mode == 1)
			ang = 0;
		float cx = cosf(ang) * bl, cy = sinf(ang) * bl;
		wline(c, b->x - cx, b->y - cy, b->x + cx, b->y + cy, d->crisp ? 0.005f : 0.0028f, 0.85f, US_MAT_LIGHT);
		/* wing blur */
		float wf = 0.5f + 0.5f * sinf(b->ph * 90.0f);
		if (d->crisp)
			us_put(c, (int)(b->x * c->s), (int)((b->y - 0.006f) * c->s), 0.5f + 0.3f * wf, US_MAT_WATER);
		else {
			us_arc(c, b->x - 0.002f, b->y - 0.004f, 0.0035f, US_PI, US_TAU, 0.0009f, 0.35f + 0.25f * wf, US_MAT_WATER);
			us_arc(c, b->x + 0.002f, b->y - 0.004f, 0.0035f, US_PI, US_TAU, 0.0009f, 0.35f + 0.25f * wf, US_MAT_WATER);
		}
	}
}

/* ---- frame ----------------------------------------------------------- */

static void life_cycle(st_t *st, plant_t *p, const us_ctx *ctx, float grow_k, float drive, dc_t *d)
{
	float dt = ctx->dt;
	us_world *w = ctx->w;
	p->phase_t += dt;
	switch (p->phase) {
	case GROW:
		p->g += dt * grow_k * drive;
		if (p->g >= 1.0f) {
			p->g = 1.0f;
			p->phase = BLOOM;
			p->phase_t = 0;
		}
		if (d->winter > 0.7f && p->phase_t > 20.0f && us_rand(&st->rng) < dt * 0.01f) {
			p->phase = WITHER;
			p->phase_t = 0;
		}
		break;
	case BLOOM: {
		float season_ok = d->spring + d->summer + 0.4f * d->autumn;
		float target = us_sat((0.25f + 0.85f * st->bass_env) * (0.3f + 0.7f * season_ok));
		p->open = us_approach(p->open, target, target > p->open ? 0.25f : 1.2f, ctx->real_dt);
		if (p->layer == 2)
			us_world_add(w, &w->bloom, 0.002f * p->open, dt);
		if (p->phase_t > p->bloom_len * (1.2f - 0.5f * ctx->a->swell)) {
			p->phase = SEED;
			p->phase_t = 0;
		}
		break;
	}
	case SEED:
		p->open = us_approach(p->open, 0, 3.0f, dt);
		p->seedness = us_sat(p->phase_t / 8.0f);
		if (p->layer == 2)
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
			new_plant(st, p, nx, 0.0f, p->layer);
		}
		break;
	}
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *st = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, A = ctx->aspect;
	float dens = ctx->p->density;
	dc_t d;
	memset(&d, 0, sizeof(d));
	d.c = c;
	d.st = st;
	d.a = a;
	d.crisp = ctx->crisp;
	d.A = A;
	d.dt = dt;
	d.t = (float)ctx->t;
	d.spring = us_world_season_w(w, 0);
	d.summer = us_world_season_w(w, 1);
	d.autumn = us_world_season_w(w, 2);
	d.winter = us_world_season_w(w, 3);

	static const float want_base[NL] = {9.0f, 6.0f, 5.0f}, want_d[NL] = {13.0f, 11.0f, 10.0f};
	for (int L = 0; L < NL; L++) {
		int want = (int)(want_base[L] + dens * want_d[L]);
		if (want > MAX_PL)
			want = MAX_PL;
		if (!st->inited || want != st->npl[L]) {
			for (int i = st->inited ? st->npl[L] : 0; i < want; i++) {
				float x = (i + 0.5f) / want + us_randr(&st->rng, -0.35f, 0.35f) / want;
				float g0 = us_sat(w->growth * us_randr(&st->rng, 0.3f, 2.2f));
				plant_t *p = &st->pl[L][i];
				new_plant(st, p, x, g0, L);
				if (g0 >= 1.0f || us_rand(&st->rng) < w->bloom + (L == 0 ? 0.3f : 0.0f)) {
					p->g = 1.0f;
					p->phase = BLOOM;
					p->phase_t = us_randr(&st->rng, 0, p->bloom_len * 0.6f);
				}
			}
			st->npl[L] = want;
		}
	}
	if (!st->inited) {
		for (int b = 0; b < MAX_BLADES; b++) {
			st->blade_x[b] = us_rand(&st->rng);
			st->blade_h[b] = us_randr(&st->rng, 0.3f, 1.0f);
			st->blade_ph[b] = us_rand(&st->rng) * 10;
		}
		st->inited = 1;
	}

	st->bass_env = us_approach(st->bass_env, us_sat(a->bass * 0.8f + a->kick * 0.6f), 0.08f, ctx->real_dt);
	float wind = us_sat(0.15f * w->wind + 0.9f * a->high + 0.4f * a->air);
	st->wind_t += dt * (0.4f + 1.6f * wind);
	d.wind = wind;
	d.wind_t = st->wind_t;

	us_world_add(w, &w->growth, 0.004f * a->bass * (1 - d.winter), dt);

	/* ---- sky: stars by night, sun / moon ---- */
	{
		float sv = us_sat(1.0f - w->daylight * 2.2f);
		if (sv > 0.01f && !ctx->crisp)
			for (int i = 0; i < 70; i++) {
				uint32_t h = (uint32_t)i * 2654435761u;
				float sx = hashf(h) * A, sy = hashf(h + 1) * 0.55f;
				float tw = 0.6f + 0.4f * us_noise2(i * 1.37f, d.t * 1.1f);
				us_point(c, sx, sy, 0.001f, 0.35f * sv * tw * (0.3f + 0.7f * hashf(h + 2)), US_MAT_LIGHT);
			}
		float ph = w->day_phase;
		float cx = A * (0.08f + 0.84f * ph);
		float cy = 0.78f + cosf(ph * US_TAU) * 0.55f;
		if (cy < 0.72f) {
			if (w->daylight > 0.35f) {
				if (ctx->crisp)
					us_disc(c, cx, cy, 0.035f, 0.5f, US_MAT_LIGHT);
				else
					us_ring(c, cx, cy, 0.035f + 0.004f * st->bass_env, 0.0015f, 0.55f, US_MAT_LIGHT);
			} else {
				us_arc(c, cx, cy, 0.028f, -1.9f, 1.9f, ctx->crisp ? 0.006f : 0.0015f, 0.5f, US_MAT_LIGHT);
				us_arc(c, cx + 0.01f, cy, 0.024f, -1.6f, 1.6f, ctx->crisp ? 0.004f : 0.0012f, 0.35f, US_MAT_LIGHT);
			}
		}
	}

	float grow_k = 0.06f * (0.35f + w->moisture) * (0.6f + 1.2f * w->mycelium) * (1.0f - 0.85f * d.winter);
	float drive = 0.12f + 1.1f * a->bass + 0.6f * a->kick + 0.2f * a->level;
	st->nanch = 0;

	for (int L = 0; L < NL; L++) {
		d.L = L;
		d.lb = LBRIGHT[L];
		d.lw = L == 2 ? 1.0f : (L == 1 ? 0.8f : 0.6f);
		/* ---- ground for this layer: hides the bases of what is behind ---- */
		{
			float gv;
			uint8_t gm;
			if (ctx->crisp) {
				gm = L == 0 ? US_MAT_BG : US_MAT_EARTH;
				gv = L == 0 ? 0.0f : (L == 1 ? 0.2f : 0.13f);
				if (d.winter > 0.55f && L > 0) { /* snowfield */
					gm = US_MAT_WATER;
					gv = L == 1 ? 0.36f : 0.3f;
				}
			} else {
				gm = US_MAT_BG;
				gv = 0.0f;
			}
			fill_ground(c, L, A, gv, gm);
			float pts[2 * 65];
			for (int i = 0; i <= 64; i++) {
				float u = i / 64.0f;
				pts[2 * i] = u * A;
				pts[2 * i + 1] = ground_y(u, L);
			}
			float snow = us_sat((d.winter - 0.4f) * 2.5f);
			wpoly(c, pts, 65, (L == 2 ? 0.0016f : 0.0012f) + (ctx->crisp ? 0.004f : 0.001f) * snow,
				    (0.32f + 0.25f * snow) * (0.5f + 0.5f * LBRIGHT[L]) + 0.2f * (L == 2), snow > 0.5f ? US_MAT_WATER : US_MAT_EARTH);
		}
		/* ---- grass tufts along this layer's crest ---- */
		{
			int nbmax = L == 2 ? MAX_BLADES : (L == 1 ? MAX_BLADES * 2 / 3 : MAX_BLADES / 2);
			int nb = (int)(nbmax * (0.25f + 0.75f * dens) * (0.2f + 0.8f * us_sat(w->growth + 0.2f)));
			float gh = 0.035f * LSCALE[L] * (0.4f + 0.8f * w->growth) * (1.0f - 0.5f * d.winter);
			uint8_t gm = d.winter > 0.55f ? US_MAT_EARTH : US_MAT_LEAF;
			for (int b = 0; b < nb; b++) {
				float u = st->blade_x[(b * 7 + L * 61) % MAX_BLADES];
				float x = u * A;
				float gy = ground_y(u, L);
				float bh = st->blade_h[(b + L * 13) % MAX_BLADES];
				float h = gh * bh * (1.0f + 0.25f * st->bass_env);
				float sw = (us_noise2(x * 2.0f - st->wind_t * 0.8f, st->blade_ph[b]) * 0.7f + 0.5f) * (0.15f + 1.3f * wind);
				float pts[8];
				for (int k = 0; k < 4; k++) {
					float t = k / 3.0f;
					pts[2 * k] = x + sw * h * t * t * 0.9f;
					pts[2 * k + 1] = gy - h * t;
				}
				uint8_t bm = gm;
				float bv = (0.35f + 0.2f * bh) * (0.45f + 0.55f * LBRIGHT[L]);
				if (d.autumn > 0.55f) { /* straw and rust */
					bm = (b & 3) == 0 ? US_MAT_LIGHT : US_MAT_EARTH;
					bv *= bm == US_MAT_LIGHT ? 0.6f : 1.2f;
				}
				wpoly(c, pts, 4, 0.001f, bv, bm);
			}
		}
		/* ---- plants ---- */
		for (int i = 0; i < st->npl[L]; i++) {
			plant_t *p = &st->pl[L][i];
			life_cycle(st, p, ctx, grow_k, drive, &d);
			float gx = p->x * A;
			float gy = ground_y(p->x, L) + 0.002f;
			float wither = p->phase == WITHER ? us_sat(1.0f - p->g) : 0.0f;
			float vbase = (0.85f - 0.4f * wither) * LBRIGHT[L];
			int pi = i + L * 37;
			switch (p->species) {
			case SP_UMBEL: draw_umbel(&d, p, pi, gx, gy, vbase); break;
			case SP_FERN: draw_fern(&d, p, pi, gx, gy, vbase); break;
			case SP_GRASS: draw_grass(&d, p, pi, gx, gy, vbase); break;
			case SP_SPIKE: draw_spike(&d, p, pi, gx, gy, vbase); break;
			case SP_VINE: draw_vine(&d, p, pi, gx, gy, vbase); break;
			default: draw_wild(&d, p, pi, gx, gy, vbase); break;
			}
		}
	}
	d.L = 2;
	d.lb = 1.0f;
	d.lw = 1.0f;

	/* ---- butterflies + bees ---- */
	insects(&d, w, dens);

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
		float gy = ground_y(us_sat(q->x / A), 2);
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

	/* ---- snowfall in winter (stateless flakes) ---- */
	if (d.winter > 0.3f) {
		int nfl = (int)((40 + 160 * dens) * us_sat((d.winter - 0.3f) * 2.0f));
		for (int i = 0; i < nfl; i++) {
			uint32_t h = (uint32_t)i * 2246822519u + 77u;
			float sp = 0.03f + 0.04f * hashf(h);
			float y = fmodf(hashf(h + 1) + d.t * sp, 1.0f);
			float x = fmodf(hashf(h + 2) * (A + 0.2f) + d.t * (0.01f + 0.05f * wind) + 0.02f * sinf(d.t * 0.7f + i), A + 0.2f) - 0.1f;
			float r = 0.0009f + 0.0014f * hashf(h + 3);
			us_point(c, x, y, r, 0.45f + 0.4f * hashf(h + 4), US_MAT_WATER);
		}
	}
}

const us_scene_vt us_scene_meadow = {"meadow", "Meadow", 0.0f, create, destroy, frame};
