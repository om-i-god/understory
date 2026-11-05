/* Understory scene: Coral reef — reaction-diffusion colonies in a sunlit shallow, seen from above.
 *
 * A Gray-Scott reaction-diffusion runs on a low-res grid (240x135, reflecting edges) and creeps across the
 * screen. Feed/kill are not global constants: a slow drifting "species map" puts each region in a regime —
 * brain coral (labyrinth), staghorn (branching fronds) or polyp mounds (dividing spots) — and the whole map
 * cycles slowly, so the reef keeps reorganising: spots split, mazes break into fronds, grazed patches die
 * back and are recolonised. Each species has its own material, so palettes show a multi-coloured reef.
 * Over it: a moving caustic web of sunlight (cellular light net), drifting plankton, schools of small
 * silver fish, and polyps on the coral that open and close with the music.
 *
 * Audio: low-mids feed growth (feed rate + iterations/frame); bass/level open the polyps (a ripple runs
 *        across the reef on each kick); kicks release spawn bundles that drift and settle as new colonies;
 *        hits make the fish schools dart; onsets nudge the kill rate; highs/air make the plankton sparkle
 *        and the caustics shimmer; level stirs the current and brightens the sunlight.
 * World: reads pollen + water_level (nutrients -> feed, growth speed), reef (initial coverage), daylight
 *        (sun vs moonlight caustics), flock + pollen (fish numbers); writes reef (follows coverage).
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#ifdef US_PROF /* section timing for development: build with -DUS_PROF */
#include <stdio.h>
#include <time.h>
static double prof_acc[12];
static long prof_n;
static double prof_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}
#define PROF(k) do { double _n = prof_now(); prof_acc[k] += _n - prof_t; prof_t = _n; } while (0)
#define PROF_START double prof_t = prof_now()
#define PROF_END do { if (++prof_n % 300 == 0) { fprintf(stderr, "coral prof:"); for (int _k = 0; _k < 12; _k++) fprintf(stderr, " %.3f", prof_acc[_k] / prof_n); fprintf(stderr, "\n"); } } while (0)
#else
#define PROF(k)
#define PROF_START
#define PROF_END
#endif

#define GW 240
#define GH 135
#define CW 48 /* coarse parameter map */
#define CH 27
#define MAX_PL 420
#define MAX_SP 160
#define MAX_POLYP 220
#define MAX_SCHOOL 4
#define MAX_FISH 30
#define KW 96 /* caustic field */
#define KH 54
#define PW 16 /* sunlight patch grid */
#define PH 9
#define FPW 40 /* caustic feature-point grid (cells across the longest supported aspect) */
#define FPH 16
#define DW 320 /* the far reef: a second, finer reaction-diffusion deeper down (smaller = further away) */
#define DH 180
#define NRAY 7 /* sun shafts slanting down through the water column */
#define MAX_FSEG 12000 /* cached far-reef segments (the deep floor changes slowly: re-traced every 4th frame) */

typedef struct {
	float x, y; /* x as fraction of width 0..1, y 0..1 */
	float vx, vy, ph, size;
} plank_t;

typedef struct {
	float x, y, vx, vy, life, age;
} spawn_t;

typedef struct {
	float gx, gy; /* grid position */
	float open, ph, size;
	int alive;
} polyp_t;

typedef struct {
	float ox, oy;   /* offset from school centre */
	float vx, vy;   /* world velocity */
	float sa, sc;   /* slot: along / across the heading */
	float tau, flash, dx, dy;
} fish_t;

typedef struct {
	float x, y;  /* centre, normalized (x in units, wraps over aspect + margin) */
	float head, head_to, speed, burst, burst_to, scare, turn;
	int n, deep; /* deep schools swim lower: smaller, dimmer, slower (parallax) */
	fish_t f[MAX_FISH];
} school_t;

typedef struct {
	us_rng rng;
	float u[2][GW * GH], v[2][GW * GH];
	float prev[GW * GH]; /* v a little while ago: growth front */
	float fF[GW * GH], fFK[GW * GH]; /* per-cell feed and feed+kill (upsampled from the coarse maps) */
	float fmap[CW * CH], kmap[CW * CH], rmap[CW * CH];
	int cur, inited;
	unsigned fcount;
	float iter_acc;
	float cycle; /* regime cycle phase */
	float cover, cover_s;
	float low_env, bass_env, nudge, hi_env, open_env, lvl_env;
	float graze_x, graze_y, graze_vx, graze_vy;
	float seed_t;
	plank_t pl[MAX_PL];
	spawn_t sp[MAX_SP];
	int nsp;
	polyp_t po[MAX_POLYP];
	school_t sc[MAX_SCHOOL];
	float flow_t, caus_t;
	float kick_wave_x, kick_wave; /* a polyp-opening ripple travelling across the reef */
	float fpx[FPW * FPH], fpy[FPW * FPH];
	int kw, kh;
	float patch[PW * PH];
	int16_t lab[KW * KH];        /* nearest feature point per caustic sample */
	float wpx[KW * KH], wpy[KW * KH]; /* warped sample positions */
	float caus_cell; /* feature cell size (units) */
	/* far reef (deeper, finer, slower) */
	float du[2][DW * DH], dv[2][DW * DH];
	float dF[DW * DH], dFK[DW * DH];
	int dcur;
	/* opening: the reef's pre-growth is spread over the first second while the scene fades in */
	int pre_left, pre_total, dpre_left;
	float intro_t;
	float ray_t;
	unsigned fcount_m;
	float fseg[MAX_FSEG][4], fsegk[MAX_FSEG];
	int nfseg;
	float fseg_A;
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x636f7261u);
	return s;
}

static void destroy(void *p) { free(p); }

static inline uint32_t hash2(int x, int y)
{
	uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	return h ^ (h >> 15);
}

/* drop a colony seed at grid cell (gx, gy) */
static void seed_at(st_t *st, float gx, float gy, float r)
{
	float *U = st->u[st->cur], *V = st->v[st->cur];
	int ir = (int)ceilf(r);
	for (int dy = -ir; dy <= ir; dy++)
		for (int dx = -ir; dx <= ir; dx++) {
			float d2 = (float)(dx * dx + dy * dy);
			if (d2 > r * r)
				continue;
			int x = (int)gx + dx, y = (int)gy + dy;
			if (x < 0 || y < 0 || x >= GW || y >= GH)
				continue;
			int i = y * GW + x;
			float k = 1.0f - 0.4f * d2 / (r * r);
			if (V[i] < 0.25f * k + 0.05f) {
				U[i] = 0.5f - 0.2f * us_rand(&st->rng);
				V[i] = (0.25f + 0.1f * us_rand(&st->rng)) * k;
			}
		}
}

/* regime path: staghorn -> brain -> polyp mounds -> staghorn (r in [0,3)) */
static void regime(float r, float *F, float *K)
{
	static const float P[4][2] = {
		{0.0545f, 0.0620f}, /* branching coral (staghorn) */
		{0.0300f, 0.0565f}, /* labyrinth (brain coral)    */
		{0.0367f, 0.0649f}, /* mitosis spots (polyp mounds) */
		{0.0545f, 0.0620f},
	};
	r = fmodf(r, 3.0f);
	if (r < 0)
		r += 3.0f;
	int i = (int)r;
	float t = r - i;
	t = t * t * (3 - 2 * t);
	*F = us_lerp(P[i][0], P[i + 1][0], t);
	*K = us_lerp(P[i][1], P[i + 1][1], t);
}

/* species from a (fractional) regime value: nearest regime, with a dithered border.
 * 0 staghorn (branching fronds), 1 brain (labyrinth), 2 polyp mounds (spots) */
static inline int species_idx(float rr, uint32_t h)
{
	rr += 0.5f + ((h & 255) / 255.0f - 0.5f) * 0.12f;
	rr -= 3.0f * floorf(rr * (1.0f / 3.0f));
	int i = (int)rr;
	return i > 2 ? 2 : i;
}
static const uint8_t SPECIES_MAT[3] = {US_MAT_BLOOM, US_MAT_LEAF, US_MAT_ACCENT};

