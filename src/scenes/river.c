/* Understory scene: River — a winding river seen from above, drawn as thousands of fine streamlines.
 *
 * Particles are advected through a flow field built from:
 *   - the channel itself (follows the meandering centreline, fastest mid-stream, slow at the banks),
 *   - potential flow around a few stones (standing bow waves, a quiet wake),
 *   - point vortices (eddies) shed behind the stones — steadily in silence, in bursts on onsets,
 *   - a little curl noise so nothing is ever laminar.
 * The canvas trails turn each particle's motion into a short streak, so the picture is the flow.
 * Beyond the banks lies a patchwork countryside: fields laid out back from the river between hedgerows
 * (plough furrows, crop drills that ripple with gusts, wildflower meadows, pastures with grazing sheep),
 * fences, a towpath, and slow cloud shadows drifting over it all; it turns gold and baled in autumn and
 * white in winter.
 *
 * On the water: a duck and her ducklings paddling the slack edge, beds of water-crowfoot streaming in the
 * current, mayfly swarms dancing over the margins, and the rings of rising and leaping trout.
 *
 * Audio: mids + level -> flow speed; onsets -> eddies shed behind stones; kicks -> surges downstream and a
 *        trout leaping in a splash ring; big onsets -> a flock of sheep bolts; bass -> meadow flowers open;
 *        low-mids + sub -> the weed streamers wave; each reed clump nods to its own mid band (the melody
 *        runs along the bank); highs -> wind, more mayflies dancing faster (each lit by its own treble band)
 *        and more rises; air -> sun glints, sharpened by the centroid; phrase swell -> the clouds thin and
 *        their shadows slide faster over land and water; stereo width -> the ducklings fan out.
 * World: reads water_level (width + speed at open, so a long storm elsewhere = swollen fast river),
 *        season (petals in spring, floating leaves in autumn, shelf ice + snow in winter), wind;
 *        writes a little moisture.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define NX 160         /* channel samples across the width */
#define MAX_P 2800     /* streamline particles */
#define MAX_ROCKS 6
#define MAX_VORT 56
#define MAX_FLOAT 24   /* leaves / petals */
#define MAX_REEDS 30 /* reed clumps */
#define MAX_TUFTS 220
#define ROCK_V 24
#define MAX_SURGE 10
#define MAX_FOAM 360
#define MAX_GLINT 72
#define MAX_FISH 4
#define MAX_TREES 12
#define TREE_V 40
#define MAX_COL 10 /* field columns per bank (between hedgerows) */
#define NBAND 3    /* fields stacked back from the river in each column */
#define MAX_SHEEP 14
#define MAX_RING 28 /* rise rings, leap splashes */
#define MAX_FLY 26  /* mayflies dancing over the water */
#define NDUCK 5     /* a duck and her ducklings */
#define DHIST 96    /* the duck's wake history the ducklings follow */
#define MAX_WEED 7  /* beds of water-crowfoot streaming in the current */
#define D0 0.05f   /* the riverside strip (towpath, reeds) before the fields start */
#define DFAR 0.8f
#define GX 48 /* coarse noise grid (curl wobble + speed texture), sampled bilinearly */
#define GY 27
#define FWN 128 /* fish outline width sampled over l in [-1, 1]: a per-pixel powf was a hot spot */

typedef struct {
	float u, y;   /* u = x / aspect */
	float pu, py; /* previous position */
	float age, life;
} prt_t;

typedef struct {
	float u, s, r; /* along (0..1), lateral (-1..1), radius */
	float shape[ROCK_V];
	float shed_t;
	float sign;
} rock_t;

typedef struct {
	float x, y, g, core, life, age;
} vort_t;

typedef struct {
	float u, y, rot, vr, size, life;
	float dx_, dy_, dr_; /* where it was drawn last frame */
	uint8_t kind; /* 0 leaf, 1 petal */
} flt_t;

typedef struct {
	float u, side, off, len, ph, lean;
	float nod, bavg; /* eased response to this clump's own mid band, and that band's slow average */
} reed_t;

typedef struct {
	float u, y, age, life, r1, v;
	uint8_t kind; /* 0 a rise (a trout sipping a fly), 1 a leap splash */
	float ph;
} ring_t;

typedef struct {
	float u, s, len, ph;
	int n;
} weed_t;

typedef struct {
	float u, y, sz;
} tuft_t;

typedef struct {
	float x, amp; /* a pulse of faster water travelling downstream */
} surge_t;

typedef struct {
	float u, y, pu, py, life, decay;
} foam_t;

typedef struct {
	float u, y, age, life, sz;
} glint_t;

typedef struct {
	float u, s;         /* position: along (0..1), lateral (-1..1) */
	float hu, hs;       /* station the fish holds against the current */
	float len, ph, dart, hd;
	float gone; /* 1 just after a leap: the shadow is out of the water */
} fish_t;

typedef struct {
	float u, side, off, r, ph;
	float lob[TREE_V], lmin, lmax;
} tree_t;

/* a field seen from above: rows at an angle (plough furrows / crop drills), or a flowery meadow,
 * or a pasture with sheep. Bounded by the hedgerows either side and by band edges back from the river. */
enum { F_PLOUGH, F_CROP, F_MEADOW, F_PASTURE };
typedef struct {
	uint8_t kind, edge; /* edge: its far boundary is 0 open, 1 hedge, 2 fence */
	float ang, sp, ph, ca, sa;
} field_t;

typedef struct {
	int side, col, band;
	float p, q, hd, ph, step;
	float flee; /* startled by a big hit, eases off */
} sheep_t;

typedef struct {
	us_rng rng;
	int inited;
	/* countryside, per bank (0 = far bank / top, 1 = near bank / bottom) */
	int ncol[2];
	float hx[2][MAX_COL + 1], hlean[2][MAX_COL + 1], hgap[2][MAX_COL + 1];
	float bd[2][MAX_COL][NBAND + 1][2]; /* band edges: d at the left / right hedge */
	field_t fd[2][MAX_COL][NBAND];
	sheep_t sh[MAX_SHEEP];
	int nsh;
	float shade_t, path_side;
	/* coarse per-frame grids: cloud shadow + crop gusts over the land */
#define LGW 40
#define LGH 24
	float lshade[LGW * LGH], lgust[LGW * LGH];
	/* crisp land fill cache (fields are static; refreshed a few columns per frame) */
	uint8_t *lc_ink, *lc_mat;
	int lc_w, lc_h, lc_phase;
	surge_t sg[MAX_SURGE];
	int nsg;
	float sgx[NX]; /* surge profile along the river, rebuilt per frame */
	foam_t fm[MAX_FOAM];
	int nfm;
	glint_t gl[MAX_GLINT];
	int ngl;
	fish_t fish[MAX_FISH];
	tree_t tree[MAX_TREES];
	float foam_acc, glint_acc;
	float seed_off;
	prt_t p[MAX_P];
	int np;
	rock_t rk[MAX_ROCKS];
	int nrocks;
	vort_t vt[MAX_VORT];
	int nv;
	flt_t fl[MAX_FLOAT];
	int nf;
	reed_t rd[MAX_REEDS];
	tuft_t tf[MAX_TUFTS];
	/* per-frame channel cache */
	float cy[NX], hw[NX], sl[NX], hb[NX]; /* hb: floodplain terraces (slow) */
	float bank_w;
	float A;
	float gcx[GX * GY], gcy[GX * GY], gmod[GX * GY], gn[GX * GY];
	/* smoothed drivers */
	float width, speed, swell_env, wind_t, flow_t, ice;
	float rock_x[MAX_ROCKS], rock_y[MAX_ROCKS];
	float fwt[FWN + 1];
	/* life on and over the water */
	ring_t rg[MAX_RING];
	int nrg;
	float rise_acc, leap_cd, startle_cd;
	float flyv[MAX_FLY];
	weed_t wd[MAX_WEED];
	float weed_t_, weed_amp;
	float dk_u, dk_s, dk_dir, dk_hist_t, dk_spread;
	float dkh[DHIST][2]; /* (u, s) of the mother, newest at dkh_i */
	int dkh_i;
	float sun; /* how far the phrase swell has thinned the clouds */
	float hi_env, lm_env; /* eased highs (mayflies) and low-mids + sub (weed) */
} st_t;

static float fish_w(float l);

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x72697672u);
	s->seed_off = us_rand(&s->rng) * 100.0f;
	return s;
}

static void destroy(void *p)
{
	st_t *st = p;
	free(st->lc_ink);
	free(st->lc_mat);
	free(st);
}

/* ---- fast hairlines ------------------------------------------------------
 * Ink canvases: Xiaolin-Wu style 1px anti-aliased line (two pixels per step, no sqrt) — the core
 * us_line is a general AA thick-line rasteriser and is ~5x the cost for the thousands of fine
 * strokes this scene draws. Crisp canvases: the core's Bresenham path. */
/* us_put, inlined: the hairlines below call it tens of thousands of times a frame */
static inline void put1(us_canvas *c, int x, int y, float v, uint8_t mat)
{
	if ((unsigned)x >= (unsigned)c->w || (unsigned)y >= (unsigned)c->h)
		return;
	int iv = (int)(v * 255.0f + 0.5f);
	if (iv <= 0)
		return;
	if (iv > 255)
		iv = 255;
	size_t i = (size_t)y * c->w + x;
	int old = c->ink[i];
	if (c->blend == US_BLEND_ADD) {
		int n = old + iv;
		c->ink[i] = (uint8_t)(n > 255 ? 255 : n);
		if (iv * 2 >= old)
			c->mat[i] = mat;
	} else if (iv > old) {
		c->ink[i] = (uint8_t)iv;
		c->mat[i] = mat;
	}
}

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
	/* keep perceived brightness of diagonals close to axis-aligned lines */
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
			put1(c, j, i, e * (1.0f - f), mat);
			put1(c, j + 1, i, e * f, mat);
		} else {
			put1(c, i, j, e * (1.0f - f), mat);
			put1(c, i, j + 1, e * f, mat);
		}
	}
}

static void hpoly(us_canvas *c, const float *xy, int n, float v, uint8_t mat)
{
	for (int i = 0; i + 1 < n; i++)
		hl(c, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], v, mat);
}

/* ---- channel ------------------------------------------------------------ */

static float center_at(const st_t *st, float x, float t)
{
	float so = st->seed_off;
	return 0.5f + 0.13f * sinf(1.75f * x + so) + 0.075f * us_noise2(x * 0.85f + so, t * 0.006f) +
	       0.02f * us_noise2(x * 2.7f, so + 3.0f);
}

static void build_channel(st_t *st, float A, float t)
{
	st->A = A;
	for (int i = 0; i < NX; i++) {
		float x = A * i / (NX - 1);
		st->cy[i] = center_at(st, x, t);
		float nz = 1.0f + 0.16f * us_noise2(x * 1.9f, st->seed_off + 9.0f);
		st->hw[i] = st->width * nz;
		st->hb[i] = st->bank_w * nz;
	}
	for (int i = 0; i < NX; i++) {
		int a = i > 0 ? i - 1 : 0, b = i < NX - 1 ? i + 1 : NX - 1;
		st->sl[i] = (st->cy[b] - st->cy[a]) / (A * (b - a) / (NX - 1));
	}
}

/* sample channel at x: centre, half-width, slope */
static void chan(const st_t *st, float x, float *c, float *h, float *s)
{
	float f = x / st->A * (NX - 1);
	if (f < 0)
		f = 0;
	if (f > NX - 1.001f)
		f = NX - 1.001f;
	int i = (int)f;
	float t = f - i;
	*c = us_lerp(st->cy[i], st->cy[i + 1], t);
	*h = us_lerp(st->hw[i], st->hw[i + 1], t);
	*s = us_lerp(st->sl[i], st->sl[i + 1], t);
}

static float surge_at(const st_t *st, float x)
{
	float f = us_clampf(x / st->A, 0, 1) * (NX - 1.001f);
	int i = (int)f;
	return us_lerp(st->sgx[i], st->sgx[i + 1], f - i);
}

static void build_grid(st_t *st, float A, float t)
{
	float nf = 3.2f, tt = st->flow_t * 0.35f;
	for (int j = 0; j < GY; j++)
		for (int i = 0; i < GX; i++) {
			float x = A * i / (GX - 1), y = (float)j / (GY - 1);
			int k = j * GX + i;
			st->gn[k] = us_noise3(x * nf, y * nf, tt);
			st->gmod[k] = us_noise2(x * 2.5f - t * 0.25f, y * 2.5f);
		}
	/* curl of the noise by central differences on the grid itself (one noise per node, not three) */
	float ix = (GX - 1) / A, iy = (float)(GY - 1);
	for (int j = 0; j < GY; j++)
		for (int i = 0; i < GX; i++) {
			int i0 = i > 0 ? i - 1 : i, i1 = i < GX - 1 ? i + 1 : i;
			int j0 = j > 0 ? j - 1 : j, j1 = j < GY - 1 ? j + 1 : j;
			int k = j * GX + i;
			st->gcx[k] = (st->gn[j1 * GX + i] - st->gn[j0 * GX + i]) * iy / (j1 - j0);
			st->gcy[k] = -(st->gn[j * GX + i1] - st->gn[j * GX + i0]) * ix / (i1 - i0);
		}
}

static void grid_sample(const st_t *st, float x, float y, float *cx, float *cy, float *md)
{
	float fx = us_clampf(x / st->A, 0, 1) * (GX - 1.001f), fy = us_clampf(y, 0, 1) * (GY - 1.001f);
	int i = (int)fx, j = (int)fy;
	float tx = fx - i, ty = fy - j;
	int k = j * GX + i;
	float w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty), w01 = (1 - tx) * ty, w11 = tx * ty;
	*cx = st->gcx[k] * w00 + st->gcx[k + 1] * w10 + st->gcx[k + GX] * w01 + st->gcx[k + GX + 1] * w11;
	*cy = st->gcy[k] * w00 + st->gcy[k + 1] * w10 + st->gcy[k + GX] * w01 + st->gcy[k + GX + 1] * w11;
	*md = st->gmod[k] * w00 + st->gmod[k + 1] * w10 + st->gmod[k + GX] * w01 + st->gmod[k + GX + 1] * w11;
}

