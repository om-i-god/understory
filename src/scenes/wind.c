/* Understory scene: Wind — a prairie of grass in perspective, combed by gusts.
 *
 * Thousands of blades and seed stalks stand in depth rows (far rows small and faint, near rows tall),
 * each with its own resting arch, in clumps and hollows, with drifts of wildflowers. A trodden path winds
 * from the bottom edge up to a lone tree; tall oat stalks in the very foreground frame the view; a hazy
 * far range and a windbreak of poplars sit on the horizon. Cloud shadows slide over the grass.
 * The steady wind is a slow travelling noise field in ground-plane coordinates: calm, broad rolling waves.
 * The music's drums arrive as events on top of it, each with its own answer:
 *   kick  -> the whole field ducks under the gust and a bright front rolls across it (a slanted wave,
 *            far rows a beat later); the tree shudders, the oat stalks heel over, poplars bow in turn.
 *   snare -> a sharp flurry: the stalks shiver, seed fluff and petals are torn off the tips in streaks,
 *            the tree loses a few leaves, a swallow darts; a hard snare startles a little flock up out
 *            of the grass.
 *   hats  -> light glints off grass tips across the field and the far rows' pale blades flash.
 * Loudness is the steady wind's strength and the light: loud passages lift the field into sun (cloud
 * shadows burn off, the sun's halo opens, cloud rims brighten); quiet ones sink into still, heavy air.
 * Also: sub -> a long swell rolling toward the viewer; bass -> poplars lean; lowmid -> flowers nod;
 * mid -> kite climbs, swallows rise; air -> kite tail + midges dance; width -> counter-gusts from the
 * other flank; centroid -> wind direction.
 * World: reads growth (grass height), season (green spring, seeding summer, golden autumn, sparse
 *        snow-bent winter), daylight (sun/moon, brightness), wind (prevailing breeze), bloom (flowers);
 *        writes pollen (seeds shaken loose, snare flurries).
 * Silence: the prevailing breeze keeps slow, gentle waves moving through; midges come out.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAX_ROWS 34
#define MAX_BLADES 3600
#define MAX_SEEDS 700
#define MAX_FRONTS 8
#define MAX_BIRD 6
#define MAX_GNAT 28
#define KITE_ROW 17
#define NTAIL 14
#define HZ 0.47f /* horizon */
#define MAX_TSEG 96
#define MAX_LEAF 420
#define MAX_TUMB 160
#define MAX_CLOUD 7
#define MAX_WISP 20
#define TREE_ROW 21

typedef struct {
	float u;    /* 0..1 across the row */
	float h;    /* height jitter */
	float ph;   /* phase / hash */
	uint8_t head; /* 1 = seed stalk */
	uint8_t tint; /* 0..3 material variation */
	float clump, fpatch; /* static ground patterns (height clumps, flower drifts), cached per aspect */
} blade_t;

typedef struct {
	float x, y, vx, vy, life, ph, sz;
} seed_t;

typedef struct {
	float pos, amp, speed, width; /* pos along the wind axis in world units */
	float sgn;                    /* +1 downwind front, -1 a counter-gust from the other side (stereo width) */
} front_t;

typedef struct {
	float x, y, vx, vy, ph, bank, alt; /* swallows riding the gusts */
} bird_t;

typedef struct {
	int parent;
	float rel, len, w;
	uint8_t depth;
} tseg_t;

typedef struct {
	int seg;
	float off, ang, sz, ph, keep;
} leaf_t;

typedef struct {
	float x, y, vx, vy, rot, vr, flip, vf, life, sz;
	uint8_t mat;
} tumb_t;

typedef struct {
	float x, y, w, h, sp, seed; /* x as fraction of the width */
} cloud_t;

typedef struct {
	float x, y, age, life, len, amp, ph, sp, dir;
} wisp_t;

#define MAX_TIPS 256
#define MAX_FLAKE 240
#define MAX_GLINT 96
#define MAX_FLUSH 12
#define NFG 7

typedef struct {
	float x, y, vx, vy, px, py, life, sz;
	uint8_t mat;
} flake_t; /* petals / seed fluff torn off by a snare */

typedef struct {
	float x, y, life, sz;
} glint_t; /* a hat: light flashing off a grass tip */

typedef struct {
	float x, y, vx, vy, ph, life, sz;
} flush_t; /* birds startled up out of the grass */

typedef struct {
	us_rng rng;
	tseg_t ts[MAX_TSEG];
	int nts;
	float tex[MAX_TSEG], tey[MAX_TSEG], tea[MAX_TSEG];
	leaf_t lf[MAX_LEAF];
	int nlf;
	tumb_t tb[MAX_TUMB];
	int ntb;
	cloud_t cl[MAX_CLOUD];
	wisp_t wp[MAX_WISP];
	float tree_u;
	float tbend, tvel; /* tree spring */
	float wisp_acc;
	int row_start[MAX_ROWS + 1];
	blade_t bl[MAX_BLADES];
	seed_t sd[MAX_SEEDS];
	int nsd;
	front_t fr[MAX_FRONTS];
	float flow;   /* integrated wind travel */
	float gust;   /* smoothed gust strength */
	float dir;    /* smoothed direction angle */
	float swell;  /* bass swell */
	float cloud_t;
	float seed_acc;
	float last_beat_t;
	double t;
	/* audio followers: each part of the music has its own carrier in the scene */
	float kick_prev, last_kick_t, kpulse; /* kicks -> gust fronts, tree shudder, poplars bowing */
	float last_onset_t;   /* onsets -> leaves torn loose, swallows dart */
	float sub_s, sub_ph;  /* sub -> a long ground swell rolling toward the viewer */
	float bass_s, lowmid_s, mid_s, air_s, swell_s, width_s;
	float counter;        /* alternates counter-gusts when the mix is wide */
	/* kite flown by a small figure out in the field; mids lift it */
	float kite_u, klift, ktilt, kvel;
	float tail[2 * (NTAIL + 1)];
	bird_t bd[MAX_BIRD];
	float gnat_x;         /* swarm centre (fraction of width), drifting downwind */
	float flower_seed;
	/* this round: sustained wind + light from loudness, a distinct answer to each drum */
	float lvl_s, breeze, light; /* loudness -> steady wind strength and how much light the field holds */
	float kimp;           /* kick: the whole field ducks under the gust as its front rolls across */
	float senv, henv;     /* snare: a shiver through the stalks; hats: tips flash */
	float tipx[MAX_TIPS], tipy[MAX_TIPS], tips[MAX_TIPS]; /* seed heads / flowers seen last frame (reservoir) */
	int ntip, tipseen;
	flake_t fk[MAX_FLAKE];
	int nfk;
	glint_t gl[MAX_GLINT];
	int ngl;
	flush_t fb[MAX_FLUSH];
	float last_flush_t;
	float fgx[NFG], fgh[NFG], fgph[NFG], fgb[NFG], fgv[NFG]; /* foreground oat stalks framing the view */
	float path_x0;        /* where the trodden path leaves the bottom edge (fraction of width) */
	float cache_A;        /* aspect the blade patterns were computed for */
} st_t;

static float row_s(int r)
{
	/* perspective scale of row r: 0 (far) .. 1 (near); rows bunch toward the horizon */
	float f = (r + 0.6f) / MAX_ROWS;
	return 0.04f + 0.96f * f * f * f;
}

static void tbranch(st_t *s, us_rng *r, int parent, float rel, float len, float w, int depth)
{
	if (s->nts >= MAX_TSEG)
		return;
	int id = s->nts++;
	tseg_t *t = &s->ts[id];
	t->parent = parent;
	t->rel = rel;
	t->len = len;
	t->w = w;
	t->depth = (uint8_t)depth;
	if (depth >= 5)
		return;
	int kids = depth == 0 ? 3 : 2;
	for (int k = 0; k < kids; k++) {
		float a = (k - (kids - 1) * 0.5f) * us_randr(r, 0.55f, 0.95f) * (depth == 0 ? 1.0f : 1.0f) + us_randn(r) * 0.15f;
		tbranch(s, r, id, a, len * us_randr(r, 0.66f, 0.82f), w * 0.6f, depth + 1);
	}
}

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng *r = &s->rng;
	us_rng_seed(r, seed ^ 0x77696e64u);
	int n = 0;
	for (int row = 0; row < MAX_ROWS; row++) {
		s->row_start[row] = n;
		float sc = row_s(row);
		/* far rows hold more (smaller) blades across the same screen width */
		int cnt = (int)(105.0f - 62.0f * sc);
		for (int k = 0; k < cnt && n < MAX_BLADES; k++) {
			blade_t *b = &s->bl[n++];
			b->u = us_randr(r, -0.03f, 1.03f);
			b->h = us_randr(r, 0.55f, 1.0f);
			b->ph = us_rand(r) * 100.0f;
			b->head = us_rand(r) < 0.22f;
			b->tint = (uint8_t)(us_rand(r) * 4);
		}
	}
	s->row_start[MAX_ROWS] = n;
	/* the lone tree: a spreading crown on a short trunk */
	s->tree_u = us_rand(r) < 0.5f ? us_randr(r, 0.16f, 0.3f) : us_randr(r, 0.7f, 0.84f);
	tbranch(s, r, -1, us_randn(r) * 0.04f, 0.28f, 1.0f, 0);
	for (int i = 0; i < s->nts && s->nlf < MAX_LEAF; i++) {
		if (s->ts[i].depth < 3)
			continue;
		int nl = s->ts[i].depth + 1;
		for (int k = 0; k < nl && s->nlf < MAX_LEAF; k++) {
			leaf_t *l = &s->lf[s->nlf++];
			l->seg = i;
			l->off = us_randr(r, 0.25f, 1.05f);
			l->ang = us_randr(r, -US_PI, US_PI);
			l->sz = us_randr(r, 0.7f, 1.3f);
			l->ph = us_rand(r) * 20;
			l->keep = us_rand(r);
		}
	}
	for (int i = 0; i < MAX_CLOUD; i++) {
		cloud_t *cl = &s->cl[i];
		cl->x = us_randr(r, -0.3f, 1.2f);
		cl->y = us_randr(r, 0.1f, 0.33f);
		cl->w = us_randr(r, 0.22f, 0.5f) * (0.6f + 1.2f * cl->y);
		cl->h = cl->w * us_randr(r, 0.22f, 0.34f);
		cl->sp = us_randr(r, 0.7f, 1.3f) * (1.3f - cl->y);
		cl->seed = us_rand(r) * 100;
	}
	s->dir = 0;
	s->cloud_t = us_rand(r) * 50;
	/* the kite flyer stands on the far side of the field from the tree */
	s->kite_u = s->tree_u < 0.5f ? us_randr(r, 0.58f, 0.7f) : us_randr(r, 0.3f, 0.42f);
	s->klift = 0.4f;
	for (int i = 0; i < MAX_BIRD; i++) {
		bird_t *b = &s->bd[i];
		b->x = us_rand(r);
		b->y = us_randr(r, HZ - 0.2f, HZ - 0.04f);
		b->vx = us_randr(r, -0.1f, 0.1f);
		b->ph = us_rand(r) * US_TAU;
		b->alt = us_rand(r);
	}
	s->gnat_x = us_rand(r);
	s->last_kick_t = s->last_onset_t = s->last_flush_t = -10;
	/* the trodden path enters from the bottom on the far side from the tree and winds up to it */
	s->path_x0 = s->tree_u < 0.5f ? us_randr(r, 0.55f, 0.68f) : us_randr(r, 0.32f, 0.45f);
	/* tall oat stalks in the very foreground, mostly at the edges, framing the field */
	for (int i = 0; i < NFG; i++) {
		float side = (i & 1) ? 1.0f : 0.0f;
		float u = side > 0 ? us_randr(r, 0.82f, 1.02f) : us_randr(r, -0.02f, 0.2f);
		if (i == NFG - 1)
			u = s->path_x0 + (s->tree_u < 0.5f ? 0.16f : -0.16f); /* one beside the path mouth */
		if (fabsf(u - s->tree_u) < 0.07f) /* keep the trunk clear */
			u += u < s->tree_u ? -0.08f : 0.08f;
		s->fgx[i] = u;
		s->fgh[i] = us_randr(r, 0.3f, 0.46f);
		s->fgph[i] = us_rand(r) * 50.0f;
	}
	s->breeze = 0.15f;
	s->light = 0.6f;
	return s;
}

