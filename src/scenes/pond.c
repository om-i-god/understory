/* Understory scene: Pond — a still pond seen from directly above.
 *
 * Under the surface a slow caustic light network (a low-res interference field of a few travelling
 * waves, warped and bent by every ripple) shifts across the bed; koi drift through it. Every drum hit
 * drops a ripple ring — a short wave packet that expands, fades, and interferes with the others both as
 * lines on the surface and as distortion in the caustics. Lily pads (notched discs with veins) drift
 * and bob as rings pass beneath them; water striders skate in bursts, leaving tiny dimple rings from their
 * legs; reed clumps fringe the bank. Around the water: grassy banks, pebbles at the waterline, ferns and
 * marsh flowers, a wooden jetty, and a tree leaning out over the pond whose shade dims the caustics and
 * whose weeping twigs dimple the surface. Deep winter puts an ice skin on the pond: muted rings, dim light,
 * crack lines that creak further on heavy hits.
 *
 * Audio: onsets -> rings (kicks -> big ones); bass -> caustic swell; highs -> striders more restless.
 * World: reads water_level (pond size), insects (striders), season (ice, autumn leaves, lotus flowers
 *        in summer, pads die back in winter), daylight (caustic strength). Writes a little moisture.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define FH 90
#define FWMAX 200
#define CGW 40 /* caustic feature-point grid */
#define CGH 20
#define NB 128 /* pond boundary samples */
#define MAX_RINGS 72
#define MAX_PADS 16
#define MAX_STRIDERS 14
#define MAX_KOI 4
#define KOI_N 10
#define MAX_CLUMPS 26
#define MAX_CRACKS 7
#define CRACK_N 36
#define MAX_LEAVES 14
#define BLIT_WMAX 4096
#define MAX_STRANDS 60 /* the overhanging tree's weeping fringe */
#define TREE_V 48
#define MAX_FERNS 12
#define MAX_PEBBLES 70
#define MAX_TUFTS 260

typedef struct {
	float x, y, r, speed, amp, age, life;
	uint8_t small; /* strider dimples / drips: no caustic contribution */
} ring_t;

typedef struct {
	float u, y, R, rot, notch, vx, vy, vr, bob, bph, flower;
} pad_t;

typedef struct {
	float u, y, hd, vx, vy, stroke_t;
} strider_t;

typedef struct {
	float u, y, hd, speed, ph, len, turn;
	float sx[KOI_N], sy[KOI_N];
	uint8_t mat;
} koi_t;

typedef struct {
	float th, off, len, ph;
} clump_t;

typedef struct {
	float u[CRACK_N], y[CRACK_N];
	int n;
	float grown;
} crack_t;

typedef struct {
	float u, y, rot, vr, size;
} leaf_t;

typedef struct {
	us_rng rng;
	int inited;
	float so;
	float land[FWMAX * FH];
	float qmask[FWMAX * FH];
	float dpx[FWMAX * FH], dpy[FWMAX * FH];
	float cpx[CGW * CGH], cpy[CGW * CGH];
	int mask_fw;
	float mask_scale, mask_A;
	float bnd[NB]; /* boundary radius factor per angle */
	float bmin;
	float scale, bass_env, ice, light_t, wave_t;
	ring_t rg[MAX_RINGS];
	int nrg;
	pad_t pad[MAX_PADS];
	strider_t sd[MAX_STRIDERS];
	koi_t koi[MAX_KOI];
	clump_t cl[MAX_CLUMPS];
	crack_t ck[MAX_CRACKS];
	leaf_t lf[MAX_LEAVES];
	int nlf;
	/* vector caustics: per grid quad, circumcentres of its two triangles + which triangle owns each side */
	float ccx[CGH][CGW - 1][2], ccy[CGH][CGW - 1][2];
	uint8_t own[CGH][CGW - 1][4]; /* top, right, bottom, left -> triangle 0/1 */
	float drip_t;
	float A;
	/* the banks */
	float wx, wy; /* willow trunk */
	float sa[MAX_STRANDS], sl[MAX_STRANDS], sph[MAX_STRANDS], sr0[MAX_STRANDS], wR;
	float lob[TREE_V];
	float jth; /* the jetty's bearing */
	float fx[MAX_FERNS], fy[MAX_FERNS], fs[MAX_FERNS], fph[MAX_FERNS];
	int nfern;
	float pb_th[MAX_PEBBLES], pb_k[MAX_PEBBLES], pb_r[MAX_PEBBLES], pb_a[MAX_PEBBLES];
	float tu_x[MAX_TUFTS], tu_y[MAX_TUFTS];
	float wind_t;
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x706f6e64u);
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

/* cheap cosine (~0.1% error) for the caustic field */
static inline float fcos(float a)
{
	float x = a * 0.15915494f + 0.25f; /* cos(a) = sin(a + pi/2), in cycles */
	x -= floorf(x + 0.5f);              /* [-0.5, 0.5] */
	float s = x * (8.0f - 16.0f * fabsf(x));
	return 0.225f * (s * fabsf(s) - s) + s;
}

/* ---- pond geometry: a soft blob centred on the frame ---- */
static void pond_radii(const st_t *st, float *rx, float *ry)
{
	*rx = st->A * 0.5f * st->scale;
	*ry = 0.5f * st->scale * 1.02f;
}

/* > 1 outside, < 1 inside */
static float pond_q(const st_t *st, float x, float y)
{
	float rx, ry;
	pond_radii(st, &rx, &ry);
	float dx = (x - st->A * 0.5f) / rx, dy = (y - 0.5f) / ry;
	float th = atan2f(dy, dx);
	float f = (th + US_PI) / US_TAU * NB;
	int i = (int)f;
	float t = f - i;
	float b = us_lerp(st->bnd[i % NB], st->bnd[(i + 1) % NB], t);
	return sqrtf(dx * dx + dy * dy) / b;
}

static void pond_pt(const st_t *st, float th, float k, float *x, float *y)
{
	float rx, ry;
	pond_radii(st, &rx, &ry);
	float f = (th + US_PI) / US_TAU * NB;
	while (f < 0)
		f += NB;
	int i = (int)f;
	float t = f - i;
	float b = us_lerp(st->bnd[i % NB], st->bnd[(i + 1) % NB], t) * k;
	*x = st->A * 0.5f + cosf(th) * rx * b;
	*y = 0.5f + sinf(th) * ry * b;
}

static int in_pad(const st_t *st, int npads, float x, float y, float grow)
{
	for (int k = 0; k < npads; k++) {
		const pad_t *p = &st->pad[k];
		float dx = x - p->u * st->A, dy = y - p->y;
		if (dx * dx + dy * dy < p->R * p->R * grow * grow)
			return 1;
	}
	return 0;
}

static void add_ring(st_t *st, float x, float y, float amp, float speed, float life, int small)
{
	if (st->nrg >= MAX_RINGS) {
		/* replace the oldest */
		int o = 0;
		for (int k = 1; k < st->nrg; k++)
			if (st->rg[k].age / st->rg[k].life > st->rg[o].age / st->rg[o].life)
				o = k;
		st->rg[o] = st->rg[--st->nrg];
	}
	ring_t *g = &st->rg[st->nrg++];
	g->x = x;
	g->y = y;
	g->r = 0.002f;
	g->amp = amp;
	g->speed = speed;
	g->age = 0;
	g->life = life;
	g->small = (uint8_t)small;
}

static void random_water_point(st_t *st, int npads, float *x, float *y)
{
	for (int tries = 0; tries < 12; tries++) {
		float th = us_rand(&st->rng) * US_TAU, k = sqrtf(us_rand(&st->rng)) * 0.85f;
		pond_pt(st, th, k, x, y);
		if (!in_pad(st, npads, *x, *y, 1.1f))
			return;
	}
}

static void new_pad(st_t *st, pad_t *p, float winter)
{
	us_rng *r = &st->rng;
	float x, y;
	pond_pt(st, us_rand(r) * US_TAU, us_randr(r, 0.25f, 0.8f), &x, &y);
	p->u = x / st->A;
	p->y = y;
	p->R = us_randr(r, 0.028f, 0.058f);
	p->rot = us_rand(r) * US_TAU;
	p->notch = us_randr(r, 0.35f, 0.6f);
	p->vx = p->vy = 0;
	p->vr = us_randr(r, -0.04f, 0.04f);
	p->bob = 0;
	p->bph = us_rand(r) * US_TAU;
	p->flower = us_rand(r) < 0.35f * (1.0f - winter) ? us_randr(r, 0.5f, 1.0f) : 0.0f;
}

static void grow_crack(st_t *st, crack_t *k)
{
	us_rng *r = &st->rng;
	float x, y;
	pond_pt(st, us_rand(r) * US_TAU, us_randr(r, 0.1f, 0.7f), &x, &y);
	float hd = us_rand(r) * US_TAU;
	k->n = CRACK_N;
	for (int i = 0; i < CRACK_N; i++) {
		k->u[i] = x / st->A;
		k->y[i] = y;
		hd += us_randn(r) * 0.35f;
		float step = us_randr(r, 0.008f, 0.02f);
		x += cosf(hd) * step;
		y += sinf(hd) * step;
	}
	k->grown = us_randr(r, 0.1f, 0.4f);
}