/* flow velocity at (x, y) in normalized units / s */
static void flow(const st_t *st, float x, float y, float t, float *vx, float *vy, float *foam)
{
	float c, h, s;
	chan(st, x, &c, &h, &s);
	float lat = (y - c) / h;
	float inv = 1.0f / sqrtf(1.0f + s * s);
	float dx = inv, dy = s * inv;
	float prof = us_sat(1.0f - 0.8f * lat * lat);
	float cx, cyv, md;
	grid_sample(st, x, y, &cx, &cyv, &md);
	float U = st->speed * (0.2f + 0.8f * prof) * (0.85f + 0.3f * md);
	float ux = dx * U, uy = dy * U;
	ux += cx * 0.006f * (0.4f + st->speed * 3.0f);
	uy += cyv * 0.006f * (0.4f + st->speed * 3.0f);
	*foam = 0;
	/* stones: potential flow around a cylinder in the local flow direction */
	for (int k = 0; k < st->nrocks; k++) {
		float rx = x - st->rock_x[k], ry = y - st->rock_y[k];
		float R = st->rk[k].r;
		float r2 = rx * rx + ry * ry;
		if (r2 > 25.0f * R * R)
			continue;
		if (r2 < R * R * 0.9f)
			r2 = R * R * 0.9f;
		/* rotate into flow frame */
		float zx = rx * dx + ry * dy, zy = -rx * dy + ry * dx;
		/* 1 - R^2 / z^2 */
		float zr2 = zx * zx - zy * zy, zi2 = 2 * zx * zy;
		float d2 = r2 * r2;
		float qx = 1.0f - R * R * zr2 / d2, qy = R * R * zi2 / d2;
		/* velocity = conj(U q) in rotated frame */
		float wx = U * qx, wy = -U * qy;
		float fade = us_smooth(25.0f * R * R, 6.0f * R * R, r2);
		float bx = dx * wx - dy * wy, by = dy * wx + dx * wy;
		ux = us_lerp(ux, bx + (ux - dx * U), fade);
		uy = us_lerp(uy, by + (uy - dy * U), fade);
		*foam += us_smooth(4.0f * R * R, R * R, r2);
	}
	/* eddies */
	for (int k = 0; k < st->nv; k++) {
		const vort_t *v = &st->vt[k];
		float rx = x - v->x, ry = y - v->y;
		float r2 = rx * rx + ry * ry + 1e-6f;
		float c2 = v->core * v->core;
		if (r2 > 30.0f * c2)
			continue;
		float env = us_sat(v->age / 0.4f) * us_sat((v->life - v->age) / 1.5f);
		float f = v->g * env / (US_TAU * r2) * (1.0f - expf(-r2 / c2));
		ux += -ry * f;
		uy += rx * f;
		*foam += env * expf(-r2 / (2.0f * c2)) * 0.6f;
	}
	/* keep off the banks */
	if (lat > 0.85f)
		uy -= (lat - 0.85f) * 0.6f * h * 4.0f;
	else if (lat < -0.85f)
		uy -= (lat + 0.85f) * 0.6f * h * 4.0f;
	*vx = ux;
	*vy = uy;
}

static void respawn(st_t *st, prt_t *q, float edge)
{
	us_rng *r = &st->rng;
	float x = us_rand(r) * st->A;
	if (edge > 0 && us_rand(r) < edge)
		x = us_rand(r) * 0.05f * st->A;
	float c, h, s;
	chan(st, x, &c, &h, &s);
	float lim = 0.97f - 0.3f * st->ice;
	float lat = us_randr(r, -lim, lim);
	q->u = x / st->A;
	q->y = c + lat * h;
	q->pu = q->u;
	q->py = q->y;
	q->age = 0;
	q->life = us_randr(r, 1.5f, 5.0f);
	for (int k = 0; k < st->nrocks; k++) {
		float dx = x - st->rock_x[k], dy = q->y - st->rock_y[k];
		if (dx * dx + dy * dy < st->rk[k].r * st->rk[k].r * 1.2f)
			q->life = 0.01f;
	}
}

static void init_scene(st_t *st, const us_ctx *ctx)
{
	us_rng *r = &st->rng;
	us_world *w = ctx->w;
	st->width = 0.085f + 0.13f * w->water_level;
	st->bank_w = st->width + 0.03f;
	st->speed = 0.06f + 0.1f * w->water_level;
	st->ice = us_smooth(0.55f, 0.95f, us_world_winter(w));
	build_channel(st, ctx->aspect, ctx->t);
	build_grid(st, ctx->aspect, ctx->t);
	st->nrocks = 0;
	for (int k = 0; k < MAX_ROCKS; k++) {
		rock_t *rk = &st->rk[k];
		/* golden-ratio spacing: however many stones the density shows, they spread along the river */
		float gu = 0.18f + 0.618034f * k;
		rk->u = 0.06f + 0.86f * (gu - floorf(gu)) + us_randr(r, -0.03f, 0.03f);
		rk->s = us_randr(r, -0.55f, 0.55f);
		rk->r = us_randr(r, 0.012f, 0.03f);
		/* smooth pebble outline: a couple of low harmonics */
		float a2 = us_randr(r, 0.05f, 0.16f), p2 = us_rand(r) * US_TAU;
		float a3 = us_randr(r, 0.02f, 0.08f), p3 = us_rand(r) * US_TAU;
		for (int v = 0; v < ROCK_V; v++) {
			float th = US_TAU * v / ROCK_V;
			rk->shape[v] = 1.0f + a2 * cosf(2 * th + p2) + a3 * cosf(3 * th + p3);
		}
		rk->shed_t = us_rand(r) * 3.0f;
		rk->sign = us_rand(r) < 0.5f ? -1.0f : 1.0f;
	}
	for (int i = 0; i < MAX_REEDS; i++) {
		reed_t *d = &st->rd[i];
		d->u = (i + us_rand(r)) / MAX_REEDS;
		d->side = us_rand(r) < 0.5f ? 1.0f : -1.0f;
		d->off = us_randr(r, 0.0f, 0.012f);
		d->len = us_randr(r, 0.022f, 0.05f);
		d->ph = us_rand(r) * 20.0f;
		d->lean = us_randr(r, -0.5f, 0.5f);
	}
	for (int i = 0; i < MAX_TUFTS; i++) {
		st->tf[i].u = us_rand(r);
		st->tf[i].y = us_rand(r);
		st->tf[i].sz = us_randr(r, 0.4f, 1.0f);
	}
	for (int i = 0; i < MAX_FISH; i++) {
		fish_t *f = &st->fish[i];
		f->hu = f->u = (i + us_randr(r, 0.2f, 0.8f)) / MAX_FISH;
		f->hs = f->s = us_randr(r, -0.45f, 0.45f);
		f->len = us_randr(r, 0.065f, 0.095f);
		f->ph = us_rand(r) * US_TAU;
		f->hd = 0;
	}
	for (int i = 0; i < MAX_TREES; i++) {
		tree_t *tr = &st->tree[i];
		/* alternate banks, spread along the river */
		float gu = 0.41f + 0.618034f * i;
		tr->u = gu - floorf(gu);
		tr->side = (i & 1) ? 1.0f : -1.0f;
		int far = (i % 3) == 2; /* every third tree stands back in the fields */
		tr->off = far ? us_randr(r, 0.1f, 0.42f) : us_randr(r, 0.0f, 0.03f);
		tr->r = far ? us_randr(r, 0.035f, 0.06f) : us_randr(r, 0.05f, 0.085f);
		tr->ph = us_rand(r) * 20.0f;
		float a1 = us_randr(r, 0.04f, 0.09f), p1 = us_rand(r) * US_TAU;
		float a2 = us_randr(r, 0.03f, 0.07f), p2 = us_rand(r) * US_TAU;
		for (int v = 0; v < TREE_V; v++) {
			float th = US_TAU * v / TREE_V;
			/* lobed crown: leafy clumps around the rim */
			tr->lob[v] = 1.0f + a1 * cosf(2 * th + p1) + a2 * cosf(3 * th + p2) + 0.07f * fabsf(sinf(4.5f * th + p1));
		}
		tr->lmin = 10.0f;
		tr->lmax = 0.0f;
		for (int v = 0; v < TREE_V; v++) {
			tr->lmin = fminf(tr->lmin, tr->lob[v]);
			tr->lmax = fmaxf(tr->lmax, tr->lob[v]);
		}
	}
	/* countryside: hedgerows run back from the river, splitting each bank into columns of fields */
	float A0 = ctx->aspect;
	st->path_side = us_rand(r) < 0.5f ? 0.0f : 1.0f;
	for (int sd = 0; sd < 2; sd++) {
		float x = -us_randr(r, 0.02f, 0.2f);
		int n = 0;
		while (n <= MAX_COL) {
			st->hx[sd][n] = x;
			st->hlean[sd][n] = us_randr(r, -0.18f, 0.18f);
			st->hgap[sd][n] = us_randr(r, 0.08f, 0.5f);
			n++;
			if (x > A0 + 0.05f)
				break;
			x += us_randr(r, 0.22f, 0.38f);
		}
		st->ncol[sd] = n - 1;
		for (int k = 0; k < st->ncol[sd]; k++) {
			float b1 = us_randr(r, 0.14f, 0.24f), b2 = b1 + us_randr(r, 0.13f, 0.21f);
			float t1 = us_randr(r, -0.05f, 0.05f), t2 = us_randr(r, -0.05f, 0.05f);
			st->bd[sd][k][0][0] = st->bd[sd][k][0][1] = D0;
			st->bd[sd][k][1][0] = b1 - t1;
			st->bd[sd][k][1][1] = b1 + t1;
			st->bd[sd][k][2][0] = b2 - t2;
			st->bd[sd][k][2][1] = b2 + t2;
			st->bd[sd][k][3][0] = st->bd[sd][k][3][1] = DFAR;
			for (int b = 0; b < NBAND; b++) {
				field_t *f = &st->fd[sd][k][b];
				float u = us_rand(r);
				f->kind = u < 0.27f ? F_PLOUGH : u < 0.62f ? F_CROP : u < 0.82f ? F_MEADOW : F_PASTURE;
				/* rows mostly along or across the field, sometimes on the slant */
				float ar = us_rand(r);
				f->ang = ar < 0.4f ? 0.0f : ar < 0.75f ? US_PI * 0.5f : us_randr(r, -0.9f, 0.9f);
				f->ang += us_randr(r, -0.12f, 0.12f);
				f->ca = cosf(f->ang);
				f->sa = sinf(f->ang);
				f->sp = us_randr(r, 0.85f, 1.2f);
				f->ph = us_rand(r) * 50.0f;
				u = us_rand(r);
				f->edge = u < 0.55f ? 1 : u < 0.8f ? 2 : 0;
			}
		}
	}
	/* a small flock in each pasture near the river */
	st->nsh = 0;
	for (int sd = 0; sd < 2; sd++)
		for (int k = 0; k < st->ncol[sd]; k++)
			for (int b = 0; b < 2; b++) {
				if (st->fd[sd][k][b].kind != F_PASTURE)
					continue;
				int n = 3 + (int)(us_rand(r) * 4.0f);
				float cp = us_randr(r, 0.3f, 0.7f), cq = us_randr(r, 0.3f, 0.7f);
				for (int i = 0; i < n && st->nsh < MAX_SHEEP; i++) {
					sheep_t *q = &st->sh[st->nsh++];
					q->side = sd;
					q->col = k;
					q->band = b;
					q->p = us_clampf(cp + us_randn(r) * 0.12f, 0.1f, 0.9f);
					q->q = us_clampf(cq + us_randn(r) * 0.15f, 0.12f, 0.88f);
					q->hd = us_rand(r) * US_TAU;
					q->ph = us_rand(r) * 30.0f;
					q->step = 0;
				}
			}
	for (int i = 0; i < MAX_WEED; i++) {
		weed_t *wb = &st->wd[i];
		float gu = 0.07f + 0.618034f * i;
		wb->u = 0.03f + 0.9f * (gu - floorf(gu));
		wb->s = us_randr(r, -0.55f, 0.45f);
		wb->len = us_randr(r, 0.05f, 0.09f);
		wb->ph = us_rand(r) * US_TAU;
		wb->n = 3 + (int)(us_rand(r) * 3.0f);
	}
	st->dk_u = us_randr(r, 0.3f, 0.7f);
	st->dk_s = us_rand(r) < 0.5f ? -0.62f : 0.62f;
	st->dk_dir = -1.0f;
	for (int k = 0; k < DHIST; k++) { /* a wake already trailing downstream of her */
		st->dkh[(DHIST - k) % DHIST][0] = st->dk_u + 0.0015f * k;
		st->dkh[(DHIST - k) % DHIST][1] = st->dk_s;
	}
	for (int i = 0; i <= FWN; i++)
		st->fwt[i] = fish_w(fminf(-1.0f + 2.0f * i / FWN, 0.999f)); /* sin(pi) < 0 at the snout: powf NaN */
	st->np = 0;
	st->inited = 1;
}

/* closed blob outline for a stone */
static void draw_rock(us_canvas *c, int crisp, const rock_t *rk, float x, float y, float v, float shrink)
{
	float pts[2 * (ROCK_V + 1)];
	for (int i = 0; i <= ROCK_V; i++) {
		int j = i % ROCK_V;
		float a = US_TAU * j / ROCK_V;
		float rr = rk->r * rk->shape[j] * shrink;
		pts[2 * i] = x + cosf(a) * rr * 1.15f;
		pts[2 * i + 1] = y + sinf(a) * rr;
	}
	if (crisp) {
		for (int i = 0; i < ROCK_V; i++)
			us_tri(c, x, y, pts[2 * i], pts[2 * i + 1], pts[2 * i + 2], pts[2 * i + 3], v * 0.45f, US_MAT_EARTH);
		us_polyline(c, pts, ROCK_V + 1, 0.0015f, v, US_MAT_EARTH);
	} else {
		hpoly(c, pts, ROCK_V + 1, v, US_MAT_EARTH);
	}
}

/* wipe last frame's drawing of a floating thing (only its own material, so the streamlines beneath
 * survive) — floaters then glide cleanly instead of smearing into chevron chains under the trails */
static void wipe_disc(us_canvas *c, float x, float y, float r, uint8_t mat)
{
	int x0 = (int)floorf((x - r) * c->s), x1 = (int)ceilf((x + r) * c->s);
	int y0 = (int)floorf((y - r) * c->s), y1 = (int)ceilf((y + r) * c->s);
	float r2 = r * r * c->s * c->s;
	for (int j = y0 < 0 ? 0 : y0; j <= y1 && j < c->h; j++)
		for (int i = x0 < 0 ? 0 : x0; i <= x1 && i < c->w; i++) {
			float dx = i + 0.5f - x * c->s, dy = j + 0.5f - y * c->s;
			size_t o = (size_t)j * c->w + i;
			if (dx * dx + dy * dy <= r2 && c->mat[o] == mat) {
				c->ink[o] = 0;
				c->mat[o] = US_MAT_WATER; /* else the ink bloom would relight it in the floater's colour */
			}
		}
}

