/* Understory scene: Mycelium — the underground network.
 *
 * A soil cross-section: a thin surface line near the top with grass, a few plants whose roots reach down,
 * and mushrooms that fruit above ground. Below, white hyphae grow as wandering tips that branch, and fuse
 * when they meet another thread (anastomosis), so the colony becomes a real network. The network is a
 * fixed-capacity graph: every node knows its parent and its path distance from its colony's spore, so a
 * nutrient pulse launched at the spore reaches node i at t0 + d_i / v — pulses race outward through every
 * branch with no per-frame propagation state. When the colony gets too big the oldest threads senesce and
 * retract first (a fairy-ring: the old centre dies back, the young margin keeps exploring), and new
 * branches recolonise the gaps, so it lives forever without saturating.
 *
 * Audio: bass (sustained) drives tip growth + branching; kicks launch bright nutrient pulses through the
 *        network; highs sway the grass; onsets make mature mushrooms puff spores.
 * World: initial network size from `mycelium`; reads `moisture` (growth rate, fruiting); writes `mycelium`.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAX_NODES 10000
#define MAX_TIPS 200
#define MAX_PULSES 24
#define MAX_LINKS 600
#define MAX_MUSH 14
#define MAX_GERM 8
#define RX 8 /* coverage regions across the soil */
#define RY 4
#define MAX_SPORES 220
#define MAX_PLANTS 4
#define ROOT_SEGS 48
#define N_SPECK 320
#define GX 96
#define GY 64

#define STEP 0.0065f      /* hyphal segment length (normalized units)  */
#define PULSE_V 0.6f      /* pulse speed along the network (units / s) */
#define PULSE_W 0.035f    /* pulse half-width along the path           */
#define DIE_T 3.0f        /* seconds a senescent thread takes to fade  */
#define CROWD 9          /* nodes per hash cell before a tip gives up (keeps the web open) */

typedef struct {
	float x, y;    /* x in normalized units at aspect `ax` time of birth: stored as fraction of width */
	float d;       /* path distance from the colony spore */
	float born;    /* sim time of birth */
	float die;     /* sim time senescence started (0 = healthy) */
	int32_t parent;
	uint16_t mass; /* descendants (capped): cords get thicker */
	uint8_t alive, colony;
} node_t;

typedef struct {
	int node;
	float ang, acc, vig; /* heading, distance accumulated since last node, vigour */
} tip_t;

typedef struct {
	float t0, s;
	int colony; /* each pulse fires one colony, so different parts of the web light in turn */
} pulse_t;

typedef struct {
	int a, b;
	float t; /* creation time: a node index reused later is newer than this */
} link_t;

typedef struct {
	float x;    /* fraction of width */
	float g;    /* growth 0..1 */
	float age, life;
	float h, r; /* stipe height, cap radius */
	float lean, puff, glow;
	int alive, kind; /* kind 0 parasol, 1 inkcap bell, 2 spotted dome */
	int feed;        /* the thread that feeds it (pulses arriving here make it glow + puff) */
	float feed_born;
} mush_t;

typedef struct {
	float x, y, t; /* fraction of width, y, sim time of germination */
} germ_t;

typedef struct {
	float x, y, vx, vy, life;
} spore_t;

typedef struct {
	float x0, y0, x1, y1, w; /* root segments relative to the plant base (normalized units) */
} rseg_t;

typedef struct {
	float x; /* fraction */
	float h;
	int nroot;
	rseg_t root[ROOT_SEGS];
	float leaf_ph;
} plant_t;

typedef struct {
	us_rng rng;
	int inited;
	float t; /* sim time */

	node_t nd[MAX_NODES];
	int nalive, ndying;
	int fifo[MAX_NODES]; /* creation order ring: head = oldest */
	int fhead, fcount, fmark; /* fmark = how many from head are marked dying */
	int freelist[MAX_NODES];
	int nfree;
	int cell_head[GX * GY];
	int cell_next[MAX_NODES];
	uint8_t cell_cnt[GX * GY];

	tip_t tip[MAX_TIPS];
	int ntips;
	link_t link[MAX_LINKS];
	int nlinks, link_w;

	pulse_t pulse[MAX_PULSES];
	int npulses;
	float pulse_cool, idle_pulse_t;
	float spore_t;
	int ncolonies;

	mush_t mush[MAX_MUSH];
	spore_t spore[MAX_SPORES];
	int nspores;
	plant_t plant[MAX_PLANTS];
	int nplants;
	float speck_x[N_SPECK], speck_y[N_SPECK], speck_v[N_SPECK];
	float pebble[8][3];

	float bass_env, sway_t, glow_env, prev_kick, frame_dt;
	float empty_t[RX * RY]; /* how long each soil region has been (nearly) empty */
	float germ_cool;
	germ_t germ[MAX_GERM];
	int ngerm;
	float dmin[256], dmin_next[256]; /* per colony: path distance of its oldest living thread (pulse source) */
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x6d796365u);
	return s;
}

static void destroy(void *p) { free(p); }

static float soil_y(float xf)
{
	return 0.165f + 0.012f * us_noise2(xf * 3.1f, 2.7f) + 0.004f * us_noise2(xf * 13.0f, 0.4f);
}

/* ---- graph ------------------------------------------------------------ */

static int cell_of(float xf, float y)
{
	int cx = (int)(xf * GX), cy = (int)(y * GY);
	cx = cx < 0 ? 0 : (cx >= GX ? GX - 1 : cx);
	cy = cy < 0 ? 0 : (cy >= GY ? GY - 1 : cy);
	return cy * GX + cx;
}