static void init_scene(st_t *st, const us_ctx *ctx)
{
	us_rng *r = &st->rng;
	us_world *w = ctx->w;
	st->A = ctx->aspect;
	st->scale = 0.8f + 0.2f * w->water_level;
	st->ice = us_smooth(0.6f, 0.9f, us_world_winter(w));
	for (int i = 0; i < NB; i++) {
		float th = US_TAU * i / NB;
		st->bnd[i] = 1.0f + 0.13f * us_noise2(cosf(th) * 1.1f + st->so, sinf(th) * 1.1f) +
			     0.03f * us_noise2(cosf(th) * 3.5f, sinf(th) * 3.5f + st->so);
	}
	st->bmin = 10.0f;
	for (int i = 0; i < NB; i++)
		if (st->bnd[i] < st->bmin)
			st->bmin = st->bnd[i];
	float winter = us_world_winter(w);
	for (int i = 0; i < MAX_PADS; i++)
		new_pad(st, &st->pad[i], winter);
	for (int i = 0; i < MAX_STRIDERS; i++) {
		float x, y;
		pond_pt(st, us_rand(r) * US_TAU, us_randr(r, 0.1f, 0.8f), &x, &y);
		st->sd[i].u = x / st->A;
		st->sd[i].y = y;
		st->sd[i].hd = us_rand(r) * US_TAU;
		st->sd[i].stroke_t = us_rand(r) * 2.0f;
	}
	for (int i = 0; i < MAX_KOI; i++) {
		koi_t *k = &st->koi[i];
		float x, y;
		pond_pt(st, us_rand(r) * US_TAU, us_randr(r, 0.2f, 0.6f), &x, &y);
		k->u = x / st->A;
		k->y = y;
		k->hd = us_rand(r) * US_TAU;
		k->speed = us_randr(r, 0.025f, 0.045f);
		k->ph = us_rand(r) * US_TAU;
		k->len = us_randr(r, 0.09f, 0.13f);
		k->turn = us_randr(r, -0.3f, 0.3f);
		k->mat = (i & 1) ? US_MAT_BLOOM : US_MAT_ACCENT;
		for (int j = 0; j < KOI_N; j++) {
			k->sx[j] = x - cosf(k->hd) * k->len * j / (KOI_N - 1);
			k->sy[j] = y - sinf(k->hd) * k->len * j / (KOI_N - 1);
		}
	}
	for (int i = 0; i < MAX_CLUMPS; i++) {
		st->cl[i].th = (i + us_rand(r) * 0.8f) / MAX_CLUMPS * US_TAU;
		st->cl[i].off = us_randr(r, 1.0f, 1.06f);
		st->cl[i].len = us_randr(r, 0.025f, 0.055f);
		st->cl[i].ph = us_rand(r) * 20.0f;
	}
	for (int i = 0; i < MAX_CRACKS; i++)
		grow_crack(st, &st->ck[i]);
	/* the willow stands in the corner with the most bank */
	{
		float A = st->A, best = -1, bth = 0;
		for (int k = 0; k < 4; k++) {
			float x = (k & 1) ? A * 0.955f : A * 0.045f, y = (k & 2) ? 0.93f : 0.07f;
			float q = pond_q(st, x, y);
			if (q > best) {
				best = q;
				bth = atan2f((y - 0.5f) / 0.5f, (x - A * 0.5f) / (A * 0.5f));
			}
		}
		pond_pt(st, bth, 1.12f, &st->wx, &st->wy);
		st->wx = us_clampf(st->wx, A * 0.07f, A * 0.93f);
		st->wy = us_clampf(st->wy, 0.1f, 0.9f);
		for (int v = 0; v < TREE_V; v++) {
			float th = US_TAU * v / TREE_V;
			st->lob[v] = 1.0f + 0.06f * cosf(2 * th + st->so) + 0.05f * cosf(3 * th + 1.3f * st->so) + 0.08f * fabsf(sinf(4.5f * th + st->so));
		}
		/* the jetty juts in from a side away from the tree */
		/* on the long side opposite the tree, a little toward the far end */
		st->jth = (sinf(bth) < 0 ? 1.0f : -1.0f) * US_PI * 0.5f + (cosf(bth) < 0 ? -1.0f : 1.0f) * (sinf(bth) < 0 ? -1.0f : 1.0f) * us_randr(r, 0.15f, 0.45f);
		/* a dome of hanging strands seen from above: they start across the crown (most near its rim) and
		 * cascade outward past it */
		st->wR = us_randr(r, 0.12f, 0.15f);
		for (int i = 0; i < MAX_STRANDS; i++) {
			st->sa[i] = us_rand(r) * US_TAU;
			st->sr0[i] = st->wR * us_randr(r, 0.82f, 1.0f);
			st->sl[i] = st->wR * us_randr(r, 0.12f, 0.32f);
			st->sph[i] = us_rand(r) * 20.0f;
		}
		/* ferns in the other corners, well back from the water and clear of the willow trunk */
		st->nfern = 0;
		for (int tries = 0; tries < 400 && st->nfern < MAX_FERNS; tries++) {
			float x = us_rand(r) * A, y = us_rand(r);
			float dx = x - st->wx, dy = y - st->wy;
			if (pond_q(st, x, y) < 1.2f || dx * dx + dy * dy < (st->wR + 0.1f) * (st->wR + 0.1f))
				continue;
			int ok = 1;
			for (int k = 0; k < st->nfern && ok; k++) {
				float ex = x - st->fx[k], ey = y - st->fy[k];
				ok = ex * ex + ey * ey > 0.012f;
			}
			if (!ok)
				continue;
			st->fx[st->nfern] = x;
			st->fy[st->nfern] = y;
			st->fs[st->nfern] = us_randr(r, 0.04f, 0.07f);
			st->fph[st->nfern] = us_rand(r) * US_TAU;
			st->nfern++;
		}
	}
	for (int i = 0; i < MAX_PEBBLES; i++) {
		st->pb_th[i] = us_rand(r) * US_TAU;
		st->pb_k[i] = us_randr(r, 1.025f, 1.1f);
		st->pb_r[i] = us_randr(r, 0.0025f, 0.0075f);
		st->pb_a[i] = us_rand(r) * US_PI;
	}
	for (int i = 0; i < MAX_TUFTS; i++) {
		st->tu_x[i] = us_rand(r);
		st->tu_y[i] = us_rand(r);
	}
	st->inited = 1;
}

/* lily pad: notched disc. crisp -> filled fan; ink -> outline + veins */
static void draw_pad(us_canvas *c, int crisp, const pad_t *p, float x, float y, float R, float v, uint8_t mat)
{
	enum { N = 22 };
	float a0 = p->rot + p->notch * 0.5f, a1 = p->rot + US_TAU - p->notch * 0.5f;
	float pts[2 * (N + 3)];
	pts[0] = x;
	pts[1] = y;
	for (int i = 0; i <= N; i++) {
		float a = a0 + (a1 - a0) * i / N;
		float rr = R * (1.0f + 0.03f * sinf(a * 5.0f + p->bph));
		pts[2 * (i + 1)] = x + cosf(a) * rr;
		pts[2 * (i + 1) + 1] = y + sinf(a) * rr;
	}
	pts[2 * (N + 2)] = x;
	pts[2 * (N + 2) + 1] = y;
	if (crisp) {
		for (int i = 1; i <= N; i++)
			us_tri(c, x, y, pts[2 * i], pts[2 * i + 1], pts[2 * i + 2], pts[2 * i + 3], v * 0.55f, mat);
		hpoly(c, pts + 2, N + 1, v, mat);
	} else {
		hpoly(c, pts, N + 3, v, mat);
		/* veins */
		for (int k = 1; k < 8; k++) {
			float a = a0 + (a1 - a0) * k / 8.0f;
			hl(c, x, y, x + cosf(a) * R * 0.86f, y + sinf(a) * R * 0.86f, v * 0.35f, mat);
		}
	}
}

static void draw_strider(us_canvas *c, int crisp, float x, float y, float hd, float v)
{
	float cx = cosf(hd), sy = sinf(hd);
	float L = 0.0085f;
	/* body */
	us_line(c, x - cx * L, y - sy * L, x + cx * L, y + sy * L, 0.0018f, v, US_MAT_ACCENT);
	/* legs: (forward, side) lengths for front, middle (oars), hind */
	static const float legs[3][4] = {
		{0.6f, 0.4f, 1.1f, 0.9f},    /* front: short, forward-diagonal */
		{0.1f, 1.1f, 1.8f, 1.7f},    /* middle: long oars, sideways */
		{-0.4f, 0.7f, -2.6f, 1.6f},  /* hind: long, trailing back */
	};
	for (int k = 0; k < 3; k++) {
		for (int s = -1; s <= 1; s += 2) {
			float kx = legs[k][0] * L, ky = legs[k][1] * L * s;
			float tx = legs[k][2] * L, ty = legs[k][3] * L * s;
			float jx = x + cx * kx - sy * ky, jy = y + sy * kx + cx * ky;
			float ex = x + cx * tx - sy * ty, ey = y + sy * tx + cx * ty;
			hl(c, x + cx * (0.2f - 0.3f * k) * L, y + sy * (0.2f - 0.3f * k) * L, jx, jy, v * 0.85f, US_MAT_ACCENT);
			hl(c, jx, jy, ex, ey, v * 0.75f, US_MAT_ACCENT);
			/* surface-tension dimple under each foot */
			if (!crisp && k > 0)
				us_ring(c, ex, ey, 0.0022f, 0.001f, v * 0.4f, US_MAT_WATER);
		}
	}
}

/* ---- vector caustics (Ink): the same cellular net as the Pixel field, drawn as its exact Voronoi
 *      edges -- dual of the (jittered-grid) Delaunay triangulation -- so the light is a web of fine
 *      filaments instead of a blurry low-res texture ---- */
static void circumcenter(float ax, float ay, float bx, float by, float cx, float cy, float *ox, float *oy)
{
	float d = 2.0f * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
	if (fabsf(d) < 1e-9f) {
		*ox = (ax + bx + cx) / 3.0f;
		*oy = (ay + by + cy) / 3.0f;
		return;
	}
	float a2 = ax * ax + ay * ay, b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
	*ox = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
	*oy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
}

/* bilinear sample of a FW x FH grid at normalized (x, y) */
static float grid_at(const float *g, int fw, int fh, float A, float x, float y)
{
	float fx = x / A * fw - 0.5f, fy = y * fh - 0.5f;
	fx = us_clampf(fx, 0, fw - 1.001f);
	fy = us_clampf(fy, 0, fh - 1.001f);
	int i = (int)fx, j = (int)fy;
	float tx = fx - i, ty = fy - j;
	const float *q = g + j * fw + i;
	return us_lerp(us_lerp(q[0], q[1], tx), us_lerp(q[fw], q[fw + 1], tx), ty);
}

/* the field shades pixel p with the net at warp(p) = p + off(p); a net point v is therefore drawn at
 * the p that solves p + off(p) = v (two fixed-point steps are plenty for these gentle warps) */
static void unwarp(const st_t *st, int fw, int fh, float lt, float vx, float vy, float *px, float *py)
{
	float x = vx, y = vy;
	for (int it = 0; it < 2; it++) {
		float ox = 0.02f * fcos(y * 6.0f - lt * 0.6f) + 0.01f * fcos(y * 19.0f + lt * 0.7f) +
			   fcos(x * 13.0f + lt * 0.3f) * 0.012f * fcos(y * 11.0f - lt * 0.4f);
		float oy = 0.02f * fcos(x * 7.0f + lt * 0.5f) + 0.012f * fcos(x * 17.0f - lt * 0.8f);
		ox += grid_at(st->dpx, fw, fh, st->A, x, y);
		oy += grid_at(st->dpy, fw, fh, st->A, x, y);
		x = vx - ox;
		y = vy - oy;
	}
	*px = x;
	*py = y;
}