static const uint8_t flt_mat[3] = {US_MAT_ACCENT, US_MAT_BLOOM, US_MAT_ACCENT};

static void leaf_shape(us_canvas *c, int crisp, float cx, float cy, float ang, float len, float wid, float v, uint8_t mat)
{
	float dx = cosf(ang), dy = sinf(ang);
	float tx = cx + dx * len, ty = cy + dy * len;
	float bx = cx - dx * len, by = cy - dy * len;
	float nx = -dy * wid, ny = dx * wid;
	us_tri(c, bx, by, cx + nx, cy + ny, tx, ty, crisp ? v : v * 0.55f, mat);
	us_tri(c, bx, by, cx - nx, cy - ny, tx, ty, crisp ? v : v * 0.55f, mat);
	if (!crisp) {
		float pts[] = {bx, by, cx + nx, cy + ny, tx, ty, cx - nx, cy - ny, bx, by};
		hpoly(c, pts, 5, v, mat);
	}
}

/* fish body half-width at axial position l (-1 tail .. +1 snout), in units of body length */
static float fish_w(float l)
{
	if (l > -0.62f) {
		float q = (l + 0.62f) / 1.62f; /* 0 at the tail root .. 1 at the snout */
		float w = 0.15f * powf(sinf(US_PI * (0.08f + 0.92f * q)), 0.7f) * (0.35f + 0.65f * us_smooth(0.0f, 0.45f, q));
		if (l > 0.18f && l < 0.42f) /* pectoral fins, swept back */
			w += 0.14f * (0.42f - l) / 0.24f;
		if (l > -0.42f && l < -0.26f) /* pelvic fins */
			w += 0.06f * (-0.26f - l) / 0.16f;
		return w;
	}
	return 0.02f + 0.11f * us_smooth(-0.62f, -1.0f, l); /* tail fin flares out */
}

static inline float fish_wt(const float *tab, float l)
{
	float f = (l + 1.0f) * (0.5f * FWN);
	int i = (int)f;
	if (i >= FWN)
		i = FWN - 1;
	return us_lerp(tab[i], tab[i + 1], f - i);
}

/* a trout seen from above, as a shadow carved out of the water lines (plus a faint rim in ink) */
static void fish_draw(us_canvas *c, const float *ftab, float x, float y, float ang, float L, float wag, float vis)
{
	float ax = cosf(ang), ay = sinf(ang);
	float R = L * 0.62f;
	int x0 = (int)floorf((x - R) * c->s), x1 = (int)ceilf((x + R) * c->s);
	int y0 = (int)floorf((y - R) * c->s), y1 = (int)ceilf((y + R) * c->s);
	float keep = c->crisp ? 0.0f : 0.1f;
	keep = us_lerp(1.0f, keep, vis);
	float inv = 1.0f / c->s;
	for (int j = y0 < 0 ? 0 : y0; j <= y1 && j < c->h; j++)
		for (int i = x0 < 0 ? 0 : x0; i <= x1 && i < c->w; i++) {
			float px = (i + 0.5f) * inv - x, py = (j + 0.5f) * inv - y;
			float l = (px * ax + py * ay) / (L * 0.5f);
			if (l < -1.0f || l > 1.0f)
				continue;
			float d = (-px * ay + py * ax) / L;
			d -= wag * (1.0f - l) * (1.0f - l) * 0.05f; /* the tail sweeps */
			float hw = fish_wt(ftab, l);
			/* forked tail */
			if (fabsf(d) > hw || (l < -0.9f && fabsf(d) < 0.05f * us_smooth(-0.88f, -1.0f, l)))
				continue;
			size_t o = (size_t)j * c->w + i;
			c->ink[o] = (uint8_t)(c->ink[o] * keep);
		}
	if (!c->crisp) {
		float pl[2 * 42], pr[2 * 42];
		for (int k = 0; k <= 40; k++) {
			float l = 1.0f - 2.0f * k / 40.0f;
			float hw = fish_wt(ftab, l) * L;
			float sp = wag * (1.0f - l) * (1.0f - l) * 0.05f * L;
			float cxp = x + ax * l * L * 0.5f - ay * sp, cyp = y + ay * l * L * 0.5f + ax * sp;
			pl[2 * k] = cxp - ay * hw;
			pl[2 * k + 1] = cyp + ax * hw;
			pr[2 * k] = cxp + ay * hw;
			pr[2 * k + 1] = cyp - ax * hw;
		}
		hpoly(c, pl, 41, 0.38f * vis, US_MAT_STEM);
		hpoly(c, pr, 41, 0.38f * vis, US_MAT_STEM);
	}
}

/* zero the ink inside a lobed crown (so the water does not show through the canopy) */
/* atan2 in [-pi, pi], ~0.01 rad accurate: plenty for picking a lobe */
static float fast_atan2(float y, float x)
{
	float ax = fabsf(x), ay = fabsf(y);
	float mx = fmaxf(ax, ay), mn = fminf(ax, ay);
	float a = mn / (mx + 1e-12f);
	float s2 = a * a;
	float r = ((-0.0464964749f * s2 + 0.15931422f) * s2 - 0.327622764f) * s2 * a + a;
	if (ay > ax)
		r = 1.57079637f - r;
	if (x < 0)
		r = 3.14159274f - r;
	if (y < 0)
		r = -r;
	return r;
}

/* hairline arc (cheap: a few Wu segments) */
static void harc(us_canvas *c, float x, float y, float r, float a0, float a1, float v, uint8_t mat)
{
	int n = 2 + (int)(r * c->s * fabsf(a1 - a0) / 4.0f);
	if (n > 32)
		n = 32;
	float px = x + cosf(a0) * r, py = y + sinf(a0) * r;
	for (int i = 1; i <= n; i++) {
		float a = a0 + (a1 - a0) * i / n;
		float qx = x + cosf(a) * r, qy = y + sinf(a) * r;
		hl(c, px, py, qx, qy, v, mat);
		px = qx;
		py = qy;
	}
}

static void carve_crown(us_canvas *c, const tree_t *tr, float x, float y, float R, float keep, int lobed)
{
	float lmin = lobed ? tr->lmin : 1.0f, lmax = lobed ? tr->lmax : 1.0f;
	float rmin2 = R * R * lmin * lmin, rmax2 = R * R * lmax * lmax;
	float E = R * lmax, inv = 1.0f / c->s;
	int y0 = (int)floorf((y - E) * c->s), y1 = (int)ceilf((y + E) * c->s);
	int k8 = (int)(keep * 256.0f);
	for (int j = y0 < 0 ? 0 : y0; j <= y1 && j < c->h; j++) {
		float dy = (j + 0.5f) * inv - y, dy2 = dy * dy;
		if (dy2 > rmax2)
			continue;
		/* row span of the outer disc, and of the inner disc that needs no outline test */
		float ho = sqrtf(rmax2 - dy2), hi = dy2 < rmin2 ? sqrtf(rmin2 - dy2) : -1.0f;
		int i0 = (int)floorf((x - ho) * c->s), i1 = (int)ceilf((x + ho) * c->s);
		int a0 = hi >= 0 ? (int)ceilf((x - hi) * c->s - 0.5f) : 1 << 30, a1 = hi >= 0 ? (int)floorf((x + hi) * c->s - 0.5f) : -(1 << 30);
		if (i0 < 0)
			i0 = 0;
		if (i1 > c->w - 1)
			i1 = c->w - 1;
		uint8_t *row = c->ink + (size_t)j * c->w;
		/* the inner run needs no outline test: a tight loop the compiler vectorises */
		int b0 = a0 < i0 ? i0 : a0, b1 = a1 > i1 ? i1 : a1;
		if (b0 <= b1) {
			if (k8 == 0)
				memset(row + b0, 0, (size_t)(b1 - b0 + 1));
			else
				for (int i = b0; i <= b1; i++)
					row[i] = (uint8_t)((row[i] * k8) >> 8);
		}
		for (int i = i0; i <= i1; i++) {
			if (i == b0 && b0 <= b1) {
				i = b1;
				continue;
			}
			float dx = (i + 0.5f) * inv - x;
			float r2 = dx * dx + dy2;
			if (r2 > rmax2)
				continue;
			if (lobed) { /* only the rim band needs the lobed outline */
				float th = fast_atan2(dy, dx);
				if (th < 0)
					th += US_TAU;
				float fv = th / US_TAU * TREE_V;
				int k = (int)fv % TREE_V;
				float rr = R * us_lerp(tr->lob[k], tr->lob[(k + 1) % TREE_V], fv - floorf(fv));
				if (r2 > rr * rr)
					continue;
			}
			row[i] = (uint8_t)((row[i] * k8) >> 8);
		}
	}
}

/* ---- countryside ---------------------------------------------------------
 * Fields live in (x, d) space: d = distance back from the floodplain edge, so the patchwork follows the
 * river's meander the way real field boundaries follow a valley. A field is the quad between two
 * hedgerows and two band edges; (p, q) in [0,1]^2 are its local coordinates. */

static float hsh(float a, float b)
{
	uint32_t h = (uint32_t)(int32_t)(a * 1013.0f) * 0x9E3779B1u ^ (uint32_t)(int32_t)(b * 917.0f) * 0x85EBCA77u;
	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 12;
	h *= 0x297A2D39u;
	h ^= h >> 15;
	return (h >> 8) * (1.0f / 16777216.0f);
}

static void bank_at(const st_t *st, float x, float *cc, float *hb)
{
	float f = us_clampf(x / st->A, 0, 1) * (NX - 1.001f);
	int i = (int)f;
	float t = f - i;
	*cc = us_lerp(st->cy[i], st->cy[i + 1], t);
	*hb = us_lerp(st->hb[i], st->hb[i + 1], t);
}

static void land_xy(const st_t *st, int sd, float x, float d, float *ox, float *oy)
{
	float cc, hb;
	bank_at(st, x, &cc, &hb);
	*ox = x;
	*oy = cc + (sd ? 1.0f : -1.0f) * (hb + d);
}

static void fgeo(const st_t *st, int sd, int k, int b, float p, float q, float *x, float *y)
{
	const float(*e)[2] = st->bd[sd][k];
	float da = us_lerp(e[b][0], e[b][1], p), db = us_lerp(e[b + 1][0], e[b + 1][1], p);
	float d = us_lerp(da, db, q);
	float xl = st->hx[sd][k] + st->hlean[sd][k] * d, xr = st->hx[sd][k + 1] + st->hlean[sd][k + 1] * d;
	land_xy(st, sd, us_lerp(xl, xr, p), d, x, y);
}

/* slow cloud shadows drifting over the land, and gusts running through the crops: coarse grids rebuilt
 * once per frame, sampled bilinearly (hundreds of noise calls instead of thousands) */
static void build_land_grids(st_t *st)
{
	for (int j = 0; j < LGH; j++)
		for (int i = 0; i < LGW; i++) {
			float x = st->A * i / (LGW - 1), y = (float)j / (LGH - 1);
			float n = us_noise2(x * 1.1f - st->shade_t, y * 1.5f + st->seed_off);
			/* the phrase swell thins the cloud: at the height of a phrase the sun breaks through */
			float c0 = -0.08f + 0.36f * st->sun;
			st->lshade[j * LGW + i] = 1.0f - 0.5f * us_smooth(c0, c0 + 0.45f, n);
			st->lgust[j * LGW + i] = us_noise2(x * 3.5f - st->wind_t * 0.8f, y * 3.5f);
		}
}

static float lgrid(const st_t *st, const float *g, float x, float y)
{
	float fx = us_clampf(x / st->A, 0, 1) * (LGW - 1.001f), fy = us_clampf(y, 0, 1) * (LGH - 1.001f);
	int i = (int)fx, j = (int)fy;
	float tx = fx - i, ty = fy - j;
	const float *q = g + j * LGW + i;
	return us_lerp(us_lerp(q[0], q[1], tx), us_lerp(q[LGW], q[LGW + 1], tx), ty);
}

static float shade_at(const st_t *st, float x, float y) { return lgrid(st, st->lshade, x, y); }

/* rows across one field (furrows / drills), clipped to the field with a headland margin */
static void field_rows(us_canvas *c, const st_t *st, int sd, int k, int b, float sp, float v, uint8_t mat, float gust,
		       int dashed)
{
	const field_t *f = &st->fd[sd][k][b];
	const float(*e)[2] = st->bd[sd][k];
	float D = 0.5f * ((e[b + 1][0] - e[b][0]) + (e[b + 1][1] - e[b][1]));
	float dm = 0.5f * (e[b][0] + e[b + 1][0]);
	float W = (st->hx[sd][k + 1] - st->hx[sd][k]) + (st->hlean[sd][k + 1] - st->hlean[sd][k]) * dm;
	if (W < 0.02f || D < 0.02f)
		return;
	float ca = f->ca, sa = f->sa;
	float nx = -sa, ny = ca;
	float pr[4] = {0, nx * W, ny * D, nx * W + ny * D};
	float nmin = pr[0], nmax = pr[0];
	for (int i = 1; i < 4; i++) {
		nmin = fminf(nmin, pr[i]);
		nmax = fmaxf(nmax, pr[i]);
	}
	sp *= f->sp;
	int n = (int)((nmax - nmin) / sp);
	if (n > 160)
		n = 160;
	float mx = 0.012f, my = 0.008f;
	for (int i = 0; i < n; i++) {
		float o = nmin + (i + 0.5f) * (nmax - nmin) / n;
		float px0 = nx * o, py0 = ny * o;
		/* Liang-Barsky clip of P0 + s*(ca, sa) to [mx, W-mx] x [my, D-my] */
		float s0 = -4.0f, s1 = 4.0f;
		float lo[2] = {mx, my}, hi[2] = {W - mx, D - my}, p0[2] = {px0, py0}, dd[2] = {ca, sa};
		int ok = 1;
		for (int a = 0; a < 2 && ok; a++) {
			if (fabsf(dd[a]) < 1e-6f) {
				if (p0[a] < lo[a] || p0[a] > hi[a])
					ok = 0;
				continue;
			}
			float ta = (lo[a] - p0[a]) / dd[a], tb = (hi[a] - p0[a]) / dd[a];
			if (ta > tb) {
				float tt = ta;
				ta = tb;
				tb = tt;
			}
			s0 = fmaxf(s0, ta);
			s1 = fminf(s1, tb);
		}
		if (!ok || s1 - s0 < sp)
			continue;
		int m = 1 + (int)((s1 - s0) / 0.04f);
		if (m > 18)
			m = 18;
		float lx = 0, ly = 0;
		for (int j = 0; j <= m; j++) {
			float s = s0 + (s1 - s0) * j / m;
			float X = px0 + ca * s, Y = py0 + sa * s;
			float x, y;
			fgeo(st, sd, k, b, X / W, Y / D, &x, &y);
			if (j > 0 && !(dashed && ((i + j) & 1)) && ((y > -0.01f && y < 1.01f) || (ly > -0.01f && ly < 1.01f))) {
				float vv = v * shade_at(st, x, y);
				if (gust > 0) /* wind running through standing crops: bright waves rolling across */
					vv *= 1.0f + gust * lgrid(st, st->lgust, x, y);
				hl(c, lx, ly, x, y, vv, mat);
			}
			lx = x;
			ly = y;
		}
	}
}

