/* Understory scene: Wind — a prairie of grass in perspective, combed by gusts.
 *
 * Thousands of blades and seed stalks stand in depth rows (far rows small and faint, near rows tall).
 * The wind is a travelling noise field in ground-plane coordinates, so gust fronts are visible as
 * bright sheen waves sweeping across the field (bent blades catch the light). Cloud shadows slide
 * over the grass with the wind.
 * Audio: highs/air -> gust strength, centroid -> gust direction (across vs diagonally toward/away
 *        from the viewer), onsets -> a sudden gust front sweeping the field, bass -> a slow swell of
 *        the whole field. Strong gusts shake seeds/pollen loose from the seed heads.
 * World: reads growth (grass height), season (green spring, seeding summer, golden autumn, sparse
 *        snow-bent winter), daylight (sun/moon, brightness), wind (prevailing breeze); writes pollen.
 * A lone tree on a rise flexes on a spring (trunk leans, crown streams downwind, leaves silver as
 * gusts flip them) and gives up leaves that tumble away across the field. Cumulus clouds stream and
 * shear with the wind; wisps of moving air (thin travelling streamlines) ride the stronger gusts.
 * A hazy far range and a windbreak of poplars bending with the gusts sit on the horizon; gust crests
 * flash the blades' pale sides (LIGHT); wisps of air end in a small curl (an eddy).
 * Silence: the prevailing breeze keeps slow waves rolling through.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAX_ROWS 34
#define MAX_BLADES 3600
#define MAX_SEEDS 700
#define MAX_FRONTS 6
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
} blade_t;

typedef struct {
	float x, y, vx, vy, life, ph, sz;
} seed_t;

typedef struct {
	float pos, amp, speed, width; /* pos along the wind axis in world units */
} front_t;

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
	float x, y, age, life, len, amp, ph, sp;
} wisp_t;

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
	/* coordinate along the wind axis */
	float along = X * ax + Z * az;
	float across = -X * az + Z * ax;
	float g = us_noise2(along * 0.55f - s->flow, across * 0.35f + 3.7f);
	g += 0.5f * us_noise2(along * 1.4f - s->flow * 2.1f, across * 0.9f + 9.1f);
	float bend = us_smooth(-0.35f, 0.65f, g);
	for (int i = 0; i < MAX_FRONTS; i++) {
		const front_t *f = &s->fr[i];
		if (f->amp <= 0.001f)
			continue;
		float d = (along - f->pos) / f->width;
		if (d > -3.0f && d < 3.0f)
			bend += f->amp * expf(-d * d) * (d < 0 ? 1.0f : 0.6f);
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
		float fl = g->depth >= 2 ? us_noise2(i * 0.37f, t * (1.2f + 5.0f * gust)) * (0.02f + 0.14f * gust) * g->depth * 0.33f : 0.0f;
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
		float fl = us_noise2(l->ph, t * (2.0f + 9.0f * gust));
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
		if (tear > 0 && s->ntb < MAX_TUMB && us_rand(&s->rng) < tear && torn < 2) {
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

	/* ---- wind state ---- */
	float tg = us_sat(0.25f * w->wind + 0.85f * a->high + 0.55f * a->air + 0.15f * a->level);
	s->gust = us_approach(s->gust, tg, tg > s->gust ? 0.35f : 1.5f, rdt);
	float tdir = a->active > 0.2f ? (a->centroid - 0.5f) * 2.4f : 0.35f * us_noise2((float)ctx->t * 0.02f, 1.3f);
	s->dir = us_approach(s->dir, us_clampf(tdir, -1.1f, 1.1f), 3.0f, rdt);
	s->swell = us_approach(s->swell, us_sat(a->bass * 0.7f + a->sub * 0.4f), 0.8f, rdt);
	float ax = cosf(s->dir), az = sinf(s->dir); /* ax > 0: blowing left -> right */
	float gstr = 0.12f + 0.55f * s->gust + 0.1f * winter;
	s->flow += dt * (0.25f + 1.4f * s->gust);
	s->cloud_t += dt * (0.02f + 0.06f * s->gust);
	float sgn = ax >= 0 ? 1.0f : -1.0f;

	/* onsets -> a sudden gust front sweeps in from upwind */
	if (a->beat && a->onset > 0.45f && ctx->t - s->last_beat_t > 0.8f) {
		s->last_beat_t = (float)ctx->t;
		for (int i = 0; i < MAX_FRONTS; i++)
			if (s->fr[i].amp <= 0.001f) {
				s->fr[i].amp = 0.35f + 0.65f * a->onset * (0.6f + 0.6f * s->gust);
				s->fr[i].pos = -14.0f;
				s->fr[i].speed = us_randr(r, 5.0f, 8.0f);
				s->fr[i].width = us_randr(r, 1.4f, 2.6f);
				break;
			}
	}
	for (int i = 0; i < MAX_FRONTS; i++) {
		front_t *f = &s->fr[i];
		if (f->amp <= 0.001f)
			continue;
		f->pos += f->speed * dt;
		f->amp *= expf(-dt * 0.45f);
		if (f->pos > 16.0f)
			f->amp = 0;
	}

	float dayv = 0.8f + 0.2f * us_smooth(0.0f, 0.6f, day);

	/* ---- the tree's spring: pushed by the local wind (fronts included), overshoots and rebounds ---- */
	float tear = 0;
	{
		float sc = row_s(TREE_ROW), Z = 1.0f / sc;
		float X = (s->tree_u * A - A * 0.5f) * Z;
		float bend = wind_at(s, X * 0.25f, Z * 0.25f, ax, az);
		float target = gstr * bend * (0.35f + 0.65f * fabsf(ax) + 0.2f) * 2.4f;
		s->tvel += ((target - s->tbend) * 28.0f - s->tvel * 3.2f) * dt;
		s->tbend += s->tvel * dt;
		float force = gstr * bend;
		if (force > 0.3f)
			tear = dt * (force - 0.3f) * 0.12f * (0.3f + 2.5f * autumn) * (1.0f - winter);
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
		float halo = 1.0f + 0.08f * s->swell;
		if (cy < HZ - 0.03f) {
			if (ctx->crisp) {
				us_disc(c, cx, cy, 0.05f * halo, 0.14f, US_MAT_LIGHT);
				us_disc(c, cx, cy, 0.03f, 0.75f, US_MAT_LIGHT);
			} else {
				us_disc(c, cx, cy, 0.03f, 0.18f, US_MAT_LIGHT);
				wring(c, cx, cy, 0.03f, 0.0016f, 0.85f, US_MAT_LIGHT);
				wring(c, cx, cy, 0.045f * halo, 0.0012f, 0.2f, US_MAT_LIGHT);
				wring(c, cx, cy, 0.068f * halo, 0.0011f, 0.1f, US_MAT_LIGHT);
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
			float cvv = (0.2f + 0.16f * dayv) * (1.0f - 0.25f * k / MAX_CLOUD);
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
			float bendk = sgn * (0.004f + 0.02f * s->gust) * (0.6f + 0.4f * us_noise2(u * 9.0f, (float)ctx->t * (0.8f + 2.0f * s->gust)));
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

	/* ---- ground fill (pixel styles): dark meadow / snow ---- */
	if (ctx->crisp) {
		float gv = (0.12f + 0.06f * winter) * dayv;
		us_tri(c, 0, HZ, A, HZ, 0, 1.01f, gv, winter > 0.5f ? US_MAT_WATER : US_MAT_EARTH);
		us_tri(c, A, HZ, A, 1.01f, 0, 1.01f, gv, winter > 0.5f ? US_MAT_WATER : US_MAT_EARTH);
	}

	/* ---- grass ---- */
	float height = (0.30f + 0.95f * w->growth) * (1.0f - 0.45f * winter) * (1.0f + 0.12f * s->swell);
	float keep = (0.3f + 0.7f * dens) * (1.0f - 0.55f * winter);
	int rows_on = MAX_ROWS;
	float lean0 = 0.15f * winter; /* snow-bent */
	float shake = 0;
	for (int row = 0; row < rows_on; row++) {
		float sc = row_s(row);
		float y = HZ + (1.03f - HZ) * sc;
		float Z = 1.0f / sc;
		int b0 = s->row_start[row], b1 = s->row_start[row + 1];
		int nb = (int)((b1 - b0) * keep);
		float H = 0.42f * sc * height;
		float wid = ctx->crisp ? (sc > 0.55f ? 0.012f : 0.0f) : 0.001f + 0.0022f * sc;
		float vrow = (ctx->crisp ? 0.3f + 0.65f * sqrtf(sc) : 0.36f + 0.74f * sqrtf(sc)) * dayv;
		if (row == TREE_ROW)
			draw_tree(s, c, ctx, A, sgn, s->gust, autumn, winter, dayv, tear);
		/* pixel canvases cannot resolve every far blade: thin the farthest rows */
		int kstep = ctx->crisp && sc < 0.1f ? 2 : 1;
		for (int k = 0; k < nb; k += kstep) {
			blade_t *b = &s->bl[b0 + k];
			float x = b->u * A;
			float X = (x - A * 0.5f) * Z;
			float bend = wind_at(s, X * 0.25f, Z * 0.25f, ax, az);
			float flut = 0.08f * us_noise2(b->ph, (float)ctx->t * (1.5f + 3.0f * s->gust));
			float bb = (lean0 + gstr * bend + flut) * (ax >= 0 ? 1.0f : -1.0f) * (0.35f + 0.65f * fabsf(ax) + 0.2f);
			float h = H * b->h * (b->head ? 1.25f : 1.0f);
			/* blowing toward/away from the viewer foreshortens the blades */
			float fs = 1.0f - 0.35f * fabsf(az) * us_sat(bend);
			float shadow = 0.62f + 0.38f * us_smooth(-0.3f, 0.3f, us_noise2(X * 0.05f - s->cloud_t * 2.0f * ax, Z * 0.08f - s->cloud_t * 2.0f * az));
			float sheen = 0.68f + 0.6f * us_sat(bend); /* bent blades catch the light: gust fronts read as sheen waves */
			float v = us_sat(vrow * shadow * sheen * (0.75f + 0.25f * b->h));
			uint8_t m = US_MAT_LEAF;
			if (autumn > 0.5f)
				m = b->tint < 2 ? US_MAT_LIGHT : (b->tint == 2 ? US_MAT_EARTH : US_MAT_STEM); /* sun-gold, dry, straw */
			else if (winter > 0.5f)
				m = b->tint < 3 ? US_MAT_EARTH : US_MAT_STEM;
			else if (summer > 0.5f && b->tint == 0)
				m = US_MAT_STEM;
			/* the crest of a gust: bent blades flash their pale sides, a travelling sheen of light */
			if (winter < 0.5f && autumn < 0.5f && b->tint == 1 && bend > 0.95f && sc > 0.08f)
				m = US_MAT_LIGHT;
			float pts[8];
			float bx = bb * 0.9f;
			for (int q = 0; q < 4; q++) {
				float t = q / 3.0f;
				pts[2 * q] = x + bx * h * t * t;
				pts[2 * q + 1] = y - h * fs * t * (1.0f - 0.3f * fabsf(bx) * t);
			}
			blade(c, x, y, bx, h, fs, wid, v, m);
			float tx = pts[6], ty = pts[7];
			if (winter > 0.4f && b->tint == 3) /* snow caught on the tips */
				us_point(c, tx, ty, 0.0015f + 0.003f * sc, 0.6f * winter * dayv, US_MAT_WATER);
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
				/* strong gusts shake seeds loose */
				float force = bend * gstr;
				if (force > 0.35f && s->nsd < MAX_SEEDS &&
				    us_rand(r) < dt * (force - 0.3f) * 3.0f * (0.4f + summer + autumn) * (0.5f + w->growth)) {
					seed_t *q = &s->sd[s->nsd++];
					q->x = hx;
					q->y = hy;
					q->vx = (ax >= 0 ? 1.0f : -1.0f) * us_randr(r, 0.05f, 0.18f) * (0.5f + s->gust);
					q->vy = -us_randr(r, 0.01f, 0.05f) * (0.3f + sc);
					q->life = 1.0f;
					q->ph = us_rand(r) * 10;
					q->sz = 0.0008f + 0.0018f * sc;
					shake += 1.0f;
				}
			}
		}
	}

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
		float rate = (s->gust * s->gust * 2.5f + 0.25f) * (0.4f + 0.8f * dens);
		for (int i = 0; i < MAX_FRONTS; i++)
			rate += s->fr[i].amp * 2.0f;
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
			q->x = sgn > 0 ? us_randr(r, -0.2f, A * 0.7f) : us_randr(r, A * 0.3f, A + 0.2f);
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
			q->x += sgn * spd * dt;
			float env = sinf(US_PI * q->age / q->life);
			/* the stroke grows out of nothing at the head, and ends in a small curl */
			float L = q->len * us_sat(q->age * 2.0f / q->life + 0.2f);
			enum { NW = 16 };
			float px = 0, py = 0;
			float vk = (ctx->crisp ? 0.5f : 0.5f) * dayv * env;
			for (int k = 0; k <= NW; k++) {
				float f = (float)k / NW;
				float x = q->x - sgn * L * f;
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
					float a = US_PI * 0.5f - sgn * f * US_PI * 1.6f;
					float rr = R * (1.0f - 0.55f * f);
					float x = cxr + sgn * cosf(a) * rr * 0.0f + cosf(a) * rr * sgn, y = cyr + sinf(a) * rr;
					(void)x;
					x = cxr + sgn * cosf(a) * rr;
					wline(c, ppx, ppy, x, y, ctx->crisp ? 0.004f : 0.0011f, vk * (1.0f - 0.6f * f), US_MAT_ACCENT);
					ppx = x;
					ppy = y;
				}
			}
		}
	}
}

const us_scene_vt us_scene_wind = {"wind", "Wind", 0.0f, create, destroy, frame};
