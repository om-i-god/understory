/* Understory scene: Wind — a prairie of grass in perspective, combed by gusts.
 *
 * An open windswept hillside under a big sky. The ground is three rolling layers, each filled (a solid grass
 * colour in pixel, a faint glow in ink) and then planted: the far downs (fine, dense grass, a texture) with a
 * windbreak of poplars on their crest, the mid hill with a lone tree on its knoll as the focal silhouette,
 * and the near slope of tall, sparse blades and seed heads. A trodden path winds from the bottom edge up
 * to the tree; tall oat stalks frame the foreground; a hazy range sits behind the horizon. Gusts show as
 * ragged lighter sweeps of bent grass travelling over every layer (pale undersides up), cloud shadows slide
 * over them. The steady wind is a slow travelling noise field in ground-plane coordinates.
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
 * Life: a kestrel hangs on the wind by day (a pale barn owl, face on with its legs let down, by night):
 *        wings flickering, head dead still, almost motionless when it blows hard; a kick's gust jolts it, a
 *        hard snare and it stoops into the grass in a puff of seed and climbs back out. A hare on the near
 *        slope sits, grazes and lollops; a snare sits it bolt upright, a hard one sends it jinking away at full
 *        stretch (it comes back later); kicks flick its ears. Butterflies (white, brimstone, red, blue) flutter
 *        low by day, flung along by the gust fronts and scattered up by the snare. Two swallows skim the grass
 *        tips. Wind-flagged hawthorns on the mid hill bow to the kick fronts (may blossom in spring, haws in
 *        autumn, bare in winter); poppies and cornflowers among the wildflowers.
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
#define NTAIL 14
#define HZ 0.47f /* horizon */
#define MAX_TSEG 96
#define MAX_LEAF 420
#define MAX_TUMB 160
#define MAX_CLOUD 7
#define MAX_WISP 20
/* the ground is three rolling layers, far to near: A the far downs (fine dense grass, a texture),
 * B the mid hill with the tree on its knoll, C the near slope (tall, sparse, individual blades) */
#define NLAY 3
#define RG_N 256
#define TREE_D 0.04f /* depth of the tree within layer B (it stands on the crest) */
#define KITE_D 0.3f  /* the kite flyer, further down layer B */
static const int lay_rows[NLAY] = {10, 11, 13};
static const int lay_cnt[NLAY] = {150, 72, 17};
static const float lay_s0[NLAY] = {0.025f, 0.09f, 0.34f}, lay_s1[NLAY] = {0.07f, 0.28f, 1.0f};
static const float lay_drop[NLAY] = {0.14f, 0.26f, 0.40f};

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

#define NBF 7   /* butterflies */
#define NHAW 3  /* wind-bent hawthorns */
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
	uint8_t rlay[MAX_ROWS];            /* which ground layer a row belongs to */
	float rd[MAX_ROWS], rsc[MAX_ROWS]; /* depth within the layer (0 ridge .. 1 front), perspective scale */
	float rg[NLAY][RG_N + 1];          /* ridge line of each layer (y), sampled across the width */
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
	/* the hunter: a kestrel hanging on the wind by day, a barn owl quartering the field by night */
	float hk_x, hk_y, hk_sx, hk_sy, hk_tx, hk_ty, hk_t, hk_dur, hk_flap, hk_cool, hk_hold, hk_d, hk_j;
	int hk_st, hk_owl;
	/* a hare on the near slope */
	float hr_u, hr_d, hr_dir, hr_t, hr_ph, hr_alert, hr_hold, hr_graze, hr_spd, hr_cool, hr_ear, hr_fd;
	int hr_st;
	/* butterflies tossed by the gusts */
	float bf_x[NBF], bf_y[NBF], bf_vx[NBF], bf_vy[NBF], bf_ph[NBF], bf_fr[NBF];
	uint8_t bf_kind[NBF];
	/* hawthorns on the mid hill, flagged by the prevailing wind */
	float hw_u[NHAW], hw_d[NHAW], hw_h[NHAW], hw_b[NHAW], hw_v[NHAW], hw_lean;
	int life_init;
} st_t;

/* ridge line (top edge) of ground layer k at x (normalized units) */
static float ridge(const st_t *s, int k, float x, float A)
{
	float f = us_clampf(x / A, 0.0f, 1.0f) * RG_N;
	int i = (int)f;
	if (i >= RG_N)
		i = RG_N - 1;
	return us_lerp(s->rg[k][i], s->rg[k][i + 1], f - i);
}

/* the ground under a blade of row r at x */
static float row_y(const st_t *s, int r, float x, float A)
{
	int k = s->rlay[r];
	return ridge(s, k, x, A) + s->rd[r] * lay_drop[k];
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
	int n = 0, row = 0;
	for (int k = 0; k < NLAY; k++)
		for (int j = 0; j < lay_rows[k]; j++, row++) {
			s->row_start[row] = n;
			float f = (j + 0.5f) / lay_rows[k];
			s->rlay[row] = (uint8_t)k;
			s->rd[row] = powf(f, 1.3f); /* rows bunch toward the ridge (farther away) */
			s->rsc[row] = us_lerp(lay_s0[k], lay_s1[k], s->rd[row]);
			for (int q = 0; q < lay_cnt[k] && n < MAX_BLADES; q++) {
				blade_t *b = &s->bl[n++];
				b->u = us_randr(r, -0.03f, 1.03f);
				b->h = us_randr(r, k == 2 ? 0.4f : 0.55f, 1.0f);
				b->ph = us_rand(r) * 100.0f;
				b->head = us_rand(r) < (k == 2 ? 0.34f : 0.2f);
				b->tint = (uint8_t)(us_rand(r) * 4);
			}
		}
	s->row_start[MAX_ROWS] = n;
	/* the lone tree: a spreading crown on a short trunk, on a knoll of the mid hill */
	s->tree_u = us_rand(r) < 0.5f ? us_randr(r, 0.17f, 0.3f) : us_randr(r, 0.7f, 0.83f);
	{
		float hs = us_rand(r) * 50.0f;
		float cs = s->tree_u < 0.5f ? -1.0f : 1.0f; /* the near slope rises away from the tree */
		for (int i = 0; i <= RG_N; i++) {
			float u = (float)i / RG_N;
			float dk = (u - s->tree_u) / 0.17f;
			s->rg[0][i] = HZ - 0.006f - 0.042f * (0.5f + 0.5f * us_noise2(u * 2.6f + hs, 1.3f)) - 0.004f * us_noise2(u * 11.0f + hs, 4.0f);
			s->rg[1][i] = HZ + 0.125f - 0.07f * (0.5f + 0.5f * us_noise2(u * 1.9f + hs + 10.0f, 2.1f)) - 0.1f * expf(-dk * dk) -
				      0.003f * us_noise2(u * 14.0f + hs, 6.0f);
			s->rg[2][i] = 0.73f - 0.06f * (0.5f + 0.5f * us_noise2(u * 1.5f + hs + 20.0f, 3.3f)) + 0.1f * (u - 0.5f) * cs -
				      0.004f * us_noise2(u * 17.0f + hs, 8.0f);
		}
	}
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
	/* hawthorns on the mid hill, clear of the tree, the kite flyer and the path */
	s->hw_lean = us_rand(r) < 0.5f ? -1.0f : 1.0f;
	for (int i = 0; i < NHAW; i++) {
		float u = 0.5f;
		for (int tries = 0; tries < 40; tries++) {
			u = us_randr(r, 0.04f, 0.96f);
			float side = s->path_x0 > s->tree_u ? 1.0f : -1.0f;
			int ok = fabsf(u - s->tree_u) > 0.14f && fabsf(u - s->kite_u) > 0.1f && fabsf(u - (s->tree_u + side * 0.12f)) > 0.08f;
			for (int j = 0; j < i; j++)
				ok = ok && fabsf(u - s->hw_u[j]) > 0.12f;
			if (ok)
				break;
		}
		s->hw_u[i] = u;
		s->hw_d[i] = us_randr(r, 0.05f, 0.16f);
		s->hw_h[i] = us_randr(r, 0.12f, 0.15f);
	}
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
	float g = us_noise2(along * 0.7f - s->flow, across * 0.18f + 3.7f);
	g += 0.35f * us_noise2(along * 1.9f - s->flow * 1.7f, across * 0.5f + 9.1f);
	return us_smooth(-0.45f, 0.75f, g);
}

/* the gust bands seen on the ground: lighter sweeps of bent grass with their pale undersides up. The same
 * travelling field as the breeze, finer, and warped so the sweeps are ragged rather than ruled lines. */