/* a hedge bush from above: a scalloped clump, shaded on its lower right */
/* unit scallops (outer clump arc, inner sunlit lump), precomputed: hedges draw ~1000 bushes a frame */
static const float BUSH_A[2][7] = {{-0.4f, 0.3f, 1.0f, 1.7f, 2.4f, 0, 0}, {-0.1f, 0.9f, 1.9f, 0, 0, 0, 0}};
static float bush_xy[2][7][2];
static int bush_ready;

static void bush(us_canvas *c, int crisp, float x, float y, float r, float v, uint8_t mat)
{
	if (y < -r || y > 1.0f + r)
		return;
	if (crisp) {
		us_disc(c, x, y, r, v * 0.75f, mat);
		us_disc(c, x - r * 0.3f, y - r * 0.3f, r * 0.5f, v, mat);
		return;
	}
	if (!bush_ready) {
		for (int a = 0; a < 2; a++)
			for (int i = 0; i < 7; i++) {
				bush_xy[a][i][0] = cosf(BUSH_A[a][i]);
				bush_xy[a][i][1] = sinf(BUSH_A[a][i]);
			}
		bush_ready = 1;
	}
	for (int i = 0; i < 4; i++)
		hl(c, x + bush_xy[0][i][0] * r, y + bush_xy[0][i][1] * r, x + bush_xy[0][i + 1][0] * r, y + bush_xy[0][i + 1][1] * r,
		   v, mat);
	float ix = x - r * 0.25f, iy = y - r * 0.3f, ir = r * 0.55f;
	for (int i = 0; i < 2; i++)
		hl(c, ix + bush_xy[1][i][0] * ir, iy + bush_xy[1][i][1] * ir, ix + bush_xy[1][i + 1][0] * ir,
		   iy + bush_xy[1][i + 1][1] * ir, v * 0.7f, mat);
}

static void draw_land(us_canvas *c, st_t *st, const us_ctx *ctx, float wind, float bloom)
{
	const us_world *w = ctx->w;
	int crisp = ctx->crisp;
	float dens = ctx->p->density, t = (float)ctx->t;
	float winter = us_world_winter(w), autumn = us_world_season_w(w, 2), spring = us_world_season_w(w, 0),
	      summer = us_world_season_w(w, 1);
	float snow = us_smooth(0.5f, 0.85f, winter);
	float ripe = us_smooth(0.3f, 0.7f, 0.6f * summer + autumn);
	uint8_t crop_m = ripe > 0.5f ? US_MAT_LIGHT : US_MAT_LEAF;
	uint8_t hedge_m = snow > 0.5f ? US_MAT_STEM : US_MAT_LEAF;
	float lush = 1.0f - 0.5f * snow;

	for (int sd = 0; sd < 2; sd++)
		for (int k = 0; k < st->ncol[sd]; k++)
			for (int b = 0; b < NBAND; b++) {
				const field_t *f = &st->fd[sd][k][b];
				/* ink rows (crisp canvases fill the fields per pixel, see the land fill) */
				if (!crisp) {
					float spm = 1.38f - 0.45f * dens;
					if (f->kind == F_PLOUGH)
field_rows(c, st, sd, k, b, 0.0075f * spm, 0.27f + 0.04f * snow, snow > 0.5f ? US_MAT_WATER : US_MAT_EARTH,
							   0, 0);
					else if (f->kind == F_CROP) {
						if (snow > 0.5f)
field_rows(c, st, sd, k, b, 0.013f * spm, 0.2f, US_MAT_WATER, 0, 1);
						else
							field_rows(c, st, sd, k, b, 0.0098f * spm, 0.29f + 0.05f * ripe, crop_m, 0.25f + 0.5f * wind,
								   spring > 0.5f || autumn > 0.6f);
					}
				}
				/* meadow and pasture: grass ticks, wildflowers, sheep */
				if (f->kind == F_MEADOW || f->kind == F_PASTURE) {
					const float(*e)[2] = st->bd[sd][k];
					float D = 0.5f * ((e[b + 1][0] - e[b][0]) + (e[b + 1][1] - e[b][1]));
					float W = st->hx[sd][k + 1] - st->hx[sd][k];
					if (b == NBAND - 1)
						D = fminf(D, 0.4f);
					int meadow = f->kind == F_MEADOW;
					int n = (int)(W * D * (meadow ? 1800.0f : 700.0f) * (0.45f + 0.8f * dens));
					if (n > 260)
						n = 260;
					float sw = 0.6f * wind;
					for (int i = 0; i < n; i++) {
						float hp = hsh(i + f->ph, 1.7f), hq = hsh(i * 1.31f, f->ph + 4.1f), hk = hsh(i * 0.7f, f->ph);
						float x, y;
						fgeo(st, sd, k, b, 0.03f + 0.94f * hp, 0.04f + 0.92f * hq, &x, &y);
						if (y < -0.01f || y > 1.01f)
							continue;
						float sh = shade_at(st, x, y);
						int flower = meadow && hk < 0.55f * (1.0f - snow) * (0.4f + 0.6f * (spring + summer));
						if (flower) {
							/* buttercups, campion, ox-eye daisies; the bass opens them */
							uint8_t fm = hk < 0.2f ? US_MAT_LIGHT : hk < 0.38f ? US_MAT_BLOOM : US_MAT_ACCENT;
							float rr = (0.0011f + 0.0013f * hq) * (0.8f + 0.5f * bloom);
							float v = (0.5f + 0.35f * hp) * sh * (0.75f + 0.25f * bloom);
							if (crisp)
								us_put(c, (int)(x * c->s), (int)(y * c->s), 0.62f + 0.3f * hp, fm);
							else
								us_point(c, x, y, rr, v, fm);
						} else if (!crisp) {
							float L = 0.0045f * (0.6f + 0.4f * hp);
							float a0 = -US_PI * 0.5f + sw * lgrid(st, st->lgust, x, y);
							float v = (0.13f + 0.08f * hq) * sh * lush;
							hl(c, x - L * 0.5f, y, x + cosf(a0 - 0.5f) * L, y + sinf(a0 - 0.5f) * L, v, US_MAT_LEAF);
							hl(c, x + L * 0.5f, y, x + cosf(a0 + 0.5f) * L, y + sinf(a0 + 0.5f) * L, v, US_MAT_LEAF);
						}
					}
				}
				/* autumn: round bales left on the stubble */
				if (f->kind == F_CROP && autumn > 0.5f && snow < 0.5f && b < 2) {
					for (int i = 0; i < 5; i++) {
						float hp = hsh(i + f->ph, 9.3f), hq = hsh(i, f->ph + 2.0f);
						if (hsh(i, f->ph) > 0.7f)
							continue;
						float x, y;
						fgeo(st, sd, k, b, 0.15f + 0.7f * hp, 0.2f + 0.6f * hq, &x, &y);
						if (crisp)
							us_disc(c, x, y, 0.005f, 0.75f, US_MAT_LIGHT);
						else {
							us_ring(c, x, y, 0.0045f, 0.0011f, 0.7f, US_MAT_LIGHT);
							us_ring(c, x, y, 0.002f, 0.0009f, 0.45f, US_MAT_LIGHT);
						}
					}
				}
			}

	/* hedgerows: back from the river between the columns, with a gateway in each */
	float br = crisp ? 0.0085f : 0.0082f, bv = (crisp ? 0.48f : 0.42f) * (0.8f + 0.2f * lush);
	if (crisp && snow > 0.5f)
		bv = 0.72f; /* bare hedges must stand above the bright snowfield fill */
	float step = crisp ? 0.011f : 0.0138f;
	for (int sd = 0; sd < 2; sd++) {
		for (int k = 0; k <= st->ncol[sd]; k++) {
			float hx = st->hx[sd][k];
			if (hx < -0.05f || hx > st->A + 0.05f)
				continue;
			for (float d = D0 + 0.004f; d < 0.7f; d += step) {
				int j = (int)(d / step);
				float hj = hsh(j + k * 17.0f, sd + 3.0f);
				if (fabsf(d - st->hgap[sd][k]) < 0.022f)
					continue;
				float x, y;
				land_xy(st, sd, hx + st->hlean[sd][k] * d + (hj - 0.5f) * 0.004f, d, &x, &y);
				if (y < -0.02f || y > 1.02f)
					continue;
				float sh = shade_at(st, x, y);
				float rr = br * (0.75f + 0.5f * hj) * (hj > 0.94f ? 2.4f : 1.0f);
				float sx = 0.0015f * wind * lgrid(st, st->lgust, x, y);
				bush(c, crisp, x + sx, y, rr, bv * sh, hedge_m);
				if (snow > 0.3f && !crisp)
					harc(c, x - rr * 0.2f, y - rr * 0.2f, rr * 0.6f, 3.4f, 5.6f, 0.5f * snow, US_MAT_ACCENT);
			}
		}
		/* band edges: some hedged, some fenced, some just the change of crop */
		for (int k = 0; k < st->ncol[sd]; k++)
			for (int b = 0; b < NBAND - 1; b++) {
				const field_t *f = &st->fd[sd][k][b];
				if (!f->edge)
					continue;
				float x0, y0, x1, y1;
				fgeo(st, sd, k, b, 0, 1, &x0, &y0);
				fgeo(st, sd, k, b, 1, 1, &x1, &y1);
				float len = fabsf(x1 - x0);
				int n = (int)(len / step);
				float px = x0, py = y0;
				for (int i = 0; i <= n; i++) {
					float p = (i + 0.5f) / (n + 1);
					float x, y;
					fgeo(st, sd, k, b, p, 1, &x, &y);
					if (y < -0.02f || y > 1.02f) {
						px = x;
						py = y;
						continue;
					}
					float sh = shade_at(st, x, y);
					if (f->edge == 1) {
						float hj = hsh(i + k * 7.0f, b + sd * 5.0f);
						bush(c, crisp, x, y, br * (0.7f + 0.5f * hj), bv * 0.9f * sh, hedge_m);
					} else {
						/* post and wire */
						us_point(c, x, y, crisp ? 0.001f : 0.0012f, 0.4f * sh, US_MAT_EARTH);
						if (i > 0 && !crisp)
							hl(c, px, py, x, y, 0.16f * sh, US_MAT_STEM);
					}
					px = x;
					py = y;
				}
			}
	}

	/* the towpath along one bank: a trodden line through the riverside grass */
	{
		int sd = (int)st->path_side;
		float pa[2 * 81], pb[2 * 81];
		for (int i = 0; i <= 80; i++) {
			float x = st->A * i / 80.0f;
			float d = 0.027f + 0.009f * us_noise2(x * 2.3f, st->seed_off + 7.0f);
			land_xy(st, sd, x, d - 0.0032f, &pa[2 * i], &pa[2 * i + 1]);
			land_xy(st, sd, x, d + 0.0032f, &pb[2 * i], &pb[2 * i + 1]);
		}
		if (crisp)
			hpoly(c, pa, 81, 0.42f + 0.2f * snow, snow > 0.5f ? US_MAT_ACCENT : US_MAT_EARTH);
		else {
			hpoly(c, pa, 81, 0.2f + 0.1f * snow, US_MAT_EARTH);
			hpoly(c, pb, 81, 0.16f + 0.1f * snow, US_MAT_EARTH);
		}
	}

	/* sheep grazing: drift a step at a time, heads down; a hard hit sets them trotting */
	{
		int nsh = (int)(st->nsh * (0.4f + 0.6f * dens) + 0.5f);
		float trot = us_sat(ctx->a->kick * 0.8f);
		for (int i = 0; i < nsh; i++) {
			sheep_t *q = &st->sh[i];
			q->step = us_approach(q->step, us_sat(0.5f + 0.5f * sinf(t * 0.4f + q->ph)) * 0.3f + trot, 0.6f, ctx->dt);
			q->hd += ctx->dt * 0.5f * us_noise2(t * 0.15f, q->ph) * (1.0f - q->flee);
			q->flee = us_approach(q->flee, 0.0f, 0.5f, ctx->dt);
			/* a startled flock bolts a few lengths away from the water, then settles */
			float spd = (0.004f * q->step + 0.05f * q->flee * q->flee) * ctx->dt;
			const float(*e)[2] = st->bd[q->side][q->col];
			float W = st->hx[q->side][q->col + 1] - st->hx[q->side][q->col];
			float D = 0.5f * ((e[q->band + 1][0] - e[q->band][0]) + (e[q->band + 1][1] - e[q->band][1]));
			q->p += cosf(q->hd) * spd / fmaxf(W, 0.05f);
			q->q += sinf(q->hd) * spd / fmaxf(D, 0.05f);
			if (q->p < 0.1f || q->p > 0.9f || q->q < 0.12f || q->q > 0.88f) {
				q->hd += US_PI * ctx->dt * 2.0f;
				q->p = us_clampf(q->p, 0.1f, 0.9f);
				q->q = us_clampf(q->q, 0.12f, 0.88f);
			}
			float x, y;
			fgeo(st, q->side, q->col, q->band, q->p, q->q, &x, &y);
			if (y < -0.02f || y > 1.02f)
				continue;
			float ax = cosf(q->hd), ay = sinf(q->hd) * (q->side ? 1.0f : -1.0f);
			float sv = 0.85f * (0.7f + 0.3f * shade_at(st, x, y));
			uint8_t sm = US_MAT_ACCENT;
			if (crisp) {
				us_disc(c, x, y, 0.0055f, sv, sm);
				us_point(c, x + ax * 0.008f, y + ay * 0.008f, 0.001f, 0.35f, US_MAT_EARTH);
			} else {
				float pts[2 * 13];
				for (int v = 0; v <= 12; v++) {
					float a = US_TAU * v / 12.0f;
					float wob = 1.0f + 0.12f * sinf(a * 5.0f + q->ph);
					float ex = cosf(a) * 0.0058f * wob, ey = sinf(a) * 0.0039f * wob;
					pts[2 * v] = x + ax * ex - ay * ey;
					pts[2 * v + 1] = y + ay * ex + ax * ey;
				}
				hpoly(c, pts, 13, sv, sm);
				us_point(c, x, y, 0.0026f, sv * 0.4f, sm);
				us_point(c, x + ax * 0.0074f, y + ay * 0.0074f, 0.0017f, 0.45f, US_MAT_EARTH);
			}
		}
	}
}

