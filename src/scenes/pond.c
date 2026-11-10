/* Understory scene: Pond — a still pond seen from directly above.
 *
 * Under the surface a slow caustic light network (the Voronoi edges of a drifting, warped cellular net)
 * lies on the bed, gathered in the sun's reflection and fading to a faint web away from it; koi drift
 * through it and hide it where they pass. Lily pads (solid, notched, veined) and their lotus flowers are the
 * focal points; water striders skate in bursts, dimpling the surface; reed clumps fringe the bank. Around
 * the water: grassy banks, pebbles at the waterline, ferns and marsh flowers, a wooden jetty, and a tree
 * leaning out over the pond whose shade dims the caustics and whose weeping twigs dimple the surface. Deep
 * winter puts an ice skin on the pond: muted rings, dim light, crack lines that creak further on kicks.
 *
 * Audio, one answer per part of the music:
 *   kick  -> a lily pad dips, a sharp ring spreads from under it, a front of focused light sweeps out
 *            across the bed from it and the whole net surges for an instant; koi dart.
 *   snare -> a splash: the frog kicks off its pad (or a fish breaks the surface), a crown of spray, the drops
 *            pattering back as a shiver of dimples; nearby pads lurch; a dragonfly darts.
 *   hats  -> a scatter of sun glints in the sun's reflection; a strider strokes.
 *   level / swell -> the light on the water (quiet = glassy and dim); bass focuses it; tonal onsets with no
 *            drum (bells, piano) drop single soft rings; bands nod the reeds and open the lotuses; mids
 *            rock the pads; width brings the breeze (riffles, fronds); low mids rustle leaves loose.
 * World: reads water_level (pond size), insects (striders), season (ice, autumn leaves, lotus flowers
 *        in summer, pads die back in winter), daylight (caustic strength, the sun's place). Writes a little
 *        moisture.
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
#define MAX_DF 3      /* dragonflies */
#define MAX_GLINTS 96 /* sun glints on the surface (hats / air) */
#define MAX_FALL 8    /* leaves falling from the tree */
#define NRIFF 3       /* cat's-paw wind riffles (stereo width) */
#define MAX_PULSES 4  /* kick: fronts of focused light sweeping out across the bed */
#define MAX_SPLASH 4  /* snare: splashes (spray crown + a shiver of dimples) */
#define MAX_IRIS 12   /* flag irises in the shallows along the bank */
#define MAX_ROS 10    /* broad-leaved marsh plants (rosettes of round leaves) back on the bank */
#define MAX_MOSS 640  /* clover / moss sprigs carpeting the bank */
#define MAX_DUCK 6    /* duckweed rafts in the quiet coves */
#define DUCK_N 48

typedef struct {
	float x, y, r, speed, amp, age, life;
	uint8_t small; /* strider dimples / drips: no caustic contribution */
} ring_t;

typedef struct {
	float u, y, R, rot, notch, vx, vy, vr, bob, bph, flower;
	float rock, rph, open; /* mid-driven rocking; lotus opening (one spectrum band each) */
	float jolt, jdir;      /* a snare splash nearby: the pad lurches on the wave and settles */
} pad_t;

typedef struct {
	float u, y, hd, vx, vy, stroke_t;
} strider_t;

typedef struct {
	float u, y, hd, speed, ph, len, turn;
	float dart, dturn; /* kick: a tail flick and a darting turn, easing out */
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
	float x, y, tx, ty, hd, wph, hover_t, tap;
} fly_t;

typedef struct {
	float x, y, e, L;
} glint_t;

typedef struct {
	float x, y, z, rot, vr, size, vx, vy;
} fall_t;

typedef struct {
	float x, y, r, e;
} pulse_t;

typedef struct {
	float x, y, age, amp;
	uint32_t seed;
} splash_t;

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
	/* reactivity state */
	float clump_env[MAX_CLUMPS];
	fly_t df[MAX_DF];
	glint_t gl[MAX_GLINTS];
	int ngl;
	fall_t fall[MAX_FALL];
	int nfall;
	float rpx[NRIFF], rpy[NRIFF], riff_t;
	int frog_pad, frog_to;
	float frog_jt, frog_cool, frog_ph, frog_hd, frog_x, frog_y;
	float mid_slow, mid_cool, width_env, lowmid_env, high_env;
	/* drums: kick -> light pulse from a pad, snare -> splash (frog / fish), hats -> sun glints */
	pulse_t pl[MAX_PULSES];
	int npl;
	float kf, lvl_env;
	float sun_x, sun_y, sun_k; /* where the sun lies on the water (the caustics gather there) */
	splash_t sp[MAX_SPLASH];
	int nsp;
	unsigned hat_i, fly_i;
	us_rng lr; /* the new residents' own dice: the pond's own random stream stays as it was */
	/* a grey heron stalking the margin: bearing on the bank, where it is walking to, neck reach, strike clock */
	float hr_th, hr_home, hr_to, hr_slant, hr_neck, hr_ntgt, hr_st, hr_cool, hr_idle, hr_step, hr_fish, hr_preen, hr_look, hr_hd;
	/* a turtle cruising under the pads, coming up to breathe */
	float tt_x, tt_y, tt_hd, tt_turn, tt_depth, tt_breath, tt_stroke, tt_ph, tt_dart;
	/* the planted bank */
	float ir_th[MAX_IRIS], ir_k[MAX_IRIS], ir_ph[MAX_IRIS];
	int nir;
	float ro_x[MAX_ROS], ro_y[MAX_ROS], ro_s[MAX_ROS], ro_ph[MAX_ROS];
	int nro;
	float mo_x[MAX_MOSS], mo_y[MAX_MOSS], mo_a[MAX_MOSS];
	float dk_th[MAX_DUCK], dk_k[MAX_DUCK], dk_dx[MAX_DUCK][DUCK_N], dk_dy[MAX_DUCK][DUCK_N];
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x706f6e64u);
	us_rng_seed(&s->lr, seed ^ 0x6c696665u);
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
	p->rock = 0;
	p->rph = us_rand(r) * US_TAU;
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
	for (int i = 0; i < MAX_DF; i++) {
		fly_t *f = &st->df[i];
		pond_pt(st, us_rand(r) * US_TAU, us_randr(r, 0.3f, 0.85f), &f->x, &f->y);
		f->tx = f->x;
		f->ty = f->y;
		f->hd = us_rand(r) * US_TAU;
		f->hover_t = us_randr(r, 1.0f, 4.0f);
	}
	for (int i = 0; i < NRIFF; i++) {
		st->rpx[i] = us_rand(r) * st->A;
		st->rpy[i] = us_rand(r);
	}
	st->frog_pad = st->frog_to = 0;
	st->frog_jt = -1.0f;
	st->frog_hd = us_rand(r) * US_TAU;
	st->frog_cool = 2.0f;
	/* ---- the margin's residents and planting ---- */
	{
		us_rng *lr = &st->lr;
		float A = st->A, rx, ry;
		pond_radii(st, &rx, &ry);
		float wth = atan2f((st->wy - 0.5f) / ry, (st->wx - A * 0.5f) / rx);
		/* the heron's beat: a stretch of shallows well away from the willow and the jetty, fully in frame */
		float best = -1.0f;
		st->hr_home = 0.0f;
		for (int k = 0; k < 64; k++) {
			float th = -US_PI + US_TAU * (k + us_rand(lr) * 0.5f) / 64.0f, x, y;
			pond_pt(st, th, 0.94f, &x, &y);
			if (x < 0.14f || x > A - 0.14f || y < 0.16f || y > 0.84f)
				continue;
			float d = fminf(fabsf(remainderf(th - wth, US_TAU)), fabsf(remainderf(th - st->jth, US_TAU)) * 1.3f);
			d += 0.15f * us_rand(lr);
			if (d > best) {
				best = d;
				st->hr_home = th;
			}
		}
		st->hr_th = st->hr_to = st->hr_home;
		st->hr_slant = us_randr(lr, 0.25f, 0.55f) * (us_rand(lr) < 0.5f ? -1.0f : 1.0f);
		st->hr_neck = st->hr_ntgt = 0.45f;
		st->hr_st = -1.0f;
		st->hr_idle = us_randr(lr, 2.0f, 5.0f);
		/* the turtle */
		pond_pt(st, us_rand(lr) * US_TAU, us_randr(lr, 0.2f, 0.5f), &st->tt_x, &st->tt_y);
		st->tt_hd = us_rand(lr) * US_TAU;
		st->tt_depth = 0.7f;
		st->tt_breath = us_randr(lr, 4.0f, 12.0f);
		/* flag irises standing in the shallows along the bank */
		st->nir = 0;
		for (int tries = 0; tries < 200 && st->nir < MAX_IRIS; tries++) {
			float th = -US_PI + us_rand(lr) * US_TAU;
			if (fabsf(remainderf(th - st->jth, US_TAU)) < 0.3f || fabsf(remainderf(th - st->hr_home, US_TAU)) < 0.35f ||
			    fabsf(remainderf(th - wth, US_TAU)) < 0.45f)
				continue;
			int ok = 1;
			for (int k = 0; k < st->nir && ok; k++)
				ok = fabsf(remainderf(th - st->ir_th[k], US_TAU)) > 0.32f;
			if (!ok)
				continue;
			st->ir_th[st->nir] = th;
			st->ir_k[st->nir] = us_randr(lr, 1.02f, 1.07f);
			st->ir_ph[st->nir] = us_rand(lr) * US_TAU;
			st->nir++;
		}
		/* broad-leaved marsh plants back on the bank, clear of the ferns and the willow */
		st->nro = 0;
		for (int tries = 0; tries < 500 && st->nro < MAX_ROS; tries++) {
			float x = us_randr(lr, 0.04f, A - 0.04f), y = us_randr(lr, 0.05f, 0.95f);
			float q = pond_q(st, x, y);
			float dx = x - st->wx, dy = y - st->wy;
			if (q < 1.16f || q > 1.8f || dx * dx + dy * dy < (st->wR + 0.05f) * (st->wR + 0.05f))
				continue;
			int ok = 1;
			for (int k = 0; k < st->nfern && ok; k++) {
				float ex = x - st->fx[k], ey = y - st->fy[k];
				ok = ex * ex + ey * ey > 0.009f;
			}
			for (int k = 0; k < st->nro && ok; k++) {
				float ex = x - st->ro_x[k], ey = y - st->ro_y[k];
				ok = ex * ex + ey * ey > 0.012f;
			}
			if (!ok)
				continue;
			st->ro_x[st->nro] = x;
			st->ro_y[st->nro] = y;
			st->ro_s[st->nro] = us_randr(lr, 0.03f, 0.044f);
			st->ro_ph[st->nro] = us_rand(lr) * US_TAU;
			st->nro++;
		}
		/* clover and moss: sprigs gathered in loose patches over the bank */
		for (int i = 0; i < MAX_MOSS;) {
			float cx = us_rand(lr) * A, cy = us_rand(lr);
			if (pond_q(st, cx, cy) < 1.06f)
				continue;
			int n = 10 + (int)(us_rand(lr) * 14.0f);
			for (int k = 0; k < n && i < MAX_MOSS; k++, i++) {
				st->mo_x[i] = cx + us_randn(lr) * 0.022f;
				st->mo_y[i] = cy + us_randn(lr) * 0.018f;
				st->mo_a[i] = us_rand(lr) * US_TAU;
			}
		}
		/* duckweed rafts in the coves */
		for (int i = 0; i < MAX_DUCK; i++) {
			float th = -US_PI + us_rand(lr) * US_TAU;
			for (int tries = 0; tries < 20 && fabsf(remainderf(th - st->hr_home, US_TAU)) < 0.4f; tries++)
				th = -US_PI + us_rand(lr) * US_TAU;
			st->dk_th[i] = th;
			st->dk_k[i] = us_randr(lr, 0.84f, 0.92f);
			float tx = -sinf(th) * rx, ty = cosf(th) * ry, tl = sqrtf(tx * tx + ty * ty) + 1e-6f;
			tx /= tl, ty /= tl;
			float len = us_randr(lr, 0.035f, 0.06f);
			for (int k = 0; k < DUCK_N; k++) {
				float u = us_randn(lr) * len * 0.5f, v = us_randn(lr) * len * 0.2f;
				st->dk_dx[i][k] = tx * u - ty * v;
				st->dk_dy[i][k] = ty * u + tx * v;
			}
		}
	}
	st->inited = 1;
}

static void carve_disc(us_canvas *c, float x, float y, float R, float keep, const float *lob);
static void leaf_shape(us_canvas *c, int crisp, float x, float y, float rot, float size, float v, uint8_t mat);

