/* Understory scene: Mycelium — the underground network.
 *
 * A soil cross-section, layered like a cut bank: above, a strip of the living surface (two rows of grass,
 * flowering plants, leaf litter, the trunk of an old tree whose buttress roots plunge through the frame,
 * and mushrooms that fruit where the network is rich); below, the soil horizons — a dark humus of leaf
 * fragments, the topsoil where earthworms tunnel, a stonier subsoil, and fractured bedrock at the bottom.
 * Hyphae wrap around the stones. Below, white hyphae grow as wandering tips that branch, and fuse
 * when they meet another thread (anastomosis), so the colony becomes a real network. The network is a
 * fixed-capacity graph: every node knows its parent and its path distance from its colony's spore, so a
 * nutrient pulse launched at the spore reaches node i at t0 + d_i / v — pulses race outward through every
 * branch with no per-frame propagation state. When the colony gets too big the oldest threads senesce and
 * retract first (a fairy-ring: the old centre dies back, the young margin keeps exploring), and new
 * branches recolonise the gaps, so it lives forever without saturating.
 *
 * Small lives reward a long look: a mole in its tunnel with a grass-lined nest, springtails in the humus,
 * a ground beetle on the surface, root hairs on the fine roots, bioluminescent hyphal tips.
 *
 * Audio: bass (sustained) drives tip growth + branching; kicks launch bright nutrient pulses through the
 *        network, make the mushroom caps squash and spring back, and shake crumbs from the tunnel roof;
 *        sub-bass sets the mole digging; low-mids walk the beetle; hats make the springtails leap;
 *        air lets a spore haze drift from open gills and stirs the root hairs; highs sway the grass;
 *        onsets puff spores; the melody bands each warm their own region of the network, at a depth
 *        chosen by the spectral centroid (bright = shallow); every mushroom listens to its own band;
 *        the phrase swell gathers foxfire halos round the tips; stereo width spreads the spore cloud.
 *        In silence the tips breathe, faint pulses still travel, and the animals amble.
 * World: initial network size from `mycelium`; reads `moisture` (growth rate, fruiting); writes `mycelium`.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>
#ifdef US_PROF /* section timing for development: build with -DUS_PROF */
#include <stdio.h>
#include <time.h>
static double prof_acc[12], prof_t;
static long prof_n;
static double prof_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}
#define PROF(k) do { double _n = prof_now(); prof_acc[k] += _n - prof_t; prof_t = _n; } while (0)
#define PROF_START prof_t = prof_now()
#define PROF_END do { if (++prof_n % 300 == 0) { fprintf(stderr, "myc prof:"); for (int _k = 0; _k < 12; _k++) fprintf(stderr, " %.3f", prof_acc[_k] / prof_n); fprintf(stderr, "\n"); } } while (0)
#else
#define PROF(k)
#define PROF_START
#define PROF_END
#endif

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
#define N_SPECK 700
#define TREE_SEGS 220
#define MAX_STONE 14
#define STONE_V 10
#define N_FRAG 110 /* humus: leaf fragments */
#define N_LITTER 46 /* fallen leaves on the surface */
#define N_STRATA 40 /* bedrock bedding lines */
#define N_STAR 50
#define MAX_WORM 2
#define WORM_N 18
#define GX 96
#define GY 64
#define N_SPRING 12 /* springtails in the humus */
#define TUN_N 22    /* mole tunnel: points evenly spaced in x */
#define TUN_R 0.0105f
#define MAX_CRUMB 28
#define NCOL 12     /* melody regions across the network, one band each */

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
	int emit;        /* spores still to release from this puff (they leave one by one, not as a line) */
	float emit_t;
	float benv;      /* its own spectrum band: every cap listens to a different voice */
} mush_t;

typedef struct {
	float x, y, t; /* fraction of width, y, sim time of germination */
} germ_t;

typedef struct {
	float x, y, vx, vy, life, ph;
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
	float x, y, rx, ry; /* x fraction of width; radii in units */
	float rv[STONE_V];  /* outline radius jitter */
} stone_t;

typedef struct {
	float x[WORM_N], y[WORM_N]; /* units (x fraction of width * aspect at time of drawing is not needed: worms live in xf) */
	float ang, ph, speed;
	float tr[40][2]; /* the burrow it leaves behind (ring buffer) */
	int ntr, tr_w;
	float tr_acc;
} worm_t;

typedef struct {
	float xf, dfr;      /* fraction of width; depth fraction inside the humus */
	float hy, vy, vx;   /* hop: height above its floor (units, <=0), vertical/horizontal speed */
	float face, cool, ph;
} springtail_t;

typedef struct {
	float xf, dir, ph, pause, legph;
} beetle_t;

typedef struct {
	float xf, y, vy, life;
} crumb_t;

typedef struct {
	us_rng rng, rng2; /* rng2: the small lives, so they never disturb the layout */
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
	float blade_x[260], blade_v[260];
	float frag[N_FRAG][4];    /* xf, y, angle, size */
	float litter[N_LITTER][4]; /* xf, angle, size, hue (0..1: >0.5 turns red in autumn) */
	float strata[N_STRATA][3]; /* xf, y, length */
	float star[N_STAR][3];
	stone_t stone[MAX_STONE];
	int nstones;
	float tree_x; /* fraction of width (0 = no tree) */
	int ntree;
	rseg_t tree[TREE_SEGS];
	worm_t worm[MAX_WORM];
	springtail_t spring[N_SPRING];
	beetle_t beetle;
	float tun_x0, tun_x1, tun_y[TUN_N], tun_ymin, tun_ymax; /* tunnel: xf span, centre line */
	float nest_xf, nest_y;                                  /* nest chamber at the tunnel's far end */
	float mole_u, mole_dir, mole_paw;                       /* position along the tunnel 0..1 */
	crumb_t crumb[MAX_CRUMB];
	int ncrumb;
	/* audio envelopes (each part of the music gets its own creature / response) */
	float sub_env, lm_env, air_env, swell_g, cent_env, width_env;
	float hi_fast, hi_slow, hi_cool;
	float bob_x, bob_v; /* kick: damped wobble of the mushroom caps */
	float colenv[NCOL]; /* melody: per-band glow of network regions */
	float depth_w[GY];

	float bass_env, sway_t, glow_env, prev_kick, frame_dt;
	float empty_t[RX * RY]; /* how long each soil region has been (nearly) empty */
	float germ_cool;
	germ_t germ[MAX_GERM];
	int ngerm;
	float dmin[256], dmin_next[256]; /* per colony: path distance of its oldest living thread (pulse source) */
	unsigned fcount_m;
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x6d796365u);
	us_rng_seed(&s->rng2, seed ^ 0x63726974u);
	return s;
}

static void destroy(void *p) { free(p); }

#define SOIL0 0.30f /* the surface sits here: a strip of the world above, the cut bank below */
static float soil_y(float xf)
{
	return SOIL0 + 0.012f * us_noise2(xf * 3.1f, 2.7f) + 0.004f * us_noise2(xf * 13.0f, 0.4f);
}
/* soil horizons: humus -> topsoil -> subsoil -> bedrock */
static float hz_humus(float xf) { return soil_y(xf) + 0.05f + 0.008f * us_noise2(xf * 6.0f, 9.1f); }
static float hz_sub(float xf) { return 0.6f + 0.035f * us_noise2(xf * 2.3f, 7.1f) + 0.008f * us_noise2(xf * 11.0f, 1.3f); }
static float hz_rock(float xf) { return 0.87f + 0.03f * us_noise2(xf * 1.7f, 3.3f) + 0.007f * us_noise2(xf * 9.0f, 5.5f); }

/* stones: is (x units, y) inside one? (A = aspect) */
static int in_stone(const st_t *st, float x, float y, float A, float pad)
{
	for (int k = 0; k < st->nstones; k++) {
		const stone_t *s = &st->stone[k];
		float dx = (x - s->x * A) / (s->rx + pad), dy = (y - s->y) / (s->ry + pad);
		if (dx * dx + dy * dy < 1.0f)
			return 1;
	}
	return 0;
}