/* one Gray-Scott step on a W x H grid with reflecting edges (per-cell feed F and feed+kill FK) */
static void rd_step(const float *U, const float *V, float *U2, float *V2, const float *F, const float *FK, int W,
		    int H)
{
	for (int y = 0; y < H; y++) {
		const float *Ur = U + y * W, *Vr = V + y * W;
		const float *Uu = U + (y ? y - 1 : 1) * W, *Vu = V + (y ? y - 1 : 1) * W;
		const float *Ud = U + (y + 1 < H ? y + 1 : H - 2) * W, *Vd = V + (y + 1 < H ? y + 1 : H - 2) * W;
		const float *Fr = F + y * W, *FKr = FK + y * W;
		float *Uo = U2 + y * W, *Vo = V2 + y * W;
		/* interior: branch-free so it vectorises */
		for (int x = 1; x < W - 1; x++) {
			float u = Ur[x], v = Vr[x];
			float lu = -u + 0.2f * (Ur[x - 1] + Ur[x + 1] + Uu[x] + Ud[x]) +
				   0.05f * (Uu[x - 1] + Uu[x + 1] + Ud[x - 1] + Ud[x + 1]);
			float lv = -v + 0.2f * (Vr[x - 1] + Vr[x + 1] + Vu[x] + Vd[x]) +
				   0.05f * (Vu[x - 1] + Vu[x + 1] + Vd[x - 1] + Vd[x + 1]);
			float uvv = u * v * v;
			float nu = u + (0.55f * lu - uvv + Fr[x] * (1.0f - u));
			float nv = v + (0.275f * lv + uvv - FKr[x] * v);
			Uo[x] = fminf(fmaxf(nu, 0.0f), 1.0f);
			Vo[x] = fminf(fmaxf(nv, 0.0f), 1.0f);
		}
		/* reflecting edges */
		for (int e = 0; e < 2; e++) {
			int x = e ? W - 1 : 0, xl = e ? W - 2 : 1, xr = xl;
			float u = Ur[x], v = Vr[x];
			float lu = -u + 0.2f * (Ur[xl] + Ur[xr] + Uu[x] + Ud[x]) + 0.05f * (Uu[xl] + Uu[xr] + Ud[xl] + Ud[xr]);
			float lv = -v + 0.2f * (Vr[xl] + Vr[xr] + Vu[x] + Vd[x]) + 0.05f * (Vu[xl] + Vu[xr] + Vd[xl] + Vd[xr]);
			float uvv = u * v * v;
			float nu = u + (0.55f * lu - uvv + Fr[x] * (1.0f - u));
			float nv = v + (0.275f * lv + uvv - FKr[x] * v);
			Uo[x] = us_sat(nu);
			Vo[x] = us_sat(nv);
		}
	}
}

static void rd_iterate(st_t *st)
{
	rd_step(st->u[st->cur], st->v[st->cur], st->u[st->cur ^ 1], st->v[st->cur ^ 1], st->fF, st->fFK, GW, GH);
	st->cur ^= 1;
}

static void far_iterate(st_t *st)
{
	rd_step(st->du[st->dcur], st->dv[st->dcur], st->du[st->dcur ^ 1], st->dv[st->dcur ^ 1], st->dF, st->dFK, DW, DH);
	st->dcur ^= 1;
}

/* upsample the coarse feed/kill maps to per-cell arrays (bilinear: smooth regime borders) */
static void upsample_maps(st_t *st)
{
	static int xa[GW], xb[GW];
	static float tx[GW];
	static int ready;
	if (!ready) {
		for (int x = 0; x < GW; x++) {
			float fx = (x + 0.5f) * CW / GW - 0.5f;
			int x0 = (int)floorf(fx);
			tx[x] = fx - x0;
			xa[x] = x0 < 0 ? 0 : x0;
			xb[x] = x0 + 1 >= CW ? CW - 1 : x0 + 1;
		}
		ready = 1;
	}
	float rf[CW], rk[CW];
	for (int y = 0; y < GH; y++) {
		float fy = (y + 0.5f) * CH / GH - 0.5f;
		int y0 = (int)floorf(fy);
		float ty = fy - y0;
		int ya = y0 < 0 ? 0 : y0, yb = y0 + 1 >= CH ? CH - 1 : y0 + 1;
		for (int i = 0; i < CW; i++) {
			rf[i] = us_lerp(st->fmap[ya * CW + i], st->fmap[yb * CW + i], ty);
			rk[i] = us_lerp(st->kmap[ya * CW + i], st->kmap[yb * CW + i], ty);
		}
		float *oF = st->fF + y * GW, *oFK = st->fFK + y * GW;
		for (int x = 0; x < GW; x++) {
			float F = rf[xa[x]] + (rf[xb[x]] - rf[xa[x]]) * tx[x];
			float K = rk[xa[x]] + (rk[xb[x]] - rk[xa[x]]) * tx[x];
			oF[x] = F;
			oFK[x] = F + K;
		}
	}
}

static void place_polyp(st_t *st, polyp_t *p)
{
	const float *V = st->v[st->cur];
	for (int tries = 0; tries < 6; tries++) {
		int x = 1 + (int)(us_rand(&st->rng) * (GW - 2)), y = 1 + (int)(us_rand(&st->rng) * (GH - 2));
		if (V[y * GW + x] > 0.27f) {
			p->gx = x + 0.5f;
			p->gy = y + 0.5f;
			p->alive = 1;
			p->open = 0;
			p->ph = us_rand(&st->rng) * US_TAU;
			p->size = us_randr(&st->rng, 0.7f, 1.3f);
			return;
		}
	}
	p->alive = 0;
}

static void init_school(st_t *st, school_t *s, float A)
{
	us_rng *r = &st->rng;
	s->x = us_rand(r) * A;
	s->y = us_randr(r, 0.2f, 0.8f);
	s->head = us_rand(r) < 0.5f ? us_randr(r, -0.4f, 0.4f) : US_PI + us_randr(r, -0.4f, 0.4f);
	s->head_to = s->head;
	s->speed = us_randr(r, 0.03f, 0.05f);
	s->n = MAX_FISH;
	for (int i = 0; i < MAX_FISH; i++) {
		fish_t *f = &s->f[i];
		/* an elongated shoal: more spread along the heading than across */
		f->sa = us_randn(r) * 0.035f;
		f->sc = us_randn(r) * 0.018f;
		f->ox = f->sa * cosf(s->head);
		f->oy = f->sa * sinf(s->head);
		f->vx = cosf(s->head) * s->speed;
		f->vy = sinf(s->head) * s->speed;
		f->tau = us_randr(r, 0.06f, 0.4f);
		f->dx = cosf(s->head);
		f->dy = sinf(s->head);
	}
}