/* lily pad: notched disc. crisp -> filled fan; ink -> the pad hides the light beneath it, a faint wash of
 * green, a clear rim and fine veins */
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
		carve_disc(c, x, y, R * 0.97f, 0.1f, NULL);
		for (int i = 1; i <= N; i++)
			us_tri(c, x, y, pts[2 * i], pts[2 * i + 1], pts[2 * i + 2], pts[2 * i + 3], v * 0.14f, mat);
		hpoly(c, pts, N + 3, v, mat);
		/* veins */
		for (int k = 1; k < 8; k++) {
			float a = a0 + (a1 - a0) * k / 8.0f;
			hl(c, x, y, x + cosf(a) * R * 0.86f, y + sinf(a) * R * 0.86f, v * 0.3f, mat);
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

/* kick light fronts: how much the bed light is focused at (x, y) by the travelling pulses */
static float pulse_at(const st_t *st, float x, float y)
{
	float b = 0;
	for (int k = 0; k < st->npl; k++) {
		const pulse_t *p = &st->pl[k];
		float dx = x - p->x, dy = y - p->y;
		float q = (sqrtf(dx * dx + dy * dy) - p->r) * 10.0f; /* front half-width ~0.1 */
		if (q > -1.0f && q < 1.0f) {
			float u = 1.0f - q * q;
			b += p->e * u * u;
		}
	}
	return b;
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
		float sdx = (qx - st->sun_x) * 2.6f, sdy = (qy - st->sun_y) * 3.4f;
		float lw = st->sun_k + (1.0f - st->sun_k) / (1.0f + sdx * sdx + sdy * sdy);
		if (st->npl)
			lw += 2.6f * pulse_at(st, qx, qy); /* a kick's front lights the net wherever it passes */
		float mv = v * node * fminf(m0, m1) * lw;
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
	float sw = sinf(k->ph - 4.5f) * 0.45f * (1.0f + 0.8f * k->dart);
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

	/* clover and moss carpeting the bank in loose patches: trefoils in Ink, single brighter pixels when crisp */
	{
		int nm = (int)(MAX_MOSS * (0.45f + 0.55f * dens));
		uint8_t mm = winter > 0.6f ? US_MAT_STEM : US_MAT_LEAF;
		for (int i = 0; i < nm; i++) {
			float x = st->mo_x[i], y = st->mo_y[i];
			if (x < 0 || x > A || y < 0 || y > 1)
				continue;
			float q = qgrid(st, fw, x, y);
			if (q < 1.07f || q > 1.95f)
				continue;
			float hv = 0.5f + 0.5f * sinf(st->mo_a[i] * 3.0f);
			float v = (0.17f + 0.1f * hv) * (0.6f + 0.4f * green);
			if (crisp) {
				if ((i & 1) == 0)
					us_put(c, (int)(x * c->s), (int)(y * c->s), v + 0.05f, mm);
				continue;
			}
			float rr = 0.0016f + 0.0008f * hv;
			for (int k = 0; k < 3; k++) {
				float an = st->mo_a[i] + US_TAU * k / 3.0f;
				us_point(c, x + cosf(an) * rr, y + sinf(an) * rr, 0.0011f, v, mm);
			}
		}
	}

	/* broad-leaved marsh plants (marsh marigold / butterbur) back on the bank: a rosette of round, notched
	 * leaves seen from above, each with its midrib and a couple of veins; they lift and turn in the breeze */
	{
		uint8_t rm = winter > 0.6f ? US_MAT_STEM : (autumn > 0.7f ? US_MAT_ACCENT : US_MAT_LEAF);
		int nro = (int)(st->nro * (0.5f + 0.5f * dens) + 0.5f);
		for (int i = 0; i < nro; i++) {
			float cx = st->ro_x[i], cy = st->ro_y[i], S = st->ro_s[i] * (0.65f + 0.35f * green);
			int nl = 5 + (i % 3);
			for (int k = 0; k < nl; k++) {
				float hk = sinf(k * 5.71f + i * 2.9f) * 43758.5f;
				hk -= floorf(hk);
				float sway = (0.02f + 0.1f * wind) * us_noise2(st->wind_t * 0.12f + k * 0.9f, i * 2.3f);
				float an = st->ro_ph[i] + US_TAU * (k + 0.3f * hk) / nl + sway;
				float LR = S * (0.45f + 0.25f * hk), dist = S * (0.42f + 0.2f * hk);
				float lx = cx + cosf(an) * dist, ly = cy + sinf(an) * dist;
				float dx = cosf(an), dy = sinf(an), nx = -dy, ny = dx;
				float v = (0.3f + 0.12f * hk) * (0.65f + 0.35f * green);
				enum { NL = 14 };
				float lp[2 * (NL + 1)];
				for (int m = 0; m <= NL; m++) {
					/* a round leaf with a notch where the stalk joins (the inner end) */
					float th = US_PI + US_TAU * m / NL;
					float notch = 1.0f - 0.35f * expf(-powf(remainderf(th - US_PI, US_TAU) / 0.35f, 2.0f));
					float ex = cosf(th) * LR * notch, ey = sinf(th) * LR * 0.92f * notch;
					lp[2 * m] = lx + dx * ex + nx * ey;
					lp[2 * m + 1] = ly + dy * ex + ny * ey;
				}
				if (crisp) {
					/* leaves a little apart (the dark bank between them) and alternately lit, each with a pale midrib */
					float sh = (k & 1) ? 0.75f : 1.0f;
					for (int m = 0; m < NL; m++)
						us_tri(c, lx, ly, lx + (lp[2 * m] - lx) * 0.82f, ly + (lp[2 * m + 1] - ly) * 0.82f, lx + (lp[2 * m + 2] - lx) * 0.82f,
						       ly + (lp[2 * m + 3] - ly) * 0.82f, v * 0.85f * sh, rm);
					us_line(c, lx - dx * LR * 0.5f, ly - dy * LR * 0.5f, lx + dx * LR * 0.6f, ly + dy * LR * 0.6f, 0.001f, v * 1.4f, rm);
					continue;
				}
				carve_disc(c, lx, ly, LR * 0.95f, 0.25f, NULL);
				hpoly(c, lp, NL + 1, v, rm);
				hl(c, lx - dx * LR * 0.65f, ly - dy * LR * 0.65f, lx + dx * LR * 0.8f, ly + dy * LR * 0.8f, v * 0.6f, rm);
				for (int sd = -1; sd <= 1; sd += 2) {
					hl(c, lx - dx * LR * 0.1f, ly - dy * LR * 0.1f, lx + dx * LR * 0.35f + nx * LR * 0.6f * sd,
					   ly + dy * LR * 0.35f + ny * LR * 0.6f * sd, v * 0.4f, rm);
					hl(c, lx + dx * LR * 0.3f, ly + dy * LR * 0.3f, lx + dx * LR * 0.7f + nx * LR * 0.45f * sd,
					   ly + dy * LR * 0.7f + ny * LR * 0.45f * sd, v * 0.35f, rm);
				}
			}
			/* marigold flowers at the heart in spring */
			if (spring > 0.3f && winter < 0.3f && (i % 2) == 0) {
				for (int k = 0; k < 3; k++) {
					float an = st->ro_ph[i] + k * 2.1f + 0.4f;
					float fx = cx + cosf(an) * S * 0.18f, fy = cy + sinf(an) * S * 0.18f;
					if (crisp)
						us_put(c, (int)(fx * c->s), (int)(fy * c->s), 0.85f * spring, US_MAT_LIGHT);
					else
						us_disc(c, fx, fy, 0.0022f, 0.8f * spring, US_MAT_LIGHT);
				}
			}
		}
	}

	/* flag irises standing in the shallows: fans of sword leaves (seen from above, a flat fan of blades) with,
	 * from late spring through summer, a yellow flower or two of three drooping falls; the louder the music
	 * the wider the flowers open */
	{
		float fl = us_sat(spring * 0.8f + summer) * (1.0f - winter);
		uint8_t im = winter > 0.6f ? US_MAT_STEM : (autumn > 0.6f ? US_MAT_ACCENT : US_MAT_LEAF);
		int nir = (int)(st->nir * (0.5f + 0.5f * dens) + 0.5f);
		for (int i = 0; i < nir; i++) {
			float x, y;
			pond_pt(st, st->ir_th[i], st->ir_k[i], &x, &y);
			float inward = atan2f(0.5f - y, A * 0.5f - x);
			float sway = (0.02f + 0.1f * wind) * us_noise2(st->wind_t * 0.15f + i * 1.7f, 4.4f);
			for (int f = 0; f < 3; f++) {
				float fan = st->ir_ph[i] + f * 1.15f + sway;
				float ox = x + cosf(st->ir_ph[i] + f * 2.2f) * 0.006f, oy = y + sinf(st->ir_ph[i] + f * 2.2f) * 0.006f;
				int nbl = 5 + ((i + f) % 3);
				for (int b = 0; b < nbl; b++) {
					float hb = sinf(b * 3.3f + i * 7.7f + f) * 43758.5f;
					hb -= floorf(hb);
					float a0 = inward + fan + (b / (float)(nbl - 1) - 0.5f) * 0.9f;
					float Lb = (0.018f + 0.016f * hb) * (0.7f + 0.3f * green);
					float bw = 0.0016f;
					float dx = cosf(a0), dy = sinf(a0), nx = -dy * bw, ny = dx * bw;
					float tipx = ox + dx * Lb + nx * 1.5f * sway, tipy = oy + dy * Lb + ny * 1.5f * sway;
					float v = (0.38f + 0.14f * hb) * (0.7f + 0.3f * green);
					if (crisp) {
						us_line(c, ox, oy, tipx, tipy, 0.001f, v + 0.05f, im);
						continue;
					}
					float bp[] = {ox + nx, oy + ny, ox + dx * Lb * 0.6f + nx * 1.2f, oy + dy * Lb * 0.6f + ny * 1.2f, tipx, tipy,
						      ox + dx * Lb * 0.6f - nx * 0.6f, oy + dy * Lb * 0.6f - ny * 0.6f, ox - nx, oy - ny};
					hpoly(c, bp, 5, v, im);
				}
			}
			if (fl > 0.1f && (i % 3) != 2) {
				float an = st->ir_ph[i] * 3.0f + 0.3f * sway;
				float fx = x + cosf(inward + 0.6f) * 0.012f, fy = y + sinf(inward + 0.6f) * 0.012f;
				float P = 0.0065f * fl * (0.75f + 0.45f * st->lvl_env);
				for (int k = 0; k < 3; k++) {
					float pa = an + US_TAU * k / 3.0f;
					leaf_shape(c, crisp, fx + cosf(pa) * P, fy + sinf(pa) * P, pa, P, 0.8f, US_MAT_LIGHT);
					/* the standards: three narrow upright petals between the falls */
					float sa = pa + US_PI / 3.0f;
					hl(c, fx, fy, fx + cosf(sa) * P * 0.9f, fy + sinf(sa) * P * 0.9f, 0.55f, US_MAT_LIGHT);
				}
				if (!crisp)
					us_point(c, fx, fy, 0.0012f, 0.7f, US_MAT_BLOOM);
			}
		}
	}

	/* ferns: a rosette of arching fronds seen from above. Each frond's rachis sweeps round in a gentle
	 * curve (the whole rosette swirls one way), its pinnae alternate along it, angled toward the tip, longest
	 * a third of the way out and tapering to a point; young fronds are shorter, and in spring a fiddlehead
	 * or two is still coiled at the crown */
	{
		uint8_t fm = winter > 0.6f ? US_MAT_STEM : (autumn > 0.6f ? US_MAT_ACCENT : US_MAT_LEAF);
		int nfern = (int)(st->nfern * (0.5f + 0.5f * dens) + 0.5f);
		for (int i = 0; i < nfern; i++) {
			float cx = st->fx[i], cy = st->fy[i], S = st->fs[i] * (0.75f + 0.25f * green);
			float hand = (i & 1) ? 1.0f : -1.0f;
			int nfr = 5 + (i % 3);
			for (int k = 0; k < nfr; k++) {
				float hk = sinf(k * 7.13f + i * 3.7f) * 43758.5f;
				hk -= floorf(hk);
				float a0 = st->fph[i] + US_TAU * (k + 0.55f * hk) / nfr;
				float sway = (0.06f + 0.3f * wind) * us_noise2(st->wind_t * 0.6f + k * 0.7f, i * 3.1f);
				float L = S * (0.6f + 0.75f * hk);
				float curl = hand * (0.8f + 0.7f * hk) + sway; /* total bend along the frond, radians */
				const int NS = crisp ? 6 : 11;
				/* the crown end of the stalk rises toward the eye: it starts a little way out */
				float px = cx + cosf(a0) * L * 0.1f, py = cy + sinf(a0) * L * 0.1f, ang = a0;
				float v0 = (0.42f + 0.1f * hk) * (0.7f + 0.3f * green);
				for (int s = 1; s <= NS; s++) {
					float f = (float)s / NS;
					ang = a0 + curl * f * f;
					float qx = px + cosf(ang) * L / NS, qy = py + sinf(ang) * L / NS;
					float v = v0 * (1.0f - 0.35f * f);
					if (crisp)
						us_line(c, px, py, qx, qy, 0.001f, v + 0.1f, fm);
					else
						hl(c, px, py, qx, qy, v, fm);
					/* pinnae, alternating sides, swept toward the tip */
					if (s >= 2 && (!crisp || (s & 1) == 0)) {
						float prof = sinf(US_PI * (0.1f + 0.9f * f));
						float pl = L * (0.3f * prof * (1.0f - 0.45f * f) + 0.02f);
						float tx = cosf(ang), ty = sinf(ang), nx = -ty, ny = tx;
						for (int sd = -1; sd <= 1; sd += 2) {
							float bx = 0.5f * (px + qx) + (sd < 0 ? tx : -tx) * L * 0.012f;
							float by = 0.5f * (py + qy) + (sd < 0 ? ty : -ty) * L * 0.012f;
							float ex = nx * sd * 0.82f + tx * 0.57f, ey = ny * sd * 0.82f + ty * 0.57f;
							if (crisp) {
								us_line(c, bx, by, bx + ex * pl, by + ey * pl, 0.001f, v * 0.75f, fm);
								continue;
							}
							/* a pinna bows slightly toward the tip: two segments */
							float mx = bx + ex * pl * 0.55f, my = by + ey * pl * 0.55f;
							float e2x = ex + tx * 0.35f, e2y = ey + ty * 0.35f;
							float qx2 = mx + e2x * pl * 0.45f, qy2 = my + e2y * pl * 0.45f;
							hl(c, bx, by, mx, my, v * 0.8f, fm);
							hl(c, mx, my, qx2, qy2, v * 0.6f, fm);
						}
					}
					px = qx;
					py = qy;
				}
			}
			/* fiddleheads: tight spirals at the crown in spring */
			if (!crisp && spring > 0.3f) {
				for (int q = 0; q < 2; q++) {
					float aa = st->fph[i] + q * 2.4f + 0.7f;
					float fx0 = cx + cosf(aa) * S * 0.12f, fy0 = cy + sinf(aa) * S * 0.12f;
					float pp[2 * 10];
					for (int m = 0; m < 10; m++) {
						float th = aa + hand * m * 0.9f, rr = S * 0.07f * (1.0f - m / 11.0f);
						pp[2 * m] = fx0 + cosf(th) * rr;
						pp[2 * m + 1] = fy0 + sinf(th) * rr;
					}
					hpoly(c, pp, 10, 0.45f * spring, fm);
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
				float jit = (0.25f * wind + 0.4f * st->lowmid_env) * us_noise2(st->wind_t * 1.3f + 2.0f * st->lowmid_env, q + st->so);
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

/* ---- small lives ---- */

/* a frog seen from above: a pear-shaped body, bulging eyes, hind legs folded in a Z beside it (sitting) or
 * thrown out straight behind (mid-leap). sc > 1 while airborne (closer to the eye). */
static void draw_frog(us_canvas *c, int crisp, float x, float y, float hd, float sc, float leap, float throat, float v,
		      uint8_t mat)
{
	float L = 0.019f * sc, W = 0.0135f * sc;
	float tx = cosf(hd), ty = sinf(hd), nx = -ty, ny = tx;
	enum { N = 14 };
	float pts[2 * (N + 1)];
	for (int i = 0; i <= N; i++) {
		float a = US_TAU * i / N;
		float ca = cosf(a), sa = sinf(a);
		/* wider at the haunches (behind), narrower toward the snout; the throat swells as it breathes */
		float wid = W * (1.0f - 0.25f * ca + 0.08f * throat * (ca > 0.3f ? 1.0f : 0.0f));
		float fx = ca * L, fy = sa * wid;
		pts[2 * i] = x + tx * fx + nx * fy;
		pts[2 * i + 1] = y + ty * fx + ny * fy;
	}
	if (crisp) {
		for (int i = 0; i < N; i++)
			us_tri(c, x, y, pts[2 * i], pts[2 * i + 1], pts[2 * i + 2], pts[2 * i + 3], v * 0.8f, mat);
	} else {
		/* the body hides the pad veins and the light beneath it */
		carve_disc(c, x - tx * L * 0.2f, y - ty * L * 0.2f, W * 2.0f, 0.4f, NULL);
		carve_disc(c, x - tx * L * 0.1f, y - ty * L * 0.1f, W * 1.05f, 0.08f, NULL);
		carve_disc(c, x + tx * L * 0.45f, y + ty * L * 0.45f, W * 0.7f, 0.08f, NULL);
		hpoly(c, pts, N + 1, v, mat);
		/* dorsal ridges */
		hl(c, x + tx * L * 0.5f + nx * W * 0.35f, y + ty * L * 0.5f + ny * W * 0.35f, x - tx * L * 0.6f + nx * W * 0.45f,
		   y - ty * L * 0.6f + ny * W * 0.45f, v * 0.45f, mat);
		hl(c, x + tx * L * 0.5f - nx * W * 0.35f, y + ty * L * 0.5f - ny * W * 0.35f, x - tx * L * 0.6f - nx * W * 0.45f,
		   y - ty * L * 0.6f - ny * W * 0.45f, v * 0.45f, mat);
	}
	/* eyes */
	for (int s = -1; s <= 1; s += 2)
		us_point(c, x + tx * L * 0.55f + nx * W * 0.55f * s, y + ty * L * 0.55f + ny * W * 0.55f * s, 0.0022f * sc, v * 1.2f,
			 US_MAT_LIGHT);
	/* legs */
	for (int s = -1; s <= 1; s += 2) {
		float hx = x - tx * L * 0.65f + nx * W * 0.7f * s, hy = y - ty * L * 0.65f + ny * W * 0.7f * s;
		float kx, ky, fx, fy;
		if (leap > 0.5f) { /* stretched out behind */
			kx = hx - tx * L * 1.0f + nx * W * 0.4f * s, ky = hy - ty * L * 1.0f + ny * W * 0.4f * s;
			fx = kx - tx * L * 1.1f + nx * W * 0.2f * s, fy = ky - ty * L * 1.1f + ny * W * 0.2f * s;
		} else { /* folded: knee forward and out, foot back */
			kx = hx + tx * L * 0.5f + nx * W * 0.9f * s, ky = hy + ty * L * 0.5f + ny * W * 0.9f * s;
			fx = kx - tx * L * 1.1f + nx * W * 0.4f * s, fy = ky - ty * L * 1.1f + ny * W * 0.4f * s;
		}
		us_line(c, hx, hy, kx, ky, 0.0026f * sc, v, mat);
		us_line(c, kx, ky, fx, fy, 0.002f * sc, v * 0.9f, mat);
		us_point(c, fx, fy, 0.002f * sc, v * 0.8f, mat); /* webbed foot */
		/* little arms */
		float ax = x + tx * L * 0.3f + nx * W * 0.9f * s, ay = y + ty * L * 0.3f + ny * W * 0.9f * s;
		us_line(c, ax, ay, ax + tx * L * 0.45f + nx * W * 0.5f * s, ay + ty * L * 0.45f + ny * W * 0.5f * s, 0.0018f * sc, v * 0.85f, mat);
	}
}

/* a dragonfly from above: segmented abdomen, thorax, two pairs of glassy wings */
static void draw_dragonfly(us_canvas *c, int crisp, const fly_t *f, float wv, float buzz)
{
	float tx = cosf(f->hd), ty = sinf(f->hd), nx = -ty, ny = tx;
	float x = f->x, y = f->y;
	float AL = 0.036f;
	if (crisp)
		us_line(c, x, y, x - tx * AL, y - ty * AL, 0.001f, 0.85f, US_MAT_ACCENT);
	else {
		hl(c, x, y, x - tx * AL, y - ty * AL, 0.8f, US_MAT_ACCENT);
		for (int k = 1; k < 7; k++) {
			float q = k / 7.0f * AL;
			us_point(c, x - tx * q, y - ty * q, 0.0011f, 0.55f, US_MAT_ACCENT);
		}
	}
	us_disc(c, x + tx * 0.002f, y + ty * 0.002f, 0.0028f, 0.9f, US_MAT_ACCENT);
	us_point(c, x + tx * 0.0055f, y + ty * 0.0055f, 0.002f, 1.0f, US_MAT_LIGHT);
	/* wings shiver while it hovers: a tiny sweep, faster and brighter in bright music */
	for (int p = 0; p < 2; p++) {
		float rx = x + tx * (0.001f - p * 0.0035f), ry = y + ty * (0.001f - p * 0.0035f);
		float WL = p ? 0.025f : 0.028f;
		for (int s = -1; s <= 1; s += 2) {
			float sweep = (p ? -0.22f : 0.12f) + 0.07f * buzz * sinf(f->wph + p * 1.3f);
			float wx = nx * s + tx * sweep, wy = ny * s + ty * sweep;
			float wl = sqrtf(wx * wx + wy * wy);
			wx /= wl, wy /= wl;
			float ex = rx + wx * WL, ey = ry + wy * WL;
			if (crisp) {
				us_line(c, rx, ry, ex, ey, 0.001f, wv, US_MAT_LIGHT);
				continue;
			}
			/* a narrow lens: leading and trailing edges */
			float mx = rx + wx * WL * 0.5f, my = ry + wy * WL * 0.5f;
			float ox = tx * WL * 0.13f, oy = ty * WL * 0.13f;
			float lp[] = {rx, ry, mx + ox, my + oy, ex, ey, mx - ox * 0.8f, my - oy * 0.8f, rx, ry};
			hpoly(c, lp, 5, wv, US_MAT_LIGHT);
			us_point(c, ex - wx * WL * 0.12f + ox * 0.4f, ey - wy * WL * 0.12f + oy * 0.4f, 0.0009f, wv * 1.3f, US_MAT_ACCENT);
		}
	}
}

static void leaf_shape(us_canvas *c, int crisp, float x, float y, float rot, float size, float v, uint8_t mat)
{
	float dx = cosf(rot), dy = sinf(rot);
	float L = size, W = size * 0.45f;
	float nx = -dy * W, ny = dx * W;
	if (crisp) {
		us_tri(c, x - dx * L, y - dy * L, x + nx, y + ny, x + dx * L, y + dy * L, v, mat);
		us_tri(c, x - dx * L, y - dy * L, x - nx, y - ny, x + dx * L, y + dy * L, v, mat);
		return;
	}
	float pp[] = {x - dx * L, y - dy * L, x + nx, y + ny, x + dx * L, y + dy * L, x - nx, y - ny, x - dx * L, y - dy * L};
	hpoly(c, pp, 5, v, mat);
	hl(c, x - dx * L * 1.3f, y - dy * L * 1.3f, x + dx * L * 0.8f, y + dy * L * 0.8f, v * 0.7f, mat);
}

/* a grey heron seen from above: a long teardrop of folded wings with the primaries crossed at the tail, the
 * neck folded in an S (or shot out straight in a strike), a small head with its black plume trailing back,
 * and a long yellow dagger of a bill. (hx, hy) is the head, hdir where it points */
static void draw_heron(us_canvas *c, int crisp, float x, float y, float hd, float S, float hx, float hy, float hdir,
		       float fish, float fph, float v)
{
	float tx = cosf(hd), ty = sinf(hd), nx = -ty, ny = tx;
	enum { NBD = 12 };
	float pts[2 * (2 * NBD + 1)];
	int np = 0;
	/* body outline: pointed at the tail, rounded at the breast */
	for (int side = 1; side >= -1; side -= 2)
		for (int k = 0; k < NBD; k++) {
			int kk = side > 0 ? k : NBD - 1 - k;
			float f = (kk + 0.5f) / NBD;
			float u = -0.8f + 1.3f * f;
			float hw = 0.34f * S * powf(f, 0.7f) * sqrtf(fmaxf(0.0f, 1.0f - f * f * f * f * f)) + 0.012f * S;
			pts[2 * np] = x + tx * u * S + nx * hw * side;
			pts[2 * np + 1] = y + ty * u * S + ny * hw * side;
			np++;
		}
	pts[2 * np] = pts[0], pts[2 * np + 1] = pts[1];
	np++;
	float bx = x + tx * 0.42f * S, by = y + ty * 0.42f * S; /* the neck's base */
	/* the neck: a quadratic curve from the shoulders to the head, kinked sideways when folded */
	float mx = 0.5f * (bx + hx), my = 0.5f * (by + hy);
	float ddx = hx - bx, ddy = hy - by, dl = sqrtf(ddx * ddx + ddy * ddy) + 1e-6f;
	float kink = us_sat(1.0f - dl / (1.3f * S)) * 0.28f * S;
	float cx = mx - ddy / dl * kink, cy = my + ddx / dl * kink;
	enum { NN = 8 };
	float neck[2 * (NN + 1)];
	for (int k = 0; k <= NN; k++) {
		float f = (float)k / NN, g = 1.0f - f;
		neck[2 * k] = g * g * bx + 2 * g * f * cx + f * f * hx;
		neck[2 * k + 1] = g * g * by + 2 * g * f * cy + f * f * hy;
	}
	float ex = cosf(hdir), ey = sinf(hdir), enx = -ey, eny = ex;
	float HL = 0.13f * S, HW = 0.055f * S, BL = 0.42f * S;
	if (crisp) {
		float px1 = 1.0f / c->s;
		for (int k = 0; k + 1 < np; k++)
			us_tri(c, x - tx * 0.1f * S, y - ty * 0.1f * S, pts[2 * k], pts[2 * k + 1], pts[2 * k + 2], pts[2 * k + 3], v * 0.6f,
			       US_MAT_ACCENT);
		/* a paler back between the grey wings, dark primaries crossed at the tail */
		us_line(c, x + tx * 0.35f * S, y + ty * 0.35f * S, x - tx * 0.2f * S, y - ty * 0.2f * S, 1.2f * px1, v * 0.95f, US_MAT_ACCENT);
		us_line(c, x - tx * 0.45f * S, y - ty * 0.45f * S, x - tx * 0.85f * S, y - ty * 0.85f * S, px1, 0.25f, US_MAT_EARTH);
		/* the neck, a bright white rope, and the head */
		for (int k = 0; k < NN; k++)
			us_line(c, neck[2 * k], neck[2 * k + 1], neck[2 * k + 2], neck[2 * k + 3], 1.5f * px1, v, US_MAT_ACCENT);
		us_disc(c, hx, hy, fmaxf(HW * 1.4f, 1.2f * px1), v, US_MAT_ACCENT);
		us_line(c, hx, hy, hx - ex * HL * 2.6f + enx * HW, hy - ey * HL * 2.6f + eny * HW, px1, 0.3f, US_MAT_EARTH);
		us_line(c, hx + ex * HL * 0.5f, hy + ey * HL * 0.5f, hx + ex * (HL + BL), hy + ey * (HL + BL), px1, 0.95f, US_MAT_LIGHT);
	} else {
		/* it hides the light on the bed beneath it */
		carve_disc(c, x - tx * 0.05f * S, y - ty * 0.05f * S, 0.24f * S, 0.15f, NULL);
		carve_disc(c, x - tx * 0.5f * S, y - ty * 0.5f * S, 0.12f * S, 0.2f, NULL);
		hpoly(c, pts, np, v, US_MAT_ACCENT);
		/* the folded wings: their leading edges down the back, primaries crossing over the tail */
		for (int sd = -1; sd <= 1; sd += 2) {
			float wp[] = {x + tx * 0.32f * S + nx * 0.12f * S * sd, y + ty * 0.32f * S + ny * 0.12f * S * sd,
				      x - tx * 0.15f * S + nx * 0.1f * S * sd, y - ty * 0.15f * S + ny * 0.1f * S * sd,
				      x - tx * 0.95f * S - nx * 0.05f * S * sd, y - ty * 0.95f * S - ny * 0.05f * S * sd};
			hpoly(c, wp, 3, v * 0.55f, US_MAT_ACCENT);
		}
		/* scapular feathers: a few short strokes on the back */
		for (int k = 0; k < 3; k++) {
			float u = 0.1f - 0.22f * k;
			hl(c, x + tx * u * S, y + ty * u * S, x + tx * (u - 0.16f) * S + nx * 0.04f * S, y + ty * (u - 0.16f) * S + ny * 0.04f * S,
			   v * 0.3f, US_MAT_ACCENT);
		}
		/* neck: two edges */
		for (int sd = -1; sd <= 1; sd += 2) {
			float ed[2 * (NN + 1)];
			for (int k = 0; k <= NN; k++) {
				int k0 = k > 0 ? k - 1 : 0, k1 = k < NN ? k + 1 : NN;
				float gx = neck[2 * k1] - neck[2 * k0], gy = neck[2 * k1 + 1] - neck[2 * k0 + 1], gl = sqrtf(gx * gx + gy * gy) + 1e-6f;
				float wdt = (0.05f - 0.018f * k / NN) * S;
				ed[2 * k] = neck[2 * k] - gy / gl * wdt * sd;
				ed[2 * k + 1] = neck[2 * k + 1] + gx / gl * wdt * sd;
			}
			hpoly(c, ed, NN + 1, v * 0.85f, US_MAT_ACCENT);
		}
		/* head: a small lozenge, the black plume trailing from the crown, the eye */
		float hp[] = {hx + ex * HL, hy + ey * HL, hx + enx * HW, hy + eny * HW, hx - ex * HL, hy - ey * HL, hx - enx * HW, hy - eny * HW,
			      hx + ex * HL, hy + ey * HL};
		hpoly(c, hp, 5, v, US_MAT_ACCENT);
		float plx = hx - ex * HL * 0.6f, ply = hy - ey * HL * 0.6f;
		hl(c, plx, ply, plx - ex * HL * 1.5f + enx * HW * 0.8f, ply - ey * HL * 1.5f + eny * HW * 0.8f, v * 0.5f, US_MAT_EARTH);
		hl(c, plx - ex * HL * 1.5f + enx * HW * 0.8f, ply - ey * HL * 1.5f + eny * HW * 0.8f, plx - ex * HL * 2.6f + enx * HW * 2.0f,
		   ply - ey * HL * 2.6f + eny * HW * 2.0f, v * 0.35f, US_MAT_EARTH);
		us_point(c, hx + ex * HL * 0.35f + enx * HW * 0.5f, hy + ey * HL * 0.35f + eny * HW * 0.5f, 0.0012f, v, US_MAT_LIGHT);
		/* the bill: a dagger */
		float b0x = hx + ex * HL * 0.8f, b0y = hy + ey * HL * 0.8f, b1x = hx + ex * (HL + BL), b1y = hy + ey * (HL + BL);
		hl(c, b0x + enx * HW * 0.45f, b0y + eny * HW * 0.45f, b1x, b1y, 0.9f, US_MAT_LIGHT);
		hl(c, b0x - enx * HW * 0.45f, b0y - eny * HW * 0.45f, b1x, b1y, 0.9f, US_MAT_LIGHT);
	}
	/* a fish crosswise in the bill, flapping */
	if (fish > 0.0f) {
		float fx = hx + ex * (HL + BL * 0.75f), fy = hy + ey * (HL + BL * 0.75f);
		float wig = 0.5f * sinf(fph * 18.0f);
		float ax = enx * cosf(wig) + ex * sinf(wig), ay = eny * cosf(wig) + ey * sinf(wig);
		float FL = 0.18f * S;
		us_line(c, fx - ax * FL, fy - ay * FL, fx + ax * FL, fy + ay * FL, crisp ? 0.001f : 0.0022f, 0.9f * fish, US_MAT_BLOOM);
		float t2x = fx - ax * FL, t2y = fy - ay * FL;
		us_line(c, t2x, t2y, t2x - ax * FL * 0.4f + ex * FL * 0.3f, t2y - ay * FL * 0.4f + ey * FL * 0.3f, 0.001f, 0.8f * fish, US_MAT_BLOOM);
		us_line(c, t2x, t2y, t2x - ax * FL * 0.4f - ex * FL * 0.3f, t2y - ay * FL * 0.4f - ey * FL * 0.3f, 0.001f, 0.8f * fish, US_MAT_BLOOM);
	}
}

/* a pond turtle from above: an oval carapace with its scutes, paddle flippers that sweep back on a stroke, the
 * head craned out when it breathes */
static void draw_turtle(us_canvas *c, int crisp, float x, float y, float hd, float L, float stroke, float ph, float head,
			float v)
{
	float tx = cosf(hd), ty = sinf(hd), nx = -ty, ny = tx;
	float W = L * 0.4f, H = L * 0.5f;
	/* flippers first (the shell covers their roots) */
	for (int sd = -1; sd <= 1; sd += 2) {
		float sw = sinf(ph + (sd > 0 ? 0.0f : 0.4f));
		/* front: at rest pointing forward and out; a stroke sweeps them back along the shell */
		float fa = hd + sd * (0.75f + 0.9f * stroke * (0.5f + 0.5f * sw) + 0.1f * sw);
		float fx = x + tx * H * 0.55f + nx * W * 0.75f * sd, fy = y + ty * H * 0.55f + ny * W * 0.75f * sd;
		float FL = L * 0.27f;
		leaf_shape(c, crisp, fx + cosf(fa) * FL, fy + sinf(fa) * FL, fa, FL, v * 0.9f, US_MAT_STEM);
		/* hind */
		float ra = hd + US_PI - sd * (0.7f + 0.3f * sinf(ph + 1.6f + (sd > 0 ? 0.0f : 0.4f)));
		float rx = x - tx * H * 0.6f + nx * W * 0.6f * sd, ry = y - ty * H * 0.6f + ny * W * 0.6f * sd;
		float RL = L * 0.15f;
		leaf_shape(c, crisp, rx + cosf(ra) * RL, ry + sinf(ra) * RL, ra, RL, v * 0.8f, US_MAT_STEM);
	}
	/* tail */
	us_line(c, x - tx * H * 0.95f, y - ty * H * 0.95f, x - tx * H * 1.25f, y - ty * H * 1.25f, crisp ? 0.001f : 0.0012f, v * 0.7f, US_MAT_STEM);
	/* head and neck, stretched out to breathe */
	float hl0 = H * (0.95f + 0.45f * head);
	float hxx = x + tx * hl0, hyy = y + ty * hl0;
	enum { NC = 18 };
	float cp[2 * (NC + 1)];
	for (int k = 0; k <= NC; k++) {
		float an = US_TAU * k / NC;
		float u = cosf(an) * H, w2 = sinf(an) * W * (1.0f - 0.1f * cosf(an));
		cp[2 * k] = x + tx * u + nx * w2;
		cp[2 * k + 1] = y + ty * u + ny * w2;
	}
	if (crisp) {
		us_line(c, x + tx * H * 0.8f, y + ty * H * 0.8f, hxx, hyy, 0.001f, v * 0.85f, US_MAT_STEM);
		us_disc(c, hxx, hyy, L * 0.09f, v, US_MAT_STEM);
		for (int k = 0; k < NC; k++)
			us_tri(c, x, y, cp[2 * k], cp[2 * k + 1], cp[2 * k + 2], cp[2 * k + 3], v * 0.8f, US_MAT_STEM);
		/* the keel: a paler stripe down the scutes */
		us_line(c, x + tx * H * 0.55f, y + ty * H * 0.55f, x - tx * H * 0.55f, y - ty * H * 0.55f, 0.001f, v, US_MAT_LEAF);
		return;
	}
	/* the shell hides what lies under it */
	carve_disc(c, x, y, W * 1.05f, 0.12f, NULL);
	carve_disc(c, x + tx * H * 0.45f, y + ty * H * 0.45f, W * 0.7f, 0.15f, NULL);
	carve_disc(c, x - tx * H * 0.45f, y - ty * H * 0.45f, W * 0.7f, 0.15f, NULL);
	hpoly(c, cp, NC + 1, v, US_MAT_STEM);
	/* neck and head: an oval with a yellow stripe behind each eye */
	for (int sd = -1; sd <= 1; sd += 2)
		hl(c, x + tx * H * 0.85f + nx * L * 0.05f * sd, y + ty * H * 0.85f + ny * L * 0.05f * sd, hxx - tx * L * 0.05f + nx * L * 0.045f * sd,
		   hyy - ty * L * 0.05f + ny * L * 0.045f * sd, v * 0.7f, US_MAT_STEM);
	float hp[2 * 9];
	for (int k = 0; k <= 8; k++) {
		float an = US_TAU * k / 8.0f;
		hp[2 * k] = hxx + tx * cosf(an) * L * 0.1f + nx * sinf(an) * L * 0.07f;
		hp[2 * k + 1] = hyy + ty * cosf(an) * L * 0.1f + ny * sinf(an) * L * 0.07f;
	}
	hpoly(c, hp, 9, v * 0.9f, US_MAT_STEM);
	for (int sd = -1; sd <= 1; sd += 2)
		hl(c, hxx + nx * L * 0.05f * sd, hyy + ny * L * 0.05f * sd, hxx - tx * L * 0.14f + nx * L * 0.06f * sd,
		   hyy - ty * L * 0.14f + ny * L * 0.06f * sd, v * 0.8f, US_MAT_LIGHT);
	/* scutes: an inner ring where the vertebrals and costals meet the marginals, spokes out to the rim, the
	 * keel row split in three */
	float ip[2 * (NC + 1)];
	for (int k = 0; k <= NC; k++) {
		ip[2 * k] = x + (cp[2 * k] - x) * 0.72f;
		ip[2 * k + 1] = y + (cp[2 * k + 1] - y) * 0.72f;
	}
	hpoly(c, ip, NC + 1, v * 0.55f, US_MAT_STEM);
	for (int k = 0; k < NC; k += 2)
		hl(c, ip[2 * k], ip[2 * k + 1], cp[2 * k], cp[2 * k + 1], v * 0.4f, US_MAT_STEM);
	for (int sd = -1; sd <= 1; sd += 2)
		hl(c, x + tx * H * 0.6f + nx * W * 0.25f * sd, y + ty * H * 0.6f + ny * W * 0.25f * sd, x - tx * H * 0.6f + nx * W * 0.25f * sd,
		   y - ty * H * 0.6f + ny * W * 0.25f * sd, v * 0.45f, US_MAT_STEM);
	for (int k = -1; k <= 1; k += 2) {
		float u = k * H * 0.22f;
		hl(c, x + tx * u + nx * W * 0.25f, y + ty * u + ny * W * 0.25f, x + tx * u - nx * W * 0.25f, y + ty * u - ny * W * 0.25f, v * 0.45f,
		   US_MAT_STEM);
		hl(c, x + tx * u + nx * W * 0.25f, y + ty * u + ny * W * 0.25f, x + tx * u * 1.2f + nx * W * 0.72f, y + ty * u * 1.2f + ny * W * 0.72f,
		   v * 0.35f, US_MAT_STEM);
		hl(c, x + tx * u - nx * W * 0.25f, y + ty * u - ny * W * 0.25f, x + tx * u * 1.2f - nx * W * 0.72f, y + ty * u * 1.2f - ny * W * 0.72f,
		   v * 0.35f, US_MAT_STEM);
	}
}

/* ---- snare splashes: a crown of spray flung out from the spot, the drops pattering back as a shiver
 *      of tiny dimples round it, and one sharp ring ---- */
static void add_splash(st_t *st, float x, float y, float amp)
{
	if (st->nsp >= MAX_SPLASH) {
		int o = 0;
		for (int k = 1; k < st->nsp; k++)
			if (st->sp[k].age > st->sp[o].age)
				o = k;
		st->sp[o] = st->sp[--st->nsp];
	}
	splash_t *p = &st->sp[st->nsp++];
	p->x = x;
	p->y = y;
	p->age = 0;
	p->amp = amp;
	p->seed = us_rng_u32(&st->rng);
	/* the pads round about lurch on the splash's wave */
	for (int k = 0; k < MAX_PADS; k++) {
		pad_t *q = &st->pad[k];
		float dx = q->u * st->A - x, dy = q->y - y;
		float prox = us_sat(1.25f - sqrtf(dx * dx + dy * dy) / 0.4f);
		if (prox * amp > q->jolt) {
			q->jolt = prox * amp;
			q->jdir = (k & 1) ? 1.0f : -1.0f;
		}
	}
	add_ring(st, x, y, 0.7f + 0.4f * amp, 0.24f, 1.1f, 0);
}

static inline float hash01(uint32_t h)
{
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	h ^= h >> 16;
	return (h & 0xffffff) * (1.0f / 16777216.0f);
}

#define SPLASH_T 0.55f
static void draw_splashes(us_canvas *c, st_t *st, const us_ctx *ctx, int fw, int npads)
{
	int crisp = ctx->crisp;
	float dens = ctx->p->density;
	for (int i = 0; i < st->nsp;) {
		splash_t *p = &st->sp[i];
		p->age += ctx->dt;
		if (p->age > SPLASH_T) {
			*p = st->sp[--st->nsp];
			continue;
		}
		i++;
		float t = p->age;
		/* the crown: drops flung out on short arcs, streaking along their flight, brightest at first */
		int nd = 18 + (int)(16.0f * dens);
		float fl = 0.16f; /* flight time */
		for (int d = 0; d < nd; d++) {
			float h1 = hash01(p->seed + d * 7919u), h2 = hash01(p->seed ^ (d * 104729u + 17u));
			float ang = US_TAU * (d + 0.6f * h1) / nd;
			float vel = 0.35f + 0.45f * h2;
			float tf = fminf(t, fl);
			float rr = 0.006f + vel * tf * (1.0f - 0.45f * tf / fl);
			float ca = cosf(ang), sa = sinf(ang);
			float x = p->x + ca * rr, y = p->y + sa * rr;
			if (t < fl) {
				float hgt = sinf(US_PI * t / fl); /* up toward the eye and back */
				float v = p->amp * (1.0f - 0.5f * t / fl);
				float L = 0.006f + 0.018f * hgt * (0.5f + h2);
				if (crisp)
					us_line(c, x - ca * L, y - sa * L, x, y, 0.001f, v, US_MAT_WATER);
				else {
					hl(c, x - ca * L, y - sa * L, x, y, v * 0.8f, US_MAT_WATER);
					us_point(c, x, y, 0.0011f + 0.0011f * hgt, v, US_MAT_WATER);
				}
			} else if (!crisp) {
				/* landed: a pinprick dimple */
				float v = p->amp * 0.5f * (1.0f - (t - fl) / (SPLASH_T - fl));
				if (v > 0.02f && qgrid(st, fw, x, y) < 0.97f)
					us_ring(c, x, y, 0.002f + 0.01f * (t - fl), 0.001f, v, US_MAT_WATER);
			}
		}
		/* the shiver: spray pattering down over a wider patch, dashes curving round the spot */
		float f2 = us_sat(t / 0.2f), sv = p->amp * (1.0f - f2) * (1.0f - f2);
		if (sv > 0.03f) {
			int nsh = (int)((crisp ? 50.0f : 130.0f) * (0.6f + 0.8f * dens));
			float R = 0.09f + 0.22f * sqrtf(f2);
			for (int d = 0; d < nsh; d++) {
				float h1 = hash01(p->seed + 31u * d + 5u), h2 = hash01(p->seed * 3u + 131u * d);
				float ang = US_TAU * h1, rr = R * (0.35f + 0.65f * sqrtf(h2));
				float x = p->x + cosf(ang) * rr, y = p->y + sinf(ang) * rr;
				if (qgrid(st, fw, x, y) > 0.96f || in_pad(st, npads, x, y, 1.0f))
					continue;
				float L = 0.005f + 0.007f * h2;
				float tx = -sinf(ang) * L, ty = cosf(ang) * L;
				float v = sv * (0.45f + 0.55f * h1);
				if (crisp)
					us_put(c, (int)(x * c->s), (int)(y * c->s), v, US_MAT_WATER);
				else
					hl(c, x - tx, y - ty, x + tx, y + ty, v, US_MAT_WATER);
			}
		}
		/* white water at the heart for the first instant */
		if (t < 0.12f)
			us_disc(c, p->x, p->y, 0.006f + 0.012f * p->amp * (1.0f - t / 0.12f), p->amp * (1.0f - t / 0.12f), US_MAT_WATER);
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
	us_rng *r = &st->rng, *lr = &st->lr;

	if (!st->inited)
		init_scene(st, ctx);
	st->A = A;

	/* ---- drivers ---- */
	st->ice = us_approach(st->ice, us_smooth(0.6f, 0.9f, winter), 8.0f, dt);
	st->scale = us_approach(st->scale, 0.8f + 0.2f * w->water_level, 20.0f, dt);
	/* the bass swells the light slowly (the kick has its own, sharper answer below) */
	st->bass_env = us_approach(st->bass_env, us_sat(0.8f * a->bass + 0.3f * a->sub), 0.9f, ctx->real_dt);
	st->lvl_env = us_approach(st->lvl_env, a->level, 0.25f, ctx->real_dt);
	{
		float sa = US_TAU * w->day_phase + st->so;
		st->sun_x = A * 0.5f + cosf(sa) * A * 0.17f;
		st->sun_y = 0.5f + sinf(sa) * 0.16f;
		/* away from the sun's patch the net fades to a faint web (less so as the music swells) */
		st->sun_k = 0.18f + 0.4f * st->lvl_env;
	}
	/* the light drifts a little faster as the phrase swells; slow enough that the water reads as calm */
	st->light_t += dt * (0.03f + 0.07f * st->lvl_env + 0.04f * a->swell) * (ctx->crisp ? 2.5f : 1.0f); /* whole pixels need a little more drift to read as moving */
	st->wave_t += dt;
	float rdt = ctx->real_dt;
	st->width_env = us_approach(st->width_env, a->width, 1.2f, rdt);
	st->lowmid_env = us_approach(st->lowmid_env, a->lowmid, 0.35f, rdt);
	st->high_env = us_approach(st->high_env, us_sat(0.7f * a->high + 0.5f * a->air), 0.12f, rdt);
	/* a melodic step: the mids rising sharply over their recent level */
	st->mid_slow = us_approach(st->mid_slow, a->mid, 0.7f, rdt);
	st->mid_cool -= rdt;
	int mid_hit = 0;
	if (a->mid - st->mid_slow > 0.1f && st->mid_cool <= 0) {
		mid_hit = 1;
		st->mid_cool = 0.3f;
	}
	/* the breeze: stereo width opens the air (riffles, grass, fronds), a little sizzle on top */
	float wind = us_sat(0.3f * w->wind + 0.65f * st->width_env + 0.12f * a->air);
	float ice = st->ice, open = 1.0f - ice;
	us_world_add(w, &w->moisture, 0.0005f * open, dt);

	int snare_go = a->snare_hit && ice < 0.6f, splashed = 0;
	int hat_go = a->hat_hit && ice < 0.8f;
	int npads = (int)((3 + dens * (MAX_PADS - 3)) * (1.0f - 0.85f * winter) + 0.5f);
	if (npads > MAX_PADS)
		npads = MAX_PADS;

	/* ---- kick: a lily pad dips, a ring spreads from under it and a front of focused light sweeps out
	 *      across the bed from the same spot (the whole net brightens for a moment as the water heaves) ---- */
	st->kf *= expf(-rdt / 0.08f);
	for (int i = 0; i < st->npl;) {
		pulse_t *p = &st->pl[i];
		p->r += dt * 2.4f;
		p->e *= expf(-rdt / 0.085f);
		if (p->e < 0.03f || p->r > 1.6f) {
			*p = st->pl[--st->npl];
			continue;
		}
		i++;
	}
	if (a->kick_hit) {
		float ks = us_sat(0.55f + 0.5f * a->kick);
		st->kf = fmaxf(st->kf, ks * (1.0f - 0.5f * ice));
		float x, y;
		if (npads > 0) {
			pad_t *p = &st->pad[us_rng_u32(r) % (unsigned)npads];
			x = p->u * A;
			y = p->y;
			p->bob = us_sat(p->bob + 0.8f * ks);
		} else
			random_water_point(st, npads, &x, &y);
		add_ring(st, x, y, (0.75f + 0.35f * ks) * (1.0f - 0.55f * ice), 0.16f + 0.05f * ks, 1.8f, 0);
		if (st->npl >= MAX_PULSES) { /* replace the weakest */
			int o = 0;
			for (int k = 1; k < st->npl; k++)
				if (st->pl[k].e < st->pl[o].e)
					o = k;
			st->pl[o] = st->pl[--st->npl];
		}
		st->pl[st->npl++] = (pulse_t){x, y, 0.05f, ks * (1.0f - 0.6f * ice)};
	}
	/* a tonal onset with no drum in it (a bell, a piano note): a single soft drop */
	if (a->beat && !a->kick_hit && !a->snare_hit && !a->hat_hit && a->kick < 0.2f && a->snare < 0.2f &&
	    us_rand(r) < 0.7f - 0.6f * ice) {
		float x, y;
		random_water_point(st, npads, &x, &y);
		add_ring(st, x, y, (0.35f + 0.3f * a->onset) * (1.0f - 0.55f * ice), 0.07f, 2.4f, 0);
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
					float pk = expf(-q * q * 0.5f) * fcos(q * 2.6f) * env * 0.011f / d;
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
		/* light on the water: loudness and the phrase swell brighten it, the bass focuses it; the kick
		 * throws a brief surge through the whole net */
		float gain = (0.06f + 0.1f * st->bass_env + 0.05f * w->daylight + 0.05f * a->swell + 0.36f * st->lvl_env) *
			     (1.0f - 0.65f * ice);
		float kick_k = 1.1f * st->kf * (1.0f - 0.6f * ice) * (0.6f + 0.6f * st->lvl_env); /* added on top: the surge reads the same in a quiet or a loud mix */
		if (!ctx->crisp) {
			/* Ink: fine filaments; bass focuses the light (brighter, and the net breathes) */
			caustic_lines(st, c, gw, gh, cs, lt, FW, FH, 0.06f + 1.35f * gain + kick_k);
			/* a second, finer and fainter net drifting the other way: the two interfere like real caustics */
			int gw2, gh2;
			const float cs2 = 0.083f;
			caustic_points(st, A, cs2, lt * 1.35f + 40.0f, 0x51ed270bu, &gw2, &gh2);
			/* brighter timbre -> the finer net catches more light */
			caustic_lines(st, c, gw2, gh2, cs2, lt, FW, FH, (0.06f + 1.35f * gain + kick_k) * (0.12f + 0.2f * a->centroid));
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
		caustic_lines(st, c, gw, gh, cs, lt, FW, FH, 0.24f + 1.3f * gain + 0.75f * kick_k);
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
			/* a kick startles it: a flick of the tail and a darting turn that eases out */
			if (a->kick_hit && us_rand(r) < 0.45f + 0.3f * a->kick) {
				k->dart = us_sat(k->dart + 0.6f + 0.4f * a->kick);
				k->dturn = (us_rand(r) < 0.5f ? -1.0f : 1.0f) * us_randr(r, 2.0f, 3.6f);
				if (q > 0.7f) {
					float tc = atan2f(0.5f - y, A * 0.5f - x);
					k->dturn = fabsf(k->dturn) * (remainderf(tc - k->hd, US_TAU) > 0 ? 1.0f : -1.0f);
				}
			}
			k->dart = us_approach(k->dart, 0.0f, 0.4f, rdt);
			k->hd += (k->turn + k->dturn * k->dart) * dt;
			k->ph += dt * 22.0f * k->dart;
			float spd = k->speed * (1.0f + 0.6f * st->bass_env + 2.4f * k->dart) * (1.0f - 0.6f * ice);
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
			float kv = 0.75f * (1.0f - 0.5f * ice);
			koi_draw(c, ctx->crisp, k, i, kv);
		}
	}

	/* ---- a turtle cruising below: slow paddling, a stroke of the flippers on each kick; every half minute
	 *      or so it rises to breathe (head craned out, a ring where the nostrils break the surface); a snare
	 *      sends it diving. Asleep in the mud in winter ---- */
	if (winter < 0.7f && ice < 0.5f) {
		float x = st->tt_x, y = st->tt_y;
		st->tt_breath -= dt;
		float tgt = 0.75f;
		if (st->tt_breath < 0.0f) {
			tgt = 0.0f; /* up for air */
			if (st->tt_breath < -6.0f)
				st->tt_breath = us_randr(lr, 18.0f, 34.0f) * (1.2f - 0.4f * summer);
		}
		int was_up = st->tt_depth < 0.12f;
		if (snare_go && st->tt_depth < 0.5f) {
			st->tt_dart = 1.0f;
			st->tt_breath = us_randr(lr, 10.0f, 20.0f);
			st->tt_turn = (us_rand(lr) < 0.5f ? -1.0f : 1.0f) * 2.5f;
		}
		if (a->kick_hit)
			st->tt_stroke = us_sat(st->tt_stroke + 0.5f + 0.5f * a->kick);
		st->tt_stroke = us_approach(st->tt_stroke, 0.0f, 0.35f, rdt);
		st->tt_dart = us_approach(st->tt_dart, 0.0f, 0.6f, rdt);
		if (st->tt_dart > 0.3f)
			tgt = 1.0f;
		st->tt_depth = us_approach(st->tt_depth, tgt, st->tt_dart > 0.3f ? 0.3f : 2.0f, dt);
		if (was_up != (st->tt_depth < 0.12f))
			add_ring(st, x + cosf(st->tt_hd) * 0.03f, y + sinf(st->tt_hd) * 0.03f, 0.45f + 0.3f * st->tt_dart, 0.05f, 1.8f, 0);
		/* breathing at the surface: the odd dimple at the nostrils */
		if (st->tt_depth < 0.12f && us_rand(lr) < dt * 0.5f)
			add_ring(st, x + cosf(st->tt_hd) * 0.032f, y + sinf(st->tt_hd) * 0.032f, 0.3f, 0.03f, 1.0f, 1);
		/* wander, turn back from the bank */
		float turn = us_noise2(t * 0.07f, 31.7f) * 0.5f + st->tt_turn * st->tt_dart;
		float q = pond_q(st, x + cosf(st->tt_hd) * 0.08f, y + sinf(st->tt_hd) * 0.08f);
		if (q > 0.8f) {
			float tc = atan2f(0.5f - y, A * 0.5f - x);
			turn = remainderf(tc - st->tt_hd, US_TAU) > 0 ? 0.9f : -0.9f;
		}
		st->tt_hd += turn * dt;
		float surf = st->tt_depth < 0.12f ? 0.25f : 1.0f; /* it hangs at the surface while it breathes */
		float spd = (0.008f * surf + 0.035f * st->tt_stroke + 0.07f * st->tt_dart) * (1.0f + 0.4f * st->bass_env);
		x += cosf(st->tt_hd) * spd * dt;
		y += sinf(st->tt_hd) * spd * dt;
		st->tt_x = x, st->tt_y = y;
		st->tt_ph += dt * (0.5f + 9.0f * st->tt_stroke + 10.0f * st->tt_dart);
		float d = st->tt_depth;
		float L = 0.058f * (1.0f - 0.12f * d);
		float tv = (0.78f - 0.42f * d) * (0.85f + 0.2f * st->lvl_env) + 0.3f * st->kf;
		draw_turtle(c, ctx->crisp, x, y, st->tt_hd, L, us_sat(st->tt_stroke + st->tt_dart), st->tt_ph, us_sat(1.0f - d * 3.0f), tv);
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
		draw_banks(c, st, ctx, wind, FW, npads);
		int ncl = (int)(MAX_CLUMPS * (0.35f + 0.65f * dens));
		uint8_t rm = winter > 0.6f ? US_MAT_STEM : US_MAT_LEAF;
		float green = 1.0f - 0.6f * winter;
		for (int i = 0; i < ncl; i++) {
			clump_t *q = &st->cl[i];
			float x, y;
			pond_pt(st, q->th, q->off, &x, &y);
			float inward = atan2f(0.5f - y, A * 0.5f - x);
			/* each clump listens to one band (low at one end of the bank, high at the other): its band
			 * nods it toward the water and lights the cattail */
			int bi = 2 + i * (US_NBANDS - 4) / (ncl > 1 ? ncl - 1 : 1);
			if (bi > US_NBANDS - 1)
				bi = US_NBANDS - 1;
			float be = a->bands[bi];
			st->clump_env[i] = us_approach(st->clump_env[i], be, be > st->clump_env[i] ? 0.06f : 0.45f, rdt);
			float ce = st->clump_env[i];
			float sway = us_noise2(t * (0.2f + 0.5f * wind) + q->ph, i * 1.3f) * (0.1f + 0.4f * wind) + 0.22f * ce * sinf(q->ph * 3.0f);
			int nb = 6 + (i % 5);
			for (int b = 0; b < nb; b++) {
				float hsh = sinf(i * 12.9898f + b * 78.233f) * 43758.5f;
				hsh -= floorf(hsh);
				float spread = (b / (float)(nb - 1) - 0.5f) * 2.6f;
				float Lb = q->len * (0.5f + 0.5f * hsh) * (0.7f + 0.3f * green) * (1.0f + 0.12f * ce);
				float a0 = inward + spread + sway * (0.6f + 0.4f * hsh);
				float bend = 0.3f * (spread > 0 ? 1.0f : -1.0f) * (0.3f + hsh) + sway * 0.5f;
				float mx = x + cosf(a0) * Lb * 0.5f, my = y + sinf(a0) * Lb * 0.5f;
				float tx = mx + cosf(a0 + bend) * Lb * 0.5f, ty = my + sinf(a0 + bend) * Lb * 0.5f;
				float bp[6] = {x, y, mx, my, tx, ty};
				hpoly(c, bp, 3, 0.4f + 0.15f * green + 0.1f * hsh, rm);
				if (b == nb / 2 && (i % 2) == 0) {
					float hx = x + cosf(a0) * Lb * 0.95f, hy = y + sinf(a0) * Lb * 0.95f;
					us_line(c, x + cosf(a0) * Lb * 0.7f, y + sinf(a0) * Lb * 0.7f, hx, hy, 0.0036f, 0.55f + 0.4f * ce, US_MAT_STEM);
				}
			}
		}
	}

	/* ---- ripple rings on the surface (clipped by bank and pads) ---- */
	for (int k = 0; k < st->nrg; k++) {
		ring_t *g = &st->rg[k];
		float f = g->age / g->life;
		/* a ring lands bright and sharp, then settles quickly into a faint, lingering ripple */
		float fade = powf(1.0f - f, 1.5f) * us_sat(g->age / 0.05f) * (0.16f + 0.84f * expf(-g->age / 0.12f));
		int waves = g->small ? 1 : (g->amp > 0.6f ? 3 : 2);
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

	/* ---- cat's-paws: patches of fine riffles the breeze drags across the water (stereo width) ---- */
	if (ice < 0.6f) {
		float rs = us_sat(0.08f + 0.9f * wind) * (1.0f - ice);
		float wa = 0.6f * us_noise2(t * 0.03f, st->so) + st->so;
		float wx = cosf(wa), wy = sinf(wa);
		st->riff_t += dt * (0.5f + 3.0f * wind);
		int nd = ctx->crisp ? 12 : 26;
		for (int pI = 0; pI < NRIFF; pI++) {
			st->rpx[pI] += wx * dt * (0.015f + 0.07f * wind);
			st->rpy[pI] += wy * dt * (0.015f + 0.07f * wind);
			float rx = st->rpx[pI], ry = st->rpy[pI];
			/* blown off the far side: come in again upwind */
			if (pond_q(st, rx, ry) > 1.25f && (rx - A * 0.5f) * wx + (ry - 0.5f) * wy > 0) {
				float th = atan2f(-wy, -wx) + us_randr(r, -0.8f, 0.8f);
				pond_pt(st, th, 1.15f, &st->rpx[pI], &st->rpy[pI]);
				continue;
			}
			float pr = 0.09f + 0.03f * pI;
			float pv = rs * (0.5f + 0.5f * us_noise2(t * 0.2f, pI * 5.1f + st->so));
			if (pv < 0.03f)
				continue;
			for (int d = 0; d < nd; d++) {
				float h1 = sinf(d * 12.9898f + pI * 78.233f) * 43758.5f, h2 = sinf(d * 39.346f + pI * 11.135f) * 24634.6f;
				h1 -= floorf(h1);
				h2 -= floorf(h2);
				float ox = (h1 * 2.0f - 1.0f) * pr * 1.3f, oy = (h2 * 2.0f - 1.0f) * pr * 0.7f;
				float fall = 1.0f - (ox * ox / (pr * pr * 1.69f) + oy * oy / (pr * pr * 0.49f));
				if (fall <= 0)
					continue;
				/* long axis along the wind, dashes across it */
				float dx = rx + wx * ox - wy * oy, dy = ry + wy * ox + wx * oy;
				if (qgrid(st, FW, dx, dy) > 0.95f || in_pad(st, npads, dx, dy, 1.05f))
					continue;
				float fl = 0.5f + 0.5f * sinf(st->riff_t * 2.0f + h1 * 40.0f);
				float v = pv * fall * fl * 0.55f;
				if (v < 0.02f)
					continue;
				float hlen = 0.004f + 0.006f * h2;
				hl(c, dx + wy * hlen, dy - wx * hlen, dx - wy * hlen, dy + wx * hlen, v, US_MAT_WATER);
			}
		}
	}

	/* ---- glints: every hat throws a scatter of sun sparks off the water, thickest in the sun's own
	 *      reflection (a broad patch that wanders with the time of day); the air keeps a few twinkling ---- */
	{
		float sun_x = st->sun_x, sun_y = st->sun_y;
		float rate = (0.4f + 10.0f * st->high_env * st->high_env) * (1.0f - 0.7f * ice) * (0.6f + 0.6f * w->daylight);
		float n_new = rate * dt;
		int burst = 0;
		if (hat_go) {
			burst = (int)((16.0f + 26.0f * dens) * (0.7f + 0.5f * a->hat));
			n_new += burst;
		}
		for (int guard = 0; n_new > 0 && st->ngl < MAX_GLINTS && guard < MAX_GLINTS; guard++) {
			if (n_new < 1.0f && us_rand(r) > n_new)
				break;
			n_new -= 1.0f;
			float gx = 0, gy = 0;
			int ok = 0;
			for (int tries = 0; tries < 4 && !ok; tries++) {
				gx = sun_x + us_randn(r) * 0.17f * A * 0.6f;
				gy = sun_y + us_randn(r) * 0.15f;
				ok = pond_q(st, gx, gy) < 0.93f && !in_pad(st, npads, gx, gy, 1.1f);
			}
			if (!ok)
				continue;
			glint_t *g = &st->gl[st->ngl++];
			g->x = gx;
			g->y = gy;
			g->e = burst ? us_randr(r, 0.75f, 1.0f) : us_randr(r, 0.4f, 0.8f) * (0.5f + 0.5f * st->high_env);
			g->L = us_randr(r, 0.6f, 1.0f);
		}
		for (int i = 0; i < st->ngl;) {
			glint_t *g = &st->gl[i];
			g->e *= expf(-dt / 0.07f);
			if (g->e < 0.04f) {
				*g = st->gl[--st->ngl];
				continue;
			}
			float v = us_sat(g->e * 1.2f);
			if (ctx->crisp) {
				int px = (int)(g->x * c->s), py = (int)(g->y * c->s);
				us_put(c, px, py, v, US_MAT_LIGHT);
				if (v > 0.45f) {
					us_put(c, px - 1, py, v * 0.6f, US_MAT_LIGHT);
					us_put(c, px + 1, py, v * 0.6f, US_MAT_LIGHT);
					us_put(c, px, py - 1, v * 0.45f, US_MAT_LIGHT);
					us_put(c, px, py + 1, v * 0.45f, US_MAT_LIGHT);
				}
			} else {
				/* a four-point star: long level arms, shorter upright ones, a bright core */
				float L = (0.004f + 0.014f * g->e) * g->L;
				hl(c, g->x - L, g->y, g->x + L, g->y, v, US_MAT_LIGHT);
				hl(c, g->x, g->y - L * 0.6f, g->x, g->y + L * 0.6f, v * 0.8f, US_MAT_LIGHT);
				us_point(c, g->x, g->y, 0.0012f + 0.001f * g->e, v, US_MAT_LIGHT);
			}
			i++;
		}
	}

	/* ---- duckweed rafts in the coves: a scatter of tiny round fronds that passing rings jostle ---- */
	if (ice < 0.5f) {
		float dw = us_sat(0.3f + 0.5f * w->moisture + summer + 0.5f * autumn) * (1.0f - winter);
		int nd = (int)(MAX_DUCK * (0.35f + 0.65f * dens) + 0.5f), nper = (int)(DUCK_N * us_sat(dw));
		for (int i = 0; i < nd; i++) {
			float cx, cy;
			pond_pt(st, st->dk_th[i] + 0.03f * us_noise2(t * 0.02f, i * 3.7f), st->dk_k[i], &cx, &cy);
			for (int k = 0; k < nper; k++) {
				float x = cx + st->dk_dx[i][k], y = cy + st->dk_dy[i][k], lift = 0.0f;
				/* only the strong rings (the kick's) rock the raft: the fronds ride up the crest and catch the light */
				for (int g = 0; g < st->nrg; g++) {
					ring_t *rg = &st->rg[g];
					if (rg->small || rg->amp < 0.7f)
						continue;
					float ddx = x - rg->x, ddy = y - rg->y, d = sqrtf(ddx * ddx + ddy * ddy) + 1e-5f;
					float front = fabsf(d - rg->r);
					if (front < 0.025f) {
						float e = rg->amp * (1.0f - rg->age / rg->life) * (1.0f - front / 0.025f);
						x += ddx / d * e * 0.005f, y += ddy / d * e * 0.005f;
						lift = fmaxf(lift, e);
					}
				}
				if (pond_q(st, x, y) > 0.985f)
					continue;
				float hv = 0.5f + 0.5f * sinf(k * 2.17f + i * 1.3f);
				float v = 0.4f + 0.25f * hv + 0.45f * lift + 0.35f * st->kf;
				if (ctx->crisp)
					us_put(c, (int)(x * c->s), (int)(y * c->s), v + 0.1f, US_MAT_LEAF);
				else {
					us_point(c, x, y, 0.0013f + 0.0006f * hv, v, US_MAT_LEAF);
					if (k & 1)
						us_point(c, x + 0.0018f, y + 0.0006f, 0.001f, v * 0.8f, US_MAT_LEAF);
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
			p->jolt *= expf(-rdt / 0.13f);
			p->bph += dt * (3.0f + 2.0f * p->bob);
			/* the mids rock the pads, each on its own rhythm */
			p->rock = us_approach(p->rock, a->mid * (0.6f + 0.4f * sinf(i * 2.1f)), 0.25f, rdt);
			p->rph += dt * (2.0f + 0.25f * (i % 4) + 2.0f * p->rock);
			float bobs = 1.0f + 0.05f * p->bob * sinf(p->bph) + 0.04f * p->rock * sinf(p->rph * 0.5f) + 0.1f * p->jolt;
			float R = p->R * bobs;
			float v = padv * (1.0f - 0.4f * winter) + 0.25f * p->bob + 0.1f * p->rock + 0.3f * p->jolt + 0.2f * st->lvl_env - 0.1f;
			pad_t pd = *p;
			pd.rot += 0.12f * p->rock * sinf(p->rph) + 0.3f * p->jolt * p->jdir;
			draw_pad(c, ctx->crisp, &pd, x, y, R, v, pm);
			/* lotus flower in summer / spring */
			float bloom = p->flower * us_sat(1.2f - autumn - winter * 1.5f);
			if (bloom > 0.05f) {
				/* each lotus opens to its own band of the spectrum */
				int bi = 6 + (i * 7) % (US_NBANDS - 8);
				float be = a->bands[bi];
				p->open = us_approach(p->open, be, be > p->open ? 0.08f : 0.5f, rdt);
				float fr = p->R * 0.42f * bloom * (0.7f + 0.55f * p->open);
				for (int q = 0; q < 8; q++) {
					float pa = p->rot * 0.5f + q * US_TAU / 8 + (q & 1) * (0.2f + 0.25f * p->open);
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

	/* ---- a frog on the pads: sits and breathes, and leaps to another pad on a big hit ---- */
	if (winter < 0.6f && npads >= 2) {
		if (st->frog_pad >= npads)
			st->frog_pad = 0;
		pad_t *fp = &st->pad[st->frog_pad];
		float fx = fp->u * A + cosf(fp->rot) * fp->R * 0.3f, fy = fp->y + sinf(fp->rot) * fp->R * 0.3f;
		st->frog_cool -= dt;
		st->frog_ph += dt * (2.2f + 1.5f * st->bass_env);
		if (st->frog_jt < 0) {
			st->frog_x = fx;
			st->frog_y = fy;
			/* the snare sends it off its pad with a splash; in quiet music it still hops now and then */
			int want = (snare_go && st->frog_cool <= 0) || (st->frog_cool <= -3.0f && us_rand(r) < dt * 0.12f);
			if (want) {
				/* the nearest pad that is not too near */
				int best = -1;
				float bd = 1e9f;
				for (int k = 0; k < npads; k++) {
					if (k == st->frog_pad)
						continue;
					float dx = st->pad[k].u * A - fx, dy = st->pad[k].y - fy, d2 = dx * dx + dy * dy;
					d2 += us_rand(r) * 0.004f;
					if (d2 < bd && d2 > 0.0025f) {
						bd = d2;
						best = k;
					}
				}
				if (best >= 0 && bd < 0.12f) {
					st->frog_to = best;
					st->frog_jt = 0;
					st->frog_hd = atan2f(st->pad[best].y - fy, st->pad[best].u * A - fx);
					fp->bob = us_sat(fp->bob + 0.6f);
					if (snare_go) {
						add_splash(st, fx, fy, 0.75f + 0.25f * a->snare);
						splashed = 1;
					}
				}
			}
		}
		float fsx = st->frog_x, fsy = st->frog_y, sc = 1.0f, leap = 0;
		if (st->frog_jt >= 0) {
			if (st->frog_to >= npads)
				st->frog_to = st->frog_pad;
			pad_t *tp = &st->pad[st->frog_to];
			float tx = tp->u * A + cosf(tp->rot) * tp->R * 0.3f, ty = tp->y + sinf(tp->rot) * tp->R * 0.3f;
			st->frog_jt += dt / 0.42f;
			float f = us_sat(st->frog_jt), e = f * f * (3.0f - 2.0f * f);
			fsx = us_lerp(st->frog_x, tx, e);
			fsy = us_lerp(st->frog_y, ty, e);
			float hgt = sinf(US_PI * f);
			sc = 1.0f + 0.7f * hgt;
			leap = 1.0f;
			/* its shadow on the water */
			if (!ctx->crisp && hgt > 0.05f)
				carve_disc(c, fsx + 0.012f * hgt, fsy + 0.014f * hgt, 0.009f, 0.35f, NULL);
			if (st->frog_jt >= 1.0f) {
				st->frog_pad = st->frog_to;
				st->frog_jt = -1.0f;
				st->frog_cool = us_randr(r, 0.25f, 0.9f);
				tp->bob = us_sat(tp->bob + 0.9f);
				add_ring(st, tx, ty, 0.45f, 0.06f, 1.4f, 0);
			}
		}
		float throat = 0.5f + 0.5f * sinf(st->frog_ph);
		draw_frog(c, ctx->crisp, fsx, fsy, st->frog_hd, sc, leap, throat, 0.8f, ctx->crisp ? US_MAT_EARTH : US_MAT_STEM);
	}
	/* no frog free to jump: a fish breaks the surface instead */
	if (snare_go && !splashed) {
		float x, y;
		random_water_point(st, npads, &x, &y);
		add_splash(st, x, y, 0.6f + 0.4f * a->snare);
	}
	draw_splashes(c, st, ctx, FW, npads);

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
			int stroke = s->stroke_t <= 0 || (hat_go && (unsigned)i == st->hat_i % (unsigned)ns);
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

	/* ---- a grey heron in the shallows at the margin: stands stock still, now and then pacing a slow step or
	 *      two along the bank, craning its neck to peer in, preening when it's quiet. A hard snare is its
	 *      strike: the neck shoots out, the bill stabs the water (a sharp ring and a spit of spray) and
	 *      sometimes comes up with a fish. Louder music keeps it more alert (neck up). On ice it waits on
	 *      the bank ---- */
	{
		float onice = ice > 0.5f ? 1.0f : 0.0f;
		float S = 0.082f;
		st->hr_cool -= dt;
		st->hr_idle -= dt;
		st->hr_fish -= dt;
		int striking = st->hr_st >= 0.0f;
		if (!striking && st->hr_idle <= 0.0f) {
			float u = us_rand(lr);
			if (u < 0.2f) { /* a few slow steps along the bank */
				st->hr_to = st->hr_home + us_randr(lr, -0.15f, 0.15f);
				st->hr_idle = us_randr(lr, 5.0f, 10.0f);
			} else if (u < 0.55f) { /* crane in to peer at the water */
				st->hr_ntgt = us_randr(lr, 0.7f, 0.95f);
				st->hr_idle = us_randr(lr, 2.5f, 5.0f);
			} else if (u < 0.7f && st->lvl_env < 0.35f && st->hr_fish <= 0.0f) { /* preen */
				st->hr_preen = 1.0f;
				st->hr_idle = us_randr(lr, 2.0f, 3.5f);
			} else { /* settle */
				st->hr_ntgt = 0.38f + 0.25f * st->lvl_env; /* louder: it settles less low */
				st->hr_look = us_randr(lr, -0.5f, 0.5f);
				st->hr_idle = us_randr(lr, 3.0f, 7.0f);
			}
		}
		if (st->hr_idle < 1.0f)
			st->hr_preen = 0.0f;
		/* strike: a hard snare (or, in a long silence, its own patience running out) */
		int strike = !striking && onice < 0.5f && st->hr_cool <= 0.0f &&
			     ((snare_go && a->snare > 0.5f) || us_rand(lr) < dt * 0.025f);
		if (strike) {
			st->hr_st = 0.0f;
			st->hr_cool = us_randr(lr, 3.0f, 5.0f);
			st->hr_preen = 0.0f;
			striking = 1;
		}
		/* walking: deliberate steps, a dimple where each foot goes down */
		float dth = remainderf(st->hr_to - st->hr_th, US_TAU);
		int walking = !striking && fabsf(dth) > 0.004f;
		float wx, wy;
		pond_pt(st, st->hr_th, 0.93f + 0.04f * onice, &wx, &wy);
		/* a swollen pond can push its margin out of frame: then it wades a little further in */
		wx = us_clampf(wx, 0.12f, A - 0.12f);
		wy = us_clampf(wy, 0.12f, 0.88f);
		if (walking) {
			float ph0 = st->hr_step;
			st->hr_step += dt * 0.9f;
			if (floorf(ph0 * 2.0f) != floorf(st->hr_step * 2.0f) && onice < 0.5f) {
				float sd = fmodf(floorf(st->hr_step * 2.0f), 2.0f) < 1.0f ? 1.0f : -1.0f;
				add_ring(st, wx + sd * 0.006f, wy + 0.004f, 0.35f, 0.03f, 1.0f, 1);
			}
			float pace = us_sat(sinf(US_TAU * st->hr_step) * 1.5f);
			float mv = (dth > 0 ? 1.0f : -1.0f) * fminf(fabsf(dth), dt * 0.06f * pace);
			st->hr_th += mv;
		}
		/* the body faces into the pond, angled along the bank; it turns a little toward where it walks */
		float inward = atan2f(0.5f - wy, A * 0.5f - wx);
		float hd_t = inward + st->hr_slant + (walking ? 0.45f * (dth > 0 ? 1.0f : -1.0f) * us_sat(fabsf(dth) * 10.0f) : 0.0f);
		if (st->hr_hd == 0.0f)
			st->hr_hd = hd_t;
		st->hr_hd += remainderf(hd_t - st->hr_hd, US_TAU) * us_sat(dt / 1.5f);
		float hd = st->hr_hd;
		/* neck: hunched, more alert in loud music, stretched to peer; the strike shoots it out */
		float base_n = st->hr_ntgt;
		st->hr_neck = us_approach(st->hr_neck, us_sat(base_n), 1.2f, dt);
		float reach = st->hr_neck, hdir_off = st->hr_look * (1.0f - reach) + 0.05f * us_noise2(t * 0.1f, 3.3f);
		float fish = 0.0f;
		if (striking) {
			st->hr_st += rdt;
			float s0 = st->hr_st;
			float dip = 0.0f;
			if (s0 < 0.1f) /* lunge */
				dip = s0 / 0.1f;
			else if (s0 < 0.38f) /* bill in the water */
				dip = 1.0f;
			else /* draw back */
				dip = us_sat(1.0f - (s0 - 0.38f) / 0.7f);
			int hit_now = (s0 - rdt) < 0.1f && s0 >= 0.1f;
			reach = us_lerp(reach, 1.15f, dip * dip * (3.0f - 2.0f * dip));
			hdir_off *= 1.0f - dip;
			if (hit_now) {
				float hx = wx + cosf(hd + hdir_off) * S * (0.42f + 1.0f * reach), hy = wy + sinf(hd + hdir_off) * S * (0.42f + 1.0f * reach);
				float bx = hx + cosf(hd) * S * 0.5f, by = hy + sinf(hd) * S * 0.5f;
				add_ring(st, bx, by, 0.55f, 0.08f, 1.1f, 0);
				for (int k = 0; k < 4; k++)
					add_ring(st, bx + us_randn(lr) * 0.01f, by + us_randn(lr) * 0.01f, 0.4f, 0.03f, 0.7f, 1);
				if (us_rand(lr) < 0.4f)
					st->hr_fish = 1.8f;
			}
			if (s0 > 1.1f) {
				st->hr_st = -1.0f;
				st->hr_neck = reach;
				st->hr_ntgt = 0.4f;
				st->hr_idle = us_randr(lr, 1.5f, 3.0f);
			}
		}
		if (st->hr_fish > 0.0f && !(striking && st->hr_st < 0.38f))
			fish = us_sat(st->hr_fish / 0.3f);
		/* the head: out along the body line with the reach; preening tucks it back over a shoulder */
		float hdir = hd + hdir_off;
		float hx = wx + cosf(hdir) * S * (0.42f + 1.0f * reach), hy = wy + sinf(hdir) * S * (0.42f + 1.0f * reach);
		if (st->hr_preen > 0.0f) {
			float pr = us_sat(st->hr_preen);
			float sd = st->hr_slant > 0 ? 1.0f : -1.0f;
			float px = wx - sinf(hd) * S * 0.22f * sd - cosf(hd) * S * 0.05f, py = wy + cosf(hd) * S * 0.22f * sd - sinf(hd) * S * 0.05f;
			px += 0.004f * sinf(t * 7.0f), py += 0.004f * cosf(t * 5.3f);
			hx = us_lerp(hx, px, pr), hy = us_lerp(hy, py, pr);
			hdir = hd + US_PI * 0.75f * sd * pr + 0.3f * sinf(t * 4.0f) * pr;
		}
		/* swallowing: the fish goes down headfirst */
		if (fish > 0.0f && st->hr_fish < 0.4f)
			hdir = hd + 0.6f * sinf(st->hr_fish * 20.0f) * (0.4f - st->hr_fish);
		float hv = (0.72f + 0.12f * st->lvl_env) * (0.85f + 0.15f * w->daylight);
		draw_heron(c, ctx->crisp, wx, wy, hd, S, hx, hy, hdir, fish, t, hv);
	}

	if (hat_go)
		st->hat_i++;
	draw_overhang(c, st, ctx, wind);

	/* ---- leaves falling from the tree: the low mids rustle the crown loose; they spin down and settle ---- */
	{
		float bare = us_smooth(0.5f, 0.9f, winter), crown = 1.0f - bare;
		float rate = crown * (0.04f + 0.6f * autumn) * (0.3f + 2.2f * st->lowmid_env * st->lowmid_env + wind);
		if (st->nfall < MAX_FALL && us_rand(r) < rate * dt) {
			fall_t *l = &st->fall[st->nfall++];
			float th = us_rand(r) * US_TAU, rr = st->wR * sqrtf(us_rand(r)) * 0.9f;
			l->x = st->wx + cosf(th) * rr;
			l->y = st->wy + sinf(th) * rr;
			l->z = 1.0f;
			l->rot = us_rand(r) * US_TAU;
			l->vr = us_randr(r, 2.0f, 5.0f) * (us_rand(r) < 0.5f ? -1.0f : 1.0f);
			l->size = us_randr(r, 0.008f, 0.012f);
			/* drifts out, away from the trunk, toward the water */
			float ox = A * 0.5f - st->wx, oy = 0.5f - st->wy, ol = sqrtf(ox * ox + oy * oy) + 1e-5f;
			l->vx = ox / ol * us_randr(r, 0.02f, 0.05f);
			l->vy = oy / ol * us_randr(r, 0.02f, 0.05f);
		}
		float gold = us_smooth(0.4f, 0.85f, autumn);
		uint8_t lm = gold > 0.5f ? US_MAT_LIGHT : (autumn > 0.3f ? US_MAT_ACCENT : US_MAT_LEAF);
		float wa = 0.6f * us_noise2(t * 0.03f, st->so) + st->so;
		for (int i = 0; i < st->nfall;) {
			fall_t *l = &st->fall[i];
			l->z -= dt / 3.2f;
			float flut = sinf(l->rot * 0.7f);
			l->x += (l->vx + cosf(wa) * 0.03f * wind + 0.012f * flut) * dt;
			l->y += (l->vy + sinf(wa) * 0.03f * wind) * dt;
			l->rot += l->vr * dt * (0.5f + l->z);
			if (l->z <= 0) {
				if (pond_q(st, l->x, l->y) < 0.95f && ice < 0.5f) {
					add_ring(st, l->x, l->y, 0.4f, 0.03f, 1.0f, 1);
					if (st->nlf < MAX_LEAVES) {
						leaf_t *s2 = &st->lf[st->nlf++];
						s2->u = l->x / A;
						s2->y = l->y;
						s2->rot = l->rot;
						s2->vr = us_randr(r, -0.2f, 0.2f);
						s2->size = l->size;
					}
				}
				*l = st->fall[--st->nfall];
				continue;
			}
			/* tumbling: the leaf foreshortens as it turns */
			float sz = l->size * (1.0f + 0.9f * l->z) * (0.55f + 0.45f * fabsf(cosf(l->rot * 1.3f)));
			leaf_shape(c, ctx->crisp, l->x, l->y, l->rot, sz, 0.75f, lm);
			i++;
		}
	}

	/* ---- dragonflies: hover in place on shivering wings, dart off on a melodic step, sometimes tap the water ---- */
	{
		float pop = us_sat(0.3f + 0.7f * w->insects) * (1.0f - winter) * (0.8f + 0.5f * summer);
		int nfly = (int)(pop * (1.0f + 2.0f * dens) + 0.5f);
		if (nfly > MAX_DF)
			nfly = MAX_DF;
		for (int i = 0; i < nfly; i++) {
			fly_t *f = &st->df[i];
			f->hover_t -= dt;
			int go = f->hover_t <= 0 || (mid_hit && us_rand(r) < 0.4f) || (snare_go && i == (int)(st->fly_i % (unsigned)nfly));
			if (go) {
				/* a new perch over the water, or along the reeds */
				float th = us_rand(r) * US_TAU;
				float k = us_rand(r) < 0.35f ? us_randr(r, 0.92f, 1.02f) : us_randr(r, 0.2f, 0.8f);
				float nx, ny;
				pond_pt(st, th, k, &nx, &ny);
				float dx = nx - f->x, dy = ny - f->y, d = sqrtf(dx * dx + dy * dy);
				if (d > 0.35f) { /* keep the hop short */
					nx = f->x + dx / d * 0.35f;
					ny = f->y + dy / d * 0.35f;
				}
				f->tx = nx;
				f->ty = ny;
				f->hover_t = us_randr(r, 2.5f, 7.0f);
				f->tap = us_rand(r) < 0.35f ? 1.0f : 0.0f;
			}
			float ox = f->x, oy = f->y;
			float jx = 0.004f * us_noise2(t * 1.7f, i * 9.1f), jy = 0.004f * us_noise2(i * 4.3f, t * 1.9f);
			f->x = us_approach(f->x, f->tx + jx, 0.16f, dt);
			f->y = us_approach(f->y, f->ty + jy, 0.16f, dt);
			float vx = (f->x - ox) / (dt + 1e-6f), vy = (f->y - oy) / (dt + 1e-6f);
			float sp = sqrtf(vx * vx + vy * vy);
			if (sp > 0.08f)
				f->hd += remainderf(atan2f(vy, vx) - f->hd, US_TAU) * us_sat(dt * 12.0f);
			else
				f->hd += 0.3f * dt * us_noise2(t * 0.4f, i * 2.0f);
			/* arrived: dip the abdomen to the water */
			if (f->tap > 0 && sp < 0.02f && pond_q(st, f->x, f->y) < 0.9f && ice < 0.5f) {
				add_ring(st, f->x - cosf(f->hd) * 0.026f, f->y - sinf(f->hd) * 0.026f, 0.5f, 0.03f, 1.0f, 1);
				f->tap = 0;
			}
			f->wph += dt * (40.0f + 30.0f * a->centroid);
			float wv = 0.3f + 0.35f * a->centroid + 0.15f * us_sat(sp * 3.0f);
			draw_dragonfly(c, ctx->crisp, f, wv, 0.4f + a->centroid);
		}
		if (snare_go)
			st->fly_i++;
	}

	/* ---- ice skin: frost rim, crack lines that creak further on heavy hits ---- */
	if (ice > 0.02f) {
		for (int i = 0; i < MAX_CRACKS; i++) {
			crack_t *k = &st->ck[i];
			if (a->kick_hit && us_rand(r) < 0.3f)
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