/* crisp canvases: the land filled per pixel, every field its own colour and row pattern. The fields barely
 * move (the channel drifts over minutes), so the classification is cached and a quarter of the columns
 * are refreshed each frame; only the water edge is exact every frame. */
static void land_fill_crisp(us_canvas *c, st_t *st, const us_ctx *ctx)
{
	float winter = us_world_winter(ctx->w), autumn = us_world_season_w(ctx->w, 2), summer = us_world_season_w(ctx->w, 1);
	float snow = us_smooth(0.5f, 0.85f, winter);
	float ripe = us_smooth(0.3f, 0.7f, 0.6f * summer + autumn);
	float inv = 1.0f / c->s;
	float spc = 3.0f * inv;
	int full = 0;
	if (!st->lc_ink || st->lc_w != c->w || st->lc_h != c->h) {
		free(st->lc_ink);
		free(st->lc_mat);
		st->lc_ink = malloc((size_t)c->w * c->h);
		st->lc_mat = malloc((size_t)c->w * c->h);
		if (!st->lc_ink || !st->lc_mat) {
			free(st->lc_ink);
			free(st->lc_mat);
			st->lc_ink = st->lc_mat = NULL;
			return;
		}
		st->lc_w = c->w;
		st->lc_h = c->h;
		full = 1;
	}
	st->lc_phase = (st->lc_phase + 1) & 3;
	uint8_t snow_iv = (uint8_t)((snow > 0.5f ? 0.62f : 0.16f) * 255.0f), snow_m = snow > 0.5f ? US_MAT_ACCENT : US_MAT_EARTH;
	for (int px = 0; px < c->w; px++) {
		float x = (px + 0.5f) * inv, cc, h, sl, hb;
		chan(st, x, &cc, &h, &sl);
		int yt = (int)((cc - h) * c->s), yb = (int)((cc + h) * c->s);
		uint8_t *li = st->lc_ink + px, *lm = st->lc_mat + px;
		if (full || ((px + st->lc_phase) & 3) == 0) {
			bank_at(st, x, &cc, &hb);
			int kk[2] = {0, 0};
			uint8_t *ci = li, *cm = lm;
			for (int py = 0; py < c->h; py++, ci += c->w, cm += c->w) {
				float v;
				uint8_t m;
				float y = (py + 0.5f) * inv;
				int sd = y > cc;
				float d = (sd ? y - cc : cc - y) - hb;
				if (d < D0) {
					*ci = snow_iv;
					*cm = snow_m;
					continue;
				}
				int k = kk[sd], nc = st->ncol[sd];
				const float *hx = st->hx[sd], *hlv = st->hlean[sd];
				while (k + 1 < nc && x > hx[k + 1] + hlv[k + 1] * d)
					k++;
				while (k > 0 && x < hx[k] + hlv[k] * d)
					k--;
				kk[sd] = k;
				float xl = hx[k] + hlv[k] * d, xr = hx[k + 1] + hlv[k + 1] * d;
				float p = (x - xl) / (xr - xl);
				const float(*e)[2] = st->bd[sd][k];
				int b = 0;
				while (b < NBAND - 1 && d > e[b + 1][0] + (e[b + 1][1] - e[b + 1][0]) * p)
					b++;
				float da = e[b][0] + (e[b][1] - e[b][0]) * p;
				const field_t *f = &st->fd[sd][k][b];
				float X = x - xl, Y = d - da;
				float pr = (-f->sa * X + f->ca * Y) / (spc * f->sp);
				int stripe = pr - floorf(pr) < 0.42f;
					if (snow > 0.5f) { /* snowfields: furrows show as a faint brighter ribbing */
						v = stripe && f->kind != F_MEADOW && f->kind != F_PASTURE ? 0.85f : 0.62f;
						m = US_MAT_ACCENT;
				} else if (f->kind == F_PLOUGH) {
					v = stripe ? 0.38f : 0.18f;
					m = US_MAT_EARTH;
				} else if (f->kind == F_CROP) {
					v = stripe ? 0.24f : 0.15f;
					m = stripe ? (ripe > 0.5f ? US_MAT_LIGHT : US_MAT_LEAF) : (ripe > 0.5f ? US_MAT_EARTH : US_MAT_STEM);
				} else if (f->kind == F_MEADOW) {
					v = 0.2f;
					m = US_MAT_LEAF;
				} else {
					v = 0.2f;
					m = US_MAT_STEM;
				}
				*ci = (uint8_t)(v * 255.0f + 0.5f);
				*cm = m;
			}
		}
		uint8_t *ink = c->ink + px, *mt = c->mat + px;
		for (int py = 0; py < c->h; py++, ink += c->w, mt += c->w, li += c->w, lm += c->w) {
			uint8_t iv = (py >= yt && py <= yb) ? 23 : *li;
			uint8_t m = (py >= yt && py <= yb) ? US_MAT_WATER : *lm;
			if (iv > *ink) {
				*ink = iv;
				*mt = m;
			}
		}
	}
}

/* ---- life on the water ----------------------------------------------------- */

static void ring_add(st_t *st, float x, float y, float r1, float life, float v, int kind, float delay)
{
	if (st->nrg >= MAX_RING)
		return;
	ring_t *g = &st->rg[st->nrg++];
	g->u = x / st->A;
	g->y = y;
	g->age = -delay;
	g->life = life;
	g->r1 = r1;
	g->v = v;
	g->kind = (uint8_t)kind;
	g->ph = us_rand(&st->rng) * US_TAU;
}

/* rings spread and ease out while the current carries them; a leap also throws a crown of spray */
static void rings_step(us_canvas *c, st_t *st, const us_ctx *ctx)
{
	float dt = ctx->dt, A = st->A, t = (float)ctx->t;
	for (int i = 0; i < st->nrg;) {
		ring_t *g = &st->rg[i];
		g->age += dt;
		if (g->age >= g->life) {
			*g = st->rg[--st->nrg];
			continue;
		}
		i++;
		float x = g->u * A, vx, vy, fo;
		flow(st, x, g->y, t, &vx, &vy, &fo);
		g->u += vx * 0.85f * dt / A;
		g->y += vy * 0.85f * dt;
		if (g->age < 0)
			continue;
		float p = g->age / g->life, q = 1.0f - p;
		float r = 0.0015f + g->r1 * (1.0f - q * q * q);
		float v = g->v * powf(q, 1.6f) * us_sat(g->age / 0.06f);
		/* the disturbed water inside the ring is smoothed flat for a moment: a darker disc that lets the
		 * bright crest read against the streamlines */
		float calm = (g->kind ? 0.6f : 0.4f) * q * q;
		if (calm > 0.05f)
			carve_crown(c, NULL, x, g->y, r * 0.92f, 1.0f - calm, 0);
		if (ctx->crisp) {
			us_ring(c, x, g->y, r, 0.001f, v, US_MAT_WATER);
			if (g->kind == 1 && g->age < 0.3f)
				us_disc(c, x, g->y, 0.004f, 0.9f * (1.0f - g->age / 0.3f), US_MAT_WATER);
		} else {
			harc(c, x, g->y, r, 0.0f, US_TAU, v, US_MAT_WATER);
			harc(c, x, g->y, r + 1.0f / c->s, 0.0f, US_TAU, v * 0.4f, US_MAT_WATER);
			if (g->kind == 0 && r > 0.004f)
				harc(c, x, g->y, r * 0.55f, 0.0f, US_TAU, v * 0.55f, US_MAT_WATER);
		}
		/* spray: droplets flung outward from the leap, falling back within half a second */
		if (g->kind == 1 && g->age < 0.45f) {
			float sp = g->age / 0.45f, e = 1.0f - (1.0f - sp) * (1.0f - sp);
			float dv = 0.9f * (1.0f - sp);
			for (int k = 0; k < 11; k++) {
				float hk = hsh(k * 1.7f, g->ph);
				float a = g->ph + k * (US_TAU / 11.0f) + 0.4f * hk;
				float rr = g->r1 * (0.35f + 0.4f * hk) * e;
				float ca = cosf(a), sa = sinf(a);
				if (ctx->crisp)
					us_put(c, (int)((x + ca * rr) * c->s), (int)((g->y + sa * rr) * c->s), dv, US_MAT_WATER);
				else
					hl(c, x + ca * rr * 0.8f, g->y + sa * rr * 0.8f, x + ca * rr, g->y + sa * rr, dv, US_MAT_WATER);
			}
		}
	}
}