static void init_scene(st_t *st, const us_ctx *ctx)
{
	us_rng *r = &st->rng;
	for (int i = 0; i < GW * GH; i++) {
		st->u[0][i] = st->u[1][i] = 1.0f;
		st->v[0][i] = st->v[1][i] = 0.0f;
	}
	st->cur = 0;
	/* memory: a reef that was well fed elsewhere opens with more colonies */
	float reef = ctx->w->reef;
	int n = 8 + (int)(reef * 40.0f);
	for (int k = 0; k < n; k++)
		seed_at(st, us_rand(r) * GW, us_rand(r) * GH, us_randr(r, 2.0f, 4.5f));
	st->cycle = us_rand(r) * 3.0f;
	st->graze_x = us_rand(r);
	st->graze_y = us_rand(r);
	float ga = us_rand(r) * US_TAU;
	st->graze_vx = cosf(ga) * 0.006f;
	st->graze_vy = sinf(ga) * 0.004f;
	for (int i = 0; i < MAX_PL; i++) {
		plank_t *p = &st->pl[i];
		p->x = us_rand(r);
		p->y = us_rand(r);
		p->ph = us_rand(r) * 50.0f;
		p->size = us_rand(r);
	}
	/* a little pre-growth so the colonies aren't bare dots when the scene opens (more for a big reef) */
	for (int y = 0; y < CH; y++)
		for (int x = 0; x < CW; x++) {
			float rr = st->cycle + 2.1f * us_noise3((float)x / CW * 2.1f, (float)y / CH * 1.7f, 0.0f);
			regime(rr, &st->fmap[y * CW + x], &st->kmap[y * CW + x]);
			st->rmap[y * CW + x] = rr;
		}
	upsample_maps(st);
	/* only a little growth now; the rest is spread over the first second (see frame) so opening the
	 * scene is one ordinary frame, and the reef visibly grows in while it fades up */
	st->pre_total = 260 + (int)(reef * 800.0f);
	for (int k = 0; k < 24; k++)
		rd_iterate(st);
	st->pre_left = st->pre_total - 24;
	memcpy(st->prev, st->v[st->cur], sizeof(st->prev));
	/* the far reef: a mix of labyrinths and spot fields, many small colonies on the deeper floor */
	for (int i = 0; i < DW * DH; i++) {
		st->du[0][i] = st->du[1][i] = 1.0f;
		st->dv[0][i] = st->dv[1][i] = 0.0f;
	}
	for (int y = 0; y < DH; y++)
		for (int x = 0; x < DW; x++) {
			float m = us_smooth(-0.3f, 0.3f, us_noise2(x * 0.011f + 5.0f, y * 0.013f));
			float F = us_lerp(0.0300f, 0.0367f, m), K = us_lerp(0.0565f, 0.0649f, m);
			st->dF[y * DW + x] = F;
			st->dFK[y * DW + x] = F + K;
		}
	{
		float *U = st->du[0], *V = st->dv[0];
		int ns = 40 + (int)(reef * 40.0f);
		for (int k = 0; k < ns; k++) {
			int cx = (int)(us_rand(r) * DW), cy = (int)(us_rand(r) * DH);
			for (int dy = -3; dy <= 3; dy++)
				for (int dx = -3; dx <= 3; dx++) {
					int x = cx + dx, y = cy + dy;
					if (x < 0 || y < 0 || x >= DW || y >= DH || dx * dx + dy * dy > 9)
						continue;
					U[y * DW + x] = 0.5f;
					V[y * DW + x] = 0.25f;
				}
		}
	}
	st->dpre_left = 900;
	for (int i = 0; i < MAX_POLYP; i++)
		place_polyp(st, &st->po[i]);
	for (int k = 0; k < MAX_SCHOOL; k++) {
		init_school(st, &st->sc[k], ctx->aspect);
		st->sc[k].deep = k & 1;
	}
	st->caus_cell = 0.095f;
	st->inited = 1;
}

/* inlined max-blend pixel write (same result as us_put in MAX mode, without the call per pixel) */
static inline void put_max(us_canvas *c, int px, int py, float v, uint8_t mat)
{
	if ((unsigned)px >= (unsigned)c->w || (unsigned)py >= (unsigned)c->h)
		return;
	int iv = (int)(v * 255.0f + 0.5f);
	size_t i = (size_t)py * c->w + px;
	if (iv > c->ink[i]) {
		c->ink[i] = (uint8_t)(iv > 255 ? 255 : iv);
		c->mat[i] = mat;
	}
}

/* cheap anti-aliased hairline (Xiaolin Wu): 2 pixels per step, no sqrt. Used for the thousands of
 * short contour segments in Ink, where us_line's general-width coverage loop is too costly. */
static void wu_line(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
	float x0 = ax * c->s, y0 = ay * c->s, x1 = bx * c->s, y1 = by * c->s;
	int steep = fabsf(y1 - y0) > fabsf(x1 - x0);
	if (steep) {
		float t = x0; x0 = y0; y0 = t;
		t = x1; x1 = y1; y1 = t;
	}
	if (x0 > x1) {
		float t = x0; x0 = x1; x1 = t;
		t = y0; y0 = y1; y1 = t;
	}
	float dx = x1 - x0, g = dx > 1e-5f ? (y1 - y0) / dx : 0.0f;
	int i0 = (int)floorf(x0 + 0.5f), i1 = (int)floorf(x1 + 0.5f);
	/* partial coverage at the ends keeps joined segments seamless */
	for (int i = i0; i <= i1; i++) {
		float cx = i + 0.5f;
		float cov = 1.0f;
		if (i == i0) cov = us_sat(cx + 0.5f - x0);
		if (i == i1) cov = us_sat(x1 - (cx - 0.5f)) * (i == i0 ? cov : 1.0f);
		float y = y0 + g * (us_clampf(cx, x0, x1) - x0) - 0.5f;
		int iy = (int)floorf(y);
		float f = y - iy;
		if (steep) {
			put_max(c, iy, i, v * cov * (1 - f), mat);
			put_max(c, iy + 1, i, v * cov * f, mat);
		} else {
			put_max(c, i, iy, v * cov * (1 - f), mat);
			put_max(c, i, iy + 1, v * cov * f, mat);
		}
	}
}

/* 1-px crisp line (DDA) for pixel canvases */
static void px_line(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
	float x0 = ax * c->s, y0 = ay * c->s, x1 = bx * c->s, y1 = by * c->s;
	float dx = x1 - x0, dy = y1 - y0;
	int n = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
	if (n < 1)
		n = 1;
	float ix = dx / n, iy = dy / n;
	for (int k = 0; k <= n; k++)
		us_put(c, (int)(x0 + ix * k), (int)(y0 + iy * k), v, mat);
}

static const int8_t MS_SEGS[16][4] = {
	{-1, -1, -1, -1}, {3, 0, -1, -1}, {0, 1, -1, -1}, {3, 1, -1, -1},
	{1, 2, -1, -1},   {3, 0, 1, 2},   {0, 2, -1, -1}, {3, 2, -1, -1},
	{2, 3, -1, -1},   {2, 0, -1, -1}, {0, 1, 2, 3},   {2, 1, -1, -1},
	{1, 3, -1, -1},   {1, 0, -1, -1}, {0, 3, -1, -1}, {-1, -1, -1, -1},
};

/* marching squares on v at level lv; growth-front segments drawn brighter.
 * mode 0: the colony outline. In Ink each species has its own line character so they read apart even in one
 *         colour: staghorn + brain solid, polyp mounds dashed (stippled rims).
 * mode 1: the inner groove — brain coral only (its meanders become double-walled valleys). */
static void contours(st_t *st, us_canvas *c, float A, float lv, float base, float hot_gain, float wdt, int stride,
		     int mode)
{
	const float *V = st->v[st->cur];
	float sx = A / GW, sy = 1.0f / GH;
	for (int y = 0; y + stride < GH; y += stride) {
		const float *Vr = V + y * GW, *Vd = Vr + stride * GW;
		for (int x = 0; x + stride < GW; x += stride) {
			float a = Vr[x], b = Vr[x + stride], d = Vd[x], e = Vd[x + stride];
			int cs = (a > lv) | ((b > lv) << 1) | ((e > lv) << 2) | ((d > lv) << 3);
			if (cs == 0 || cs == 15)
				continue;
			int i00 = y * GW + x, i11 = i00 + stride * GW + stride;
			uint32_t h = hash2(x, y);
			int sp = species_idx(st->rmap[(y * CH / GH) * CW + x * CW / GW], h);
			if (!c->crisp) {
				if (mode == 1 && sp != 1)
					continue;
				if (mode == 0 && sp == 2 && (x + y) % 3 == 0)
					continue; /* dashed rim */
			}
			/* edge crossing points: 0 top (a-b), 1 right (b-e), 2 bottom (d-e), 3 left (a-d) */
			float px[4], py[4];
			float x0 = (x + 0.5f) * sx, y0 = (y + 0.5f) * sy, x1 = (x + stride + 0.5f) * sx, y1 = (y + stride + 0.5f) * sy;
#define T(p, q) us_clampf((lv - (p)) / ((q) - (p) + 1e-9f), 0, 1)
			px[0] = us_lerp(x0, x1, T(a, b)), py[0] = y0;
			px[1] = x1, py[1] = us_lerp(y0, y1, T(b, e));
			px[2] = us_lerp(x0, x1, T(d, e)), py[2] = y1;
			px[3] = x0, py[3] = us_lerp(y0, y1, T(a, d));
#undef T
			/* growth front: v rising here */
			float g = (V[i00] - st->prev[i00]) + (V[i11] - st->prev[i11]);
			float hot = us_sat(g * hot_gain);
			float val = base + (1.0f - base) * hot;
			uint8_t m = SPECIES_MAT[sp]; /* growth fronts: same hue, brighter end of its ramp */
			for (int s = 0; s < 4 && MS_SEGS[cs][s] >= 0; s += 2) {
				int p = MS_SEGS[cs][s], q = MS_SEGS[cs][s + 1];
				if (c->crisp)
					us_line(c, px[p], py[p], px[q], py[q], wdt, val, m);
				else
					wu_line(c, px[p], py[p], px[q], py[q], val, m);
			}
		}
	}
}