static int new_node(st_t *st, int parent, float xf, float y, float d, int colony)
{
	if (st->nfree <= 0)
		return -1;
	int i = st->freelist[--st->nfree];
	node_t *n = &st->nd[i];
	n->x = xf;
	n->y = y;
	n->d = d;
	n->born = st->t;
	n->die = 0;
	n->parent = parent;
	n->mass = 0;
	n->alive = 1;
	n->colony = (uint8_t)colony;
	st->nalive++;
	/* fifo */
	st->fifo[(st->fhead + st->fcount) % MAX_NODES] = i;
	st->fcount++;
	/* spatial hash */
	int c = cell_of(xf, y);
	st->cell_next[i] = st->cell_head[c];
	st->cell_head[c] = i;
	/* cords: ancestors get heavier */
	for (int p = parent, k = 0; p >= 0 && k < 48 && st->nd[p].alive; p = st->nd[p].parent, k++) {
		if (st->nd[p].mass < 60000)
			st->nd[p].mass++;
		if (st->nd[p].parent >= 0 && st->nd[st->nd[p].parent].born > st->nd[p].born)
			break; /* parent slot was recycled */
	}
	return i;
}

static void rebuild_hash(st_t *st)
{
	memset(st->cell_cnt, 0, sizeof(st->cell_cnt));
	for (int i = 0; i < GX * GY; i++)
		st->cell_head[i] = -1;
	for (int i = 0; i < MAX_NODES; i++)
		if (st->nd[i].alive) {
			int c = cell_of(st->nd[i].x, st->nd[i].y);
			st->cell_next[i] = st->cell_head[c];
			st->cell_head[c] = i;
			if (st->cell_cnt[c] < 255)
				st->cell_cnt[c]++;
		}
}

static void add_tip(st_t *st, int node, float ang, float vig)
{
	if (st->ntips >= MAX_TIPS || node < 0)
		return;
	tip_t *t = &st->tip[st->ntips++];
	t->node = node;
	t->ang = ang;
	t->acc = 0;
	t->vig = vig;
}

static void new_colony_at(st_t *st, float xf, float y)
{
	us_rng *r = &st->rng;
	int c = st->ncolonies++ & 255;
	int n = new_node(st, -1, xf, y, 0, c);
	int k = 3 + (int)(us_rand(r) * 3);
	float a0 = us_rand(r) * US_TAU;
	for (int i = 0; i < k; i++)
		add_tip(st, n, a0 + US_TAU * i / k + us_randn(r) * 0.2f, 1.0f);
	/* the spore itself, swelling and sprouting */
	germ_t *g = &st->germ[st->ngerm < MAX_GERM ? st->ngerm++ : (int)(us_rand(r) * MAX_GERM)];
	g->x = xf;
	g->y = y;
	g->t = st->t;
}

static void new_colony(st_t *st, float A)
{
	us_rng *r = &st->rng;
	float xf = us_randr(r, 0.08f, 0.92f);
	float y = us_randr(r, soil_y(xf) + 0.12f, 0.9f);
	new_colony_at(st, xf, y);
	(void)A;
}

/* look for a foreign thread near (xf,y) to fuse with; returns node or -1 */
static int find_fuse(st_t *st, float xf, float y, float A, const node_t *self)
{
	int cx = (int)(xf * GX), cy = (int)(y * GY);
	float best = 0.0065f * 0.0065f;
	int bi = -1;
	for (int j = cy - 1; j <= cy + 1; j++) {
		if (j < 0 || j >= GY)
			continue;
		for (int i = cx - 1; i <= cx + 1; i++) {
			if (i < 0 || i >= GX)
				continue;
			for (int n = st->cell_head[j * GX + i]; n >= 0; n = st->cell_next[n]) {
				const node_t *o = &st->nd[n];
				if (!o->alive || o->die > 0)
					continue;
				/* own recent lineage has a similar path distance in the same colony */
				if (o->colony == self->colony && fabsf(o->d - self->d) < 0.06f)
					continue;
				float dx = (o->x - xf) * A, dy = o->y - y;
				float d2 = dx * dx + dy * dy;
				if (d2 < best) {
					best = d2;
					bi = n;
				}
			}
		}
	}
	return bi;
}

static void kill_tip(st_t *st, int k) { st->tip[k] = st->tip[--st->ntips]; }

/* advance all tips by dt with growth speed sp (units/s) */
static void grow(st_t *st, float A, float sp, float branch_p, float dt)
{
	us_rng *r = &st->rng;
	for (int k = 0; k < st->ntips;) {
		tip_t *t = &st->tip[k];
		node_t *cur = &st->nd[t->node];
		if (!cur->alive || cur->die > 0) {
			kill_tip(st, k);
			continue;
		}
		t->acc += sp * t->vig * dt;
		int dead = 0;
		while (t->acc >= STEP && !dead) {
			t->acc -= STEP;
			cur = &st->nd[t->node];
			/* random walk with persistence; shy of the surface and the edges; slight gravitropism */
			t->ang += us_randn(r) * 0.28f;
			float x = cur->x * A, y = cur->y;
			float sy = soil_y(cur->x);
			float ty = 0.0f;
			if (y < sy + 0.025f)
				ty = 1.0f;
			float dxv = cosf(t->ang), dyv = sinf(t->ang);
			dyv += ty * 0.6f;
			t->ang = atan2f(dyv, dxv);
			/* avoidance: hyphae turn toward unexplored soil (fewer threads ahead-left vs ahead-right) */
			{
				float la = t->ang + 0.6f, ra = t->ang - 0.6f, L = STEP * 5.0f;
				int cl = st->cell_cnt[cell_of((x + cosf(la) * L) / A, y + sinf(la) * L)];
				int cr = st->cell_cnt[cell_of((x + cosf(ra) * L) / A, y + sinf(ra) * L)];
				t->ang += us_clampf(0.05f * (float)(cr - cl), -0.25f, 0.25f);
			}
			float nx = x + cosf(t->ang) * STEP, ny = y + sinf(t->ang) * STEP;
			if (nx < 0.006f || nx > A - 0.006f || ny > 0.99f || ny < sy + 0.008f ||
			    st->cell_cnt[cell_of(nx / A, ny)] > CROWD) { /* edges + crowding: tips stop */
				dead = 1;
				break;
			}
			float nxf = nx / A;
			int fuse = find_fuse(st, nxf, ny, A, cur);
			if (fuse >= 0 && us_rand(r) < 0.7f) {
				/* anastomosis: connect and stop */
				link_t *l = &st->link[st->link_w];
				l->a = t->node;
				l->b = fuse;
				l->t = st->t;
				st->link_w = (st->link_w + 1) % MAX_LINKS;
				if (st->nlinks < MAX_LINKS)
					st->nlinks++;
				dead = 1;
				break;
			}
			int n = new_node(st, t->node, nxf, ny, cur->d + STEP, cur->colony);
			if (n < 0) {
				dead = 1;
				break;
			}
			t->node = n;
			t->vig = us_approach(t->vig, 0.55f, 40.0f, STEP / 0.01f);
			if (us_rand(r) < branch_p && st->ntips < MAX_TIPS) {
				float side = us_rand(r) < 0.5f ? -1.0f : 1.0f;
				add_tip(st, n, t->ang + side * us_randr(r, 0.5f, 1.1f), t->vig * 0.9f);
				t = &st->tip[k]; /* (array unchanged in place, but keep it explicit) */
			}
		}
		if (dead)
			kill_tip(st, k);
		else
			k++;
	}
}