/* a duck from above: teardrop body, head forward, and the V of her wake */
static void duck_draw(us_canvas *c, int crisp, float x, float y, float hd, float L, float v, float wake)
{
	float ax = cosf(hd), ay = sinf(hd);
	/* wake: two arms opening behind, a fainter second chevron further back */
	for (int k = 0; k < 2; k++) {
		float back = L * (0.45f + 1.3f * k), arm = L * (1.3f + 0.6f * k) * (0.6f + 0.6f * wake);
		float bx = x - ax * back, by = y - ay * back;
		float wv = (0.42f - 0.18f * k) * (0.5f + 0.5f * wake);
		for (int sd = -1; sd <= 1; sd += 2) {
			float a = hd + US_PI + sd * 0.42f;
			float ex = bx + cosf(a) * arm, ey = by + sinf(a) * arm;
			if (crisp)
				us_line(c, bx, by, ex, ey, 0.001f, wv * 0.8f, US_MAT_WATER);
			else
				hl(c, bx, by, ex, ey, wv, US_MAT_WATER);
		}
	}
	/* she sits on the water: clear the streamlines under her so a bright surge cannot swallow her */
	carve_crown(c, NULL, x + ax * L * 0.08f, y + ay * L * 0.08f, L * 0.42f, 0.0f, 0);
	if (crisp) {
		us_disc(c, x, y, L * 0.6f, v, US_MAT_ACCENT);
		us_put(c, (int)((x + ax * L * 0.75f) * c->s), (int)((y + ay * L * 0.75f) * c->s), v, US_MAT_STEM);
		return;
	}
	float pts[2 * 13];
	for (int k = 0; k <= 12; k++) {
		float a = US_TAU * k / 12.0f, ca = cosf(a);
		float ex = ca * L * 0.5f, ey = sinf(a) * L * 0.3f * (ca > 0 ? 1.0f : 0.8f + 0.2f * (1.0f + ca));
		pts[2 * k] = x + ax * ex - ay * ey;
		pts[2 * k + 1] = y + ay * ex + ax * ey;
	}
	hpoly(c, pts, 13, v, US_MAT_ACCENT);
	us_point(c, x - ax * L * 0.05f, y - ay * L * 0.05f, L * 0.16f, v * 0.45f, US_MAT_ACCENT);
	us_point(c, x + ax * L * 0.52f, y + ay * L * 0.52f, L * 0.13f, v * 0.9f, US_MAT_STEM);
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *st = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, A = ctx->aspect, t = (float)ctx->t;
	float dens = ctx->p->density;
	float winter = us_world_winter(w), autumn = us_world_season_w(w, 2), spring = us_world_season_w(w, 0);
	us_rng *r = &st->rng;

	if (!st->inited)
		init_scene(st, ctx);

	/* ---- drivers ---- */
	st->ice = us_approach(st->ice, us_smooth(0.55f, 0.95f, winter), 6.0f, dt);
	st->swell_env = us_approach(st->swell_env, us_sat(0.7f * a->bass + 0.6f * a->kick + 0.3f * a->sub), 0.35f, ctx->real_dt);
	float wl = w->water_level;
	/* the water edge only follows the slow phrase swell: bass is shown as current, not as a pulsing
	 * bank (a pulsing edge smears into a glow under the trails) */
	float w_target = 0.085f + 0.13f * wl + 0.02f * a->swell;
	st->width = us_approach(st->width, w_target, 4.0f, dt);
	/* the floodplain terraces follow the water level only slowly; bass swells the water over them */
	st->bank_w = us_approach(st->bank_w, 0.085f + 0.13f * wl + 0.03f, 25.0f, dt);
	float s_target = (0.05f + 0.09f * wl + 0.14f * us_sat(0.6f * a->mid + 0.4f * a->lowmid) + 0.08f * a->level +
			  0.06f * st->swell_env) *
			 (1.0f - 0.35f * st->ice);
	st->speed = us_approach(st->speed, s_target, 0.8f, dt);
	st->flow_t += dt * (0.3f + 4.0f * st->speed);
	float wind = us_sat(0.2f * w->wind + 0.9f * a->high + 0.4f * a->air);
	st->wind_t += dt * (0.5f + 2.0f * wind);
	st->hi_env = us_approach(st->hi_env, us_sat(0.8f * a->high + 0.3f * a->air), 0.25f, ctx->real_dt);
	st->lm_env = us_approach(st->lm_env, us_sat(0.75f * a->lowmid + 0.5f * a->sub), 0.3f, ctx->real_dt);
	st->sun = us_approach(st->sun, a->swell, 2.5f, dt);
	st->leap_cd -= dt;
	st->startle_cd -= dt;

	us_world_add(w, &w->moisture, 0.0012f * (0.25f + a->level) * (1.0f - 0.7f * st->ice), dt);

	build_channel(st, A, t);
	build_grid(st, A, t);

	/* ---- surges: every kick sends a pulse of faster, brighter water downstream ---- */
	if (a->beat && fmaxf(a->kick, 0.6f * a->onset) > 0.2f && st->nsg < MAX_SURGE) {
		surge_t *g = &st->sg[st->nsg++];
		g->x = -0.08f;
		g->amp = us_sat(0.35f + 0.8f * fmaxf(a->kick, 0.6f * a->onset));
	}
	memset(st->sgx, 0, sizeof(st->sgx));
	for (int k = 0; k < st->nsg;) {
		surge_t *g = &st->sg[k];
		g->x += (0.3f + 2.2f * st->speed) * dt;
		g->amp *= expf(-dt * 0.3f);
		if (g->x > A + 0.25f || g->amp < 0.04f) {
			*g = st->sg[--st->nsg];
			continue;
		}
		for (int i = 0; i < NX; i++) {
			float d = (A * i / (NX - 1) - g->x) / 0.085f;
			if (d > -3.0f && d < 3.0f)
				/* steep front, longer tail: a bore rolling downstream */
				st->sgx[i] += g->amp * expf(-d * d * (d > 0 ? 2.0f : 0.5f));
		}
		k++;
	}
	for (int i = 0; i < NX; i++)
		st->sgx[i] = us_sat(st->sgx[i]);
	st->nrocks = 2 + (int)(dens * 4.0f + 0.5f);
	if (st->nrocks > MAX_ROCKS)
		st->nrocks = MAX_ROCKS;
	for (int k = 0; k < st->nrocks; k++) {
		float x = st->rk[k].u * A, cc, h, s;
		chan(st, x, &cc, &h, &s);
		float lat = st->rk[k].s * (1.0f - 0.2f * st->ice);
		st->rock_x[k] = x;
		st->rock_y[k] = cc + lat * h;
	}

	/* ---- eddies: steady shedding + onset bursts ---- */
	float hit = fmaxf(a->onset, a->kick);
	/* each hit sheds a strong eddy pair behind one stone */
	int burst_rock = (a->beat && us_rand(r) < 0.45f + 0.55f * hit) ? (int)(us_rand(r) * st->nrocks) : -1;
	for (int k = 0; k < st->nrocks; k++) {
		rock_t *rk = &st->rk[k];
		rk->shed_t -= dt * (0.25f + 3.0f * st->speed);
		int burst = k == burst_rock;
		if ((rk->shed_t <= 0 || burst) && st->nv < MAX_VORT) {
			float cc, h, s;
			chan(st, st->rock_x[k], &cc, &h, &s);
			float inv = 1.0f / sqrtf(1 + s * s), dx = inv, dy = s * inv;
			rk->sign = -rk->sign;
			float R = rk->r;
			vort_t *v = &st->vt[st->nv++];
			v->x = st->rock_x[k] + dx * R * 1.7f - dy * rk->sign * R * 0.7f;
			v->y = st->rock_y[k] + dy * R * 1.7f + dx * rk->sign * R * 0.7f;
			v->core = R * us_randr(r, 0.8f, 1.3f) * (burst ? 1.25f : 1.0f);
			float strength = burst ? (1.1f + 1.4f * hit) : 0.8f;
			v->g = rk->sign * 9.0f * st->speed * v->core * strength;
			v->life = burst ? us_randr(r, 2.5f, 4.5f) : us_randr(r, 2.0f, 3.5f);
			v->age = 0;
			if (rk->shed_t <= 0)
				rk->shed_t = us_randr(r, 0.8f, 1.6f);
		}
	}
	for (int k = 0; k < st->nv;) {
		vort_t *v = &st->vt[k];
		v->age += dt;
		float cc, h, s;
		chan(st, v->x, &cc, &h, &s);
		float inv = 1.0f / sqrtf(1 + s * s);
		v->x += inv * st->speed * 0.75f * dt;
		v->y += s * inv * st->speed * 0.75f * dt + (cc - v->y) * 0.05f * dt;
		v->core += dt * 0.0025f;
		if (v->age > v->life || v->x > A + 0.1f) {
			*v = st->vt[--st->nv];
			continue;
		}
		k++;
	}

	/* ---- floaters: wipe last frame's drawing so they glide instead of smearing ---- */
	for (int i = 0; i < st->nf; i++)
		if (st->fl[i].dr_ > 0)
			wipe_disc(c, st->fl[i].dx_, st->fl[i].dy_, st->fl[i].dr_, flt_mat[st->fl[i].kind]);

	/* ---- land (crisp fill: a patchwork of fields) + water fill ---- */
	st->shade_t += dt * (0.012f + 0.04f * wind + 0.03f * a->swell);
	build_land_grids(st);
	if (ctx->crisp)
		land_fill_crisp(c, st, ctx);

	/* ---- banks: water edge + terraced contours (the floodplain remembers higher water) ---- */
	{
		float top[2 * NX], bot[2 * NX];
		/* topographic terraces: the floodplain drawn as a contour map */
		float offs[8] = {0.0f, -0.012f, 0.012f, 0.045f, 0.09f, 0.15f, 0.22f, 0.3f};
		float vals[8] = {0.62f, 0.34f, 0.24f, 0.17f, 0.13f, 0.11f, 0.09f, 0.08f};
		int nL = 4; /* the outer terraces would cross the fields: the headland line is the last */
		for (int L = 0; L < nL; L++) {
			int stp = L ? 3 : 1, n = 0;
			for (int i = 0; i < NX; i += stp) {
				float x = A * i / (NX - 1);
				float wob = L ? 0.005f * L * us_noise2(x * (4.0f - 0.3f * L), L * 3.7f + st->seed_off) : 0;
				float o = offs[L] * (1.0f + 0.4f * us_noise2(x * 1.3f, L + 20.0f)) + wob;
				top[2 * n] = x;
				float hh = L ? st->hb[i] : st->hw[i];
				top[2 * n + 1] = st->cy[i] - hh - o;
				bot[2 * n] = x;
				bot[2 * n + 1] = st->cy[i] + hh + o;
				n++;
			}
			float v = vals[L] + 0.15f * winter * (L > 0);
			if (L || !ctx->crisp) { /* the water edge too: a bright hairline reads as well as a 1.2px AA stroke */
				hpoly(c, top, n, v, US_MAT_EARTH);
				hpoly(c, bot, n, v, US_MAT_EARTH);
			} else {
				us_polyline(c, top, n, 0.0017f, v, US_MAT_EARTH);
				us_polyline(c, bot, n, 0.0017f, v, US_MAT_EARTH);
			}
		}
		/* shallow line just inside the water */
		for (int i = 0; i < NX; i++) {
			float x = A * i / (NX - 1);
			float o = st->hw[i] * (0.86f - 0.28f * st->ice) + 0.003f * us_noise2(x * 9.0f, t * 0.4f);
			top[2 * i] = x;
			top[2 * i + 1] = st->cy[i] - o;
			bot[2 * i] = x;
			bot[2 * i + 1] = st->cy[i] + o;
		}
		float sv = 0.16f + 0.5f * st->ice;
		hpoly(c, top, NX, sv, st->ice > 0.3f ? US_MAT_ACCENT : US_MAT_WATER);
		hpoly(c, bot, NX, sv, st->ice > 0.3f ? US_MAT_ACCENT : US_MAT_WATER);
		/* shelf ice hatching in winter */
		if (st->ice > 0.05f) {
			/* irregular frost fractures between the bank and the ice edge */
			for (int i = 0; i < NX; i++) {
				float x = A * i / (NX - 1);
				float h = st->hw[i];
				float d = h * 0.28f * st->ice;
				for (int side = -1; side <= 1; side += 2) {
					float h1 = sinf(i * 12.9898f + side * 4.1f) * 43758.5453f;
					h1 -= floorf(h1);
					if (h1 < 0.5f)
						continue;
					float h2 = h1 * 7.31f - floorf(h1 * 7.31f), h3 = h1 * 13.7f - floorf(h1 * 13.7f);
					float L = d * (0.3f + 0.7f * h2);
					float ang = (h3 - 0.5f) * 1.4f;
					float y0 = st->cy[i] + side * h;
					hl(c, x, y0, x + sinf(ang) * L, y0 - side * cosf(ang) * L, 0.32f * st->ice, US_MAT_ACCENT);
				}
			}
		}
	}

	/* ---- riverside grass tufts (snow-dusted in winter), then the fields beyond ---- */
	{
		int nt = (int)(MAX_TUFTS * (0.3f + 0.7f * dens));
		float leafy = 1.0f - 0.7f * winter;
		for (int i = 0; i < nt; i++) {
			tuft_t *q = &st->tf[i];
			float x, y;
			land_xy(st, i & 1, q->u * A, 0.004f + q->y * (D0 - 0.01f), &x, &y);
			float sz = 0.006f * q->sz;
			float sw = us_noise2(x * 2.0f - st->wind_t * 0.5f, y * 3.0f) * wind * 0.5f;
			uint8_t m = winter > 0.6f ? US_MAT_ACCENT : US_MAT_LEAF;
			float v = (0.2f + 0.12f * q->sz) * (0.6f + 0.4f * leafy) + 0.12f * winter;
			for (int b = -1; b <= 1; b++) {
				float ang = -US_PI * 0.5f + b * 0.55f + sw;
				hl(c, x, y, x + cosf(ang) * sz, y + sinf(ang) * sz, v, m);
			}
		}
	}
	/* a sharp, big hit startles one flock */
	if (a->beat && a->onset > 0.7f && st->startle_cd <= 0 && st->nsh > 0 && us_rand(r) < 0.4f) {
		const sheep_t *s0 = &st->sh[(int)(us_rand(r) * st->nsh * (0.4f + 0.6f * dens))];
		/* they bolt together, roughly toward open grass in the middle of their field */
		float bolt = atan2f(0.5f - s0->q, 0.5f - s0->p) + us_randr(r, -0.8f, 0.8f);
		for (int i = 0; i < st->nsh; i++) {
			sheep_t *q = &st->sh[i];
			if (q->side == s0->side && q->col == s0->col && q->band == s0->band) {
				q->flee = 1.0f;
				q->hd = bolt + 0.35f * us_randn(r);
			}
		}
		st->startle_cd = 4.0f;
	}
	draw_land(c, st, ctx, wind, st->swell_env);

	/* ---- stones + bow waves ---- */
	for (int k = 0; k < st->nrocks; k++) {
		rock_t *rk = &st->rk[k];
		float x = st->rock_x[k], y = st->rock_y[k];
		float cc, h, s;
		chan(st, x, &cc, &h, &s);
		float ang = atanf(s);
		draw_rock(c, ctx->crisp, rk, x, y, 0.72f, 1.0f);
		if (!ctx->crisp)
			draw_rock(c, 0, rk, x - rk->r * 0.15f, y - rk->r * 0.15f, 0.3f, 0.55f);
		if (st->ice > 0.4f) /* snow cap */
			draw_rock(c, ctx->crisp, rk, x - rk->r * 0.1f, y - rk->r * 0.2f, 0.55f * st->ice, 0.5f);
		/* standing waves upstream: arcs facing into the current, brighter when fast */
		float sg = surge_at(st, x);
		float bw = us_sat(st->speed * 5.0f + sg);
		/* the pillow of water piled against the stone: more crests, pushed further out, when a surge hits */
		int nq = 3 + (int)(2.0f * sg + 0.5f);
		for (int q = 0; q < nq; q++) {
			float rr = rk->r * (1.35f + (0.38f + 0.12f * sg) * q) + 0.002f * sinf(t * (6.0f + 6.0f * sg) + q * 2.0f);
			float spread = 1.15f - 0.15f * q + 0.2f * sg;
			float av = (0.55f - 0.1f * q) * (0.4f + 0.6f * bw);
			if (ctx->crisp)
				us_arc(c, x, y, rr, ang + US_PI - spread, ang + US_PI + spread, 0.001f, av, US_MAT_WATER);
			else
				harc(c, x, y, rr, ang + US_PI - spread, ang + US_PI + spread, av * 1.1f, US_MAT_WATER);
		}
		/* white water: foam peels off both shoulders of the stone and rides the wake */
		float frate = (2.0f + 30.0f * st->speed + 70.0f * sg) * (0.4f + 0.8f * dens);
		st->foam_acc += frate * dt;
		while (st->foam_acc >= 1.0f) {
			st->foam_acc -= 1.0f;
			if (st->nfm >= MAX_FOAM)
				continue;
			foam_t *f = &st->fm[st->nfm++];
			float side = us_rand(r) < 0.5f ? -1.0f : 1.0f;
			float th = ang + US_PI + side * us_randr(r, 0.5f, 1.4f);
			float rr = rk->r * us_randr(r, 1.05f, 1.35f);
			f->u = (x + cosf(th) * rr * 1.15f) / A;
			f->y = y + sinf(th) * rr;
			f->pu = f->u;
			f->py = f->y;
			f->life = 1.0f;
			f->decay = us_randr(r, 0.35f, 0.9f);
		}
	}

	/* ---- foam flecks ---- */
	for (int i = 0; i < st->nfm;) {
		foam_t *f = &st->fm[i];
		float x = f->u * A, vx, vy, fo;
		flow(st, x, f->y, t, &vx, &vy, &fo);
		float sg = surge_at(st, x);
		f->pu = f->u;
		f->py = f->y;
		x += vx * (1.0f + 0.9f * sg) * dt;
		f->y += vy * (1.0f + 0.9f * sg) * dt;
		f->u = x / A;
		f->life -= dt * f->decay;
		if (f->life <= 0 || x > A + 0.02f) {
			*f = st->fm[--st->nfm];
			continue;
		}
		float v = 0.85f * us_sat(f->life * 1.6f) * (1.0f - 0.3f * st->ice);
		if (ctx->crisp)
			us_point(c, x, f->y, 0.001f, v, US_MAT_WATER);
		else
			hl(c, f->pu * A, f->py, x, f->y, v, US_MAT_WATER);
		i++;
	}

	/* ---- water-crowfoot: weed beds streaming in the current, waving with the low-mids and sub ---- */
	{
		st->weed_amp = st->lm_env;
		st->weed_t_ += dt * (1.2f + 16.0f * st->speed);
		int nw = 3 + (int)(dens * (MAX_WEED - 3) + 0.5f);
		float fl = (1.0f - winter) * us_smooth(0.3f, 0.7f, spring + us_world_season_w(w, 1));
		float wamp = 0.0025f + 0.009f * st->weed_amp;
		float wv = 0.2f * (1.0f - 0.5f * st->ice);
		for (int i = 0; i < nw && i < MAX_WEED; i++) {
			weed_t *wb = &st->wd[i];
			float x0 = wb->u * A, cc, h, sl;
			chan(st, x0, &cc, &h, &sl);
			float inv = 1.0f / sqrtf(1.0f + sl * sl), dx = inv, dy = sl * inv;
			for (int k = 0; k < wb->n; k++) {
				float rx = x0 + (k - wb->n * 0.5f) * 0.011f, ry = cc + (wb->s + (k - wb->n * 0.5f) * 0.09f) * h;
				int clear = fabsf((ry - cc) / h) < 0.75f;
				for (int m = 0; m < st->nrocks && clear; m++) {
					float ddx = rx - st->rock_x[m], ddy = ry - st->rock_y[m];
					if (ddx * ddx + ddy * ddy < 9.0f * st->rk[m].r * st->rk[m].r)
						clear = 0;
				}
				if (!clear)
					continue;
				float pts[2 * 8], L = wb->len * (0.75f + 0.25f * sinf(wb->ph + k * 2.1f));
				for (int j = 0; j < 8; j++) {
					float f = j / 7.0f;
					float lat = wamp * f * sinf(st->weed_t_ - j * 0.95f + wb->ph + k * 1.3f);
					pts[2 * j] = rx + dx * L * f - dy * lat;
					pts[2 * j + 1] = ry + dy * L * f + dx * lat;
				}
				if (ctx->crisp)
					us_polyline(c, pts, 8, 0.001f, 0.3f * (1.0f - 0.5f * st->ice), US_MAT_LEAF);
				else
					hpoly(c, pts, 8, wv * (1.0f - 0.3f * (k & 1)), US_MAT_LEAF);
				/* in spring and summer the white flowers float on the surface */
				if (fl > 0.05f && (k & 1) == 0) {
					if (ctx->crisp)
						us_put(c, (int)(pts[10] * c->s), (int)(pts[11] * c->s), 0.55f * fl, US_MAT_BLOOM);
					else {
						us_point(c, pts[10], pts[11], 0.0011f, 0.42f * fl, US_MAT_BLOOM);
						us_point(c, pts[14], pts[15], 0.0009f, 0.32f * fl, US_MAT_BLOOM);
					}
				}
			}
		}
	}

	/* ---- streamline particles ---- */
	int want = 450 + (int)(dens * 2300.0f);
	if (ctx->crisp) /* a 1px streak covers far more of a low-res canvas */
		want = want * 11 / 20;
	if (want > MAX_P)
		want = MAX_P;
	while (st->np < want) {
		prt_t *q = &st->p[st->np++];
		respawn(st, q, 0);
		q->age = us_rand(r) * q->life * 0.8f;
	}
	if (st->np > want)
		st->np = want;
	float vnorm = 1.0f / (0.05f + st->speed * 1.6f);
	float energy = us_smooth(0.06f, 0.3f, st->speed);
	float ice_lim = 0.98f - 0.28f * st->ice;
	for (int i = 0; i < st->np; i++) {
		prt_t *q = &st->p[i];
		float x = q->u * A, y = q->y;
		float vx, vy, foam;
		flow(st, x, y, t, &vx, &vy, &foam);
		float sg = surge_at(st, x);
		float vx2 = vx * (1.0f + 0.9f * sg), vy2 = vy * (1.0f + 0.9f * sg);
		float nxp = x + vx2 * dt, nyp = y + vy2 * dt;
		q->age += dt;
		float cc, h, s;
		chan(st, nxp, &cc, &h, &s);
		float lat = (nyp - cc) / h;
		int dead = q->age > q->life || nxp > A + 0.02f || nxp < -0.05f || fabsf(lat) > ice_lim;
		for (int k = 0; k < st->nrocks && !dead; k++) {
			float dx = nxp - st->rock_x[k], dy = nyp - st->rock_y[k];
			if (dx * dx + dy * dy < st->rk[k].r * st->rk[k].r * 0.85f)
				dead = 1;
		}
		if (dead) {
			respawn(st, q, nxp > A ? 0.7f : 0.0f);
			continue;
		}
		float sp = sqrtf(vx2 * vx2 + vy2 * vy2) * vnorm;
		float fade = us_sat(q->age / 0.35f) * us_sat((q->life - q->age) / 0.5f);
		float v = (0.16f + 0.3f * us_sat(sp) + 0.3f * energy + 0.35f * us_sat(foam) + 0.22f * sg) * fade;
		v *= 0.25f + 0.75f * shade_at(st, x, y); /* cloud shadows slide over the water too */
		v *= (1.0f - 0.35f * st->ice) * (ctx->crisp ? 0.85f : 1.0f);
		float px = q->u * A, py = q->y;
		q->pu = q->u;
		q->py = q->y;
		q->u = nxp / A;
		q->y = nyp;
		if (v > 0.02f)
			hl(c, px, py, nxp, nyp, v, US_MAT_WATER);
	}

	/* ---- trout: shadows holding station against the current, darting upstream on hits ---- */
	{
		int nfish = 1 + (int)(dens * 3.0f + 0.5f);
		float vis = 1.0f - 0.65f * st->ice;
		/* a kick: a trout leaps clear and falls back in a splash */
		int leap = -1;
		if (a->beat && a->kick > 0.45f && st->leap_cd <= 0 && us_rand(r) < 0.55f) {
			leap = (int)(us_rand(r) * (nfish < MAX_FISH ? nfish : MAX_FISH));
			st->leap_cd = 0.9f;
		}
		for (int i = 0; i < nfish && i < MAX_FISH; i++) {
			fish_t *f = &st->fish[i];
			/* each fish has a favourite lie it slips back to after a dart */
			float home = (i + 0.5f) / nfish + 0.12f * us_noise2(t * 0.02f, i * 7.1f);
			f->hu = us_approach(f->hu, home, 7.0f, dt);
			f->hs = us_clampf(f->hs + dt * 0.08f * us_noise2(t * 0.07f + i * 3.0f, 1.3f), -0.55f, 0.55f);
			if (a->beat && us_rand(r) < 0.1f + 0.25f * a->onset) {
				f->dart = 1.0f;
				f->hu -= us_randr(r, 0.04f, 0.12f);
				f->hs = us_clampf(f->hs + us_randr(r, -0.3f, 0.3f), -0.55f, 0.55f);
			}
			if (f->hu < 0.05f)
				f->hu = 0.05f;
			if (f->hu > 0.93f)
				f->hu -= 0.002f;
			float tau = 0.3f + 2.5f * (1.0f - f->dart);
			float pu = f->u, ps = f->s;
			f->u = us_approach(f->u, f->hu, tau, dt);
			f->s = us_approach(f->s, f->hs, tau * 1.5f, dt);
			f->dart = us_approach(f->dart, 0.0f, 0.5f, dt);
			f->ph += dt * (4.0f + 14.0f * f->dart + 8.0f * st->speed);
			float x = f->u * A, cc, h, sl;
			chan(st, x, &cc, &h, &sl);
			float y = cc + f->s * h * 0.85f;
			/* never swim over a stone: slide aside (the body is a segment along the current) */
			for (int k = 0; k < st->nrocks; k++) {
				float dxr = x - st->rock_x[k], dyr = y - st->rock_y[k];
				float half = f->len * 0.5f;
				float dxs = dxr - us_clampf(dxr, -half, half);
				float dd = sqrtf(dxs * dxs + dyr * dyr);
				float need = st->rk[k].r * 1.3f + f->len * 0.16f + 0.004f;
				if (dd < need) {
					float sgn = dyr >= 0 ? 1.0f : -1.0f;
					if (fabsf(f->s + sgn * 0.15f) > 0.62f)
						sgn = -sgn;
					float push = (need - dd) / (h * 0.85f);
					f->hs = us_clampf(f->hs + sgn * push * 0.6f, -0.55f, 0.55f);
					f->s = us_clampf(f->s + sgn * push, -0.7f, 0.7f);
					y = cc + f->s * h * 0.85f;
				}
			}
			/* head into the current, turned a little by sideways motion */
			float vs = (f->s - ps) * h / fmaxf(dt, 1e-3f), vu = (f->u - pu) * A / fmaxf(dt, 1e-3f);
			float tgt = US_PI + atanf(sl) - us_clampf(atan2f(vs, fmaxf(0.02f, -vu + 0.02f)), -0.35f, 0.35f);
			f->hd = us_approach(f->hd, tgt, 0.25f, dt);
			if (i == leap && f->gone < 0.3f) {
				float k = us_sat(a->kick);
				ring_add(st, x, y, 0.03f + 0.025f * k, 1.9f, 0.8f, 1, 0.0f);
				ring_add(st, x, y, 0.02f + 0.015f * k, 1.5f, 0.55f, 0, 0.2f);
				f->gone = 1.0f;
				f->dart = 1.0f;
			}
			f->gone = us_approach(f->gone, 0.0f, 0.45f, dt);
			float fv = vis * us_smooth(0.6f, 0.05f, f->gone);
			if (fv > 0.02f)
				fish_draw(c, st->fwt, x, y, f->hd, f->len, sinf(f->ph) * (0.6f + 0.8f * f->dart), fv);
		}
	}

	rings_step(c, st, ctx);

	/* ---- floating leaves (autumn), petals (spring), bits of ice (winter) ---- */
	{
		float rate = 0.1f + 0.9f * autumn + 0.5f * spring + 0.3f * st->ice;
		rate *= 0.4f + 0.9f * dens;
		if (st->nf < MAX_FLOAT && us_rand(r) < dt * rate) {
			flt_t *f = &st->fl[st->nf++];
			float x = us_randr(r, 0, 0.02f) * A;
			float cc, h, s;
			chan(st, x, &cc, &h, &s);
			f->u = x / A;
			f->y = cc + us_randr(r, -0.6f, 0.6f) * h;
			f->rot = us_rand(r) * US_TAU;
			f->vr = us_randr(r, -0.35f, 0.35f);
			f->size = us_randr(r, 0.009f, 0.016f);
			f->life = 1.0f;
			f->dr_ = 0;
			f->kind = (us_rand(r) * (autumn + spring + st->ice + 0.05f) < autumn + 0.05f) ? 0 : (spring > st->ice ? 1 : 2);
		}
		for (int i = 0; i < st->nf;) {
			flt_t *f = &st->fl[i];
			float x = f->u * A, vx, vy, foam;
			flow(st, x, f->y, t, &vx, &vy, &foam);
			x += vx * dt * 0.9f;
			f->y += vy * dt * 0.9f;
			f->u = x / A;
			f->rot += (f->vr + foam * 3.0f) * dt;
			if (x > A + 0.05f) {
				*f = st->fl[--st->nf];
				continue;
			}
			f->dx_ = x;
			f->dy_ = f->y;
			f->dr_ = f->size * 1.25f + 2.0f / c->s;
			if (f->kind == 0)
				leaf_shape(c, ctx->crisp, x, f->y, f->rot, f->size, f->size * 0.45f, 0.85f, US_MAT_ACCENT);
			else if (f->kind == 1) {
				/* a fallen blossom: soft disc + bright eye */
				us_disc(c, x, f->y, f->size * 0.45f, 0.6f, US_MAT_BLOOM);
				us_ring(c, x, f->y, f->size * 0.5f, 0.0012f, 0.85f, US_MAT_BLOOM);
				us_disc(c, x, f->y, f->size * 0.14f, 1.0f, US_MAT_BLOOM);
			} else {
				float pts[10];
				for (int p = 0; p < 5; p++) {
					float pa = f->rot + US_TAU * (p % 4) / 4 + 0.3f * (p & 1);
					pts[2 * p] = x + cosf(pa) * f->size * 0.8f;
					pts[2 * p + 1] = f->y + sinf(pa) * f->size * 0.6f;
				}
				pts[8] = pts[0];
				pts[9] = pts[1];
				us_polyline(c, pts, 5, 0.0012f, 0.7f, US_MAT_ACCENT);
			}
			i++;
		}
	}

	/* ---- a duck and her ducklings paddle the slack water by one bank; they turn at the ends of the
	 *      reach. Stereo width fans the ducklings out from single file ---- */
	if (st->ice < 0.6f) {
		float vel = 0.012f * (1.0f - st->ice);
		st->dk_u += st->dk_dir * vel * dt / A;
		if (st->dk_u < 0.07f)
			st->dk_dir = 1.0f;
		else if (st->dk_u > 0.93f)
			st->dk_dir = -1.0f;
		float side = st->dk_s >= 0 ? 1.0f : -1.0f;
		st->dk_s = side * (0.6f + 0.12f * us_noise2(t * 0.05f, st->seed_off));
		st->dk_spread = us_approach(st->dk_spread, a->width, 1.2f, dt);
		st->dk_hist_t += dt;
		if (st->dk_hist_t >= 0.1f) {
			st->dk_hist_t -= 0.1f;
			st->dkh_i = (st->dkh_i + 1) % DHIST;
			st->dkh[st->dkh_i][0] = st->dk_u;
			st->dkh[st->dkh_i][1] = st->dk_s;
		}
		float dv = (0.8f + 0.15f * w->daylight) * (1.0f - st->ice);
		int ndk = 2 + (int)(dens * (NDUCK - 2) + 0.5f);
		float hx[2], hy[2]; /* this duck's position, and a moment ago */
		for (int k = ndk - 1; k >= 0; k--) {
			for (int e = 0; e < 2; e++) {
				float off = k * 14.0f + e * 5.0f;
				int i0 = (int)off;
				float fr = off - i0;
				const float *p0 = st->dkh[(st->dkh_i - i0 + 2 * DHIST) % DHIST], *p1 = st->dkh[(st->dkh_i - i0 - 1 + 2 * DHIST) % DHIST];
				float u = k == 0 && e == 0 ? st->dk_u : us_lerp(p0[0], p1[0], fr);
				float sl = k == 0 && e == 0 ? st->dk_s : us_lerp(p0[1], p1[1], fr);
				/* ducklings to alternate sides, further out the wider the stereo image */
				if (k > 0)
					sl -= side * ((k & 1) ? 1.0f : -0.6f) * (0.02f + 0.16f * st->dk_spread) * ((k + 1) / 2);
				float cc, h, ss;
				chan(st, u * A, &cc, &h, &ss);
				hx[e] = u * A;
				hy[e] = cc + sl * h;
			}
			float hd = atan2f(hy[0] - hy[1], hx[0] - hx[1]);
			float L = k ? 0.0085f : 0.0145f;
			/* bob a little out of step; paddling harder (brighter wake) against the current */
			float bob = 1.0f + 0.04f * sinf(t * 3.0f + k * 1.7f);
			duck_draw(c, ctx->crisp, hx[0], hy[0], hd, L * bob, dv * (k ? 0.75f : 0.9f),
				  us_sat(0.5f + 0.5f * st->dk_dir * -1.0f + 3.0f * st->speed - 0.3f));
		}
	}

	/* ---- sun glints: sparkles on the moving water, livelier with the highs ---- */
	{
		float sun = 0.25f + 0.75f * w->daylight;
		float grate = (5.0f + 55.0f * us_sat(0.5f * a->high + 0.6f * a->air) + 25.0f * energy) * sun *
			      (0.3f + 0.9f * dens) * (1.0f - 0.5f * st->ice);
		st->glint_acc += grate * dt;
		while (st->glint_acc >= 1.0f) {
			st->glint_acc -= 1.0f;
			if (st->ngl >= MAX_GLINT)
				continue;
			glint_t *g = &st->gl[st->ngl++];
			float x = us_rand(r) * A, cc, h, sl;
			chan(st, x, &cc, &h, &sl);
			g->u = x / A;
			g->y = cc + us_randr(r, -0.75f, 0.75f) * h;
			g->age = 0;
			g->life = us_randr(r, 0.15f, 0.45f);
			/* bright, airy music sharpens the sparkle into bigger stars */
			g->sz = us_randr(r, 0.003f, 0.009f) * (0.7f + 0.7f * a->centroid);
			if (shade_at(st, x, g->y) < 0.8f) /* no sun on the water under a cloud */
				st->ngl--;
		}
		for (int i = 0; i < st->ngl;) {
			glint_t *g = &st->gl[i];
			g->age += ctx->real_dt;
			if (g->age >= g->life) {
				*g = st->gl[--st->ngl];
				continue;
			}
			float x = g->u * A, cc, h, sl, vx, vy, fo;
			flow(st, x, g->y, t, &vx, &vy, &fo);
			g->u += vx * dt / A;
			g->y += vy * dt;
			float v = sinf(US_PI * g->age / g->life);
			v = v * v * (0.75f + 0.25f * sun);
			(void)cc, (void)h, (void)sl;
			if (ctx->crisp) {
				us_point(c, x, g->y, 0.001f, v, US_MAT_LIGHT);
				if (g->sz > 0.006f && v > 0.5f) {
					float q = 1.5f / c->s;
					us_line(c, x - q, g->y, x + q, g->y, 0.001f, v * 0.6f, US_MAT_LIGHT);
					us_line(c, x, g->y - q, x, g->y + q, 0.001f, v * 0.6f, US_MAT_LIGHT);
				}
			} else {
				float L = g->sz * (0.5f + 0.5f * v);
				hl(c, x - L, g->y, x + L, g->y, v, US_MAT_LIGHT);
				hl(c, x, g->y - L * 0.7f, x, g->y + L * 0.7f, v, US_MAT_LIGHT);
				hl(c, x - L * 0.3f, g->y - L * 0.3f, x + L * 0.3f, g->y + L * 0.3f, v * 0.5f, US_MAT_LIGHT);
				hl(c, x - L * 0.3f, g->y + L * 0.3f, x + L * 0.3f, g->y - L * 0.3f, v * 0.5f, US_MAT_LIGHT);
			}
			i++;
		}
	}

	/* ---- reed clumps on the banks: fans of blades seen from above, swaying with highs ---- */
	{
		int nr = (int)(MAX_REEDS * (0.3f + 0.7f * dens));
		float green = 1.0f - 0.6f * winter;
		uint8_t rm = winter > 0.6f ? US_MAT_STEM : US_MAT_LEAF;
		for (int i = 0; i < nr; i++) {
			reed_t *d = &st->rd[i];
			float x = d->u * A, cc, h, s;
			chan(st, x, &cc, &h, &s);
			/* rooted on the (slow) bank, not the water edge: in a flood they stand in the shallows */
			float fi = us_clampf(x / A, 0, 1) * (NX - 1);
			float hbk = st->hb[(int)fi];
			float by = cc + d->side * (hbk - 0.014f + d->off);
			/* each clump answers its own band of the mids: the melody runs along the bank as the reeds
			 * nod downstream one after another (quick to bow, slow to stand back up) */
			float nb_ = a->bands[12 + (i * 7) % 11];
			d->bavg = us_approach(d->bavg, nb_, 2.5f, ctx->real_dt);
			float nt = us_sat((nb_ - 0.65f * d->bavg) * 1.6f); /* a new note bows them; a held one lets them half rise */
			d->nod = us_approach(d->nod, nt, nt > d->nod ? 0.07f : 0.45f, ctx->real_dt);
			float sway = us_noise2(x * 2.5f - st->wind_t * 0.7f, d->ph) * (0.1f + 0.55f * wind) +
				     0.1f * sinf(st->wind_t * 2.3f + d->ph) * wind;
			sway += d->side * 0.5f * d->nod;
			float base = (d->side > 0 ? -US_PI * 0.5f : US_PI * 0.5f) + d->lean * 0.5f;
			if (ctx->crisp) {
				/* at pixel scale a fan of 5-8 blades fills in to a blob: three long, distinct blades */
				for (int b = 0; b < 3; b++) {
					float spread = (b - 1) * 0.75f;
					float L = d->len * (b == 1 ? 1.35f : 1.05f) * (0.75f + 0.25f * green);
					float a0 = base + spread + sway * 0.8f;
					float tx = x + cosf(a0) * L, ty = by + sinf(a0) * L;
					us_line(c, x, by, tx, ty, 0.001f, 0.62f + 0.12f * green, rm);
					if (b == 1 && (i % 2) == 0)
						us_point(c, x + cosf(a0) * L * 0.85f, by + sinf(a0) * L * 0.85f, 0.0012f, 0.7f, US_MAT_STEM);
				}
				continue;
			}
			int nb = 5 + (i % 4);
			for (int b = 0; b < nb; b++) {
				float hsh = sinf(i * 12.9898f + b * 78.233f) * 43758.5f;
				hsh -= floorf(hsh);
				float spread = (b / (float)(nb - 1) - 0.5f) * 2.2f;
				float L = d->len * (0.55f + 0.45f * hsh) * (0.75f + 0.25f * green);
				float a0 = base + spread + sway * (0.6f + 0.4f * hsh);
				float bend = 0.35f * (spread > 0 ? 1.0f : -1.0f) * (0.3f + hsh) + sway * 0.6f;
				float mx = x + cosf(a0) * L * 0.5f, my = by + sinf(a0) * L * 0.5f;
				float tx = mx + cosf(a0 + bend) * L * 0.5f, ty = my + sinf(a0 + bend) * L * 0.5f;
				float pts[6] = {x, by, mx, my, tx, ty};
				hpoly(c, pts, 3, 0.42f + 0.12f * green + 0.1f * hsh, rm);
				/* a few cattail heads stand up from the clump */
				if (b == nb / 2 && (i % 2) == 0) {
					float hx = x + cosf(a0) * L * 0.95f, hy = by + sinf(a0) * L * 0.95f;
					us_line(c, x, by, hx, hy, 0.0011f, 0.5f, US_MAT_STEM);
					us_line(c, x + cosf(a0) * L * 0.7f, by + sinf(a0) * L * 0.7f, hx, hy, 0.0036f, 0.62f, US_MAT_STEM);
				}
			}
		}
	}

	/* ---- mayflies: little swarms dancing over the water by the banks; the highs quicken the dance and
	 *      bring more out, each fly lit by its own treble band; a fly that touches down brings a rise ---- */
	{
		float he = st->hi_env;
		int nfly = 5 + (int)((MAX_FLY - 5) * us_sat(0.1f + 0.9f * he) * (0.4f + 0.6f * dens));
		if (st->ice > 0.5f)
			nfly = 0;
		float tw = t * (0.8f + 2.2f * he);
		float rr = 0.2f + 2.2f * he; /* rises per second */
		st->rise_acc += rr * dt * (nfly > 0);
		int rise = -1;
		if (st->rise_acc >= 1.0f) {
			st->rise_acc -= 1.0f;
			rise = (int)(us_rand(r) * nfly);
		}
		for (int i = 0; i < nfly && i < MAX_FLY; i++) {
			int sw = i % 3;
			float su = 0.2f + 0.3f * sw + 0.08f * us_noise2(t * 0.03f, sw * 5.0f + st->seed_off);
			float cx0 = su * A, cc, h, sl;
			chan(st, cx0, &cc, &h, &sl);
			float cy0 = cc + ((sw & 1) ? 0.62f : -0.62f) * h;
			float h1 = hsh(i, 3.3f), h2 = hsh(i, 7.7f), h3 = hsh(i, 1.1f);
			float x = cx0 + 0.05f * sinf(tw * (0.7f + 0.6f * h1) + h2 * 20.0f) + 0.012f * sinf(tw * 2.3f + h3 * 9.0f);
			float y = cy0 + 0.026f * sinf(tw * (0.5f + 0.5f * h2) + h1 * 20.0f) + 0.006f * cosf(tw * 3.1f + h2 * 7.0f);
			float bnd = a->bands[22 + i % 9];
			st->flyv[i] = us_approach(st->flyv[i], 0.35f + 0.65f * bnd, bnd > st->flyv[i] ? 0.05f : 0.3f, ctx->real_dt);
			/* the up-and-down dance shows from above as wings catching the light */
			float v = st->flyv[i] * (0.55f + 0.45f * sinf(tw * 4.0f + h3 * 30.0f)) * (i < nfly - 1 ? 1.0f : 0.5f);
			if (ctx->crisp) { /* a low-res pixel is a big fly: half of them, and dimmer */
				if ((i & 1) == 0 && v > 0.3f)
					us_put(c, (int)(x * c->s), (int)(y * c->s), 0.3f + 0.45f * v, US_MAT_LIGHT);
			}
			else {
				float wl = 1.6f / c->s;
				hl(c, x - wl, y, x + wl, y, v * 0.5f, US_MAT_LIGHT);
			}
			if (i == rise)
				ring_add(st, x, y, 0.012f + 0.01f * h2, 1.3f, 0.5f, 0, 0.0f);
		}
	}

	/* ---- trees on the banks, seen from above: lobed crowns overhanging the water, casting shade;
	 *      golden in autumn, bare branches in winter ---- */
	{
		int ntree = 3 + (int)(dens * 9.0f + 0.5f);
		if (ntree > MAX_TREES)
			ntree = MAX_TREES;
		float bare = us_smooth(0.45f, 0.85f, winter);
		float gold = us_smooth(0.35f, 0.8f, autumn);
		uint8_t cm = gold > 0.5f ? US_MAT_LIGHT : US_MAT_LEAF;
		for (int i = 0; i < ntree; i++) {
			tree_t *tr = &st->tree[i];
			float x = tr->u * A, cc, h, sl;
			chan(st, x, &cc, &h, &sl);
			float hbk = st->hb[(int)(us_clampf(tr->u, 0, 1) * (NX - 1))];
			float y = cc + tr->side * (hbk + tr->off);
			float sw = 0.004f * (0.3f + wind);
			float ox = sw * us_noise2(st->wind_t * 0.6f, tr->ph), oy = sw * 0.6f * us_noise2(tr->ph, st->wind_t * 0.6f);
			float R = tr->r * (0.75f + 0.25f * (1.0f - bare));
			float crown = 1.0f - bare;
			/* the crown's shadow falls on the water, down and to the right of it */
			if (crown > 0.05f) {
				carve_crown(c, tr, x + R * 0.35f, y + R * 0.3f, R * 0.95f, us_lerp(1.0f, 0.45f, crown), 0);
				carve_crown(c, tr, x + ox, y + oy, R * (0.6f + 0.4f * crown), us_lerp(1.0f, 0.0f, crown), 1);
			}
			float cx = x + ox, cy = y + oy;
			if (crown > 0.05f) {
				float pts[2 * (TREE_V + 1)];
				for (int v = 0; v <= TREE_V; v++) {
					int k = v % TREE_V;
					float th = US_TAU * k / TREE_V;
					pts[2 * v] = cx + cosf(th) * R * tr->lob[k];
					pts[2 * v + 1] = cy + sinf(th) * R * tr->lob[k];
				}
				if (ctx->crisp) {
					for (int v = 0; v < TREE_V; v++)
						us_tri(c, cx, cy, pts[2 * v], pts[2 * v + 1], pts[2 * v + 2], pts[2 * v + 3], 0.32f * crown, cm);
					/* sunlit upper-left lump */
					us_disc(c, cx - R * 0.22f, cy - R * 0.22f, R * 0.5f, 0.5f * crown, cm);
					us_disc(c, cx - R * 0.34f, cy - R * 0.32f, R * 0.22f, 0.7f * crown, cm);
					us_polyline(c, pts, TREE_V + 1, 0.001f, 0.45f * crown, cm);
				} else {
					hpoly(c, pts, TREE_V + 1, 0.5f * crown, cm);
					/* leaf clumps: little crescents on their shaded (lower-right) side, the way a crown is
					 * drawn from above; they shiver with the wind */
					int nq = 26;
					for (int q = 0; q < nq; q++) {
						float h1 = sinf(q * 12.9898f + tr->ph * 3.1f) * 43758.5453f;
						h1 -= floorf(h1);
						float h2 = h1 * 9.17f - floorf(h1 * 9.17f);
						float rr = R * 0.82f * sqrtf(h1), th = US_TAU * h2;
						float qx = cx + cosf(th) * rr, qy = cy + sinf(th) * rr;
						float qr = R * (0.13f + 0.08f * h2) * (1.0f - 0.35f * rr / R);
						float jit = 0.25f * wind * us_noise2(st->wind_t * 1.3f, q + tr->ph);
						harc(c, qx, qy, qr, -0.2f + jit, 2.0f + jit, (0.42f - 0.18f * rr / R) * crown, cm);
					}
				}
			}
			/* limbs: faint through the leaves, the whole picture in winter */
		float lv = (ctx->crisp ? 0.0f : 0.05f) * crown + (ctx->crisp ? 0.72f : 0.5f) * bare;
			if (lv > 0.02f) {
				for (int b = 0; b < 6; b++) {
					float th = tr->ph + b * US_TAU / 6.0f + 0.2f * sinf(tr->ph * 3.0f + b);
					float L1 = R * (0.55f + 0.1f * (b % 3));
					float ex = cx + cosf(th) * L1, ey = cy + sinf(th) * L1;
					hl(c, x, y, ex, ey, lv, US_MAT_STEM);
					for (int k = -1; k <= 1; k += 2) {
						float t2 = th + k * 0.45f;
						hl(c, ex, ey, ex + cosf(t2) * R * 0.38f, ey + sinf(t2) * R * 0.38f, lv * 0.8f, US_MAT_STEM);
					}
				}
				if (bare > 0.5f)
					us_disc(c, x, y, R * 0.06f, 0.6f * bare, US_MAT_STEM);
			}
		}
	}
}

const us_scene_vt us_scene_river = {"river", "River", 0.42f, create, destroy, frame};