/* Ink: staghorn flesh is engraved with short diagonal hatching (and mounds get a stipple), so the three
 * species are distinguishable by line character alone in single-colour Ink */
static void coral_texture(st_t *st, us_canvas *c, float A, float val)
{
	const float *V = st->v[st->cur];
	float sx = A / GW, sy = 1.0f / GH;
	float hl = 1.6f * sx;
	for (int y = 1; y < GH - 1; y += 3) {
		for (int x = 1 + (y / 3 & 1); x < GW - 1; x += 3) {
			float v = V[y * GW + x];
			if (v < 0.2f)
				continue;
			uint32_t h = hash2(x, y);
			int sp = species_idx(st->rmap[(y * CH / GH) * CW + x * CW / GW], h);
			float cx = (x + 0.5f) * sx, cy = (y + 0.5f) * sy;
			float k = us_sat((v - 0.2f) * 8.0f);
			if (sp == 0) {
				wu_line(c, cx - hl, cy + hl, cx + hl, cy - hl, val * k, US_MAT_BLOOM);
			} else if (sp == 2 && (h & 1) == 0) {
				us_put(c, (int)(cx * c->s), (int)(cy * c->s), val * k, US_MAT_ACCENT);
			}
		}
	}
}

/* ---- caustics: a cellular light net (F2 - F1 of jittered, drifting feature points) ---- */
static void caustic_field(st_t *st, float A)
{
	float cs = st->caus_cell, t = st->caus_t;
	int fw = (int)ceilf(A / cs) + 3, fh = (int)ceilf(1.0f / cs) + 3;
	if (fw > FPW) fw = FPW;
	if (fh > FPH) fh = FPH;
	for (int j = 0; j < fh; j++)
		for (int i = 0; i < fw; i++) {
			uint32_t h = hash2(i + 101, j + 37);
			float bx = 0.2f + 0.6f * ((h & 1023) / 1023.0f), by = 0.2f + 0.6f * (((h >> 10) & 1023) / 1023.0f);
			float jx = us_noise3(i * 0.71f + 0.3f, j * 0.73f + 0.6f, t * 0.35f);
			float jy = us_noise3(i * 0.71f + 17.3f, j * 0.73f + 0.2f, t * 0.35f);
			st->fpx[j * FPW + i] = (i - 1 + bx + 0.3f * jx) * cs;
			st->fpy[j * FPW + i] = (j - 1 + by + 0.3f * jy) * cs;
		}
	/* gentle wobble: surface waves bend the light (separable, so it's two small tables) */
	float wobx[KH], woby[KW];
	int kw = st->kw, kh = st->kh;
	for (int y = 0; y < kh; y++) {
		float py = (y + 0.5f) / kh;
		wobx[y] = 0.012f * sinf(py * 23.0f + t * 1.3f) + 0.006f * sinf(py * 51.0f - t * 2.1f);
	}
	for (int x = 0; x < kw; x++) {
		float px = (x + 0.5f) / kw * A;
		woby[x] = 0.012f * sinf(px * 19.0f - t * 1.1f) + 0.006f * sinf(px * 47.0f + t * 1.7f);
	}
	float ics = 1.0f / cs;
	/* sunlight patches: a coarse noise grid */
	for (int j = 0; j < PH; j++)
		for (int i = 0; i < PW; i++)
			st->patch[j * PW + i] = 0.5f + 0.5f * us_noise2(i * 0.35f + t * 0.05f, j * 0.42f);
	for (int y = 0; y < kh; y++) {
		float py0 = (y + 0.5f) / kh;
		for (int x = 0; x < kw; x++) {
			float px = (x + 0.5f) / kw * A + wobx[y], py = py0 + woby[x];
			int ci = (int)floorf(px * ics) + 1, cj = (int)floorf(py * ics) + 1;
			ci = ci < 1 ? 1 : (ci > fw - 2 ? fw - 2 : ci);
			cj = cj < 1 ? 1 : (cj > fh - 2 ? fh - 2 : cj);
			float f1 = 1e9f;
			int best = 0;
			for (int jj = cj - 1; jj <= cj + 1; jj++) {
				const float *fx = st->fpx + jj * FPW, *fy = st->fpy + jj * FPW;
				for (int ii = ci - 1; ii <= ci + 1; ii++) {
					float dx = fx[ii] - px, dy = fy[ii] - py;
					float d = dx * dx + dy * dy;
					int lt = d < f1;
					best = lt ? jj * FPW + ii : best;
					f1 = fminf(f1, d);
				}
			}
			st->lab[y * kw + x] = (int16_t)best;
			st->wpx[y * kw + x] = px;
			st->wpy[y * kw + x] = py;
		}
	}
}

/* Exact single-line Voronoi web: wherever two neighbouring samples belong to different cells, put a vertex where
 * the bisector of their two feature points crosses that grid edge; join the vertices inside each grid square. */
static void caustic_lines(st_t *st, us_canvas *c, float A, float val)
{
	const float *V = st->v[st->cur];
	int kw = st->kw, kh = st->kh;
	float sx = A / kw, sy = 1.0f / kh;
	for (int y = 0; y + 1 < kh; y++)
		for (int x = 0; x + 1 < kw; x++) {
			int id[4] = {y * kw + x, y * kw + x + 1, (y + 1) * kw + x + 1, (y + 1) * kw + x}; /* tl tr br bl */
			int l0 = st->lab[id[0]];
			if (l0 == st->lab[id[1]] && l0 == st->lab[id[2]] && l0 == st->lab[id[3]])
				continue;
			float gx0 = (x + 0.5f) * sx, gy0 = (y + 0.5f) * sy;
			float cx[4] = {gx0, gx0 + sx, gx0 + sx, gx0}, cy[4] = {gy0, gy0, gy0 + sy, gy0 + sy};
			float vx[4], vy[4];
			int nv = 0;
			for (int e = 0; e < 4; e++) {
				int p = id[e], q = id[(e + 1) & 3];
				int la = st->lab[p], lb = st->lab[q];
				if (la == lb)
					continue;
				/* point X = P + t (Q - P) equidistant from feature a and b (in warped space) */
				float ax = st->fpx[la], ay = st->fpy[la], bx = st->fpx[lb], by = st->fpy[lb];
				float Px = st->wpx[p], Py = st->wpy[p], Qx = st->wpx[q], Qy = st->wpy[q];
				float nx = bx - ax, ny = by - ay;
				float den = nx * (Qx - Px) + ny * (Qy - Py);
				float tt = 0.5f;
				if (fabsf(den) > 1e-9f)
					tt = us_sat((0.5f * (bx * bx + by * by - ax * ax - ay * ay) - (nx * Px + ny * Py)) / den);
				int e1 = (e + 1) & 3;
				vx[nv] = us_lerp(cx[e], cx[e1], tt);
				vy[nv] = us_lerp(cy[e], cy[e1], tt);
				nv++;
			}
			/* sunlight is patchy, and dimmer over coral (it's lighting the coral instead) */
			int gx = (int)((x + 0.5f) * GW / kw), gy = (int)((y + 0.5f) * GH / kh);
			int over = V[gy * GW + gx] > 0.12f;
			if (over && !c->crisp)
				continue; /* Ink: the coral's own engraving carries it (and the web there was only clutter) */
			float patch = st->patch[(y * PH / kh) * PW + x * PW / kw];
			float vv = val * (over ? 0.3f : 1.0f) * patch * patch * 1.7f;
			if (vv < (c->crisp ? 0.02f : 0.035f))
				continue;
			if (c->crisp) {
				for (int k = 0; k + 1 < nv; k++)
					px_line(c, vx[k], vy[k], vx[k + 1], vy[k + 1], vv, US_MAT_WATER);
			} else if (nv == 2) {
				wu_line(c, vx[0], vy[0], vx[1], vy[1], vv, US_MAT_WATER);
			} else if (nv > 2) {
				float mx = 0, my = 0;
				for (int k = 0; k < nv; k++)
					mx += vx[k], my += vy[k];
				mx /= nv, my /= nv;
				for (int k = 0; k < nv; k++)
					wu_line(c, vx[k], vy[k], mx, my, vv, US_MAT_WATER);
			}
		}
}