/* senescence: oldest threads die first, fade, then are freed */
static void senesce(st_t *st, int target, float dt)
{
	(void)dt;
	/* mark */
	int healthy = st->nalive - st->ndying;
	while (healthy > target && st->fmark < st->fcount) {
		int i = st->fifo[(st->fhead + st->fmark) % MAX_NODES];
		st->fmark++;
		if (st->nd[i].alive && st->nd[i].die <= 0) {
			st->nd[i].die = st->t;
			st->ndying++;
			healthy--;
		}
	}
	/* free */
	while (st->fcount > 0 && st->fmark > 0) {
		int i = st->fifo[st->fhead];
		node_t *n = &st->nd[i];
		if (n->alive && n->die > 0 && st->t - n->die < DIE_T)
			break;
		if (n->alive) {
			n->alive = 0;
			st->nalive--;
			st->ndying--;
			st->freelist[st->nfree++] = i;
		}
		st->fhead = (st->fhead + 1) % MAX_NODES;
		st->fcount--;
		st->fmark--;
	}
}

/* lateral branching from living threads: recolonises gaps once the margin hits the edges */
static void lateral(st_t *st, int count, float A)
{
	us_rng *r = &st->rng;
	for (int k = 0; k < count && st->ntips < MAX_TIPS; k++) {
		for (int tries = 0; tries < 8; tries++) {
			int i = (int)(us_rand(r) * MAX_NODES);
			node_t *n = &st->nd[i];
			if (!n->alive || n->die > 0 || st->t - n->born < 4.0f || st->cell_cnt[cell_of(n->x, n->y)] > CROWD - 3)
				continue;
			(void)A;
			add_tip(st, i, us_rand(r) * US_TAU, us_randr(r, 0.5f, 0.9f));
			break;
		}
	}
}

static void make_plant(st_t *st, plant_t *p, float xf)
{
	us_rng *r = &st->rng;
	p->x = xf;
	p->h = us_randr(r, 0.05f, 0.11f);
	p->leaf_ph = us_rand(r) * 10;
	p->nroot = 0;
	/* recursive-ish root: a stack of (x, y, ang, len, depth) */
	float sx[ROOT_SEGS], sy[ROOT_SEGS], sa[ROOT_SEGS], sl[ROOT_SEGS];
	int sd[ROOT_SEGS], top = 0;
	sx[0] = 0, sy[0] = 0, sa[0] = US_PI * 0.5f, sl[0] = us_randr(r, 0.12f, 0.22f), sd[0] = 0;
	top = 1;
	while (top > 0 && p->nroot < ROOT_SEGS) {
		top--;
		float x = sx[top], y = sy[top], a = sa[top], L = sl[top];
		int dpt = sd[top];
		int n = 4 + (int)(us_rand(r) * 3);
		for (int k = 0; k < n && p->nroot < ROOT_SEGS; k++) {
			a += us_randn(r) * 0.25f;
			a = us_lerp(a, US_PI * 0.5f, 0.15f);
			float l = L / n;
			rseg_t *s = &p->root[p->nroot++];
			s->x0 = x, s->y0 = y;
			x += cosf(a) * l, y += sinf(a) * l;
			s->x1 = x, s->y1 = y;
			s->w = (dpt == 0 ? 0.003f : 0.0018f) * (1.0f - 0.12f * k);
			if (dpt < 2 && top < ROOT_SEGS - 1 && us_rand(r) < 0.45f) {
				sx[top] = x, sy[top] = y;
				sa[top] = a + (us_rand(r) < 0.5f ? -1 : 1) * us_randr(r, 0.5f, 1.1f);
				sl[top] = L * us_randr(r, 0.35f, 0.6f);
				sd[top] = dpt + 1;
				top++;
			}
		}
	}
}