static void caustic_points(st_t *st, float A, float cs, float lt, uint32_t salt, int *gwo, int *gho)
{
	int gw = (int)(A / cs) + 3, gh = (int)(1.0f / cs) + 3;
	if (gw > CGW)
		gw = CGW;
	if (gh > CGH)
		gh = CGH;
	for (int j = 0; j < gh; j++)
		for (int i = 0; i < gw; i++) {
			uint32_t hsh = (uint32_t)(i * 73856093u) ^ (uint32_t)(j * 19349663u) ^ salt;
			hsh = hsh * 2654435761u;
			float h1 = (hsh & 1023) / 1023.0f, h2 = ((hsh >> 10) & 1023) / 1023.0f, h3 = ((hsh >> 20) & 1023) / 1023.0f;
			float ph = h3 * US_TAU + lt * (0.5f + 0.5f * h1);
			st->cpx[j * CGW + i] = (i - 1 + 0.5f + 0.32f * fcos(ph) + 0.12f * (h1 - 0.5f)) * cs;
			st->cpy[j * CGW + i] = (j - 1 + 0.5f + 0.32f * fcos(ph * 1.3f + 1.7f) + 0.12f * (h2 - 0.5f)) * cs;
		}
	*gwo = gw;
	*gho = gh;
}

static void caustic_edge(const st_t *st, us_canvas *c, int fw, int fh, float lt, float cs, float gain, float x0,
			 float y0, float x1, float y1)
{
	float dx = x1 - x0, dy = y1 - y0;
	float L = sqrtf(dx * dx + dy * dy);
	if (L < 1e-4f || L > cs * 2.5f)
		return;
	/* short edges are where the light converges: brighter */
	float v = gain * us_clampf(1.35f - 0.9f * L / cs, 0.35f, 1.25f);
	int n = 3 + (int)(L / cs * 4.0f);
	/* filaments bow gently (symmetric in the endpoints, so an edge keeps its curve frame to frame) */
	float bow = L * 0.14f * fcos((x0 + x1) * 37.0f + (y0 + y1) * 91.0f + lt * 0.6f);
	float nx = -dy / L * bow, ny = dx / L * bow;
	float px, py;
	unwarp(st, fw, fh, lt, x0, y0, &px, &py);
	float m0 = us_smooth(1.0f, 0.9f, grid_at(st->qmask, fw, fh, st->A, px, py));
	for (int k = 1; k <= n; k++) {
		float f = (float)k / n, fm = (k - 0.5f) / n;
		float b = sinf(US_PI * f);
		float qx, qy;
		unwarp(st, fw, fh, lt, x0 + dx * f + nx * b, y0 + dy * f + ny * b, &qx, &qy);
		float m1 = us_smooth(1.0f, 0.9f, grid_at(st->qmask, fw, fh, st->A, qx, qy));
		/* brightest at the nodes where filaments meet */
		float node = 0.5f + 0.7f * (2.0f * fm - 1.0f) * (2.0f * fm - 1.0f);
		float mv = v * node * fminf(m0, m1);
		if (mv > 0.01f)
			hl(c, px, py, qx, qy, mv, US_MAT_WATER);
		px = qx;
		py = qy;
		m0 = m1;
	}
}

static void caustic_lines(st_t *st, us_canvas *c, int gw, int gh, float cs, float lt, int fw, int fh, float gain)
{
	float(*ccx)[CGW - 1][2] = st->ccx, (*ccy)[CGW - 1][2] = st->ccy;
	uint8_t(*own)[CGW - 1][4] = st->own;
	for (int j = 0; j + 1 < gh; j++)
		for (int i = 0; i + 1 < gw; i++) {
			float ax = st->cpx[j * CGW + i], ay = st->cpy[j * CGW + i];
			float bx = st->cpx[j * CGW + i + 1], by = st->cpy[j * CGW + i + 1];
			float cx = st->cpx[(j + 1) * CGW + i], cy = st->cpy[(j + 1) * CGW + i];
			float dx = st->cpx[(j + 1) * CGW + i + 1], dy = st->cpy[(j + 1) * CGW + i + 1];
			float ox, oy;
			circumcenter(ax, ay, bx, by, cx, cy, &ox, &oy);
			float rr = (ax - ox) * (ax - ox) + (ay - oy) * (ay - oy);
			int diag_bc = (dx - ox) * (dx - ox) + (dy - oy) * (dy - oy) >= rr; /* Delaunay: d outside abc */
			if (diag_bc) {
				ccx[j][i][0] = ox;
				ccy[j][i][0] = oy; /* abc: top, left */
				circumcenter(bx, by, dx, dy, cx, cy, &ccx[j][i][1], &ccy[j][i][1]); /* bdc: right, bottom */
				own[j][i][0] = 0, own[j][i][3] = 0, own[j][i][1] = 1, own[j][i][2] = 1;
			} else {
				circumcenter(ax, ay, bx, by, dx, dy, &ccx[j][i][0], &ccy[j][i][0]); /* abd: top, right */
				circumcenter(ax, ay, dx, dy, cx, cy, &ccx[j][i][1], &ccy[j][i][1]); /* adc: left, bottom */
				own[j][i][0] = 0, own[j][i][1] = 0, own[j][i][3] = 1, own[j][i][2] = 1;
			}
		}
	for (int j = 0; j + 1 < gh; j++)
		for (int i = 0; i + 1 < gw; i++) {
			/* the diagonal's dual */
			caustic_edge(st, c, fw, fh, lt, cs, gain, ccx[j][i][0], ccy[j][i][0], ccx[j][i][1], ccy[j][i][1]);
			if (j > 0) { /* shared side with the quad above */
				int t0 = own[j][i][0], t1 = own[j - 1][i][2];
				caustic_edge(st, c, fw, fh, lt, cs, gain, ccx[j][i][t0], ccy[j][i][t0], ccx[j - 1][i][t1], ccy[j - 1][i][t1]);
			}
			if (i > 0) { /* shared side with the quad to the left */
				int t0 = own[j][i][3], t1 = own[j][i - 1][1];
				caustic_edge(st, c, fw, fh, lt, cs, gain, ccx[j][i][t0], ccy[j][i][t0], ccx[j][i - 1][t1], ccy[j][i - 1][t1]);
			}
		}
}

/* multiply the ink inside a triangle (darkens what lies beneath a solid body) */
static void carve_tri(us_canvas *c, float x0, float y0, float x1, float y1, float x2, float y2, float keep)
{
	float s = c->s;
	x0 *= s, y0 *= s, x1 *= s, y1 *= s, x2 *= s, y2 *= s;
	int bx0 = (int)floorf(fminf(x0, fminf(x1, x2))), bx1 = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
	int by0 = (int)floorf(fminf(y0, fminf(y1, y2))), by1 = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
	float d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
	if (fabsf(d) < 1e-6f)
		return;
	float id = 1.0f / d;
	int k8 = (int)(keep * 256.0f);
	for (int j = by0 < 0 ? 0 : by0; j <= by1 && j < c->h; j++)
		for (int i = bx0 < 0 ? 0 : bx0; i <= bx1 && i < c->w; i++) {
			float px = i + 0.5f, py = j + 0.5f;
			float l0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) * id;
			float l1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) * id;
			if (l0 < 0 || l1 < 0 || l0 + l1 > 1)
				continue;
			uint8_t *p = c->ink + (size_t)j * c->w + i;
			*p = (uint8_t)((*p * k8) >> 8);
		}
}

/* a fan-shaped fin: rays from the root, joined by a scalloped edge */
static void fan_fin(us_canvas *c, int crisp, float x, float y, float ang, float spread, float len, float v, uint8_t mat)
{
	enum { R = 5 };
	float tx[R], ty[R];
	for (int k = 0; k < R; k++) {
		float a = ang + spread * (k / (float)(R - 1) - 0.5f);
		float l = len * (0.85f + 0.15f * sinf(US_PI * k / (R - 1)));
		tx[k] = x + cosf(a) * l;
		ty[k] = y + sinf(a) * l;
	}
	if (crisp) {
		for (int k = 0; k + 1 < R; k++)
			us_tri(c, x, y, tx[k], ty[k], tx[k + 1], ty[k + 1], v * 0.75f, mat);
		return;
	}
	for (int k = 0; k < R; k++)
		hl(c, x, y, tx[k], ty[k], v * (k == 0 || k == R - 1 ? 0.8f : 0.4f), mat);
	for (int k = 0; k + 1 < R; k++)
		hl(c, tx[k], ty[k], tx[k + 1], ty[k + 1], v * 0.7f, mat);
}

/* koi from above: a solid body (it shades the light on the bed beneath it), kohaku-style patches,
 * fan pectorals, a flowing two-lobed tail */