static void destroy(void *p) { free(p); }

/* Fast tapered blade: a quadratic curve rasterised as one horizontal span per pixel row
 * (blades are mostly upright; strongly bent tips still connect because each row spans from the
 * previous x to the current one). Coverage-weighted edges in ink, hard pixels in crisp.
 * (x0,y0) base, tip offset bx*h at full height h*fs, base width w (normalized). */
static void blade(us_canvas *c, float x0, float y0, float bx, float h, float fs, float w, float v, uint8_t m)
{
	float S = c->s;
	float hp = h * fs * S;
	int n = (int)ceilf(hp);
	if (n < 1)
		n = 1;
	float k = 0.3f * fabsf(bx);
	float px_prev = x0 * S;
	int last_row = (int)floorf(y0 * S);
	for (int i = 1; i <= n; i++) {
		float t = (float)i / n;
		float px = (x0 + bx * h * t * t) * S;
		float py = (y0 - h * fs * t * (1.0f - k * t)) * S;
		int iy = (int)floorf(py);
		if (iy == last_row && i < n)
			continue;
		last_row = iy;
		if ((unsigned)iy >= (unsigned)c->h) {
			px_prev = px;
			continue;
		}
		float hw = 0.5f * w * S * (1.0f - 0.75f * t);
		if (hw < 0.5f)
			hw = 0.5f;
		float a = (px < px_prev ? px : px_prev) - hw, b = (px < px_prev ? px_prev : px) + hw;
		px_prev = px;
		if (c->crisp) {
			int ia = (int)floorf(a + 0.5f), ib = (int)floorf(b - 0.5f);
			if (ib < ia)
				ib = ia;
			for (int ix = ia; ix <= ib; ix++)
				us_put(c, ix, iy, v, m);
		} else {
			int ia = (int)floorf(a), ib = (int)floorf(b);
			float vt = v * (1.0f - 0.35f * t);
			if (ia == ib) {
				us_put(c, ia, iy, vt * (b - a), m);
				continue;
			}
			us_put(c, ia, iy, vt * (ia + 1 - a), m);
			for (int ix = ia + 1; ix < ib; ix++)
				us_put(c, ix, iy, vt, m);
			us_put(c, ib, iy, vt * (b - ib), m);
		}
	}
}

/* wind bend at a ground point (world X lateral, Z depth) */
static float wind_at(const st_t *s, float X, float Z, float ax, float az)
{
	/* coordinate along the wind axis: slow, broad rolling waves (the steady breeze) */
	float along = X * ax + Z * az;
	float across = -X * az + Z * ax;
	float g = us_noise2(along * 0.45f - s->flow, across * 0.3f + 3.7f);
	g += 0.35f * us_noise2(along * 1.2f - s->flow * 1.7f, across * 0.8f + 9.1f);
	return us_smooth(-0.45f, 0.75f, g);
}

/* kick gust fronts, in screen space: u = 0..1 downwind across the frame, sc = row depth (far rows are
 * reached a little later, so each front sweeps in as a slanted wave). Returns signed extra bend. */
static float front_at(const st_t *s, float u, float sc)
{
	float bend = 0;
	for (int i = 0; i < MAX_FRONTS; i++) {
		const front_t *f = &s->fr[i];
		if (f->amp <= 0.002f)
			continue;
		float uu = f->sgn > 0 ? u : 1.0f - u;
		float d = (uu + 0.22f * (1.0f - sc) - f->pos) / f->width;
		/* steep leading edge, long recovering wake behind the crest */
		if (d > -3.5f && d < 2.0f)
			bend += f->sgn * f->amp * (d > 0 ? expf(-d * d * 3.0f) : expf(-d * d * 0.35f));
	}
	return bend;
}