/* faded-out pixels keep their old material, which the Ink bloom would light up in stale colours: hand the
 * dark ones the ambient material. An eighth of the rows per frame (the lag is invisible, the cost an eighth). */
static void settle_mats(us_canvas *c, uint8_t ambient, unsigned frame)
{
	for (int y = (int)(frame & 7); y < c->h; y += 8) {
		const uint8_t *restrict ink = c->ink + (size_t)y * c->w;
		uint8_t *restrict mat = c->mat + (size_t)y * c->w;
		for (int i = 0; i < c->w; i++)
			mat[i] = ink[i] < 20 ? ambient : mat[i];
	}
}

/* the far reef: faint single iso-lines of the deeper colonies, hidden wherever the near reef covers them.
 * Water haze: brightness falls off with depth (a slow noise depth map), so the floor reads as further away.
 * The floor grows one step every other frame, so the traced segments are cached and refreshed every 4th. */
static void far_trace(st_t *st, float A, float t)
{
	const float *D = st->dv[st->dcur], *V = st->v[st->cur];
	float sx = A / DW, sy = 1.0f / DH, lv = 0.2f;
	float dep[10 * 6];
	for (int j = 0; j < 6; j++)
		for (int i = 0; i < 10; i++) {
			float n = us_noise3(i * 0.38f, j * 0.45f, t * 0.01f);
			dep[j * 10 + i] = us_sat(0.55f + 0.75f * n);
		}
	int ns = 0;
	for (int y = 0; y + 1 < DH; y++) {
		const float *Dr = D + y * DW, *Dd = Dr + DW;
		const float *Vr = V + (y * GH / DH) * GW;
		for (int x = (y & 1); x + 1 < DW; x += 2) { /* dashed: the far floor reads softer than the near reef */
			float a = Dr[x], b = Dr[x + 1], d = Dd[x], e = Dd[x + 1];
			int cs = (a > lv) | ((b > lv) << 1) | ((e > lv) << 2) | ((d > lv) << 3);
			if (cs == 0 || cs == 15 || Vr[x * GW / DW] > 0.09f)
				continue;
			float k = dep[(y * 6 / DH) * 10 + x * 10 / DW];
			if (k < 0.08f)
				continue;
			float x0 = (x + 0.5f) * sx, y0 = (y + 0.5f) * sy, x1 = x0 + sx, y1 = y0 + sy;
			float px[4], py[4];
#define T(p, q) us_clampf((lv - (p)) / ((q) - (p) + 1e-9f), 0, 1)
			px[0] = us_lerp(x0, x1, T(a, b)), py[0] = y0;
			px[1] = x1, py[1] = us_lerp(y0, y1, T(b, e));
			px[2] = us_lerp(x0, x1, T(d, e)), py[2] = y1;
			px[3] = x0, py[3] = us_lerp(y0, y1, T(a, d));
#undef T
			for (int q2 = 0; q2 < 4 && MS_SEGS[cs][q2] >= 0 && ns < MAX_FSEG; q2 += 2) {
				int p = MS_SEGS[cs][q2], q = MS_SEGS[cs][q2 + 1];
				st->fseg[ns][0] = px[p], st->fseg[ns][1] = py[p];
				st->fseg[ns][2] = px[q], st->fseg[ns][3] = py[q];
				st->fsegk[ns++] = k;
			}
		}
	}
	st->nfseg = ns;
	st->fseg_A = A;
}

static void far_lines(st_t *st, us_canvas *c, float A, float val, float t)
{
	if ((st->fcount_m & 3) == 0 || st->fseg_A != A)
		far_trace(st, A, t);
	for (int i = 0; i < st->nfseg; i++)
		wu_line(c, st->fseg[i][0], st->fseg[i][1], st->fseg[i][2], st->fseg[i][3], val * st->fsegk[i], US_MAT_WATER);
}

/* sun shafts: bundles of fine lines fanning down from the bright surface, slowly breathing in and out.
 * Each fades with depth. Pixel canvases draw them as a sparse stipple so they stay a hint, not a band. */
static void sun_rays(st_t *st, us_canvas *c, float A, float I)
{
	if (I < 0.004f)
		return;
	float t = st->ray_t;
	float vx = A * (0.45f + 0.18f * sinf(t * 0.021f)), vy = -0.9f;
	for (int i = 0; i < NRAY; i++) {
		float ri = us_noise2(i * 3.17f, t * 0.06f);
		float k = us_sat(0.35f + 1.1f * ri);
		k *= k;
		if (k < 0.02f)
			continue;
		float an = -0.62f + 1.24f * (i + 0.5f) / NRAY + 0.08f * us_noise2(i * 1.9f + 4.0f, t * 0.025f);
		/* a shaft is a bundle of hairlines with a soft (gaussian) cross-profile; the bloom fuses it into a beam */
		int nl = c->crisp ? 2 : 4 + (i * 7 % 3);
		float spread = c->crisp ? 0.006f : 0.005f;
		for (int l = 0; l < nl; l++) {
			float u = (l - 0.5f * (nl - 1)) / (0.5f * nl);
			float a = an + (l - 0.5f * (nl - 1)) * spread;
			float dx = sinf(a), dy = cosf(a);
			float s0 = -vy / dy, s1 = (1.02f - vy) / dy;
			float lv = I * k * expf(-u * u * 2.0f);
			/* five segments, each dimmer: the light is absorbed as it goes down */
			for (int sg = 0; sg < 4; sg++) {
				float u0 = us_lerp(s0, s1, sg / 5.0f), u1 = us_lerp(s0, s1, (sg + 1) / 5.0f);
				float v = lv * (1.0f - 0.18f * sg);
				float xa = vx + dx * u0, ya = vy + dy * u0, xb = vx + dx * u1, yb = vy + dy * u1;
				if (c->crisp) {
					float pxa = xa * c->s, pya = ya * c->s, pxb = xb * c->s, pyb = yb * c->s;
					int n = (int)fmaxf(fabsf(pxb - pxa), fabsf(pyb - pya));
					for (int q = 0; q <= n; q += 3)
						us_put(c, (int)(pxa + (pxb - pxa) * q / (n + 1e-6f)),
						       (int)(pya + (pyb - pya) * q / (n + 1e-6f)), v, US_MAT_LIGHT);
				} else {
					wu_line(c, xa, ya, xb, yb, v, US_MAT_LIGHT);
				}
			}
		}
	}
}

/* pixel canvases: per-pixel reef body (species materials, banded rim/flesh/core) over a seabed with the far
 * reef dithered in. Each species has its own fill texture so Mono-pixel tells them apart too:
 * staghorn solid with a bright core, brain banded with a dark groove, polyp mounds checkered. */