static void koi_draw(us_canvas *c, int crisp, koi_t *k, int idx, float kv)
{
	float L[2 * KOI_N], Rr[2 * KOI_N];
	for (int j = 0; j < KOI_N; j++) {
		int jn = j < KOI_N - 1 ? j + 1 : j, jp = j > 0 ? j - 1 : j;
		float tx = k->sx[jp] - k->sx[jn], ty = k->sy[jp] - k->sy[jn];
		float tl = sqrtf(tx * tx + ty * ty) + 1e-6f;
		float nx = -ty / tl, ny = tx / tl;
		float f = (float)j / (KOI_N - 1);
		float wv = sinf(k->ph - f * 4.0f) * 0.012f * f;
		/* blunt head, full shoulders, a slim wrist before the tail */
		float wid = k->len * (0.17f * powf(sinf(US_PI * (0.12f + 0.88f * f)), 0.6f) * (1.0f - 0.55f * f * f) + 0.012f);
		float bx = k->sx[j] + nx * wv, by = k->sy[j] + ny * wv;
		L[2 * j] = bx + nx * wid;
		L[2 * j + 1] = by + ny * wid;
		Rr[2 * j] = bx - nx * wid;
		Rr[2 * j + 1] = by - ny * wid;
	}
	float hdx = k->sx[0] - k->sx[1], hdy = k->sy[0] - k->sy[1];
	float hn = sqrtf(hdx * hdx + hdy * hdy) + 1e-6f;
	hdx /= hn, hdy /= hn;
	float snx = k->sx[0] + hdx * k->len * 0.07f, sny = k->sy[0] + hdy * k->len * 0.07f;
	float ha = atan2f(hdy, hdx);
	uint8_t pm = k->mat == US_MAT_BLOOM ? US_MAT_LIGHT : US_MAT_BLOOM; /* patch colour */

	/* tail: two lobes swishing behind the wrist */
	int e = KOI_N - 1;
	float ta = atan2f(k->sy[e] - k->sy[e - 2], k->sx[e] - k->sx[e - 2]);
	float sw = sinf(k->ph - 4.5f) * 0.45f;
	float fl = k->len * 0.34f;
	for (int s = -1; s <= 1; s += 2)
		fan_fin(c, crisp, k->sx[e - 1], k->sy[e - 1], ta + s * 0.42f + sw, 0.55f, fl, kv * 0.9f, k->mat);
	/* pectoral fans */
	float px0 = k->sx[2], py0 = k->sy[2];
	for (int s = -1; s <= 1; s += 2) {
		float side = ha + s * 1.5708f;
		float rx = px0 + cosf(side) * k->len * 0.12f, ry = py0 + sinf(side) * k->len * 0.12f;
		float fa = ha + US_PI + s * (1.05f + 0.3f * sinf(k->ph * 0.5f + s));
		fan_fin(c, crisp, rx, ry, fa, 0.9f, fl * 0.55f, kv * 0.85f, k->mat);
	}

	if (crisp) {
		for (int j = 0; j < KOI_N - 1; j++) {
			us_tri(c, L[2 * j], L[2 * j + 1], Rr[2 * j], Rr[2 * j + 1], L[2 * j + 2], L[2 * j + 3], kv, k->mat);
			us_tri(c, Rr[2 * j], Rr[2 * j + 1], Rr[2 * j + 2], Rr[2 * j + 3], L[2 * j + 2], L[2 * j + 3], kv, k->mat);
		}
		us_tri(c, L[0], L[1], snx, sny, Rr[0], Rr[1], kv, k->mat);
	} else {
		/* the body hides the caustic web on the bed beneath it */
		for (int j = 0; j < KOI_N - 1; j++) {
			carve_tri(c, L[2 * j], L[2 * j + 1], Rr[2 * j], Rr[2 * j + 1], L[2 * j + 2], L[2 * j + 3], 0.12f);
			carve_tri(c, Rr[2 * j], Rr[2 * j + 1], Rr[2 * j + 2], Rr[2 * j + 3], L[2 * j + 2], L[2 * j + 3], 0.12f);
		}
		carve_tri(c, L[0], L[1], snx, sny, Rr[0], Rr[1], 0.12f);
		/* one closed outline: left flank, round snout, right flank */
		float ol[2 * (2 * KOI_N + 4)];
		int n = 0;
		for (int j = KOI_N - 1; j >= 0; j--) {
			ol[2 * n] = L[2 * j];
			ol[2 * n + 1] = L[2 * j + 1];
			n++;
		}
		float mx = 0.5f * (L[0] + Rr[0]), my = 0.5f * (L[1] + Rr[1]);
		ol[2 * n] = 0.5f * (L[0] + snx) + (L[0] - mx) * 0.25f;
		ol[2 * n + 1] = 0.5f * (L[1] + sny) + (L[1] - my) * 0.25f;
		n++;
		ol[2 * n] = snx;
		ol[2 * n + 1] = sny;
		n++;
		ol[2 * n] = 0.5f * (Rr[0] + snx) + (Rr[0] - mx) * 0.25f;
		ol[2 * n + 1] = 0.5f * (Rr[1] + sny) + (Rr[1] - my) * 0.25f;
		n++;
		for (int j = 0; j < KOI_N; j++) {
			ol[2 * n] = Rr[2 * j];
			ol[2 * n + 1] = Rr[2 * j + 1];
			n++;
		}
		hpoly(c, ol, n, kv, k->mat);
		/* faint dorsal line */
		float sp[2 * KOI_N];
		for (int j = 1; j < KOI_N - 1; j++) {
			sp[2 * (j - 1)] = 0.5f * (L[2 * j] + Rr[2 * j]);
			sp[2 * (j - 1) + 1] = 0.5f * (L[2 * j + 1] + Rr[2 * j + 1]);
		}
		hpoly(c, sp, KOI_N - 2, kv * 0.3f, k->mat);
	}
	/* patches: two or three blots across the back, the same on every frame (seeded by the fish) */
	for (int q = 0; q < 3; q++) {
		float h = sinf(idx * 12.9898f + q * 78.233f) * 43758.5453f;
		h -= floorf(h);
		if (q == 2 && h < 0.5f)
			continue;
		int j0 = 1 + (int)(h * 3.0f) + q * 2;
		int j1 = j0 + 1 + (q == 0);
		if (j1 > KOI_N - 3)
			j1 = KOI_N - 3;
		float bias = (h - 0.5f) * 0.6f, wdt = 0.55f + 0.3f * h;
		for (int j = j0; j < j1; j++) {
			float ax = 0.5f * (L[2 * j] + Rr[2 * j]), ay = 0.5f * (L[2 * j + 1] + Rr[2 * j + 1]);
			float bx2 = 0.5f * (L[2 * j + 2] + Rr[2 * j + 2]), by2 = 0.5f * (L[2 * j + 3] + Rr[2 * j + 3]);
			float hx = 0.5f * (L[2 * j] - Rr[2 * j]), hy = 0.5f * (L[2 * j + 1] - Rr[2 * j + 1]);
			float gx = 0.5f * (L[2 * j + 2] - Rr[2 * j + 2]), gy = 0.5f * (L[2 * j + 3] - Rr[2 * j + 3]);
			float a1x = ax + hx * (bias + wdt), a1y = ay + hy * (bias + wdt), a2x = ax + hx * (bias - wdt), a2y = ay + hy * (bias - wdt);
			float b1x = bx2 + gx * (bias + wdt), b1y = by2 + gy * (bias + wdt), b2x = bx2 + gx * (bias - wdt), b2y = by2 + gy * (bias - wdt);
			if (crisp) {
				us_tri(c, a1x, a1y, a2x, a2y, b1x, b1y, 0.85f, pm);
				us_tri(c, a2x, a2y, b2x, b2y, b1x, b1y, 0.85f, pm);
			} else {
				/* ink: the patch is hatched across the body */
				for (int hk = 0; hk < 4; hk++) {
					float f = (hk + 0.5f) / 4.0f;
					hl(c, us_lerp(a1x, b1x, f), us_lerp(a1y, b1y, f), us_lerp(a2x, b2x, f), us_lerp(a2y, b2y, f), kv * 0.75f, pm);
				}
			}
		}
	}
	/* eyes */
	if (!crisp) {
		float ex = k->sx[0] * 0.65f + k->sx[1] * 0.35f, ey = k->sy[0] * 0.65f + k->sy[1] * 0.35f;
		for (int s = -1; s <= 1; s += 2)
			us_point(c, ex + cosf(ha + s * 1.5708f) * k->len * 0.075f, ey + sinf(ha + s * 1.5708f) * k->len * 0.075f,
				 0.0014f, kv * 1.3f, k->mat);
	}
}
/* ---- the banks: grass, pebbles at the waterline, ferns in the corners, a weeping willow trailing its
 *      strands over the water (the tips dimple the surface) ---- */
static float qgrid(const st_t *st, int fw, float x, float y)
{
	int i = (int)(x / st->A * fw), j = (int)(y * FH);
	if (i < 0 || j < 0 || i >= fw || j >= FH)
		return 2.0f;
	return st->qmask[j * fw + i];
}

static void draw_banks(us_canvas *c, st_t *st, const us_ctx *ctx, float wind, int fw, int npads)
{
	const us_world *w = ctx->w;
	float A = st->A, dt = ctx->dt, dens = ctx->p->density;
	float winter = us_world_winter(w), autumn = us_world_season_w(w, 2);
	float spring = us_world_season_w(w, 0), summer = us_world_season_w(w, 1);
	int crisp = ctx->crisp;
	float green = 1.0f - 0.6f * winter;
	uint8_t gm = winter > 0.6f ? US_MAT_STEM : US_MAT_LEAF;
	st->wind_t += dt * (0.4f + 2.0f * wind);
	(void)npads;

	/* grass on the banks */
	{
		int nt = (int)(MAX_TUFTS * (0.35f + 0.65f * dens) * (crisp ? 0.5f : 1.0f));
		for (int i = 0; i < nt; i++) {
			float x = st->tu_x[i] * A, y = st->tu_y[i];
			float q = qgrid(st, fw, x, y);
			if (q < 1.1f || q > 1.9f)
				continue;
			float sz = (0.005f + 0.004f * st->tu_y[(i * 7 + 1) % MAX_TUFTS]) * (0.7f + 0.3f * green);
			float sw = 0.5f * wind * us_noise2(x * 2.0f - st->wind_t * 0.4f, y * 3.0f);
			float v = (0.2f + 0.1f * st->tu_x[(i * 3 + 2) % MAX_TUFTS]) * (0.7f + 0.3f * green);
			for (int b = -1; b <= 1; b++) {
				float ang = -US_PI * 0.5f + b * 0.55f + sw;
				hl(c, x, y, x + cosf(ang) * sz, y + sinf(ang) * sz, v, gm);
			}
		}
	}

	/* pebbles along the waterline */
	{
		int np = (int)(MAX_PEBBLES * (0.4f + 0.6f * dens));
		for (int i = 0; i < np; i++) {
			float x, y;
			pond_pt(st, st->pb_th[i], st->pb_k[i], &x, &y);
			float rr = st->pb_r[i], ca = cosf(st->pb_a[i]), sa = sinf(st->pb_a[i]);
			float v = 0.32f + 0.25f * (rr / 0.0075f) + 0.2f * winter;
			uint8_t m = winter > 0.6f ? US_MAT_ACCENT : US_MAT_EARTH;
			if (crisp) {
				us_disc(c, x, y, rr * 0.9f, v * 0.8f, m);
				continue;
			}
			float pts[2 * 9];
			for (int k = 0; k <= 8; k++) {
				float a = US_TAU * k / 8.0f;
				float ex = cosf(a) * rr * 1.3f, ey = sinf(a) * rr * 0.85f;
				pts[2 * k] = x + ex * ca - ey * sa;
				pts[2 * k + 1] = y + ex * sa + ey * ca;
			}
			hpoly(c, pts, 9, v, m);
			hl(c, x - rr * 0.5f * ca, y - rr * 0.5f * sa - rr * 0.25f, x + rr * 0.2f * ca, y + rr * 0.2f * sa - rr * 0.35f, v * 0.6f, m);
		}
	}

	/* marsh marigolds (spring) and forget-me-nots along the bank */
	{
		float fl = us_sat(spring + summer * 0.7f) * (1.0f - winter);
		int nf = (int)(60.0f * fl * (0.4f + 0.6f * dens));
		for (int i = 0; i < nf; i++) {
			float h1 = st->tu_x[(i * 11 + 5) % MAX_TUFTS], h2 = st->tu_y[(i * 13 + 7) % MAX_TUFTS];
			float x, y;
			pond_pt(st, h1 * US_TAU, 1.06f + 0.12f * h2, &x, &y);
			uint8_t m = (i % 3) == 0 ? US_MAT_LIGHT : (i % 3) == 1 ? US_MAT_BLOOM : US_MAT_ACCENT;
			float bl = 0.85f + 0.15f * sinf(ctx->t * 0.7f + i);
			if (crisp)
				us_put(c, (int)(x * c->s), (int)(y * c->s), 0.8f * bl, m);
			else {
				float rr = 0.0018f + 0.0012f * h2;
				for (int k = 0; k < 5; k++) {
					float a = US_TAU * k / 5.0f + h1 * 9.0f;
					us_point(c, x + cosf(a) * rr, y + sinf(a) * rr, 0.0011f, 0.75f * bl, m);
				}
				us_point(c, x, y, 0.0009f, 0.9f, US_MAT_LIGHT);
			}
		}
	}

	/* ferns: fronds from above, pinnae shortening toward the tips, swaying with the wind */
	{
		uint8_t fm = winter > 0.6f ? US_MAT_STEM : (autumn > 0.6f ? US_MAT_ACCENT : US_MAT_LEAF);
		int nfern = (int)(st->nfern * (0.5f + 0.5f * dens) + 0.5f);
		for (int i = 0; i < nfern; i++) {
			float cx = st->fx[i], cy = st->fy[i], S = st->fs[i] * (0.75f + 0.25f * green);
			int nfr = 7;
			for (int k = 0; k < nfr; k++) {
				float a0 = st->fph[i] + US_TAU * k / nfr + 0.15f * sinf(k * 2.3f);
				float sway = 0.18f * wind * us_noise2(st->wind_t * 0.6f + k, i * 3.1f);
				float L = S * (0.8f + 0.2f * sinf(k * 1.7f + i));
				float px = cx, py = cy;
				const int NS = 8;
				for (int s = 1; s <= NS; s++) {
					float f = (float)s / NS;
					float a = a0 + (0.25f + sway) * f; /* fronds curl a little */
					float qx = cx + cosf(a0 + 0.12f * f) * L * f + cosf(a) * 0.0f, qy = cy + sinf(a0 + 0.12f * f) * L * f;
					qx += -sinf(a0) * (0.15f + sway) * L * f * f;
					qy += cosf(a0) * (0.15f + sway) * L * f * f;
					float v = (0.38f - 0.12f * f) * (0.7f + 0.3f * green);
					if (crisp) {
						us_line(c, px, py, qx, qy, 0.001f, v + 0.1f, fm);
					} else {
						hl(c, px, py, qx, qy, v, fm);
						/* pinnae */
						float pl = L * 0.2f * (1.0f - f) + 0.002f;
						float dx = qx - px, dy = qy - py, dl = sqrtf(dx * dx + dy * dy) + 1e-6f;
						float nx = -dy / dl, ny = dx / dl;
						hl(c, qx, qy, qx + (nx + dx / dl * 0.5f) * pl, qy + (ny + dy / dl * 0.5f) * pl, v * 0.85f, fm);
						hl(c, qx, qy, qx + (-nx + dx / dl * 0.5f) * pl, qy + (-ny + dy / dl * 0.5f) * pl, v * 0.85f, fm);
					}
					px = qx;
					py = qy;
				}
			}
		}
	}

}