static void init_scene(st_t *st, const us_ctx *ctx)
{
	us_rng *r = &st->rng;
	float A = ctx->aspect;
	st->nfree = 0;
	for (int i = MAX_NODES - 1; i >= 0; i--)
		st->freelist[st->nfree++] = i;
	for (int i = 0; i < GX * GY; i++)
		st->cell_head[i] = -1;
	for (int i = 0; i < N_SPECK; i++) {
		st->speck_x[i] = us_rand(r);
		st->speck_y[i] = us_rand(r);
		st->speck_v[i] = us_rand(r);
	}
	for (int i = 0; i < 8; i++) {
		st->pebble[i][0] = us_rand(r);
		st->pebble[i][1] = us_randr(r, 0.25f, 0.97f);
		st->pebble[i][2] = us_randr(r, 0.004f, 0.012f);
	}
	st->nplants = 3;
	for (int i = 0; i < st->nplants; i++)
		make_plant(st, &st->plant[i], (i + 0.5f) / st->nplants + us_randr(r, -0.12f, 0.12f));

	/* memory: a network that grew elsewhere (world mycelium) is already here when the scene opens */
	float m = ctx->w->mycelium;
	int cols = 2 + (int)(m * 4.0f);
	for (int i = 0; i < cols; i++)
		new_colony(st, A);
	int want = (int)(MAX_NODES * (0.04f + 0.6f * m));
	float sdt = 1.0f / 30.0f;
	for (int it = 0; it < 1500 && st->nalive < want; it++) {
		st->t += sdt;
		rebuild_hash(st);
		grow(st, A, 0.05f, 0.06f, sdt);
		if (st->ntips < 12)
			lateral(st, 4, A);
	}
	st->inited = 1;
}

/* fast thin AA line (Wu): two pixels per step, no sqrt — for thousands of hyphal hairlines in Ink */
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
	for (int i = i0; i <= i1; i++) {
		float cx = i + 0.5f;
		float cov = 1.0f;
		if (i == i0) cov = us_sat(cx + 0.5f - x0);
		if (i == i1) cov = us_sat(x1 - (cx - 0.5f)) * (i == i0 ? cov : 1.0f);
		float y = y0 + g * (us_clampf(cx, x0, x1) - x0) - 0.5f;
		int iy = (int)floorf(y);
		float f = y - iy;
		if (steep) {
			us_put(c, iy, i, v * cov * (1 - f), mat);
			us_put(c, iy + 1, i, v * cov * f, mat);
		} else {
			us_put(c, i, iy, v * cov * (1 - f), mat);
			us_put(c, i, iy + 1, v * cov * f, mat);
		}
	}
}

/* 1-px crisp line for pixel canvases that always claims the material (ink still max-blends), so a bright
 * LIGHT pulse head doesn't linger in the trails as gold threads: the thread's own STEM takes it back */
static void px_line(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
	float x0 = ax * c->s, y0 = ay * c->s, x1 = bx * c->s, y1 = by * c->s;
	float dx = x1 - x0, dy = y1 - y0;
	int n = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
	if (n < 1)
		n = 1;
	float ix = dx / n, iy = dy / n;
	int iv = (int)(us_sat(v) * 255.0f + 0.5f);
	for (int k = 0; k <= n; k++) {
		int px = (int)(x0 + ix * k), py = (int)(y0 + iy * k);
		if ((unsigned)px >= (unsigned)c->w || (unsigned)py >= (unsigned)c->h)
			continue;
		size_t i = (size_t)py * c->w + px;
		if (iv > c->ink[i])
			c->ink[i] = (uint8_t)iv;
		c->mat[i] = mat;
	}
}

/* colony of a random living thread (bigger colonies fire more often) */
static int pick_colony(st_t *st)
{
	for (int tries = 0; tries < 32; tries++) {
		const node_t *n = &st->nd[(int)(us_rand(&st->rng) * MAX_NODES)];
		if (n->alive && n->die <= 0)
			return n->colony;
	}
	return 0;
}