static void draw_pixel_body(st_t *st, us_canvas *c, float sw, float body_gain, float far_v)
{
	const float *V = st->v[st->cur], *D = st->dv[st->dcur];
	float l0 = 0.14f - sw, l1 = 0.2f - sw, l2 = 0.3f - sw;
	uint8_t b0 = (uint8_t)(us_sat(0.26f * body_gain) * 255), b1 = (uint8_t)(us_sat(0.42f * body_gain) * 255),
		b2 = (uint8_t)(us_sat(0.62f * body_gain) * 255);
	uint8_t bf = (uint8_t)(us_sat(far_v) * 255);
	for (int j = 0; j < c->h; j++) {
		int gy = j * GH / c->h, ry = j * CH / c->h, dy = j * DH / c->h;
		const float *Vr = V + gy * GW, *Dr = D + dy * DW;
		uint8_t *ink = c->ink + (size_t)j * c->w, *mat = c->mat + (size_t)j * c->w;
		for (int i = 0; i < c->w; i++) {
			float vv = Vr[i * GW / c->w];
			uint32_t h = hash2(i, j);
			uint8_t b, m;
			if (vv > l0) {
				int sp = species_idx(st->rmap[ry * CW + i * CW / c->w], h);
				b = vv > l2 ? b2 : (vv > l1 ? b1 : b0);
				if (sp == 1 && vv > l1 && vv < l1 + 0.035f)
					b = b0 / 2; /* brain: the groove between the ridges */
				else if (sp == 2 && vv <= l2 && ((i + j) & 1))
					b = b0; /* mounds: checkered flesh */
				m = SPECIES_MAT[sp];
			} else if (bf && ((i | j) & 1) == 0 && Dr[i * DW / c->w] > 0.2f) {
				b = bf, m = US_MAT_WATER; /* far reef, seen through the water */
			} else if ((h >> 8) % 61 == 0) {
				b = 33, m = US_MAT_EARTH; /* sand grains */
			} else {
				continue;
			}
			if (b > ink[i]) {
				ink[i] = b;
				mat[i] = m;
			}
		}
	}
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *st = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	us_rng *r = &st->rng;
	float dt = ctx->dt, rdt = ctx->real_dt, A = ctx->aspect, dens = ctx->p->density;
	if (!st->inited)
		init_scene(st, ctx);
	PROF_START;

	st->low_env = us_approach(st->low_env, us_sat(a->lowmid * 0.8f + a->mid * 0.3f), 1.5f, rdt);
	st->bass_env = us_approach(st->bass_env, us_sat(a->bass * 0.7f + a->kick * 0.7f), 0.1f, rdt);
	st->hi_env = us_approach(st->hi_env, us_sat(a->high + 0.6f * a->air), 0.2f, rdt);
	st->lvl_env = us_approach(st->lvl_env, us_sat(a->level * 1.2f), 0.6f, rdt);
	/* polyps: open with sustained bass/level (slow), close in silence */
	st->open_env = us_approach(st->open_env, us_sat(0.15f + 0.55f * a->bass + 0.45f * a->level), 0.8f, rdt);
	st->nudge = us_approach(st->nudge, 0.0f, 2.5f, rdt);
	if (a->beat)
		st->nudge = us_sat(st->nudge + 0.35f * a->onset);
	if (a->beat && a->kick > 0.3f) {
		st->kick_wave = us_sat(0.4f + a->kick);
		st->kick_wave_x = -0.1f;
	}
	st->kick_wave_x += rdt * 1.6f * A; /* ripple crosses the screen in ~0.6 s */
	st->kick_wave = us_approach(st->kick_wave, 0.0f, 0.9f, rdt);
	float nutrients = us_sat(0.4f * w->pollen + 0.6f * w->water_level);

	/* ---- parameter map: slow regime cycle + spatial drift + grazing patch ---- */
	st->cycle += dt * (1.0f / 70.0f) * (0.6f + 0.8f * st->low_env);
	st->flow_t += dt * (0.02f + 0.05f * a->level);
	st->caus_t += dt * (0.35f + 0.5f * st->lvl_env + 0.4f * st->hi_env);
	st->graze_x += st->graze_vx * dt;
	st->graze_y += st->graze_vy * dt;
	st->graze_x -= floorf(st->graze_x);
	st->graze_y -= floorf(st->graze_y);
	if (us_rand(r) < dt * 0.03f) {
		float ga = us_rand(r) * US_TAU;
		st->graze_vx = cosf(ga) * 0.007f;
		st->graze_vy = sinf(ga) * 0.005f;
	}
	/* coverage target rises with density; overgrown reefs get grazed harder */
	float target = 0.18f + 0.18f * dens + 0.06f * nutrients;
	float over = us_clampf((st->cover_s - target) * 6.0f, -1.0f, 1.5f);
	float t = (float)ctx->t;
	/* the maps drift slowly: refresh them every third frame */
	if (st->fcount++ % 3 == 0) {
	for (int y = 0; y < CH; y++)
		for (int x = 0; x < CW; x++) {
			float fx = (float)x / CW, fy = (float)y / CH;
			float rr = st->cycle + 2.1f * us_noise3(fx * 2.1f, fy * 1.7f, t * 0.004f);
			float F, K;
			regime(rr, &F, &K);
			F += 0.004f * st->low_env + 0.003f * nutrients - 0.002f;
			K += 0.0012f * st->nudge * us_noise2(fx * 6.0f + t * 0.1f, fy * 6.0f);
			/* grazing patch (toroidal distance) */
			float gx = fabsf(fx - st->graze_x), gy = fabsf(fy - st->graze_y);
			gx = gx > 0.5f ? 1 - gx : gx;
			gy = gy > 0.5f ? 1 - gy : gy;
			float gd = (gx * gx * 3.0f + gy * gy * 3.0f);
			float graze = expf(-gd * 40.0f) * (0.6f + 0.9f * us_sat(over));
			K += 0.006f * graze + 0.0015f * us_sat(over);
			/* habitat: slowly wandering barren ground (sand channels) keeps open water between colonies */
			float hab = us_fbm3(fx * 2.6f + 3.3f, fy * 2.0f, t * 0.006f, 2);
			K += 0.014f * us_smooth(0.0f, 0.22f, hab - 0.25f * dens + 0.12f);
			st->rmap[y * CW + x] = rr;
			st->fmap[y * CW + x] = F;
			st->kmap[y * CW + x] = K;
		}
	upsample_maps(st);
	}
	PROF(0);

	/* ---- run the RD: slow creep; low-mids + nutrients make it faster ---- */
	float ips = 65.0f + 130.0f * st->low_env + 45.0f * nutrients;
	st->iter_acc += ips * dt;
	int iters = (int)st->iter_acc;
	if (iters > 7)
		iters = 7;
	st->iter_acc -= (float)(int)st->iter_acc;
	for (int k = 0; k < iters; k++)
		rd_iterate(st);
	/* opening: the rest of the pre-growth, spread over ~1 s (near) and ~2 s (far reef) */
	st->intro_t += rdt;
	if (st->pre_left > 0) {
		int n = (st->pre_total + 29) / 30;
		if (n > st->pre_left)
			n = st->pre_left;
		for (int k = 0; k < n; k++)
			rd_iterate(st);
		st->pre_left -= n;
	}
	{
		int n = st->dpre_left > 0 ? (st->dpre_left < 15 ? st->dpre_left : 15) : 0;
		st->dpre_left -= n;
		/* afterwards the deep floor grows slowly: one step every other frame */
		if (n == 0 && (st->fcount & 1))
			n = 1;
		for (int k = 0; k < n; k++)
			far_iterate(st);
	}
	float fa = us_smooth(0.0f, 1.3f, st->intro_t), fd = us_smooth(0.6f, 2.4f, st->intro_t);
	PROF(1);

	/* growth-front memory (~0.7 s) */
	{
		const float *V = st->v[st->cur];
		float kk = 1.0f - expf(-dt / 0.7f);
		int sum = 0;
		for (int i = 0; i < GW * GH; i++) {
			st->prev[i] += (V[i] - st->prev[i]) * kk;
			sum += V[i] > 0.18f;
		}
		st->cover = (float)sum / (GW * GH);
		st->cover_s = us_approach(st->cover_s, st->cover, 3.0f, dt);
	}
	/* world: reef follows coverage (slowly) */
	us_world_add(w, &w->reef, 0.02f * (us_sat(st->cover_s * 2.2f) - w->reef), dt);

	/* ---- re-seeding: never dies out ---- */
	st->seed_t -= dt;
	if (st->seed_t <= 0 || st->cover < 0.02f) {
		seed_at(st, us_rand(r) * GW, us_rand(r) * GH, us_randr(r, 2.0f, 3.5f));
		st->seed_t = us_randr(r, 4.0f, 12.0f) * (st->cover < 0.05f ? 0.2f : 1.0f);
	}

	/* ---- spawning: kicks/bass release bundles from the living edge ---- */
	if (a->beat && st->bass_env > 0.25f) {
		int nb = 1 + (int)(st->bass_env * (2.0f + 5.0f * dens));
		const float *V = st->v[st->cur];
		for (int b = 0; b < nb && st->nsp < MAX_SP; b++) {
			for (int tries = 0; tries < 12; tries++) {
				int x = (int)(us_rand(r) * GW), y = (int)(us_rand(r) * GH);
				float vv = V[y * GW + x];
				if (vv > 0.12f && vv < 0.3f) {
					spawn_t *q = &st->sp[st->nsp++];
					q->x = (x + 0.5f) / GW;
					q->y = (y + 0.5f) / GH;
					float an = us_rand(r) * US_TAU;
					float spd = us_randr(r, 0.01f, 0.04f) * (0.5f + st->bass_env);
					q->vx = cosf(an) * spd / A;
					q->vy = sinf(an) * spd - 0.01f;
					q->life = us_randr(r, 6.0f, 16.0f);
					q->age = 0;
					break;
				}
			}
		}
	}

	/* ---- sunlight: caustic net (moonlight at night: dimmer, slower) ---- */
	float day = 0.45f + 0.55f * w->daylight;
	float light = fa * day * (0.26f + 0.22f * st->lvl_env + 0.12f * st->hi_env + 0.08f * a->onset);
	st->ray_t += dt * (1.0f + 0.8f * st->lvl_env);
	/* shafts: strongest in daylight, swelling with phrase-scale loudness, never flashing */
	float rays = fa * (0.17f + 0.25f * w->daylight) * (0.75f + 0.45f * a->swell);
	st->kw = ctx->crisp ? 64 : 76; /* Ink: the light net is background now (rays + far reef carry depth), so a coarser one */
	st->kh = ctx->crisp ? 36 : 43;
	caustic_field(st, A);
	PROF(11);

	/* ---- draw ---- */
	if (!ctx->crisp)
		settle_mats(c, US_MAT_WATER, st->fcount_m++);
	PROF(10);
	sun_rays(st, c, A, rays * (ctx->crisp ? 1.6f : 1.5f));
	PROF(9);
	if (ctx->crisp) {
		float sw = 0.05f * st->bass_env;
		caustic_lines(st, c, A, us_sat(0.06f + light * 0.45f));
		draw_pixel_body(st, c, sw, (0.9f + 0.25f * st->bass_env) * fa, 0.17f * fd);
		contours(st, c, A, 0.14f - sw, (0.7f + 0.2f * st->bass_env) * fa, 35.0f, 0.003f, 2, 0);
	} else {
		far_lines(st, c, A, 0.2f * fd, (float)ctx->t);
		PROF(2);
		caustic_lines(st, c, A, us_sat(light));
		PROF(3);
		/* ink: topographic iso-lines — growth rings of the colony */
		contours(st, c, A, 0.12f - 0.05f * st->bass_env, (0.66f + 0.2f * st->bass_env) * fa, 60.0f * fa, 0.0013f, 1, 0);
		contours(st, c, A, 0.22f - 0.04f * st->bass_env, 0.34f * fa, 0.0f, 0.001f, 1, 1);
		coral_texture(st, c, A, 0.3f * fa);
	}

	PROF(4);
	/* ---- polyps: little stars on the coral that open with the music ---- */
	{
		int np = 40 + (int)(dens * (MAX_POLYP - 40));
		const float *V = st->v[st->cur];
		float sx = A / GW, sy = 1.0f / GH;
		for (int i = 0; i < np; i++) {
			polyp_t *p = &st->po[i];
			if (!p->alive || V[(int)p->gy * GW + (int)p->gx] < 0.22f) {
				/* coral died back here (or never found a home): look for a new spot now and then */
				if (us_rand(r) < 0.08f)
					place_polyp(st, p);
				else
					p->alive = 0;
				if (!p->alive)
					continue;
			}
			float x = p->gx * sx, y = p->gy * sy;
			float wave = st->kick_wave * expf(-fabsf(x - st->kick_wave_x) * 9.0f);
			p->ph += dt * (0.4f + 0.6f * p->size);
			float target = us_sat(st->open_env * (0.75f + 0.25f * sinf(p->ph)) + wave);
			p->open = us_approach(p->open, target, target > p->open ? 0.12f : 0.6f, rdt);
			float o = p->open;
			float rad = (0.004f + 0.009f * o) * p->size;
			/* closed polyps wear their coral's colour; open ones glow (the music lights the reef up) */
			int sp = species_idx(st->rmap[((int)p->gy * CH / GH) * CW + (int)p->gx * CW / GW],
					     hash2((int)p->gx, (int)p->gy));
			uint8_t pm = o > 0.6f ? US_MAT_LIGHT : SPECIES_MAT[sp];
			if (ctx->crisp) {
				/* 1 px closed, a plus when open */
				us_put(c, (int)(x * c->s), (int)(y * c->s), (0.55f + 0.4f * o) * fa, US_MAT_LIGHT);
				if (o > 0.45f) {
					int px = (int)(x * c->s), py = (int)(y * c->s);
					float vv = (0.35f + 0.5f * o) * fa;
					us_put(c, px - 1, py, vv, pm);
					us_put(c, px + 1, py, vv, pm);
					us_put(c, px, py - 1, vv, pm);
					us_put(c, px, py + 1, vv, pm);
				}
			} else {
				int nt = 6;
				float rot = p->ph * 0.15f;
				float vv = (0.4f + 0.55f * o) * fa;
				for (int k = 0; k < nt; k++) {
					float an = rot + k * (US_TAU / nt);
					/* tentacles curl inward when closed */
					float bend = (1.0f - o) * 0.9f;
					float mx = x + cosf(an) * rad * 0.55f, my = y + sinf(an) * rad * 0.55f;
					float ex = x + cosf(an + bend) * rad, ey = y + sinf(an + bend) * rad;
					wu_line(c, x, y, mx, my, vv * 0.8f, pm);
					wu_line(c, mx, my, ex, ey, vv, pm);
				}
				us_point(c, x, y, 0.0012f, (0.5f + 0.4f * o) * fa, US_MAT_LIGHT);
			}
		}
	}

	PROF(5);
	/* ---- spawn bundles: rise, wander, settle and seed ---- */
	for (int i = 0; i < st->nsp;) {
		spawn_t *q = &st->sp[i];
		float px = q->x * A, py = q->y;
		float fx = us_noise3(px * 2.0f, py * 2.0f, st->flow_t * 3.0f);
		float fy = us_noise3(px * 2.0f + 7.1f, py * 2.0f, st->flow_t * 3.0f);
		q->vx += (fx * 0.02f / A) * dt;
		q->vy += (fy * 0.02f - 0.006f) * dt;
		q->vx *= expf(-dt * 0.6f);
		q->vy *= expf(-dt * 0.6f);
		q->x += q->vx * dt;
		q->y += q->vy * dt;
		q->x -= floorf(q->x);
		q->y -= floorf(q->y);
		q->age += dt;
		if (q->age >= q->life) {
			seed_at(st, q->x * GW, q->y * GH, us_randr(r, 1.5f, 2.5f));
			*q = st->sp[--st->nsp];
			continue;
		}
		float fade = us_sat(q->age * 2.0f) * us_sat((q->life - q->age) * 0.5f);
		float v = (0.55f + 0.45f * st->bass_env) * fade;
		us_disc(c, q->x * A, q->y, ctx->crisp ? 0.004f : 0.0028f, v, US_MAT_BLOOM);
		if (!ctx->crisp)
			us_ring(c, q->x * A, q->y, 0.006f + 0.003f * st->bass_env, 0.0008f, 0.35f * v, US_MAT_BLOOM);
		i++;
	}

	/* ---- plankton / spores drifting in the current ---- */
	{
		int n = 60 + (int)(dens * (MAX_PL - 60));
		float sparkle = st->hi_env;
		float tt = st->flow_t;
		for (int i = 0; i < n; i++) {
			plank_t *p = &st->pl[i];
			float px = p->x * A;
			float fx = us_noise3(px * 1.4f, p->y * 1.4f, tt);
			float fy = us_noise3(px * 1.4f + 31.7f, p->y * 1.4f, tt);
			float spd = 0.012f + 0.03f * a->level;
			p->x += (fx * spd + 0.004f) * dt / A;
			p->y += fy * spd * dt;
			p->x -= floorf(p->x);
			p->y -= floorf(p->y);
			p->ph += dt * (0.5f + 3.0f * sparkle);
			float tw = 0.5f + 0.5f * sinf(p->ph + p->size * 9.0f);
			float v = 0.18f + 0.22f * p->size + (0.25f + 0.35f * sparkle) * tw * tw * tw + 0.2f * a->kick * p->size;
			us_point(c, p->x * A, p->y, (ctx->crisp ? 0.002f : 0.0011f) * (0.8f + 0.6f * p->size), us_sat(v) * fa,
				 p->size > 0.85f ? US_MAT_LIGHT : US_MAT_WATER);
		}
	}

	PROF(6);
	/* ---- fish schools: cruise the reef, dart together on hits ---- */
	{
		float life = us_sat(0.35f + 0.4f * w->flock + 0.3f * w->pollen + 0.3f * w->reef);
		int ns = 1 + (int)(dens * 2.0f + life * 1.2f);
		if (ns > MAX_SCHOOL)
			ns = MAX_SCHOOL;
		int nf = 8 + (int)((MAX_FISH - 8) * (0.3f + 0.7f * dens) * life);
		if (nf > MAX_FISH)
			nf = MAX_FISH;
		float W = A + 0.3f; /* wrap span: schools leave one side and come back on the other */
		int dart = a->beat && a->onset > 0.35f;
		for (int k = 0; k < ns; k++) {
			school_t *s = &st->sc[k];
			if (dart && us_rand(r) < 0.75f) {
				/* a hit startles the school: it wheels and sprints, but as a smooth surge (no teleporting
				 * headings), so fish stay crisp little bodies instead of smearing into streaks */
				float side = us_rand(r) < 0.5f ? -1.0f : 1.0f;
				s->head_to += side * us_randr(r, 0.7f, 1.6f) * (0.5f + 0.5f * a->onset);
				s->burst_to = us_sat(s->burst_to + 0.45f + 0.4f * a->onset);
				s->scare = us_sat(s->scare + 0.5f * a->onset);
			}
			s->burst_to = us_approach(s->burst_to, 0.0f, 0.6f, rdt);
			s->burst = us_approach(s->burst, s->burst_to, 0.12f, rdt);
			s->scare = us_approach(s->scare, 0.0f, 1.2f, rdt);
			/* meander; steer back toward the middle band vertically */
			s->turn = us_noise2(k * 13.7f, st->flow_t * 4.0f + k) * 0.9f;
			float hx = cosf(s->head), hy = sinf(s->head);
			float wx = hx >= 0 ? 1.0f : -1.0f;
			float wy = (0.5f - s->y) * (s->y < 0.2f || s->y > 0.8f ? 4.0f : 0.8f);
			float wl = sqrtf(wx * wx + wy * wy);
			s->head_to += (s->turn + 1.5f * (hx * wy - hy * wx) / wl) * dt;
			s->head = us_approach(s->head, s->head_to, 0.22f, rdt);
			float spd = s->speed * (1.0f + 0.6f * st->lvl_env + 1.8f * s->burst) * (s->deep ? 0.65f : 1.0f);
			float cvx = cosf(s->head) * spd, cvy = sinf(s->head) * spd;
			s->x += cvx * dt;
			s->y += cvy * dt;
			if (s->y < 0.04f) { s->y = 0.04f; s->head = -s->head; s->head_to = -s->head_to; }
			if (s->y > 0.96f) { s->y = 0.96f; s->head = -s->head; s->head_to = -s->head_to; }
			if (s->x > A + 0.15f) s->x -= W;
			if (s->x < -0.15f) s->x += W;
			float ca = cosf(s->head), sa = sinf(s->head);
			float spread = 1.0f + 2.2f * s->scare;
			for (int i = 0; i < nf; i++) {
				fish_t *f = &s->f[i];
				/* where this fish wants to be in the shoal */
				float tx = (f->sa * ca - f->sc * sa) * spread, ty = (f->sa * sa + f->sc * ca) * spread;
				float jit = us_noise2(i * 3.1f + k * 17.0f, st->flow_t * 9.0f);
				float wvx = cvx + (tx - f->ox) * 1.6f + jit * 0.01f * -sa;
				float wvy = cvy + (ty - f->oy) * 1.6f + jit * 0.01f * ca;
				/* each fish reacts with its own lag: turns ripple through the school */
				float tau = f->tau * (s->burst > 0.3f ? 0.6f : 1.0f);
				f->vx = us_approach(f->vx, wvx, tau, dt);
				f->vy = us_approach(f->vy, wvy, tau, dt);
				f->ox += (f->vx - cvx) * dt;
				f->oy += (f->vy - cvy) * dt;
				float vl = sqrtf(f->vx * f->vx + f->vy * f->vy) + 1e-6f;
				float ndx = f->vx / vl, ndy = f->vy / vl;
				float turn = fabsf(ndx * f->dy - ndy * f->dx);
				f->dx = ndx;
				f->dy = ndy;
				/* silver flank flashes as a fish turns */
				f->flash = us_approach(f->flash, us_sat(turn * 6.0f), 0.15f, dt);
				float x = s->x + f->ox, y = s->y + f->oy;
				if (x > A + 0.15f) x -= W;
				if (x < -0.15f) x += W;
				if (x < -0.03f || x > A + 0.03f)
					continue;
				/* near schools: bigger, silver; deep schools: small, dim, blue with the water between */
				float sz = s->deep ? 0.6f : 1.15f;
				float L = (ctx->crisp ? 0.016f : 0.011f) * sz;
				float vv = us_sat(0.62f + 0.38f * f->flash + 0.1f * s->burst) * (s->deep ? 0.42f : 1.0f) * fa;
				uint8_t fm = s->deep ? US_MAT_WATER : US_MAT_ACCENT;
				float bx = x - ndx * L, by = y - ndy * L;
				if (ctx->crisp) {
					if (s->deep)
						px_line(c, x, y, bx, by, vv, fm);
					else
						us_line(c, x, y, bx, by, 0.0045f, vv, fm);
				} else {
					float px = -ndy, py = ndx;
					/* spindle body: two arcs approximated by a 4-point outline */
					float mx = x - ndx * L * 0.4f, my = y - ndy * L * 0.4f;
					float bw = 0.0018f * sz;
					wu_line(c, x, y, mx + px * bw, my + py * bw, vv, fm);
					wu_line(c, x, y, mx - px * bw, my - py * bw, vv, fm);
					wu_line(c, mx + px * bw, my + py * bw, bx, by, vv * 0.85f, fm);
					wu_line(c, mx - px * bw, my - py * bw, bx, by, vv * 0.85f, fm);
					/* tail fin, beating */
					float beat = sinf((float)ctx->t * (14.0f + 20.0f * s->burst) + i) * 0.0012f * sz;
					float tl = 0.0035f * sz, tw = 0.0022f * sz;
					wu_line(c, bx, by, bx - ndx * tl + px * (tw + beat), by - ndy * tl + py * (tw + beat), vv * 0.7f, fm);
					wu_line(c, bx, by, bx - ndx * tl - px * (tw - beat), by - ndy * tl - py * (tw - beat), vv * 0.7f, fm);
				}
			}
		}
	}
	PROF(7);
	PROF_END;
}

const us_scene_vt us_scene_coral = {"coral", "Coral", 0.12f, create, destroy, frame};
