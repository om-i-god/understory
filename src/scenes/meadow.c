/* Understory scene: Meadow — plants grow & bloom with the bass, sway with the highs.
 *
 * Three depth layers of rolling meadow (far hills faint and small, near bank tall and bright); the
 * nearer hill occludes the bases of the plants behind it. Six species, each with a seeded genome and a
 * life cycle SPROUT/GROW -> BLOOM -> SEED -> WITHER -> reseed nearby:
 *   wildflower (branching skeleton, star flowers)   umbel (Queen Anne's lace: dome -> bird's-nest seed head)
 *   fern (fiddleheads that unfurl with the bass)     oat grass (drooping panicles, anthers dangle in bloom)
 *   foxglove spike (bells open bottom-up)            vine (bindweed climbing last year's stalk, trumpets)
 * In front of the bank stand the focal plants: an oxeye cluster on the left third (big heads with a
 * golden-angle disc) and a dandelion clock on the right third; tall blades at the lens frame the corners.
 * Butterflies and bees visit open blooms when the world has insects / pollen.
 * Sky: blue ridged mountains and a wooded ridge behind the meadow (aerial perspective: fainter, finer,
 * cooler with distance), valley mist, cirrus combs, cumulus that drift downwind and swell with the
 * phrase (rims lit from the sun / moon side), a sun with breathing halo + crepuscular shafts, twilight
 * afterglow over the mountains, a haloed moon, stars and the Milky Way; swallows (bats after dark) and
 * skeins of geese.
 * Music: loudness is the meadow's light and fullness (quiet: half-closed flowers at dusk; loud: open and
 * bright). Kick: every stem bows downwind together and the flower heads swell. Snare: a quick shiver,
 * flower heads twitch, pollen bursts from open flowers and the dandelion lets seeds go. Hats: dew on the
 * grass tips glints, a different handful each tick. Bass drives growth; highs + world wind drive sway.
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
#define MAX_CLOUD 7
#define MAX_BUMP 11
#define MAX_BIRD 16
#define NDAISY 16 /* front-bank flowers, one per spectrum band */
#define MAX_FF 14 /* fireflies */
#define NMIDGE 22
#define NHERO 3     /* the focal oxeye cluster */
#define NCLOCK 56   /* seeds on the dandelion clock */
#define MAX_PUFF 24 /* pollen puffs a snare knocks out of the flowers */
#define MAX_TIP 96  /* near grass tips where the hats glint */

/* crisp-canvas ink values that land on palette shade steps 1..3 (after the compose gamma lift) */
#define STEP1 0.17f
#define STEP2 0.38f
#define STEP3 0.62f

static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

enum { GROW = 0, BLOOM, SEED, WITHER };
enum { SP_WILD = 0, SP_UMBEL, SP_FERN, SP_GRASS, SP_SPIKE, SP_VINE, SP_COUNT };

static const float LSCALE[NL] = {0.34f, 0.6f, 1.08f};
static const float LBRIGHT[NL] = {0.36f, 0.6f, 1.0f};

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
	uint8_t mat, kind; /* kind 0 petal, 1 leaf, 2 seed/pollen, 3 dandelion parachute */
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
	float x, y, s; /* x: 0..1 of (width + margin), y: base line, s: scale */
	float spd, depth; /* depth 0 far (small, faint) .. 1 near */
	int nb;
	float bx[MAX_BUMP], by[MAX_BUMP], br[MAX_BUMP]; /* bumps relative to (x, y), units of s */
} cloud_t;

typedef struct {
	float x, y, vx, vy, ph, size;
	int alive, kind; /* kind 0 skein member, 1 swallow */
	int slot;
} bird_t;

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
	float wind_t, wind_env, bloom_env;
	/* sky */
	int sky_inited;
	cloud_t cloud[MAX_CLOUD];
	bird_t bird[MAX_BIRD];
	float skein_x, skein_y, skein_t, skein_dir, skein_wait;
	int skein_n;
	float swell_env;
	/* eased audio features (each one owns a distinct part of the meadow) */
	float sub_env, lm_env, mid_env, hi_env, air_env, cen_env, wid_env;
	float band_env[NDAISY];
	float midge_t, ff_t;  /* accumulated phases (rate changes never jump) */
	/* front-bank flowers tied to bands */
	float daisy_x[NDAISY], daisy_h[NDAISY], daisy_ph[NDAISY];
	uint8_t daisy_kind[NDAISY];
	/* a snail crossing the near bank */
	float snail_x, snail_dir, snail_hide;
	/* drums + loudness, each with its own answer */
	float glow;            /* eased loudness: louder = fuller bloom, more light */
	float bow, bow_v;      /* kick: the whole meadow bows (a damped spring knocked on each hit) */
	float rat, rat_v;      /* snare: a quick rattle through the grass */
	uint32_t hat_seed;     /* hats: which dew drops glint this time */
	float puff_x[MAX_PUFF], puff_y[MAX_PUFF], puff_age[MAX_PUFF], puff_r[MAX_PUFF];
	uint8_t puff_mat[MAX_PUFF];
	int npuff;
	/* the focal plants: an oxeye cluster on the left third, a dandelion clock on the right */
	uint8_t clock_on[NCLOCK]; /* seeds still on the clock */
	float clock_regrow;
	float tip_x[MAX_TIP], tip_y[MAX_TIP];
	int ntip;
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
	float puff;                 /* > 0 on the frame a kick lands: it shakes seed loose */
	float lm, mid;              /* eased lowmid (fern breath), mid (foxglove bells ring) */
	float bow;                  /* kick bow of every stem (radians-ish, added to sway) */
	float rat;                  /* snare rattle of the grasses */
	float kp;                   /* kick pulse 0..1: flower heads swell + brighten */
	float lift;                 /* loudness light: multiplies plant brightness (quiet ~0.7, loud ~1.15) */
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

/* two ground tones Bayer-dithered: fraction fa0 of tone a at the crest, easing to fa1 `depth` below it */
typedef struct {
	float va, vb;
	uint8_t ma, mb;
	float fa0, fa1, depth;
} tone_t;