static float pulse_glow(const st_t *st, const node_t *n, float *head)
{
	float g = 0;
	if (head)
		*head = 0;
	for (int k = 0; k < st->npulses; k++) {
		const pulse_t *p = &st->pulse[k];
		if (p->colony != n->colony)
			continue;
		float d = n->d - st->dmin[n->colony];
		float front = (st->t - p->t0) * PULSE_V;
		float e = front - d;
		if (e < -2.5f * PULSE_W || e > 6.0f * PULSE_W)
			continue;
		/* sharp leading edge, softer tail */
		float w = e < 0 ? PULSE_W * 0.6f : PULSE_W * 1.3f;
		g += p->s * expf(-(e * e) / (w * w)) * expf(-front * 0.3f);
		if (head && e >= 0 && e < STEP + PULSE_V * st->frame_dt * 1.3f)
			*head = fmaxf(*head, p->s * expf(-front * 0.3f));
	}
	return g;
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *st = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	us_rng *r = &st->rng;
	float dt = ctx->dt, A = ctx->aspect, dens = ctx->p->density;
	if (!st->inited)
		init_scene(st, ctx);
	st->t += dt;
	st->frame_dt = dt;

	st->bass_env = us_approach(st->bass_env, us_sat(a->bass * 0.9f + a->sub * 0.4f), 0.6f, ctx->real_dt);
	float wind = us_sat(0.15f * w->wind + 0.9f * a->high + 0.4f * a->air);
	st->sway_t += dt * (0.3f + 1.4f * wind);
	float moist = w->moisture;

	/* ---- growth ---- */
	int target = (int)(MAX_NODES * (0.35f + 0.6f * dens));
	if (target > MAX_NODES - 200)
		target = MAX_NODES - 200;
	rebuild_hash(st);
	float spd = (0.006f + 0.05f * st->bass_env + 0.02f * a->level) * (0.5f + moist);
	float bp = (0.035f + 0.04f * dens) * (0.7f + 0.8f * st->bass_env);
	grow(st, A, spd, bp, dt);
	senesce(st, target, dt);
	/* keep exploring: lateral branches when tips are few, new spores now and then */
	int want_tips = 10 + (int)(dens * 30.0f + st->bass_env * 30.0f);
	if (st->ntips < want_tips && us_rand(r) < dt * (2.0f + 6.0f * st->bass_env))
		lateral(st, 1 + (st->ntips < 4), A);
	st->spore_t -= dt;
	if ((st->spore_t <= 0 && st->nalive < target * 0.8f) || st->nalive < 40) {
		new_colony(st, A);
		st->spore_t = us_randr(r, 20.0f, 45.0f);
	}
	/* coverage guarantee: soil regions that stay (nearly) empty get a spore germinating in them */
	{
		int cnt[RX * RY] = {0};
		int y0 = (int)(0.2f * GY);
		for (int cy = y0; cy < GY; cy++) {
			int ry = (cy - y0) * RY / (GY - y0);
			for (int cx = 0; cx < GX; cx++)
				cnt[ry * RX + cx * RX / GX] += st->cell_cnt[cy * GX + cx];
		}
		float expect = (float)st->nalive / (RX * RY);
		int best = -1;
		float bt = 0;
		for (int k = 0; k < RX * RY; k++) {
			if (cnt[k] < 6 + 0.12f * expect)
				st->empty_t[k] += dt;
			else
				st->empty_t[k] = 0;
			if (st->empty_t[k] > bt) {
				bt = st->empty_t[k];
				best = k;
			}
		}
		st->germ_cool -= dt * (0.6f + 1.2f * st->bass_env);
		if (best >= 0 && bt > 1.5f && st->germ_cool <= 0) {
			int rx = best % RX, ry = best / RX;
			float xf = (rx + us_randr(r, 0.2f, 0.8f)) / RX;
			float yy = 0.2f + (ry + us_randr(r, 0.2f, 0.8f)) * (0.8f / RY);
			yy = fmaxf(yy, soil_y(xf) + 0.04f);
			new_colony_at(st, xf, fminf(yy, 0.95f));
			st->empty_t[best] = 0;
			st->germ_cool = us_randr(r, 1.5f, 4.0f);
		}
	}
	float frac = (float)(st->nalive - st->ndying) / MAX_NODES;
	us_world_add(w, &w->mycelium, 0.01f * (us_sat(frac * 1.4f) - w->mycelium) + 0.002f * st->bass_env * moist, dt);

	/* ---- pulses: kicks launch nutrient waves from every spore ---- */
	st->pulse_cool -= ctx->real_dt;
	st->idle_pulse_t -= dt;
	/* kick hit = the kick envelope jumping (a->beat follows broadband onsets, not every kick) */
	int kick_hit = a->kick > st->prev_kick + 0.2f && a->kick > 0.3f;
	st->prev_kick = a->kick;
	if (kick_hit && st->pulse_cool <= 0 && st->npulses < MAX_PULSES) {
		int c0 = pick_colony(st);
		st->pulse[st->npulses++] = (pulse_t){st->t, us_sat(0.7f + 0.6f * a->kick), c0};
		/* a second colony answers (kicks light the web in two places) */
		int c1 = pick_colony(st);
		if (c1 != c0 && st->npulses < MAX_PULSES && us_rand(r) < 0.45f)
			st->pulse[st->npulses++] = (pulse_t){st->t, us_sat(0.6f + 0.5f * a->kick), c1};
		st->pulse_cool = 0.18f;
	} else if (st->idle_pulse_t <= 0 && st->npulses < MAX_PULSES) {
		/* in quiet the network still breathes: slow faint pulses */
		st->pulse[st->npulses++] = (pulse_t){st->t, 0.4f, pick_colony(st)};
		st->idle_pulse_t = us_randr(r, 3.0f, 7.0f);
	}
	if (kick_hit)
		st->idle_pulse_t = us_randr(r, 3.0f, 7.0f);
	for (int k = 0; k < st->npulses;) {
		if ((st->t - st->pulse[k].t0) * PULSE_V > 3.0f) {
			st->pulse[k] = st->pulse[--st->npulses];
			continue;
		}
		k++;
	}

	/* ---- above ground: soil line, grass, plants ---- */
	{
		float pts[2 * 97];
		for (int i = 0; i <= 96; i++) {
			float xf = i / 96.0f;
			pts[2 * i] = xf * A;
			pts[2 * i + 1] = soil_y(xf);
		}
		us_polyline(c, pts, 97, 0.0016f, 0.6f, US_MAT_EARTH);
		int nb = 90 + (int)(dens * 120);
		for (int b = 0; b < nb; b++) {
			float xf = st->speck_x[b];
			float x = xf * A, gy = soil_y(xf);
			float h = 0.008f + 0.016f * st->speck_v[b] * (0.6f + 0.6f * w->growth);
			float sw = us_noise2(x * 2.0f - st->sway_t, st->speck_y[b] * 9) * (0.1f + 0.25f * wind);
			us_line(c, x, gy, x + sw * h * 0.8f, gy - h, 0.001f, 0.3f + 0.2f * st->speck_v[b], US_MAT_LEAF);
		}
	}
	for (int i = 0; i < st->nplants; i++) {
		plant_t *p = &st->plant[i];
		float bx = p->x * A, by = soil_y(p->x);
		/* stem + a few leaves */
		float sw = us_noise2(bx * 1.3f - st->sway_t * 0.5f, i * 3.3f) * (0.02f + 0.08f * wind);
		float tx = bx + sw * p->h, ty = by - p->h;
		us_line(c, bx, by, tx, ty, 0.002f, 0.6f, US_MAT_STEM);
		for (int k = 1; k <= 3; k++) {
			float f = k / 4.0f;
			float lx = bx + (tx - bx) * f, ly = by + (ty - by) * f;
			float side = (k & 1) ? 1.0f : -1.0f;
			float la = -US_PI * 0.5f + side * (0.9f + 0.15f * sinf(st->sway_t + p->leaf_ph + k));
			float ll = p->h * 0.32f;
			us_line(c, lx, ly, lx + cosf(la) * ll, ly + sinf(la) * ll, 0.0015f, 0.5f, US_MAT_LEAF);
		}
		us_disc(c, tx, ty, ctx->crisp ? 0.004f : 0.0032f, 0.7f, US_MAT_BLOOM);
		/* roots */
		for (int k = 0; k < p->nroot; k++) {
			rseg_t *s = &p->root[k];
			us_line(c, bx + s->x0, by + s->y0, bx + s->x1, by + s->y1, s->w, 0.42f, US_MAT_EARTH);
		}
	}

	/* ---- soil texture: specks + pebbles ---- */
	for (int i = 0; i < N_SPECK; i++) {
		float xf = st->speck_x[i], sy = soil_y(xf);
		float y = sy + 0.02f + st->speck_y[i] * (0.99f - sy - 0.02f);
		us_point(c, xf * A, y, ctx->crisp ? 0.0016f : 0.0012f, 0.08f + 0.14f * st->speck_v[i], US_MAT_EARTH);
	}
	for (int i = 0; i < 8; i++) {
		float x = st->pebble[i][0] * A, y = st->pebble[i][1], rr = st->pebble[i][2];
		if (ctx->crisp)
			us_disc(c, x, y, rr, 0.16f, US_MAT_EARTH);
		else
			us_ring(c, x, y, rr, 0.0012f, 0.24f, US_MAT_EARTH);
	}

	/* ---- the network ---- */
	{
		for (int k = 0; k < 256; k++)
			st->dmin_next[k] = 1e9f;
		float t = st->t;
		for (int i = 0; i < MAX_NODES; i++) {
			node_t *n = &st->nd[i];
			if (!n->alive)
				continue;
			if (n->die <= 0 && n->d < st->dmin_next[n->colony])
				st->dmin_next[n->colony] = n->d;
			int p = n->parent;
			if (p < 0 || !st->nd[p].alive || st->nd[p].born > n->born) { /* parent died (slot maybe recycled) */
				n->parent = -1;
				continue;
			}
			node_t *q = &st->nd[p];
			float age = t - n->born;
			float youth = expf(-age * 0.25f);
			float cord = us_sat(log2f(1.0f + n->mass) / 9.0f);
			float v = 0.3f + 0.2f * cord + 0.4f * youth;
			if (n->die > 0)
				v *= us_sat(1.0f - (t - n->die) / DIE_T);
			float head;
			float g = pulse_glow(st, n, &head);
			v += 0.45f * g;
			/* pixel styles keep threads in the STEM ramp: a LIGHT mat would linger through the trails */
			uint8_t m = g > 0.5f && !ctx->crisp ? US_MAT_LIGHT : US_MAT_STEM;
			float x0 = q->x * A, y0 = q->y, x1 = n->x * A, y1 = n->y;
			if (head > 0.12f) {
				/* the nutrient packet itself: a bright bead racing along the thread (trails make it a comet) */
				float hv = us_sat(0.6f + 0.5f * head);
				if (ctx->crisp)
					px_line(c, x0, y0, x1, y1, hv, US_MAT_LIGHT);
				else
					us_line(c, x0, y0, x1, y1, 0.0016f + 0.002f * cord, hv, US_MAT_LIGHT);
				continue;
			}
			if (ctx->crisp) {
				px_line(c, x0, y0, x1, y1, v, m);
			} else {
				float vv = us_sat(v * (1.0f + 0.3f * cord));
				wu_line(c, x0, y0, x1, y1, vv, m);
				if (cord > 0.6f) { /* thick cords: a second hairline a pixel across */
					float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy) + 1e-6f;
					float o = (cord - 0.45f) * 2.2f / c->s;
					float ox = -dy / l * o, oy = dx / l * o;
					wu_line(c, x0 + ox, y0 + oy, x1 + ox, y1 + oy, vv * 0.85f, m);
				}
			}
		}
		/* fusion links */
		for (int k = 0; k < st->nlinks; k++) {
			link_t *l = &st->link[k];
			node_t *na = &st->nd[l->a], *nb = &st->nd[l->b];
			if (!na->alive || !nb->alive || na->born > l->t || nb->born > l->t)
				continue;
			float g = fmaxf(pulse_glow(st, na, NULL), pulse_glow(st, nb, NULL));
			float v = 0.3f + 0.5f * g;
			if (na->die > 0 || nb->die > 0)
				v *= 0.4f;
			if (ctx->crisp)
				px_line(c, na->x * A, na->y, nb->x * A, nb->y, us_sat(v), US_MAT_STEM);
			else
				wu_line(c, na->x * A, na->y, nb->x * A, nb->y, us_sat(v), g > 0.5f ? US_MAT_LIGHT : US_MAT_STEM);
		}
		for (int k = 0; k < 256; k++)
			st->dmin[k] = st->dmin_next[k] < 1e8f ? st->dmin_next[k] : 0.0f;
		/* growing tips */
		for (int k = 0; k < st->ntips; k++) {
			node_t *n = &st->nd[st->tip[k].node];
			us_point(c, n->x * A, n->y, ctx->crisp ? 0.002f : 0.0016f, 0.65f + 0.35f * st->bass_env, US_MAT_LIGHT);
		}
	}

	/* ---- germinating spores: a seed swells and a ring of first hyphae flicks out ---- */
	for (int k = 0; k < st->ngerm;) {
		germ_t *g = &st->germ[k];
		float age = st->t - g->t;
		if (age > 2.5f || age < 0) {
			*g = st->germ[--st->ngerm];
			continue;
		}
		float x = g->x * A, y = g->y, f = age / 2.5f;
		us_disc(c, x, y, ctx->crisp ? 0.004f : 0.0026f, 0.9f - 0.4f * f, US_MAT_ACCENT);
		if (!ctx->crisp)
			us_ring(c, x, y, 0.004f + 0.016f * f, 0.001f, 0.55f * (1.0f - f), US_MAT_ACCENT);
		k++;
	}

	/* ---- fruiting bodies: pop up above the soil where the network is rich and the world is wet/green ---- */
	{
		float autumn = us_world_season_w(w, 2), winter = us_world_winter(w);
		float lush = 0.6f * moist + 0.5f * w->growth + 0.3f * w->mycelium;
		float fruit_ok = us_smooth(0.1f, 0.3f, frac) * us_smooth(0.12f, 0.45f, lush) * (1.0f + 1.5f * autumn) *
				 (1.0f - 0.8f * winter);
		if (us_rand(r) < dt * 0.22f * fruit_ok * (0.4f + 1.2f * st->bass_env)) {
			int mi = -1;
			for (int m = 0; m < MAX_MUSH; m++)
				if (!st->mush[m].alive) {
					mi = m;
					break;
				}
			/* fruit above a thread that reaches near the surface */
			for (int tries = 0; tries < 60 && mi >= 0; tries++) {
				int ni = (int)(us_rand(r) * MAX_NODES);
				node_t *n = &st->nd[ni];
				if (!n->alive || n->die > 0 || n->y > soil_y(n->x) + 0.08f || n->x < 0.03f || n->x > 0.97f)
					continue;
				int kind = (int)(us_rand(r) * 3.0f);
				int sib = us_rand(r) < 0.5f ? 1 + (int)(us_rand(r) * 2.0f) : 0; /* clusters */
				float h0 = kind == 0 ? us_randr(r, 0.06f, 0.1f) : kind == 1 ? us_randr(r, 0.045f, 0.075f)
							       : us_randr(r, 0.04f, 0.065f);
				for (int sbi = 0; sbi <= sib && mi >= 0; sbi++) {
					mush_t *mu = &st->mush[mi];
					memset(mu, 0, sizeof(*mu));
					mu->alive = 1;
					mu->kind = kind;
					mu->x = us_clampf(n->x + (sbi ? us_randr(r, -0.025f, 0.025f) : 0.0f), 0.01f, 0.99f);
					mu->g = sbi ? -us_randr(r, 0.1f, 0.6f) : 0.0f; /* siblings come up a little later */
					mu->life = us_randr(r, 30.0f, 60.0f);
					mu->h = h0 * (sbi ? us_randr(r, 0.45f, 0.75f) : 1.0f);
					mu->r = mu->h * (kind == 0 ? us_randr(r, 0.55f, 0.75f) : kind == 1 ? us_randr(r, 0.3f, 0.38f)
										 : us_randr(r, 0.45f, 0.6f));
					mu->lean = us_randn(r) * 0.1f + (sbi ? (mu->x - n->x) * 6.0f : 0.0f);
					mu->feed = ni;
					mu->feed_born = n->born;
					mi = -1;
					for (int m = 0; m < MAX_MUSH; m++)
						if (!st->mush[m].alive) {
							mi = m;
							break;
						}
				}
				break;
			}
		}
		for (int m = 0; m < MAX_MUSH; m++) {
			mush_t *mu = &st->mush[m];
			if (!mu->alive)
				continue;
			mu->age += dt;
			float wilt = us_sat((mu->age - mu->life) / 8.0f);
			/* growth comes in pushes with the bass: mushrooms "pop" */
			mu->g = fminf(1.0f, mu->g + dt * (0.06f + 0.55f * st->bass_env) * (1.0f - wilt));
			if (wilt >= 1.0f) {
				mu->alive = 0;
				continue;
			}
			if (mu->g <= 0)
				continue;
			/* is the feeding thread still there? pulses arriving through it light the mushroom */
			node_t *fn = &st->nd[mu->feed];
			int fed = fn->alive && fn->born == mu->feed_born;
			float pg = fed ? pulse_glow(st, fn, NULL) : 0.0f;
			mu->glow = fmaxf(mu->glow * expf(-dt * 2.5f), us_sat(pg));
			float g = mu->g;
			float gm = g - 1.0f;
			float pop = 1.0f + 2.70158f * gm * gm * gm + 1.70158f * gm * gm; /* ease-out-back: overshoot */
			float bx = mu->x * A, by = soil_y(mu->x);
			float h = mu->h * pop * (1.0f - 0.45f * wilt);
			float lean = mu->lean + wilt * 0.35f;
			float tx = bx + sinf(lean) * h, ty = by - cosf(lean) * h;
			float vv = us_sat((0.72f + 0.28f * mu->glow) * (1.0f - 0.6f * wilt));
			/* rhizomorph: the cord down to the feeding thread */
			if (fed)
				us_line(c, bx, by, fn->x * A, fn->y, ctx->crisp ? 0.003f : 0.0014f, 0.45f + 0.5f * mu->glow,
					mu->glow > 0.4f ? US_MAT_LIGHT : US_MAT_STEM);
			/* stipe */
			float sw = (mu->kind == 2 ? 0.0026f : 0.0016f) * (0.6f + 0.4f * pop);
			us_line(c, bx, by, tx, ty, ctx->crisp ? 0.0045f : sw, vv * 0.85f, US_MAT_STEM);
			if (mu->kind == 2)
				us_disc(c, bx, by - 0.003f, ctx->crisp ? 0.0045f : 0.0032f, vv * 0.8f, US_MAT_STEM); /* volva */
			float cr = mu->r * us_sat(pop * 1.3f - 0.15f);
			if (cr < 0.001f)
				continue;
			float open = us_sat((g - 0.35f) * 1.6f) * (1.0f - 0.3f * wilt);
			/* cap outline: half-width cw, height ch, profile per kind */
			float cw, ch, flat;
			if (mu->kind == 0) { /* parasol: egg -> wide flat umbrella with a central bump */
				cw = cr * (0.45f + 0.55f * open);
				ch = cr * (0.95f - 0.62f * open);
				flat = open;
			} else if (mu->kind == 1) { /* inkcap: tall bell */
				cw = cr * (0.85f + 0.25f * open);
				ch = cr * (2.4f - 0.5f * open);
				flat = 0.0f;
			} else { /* spotted dome */
				cw = cr * (0.7f + 0.3f * open);
				ch = cr * (0.95f - 0.3f * open);
				flat = 0.2f * open;
			}
			uint8_t capm = mu->kind == 1 ? US_MAT_ACCENT : US_MAT_BLOOM;
			float pts[2 * 15];
			int np = 15;
			float ca = cosf(lean), sa = sinf(lean);
			for (int k = 0; k < np; k++) {
				float u = -1.0f + 2.0f * k / (np - 1);
				float dome = sqrtf(fmaxf(0.0f, 1.0f - u * u));
				float prof = us_lerp(dome, 0.35f * (1.0f - u * u) + 0.6f * expf(-u * u * 30.0f), flat);
				if (mu->kind == 1)
					prof = powf(fmaxf(0.0f, 1.0f - u * u), 0.45f);
				float lx = u * cw, ly = -ch * prof;
				/* rotate with the lean */
				pts[2 * k] = tx + lx * ca - ly * sa;
				pts[2 * k + 1] = ty + lx * sa + ly * ca;
			}
			if (ctx->crisp) {
				for (int k = 0; k + 1 < np; k++)
					us_tri(c, tx, ty, pts[2 * k], pts[2 * k + 1], pts[2 * k + 2], pts[2 * k + 3], vv, capm);
			} else {
				us_polyline(c, pts, np, 0.0015f, vv, capm);
				us_line(c, pts[0], pts[1], pts[2 * np - 2], pts[2 * np - 1], 0.001f, vv * 0.7f, capm);
				/* gills: fine radiating lines under the cap, lit by arriving pulses */
				float gv = vv * (0.35f + 0.6f * mu->glow);
				for (int k = -4; k <= 4; k++) {
					float u = k / 5.0f;
					float gx0 = tx + u * cw * ca, gy0 = ty + u * cw * sa;
					us_line(c, gx0, gy0, tx + u * cw * 0.3f * ca - 0.0035f * sa * (mu->kind == 1 ? 0 : 1),
						ty + u * cw * 0.3f * sa + cr * 0.15f * ca, 0.0007f, gv, capm);
				}
			}
			if (mu->kind == 2) { /* spots */
				static const float SP[6][2] = {{-0.55f, 0.35f}, {-0.15f, 0.7f}, {0.3f, 0.5f}, {0.62f, 0.25f}, {0.05f, 0.3f}, {-0.35f, 0.15f}};
				for (int k = 0; k < 6; k++) {
					float u = SP[k][0], dome = sqrtf(fmaxf(0.0f, 1.0f - u * u));
					float lx = u * cw, ly = -ch * dome * SP[k][1] - ch * 0.12f;
					float sx = tx + lx * ca - ly * sa, sy = ty + lx * sa + ly * ca;
					if (ctx->crisp)
						us_put(c, (int)(sx * c->s), (int)(sy * c->s), vv, US_MAT_LIGHT);
					else
						us_disc(c, sx, sy, 0.0011f + 0.0006f * open, vv, US_MAT_LIGHT);
				}
			} else if (mu->kind == 0 && !ctx->crisp && open > 0.3f) { /* ring on the stipe */
				float rx = bx + (tx - bx) * 0.72f, ry = by + (ty - by) * 0.72f;
				us_line(c, rx - 0.004f, ry + 0.002f, rx + 0.004f, ry + 0.002f, 0.0009f, vv * 0.7f, US_MAT_STEM);
			}
			/* spore puffs: when a pulse arrives through the cord, or on hits from mature caps */
			mu->puff -= dt;
			int arrive = mu->glow > 0.45f;
			if ((arrive || a->beat) && open > 0.7f && wilt <= 0 && mu->puff <= 0 &&
			    (arrive || us_rand(r) < 0.3f + 0.5f * a->onset)) {
				mu->puff = us_randr(r, 0.8f, 2.0f);
				int ns = 2 + (int)(4.0f * mu->glow);
				for (int k = 0; k < ns && st->nspores < MAX_SPORES; k++) {
					spore_t *sp2 = &st->spore[st->nspores++];
					sp2->x = tx + us_randr(r, -cw, cw);
					sp2->y = ty + 0.002f;
					sp2->vx = us_randr(r, -0.008f, 0.008f) + 0.003f * wind;
					sp2->vy = us_randr(r, -0.012f, -0.002f);
					sp2->life = 1.0f;
				}
			}
		}
		for (int k = 0; k < st->nspores;) {
			spore_t *s = &st->spore[k];
			s->vx += us_noise2(s->x * 9.0f, s->y * 9.0f + st->sway_t) * 0.006f * dt + 0.002f * wind * dt;
			s->vy -= 0.001f * dt;
			s->vx *= expf(-dt * 0.5f);
			s->x += s->vx * dt;
			s->y += s->vy * dt;
			s->life -= dt * 0.22f;
			if (s->life <= 0 || s->y < -0.02f || s->x < -0.02f || s->x > A + 0.02f) {
				*s = st->spore[--st->nspores];
				continue;
			}
			us_point(c, s->x, s->y, ctx->crisp ? 0.0016f : 0.0011f, 0.45f * s->life, US_MAT_ACCENT);
			k++;
		}
	}
}

const us_scene_vt us_scene_mycelium = {"mycelium", "Mycelium", 0.75f, create, destroy, frame};