/* multiply the ink inside a disc (shade / occlusion) */
static void carve_disc(us_canvas *c, float x, float y, float R, float keep, const float *lob)
{
	int k8 = (int)(keep * 256.0f);
	float Rm = R * 1.2f;
	int y0 = (int)floorf((y - Rm) * c->s), y1 = (int)ceilf((y + Rm) * c->s);
	int x0 = (int)floorf((x - Rm) * c->s), x1 = (int)ceilf((x + Rm) * c->s);
	float inv = 1.0f / c->s;
	for (int j = y0 < 0 ? 0 : y0; j <= y1 && j < c->h; j++) {
		float dy = (j + 0.5f) * inv - y;
		uint8_t *row = c->ink + (size_t)j * c->w;
		for (int i = x0 < 0 ? 0 : x0; i <= x1 && i < c->w; i++) {
			float dx = (i + 0.5f) * inv - x;
			float r2 = dx * dx + dy * dy, rr = R;
			if (r2 > Rm * Rm)
				continue;
			if (lob && r2 > R * R * 0.75f) {
				float th = atan2f(dy, dx);
				if (th < 0)
					th += US_TAU;
				float fv = th / US_TAU * TREE_V;
				int k = (int)fv % TREE_V;
				rr = R * us_lerp(lob[k], lob[(k + 1) % TREE_V], fv - floorf(fv));
			}
			if (r2 <= rr * rr)
				row[i] = (uint8_t)((row[i] * k8) >> 8);
		}
	}
}

/* a wooden jetty out over the water, and a tree leaning over the pond from the bank: its shade dims the
 * caustics, its crown hides them, a fringe of weeping twigs trails from its rim and dimples the surface */