/* overwrite (not max) everything below a layer's ground line: the nearer hill hides what is behind it */
static void fill_ground(us_canvas *c, int L, float A, const tone_t *tn)
{
	int iv = (int)(us_sat(tn->vb) * 255.0f + 0.5f), ia = (int)(us_sat(tn->va) * 255.0f + 0.5f);
	uint8_t mat = tn->mb;
	int two = ia != iv || tn->ma != tn->mb;
	float dpx = tn->depth * c->s + 1.0f;
	float g0a = 0, g0b = 0, g1a = 0, g1b = 0;
	for (int px = 0; px < c->w; px++) {
		if ((px & 3) == 0) { /* the crests are smooth: sample every 4th column, lerp between */
			float ua = (px + 0.5f) / c->s / A, ub = (px + 4.5f) / c->s / A;
			g0a = ground_y(ua, L);
			g0b = ground_y(ub, L);
			if (L < 2) {
				g1a = ground_y(ua, L + 1);
				g1b = ground_y(ub, L + 1);
			}
		}
		float f = (px & 3) * 0.25f;
		int y0 = (int)ceilf(us_lerp(g0a, g0b, f) * c->s);
		if (y0 < 0)
			y0 = 0;
		/* the next (nearer) layer overwrites everything below its own crest anyway */
		int y1 = L < 2 ? (int)ceilf(us_lerp(g1a, g1b, f) * c->s) + 2 : c->h;
		if (y1 > c->h)
			y1 = c->h;
		if (two) {
			for (int py = y0; py < y1; py++) {
				size_t i = (size_t)py * c->w + px;
				float f = us_lerp(tn->fa0, tn->fa1, us_sat((py - y0) / dpx));
				int a = BAYER[((py & 3) << 2) | (px & 3)] < (int)(f * 16.0f + 0.5f);
				c->ink[i] = (uint8_t)(a ? ia : iv);
				c->mat[i] = a ? tn->ma : mat;
			}
			continue;
		}
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

/* fast ring from Wu lines (us_ring is exact but costly for big faint halos) */
static void wring(us_canvas *c, float x, float y, float r, float width, float v, uint8_t mat)
{
	if (c->crisp) {
		us_ring(c, x, y, r, width, v, mat);
		return;
	}
	int n = (int)(r * c->s * 0.6f);
	if (n < 6) n = 6;
	if (n > 128) n = 128;
	float px = x + r, py = y;
	for (int i = 1; i <= n; i++) {
		float a = US_TAU * i / n;
		float qx = x + cosf(a) * r, qy = y + sinf(a) * r;
		wline(c, px, py, qx, qy, width, v, mat);
		px = qx;
		py = qy;
	}
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
	return us_noise2(gx * 1.7f - d->wind_t * 0.6f, k) * (0.03f + 0.22f * d->wind) + d->bow + 0.3f * d->rat;
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
	static const float HFIT[NL] = {1.0f, 0.88f, 0.7f}; /* the near bank stays under the focal plants */
	p->height = us_randr(r, hlo[p->species], hhi[p->species]) * LSCALE[L] * HFIT[L];
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
		float sway = (us_noise2(gx * 1.7f - d->wind_t * 0.6f, k * 0.31f + pi) * (0.03f + 0.22f * d->wind) + d->bow + 0.3f * d->rat) *
			     (0.3f + hgt);
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
				if (d->L > 0 && d->puff > 0 && us_rand(&st->rng) < 0.7f * d->puff) /* the kick shakes seed loose */
					for (int q = 0; q < 2; q++)
						spawn(st, ex, ey, us_randr(&st->rng, 0.04f, 0.12f), us_randr(&st->rng, -0.07f, -0.01f), 0.004f,
						      US_MAT_ACCENT, 2);
			} else {
				float op = p->open, kp = d->kp * op;
				float rot = s->petal_rot + 0.3f * sway + 6.0f * d->rat; /* a snare twitches the head */
				for (int q = 0; q < np; q++) {
					float qa = rot + US_TAU * q / np;
					petal(c, d->crisp, ex, ey, qa, fr * (0.25f + 0.75f * op) * (1.0f + 0.5f * kp), fr * 0.22f * (0.4f + 0.6f * op),
					      us_sat((0.6f + 0.4f * op + 0.35f * kp) * vbase), fm);
				}
				us_disc(c, ex, ey, fr * (0.12f + 0.08f * kp), us_sat(vbase * (1.0f + 0.4f * kp)), US_MAT_LIGHT);
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
		float rl = h * 0.12f * (0.45f + 0.55f * fmaxf(op, seed * 0.7f)) * bud * (1.0f + 0.3f * d->kp * op);
		float spread = (0.35f + 0.9f * op) * (1.0f - 0.55f * seed);
		uint8_t fm = d->autumn > 0.55f || dry ? US_MAT_EARTH : US_MAT_ACCENT;
		float fv = us_sat((0.55f + 0.45f * op + 0.3f * d->kp * op) * vbase * (dry ? 0.9f : 1.0f));
		for (int k = 0; k < nr; k++) {
			float f = nr > 1 ? (float)k / (nr - 1) : 0.5f;
			float phi = (f - 0.5f) * 2.0f * spread + (ang + US_PI * 0.5f) * 0.6f + 3.0f * d->rat;
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
		if ((p->phase == SEED || dry) && d->L > 0 && d->puff > 0) /* a kick knocks a puff out of the nest */
			for (int q = 0; q < 3; q++)
				if (us_rand(&st->rng) < 0.8f * d->puff)
					spawn(st, tx + us_randr(&st->rng, -rl, rl) * 0.5f, ty - rl, us_randr(&st->rng, 0.04f, 0.13f),
					      us_randr(&st->rng, -0.08f, -0.01f), 0.004f, US_MAT_ACCENT, 2);
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
			float arch = side * (0.045f + 0.03f * dead) * (0.5f + s) * (1.0f - 0.5f * d->lm * (1.0f - dead));
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
					float pa = ha + sd * (1.05f - 0.25f * dead + 0.25f * d->lm) + (sd * side < 0 ? 0.15f : 0) + dead * 0.6f;
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
	float face = us_rand(&gr) < 0.5f ? -1.0f : 1.0f; /* foxglove bells all hang to one side of the raceme */
	if (lf > 0.05f)
		for (int k = 0; k < 6; k++) {
			float la = -US_PI * 0.5f + (k & 1 ? -1.0f : 1.0f) * (0.65f + 0.22f * (k >> 1)) + 0.12f * sway_n(d, gx, k + 9.0f);
			float ll = H * (0.18f - 0.025f * (k >> 1)) * us_sat(p->g * 2.5f) * lf;
			petal(c, d->crisp, gx, gy, la, ll, ll * 0.2f, 0.62f * vbase, leaf_mat(d, p->gene + k));
		}
	enum { NS = 14 };
	float pts[2 * (NS + 1)];
	float frac = us_sat(p->g * 1.1f);
	if (frac < 0.05f)
		return;
	/* the stalk: gently curved, the flowering tip nods over toward the bells and sways */
	{
		float lean = us_randn(&gr) * 0.05f, curve = us_randn(&gr) * 0.008f;
		float ang = -US_PI * 0.5f + lean, x = gx, y = gy, seg = H * frac / NS;
		float sw = sway_n(d, gx, pi + 0.3f);
		pts[0] = x;
		pts[1] = y;
		for (int k = 0; k < NS; k++) {
			float hgt = (float)k / NS;
			float nod = face * 0.07f * us_smooth(0.6f, 1.0f, hgt) * frac;
			ang += curve + nod + sw * (0.2f + hgt) * 0.5f;
			x += cosf(ang) * seg;
			y += sinf(ang) * seg;
			pts[2 * k + 2] = x;
			pts[2 * k + 3] = y;
		}
	}
	uint8_t sm = d->winter > 0.5f ? US_MAT_EARTH : US_MAT_STEM;
	for (int k = 0; k < NS; k++) /* tapering stem */
		wline(c, pts[2 * k], pts[2 * k + 1], pts[2 * k + 2], pts[2 * k + 3], lw(d, 0.0026f - 0.0013f * k / NS), 0.8f * vbase, sm);
	/* small stem leaves on the lower half */
	if (lf > 0.05f)
		for (int i = 2; i < 7; i++) {
			float dx = pts[2 * i + 2] - pts[2 * i], dy = pts[2 * i + 3] - pts[2 * i + 1];
			float ha = atan2f(dy, dx);
			float ll = H * (0.085f - 0.009f * i) * lf;
			petal(c, d->crisp, pts[2 * i], pts[2 * i + 1], ha + ((i & 1) ? 0.85f : -0.85f), ll, ll * 0.24f, 0.6f * vbase,
			      leaf_mat(d, p->gene + 31 + i));
		}
	if (frac < 0.55f)
		return;
	int nbell = d->L == 0 ? 8 : 16;
	uint8_t fm = flower_mat(p);
	float op = p->open;
	int dry = d->winter > 0.5f || p->phase == WITHER;
	for (int b = 0; b < nbell; b++) {
		float bf = (float)b / nbell;
		float s = 0.4f + 0.58f * powf(bf, 0.85f); /* crowding toward the tip */
		if (s > frac)
			break;
		float t = s * NS;
		int i0 = (int)t;
		if (i0 > NS - 1)
			i0 = NS - 1;
		float ft = t - i0;
		float nx = us_lerp(pts[2 * i0], pts[2 * i0 + 2], ft), ny = us_lerp(pts[2 * i0 + 1], pts[2 * i0 + 3], ft);
		float sang = atan2f(pts[2 * i0 + 3] - pts[2 * i0 + 1], pts[2 * i0 + 2] - pts[2 * i0]);
		/* slight spiral: most bells face out, every third peeks round the front or back */
		float sd = face * ((b % 3) == 2 ? -0.55f : 1.0f);
		float jit = 0.25f * (hashf(p->gene + b * 7u) - 0.5f);
		if (p->phase == SEED || dry) {
			float cap = us_sat(1.0f - bf * 1.1f);
			if (cap > 0.1f) {
				float cx = nx + sd * 0.004f * LSCALE[d->L], cy = ny + 0.002f;
				us_disc(c, cx, cy, 0.0024f * LSCALE[d->L] + 0.0008f, 0.6f * vbase, dry ? US_MAT_EARTH : US_MAT_ACCENT);
				if (!d->crisp)
					wline(c, cx, cy, cx + sd * 0.002f, cy - 0.004f * LSCALE[d->L], 0.001f, 0.5f * vbase, dry ? US_MAT_EARTH : US_MAT_ACCENT);
			}
			continue;
		}
		/* opening wave climbs the spike: lower bells are open, the tip stays in tight buds */
		float ob = us_sat(op * 1.7f - bf * 1.1f);
		float bl = H * (0.075f - 0.042f * bf);
		if (ob < 0.12f || bf > 0.82f) {
			float br = (0.0011f + 0.0016f * LSCALE[d->L]) * (1.0f - 0.5f * bf);
			float bx = nx + sd * br * 1.2f, by = ny + br * 0.4f;
			if (d->crisp)
				us_disc(c, bx, by, br, 0.6f * vbase, US_MAT_LEAF);
			else {
				petal(c, 0, nx, ny, sang + US_PI * 0.5f * sd + 0.4f * sd, br * 2.6f, br * 0.8f, 0.6f * vbase, ob > 0.05f ? fm : US_MAT_LEAF);
			}
			continue;
		}
		/* a nodding bell: tube hanging out and down from a short pedicel, flared lower lip */
		float ring = d->mid * 0.32f * sinf(d->t * 7.0f + b * 1.3f + pi) * (0.4f + 0.6f * bf);
		float ba = US_PI * 0.5f - sd * (0.95f - 0.45f * ob) + jit + 0.6f * sway_n(d, gx, pi + b * 0.37f) + ring;
		float x0 = nx + sd * bl * 0.18f, y0 = ny - bl * 0.02f;
		float bv = us_sat((0.6f + 0.4f * ob + 0.3f * d->kp * ob) * vbase);
		float L2 = bl * (0.55f + 0.45f * ob) * (1.0f + 0.25f * d->kp * ob);
		float ux = cosf(ba), uy = sinf(ba), vx = -uy, vy = ux;
		float w0 = bl * 0.12f, w1 = bl * (0.2f + 0.14f * ob);
		float mx = x0 + ux * L2, my = y0 + uy * L2;
		if (d->crisp) {
			wline(c, nx, ny, x0, y0, 0.003f, 0.6f * vbase, US_MAT_STEM);
			us_tri(c, x0 + vx * w0, y0 + vy * w0, x0 - vx * w0, y0 - vy * w0, mx + vx * w1, my + vy * w1, bv, fm);
			us_tri(c, x0 - vx * w0, y0 - vy * w0, mx - vx * w1, my - vy * w1, mx + vx * w1, my + vy * w1, bv, fm);
			continue;
		}
		float wl = lw(d, 0.0012f);
		wline(c, nx, ny, x0, y0, lw(d, 0.001f), 0.6f * vbase, US_MAT_STEM);
		/* the two sides of the tube bulge gently; the lower lip reaches further than the upper */
		float sx = vx * (sd > 0 ? 1.0f : -1.0f), sy = vy * (sd > 0 ? 1.0f : -1.0f); /* +s = upper side */
		float up[] = {x0 + sx * w0, y0 + sy * w0, x0 + ux * L2 * 0.5f + sx * w1 * 0.95f, y0 + uy * L2 * 0.5f + sy * w1 * 0.95f,
			      mx + sx * w1, my + sy * w1, mx + sx * w1 * 1.3f + ux * bl * 0.06f, my + sy * w1 * 1.3f + uy * bl * 0.06f};
		float lo[] = {x0 - sx * w0, y0 - sy * w0, x0 + ux * L2 * 0.5f - sx * w1 * 0.85f, y0 + uy * L2 * 0.5f - sy * w1 * 0.85f,
			      mx - sx * w1, my - sy * w1, mx - sx * w1 * 1.15f + ux * bl * 0.22f, my - sy * w1 * 1.15f + uy * bl * 0.22f};
		wpoly(c, up, 4, wl, bv, fm);
		wpoly(c, lo, 4, wl, bv, fm);
		/* the mouth: an open ellipse seen at an angle */
		float q[2 * 9];
		float mr = w1 * 1.05f;
		for (int e = 0; e <= 8; e++) {
			float ea = e * US_TAU / 8;
			q[2 * e] = mx + ux * bl * 0.04f + sx * mr * cosf(ea) + ux * mr * 0.4f * sinf(ea);
			q[2 * e + 1] = my + uy * bl * 0.04f + sy * mr * cosf(ea) + uy * mr * 0.4f * sinf(ea);
		}
		wpoly(c, q, 9, wl, bv * 0.85f, fm);
		/* freckles down the throat */
		for (int f = 0; f < 3; f++) {
			float ft2 = 0.45f + 0.18f * f;
			us_point(c, x0 + ux * L2 * ft2 - sx * w1 * 0.3f, y0 + uy * L2 * ft2 - sy * w1 * 0.3f, 0.0009f, bv * 0.75f, US_MAT_LIGHT);
		}
		if (d->L > 0 && b % 3 == 0)
			add_anchor(st, mx, my, ob);
		if (d->L > 0 && d->a->beat && ob > 0.8f && us_rand(&st->rng) < 0.04f * d->a->onset)
			spawn(st, mx, my, us_randr(&st->rng, 0.0f, 0.05f), 0.0f, bl * 0.5f, fm, 0);
	}
	snowcap(d, pts[2 * NS], pts[2 * NS + 1], 0.004f);
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
			float tl = 0.03f * LSCALE[d->L] * (0.6f + 0.4f * fo) * (1.0f + 0.3f * d->kp * fo);
			float ta = -US_PI * 0.5f + sd * (0.7f + 0.3f * sway_n(d, gx, i * 0.2f));
			float mx = x + cosf(ta) * tl, my = y + sinf(ta) * tl;
			float mr = tl * (0.12f + 0.42f * fo);
			float nx = -sinf(ta), ny = cosf(ta);
			float bv = us_sat((0.5f + 0.5f * fo + 0.3f * d->kp * fo) * vbase);
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
			/* rounded forewing + hindwing outlines, an inner vein contour and an eyespot */
			float am = 0.5f * (a1x + a2x), amy = 0.5f * (a1y + a2y);
			float p1[] = {RX(0, -sz * 0.2f), RY(0, -sz * 0.2f), RX(a1x * 0.55f, a1y * 1.02f), RY(a1x * 0.55f, a1y * 1.02f),
				      RX(a1x, a1y), RY(a1x, a1y), RX(am * 1.1f, amy), RY(am * 1.1f, amy), RX(a2x, a2y), RY(a2x, a2y),
				      RX(0, sz * 0.05f), RY(0, sz * 0.05f)};
			wpoly(c, p1, 6, 0.0013f, v, m);
			float bm = 0.5f * (b1x + b2x) * 1.15f, bmy = 0.5f * (b1y + b2y) * 1.1f;
			float p2[] = {RX(0, sz * 0.05f), RY(0, sz * 0.05f), RX(b1x, b1y), RY(b1x, b1y), RX(bm, bmy), RY(bm, bmy),
				      RX(b2x, b2y), RY(b2x, b2y), RX(0, sz * 0.3f), RY(0, sz * 0.3f)};
			wpoly(c, p2, 5, 0.0013f, v * 0.85f, m);
			float i1[] = {RX(0, -sz * 0.05f), RY(0, -sz * 0.05f), RX(a1x * 0.6f, a1y * 0.62f), RY(a1x * 0.6f, a1y * 0.62f),
				      RX(am * 0.62f, amy * 0.5f), RY(am * 0.62f, amy * 0.5f)};
			wpoly(c, i1, 3, 0.001f, v * 0.5f, m);
			us_point(c, RX(a1x * 0.62f, a1y * 0.45f), RY(a1x * 0.62f, a1y * 0.45f), sz * 0.09f * (0.4f + 0.6f * fx), v, US_MAT_LIGHT);
			us_point(c, RX(b1x * 0.6f, b1y * 1.3f), RY(b1x * 0.6f, b1y * 1.3f), sz * 0.06f * (0.4f + 0.6f * fx), v * 0.8f, US_MAT_LIGHT);
		}
	}
	wline(c, RX(0, -sz * 0.35f), RY(0, -sz * 0.35f), RX(0, sz * 0.45f), RY(0, sz * 0.45f), crisp ? 0.003f : 0.0016f, v,
		US_MAT_STEM);
	if (!crisp) { /* antennae */
		wline(c, RX(0, -sz * 0.35f), RY(0, -sz * 0.35f), RX(-sz * 0.22f, -sz * 0.75f), RY(-sz * 0.22f, -sz * 0.75f), 0.001f, v * 0.7f, US_MAT_STEM);
		wline(c, RX(0, -sz * 0.35f), RY(0, -sz * 0.35f), RX(sz * 0.22f, -sz * 0.75f), RY(sz * 0.22f, -sz * 0.75f), 0.001f, v * 0.7f, US_MAT_STEM);
	}
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
	/* a hat (bright transient) makes the bees dart */
	float hat = d->a->beat && d->a->high > 0.4f ? us_sat(d->a->high * (0.5f + d->a->onset)) : 0.0f;

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
			f->ph += dt * 14.0f * (1.0f + 0.7f * d->mid); /* the melody quickens the wingbeat */
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
		draw_butterfly(c, d->crisp, f->x, f->y, f->size * (d->crisp ? 1.0f : 1.55f), f->ph, 0.95f, f->mat, tilt);
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
			if (hat > 0 && us_rand(r) < 0.55f * hat) { /* startled off the flower */
				b->rest = 0;
				b->vx = us_randr(r, -0.7f, 0.7f) * hat;
				b->vy = -us_randr(r, 0.2f, 0.6f) * hat;
			}
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
			if (hat > 0 && us_rand(r) < 0.6f * hat) { /* a quick jink sideways, eased out by the steering */
				b->vx += us_randr(r, -0.6f, 0.6f) * hat;
				b->vy += us_randr(r, -0.4f, 0.3f) * hat;
			}
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

/* ---- sky: far mountains, a wooded ridge, clouds, sun / moon, birds ---------- */

static float far_y(float u, int k)
{
	if (k == 0) { /* blue mountains: ridged noise, sharp crests */
		float r = 1.0f - fabsf(us_noise2(u * 3.1f + 11.0f, 3.7f));
		float r2 = 1.0f - fabsf(us_noise2(u * 7.3f + 2.0f, 9.1f));
		return 0.54f - 0.09f * r * r - 0.03f * r2 * r2 * r - 0.025f * us_noise2(u * 1.3f, 8.1f) - 0.005f * us_noise2(u * 23.0f, 1.3f);
	}
	return 0.56f + 0.03f * us_noise2(u * 2.2f + 5.0f, 2.9f) + 0.006f * us_noise2(u * 9.0f, 6.6f); /* wooded ridge */
}

static void new_cloud(st_t *st, cloud_t *k, float x)
{
	us_rng *r = &st->rng;
	k->x = x;
	k->depth = us_rand(r);
	/* far clouds sit low and small near the mountains, near clouds high and big */
	k->y = us_lerp(0.385f, 0.235f, k->depth) + us_randr(r, -0.02f, 0.02f);
	k->s = us_lerp(0.026f, 0.066f, k->depth) * us_randr(r, 0.85f, 1.15f);
	k->spd = us_lerp(0.35f, 1.0f, k->depth);
	/* a wide row of small billows along the flat base, a taller tier of big ones heaped over the middle */
	int low = 5 + (int)(us_rand(r) * 3), top = 2 + (int)(us_rand(r) * 2.0f);
	float wide = us_randr(r, 1.7f, 2.3f), lean = us_randr(r, -0.35f, 0.35f);
	k->nb = 0;
	for (int i = 0; i < low; i++) {
		float t = (i + 0.5f) / low * 2.0f - 1.0f;
		k->bx[k->nb] = t * wide + us_randr(r, -0.1f, 0.1f);
		k->br[k->nb] = (0.3f + 0.32f * (1.0f - t * t)) * us_randr(r, 0.8f, 1.25f);
		k->by[k->nb] = -k->br[k->nb] * us_randr(r, 0.45f, 0.75f);
		k->nb++;
	}
	for (int i = 0; i < top; i++) {
		float t = top > 1 ? (float)i / (top - 1) * 2.0f - 1.0f : 0.0f;
		k->bx[k->nb] = t * 0.75f + lean + us_randr(r, -0.12f, 0.12f);
		k->br[k->nb] = us_randr(r, 0.5f, 0.78f) * (1.0f - 0.2f * fabsf(t));
		k->by[k->nb] = -us_randr(r, 0.5f, 0.85f);
		k->nb++;
	}
}

typedef struct {
	float cx[MAX_BUMP], cy[MAX_BUMP], r[MAX_BUMP];
	float base, x0, x1;
	int nb;
} cshape_t;

static void cloud_shape(const cloud_t *k, int idx, float A, float t, float breath, cshape_t *o)
{
	float X0 = k->x * (A + 0.6f) - 0.3f;
	o->nb = k->nb;
	o->base = k->y;
	o->x0 = 1e9f;
	o->x1 = -1e9f;
	for (int i = 0; i < k->nb; i++) {
		float b = breath * (1.0f + 0.07f * us_noise2(t * 0.09f + i * 1.7f, idx * 3.1f));
		o->r[i] = k->br[i] * k->s * b;
		o->cx[i] = X0 + k->bx[i] * k->s;
		o->cy[i] = k->y + k->by[i] * k->s * b;
		if (o->cx[i] - o->r[i] < o->x0)
			o->x0 = o->cx[i] - o->r[i];
		if (o->cx[i] + o->r[i] > o->x1)
			o->x1 = o->cx[i] + o->r[i];
	}
}

static int in_cloud(const cshape_t *o, float x, float y, int skip, float shrink)
{
	if (y > o->base + 1e-4f)
		return 0;
	for (int j = 0; j < o->nb; j++) {
		if (j == skip)
			continue;
		float dx = x - o->cx[j], dy = y - o->cy[j], rr = o->r[j] * shrink;
		if (dx * dx + dy * dy < rr * rr)
			return 1;
	}
	return 0;
}

/* overwrite the cloud body (hides stars / moon behind it), then draw a light-facing outline */
static void draw_cloud(dc_t *d, const cshape_t *o, float depth, float lx, float ly, float lv, float skyv)
{
	us_canvas *c = d->c;
	float fillv = d->crisp ? 0.3f : 0.012f;
	int iv = (int)(fillv * 255.0f);
	int px0 = (int)floorf(o->x0 * c->s), px1 = (int)ceilf(o->x1 * c->s);
	float top = 1e9f;
	for (int j = 0; j < o->nb; j++)
		if (o->cy[j] - o->r[j] < top)
			top = o->cy[j] - o->r[j];
	int py0 = (int)floorf(top * c->s), py1 = (int)ceilf(o->base * c->s);
	if (px0 < 0) px0 = 0;
	if (py0 < 0) py0 = 0;
	if (px1 > c->w - 1) px1 = c->w - 1;
	if (py1 > c->h - 1) py1 = c->h - 1;
	for (int py = py0; py <= py1; py++) { /* per row: fill the chord of every bump (no per-pixel tests) */
		float y = (py + 0.5f) / c->s;
		if (y > o->base)
			break;
		for (int j = 0; j < o->nb; j++) {
			float dy = y - o->cy[j], h2 = o->r[j] * o->r[j] - dy * dy;
			if (h2 <= 0)
				continue;
			float hw = sqrtf(h2);
			int a = (int)ceilf((o->cx[j] - hw) * c->s - 0.5f), b = (int)floorf((o->cx[j] + hw) * c->s - 0.5f);
			if (a < px0) a = px0;
			if (b > px1) b = px1;
			uint8_t *ip = c->ink + (size_t)py * c->w, *mp = c->mat + (size_t)py * c->w;
			for (int px = a; px <= b; px++) {
				ip[px] = (uint8_t)iv;
				mp[px] = US_MAT_BG;
			}
		}
	}
	float base_v = (0.3f + 0.34f * depth) * skyv;
	float wdt = d->crisp ? 0.0045f : 0.0011f + 0.0004f * depth;
	/* light direction (towards sun / moon) for the rim */
	float cxm = 0.5f * (o->x0 + o->x1), cym = o->base - 0.02f;
	float ldx = lx - cxm, ldy = ly - cym, ll = sqrtf(ldx * ldx + ldy * ldy) + 1e-4f;
	ldx /= ll;
	ldy /= ll;
	for (int j = 0; j < o->nb; j++) {
		enum { NS = 22 };
		float px = 0, py = 0;
		int prev = 0;
		for (int q = 0; q <= NS; q++) {
			float a = US_PI * 0.92f + US_PI * 1.16f * q / NS;
			float nx = cosf(a), ny = sinf(a);
			float x = o->cx[j] + nx * o->r[j], y = o->cy[j] + ny * o->r[j];
			int vis = y <= o->base + 1e-4f && !in_cloud(o, x, y, j, 0.985f);
			if (vis && prev) {
				float lit = us_sat(nx * ldx + ny * ldy);
				float v = base_v * (0.55f - 0.22f * ny + 0.9f * lit * lv);
				wline(c, px, py, x, y, wdt, v, US_MAT_WATER);
			}
			px = x;
			py = y;
			prev = vis;
		}
		/* inner billow: a partial inner contour gives the cauliflower its volume */
		if (!d->crisp && depth > 0.3f && o->r[j] > 0.022f) {
			float ir = o->r[j] * 0.68f, icy = o->cy[j] + o->r[j] * 0.22f;
			float a0 = ldx < 0 ? US_PI * 1.08f : US_PI * 1.45f;
			float pxx = 0, pyy = 0;
			for (int q = 0; q <= 10; q++) {
				float a = a0 + 0.5f * q / 10.0f * US_PI;
				float x = o->cx[j] + cosf(a) * ir, y = icy + sinf(a) * ir;
				int ok = y < o->base - 0.002f;
				if (q > 0 && ok)
					wline(c, pxx, pyy, x, y, 0.001f, base_v * 0.42f * sinf(US_PI * q / 10.0f), US_MAT_WATER);
				pxx = x;
				pyy = y;
			}
		}
	}
	/* flat, faintly ragged base */
	{
		float xa = o->x0 + 0.15f * (o->x1 - o->x0), xb = o->x1 - 0.15f * (o->x1 - o->x0);
		float prx = xa, pry = o->base;
		for (int q = 1; q <= 8; q++) {
			float x = us_lerp(xa, xb, q / 8.0f);
			float y = o->base + 0.0015f * us_noise2(x * 40.0f, o->base * 10.0f);
			float v = base_v * 0.4f * sinf(US_PI * (q - 0.5f) / 8.0f);
			wline(c, prx, pry, x, y, wdt * 0.9f, v, US_MAT_WATER);
			prx = x;
			pry = y;
		}
	}
}

static void draw_bird(us_canvas *c, int crisp, float x, float y, float s, float f, float dir, float v, uint8_t m, int bat)
{
	float w = crisp ? 0.0042f : 0.0012f;
	for (int sd = -1; sd <= 1; sd += 2) {
		float ex = x + sd * s * 0.48f, ey = y - s * 0.34f * f - s * 0.04f;
		float tx = x + sd * s, ty = y + s * (0.10f - 0.26f * f);
		wline(c, x + sd * s * 0.08f, y, ex, ey, w, v, m);
		if (bat) { /* scalloped trailing edge */
			float hx = x + sd * s * 0.74f, hy = ty + s * 0.22f;
			wline(c, ex, ey, tx, ty, w, v, m);
			wline(c, tx, ty, hx, hy, w, v * 0.8f, m);
			wline(c, hx, hy, x + sd * s * 0.38f, ey + s * 0.3f, w, v * 0.8f, m);
		} else
			wline(c, ex, ey, tx, ty, w, v, m);
	}
	if (s > 0.007f || crisp)
		wline(c, x - dir * s * 0.16f, y + s * 0.02f, x + dir * s * 0.22f, y - s * 0.02f, w * 1.3f, v, m);
}

static void sky(dc_t *d, const us_ctx *ctx, float dens)
{
	st_t *st = d->st;
	us_canvas *c = d->c;
	us_world *w = ctx->w;
	us_rng *r = &st->rng;
	float A = d->A, t = d->t, dt = d->dt;
	int crisp = d->crisp;
	if (!st->sky_inited) {
		for (int i = 0; i < MAX_CLOUD; i++)
			new_cloud(st, &st->cloud[i], (i + us_rand(r) * 0.7f) / MAX_CLOUD);
		st->skein_wait = us_randr(r, 2.0f, 8.0f);
		st->sky_inited = 1;
	}
	st->swell_env = us_approach(st->swell_env, d->a->swell, 2.0f, ctx->real_dt);
	float day = w->daylight;
	float night = us_sat(1.0f - day * 2.2f);

	/* celestial bodies: sun by day, moon opposite it; elevation from the day phase */
	float sph = w->day_phase, mph = fmodf(w->day_phase + 0.5f, 1.0f);
	float sx = A * (0.08f + 0.84f * sph), sy = 0.62f + 0.54f * cosf(sph * US_TAU);
	float mx = A * (0.08f + 0.84f * mph), my = 0.62f + 0.54f * cosf(mph * US_TAU);
	float lx = sy < my ? sx : mx, ly = sy < my ? sy : my; /* the higher one lights the clouds */
	float lv = sy < my ? 0.9f : 0.75f * night;

	/* stars */
	if (night > 0.01f) {
		int ns = crisp ? 60 : 130;
		for (int i = 0; i < ns; i++) {
			uint32_t h = (uint32_t)i * 2654435761u;
			float x = hashf(h) * A, y = hashf(h + 1) * 0.6f;
			if (y > far_y(x / A, 0) - 0.004f)
				continue;
			float mag = hashf(h + 2);
			float tw = 0.65f + 0.35f * us_noise2(i * 1.37f, t * (0.6f + mag));
			float v = (0.12f + 0.5f * mag * mag) * night * tw;
			us_point(c, x, y, crisp ? 0.003f : 0.0009f + 0.0006f * mag, v, US_MAT_LIGHT);
			if (!crisp && mag > 0.93f) { /* the few bright ones get a faint glint */
				wline(c, x - 0.006f, y, x + 0.006f, y, 0.001f, v * 0.35f, US_MAT_LIGHT);
				wline(c, x, y - 0.006f, x, y + 0.006f, 0.001f, v * 0.35f, US_MAT_LIGHT);
			}
		}
	}

	/* the Milky Way after dark: a diagonal river of star dust with a dark rift down its middle; the
	 * phrase (swell) brings it up a little, like eyes adjusting */
	if (night > 0.05f) {
		int n = (int)((crisp ? 170 : 420) * (0.6f + 0.4f * dens));
		float x0 = A * 0.97f, y0 = -0.02f, x1 = A * 0.1f, y1 = 0.52f;
		float bx = x1 - x0, by = y1 - y0, bl = sqrtf(bx * bx + by * by);
		float nx = -by / bl, ny = bx / bl;
		float mw = night * (0.75f + 0.35f * st->swell_env);
		for (int i = 0; i < n; i++) {
			uint32_t h = (uint32_t)i * 2246822519u + 4099u;
			float s = hashf(h), g = (hashf(h + 1) + hashf(h + 2) + hashf(h + 3) - 1.5f); /* ~gaussian across */
			float wdt = 0.05f + 0.03f * us_noise2(s * 4.0f, 7.7f);
			float off = g * wdt + 0.02f * us_noise2(s * 3.0f, 1.9f);
			float x = x0 + bx * s + nx * off, y = y0 + by * s + ny * off;
			if (y < 0.0f || y > far_y(us_sat(x / A), 0) - 0.006f)
				continue;
			float rift = fabsf(off - 0.006f * us_noise2(s * 9.0f, 3.3f)) < 0.012f * (0.5f + 0.5f * us_noise2(s * 5.0f, 8.8f)) &&
				     s > 0.2f && s < 0.85f;
			if (rift && hashf(h + 4) < 0.8f)
				continue;
			float core = expf(-g * g * 3.0f);
			float v = (0.1f + 0.3f * hashf(h + 5) * hashf(h + 5) + 0.12f * core) * mw;
			if (crisp) {
				if (hashf(h + 6) < 0.55f + 0.4f * core)
					us_put(c, (int)(x * c->s), (int)(y * c->s), STEP1 * (0.7f + 0.6f * core) * mw, US_MAT_LIGHT);
			} else /* sub-pixel dust: one pixel each is plenty (and cheap) */
				us_put(c, (int)(x * c->s), (int)(y * c->s), v * (1.0f + 0.5f * core), US_MAT_LIGHT);
		}
	}

	/* sun: disc, breathing halo, slow crepuscular shafts fanning down through the air */
	if (sy < 0.6f && day > 0.05f) {
		float R = 0.034f, vk = us_smooth(0.05f, 0.4f, day);
		float halo = 1.0f + 0.12f * st->bass_env;
		if (crisp) {
			float fl = us_smooth(0.35f, 0.9f, st->swell_env);
			if (fl > 0.05f) /* swell flare: a wide pale corona */
				us_disc(c, sx, sy, R * (2.4f + 1.2f * fl), STEP1 * (0.6f + 0.6f * fl) * vk, US_MAT_LIGHT);
			us_disc(c, sx, sy, R * 2.0f * halo, 0.12f * vk, US_MAT_LIGHT);
			us_disc(c, sx, sy, R * 1.35f, 0.25f * vk, US_MAT_LIGHT);
			us_disc(c, sx, sy, R, 0.85f * vk, US_MAT_LIGHT);
		} else {
			int nshaft = 9;
			for (int k = 0; k < nshaft; k++) {
				float a = US_PI * 0.5f + (k - (nshaft - 1) * 0.5f) * 0.21f + 0.08f * us_noise2(t * 0.05f, k * 2.3f);
				float len = 0.32f + 0.25f * (0.5f + 0.5f * us_noise2(t * 0.07f, k * 5.1f)) + 0.12f * st->swell_env;
				float sv = (0.05f + 0.07f * (0.5f + 0.5f * us_noise2(t * 0.11f, k * 1.9f))) * vk * (0.7f + 0.5f * st->swell_env) *
					   (0.55f + 0.8f * st->cen_env); /* bright music, clear air */
				float x0 = sx + cosf(a) * R * 1.8f, y0 = sy + sinf(a) * R * 1.8f;
				enum { SEG = 6 };
				for (int q = 0; q < SEG; q++) { /* taper out along the shaft */
					float t0 = (float)q / SEG, t1 = (float)(q + 1) / SEG;
					wline(c, x0 + cosf(a) * len * t0, y0 + sinf(a) * len * t0, x0 + cosf(a) * len * t1,
					      y0 + sinf(a) * len * t1, 0.001f, sv * (1.0f - t0), US_MAT_LIGHT);
				}
			}
			/* a big swell flares the sun: a fine corona of rays reaching out all round */
			float fl = us_smooth(0.35f, 0.9f, st->swell_env);
			if (fl > 0.01f)
				for (int k = 0; k < 16; k++) {
					float a = k * US_TAU / 16 + 0.1f * us_noise2(t * 0.04f, k * 1.3f) + t * 0.01f;
					float rl = R * (1.6f + (3.0f + 1.5f * (k & 1)) * fl * (0.7f + 0.3f * us_noise2(t * 0.2f, k * 3.7f)));
					float x0 = sx + cosf(a) * R * 1.2f, y0 = sy + sinf(a) * R * 1.2f;
					float xm = sx + cosf(a) * (R * 1.2f + rl) * 0.5f, ym = sy + sinf(a) * (R * 1.2f + rl) * 0.5f;
					wline(c, x0, y0, xm, ym, 0.001f, 0.2f * fl * vk, US_MAT_LIGHT);
					wline(c, xm, ym, sx + cosf(a) * rl, sy + sinf(a) * rl, 0.001f, 0.08f * fl * vk, US_MAT_LIGHT);
				}
			us_disc(c, sx, sy, R, 0.22f * vk, US_MAT_LIGHT);
			wring(c, sx, sy, R, 0.0018f, 0.9f * vk, US_MAT_LIGHT);
			wring(c, sx, sy, R * 1.45f * halo, 0.0012f, 0.22f * vk, US_MAT_LIGHT);
			wring(c, sx, sy, R * 2.2f * halo, 0.0012f, 0.11f * vk, US_MAT_LIGHT);
		}
	}
	/* twilight: when the sun sits just below / on the mountains, wide faint arcs of afterglow rise over them */
	{
		float tw = us_smooth(0.42f, 0.55f, sy) * (1.0f - us_smooth(0.72f, 0.85f, sy));
		if (tw > 0.01f) {
			int nr = crisp ? 3 : 5;
			for (int k = 0; k < nr; k++) {
				float R = 0.09f + 0.075f * k + 0.01f * st->swell_env;
				float v = tw * (0.16f - 0.025f * k) * (0.8f + 0.2f * st->bass_env);
				enum { NA = 48 };
				float px = 0, py = 0;
				for (int q = 0; q <= NA; q++) {
					float a = US_PI * (1.08f + 0.84f * q / NA);
					float x = sx + cosf(a) * R * 1.6f, y = sy + sinf(a) * R;
					if (q > 0)
						wline(c, px, py, x, y, crisp ? 0.004f : 0.0012f, v * sinf(US_PI * q / NA), crisp ? US_MAT_LIGHT : US_MAT_BLOOM);
					px = x;
					py = y;
				}
			}
		}
	}
	/* moon: lit disc with faint maria and a breathing triple halo */
	if (my < 0.6f && night > 0.02f) {
		float R = 0.03f, vk = us_smooth(0.0f, 0.5f, night);
		float halo = 1.0f + 0.1f * st->bass_env + 0.05f * st->swell_env;
		if (crisp) {
			us_disc(c, mx, my, R * 2.1f * halo, 0.1f * vk, US_MAT_LIGHT);
			us_disc(c, mx, my, R * 1.4f * halo, 0.2f * vk, US_MAT_LIGHT);
			us_disc(c, mx, my, R, 0.8f * vk, US_MAT_LIGHT);
			us_disc(c, mx - R * 0.3f, my - R * 0.15f, R * 0.3f, 0.6f * vk, US_MAT_LIGHT);
		} else {
			us_disc(c, mx, my, R, 0.07f * vk, US_MAT_LIGHT);
			wring(c, mx, my, R, 0.0018f, 0.85f * vk, US_MAT_LIGHT);
			static const float mar[4][3] = {{-0.3f, -0.2f, 0.26f}, {0.25f, 0.1f, 0.2f}, {-0.05f, 0.4f, 0.16f}, {0.35f, -0.35f, 0.1f}};
			for (int k = 0; k < 4; k++)
				wring(c, mx + mar[k][0] * R, my + mar[k][1] * R, mar[k][2] * R, 0.001f, 0.32f * vk, US_MAT_LIGHT);
			wring(c, mx, my, R * 1.55f * halo, 0.0012f, 0.2f * vk, US_MAT_LIGHT);
			wring(c, mx, my, R * 2.5f * halo, 0.0011f, 0.1f * vk, US_MAT_LIGHT);
			wring(c, mx, my, R * 3.8f * halo, 0.001f, 0.05f * vk, US_MAT_LIGHT);
		}
	}

	/* far mountains + wooded ridge: overwrite fill so they hide the low sun / moon */
	{
		float mv = crisp ? STEP3 : 0.0f, rv = crisp ? STEP1 : 0.0f;
		int imv = (int)(mv * 255.0f), irv = (int)(rv * 255.0f), imv1 = (int)(STEP1 * 255.0f);
		enum { MAXW = 4096 };
		static short cym[MAXW], cyr[MAXW], cyg[MAXW]; /* per-column profile rows (scratch, rebuilt each frame) */
		int W = c->w < MAXW ? c->w : MAXW, ytop = c->h, ybot = 0;
		float fm0 = 0, fm1 = 0, fr0 = 0, fr1 = 0, fg0 = 0, fg1 = 0;
		for (int px = 0; px < W; px++) {
			if ((px & 7) == 0) { /* the profiles are smooth: sample every 8th column, lerp between */
				float u0 = (px + 0.5f) / c->s / A, u1 = (px + 8.5f) / c->s / A;
				fm0 = far_y(u0, 0); fm1 = far_y(u1, 0);
				fr0 = far_y(u0, 1); fr1 = far_y(u1, 1);
				fg0 = ground_y(u0, 0); fg1 = ground_y(u1, 0);
			}
			float f = (px & 7) * 0.125f;
			int ym = (int)ceilf(us_lerp(fm0, fm1, f) * c->s), yr = (int)ceilf(us_lerp(fr0, fr1, f) * c->s);
			int yg = (int)ceilf(us_lerp(fg0, fg1, f) * c->s) + 2;
			if (ym < 0) ym = 0;
			if (yg > c->h) yg = c->h;
			cym[px] = (short)ym;
			cyr[px] = (short)yr;
			cyg[px] = (short)yg;
			if (ym < ytop) ytop = ym;
			if (yg > ybot) ybot = yg;
		}
		for (int py = ytop; py < ybot; py++) { /* row-major: cache friendly */
			uint8_t *ip = c->ink + (size_t)py * c->w, *mp = c->mat + (size_t)py * c->w;
			for (int px = 0; px < W; px++) {
				if (py < cym[px] || py >= cyg[px])
					continue;
				if (crisp) {
					/* aerial perspective: blue-slate mountains (bluer down the slopes), a dark olive ridge */
					int b = BAYER[((py & 3) << 2) | (px & 3)];
					if (py < cyr[px]) {
						int f = 1 + (py - cym[px]) * 64 / (int)(c->s * 0.08f + 1.0f);
						int blue = b < (f < 3 ? f : 3);
						ip[px] = (uint8_t)(blue ? imv1 : imv);
						mp[px] = blue ? US_MAT_WATER : US_MAT_BG;
					} else {
						int lit = py - cyr[px] < 2 && b < 6; /* a lit rim of canopy along the crest */
						ip[px] = (uint8_t)(lit ? imv1 : irv);
						mp[px] = lit ? US_MAT_LEAF : US_MAT_EARTH;
					}
					continue;
				}
				uint8_t v = (uint8_t)(py < cyr[px] ? imv : irv);
				if (ip[px] != v) { /* ink: the cleared sky is already black, only erase what lies behind */
					ip[px] = v;
					mp[px] = US_MAT_BG;
				}
			}
		}
		float snow = us_sat((d->winter - 0.3f) * 2.5f);
		float pts[2 * 97];
		for (int k = 0; k < 2; k++) {
			for (int i = 0; i <= 96; i++) {
				float u = i / 96.0f;
				pts[2 * i] = u * A;
				pts[2 * i + 1] = far_y(u, k);
			}
			float v = k == 0 ? 0.2f + 0.04f * day : 0.15f;
			wpoly(c, pts, 97, crisp ? 0.004f : 0.001f, v, k == 0 ? US_MAT_WATER : US_MAT_EARTH);
			if (k == 0) /* snow on the high crests: brighter line + a few slope streaks */
				for (int i = 0; i < 96; i++) {
					float y0 = pts[2 * i + 1], y1 = pts[2 * i + 3];
					float hi = us_sat((0.47f - 0.5f * (y0 + y1)) * 25.0f) * (0.35f + 0.65f * snow);
					if (hi > 0.05f) {
						wline(c, pts[2 * i], y0, pts[2 * i + 2], y1, crisp ? 0.004f : 0.0012f, 0.2f + 0.3f * hi, US_MAT_WATER);
						if (!crisp && (i % 2) == 0)
							wline(c, pts[2 * i], y0 + 0.002f, pts[2 * i] + 0.004f * (y1 > y0 ? 1 : -1), y0 + 0.012f * hi, 0.001f,
							      0.14f * hi, US_MAT_WATER);
					}
				if (k == 0 && !crisp) /* fall-line hatching on the flanks turned away from the sun / moon */
					for (int i = 0; i < 96; i++) {
						float x0 = pts[2 * i], y0 = pts[2 * i + 1], sl = (pts[2 * i + 3] - y0) / (pts[2 * i + 2] - x0);
						int shade = (sl < 0) == (lx > x0);
						if (!shade || fabsf(sl) < 0.08f)
							continue;
						for (int h = 0; h < 2; h++) {
							float hx = x0 + (pts[2 * i + 2] - x0) * (0.25f + 0.5f * h);
							float hy = y0 + (pts[2 * i + 3] - y0) * (0.25f + 0.5f * h) + 0.002f;
							float ln = (0.008f + 0.02f * hashf((uint32_t)i * 31u + h)) * us_sat(fabsf(sl) * 2.0f);
							float ry = far_y(hx / A, 1) - 0.003f;
							float ey = hy + ln;
							if (ey > ry)
								ey = ry;
							if (ey > hy + 0.003f)
								wline(c, hx, hy, hx + (sl > 0 ? 1.0f : -1.0f) * ln * 0.35f * (ey - hy) / ln, ey, 0.001f,
								      0.09f + 0.03f * day, US_MAT_WATER);
						}
					}
			}
		}
		/* trees on the ridge: a hazy tree line, conifers and round crowns */
		int nt = (int)(70 + 90 * dens);
		uint8_t tm = d->winter > 0.6f ? US_MAT_EARTH : US_MAT_LEAF;
		for (int i = 0; i < nt; i++) {
			uint32_t h = (uint32_t)i * 2246822519u + 991u;
			float u = hashf(h);
			float x = u * A, y = far_y(u, 1) + 0.001f;
			float th = 0.01f + 0.022f * hashf(h + 1) * hashf(h + 4);
			float sw = 0.0015f * us_noise2(x * 3.0f - d->wind_t * 0.5f, 4.0f) * (0.5f + d->wind);
			float v = 0.17f + 0.1f * hashf(h + 3);
			if (hashf(h + 2) < 0.6f) {
				if (crisp)
					us_tri(c, x - th * 0.25f, y, x + th * 0.25f, y, x + sw, y - th, v + 0.08f, tm);
				else {
					wline(c, x - th * 0.24f, y, x + sw, y - th, 0.001f, v, tm);
					wline(c, x + sw, y - th, x + th * 0.24f, y, 0.001f, v, tm);
					wline(c, x - th * 0.15f, y - th * 0.4f, x + th * 0.15f, y - th * 0.42f, 0.001f, v * 0.6f, tm);
				}
			} else {
				float rr = th * 0.36f;
				if (crisp)
					us_disc(c, x + sw, y - th * 0.62f, rr, v + 0.08f, tm);
				else {
					wline(c, x, y, x + sw * 0.5f, y - th * 0.35f, 0.001f, v * 0.8f, US_MAT_STEM);
					wring(c, x + sw, y - th * 0.62f, rr, 0.001f, v, tm);
				}
			}
			if (snow > 0.3f && !crisp)
				us_point(c, x + sw, y - th, 0.0014f, 0.4f * snow, US_MAT_WATER);
		}
		/* valley mist drifting in front of the ridge (more when the air is wet and the light is low) */
		float mist = (0.35f + 0.65f * w->moisture) * (0.55f + 0.45f * (1.0f - day));
		for (int k = 0; k < 3; k++) {
			float yb = 0.585f + 0.018f * k + 0.006f * us_noise2(t * 0.03f, k * 7.0f);
			float xs = fmodf(t * (0.006f + 0.003f * k) + k * 0.47f, 1.0f) * (A + 0.8f) - 0.8f;
			float len = (0.3f + 0.12f * k) * (0.75f + 0.8f * st->wid_env); /* a wide stereo field opens the valley air */
			enum { MS = 18 };
			float prx = xs, pry = yb;
			for (int q = 1; q <= MS; q++) {
				float x = xs + len * q / MS;
				float y = yb + 0.004f * us_noise2(x * 6.0f + k, t * 0.05f);
				float v = 0.08f * mist * sinf(US_PI * (q - 0.5f) / MS) * (0.8f + 0.5f * st->wid_env);
				for (int rep = 0; rep < 2; rep++) {
					float ox = rep ? A + 0.8f : 0.0f; /* wrap */
					if (x - ox > -0.05f && prx - ox < A + 0.05f)
						wline(c, prx - ox, pry, x - ox, y, crisp ? 0.004f : 0.0012f, v, US_MAT_WATER);
				}
				prx = x;
				pry = y;
			}
		}
	}

	/* high cirrus: a few combed streaks near the top of the sky */
	if (!crisp || dens > 0.3f) {
		int ncs = 2 + (int)(dens * 3.0f);
		for (int k = 0; k < ncs; k++) {
			uint32_t h = (uint32_t)k * 3266489917u + 17u;
			float span = A + 1.0f;
			float x0 = fmodf(hashf(h) * span + t * (0.004f + 0.003f * hashf(h + 1)) * (1.0f + 2.0f * d->wind), span) - 0.6f;
			float y0 = 0.035f + 0.11f * hashf(h + 2);
			float len = 0.22f + 0.25f * hashf(h + 3);
			int nl = 3 + (int)(hashf(h + 4) * 3);
			for (int l = 0; l < nl; l++) {
				float ox = l * 0.02f * (hashf(h + 5 + l) - 0.3f), oy = l * 0.0065f;
				float ll = len * (0.6f + 0.4f * hashf(h + 9 + l));
				enum { CS = 12 };
				float prx = x0 + ox, pry = y0 + oy;
				for (int q = 1; q <= CS; q++) {
					float x = x0 + ox + ll * q / CS;
					float y = y0 + oy - 0.012f * (float)q / CS + 0.003f * us_noise2(x * 8.0f, k + l * 0.3f + t * 0.02f);
					float v = (0.1f + 0.06f * hashf(h + 13 + l)) * sinf(US_PI * (q - 0.5f) / CS) * (0.7f + 0.3f * st->swell_env);
					wline(c, prx, pry, x, y, crisp ? 0.004f : 0.001f, v, US_MAT_WATER);
					prx = x;
					pry = y;
				}
			}
		}
	}

	/* clouds: two depths drifting downwind, swelling with the phrase */
	{
		float breath = 1.0f + 0.07f * st->swell_env + 0.03f * st->bass_env;
		float skyv = 0.75f + 0.25f * us_sat(day + night * 0.6f);
		int nc = 3 + (int)(dens * 4.0f + 0.5f);
		if (nc > MAX_CLOUD)
			nc = MAX_CLOUD;
		/* far ones first so nearer clouds overwrite them */
		int order[MAX_CLOUD];
		for (int i = 0; i < MAX_CLOUD; i++)
			order[i] = i;
		for (int i = 1; i < MAX_CLOUD; i++)
			for (int j = i; j > 0 && st->cloud[order[j]].depth < st->cloud[order[j - 1]].depth; j--) {
				int tmp = order[j];
				order[j] = order[j - 1];
				order[j - 1] = tmp;
			}
		for (int oi = 0; oi < MAX_CLOUD; oi++) {
			int i = order[oi];
			cloud_t *k = &st->cloud[i];
			k->x += dt * k->spd * (0.0035f + 0.018f * d->wind) / (A + 0.6f);
			if (k->x > 1.0f)
				new_cloud(st, k, 0.0f);
			if (i >= nc)
				continue;
			cshape_t o;
			cloud_shape(k, i, A, t, breath, &o);
			draw_cloud(d, &o, k->depth, lx, ly, lv, skyv * (0.6f + 0.4f * k->depth));
		}
	}

	/* birds: a skein of geese now and then, swallows (bats after dark) wheeling */
	{
		float lift = d->a->high;
		int bat = day < 0.12f;
		int nsw = (int)((1.0f + 2.5f * w->flock + 2.0f * dens) * (0.5f + 0.5f * us_smooth(0.0f, 0.4f, day)) + 0.5f);
		if (d->winter > 0.6f && !bat)
			nsw /= 2;
		if (nsw > 6)
			nsw = 6;
		for (int i = 0; i < 6; i++) {
			bird_t *b = &st->bird[i];
			if (!b->alive) {
				if (i >= nsw)
					continue;
				b->alive = 1;
				b->kind = 1;
				b->x = us_rand(r) < 0.5f ? -0.05f : A + 0.05f;
				b->y = us_randr(r, 0.08f, 0.3f);
				b->vx = b->x < 0 ? 0.1f : -0.1f;
				b->vy = 0;
				b->ph = us_rand(r) * 10.0f;
				b->size = us_randr(r, 0.007f, 0.011f);
			}
			int leaving = i >= nsw;
			float tx = leaving ? (b->x < A * 0.5f ? -0.3f : A + 0.3f) : A * (0.5f + 0.5f * us_noise2(t * 0.045f + i * 7.0f, 1.3f));
			float ty = 0.2f + 0.14f * us_noise2(t * 0.06f + i * 3.0f, 5.5f);
			float dx = tx - b->x, dy = ty - b->y, dl = sqrtf(dx * dx + dy * dy) + 1e-4f;
			float sp = 0.13f + 0.08f * lift;
			b->vx += ((dx / dl) * sp - b->vx) * dt * 1.2f + 0.25f * us_noise2(t * 0.7f, i * 9.0f) * dt;
			b->vy += ((dy / dl) * sp - b->vy) * dt * 1.2f + 0.25f * us_noise2(t * 0.7f + 3.0f, i * 9.0f) * dt;
			b->x += b->vx * dt;
			b->y += b->vy * dt;
			if (leaving && (b->x < -0.2f || b->x > A + 0.2f)) {
				b->alive = 0;
				continue;
			}
			/* flap in bursts, glide between */
			float burst = us_smooth(-0.2f, 0.3f, us_noise2(t * 0.5f, i * 4.0f));
			b->ph += dt * (bat ? 16.0f : 13.0f) * (0.25f + 0.75f * burst);
			float f = bat ? sinf(b->ph) : sinf(b->ph) * (0.25f + 0.75f * burst) + 0.2f * (1.0f - burst);
			draw_bird(c, crisp, b->x, b->y, b->size, f, b->vx >= 0 ? 1.0f : -1.0f, 0.62f, US_MAT_ACCENT, bat);
		}
		/* skein */
		if (st->skein_n == 0) {
			st->skein_wait -= dt * (0.5f + w->flock + 0.5f * st->swell_env);
			if (st->skein_wait <= 0 && !bat && d->winter < 0.8f) {
				st->skein_dir = us_rand(r) < 0.5f ? 1.0f : -1.0f;
				st->skein_x = st->skein_dir > 0 ? -0.08f : A + 0.08f;
				st->skein_y = us_randr(r, 0.1f, 0.26f);
				st->skein_n = 5 + (int)(us_rand(r) * (4.0f + 6.0f * w->flock));
				if (st->skein_n > MAX_BIRD - 6)
					st->skein_n = MAX_BIRD - 6;
				st->skein_t = 0;
			}
		} else {
			st->skein_t += dt;
			st->skein_x += st->skein_dir * dt * (0.045f + 0.015f * st->swell_env);
			st->skein_y += dt * 0.004f * us_noise2(st->skein_t * 0.2f, 2.0f);
			float s = 0.0085f;
			for (int k = 0; k < st->skein_n; k++) {
				int rank = (k + 1) / 2;
				float side = (k & 1) ? 1.0f : -0.65f;
				float x = st->skein_x - st->skein_dir * rank * 0.026f + 0.003f * us_noise2(st->skein_t * 0.4f, k * 3.3f);
				float y = st->skein_y + side * rank * 0.011f + 0.003f * us_noise2(st->skein_t * 0.35f + 9.0f, k * 3.3f);
				float f = sinf(st->skein_t * 5.5f - rank * 0.7f);
				draw_bird(c, crisp, x, y, s * (1.0f - 0.03f * rank), f, st->skein_dir, 0.55f, US_MAT_ACCENT, 0);
			}
			float tail = st->skein_x - st->skein_dir * (st->skein_n / 2 + 1) * 0.026f;
			if ((st->skein_dir > 0 && tail > A + 0.1f) || (st->skein_dir < 0 && tail < -0.1f)) {
				st->skein_n = 0;
				st->skein_wait = us_randr(r, 18.0f, 40.0f);
			}
		}
	}
}

/* ---- small life, texture and the band-tied flowers ---------------------- */

static float env(float cur, float x, float atk, float rel, float dt)
{
	return us_approach(cur, x, x > cur ? atk : rel, dt);
}

/* damped spring (period s, damping ratio z), sub-stepped so a long frame can't blow it up */
static void spring(float *x, float *v, float period, float z, float dt, float lim)
{
	float W = US_TAU / period, h = us_clampf(dt, 0.0f, 0.1f);
	int ns = 1 + (int)(h / 0.005f);
	h /= ns;
	for (int q = 0; q < ns; q++) {
		*v += (-W * W * *x - 2.0f * z * W * *v) * h;
		*x += *v * h;
	}
	*x = us_clampf(*x, -lim, lim);
	*v = us_clampf(*v, -4.0f, 4.0f);
	if (!(*x == *x) || !(*v == *v)) /* never carry a NaN */
		*x = *v = 0.0f;
}

/* turf texture below each crest: grass ticks that lean with the wind, far wildflower speckle, pebbles */
static void hill_texture(dc_t *d, int L, float dens)
{
	us_canvas *c = d->c;
	static const int NB[NL] = {80, 100, 70};
	int n = (int)(NB[L] * (0.5f + dens));
	float bloomy = (d->spring + d->summer + 0.4f * d->autumn) * (1.0f - d->winter);
	uint8_t tm = d->winter > 0.55f ? US_MAT_WATER : (d->autumn > 0.55f ? US_MAT_EARTH : US_MAT_LEAF);
	for (int i = 0; i < n; i++) {
		uint32_t h = (uint32_t)i * 2246822519u + (uint32_t)L * 977u + 13u;
		float u = hashf(h), x = u * d->A;
		float dep = hashf(h + 1);
		dep = dep * dep * (L == 2 ? 0.1f : 0.05f) + 0.004f;
		float y = ground_y(u, L) + dep;
		if (L < 2 && y > ground_y(u, L + 1) - 0.004f)
			continue;
		float fade = 1.0f - 0.5f * dep / (L == 2 ? 0.1f : 0.055f);
		float kind = hashf(h + 2);
		if (kind < 0.13f) { /* a distant flower: one bright speck */
			if (bloomy < 0.15f)
				continue;
			uint8_t fm = hashf(h + 3) < 0.5f ? US_MAT_BLOOM : (d->autumn > 0.5f ? US_MAT_LIGHT : US_MAT_ACCENT);
			if (d->crisp)
				us_put(c, (int)(x * c->s), (int)(y * c->s), STEP2 + 0.2f * bloomy, fm);
			else
				us_point(c, x, y, 0.0009f + 0.0005f * LSCALE[L], (0.3f + 0.25f * d->lb) * bloomy * fade, fm);
		} else if (kind < 0.2f && L == 2) { /* pebble half sunk in the bank */
			float r = 0.0025f + 0.002f * hashf(h + 4);
			if (d->crisp) {
				us_put(c, (int)(x * c->s), (int)(y * c->s), STEP2, US_MAT_EARTH);
				us_put(c, (int)(x * c->s) + 1, (int)(y * c->s), STEP1 + 0.05f, US_MAT_EARTH);
			} else
				us_arc(c, x, y, r, US_PI, US_TAU, 0.001f, 0.28f * fade, US_MAT_EARTH);
		} else { /* a grass tick */
			if (d->crisp) {
				if ((i & 1) == 0)
					us_put(c, (int)(x * c->s), (int)(y * c->s), STEP1 + 0.12f * (L == 1), tm);
				continue;
			}
			float ln = 0.0035f + 0.0035f * LSCALE[L] * hashf(h + 5);
			float lean = 0.25f * hashf(h + 6) - 0.1f + 0.5f * d->wind * (0.5f + 0.5f * us_noise2(x * 3.0f - d->wind_t * 0.6f, 1.7f)) +
				     2.0f * d->bow + 3.0f * d->rat;
			wline(c, x, y, x + lean * ln, y - ln, 0.001f, (0.17f + 0.1f * hashf(h + 7)) * (0.5f + 0.5f * d->lb) * fade, tm);
		}
	}
}

/* ground mist hugging the near bank: the sub breathes it up and thickens it */
static void low_mist(dc_t *d, const us_ctx *ctx)
{
	st_t *st = d->st;
	us_canvas *c = d->c;
	float se = st->sub_env, t = d->t;
	float wet = 0.6f + 0.4f * ctx->w->moisture;
	for (int k = 0; k < 3; k++) {
		enum { MS = 40 };
		float prx = 0, pry = 0;
		for (int q = 0; q <= MS; q++) {
			float u = (float)q / MS, x = u * d->A;
			float lift = (0.006f + 0.016f * se) * (k + 0.6f) * (0.8f + 0.4f * us_noise2(u * 5.0f + k, t * 0.05f));
			float y = ground_y(u, 2) - 0.004f - lift;
			float n = 0.5f + 0.5f * us_noise2(u * 3.0f - t * 0.025f * (k + 1), k * 4.0f + 1.0f);
			if (q > 0) {
				if (d->crisp) {
					if (n > 0.62f - 0.12f * se && se > 0.12f)
						us_line(c, prx, pry, x, y, 0.004f, STEP2 + 0.15f * se, US_MAT_BG);
				} else {
					float v = (0.03f + 0.15f * se) * wet * n * (1.0f - 0.25f * k);
					if (v > 0.012f)
						wline(c, prx, pry, x, y, 0.0012f, v, US_MAT_WATER);
				}
			}
			prx = x;
			pry = y;
		}
	}
}

/* a row of daisies and poppies along the near bank, each tied to its own slice of the spectrum:
 * a band sounding opens its flower and lifts its head; silence lets them half-close and nod */
static void draw_daisies(dc_t *d, const us_ctx *ctx)
{
	st_t *st = d->st;
	us_canvas *c = d->c;
	float season = us_sat(1.0f - d->winter * 1.6f) * (0.4f + 0.6f * us_sat(ctx->w->growth * 2.0f + 0.3f));
	if (season < 0.05f)
		return;
	float idle = 1.0f - us_sat(d->a->active);
	float mean = 0;
	for (int i = 0; i < NDAISY; i++)
		mean += st->band_env[i] / NDAISY;
	for (int i = 0; i < NDAISY; i++) {
		float u = st->daisy_x[i], gx = u * d->A, gy = ground_y(u, 2) + 0.007f;
		float e = 0.55f * st->band_env[i] + 0.9f * (st->band_env[i] - mean); /* the bands that stand out open widest */
		float op = us_sat(0.15f + 0.85f * us_smooth(0.05f, 0.6f, e) + idle * 0.12f * (0.6f + 0.4f * sinf(d->t * 0.3f + st->daisy_ph[i])));
		float H = (0.05f + 0.035f * st->daisy_h[i]) * season * (0.88f + 0.12f * op);
		float pts[2 * 5];
		/* a closed flower nods; an open one straightens and faces up */
		float nod = (0.035f * (1.0f - op) + 0.01f) * (st->daisy_ph[i] > 5.0f ? 1.0f : -1.0f);
		stalk(d, gx, gy, H, 1.0f, 0.08f * sinf(st->daisy_ph[i] * 3.0f), nod, 50.0f + i, 4, pts);
		uint8_t sm = d->autumn > 0.6f ? US_MAT_EARTH : US_MAT_STEM;
		wpoly(c, pts, 5, lw(d, 0.0012f), 0.62f * d->lift, sm);
		uint8_t lm = leaf_mat(d, (uint32_t)i * 77u);
		petal(c, d->crisp, pts[2], pts[3], -US_PI * 0.5f - 0.95f, H * 0.32f, H * 0.07f, 0.55f, lm);
		petal(c, d->crisp, pts[2], pts[3], -US_PI * 0.5f + 0.95f, H * 0.28f, H * 0.07f, 0.55f, lm);
		float hx = pts[8], hy = pts[9];
		int poppy = st->daisy_kind[i];
		float r = (0.0085f + 0.005f * st->daisy_h[i]) * (0.55f + 0.45f * op) * (poppy ? 1.15f : 1.0f) * (1.0f + 0.45f * d->kp * op);
		uint8_t pm = poppy ? US_MAT_BLOOM : US_MAT_ACCENT;
		float v = us_sat((0.5f + 0.5f * op) * d->lift + 0.3f * d->kp * op);
		int np = poppy ? 5 : 11;
		float lean = (pts[8] - pts[6]) / (H * 0.25f + 1e-4f); /* the head follows the stalk tip */
		for (int q = 0; q < np; q++) {
			float a0 = st->daisy_ph[i] + US_TAU * q / np + 6.0f * d->rat;
			/* open: a flat ring seen from a little above; closed: a cup pointing up */
			float ox = cosf(a0), oy = sinf(a0) * 0.42f;
			float cx = 0.38f * cosf(a0) + 0.5f * lean, cy = -0.9f;
			float px = us_lerp(cx, ox, op), py = us_lerp(cy, oy, op);
			if (poppy || d->crisp) {
				float pl = sqrtf(px * px + py * py) + 1e-4f;
				petal(c, d->crisp, hx, hy, atan2f(py, px), r * pl, r * (poppy ? 0.45f : 0.2f), v, pm);
			} else
				wline(c, hx, hy, hx + px * r, hy + py * r, lw(d, 0.0011f), v, pm);
		}
		us_disc(c, hx, hy, r * 0.26f + 0.0006f, 0.7f + 0.3f * op, poppy ? US_MAT_STEM : US_MAT_LIGHT);
		if (op > 0.5f)
			add_anchor(st, hx, hy, op);
	}
}

/* a snail crossing the near bank; a hard kick makes it pull its horns in, and it is slow to trust again */
static void snail(dc_t *d)
{
	st_t *st = d->st;
	us_canvas *c = d->c;
	if (d->winter > 0.6f)
		return;
	float hide = st->snail_hide, dir = st->snail_dir;
	if (hide < 0.25f)
		st->snail_x += dir * d->dt * 0.0022f * (1.0f - 0.6f * d->winter) / d->A;
	if (st->snail_x > 0.97f || st->snail_x < 0.03f) {
		st->snail_dir = -dir;
		st->snail_x = us_clampf(st->snail_x, 0.03f, 0.97f);
	}
	float u = st->snail_x, x = u * d->A, gy = ground_y(u, 2) + 0.001f;
	float R = 0.0065f;
	float ext = 1.0f - hide;
	/* glistening trail behind it: the air band makes it catch the light */
	float tv = 0.06f + 0.22f * st->air_env * (0.5f + 0.5f * sinf(d->t * 1.7f));
	for (int k = 0; k < 6; k++) {
		float u0 = u - dir * (0.006f + 0.012f * k) / d->A, u1 = u - dir * (0.006f + 0.012f * (k + 1)) / d->A;
		wline(c, u0 * d->A, ground_y(u0, 2) + 0.0015f, u1 * d->A, ground_y(u1, 2) + 0.0015f, d->crisp ? 0.004f : 0.001f,
		      tv * (1.0f - k / 6.0f) * (d->crisp ? 1.6f : 1.0f), US_MAT_WATER);
	}
	/* body: a soft foot along the ground, head raised */
	float hx = x + dir * R * (0.9f + 0.5f * ext), hy = gy - R * 0.45f * ext - 0.0008f;
	wline(c, x - dir * R * 1.1f, gy, x + dir * R * 0.8f * (0.6f + 0.4f * ext), gy, d->crisp ? 0.005f : 0.0022f, 0.5f, US_MAT_EARTH);
	if (ext > 0.05f) {
		wline(c, x + dir * R * 0.6f, gy - 0.0005f, hx, hy, d->crisp ? 0.004f : 0.0018f, 0.5f * ext, US_MAT_EARTH);
		if (!d->crisp)
			for (int s = 0; s < 2; s++) {
				float ax = hx + dir * R * (0.35f + 0.25f * s) * ext, ay = hy - R * (0.95f - 0.2f * s) * ext;
				wline(c, hx, hy, ax, ay, 0.001f, 0.5f * ext, US_MAT_EARTH);
				us_point(c, ax, ay, 0.0011f, 0.6f * ext, US_MAT_EARTH);
			}
	}
	/* shell: a whorl on its back */
	float sx = x - dir * R * 0.15f, sy = gy - R * 0.95f;
	if (d->crisp) {
		us_disc(c, sx, sy, R, STEP2, US_MAT_EARTH);
		us_put(c, (int)(sx * c->s), (int)(sy * c->s), STEP3, US_MAT_ACCENT);
	} else {
		us_disc(c, sx, sy, R, 0.05f, US_MAT_EARTH);
		wring(c, sx, sy, R, 0.0013f, 0.62f, US_MAT_EARTH);
		us_arc(c, sx + dir * R * 0.12f, sy + R * 0.08f, R * 0.62f, 0.0f, US_PI * 1.6f, 0.0011f, 0.45f, US_MAT_EARTH);
		us_arc(c, sx + dir * R * 0.2f, sy + R * 0.15f, R * 0.3f, US_PI, US_PI * 2.4f, 0.001f, 0.4f, US_MAT_EARTH);
		us_point(c, sx - dir * R * 0.3f, sy - R * 0.55f, 0.0009f, 0.4f, US_MAT_LIGHT); /* a highlight */
	}
}

/* midges dancing in a sunlit column over the grass: the hats widen and quicken the dance, air glints them */
static void midges(dc_t *d, const us_ctx *ctx)
{
	st_t *st = d->st;
	us_canvas *c = d->c;
	float day = ctx->w->daylight;
	float vis = (0.25f + 0.75f * us_smooth(0.05f, 0.4f, day)) * (1.0f - d->winter) * (1.0f - 0.5f * d->autumn);
	if (vis < 0.05f)
		return;
	float mt = st->midge_t;
	for (int s = 0; s < 2; s++) {
		float u = 0.22f + 0.56f * (0.5f + 0.5f * us_noise2(mt * 0.012f + s * 5.0f, s * 3.3f)) + (s ? 0.12f : -0.12f);
		u = us_sat(u);
		float cx = u * d->A, cy = ground_y(u, 2) - 0.11f - 0.04f * s + 0.02f * us_noise2(mt * 0.03f, s + 8.0f);
		float R = 0.012f + 0.028f * st->hi_env;
		for (int i = 0; i < NMIDGE / 2; i++) {
			float ph = i * 1.7f + s * 13.0f;
			float mx = cx + R * 1.5f * us_noise2(mt * 1.3f + ph, ph * 0.7f);
			float my = cy + R * 0.8f * us_noise2(ph * 0.9f, mt * 1.1f + ph);
			float gl = sinf(mt * 4.0f + ph * 3.0f);
			gl = gl > 0 ? gl * gl * gl * gl * gl * gl : 0;
			gl *= st->air_env;
			float v = (0.3f + 0.55f * gl) * vis;
			if (d->crisp)
				us_put(c, (int)(mx * c->s), (int)(my * c->s), v < STEP1 ? STEP1 : v, gl > 0.3f ? US_MAT_LIGHT : US_MAT_ACCENT);
			else
				us_point(c, mx, my, 0.0009f, v, gl > 0.3f ? US_MAT_LIGHT : US_MAT_ACCENT);
		}
	}
}

/* fireflies: the quiet-night life of the meadow, more of them the stiller the music */
static void fireflies(dc_t *d, const us_ctx *ctx)
{
	st_t *st = d->st;
	us_canvas *c = d->c;
	float night = us_sat(1.0f - ctx->w->daylight * 2.2f);
	float quiet = 1.0f - 0.65f * us_sat(d->a->level * 1.4f);
	float nf = MAX_FF * night * quiet * (1.0f - d->winter) * (1.0f - 0.5f * d->autumn) *
		   (0.45f + 0.55f * us_sat(ctx->w->insects + ctx->w->calm));
	float t = st->ff_t;
	for (int i = 0; i < MAX_FF && i < nf + 1.0f; i++) {
		float fade = us_sat(nf - i);
		uint32_t h = (uint32_t)i * 2654435761u + 4242u;
		float u = 0.04f + 0.92f * hashf(h);
		float x = u * d->A + 0.06f * us_noise2(t * 0.04f, i * 3.1f);
		float y = ground_y(u, 1) - 0.012f - 0.1f * hashf(h + 1) + 0.025f * us_noise2(i * 1.9f, t * 0.06f);
		float ph = fmodf(t * (0.22f + 0.18f * hashf(h + 2)) + hashf(h + 3), 1.0f);
		float b = ph < 0.14f ? sinf(ph / 0.14f * US_PI) : 0.0f;
		float v = (0.07f + 0.85f * b) * night * fade;
		if (d->crisp) {
			if (v > 0.1f)
				us_put(c, (int)(x * c->s), (int)(y * c->s), v < STEP1 ? STEP1 : v, US_MAT_LIGHT);
		} else {
			us_point(c, x, y, 0.0013f, v, US_MAT_LIGHT);
			if (b > 0.3f)
				wring(c, x, y, 0.0045f, 0.001f, 0.15f * b * night * fade, US_MAT_LIGHT);
		}
	}
}

/* ---- the focal plants, the drums' answers, the foreground --------------- */

static void puff_at(st_t *st, float x, float y, float r, uint8_t mat)
{
	if (st->npuff >= MAX_PUFF)
		return;
	int i = st->npuff++;
	st->puff_x[i] = x;
	st->puff_y[i] = y;
	st->puff_age[i] = 0;
	st->puff_r[i] = r;
	st->puff_mat[i] = mat;
}

/* the oxeye cluster on the left third: three tall stems rising from the very front, big heads with a
 * golden-angle disc of florets. Loudness opens them, each kick swells them, a snare knocks pollen off */
static void hero(dc_t *d, int snare)
{
	us_canvas *c = d->c;
	st_t *st = d->st;
	float bloomy = us_sat(1.0f - d->winter * 1.6f);
	static const float HH[NHERO] = {0.74f, 0.61f, 0.5f}, DX[NHERO] = {0.0f, -0.075f, 0.07f};
	static const float LEAN[NHERO] = {0.06f, -0.1f, 0.17f}, RR[NHERO] = {0.06f, 0.05f, 0.042f};
	float op = us_sat(0.25f + 0.9f * st->glow) * bloomy;
	dc_t hd = *d; /* heavy heads: the wind barely stirs them, the kick still bows them */
	hd.wind *= 0.35f;
	uint8_t sm = d->autumn > 0.6f || d->winter > 0.5f ? US_MAT_EARTH : US_MAT_STEM;
	uint8_t pm = d->autumn > 0.6f ? US_MAT_LIGHT : US_MAT_ACCENT;
	float lift = d->lift;
	for (int h = 0; h < NHERO; h++) {
		float gx = 0.25f * d->A + DX[h], gy = 1.03f;
		enum { NS = 12 };
		float pts[2 * (NS + 1)];
		float H = HH[h] * (0.92f + 0.08f * bloomy);
		float ang = stalk(&hd, gx, gy, H, 1.0f, LEAN[h], -LEAN[h] * 0.13f + 0.004f, 70.0f + h * 2.3f, NS, pts);
		for (int k = 0; k < NS; k++)
			wline(c, pts[2 * k], pts[2 * k + 1], pts[2 * k + 2], pts[2 * k + 3], 0.0032f - 0.0016f * k / NS, 0.72f * lift, sm);
		/* narrow toothed leaves, alternate, on the lower stem */
		if (leafy(d) > 0.05f)
			for (int k = 2; k < 8; k += 2) {
				float dx = pts[2 * k + 2] - pts[2 * k], dy = pts[2 * k + 3] - pts[2 * k + 1];
				float sa = atan2f(dy, dx), sd = ((k / 2 + h) & 1) ? 1.0f : -1.0f;
				float ll = 0.055f * (1.1f - 0.1f * k) * leafy(d);
				petal(c, d->crisp, pts[2 * k], pts[2 * k + 1], sa + sd * (0.75f + 0.1f * sway_n(d, gx, k)), ll, ll * 0.17f,
				      0.55f * lift, leaf_mat(d, 900u + (uint32_t)(h * 8 + k)));
			}
		snowcap(d, pts[2 * NS], pts[2 * NS + 1], 0.006f);
		if (bloomy < 0.05f)
			continue;
		/* the head faces up along the stalk, seen a little from the side */
		float hx = pts[2 * NS], hy = pts[2 * NS + 1];
		float kp = d->kp * (0.3f + 0.7f * op);
		float R = RR[h] * (0.5f + 0.5f * op) * (1.0f + 0.32f * kp);
		float tilt = (ang + US_PI * 0.5f) * 0.7f, ct = cosf(tilt), sn = sinf(tilt), sq = 0.42f;
		float pv = us_sat((0.6f + 0.35f * op) * lift + 0.35f * kp);
		int np = d->crisp ? 13 : 21;
		for (int q = 0; q < np; q++) {
			float a0 = q * US_TAU / np + h * 0.7f + (5.0f - h) * d->rat;
			/* open: a flat ring; closed: a cup of petals pointing up */
			float ox = cosf(a0), oy = sinf(a0) * sq;
			float cx = 0.35f * cosf(a0), cy = -0.85f;
			float lx = us_lerp(cx, ox, op), ly = us_lerp(cy, oy, op);
			float wx = lx * ct - ly * sn, wy = lx * sn + ly * ct;
			float pl = sqrtf(wx * wx + wy * wy) + 1e-4f;
			float back = oy < 0 ? 0.8f : 1.0f; /* far petals a touch dimmer */
			float pa = atan2f(wy, wx), dr0 = R * 0.3f * (0.4f + 0.6f * op); /* from the rim of the disc */
			petal(c, d->crisp, hx + cosf(pa) * dr0 * pl, hy + sinf(pa) * dr0 * pl, pa, R * pl * 0.75f, R * 0.15f, pv * back, pm);
		}
		/* the disc: golden-angle florets */
		float dr = R * 0.3f * (0.4f + 0.6f * op);
		if (d->crisp)
			us_disc(c, hx, hy, dr * 1.1f, us_sat(STEP3 + 0.3f * kp), US_MAT_LIGHT);
		else {
			int nf = 34;
			for (int i = 0; i < nf; i++) {
				float rr = dr * sqrtf((i + 0.5f) / nf), fa = i * 2.39996f;
				float lx = cosf(fa) * rr, ly = sinf(fa) * rr * sq;
				us_put(c, (int)((hx + lx * ct - ly * sn) * c->s), (int)((hy + lx * sn + ly * ct) * c->s),
			       us_sat((0.55f + 0.25f * (1.0f - rr / dr)) * lift + 0.4f * kp), US_MAT_LIGHT);
			}
		}
		if (op > 0.4f)
			add_anchor(st, hx, hy, op);
		if (snare && op > 0.2f)
			puff_at(st, hx, hy, R * 2.2f, US_MAT_LIGHT);
	}
}

/* the dandelion clock on the right third: a sphere of seeds turning slowly; a snare blows a few loose,
 * the wind takes one now and then, and the head fills again */
static void dandelion(dc_t *d, const us_ctx *ctx, float snare)
{
	us_canvas *c = d->c;
	st_t *st = d->st;
	float A = d->A, gx = 0.79f * A, gy = 1.03f;
	if (d->winter > 0.75f)
		return;
	uint8_t sm = d->autumn > 0.6f ? US_MAT_EARTH : US_MAT_STEM;
	/* toothed rosette leaves along the bottom edge */
	if (leafy(d) > 0.05f)
		for (int k = 0; k < 5; k++) {
			float la = -US_PI * 0.5f + (k - 2) * 0.42f + 0.06f * sway_n(d, gx, k + 30.0f);
			float ll = (0.09f - 0.012f * abs(k - 2)) * leafy(d);
			float ux = cosf(la), uy = sinf(la), nx = -uy, ny = ux;
			float pts[2 * 9];
			for (int q = 0; q <= 8; q++) { /* zig-zag edge: dandelion teeth */
				float t = q / 8.0f, wv = (q & 1 ? 0.13f : 0.05f) * ll * (1.0f - t);
				pts[2 * q] = gx + ux * ll * t + nx * wv;
				pts[2 * q + 1] = 0.995f + uy * ll * t + ny * wv;
			}
			if (d->crisp)
				us_tri(c, gx - nx * ll * 0.08f, 0.995f - ny * ll * 0.08f, gx + nx * ll * 0.08f, 0.995f + ny * ll * 0.08f,
				       gx + ux * ll, 0.995f + uy * ll, STEP2, leaf_mat(d, 700u + k));
			else
				wpoly(c, pts, 9, 0.0012f, 0.45f * d->lift, leaf_mat(d, 700u + k));
		}
	enum { NS = 10 };
	float pts[2 * (NS + 1)];
	stalk(d, gx, gy, 0.6f, 1.0f, -0.05f, 0.006f, 91.0f, NS, pts);
	wpoly(c, pts, NS + 1, 0.0022f, 0.58f * d->lift, sm);
	float hx = pts[2 * NS], hy = pts[2 * NS + 1];
	float R = 0.056f * (1.0f + 0.1f * d->kp);
	float rot = 0.4f * sinf(d->t * 0.05f) + 0.5f * sway_n(d, gx, 92.0f);
	float cr = cosf(rot), sr = sinf(rot);
	/* release: a snare blows a handful loose from the downwind side; the wind takes the odd one */
	int rel = (int)(snare * 7.0f + 0.5f);
	if (us_rand(&st->rng) < d->dt * (0.12f + 0.3f * d->wind))
		rel++;
	for (int i = 0; i < NCLOCK; i++) {
		float z = 1.0f - 2.0f * (i + 0.5f) / NCLOCK, rr = sqrtf(us_sat(1.0f - z * z)), ph = i * 2.39996f;
		float x3 = rr * cosf(ph), y3 = rr * sinf(ph);
		float dx = x3 * cr + z * sr, dz = -x3 * sr + z * cr, dy = y3;
		if (!st->clock_on[i])
			continue;
		float tx = hx + dx * R, ty = hy + dy * R;
		if (rel > 0 && dz > -0.2f && us_rand(&st->rng) < 0.35f) {
			st->clock_on[i] = 0;
			rel--;
			if (st->nparts < MAX_PARTS) {
				spawn(st, tx, ty, dx * 0.12f - 0.05f - 0.1f * snare, dy * 0.08f - 0.04f - 0.05f * snare, 0.006f, US_MAT_ACCENT, 3);
				st->parts[st->nparts - 1].rot = atan2f(dy, dx) + US_PI * 0.5f;
			}
			continue;
		}
		float v = (0.4f + 0.5f * (dz + 1.0f) * 0.5f) * d->lift + 0.25f * d->kp;
		if (d->crisp) {
			if (dz > 0.2f && (i & 1))
				us_line(c, hx, hy, tx, ty, 0.003f, STEP2, US_MAT_ACCENT);
			us_put(c, (int)(tx * c->s), (int)(ty * c->s), us_sat(dz > -0.3f ? v : STEP2), US_MAT_ACCENT);
			continue;
		}
		wline(c, hx + dx * R * 0.12f, hy + dy * R * 0.12f, tx, ty, 0.001f, v * 0.55f, US_MAT_ACCENT);
		/* the pappus: a tiny fan of hairs at the tip */
		float oa = atan2f(dy, dx), pl = R * 0.2f;
		for (int f = -1; f <= 1; f += 2)
			wline(c, tx, ty, tx + cosf(oa + f * 0.45f) * pl, ty + sinf(oa + f * 0.45f) * pl, 0.001f, v * 0.8f, US_MAT_ACCENT);
	}
	us_disc(c, hx, hy, d->crisp ? 0.006f : 0.0035f, (d->crisp ? STEP2 : 0.4f) * d->lift, US_MAT_EARTH);
	/* the head fills again */
	st->clock_regrow = fminf(st->clock_regrow + d->dt * (1.6f + 1.5f * (1.0f - us_sat(ctx->a->active))), (float)NCLOCK);
	while (st->clock_regrow >= 1.0f) {
		st->clock_regrow -= 1.0f;
		int i = (int)(us_rand(&st->rng) * NCLOCK) % NCLOCK;
		st->clock_on[i] = 1;
	}
}

/* pollen puffs a snare knocks out of the open flowers: a ring of grains flung outward, fading */
static void puffs(dc_t *d)
{
	st_t *st = d->st;
	us_canvas *c = d->c;
	for (int i = 0; i < st->npuff;) {
		st->puff_age[i] += d->dt;
		float a = st->puff_age[i] / 0.5f;
		if (a >= 1.0f) {
			int j = --st->npuff;
			st->puff_x[i] = st->puff_x[j];
			st->puff_y[i] = st->puff_y[j];
			st->puff_age[i] = st->puff_age[j];
			st->puff_r[i] = st->puff_r[j];
			st->puff_mat[i] = st->puff_mat[j];
			continue;
		}
		float e = 1.0f - (1.0f - a) * (1.0f - a) * (1.0f - a), v = (1.0f - a) * (1.0f - a);
		float r = st->puff_r[i] * (0.3f + e), x = st->puff_x[i], y = st->puff_y[i] + 0.02f * a * a;
		int n = d->crisp ? 10 : 18;
		for (int k = 0; k < n; k++) {
			uint32_t h = (uint32_t)(i * 131 + k) * 2654435761u;
			float ang = US_TAU * (k + 0.4f * hashf(h)) / n, rk = r * (0.6f + 0.5f * hashf(h + 1));
			float px = x + cosf(ang) * rk, py = y + sinf(ang) * rk * 0.8f;
			if (d->crisp)
				us_put(c, (int)(px * c->s), (int)(py * c->s), us_sat(0.3f + 0.7f * v), st->puff_mat[i]);
			else
				us_point(c, px, py, 0.0011f + 0.0008f * hashf(h + 2), 0.95f * v, st->puff_mat[i]);
		}
		i++;
	}
}

/* hats: dew drops on the grass tips catch the light, a different handful each tick */
static void dew_glints(dc_t *d, float dens)
{
	st_t *st = d->st;
	us_canvas *c = d->c;
	float hv = us_sat(d->a->hat * 1.3f);
	if (hv < 0.04f || st->ntip < 4 || d->winter > 0.7f)
		return;
	int n = (int)(36 + 44 * dens);
	for (int k = 0; k < n; k++) {
		uint32_t h = (st->hat_seed * 977u + (uint32_t)k) * 2246822519u;
		int i = (int)(hashf(h) * st->ntip) % st->ntip;
		float x = st->tip_x[i], y = st->tip_y[i], g = hv * (0.6f + 0.4f * hashf(h + 1));
		if (d->crisp) {
			int px = (int)(x * c->s), py = (int)(y * c->s);
			us_put(c, px, py, us_sat(0.45f + 0.55f * g), US_MAT_LIGHT);
			if (g > 0.5f) {
				us_put(c, px - 1, py, 0.4f * g, US_MAT_LIGHT);
				us_put(c, px + 1, py, 0.4f * g, US_MAT_LIGHT);
				us_put(c, px, py - 1, 0.4f * g, US_MAT_LIGHT);
			}
			continue;
		}
		float l = 0.006f + 0.014f * g;
		us_point(c, x, y, 0.0012f, us_sat(0.5f + 0.5f * g), US_MAT_LIGHT);
		wline(c, x - l, y, x + l, y, 0.001f, 0.7f * g, US_MAT_LIGHT);
		wline(c, x, y - l, x, y + l, 0.001f, 0.7f * g, US_MAT_LIGHT);
	}
}

/* tall blades right at the lens, framing the meadow from the bottom corners */
static void fore_blades(dc_t *d, float dens)
{
	us_canvas *c = d->c;
	int n = (int)(16 + 14 * dens);
	uint8_t m = d->autumn > 0.55f || d->winter > 0.55f ? US_MAT_EARTH : US_MAT_LEAF;
	for (int i = 0; i < n; i++) {
		uint32_t h = (uint32_t)i * 3266489917u + 555u;
		float r0 = hashf(h), u;
		if ((i % 5) == 4)
			u = 0.2f + 0.6f * r0;
		else
			u = (i & 1) ? 1.0f - 0.17f * r0 * r0 : 0.15f * r0 * r0;
		float edge = 1.0f - us_smooth(0.0f, 0.22f, fminf(u, 1.0f - u));
		float H = (0.08f + (0.2f + 0.24f * hashf(h + 1)) * edge) * (1.0f - 0.4f * d->winter);
		float x = u * d->A, y = 1.02f;
		float side = (u < 0.5f ? 1.0f : -1.0f) * (0.3f + 0.7f * hashf(h + 2));
		float curl = side * (0.02f + 0.05f * hashf(h + 3));
		float sw = 1.4f * sway_n(d, x, 120.0f + i) + 1.5f * d->rat;
		enum { NB = 7 };
		float L[2 * (NB + 1)], Rr[2 * (NB + 1)];
		float ang = -US_PI * 0.5f + side * 0.12f, px = x, py = y, seg = H / NB;
		float w0 = 0.006f + 0.004f * hashf(h + 4);
		for (int k = 0; k <= NB; k++) {
			float t = (float)k / NB, wd = w0 * (1.0f - t) * 0.5f;
			float nx = -sinf(ang), ny = cosf(ang);
			L[2 * k] = px + nx * wd;
			L[2 * k + 1] = py + ny * wd;
			Rr[2 * k] = px - nx * wd;
			Rr[2 * k + 1] = py - ny * wd;
			ang += curl + sw * (0.2f + t) * 0.35f;
			px += cosf(ang) * seg;
			py += sinf(ang) * seg;
		}
		float v = 0.3f + 0.1f * hashf(h + 5);
		if (d->crisp) {
			for (int k = 0; k < NB; k++) {
				us_tri(c, L[2 * k], L[2 * k + 1], Rr[2 * k], Rr[2 * k + 1], L[2 * k + 2], L[2 * k + 3], STEP2 * 0.8f, m);
				us_tri(c, Rr[2 * k], Rr[2 * k + 1], Rr[2 * k + 2], Rr[2 * k + 3], L[2 * k + 2], L[2 * k + 3], STEP2 * 0.8f, m);
			}
			continue;
		}
		wpoly(c, L, NB + 1, 0.0011f, v, m);
		wpoly(c, Rr, NB + 1, 0.0011f, v * 0.75f, m);
		/* the midrib, faint */
		wline(c, 0.5f * (L[0] + Rr[0]), 0.5f * (L[1] + Rr[1]), 0.5f * (L[2 * NB - 2] + Rr[2 * NB - 2]),
		      0.5f * (L[2 * NB - 1] + Rr[2 * NB - 1]), 0.001f, v * 0.4f, m);
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
		/* loudness opens the meadow (quiet: half-closed at dusk), the bass adds to it */
		float target = us_sat((0.2f + 0.65f * st->glow + 0.4f * st->bloom_env) * (0.3f + 0.7f * season_ok));
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

	static const float want_base[NL] = {9.0f, 5.0f, 4.0f}, want_d[NL] = {12.0f, 9.0f, 8.0f};
	for (int L = 0; L < NL; L++) {
		int want = (int)(want_base[L] + dens * want_d[L]);
		if (want > MAX_PL)
			want = MAX_PL;
		if (!st->inited || want != st->npl[L]) {
			for (int i = st->inited ? st->npl[L] : 0; i < want; i++) {
				float x = (i + 0.5f) / want + us_randr(&st->rng, -0.35f, 0.35f) / want;
				/* the meadow opens already standing (a young world: younger plants, fewer in flower) */
				float g0 = us_sat(us_randr(&st->rng, 0.45f, 1.0f) * (0.85f + w->growth));
				plant_t *p = &st->pl[L][i];
				new_plant(st, p, x, g0, L);
				if (g0 >= 1.0f || us_rand(&st->rng) < 0.3f + w->bloom + (L == 0 ? 0.3f : 0.0f)) {
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
		for (int i = 0; i < NDAISY; i++) {
			st->daisy_x[i] = us_sat((i + 0.5f + us_randr(&st->rng, -0.3f, 0.3f)) / NDAISY);
			st->daisy_h[i] = us_randr(&st->rng, 0.6f, 1.0f);
			st->daisy_ph[i] = us_rand(&st->rng) * 10.0f;
			st->daisy_kind[i] = us_rand(&st->rng) < 0.3f;
		}
		st->snail_x = us_randr(&st->rng, 0.15f, 0.85f);
		st->snail_dir = us_rand(&st->rng) < 0.5f ? -1.0f : 1.0f;
		for (int i = 0; i < NCLOCK; i++)
			st->clock_on[i] = us_rand(&st->rng) < 0.85f;
		st->inited = 1;
	}

	st->bass_env = us_approach(st->bass_env, us_sat(a->bass * 0.8f + a->kick * 0.6f), 0.08f, ctx->real_dt);
	{
		float rdt = ctx->real_dt;
		st->sub_env = env(st->sub_env, a->sub, 0.1f, 0.6f, rdt);
		st->lm_env = env(st->lm_env, a->lowmid, 0.12f, 0.45f, rdt);
		st->mid_env = env(st->mid_env, a->mid, 0.06f, 0.35f, rdt);
		st->hi_env = env(st->hi_env, a->high, 0.04f, 0.3f, rdt);
		st->air_env = env(st->air_env, a->air, 0.05f, 0.4f, rdt);
		st->cen_env = env(st->cen_env, a->centroid * us_sat(a->active), 1.5f, 2.5f, rdt);
		st->wid_env = env(st->wid_env, a->width * us_sat(a->active), 1.5f, 3.0f, rdt);
		for (int i = 0; i < NDAISY; i++) { /* low bands on the left of the bank, high on the right */
			int b0 = 2 + i * (US_NBANDS - 4) / NDAISY;
			float v = 0.5f * (a->bands[b0] + a->bands[b0 + 1]);
			st->band_env[i] = env(st->band_env[i], v, 0.07f, 0.4f, rdt);
		}
		/* loudness: the light and fullness of the meadow (louder = more open, brighter) */
		st->glow = env(st->glow, us_sat(a->level * us_sat(a->active * 1.5f)), 0.7f, 1.3f, rdt);
		st->bloom_env = env(st->bloom_env, a->bass, 0.6f, 1.5f, rdt);
		st->wind_env = env(st->wind_env, a->high * 0.7f + a->air * 0.3f, 0.5f, 1.5f, rdt);
		/* kick: every stem bows downwind together (a damped spring knocked on each hit) and the
		 * flower heads swell with the pulse; it is over well before the next beat */
		d.puff = 0;
		if (a->kick_hit) {
			float k0 = us_sat(0.45f + 0.7f * a->kick);
			st->bow_v += 1.1f * k0;
			d.puff = k0;
			if (k0 > 0.6f)
				st->snail_hide = 1.0f;
		}
		/* snare: a quick shiver runs through the meadow, flower heads twitch on their stalks and
		 * pollen bursts loose (the bursts are thrown further down, once the flowers are placed) */
		if (a->snare_hit)
			st->rat_v += (st->rat > 0 ? -1.0f : 1.0f) * 1.6f * us_sat(0.45f + 0.7f * a->snare);
		spring(&st->bow, &st->bow_v, 0.34f, 0.7f, rdt, 0.12f);
		spring(&st->rat, &st->rat_v, 0.16f, 0.35f, rdt, 0.06f);
		if (a->hat_hit)
			st->hat_seed++;
		d.bow = st->bow;
		d.rat = st->rat;
		d.kp = us_sat(a->kick * 1.15f);
		d.lift = 0.62f + 0.6f * st->glow;
		st->snail_hide = us_approach(st->snail_hide, 0.0f, 1.8f, dt);
		st->midge_t += dt * (1.0f + 3.5f * st->hi_env);
		st->ff_t += dt;
		d.lm = st->lm_env;
		d.mid = st->mid_env;
	}
	float wind = us_sat(0.08f + 0.15f * w->wind + 0.75f * st->wind_env); /* the highs breathe the wind, the hats glint */
	st->wind_t += dt * (0.4f + 1.6f * wind);
	d.wind = wind;
	d.wind_t = st->wind_t;

	us_world_add(w, &w->growth, 0.004f * a->bass * (1 - d.winter), dt);

	sky(&d, ctx, dens);

	float grow_k = 0.06f * (0.35f + w->moisture) * (0.6f + 1.2f * w->mycelium) * (1.0f - 0.85f * d.winter);
	float drive = 0.12f + 1.1f * a->bass + 0.6f * a->kick + 0.2f * a->level;
	st->nanch = 0;
	st->ntip = 0;

	for (int L = 0; L < NL; L++) {
		d.L = L;
		d.lb = LBRIGHT[L];
		d.lw = L == 2 ? 1.0f : (L == 1 ? 0.8f : 0.6f);
		/* ---- ground for this layer: hides the bases of what is behind ---- */
		if (L == 2)
			low_mist(&d, ctx);
		{
			tone_t tn = {0, 0, US_MAT_BG, US_MAT_BG, 0, 0, 0.05f};
			if (ctx->crisp) {
				/* each band of land its own tone: hazy sage far hill, olive mid meadow with a green rim,
				 * a dark near bank shading down into the night */
				static const tone_t T[NL] = {{STEP1, STEP2, US_MAT_LEAF, US_MAT_EARTH, 0.6f, 0.3f, 0.05f},
							     {STEP1, STEP1, US_MAT_LEAF, US_MAT_EARTH, 0.45f, 0.0f, 0.045f},
							     {STEP1, STEP1, US_MAT_BG, US_MAT_EARTH, 0.0f, 0.3f, 0.1f}};
				static const tone_t TW[NL] = {{STEP1, STEP2, US_MAT_WATER, US_MAT_BG, 0.5f, 0.5f, 0.05f},
							      {STEP2, STEP1, US_MAT_WATER, US_MAT_WATER, 0.9f, 0.5f, 0.05f},
							      {STEP2, STEP1, US_MAT_WATER, US_MAT_WATER, 0.6f, 0.15f, 0.1f}};
				tn = d.winter > 0.55f ? TW[L] : T[L]; /* snowfields */
			}
			fill_ground(c, L, A, &tn);
			float pts[2 * 65];
			for (int i = 0; i <= 64; i++) {
				float u = i / 64.0f;
				pts[2 * i] = u * A;
				pts[2 * i + 1] = ground_y(u, L);
			}
			float snow = us_sat((d.winter - 0.4f) * 2.5f);
			wpoly(c, pts, 65, (L == 2 ? 0.0016f : 0.0012f) + (ctx->crisp ? 0.004f : 0.001f) * snow,
				    (0.32f + 0.25f * snow) * (0.5f + 0.5f * LBRIGHT[L]) + 0.2f * (L == 2), snow > 0.5f ? US_MAT_WATER : US_MAT_EARTH);
			hill_texture(&d, L, dens);
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
				float sw = (us_noise2(x * 2.0f - st->wind_t * 0.8f, st->blade_ph[b]) * 0.7f + 0.5f) * (0.15f + 1.3f * wind) +
					   3.0f * d.bow + 4.0f * d.rat * (L == 2 ? 1.0f : 0.6f);
				float pts[8];
				for (int k = 0; k < 4; k++) {
					float t = k / 3.0f;
					pts[2 * k] = x + sw * h * t * t * 0.9f;
					pts[2 * k + 1] = gy - h * t;
				}
				uint8_t bm = gm;
				float bv = (0.35f + 0.2f * bh) * (0.45f + 0.55f * LBRIGHT[L]) * d.lift;
				if (d.autumn > 0.55f) { /* straw and rust */
					bm = (b & 3) == 0 ? US_MAT_LIGHT : US_MAT_EARTH;
					bv *= bm == US_MAT_LIGHT ? 0.6f : 1.2f;
				}
				wpoly(c, pts, 4, 0.001f, bv, bm);
				if (L == 2 && (b % 3) == 1 && st->ntip < MAX_TIP) { /* where the hats can glint */
					st->tip_x[st->ntip] = pts[6];
					st->tip_y[st->ntip++] = pts[7];
				}
				/* dew on the blade tips catches the light with the air band */
				if (L == 2 && (b % 6) == 0 && st->air_env > 0.05f && d.winter < 0.6f) {
					float tw = sinf(d.t * 2.3f + st->blade_ph[b] * 5.0f);
					tw = tw > 0 ? tw * tw * tw * tw : 0;
					float gv = st->air_env * tw * tw;
					if (gv > 0.04f) {
						us_point(c, pts[6], pts[7], ctx->crisp ? 0.003f : 0.0011f, (ctx->crisp ? 0.4f : 0.3f) + 0.6f * gv, US_MAT_LIGHT);
						if (!ctx->crisp && gv > 0.35f) {
							float gl = 0.006f * gv;
							wline(c, pts[6] - gl, pts[7], pts[6] + gl, pts[7], 0.001f, 0.5f * gv, US_MAT_LIGHT);
							wline(c, pts[6], pts[7] - gl, pts[6], pts[7] + gl, 0.001f, 0.5f * gv, US_MAT_LIGHT);
						}
					}
				}
			}
		}
		/* ---- plants ---- */
		for (int i = 0; i < st->npl[L]; i++) {
			plant_t *p = &st->pl[L][i];
			life_cycle(st, p, ctx, grow_k, drive, &d);
			float gx = p->x * A;
			float gy = ground_y(p->x, L) + 0.002f;
			float wither = p->phase == WITHER ? us_sat(1.0f - p->g) : 0.0f;
			float vbase = (0.85f - 0.4f * wither) * LBRIGHT[L] * d.lift;
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
		if (L == 2) {
			draw_daisies(&d, ctx);
			snail(&d);
		}
	}
	d.L = 2;
	d.lb = 1.0f;
	d.lw = 1.0f;

	/* ---- the focal plants in front of the bank ---- */
	float snare = a->snare_hit ? us_sat(0.4f + 0.8f * a->snare) : 0.0f;
	hero(&d, snare > 0);
	dandelion(&d, ctx, snare);
	/* snare: pollen bursts from a scatter of open flowers, a petal or two torn loose with it */
	if (snare > 0 && st->nanch > 0) {
		int nb = (int)(4 + 7 * snare * (0.5f + dens));
		for (int k = 0; k < nb; k++) {
			anchor_t *an = &st->anch[(int)(us_rand(&st->rng) * st->nanch) % st->nanch];
			puff_at(st, an->x, an->y, 0.03f + 0.016f * snare, us_rand(&st->rng) < 0.5f ? US_MAT_LIGHT : US_MAT_BLOOM);
			spawn(st, an->x, an->y, us_randr(&st->rng, -0.12f, 0.2f), us_randr(&st->rng, -0.16f, -0.04f), 0.006f, US_MAT_BLOOM, 0);
		}
	}
	puffs(&d);

	/* ---- butterflies + bees ---- */
	insects(&d, w, dens);
	midges(&d, ctx);
	fireflies(&d, ctx);

	/* ---- drifting petals / leaves / seeds ---- */
	for (int i = 0; i < st->nparts;) {
		part_t *q = &st->parts[i];
		if (q->kind == 3) { /* dandelion parachute: rides the air, lifting and drifting, rocking under its tuft */
			float gu = us_noise2(q->x * 2.0f + st->wind_t * 0.5f, q->y * 2.0f + i * 0.1f);
			q->vx += ((0.03f + 0.06f * wind + 0.04f * gu) - q->vx) * dt * 0.9f;
			q->vy += ((-0.012f + 0.02f * us_noise2(q->y * 3.0f, d.t * 0.3f + i)) - q->vy) * dt * 0.7f;
			q->x += q->vx * dt;
			q->y += q->vy * dt;
			q->life -= dt * 0.07f;
			if (q->life <= 0 || q->x < -0.1f || q->x > A + 0.1f || q->y < -0.05f || q->y > 1.05f) {
				*q = st->parts[--st->nparts];
				continue;
			}
			float tl = 0.35f * us_noise2(d.t * 0.8f, i * 1.3f) + us_clampf(q->vx * 2.0f, -0.3f, 0.3f);
			float ux = sinf(tl), uy = -cosf(tl), s = q->size, v = us_sat(0.75f * us_sat(q->life * 3.0f));
			float tx = q->x + ux * s, ty = q->y + uy * s;
			if (ctx->crisp) {
				us_put(c, (int)(tx * c->s), (int)(ty * c->s), us_sat(0.3f + 0.5f * v), US_MAT_ACCENT);
			} else {
				wline(c, q->x, q->y, tx, ty, 0.001f, v * 0.5f, US_MAT_ACCENT);
				us_point(c, q->x, q->y, 0.0009f, v * 0.6f, US_MAT_EARTH);
				for (int f = -2; f <= 2; f++) {
					float fa = tl - US_PI * 0.5f + f * 0.42f;
					wline(c, tx, ty, tx + cosf(fa) * s * 0.55f, ty + sinf(fa) * s * 0.55f, 0.001f, v * 0.6f, US_MAT_ACCENT);
				}
			}
			i++;
			continue;
		}
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

	/* ---- the nearest things: blades at the lens, dew glinting on the hats ---- */
	fore_blades(&d, dens);
	dew_glints(&d, dens);

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