/* mole tunnel (and its nest): is (xf, y) inside? */
static int in_tunnel(const st_t *st, float xf, float y, float A, float pad)
{
	if (st->tun_x1 <= st->tun_x0)
		return 0;
	{
		float dx = (xf - st->nest_xf) * A / (0.03f + pad), dy = (y - st->nest_y) / (0.017f + pad);
		if (dx * dx + dy * dy < 1.0f)
			return 1;
	}
	if (y < st->tun_ymin - TUN_R - pad || y > st->tun_ymax + TUN_R + pad || xf < st->tun_x0 || xf > st->tun_x1)
		return 0;
	float f = (xf - st->tun_x0) / (st->tun_x1 - st->tun_x0) * (TUN_N - 1);
	int i = (int)f;
	if (i >= TUN_N - 1)
		i = TUN_N - 2;
	float ty = us_lerp(st->tun_y[i], st->tun_y[i + 1], f - i);
	return fabsf(y - ty) < TUN_R + pad;
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
	float xf = 0.5f, y = 0.6f;
	for (int k = 0; k < 8; k++) {
		xf = us_randr(r, 0.08f, 0.92f);
		y = us_randr(r, soil_y(xf) + 0.07f, hz_rock(xf) - 0.04f);
		if (!in_stone(st, xf * A, y, A, 0.01f) && !in_tunnel(st, xf, y, A, 0.01f))
			break;
	}
	new_colony_at(st, xf, y);
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
			if (in_stone(st, nx, ny, A, 0.003f)) {
				/* a stone: the tip slides along it (turns toward whichever side is free) */
				float side = us_rand(r) < 0.5f ? -1.0f : 1.0f;
				int ok = 0;
				for (int k2 = 0; k2 < 4 && !ok; k2++) {
					float a2 = t->ang + side * (k2 < 2 ? 0.7f : -0.7f) * (1 + (k2 & 1));
					float tx = x + cosf(a2) * STEP, ty = y + sinf(a2) * STEP;
					if (!in_stone(st, tx, ty, A, 0.003f)) {
						t->ang = a2, nx = tx, ny = ty, ok = 1;
					}
				}
				if (!ok) {
					dead = 1;
					break;
				}
			}
			if (in_tunnel(st, nx / A, ny, A, 0.002f) || nx < 0.006f || nx > A - 0.006f || ny > hz_rock(nx / A) - 0.006f || ny < sy + 0.008f ||
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
	p->h = us_randr(r, 0.08f, 0.15f);
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
	/* soil grain: dense in the humus, thinning with depth (rejection sampling on a density profile) */
	for (int i = 0; i < N_SPECK; i++) {
		float xf, y;
		for (;;) {
			xf = us_rand(r);
			y = us_randr(r, soil_y(xf) + 0.004f, 0.995f);
			float d = y < hz_humus(xf) ? 1.0f : y < hz_sub(xf) ? 0.55f : y < hz_rock(xf) ? 0.3f : 0.18f;
			if (us_rand(r) < d)
				break;
		}
		st->speck_x[i] = xf;
		st->speck_y[i] = y;
		st->speck_v[i] = us_rand(r);
	}
	for (int i = 0; i < 260; i++) {
		st->blade_x[i] = us_rand(r);
		st->blade_v[i] = us_rand(r);
	}
	for (int i = 0; i < N_FRAG; i++) {
		float xf = us_rand(r);
		st->frag[i][0] = xf;
		st->frag[i][1] = us_randr(r, soil_y(xf) + 0.006f, hz_humus(xf) - 0.004f);
		st->frag[i][2] = us_randn(r) * 0.35f;
		st->frag[i][3] = us_randr(r, 0.004f, 0.011f);
	}
	for (int i = 0; i < N_LITTER; i++) {
		st->litter[i][0] = us_rand(r);
		st->litter[i][1] = us_randn(r) * 0.3f;
		st->litter[i][2] = us_randr(r, 0.006f, 0.013f);
		st->litter[i][3] = us_rand(r);
	}
	for (int i = 0; i < N_STRATA; i++) {
		float xf = us_rand(r);
		st->strata[i][0] = xf;
		st->strata[i][1] = us_randr(r, hz_rock(xf) + 0.01f, 0.995f);
		st->strata[i][2] = us_randr(r, 0.02f, 0.09f);
	}
	for (int i = 0; i < N_STAR; i++) {
		st->star[i][0] = us_rand(r);
		st->star[i][1] = us_randr(r, 0.01f, SOIL0 - 0.1f);
		st->star[i][2] = us_rand(r);
	}
	/* stones: pebbles in the topsoil, cobbles in the subsoil, boulders sunk in the bedrock */
	st->nstones = 0;
	for (int i = 0; i < MAX_STONE; i++) {
		stone_t *sn = &st->stone[st->nstones];
		float xf = us_randr(r, 0.03f, 0.97f), rr, y;
		if (i < 4) {
			rr = us_randr(r, 0.008f, 0.016f);
			y = us_randr(r, hz_humus(xf) + 0.02f, hz_sub(xf));
		} else if (i < 10) {
			rr = us_randr(r, 0.014f, 0.03f);
			y = us_randr(r, hz_sub(xf), hz_rock(xf));
		} else {
			rr = us_randr(r, 0.04f, 0.075f);
			y = hz_rock(xf) + us_randr(r, -0.005f, 0.03f);
		}
		int clash = 0;
		for (int k = 0; k < st->nstones; k++) {
			float dx = (st->stone[k].x - xf) * A, dy = st->stone[k].y - y;
			if (dx * dx + dy * dy < (st->stone[k].rx + rr + 0.02f) * (st->stone[k].rx + rr + 0.02f))
				clash = 1;
		}
		if (clash)
			continue;
		sn->x = xf;
		sn->y = y;
		sn->rx = rr;
		sn->ry = rr * us_randr(r, 0.55f, 0.8f);
		for (int k = 0; k < STONE_V; k++)
			sn->rv[k] = us_randr(r, 0.82f, 1.08f);
		st->nstones++;
	}
	/* a mole's tunnel wandering through the subsoil (threading between the stones), ending in a grass-lined nest */
	{
		us_rng *r = &st->rng2;
		int ok = 0;
		for (int tries = 0; tries < 16 && !ok; tries++) {
			float len = us_randr(r, 0.3f, 0.42f);
			st->tun_x0 = us_randr(r, 0.1f, 0.9f - len);
			st->tun_x1 = st->tun_x0 + len;
			float base = us_randr(r, 0.0f, 1.0f), ph = us_rand(r) * 9.0f;
			st->tun_ymin = 1, st->tun_ymax = 0;
			ok = 1;
			for (int i = 0; i < TUN_N; i++) {
				float xf = us_lerp(st->tun_x0, st->tun_x1, (float)i / (TUN_N - 1));
				float lo = hz_sub(xf) + 0.035f, hi = hz_rock(xf) - 0.06f;
				float y = us_lerp(lo, hi, us_clampf(base + 0.35f * us_noise2(xf * 6.0f, ph), 0.0f, 1.0f));
				st->tun_y[i] = y;
				st->tun_ymin = fminf(st->tun_ymin, y);
				st->tun_ymax = fmaxf(st->tun_ymax, y);
				if (in_stone(st, xf * A, y, A, TUN_R + 0.006f))
					ok = 0;
			}
			int nest_right = us_rand(r) < 0.5f;
			st->nest_xf = nest_right ? st->tun_x1 + 0.02f / A : st->tun_x0 - 0.02f / A;
			st->nest_y = st->tun_y[nest_right ? TUN_N - 1 : 0] - 0.004f;
			if (in_stone(st, st->nest_xf * A, st->nest_y, A, 0.03f))
				ok = 0;
		}
		if (!ok)
			st->tun_x1 = st->tun_x0; /* no room: no tunnel this time */
		st->mole_u = us_rand(r);
		st->mole_dir = 1.0f;
		/* the tunnel was dug out: its grains are packed into the floor */
		for (int i = 0; i < N_SPECK; i++)
			if (in_tunnel(st, st->speck_x[i], st->speck_y[i], A, 0.0f))
				st->speck_y[i] += TUN_R * 2.0f + 0.002f * us_rand(r);
	}
	/* an old tree at one side: its trunk leaves the top of the frame, buttress roots plunge into the soil */
	{
		float side = us_rand(r) < 0.5f ? 0.0f : 1.0f;
		st->tree_x = side ? us_randr(r, 0.84f, 0.92f) : us_randr(r, 0.08f, 0.16f);
		st->ntree = 0;
		float sx[TREE_SEGS], sy[TREE_SEGS], sa[TREE_SEGS], sl[TREE_SEGS], sw[TREE_SEGS];
		int sd[TREE_SEGS], top = 0;
		int nmain = 5;
		for (int k = 0; k < nmain; k++) {
			float f = (k + 0.5f) / nmain;
			sx[top] = (f - 0.5f) * 0.05f;
			sy[top] = 0.004f;
			sa[top] = US_PI * (0.5f + 0.85f * (f - 0.5f)) + us_randn(r) * 0.08f;
			sl[top] = us_randr(r, 0.34f, 0.56f) * (0.6f + 0.8f * (1.0f - fabsf(f - 0.5f) * 2.0f) * 0.5f);
			sw[top] = us_randr(r, 0.013f, 0.019f);
			sd[top] = 0;
			top++;
		}
		while (top > 0 && st->ntree < TREE_SEGS) {
			top--;
			float x = sx[top], y = sy[top], a = sa[top], L = sl[top], w0 = sw[top];
			int dpt = sd[top];
			int n = 8 + (int)(us_rand(r) * 4);
			for (int k = 0; k < n && st->ntree < TREE_SEGS; k++) {
				a += us_randn(r) * 0.3f;
				a = us_lerp(a, US_PI * 0.5f, 0.06f); /* gravitropism */
				if (y < 0.03f && sinf(a) < 0.2f)
					a = us_lerp(a, US_PI * 0.5f, 0.4f); /* never out of the ground */
				float l = L / n;
				rseg_t *sg = &st->tree[st->ntree++];
				sg->x0 = x, sg->y0 = y;
				x += cosf(a) * l, y += sinf(a) * l;
				sg->x1 = x, sg->y1 = y;
				sg->w = w0 * (1.0f - 0.75f * (float)k / n);
				if (dpt < 3 && top < TREE_SEGS - 1 && us_rand(r) < 0.42f / (1.0f + dpt)) {
					sx[top] = x, sy[top] = y;
					sa[top] = a + (us_rand(r) < 0.5f ? -1 : 1) * us_randr(r, 0.45f, 1.0f);
					sl[top] = L * us_randr(r, 0.35f, 0.55f);
					sw[top] = sg->w * 0.6f;
					sd[top] = dpt + 1;
					top++;
				}
			}
		}
	}
	/* plants, kept clear of the tree */
	st->nplants = 3;
	for (int i = 0; i < st->nplants; i++) {
		float xf = (i + 0.5f) / st->nplants + us_randr(r, -0.12f, 0.12f);
		if (fabsf(xf - st->tree_x) < 0.1f)
			xf += xf < st->tree_x ? -0.1f : 0.1f;
		make_plant(st, &st->plant[i], us_clampf(xf, 0.04f, 0.96f));
	}
	for (int k = 0; k < MAX_WORM; k++) {
		worm_t *wm = &st->worm[k];
		float xf = us_randr(r, 0.15f, 0.85f), y = us_randr(r, hz_humus(xf) + 0.03f, hz_sub(xf) - 0.03f);
		wm->ang = us_rand(r) < 0.5f ? 0.0f : US_PI;
		wm->ph = us_rand(r) * 10.0f;
		wm->speed = us_randr(r, 0.005f, 0.008f);
		for (int i = 0; i < WORM_N; i++) {
			wm->x[i] = xf * A - cosf(wm->ang) * i * 0.0046f;
			wm->y[i] = y;
		}
	}

	for (int k = 0; k < N_SPRING; k++) {
		springtail_t *sp = &st->spring[k];
		sp->xf = us_randr(&st->rng2, 0.03f, 0.97f);
		sp->dfr = us_randr(&st->rng2, 0.25f, 0.95f);
		sp->face = us_rand(&st->rng2) < 0.5f ? -1.0f : 1.0f;
		sp->ph = us_rand(&st->rng2) * 10.0f;
	}
	st->beetle.xf = us_randr(&st->rng2, 0.2f, 0.8f);
	st->beetle.dir = us_rand(&st->rng2) < 0.5f ? -1.0f : 1.0f;
	st->beetle.ph = us_rand(&st->rng2) * 10.0f;

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

/* inlined max-blend pixel write (same as us_put in MAX mode, minus the call per pixel) */
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
			put_max(c, iy, i, v * cov * (1 - f), mat);
			put_max(c, iy + 1, i, v * cov * f, mat);
		} else {
			put_max(c, i, iy, v * cov * (1 - f), mat);
			put_max(c, i, iy + 1, v * cov * f, mat);
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

/* a hairline in Ink, a 1-px max-blended line in the pixel styles (doesn't steal brighter pixels) */
static void hline(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
	if (!c->crisp) {
		wu_line(c, ax, ay, bx, by, v, mat);
		return;
	}
	float x0 = ax * c->s, y0 = ay * c->s, x1 = bx * c->s, y1 = by * c->s;
	float dx = x1 - x0, dy = y1 - y0;
	int n = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
	if (n < 1)
		n = 1;
	for (int k = 0; k <= n; k++)
		put_max(c, (int)(x0 + dx * k / n), (int)(y0 + dy * k / n), v, mat);
}

/* fine root hairs: a few short bristles off a root segment, stirred by the air band */
static void root_hairs(us_canvas *c, float x0, float y0, float x1, float y1, int k, float stir, float t)
{
	float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy) + 1e-6f;
	float ux = dx / l, uy = dy / l, nx = -uy, ny = ux;
	for (int j = 0; j < 3; j++) {
		uint32_t h = (uint32_t)k * 2654435761u ^ (uint32_t)j * 40503u;
		h ^= h >> 13;
		h *= 0x5bd1e995u;
		h ^= h >> 15;
		float u = 0.15f + 0.7f * (float)((h >> 8) & 255) / 255.0f;
		float side = (h & 1) ? 1.0f : -1.0f;
		float L = 0.0025f + 0.0035f * (float)((h >> 16) & 255) / 255.0f;
		float wob = stir * 0.7f * sinf(t * 5.0f + k * 1.7f + j * 2.1f);
		float bx = x0 + dx * u, by = y0 + dy * u;
		float ex = bx + (nx * side + ux * (0.45f + wob)) * L, ey = by + (ny * side + uy * (0.45f + wob)) * L;
		wu_line(c, bx, by, ex, ey, 0.15f + 0.12f * stir, US_MAT_EARTH);
	}
}

static void tun_at(const st_t *st, float u, float A, float *xf, float *y, float *slope)
{
	u = us_sat(u);
	float f = u * (TUN_N - 1);
	int i = (int)f;
	if (i >= TUN_N - 1)
		i = TUN_N - 2;
	*xf = us_lerp(st->tun_x0, st->tun_x1, u);
	*y = us_lerp(st->tun_y[i], st->tun_y[i + 1], f - i);
	*slope = (st->tun_y[i + 1] - st->tun_y[i]) / fmaxf((st->tun_x1 - st->tun_x0) * A / (TUN_N - 1), 1e-4f);
}

/* the little lives of the bank: a mole in its tunnel (sub-bass digs, kicks shake crumbs from the roof),
 * springtails in the humus (hats make them spring), a ground beetle on the surface (low-mids walk it) */
static void draw_critters(st_t *st, const us_ctx *ctx, us_canvas *c, int kick_hit, int hi_hit)
{
	us_rng *r = &st->rng2;
	float A = ctx->aspect, dt = ctx->dt;
	int crisp = c->crisp;
	float quiet = us_sat(ctx->a->silence * 0.5f);

	/* ---- tunnel + nest ---- */
	if (st->tun_x1 > st->tun_x0) {
		float step = 1.0f / (TUN_N - 1);
		for (int i = 0; i + 1 < TUN_N; i++) {
			float xa = us_lerp(st->tun_x0, st->tun_x1, i * step) * A, xb = us_lerp(st->tun_x0, st->tun_x1, (i + 1) * step) * A;
			float ya = st->tun_y[i], yb = st->tun_y[i + 1];
			float ja = 0.0015f * us_noise2(xa * 40.0f, 1.3f), jb = 0.0015f * us_noise2(xb * 40.0f, 1.3f);
			float ka = 0.0012f * us_noise2(xa * 40.0f, 7.7f), kb = 0.0012f * us_noise2(xb * 40.0f, 7.7f);
			hline(c, xa, ya - TUN_R + ja, xb, yb - TUN_R + jb, 0.3f, US_MAT_EARTH);
			hline(c, xa, ya + TUN_R + ka, xb, yb + TUN_R + kb, 0.36f, US_MAT_EARTH);
			if (!crisp) /* packed floor */
				wu_line(c, xa, ya + TUN_R + ka + 0.0017f, xb, yb + TUN_R + kb + 0.0017f, 0.13f, US_MAT_EARTH);
		}
		/* nest: a round chamber lined with dry grass */
		float nx = st->nest_xf * A, ny = st->nest_y;
		float px = 0, py = 0;
		for (int k = 0; k <= 16; k++) {
			float an = US_TAU * k / 16.0f;
			float qx = nx + cosf(an) * 0.03f * (1.0f + 0.06f * us_noise2(k * 0.9f, 4.0f));
			float qy = ny + sinf(an) * 0.017f;
			if (k > 0)
				hline(c, px, py, qx, qy, 0.32f, US_MAT_EARTH);
			px = qx, py = qy;
		}
		for (int k = 0; k < (crisp ? 2 : 6); k++) {
			float yy = ny + 0.0145f - 0.0024f * k * (crisp ? 2.0f : 1.0f);
			float q = (yy - ny) / 0.017f, hw = 0.028f * sqrtf(fmaxf(0.0f, 1.0f - q * q));
			float o = 0.004f * sinf(k * 2.3f);
			hline(c, nx - hw + fabsf(o), yy, nx + hw - fabsf(o) * 0.5f, yy - 0.001f * (k & 1), 0.22f + 0.05f * (k & 1), US_MAT_LEAF);
		}
	}

	/* ---- mole: sub-bass drives the digging ---- */
	if (st->tun_x1 > st->tun_x0) {
		float len = (st->tun_x1 - st->tun_x0) * A;
		float rest = us_smooth(-0.2f, 0.4f, us_noise2(st->t * 0.07f, 5.5f)); /* wanders, rests, wanders */
		float spd = (0.002f + 0.004f * rest + 0.022f * st->sub_env) / fmaxf(len, 0.05f);
		st->mole_u += st->mole_dir * spd * dt;
		if (st->mole_u > 1.0f)
			st->mole_u = 1.0f, st->mole_dir = -1.0f;
		if (st->mole_u < 0.0f)
			st->mole_u = 0.0f, st->mole_dir = 1.0f;
		st->mole_paw += dt * (1.5f + 26.0f * st->sub_env + 4.0f * rest);
		float mxf, my, sl;
		tun_at(st, st->mole_u, A, &mxf, &my, &sl);
		float dir = st->mole_dir;
		float ang = atanf(sl), ca = cosf(ang), sa = sinf(ang);
		float rx = 0.016f, ry = 0.0084f;
		float bx = mxf * A, by = my + TUN_R - ry - 0.0006f;
		/* crumbs: digging loosens the roof ahead of the mole; kicks shake the whole tunnel */
		int nk = kick_hit ? 1 + (us_rand(r) < 0.4f) : 0, dig = us_rand(r) < dt * 8.0f * st->sub_env * st->sub_env;
		for (int k = 0; k < nk + dig && st->ncrumb < MAX_CRUMB; k++) {
			crumb_t *cr = &st->crumb[st->ncrumb++];
			float u = k == nk ? st->mole_u + dir * 0.035f : us_rand(r);
			float cx, cy, cs;
			tun_at(st, u, A, &cx, &cy, &cs);
			cr->xf = cx;
			cr->y = cy - TUN_R + 0.002f;
			cr->vy = 0;
			cr->life = 1.0f;
		}
		if (crisp) {
			us_disc(c, bx, by, ry, 0.5f, US_MAT_EARTH);
			us_disc(c, bx - dir * ca * rx * 0.55f, by - dir * sa * rx * 0.55f, ry * 0.9f, 0.5f, US_MAT_EARTH);
			put_max(c, (int)((bx + dir * ca * rx * 1.25f) * c->s), (int)((by + dir * sa * rx * 1.25f) * c->s), 0.8f, US_MAT_BLOOM);
		} else {
			/* body: an engraved oval, a pointed snout, two spade paws */
			float qx = 0, qy = 0;
			for (int k = 0; k <= 14; k++) {
				float an = US_TAU * k / 14.0f;
				float lx = cosf(an) * rx, ly = sinf(an) * ry * (cosf(an) * dir > 0 ? 0.82f : 1.0f);
				float wx = bx + lx * ca - ly * sa, wy = by + lx * sa + ly * ca;
				if (k > 0)
					wu_line(c, qx, qy, wx, wy, sinf(an) < 0 ? 0.78f : 0.55f, US_MAT_EARTH);
				qx = wx, qy = wy;
			}
			for (int k = -2; k <= 2; k++) { /* fur */
				float u = k * 0.28f * rx;
				float fx = bx + u * ca, fy = by + u * sa - ry * 0.5f;
				wu_line(c, fx, fy, fx - dir * 0.0026f, fy + 0.0012f, 0.24f, US_MAT_EARTH);
			}
			float hx = bx + dir * ca * rx, hy = by + dir * sa * rx;
			float sx = hx + dir * ca * 0.0058f, sy = hy + dir * sa * 0.0058f + 0.0008f;
			wu_line(c, hx, hy - 0.0022f, sx, sy, 0.55f, US_MAT_EARTH);
			wu_line(c, hx, hy + 0.0026f, sx, sy, 0.45f, US_MAT_EARTH);
			us_point(c, sx, sy, 0.0011f, 0.85f, US_MAT_BLOOM); /* the pink nose */
			wu_line(c, bx - dir * ca * rx, by - dir * sa * rx, bx - dir * (rx + 0.0035f), by - dir * sa * rx + 0.0012f, 0.35f, US_MAT_EARTH);
			for (int k = 0; k < 2; k++) { /* paws paddle in alternation */
				float pa = st->mole_paw + k * US_PI;
				float px0 = hx - dir * 0.003f * (1 + k), py0 = hy + ry * 0.55f;
				float px1 = px0 + dir * (0.0045f + 0.0025f * sinf(pa)), py1 = py0 + 0.0018f - 0.0014f * cosf(pa);
				wu_line(c, px0, py0, px1, py1, 0.6f, US_MAT_EARTH);
				wu_line(c, px1, py1 - 0.0013f, px1 + dir * 0.0006f, py1 + 0.0013f, 0.55f, US_MAT_EARTH);
			}
		}
		for (int k = 0; k < st->ncrumb;) {
			crumb_t *cr = &st->crumb[k];
			float cx, cy, cs, u = (cr->xf - st->tun_x0) / (st->tun_x1 - st->tun_x0);
			tun_at(st, u, A, &cx, &cy, &cs);
			float floor_y = cy + TUN_R - 0.0012f;
			if (cr->y < floor_y) {
				cr->vy += 0.5f * dt;
				cr->y = fminf(floor_y, cr->y + cr->vy * dt);
			} else {
				cr->life -= dt * 0.5f;
			}
			if (cr->life <= 0) {
				*cr = st->crumb[--st->ncrumb];
				continue;
			}
			float v = (cr->y < floor_y ? 0.3f : 0.55f) * us_smooth(0.0f, 0.4f, cr->life); /* dim in flight: no streaks */
			if (crisp)
				put_max(c, (int)(cr->xf * A * c->s), (int)(cr->y * c->s), v, US_MAT_EARTH);
			else
				us_point(c, cr->xf * A, cr->y, 0.0011f, v, US_MAT_EARTH);
			k++;
		}
	}

	/* ---- springtails: hats make them leap (a flick of the furcula) ---- */
	for (int k = 0; k < N_SPRING; k++) {
		springtail_t *sp = &st->spring[k];
		float floor_y = us_lerp(soil_y(sp->xf) + 0.007f, hz_humus(sp->xf) - 0.004f, sp->dfr);
		sp->cool -= dt;
		int air = sp->hy < 0.0f || sp->vy != 0.0f;
		if (!air) {
			sp->ph += dt * (1.0f + 2.0f * us_rand(r));
			sp->xf += sp->face * 0.003f * us_sat(sinf(sp->ph)) * dt / A;
			int go = (hi_hit && us_rand(r) < 0.3f) || us_rand(r) < dt * (0.05f + 0.1f * quiet);
			if (go && sp->cool <= 0) {
				if (us_rand(r) < 0.3f)
					sp->face = -sp->face;
				sp->vy = -us_randr(r, 0.14f, 0.22f) * (0.8f + 0.4f * st->hi_slow);
				sp->vx = sp->face * us_randr(r, 0.02f, 0.05f);
				sp->cool = us_randr(r, 0.15f, 0.4f);
			}
		} else {
			sp->vy += 2.6f * dt;
			sp->hy += sp->vy * dt;
			sp->xf += sp->vx * dt / A;
			float ceil_y = soil_y(sp->xf) + 0.004f;
			if (floor_y + sp->hy < ceil_y)
				sp->hy = ceil_y - floor_y, sp->vy = fmaxf(sp->vy, 0.0f);
			if (sp->hy >= 0.0f)
				sp->hy = 0.0f, sp->vy = 0.0f, sp->vx = 0.0f;
		}
		if (sp->xf < 0.02f)
			sp->xf = 0.02f, sp->face = 1.0f;
		if (sp->xf > 0.98f)
			sp->xf = 0.98f, sp->face = -1.0f;
		float x = sp->xf * A, y = floor_y + sp->hy;
		if (crisp) {
			put_max(c, (int)(x * c->s), (int)(y * c->s), 0.8f, US_MAT_ACCENT);
			continue;
		}
		float f = sp->face, tilt = air ? sp->vy * 0.012f : 0.0f;
		float hx = x + f * 0.0026f, hy2 = y - 0.0005f + tilt;
		wu_line(c, x - f * 0.0028f, y + 0.0003f - tilt, hx, hy2, 0.8f, US_MAT_ACCENT);
		wu_line(c, x - f * 0.0024f, y + 0.0011f - tilt, hx - f * 0.0004f, hy2 + 0.0008f, 0.5f, US_MAT_ACCENT);
		wu_line(c, hx, hy2, hx + f * 0.0018f, hy2 - 0.0017f, 0.45f, US_MAT_ACCENT); /* antennae */
		wu_line(c, hx, hy2, hx + f * 0.0024f, hy2 - 0.0006f, 0.4f, US_MAT_ACCENT);
	}

	/* ---- ground beetle: low-mids set its walk; it pauses, and turns at the edges and the tree ---- */
	{
		beetle_t *b = &st->beetle;
		float idle = us_smooth(0.3f, 0.55f, us_noise2(st->t * 0.15f, 2.2f)) * (1.0f - us_sat(st->lm_env * 2.0f));
		float spd = 0.014f * (0.12f + 1.4f * st->lm_env) * (1.0f - idle);
		b->xf += b->dir * spd * dt / A;
		b->legph += spd * dt * 900.0f;
		float tx = st->tree_x;
		if (b->xf < 0.03f || (b->dir > 0 && b->xf > tx - 0.06f && b->xf < tx))
			b->dir = -b->dir;
		else if (b->xf > 0.97f || (b->dir < 0 && b->xf < tx + 0.06f && b->xf > tx))
			b->dir = -b->dir;
		if (b->xf < 0.03f)
			b->dir = 1.0f;
		if (b->xf > 0.97f)
			b->dir = -1.0f;
		float d = b->dir, x = b->xf * A, gy = soil_y(b->xf), y = gy - 0.0046f;
		if (crisp) {
			us_disc(c, x, y, 0.0055f, 0.75f, US_MAT_STEM);
			put_max(c, (int)((x + d * 0.009f) * c->s), (int)(y * c->s), 0.6f, US_MAT_STEM);
		} else {
			float qx = 0, qy = 0, rx = 0.0075f, ry = 0.0036f;
			for (int k = 0; k <= 12; k++) { /* elytra: a domed oval, flat below */
				float an = US_PI + US_PI * k / 12.0f;
				float wx = x - d * 0.001f + cosf(an) * rx, wy = y + sinf(an) * ry;
				if (k > 0)
					wu_line(c, qx, qy, wx, wy, 0.85f, US_MAT_STEM);
				qx = wx, qy = wy;
			}
			wu_line(c, x - d * 0.001f - rx, y, x - d * 0.001f + rx, y, 0.6f, US_MAT_STEM);
			wu_line(c, x - d * 0.006f, y - 0.0022f, x + d * 0.004f, y - 0.0031f, 0.35f, US_MAT_LIGHT); /* sheen */
			us_ring(c, x + d * 0.0093f, y - 0.0008f, 0.0022f, 0.0007f, 0.7f, US_MAT_STEM); /* pronotum */
			us_point(c, x + d * 0.0128f, y - 0.0002f, 0.0012f, 0.7f, US_MAT_STEM);
			wu_line(c, x + d * 0.0135f, y - 0.0008f, x + d * 0.0175f, y - 0.0042f, 0.45f, US_MAT_STEM);
			wu_line(c, x + d * 0.0135f, y - 0.0006f, x + d * 0.0185f, y - 0.0028f, 0.4f, US_MAT_STEM);
			for (int k = 0; k < 3; k++) { /* tripod gait */
				float lx = x + d * (-0.004f + 0.0045f * k), sw = 0.0022f * sinf(b->legph + (k & 1) * US_PI);
				float kx = lx + d * 0.0015f + sw * 0.5f, ky = y + 0.001f;
				wu_line(c, lx, y, kx, ky, 0.55f, US_MAT_STEM);
				wu_line(c, kx, ky, kx + sw + d * 0.001f * (k - 1), gy, 0.5f, US_MAT_STEM);
			}
		}
	}
}

/* the world around the network: sky, surface life, the tree, soil horizons, stones, roots, worms */
static void draw_ground(st_t *st, const us_ctx *ctx, us_canvas *c, float wind)
{
	us_world *w = ctx->w;
	float A = ctx->aspect, dens = ctx->p->density, dt = ctx->dt;
	int crisp = c->crisp;
	float spring = us_world_season_w(w, 0), summer = us_world_season_w(w, 1);
	float autumn = us_world_season_w(w, 2), winter = us_world_winter(w);
	float grow_k = (0.55f + 0.5f * w->growth) * (0.75f + 0.35f * summer + 0.15f * spring) * (1.0f - 0.55f * winter);

	/* night: a few stars over the surface strip */
	float night = us_sat(1.0f - w->daylight * 1.8f);
	if (night > 0.02f)
		for (int i = 0; i < N_STAR; i++) {
			float tw = 0.6f + 0.4f * sinf(st->t * (0.7f + st->star[i][2]) + i * 2.3f);
			float v = night * (0.12f + 0.3f * st->star[i][2] * st->star[i][2]) * tw;
			put_max(c, (int)(st->star[i][0] * A * c->s), (int)(st->star[i][1] * c->s), v, US_MAT_LIGHT);
		}

	/* back row of grass: shorter, dimmer, a touch higher — the surface has depth */
	int nback = 60 + (int)(dens * 90);
	for (int b = 0; b < nback; b++) {
		float xf = st->blade_x[b], v0 = st->blade_v[b];
		float x = xf * A, gy = soil_y(xf) - 0.006f;
		float h = (0.01f + 0.022f * v0) * grow_k;
		float sw = us_noise2(x * 1.6f - st->sway_t * 0.8f, v0 * 7.0f) * (0.15f + 0.35f * wind);
		hline(c, x, gy, x + sw * h, gy - h, 0.14f + 0.1f * v0, US_MAT_LEAF);
	}

	PROF(1);
	/* the tree: trunk (leaves the top of the frame) with a buttress flare */
	if (st->tree_x > 0) {
		float tx = st->tree_x * A, gy = soil_y(st->tree_x);
		enum { NS = 24 };
		float lx[(NS + 1) * 2], rx[(NS + 1) * 2];
		for (int k = 0; k <= NS; k++) {
			float y = -0.01f + (gy + 0.004f + 0.01f) * k / NS;
			float hw = 0.024f + 0.026f * expf(-(gy - y) / 0.035f);
			float bend = 0.006f * us_noise2(y * 4.0f, 1.7f);
			lx[2 * k] = tx - hw + bend, lx[2 * k + 1] = y;
			rx[2 * k] = tx + hw + bend * 0.7f, rx[2 * k + 1] = y;
		}
		if (crisp) {
			/* filled bark */
			for (int k = 0; k < NS; k++) {
				int y0 = (int)(lx[2 * k + 1] * c->s), y1 = (int)(lx[2 * k + 3] * c->s);
				for (int py = y0; py <= y1; py++) {
					int xa = (int)(lx[2 * k] * c->s), xb = (int)(rx[2 * k] * c->s);
					for (int px = xa; px <= xb; px++)
						put_max(c, px, py, ((px + py * 3) % 7 == 0) ? 0.26f : 0.15f, US_MAT_EARTH);
				}
			}
		}
		for (int k = 0; k < NS; k++) {
			hline(c, lx[2 * k], lx[2 * k + 1], lx[2 * k + 2], lx[2 * k + 3], 0.68f, US_MAT_EARTH);
			hline(c, rx[2 * k], rx[2 * k + 1], rx[2 * k + 2], rx[2 * k + 3], 0.55f, US_MAT_EARTH);
		}
		if (!crisp) {
			/* bark: broken vertical furrows */
			for (int j = 1; j < 7; j++) {
				float f = j / 7.0f;
				for (int k = (j & 1); k < NS; k += 2) {
					float xa = us_lerp(lx[2 * k], rx[2 * k], f), xb = us_lerp(lx[2 * k + 2], rx[2 * k + 2], f);
					float wig = 0.002f * us_noise2(j * 3.0f, k * 0.7f);
					wu_line(c, xa + wig, lx[2 * k + 1], xb - wig, lx[2 * k + 3], 0.15f + 0.05f * (j & 2), US_MAT_EARTH);
				}
			}
		}
		/* roots: thick ones drawn as two edges (engraved), fine ones as single hairlines */
		for (int k = 0; k < st->ntree; k++) {
			const rseg_t *sg = &st->tree[k];
			float x0 = tx + sg->x0, y0 = gy + sg->y0, x1 = tx + sg->x1, y1 = gy + sg->y1;
			if (crisp) {
				if (sg->w > 0.004f)
					us_line(c, x0, y0, x1, y1, sg->w, 0.5f, US_MAT_EARTH);
				else
					hline(c, x0, y0, x1, y1, 0.4f, US_MAT_EARTH);
				continue;
			}
			if (sg->w > 0.0028f) {
				float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy) + 1e-6f;
				float ox = -dy / l * sg->w * 0.5f, oy = dx / l * sg->w * 0.5f;
				if (sg->w > 0.006f) /* the big roots: a dim core line between the engraved edges */
					wu_line(c, x0, y0, x1, y1, 0.2f, US_MAT_EARTH);
				wu_line(c, x0 + ox, y0 + oy, x1 + ox, y1 + oy, 0.85f, US_MAT_EARTH);
				wu_line(c, x0 - ox, y0 - oy, x1 - ox, y1 - oy, 0.65f, US_MAT_EARTH);
				/* engraved cross-hatching along the root body */
				for (int j = 1; j < 3; j++) {
					float u = j / 3.0f, mx = us_lerp(x0, x1, u), my = us_lerp(y0, y1, u);
					wu_line(c, mx + ox * 0.8f + dx * 0.08f, my + oy * 0.8f + dy * 0.08f, mx - ox * 0.8f - dx * 0.08f,
						my - oy * 0.8f - dy * 0.08f, 0.26f, US_MAT_EARTH);
				}
			} else {
				wu_line(c, x0, y0, x1, y1, 0.22f + 60.0f * sg->w, US_MAT_EARTH);
				if (sg->w < 0.0022f)
					root_hairs(c, x0, y0, x1, y1, k, st->air_env, st->t);
			}
		}
	}

	PROF(2);
	/* soil horizons: dotted boundaries between humus / topsoil / subsoil / bedrock */
	{
		float step = 0.009f;
		int n = (int)(A / step) + 1;
		for (int k = 0; k < n; k += 2) {
			float xa = k * step, xb = xa + step * 0.8f;
			float fa = xa / A, fb = xb / A;
			hline(c, xa, hz_humus(fa), xb, hz_humus(fb), 0.15f, US_MAT_EARTH);
			if (k % 4 == 0)
				hline(c, xa, hz_sub(fa), xb, hz_sub(fb), 0.13f, US_MAT_EARTH);
			hline(c, xa, hz_rock(fa), xb, hz_rock(fb), 0.2f, US_MAT_EARTH);
		}
	}
	/* humus: matted leaf fragments */
	for (int i = 0; i < N_FRAG; i++) {
		float x = st->frag[i][0] * A, y = st->frag[i][1], an = st->frag[i][2], sz = st->frag[i][3];
		float dx = cosf(an) * sz, dy = sinf(an) * sz;
		float bx = -dy * 0.25f, by = dx * 0.25f;
		float v = 0.17f + 0.08f * (i % 3);
		hline(c, x - dx, y - dy, x + bx, y + by, v, US_MAT_EARTH);
		hline(c, x + bx, y + by, x + dx, y + dy, v, US_MAT_EARTH);
	}
	/* grain */
	for (int i = 0; i < N_SPECK; i++) {
		float v = 0.07f + 0.15f * st->speck_v[i];
		int px = (int)(st->speck_x[i] * A * c->s), py = (int)(st->speck_y[i] * c->s);
		put_max(c, px, py, v * (crisp ? 1.3f : 1.4f), US_MAT_EARTH);
		if (!crisp && st->speck_v[i] > 0.6f) /* the coarser grains are two pixels */
			put_max(c, px + 1, py, v, US_MAT_EARTH);
	}
	/* bedrock: bedding planes */
	for (int i = 0; i < N_STRATA; i++) {
		float x = st->strata[i][0] * A, y = st->strata[i][1], L = st->strata[i][2];
		if (in_stone(st, x, y, A, 0.004f))
			continue;
		float yb = y + 0.004f * us_noise2(x * 3.0f, y * 5.0f);
		hline(c, x - L * 0.5f, y, x, yb, 0.2f, US_MAT_EARTH);
		hline(c, x, yb, x + L * 0.5f, y + 0.002f, 0.16f, US_MAT_EARTH);
	}
	/* stones: outlined, with engraved shading on the lower side */
	for (int k = 0; k < st->nstones; k++) {
		const stone_t *sn = &st->stone[k];
		float cx = sn->x * A, cy = sn->y;
		float px[STONE_V + 1], py[STONE_V + 1];
		for (int j = 0; j <= STONE_V; j++) {
			float an = US_TAU * (j % STONE_V) / STONE_V;
			px[j] = cx + cosf(an) * sn->rx * sn->rv[j % STONE_V];
			py[j] = cy + sinf(an) * sn->ry * sn->rv[j % STONE_V];
		}
		if (crisp)
			for (int j = 0; j < STONE_V; j++)
				us_tri(c, cx, cy, px[j], py[j], px[j + 1], py[j + 1], 0.17f, US_MAT_EARTH);
		for (int j = 0; j < STONE_V; j++)
			hline(c, px[j], py[j], px[j + 1], py[j + 1], j < STONE_V / 2 ? 0.44f : 0.32f, US_MAT_EARTH);
		if (!crisp) {
			for (int j = 0; j < 4; j++) { /* hatching, lower-right */
				float o = 0.15f + 0.17f * j;
				float hl = sqrtf(fmaxf(0.0f, 1.0f - o * o)) * 0.7f;
				float ax = cx + (0.7071f * o + 0.7071f * hl) * sn->rx, ay = cy + (0.7071f * o - 0.7071f * hl) * sn->ry;
				float bx = cx + (0.7071f * o - 0.7071f * hl) * sn->rx, by = cy + (0.7071f * o + 0.7071f * hl) * sn->ry;
				wu_line(c, ax, ay, bx, by, 0.2f, US_MAT_EARTH);
			}
		}
	}
	/* plant roots */
	for (int i = 0; i < st->nplants; i++) {
		plant_t *p = &st->plant[i];
		float bx = p->x * A, by = soil_y(p->x);
		for (int k = 0; k < p->nroot; k++) {
			rseg_t *sg = &p->root[k];
			hline(c, bx + sg->x0, by + sg->y0, bx + sg->x1, by + sg->y1, 0.3f + 40.0f * sg->w, US_MAT_EARTH);
			if (!crisp && sg->w < 0.0018f)
				root_hairs(c, bx + sg->x0, by + sg->y0, bx + sg->x1, by + sg->y1, k + 977 * i, st->air_env, st->t);
		}
	}

	PROF(3);
	/* earthworms: follow-the-leader bodies tunnelling slowly through the topsoil, leaving burrows */
	for (int k = 0; k < MAX_WORM; k++) {
		worm_t *wm = &st->worm[k];
		float hx = wm->x[0], hy = wm->y[0], hf = hx / A;
		wm->ph += dt * (2.0f + 3.0f * st->bass_env);
		float sp = wm->speed * (0.5f + 0.4f * sinf(wm->ph) * sinf(wm->ph) + 0.5f * st->bass_env);
		wm->ang += us_noise2(k * 7.3f, st->t * 0.15f) * 0.8f * dt;
		float top = hz_humus(hf) + 0.025f, bot = hz_sub(hf) + 0.04f;
		float ca = cosf(wm->ang);
		if (hy < top) /* turn downward */
			wm->ang += (ca >= 0 ? 1.0f : -1.0f) * 0.9f * dt;
		else if (hy > bot)
			wm->ang -= (ca >= 0 ? 1.0f : -1.0f) * 0.9f * dt;
		if ((hx < 0.05f && ca < 0) || (hx > A - 0.05f && ca > 0))
			wm->ang = US_PI - wm->ang;
		float nx = hx + cosf(wm->ang) * sp * dt, ny = hy + sinf(wm->ang) * sp * dt;
		if (in_stone(st, nx, ny, A, 0.004f)) {
			wm->ang += 1.2f * dt * 8.0f;
		} else {
			wm->x[0] = nx, wm->y[0] = ny;
			wm->tr_acc += sp * dt;
		}
		for (int i = 1; i < WORM_N; i++) { /* each segment keeps its distance to the one ahead */
			float dx = wm->x[i] - wm->x[i - 1], dy = wm->y[i] - wm->y[i - 1];
			float l = sqrtf(dx * dx + dy * dy) + 1e-6f, seg = 0.0046f * (1.0f + 0.12f * sinf(wm->ph - i * 0.7f));
			wm->x[i] = wm->x[i - 1] + dx / l * seg;
			wm->y[i] = wm->y[i - 1] + dy / l * seg;
		}
		if (wm->tr_acc > 0.008f) {
			wm->tr_acc = 0;
			wm->tr[wm->tr_w][0] = wm->x[WORM_N - 1];
			wm->tr[wm->tr_w][1] = wm->y[WORM_N - 1];
			wm->tr_w = (wm->tr_w + 1) % 40;
			if (wm->ntr < 40)
				wm->ntr++;
		}
		for (int i = 0; i < wm->ntr; i++) { /* burrow: dotted, fading */
			int j = (wm->tr_w - 1 - i + 40) % 40;
			float v = 0.2f * (1.0f - (float)i / 40.0f);
			if (crisp)
				put_max(c, (int)(wm->tr[j][0] * c->s), (int)(wm->tr[j][1] * c->s), v, US_MAT_EARTH);
			else
				us_ring(c, wm->tr[j][0], wm->tr[j][1], 0.0028f, 0.0008f, v, US_MAT_EARTH);
		}
		for (int i = 0; i + 1 < WORM_N; i++) {
			float x0 = wm->x[i], y0 = wm->y[i], x1 = wm->x[i + 1], y1 = wm->y[i + 1];
			float girth = (i < 2 ? 0.003f : i > WORM_N - 4 ? 0.0024f : 0.0038f) *
				      (1.0f + 0.15f * sinf(wm->ph - i * 0.7f));
			float v = i == 4 || i == 5 ? 0.62f : 0.48f; /* the clitellum band */
			if (crisp) {
				us_line(c, x0, y0, x1, y1, 0.009f, v, US_MAT_BLOOM);
				continue;
			}
			float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy) + 1e-6f;
			float ox = -dy / l * girth, oy = dx / l * girth;
			wu_line(c, x0 + ox, y0 + oy, x1 + ox, y1 + oy, v, US_MAT_BLOOM);
			wu_line(c, x0 - ox, y0 - oy, x1 - ox, y1 - oy, v * 0.8f, US_MAT_BLOOM);
			wu_line(c, x1 + ox, y1 + oy, x1 - ox, y1 - oy, v * 0.5f, US_MAT_BLOOM); /* segment ring */
		}
	}

	PROF(4);
	/* the surface: soil line, fallen leaves, the front row of grass, plants, snow */
	{
		float pts[2 * 97];
		for (int i = 0; i <= 96; i++) {
			float xf = i / 96.0f;
			pts[2 * i] = xf * A;
			pts[2 * i + 1] = soil_y(xf);
		}
		us_polyline(c, pts, 97, crisp ? 0.004f : 0.0016f, 0.55f, US_MAT_EARTH);
		if (winter > 0.2f) {
			for (int i = 0; i <= 96; i++)
				pts[2 * i + 1] -= 0.003f * winter;
			us_polyline(c, pts, 97, crisp ? 0.007f : 0.0016f + 0.004f * winter, 0.8f * us_smooth(0.2f, 0.6f, winter),
				    US_MAT_ACCENT);
		}
	}
	int nlit = 16 + (int)(30.0f * autumn) + (int)(dens * 6);
	if (nlit > N_LITTER)
		nlit = N_LITTER;
	for (int i = 0; i < nlit; i++) {
		float xf = st->litter[i][0], an = st->litter[i][1], sz = st->litter[i][2];
		float x = xf * A, y = soil_y(xf) - 0.003f;
		float dx = cosf(an) * sz, dy = sinf(an) * sz * 0.5f;
		float wx = -sinf(an) * sz * 0.35f, wy = -cosf(an) * sz * 0.2f;
		uint8_t m = autumn > 0.3f && st->litter[i][3] > 0.45f ? US_MAT_BLOOM : US_MAT_EARTH;
		float v = 0.45f;
		if (crisp) {
			us_line(c, x - dx, y - dy, x + dx, y + dy, 0.006f, v, m);
		} else {
			wu_line(c, x - dx, y - dy, x + wx, y + wy, v, m);
			wu_line(c, x + wx, y + wy, x + dx, y + dy, v, m);
			wu_line(c, x - dx, y - dy, x - wx * 0.6f, y - wy * 0.6f + 0.001f, v * 0.8f, m);
			wu_line(c, x - wx * 0.6f, y - wy * 0.6f + 0.001f, x + dx, y + dy, v * 0.8f, m);
		}
	}
	int nb = 80 + (int)(dens * 110);
	for (int b = 0; b < nb; b++) {
		float xf = st->blade_x[259 - b], v0 = st->blade_v[259 - b];
		float x = xf * A, gy = soil_y(xf);
		float h = (0.012f + 0.03f * v0) * grow_k;
		float sw = us_noise2(x * 2.0f - st->sway_t, v0 * 9.0f) * (0.12f + 0.3f * wind);
		float mx = x + sw * h * 0.35f, my = gy - h * 0.55f;
		float tx = x + sw * h + 0.1f * h * (v0 - 0.5f), ty = gy - h;
		float v = 0.3f + 0.25f * v0;
		hline(c, x, gy, mx, my, v, US_MAT_LEAF);
		hline(c, mx, my, tx, ty, v, US_MAT_LEAF);
	}
	for (int i = 0; i < st->nplants; i++) {
		plant_t *p = &st->plant[i];
		float bx = p->x * A, by = soil_y(p->x);
		float h = p->h * (0.6f + 0.5f * grow_k);
		float sw = us_noise2(bx * 1.3f - st->sway_t * 0.5f, i * 3.3f) * (0.04f + 0.12f * wind);
		float mx = bx + sw * h * 0.3f, my = by - h * 0.5f;
		float tx = bx + sw * h, ty = by - h;
		if (crisp) {
			us_line(c, bx, by, mx, my, 0.006f, 0.55f, US_MAT_STEM);
			us_line(c, mx, my, tx, ty, 0.006f, 0.55f, US_MAT_STEM);
		} else {
			us_line(c, bx, by, mx, my, 0.002f, 0.6f, US_MAT_STEM);
			us_line(c, mx, my, tx, ty, 0.0018f, 0.6f, US_MAT_STEM);
		}
		for (int k = 1; k <= 4; k++) { /* leaves: pointed blades from the stem */
			float f = k / 5.5f;
			float lx = us_lerp(bx, tx, f), ly = us_lerp(by, ty, f);
			float side = (k & 1) ? 1.0f : -1.0f;
			float la = -US_PI * 0.5f + side * (1.0f + 0.15f * sinf(st->sway_t + p->leaf_ph + k));
			float ll = h * (0.36f - 0.05f * k);
			float ex = lx + cosf(la) * ll, ey = ly + sinf(la) * ll + ll * 0.25f;
			float nx = -sinf(la) * ll * 0.18f, ny = cosf(la) * ll * 0.18f;
			float qx = (lx + ex) * 0.5f, qy = (ly + ey) * 0.5f;
			if (crisp) {
				us_tri(c, lx, ly, qx + nx, qy + ny, ex, ey, 0.5f, US_MAT_LEAF);
				us_tri(c, lx, ly, qx - nx, qy - ny, ex, ey, 0.42f, US_MAT_LEAF);
			} else {
				wu_line(c, lx, ly, qx + nx, qy + ny, 0.5f, US_MAT_LEAF);
				wu_line(c, qx + nx, qy + ny, ex, ey, 0.5f, US_MAT_LEAF);
				wu_line(c, lx, ly, qx - nx, qy - ny, 0.42f, US_MAT_LEAF);
				wu_line(c, qx - nx, qy - ny, ex, ey, 0.42f, US_MAT_LEAF);
				wu_line(c, lx, ly, ex, ey, 0.25f, US_MAT_LEAF);
			}
		}
		/* flower: open in spring/summer, pale seed head in autumn, bare in winter */
		float fl = us_sat(1.0f - winter * 1.4f);
		if (fl > 0.05f) {
			float pr = (crisp ? 0.009f : 0.0075f) * fl;
			uint8_t fm = autumn > 0.55f ? US_MAT_ACCENT : US_MAT_BLOOM;
			for (int k = 0; k < 5; k++) {
				float an = k * (US_TAU / 5) + p->leaf_ph + 0.2f * sinf(st->sway_t * 0.7f + i);
				float ex = tx + cosf(an) * pr, ey = ty + sinf(an) * pr * 0.8f;
				if (crisp) {
					us_line(c, tx, ty, ex, ey, 0.006f, 0.75f, fm);
				} else {
					wu_line(c, tx, ty, ex, ey, 0.75f, fm);
					us_ring(c, (tx + ex) * 0.5f, (ty + ey) * 0.5f, pr * 0.32f, 0.0009f, 0.6f, fm);
				}
			}
			us_disc(c, tx, ty, crisp ? 0.004f : 0.0022f, 0.85f, US_MAT_LIGHT);
		}
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
	PROF_START;
	st->t += dt;
	st->frame_dt = dt;

	st->bass_env = us_approach(st->bass_env, us_sat(a->bass * 0.9f + a->sub * 0.4f), 0.6f, ctx->real_dt);
	float wind = us_sat(0.15f * w->wind + 0.9f * a->high + 0.4f * a->air);
	st->sway_t += dt * (0.3f + 1.4f * wind);
	float moist = w->moisture;
	/* one envelope per part of the music: each drives its own creature or response */
	{
		float rdt = ctx->real_dt;
		st->sub_env = us_approach(st->sub_env, a->sub, 0.25f, rdt);
		st->lm_env = us_approach(st->lm_env, a->lowmid, 0.3f, rdt);
		st->air_env = us_approach(st->air_env, a->air, 0.2f, rdt);
		st->swell_g = us_approach(st->swell_g, a->swell, 1.0f, rdt);
		st->cent_env = us_approach(st->cent_env, a->centroid, 0.8f, rdt);
		st->width_env = us_approach(st->width_env, a->width, 0.6f, rdt);
		float hb = 0.25f * (a->bands[26] + a->bands[27] + a->bands[28] + a->bands[29]);
		st->hi_fast = us_approach(st->hi_fast, hb, 0.02f, rdt);
		st->hi_slow = us_approach(st->hi_slow, hb, 0.3f, rdt);
		/* melody: each region of the network listens to its own band (fast rise, slow ease out) */
		for (int k = 0; k < NCOL; k++) {
			int b = 7 + k * 18 / (NCOL - 1);
			float tgt = 0.5f * (a->bands[b] + a->bands[b + 1]);
			st->colenv[k] = us_approach(st->colenv[k], tgt, tgt > st->colenv[k] ? 0.08f : 0.45f, rdt);
		}
		/* brightness of the music chooses the depth that glows: bright = shallow, dark = deep */
		float yc = us_lerp(0.8f, 0.4f, st->cent_env);
		for (int j = 0; j < GY; j++) {
			float q = ((j + 0.5f) / GY - yc) / 0.17f;
			st->depth_w[j] = expf(-q * q);
		}
	}
	st->hi_cool -= ctx->real_dt;
	int hi_hit = st->hi_fast - st->hi_slow > 0.04f && st->hi_fast > 0.25f && st->hi_cool <= 0;
	if (hi_hit)
		st->hi_cool = 0.1f;

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
		const float Y0 = SOIL0 + 0.03f, Y1 = 0.86f;
		int y0 = (int)(Y0 * GY), y1 = (int)(Y1 * GY);
		for (int cy = y0; cy < y1; cy++) {
			int ry = (cy - y0) * RY / (y1 - y0);
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
			float yy = Y0 + (ry + us_randr(r, 0.2f, 0.8f)) * ((Y1 - Y0) / RY);
			yy = fminf(fmaxf(yy, soil_y(xf) + 0.04f), hz_rock(xf) - 0.03f);
			if (!in_stone(st, xf * A, yy, A, 0.008f) && !in_tunnel(st, xf, yy, A, 0.008f))
				new_colony_at(st, xf, yy);
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
	if (kick_hit) {
		st->idle_pulse_t = us_randr(r, 3.0f, 7.0f);
		st->bob_v -= 9.0f * us_sat(0.5f + 0.5f * a->kick);
	}
	/* the caps' wobble: a damped spring (~3.5 Hz) knocked by each kick, gone in ~0.4 s */
	for (int k = 0; k < 2; k++) {
		float h = 0.5f * fminf(ctx->real_dt, 0.05f), om = 22.0f;
		st->bob_v += (-om * om * st->bob_x - 2.0f * 0.3f * om * st->bob_v) * h;
		st->bob_x += st->bob_v * h;
	}
	for (int k = 0; k < st->npulses;) {
		if ((st->t - st->pulse[k].t0) * PULSE_V > 3.0f) {
			st->pulse[k] = st->pulse[--st->npulses];
			continue;
		}
		k++;
	}

	PROF(0);
	/* ---- the world around the network ---- */
	if (!ctx->crisp)
		settle_mats(c, US_MAT_ACCENT, st->fcount_m++);
	draw_ground(st, ctx, c, wind);
	draw_critters(st, ctx, c, kick_hit, hi_hit);

	PROF(7);
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
			{ /* melody: the region's own band warms it, at the depth the centroid picks */
				float fx = n->x * (NCOL - 1);
				int ci = (int)fx;
				ci = ci < 0 ? 0 : (ci > NCOL - 2 ? NCOL - 2 : ci);
				float mel = us_lerp(st->colenv[ci], st->colenv[ci + 1], fx - ci);
				int gj = (int)(n->y * GY);
				mel *= st->depth_w[gj < 0 ? 0 : (gj >= GY ? GY - 1 : gj)];
				v += 0.4f * mel * mel * (0.6f + 0.4f * cord);
			}
			/* pale threads; pulses light them gold (pixel styles keep the thread's own ramp: a LIGHT mat
			 * would linger through the trails) */
			uint8_t m = g > 0.5f && !ctx->crisp ? US_MAT_LIGHT : US_MAT_ACCENT;
			if (ctx->crisp && v < 0.45f)
				m = US_MAT_STEM; /* pixel: older threads sink into a darker ramp, fresh ones stay pale */
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
				px_line(c, na->x * A, na->y, nb->x * A, nb->y, us_sat(v), US_MAT_ACCENT);
			else
				wu_line(c, na->x * A, na->y, nb->x * A, nb->y, us_sat(v), g > 0.5f ? US_MAT_LIGHT : US_MAT_ACCENT);
		}
		for (int k = 0; k < 256; k++)
			st->dmin[k] = st->dmin_next[k] < 1e8f ? st->dmin_next[k] : 0.0f;
		/* growing tips: bioluminescent — each breathes on its own in quiet, and the phrase swell
		 * gathers a soft halo of foxfire round them */
		float halo = us_smooth(0.15f, 0.8f, st->swell_g);
		for (int k = 0; k < st->ntips; k++) {
			node_t *n = &st->nd[st->tip[k].node];
			float breath = 0.5f + 0.5f * sinf(st->t * (0.7f + 0.05f * (k & 7)) + k * 2.39f);
			float tv = us_sat(0.5f + 0.3f * st->bass_env + 0.2f * breath);
			us_point(c, n->x * A, n->y, ctx->crisp ? 0.002f : 0.0016f, tv, US_MAT_LIGHT);
			if (!ctx->crisp && halo > 0.02f)
				us_ring(c, n->x * A, n->y, 0.0035f + 0.003f * halo * breath, 0.0012f, 0.22f * halo * (0.5f + 0.5f * breath),
					US_MAT_LIGHT);
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

	PROF(8);
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
				float h0 = kind == 0 ? us_randr(r, 0.1f, 0.16f) : kind == 1 ? us_randr(r, 0.075f, 0.12f)
							       : us_randr(r, 0.065f, 0.1f);
				for (int sbi = 0; sbi <= sib && mi >= 0; sbi++) {
					mush_t *mu = &st->mush[mi];
					memset(mu, 0, sizeof(*mu));
					mu->alive = 1;
					mu->kind = kind;
					mu->x = us_clampf(n->x + (sbi ? us_randr(r, -0.03f, 0.03f) : 0.0f), 0.01f, 0.99f);
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
			{ /* each cap listens to its own band (mushrooms spread over the spectrum, low to high) */
				float tb = a->bands[3 + (m * 19) % 26];
				mu->benv = us_approach(mu->benv, tb, tb > mu->benv ? 0.07f : 0.4f, ctx->real_dt);
			}
			float g = mu->g;
			float gm = g - 1.0f;
			float pop = 1.0f + 2.70158f * gm * gm * gm + 1.70158f * gm * gm; /* ease-out-back: overshoot */
			float bx = mu->x * A, by = soil_y(mu->x);
			/* kicks: the soft caps squash and spring back (smaller ones wobble more) */
			float bob = st->bob_x * (0.13f + 0.05f * (0.12f - fminf(mu->h, 0.12f)) / 0.06f) * us_sat(g * 2.0f - 1.0f);
			float h = mu->h * pop * (1.0f - 0.45f * wilt) * (1.0f + bob);
			float lean = mu->lean + wilt * 0.35f;
			float tx = bx + sinf(lean) * h, ty = by - cosf(lean) * h;
			float vv = us_sat((0.68f + 0.28f * mu->glow + 0.3f * mu->benv) * (1.0f - 0.6f * wilt));
			/* rhizomorph: the cord down to the feeding thread */
			if (fed) { /* a gently sagging cord, not a ruler line */
				float ex = fn->x * A, ey = fn->y, cv = 0.45f + 0.5f * mu->glow;
				uint8_t cm = mu->glow > 0.4f ? US_MAT_LIGHT : US_MAT_ACCENT;
				float qx = bx, qy = by, sag = 0.25f * (ex - bx);
				for (int k = 1; k <= 4; k++) {
					float u = k / 4.0f;
					float px = us_lerp(bx, ex, u) + sag * sinf(u * US_PI), py = us_lerp(by, ey, u);
					if (ctx->crisp)
						hline(c, qx, qy, px, py, cv, cm);
					else
						wu_line(c, qx, qy, px, py, cv, cm);
					qx = px, qy = py;
				}
			}
			/* stipe: in Ink an outlined column (two edges), in pixel a solid one */
			float sw = (mu->kind == 2 ? 0.0055f : mu->kind == 0 ? 0.0038f : 0.0045f) * (0.6f + 0.4f * pop) * (mu->h / 0.12f);
			if (ctx->crisp) {
				us_line(c, bx, by, tx, ty, fmaxf(sw * 1.4f, 0.006f), vv * 0.85f, US_MAT_ACCENT);
			} else {
				float ox = cosf(lean) * sw * 0.5f, oy = sinf(lean) * sw * 0.5f;
				wu_line(c, bx - ox * 1.2f, by - oy, tx - ox, ty - oy, vv * 0.85f, US_MAT_ACCENT);
				wu_line(c, bx + ox * 1.2f, by + oy, tx + ox, ty + oy, vv * 0.7f, US_MAT_ACCENT);
			}
			if (mu->kind == 2)
				us_disc(c, bx, by - 0.004f, ctx->crisp ? 0.0065f : 0.005f * (mu->h / 0.08f), vv * 0.8f, US_MAT_ACCENT); /* volva */
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
			cw *= 1.0f - 1.6f * bob, ch *= 1.0f + 1.8f * bob; /* squash: wider and flatter, then back */
			/* parasol: brown (EARTH); inkcap: white (ACCENT); fly agaric: red (BLOOM) with pale spots */
			uint8_t capm = mu->kind == 0 ? US_MAT_EARTH : mu->kind == 1 ? US_MAT_ACCENT : US_MAT_BLOOM;
			if (mu->kind == 0)
				vv = us_sat(vv * 1.25f);
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
					us_tri(c, tx, ty, pts[2 * k], pts[2 * k + 1], pts[2 * k + 2], pts[2 * k + 3],
					       mu->kind == 0 ? us_sat(vv * 1.3f) : vv, capm);
			} else {
				for (int k = 0; k + 1 < np; k++)
					wu_line(c, pts[2 * k], pts[2 * k + 1], pts[2 * k + 2], pts[2 * k + 3], vv, capm);
				wu_line(c, pts[0], pts[1], pts[2 * np - 2], pts[2 * np - 1], vv * 0.7f, capm);
				/* a second, inner shell line gives the cap body */
				for (int k = 2; k + 3 < np; k++) {
					float ax = us_lerp(tx, pts[2 * k], 0.8f), ay = us_lerp(ty, pts[2 * k + 1], 0.8f);
					float bx2 = us_lerp(tx, pts[2 * k + 2], 0.8f), by2 = us_lerp(ty, pts[2 * k + 3], 0.8f);
					wu_line(c, ax, ay, bx2, by2, vv * 0.4f, capm);
				}
				/* gills: fine radiating lines under the cap, lit by arriving pulses */
				float gv = us_sat(vv * (0.3f + 0.6f * mu->glow + 0.7f * mu->benv));
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
						us_put(c, (int)(sx * c->s), (int)(sy * c->s), vv, US_MAT_ACCENT);
					else
						us_ring(c, sx, sy, (0.0012f + 0.0008f * open) * (mu->h / 0.08f), 0.0009f, vv, US_MAT_ACCENT);
				}
			} else if (mu->kind == 0 && !ctx->crisp && open > 0.3f) { /* ring on the stipe */
				float rx = bx + (tx - bx) * 0.72f, ry = by + (ty - by) * 0.72f;
				us_line(c, rx - 0.007f, ry + 0.003f, rx + 0.007f, ry + 0.003f, 0.0009f, vv * 0.7f, US_MAT_ACCENT);
			}
			/* spore puffs: when a pulse arrives through the cord, or on hits from mature caps */
			mu->puff -= dt;
			int arrive = mu->glow > 0.45f;
			/* air (the sizzle above the hats) lets a fine haze of spores drift from open gills */
			if (open > 0.7f && wilt <= 0 && us_rand(r) < dt * 2.2f * st->air_env * st->air_env)
				mu->emit++;
			if ((arrive || a->beat) && open > 0.7f && wilt <= 0 && mu->puff <= 0 &&
			    (arrive || us_rand(r) < 0.3f + 0.5f * a->onset)) {
				mu->puff = us_randr(r, 0.8f, 2.0f);
				mu->emit += 2 + (int)(4.0f * mu->glow);
			}
			/* spores drop out of the gills one at a time, then the air takes them */
			mu->emit_t -= dt;
			if (mu->emit > 0 && mu->emit_t <= 0 && st->nspores < MAX_SPORES) {
				spore_t *sp2 = &st->spore[st->nspores++];
				float u = us_randr(r, -0.85f, 0.85f);
				sp2->x = tx + u * cw * cosf(lean);
				sp2->y = ty + u * cw * sinf(lean) + us_randr(r, 0.0f, 0.004f);
				sp2->vx = us_randn(r) * 0.008f * (0.6f + 1.2f * st->width_env) + 0.004f * wind; /* wide mixes spread the cloud */
				sp2->vy = us_randr(r, -0.004f, 0.01f);
				sp2->life = 1.0f;
				sp2->ph = us_rand(r) * US_TAU;
				mu->emit--;
				mu->emit_t = us_randr(r, 0.04f, 0.16f);
			}
		}
		for (int k = 0; k < st->nspores;) {
			spore_t *s = &st->spore[k];
			/* warm air lifts them; a slow curl field makes each wander its own way */
			float n1 = us_noise3(s->x * 7.0f, s->y * 7.0f, st->t * 0.2f), n2 = us_noise3(s->x * 7.0f + 9.1f, s->y * 7.0f, st->t * 0.2f);
			/* plus a private random walk: spores leaving the same gill one after another would otherwise
			 * ride the same streamline and read as a dotted streak */
			float jit = 0.0025f * sqrtf(dt * 30.0f);
			s->vx += (n1 * 0.006f + 0.003f * wind) * dt + us_randn(r) * jit;
			s->vy += (n2 * 0.006f - 0.004f) * dt + us_randn(r) * jit * 0.8f;
			s->vx *= expf(-dt * 0.6f);
			s->vy *= expf(-dt * 0.6f);
			s->x += s->vx * dt;
			s->y += s->vy * dt;
			s->life -= dt * 0.16f;
			if (s->life <= 0 || s->y < -0.02f || s->x < -0.02f || s->x > A + 0.02f) {
				*s = st->spore[--st->nspores];
				continue;
			}
			float tw = 0.65f + 0.35f * sinf(st->t * 2.3f + s->ph);
			float v = 0.55f * us_smooth(0.0f, 0.25f, s->life) * tw;
			if (ctx->crisp)
				put_max(c, (int)(s->x * c->s), (int)(s->y * c->s), v, US_MAT_ACCENT);
			else
				us_point(c, s->x, s->y, 0.0012f, v, US_MAT_ACCENT);
			k++;
		}
	}
	PROF(9);
	PROF_END;
}

const us_scene_vt us_scene_mycelium = {"mycelium", "Mycelium", 0.3f, create, destroy, frame};