static void draw_overhang(us_canvas *c, st_t *st, const us_ctx *ctx, float wind)
{
	const us_world *w = ctx->w;
	float dt = ctx->dt;
	float winter = us_world_winter(w), autumn = us_world_season_w(w, 2);
	int crisp = ctx->crisp;
	/* ---- jetty ---- */
	{
		float x0, y0, x1, y1;
		pond_pt(st, st->jth, 1.14f, &x0, &y0);
		pond_pt(st, st->jth, 0.8f, &x1, &y1);
		float dx = x1 - x0, dy = y1 - y0, L = sqrtf(dx * dx + dy * dy) + 1e-6f;
		float ux = dx / L, uy = dy / L, nx = -uy, ny = ux;
		float hw = 0.024f;
		/* the boards hide the water beneath */
		for (float f = 0; f <= L; f += hw * 0.8f)
			carve_disc(c, x0 + ux * f, y0 + uy * f, hw * 1.05f, 0.12f, NULL);
		float snow = us_smooth(0.6f, 0.9f, winter);
		uint8_t bm = snow > 0.5f ? US_MAT_ACCENT : US_MAT_EARTH;
		int np = (int)(L / 0.0085f);
		for (int k = 0; k <= np; k++) {
			float f = (k + 0.5f) / (np + 1) * L;
			float j = 0.0015f * sinf(k * 12.9898f);
			float ax = x0 + ux * f + nx * hw, ay = y0 + uy * f + ny * hw;
			float bx = x0 + ux * f - nx * hw, by = y0 + uy * f - ny * hw;
			float v = 0.55f + 0.1f * sinf(k * 7.1f) + 0.2f * snow;
			if (crisp)
				us_line(c, ax + ux * j, ay + uy * j, bx + ux * j, by + uy * j, 0.001f, v, bm);
			else {
				hl(c, ax + ux * j, ay + uy * j, bx + ux * j, by + uy * j, v, bm);
				hl(c, ax + ux * (j + 0.0055f), ay + uy * (j + 0.0055f), bx + ux * (j + 0.0055f), by + uy * (j + 0.0055f), v * 0.45f, bm);
			}
		}
		/* posts at the end, ringed by the water lapping them */
		for (int s = -1; s <= 1; s += 2) {
			float px = x1 + nx * hw * s * 1.1f, py = y1 + ny * hw * s * 1.1f;
			us_disc(c, px, py, 0.004f, 0.5f, US_MAT_EARTH);
			if (!crisp)
				us_ring(c, px, py, 0.0075f + 0.0015f * sinf(ctx->t * 1.3f + s), 0.0009f, 0.3f * (1.0f - st->ice), US_MAT_WATER);
		}
	}
	/* ---- the tree ---- */
	float bare = us_smooth(0.5f, 0.9f, winter);
	float gold = us_smooth(0.4f, 0.85f, autumn);
	uint8_t lm = gold > 0.5f ? US_MAT_LIGHT : US_MAT_LEAF;
	float crown = 1.0f - bare;
	float R = st->wR * (0.8f + 0.2f * crown);
	float sw = 0.004f * (0.3f + wind);
	float ox = sw * us_noise2(st->wind_t * 0.5f, 1.7f), oy = sw * 0.6f * us_noise2(4.2f, st->wind_t * 0.5f);
	float cx = st->wx + ox, cy = st->wy + oy;
	if (crown > 0.05f) {
		carve_disc(c, st->wx + R * 0.3f, st->wy + R * 0.28f, R * 0.98f, us_lerp(1.0f, 0.4f, crown), st->lob);
		carve_disc(c, cx, cy, R, us_lerp(1.0f, 0.0f, crown), st->lob);
	}
	/* the weeping fringe */
	{
		int ns = (int)(MAX_STRANDS * (0.6f + 0.4f * ctx->p->density));
		for (int i = 0; i < ns; i++) {
			float a0 = st->sa[i], L = st->sl[i];
			float swy = (0.08f + 0.35f * wind) * us_noise2(st->wind_t * 0.4f + st->sph[i], i * 0.37f);
			float px = cx + cosf(a0) * st->sr0[i] * R / st->wR, py = cy + sinf(a0) * st->sr0[i] * R / st->wR;
			const int NS = 5;
			for (int s = 1; s <= NS; s++) {
				float f = (float)s / NS;
				float a = a0 + swy * f;
				float qx = px + cosf(a) * L / NS, qy = py + sinf(a) * L / NS;
				float v = (0.42f - 0.2f * f);
				if (crisp)
					us_line(c, px, py, qx, qy, 0.001f, v, bare > 0.5f ? US_MAT_STEM : lm);
				else {
					hl(c, px, py, qx, qy, v * 0.7f, bare > 0.5f ? US_MAT_STEM : lm);
					if (bare < 0.9f && (s & 1))
						us_point(c, qx, qy, 0.0012f, v * crown, lm);
				}
				px = qx;
				py = qy;
			}
			if (pond_q(st, px, py) < 0.95f && st->ice < 0.5f && us_rand(&st->rng) < dt * (0.04f + 0.4f * wind))
				add_ring(st, px, py, 0.45f, 0.03f, 0.9f, 1);
		}
	}
	if (crown > 0.05f) {
		float pts[2 * (TREE_V + 1)];
		for (int v = 0; v <= TREE_V; v++) {
			int k = v % TREE_V;
			float th = US_TAU * k / TREE_V;
			pts[2 * v] = cx + cosf(th) * R * st->lob[k];
			pts[2 * v + 1] = cy + sinf(th) * R * st->lob[k];
		}
		if (crisp) {
			for (int v = 0; v < TREE_V; v++)
				us_tri(c, cx, cy, pts[2 * v], pts[2 * v + 1], pts[2 * v + 2], pts[2 * v + 3], 0.3f * crown, lm);
			for (int q = 0; q < 9; q++) {
				float h1 = st->sph[q * 5] / 20.0f, h2 = st->sph[q * 5 + 2] / 20.0f;
				float rr = R * 0.65f * sqrtf(h1), th = US_TAU * h2;
				us_disc(c, cx + cosf(th) * rr - R * 0.1f, cy + sinf(th) * rr - R * 0.1f, R * 0.22f, (0.42f + 0.18f * (q & 1)) * crown, lm);
			}
			us_polyline(c, pts, TREE_V + 1, 0.001f, 0.5f * crown, lm);
		} else {
			hpoly(c, pts, TREE_V + 1, 0.5f * crown, lm);
			/* leaf clumps: crescents on their shaded side, shivering with the wind */
			for (int q = 0; q < 46; q++) {
				float h1 = sinf(q * 12.9898f + st->so * 3.1f) * 43758.5453f;
				h1 -= floorf(h1);
				float h2 = h1 * 9.17f - floorf(h1 * 9.17f);
				float rr = R * 0.84f * sqrtf(h1), th = US_TAU * h2;
				float qx = cx + cosf(th) * rr, qy = cy + sinf(th) * rr;
				float qr = R * (0.11f + 0.07f * h2) * (1.0f - 0.35f * rr / R);
				float jit = 0.25f * wind * us_noise2(st->wind_t * 1.3f, q + st->so);
				float pp[2 * 7];
				for (int k = 0; k <= 6; k++) {
					float aa = -0.2f + jit + 2.2f * k / 6.0f;
					pp[2 * k] = qx + cosf(aa) * qr;
					pp[2 * k + 1] = qy + sinf(aa) * qr;
				}
				hpoly(c, pp, 7, (0.46f - 0.2f * rr / R) * crown, lm);
			}
		}
	}
	/* limbs: faint through the leaves, the whole picture in winter */
	float lv = (crisp ? 0.0f : 0.06f) * crown + 0.5f * bare;
	if (lv > 0.02f)
		for (int b = 0; b < 7; b++) {
			float th = st->so + b * US_TAU / 7.0f;
			float L1 = R * (0.55f + 0.1f * (b % 3));
			float ex = cx + cosf(th) * L1, ey = cy + sinf(th) * L1;
			hl(c, st->wx, st->wy, ex, ey, lv, US_MAT_STEM);
			for (int k = -1; k <= 1; k += 2)
				hl(c, ex, ey, ex + cosf(th + k * 0.45f) * R * 0.38f, ey + sinf(th + k * 0.45f) * R * 0.38f, lv * 0.8f, US_MAT_STEM);
		}
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *st = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, A = ctx->aspect, t = (float)ctx->t;
	float dens = ctx->p->density;
	float winter = us_world_winter(w), autumn = us_world_season_w(w, 2), summer = us_world_season_w(w, 1);
	us_rng *r = &st->rng;

	if (!st->inited)
		init_scene(st, ctx);
	st->A = A;

	/* ---- drivers ---- */
	st->ice = us_approach(st->ice, us_smooth(0.6f, 0.9f, winter), 8.0f, dt);
	st->scale = us_approach(st->scale, 0.8f + 0.2f * w->water_level, 20.0f, dt);
	st->bass_env = us_approach(st->bass_env, us_sat(0.8f * a->bass + 0.5f * a->kick + 0.3f * a->sub), 0.25f, ctx->real_dt);
	st->light_t += dt * (0.12f + 0.25f * a->level);
	st->wave_t += dt;
	float ice = st->ice, open = 1.0f - ice;
	us_world_add(w, &w->moisture, 0.0005f * open, dt);

	int npads = (int)((3 + dens * (MAX_PADS - 3)) * (1.0f - 0.85f * winter) + 0.5f);
	if (npads > MAX_PADS)
		npads = MAX_PADS;

	/* ---- rings: every hit drops one; kicks drop big ones; quiet rain / fish keep it alive ---- */
	if (a->beat && us_rand(r) < 1.0f - 0.75f * ice) {
		float x, y;
		random_water_point(st, npads, &x, &y);
		float big = us_sat(a->kick * 1.2f);
		float amp = (0.55f + 0.45f * a->onset) * (1.0f - 0.55f * ice);
		add_ring(st, x, y, amp * (0.8f + 0.4f * big), 0.08f + 0.06f * big, 3.0f + 2.0f * big, 0);
		/* a big hit sometimes lands as a little cluster */
		if (big > 0.5f && us_rand(r) < 0.4f * dens + 0.1f)
			add_ring(st, x + us_randr(r, -0.04f, 0.04f), y + us_randr(r, -0.04f, 0.04f), amp * 0.5f, 0.07f, 2.4f, 0);
	}
	st->drip_t -= dt;
	if (st->drip_t <= 0) {
		/* silence still drips: a drop from the reeds, a rising fish */
		float x, y;
		random_water_point(st, npads, &x, &y);
		add_ring(st, x, y, (0.35f + 0.2f * us_rand(r)) * (1.0f - 0.6f * ice), 0.06f, 2.6f, 0);
		st->drip_t = us_randr(r, 1.0f, 3.5f) * (1.6f - 0.8f * dens) * (1.0f + 2.0f * ice);
	}
	for (int i = 0; i < st->nrg;) {
		ring_t *g = &st->rg[i];
		g->age += dt;
		g->r += g->speed * dt * (1.0f - 0.25f * g->age / g->life);
		if (g->age >= g->life) {
			*g = st->rg[--st->nrg];
			continue;
		}
		i++;
	}

	/* ---- caustic light on the bed: the edges of a drifting, warped cellular net (the classic
	 *      swimming-pool caustic), with every ripple displacing it radially as it passes ---- */
	int FW = (int)(FH * A + 0.5f);
	if (FW > FWMAX)
		FW = FWMAX;
	if (FW < 2)
		FW = 2;
	{
		float sx = A / FW, sy = 1.0f / FH;
		float lt = st->light_t;
		/* feature points: one per cell, wandering on slow circles */
		const float cs = 0.13f; /* cell size */
		int gw, gh;
		caustic_points(st, A, cs, lt, 0x9e3779b9u, &gw, &gh);
		/* ripple displacement */
		memset(st->dpx, 0, sizeof(float) * FW * FH);
		memset(st->dpy, 0, sizeof(float) * FW * FH);
		for (int k = 0; k < st->nrg; k++) {
			ring_t *g = &st->rg[k];
			if (g->small)
				continue;
			float env = g->amp * (1.0f - g->age / g->life);
			float band = 0.05f;
			float r0 = g->r - band * 2.2f, r1 = g->r + band * 0.4f;
			if (r0 < 0)
				r0 = 0;
			int i0 = (int)((g->x - r1) / sx), i1 = (int)((g->x + r1) / sx) + 1;
			int j0 = (int)((g->y - r1) / sy), j1 = (int)((g->y + r1) / sy) + 1;
			if (i0 < 0) i0 = 0;
			if (j0 < 0) j0 = 0;
			if (i1 > FW) i1 = FW;
			if (j1 > FH) j1 = FH;
			for (int j = j0; j < j1; j++) {
				float dy = (j + 0.5f) * sy - g->y;
				for (int i = i0; i < i1; i++) {
					float dx = (i + 0.5f) * sx - g->x;
					float d = sqrtf(dx * dx + dy * dy) + 1e-5f;
					if (d < r0 || d > r1)
						continue;
					float q = (g->r - d) / band;
					float pk = expf(-q * q * 0.5f) * fcos(q * 2.6f) * env * 0.022f / d;
					st->dpx[j * FW + i] += dx * pk;
					st->dpy[j * FW + i] += dy * pk;
				}
			}
		}
		/* the pond mask only changes when the pond's size or the aspect does */
		if (st->mask_fw != FW || fabsf(st->mask_scale - st->scale) > 0.002f || st->mask_A != A) {
			for (int j = 0; j < FH; j++)
				for (int i = 0; i < FW; i++) {
					float q = pond_q(st, (i + 0.5f) * sx, (j + 0.5f) * sy);
					st->qmask[j * FW + i] = q;
					st->land[j * FW + i] = us_smooth(1.0f, 1.03f, q);
				}
			st->mask_fw = FW;
			st->mask_scale = st->scale;
			st->mask_A = A;
		}
		float gain = (0.13f + 0.3f * st->bass_env + 0.06f * w->daylight) * (1.0f - 0.65f * ice);
		if (!ctx->crisp) {
			/* Ink: fine filaments; bass focuses the light (brighter, and the net breathes) */
			caustic_lines(st, c, gw, gh, cs, lt, FW, FH, 0.2f + 2.0f * gain);
			/* a second, finer and fainter net drifting the other way: the two interfere like real caustics */
			int gw2, gh2;
			const float cs2 = 0.083f;
			caustic_points(st, A, cs2, lt * 1.35f + 40.0f, 0x51ed270bu, &gw2, &gh2);
			caustic_lines(st, c, gw2, gh2, cs2, lt, FW, FH, (0.2f + 2.0f * gain) * 0.42f);
			goto caustics_done;
		}
		/* Pixel: one pass fills the pond bed and the land around it (nearest lookup in the mask grid), then
		 * the same vector net as Ink, drawn as 1px lines: a fine web instead of an upscaled, 2px-thick field */
		{
			float lv = 0.13f + 0.1f * winter;
			uint8_t lm = winter > 0.6f ? US_MAT_ACCENT : US_MAT_EARTH;
			int bed = (int)(255.0f * (0.11f + 0.03f * open));
			int land = (int)(255.0f * lv);
			int ix[BLIT_WMAX];
			int W = c->w < BLIT_WMAX ? c->w : BLIT_WMAX;
			for (int px = 0; px < W; px++) {
				int i = px * FW / c->w;
				ix[px] = i < FW ? i : FW - 1;
			}
			for (int py = 0; py < c->h; py++) {
				int j = py * FH / c->h;
				if (j >= FH)
					j = FH - 1;
				const float *qrow = st->qmask + j * FW;
				uint8_t *ink = c->ink + (size_t)py * c->w, *mt = c->mat + (size_t)py * c->w;
				for (int px = 0; px < W; px++) {
					float q = qrow[ix[px]];
					int iv;
					uint8_t m;
					if (q < 0.995f) {
						iv = bed;
						m = ice > 0.5f ? US_MAT_ACCENT : US_MAT_WATER;
					} else if (q > 1.012f) {
						iv = land;
						m = lm;
					} else
						continue;
					if (iv > ink[px]) {
						ink[px] = (uint8_t)iv;
						mt[px] = m;
					}
				}
			}
		}
		caustic_lines(st, c, gw, gh, cs, lt, FW, FH, 0.42f + 1.6f * gain);
	}
caustics_done:

	/* ---- koi below the surface ---- */
	{
		int nk = ice > 0.7f ? 1 : (1 + (int)(dens * 3.0f + 0.5f));
		if (nk > MAX_KOI)
			nk = MAX_KOI;
		for (int i = 0; i < nk; i++) {
			koi_t *k = &st->koi[i];
			k->ph += dt * (2.0f + 30.0f * k->speed);
			float x = k->u * A, y = k->y;
			/* wander, turn back from the bank */
			k->turn = us_approach(k->turn, us_noise2(t * 0.1f, i * 7.3f) * 0.8f, 2.0f, dt);
			float q = pond_q(st, x + cosf(k->hd) * 0.08f, y + sinf(k->hd) * 0.08f);
			if (q > 0.82f) {
				float tc = atan2f(0.5f - y, A * 0.5f - x);
				float dd = remainderf(tc - k->hd, US_TAU);
				k->turn = dd > 0 ? 1.2f : -1.2f;
			}
			k->hd += k->turn * dt;
			float spd = k->speed * (1.0f + 0.6f * st->bass_env) * (1.0f - 0.6f * ice);
			x += cosf(k->hd) * spd * dt;
			y += sinf(k->hd) * spd * dt;
			k->u = x / A;
			k->y = y;
			/* a fish sometimes kisses the surface */
			if (ice < 0.5f && us_rand(r) < dt * 0.04f)
				add_ring(st, x + cosf(k->hd) * 0.01f, y + sinf(k->hd) * 0.01f, 0.4f, 0.05f, 2.2f, 0);
			/* spine follows the head */
			k->sx[0] = x;
			k->sy[0] = y;
			float seg = k->len / (KOI_N - 1);
			for (int j = 1; j < KOI_N; j++) {
				float dx = k->sx[j] - k->sx[j - 1], dy = k->sy[j] - k->sy[j - 1];
				float d = sqrtf(dx * dx + dy * dy) + 1e-6f;
				k->sx[j] = k->sx[j - 1] + dx / d * seg;
				k->sy[j] = k->sy[j - 1] + dy / d * seg;
			}
			float kv = 0.6f * (1.0f - 0.5f * ice);
			koi_draw(c, ctx->crisp, k, i, kv);
		}
	}

	/* ---- bank: double shoreline + land fill (crisp) + reed clumps ---- */
	{
		float pts[2 * (NB + 1)];
		for (int L = 0; L < 3; L++) {
			float k = 1.0f + 0.035f * L * L;
			for (int i = 0; i <= NB; i++) {
				float th = -US_PI + US_TAU * i / NB;
				pond_pt(st, th, k, &pts[2 * i], &pts[2 * i + 1]);
			}
			if (L == 0)
				us_polyline(c, pts, NB + 1, 0.0018f, 0.6f + 0.25f * ice, ice > 0.5f ? US_MAT_ACCENT : US_MAT_EARTH);
			else
				hpoly(c, pts, NB + 1, 0.3f / L, US_MAT_EARTH);
		}
		float wind = us_sat(0.25f * w->wind + 0.8f * a->high + 0.3f * a->air);
		draw_banks(c, st, ctx, wind, FW, npads);
		int ncl = (int)(MAX_CLUMPS * (0.35f + 0.65f * dens));
		uint8_t rm = winter > 0.6f ? US_MAT_STEM : US_MAT_LEAF;
		float green = 1.0f - 0.6f * winter;
		for (int i = 0; i < ncl; i++) {
			clump_t *q = &st->cl[i];
			float x, y;
			pond_pt(st, q->th, q->off, &x, &y);
			float inward = atan2f(0.5f - y, A * 0.5f - x);
			float sway = us_noise2(t * (0.3f + 1.2f * wind) + q->ph, i * 1.3f) * (0.12f + 0.6f * wind);
			int nb = 6 + (i % 5);
			for (int b = 0; b < nb; b++) {
				float hsh = sinf(i * 12.9898f + b * 78.233f) * 43758.5f;
				hsh -= floorf(hsh);
				float spread = (b / (float)(nb - 1) - 0.5f) * 2.6f;
				float Lb = q->len * (0.5f + 0.5f * hsh) * (0.7f + 0.3f * green);
				float a0 = inward + spread + sway * (0.6f + 0.4f * hsh);
				float bend = 0.3f * (spread > 0 ? 1.0f : -1.0f) * (0.3f + hsh) + sway * 0.5f;
				float mx = x + cosf(a0) * Lb * 0.5f, my = y + sinf(a0) * Lb * 0.5f;
				float tx = mx + cosf(a0 + bend) * Lb * 0.5f, ty = my + sinf(a0 + bend) * Lb * 0.5f;
				float bp[6] = {x, y, mx, my, tx, ty};
				hpoly(c, bp, 3, 0.4f + 0.15f * green + 0.1f * hsh, rm);
				if (b == nb / 2 && (i % 2) == 0) {
					float hx = x + cosf(a0) * Lb * 0.95f, hy = y + sinf(a0) * Lb * 0.95f;
					us_line(c, x + cosf(a0) * Lb * 0.7f, y + sinf(a0) * Lb * 0.7f, hx, hy, 0.0036f, 0.62f, US_MAT_STEM);
				}
			}
		}
	}

	/* ---- ripple rings on the surface (clipped by bank and pads) ---- */
	for (int k = 0; k < st->nrg; k++) {
		ring_t *g = &st->rg[k];
		float f = g->age / g->life;
		float fade = powf(1.0f - f, 1.5f) * us_sat(g->age / 0.08f);
		int waves = g->small ? 1 : 3;
		for (int wv = 0; wv < waves; wv++) {
			float rr = g->r - wv * 0.016f * (1.0f + 0.8f * f);
			if (rr <= 0.001f)
				continue;
			float v = us_sat(g->amp * fade * (wv == 0 ? 1.15f : (wv == 1 ? 0.65f : 0.35f)) * (g->small ? 0.5f : 1.0f));
			if (v < 0.02f)
				continue;
			int n = (int)(rr * c->s * 0.8f);
			if (n < 10)
				n = 10;
			if (n > 160)
				n = 160;
			/* skip the per-vertex bank test when the ring's box is safely inside the pond */
			float rx_, ry_;
			pond_radii(st, &rx_, &ry_);
			float ex = (fabsf(g->x - A * 0.5f) + rr) / rx_, ey = (fabsf(g->y - 0.5f) + rr) / ry_;
			int inside = ex * ex + ey * ey < st->bmin * st->bmin * 0.9f;
			/* pads only matter if one is near */
			int padnear = 0;
			for (int pk = 0; pk < npads && !padnear; pk++) {
				float dx = st->pad[pk].u * A - g->x, dy = st->pad[pk].y - g->y;
				float d = sqrtf(dx * dx + dy * dy);
				padnear = fabsf(d - rr) < st->pad[pk].R * 1.05f;
			}
			float px = g->x + rr, py = g->y;
			int pok = (inside || pond_q(st, px, py) < 0.97f) && !(padnear && in_pad(st, npads, px, py, 1.0f));
			for (int s = 1; s <= n; s++) {
				float th = US_TAU * s / n;
				float qx = g->x + cosf(th) * rr, qy = g->y + sinf(th) * rr;
				int ok = (inside || pond_q(st, qx, qy) < 0.97f) && !(padnear && in_pad(st, npads, qx, qy, 1.0f));
				if (ok && pok)
					hl(c, px, py, qx, qy, v, US_MAT_WATER);
				px = qx;
				py = qy;
				pok = ok;
			}
		}
	}

	/* ---- interference: where two crests cross they heap up into a bright point ---- */
	{
		int nb = 0;
		int big[MAX_RINGS];
		for (int k = 0; k < st->nrg; k++)
			if (!st->rg[k].small && st->rg[k].age > 0.1f)
				big[nb++] = k;
		float rx_, ry_;
		pond_radii(st, &rx_, &ry_);
		int pairs = 0;
		for (int p = 0; p < nb && pairs < 500; p++)
			for (int q = p + 1; q < nb && pairs < 500; q++) {
				ring_t *g1 = &st->rg[big[p]], *g2 = &st->rg[big[q]];
				float dx = g2->x - g1->x, dy = g2->y - g1->y;
				float d2 = dx * dx + dy * dy;
				if (d2 < 1e-6f)
					continue;
				float d = sqrtf(d2);
				float e1 = g1->amp * powf(1.0f - g1->age / g1->life, 1.5f), e2 = g2->amp * powf(1.0f - g2->age / g2->life, 1.5f);
				float ev = fminf(e1, e2);
				if (ev < 0.06f)
					continue;
				pairs++;
				/* each ring's first two crests */
				for (int c1 = 0; c1 < 2; c1++)
					for (int c2 = 0; c2 < 2; c2++) {
						float r1 = g1->r - c1 * 0.016f * (1.0f + 0.8f * g1->age / g1->life);
						float r2 = g2->r - c2 * 0.016f * (1.0f + 0.8f * g2->age / g2->life);
						if (r1 <= 0.002f || r2 <= 0.002f || d > r1 + r2 || d < fabsf(r1 - r2))
							continue;
						float aa = (r1 * r1 - r2 * r2 + d2) / (2.0f * d);
						float h = sqrtf(fmaxf(r1 * r1 - aa * aa, 0.0f));
						float mx = g1->x + dx * aa / d, my = g1->y + dy * aa / d;
						float v = us_sat(ev * (c1 + c2 == 0 ? 1.6f : 0.9f));
						for (int s = -1; s <= 1; s += 2) {
							float ix = mx - dy / d * h * s, iy = my + dx / d * h * s;
							float ex = (ix - A * 0.5f) / rx_, ey = (iy - 0.5f) / ry_;
							if (ex * ex + ey * ey > st->bmin * st->bmin * 0.92f || in_pad(st, npads, ix, iy, 1.0f))
								continue;
							v = us_sat(v * 1.35f);
							if (ctx->crisp) {
								us_point(c, ix, iy, 0.001f, 0.45f + 0.55f * v, US_MAT_LIGHT);
								if (v > 0.6f) {
									float q = 1.0f / c->s;
									us_put(c, (int)(ix * c->s) - 1, (int)(iy * c->s), 0.5f * v, US_MAT_LIGHT);
									us_put(c, (int)(ix * c->s) + 1, (int)(iy * c->s), 0.5f * v, US_MAT_LIGHT);
									(void)q;
								}
							} else {
								/* a four-point star of light, long arms along the crests' bisector */
								float L = 0.004f + 0.009f * v;
								hl(c, ix - L, iy, ix + L, iy, v, US_MAT_LIGHT);
								hl(c, ix, iy - L, ix, iy + L, v, US_MAT_LIGHT);
								hl(c, ix - L * 0.35f, iy - L * 0.35f, ix + L * 0.35f, iy + L * 0.35f, v * 0.6f, US_MAT_LIGHT);
								hl(c, ix - L * 0.35f, iy + L * 0.35f, ix + L * 0.35f, iy - L * 0.35f, v * 0.6f, US_MAT_LIGHT);
								us_point(c, ix, iy, 0.0016f, v, US_MAT_LIGHT);
							}
						}
					}
			}
	}

	/* ---- lily pads: drift, part for passing rings, bob ---- */
	{
		float padv = 0.6f + 0.1f * summer;
		uint8_t pm = autumn > 0.5f ? US_MAT_ACCENT : US_MAT_LEAF;
		for (int i = 0; i < npads; i++) {
			pad_t *p = &st->pad[i];
			float x = p->u * A, y = p->y;
			/* slow current + ring pushes */
			float cx = 0.004f * us_noise2(x * 2.0f + t * 0.02f, y * 2.0f), cy = 0.004f * us_noise2(x * 2.0f, y * 2.0f - t * 0.02f);
			p->vx += (cx - p->vx) * dt * 0.5f;
			p->vy += (cy - p->vy) * dt * 0.5f;
			for (int k = 0; k < st->nrg; k++) {
				ring_t *g = &st->rg[k];
				if (g->small)
					continue;
				float dx = x - g->x, dy = y - g->y;
				float d = sqrtf(dx * dx + dy * dy) + 1e-5f;
				float front = fabsf(d - g->r);
				if (front < p->R) {
					float e = g->amp * (1.0f - g->age / g->life) * (1.0f - front / p->R);
					p->bob = us_sat(p->bob + e * dt * 6.0f);
					p->vx += dx / d * e * dt * 0.02f;
					p->vy += dy / d * e * dt * 0.02f;
				}
			}
			/* neighbours and bank */
			for (int k = 0; k < npads; k++) {
				if (k == i)
					continue;
				float dx = x - st->pad[k].u * A, dy = y - st->pad[k].y;
				float d2 = dx * dx + dy * dy, mind = p->R + st->pad[k].R + 0.004f;
				if (d2 < mind * mind && d2 > 1e-8f) {
					float d = sqrtf(d2);
					p->vx += dx / d * (mind - d) * dt * 2.0f;
					p->vy += dy / d * (mind - d) * dt * 2.0f;
				}
			}
			if (pond_q(st, x, y) > 0.86f) {
				p->vx += (A * 0.5f - x) * dt * 0.02f;
				p->vy += (0.5f - y) * dt * 0.02f;
			}
			p->vx *= expf(-dt * 0.6f);
			p->vy *= expf(-dt * 0.6f);
			x += p->vx * dt;
			y += p->vy * dt;
			p->u = x / A;
			p->y = y;
			p->rot += (p->vr + 0.6f * p->bob * sinf(p->bph * 0.7f)) * dt;
			p->bob *= expf(-dt * 1.2f);
			p->bph += dt * (3.0f + 2.0f * p->bob);
			float bobs = 1.0f + 0.05f * p->bob * sinf(p->bph);
			float R = p->R * bobs;
			float v = padv * (1.0f - 0.4f * winter) + 0.25f * p->bob;
			draw_pad(c, ctx->crisp, p, x, y, R, v, pm);
			/* lotus flower in summer / spring */
			float bloom = p->flower * us_sat(1.2f - autumn - winter * 1.5f);
			if (bloom > 0.05f) {
				float fr = p->R * 0.42f * bloom;
				for (int q = 0; q < 8; q++) {
					float pa = p->rot * 0.5f + q * US_TAU / 8 + (q & 1) * 0.2f;
					float pl = fr * ((q & 1) ? 0.7f : 1.0f);
					float dx = cosf(pa), dy = sinf(pa);
					float tx = x + dx * pl, ty = y + dy * pl;
					float mx = x + dx * pl * 0.5f, my = y + dy * pl * 0.5f;
					float nx = -dy * pl * 0.28f, ny = dx * pl * 0.28f;
					if (ctx->crisp) {
						us_tri(c, x, y, mx + nx, my + ny, tx, ty, 0.85f, US_MAT_BLOOM);
						us_tri(c, x, y, mx - nx, my - ny, tx, ty, 0.85f, US_MAT_BLOOM);
					} else {
						float pp[] = {x, y, mx + nx, my + ny, tx, ty, mx - nx, my - ny, x, y};
						hpoly(c, pp, 5, 0.9f, US_MAT_BLOOM);
					}
				}
				us_disc(c, x, y, fr * 0.18f, 1.0f, US_MAT_LIGHT);
			}
			/* pads that drift too far reset (never happens often) */
			if (pond_q(st, x, y) > 1.0f)
				new_pad(st, p, winter);
		}
	}

	/* ---- autumn leaves on the surface ---- */
	{
		if (st->nlf < MAX_LEAVES && us_rand(r) < dt * (0.02f + 0.35f * autumn) * (1.0f - ice)) {
			leaf_t *l = &st->lf[st->nlf++];
			float x, y;
			pond_pt(st, us_rand(r) * US_TAU, 0.9f, &x, &y);
			l->u = x / A;
			l->y = y;
			l->rot = us_rand(r) * US_TAU;
			l->vr = us_randr(r, -0.2f, 0.2f);
			l->size = us_randr(r, 0.008f, 0.014f);
		}
		for (int i = 0; i < st->nlf;) {
			leaf_t *l = &st->lf[i];
			float x = l->u * A;
			x += 0.006f * us_noise2(x * 2.0f + t * 0.03f, l->y * 2.0f + 5.0f) * dt * 2.0f + (A * 0.5f - x) * 0.002f * dt;
			l->y += 0.006f * us_noise2(x * 2.0f, l->y * 2.0f - t * 0.03f + 9.0f) * dt * 2.0f + (0.5f - l->y) * 0.002f * dt;
			l->u = x / A;
			l->rot += l->vr * dt;
			if (autumn < 0.2f && us_rand(r) < dt * 0.02f) {
				*l = st->lf[--st->nlf];
				continue;
			}
			float dx = cosf(l->rot), dy = sinf(l->rot);
			float L = l->size, W = l->size * 0.45f;
			float nx = -dy * W, ny = dx * W;
			us_tri(c, x - dx * L, l->y - dy * L, x + nx, l->y + ny, x + dx * L, l->y + dy * L, 0.55f, US_MAT_ACCENT);
			us_tri(c, x - dx * L, l->y - dy * L, x - nx, l->y - ny, x + dx * L, l->y + dy * L, 0.55f, US_MAT_ACCENT);
			if (!ctx->crisp)
				hl(c, x - dx * L * 1.3f, l->y - dy * L * 1.3f, x + dx * L, l->y + dy * L, 0.85f, US_MAT_ACCENT);
			i++;
		}
	}

	/* ---- water striders: skate in strokes, dimples at the feet, flee new rings ---- */
	{
		float pop = us_sat(w->insects) * (1.0f - ice) * (1.0f - 0.8f * winter);
		int ns = (int)((1.0f + 12.0f * pop) * (0.4f + 0.8f * dens));
		if (ns > MAX_STRIDERS)
			ns = MAX_STRIDERS;
		float restless = 0.3f + 1.4f * pop + 1.2f * a->high + 0.6f * a->air;
		for (int i = 0; i < ns; i++) {
			strider_t *s = &st->sd[i];
			float x = s->u * A, y = s->y;
			s->stroke_t -= dt * restless;
			int stroke = s->stroke_t <= 0;
			/* startled by a fresh ring nearby */
			for (int k = 0; k < st->nrg && !stroke; k++) {
				ring_t *g = &st->rg[k];
				if (g->small || g->age > 0.1f)
					continue;
				float dx = x - g->x, dy = y - g->y;
				if (dx * dx + dy * dy < 0.012f) {
					stroke = 1;
					s->hd = atan2f(dy, dx) + us_randr(r, -0.5f, 0.5f);
				}
			}
			if (stroke) {
				s->hd += us_randn(r) * 0.6f;
				float q = pond_q(st, x + cosf(s->hd) * 0.1f, y + sinf(s->hd) * 0.1f);
				if (q > 0.85f || in_pad(st, npads, x + cosf(s->hd) * 0.05f, y + sinf(s->hd) * 0.05f, 1.2f))
					s->hd = atan2f(0.5f - y, A * 0.5f - x) + us_randr(r, -0.7f, 0.7f);
				float imp = us_randr(r, 0.12f, 0.22f) * (0.8f + 0.5f * a->high);
				s->vx += cosf(s->hd) * imp;
				s->vy += sinf(s->hd) * imp;
				s->stroke_t = us_randr(r, 0.4f, 2.2f);
				/* the oar legs push dimples into the surface */
				for (int sd = -1; sd <= 1; sd += 2) {
					float ox = x - sinf(s->hd) * 0.012f * sd, oy = y + cosf(s->hd) * 0.012f * sd;
					add_ring(st, ox, oy, 0.5f, 0.035f, 0.7f, 1);
				}
			}
			s->vx *= expf(-dt * 2.2f);
			s->vy *= expf(-dt * 2.2f);
			x += s->vx * dt;
			y += s->vy * dt;
			if (pond_q(st, x, y) > 0.95f) {
				s->vx = -s->vx * 0.5f;
				s->vy = -s->vy * 0.5f;
				x += (A * 0.5f - x) * 0.01f;
				y += (0.5f - y) * 0.01f;
			}
			s->u = x / A;
			s->y = y;
			float sp = sqrtf(s->vx * s->vx + s->vy * s->vy);
			if (sp > 0.01f)
				s->hd = atan2f(s->vy, s->vx);
			draw_strider(c, ctx->crisp, x, y, s->hd, 0.85f);
		}
	}

	draw_overhang(c, st, ctx, us_sat(0.25f * w->wind + 0.8f * a->high + 0.3f * a->air));

	/* ---- ice skin: frost rim, crack lines that creak further on heavy hits ---- */
	if (ice > 0.02f) {
		for (int i = 0; i < MAX_CRACKS; i++) {
			crack_t *k = &st->ck[i];
			if (a->beat && a->kick > 0.4f && us_rand(r) < 0.25f)
				k->grown = us_sat(k->grown + 0.06f * a->kick);
			int n = (int)(k->n * k->grown * ice);
			for (int j = 0; j + 1 < n; j++) {
				float x0 = k->u[j] * A, y0 = k->y[j], x1 = k->u[j + 1] * A, y1 = k->y[j + 1];
				if (pond_q(st, x1, y1) > 0.97f)
					break;
				hl(c, x0, y0, x1, y1, 0.55f * ice, US_MAT_ACCENT);
				/* little side fractures */
				if ((j % 5) == 2) {
					float dx = y1 - y0, dy = x0 - x1;
					hl(c, x1, y1, x1 + dx * 0.9f, y1 + dy * 0.9f, 0.35f * ice, US_MAT_ACCENT);
				}
			}
		}
		/* frost: a stippled rim just inside the bank */
		for (int i = 0; i < 160; i++) {
			float th = US_TAU * i / 160.0f + 0.02f * sinf(i * 7.1f);
			float kk = 0.9f + 0.08f * (0.5f + 0.5f * sinf(i * 3.7f + st->so));
			float x, y;
			pond_pt(st, th, kk, &x, &y);
			us_point(c, x, y, 0.0012f, 0.45f * ice, US_MAT_ACCENT);
		}
	}
}

const us_scene_vt us_scene_pond = {"pond", "Pond", 0.04f, create, destroy, frame};