/* fast thin antialiased line for ink (two-pixel Wu spans); us_line for crisp / wide lines */
static void wline(us_canvas *c, float ax, float ay, float bx, float by, float width, float v, uint8_t mat)
{
	float wp = width * c->s;
	if (c->crisp || wp > 1.6f) {
		us_line(c, ax, ay, bx, by, width, v, mat);
		return;
	}
	v *= us_clampf(wp, 0.35f, 1.0f);
	float x0 = ax * c->s - 0.5f, y0 = ay * c->s - 0.5f, x1 = bx * c->s - 0.5f, y1 = by * c->s - 0.5f;
	int steep = fabsf(y1 - y0) > fabsf(x1 - x0);
	if (steep) {
		float t = x0; x0 = y0; y0 = t;
		t = x1; x1 = y1; y1 = t;
	}
	if (x0 > x1) {
		float t = x0; x0 = x1; x1 = t;
		t = y0; y0 = y1; y1 = t;
	}
	float g = fabsf(x1 - x0) > 1e-6f ? (y1 - y0) / (x1 - x0) : 0.0f;
	int i0 = (int)floorf(x0 + 0.5f), i1 = (int)floorf(x1 + 0.5f);
	int lim = steep ? c->h : c->w;
	if (i0 < 0) i0 = 0;
	if (i1 > lim - 1) i1 = lim - 1;
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

static void wring(us_canvas *c, float x, float y, float r, float width, float v, uint8_t mat)
{
	int n = (int)(r * c->s * 0.6f);
	if (n < 8) n = 8;
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

/* crisp-only overwrite primitives: the tree must hide the (brighter) clouds behind it */
static void oput(us_canvas *c, int x, int y, float v, uint8_t m)
{
	if ((unsigned)x >= (unsigned)c->w || (unsigned)y >= (unsigned)c->h)
		return;
	size_t i = (size_t)y * c->w + x;
	c->ink[i] = (uint8_t)(us_sat(v) * 255.0f + 0.5f);
	c->mat[i] = m;
}

static void otri(us_canvas *c, float x0, float y0, float x1, float y1, float x2, float y2, float v, uint8_t m)
{
	float S = c->s;
	x0 *= S; y0 *= S; x1 *= S; y1 *= S; x2 *= S; y2 *= S;
	int ax = (int)floorf(fminf(x0, fminf(x1, x2))), bx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
	int ay = (int)floorf(fminf(y0, fminf(y1, y2))), by = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
	float d = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
	if (fabsf(d) < 1e-6f || bx - ax > 64 || by - ay > 64)
		return;
	for (int y = ay; y <= by; y++)
		for (int x = ax; x <= bx; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / d;
			float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / d;
			if (w0 >= 0 && w1 >= 0 && w0 + w1 <= 1.0f)
				oput(c, x, y, v, m);
		}
}

static void oline(us_canvas *c, float ax, float ay, float bx, float by, float width, float v, uint8_t m)
{
	float S = c->s;
	float dx = (bx - ax) * S, dy = (by - ay) * S;
	int n = (int)(fmaxf(fabsf(dx), fabsf(dy))) + 1;
	int r = (int)(width * S * 0.5f);
	for (int i = 0; i <= n; i++) {
		float t = (float)i / n;
		int x = (int)floorf(ax * S + dx * t), y = (int)floorf(ay * S + dy * t);
		for (int k = -r; k <= r; k++)
			oput(c, x + k, y, v, m);
	}
}

static void leafshape(us_canvas *c, float x, float y, float ang, float len, float wid, float v, uint8_t m)
{
	float dx = cosf(ang), dy = sinf(ang);
	float tx = x + dx * len, ty = y + dy * len, mx = x + dx * len * 0.5f, my = y + dy * len * 0.5f;
	float nx = -dy * wid, ny = dx * wid;
	if (c->crisp) {
		us_tri(c, x, y, mx + nx, my + ny, tx, ty, v, m);
		us_tri(c, x, y, mx - nx, my - ny, tx, ty, v, m);
	} else {
		wline(c, x, y, mx + nx, my + ny, 0.0011f, v, m);
		wline(c, mx + nx, my + ny, tx, ty, 0.0011f, v, m);
		wline(c, tx, ty, mx - nx, my - ny, 0.0011f, v, m);
		wline(c, mx - nx, my - ny, x, y, 0.0011f, v, m);
	}
}

static uint8_t leaf_mat(float autumn, float h)
{
	if (autumn > 0.5f)
		return h < 0.4f ? US_MAT_LIGHT : (h < 0.75f ? US_MAT_BLOOM : US_MAT_EARTH);
	return US_MAT_LEAF;
}

/* the lone tree, flexing on its spring; tears leaves loose in strong gusts (tear = per-leaf odds this frame) */
static void draw_tree(st_t *s, us_canvas *c, const us_ctx *ctx, float A, float sgn, float gust, float autumn, float winter,
		      float dayv, float tear)
{
	float bx = s->tree_u * A;
	float sc = row_s(TREE_ROW);
	float by = HZ + (1.03f - HZ) * sc;
	float H = 0.36f * (0.9f + 0.2f * ctx->w->growth);
	float t = (float)ctx->t;
	static const float flex[6] = {0.05f, 0.1f, 0.14f, 0.19f, 0.25f, 0.32f};
	float leafk = (1.0f - 0.97f * winter) * (1.0f - 0.5f * autumn);
	for (int i = 0; i < s->nts; i++) {
		tseg_t *g = &s->ts[i];
		float px, py, pa;
		if (g->parent < 0) {
			px = bx;
			py = by;
			pa = -US_PI * 0.5f;
		} else {
			px = s->tex[g->parent];
			py = s->tey[g->parent];
			pa = s->tea[g->parent];
		}
		float fl = g->depth >= 2 ? (us_noise2(i * 0.37f, t * (0.5f + 2.0f * gust)) * (0.015f + 0.07f * gust) +
					    us_noise2(i * 0.91f + 5.0f, t * 16.0f) * 0.1f * s->senv) * g->depth * 0.33f : 0.0f;
		/* crowns compress downwind: branches swing over with the bend */
		float ang = pa + g->rel * (1.0f - 0.25f * us_sat(fabsf(s->tbend))) + sgn * s->tbend * flex[g->depth] + fl;
		float L = g->len * H;
		float ex = px + cosf(ang) * L, ey = py + sinf(ang) * L;
		s->tex[i] = ex;
		s->tey[i] = ey;
		s->tea[i] = ang;
		float wd = 0.0075f * g->w;
		if (wd < 0.0011f)
			wd = 0.0011f;
		float bv = us_sat((ctx->crisp ? 0.45f : 0.36f) * dayv + 0.05f * (5 - g->depth));
		if (c->crisp)
			oline(c, px, py, ex, ey, wd, bv, US_MAT_STEM);
		else
			wline(c, px, py, ex, ey, wd, bv, US_MAT_STEM);
		if (winter > 0.4f && g->depth <= 3 && fabsf(cosf(ang)) > 0.5f) /* snow along the limbs */
			wline(c, px, py - 0.002f, ex, ey - 0.002f, 0.0016f, 0.6f * winter * dayv, US_MAT_WATER);
	}
	float stream = sgn > 0 ? 0.25f : US_PI - 0.25f;
	int torn = 0;
	int nl = (int)(s->nlf * (0.45f + 0.55f * ctx->p->density));
	for (int k = 0; k < nl; k++) {
		leaf_t *l = &s->lf[k];
		if (l->keep > leafk)
			continue;
		tseg_t *g = &s->ts[l->seg];
		float px = g->parent >= 0 ? s->tex[g->parent] : bx, py = g->parent >= 0 ? s->tey[g->parent] : by;
		float x = px + (s->tex[l->seg] - px) * l->off, y = py + (s->tey[l->seg] - py) * l->off;
		float lift = us_sat(0.25f + 1.2f * gust + 0.3f * fabsf(s->tbend));
		float fl = us_noise2(l->ph, t * (0.8f + 3.0f * gust)) + 0.8f * s->senv * us_noise2(l->ph + 3.0f, t * 18.0f);
		/* rotate toward the downwind direction by the shortest way */
		float da = stream - l->ang;
		da -= US_TAU * floorf((da + US_PI) / US_TAU);
		float la = l->ang + da * lift * 0.8f + fl * (0.3f + 0.6f * gust);
		/* gusts flip the leaves: their pale undersides catch the light */
		float sheen = us_sat(0.5f + fl * 0.8f) * lift;
		float v = us_sat((0.42f + 0.45f * sheen) * dayv);
		float len = 0.0125f * l->sz;
		float hk = l->keep * 3.1f;
		uint8_t m = leaf_mat(autumn, hk - floorf(hk));
		if (c->crisp) {
			float ddx = cosf(la) * len, ddy = sinf(la) * len, wv = len * (0.35f - 0.2f * sheen);
			otri(c, x, y, x + ddx * 0.5f - sinf(la) * wv, y + ddy * 0.5f + cosf(la) * wv, x + ddx, y + ddy, v, m);
			otri(c, x, y, x + ddx * 0.5f + sinf(la) * wv, y + ddy * 0.5f - cosf(la) * wv, x + ddx, y + ddy, v, m);
		} else
			leafshape(c, x, y, la, len, len * (0.35f - 0.2f * sheen), v, m);
		if (tear > 0 && s->ntb < MAX_TUMB && us_rand(&s->rng) < tear && torn < (tear > 0.004f ? 5 : 2)) {
			torn++;
			tumb_t *q = &s->tb[s->ntb++];
			q->x = x;
			q->y = y;
			q->vx = sgn * us_randr(&s->rng, 0.0f, 0.2f);
			q->vy = us_randr(&s->rng, -0.1f, 0.03f);
			q->rot = la;
			q->vr = us_randr(&s->rng, -6.0f, 6.0f);
			q->flip = us_rand(&s->rng) * US_TAU;
			q->vf = us_randr(&s->rng, 4.0f, 11.0f);
			q->life = 1.0f;
			q->sz = len;
			q->mat = m;
		}
	}
}


/* A small figure out in the field flies a kite. The melody (mids) lifts it, the air band sets its tail
 * snaking, kicks make it lurch; in silence it sinks low and the tail hangs slack. */
static void draw_kite(st_t *s, us_canvas *c, const us_ctx *ctx, float A, float sgn, float gy, float sc, float dayv, float winter)
{
	float t = (float)ctx->t, dt = ctx->dt;
	float fx = s->kite_u * A, fh = 0.5f * sc;
	float v = us_sat((c->crisp ? 0.55f : 0.62f) * dayv);
	/* the flyer: head, body, legs braced against the pull, an arm up to the line */
	float hx = fx + sgn * fh * 0.25f, hy = gy - fh * 0.78f;
	/* drawn after the grass: the flyer stands waist-deep, so only the upper body is cut out of the blades */
	if (c->crisp) {
		oline(c, fx, gy - fh * 0.3f, fx + sgn * fh * 0.08f, gy - fh * 0.85f, 2.5f / c->s, v, US_MAT_ACCENT);
		oline(c, fx + sgn * fh * 0.08f, gy - fh * 0.85f, hx, hy, 0.5f / c->s, v, US_MAT_ACCENT);
		otri(c, fx + sgn * fh * 0.08f - fh * 0.12f, gy - fh * 0.88f, fx + sgn * fh * 0.08f + fh * 0.12f, gy - fh * 0.88f,
		     fx + sgn * fh * 0.08f, gy - fh * 1.12f, v, US_MAT_ACCENT);
	} else {
		float sx = fx + sgn * fh * 0.06f, sy = gy - fh * 0.75f;
		oline(c, fx, gy - fh * 0.42f, sx, sy, fh * 0.16f, 0.0f, US_MAT_BG);
		oline(c, sx, sy - fh * 0.04f, sx + sgn * fh * 0.02f, sy - fh * 0.2f, fh * 0.2f, 0.0f, US_MAT_BG);
		wline(c, fx - sgn * fh * 0.12f, gy, fx, gy - fh * 0.42f, 0.0013f, v, US_MAT_STEM);
		wline(c, fx + sgn * fh * 0.1f, gy, fx, gy - fh * 0.42f, 0.0013f, v, US_MAT_STEM);
		wline(c, fx, gy - fh * 0.42f, sx, sy, 0.0016f, v, US_MAT_STEM);
		wring(c, sx + sgn * fh * 0.02f, sy - fh * 0.12f, fh * 0.09f, 0.0012f, v, US_MAT_STEM);
		wline(c, sx, sy + fh * 0.04f, hx, hy, 0.0011f, v, US_MAT_STEM);
	}
	/* kite flight: lift eases toward the melody, a spring gives it a lurch on kicks */
	float lt = us_sat(0.18f + 0.6f * s->mid_s + 0.3f * s->gust + 0.1f * ctx->w->wind) * (1.0f - 0.5f * winter);
	s->klift = us_approach(s->klift, lt, lt > s->klift ? 1.2f : 3.0f, dt);
	float tilt0 = sgn * (0.35f + 0.25f * s->gust) + (0.12f + 0.35f * s->gust) * us_noise2(7.7f, t * (0.6f + 1.6f * s->gust));
	s->kvel += ((tilt0 - s->ktilt) * 18.0f - s->kvel * 2.6f) * dt + sgn * s->kpulse * 6.0f * dt;
	s->ktilt += s->kvel * dt;
	float kh = 0.06f + 0.3f * s->klift;
	float kx = hx + sgn * (0.07f + 0.12f * s->gust + 0.08f * (1.0f - s->klift)) + 0.015f * us_noise2(3.3f, t * 0.5f);
	float ky = hy - kh + 0.012f * us_noise2(5.1f, t * (0.7f + s->gust));
	if (kx < 0.03f) kx = 0.03f;
	if (kx > A - 0.03f) kx = A - 0.03f;
	float ks = (0.016f + 0.006f * s->klift) * (c->crisp ? 1.6f : 1.0f);
	float ca = cosf(s->ktilt), sa = sinf(s->ktilt);
	/* diamond in kite space: top, right, bottom, left (long lower spine) */
	static const float kp[8] = {0, -1.0f, 0.62f, -0.25f, 0, 1.45f, -0.62f, -0.25f};
	float P[8];
	for (int i = 0; i < 4; i++) {
		float qx = kp[2 * i] * ks, qy = kp[2 * i + 1] * ks;
		P[2 * i] = kx + qx * ca - qy * sa;
		P[2 * i + 1] = ky + qx * sa + qy * ca;
	}
	float kv = us_sat(0.7f * dayv + 0.1f);
	/* the line sags more when the kite is low and the wind slack */
	float sag = 0.01f + 0.05f * (1.0f - s->klift);
	float px = hx, py = hy;
	for (int i = 1; i <= 14; i++) {
		float f = i / 14.0f;
		float x = us_lerp(hx, P[4], f), y = us_lerp(hy, P[5], f) + sag * 4.0f * f * (1.0f - f);
		if (c->crisp)
			us_line(c, px, py, x, y, 0.001f, 0.35f * dayv, US_MAT_STEM);
		else
			wline(c, px, py, x, y, 0.0008f, 0.28f * dayv, US_MAT_STEM);
		px = x;
		py = y;
	}
	if (c->crisp) {
		us_tri(c, P[0], P[1], P[2], P[3], P[4], P[5], kv, US_MAT_BLOOM);
		us_tri(c, P[0], P[1], P[4], P[5], P[6], P[7], kv * 0.75f, US_MAT_BLOOM);
	} else {
		for (int i = 0; i < 4; i++)
			wline(c, P[2 * i], P[2 * i + 1], P[(2 * i + 2) & 7], P[(2 * i + 3) & 7], 0.0014f, kv, US_MAT_BLOOM);
		wline(c, P[0], P[1], P[4], P[5], 0.001f, kv * 0.6f, US_MAT_BLOOM);
		wline(c, P[2], P[3], P[6], P[7], 0.001f, kv * 0.6f, US_MAT_BLOOM);
		/* panel hatching on the lee half */
		for (int k = 1; k < 4; k++) {
			float f = k / 4.0f;
			wline(c, us_lerp(P[0], P[2], f), us_lerp(P[1], P[3], f), us_lerp(P[4], P[2], f), us_lerp(P[5], P[3], f), 0.0008f,
			      kv * 0.3f, US_MAT_BLOOM);
		}
	}
	/* the tail: each knot lags the one before it, a travelling wave whose speed/size follow the air band */
	float seg = ks * 0.42f;
	float dwx = sgn * (0.3f + 0.6f * s->klift), dwy = 1.0f - 0.6f * s->klift;
	float dn = sqrtf(dwx * dwx + dwy * dwy);
	dwx /= dn;
	dwy /= dn;
	float amp = ks * (0.15f + 0.9f * s->air_s), wf = 3.0f + 9.0f * s->air_s;
	if (s->tail[0] == 0 && s->tail[1] == 0)
		for (int k = 0; k <= NTAIL; k++) {
			s->tail[2 * k] = P[4] + dwx * seg * k;
			s->tail[2 * k + 1] = P[5] + dwy * seg * k;
		}
	s->tail[0] = P[4];
	s->tail[1] = P[5];
	for (int k = 1; k <= NTAIL; k++) {
		float f = (float)k / NTAIL;
		float wv = amp * f * sinf(k * 0.9f - t * wf);
		float tx = P[4] + dwx * seg * k - dwy * wv, ty = P[5] + dwy * seg * k + dwx * wv;
		s->tail[2 * k] = us_approach(s->tail[2 * k], tx, 0.03f + 0.012f * k, dt);
		s->tail[2 * k + 1] = us_approach(s->tail[2 * k + 1], ty, 0.03f + 0.012f * k, dt);
		float ax = s->tail[2 * k - 2], ay = s->tail[2 * k - 1], bx = s->tail[2 * k], by = s->tail[2 * k + 1];
		float tv = kv * (1.0f - 0.5f * f);
		if (c->crisp)
			us_line(c, ax, ay, bx, by, 0.001f, tv, US_MAT_ACCENT);
		else {
			wline(c, ax, ay, bx, by, 0.0009f, tv, US_MAT_ACCENT);
			if (k % 3 == 0) { /* paper bows tied along it */
				float nx = -(by - ay), ny = bx - ax;
				wline(c, bx - nx * 0.45f, by - ny * 0.45f, bx + nx * 0.45f, by + ny * 0.45f, 0.001f, tv * 0.8f, US_MAT_BLOOM);
			}
		}
	}
}

/* the trodden path's centre x at depth sc (only nearer than the tree); *hw = its half width */
static float path_at(const st_t *s, float A, float sc, float *hw)
{
	float st = row_s(TREE_ROW);
	float f = us_sat((sc - st) / (1.0f - st));
	float side = s->path_x0 > s->tree_u ? 1.0f : -1.0f;
	float xt = s->tree_u * A + side * 0.035f, xb = s->path_x0 * A;
	float xc = us_lerp(xt, xb, powf(f, 0.75f)) - side * 0.05f * sinf(f * US_PI) * (0.4f + f);
	*hw = 0.006f + 0.12f * sc;
	return xc;
}

/* reservoir of grass tips (seed heads, flowers) this frame, for snare flurries and hat glints */
static void tip_add(st_t *s, float x, float y, float sc)
{
	int i = s->tipseen++;
	if (i >= MAX_TIPS) {
		i = (int)(us_rng_u32(&s->rng) % (uint32_t)s->tipseen);
		if (i >= MAX_TIPS)
			return;
	} else
		s->ntip = i + 1;
	s->tipx[i] = x;
	s->tipy[i] = y;
	s->tips[i] = sc;
}

/* tall oat stalks in the very foreground: slow, heavy swaying; kicks push them over, snares rattle the
 * spikelets. They frame the view and give the field a near layer. */
static void draw_fg(st_t *s, us_canvas *c, const us_ctx *ctx, float A, float sgn, float gstr, float ax, float az,
		    float height, float dayv, float lightk, float winter, float spring)
{
	float dt = ctx->dt, t = (float)ctx->t;
	int n = 3 + (int)((NFG - 3) * ctx->p->density + 0.5f);
	if (n > NFG)
		n = NFG;
	float H0 = (0.75f + 0.35f * ctx->w->growth) * (1.0f - 0.45f * winter);
	uint8_t sm = spring > 0.5f ? US_MAT_LEAF : US_MAT_STEM;
	for (int i = 0; i < n; i++) {
		float u = s->fgx[i];
		float x = u * A, y = 1.03f;
		float X = (x - A * 0.5f);
		float bend = wind_at(s, X * 0.25f, 0.25f, ax, az);
		float fb = front_at(s, u, 1.0f);
		float target = (0.22f + 0.1f * fmodf(s->fgph[i], 1.0f) + gstr * bend + 0.03f * us_noise2(s->fgph[i], t * 0.4f) + 0.3f * s->kimp) * sgn + 0.75f * fb;
		s->fgv[i] += ((target - s->fgb[i]) * 16.0f - s->fgv[i] * 2.8f) * dt;
		s->fgb[i] = us_clampf(s->fgb[i] + s->fgv[i] * dt, -1.2f, 1.2f);
		float bb = s->fgb[i];
		float Hh = s->fgh[i] * H0 * (0.85f + 0.15f * height);
		float v = us_sat((c->crisp ? 0.5f : 0.68f) * dayv * lightk);
		enum { NS = 12 };
		float px = x, py = y;
		float wd = c->crisp ? 0.006f : 0.0024f;
		for (int k = 1; k <= NS; k++) {
			float f = (float)k / NS;
			float qx = x + bb * Hh * 0.55f * f * f, qy = y - Hh * f * (1.0f - 0.18f * fabsf(bb) * f);
			if (c->crisp)
				us_line(c, px, py, qx, qy, wd, v, sm);
			else
				wline(c, px, py, qx, qy, wd * (1.0f - 0.4f * f), v, sm);
			/* oat spikelets dangling from the upper third on fine hair-stalks */
			if (k >= NS - 5 && winter < 0.6f) {
				for (int side = -1; side <= 1; side += 2) {
					float ph = s->fgph[i] + k * 1.7f + side;
					float sw = 0.2f * us_noise2(ph, t * 0.7f) + 1.4f * s->senv * us_noise2(ph + 9.0f, t * 17.0f);
					/* hair-stalks splay out and the spikelets hang: wider near the top of the panicle */
					float ang = US_PI * 0.5f - side * (1.1f - 0.12f * (k - NS + 5)) - bb * 0.7f + sw;
					float ln = 0.016f + 0.007f * (NS - k);
					float ex = qx + cosf(ang) * ln, ey = qy + sinf(ang) * ln * 0.8f;
					if (c->crisp) {
						us_line(c, qx, qy, ex, ey, 0.002f, v * 0.8f, sm);
						us_disc(c, ex, ey, 0.005f, us_sat(v * 1.3f), US_MAT_ACCENT);
					} else {
						wline(c, qx, qy, ex, ey, 0.0009f, v * 0.7f, sm);
						leafshape(c, ex, ey, ang + 0.15f, 0.02f, 0.005f, us_sat(v * 1.25f), US_MAT_ACCENT);
					}
					if (side > 0)
						tip_add(s, ex, ey, 1.0f);
				}
			}
			px = qx;
			py = qy;
		}
	}
}

/* snare -> a flurry torn from the tips; hats -> glints off them; a hard snare flushes birds from the grass */
static void spawn_drums(st_t *s, const us_ctx *ctx, int snapped, float sgn)
{
	const us_audio *a = ctx->a;
	us_rng *r = &s->rng;
	float dens = ctx->p->density;
	if (snapped && s->ntip > 0) {
		int n = (int)((40.0f + 60.0f * us_sat(a->snare)) * (0.5f + dens));
		us_world_add(ctx->w, &ctx->w->pollen, 0.0002f * n, 1.0f);
		for (int k = 0; k < n && s->nfk < MAX_FLAKE; k++) {
			int j = (int)(us_rng_u32(r) % (uint32_t)s->ntip);
			float tsc = s->tips[j];
			int petal = tsc >= 2.0f;
			if (petal)
				tsc -= 2.0f;
			flake_t *q = &s->fk[s->nfk++];
			q->x = q->px = s->tipx[j];
			q->y = q->py = s->tipy[j];
			q->vx = sgn * us_randr(r, 0.2f, 0.65f) * (0.4f + tsc) + us_randn(r) * 0.06f;
			q->vy = -us_randr(r, 0.08f, 0.4f) * (0.35f + tsc);
			q->life = us_randr(r, 0.8f, 1.0f);
			q->sz = 0.0016f + 0.0034f * tsc;
			q->mat = petal ? US_MAT_BLOOM : US_MAT_ACCENT;
		}
		if (ctx->t - s->last_flush_t > 1.8f && us_sat(a->snare) > 0.3f) {
			/* a hard snare startles a little flock up out of the mid field */
			s->last_flush_t = (float)ctx->t;
			int j = (int)(us_rng_u32(r) % (uint32_t)s->ntip);
			float tsc = fmodf(s->tips[j], 2.0f);
			float cx = s->tipx[j], cy = s->tipy[j];
			int nb = 4 + (int)(us_rand(r) * 5);
			for (int i = 0, made = 0; i < MAX_FLUSH && made < nb; i++) {
				flush_t *b = &s->fb[i];
				if (b->life > 0)
					continue;
				made++;
				b->x = cx + us_randn(r) * 0.02f;
				b->y = cy + us_randn(r) * 0.01f;
				b->vx = sgn * us_randr(r, 0.05f, 0.25f) + us_randn(r) * 0.12f;
				b->vy = -us_randr(r, 0.3f, 0.55f);
				b->ph = us_rand(r) * US_TAU;
				b->life = us_randr(r, 2.5f, 3.5f);
				b->sz = 0.007f + 0.01f * us_clampf(tsc, 0.2f, 1.0f);
			}
		}
	}
	if (a->hat_hit && s->ntip > 0) {
		int n = (int)((16.0f + 30.0f * us_sat(a->hat)) * (0.5f + dens));
		for (int k = 0; k < n && s->ngl < MAX_GLINT; k++) {
			int j = (int)(us_rng_u32(r) % (uint32_t)s->ntip);
			glint_t *g = &s->gl[s->ngl++];
			g->x = s->tipx[j];
			g->y = s->tipy[j];
			g->life = 1.0f;
			g->sz = 0.006f + 0.011f * fminf(fmodf(s->tips[j], 2.0f), 1.0f);
		}
	}
}

static void draw_drums(st_t *s, us_canvas *c, const us_ctx *ctx, float sgn, float dayv)
{
	float dt = ctx->dt, rdt = ctx->real_dt, t = (float)ctx->t;
	/* torn petals and seed fluff: a fast streak at first, then drifting down the wind */
	for (int i = 0; i < s->nfk;) {
		flake_t *q = &s->fk[i];
		q->px = q->x;
		q->py = q->y;
		float flt = us_noise2(q->sz * 900.0f + i * 0.1f, t * 2.0f);
		q->vx += (sgn * (0.06f + 0.18f * s->breeze) - q->vx) * dt * 2.2f;
		q->vy += (0.035f + 0.05f * flt - q->vy) * dt * 1.8f;
		q->x += q->vx * dt;
		q->y += q->vy * dt;
		q->life -= rdt * 1.25f;
		if (q->life <= 0 || q->x < -0.05f || q->x > ctx->aspect + 0.05f || q->y > 1.03f || q->y < -0.02f) {
			*q = s->fk[--s->nfk];
			continue;
		}
		float v = us_sat(q->life * 1.3f) * 0.9f * dayv;
		float sx = q->x - q->vx * 0.09f, sy = q->y - q->vy * 0.09f; /* motion streak, long while it is fast */
		if (c->crisp) {
			us_line(c, sx, sy, q->x, q->y, 0.001f, v * 0.7f, q->mat);
			us_point(c, q->x, q->y, fmaxf(q->sz * 1.5f, 0.006f), v, q->mat);
		} else {
			wline(c, sx, sy, q->x, q->y, 0.0011f, v * 0.75f, q->mat);
			us_point(c, q->x, q->y, q->sz, v, q->mat);
		}
		i++;
	}
	/* glints: four-ray sparkles that wink out in a breath */
	for (int i = 0; i < s->ngl;) {
		glint_t *g = &s->gl[i];
		g->life -= rdt / 0.16f;
		if (g->life <= 0) {
			*g = s->gl[--s->ngl];
			continue;
		}
		float l = g->life * g->life;
		float v = us_sat(0.25f + 0.75f * l) * (0.75f + 0.25f * dayv);
		float L = g->sz * (0.4f + 0.6f * l);
		if (c->crisp) {
			int px = (int)(g->x * c->s), py = (int)(g->y * c->s);
			us_put(c, px, py, v, US_MAT_LIGHT);
			if (g->life > 0.45f) {
				us_put(c, px - 1, py, v * 0.7f, US_MAT_LIGHT);
				us_put(c, px + 1, py, v * 0.7f, US_MAT_LIGHT);
				us_put(c, px, py - 1, v * 0.7f, US_MAT_LIGHT);
				us_put(c, px, py + 1, v * 0.7f, US_MAT_LIGHT);
			}
		} else {
			wline(c, g->x - L, g->y, g->x + L, g->y, 0.001f, v, US_MAT_LIGHT);
			wline(c, g->x, g->y - L, g->x, g->y + L, 0.001f, v, US_MAT_LIGHT);
			us_point(c, g->x, g->y, 0.0016f, v, US_MAT_LIGHT);
		}
		i++;
	}
	/* startled birds climbing out of the grass, wings beating hard */
	for (int i = 0; i < MAX_FLUSH; i++) {
		flush_t *b = &s->fb[i];
		if (b->life <= 0)
			continue;
		b->life -= dt;
		b->vx += (sgn * 0.22f - b->vx) * dt * 0.6f;
		b->vy += (-0.12f - b->vy) * dt * 0.7f;
		b->x += b->vx * dt;
		b->y += b->vy * dt;
		b->ph += dt * 24.0f;
		if (b->y < -0.05f || b->x < -0.1f || b->x > ctx->aspect + 0.1f)
			b->life = 0;
		if (b->life <= 0)
			continue;
		float fl = sinf(b->ph), ws = b->sz;
		float v = 0.8f * us_sat(b->life) * dayv;
		float tip = -fl * ws * 0.7f;
		if (c->crisp) {
			us_line(c, b->x - ws, b->y + tip, b->x, b->y, 0.002f, v, US_MAT_ACCENT);
			us_line(c, b->x, b->y, b->x + ws, b->y + tip, 0.002f, v, US_MAT_ACCENT);
		} else {
			float ey = b->y + tip * 0.4f - ws * 0.1f;
			wline(c, b->x, b->y, b->x - ws * 0.45f, ey, 0.0013f, v, US_MAT_ACCENT);
			wline(c, b->x - ws * 0.45f, ey, b->x - ws, b->y + tip, 0.001f, v, US_MAT_ACCENT);
			wline(c, b->x, b->y, b->x + ws * 0.45f, ey, 0.0013f, v, US_MAT_ACCENT);
			wline(c, b->x + ws * 0.45f, ey, b->x + ws, b->y + tip, 0.001f, v, US_MAT_ACCENT);
		}
	}
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *s = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, rdt = ctx->real_dt, A = ctx->aspect;
	float dens = ctx->p->density;
	us_rng *r = &s->rng;
	float spring = us_world_season_w(w, 0), summer = us_world_season_w(w, 1);
	float autumn = us_world_season_w(w, 2), winter = us_world_season_w(w, 3);
	float day = w->daylight;
	s->t = ctx->t;

	/* ---- audio followers (fast attack, eased release so nothing strobes) ---- */
	s->sub_s = us_approach(s->sub_s, a->sub, a->sub > s->sub_s ? 0.25f : 1.2f, rdt);
	s->bass_s = us_approach(s->bass_s, a->bass, a->bass > s->bass_s ? 0.15f : 0.6f, rdt);
	s->lowmid_s = us_approach(s->lowmid_s, a->lowmid, 0.3f, rdt);
	s->mid_s = us_approach(s->mid_s, a->mid, a->mid > s->mid_s ? 0.4f : 1.4f, rdt);
	s->air_s = us_approach(s->air_s, a->air, a->air > s->air_s ? 0.05f : 0.3f, rdt);
	s->swell_s = us_approach(s->swell_s, a->swell, 2.0f, rdt);
	s->width_s = us_approach(s->width_s, a->width, 1.0f, rdt);
	s->lvl_s = us_approach(s->lvl_s, a->level, a->level > s->lvl_s ? 0.35f : 1.1f, rdt);
	s->sub_ph += dt * (0.3f + 0.6f * s->sub_s);
	s->kpulse = us_approach(s->kpulse, 0.0f, 0.35f, rdt);

	/* ---- wind state: loudness is the steady wind; quiet is still, heavy air ---- */
	float tb = us_sat(0.08f + 0.15f * w->wind + 0.55f * s->lvl_s + 0.2f * s->swell_s);
	s->breeze = us_approach(s->breeze, tb, tb > s->breeze ? 1.2f : 2.5f, rdt);
	s->gust = s->breeze; /* the kite, birds, clouds and leaves all ride the steady wind */
	/* louder passages bring the light out over the field; quiet ones sink into shade */
	float tl = us_sat(0.12f + 0.95f * a->level + 0.2f * s->swell_s);
	s->light = us_approach(s->light, tl, tl > s->light ? 0.25f : 0.6f, rdt);
	float tdir = a->active > 0.2f ? (a->centroid - 0.5f) * 1.6f : 0.35f * us_noise2((float)ctx->t * 0.02f, 1.3f);
	s->dir = us_approach(s->dir, us_clampf(tdir, -0.9f, 0.9f), 6.0f, rdt);
	s->swell = us_approach(s->swell, us_sat(a->bass * 0.7f + a->sub * 0.4f), 1.2f, rdt);
	float ax = cosf(s->dir), az = sinf(s->dir); /* ax > 0: blowing left -> right */
	float gstr = 0.07f + 0.3f * s->breeze + 0.1f * winter;
	s->flow += dt * (0.08f + 0.3f * s->breeze);
	s->cloud_t += dt * (0.015f + 0.05f * s->breeze);
	float sgn = ax >= 0 ? 1.0f : -1.0f;

	/* kick -> a gust front rolls across the field (a counter-gust from the other side when the mix is wide) */
	int kicked = 0;
	if (a->kick_hit && ctx->t - s->last_kick_t > 0.12f) {
		kicked = 1;
		s->last_kick_t = (float)ctx->t;
		s->kpulse = us_sat(s->kpulse + 0.5f + 0.5f * a->kick);
	} else if (a->beat && a->onset > 0.6f && ctx->t - s->last_kick_t > 1.6f && ctx->t - s->last_beat_t > 1.6f) {
		kicked = 2; /* music without drums still sends the odd soft front on big swells/attacks */
		s->last_beat_t = (float)ctx->t;
	}
	s->kick_prev = a->kick;
	s->kimp = us_approach(s->kimp, 0.0f, 0.22f, rdt);
	if (kicked) {
		float ks = kicked == 1 ? 0.65f + 0.35f * us_sat(a->kick) : 0.4f;
		if (ks > s->kimp)
			s->kimp = ks;
		float sg = 1.0f;
		s->counter += s->width_s * 0.6f;
		if (s->counter >= 1.0f) {
			s->counter -= 1.0f;
			sg = -1.0f;
		}
		int slot = -1;
		float weakest = 9;
		for (int i = 0; i < MAX_FRONTS; i++) {
			if (s->fr[i].amp <= 0.002f) {
				slot = i;
				break;
			}
			if (s->fr[i].amp < weakest) {
				weakest = s->fr[i].amp;
				slot = i;
			}
		}
		front_t *f = &s->fr[slot];
		f->amp = (sg > 0 ? 1.0f : 0.6f) * ks * (0.75f + 0.25f * s->breeze);
		f->sgn = sg * sgn;
		f->pos = us_randr(r, -0.05f, 0.1f);
		f->speed = us_randr(r, 3.6f, 4.4f);
		f->width = us_randr(r, 0.09f, 0.13f);
	}
	for (int i = 0; i < MAX_FRONTS; i++) {
		front_t *f = &s->fr[i];
		if (f->amp <= 0.002f)
			continue;
		f->pos += f->speed * rdt;
		f->amp *= expf(-rdt * 4.0f);
		if (f->pos > 1.6f)
			f->amp = 0;
	}
	/* snare -> a sharp flurry: stalks shiver, seeds and petals are torn off, leaves snap, birds startle */
	int snapped = 0;
	s->senv = us_approach(s->senv, 0.0f, 0.1f, rdt);
	if (a->snare_hit && ctx->t - s->last_onset_t > 0.08f) {
		snapped = 1;
		s->last_onset_t = (float)ctx->t;
		s->senv = fmaxf(s->senv, 0.7f + 0.3f * us_sat(a->snare));
	}
	/* hats -> light flashes off the grass tips */
	s->henv = us_approach(s->henv, 0.0f, 0.06f, rdt);
	if (a->hat_hit)
		s->henv = fmaxf(s->henv, 0.6f + 0.4f * us_sat(a->hat));

	float dayv = 0.8f + 0.2f * us_smooth(0.0f, 0.6f, day);
	float lightk = 0.45f + 0.75f * s->light; /* how lit the field is (loudness) */

	/* ---- the tree's spring: pushed by the local wind (fronts included), overshoots and rebounds ---- */
	float tear = 0;
	{
		float sc = row_s(TREE_ROW), Z = 1.0f / sc;
		float X = (s->tree_u * A - A * 0.5f) * Z;
		float bend = wind_at(s, X * 0.25f, Z * 0.25f, ax, az);
		float fb = front_at(s, s->tree_u, sc) * sgn;
		float target = (gstr * bend + 0.35f * fb + 0.12f * s->kimp) * 2.2f;
		s->tvel += ((target - s->tbend) * 28.0f - s->tvel * 3.6f) * dt;
		s->tbend += s->tvel * dt;
		s->tbend = us_clampf(s->tbend, -2.0f, 2.0f);
		float force = gstr * bend + 0.4f * fabsf(fb);
		if (force > 0.3f)
			tear = dt * (force - 0.3f) * 0.12f * (0.3f + 2.5f * autumn) * (1.0f - winter);
		if (snapped) /* a sharp hit snaps a few leaves off at once */
			tear += (0.008f + 0.01f * a->snare) * (0.4f + autumn) * (1.0f - winter);
	}

	/* ---- sky: sun / moon, cumulus ---- */
	{
		/* stars by night (stable hash positions, faint twinkle) */
		float sv = us_sat(1.0f - day * 2.0f);
		if (sv > 0.01f)
			for (int i = 0; i < 90; i++) {
				uint32_t hsh = (uint32_t)i * 2654435761u;
				float sx = ((hsh >> 8) & 1023) / 1023.0f * A;
				float sy = ((hsh >> 18) & 1023) / 1023.0f * (HZ - 0.06f);
				float tw = 0.6f + 0.4f * us_noise2(i * 1.37f, (float)ctx->t * 1.3f);
				us_point(c, sx, sy, 0.0011f, 0.45f * sv * tw * (0.3f + 0.7f * ((hsh >> 3) & 7) / 7.0f), US_MAT_LIGHT);
			}
		/* sun by day, moon (half a day out of phase) by night */
		float ph = w->day_phase;
		float cx = A * (0.1f + 0.8f * ph);
		float cy = HZ - 0.02f + cosf(ph * US_TAU) * 0.36f;
		float mph = fmodf(ph + 0.5f, 1.0f);
		float mx = A * (0.1f + 0.8f * mph), my = HZ - 0.02f + cosf(mph * US_TAU) * 0.36f;
		float halo = 0.9f + 0.35f * s->light;
		if (cy < HZ - 0.03f) {
			if (ctx->crisp) {
				us_disc(c, cx, cy, 0.05f * halo, 0.14f, US_MAT_LIGHT);
				us_disc(c, cx, cy, 0.03f, 0.75f, US_MAT_LIGHT);
			} else {
				us_disc(c, cx, cy, 0.03f, 0.18f, US_MAT_LIGHT);
				wring(c, cx, cy, 0.03f, 0.0016f, 0.85f, US_MAT_LIGHT);
				wring(c, cx, cy, 0.045f * halo, 0.0012f, 0.1f + 0.2f * s->light, US_MAT_LIGHT);
				wring(c, cx, cy, 0.068f * halo, 0.0011f, 0.04f + 0.12f * s->light, US_MAT_LIGHT);
				wring(c, cx, cy, 0.1f * halo, 0.0011f, 0.08f * s->light, US_MAT_LIGHT);
			}
		}
		if (my < HZ - 0.03f && day < 0.5f) {
			float nv = us_sat((0.5f - day) * 3.0f);
			if (ctx->crisp) {
				us_disc(c, mx, my, 0.042f * halo, 0.12f * nv, US_MAT_LIGHT);
				us_disc(c, mx, my, 0.022f, 0.8f * nv, US_MAT_LIGHT);
				us_disc(c, mx + 0.009f, my - 0.004f, 0.017f, 0.0f, US_MAT_BG); /* (no-op in MAX blend) */
			} else {
				/* a waxing crescent: lit limb, faint earthshine on the dark side, a breathing halo */
				us_arc(c, mx, my, 0.022f, -1.9f, 1.9f, 0.0018f, 0.85f * nv, US_MAT_LIGHT);
				us_arc(c, mx + 0.008f, my, 0.0195f, -1.62f, 1.62f, 0.0013f, 0.5f * nv, US_MAT_LIGHT);
				us_arc(c, mx, my, 0.022f, 1.9f, US_TAU - 1.9f, 0.0011f, 0.16f * nv, US_MAT_LIGHT);
				wring(c, mx, my, 0.036f * halo, 0.0012f, 0.16f * nv, US_MAT_LIGHT);
				wring(c, mx, my, 0.058f * halo, 0.0011f, 0.08f * nv, US_MAT_LIGHT);
				wring(c, mx, my, 0.088f * halo, 0.001f, 0.04f * nv, US_MAT_LIGHT);
			}
		}
		/* cumulus streaming downwind, sheared forward at their tops by the wind */
		for (int k = 0; k < MAX_CLOUD; k++) {
			cloud_t *cl = &s->cl[k];
			cl->x += dt * (0.004f + 0.035f * s->gust) * cl->sp * sgn * (0.4f + 0.6f * fabsf(ax)) / A * 1.8f;
			if (cl->x > 1.25f)
				cl->x = -0.25f - cl->w / A;
			if (cl->x < -0.25f - cl->w / A)
				cl->x = 1.25f;
			float x0 = cl->x * A, cb = cl->y + cl->h;
			enum { NC = 24 };
			float top[2 * (NC + 1)], bot[2 * (NC + 1)];
			float shear = sgn * (0.05f + 0.25f * s->gust) * cl->h;
			for (int i = 0; i <= NC; i++) {
				float u = (float)i / NC;
				float env = powf(sinf(US_PI * u), 0.55f);
				/* puffy cumulus top: the upper envelope of a few round turrets that swell and shrink */
				float hh = 0;
				for (int q = 0; q < 6; q++) {
					float uc = (q + 0.5f) / 6.0f + 0.05f * us_noise2(cl->seed + q * 1.3f, (float)ctx->t * 0.03f);
					float rq = (0.12f + 0.1f * sinf(US_PI * uc)) *
						   (0.75f + 0.35f * us_noise2(cl->seed * 1.7f + q * 2.1f, (float)ctx->t * 0.04f));
					float du = u - uc;
					float e = rq * rq - du * du;
					if (e > 0) {
						float hq = sqrtf(e) / 0.22f * (0.55f + 0.6f * sinf(US_PI * uc));
						if (hq > hh)
							hh = hq;
					}
				}
				hh = cl->h * us_clampf(hh, 0.0f, 1.2f) * (0.3f + 0.7f * env);
				top[2 * i] = x0 + u * cl->w + shear * (hh / cl->h);
				top[2 * i + 1] = cb - hh;
				bot[2 * i] = x0 + u * cl->w;
				bot[2 * i + 1] = cb + 0.004f * us_noise2(cl->seed + u * 3.0f, 9.0f) * env;
			}
			float cvv = (0.2f + 0.16f * dayv) * (1.0f - 0.25f * k / MAX_CLOUD) * (0.7f + 0.5f * s->light);
			if (ctx->crisp) {
				uint8_t cm = day > 0.3f ? US_MAT_WATER : US_MAT_BG;
				float fv = day > 0.3f ? 0.62f + 0.3f * day : 0.42f;
				/* column spans (cheaper than triangles at pixel resolution); the crown row is brighter */
				int pa = (int)floorf(x0 * c->s), pb = (int)ceilf((x0 + cl->w) * c->s);
				if (pa < 0) pa = 0;
				if (pb > c->w - 1) pb = c->w - 1;
				for (int px = pa; px <= pb; px++) {
					float uu = ((px + 0.5f) / c->s - x0) / cl->w * NC;
					int i = (int)uu;
					if (i < 0 || i >= NC)
						continue;
					float f = uu - i;
					int ty = (int)(us_lerp(top[2 * i + 1], top[2 * i + 3], f) * c->s);
					int by = (int)(us_lerp(bot[2 * i + 1], bot[2 * i + 3], f) * c->s);
					for (int py = ty; py <= by; py++)
						us_put(c, px, py, py <= ty + 1 ? fv : fv * 0.85f, cm);
				}
			} else {
				/* body: overwrite to near-black so stars do not shine through */
				int pa = (int)floorf(x0 * c->s), pb = (int)ceilf((x0 + cl->w) * c->s);
				if (pa < 0) pa = 0;
				if (pb > c->w - 1) pb = c->w - 1;
				for (int px = pa; px <= pb; px++) {
					float uu = ((px + 0.5f) / c->s - x0) / cl->w * NC;
					int i = (int)uu;
					if (i < 0 || i >= NC)
						continue;
					float f = uu - i;
					int ty = (int)ceilf(us_lerp(top[2 * i + 1], top[2 * i + 3], f) * c->s) + 1;
					int by = (int)(us_lerp(bot[2 * i + 1], bot[2 * i + 3], f) * c->s);
					if (ty < 0) ty = 0;
					if (by > c->h - 1) by = c->h - 1;
					for (int py = ty; py <= by; py++) {
						size_t ii = (size_t)py * c->w + px;
						c->ink[ii] = 3;
						c->mat[ii] = US_MAT_WATER;
					}
				}
				float lxd = (day > 0.3f ? cx : mx) - (x0 + cl->w * 0.5f);
				for (int i = 0; i < NC; i++) {
					/* the rim facing the sun / moon is lit */
					float dx = top[2 * i + 2] - top[2 * i], dy = top[2 * i + 3] - top[2 * i + 1];
					float nx = dy, ny = -dx, nl = sqrtf(nx * nx + ny * ny) + 1e-6f;
					float lit = us_sat((nx / nl) * (lxd > 0 ? 0.7f : -0.7f) + (-ny / nl) * -0.7f);
					wline(c, top[2 * i], top[2 * i + 1], top[2 * i + 2], top[2 * i + 3], 0.0013f, cvv * (1.5f + 1.2f * lit), US_MAT_WATER);
					if (i > 2 && i < NC - 3)
						wline(c, bot[2 * i], bot[2 * i + 1], bot[2 * i + 2], bot[2 * i + 3], 0.001f, cvv * 0.6f, US_MAT_WATER);
				}
				/* inner billows */
				for (int i = 3; i < NC - 3; i++) {
					float mx0 = us_lerp(top[2 * i], bot[2 * i], 0.45f), my0 = us_lerp(top[2 * i + 1], bot[2 * i + 1], 0.45f);
					float mx1 = us_lerp(top[2 * i + 2], bot[2 * i + 2], 0.45f), my1 = us_lerp(top[2 * i + 3], bot[2 * i + 3], 0.45f);
					if (us_noise2(cl->seed + i * 0.7f, 3.0f) > 0.0f)
						wline(c, mx0, my0, mx1, my1, 0.001f, cvv * 0.7f, US_MAT_WATER);
				}
				/* torn wisps trailing downwind when it blows hard */
				if (s->gust > 0.35f)
					for (int q = 0; q < 2; q++) {
						float yy = cb - cl->h * (0.25f + 0.3f * q);
						float xe = sgn > 0 ? top[2 * NC] : top[0];
						float ln = cl->w * (0.2f + 0.5f * (s->gust - 0.35f));
						wline(c, xe, yy, xe + sgn * ln, yy - 0.004f, 0.001f, cvv * 0.9f * us_sat((s->gust - 0.35f) * 4.0f),
						      US_MAT_WATER);
					}
			}
		}
		/* a far, hazy range behind the horizon (aerial perspective: faint, cool) */
		{
			float fp[2 * 97];
			for (int i = 0; i <= 96; i++) {
				float u = i / 96.0f;
				float rr = 1.0f - fabsf(us_noise2(u * 3.4f + 7.0f, 1.9f));
				fp[2 * i] = u * A;
				fp[2 * i + 1] = HZ - 0.03f - 0.075f * rr * rr - 0.02f * us_noise2(u * 1.4f, 5.0f) - 0.004f * us_noise2(u * 21.0f, 3.0f);
			}
			if (!ctx->crisp) { /* hide stars behind it */
				for (int px = 0; px < c->w; px++) {
					float uu = (px + 0.5f) / c->s / A * 96.0f;
					int i = (int)uu;
					if (i > 95) i = 95;
					float yy = us_lerp(fp[2 * i + 1], fp[2 * i + 3], uu - i);
					for (int py = (int)ceilf(yy * c->s); py < (int)(HZ * c->s); py++) {
						size_t ii = (size_t)py * c->w + px;
						c->ink[ii] = 2;
						c->mat[ii] = US_MAT_WATER;
					}
				}
				for (int i = 0; i < 96; i++)
					wline(c, fp[2 * i], fp[2 * i + 1], fp[2 * i + 2], fp[2 * i + 3], 0.0011f, (0.2f + 0.06f * day) * dayv, US_MAT_WATER);
				/* engraver's hatching down the shaded flanks, slanting with the slope */
				for (int i = 1; i < 95; i += 2) {
					float sl = fp[2 * i + 3] - fp[2 * i - 1];
					float shade = sl * (sgn > 0 ? 1.0f : -1.0f); /* the lee side, away from the low sun */
					if (shade < 0.0005f)
						continue;
					float hl = us_clampf(shade * 9.0f, 0.004f, 0.022f) * (0.6f + 0.4f * us_noise2(i * 0.9f, 4.0f));
					float x0 = fp[2 * i], y0 = fp[2 * i + 1] + 0.003f;
					if (y0 + hl > HZ - 0.004f)
						hl = HZ - 0.004f - y0;
					if (hl > 0.002f)
						wline(c, x0, y0, x0 - sl * 0.6f, y0 + hl, 0.001f, 0.15f * dayv, US_MAT_WATER);
				}
			} else {
				for (int i = 0; i < 96; i++) {
					float x0 = fp[2 * i], y0 = fp[2 * i + 1], x1 = fp[2 * i + 2], y1 = fp[2 * i + 3];
					us_tri(c, x0, y0, x1, y1, x0, HZ, 0.27f * dayv, US_MAT_BG);
					us_tri(c, x1, y1, x1, HZ, x0, HZ, 0.27f * dayv, US_MAT_BG);
				}
			}
		}
		/* distant treeline / hills on the horizon */
		float pts[2 * 65];
		for (int i = 0; i <= 64; i++) {
			float u = i / 64.0f;
			pts[2 * i] = u * A;
			pts[2 * i + 1] = HZ - 0.012f - 0.018f * (0.5f + 0.5f * us_noise2(u * 5.0f, 8.0f)) - 0.006f * us_noise2(u * 31.0f, 2.0f);
		}
		us_polyline(c, pts, 65, 0.0013f, 0.32f * dayv, US_MAT_EARTH);
		/* a windbreak of poplars along the far edge, all leaning with the gusts */
		for (int i = 0; i < 26; i++) {
			uint32_t hsh = (uint32_t)i * 2246822519u + 5u;
			float u = ((hsh >> 9) & 1023) / 1023.0f;
			float x = u * A;
			float gy = HZ - 0.012f - 0.018f * (0.5f + 0.5f * us_noise2(u * 5.0f, 8.0f));
			float th = 0.018f + 0.022f * ((hsh >> 3) & 63) / 63.0f;
			/* they lean with the bass and bow, one after another, as each kick front passes */
			float bow = s->kpulse * us_smooth(0.0f, 1.0f, 1.0f - fabsf(u - (sgn > 0 ? 1.0f - s->kpulse : s->kpulse)) * 2.5f);
			float bendk = sgn * ((0.004f + 0.02f * s->gust) * (0.6f + 0.4f * us_noise2(u * 9.0f, (float)ctx->t * (0.8f + 2.0f * s->gust))) +
					     0.014f * s->bass_s + 0.016f * bow);
			float tx = x + bendk * th / 0.03f, ty = gy - th;
			uint8_t tm = winter > 0.6f ? US_MAT_EARTH : US_MAT_LEAF;
			float tv = 0.3f * dayv;
			if (ctx->crisp) {
				us_tri(c, x - th * 0.12f, gy, x + th * 0.12f, gy, tx, ty, tv + 0.05f, tm);
			} else {
				/* a narrow spindle crown */
				float mx = x + (tx - x) * 0.45f, my = gy - th * 0.45f;
				wline(c, x - th * 0.06f, gy, mx - th * 0.13f, my, 0.001f, tv, tm);
				wline(c, mx - th * 0.13f, my, tx, ty, 0.001f, tv, tm);
				wline(c, x + th * 0.06f, gy, mx + th * 0.13f, my, 0.001f, tv, tm);
				wline(c, mx + th * 0.13f, my, tx, ty, 0.001f, tv, tm);
			}
		}
		us_line(c, 0, HZ, A, HZ, 0.0012f, 0.28f * dayv, US_MAT_EARTH);
		if (ctx->crisp)
			for (int i = 0; i < 64; i++) {
				float x0 = pts[2 * i], y0 = pts[2 * i + 1], x1 = pts[2 * i + 2], y1 = pts[2 * i + 3];
				us_tri(c, x0, y0, x1, y1, x0, HZ, 0.2f * dayv, US_MAT_EARTH);
				us_tri(c, x1, y1, x1, HZ, x0, HZ, 0.2f * dayv, US_MAT_EARTH);
			}
	}

	/* ---- pixel styles by day: banded pale sky (ink keeps its black ground) ---- */
	if (ctx->crisp && day > 0.2f) {
		float f[2 * 6];
		for (int j = 0; j < 6; j++)
			f[2 * j] = f[2 * j + 1] = (0.08f + 0.3f * j / 5.0f) * us_smooth(0.2f, 0.7f, day);
		int keep_blend = c->blend;
		us_field(c, f, 2, 6, 1.0f, US_MAT_WATER, 0, 0, A, HZ);
		c->blend = keep_blend;
	}

	/* ---- ground fill (pixel styles): dark meadow / snow, and the trodden path ---- */
	if (ctx->crisp) {
		float gv = (0.12f + 0.06f * winter) * dayv;
		us_tri(c, 0, HZ, A, HZ, 0, 1.01f, gv, winter > 0.5f ? US_MAT_WATER : US_MAT_EARTH);
		us_tri(c, A, HZ, A, 1.01f, 0, 1.01f, gv, winter > 0.5f ? US_MAT_WATER : US_MAT_EARTH);
	}
	{
		/* the path: a pale strip of bare earth winding from the bottom edge up to the tree */
		float px0 = 0, py0 = 0, pw0 = 0;
		for (int i = 0; i <= 16; i++) {
			float sc = row_s(TREE_ROW) + (1.0f - row_s(TREE_ROW)) * (i / 16.0f);
			float hw, xc = path_at(s, A, sc, &hw);
			float y = HZ + (1.03f - HZ) * sc;
			if (i > 0) {
				if (ctx->crisp) {
					float pv = (0.2f + 0.06f * winter) * dayv;
					us_tri(c, px0 - pw0 * 0.7f, py0, px0 + pw0 * 0.7f, py0, xc + hw * 0.7f, y, pv, US_MAT_EARTH);
					us_tri(c, px0 - pw0 * 0.7f, py0, xc + hw * 0.7f, y, xc - hw * 0.7f, y, pv, US_MAT_EARTH);
				} else {
					/* engraved ruts: two fine converging lines */
					float rv = 0.16f * dayv * lightk;
					wline(c, px0 - pw0 * 0.3f, py0, xc - hw * 0.3f, y, 0.001f, rv, US_MAT_EARTH);
					wline(c, px0 + pw0 * 0.3f, py0, xc + hw * 0.3f, y, 0.001f, rv, US_MAT_EARTH);
				}
			}
			px0 = xc;
			py0 = y;
			pw0 = hw;
		}
	}

	/* ---- grass ---- */
	float height = (0.30f + 0.95f * w->growth) * (1.0f - 0.45f * winter) * (1.0f + 0.08f * s->swell);
	float keep = (0.3f + 0.7f * dens) * (1.0f - 0.55f * winter);
	int rows_on = MAX_ROWS;
	float lean0 = 0.15f * winter; /* snow-bent */
	float shake = 0;
	float tt = (float)ctx->t;
	float scT = row_s(TREE_ROW);
	s->ntip = 0;
	s->tipseen = 0;
	if (s->cache_A != A) { /* clumps and flower drifts are fixed on the ground: compute once per aspect */
		s->cache_A = A;
		for (int row = 0; row < MAX_ROWS; row++) {
			float Z = 1.0f / row_s(row);
			for (int k = s->row_start[row]; k < s->row_start[row + 1]; k++) {
				blade_t *b = &s->bl[k];
				float X = (b->u * A - A * 0.5f) * Z;
				b->clump = 0.45f + 0.9f * us_smooth(-0.4f, 0.4f, us_noise2(X * 0.05f + 11.0f, Z * 0.22f + 2.0f));
				b->fpatch = us_smooth(0.05f, 0.45f, us_noise2(X * 0.08f + 40.0f, Z * 0.3f + 7.0f));
			}
		}
	}
	for (int row = 0; row < rows_on; row++) {
		float sc = row_s(row);
		float y = HZ + (1.03f - HZ) * sc;
		float Z = 1.0f / sc;
		int b0 = s->row_start[row], b1 = s->row_start[row + 1];
		int nb = (int)((b1 - b0) * keep);
		/* sub: a long, slow swell rolls through the field toward the viewer, lifting row after row */
		float roll = sinf(Z * 0.9f + s->sub_ph * 2.4f);
		float H = 0.36f * sc * height * (1.0f + 0.08f * s->sub_s * roll);
		float shk = 0.42f * (1.0f - 0.6f * s->light); /* light burns the cloud shadows off; quiet deepens them */
		float wid = ctx->crisp ? (sc > 0.55f ? 0.012f : 0.0f) : 0.001f + 0.0022f * sc;
		/* aerial perspective: far rows faint, near rows full; the light follows the loudness */
		float vrow = (ctx->crisp ? 0.3f + 0.65f * sqrtf(sc) : 0.3f + 0.8f * sqrtf(sc)) * dayv * lightk;
		float hw = 0, xc = -10;
		if (sc >= scT)
			xc = path_at(s, A, sc, &hw);
		float push = s->kimp * (0.1f + 0.22f * sc); /* the kick's pressure: the whole field ducks, near more */
		float shiv = s->senv * (0.1f + 0.3f * sc); /* the snare's shiver */
		float hatf = s->henv * (1.0f - sqrtf(sc));   /* hats glint along the far rows */
		if (row == TREE_ROW)
			draw_tree(s, c, ctx, A, sgn, s->gust, autumn, winter, dayv * lightk, tear);
		/* pixel canvases cannot resolve every far blade: thin the farthest rows */
		int kstep = ctx->crisp && sc < 0.1f ? 2 : 1;
		for (int k = 0; k < nb; k += kstep) {
			blade_t *b = &s->bl[b0 + k];
			float x = b->u * A;
			float X = (x - A * 0.5f) * Z;
			float hmul = 1.0f;
			if (xc > -5) { /* trodden path: bare in the middle, short and broken at its edges */
				float e = hw * (0.75f + 0.5f * fmodf(b->ph * 3.3f, 1.0f));
				float dx = fabsf(x - xc);
				if (dx < e * 0.55f)
					continue;
				if (dx < e * 1.8f)
					hmul = 0.2f + 0.8f * us_smooth(e * 0.55f, e * 1.8f, dx);
			}
			/* clumps: taller stands and low hollows, so the field has structure */
			hmul *= b->clump;
			float bend = wind_at(s, X * 0.25f, Z * 0.25f, ax, az);
			float fb = front_at(s, b->u, sc);
			float flut = 0.018f * us_noise2(b->ph, tt * 0.9f);
			if (shiv > 0.004f)
				flut += shiv * us_noise2(b->ph * 1.7f, tt * 15.0f);
			float persp = 0.35f + 0.65f * fabsf(ax) + 0.2f;
			/* every blade has its own resting arch (most bow downwind, some the other way), so a still field
			 * is still grass rather than rain */
			float fr7 = fmodf(b->ph * 5.13f, 1.0f);
			float rest = (fr7 < 0.72f ? 1.0f : -0.8f) * (0.1f + 0.28f * fmodf(b->ph * 2.71f, 1.0f));
			float bb = ((lean0 + rest + gstr * bend + flut + push) * sgn + fb) * persp;
			float h = H * b->h * hmul * (b->head ? 1.25f : 1.0f);
			/* blowing toward/away from the viewer foreshortens the blades */
			float fs = 1.0f - 0.35f * fabsf(az) * us_sat(bend);
			float shadow = 1.0f - shk + shk * us_smooth(-0.3f, 0.3f, us_noise2(X * 0.05f - s->cloud_t * 2.0f * ax, Z * 0.08f - s->cloud_t * 2.0f * az));
			/* bent blades catch the light: the breeze's waves are soft sheens, a kick front a bright band */
			float sheen = 0.72f + 0.3f * bend * (0.3f + s->breeze) + 0.5f * us_sat(fabsf(fb)) + 0.5f * push;
			if (b->tint == 1)
				sheen += 0.9f * hatf;
			float v = us_sat(vrow * shadow * sheen * (0.75f + 0.25f * b->h));
			uint8_t m = US_MAT_LEAF;
			if (autumn > 0.5f)
				m = b->tint < 2 ? US_MAT_LIGHT : (b->tint == 2 ? US_MAT_EARTH : US_MAT_STEM); /* sun-gold, dry, straw */
			else if (winter > 0.5f)
				m = b->tint < 3 ? US_MAT_EARTH : US_MAT_STEM;
			else if (summer > 0.5f && b->tint == 0)
				m = US_MAT_STEM;
			/* the crest of a gust front: bent blades flash their pale sides, a travelling sheen of light */
			if (winter < 0.5f && autumn < 0.5f && b->tint == 1 && b->h > 0.86f && fabsf(fb) > 0.3f && sc > 0.08f)
				m = US_MAT_LIGHT;
			float pts[8];
			float bx = us_clampf(bb * 0.9f, -1.6f, 1.6f);
			for (int q = 0; q < 4; q++) {
				float t = q / 3.0f;
				pts[2 * q] = x + bx * h * t * t;
				pts[2 * q + 1] = y - h * fs * t * (1.0f - 0.3f * fabsf(bx) * t);
			}
			blade(c, x, y, bx, h, fs, wid, v, m);
			float tx = pts[6], ty = pts[7];
			float force = gstr * bend + 0.5f * fabsf(fb);
			if (winter > 0.4f && b->tint == 3) /* snow caught on the tips */
				us_point(c, tx, ty, 0.0015f + 0.003f * sc, 0.6f * winter * dayv, US_MAT_WATER);
			int flower = 0;
			if (!b->head && b->tint == 3 && sc > 0.22f && winter < 0.5f) {
				/* wildflowers grow in drifts, not evenly */
				flower = fmodf(b->ph * 7.31f, 1.0f) < (0.03f + 0.75f * b->fpatch) * (0.6f + 0.6f * w->bloom);
			}
			if (flower) {
				/* wildflowers among the grass: heads nod to the lower mids, then go to seed clocks in late summer */
				float dx = pts[6] - pts[4], dy = pts[7] - pts[5];
				float dl = sqrtf(dx * dx + dy * dy) + 1e-6f;
				float nod = 0.35f * s->lowmid_s * us_noise2(b->ph * 3.1f, tt * 1.2f);
				float rr = (0.005f + 0.008f * sc) * (0.6f + 0.4f * w->growth);
				float fx = tx + (dx / dl + nod) * rr * 0.8f, fy = ty + dy / dl * rr * 0.8f;
				float clock = us_smooth(0.6f, 1.2f, summer + autumn * 0.5f) * (1.0f - us_smooth(0.4f, 0.9f, autumn));
				float fv = us_sat(v * 1.2f + 0.08f);
				if (clock > 0.5f && fmodf(b->ph * 3.7f, 1.0f) < 0.6f) {
					/* a dandelion clock: a ring of fluff that thins in the gusts */
					int np = c->crisp ? 4 : 9;
					for (int q = 0; q < np; q++) {
						float an = US_TAU * q / np + b->ph;
						float fq = fmodf(b->ph * 13.0f + q * 0.37f, 1.0f);
						if (fq < force * 0.8f)
							continue; /* this tuft has blown */
						float px2 = fx + cosf(an) * rr, py2 = fy + sinf(an) * rr;
						if (c->crisp)
							us_put(c, (int)(px2 * c->s), (int)(py2 * c->s), fv * 0.8f, US_MAT_ACCENT);
						else
							wline(c, fx, fy, px2, py2, 0.0008f, fv * 0.45f, US_MAT_ACCENT);
					}
				} else if (c->crisp) {
					us_disc(c, fx, fy, rr * 0.9f, fv, US_MAT_BLOOM);
				} else {
					/* five petals: little loops around a bright eye */
					for (int q = 0; q < 5; q++) {
						float an = US_TAU * q / 5 + b->ph + nod * 2.0f;
						wline(c, fx, fy, fx + cosf(an) * rr, fy + sinf(an) * rr * 0.7f, 0.0016f + 0.001f * sc, fv * 0.8f, US_MAT_BLOOM);
					}
					us_point(c, fx, fy, 0.0011f + 0.001f * sc, us_sat(fv * 1.3f), US_MAT_LIGHT);
				}
				tip_add(s, fx, fy, sc + 2.0f); /* +2 marks a flower (petals fly) */
			}
			if (b->head && winter < 0.6f && sc > 0.12f) {
				/* seed head: a short thick spike along the tip direction */
				float dx = pts[6] - pts[4], dy = pts[7] - pts[5];
				float dl = sqrtf(dx * dx + dy * dy) + 1e-6f;
				float hl = (0.012f + 0.018f * (summer + autumn)) * sc * height;
				float hx = tx + dx / dl * hl, hy = ty + dy / dl * hl;
				uint8_t hm = spring > 0.5f ? US_MAT_LEAF : (autumn > 0.5f ? US_MAT_BLOOM : US_MAT_ACCENT);
				float hv = us_sat(v * 1.25f + 0.1f);
				if (ctx->crisp)
					us_line(c, tx, ty, hx, hy, sc > 0.6f ? 0.0045f : 0.001f, hv, hm);
				else {
					/* ink: a slender spike with two fine awns */
					wline(c, tx, ty, hx, hy, 0.0012f + 0.0016f * sc, hv, hm);
					float nx = -dy / dl * hl * 0.35f, ny = dx / dl * hl * 0.35f;
					float mx = (tx + hx) * 0.5f, my = (ty + hy) * 0.5f;
					wline(c, mx, my, hx + nx, hy + ny, 0.0008f, hv * 0.6f, hm);
					wline(c, mx, my, hx - nx, hy - ny, 0.0008f, hv * 0.6f, hm);
				}
				tip_add(s, hx, hy, sc);
				/* strong gusts shake seeds loose */
				if (force > 0.35f && s->nsd < MAX_SEEDS &&
				    us_rand(r) < dt * (force - 0.3f) * 2.0f * (0.4f + summer + autumn) * (0.5f + w->growth)) {
					seed_t *q = &s->sd[s->nsd++];
					q->x = hx;
					q->y = hy;
					q->vx = sgn * us_randr(r, 0.05f, 0.18f) * (0.5f + s->gust);
					q->vy = -us_randr(r, 0.01f, 0.05f) * (0.3f + sc);
					q->life = 1.0f;
					q->ph = us_rand(r) * 10;
					q->sz = 0.0008f + 0.0018f * sc;
					shake += 1.0f;
				}
			}
		}
	}

	{
		float ksc = row_s(KITE_ROW);
		draw_kite(s, c, ctx, A, sgn, HZ + (1.03f - HZ) * ksc, ksc, dayv, winter);
	}

	/* ---- the near layer: oat stalks framing the view; then the drums' answers on top ---- */
	draw_fg(s, c, ctx, A, sgn, gstr, ax, az, height, dayv, lightk, winter, spring);
	spawn_drums(s, ctx, snapped, sgn);
	draw_drums(s, c, ctx, sgn, dayv);

	/* ---- seeds / pollen adrift ---- */
	s->seed_acc += shake;
	if (shake > 0)
		us_world_add(w, &w->pollen, 0.0006f * shake, 1.0f);
	for (int i = 0; i < s->nsd;) {
		seed_t *q = &s->sd[i];
		float dirx = ax >= 0 ? 1.0f : -1.0f;
		float lift = us_noise2(q->x * 4.0f + q->ph, (float)ctx->t * 0.7f);
		q->vx += (dirx * (0.06f + 0.25f * s->gust) - q->vx) * dt * 1.2f;
		q->vy += (-0.01f + 0.04f * lift - q->vy * 0.8f) * dt;
		q->x += q->vx * dt;
		q->y += q->vy * dt + 0.01f * sinf((float)ctx->t * 3.0f + q->ph) * dt;
		q->life -= dt * 0.2f;
		if (q->life <= 0 || q->x < -0.05f || q->x > A + 0.05f || q->y > 1.02f || q->y < 0.0f) {
			*q = s->sd[--s->nsd];
			continue;
		}
		float v = us_sat(q->life * 1.4f) * 0.75f * dayv + 0.15f;
		us_point(c, q->x, q->y, q->sz, v, US_MAT_ACCENT);
		if (!ctx->crisp && q->sz > 0.0016f) /* thistledown: a tiny tuft */
			wline(c, q->x, q->y, q->x - q->vx * 0.04f, q->y - q->sz * 2.5f, 0.0008f, v * 0.5f, US_MAT_ACCENT);
		i++;
	}

	/* ---- leaves tumbling away downwind: spinning and flipping edge-on ---- */
	for (int i = 0; i < s->ntb;) {
		tumb_t *q = &s->tb[i];
		/* each leaf sails differently (its own drag and its own eddies), so they scatter rather than queue */
		float sail = 0.45f + 0.1f * q->vf; /* vf is random per leaf, 4..11 */
		float lift = us_noise2(q->vf * 7.3f, (float)ctx->t * 1.3f + q->vf);
		q->vx += (sgn * (0.08f + 0.45f * s->gust) * (0.6f + 0.6f * fabsf(ax)) * sail - q->vx) * dt * 1.4f;
		q->vy += (0.05f - 0.3f * lift * (0.3f + s->gust) - q->vy * 1.1f) * dt;
		q->x += q->vx * dt;
		q->y += q->vy * dt;
		q->rot += q->vr * dt * (0.5f + s->gust);
		q->flip += q->vf * dt;
		q->life -= dt * 0.12f;
		if (q->life <= 0 || q->x < -0.08f || q->x > A + 0.08f || q->y > 1.03f) {
			*q = s->tb[--s->ntb];
			continue;
		}
		float wf = fabsf(cosf(q->flip));
		float v = us_sat(q->life * 2.0f) * (0.55f + 0.35f * wf) * dayv;
		leafshape(c, q->x, q->y, q->rot, q->sz * 1.2f, q->sz * (0.06f + 0.36f * wf), v, q->mat);
		i++;
	}

	/* ---- wisps of moving air: thin streamlines that ride the stronger gusts ---- */
	{
		float rate = (s->breeze * s->breeze * 0.9f + 0.1f) * (0.4f + 0.8f * dens);
		s->wisp_acc += rate * dt;
		for (int i = 0; i < MAX_WISP && s->wisp_acc >= 1.0f; i++) {
			wisp_t *q = &s->wp[i];
			if (q->life > 0)
				continue;
			s->wisp_acc -= 1.0f;
			q->life = us_randr(r, 1.6f, 3.2f);
			q->age = 0;
			q->len = us_randr(r, 0.2f, 0.45f);
			q->amp = us_randr(r, 0.006f, 0.02f);
			q->ph = us_rand(r) * US_TAU;
			q->sp = us_randr(r, 0.8f, 1.2f);
			q->y = us_randr(r, HZ - 0.32f, HZ + 0.2f);
			/* a wide mix sends streamlines in from both flanks */
			q->dir = us_rand(r) < 0.45f * s->width_s ? -sgn : sgn;
			q->x = q->dir > 0 ? us_randr(r, -0.2f, A * 0.7f) : us_randr(r, A * 0.3f, A + 0.2f);
		}
		if (s->wisp_acc > 1.0f)
			s->wisp_acc = 1.0f;
		for (int i = 0; i < MAX_WISP; i++) {
			wisp_t *q = &s->wp[i];
			if (q->life <= 0)
				continue;
			q->age += dt;
			if (q->age >= q->life) {
				q->life = 0;
				continue;
			}
			float spd = (0.25f + 0.9f * s->gust) * q->sp;
			q->x += q->dir * spd * dt;
			float env = sinf(US_PI * q->age / q->life);
			/* the stroke grows out of nothing at the head, and ends in a small curl */
			float L = q->len * us_sat(q->age * 2.0f / q->life + 0.2f);
			enum { NW = 16 };
			float px = 0, py = 0;
			float vk = (ctx->crisp ? 0.42f : 0.36f) * dayv * env;
			for (int k = 0; k <= NW; k++) {
				float f = (float)k / NW;
				float x = q->x - q->dir * L * f;
				float y = q->y + q->amp * sinf(f * 5.0f + q->ph - q->age * 4.0f) * f + 0.004f * us_noise2(x * 3.0f, q->ph);
				/* pixel: the streamline breaks into dashes that travel with it, like wind marks in a print */
				int gap = 0;
				if (k > 0 && !gap)
					wline(c, px, py, x, y, ctx->crisp ? 0.0045f : 0.0011f, vk * (1.0f - f * f), US_MAT_ACCENT);
				px = x;
				py = y;
			}
			/* the head rolls over into a small curl (an eddy shed by the gust) */
			{
				float R = (ctx->crisp ? 0.016f : 0.008f + q->amp * 0.6f) * us_sat(q->age * 1.5f);
				float hx = q->x, hy = q->y + q->amp * sinf(q->ph - q->age * 4.0f) * 0.0f;
				float cxr = hx, cyr = hy - R; /* curl turns upward, over the top */
				float ppx = hx, ppy = hy;
				enum { NCURL = 12 };
				for (int k = 1; k <= NCURL; k++) {
					float f = (float)k / NCURL;
					float a = US_PI * 0.5f - q->dir * f * US_PI * 1.6f;
					float rr = R * (1.0f - 0.55f * f);
					float x = cxr + q->dir * cosf(a) * rr * 0.0f + cosf(a) * rr * sgn, y = cyr + sinf(a) * rr;
					(void)x;
					x = cxr + q->dir * cosf(a) * rr;
					wline(c, ppx, ppy, x, y, ctx->crisp ? 0.004f : 0.0011f, vk * (1.0f - 0.6f * f), US_MAT_ACCENT);
					ppx = x;
					ppy = y;
				}
			}
		}
	}

	/* ---- swallows riding the gusts: the melody (mids) lifts them, sharp hits make one dart and bank ---- */
	{
		int nb = 2 + (int)(4.0f * us_sat(0.25f + w->flock + 0.3f * s->mid_s));
		if (nb > MAX_BIRD)
			nb = MAX_BIRD;
		float bvis = us_smooth(0.08f, 0.35f, day);
		int dart = snapped ? (int)(us_rand(r) * nb) : -1;
		for (int i = 0; i < MAX_BIRD; i++) {
			bird_t *b = &s->bd[i];
			float ty = HZ - 0.03f - (0.05f + 0.22f * s->mid_s) * (0.4f + 0.6f * b->alt) +
				   0.05f * us_noise2(b->ph, (float)ctx->t * 0.25f);
			float tvx = sgn * (0.06f + 0.22f * s->gust) * (0.7f + 0.6f * b->alt) + 0.12f * us_noise2(b->ph + 4.0f, (float)ctx->t * 0.3f);
			b->vx += (tvx - b->vx) * dt * 1.5f;
			b->vy += ((ty - b->y) * 2.0f - b->vy * 1.4f) * dt;
			if (i == dart) {
				b->vy -= 0.18f + 0.15f * a->onset;
				b->vx += sgn * 0.2f;
				b->bank = 1.0f;
			}
			b->bank = us_approach(b->bank, 0.0f, 0.45f, dt);
			b->x += b->vx * dt / A;
			b->y += b->vy * dt;
			if (b->x > 1.08f) b->x -= 1.16f;
			if (b->x < -0.08f) b->x += 1.16f;
			if (b->y < 0.04f) { b->y = 0.04f; b->vy = 0; }
			/* flap in bursts, glide between (faster beats when the melody is busy) */
			float gl = us_noise2(b->ph * 2.3f, (float)ctx->t * 0.6f);
			if (gl > -0.1f || b->bank > 0.2f)
				b->ph += dt * (9.0f + 8.0f * s->mid_s);
			if (i >= nb || bvis <= 0.01f)
				continue;
			float x = b->x * A, y = b->y;
			float ws = (0.012f + 0.008f * b->alt) * (ctx->crisp ? 1.4f : 1.0f);
			float fl = sinf(b->ph) * (gl > -0.1f || b->bank > 0.2f ? 1.0f : 0.15f);
			float dirx = b->vx >= 0 ? 1.0f : -1.0f;
			float tip = -fl * ws * 0.6f - b->bank * ws * 0.3f;
			float v = 0.62f * bvis * dayv;
			if (ctx->crisp) {
				us_put(c, (int)(x * c->s), (int)(y * c->s), v, US_MAT_ACCENT);
				us_line(c, x, y, x - ws * 0.8f, y + tip * 0.8f, 0.001f, v, US_MAT_ACCENT);
				us_line(c, x, y, x + ws * 0.8f, y + tip * 0.8f, 0.001f, v, US_MAT_ACCENT);
			} else {
				/* swept wings with a bent wrist; a forked tail behind */
				float ey = y + tip * 0.4f - ws * 0.12f;
				wline(c, x, y, x - ws * 0.45f + dirx * ws * 0.1f, ey, 0.0013f, v, US_MAT_ACCENT);
				wline(c, x - ws * 0.45f + dirx * ws * 0.1f, ey, x - ws + dirx * ws * 0.35f, y + tip, 0.001f, v, US_MAT_ACCENT);
				wline(c, x, y, x + ws * 0.45f + dirx * ws * 0.1f, ey, 0.0013f, v, US_MAT_ACCENT);
				wline(c, x + ws * 0.45f + dirx * ws * 0.1f, ey, x + ws + dirx * ws * 0.35f, y + tip, 0.001f, v, US_MAT_ACCENT);
				wline(c, x, y, x - dirx * ws * 0.45f, y + ws * 0.12f, 0.0008f, v * 0.7f, US_MAT_ACCENT);
				wline(c, x, y, x - dirx * ws * 0.45f, y - ws * 0.05f, 0.0008f, v * 0.7f, US_MAT_ACCENT);
			}
		}
	}

	/* ---- a loose swarm of midges over the near grass (fireflies by night); the air band sets them dancing.
	 *      Quiet passages bring more of them out. ---- */
	{
		s->gnat_x += dt * sgn * (0.004f + 0.02f * s->gust) / A;
		if (s->gnat_x > 1.15f) s->gnat_x -= 1.3f;
		if (s->gnat_x < -0.15f) s->gnat_x += 1.3f;
		float quiet = us_sat(w->calm + 0.6f * (1.0f - a->active) + 0.3f * w->insects);
		int ng = 6 + (int)((MAX_GNAT - 6) * quiet);
		float night = us_sat(1.0f - day * 2.5f);
		float jit = 0.003f + 0.01f * s->air_s;
		float gx = s->gnat_x * A, gy = 0.66f + 0.04f * us_noise2(s->gnat_x * 3.0f, 1.0f);
		float tt = (float)ctx->t;
		for (int i = 0; i < ng; i++) {
			float fi = i * 1.618f;
			float x = gx + 0.07f * us_noise2(fi, tt * 0.18f) + jit * us_noise2(fi + 30.0f, tt * (3.0f + 9.0f * s->air_s));
			float y = gy + 0.045f * us_noise2(fi + 60.0f, tt * 0.2f) + jit * us_noise2(fi + 90.0f, tt * (3.0f + 9.0f * s->air_s));
			if (night > 0.05f) {
				float pulse = sinf(tt * (0.9f + 0.3f * fmodf(fi, 1.0f)) + fi * 2.3f);
				pulse = pulse > 0 ? pulse * pulse * pulse : 0;
				float fv = night * (0.15f + 0.85f * pulse);
				us_point(c, x, y, c->crisp ? 0.003f : 0.0016f + 0.0018f * pulse, fv, US_MAT_LIGHT);
			}
			if (night < 0.95f && !ctx->crisp)
				us_point(c, x, y, 0.0009f, 0.4f * (1.0f - night) * dayv, US_MAT_ACCENT);
		}
	}
}

const us_scene_vt us_scene_wind = {"wind", "Wind", 0.0f, create, destroy, frame};