static float band_at(const st_t *s, float X, float Z, float ax, float az)
{
	float along = X * ax + Z * az;
	float across = -X * az + Z * ax;
	float wx = 0.9f * us_noise2(across * 0.35f + 1.3f, along * 0.25f + s->flow * 0.3f);
	float g = us_noise2(along * 0.7f + wx - s->flow, across * 0.4f + 3.7f);
	g += 0.35f * us_noise2(along * 1.9f - s->flow * 1.7f, across * 0.9f + 9.1f);
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
			bend += f->sgn * f->amp * (d > 0 ? expf(-d * d * 3.0f) : expf(-d * d * 0.7f));
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
	float by = ridge(s, 1, bx, A) + TREE_D * lay_drop[1];
	float H = 0.4f * (0.9f + 0.2f * ctx->w->growth);
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
					    us_noise2(i * 0.91f + 5.0f, t * 16.0f) * 0.24f * s->senv) * g->depth * 0.33f : 0.0f;
		/* crowns compress downwind: branches swing over with the bend */
		float ang = pa + g->rel * (1.0f - 0.25f * us_sat(fabsf(s->tbend))) + sgn * s->tbend * flex[g->depth] + fl;
		float L = g->len * H;
		float ex = px + cosf(ang) * L, ey = py + sinf(ang) * L;
		s->tex[i] = ex;
		s->tey[i] = ey;
		s->tea[i] = ang;
		float wd = (c->crisp ? 0.024f : 0.009f) * g->w;
		if (wd < 0.0011f)
			wd = 0.0011f;
		if (c->crisp) {
			float bv = us_sat(0.3f * dayv + 0.05f * (5 - g->depth));
			oline(c, px, py, ex, ey, wd, bv, US_MAT_STEM);
		} else if (wd * c->s > 2.2f) {
			/* ink: trunk and main limbs as two fine tapering edges (the crown carries the glow) */
			float bv = us_sat(0.36f * dayv + 0.03f * (5 - g->depth));
			float we = wd * 0.62f;
			float nx = -sinf(ang) * 0.5f, ny = cosf(ang) * 0.5f;
			wline(c, px + nx * wd, py + ny * wd, ex + nx * we, ey + ny * we, 0.0011f, bv, US_MAT_STEM);
			wline(c, px - nx * wd, py - ny * wd, ex - nx * we, ey - ny * we, 0.0011f, bv * 0.75f, US_MAT_STEM);
			if (g->parent < 0) /* the foot of the trunk flares into the knoll */
				for (int q = -1; q <= 1; q += 2)
					wline(c, px + q * nx * wd, py + q * ny * wd, px + q * nx * wd * 2.6f, py + 0.004f, 0.0011f, bv * 0.8f, US_MAT_STEM);
		} else {
			float bv = us_sat(0.42f * dayv + 0.04f * (5 - g->depth));
			wline(c, px, py, ex, ey, wd, bv, US_MAT_STEM);
		}
		if (winter > 0.4f && g->depth <= 3 && fabsf(cosf(ang)) > 0.5f) /* snow along the limbs */
			wline(c, px, py - 0.002f, ex, ey - 0.002f, 0.0016f, 0.6f * winter * dayv, US_MAT_WATER);
	}
	if (c->crisp && leafk > 0.2f) {
		/* pixel: a solid crown silhouette under the leaves, so the tree reads as one strong shape */
		uint8_t cm = leaf_mat(autumn, 0.9f);
		for (int i = 0; i < s->nts; i++) {
			if (s->ts[i].depth < 3)
				continue;
			float rr = (0.008f + 0.006f * (5 - s->ts[i].depth)) * (0.4f + 0.6f * leafk) * H / 0.36f;
			float cx = s->tex[i], cy = s->tey[i];
			int r = (int)(rr * c->s + 0.5f), px = (int)(cx * c->s), py = (int)(cy * c->s);
			for (int yy = -r; yy <= r; yy++)
				for (int xx = -r; xx <= r; xx++)
					if (xx * xx + yy * yy <= r * r + r)
						oput(c, px + xx, py + yy, (0.24f + 0.04f * (yy < 0)) * dayv, cm);
		}
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
		float fl = us_noise2(l->ph, t * (0.8f + 3.0f * gust)) + 1.6f * s->senv * us_noise2(l->ph + 3.0f, t * 18.0f);
		/* rotate toward the downwind direction by the shortest way */
		float da = stream - l->ang;
		da -= US_TAU * floorf((da + US_PI) / US_TAU);
		float la = l->ang + da * lift * 0.8f + fl * (0.3f + 0.6f * gust);
		/* gusts flip the leaves: their pale undersides catch the light */
		float sheen = us_sat(0.5f + fl * 0.8f) * lift;
		float v = us_sat((c->crisp ? 0.3f + 0.45f * sheen : 0.5f + 0.45f * sheen) * dayv);
		float len = (c->crisp ? 0.016f : 0.0135f) * l->sz;
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
	float ks = (0.021f + 0.008f * s->klift) * (c->crisp ? 1.4f : 1.0f);
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

/* the trodden path's centre x in layer k at depth d (it runs from the tree down the mid hill, then
 * reappears over the near slope's ridge and winds to the bottom edge); -10 where there is none */
static float path_at(const st_t *s, float A, int k, float d, float sc, float *hw)
{
	float side = s->path_x0 > s->tree_u ? 1.0f : -1.0f;
	float tx = s->tree_u * A;
	*hw = 0.003f + 0.1f * sc;
	if (k == 1) {
		if (d < TREE_D + 0.04f)
			return -10.0f;
		float f = (d - TREE_D) / (1.0f - TREE_D);
		return tx + side * (0.012f + 0.16f * f + 0.03f * sinf(f * US_PI));
	}
	if (k == 2) {
		float xt = tx + side * 0.24f, xb = s->path_x0 * A;
		return us_lerp(xt, xb, powf(d, 0.8f)) - side * 0.07f * sinf(d * US_PI);
	}
	return -10.0f;
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
		int n = (int)((ctx->crisp ? 40.0f + 60.0f * us_sat(a->snare) : 46.0f + 68.0f * us_sat(a->snare)) * (0.5f + dens));
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
		int n = (int)((ctx->crisp ? 16.0f + 30.0f * us_sat(a->hat) : 18.0f + 34.0f * us_sat(a->hat)) * (0.5f + dens) * (ctx->crisp ? 1.6f : 1.0f));
		for (int k = 0; k < n && s->ngl < MAX_GLINT; k++) {
			int j = (int)(us_rng_u32(r) % (uint32_t)s->ntip);
			glint_t *g = &s->gl[s->ngl++];
			g->x = s->tipx[j];
			g->y = s->tipy[j];
			g->life = 1.0f;
			g->sz = 0.008f + 0.014f * fminf(fmodf(s->tips[j], 2.0f), 1.0f);
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
		float v = us_sat((0.25f + 0.75f * l) * (0.3f + 0.75f * dayv));
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

/* a windbreak of poplars along the crest of the far downs, all leaning with the gusts */
static void draw_poplars(const st_t *s, us_canvas *c, const us_ctx *ctx, float A, float sgn, float dayv, float winter)
{
	float u0 = s->tree_u < 0.5f ? 0.56f : 0.06f;
	for (int i = 0; i < 18; i++) {
		uint32_t hsh = (uint32_t)i * 2246822519u + 5u;
		float u = u0 + 0.38f * (i + 0.5f * ((hsh >> 9) & 1023) / 1023.0f) / 18.0f;
		if (i >= 15) /* a few strays */
			u = ((hsh >> 9) & 1023) / 1023.0f;
		float x = u * A;
		float gy = ridge(s, 0, x, A) + 0.004f;
		float th = (0.03f + 0.03f * ((hsh >> 3) & 63) / 63.0f) * (ctx->crisp ? 1.25f : 1.0f);
		/* they lean with the bass and bow, one after another, as each kick front passes */
		float bow = s->kpulse * us_smooth(0.0f, 1.0f, 1.0f - fabsf(u - (sgn > 0 ? 1.0f - s->kpulse : s->kpulse)) * 2.5f);
		float bendk = sgn * ((0.004f + 0.02f * s->gust) * (0.6f + 0.4f * us_noise2(u * 9.0f, (float)ctx->t * (0.8f + 2.0f * s->gust))) +
				     0.014f * s->bass_s + 0.016f * bow);
		float tx = x + bendk * th / 0.03f, ty = gy - th;
		uint8_t tm = winter > 0.6f ? US_MAT_EARTH : US_MAT_LEAF;
		if (ctx->crisp) {
			float tv = 0.36f * dayv;
			otri(c, x - th * 0.16f, gy, x + th * 0.16f, gy, tx, ty, tv, tm);
			otri(c, x - th * 0.16f, gy, x + th * 0.16f, gy, (x + tx) * 0.5f, gy + th * 0.1f, tv, tm);
		} else {
			/* a narrow spindle crown */
			float tv = 0.4f * dayv;
			float mx = x + (tx - x) * 0.45f, my = gy - th * 0.45f;
			wline(c, x - th * 0.06f, gy, mx - th * 0.13f, my, 0.001f, tv, tm);
			wline(c, mx - th * 0.13f, my, tx, ty, 0.001f, tv, tm);
			wline(c, x + th * 0.06f, gy, mx + th * 0.13f, my, 0.001f, tv, tm);
			wline(c, mx + th * 0.13f, my, tx, ty, 0.001f, tv, tm);
			wline(c, x, gy, (x + mx) * 0.5f, (gy + my) * 0.5f, 0.001f, tv * 0.6f, US_MAT_STEM);
		}
	}
}

/* ---- life: shapes. A creature is a union of ellipses: filled solid (pixel: body colour with a lit top rim;
 *      ink: a near-black cut-out), then (ink) outlined along the union's outer edge only. ---------------- */

typedef struct {
	float x, y, ax, ay, bx, by, i00, i01, i10, i11;
} ell_t;

static void ell_mk(ell_t *e, float x, float y, float ax, float ay, float bx, float by)
{
	float det = ax * by - bx * ay;
	if (fabsf(det) < 1e-10f)
		det = det < 0 ? -1e-10f : 1e-10f;
	e->x = x;
	e->y = y;
	e->ax = ax;
	e->ay = ay;
	e->bx = bx;
	e->by = by;
	e->i00 = by / det;
	e->i01 = -bx / det;
	e->i10 = -ay / det;
	e->i11 = ax / det;
}

static inline int ell_in(const ell_t *e, float x, float y, float g)
{
	float dx = x - e->x, dy = y - e->y;
	float u = e->i00 * dx + e->i01 * dy, v = e->i10 * dx + e->i11 * dy;
	return u * u + v * v < g;
}

/* a creature's local frame: x forward (units of its size), y up from its feet, pitched by rot */
typedef struct {
	float X, Y, kx, ky, cr, sr;
} xf_t;

static xf_t xf_mk(float X, float Y, float d, float k, float rot)
{
	xf_t t = {X, Y, d * k, k, cosf(rot), sinf(rot)};
	return t;
}

static inline void xf_vec(const xf_t *t, float lx, float ly, float *x, float *y)
{
	float rx = lx * t->cr - ly * t->sr, ry = lx * t->sr + ly * t->cr;
	*x = t->kx * rx;
	*y = -t->ky * ry;
}

static inline void xf_pt(const xf_t *t, float lx, float ly, float *x, float *y)
{
	xf_vec(t, lx, ly, x, y);
	*x += t->X;
	*y += t->Y;
}

static void xf_ell(const xf_t *t, ell_t *e, float lx, float ly, float rx, float ry, float ang)
{
	float x, y, ax, ay, bx, by;
	xf_pt(t, lx, ly, &x, &y);
	xf_vec(t, cosf(ang) * rx, sinf(ang) * rx, &ax, &ay);
	xf_vec(t, -sinf(ang) * ry, cosf(ang) * ry, &bx, &by);
	ell_mk(e, x, y, ax, ay, bx, by);
}

static void xf_cap(const xf_t *t, ell_t *e, float x0, float y0, float x1, float y1, float w)
{
	float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy) + 1e-6f;
	xf_ell(t, e, 0.5f * (x0 + x1), 0.5f * (y0 + y1), 0.5f * l + 0.3f * w, w, atan2f(dy, dx));
}

static void xf_line(us_canvas *c, const xf_t *t, float x0, float y0, float x1, float y1, float v, uint8_t m)
{
	float ax, ay, bx, by;
	xf_pt(t, x0, y0, &ax, &ay);
	xf_pt(t, x1, y1, &bx, &by);
	if (c->crisp)
		oline(c, ax, ay, bx, by, 0.5f / c->s, v, m);
	else
		wline(c, ax, ay, bx, by, 0.0011f, v, m);
}

static void xf_tri(us_canvas *c, const xf_t *t, float x0, float y0, float x1, float y1, float x2, float y2, float v, uint8_t m)
{
	float ax, ay, bx, by, cx, cy;
	xf_pt(t, x0, y0, &ax, &ay);
	xf_pt(t, x1, y1, &bx, &by);
	xf_pt(t, x2, y2, &cx, &cy);
	if (c->crisp) {
		otri(c, ax, ay, bx, by, cx, cy, v, m);
		return;
	}
	wline(c, ax, ay, cx, cy, 0.0011f, v, m);
	wline(c, bx, by, cx, cy, 0.0011f, v * 0.8f, m);
}

static void blob(us_canvas *c, const ell_t *e, int n, float fill, float line, uint8_t m)
{
	float S = c->s;
	int crisp = c->crisp;
	float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
	for (int i = 0; i < n; i++) {
		float rx = fabsf(e[i].ax) + fabsf(e[i].bx), ry = fabsf(e[i].ay) + fabsf(e[i].by);
		x0 = fminf(x0, e[i].x - rx);
		x1 = fmaxf(x1, e[i].x + rx);
		y0 = fminf(y0, e[i].y - ry);
		y1 = fmaxf(y1, e[i].y + ry);
	}
	int px0 = (int)floorf(x0 * S), px1 = (int)ceilf(x1 * S), py0 = (int)floorf(y0 * S), py1 = (int)ceilf(y1 * S);
	if (px0 < 0) px0 = 0;
	if (py0 < 0) py0 = 0;
	if (px1 > c->w - 1) px1 = c->w - 1;
	if (py1 > c->h - 1) py1 = c->h - 1;
	if (px1 - px0 > 400 || py1 - py0 > 400)
		return;
	for (int py = (crisp || fill >= 0) ? py0 : py1 + 1; py <= py1; py++) { /* (ink, fill < 0: outline only) */
		float y = (py + 0.5f) / S;
		for (int px = px0; px <= px1; px++) {
			float x = (px + 0.5f) / S;
			int in = 0;
			for (int i = 0; i < n && !in; i++)
				in = ell_in(&e[i], x, y, 1.0f);
			if (!in)
				continue;
			float v = fill;
			if (crisp) {
				int up = 0;
				for (int i = 0; i < n && !up; i++)
					up = ell_in(&e[i], x, y - 1.0f / S, 1.0f);
				if (!up)
					v = us_sat(fill * 1.3f + 0.05f);
			}
			size_t k = (size_t)py * c->w + px;
			c->ink[k] = (uint8_t)(us_sat(v) * 255.0f);
			c->mat[k] = m;
		}
	}
	if (crisp || line < 0.004f)
		return;
	for (int i = 0; i < n; i++) {
		const ell_t *q = &e[i];
		float per = (sqrtf(q->ax * q->ax + q->ay * q->ay) + sqrtf(q->bx * q->bx + q->by * q->by)) * US_PI * S;
		int ns = (int)(per / 3.0f);
		ns = ns < 10 ? 10 : ns > 48 ? 48 : ns;
		float pxp = q->x + q->ax, pyp = q->y + q->ay;
		for (int k = 1; k <= ns; k++) {
			float a = US_TAU * k / ns, ca = cosf(a), sa = sinf(a);
			float xx = q->x + ca * q->ax + sa * q->bx, yy = q->y + ca * q->ay + sa * q->by;
			float mx = 0.5f * (xx + pxp), my = 0.5f * (yy + pyp);
			int hid = 0;
			for (int j = 0; j < n && !hid; j++)
				if (j != i)
					hid = ell_in(&e[j], mx, my, 0.92f);
			if (!hid)
				wline(c, pxp, pyp, xx, yy, 0.0011f, line, m);
			pxp = xx;
			pyp = yy;
		}
	}
}

/* ---- life: the hunter. By day a kestrel hangs on the wind facing into it, wings flickering, head dead
 *      still; by night a pale barn owl does the same on softer, slower wings. It slides to a new spot now
 *      and then; a kick's gust jostles it; a hard snare and it stoops into the grass and climbs back out. -- */

static void hunt_spot(st_t *s, float *x, float *y)
{
	us_rng *r = &s->rng;
	float mid = 0.5f * (s->tree_u + s->kite_u);
	*x = mid + us_randr(r, -0.09f, 0.09f);
	*y = us_randr(r, 0.33f, 0.41f);
}

static void hunter(st_t *s, us_canvas *c, const us_ctx *ctx, float A, float sgn, float dayv, int snapped, int kicked)
{
	us_rng *r = &s->rng;
	float dt = ctx->dt;
	int want_owl = ctx->w->daylight < 0.3f;
	if (!s->life_init) {
		s->hk_owl = want_owl;
		hunt_spot(s, &s->hk_x, &s->hk_y);
		s->hk_t = us_randr(r, 4.0f, 10.0f);
		s->hk_d = -sgn;
	}
	s->hk_cool -= dt;
	/* a kick's gust: a sharp jolt it recovers from at once */
	if (kicked)
		s->hk_j = 1.0f;
	s->hk_j = us_approach(s->hk_j, 0.0f, 0.07f, ctx->real_dt);
	switch (s->hk_st) {
	case 0: /* hovering */
		s->hk_t -= dt;
		if ((snapped && ctx->a->snare > 0.45f && s->hk_cool <= 0) || (want_owl != s->hk_owl && s->hk_cool <= -8.0f)) {
			s->hk_st = 2; /* the stoop */
			s->hk_t = 0;
			s->hk_sx = s->hk_x;
			s->hk_sy = s->hk_y;
			s->hk_tx = s->hk_x - sgn * us_randr(r, 0.01f, 0.04f);
			s->hk_ty = ridge(s, 1, s->hk_tx * A, A) + us_randr(r, 0.08f, 0.2f) * lay_drop[1];
			s->hk_cool = 6.0f;
		} else if (s->hk_t <= 0) {
			s->hk_st = 1;
			s->hk_t = 0;
			s->hk_sx = s->hk_x;
			s->hk_sy = s->hk_y;
			hunt_spot(s, &s->hk_tx, &s->hk_ty);
			s->hk_dur = us_randr(r, 2.2f, 3.5f);
		}
		break;
	case 1: /* sliding across to a new spot */
	case 4: /* climbing back up out of the grass */
		s->hk_t += dt / s->hk_dur;
		if (s->hk_t >= 1.0f) {
			s->hk_st = 0;
			s->hk_t = us_randr(r, 6.0f, 15.0f);
			s->hk_x = s->hk_tx;
			s->hk_y = s->hk_ty;
		} else {
			float e = s->hk_st == 4 ? 1.0f - (1.0f - s->hk_t) * (1.0f - s->hk_t) : us_smooth(0.0f, 1.0f, s->hk_t);
			float nx = us_lerp(s->hk_sx, s->hk_tx, e), ny = us_lerp(s->hk_sy, s->hk_ty, e) - (s->hk_st == 1 ? 0.03f * sinf(US_PI * s->hk_t) : 0.0f);
			if (fabsf(nx - s->hk_x) > 1e-5f)
				s->hk_d = nx > s->hk_x ? 1.0f : -1.0f;
			s->hk_x = nx;
			s->hk_y = ny;
		}
		break;
	case 2: /* the stoop: wings folded, faster and faster */
		s->hk_t += dt / 0.5f;
		if (s->hk_t >= 1.0f) {
			s->hk_st = 3;
			s->hk_hold = us_randr(r, 0.7f, 1.4f);
			s->hk_x = s->hk_tx;
			s->hk_y = s->hk_ty;
			/* grass and seed thrown up where it strikes */
			for (int k = 0; k < 14 && s->nfk < MAX_FLAKE; k++) {
				flake_t *q = &s->fk[s->nfk++];
				q->x = q->px = s->hk_x * A + us_randn(r) * 0.006f;
				q->y = q->py = s->hk_y;
				q->vx = us_randn(r) * 0.12f + sgn * 0.05f;
				q->vy = -us_randr(r, 0.1f, 0.3f);
				q->life = us_randr(r, 0.5f, 0.8f);
				q->sz = 0.0016f;
				q->mat = US_MAT_ACCENT;
			}
		} else {
			float e = s->hk_t * s->hk_t;
			s->hk_x = us_lerp(s->hk_sx, s->hk_tx, e);
			s->hk_y = us_lerp(s->hk_sy, s->hk_ty, e);
		}
		break;
	case 3: /* down in the grass (the look can change while it is out of sight) */
		s->hk_hold -= dt;
		if (s->hk_hold <= 0) {
			s->hk_owl = want_owl;
			s->hk_st = 4;
			s->hk_t = 0;
			s->hk_sx = s->hk_x;
			s->hk_sy = s->hk_y;
			hunt_spot(s, &s->hk_tx, &s->hk_ty);
			s->hk_dur = us_randr(r, 1.8f, 2.6f);
		}
		return;
	}
	if (s->hk_st == 0)
		s->hk_d = -sgn; /* hanging into the wind */
	/* ---- draw ---- */
	int owl = s->hk_owl, crisp = c->crisp;
	float k = (crisp ? 0.046f : 0.03f) * (owl ? 1.1f : 1.0f);
	/* a kick's gust knocks it off its spot; it works straight back */
	float x = s->hk_x * A + sgn * 0.02f * s->hk_j, y = s->hk_y - 0.012f * s->hk_j; /* (otherwise dead still on the air) */
	float vis = owl ? 0.85f : 0.62f; /* the barn owl shows pale in the dark */
	float fill = crisp ? (owl ? 0.7f : 0.42f) * dayv : 0.015f, line = us_sat(vis * dayv * 1.05f);
	uint8_t m = US_MAT_ACCENT;
	float d = s->hk_d;
	int st = s->hk_st;
	float rot = 0.0f;
	if (st == 0)
		rot = 0.55f; /* hovering: body raised, tail hanging */
	else if (st == 2)
		rot = -1.1f - 0.2f * s->hk_t;
	else if (st == 4)
		rot = 0.35f;
	xf_t t = xf_mk(x, y, d, k, rot);
	if (st == 0) /* the flicker: fast shallow beats, faster when the wind is up and a kick has jostled it */
		s->hk_flap += dt * (owl ? 9.0f : 15.0f + 6.0f * s->breeze + 10.0f * s->hk_j);
	else if (st != 2)
		s->hk_flap += dt * (owl ? 6.0f : 10.0f);
	/* in a strong wind it hangs almost still on the air, wings barely trembling; in light air it must beat;
	 * a kick's gust knocks it about and sets it beating hard */
	float famp = st == 0 ? us_sat(0.2f + 0.8f * (1.0f - s->breeze) * (1.0f - s->breeze) + 1.2f * s->hk_j) : 1.0f;
	float fl = sinf(s->hk_flap) * famp;
	if (owl && st != 2) {
		/* the barn owl, seen face on: a pale heart-shaped face and two dark eyes, broad rounded wings raised
		 * in a shallow V and beating, legs let down ready to drop */
		xf_t f = xf_mk(x, y, 1.0f, k, 0.0f);
		float el = st == 0 ? 0.55f + 0.32f * fl + 0.25f * s->hk_j : 0.2f + 0.75f * fl;
		for (int sd = -1; sd <= 1; sd += 2) {
			ell_t w;
			float ca = cosf(el), sa = sinf(el);
			xf_cap(&f, &w, sd * 0.1f, 0.12f, sd * (0.1f + 1.05f * ca), 0.12f + 1.05f * sa, 0.21f);
			blob(c, &w, 1, crisp ? fill * 0.85f : 0.015f, line * 0.85f, m);
		}
		for (int sd = -1; sd <= 1; sd += 2)
			xf_line(c, &f, sd * 0.06f, -0.26f, sd * 0.09f, -0.58f, crisp ? fill * 0.9f : line * 0.75f, m);
		ell_t e[2];
		xf_ell(&f, &e[0], 0.0f, 0.0f, 0.2f, 0.3f, 0.0f);
		xf_ell(&f, &e[1], 0.0f, 0.38f, 0.27f, 0.23f, 0.0f);
		blob(c, e, 2, fill, line, m);
		/* the face disc and the eyes */
		if (crisp) {
			ell_t fd;
			xf_ell(&f, &fd, 0.0f, 0.34f, 0.17f, 0.15f, 0.0f);
			blob(c, &fd, 1, us_sat(fill * 1.25f), 0.0f, m);
		} else {
			xf_line(c, &f, 0.0f, 0.5f, -0.16f, 0.42f, line * 0.7f, m);
			xf_line(c, &f, -0.16f, 0.42f, 0.0f, 0.2f, line * 0.7f, m);
			xf_line(c, &f, 0.0f, 0.5f, 0.16f, 0.42f, line * 0.7f, m);
			xf_line(c, &f, 0.16f, 0.42f, 0.0f, 0.2f, line * 0.7f, m);
		}
		for (int sd = -1; sd <= 1; sd += 2) {
			float ex, ey;
			xf_pt(&f, sd * 0.07f, 0.37f, &ex, &ey);
			if (crisp)
				oput(c, (int)(ex * c->s), (int)(ey * c->s), 0.05f, US_MAT_BG);
			else
				us_point(c, ex, ey, 0.0009f, line * 0.5f, m);
		}
		return;
	}
	/* wings: hovering they sweep high over the back, in flight they beat up and down, stooping they are
	 * folded tight along the body */
	float wl = owl ? 0.95f : 1.1f;
	float ta[2], tl[2]; /* far, near wing: angle in the bird's frame and length */
	if (st == 0) {
		/* hovering: the wings stand in a V over the back, the near one swept back, the far one forward,
		 * flickering; a kick's gust flares them */
		float fl2 = sinf(s->hk_flap - 0.6f) * famp;
		ta[1] = 1.95f - rot + (owl ? 0.3f : 0.38f) * fl + 0.3f * s->hk_j;
		ta[0] = 1.25f - rot + (owl ? 0.3f : 0.38f) * fl2 - 0.2f * s->hk_j;
		tl[1] = wl;
		tl[0] = wl * 0.8f;
	} else if (st == 2) {
		ta[0] = ta[1] = 3.0f;
		tl[0] = tl[1] = 0.7f;
	} else {
		/* flying: up-and-down beats, foreshortened as the wing passes level */
		float fl2 = sinf(s->hk_flap - 0.5f);
		ta[1] = 1.85f - 1.85f * (1.0f - fl);
		ta[0] = 1.85f - 1.85f * (1.0f - fl2);
		tl[1] = wl * (0.4f + 0.6f * fabsf(sinf(ta[1])));
		tl[0] = wl * 0.85f * (0.4f + 0.6f * fabsf(sinf(ta[0])));
	}
	for (int w = 0; w < 2; w++) { /* far wing first (a beat behind, dimmer), then the body, then the near wing */
		if (w == 1) {
			ell_t e[5];
			int n = 0;
			xf_ell(&t, &e[n++], 0.0f, 0.0f, 0.36f, owl ? 0.17f : 0.12f, 0.0f);
			xf_ell(&t, &e[n++], owl ? 0.36f : 0.33f, owl ? 0.08f : 0.04f, owl ? 0.22f : 0.12f, owl ? 0.21f : 0.11f, 0.0f);
			if (owl && st != 2) /* legs let down, talons ready */
				for (int q = 0; q < 2; q++)
					xf_line(c, &t, 0.06f + 0.06f * q, -0.12f, 0.02f + 0.08f * q - 0.06f * rot, -0.5f, crisp ? fill : line * 0.8f, m);
			if (owl)
				xf_cap(&t, &e[n++], -0.3f, -0.02f, -0.48f, -0.06f, 0.08f);
			else { /* the kestrel's long tail, fanned when it hovers */
				xf_cap(&t, &e[n++], -0.28f, -0.01f, -0.78f, -0.03f, 0.06f + (st == 0 ? 0.03f : 0.0f));
				if (st == 0)
					xf_ell(&t, &e[n++], -0.72f, -0.03f, 0.12f, 0.13f, 0.0f);
			}
			blob(c, e, n, fill, line, m);
			if (owl && !crisp) /* the pale heart-shaped face */
				xf_line(c, &t, 0.5f, 0.2f, 0.52f, -0.04f, line * 0.6f, m);
			if (!owl && !crisp) /* the hooked bill */
				xf_line(c, &t, 0.44f, 0.04f, 0.5f, 0.0f, line, m);
		}
		float a = ta[w], L = tl[w];
		float tx = 0.04f + cosf(a) * L, ty = 0.06f + sinf(a) * L;
		float v = w == 0 ? 0.65f : 1.0f;
		if (owl) { /* broad rounded wings */
			ell_t e;
			xf_cap(&t, &e, 0.06f, 0.06f, tx, ty, 0.14f);
			blob(c, &e, 1, crisp ? fill * (w == 0 ? 0.8f : 1.0f) : 0.015f, line * v, m);
		} else { /* long pointed wings: a narrow triangle from the shoulder out to the tip */
			xf_tri(c, &t, 0.16f, 0.08f, -0.12f, 0.0f, tx, ty, crisp ? fill * (w == 0 ? 0.8f : 1.0f) : line * v, m);
			if (!crisp)
				xf_line(c, &t, 0.16f, 0.08f, -0.12f, 0.0f, line * v * 0.6f, m);
		}
	}
	float ex, ey;
	xf_pt(&t, 0.37f, 0.07f, &ex, &ey);
	if (crisp)
		oput(c, (int)(ex * c->s), (int)(ey * c->s), owl ? 0.2f : 0.9f, owl ? US_MAT_BG : US_MAT_LIGHT);
	else
		us_point(c, ex, ey, 0.0011f, owl ? 0.3f : 0.8f * dayv, US_MAT_LIGHT);
}

/* ---- life: a hare on the near slope. It sits, grazes, lollops a few yards; a snare sits it bolt
 *      upright with its ears straight up; a hard one and it is off, jinking away at full stretch,
 *      to come back later from the other side. A kick flicks its ears. -------------------------------- */

static void hare_update(st_t *s, const us_ctx *ctx, int snapped, int kicked)
{
	us_rng *r = &s->rng;
	float dt = ctx->dt, rdt = ctx->real_dt, A = ctx->aspect;
	float snare = us_sat(ctx->a->snare);
	if (!s->life_init) {
		s->hr_u = us_clampf(s->kite_u + (s->tree_u < 0.5f ? 0.16f : -0.16f), 0.1f, 0.9f);
		s->hr_d = us_randr(r, 0.08f, 0.16f);
		s->hr_dir = us_rand(r) < 0.5f ? -1.0f : 1.0f;
		s->hr_t = us_randr(r, 2.0f, 5.0f);
	}
	s->hr_cool -= dt;
	if (s->hr_st != 3 && s->hr_st != 4 && snapped && snare > 0.15f) {
		if (snare > 0.55f && s->hr_cool <= 0 && us_rand(r) < 0.35f) {
			s->hr_st = 3; /* bolt */
			s->hr_dir = us_rand(r) < 0.5f ? -1.0f : 1.0f;
			s->hr_fd = -0.12f;
			s->hr_cool = 18.0f;
			s->hr_hold = 0;
		} else
			s->hr_hold = 0.6f + 0.8f * snare;
	}
	if (kicked)
		s->hr_ear = 1.0f;
	s->hr_ear = us_approach(s->hr_ear, 0.0f, 0.12f, rdt);
	s->hr_hold -= rdt;
	s->hr_alert = us_approach(s->hr_alert, s->hr_hold > 0 ? 1.0f : 0.0f, s->hr_hold > 0 ? 0.04f : 0.5f, rdt);
	s->hr_t -= dt * (1.0f - 0.85f * s->hr_alert);
	float tsp = 0;
	switch (s->hr_st) {
	case 0: /* sitting, looking about */
		if (s->hr_t <= 0) {
			s->hr_st = us_rand(r) < 0.6f ? 1 : 2;
			s->hr_t = s->hr_st == 1 ? us_randr(r, 4.0f, 10.0f) : us_randr(r, 1.2f, 3.0f);
			if (s->hr_st == 2 && us_rand(r) < 0.4f)
				s->hr_dir = -s->hr_dir;
		}
		break;
	case 1: /* grazing */
		if (s->hr_t <= 0) {
			s->hr_st = 0;
			s->hr_t = us_randr(r, 2.5f, 7.0f);
		}
		break;
	case 2: /* a slow lollop */
		tsp = 0.3f;
		if (s->hr_t <= 0) {
			s->hr_st = 0;
			s->hr_t = us_randr(r, 2.5f, 7.0f);
		}
		break;
	case 3: /* bolting */
		tsp = 1.0f;
		if (us_rand(r) < dt * 0.5f)
			s->hr_fd = -s->hr_fd * 0.5f; /* a jink */
		if (s->hr_u < -0.1f || s->hr_u > 1.1f) {
			s->hr_st = 4;
			s->hr_t = us_randr(r, 6.0f, 14.0f);
		}
		break;
	case 4: /* gone; it comes back in from an edge at a lollop */
		if (s->hr_t <= 0) {
			s->hr_dir = us_rand(r) < 0.5f ? -1.0f : 1.0f;
			s->hr_u = s->hr_dir > 0 ? -0.06f : 1.06f;
			s->hr_d = us_randr(r, 0.08f, 0.2f);
			s->hr_st = 2;
			s->hr_t = us_randr(r, 4.0f, 7.0f);
		}
		break;
	}
	/* stay in the field while at ease */
	if (s->hr_st == 2 && ((s->hr_u < 0.06f && s->hr_dir < 0) || (s->hr_u > 0.94f && s->hr_dir > 0)))
		s->hr_dir = -s->hr_dir;
	s->hr_spd = us_approach(s->hr_spd, tsp * (s->hr_st == 3 ? 1.0f : 1.0f - s->hr_alert), s->hr_st == 3 ? 0.12f : 0.3f, dt);
	float sc = us_lerp(lay_s0[2], lay_s1[2], s->hr_d);
	float v = s->hr_spd * (s->hr_st == 3 ? 0.55f : 0.1f) * sc / 0.45f;
	s->hr_u += s->hr_dir * v * dt / A;
	if (s->hr_st == 3)
		s->hr_d = us_clampf(s->hr_d + s->hr_fd * dt, 0.04f, 0.3f);
	/* the gait: bounds when bolting, slow hops at a lollop */
	s->hr_ph += dt * (s->hr_st == 3 ? 13.0f : 7.0f) * us_sat(s->hr_spd * 4.0f);
	s->hr_graze = us_approach(s->hr_graze, s->hr_st == 1 && s->hr_alert < 0.3f ? 1.0f : 0.0f, 0.4f, dt);
}

static void hare_draw(st_t *s, us_canvas *c, const us_ctx *ctx, float A, float dayv)
{
	if (s->hr_st == 4)
		return;
	int crisp = c->crisp;
	float x = s->hr_u * A;
	float y = ridge(s, 2, x, A) + s->hr_d * lay_drop[2];
	float sc = us_lerp(lay_s0[2], lay_s1[2], s->hr_d);
	float k = 0.11f * sc * (crisp ? 1.35f : 1.0f);
	float run = us_sat(s->hr_spd), bolt = s->hr_st == 3 ? run : 0.0f, al = s->hr_alert, g = s->hr_graze * (1.0f - al);
	float ph = s->hr_ph;
	float hop = us_sat(sinf(ph)) * (0.12f * run + 0.25f * bolt);
	xf_t t = xf_mk(x, y - hop * k, s->hr_dir, k, 0.0f);
	float fill = crisp ? 0.68f * dayv : 0.015f, line = us_sat(0.75f * dayv);
	uint8_t m = US_MAT_EARTH;
	/* a bolting hare stretches long and low; sitting it is a hunched oval on long hind feet */
	float st = bolt * (0.5f + 0.5f * sinf(ph + 1.2f)); /* gathered .. extended */
	ell_t e[7];
	int n = 0;
	float by = 0.32f + 0.1f * al + 0.08f * bolt, bang = 0.32f + 0.35f * al - 0.25f * g - 0.3f * bolt;
	xf_ell(&t, &e[n++], -0.04f + 0.04f * al, by, 0.38f + 0.14f * st, 0.21f - 0.04f * bolt, bang);
	xf_ell(&t, &e[n++], -0.24f + 0.06f * bolt, 0.26f + 0.06f * bolt, 0.24f, 0.21f, 0.0f);
	float hx = 0.32f + 0.15f * g + 0.22f * bolt - 0.02f * al, hy = 0.56f - 0.38f * g + 0.12f * al - 0.08f * bolt;
	xf_ell(&t, &e[n++], hx, hy, 0.16f, 0.12f, -0.2f - 0.5f * g);
	/* ears: straight up when alert, laid along the back when running, back when grazing */
	float tw = 0.15f * s->hr_ear * sinf((float)ctx->t * 30.0f);
	float ea = 1.85f - 0.2f * al + 0.75f * bolt + 0.5f * g + tw, el = 0.5f + 0.06f * al;
	xf_cap(&t, &e[n++], hx - 0.04f, hy + 0.07f, hx - 0.04f + cosf(ea) * el, hy + 0.07f + sinf(ea) * el, 0.055f);
	xf_cap(&t, &e[n++], hx + 0.01f, hy + 0.08f, hx + 0.01f + cosf(ea - 0.18f) * el * 0.95f, hy + 0.08f + sinf(ea - 0.18f) * el * 0.95f, 0.05f);
	blob(c, e, n, fill, line, m);
	/* the white scut, flashing as it runs */
	ell_t sc_e;
	xf_ell(&t, &sc_e, -0.44f, 0.36f + 0.06f * bolt, 0.07f, 0.07f, 0.0f);
	blob(c, &sc_e, 1, crisp ? 0.85f * dayv : 0.015f, us_sat(0.8f * dayv), US_MAT_ACCENT);
	/* legs: long hind feet flat under it when sitting; a bound throws them forward then back */
	float lv = crisp ? fill : line;
	float fsw = sinf(ph) * (0.25f * run + 0.35f * bolt);
	xf_line(c, &t, -0.3f, 0.12f, -0.36f + 0.5f * fsw + 0.3f * st, 0.0f, lv, m);
	xf_line(c, &t, -0.36f + 0.5f * fsw + 0.3f * st, 0.0f, -0.02f + 0.5f * fsw + 0.3f * st, 0.0f, lv, m);
	xf_line(c, &t, 0.24f + 0.1f * bolt, 0.2f, 0.26f - 0.6f * fsw + 0.25f * bolt, 0.0f + 0.1f * bolt, lv, m);
	float ex, ey;
	xf_pt(&t, hx + 0.06f, hy + 0.03f, &ex, &ey);
	if (crisp)
		oput(c, (int)(ex * c->s), (int)(ey * c->s), 0.12f, US_MAT_BG);
	else
		us_point(c, ex, ey, 0.001f, 0.6f * dayv, US_MAT_LIGHT);
}

/* ---- life: butterflies by day, fluttering low over the grass and tossed by the gusts ---------------- */

static void butterflies(st_t *s, us_canvas *c, const us_ctx *ctx, float A, float sgn, int snapped)
{
	us_rng *r = &s->rng;
	us_world *w = ctx->w;
	float dt = ctx->dt, t = (float)ctx->t;
	float vis = us_smooth(0.12f, 0.4f, w->daylight) * (1.0f - us_world_winter(w));
	float warm = us_sat(0.3f + 0.5f * us_world_season_w(w, 1) + 0.3f * us_world_season_w(w, 0) + w->insects);
	int nb = 2 + (int)((NBF - 2) * warm * (0.4f + 0.8f * ctx->p->density));
	if (nb > NBF)
		nb = NBF;
	for (int i = 0; i < NBF; i++) {
		if (!s->life_init) {
			s->bf_x[i] = us_rand(r) * A;
			s->bf_y[i] = us_randr(r, 0.6f, 0.86f);
			s->bf_ph[i] = us_rand(r) * 10.0f;
			s->bf_fr[i] = us_randr(r, 9.0f, 13.0f);
			s->bf_kind[i] = (uint8_t)(us_rand(r) * 4);
		}
		float x = s->bf_x[i], y = s->bf_y[i];
		/* erratic flutter on a lazy drift downwind; a gust front picks it up and flings it along */
		float fb = front_at(s, x / A, 0.6f);
		float n1 = us_noise2(i * 13.1f, t * 0.9f), n2 = us_noise2(i * 7.7f + 40.0f, t * 1.1f);
		float tvx = sgn * (0.015f + 0.08f * s->breeze) + 0.09f * n1 + 0.35f * fb;
		float lo = 0.6f, hi = 0.88f;
		float tvy = 0.07f * n2 + (y < lo ? 0.08f : y > hi ? -0.08f : 0.0f) - 0.12f * fabsf(fb);
		s->bf_vx[i] += (tvx - s->bf_vx[i]) * us_sat(dt * 2.5f);
		s->bf_vy[i] += (tvy - s->bf_vy[i]) * us_sat(dt * 3.0f);
		if (snapped && us_rand(r) < 0.8f) { /* a snare: they scatter up */
			s->bf_vy[i] -= us_randr(r, 0.1f, 0.25f);
			s->bf_vx[i] += us_randn(r) * 0.12f;
		}
		x += s->bf_vx[i] * dt;
		y += s->bf_vy[i] * dt + 0.036f * sinf(s->bf_ph[i] * 0.5f) * dt;
		if (x > A + 0.05f) x -= A + 0.1f;
		if (x < -0.05f) x += A + 0.1f;
		y = us_clampf(y, 0.45f, 0.98f);
		s->bf_x[i] = x;
		s->bf_y[i] = y;
		s->bf_ph[i] += dt * s->bf_fr[i] * (1.0f + 0.5f * fabsf(fb));
		if (i >= nb || vis < 0.02f)
			continue;
		/* lower in the frame is nearer the viewer: a little larger */
		float sz = (0.006f + 0.006f * us_smooth(0.55f, 0.95f, y)) * (c->crisp ? 1.5f : 1.0f);
		float open = fabsf(sinf(s->bf_ph[i]));
		static const uint8_t mats[4] = {US_MAT_ACCENT, US_MAT_LIGHT, US_MAT_BLOOM, US_MAT_WATER};
		uint8_t m = mats[s->bf_kind[i]];
		float v = us_sat((s->bf_kind[i] == 0 ? 0.85f : 0.8f) * vis);
		for (int sd = -1; sd <= 1; sd += 2) {
			float af = -US_PI * 0.5f + sd * (0.2f + 1.15f * open), ah = af + sd * 0.75f;
			if (c->crisp) {
				float fx = x + cosf(af) * sz, fy = y + sinf(af) * sz;
				us_line(c, x, y, fx, fy, 0.004f, v, m);
				us_point(c, x + cosf(ah) * sz * 0.6f, y + sinf(ah) * sz * 0.6f, 0.002f, v * 0.9f, m);
			} else {
				leafshape(c, x, y, af, sz, sz * 0.42f, v, m);
				leafshape(c, x, y, ah, sz * 0.7f, sz * 0.32f, v * 0.85f, m);
			}
		}
		if (!c->crisp)
			wline(c, x, y - sz * 0.2f, x, y + sz * 0.45f, 0.0011f, v * 0.7f, US_MAT_ACCENT);
	}
}

/* ---- life: hawthorns on the mid hill, flagged by the prevailing wind: crowns combed out to the lee,
 *      bare on the windward side. White may-blossom in spring, red haws in autumn; a kick's gust bows them. */

static void hawthorn(st_t *s, us_canvas *c, const us_ctx *ctx, int i, float A, float sgn, float gstr, float dayv, float spring,
		     float autumn, float winter)
{
	float dt = ctx->dt, t = (float)ctx->t;
	int crisp = c->crisp;
	float u = s->hw_u[i], x = u * A;
	float d = s->hw_d[i];
	float y = ridge(s, 1, x, A) + d * lay_drop[1];
	float sc = us_lerp(lay_s0[1], lay_s1[1], d) / 0.12f;
	float h = s->hw_h[i] * sc;
	/* the boughs sway on a spring; a kick front bows them */
	float fb = front_at(s, u, us_lerp(lay_s0[1], lay_s1[1], d));
	float target = sgn * (0.05f + 0.25f * gstr * (0.6f + 0.4f * us_noise2(i * 3.3f, t * 0.6f))) + 0.5f * fb;
	s->hw_v[i] += ((target - s->hw_b[i]) * 22.0f - s->hw_v[i] * 4.0f) * dt;
	s->hw_b[i] += s->hw_v[i] * dt;
	float b = s->hw_b[i], L = s->hw_lean;
	float bv = (crisp ? 0.3f : 0.55f) * dayv;
	/* trunk: low and leaning hard to the lee */
	float tx = x + L * h * 0.28f + b * h * 0.1f, ty = y - h * 0.4f;
	if (crisp)
		oline(c, x, y, tx, ty, 1.5f / c->s, bv, US_MAT_STEM);
	else {
		wline(c, x - 0.0015f, y, tx, ty, 0.0011f, bv, US_MAT_STEM);
		wline(c, x + 0.0015f, y, tx + 0.001f, ty, 0.0011f, bv * 0.8f, US_MAT_STEM);
	}
	/* the crown: a lopsided mass, its windward side shorn, combed out to the lee */
	/* the crown rises from a low, shorn windward shoulder to a high streaming lee side */
	float cx = tx, cy = ty;
	ell_t e[4];
	int n = 0;
	float leaf = 1.0f - winter;
	static const float CX[4] = {-0.18f, 0.08f, 0.34f, 0.56f}, CY[4] = {0.06f, 0.2f, 0.28f, 0.24f};
	static const float RX[4] = {0.17f, 0.24f, 0.25f, 0.2f}, RY[4] = {0.15f, 0.22f, 0.22f, 0.15f};
	for (int q = 0; q < 4; q++) {
		float f = q / 3.0f;
		float ex = cx + L * h * CX[q] + b * h * (0.1f + 0.3f * f), ey = cy - h * CY[q];
		float lk = 0.8f + 0.2f * leaf;
		ell_mk(&e[n++], ex, ey, h * RX[q] * lk, 0.0f, -L * h * RY[q] * 0.3f * lk, h * RY[q] * lk);
	}
	if (winter < 0.7f) {
		blob(c, e, n, crisp ? 0.26f * dayv : -1.0f, 0.42f * dayv * leaf, US_MAT_LEAF);
		if (!crisp) /* ink: the foliage as short strokes combed downwind */
			for (int q = 0; q < 28; q++) {
				uint32_t hh = (uint32_t)q * 2246822519u + (uint32_t)i * 31u;
				const ell_t *E = &e[q & 3];
				float an = ((hh >> 8) & 1023) / 1023.0f * US_TAU, rr = sqrtf(((hh >> 18) & 1023) / 1023.0f) * 0.8f;
				float px = E->x + cosf(an) * rr * E->ax + sinf(an) * rr * E->bx, py = E->y + sinf(an) * rr * E->by;
				float ln = h * 0.1f;
				wline(c, px, py, px + L * ln + b * ln * 0.5f, py - ln * 0.25f, 0.0011f, 0.38f * dayv * leaf, US_MAT_LEAF);
			}
	}
	/* limbs (always; they are all you see in winter) */
	for (int q = 0; q < 3; q++) {
		float ex = e[q + 1].x + L * h * 0.08f, ey = e[q + 1].y - h * 0.02f * q;
		if (crisp)
			oline(c, tx, ty, ex, ey, 0.5f / c->s, bv * (winter > 0.5f ? 1.2f : 0.9f), US_MAT_STEM);
		else
			wline(c, tx, ty, ex, ey, 0.0011f, bv * (winter > 0.5f ? 1.0f : 0.6f), US_MAT_STEM);
	}
	/* may blossom / haws: dots over the crown */
	float bl = us_sat(spring * 1.4f - 0.3f) * leaf, hw = us_sat(autumn * 1.4f - 0.3f);
	if (bl > 0.05f || hw > 0.05f) {
		int nd = crisp ? 9 : 22;
		for (int q = 0; q < nd; q++) {
			uint32_t hh = (uint32_t)q * 2654435761u + (uint32_t)i * 97u;
			float a = ((hh >> 8) & 1023) / 1023.0f, bq = ((hh >> 18) & 1023) / 1023.0f;
			const ell_t *E = &e[q % n];
			float an = a * US_TAU, rr = sqrtf(bq) * 0.85f;
			float px = E->x + cosf(an) * rr * E->ax + sinf(an) * rr * E->bx, py = E->y + sinf(an) * rr * E->by;
			if (bl > 0.05f)
				us_point(c, px, py, crisp ? 0.003f : 0.0012f, bl * 0.8f * dayv, US_MAT_ACCENT);
			else if ((q & 1) == 0)
				us_point(c, px, py, crisp ? 0.003f : 0.0012f, hw * 0.75f * dayv, US_MAT_BLOOM);
		}
	}
}

/* ground fill parameters for this frame */
typedef struct {
	float ax, az, gstr, breeze, shk, light, lt, push, hat, cloud;
	float base[NLAY], band[NLAY], crest[NLAY], hiv[NLAY];
	uint8_t m[NLAY], mh[NLAY];
} fillp_t;

/* gust-band brightness down one column of layer k, from its ridge y0 to where the next layer covers it (y1):
 * the breeze's rolling waves are lighter bands of bent grass, a kick front a bright travelling crest */
static void band_col(const st_t *s, const fillp_t *fp, int k, float x, float A, float y0, float y1, float *out, int nd)
{
	for (int j = 0; j < nd; j++) {
		float y = us_lerp(y0, y1, (float)j / (nd - 1));
		float d = us_sat((y - y0) / lay_drop[k]);
		float sc = us_lerp(lay_s0[k], lay_s1[k], d), Z = 1.0f / sc;
		float X = (x - A * 0.5f) * Z;
		float bend = band_at(s, X * 0.6f, Z * 0.6f, fp->ax, fp->az);
		float fb = us_sat(fabsf(front_at(s, x / A, sc)));
		float sh = 1.0f - fp->shk +
			   fp->shk * us_smooth(-0.3f, 0.3f, us_noise2(X * 0.05f - fp->cloud * 2.0f * fp->ax, Z * 0.08f - fp->cloud * 2.0f * fp->az));
		float g = us_smooth(0.25f, 0.9f, bend) * (0.35f + 0.9f * fp->breeze);
		float v = (fp->base[k] + fp->band[k] * g) * sh * fp->light + fp->crest[k] * fb * fp->light +
			  fp->push * fp->band[k] * 0.15f * fp->light;
		if (k == 0)
			v += fp->hat * fp->band[k] * (0.4f + g); /* hats: the far downs shimmer */
		out[j] = v;
	}
}

static void fill_layer(const st_t *s, us_canvas *c, const fillp_t *fp, int k, float A)
{
	int step = c->crisp ? 2 : 4;
	float S = c->s;
	enum { MAXND = 32 };
	float a[MAXND], b[MAXND], ya0 = 0, ya1 = 0;
	/* depth samples per column: dense on small pixel canvases (no facets), fewer in ink (smooth glow) */
	static const int ndc[NLAY] = {8, 14, 22}, ndi[NLAY] = {8, 10, 14};
	int nd = c->crisp ? ndc[k] : ndi[k];
	for (int gx = 0; gx - step < c->w; gx += step) {
		float x = (gx + 0.5f) / S;
		float yb0 = ridge(s, k, x, A);
		float yb1 = k < NLAY - 1 ? ridge(s, k + 1, x, A) + 2.0f / S : 1.0f + 1.0f / S;
		if (yb1 < yb0 + 1.0f / S)
			yb1 = yb0 + 1.0f / S;
		band_col(s, fp, k, x, A, yb0, yb1, b, nd);
		if (gx > 0)
			for (int px = gx - step; px < gx && px < c->w; px++) {
				float t = (float)(px - (gx - step)) / step;
				float y0 = us_lerp(ya0, yb0, t), y1 = us_lerp(ya1, yb1, t);
				int py0 = (int)floorf(y0 * S + 0.5f), py1 = (int)(y1 * S);
				if (py0 < 0)
					py0 = 0;
				if (py1 > c->h - 1)
					py1 = c->h - 1;
				float inv = (nd - 1) / ((y1 - y0) * S);
				for (int py = py0; py <= py1; py++) {
					float f = (py + 0.5f - y0 * S) * inv;
					if (f < 0)
						f = 0;
					if (f > nd - 1.001f)
						f = nd - 1.001f;
					int j = (int)f;
					f -= j;
					float v = us_lerp(us_lerp(a[j], a[j + 1], f), us_lerp(b[j], b[j + 1], f), t);
					if (py == py0 && c->crisp) /* each layer's crest catches the light: a rim against the layer behind */
						v += 0.14f;
					size_t ii = (size_t)py * c->w + px;
					c->ink[ii] = (uint8_t)(us_sat(v) * 255.0f + 0.5f);
					c->mat[ii] = v > fp->hiv[k] ? fp->mh[k] : fp->m[k];
				}
			}
		memcpy(a, b, sizeof(float) * nd);
		ya0 = yb0;
		ya1 = yb1;
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
	float tl = us_sat(0.06f + 0.86f * a->level + 0.08f * s->swell_s);
	s->light = us_approach(s->light, tl, tl > s->light ? 0.15f : 0.4f, rdt);
	float tdir = a->active > 0.2f ? (a->centroid - 0.5f) * 1.6f : 0.35f * us_noise2((float)ctx->t * 0.02f, 1.3f);
	s->dir = us_approach(s->dir, us_clampf(tdir, -0.9f, 0.9f), 6.0f, rdt);
	s->swell = us_approach(s->swell, us_sat(a->bass * 0.7f + a->sub * 0.4f), 1.2f, rdt);
	float ax = cosf(s->dir), az = sinf(s->dir); /* ax > 0: blowing left -> right */
	float gstr = 0.07f + 0.3f * s->breeze + 0.1f * winter;
	s->flow += dt * (0.045f + 0.1f * s->breeze);
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
		f->amp = (sg > 0 ? 1.2f : 0.72f) * ks * (0.75f + 0.25f * s->breeze);
		f->sgn = sg * sgn;
		f->pos = us_randr(r, -0.05f, 0.1f);
		f->speed = us_randr(r, 4.8f, 5.6f);
		f->width = us_randr(r, 0.09f, 0.13f);
	}
	for (int i = 0; i < MAX_FRONTS; i++) {
		front_t *f = &s->fr[i];
		if (f->amp <= 0.002f)
			continue;
		f->pos += f->speed * rdt;
		f->amp *= expf(-rdt * 5.0f);
		if (f->pos > 1.6f)
			f->amp = 0;
	}
	/* snare -> a sharp flurry: stalks shiver, seeds and petals are torn off, leaves snap, birds startle */
	int snapped = 0;
	s->senv = us_approach(s->senv, 0.0f, 0.07f, rdt);
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
	float lightk = 0.5f + 0.75f * s->light; /* how lit the field is (loudness) */
	hare_update(s, ctx, snapped, kicked == 1);

	/* ---- the tree's spring: pushed by the local wind (fronts included), overshoots and rebounds ---- */
	float tear = 0;
	{
		float sc = us_lerp(lay_s0[1], lay_s1[1], TREE_D), Z = 1.0f / sc;
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

	/* ---- pixel styles by day: banded pale sky first (ink keeps its black ground);
	 *      the hills in front of it overwrite it ---- */
	if (ctx->crisp && day > 0.2f) {
		float f[2 * 6];
		for (int j = 0; j < 6; j++)
			f[2 * j] = f[2 * j + 1] = (0.08f + 0.3f * j / 5.0f) * us_smooth(0.2f, 0.7f, day);
		int keep_blend = c->blend;
		us_field(c, f, 2, 6, 1.0f, US_MAT_WATER, 0, 0, A, HZ);
		c->blend = keep_blend;
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
					wline(c, fp[2 * i], fp[2 * i + 1], fp[2 * i + 2], fp[2 * i + 3], 0.0011f, (0.26f + 0.06f * day) * dayv, US_MAT_WATER);
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
					otri(c, x0, y0, x1, y1, x0, HZ, (day > 0.35f ? 0.22f : 0.5f) * dayv, day > 0.35f ? US_MAT_WATER : US_MAT_BG);
					otri(c, x1, y1, x1, HZ, x0, HZ, (day > 0.35f ? 0.22f : 0.5f) * dayv, day > 0.35f ? US_MAT_WATER : US_MAT_BG);
				}
			}
		}
	}

	/* ---- the ground: three rolling layers, far to near. Each is filled (a solid grass colour in pixel,
	 *      a faint glow in ink) with the gust bands rolling through it, then its blades are drawn. ---- */
	float height = (0.30f + 0.95f * w->growth) * (1.0f - 0.45f * winter) * (1.0f + 0.08f * s->swell);
	float heightC = (0.55f + 0.6f * w->growth) * (1.0f - 0.4f * winter) * (1.0f + 0.08f * s->swell);
	float keep = (0.3f + 0.7f * dens) * (1.0f - 0.55f * winter);
	float lean0 = 0.15f * winter; /* snow-bent */
	float shake = 0;
	float tt = (float)ctx->t;
	float shk = 0.3f * (1.0f - 0.7f * s->light); /* light burns the cloud shadows off; quiet deepens them */
	s->ntip = 0;
	s->tipseen = 0;
	if (s->cache_A != A) { /* clumps and flower drifts are fixed on the ground: compute once per aspect */
		s->cache_A = A;
		for (int row = 0; row < MAX_ROWS; row++) {
			float Z = 1.0f / s->rsc[row] + 3.0f * s->rlay[row];
			for (int k = s->row_start[row]; k < s->row_start[row + 1]; k++) {
				blade_t *b = &s->bl[k];
				float X = (b->u * A - A * 0.5f) * Z;
				b->clump = 0.45f + 0.9f * us_smooth(-0.4f, 0.4f, us_noise2(X * 0.05f + 11.0f, Z * 0.22f + 2.0f));
				b->fpatch = us_smooth(0.05f, 0.45f, us_noise2(X * 0.08f + 40.0f, Z * 0.3f + 7.0f));
			}
		}
	}
	fillp_t fp;
	fp.ax = ax;
	fp.az = az;
	fp.gstr = gstr;
	fp.breeze = s->breeze;
	fp.lt = s->light;
	fp.shk = ctx->crisp ? shk * 0.5f : shk;
	fp.light = dayv * lightk * (0.15f + 0.85f * lightk);
	fp.push = s->kimp * (ctx->crisp ? 1.7f : 4.4f); /* ink: the whole field brightens under the gust; pixel: gently, so it never flashes */
	fp.hat = s->henv;
	fp.cloud = s->cloud_t;
	/* pixel/mono: the land follows the daylight like the sky does; by night it sinks into muted earth and
	 * the gust bands become the moonlit accents */
	float sunk = us_smooth(0.15f, 0.7f, day);
	{
		/* aerial perspective: the far downs hazy and pale, the near slope deep; the season sets the colours */
		static const float cb[NLAY] = {0.4f, 0.3f, 0.17f}, cbd[NLAY] = {0.18f, 0.32f, 0.42f}, ccr[NLAY] = {0.14f, 0.2f, 0.24f};
		static const float ib[NLAY] = {0.08f, 0.065f, 0.045f}, ibd[NLAY] = {0.06f, 0.07f, 0.06f}, icr[NLAY] = {0.08f, 0.11f, 0.1f};
		for (int k = 0; k < NLAY; k++) {
			static const float cbn[NLAY] = {0.26f, 0.2f, 0.13f};
			fp.base[k] = ctx->crisp ? us_lerp(cbn[k], cb[k], sunk) : ib[k];
			fp.band[k] = ctx->crisp ? cbd[k] * (0.72f + 0.28f * sunk) : ibd[k];
			fp.crest[k] = ctx->crisp ? ccr[k] : icr[k];
			fp.hiv[k] = ctx->crisp ? us_lerp(0.34f, 0.6f, sunk) : 0.2f;
			if (winter > 0.5f) {
				fp.m[k] = US_MAT_ACCENT;
				fp.mh[k] = US_MAT_ACCENT;
			} else if (autumn > 0.5f) {
				fp.m[k] = k == 0 ? US_MAT_EARTH : US_MAT_LIGHT;
				fp.mh[k] = k == 0 ? US_MAT_LIGHT : US_MAT_ACCENT;
			} else {
				fp.m[k] = k == 0 ? US_MAT_LEAF : US_MAT_STEM;
				fp.mh[k] = US_MAT_LEAF;
			}
			if (ctx->crisp && sunk < 0.5f && winter <= 0.5f) {
				fp.m[k] = US_MAT_EARTH;
				fp.mh[k] = k == 0 ? US_MAT_LEAF : US_MAT_STEM;
			}
		}
	}
	int tree_done = 0, kite_done = 0, hare_done = 0;
	int haw_done[NHAW] = {0};
	int nhaw = 1 + (int)((NHAW - 1) * dens + 0.5f);
	for (int k = 0; k < NLAY; k++) {
		if (k == 1)
			draw_poplars(s, c, ctx, A, sgn, dayv * lightk, winter);
		fill_layer(s, c, &fp, k, A);
		if (!ctx->crisp) {
			/* ink: the layer's skyline, a fine contour */
			float rv = (0.3f + 0.08f * k) * dayv * lightk;
			float px = 0, py = ridge(s, k, 0, A);
			for (int i = 1; i <= 96; i++) {
				float x = A * i / 96.0f, y = ridge(s, k, x, A);
				wline(c, px, py, x, y, 0.0012f, rv, US_MAT_EARTH);
				px = x;
				py = y;
			}
		}
		if (k >= 1) {
			/* the path: a pale strip of bare earth (pixel), two engraved ruts (ink) */
			float px0 = 0, py0 = 0, pw0 = 0;
			int have = 0;
			for (int i = 0; i <= 16; i++) {
				float d = i / 16.0f, sc = us_lerp(lay_s0[k], lay_s1[k], d), hw;
				float xc = path_at(s, A, k, d, sc, &hw);
				if (xc < -5) {
					have = 0;
					continue;
				}
				float y = ridge(s, k, xc, A) + d * lay_drop[k];
				if (have) {
					if (ctx->crisp) {
						float pv = (0.52f + 0.06f * winter) * dayv * lightk * (0.6f + 0.4f * sunk);
						otri(c, px0 - pw0 * 0.45f, py0, px0 + pw0 * 0.45f, py0, xc + hw * 0.45f, y, pv, US_MAT_EARTH);
						otri(c, px0 - pw0 * 0.45f, py0, xc + hw * 0.45f, y, xc - hw * 0.45f, y, pv, US_MAT_EARTH);
					} else {
						float rv = 0.2f * dayv * lightk;
						wline(c, px0 - pw0 * 0.3f, py0, xc - hw * 0.3f, y, 0.001f, rv, US_MAT_EARTH);
						wline(c, px0 + pw0 * 0.3f, py0, xc + hw * 0.3f, y, 0.001f, rv, US_MAT_EARTH);
					}
				}
				have = 1;
				px0 = xc;
				py0 = y;
				pw0 = hw;
			}
		}
		int r0 = 0;
		for (int q = 0; q < k; q++)
			r0 += lay_rows[q];
		for (int row = r0; row < r0 + lay_rows[k]; row++) {
			float sc = s->rsc[row], d = s->rd[row];
			if (k == 1 && !tree_done && d >= TREE_D) {
				draw_tree(s, c, ctx, A, sgn, s->gust, autumn, winter, dayv * lightk, tear);
				tree_done = 1;
			}
			if (k == 1)
				for (int i = 0; i < nhaw; i++)
					if (!haw_done[i] && d >= s->hw_d[i]) {
						hawthorn(s, c, ctx, i, A, sgn, gstr, dayv * lightk, spring, autumn, winter);
						haw_done[i] = 1;
					}
			if (k == 2 && !hare_done && d >= s->hr_d) {
				hare_draw(s, c, ctx, A, dayv * (0.75f + 0.25f * lightk));
				hare_done = 1;
			}
			if (k == 1 && !kite_done && d >= KITE_D) {
				float kx = s->kite_u * A;
				draw_kite(s, c, ctx, A, sgn, ridge(s, 1, kx, A) + KITE_D * lay_drop[1], us_lerp(lay_s0[1], lay_s1[1], KITE_D), dayv,
					  winter);
				kite_done = 1;
			}
			float Z = 1.0f / sc;
			int b0 = s->row_start[row], b1 = s->row_start[row + 1];
			int nb = (int)((b1 - b0) * keep);
			/* sub: a long, slow swell rolls through the field toward the viewer, lifting row after row */
			float roll = sinf(Z * 0.9f + s->sub_ph * 2.4f);
			float H = (k == 0 ? 0.26f : 0.34f) * sc * (k == 2 ? heightC : height) * (1.0f + 0.08f * s->sub_s * roll);
			float wid;
			if (ctx->crisp)
				wid = k == 2 && sc > 0.7f ? 0.011f : 0.0f;
			else
				wid = k == 0 ? 0.0009f : (k == 1 ? 0.0011f + 0.002f * sc : 0.0012f + 0.0022f * sc);
			/* aerial perspective: far rows faint, near rows full; the light follows the loudness */
			float vrow;
			if (ctx->crisp)
				vrow = k == 0 ? 0.0f : (k == 1 ? 0.5f : 0.42f + 0.25f * sc);
			else
				vrow = k == 0 ? 0.3f : (k == 1 ? 0.42f + 0.4f * sc : 0.5f + 0.3f * sc);
			vrow *= dayv * lightk * (0.15f + 0.85f * lightk) * (ctx->crisp ? 0.62f + 0.38f * sunk : 1.0f);
			if (vrow <= 0)
				continue;
			float push = s->kimp * (0.1f + 0.22f * sc); /* the kick's pressure: the whole field ducks, near more */
			float shiv = s->senv * (0.15f + 0.5f * sc); /* the snare's shiver */
			float hatf = s->henv * (k == 0 ? 1.0f : 1.0f - sqrtf(sc)); /* hats glint along the far rows */
			/* pixel canvases show the mid hill as colour with only a few strokes on it */
			int kstep = ctx->crisp ? (k == 1 ? 3 : 2) : 1;
			if (ctx->crisp && k == 1 && winter < 0.5f) {
				/* pixel: the blades left out still carry their flowers, so the hill is dotted with drifts of
				 * poppies and cornflowers */
				float fk = (0.5f + 0.7f * w->bloom) * (1.0f - 0.7f * autumn);
				for (int q = 0; q < nb; q++) {
					if ((q - (row * 7) % kstep) % kstep == 0)
						continue;
					blade_t *b = &s->bl[b0 + q];
					if (b->tint < 2 || fmodf(b->ph * 7.31f, 1.0f) > (0.02f + 0.7f * b->fpatch) * fk)
						continue;
					float x = b->u * A, y = row_y(s, row, x, A);
					float hw, xc = path_at(s, A, k, d, sc, &hw);
					if (xc > -5 && fabsf(x - xc) < hw)
						continue;
					float hh = H * b->h * b->clump * 0.8f;
					float bx = 0.2f * sgn * hh * 0.6f; /* (held at their resting lean: pixel flicker would read as noise) */
					int pop = fmodf(b->ph * 11.3f, 1.0f) < 0.55f;
					us_put(c, (int)((x + bx) * c->s), (int)((y - hh) * c->s), us_sat((0.55f + 0.3f * sunk) * dayv * lightk),
					       pop ? US_MAT_BLOOM : US_MAT_WATER);
				}
			}
			for (int q = (row * 7) % kstep; q < nb; q += kstep) {
				blade_t *b = &s->bl[b0 + q];
				float x = b->u * A;
				float y = row_y(s, row, x, A);
				float X = (x - A * 0.5f) * Z;
				float hmul = 1.0f;
				float hw, xc = path_at(s, A, k, d, sc, &hw);
				if (xc > -5) { /* trodden path: bare in the middle, short and broken at its edges */
					float e = hw * (0.75f + 0.5f * fmodf(b->ph * 3.3f, 1.0f));
					float dx = fabsf(x - xc);
					if (dx < e * 0.55f)
						continue;
					if (dx < e * 1.8f)
						hmul = 0.2f + 0.8f * us_smooth(e * 0.55f, e * 1.8f, dx);
				}
				if (k == 1 && fabsf(x - s->tree_u * A) < 0.012f && d < 0.15f)
					hmul *= 0.5f; /* keep the trunk's foot clear */
				/* clumps: taller stands and low hollows, so the field has structure */
				hmul *= b->clump;
				float bend = wind_at(s, X * 0.25f, Z * 0.25f, ax, az);
				float fb = front_at(s, b->u, sc);
				float flut = 0.018f * us_noise2(b->ph, tt * 0.9f);
				if (shiv > 0.004f)
					flut += shiv * us_noise2(b->ph * 1.7f, tt * 15.0f);
				float persp = 0.75f + 0.4f * fabsf(ax);
				/* every blade has its own resting arch (most bow downwind, some the other way), so a still field
				 * is still grass rather than rain */
				float fr7 = fmodf(b->ph * 5.13f, 1.0f);
				float rest = (fr7 < 0.72f ? 1.0f : -0.8f) * (0.1f + 0.28f * fmodf(b->ph * 2.71f, 1.0f));
				float bb = ((lean0 + rest + gstr * bend + flut + push) * sgn + fb) * persp;
				float h = H * b->h * hmul * (b->head ? 1.25f : 1.0f);
				/* blowing toward/away from the viewer foreshortens the blades */
				float fs = 1.0f - 0.15f * fabsf(az) * us_sat(bend);
				float shadow = 1.0f - fp.shk + fp.shk * us_smooth(-0.3f, 0.3f, us_noise2(X * 0.05f - s->cloud_t * 2.0f * ax, Z * 0.08f - s->cloud_t * 2.0f * az));
				/* bent blades catch the light: the breeze's waves are bright bands, a kick front a brighter crest */
				float g = us_smooth(0.25f, 0.9f, band_at(s, X * 0.6f, Z * 0.6f, ax, az)) * (0.35f + 0.9f * s->breeze); /* the same bands as the ground under it */
				float sheen = 0.66f + 0.4f * g + 0.35f * us_sat(fabsf(fb)) + 0.35f * push;
				if (b->tint == 1)
					sheen += (k == 0 ? 1.6f : 0.9f) * hatf;
				float v = us_sat(vrow * shadow * sheen * (0.75f + 0.25f * b->h));
				uint8_t m = US_MAT_LEAF;
				if (autumn > 0.5f)
					m = b->tint < 2 ? US_MAT_LIGHT : (b->tint == 2 ? US_MAT_EARTH : US_MAT_STEM); /* sun-gold, dry, straw */
				else if (winter > 0.5f)
					m = b->tint < 3 ? US_MAT_EARTH : US_MAT_STEM;
				else if (summer > 0.5f && b->tint == 0)
					m = US_MAT_STEM;
				/* the crest of a gust: bent blades flash their pale undersides, a travelling sheen of silver */
				if (winter < 0.5f && autumn < 0.5f && b->tint == 1 && b->h > 0.8f && (fabsf(fb) > 0.3f || g > 0.75f) && k > 0)
					m = US_MAT_ACCENT;
				float pts[8];
				float bx = us_clampf(bb * 0.9f, -1.6f, 1.6f);
				for (int qq = 0; qq < 4; qq++) {
					float t = qq / 3.0f;
					pts[2 * qq] = x + bx * h * t * t;
					pts[2 * qq + 1] = y - h * fs * t * (1.0f - 0.3f * fabsf(bx) * t);
				}
				blade(c, x, y, bx, h, fs, wid, v, m);
				float tx = pts[6], ty = pts[7];
				float force = gstr * bend + 0.5f * fabsf(fb);
				if (winter > 0.4f && b->tint == 3 && k > 0) /* snow caught on the tips */
					us_point(c, tx, ty, 0.0015f + 0.003f * sc, 0.6f * winter * dayv, US_MAT_WATER);
				if (k == 0) {
					if (b->tint == 1 && (q & 3) == 0)
						tip_add(s, tx, ty, sc); /* far tips glint on hats too */
					continue;
				}
				int flower = 0;
				if (!b->head && b->tint == 3 && sc > 0.15f && winter < 0.5f) {
					/* wildflowers grow in drifts, not evenly */
					flower = fmodf(b->ph * 7.31f, 1.0f) < (0.03f + 0.75f * b->fpatch) * (0.6f + 0.6f * w->bloom) * (k == 2 ? 0.6f : 1.0f);
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
						for (int qq = 0; qq < np; qq++) {
							float an = US_TAU * qq / np + b->ph;
							float fq = fmodf(b->ph * 13.0f + qq * 0.37f, 1.0f);
							if (fq < force * 0.8f)
								continue; /* this tuft has blown */
							float px2 = fx + cosf(an) * rr, py2 = fy + sinf(an) * rr;
							if (c->crisp)
								us_put(c, (int)(px2 * c->s), (int)(py2 * c->s), fv * 0.8f, US_MAT_ACCENT);
							else
								wline(c, fx, fy, px2, py2, 0.0008f, fv * 0.45f, US_MAT_ACCENT);
						}
					} else {
						/* poppies (red cups), cornflowers (ragged blue stars) and ox-eye daisies */
						float kind = fmodf(b->ph * 11.3f, 1.0f);
						int poppy = kind < 0.45f, corn = !poppy && kind < 0.78f;
						if (c->crisp) {
							us_disc(c, fx, fy, rr * 0.9f, fv, corn ? US_MAT_WATER : US_MAT_BLOOM);
						} else if (poppy) {
							/* a nodding cup of four broad petals, open to the side */
							float an0 = b->ph + nod * 2.0f;
							for (int qq = 0; qq < 4; qq++) {
								float an = an0 + US_TAU * qq / 4;
								leafshape(c, fx, fy, an, rr * 1.1f, rr * 0.55f, fv * 0.85f, US_MAT_BLOOM);
							}
							us_point(c, fx, fy, 0.001f + 0.0008f * sc, fv * 0.25f, US_MAT_BLOOM);
						} else if (corn) {
							for (int qq = 0; qq < 8; qq++) {
								float an = US_TAU * qq / 8 + b->ph;
								float ln = rr * (0.75f + 0.3f * (qq & 1));
								wline(c, fx, fy, fx + cosf(an) * ln, fy + sinf(an) * ln * 0.75f, 0.0013f + 0.0008f * sc, fv * 0.85f, US_MAT_WATER);
							}
						} else {
							/* five petals: little loops around a bright eye */
							for (int qq = 0; qq < 5; qq++) {
								float an = US_TAU * qq / 5 + b->ph + nod * 2.0f;
								wline(c, fx, fy, fx + cosf(an) * rr, fy + sinf(an) * rr * 0.7f, 0.0016f + 0.001f * sc, fv * 0.8f, US_MAT_ACCENT);
							}
							us_point(c, fx, fy, 0.0011f + 0.001f * sc, us_sat(fv * 1.3f), US_MAT_LIGHT);
						}
					}
					tip_add(s, fx, fy, sc + 2.0f); /* +2 marks a flower (petals fly) */
				}
				if (b->head && winter < 0.6f) {
					/* seed head: a spike along the tip direction; near ones carry a few paired spikelets */
					float dx = pts[6] - pts[4], dy = pts[7] - pts[5];
					float dl = sqrtf(dx * dx + dy * dy) + 1e-6f;
					float hl = (0.012f + 0.018f * (summer + autumn)) * sc * (k == 2 ? heightC * 1.6f : height);
					float hx = tx + dx / dl * hl, hy = ty + dy / dl * hl;
					uint8_t hm = spring > 0.5f ? US_MAT_LEAF : (autumn > 0.5f ? US_MAT_BLOOM : US_MAT_ACCENT);
					float hv = us_sat(v * 1.25f + 0.1f);
					if (ctx->crisp)
						us_line(c, tx, ty, hx, hy, sc > 0.6f ? 0.0075f : 0.001f, hv, hm);
					else {
						wline(c, tx, ty, hx, hy, 0.0012f + 0.0016f * sc, hv, hm);
						float nx = -dy / dl * hl * 0.35f, ny = dx / dl * hl * 0.35f;
						int na = k == 2 ? 3 : 1;
						for (int qq = 1; qq <= na; qq++) {
							float f = (float)qq / (na + 1);
							float mx = us_lerp(tx, hx, f), my = us_lerp(ty, hy, f);
							float ex = us_lerp(tx, hx, f + 0.45f), ey = us_lerp(ty, hy, f + 0.45f);
							wline(c, mx, my, ex + nx, ey + ny, 0.0008f, hv * 0.6f, hm);
							wline(c, mx, my, ex - nx, ey - ny, 0.0008f, hv * 0.6f, hm);
						}
					}
					tip_add(s, hx, hy, sc);
					/* strong gusts shake seeds loose */
					if (force > 0.35f && s->nsd < MAX_SEEDS &&
					    us_rand(r) < dt * (force - 0.3f) * 2.0f * (0.4f + summer + autumn) * (0.5f + w->growth) * (k == 2 ? 2.5f : 1.0f)) {
						seed_t *qd = &s->sd[s->nsd++];
						qd->x = hx;
						qd->y = hy;
						qd->vx = sgn * us_randr(r, 0.05f, 0.18f) * (0.5f + s->gust);
						qd->vy = -us_randr(r, 0.01f, 0.05f) * (0.3f + sc);
						qd->life = 1.0f;
						qd->ph = us_rand(r) * 10;
						qd->sz = 0.0008f + 0.0018f * sc;
						shake += 1.0f;
					}
				}
			}
		}
	}

	/* ---- the near layer: oat stalks framing the view; then the drums' answers on top ---- */
	hunter(s, c, ctx, A, sgn, dayv * (0.8f + 0.2f * lightk), snapped, kicked == 1);
	draw_fg(s, c, ctx, A, sgn, gstr, ax, az, height, dayv, lightk, winter, spring);
	spawn_drums(s, ctx, snapped, sgn);
	draw_drums(s, c, ctx, sgn, dayv * (0.45f + 0.45f * lightk));
	butterflies(s, c, ctx, A, sgn, snapped);

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
			if (i < 2) /* two skim low over the grass tips, hawking for the insects the wind stirs up */
				ty = ridge(s, 2, b->x * A, A) - 0.07f - 0.04f * s->mid_s + 0.03f * us_noise2(b->ph + 9.0f, (float)ctx->t * 0.5f);
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
	s->life_init = 1;
}

const us_scene_vt us_scene_wind = {"wind", "Wind", 0.0f, create, destroy, frame};
