/* Understory scene: Storm Front — a storm rolls in over open farmland; the drums are its weather.
 *
 * The sky is built from billows: three cloud decks at different depths (a low, heavy near deck at the
 * top of the frame, a middle bank below it, a far bank sitting on the horizon) plus a towering
 * thunderhead on the horizon. Each deck is rows of overlapping puffs grouped into masses by a drifting
 * noise field; the decks drift at parallax speeds, nearer ones faster.
 *   Ink:   each billow is drawn as the part of its outline not hidden by billows in front of it, so the
 *          masses read as rounded, overlapping cauliflower forms; the near deck's underside is the
 *          brightest edge, the folds above it fade into the deck.
 *   Crisp: billows are painted back to front, each shaded (lit crown, dark underside, dithered), so the
 *          masses read as solid volumes with lit edges.
 * Under the decks: rain curtains hang from the heaviest masses; far fields with a telegraph line, birds
 * on the wire and a farmhouse; a near ridge with a lone oak in the foreground.
 *
 * Reactivity (each drum has its own answer):
 *   kick  = the decks heave: every mass drops and swells for a moment (heavier masses more), the rain
 *           gusts (longer, brighter, more slanted streaks) and a gust front runs through the wires/grass/oak.
 *   snare = lightning inside the clouds: a sheet flash lights the billows around it from within, showing
 *           the deck's folds; when the storm is built up, some snares fire a forked bolt to the ground.
 *   hats  = the rain sparkles (drops glint) and splashes crown on the fields.
 *   loudness (eased) builds the storm: decks close and lower, masses thicken, more rain, more inner light;
 *           quiet passages break the decks up, the rain thins and stars show through the gaps.
 *   sub = a slow roll along the near deck's base; mids = a soft light wandering inside the deck;
 *   other transients (bells, plucks) = distant heat lightning inside the thunderhead; swell + charge
 *   tower the thunderhead and spread its anvil; width = where bolts strike.
 * World: reads storm_charge, moisture, wind, daylight, season; writes storm_charge (up with swell, down
 *        with every strike) and moisture (rain).
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define GW 112      /* column bins for the deck underside (rain + strike origins) */
#define MAX_SEG 900
#define MAX_DROP 1400
#define MAX_TREES 7
#define MAX_TSEG 120
#define MAX_STROKES 4
#define MAX_PUFF 900
#define NBIN 48
#define BINCAP 128
#define MAX_SPLASH 64
#define MASK_MAX (640 * 400) /* crisp canvases up to this size paint front to back, each pixel once */
#define NPOLE 6    /* telegraph poles on the far fields */
#define NBIRD 9    /* birds on the wire */
#define HZ 0.835f  /* mean horizon height */

enum { LY_TOWER = 0, LY_FAR, LY_MID, LY_NEAR };

typedef struct {
	float x0, y0, x1, y1, v, w;
} seg_t;

typedef struct {
	float x, y, vx, vy, len, v; /* x as fraction of aspect */
} drop_t;

typedef struct {
	float x, h;
	int nseg;
	float sx0[MAX_TSEG], sy0[MAX_TSEG], sx1[MAX_TSEG], sy1[MAX_TSEG], sw[MAX_TSEG];
} tree_t;

typedef struct {
	int state;   /* 0 perched, 1 flying off, 2 away, 3 gliding home */
	float p;     /* 0 on the wire .. 1 gone (negative = about to go) */
	float rest, phase;
	int span;
	float f;     /* position along the span 0..1 */
	float dx, dy; /* where it flies to (relative) */
} bird_t;

typedef struct {
	float x, y, r; /* centre + radius, normalized units */
	float v;       /* base brightness */
	float m;       /* mass weight 0..1 (how heavy the cloud is here) */
	float h;       /* height above its deck's base row (0 = underside row) */
	uint8_t layer;
	uint8_t occ;   /* 1: a core, painted but never outlined */
} puff_t;

typedef struct {
	float x, y, t;
} splash_t;

typedef struct {
	us_rng rng;
	float col[GW];   /* cloud weight at the deck's underside per column (rain source) */
	float colb[GW];  /* y of the underside per column (0 = clear sky) */

	/* the billows, back to front, binned by x for occlusion tests */
	puff_t puff[MAX_PUFF];
	int npuff;
	uint16_t bin[NBIN][BINCAP];
	int nbin[NBIN];
	float bin_k; /* bins per normalized x */

	/* lightning */
	seg_t seg[MAX_SEG];
	int nseg;
	float bolt_t;    /* time since the strike */
	float stroke_t[MAX_STROKES];
	int nstrokes;
	float bolt_x, bolt_y; /* where the channel touches (for cloud lighting) */
	float bolt_amp;  /* 1 = cloud-to-ground, ~0.6 crawler */
	float cool;
	float flash;     /* current flash brightness 0..1+ */

	/* snare: sheet lightning inside the deck */
	float sf, sf_t, sf_amp, sf_x, sf_y;

	/* heat lightning (distant, in the thunderhead) */
	float heat_next, heat_t, heat_x, heat_y, heat_v, heat_amp, small_cool;

	drop_t drop[MAX_DROP];
	int ndrop;
	splash_t splash[MAX_SPLASH];
	int nsplash;
	uint8_t mask[MASK_MAX];

	tree_t tree[MAX_TREES];
	int ntrees;
	tree_t oak;   /* the big tree on the near ridge */
	float ridge_side; /* +1: the ridge rises on the right */
	float hit_x;  /* where the last ground strike landed */

	float drift, evo;
	float A; /* aspect cached for the recursive bolt builder */
	float ch_vis;  /* smoothed charge used for drawing */
	float rain;    /* smoothed rain intensity */
	float build;   /* slowly eased loudness: how built-up the storm is */
	float lum;     /* faster eased loudness: light inside the storm, rain */
	float deck_y;  /* the near deck's base */
	int inited;

	/* the distant thunderhead on the horizon */
	float tower_u, tower_h, tower_rise;
	float anv_x0, anv_x1, anv_y, anv_th; /* the anvil's extent (ink draws it as strokes) */
	int anv_n;                           /* its billows come first in the list */

	/* audio envelopes, each with its own meaning */
	float rumble;  /* sub: a slow roll along the deck base */
	float heave;   /* kick: the decks drop + swell, the rain gusts */
	float glint;   /* hats: rain sparkle */
	float gust;    /* kick: gust front through the grass + oak */
	float gust_x, gust_amp;
	float glow_a, glow_v, glow_x, glow_y; /* mids: light wandering inside the deck */
	float swellv;

	/* far fields: telegraph line with birds, a farmhouse */
	float pole_u[NPOLE];
	bird_t bird[NBIRD];
	float barn_u, power_out;
} st_t;

static float horizon_y(float u)
{
	/* u = x fraction 0..1 */
	return 0.835f + 0.018f * us_noise2(u * 2.3f, 7.7f) + 0.007f * us_noise2(u * 9.0f, 3.1f);
}

/* ---- trees on the horizon (tiny static silhouettes, they give scale) ---- */
static void tree_branch_d(tree_t *t, us_rng *r, float x, float y, float ang, float len, float w, int depth, int maxd)
{
	if (t->nseg >= MAX_TSEG || depth > maxd || len < 0.003f)
		return;
	float ex = x + cosf(ang) * len, ey = y + sinf(ang) * len;
	int i = t->nseg++;
	t->sx0[i] = x;
	t->sy0[i] = y;
	t->sx1[i] = ex;
	t->sy1[i] = ey;
	t->sw[i] = w;
	int kids = depth == 0 ? 3 : 2;
	for (int k = 0; k < kids; k++) {
		float a = ang + us_randr(r, -0.75f, 0.75f) + (k - (kids - 1) * 0.5f) * 0.45f;
		tree_branch_d(t, r, ex, ey, a, len * us_randr(r, 0.55f, 0.78f), w * 0.65f, depth + 1, maxd);
	}
}

static void tree_branch(tree_t *t, us_rng *r, float x, float y, float ang, float len, float w, int depth)
{
	tree_branch_d(t, r, x, y, ang, len, w, depth, 4);
}

/* the near ridge: a low rise toward one side of the frame */
static float ridge_y(float u, float side)
{
	float hill = us_smooth(0.3f, 0.92f, side > 0 ? u : 1.0f - u);
	return 0.975f - 0.15f * hill + 0.012f * us_noise2(u * 4.0f, 2.7f) + 0.004f * us_noise2(u * 17.0f, 1.3f);
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

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x73746f72u);
	s->ntrees = 2 + (int)(us_rand(&s->rng) * 4);
	for (int i = 0; i < s->ntrees; i++) {
		tree_t *t = &s->tree[i];
		t->x = us_randr(&s->rng, 0.05f, 0.95f);
		t->h = us_randr(&s->rng, 0.025f, 0.05f);
		tree_branch(t, &s->rng, 0, 0, -US_PI * 0.5f + us_randn(&s->rng) * 0.05f, t->h * 0.45f, 1.0f, 0);
	}
	s->ridge_side = us_rand(&s->rng) < 0.5f ? -1.0f : 1.0f;
	tree_branch_d(&s->oak, &s->rng, 0, 0, -US_PI * 0.5f + us_randn(&s->rng) * 0.06f, 0.085f, 1.0f, 0, 5);
	s->heat_amp = 0.7f;
	s->heat_next = us_randr(&s->rng, 2.0f, 6.0f);
	s->evo = us_rand(&s->rng) * 100.0f;
	s->drift = us_rand(&s->rng) * 50.0f;
	/* far fields: a telegraph line running away from the ridge, a farmhouse at its end */
	{
		float sd = s->ridge_side;
		float u0 = 0.05f + us_rand(&s->rng) * 0.05f, du = (0.56f - u0) / (NPOLE - 1);
		for (int i = 0; i < NPOLE; i++) {
			float u = u0 + du * i + us_randn(&s->rng) * 0.006f;
			s->pole_u[i] = sd > 0 ? u : 1.0f - u;
		}
		s->barn_u = sd > 0 ? 0.64f + us_rand(&s->rng) * 0.05f : 0.36f - us_rand(&s->rng) * 0.05f;
		s->tower_u = sd > 0 ? us_randr(&s->rng, 0.28f, 0.48f) : us_randr(&s->rng, 0.52f, 0.72f);
		for (int k = 0; k < NBIRD; k++) {
			bird_t *b = &s->bird[k];
			b->span = (int)(us_rand(&s->rng) * (NPOLE - 1));
			b->f = us_randr(&s->rng, 0.15f, 0.85f);
			b->phase = us_rand(&s->rng) * US_TAU;
			b->dx = us_randr(&s->rng, -0.5f, 0.3f);
			b->dy = us_randr(&s->rng, -0.42f, -0.22f);
		}
	}
	s->tower_h = 0.3f;
	s->sf_t = 9.0f;
	s->heat_t = 9.0f;
	s->gust_x = 9.0f;
	return s;
}

static void destroy(void *p) { free(p); }

/* ---- lightning ------------------------------------------------------- */
static void add_seg(st_t *s, float x0, float y0, float x1, float y1, float v, float w)
{
	if (s->nseg >= MAX_SEG)
		return;
	seg_t *g = &s->seg[s->nseg++];
	g->x0 = x0;
	g->y0 = y0;
	g->x1 = x1;
	g->y1 = y1;
	g->v = v;
	g->w = w;
}

static void bolt(st_t *s, float x0, float y0, float x1, float y1, float disp, int depth, float v, float w, float fork_p)
{
	if (depth <= 0) {
		add_seg(s, x0, y0, x1, y1, v, w);
		return;
	}
	float dx = x1 - x0, dy = y1 - y0;
	float len = sqrtf(dx * dx + dy * dy) + 1e-6f;
	float nx = -dy / len, ny = dx / len;
	float o = us_randn(&s->rng) * disp;
	float mx = (x0 + x1) * 0.5f + nx * o, my = (y0 + y1) * 0.5f + ny * o;
	bolt(s, x0, y0, mx, my, disp * 0.5f, depth - 1, v, w, fork_p);
	bolt(s, mx, my, x1, y1, disp * 0.5f, depth - 1, v, w, fork_p);
	/* forks peel off the midpoints, angled away from the main channel and drooping */
	if (depth >= 2 && us_rand(&s->rng) < fork_p) {
		float ang = atan2f(dy, dx) + (us_rand(&s->rng) < 0.5f ? -1.0f : 1.0f) * us_randr(&s->rng, 0.35f, 0.9f);
		float fl = len * us_randr(&s->rng, 0.45f, 0.9f);
		float fx = mx + cosf(ang) * fl, fy = my + sinf(ang) * fl;
		float lim = horizon_y(us_sat(fx / s->A)) - 0.004f;
		if (fy > lim)
			fy = lim;
		if (fy < my - 0.05f)
			fy = my - 0.05f;
		bolt(s, mx, my, fx, fy, fl * 0.22f, depth - 1, v * 0.55f, w * 0.6f, fork_p * 0.75f);
	}
}

static void strike(st_t *s, float A, int ground, float dens, float energy, float width)
{
	us_rng *r = &s->rng;
	s->nseg = 0;
	s->A = A;
	/* stereo width picks where: a narrow mix strikes near the middle, a wide one out on the flanks.
	 * Start under the heaviest column found from a few tries around that spot. */
	float ut = 0.5f + (us_rand(r) < 0.5f ? -1.0f : 1.0f) * us_sat(width) * 0.4f;
	int best = -1;
	for (int k = 0; k < 7; k++) {
		int i = (int)(us_sat(ut + us_randn(r) * 0.1f) * (GW - 1));
		if (best < 0 || s->col[i] > s->col[best])
			best = i;
	}
	float u = (best + 0.5f) / GW;
	float x0 = u * A, y0 = s->colb[best] - us_randr(r, 0.02f, 0.08f);
	float fork = 0.25f + 0.35f * dens + 0.15f * energy;
	if (ground) {
		float x1 = x0 + us_randn(r) * 0.12f;
		float u1 = us_sat(x1 / A);
		float y1 = horizon_y(u1);
		/* trees attract the strike */
		for (int i = 0; i < s->ntrees; i++)
			if (fabsf(s->tree[i].x * A - x1) < 0.06f) {
				x1 = s->tree[i].x * A;
				y1 = horizon_y(s->tree[i].x) - s->tree[i].h;
			}
		bolt(s, x0, y0, x1, y1, 0.16f * (y1 - y0 + 0.1f), 7, 1.0f, 1.0f, fork);
		s->hit_x = x1;
		s->bolt_x = x0;
		s->bolt_y = y0;
		s->bolt_amp = 1.0f;
	} else {
		/* cloud-to-cloud crawler: long, mostly horizontal, inside the deck */
		float dir = us_rand(r) < 0.5f ? -1.0f : 1.0f;
		float x1 = x0 + dir * us_randr(r, 0.3f, 0.7f);
		float y1 = y0 - us_randr(r, -0.05f, 0.1f);
		bolt(s, x0, y0, x1, y1, 0.07f, 6, 0.75f, 0.7f, fork * 1.3f);
		s->bolt_x = (x0 + x1) * 0.5f;
		s->bolt_y = y0;
		s->bolt_amp = 0.65f;
		s->hit_x = s->bolt_x;
	}
	s->bolt_t = 0;
	s->nstrokes = 1 + (int)(us_rand(r) * (ground ? 3.5f : 2.0f));
	if (s->nstrokes > MAX_STROKES)
		s->nstrokes = MAX_STROKES;
	s->stroke_t[0] = 0;
	for (int k = 1; k < s->nstrokes; k++)
		s->stroke_t[k] = s->stroke_t[k - 1] + us_randr(r, 0.05f, 0.14f);
}

/* ---- light inside the sky: the bolt's flash, the snare's sheet flash, heat lightning, the mids' glow ---- */
static float lit(const st_t *s, float x, float y)
{
	float L = 0;
	if (s->flash > 0.001f) {
		float dx = x - s->bolt_x;
		float under = 0.45f + 0.55f * us_smooth(s->deck_y - 0.3f, s->deck_y + 0.05f, y);
		L += s->flash * s->bolt_amp * (0.3f + 1.2f * expf(-dx * dx / 0.12f)) * under;
	}
	if (s->sf > 0.003f) { /* sheet lightning: a wide, flat glow inside the deck */
		float dx = x - s->sf_x, dy = y - s->sf_y;
		float d2 = dx * dx * 5.0f + dy * dy * 20.0f;
		if (d2 < 7.0f)
			L += s->sf * expf(-d2);
	}
	if (s->heat_v > 0.001f) {
		float dx = x - s->heat_x, dy = (y - s->heat_y) * 1.6f;
		float d2 = dx * dx + dy * dy;
		if (d2 < 0.2f)
			L += s->heat_v * expf(-d2 / 0.025f);
	}
	if (s->glow_a > 0.004f) { /* the melody: a soft light wandering inside the deck */
		float dx = x - s->glow_x, dy = (y - s->glow_y) * 1.5f, d2 = dx * dx + dy * dy;
		if (d2 < 0.12f) {
			float g = 1.0f - d2 / 0.12f;
			L += s->glow_a * g * g * g;
		}
	}
	return L;
}

/* ---- billows ---------------------------------------------------------- */
static uint32_t hash3(int a, int b, int c)
{
	uint32_t h = (uint32_t)a * 0x9E3779B1u ^ (uint32_t)(b + 17) * 0x85EBCA77u ^ (uint32_t)(c + 101) * 0xC2B2AE3Du;
	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 12;
	h *= 0x297A2D39u;
	h ^= h >> 15;
	return h;
}

static float hf(uint32_t h, int k) { return ((h >> (k * 10)) & 1023) / 1023.0f; }

static void add_puff(st_t *s, float x, float y, float r, float v, float m, float hgt, int layer, int occ)
{
	if (s->npuff >= MAX_PUFF || !(r > 0.004f))
		return;
	if (r > 0.2f)
		r = 0.2f;
	puff_t *p = &s->puff[s->npuff++];
	p->x = x;
	p->y = y;
	p->r = r;
	p->v = v;
	p->m = m;
	p->h = hgt;
	p->layer = (uint8_t)layer;
	p->occ = (uint8_t)occ;
}

typedef struct {
	float yb;      /* base (underside) of the deck */
	float r0, sp;  /* billow radius, spacing */
	float rowstep; /* rows climb by this much */
	float speed;   /* parallax drift factor */
	float v0;      /* brightness */
	float fq;      /* mass-field frequency (masses are ~1/fq wide) */
	float heave;   /* how far a kick drops it */
	int nrows;
} deck_t;

/* one deck: rows of billows (the top rows only where the mass is thick, so thin masses dome over),
 * grouped into masses by a drifting noise field; coverage thins it into separate clouds */
static void gen_deck(st_t *s, float A, int L, const deck_t *d, float cov, float t)
{
	float off = s->drift * d->speed;
	int n0 = (int)floorf((off - 0.25f) / d->sp) - 1, n1 = (int)ceilf((off + A + 0.25f) / d->sp) + 1;
	float thr = 0.62f - 0.95f * cov;
	for (int row = d->nrows - 1; row >= 0; row--)
		for (int n = n0; n <= n1; n++) {
			uint32_t h = hash3(n, row, L);
			float xw = (n + 0.5f * (row & 1) + 0.85f * (hf(h, 0) - 0.5f)) * d->sp;
			float mf = 0.5f + 0.5f * us_fbm2(xw * d->fq + L * 13.1f, s->evo * 0.3f + L * 3.3f, 2);
			float tr = thr + 0.07f * row;
			float m = us_smooth(tr, tr + 0.28f, mf);
			if (m < 0.06f)
				continue;
			float x = xw - off;
			float boil = 1.0f + 0.12f * us_noise2(n * 0.71f + row * 3.1f, s->evo * 1.5f + L);
			/* masses differ in scale: big rolling heads beside smaller, tighter ones */
			float sc = 0.7f + 0.55f * (0.5f + 0.5f * us_noise2(xw * d->fq * 0.55f + 4.0f * L, 9.3f + s->evo * 0.1f));
			float r = d->r0 * sc * (0.55f + 0.6f * hf(h, 1)) * (0.4f + 0.6f * m) * boil;
			if (L == LY_NEAR)
				r *= 1.0f + 0.16f * s->heave * m; /* the kick: masses swell as they drop */
			if (x < -r || x > A + r)
				continue;
			float y = d->yb - 0.8f * r - row * d->rowstep * sc + d->rowstep * 0.9f * (hf(h, 2) - 0.5f);
			y += d->heave * s->heave * (0.4f + 0.6f * m); /* heavier masses heave further */
			if (row == 0) {
				/* heavy masses hang lower; the base rolls */
				y += (0.05f * m + 0.03f * us_noise2(xw * d->fq * 1.7f, 2.1f + L)) * d->rowstep / 0.06f;
				if (L == LY_NEAR) /* sub: the base sags, heavier where the bass sits */
					y += s->rumble * 0.014f * (0.5f + 0.5f * sinf(xw * 4.0f + 1.3f)); /* sags where the bass sits */
			}
			add_puff(s, x, y, r, d->v0 * (0.75f + 0.25f * m), m, (float)row, L, 0);
		}
}

/* the distant thunderhead: tiers of billows stacked into a tower (broad foot, a waist, a swelling crown)
 * and, as the storm matures, an anvil streaming downwind (left) from its top. Outline-less cores fill
 * each tier, so in Ink only the tower's lumpy silhouette shows. Its billows boil slowly. */
static void gen_tower(st_t *s, float A)
{
	float H = s->tower_h, cx = s->tower_u * A;
	float W = 0.05f + 0.015f * s->ch_vis + 0.015f * s->swellv;
	float anv = 0.35f + 0.65f * us_smooth(0.15f, 0.7f, s->ch_vis + 0.6f * s->swellv);
	float boil = s->evo * 2.0f + s->tower_rise * 8.0f;
	float ybase = HZ + 0.01f, ytop = HZ - H;
	/* the anvil, behind the crown */
	if (anv > 0.05f) {
		float x0 = cx + W * (0.9f + 0.6f * anv), x1 = cx - W * (1.4f + 3.4f * anv);
		float th = 0.02f + 0.025f * anv;
		s->anv_x0 = x0;
		s->anv_x1 = x1;
		s->anv_y = ytop - 0.01f;
		s->anv_th = th;
		int n = (int)((x0 - x1) / 0.02f) + 1;
		if (n > 40)
			n = 40;
		for (int k = 0; k < n; k++) {
			float f = (float)k / (n > 1 ? n - 1 : 1), x = us_lerp(x0, x1, f);
			uint32_t h = hash3(k, 9, 78);
			float tk = th * (1.0f - 0.6f * f);
			float r = (0.012f + 0.01f * hf(h, 1)) * (1.0f - 0.4f * f) * (1.0f + 0.15f * us_noise2(k * 0.7f, boil));
			add_puff(s, x, ytop - 0.01f + r * 0.9f, r, 0.32f, 0.6f, 9.0f, LY_TOWER, 0);
			add_puff(s, x + 0.006f, ytop - 0.01f + tk + 0.004f * (hf(h, 2) - 0.5f), r * 0.9f, 0.3f, 0.6f, 9.0f, LY_TOWER, 0);
			if ((k & 1) == 0)
				add_puff(s, x, ytop - 0.01f + tk * 0.55f, tk * 0.75f + 0.006f, 0.3f, 0.6f, 9.0f, LY_TOWER, 1);
		}
	}
	s->anv_n = s->npuff;
	/* tiers, top first (lower tiers sit in front) */
	enum { NT = 7 };
	float cy_[NT], hw_[NT], r_[NT];
	for (int k = NT - 1; k >= 0; k--) {
		float q = (float)k / (NT - 1);
		float hw = W * (0.9f + 0.4f * q * q - 0.3f * us_smooth(0.85f, 1.0f, q));
		float r = (0.026f + 0.016f * q) * (1.0f + 0.1f * us_noise2(k * 1.7f, boil));
		float y = ybase - r * 0.6f - q * (H - r * 1.6f);
		cy_[k] = y;
		hw_[k] = hw;
		r_[k] = r;
		int n = (int)(2.0f * hw / (1.15f * r)) + 1;
		if (n < 2)
			n = 2;
		if (n > 9)
			n = 9;
		for (int j = 0; j < n; j++) {
			uint32_t h = hash3(j, k, 77);
			float f = (float)j / (n - 1);
			float rr = r * (0.8f + 0.4f * hf(h, 1)) * (1.0f + 0.15f * us_noise2(j * 0.9f + k * 3.1f, boil));
			float x = cx - hw + rr * 0.5f + f * (2.0f * hw - rr) + 0.25f * r * (hf(h, 0) - 0.5f);
			add_puff(s, x, y + 0.3f * r * (hf(h, 2) - 0.5f), rr, 0.5f, 0.7f, (float)k, LY_TOWER, 0);
		}
	}
	/* cores (no outline): hide the inner edges, leave the silhouette */
	for (int k = 0; k < NT; k++) {
		float rc = r_[k] * 0.9f, span = hw_[k] - r_[k] * 0.9f;
		int n = span > 0.0f ? (int)(2.0f * span / rc) + 1 : 1;
		if (n > 8)
			n = 8;
		for (int j = 0; j < n; j++) {
			float x = n > 1 ? cx - span + 2.0f * span * j / (n - 1) : cx;
			add_puff(s, x, cy_[k] + 0.25f * rc, rc, 0.34f, 0.7f, (float)k, LY_TOWER, 1);
		}
	}
}

static void bin_puffs(st_t *s, float A)
{
	s->bin_k = NBIN / A;
	memset(s->nbin, 0, sizeof(s->nbin));
	for (int i = 0; i < s->npuff; i++) {
		const puff_t *p = &s->puff[i];
		int b0 = (int)floorf((p->x - p->r) * s->bin_k), b1 = (int)floorf((p->x + p->r) * s->bin_k);
		if (b0 < 0)
			b0 = 0;
		if (b1 > NBIN - 1)
			b1 = NBIN - 1;
		for (int b = b0; b <= b1; b++)
			if (s->nbin[b] < BINCAP)
				s->bin[b][s->nbin[b]++] = (uint16_t)i;
	}
}

/* is (x,y) inside any billow drawn after (in front of) billow `from`? (-1: any billow) */
static int covered(const st_t *s, int from, float x, float y)
{
	int b = (int)floorf(x * s->bin_k);
	if (b < 0 || b >= NBIN)
		return 0;
	const uint16_t *bl = s->bin[b];
	for (int k = s->nbin[b] - 1; k >= 0; k--) { /* indices ascend: walk back from the front */
		int i = bl[k];
		if (i <= from)
			break;
		const puff_t *p = &s->puff[i];
		float dx = x - p->x, dy = y - p->y;
		if (dx * dx + dy * dy < p->r * p->r * 0.96f)
			return 1;
	}
	return 0;
}

/* Ink: the visible part of each billow's outline. Undersides are the lit edges of the decks; the folds
 * higher up fade back into the deck; the thunderhead's crown catches the light on top. */
static void draw_puffs_ink(st_t *s, us_canvas *c, float A)
{
	float sl = 0.7f + 0.5f * s->lum; /* louder: more light inside the storm */
	for (int i = 0; i < s->npuff; i++) {
		const puff_t *p = &s->puff[i];
		if (p->occ || (p->layer == LY_NEAR && p->h > 3.5f) || p->h > 8.5f) /* cores; the near deck's high folds are lost in its mass */
			continue;
		int N = (int)(p->r * c->s * 0.45f);
		if (N < 12)
			N = 12;
		if (N > 64)
			N = 64;
		float lw, rowf;
		int top_lit = p->layer == LY_TOWER;
		switch (p->layer) {
		case LY_NEAR: lw = 0.00145f; rowf = p->h < 0.5f ? 1.0f : 0.62f / (1.0f + 0.35f * p->h); break;
		case LY_MID: lw = 0.0012f; rowf = p->h < 0.5f ? 1.0f : 0.7f; break;
		default: lw = 0.001f; rowf = 1.0f; break;
		}
		int clip_hz = p->layer <= LY_FAR && p->y + p->r > HZ - 0.03f;
		float a0 = (float)(i % 7) * 0.9f, lp = (float)(i % 11) * 0.57f;
		float px = 0, py = 0;
		int pv = 0;
		for (int q = 0; q <= N; q++) {
			float ang = a0 + q * US_TAU / N;
			float ca = cosf(ang), sa = sinf(ang);
			float rl = p->r * (1.0f + 0.05f * sinf(3.0f * ang + lp) + 0.03f * sinf(5.0f * ang - 2.0f * lp)); /* billows are lumpy, not round */
			float x = p->x + ca * rl, y = p->y + sa * rl;
			/* distant billows: only their crowns, the bases dissolve into the haze */
			int vis = y > -0.02f && x > -0.02f && x < A + 0.02f && (p->layer != LY_FAR || sa < 0.35f) && !covered(s, i, x, y);
			if (vis && clip_hz && y > horizon_y(us_sat(x / A)) - 0.002f)
				vis = 0;
			if (vis && pv) {
				float mx = (x + px) * 0.5f, my = (y + py) * 0.5f;
				/* decks: the underside is the bright edge, the fold over each billow's top fades into the mass */
				float shade = top_lit ? 0.45f + 0.55f * us_sat(0.3f - sa) : 0.12f + 0.88f * us_sat(0.2f + 1.2f * sa);
				float L = lit(s, mx, my);
				float v = p->v * shade * rowf * sl + 0.95f * L * (0.6f + 0.4f * rowf) * (0.4f + 0.6f * shade);
				if (v > 0.075f) /* the faintest folds are not worth their ink */
					wline(c, px, py, x, y, lw, us_sat(v), L > 0.35f ? US_MAT_LIGHT : US_MAT_ACCENT);
			}
			px = x;
			py = y;
			pv = vis;
		}
	}
}

/* Ink: the anvil as a few long strokes: a flat, sheared top and a ragged underside thinning downwind */
static void draw_anvil_ink(st_t *s, us_canvas *c, float A)
{
	if (s->anv_x0 - s->anv_x1 < 0.02f)
		return;
	const int N = 40;
	for (int e = 0; e < 2; e++) {
		float px = 0, py = 0;
		for (int k = 0; k <= N; k++) {
			float f = (float)k / N, x = us_lerp(s->anv_x0, s->anv_x1, f);
			float tk = s->anv_th * (1.0f - 0.6f * f);
			/* top: a gentle, sheared dome; underside: small hanging scallops */
			float y = e == 0 ? s->anv_y + 0.006f * us_noise2(x * 5.0f, s->evo) - 0.012f * f * (1.0f - f) * 4.0f * 0.5f
				: s->anv_y + tk + 0.004f * us_noise2(x * 8.0f, s->evo * 1.3f) + 0.006f * (1.0f - f) * fabsf(sinf(x * 60.0f + s->evo));
			if (k > 0 && x >= 0 && x <= A && !covered(s, s->anv_n - 1, x, y)) {
				float fade = us_smooth(1.0f, 0.75f, f) * us_smooth(0.0f, 0.08f, f);
				float L = lit(s, x, y);
				float v = 0.3f * fade * (0.7f + 0.5f * s->lum) + 0.8f * L;
				if (v > 0.05f)
					wline(c, px, py, x, y, 0.001f, us_sat(v), L > 0.35f ? US_MAT_LIGHT : US_MAT_ACCENT);
			}
			px = x;
			py = y;
		}
	}
}

/* Crisp: paint billows back to front, each a shaded volume (lit crown, dark underside, ordered dither) */
static const uint8_t bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static void draw_puffs_crisp(st_t *s, us_canvas *c, float A)
{
	float sl = 0.68f + 0.55f * s->lum;
	int any_light = s->flash > 0.001f || s->sf > 0.003f || s->heat_v > 0.001f || s->glow_a > 0.004f;
	/* front to back, skipping pixels a nearer billow already painted (same picture as back to front) */
	int use_mask = (size_t)c->w * c->h <= MASK_MAX;
	if (use_mask)
		memset(s->mask, 0, (size_t)c->w * c->h);
	for (int ii = 0; ii < s->npuff; ii++) {
		int i = use_mask ? s->npuff - 1 - ii : ii;
		const puff_t *p = &s->puff[i];
		float rp = p->r * c->s;
		int px0 = (int)floorf((p->x - p->r) * c->s), px1 = (int)ceilf((p->x + p->r) * c->s);
		int py0 = (int)floorf((p->y - p->r) * c->s), py1 = (int)ceilf((p->y + p->r) * c->s);
		if (px0 < 0) px0 = 0;
		if (py0 < 0) py0 = 0;
		if (px1 > c->w) px1 = c->w;
		if (py1 > c->h) py1 = c->h;
		int clip_hz = p->layer <= LY_FAR && p->y + p->r > HZ - 0.03f;
		float inv = 1.0f / (rp + 1e-6f);
		float cxp = p->x * c->s, cyp = p->y * c->s;
		/* the near deck is darkest (heavy, close); distant billows paler */
		float body = p->v * sl;
		int rim = (p->layer >= LY_MID && p->h < 0.5f) || (p->layer == LY_TOWER && p->h > 2.5f);
		for (int py = py0; py < py1; py++) {
			float dy = (py + 0.5f - cyp) * inv;
			if (dy <= -1.0f || dy >= 1.0f)
				continue;
			float yn = (py + 0.5f) / c->s;
			for (int px = px0; px < px1; px++) {
				float dx = (px + 0.5f - cxp) * inv;
				float d2 = dx * dx + dy * dy;
				size_t k = (size_t)py * c->w + px;
				if (d2 >= 1.0f || (use_mask && s->mask[k]))
					continue;
				float xn = (px + 0.5f) / c->s;
				if (clip_hz && yn > HZ - 0.03f && yn > horizon_y(us_sat(xn / A)))
					continue;
				float top = p->occ ? us_sat(0.32f - 0.2f * dy) : us_sat(0.55f - 0.7f * dy - 0.2f * dx); /* cores: nearly flat */
				float v = body * (0.5f + 0.62f * top);
				uint8_t m = US_MAT_BG;
				if (d2 > 0.74f && top > 0.7f && rim) { /* the lit rim of each billow's crown */
					m = US_MAT_ACCENT;
					v = 0.12f + 0.3f * p->v * sl;
				}
				v += (bayer4[(py & 3) * 4 + (px & 3)] * (1.0f / 16.0f) - 0.47f) * 0.1f;
				if (any_light) {
					float L = lit(s, xn, yn) * (0.3f + 0.7f * top) * (0.75f + 0.25f * d2); /* lights the billows' crowns + edges: keeps their form */
					if (L > 0.12f) {
						v = 0.45f * v + 0.85f * L;
						m = L > 0.42f ? US_MAT_LIGHT : US_MAT_ACCENT;
					}
				}
				c->ink[k] = (uint8_t)(us_sat(v) * 255.0f);
				c->mat[k] = m;
				if (use_mask)
					s->mask[k] = 1;
			}
		}
	}
}

/* ---- far fields: a telegraph line with birds on the wire, a farmhouse with a lit window ---- */
static float gust_front(const st_t *s, float x)
{
	float dx = x - s->gust_x;
	return s->gust_amp * expf(-dx * dx / 0.02f);
}

static void pole_top(const st_t *s, int i, float A, float *x, float *y)
{
	float u = s->pole_u[i];
	*x = u * A;
	*y = horizon_y(u) - 0.034f;
}

/* point on wire w (0 upper, 1 lower) of span i at fraction f */
static void wire_pt(const st_t *s, int i, int w, float f, float A, float *x, float *y)
{
	float x0, y0, x1, y1;
	pole_top(s, i, A, &x0, &y0);
	pole_top(s, i + 1, A, &x1, &y1);
	float off = w ? 0.005f : 0.0f;
	*x = us_lerp(x0, x1, f);
	float sag = 0.0045f + 0.004f * gust_front(s, *x) + 0.0015f * s->gust;
	*y = us_lerp(y0, y1, f) + off + sag * 4.0f * f * (1.0f - f);
}

/* crisp only: a 1px line written over whatever is there (the far line must cut across the dark thunderhead) */
static void stamp_line(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
	float x0 = ax * c->s, y0 = ay * c->s, x1 = bx * c->s, y1 = by * c->s;
	int n = (int)(fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)) + 1.0f);
	uint8_t iv = (uint8_t)(us_sat(v) * 255.0f);
	for (int k = 0; k <= n; k++) {
		float f = (float)k / n;
		int px = (int)(x0 + (x1 - x0) * f), py = (int)(y0 + (y1 - y0) * f);
		if ((unsigned)px >= (unsigned)c->w || (unsigned)py >= (unsigned)c->h)
			continue;
		size_t i = (size_t)py * c->w + px;
		c->ink[i] = iv;
		c->mat[i] = mat;
	}
}

static void draw_far(st_t *s, const us_ctx *ctx, us_canvas *c, float A, float L)
{
	int crisp = c->crisp;
	float lv = us_sat((crisp ? 0.42f : 0.22f) + 0.6f * L + 0.1f * s->heat_v);
	/* poles + crossbars */
	for (int i = 0; i < NPOLE; i++) {
		float x, y;
		pole_top(s, i, A, &x, &y);
		float by = horizon_y(s->pole_u[i]);
		float lean = 0.002f * us_noise2(i * 3.1f, 0.5f);
		if (crisp) {
			stamp_line(c, x + lean, y - 0.002f, x, by, lv, US_MAT_STEM);
			stamp_line(c, x - 0.006f + lean, y + 0.001f, x + 0.006f + lean, y + 0.001f, lv, US_MAT_STEM);
		} else {
			wline(c, x + lean, y - 0.002f, x, by, 0.0012f, lv, US_MAT_STEM);
			wline(c, x - 0.006f + lean, y + 0.001f, x + 0.006f + lean, y + 0.001f, 0.001f, lv, US_MAT_STEM);
		}
	}
	/* wires, a gentle catenary; the gust front lifts and swings them */
	for (int i = 0; i < NPOLE - 1; i++)
		for (int w = 0; w < 2; w++) {
			float pts[2 * 11];
			for (int k = 0; k <= 10; k++)
				wire_pt(s, i, w, k / 10.0f, A, &pts[2 * k], &pts[2 * k + 1]);
			if (crisp) {
				if (w == 0)
					for (int k = 0; k < 10; k++)
						stamp_line(c, pts[2 * k], pts[2 * k + 1], pts[2 * k + 2], pts[2 * k + 3], lv * 0.8f, US_MAT_STEM);
			} else
				for (int k = 0; k < 10; k++)
					wline(c, pts[2 * k], pts[2 * k + 1], pts[2 * k + 2], pts[2 * k + 3], 0.0008f, lv * 0.75f, US_MAT_STEM);
		}
	/* the farmhouse: a gabled barn with one warm window (the power blinks out on a strike) */
	{
		float bx = s->barn_u * A, by = horizon_y(s->barn_u) + 0.002f;
		float hw = 0.014f, hh = 0.013f, rh = 0.009f;
		float bv = us_sat(0.2f + 0.6f * L + 0.1f * s->heat_v);
		if (crisp) {
			us_tri(c, bx - hw, by, bx + hw, by, bx + hw, by - hh, bv * 0.7f, US_MAT_STEM);
			us_tri(c, bx - hw, by, bx + hw, by - hh, bx - hw, by - hh, bv * 0.7f, US_MAT_STEM);
			us_tri(c, bx - hw - 0.002f, by - hh, bx + hw + 0.002f, by - hh, bx, by - hh - rh, bv * 0.7f, US_MAT_STEM);
		} else {
			wline(c, bx - hw, by, bx - hw, by - hh, 0.001f, bv, US_MAT_STEM);
			wline(c, bx + hw, by, bx + hw, by - hh, 0.001f, bv, US_MAT_STEM);
			wline(c, bx - hw - 0.003f, by - hh + 0.002f, bx, by - hh - rh, 0.001f, bv, US_MAT_STEM);
			wline(c, bx, by - hh - rh, bx + hw + 0.003f, by - hh + 0.002f, 0.001f, bv, US_MAT_STEM);
			/* a lean-to and a silo beside it */
			wline(c, bx + hw, by - hh * 0.6f, bx + hw + 0.012f, by - hh * 0.35f, 0.001f, bv * 0.8f, US_MAT_STEM);
			wline(c, bx + hw + 0.012f, by - hh * 0.35f, bx + hw + 0.012f, by, 0.001f, bv * 0.8f, US_MAT_STEM);
			wline(c, bx - hw - 0.004f, by, bx - hw - 0.004f, by - hh * 1.5f, 0.001f, bv * 0.8f, US_MAT_STEM);
			wline(c, bx - hw - 0.011f, by, bx - hw - 0.011f, by - hh * 1.5f, 0.001f, bv * 0.8f, US_MAT_STEM);
			us_arc(c, bx - hw - 0.0075f, by - hh * 1.5f, 0.0035f, US_PI, US_TAU, 0.001f, bv * 0.8f, US_MAT_STEM);
		}
		float win = (0.45f + 0.08f * us_noise2((float)ctx->t * 0.6f, 9.1f)) * (1.0f - us_sat(s->power_out));
		if (win > 0.02f)
			us_disc(c, bx - hw * 0.35f, by - hh * 0.5f, crisp ? 0.0035f : 0.0026f, win, US_MAT_LIGHT);
	}
}

/* birds on the upper wire; a sharp onset flushes them, they wheel off into the rain and drift home in a lull */
static void birds_step(st_t *s, const us_audio *a, float rdt)
{
	us_rng *r = &s->rng;
	int flush = a->beat && a->onset > 0.6f;
	for (int k = 0; k < NBIRD; k++) {
		bird_t *b = &s->bird[k];
		switch (b->state) {
		case 0:
			if (flush && us_rand(r) < 0.8f) {
				b->state = 1;
				b->p = -us_rand(r) * 0.12f; /* a ragged, staggered take-off */
			}
			break;
		case 1:
			b->p += rdt * 0.32f;
			if (b->p >= 1.0f) {
				b->p = 1.0f;
				b->state = 2;
				b->rest = us_randr(r, 3.0f, 9.0f);
			}
			break;
		case 2:
			if (a->onset < 0.35f)
				b->rest -= rdt;
			if (b->rest <= 0) {
				b->state = 3;
				b->span = (int)(us_rand(r) * (NPOLE - 1));
				b->f = us_randr(r, 0.15f, 0.85f);
			}
			break;
		default:
			b->p -= rdt * 0.12f;
			if (flush && b->p > 0.15f) { /* spooked again on the way in */
				b->state = 1;
			} else if (b->p <= 0.0f) {
				b->p = 0.0f;
				b->state = 0;
			}
		}
	}
}

static void draw_birds(st_t *s, const us_ctx *ctx, us_canvas *c, float A, float L)
{
	for (int k = 0; k < NBIRD; k++) {
		bird_t *b = &s->bird[k];
		float px, py;
		wire_pt(s, b->span, 0, b->f, A, &px, &py);
		float p = us_sat(b->p);
		if (b->state == 0 || p <= 0.0f) {
			/* perched: a small hunched body and a tail tick */
			float v = us_sat(0.3f + 0.6f * L);
			if (c->crisp)
				stamp_line(c, px, py - 0.004f, px, py - 0.002f, us_sat(v + 0.2f), US_MAT_ACCENT);
			else {
				us_disc(c, px, py - 0.0024f, 0.0019f, v, US_MAT_ACCENT);
				wline(c, px + 0.001f, py - 0.002f, px + 0.0035f, py + 0.0015f, 0.001f, v * 0.8f, US_MAT_ACCENT);
			}
			continue;
		}
		if (p >= 1.0f)
			continue;
		/* a curved path from the wire up and away; smaller + fainter with distance */
		float e = p * p * (3.0f - 2.0f * p) * 0.6f + 0.4f * p;
		float cx = px + b->dx * 0.25f, cy = py + b->dy * 0.85f;
		float fx = px + b->dx, fy = py + b->dy;
		float x = (1 - e) * (1 - e) * px + 2 * (1 - e) * e * cx + e * e * fx;
		float y = (1 - e) * (1 - e) * py + 2 * (1 - e) * e * cy + e * e * fy;
		float sc = 1.0f - 0.65f * p;
		float flap = b->state == 1 ? sinf((float)ctx->t * 17.0f + b->phase) : 0.35f + 0.25f * sinf((float)ctx->t * 3.0f + b->phase);
		float v = us_sat((0.38f + 0.5f * L) * (1.0f - 0.6f * p));
		float ww = 0.0075f * sc, wy = 0.0045f * sc * flap;
		if (c->crisp) {
			us_line(c, x - ww, y - wy, x, y, 0.002f, v, US_MAT_ACCENT);
			us_line(c, x, y, x + ww, y - wy, 0.002f, v, US_MAT_ACCENT);
		} else {
			float mx = ww * 0.45f, my = wy * 0.2f + 0.0012f * sc;
			wline(c, x - ww, y - wy, x - mx, y - my, 0.001f, v, US_MAT_ACCENT);
			wline(c, x - mx, y - my, x, y, 0.001f, v, US_MAT_ACCENT);
			wline(c, x, y, x + mx, y - my, 0.001f, v, US_MAT_ACCENT);
			wline(c, x + mx, y - my, x + ww, y - wy, 0.001f, v, US_MAT_ACCENT);
		}
	}
}

static void spawn_splashes(st_t *s, int n)
{
	us_rng *r = &s->rng;
	for (int k = 0; k < n; k++) {
		splash_t *p = s->nsplash < MAX_SPLASH ? &s->splash[s->nsplash++] : &s->splash[(int)(us_rand(r) * MAX_SPLASH) % MAX_SPLASH];
		p->x = us_rand(r);
		p->y = HZ + 0.015f + (0.985f - HZ - 0.015f) * sqrtf(us_rand(r));
		p->t = 0.0f;
	}
}

/* hats: little crowns of spray on the fields, nearer ones bigger */
static void draw_splashes(st_t *s, us_canvas *c, float A, float rdt)
{
	const float life = 0.16f;
	for (int k = 0; k < s->nsplash;) {
		splash_t *p = &s->splash[k];
		p->t += rdt;
		if (p->t > life) {
			*p = s->splash[--s->nsplash];
			continue;
		}
		float f = p->t / life, x = p->x * A, y = p->y;
		float sz = 0.004f + 0.05f * (y - HZ);
		float v = us_sat(0.75f * (1.0f - f) + 0.2f);
		if (c->crisp) {
			us_line(c, x - sz * 0.6f, y - sz * (0.4f + 0.6f * f), x, y, 0.002f, v, US_MAT_WATER);
			us_line(c, x + sz * 0.6f, y - sz * (0.4f + 0.6f * f), x, y, 0.002f, v, US_MAT_WATER);
		} else {
			float hgt = sz * (0.5f + 0.8f * f);
			wline(c, x - sz * 0.25f, y, x - sz * 0.75f, y - hgt, 0.001f, v, US_MAT_WATER);
			wline(c, x + sz * 0.25f, y, x + sz * 0.75f, y - hgt, 0.001f, v, US_MAT_WATER);
			wline(c, x, y, x, y - hgt * 1.2f, 0.001f, v * 0.8f, US_MAT_WATER);
			us_point(c, x, y - hgt * 1.6f - sz * 0.3f, 0.0012f, v * 0.8f, US_MAT_WATER);
		}
		k++;
	}
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *s = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, rdt = ctx->real_dt, A = ctx->aspect;
	float dens = ctx->p->density;
	float t = (float)ctx->t;
	us_rng *r = &s->rng;

	/* ---- charge: swell builds it (world also builds it slowly), strikes discharge ---- */
	float drive = us_smooth(0.25f, 0.8f, a->swell) * 0.03f + 0.008f * a->level * a->active;
	us_world_add(w, &w->storm_charge, drive, dt);
	float ch = w->storm_charge;
	if (!s->inited) {
		s->ch_vis = ch;
		s->rain = us_sat(ch * 0.6f + w->moisture * 0.2f - 0.2f);
		s->build = 0.35f * ch;
		s->inited = 1;
	}
	s->ch_vis = us_approach(s->ch_vis, ch, 2.5f, rdt);
	float cv = s->ch_vis;

	/* ---- loudness builds the storm; quiet clears it ---- */
	float act = 0.3f + 0.7f * a->active;
	{
		float bt = us_sat(a->level * act * 1.15f);
		s->build = us_approach(s->build, bt, bt > s->build ? 1.2f : 1.8f, rdt); /* the clouds gather slowly */
		s->lum = us_approach(s->lum, bt, bt > s->lum ? 0.25f : 0.5f, rdt);     /* light + rain follow faster */
	}
	float bd = s->build;
	float wind = 0.008f + 0.025f * w->wind + 0.02f * cv + 0.02f * bd;
	s->drift += dt * (0.005f + 0.012f * w->wind + 0.01f * cv + 0.012f * bd); /* slow: the drums should be what moves it */
	s->evo += dt * (0.012f + 0.015f * cv + 0.02f * bd);

	/* ---- the music's parts, each eased into its own envelope ---- */
	s->rumble = us_approach(s->rumble, a->sub * act, a->sub > s->rumble ? 0.6f : 1.2f, rdt);
	s->swellv = us_approach(s->swellv, a->swell, 2.0f, rdt);
	{
		float gt = a->mid * a->mid * act;
		s->glow_v = us_approach(s->glow_v, gt, gt > s->glow_v ? 0.15f : 0.5f, rdt);
	}
	/* kick: the decks heave and the rain gusts */
	s->heave *= expf(-rdt / 0.075f);
	if (a->kick_hit) {
		float k = 0.65f + 0.35f * us_sat(a->kick);
		if (k > s->heave)
			s->heave = k;
	}
	/* hats: the rain glints and splashes */
	s->glint *= expf(-rdt / 0.05f);
	if (a->hat_hit) {
		float g = 0.6f + 0.4f * us_sat(a->hat);
		if (g > s->glint)
			s->glint = g;
		spawn_splashes(s, 4 + (int)(10.0f * us_sat(0.25f + s->rain) * (0.5f + dens)));
	}
	/* kick: a gust front sweeps downwind (right to left) through the wires, grass and the oak */
	s->gust = us_approach(s->gust, 0.0f, 0.35f, rdt);
	if (a->kick > s->gust)
		s->gust = us_approach(s->gust, a->kick, 0.05f, rdt);
	s->gust_x -= rdt * 2.6f;
	s->gust_amp = us_approach(s->gust_amp, 0.0f, 0.6f, rdt);
	if (a->kick_hit && s->gust_x < A * 0.55f) {
		s->gust_x = A + 0.15f;
		s->gust_amp = us_sat(0.5f + a->kick);
	}
	s->tower_rise += dt * (0.004f + 0.014f * s->swellv + 0.006f * cv);
	s->tower_h = us_approach(s->tower_h, 0.27f + 0.05f * cv + 0.06f * s->swellv, 3.0f, rdt);
	s->power_out = us_approach(s->power_out, 0.0f, 0.9f, rdt);
	birds_step(s, a, rdt);

	/* ---- the decks ---- */
	float cov = us_sat(0.08f + 0.62f * bd + 0.25f * cv + 0.08f * w->moisture + 0.24f * (dens - 0.5f));
	float yb = 0.15f + 0.12f * cov + 0.05f * cv;
	s->deck_y = yb;
	s->npuff = 0;
	gen_tower(s, A);
	{
		deck_t far = {HZ - 0.07f - 0.03f * cov, 0.03f, 0.027f, 0.022f, 0.25f, 0.34f, 3.0f, 0.006f, 2};
		deck_t mid = {yb + 0.16f + 0.04f * cov, 0.05f, 0.046f, 0.036f, 0.55f, 0.46f, 1.7f, 0.026f, 2 + (dens > 0.35f) + (dens > 0.75f)};
		int nr = 2 + (int)((yb + 0.08f) / 0.06f);
		if (nr > 8)
			nr = 8;
		deck_t nearl = {yb, 0.085f, 0.074f, 0.06f, 1.0f, 0.62f, 0.9f, 0.046f, nr};
		gen_deck(s, A, LY_FAR, &far, us_sat(cov * 0.35f + 0.05f), t);
		gen_deck(s, A, LY_MID, &mid, us_sat(cov * 0.9f), t);
		gen_deck(s, A, LY_NEAR, &nearl, cov, t);
	}
	bin_puffs(s, A);
	/* the visible underside per column (near + middle decks): rain source + strike origins */
	for (int i = 0; i < GW; i++) {
		s->col[i] = 0;
		s->colb[i] = 0;
	}
	for (int k = 0; k < s->npuff; k++) {
		const puff_t *p = &s->puff[k];
		if (p->layer < LY_MID)
			continue;
		int i0 = (int)floorf((p->x - p->r) / A * GW), i1 = (int)ceilf((p->x + p->r) / A * GW);
		if (i0 < 0) i0 = 0;
		if (i1 > GW - 1) i1 = GW - 1;
		for (int i = i0; i <= i1; i++) {
			float dx = (i + 0.5f) / GW * A - p->x;
			float q = p->r * p->r - dx * dx;
			if (q <= 0)
				continue;
			float bot = p->y + sqrtf(q);
			if (bot > s->colb[i]) {
				s->colb[i] = bot;
				s->col[i] = p->m * (p->layer == LY_NEAR ? 1.0f : 0.8f);
			}
		}
	}

	/* mids: a soft light wanders inside the deck; brighter music lifts it higher into the cloud */
	s->glow_x = A * (0.5f + 0.45f * us_noise2(t * 0.045f, 1.7f));
	s->glow_y = us_approach(s->glow_y, us_clampf(yb - 0.03f - 0.15f * a->centroid, 0.05f, 0.5f), 1.0f, rdt);
	s->glow_a = (ctx->crisp ? 0.12f : 0.18f) * s->glow_v * (0.75f + 0.25f * us_noise2(t * 1.3f, 5.5f));

	/* ---- snare: lightning inside the clouds; a built-up storm sends some of it to the ground ---- */
	s->cool -= rdt;
	s->bolt_t += rdt;
	s->sf_t += rdt;
	if (a->snare_hit) {
		int best = (int)(us_rand(r) * GW);
		for (int k = 0; k < 4; k++) {
			int i = (int)(us_rand(r) * GW) % GW;
			if (s->col[i] > s->col[best])
				best = i;
		}
		s->sf_x = (best + 0.5f) / GW * A;
		s->sf_y = (s->colb[best] > 0 ? s->colb[best] : yb) - us_randr(r, 0.03f, 0.1f);
		s->sf_t = 0;
		s->sf_amp = 0.8f + 0.4f * us_sat(a->snare);
		float pb = us_sat(0.6f * bd + 0.45f * cv - 0.15f);
		if (s->cool <= 0 && a->snare > 0.45f && us_rand(r) < pb) {
			if (us_rand(r) < 0.35f + 0.4f * cv) {
				strike(s, A, 1, dens, a->snare, a->width);
				s->power_out = 1.6f;
				us_world_add(w, &w->storm_charge, -us_randr(r, 0.05f, 0.1f), 1.0f);
				s->cool = us_randr(r, 0.8f, 2.5f) * (1.25f - 0.6f * ch);
			} else {
				strike(s, A, 0, dens, a->snare, a->width);
				us_world_add(w, &w->storm_charge, -0.03f, 1.0f);
				s->cool = us_randr(r, 0.4f, 1.5f);
			}
			s->sf_x = s->bolt_x;
		}
	}
	{
		float tt = s->sf_t;
		s->sf = tt < 0.6f ? s->sf_amp * (expf(-tt / 0.07f) + (tt > 0.1f ? 0.45f * expf(-(tt - 0.1f) / 0.06f) : 0.0f)) : 0.0f;
	}
	/* other transients (bells, plucks, stray hits): a flicker deep inside the distant thunderhead */
	s->small_cool -= rdt;
	if (a->beat && !a->kick_hit && !a->snare_hit && !a->hat_hit && a->onset > 0.25f && s->small_cool <= 0) {
		s->heat_t = 0;
		s->heat_x = s->tower_u * A + us_randn(r) * 0.04f;
		s->heat_y = HZ - s->tower_h * us_randr(r, 0.3f, 0.8f);
		s->heat_amp = 0.5f + 0.6f * a->onset;
		s->small_cool = us_randr(r, 0.6f, 1.5f);
	}
	/* flash envelope from the return strokes */
	s->flash = 0;
	if (s->nseg > 0)
		for (int k = 0; k < s->nstrokes; k++) {
			float tt = s->bolt_t - s->stroke_t[k];
			if (tt >= 0)
				s->flash += expf(-tt / 0.045f) * (k == 0 ? 1.0f : 0.8f);
		}
	if (s->flash > 1.2f)
		s->flash = 1.2f;

	/* ---- heat lightning: distant, in-cloud, the storm's breath in long quiet (not the first seconds) ---- */
	s->heat_next -= rdt;
	if (a->silence > 0.2f && a->silence < 3.0f && s->heat_next < 0.5f)
		s->heat_next = 0.5f;
	if (s->heat_next <= 0) {
		s->heat_t = 0;
		s->heat_x = us_randr(r, 0.1f, 0.9f) * A;
		s->heat_y = us_randr(r, 0.1f, yb);
		if (us_rand(r) < 0.6f) { /* often deep inside the distant thunderhead */
			s->heat_x = s->tower_u * A + us_randn(r) * 0.05f;
			s->heat_y = HZ - s->tower_h * us_randr(r, 0.35f, 0.85f);
		}
		s->heat_next = us_randr(r, 4.0f, 12.0f) * (1.2f - 0.5f * cv);
		s->heat_amp = 0.7f;
	}
	s->heat_t += rdt;
	{
		float ht = s->heat_t;
		float v = 0;
		if (ht < 0.7f)
			v = (expf(-ht / 0.06f) + 0.7f * (ht > 0.18f ? expf(-(ht - 0.18f) / 0.05f) : 0) +
			     0.5f * (ht > 0.4f ? expf(-(ht - 0.4f) / 0.08f) : 0)) *
			    0.5f;
		s->heat_v = v * s->heat_amp;
	}

	/* ---- stars in the gaps of a thin deck (gone once it closes over) ---- */
	{
		float sv = (1.0f - us_smooth(0.35f, 0.85f, cov)) * (1.0f - us_smooth(0.2f, 0.6f, w->daylight));
		if (sv > 0.02f)
			for (int i = 0; i < (ctx->crisp ? 50 : 110); i++) {
				uint32_t h = (uint32_t)i * 2654435761u + 77u;
				float u = ((h >> 8) & 4095) / 4095.0f, y = ((h >> 20) & 1023) / 1023.0f * 0.7f;
				if (covered(s, -1, u * A, y))
					continue;
				float tw = 0.7f + 0.3f * us_noise2(i * 1.3f, t * 1.5f);
				float m = ((h >> 3) & 31) / 31.0f;
				us_point(c, u * A, y, ctx->crisp ? 0.003f : 0.001f + 0.0006f * m, (0.12f + 0.4f * m * m) * tw * sv, US_MAT_LIGHT);
			}
	}

	/* ---- the clouds ---- */
	if (ctx->crisp)
		draw_puffs_crisp(s, c, A);
	else {
		draw_anvil_ink(s, c, A);
		draw_puffs_ink(s, c, A);
	}

	/* ---- rain curtains under the heavy masses ---- */
	float rain_t = us_sat(0.15f * cv + 1.0f * s->lum * (0.35f + 0.65f * cov) + 0.12f * w->moisture - 0.15f);
	s->rain = us_approach(s->rain, rain_t, 0.6f, rdt);
	us_world_add(w, &w->moisture, 0.006f * s->rain, dt);
	{
		float hv = s->heave;
		float slant = 0.15f + 1.2f * wind + 0.45f * hv; /* the kick's gust leans the rain over */
		int spawn = (int)(s->rain * (160.0f + 900.0f * dens) * rdt * (1.0f + 1.5f * hv) + us_rand(r));
		for (int k = 0; k < spawn && s->ndrop < MAX_DROP; k++) {
			int i = (int)(us_rand(r) * GW) % GW;
			if (s->colb[i] <= 0.0f)
				continue;
			float u = (i + 0.5f) / GW;
			float shaft = us_smooth(-0.1f, 0.45f, us_noise3(u * A * 2.6f + s->drift * 1.2f, 3.3f, s->evo * 0.6f));
			if (us_rand(r) > us_smooth(0.3f, 0.75f, s->col[i]) * (0.3f + 0.7f * shaft))
				continue;
			drop_t *d = &s->drop[s->ndrop++];
			d->x = (i + us_rand(r)) / GW;
			d->y = s->colb[i] + us_randr(r, -0.015f, 0.03f);
			d->vy = us_randr(r, 1.1f, 1.7f);
			d->vx = d->vy * us_randr(r, 0.85f, 1.15f);
			d->len = us_randr(r, 0.02f, 0.05f);
			d->v = us_randr(r, 0.18f, 0.4f);
		}
		int kk = 0;
		for (int k = 0; k < s->ndrop;) {
			drop_t *d = &s->drop[k];
			float sx = d->vx * slant;
			d->x += sx * dt * (1.0f + 0.8f * hv) / A;
			d->y += d->vy * dt * (1.0f + 0.5f * hv);
			float gy = horizon_y(us_sat(d->x));
			if (d->y > gy || d->x > 1.05f || d->x < -0.05f) {
				*d = s->drop[--s->ndrop];
				continue;
			}
			uint32_t hh = (uint32_t)k * 2654435761u;
			if (ctx->crisp && (kk++ & 1)) {
				k++;
				continue;
			}
			float x = d->x * A;
			float tl = d->len / (d->vy + 1e-3f) * (1.0f + 1.1f * hv);
			/* hats: a scatter of drops catch the light */
			float glint = s->glint * ((hh >> 27) < 9 ? 0.75f : 0.1f);
			float v = d->v * (0.8f + 0.4f * s->lum + 0.9f * hv) + glint + 0.5f * lit(s, x, d->y);
			float x1 = x - sx * tl, y1 = d->y - d->vy * tl;
			if (ctx->crisp)
				us_line(c, x, d->y, x1, y1, 0.002f, us_sat(v), US_MAT_WATER);
			else
				wline(c, x, d->y, x1, y1, 0.0009f, us_sat(v), US_MAT_WATER);
			k++;
		}
	}

	/* ---- lightning channel ---- */
	if (s->nseg > 0) {
		float after = 0.32f * expf(-s->bolt_t / 0.5f); /* retinal afterimage */
		float fv = s->flash > after ? s->flash : after;
		if (fv < 0.01f) {
			s->nseg = 0;
		} else {
			for (int k = 0; k < s->nseg; k++) {
				seg_t *g = &s->seg[k];
				float v = us_sat(fv * g->v * (s->flash > after ? 1.0f : 0.8f));
				if (!ctx->crisp && s->flash > 0.2f) {
					us_line(c, g->x0, g->y0, g->x1, g->y1, 0.016f * g->w, v * 0.12f, US_MAT_LIGHT);
					us_line(c, g->x0, g->y0, g->x1, g->y1, 0.0065f * g->w, v * 0.35f, US_MAT_LIGHT);
				}
				us_line(c, g->x0, g->y0, g->x1, g->y1, (ctx->crisp ? 0.003f : 0.0032f) * g->w, us_sat(v * 1.15f), US_MAT_LIGHT);
			}
		}
	}

	/* ---- horizon, ground, trees (lit by the flash) ---- */
	{
		float L = s->flash * s->bolt_amp;
		float pts[2 * 65];
		for (int i = 0; i <= 64; i++) {
			float u = i / 64.0f;
			pts[2 * i] = u * A;
			pts[2 * i + 1] = horizon_y(u);
		}
		float hv = 0.4f + 0.5f * L + 0.15f * s->heat_v;
		us_polyline(c, pts, 65, 0.0016f, us_sat(hv), US_MAT_EARTH);
		if (ctx->crisp)
			for (int i = 0; i < 64; i++) {
				float x0 = pts[2 * i], y0 = pts[2 * i + 1], x1 = pts[2 * i + 2], y1 = pts[2 * i + 3];
				float gv = 0.16f + 0.3f * L;
				us_tri(c, x0, y0, x1, y1, x0, 1.01f, gv, US_MAT_EARTH);
				us_tri(c, x1, y1, x1, 1.01f, x0, 1.01f, gv, US_MAT_EARTH);
			}
		else {
			/* a few furrows in the foreground field, catching the flash */
			for (int k = 1; k <= 4; k++) {
				float yy = 0.84f + 0.04f * k * k * 0.6f;
				float fp[2 * 17];
				for (int i = 0; i <= 16; i++) {
					float u = i / 16.0f;
					fp[2 * i] = u * A;
					fp[2 * i + 1] = yy + 0.01f * us_noise2(u * 3.0f, k * 1.7f) * k;
				}
				us_polyline(c, fp, 17, 0.001f, us_sat((0.10f + 0.05f * k) + 0.5f * L), US_MAT_EARTH);
			}
		}
		for (int i = 0; i < s->ntrees; i++) {
			tree_t *t = &s->tree[i];
			float tx = t->x * A, ty = horizon_y(t->x) + 0.003f;
			for (int k = 0; k < t->nseg; k++)
				us_line(c, tx + t->sx0[k], ty + t->sy0[k], tx + t->sx1[k], ty + t->sy1[k],
					0.0022f * t->sw[k] + 0.0008f, us_sat(0.45f + 0.5f * L), US_MAT_STEM);
		}
		draw_far(s, ctx, c, A, L);
	}

	/* ---- the near ridge + its oak: black until a flash reveals them ---- */
	{
		float side = s->ridge_side;
		float Lg = s->flash * s->bolt_amp;
		/* light falls off away from where the stroke came down */
		float hx = s->hit_x;
		int iv0 = 0;
		uint8_t fm = US_MAT_EARTH;
		if (ctx->crisp) { /* a dark silhouette against the far fields until the sky lights it */
			float lv = 0.45f * Lg + 0.1f * s->heat_v;
			fm = lv > 0.12f ? US_MAT_EARTH : US_MAT_BG;
			iv0 = (int)(us_sat(lv > 0.12f ? 0.1f + lv : 0.0f) * 255.0f);
		}
		for (int px = 0; px < c->w; px++) {
			float u = (px + 0.5f) / c->s / A;
			int y0 = (int)ceilf(ridge_y(u, side) * c->s);
			for (int py = y0 < 0 ? 0 : y0; py < c->h; py++) {
				size_t i = (size_t)py * c->w + px;
				c->ink[i] = (uint8_t)iv0;
				c->mat[i] = fm;
			}
		}
		float pts[2 * 97];
		for (int i = 0; i <= 96; i++) {
			float u = i / 96.0f;
			pts[2 * i] = u * A;
			pts[2 * i + 1] = ridge_y(u, side);
		}
		for (int i = 0; i < 96; i++) {
			float x = pts[2 * i];
			float dx = x - hx;
			float L = Lg * (0.4f + 0.6f * expf(-dx * dx / 0.15f)) + 0.15f * s->heat_v;
			wline(c, pts[2 * i], pts[2 * i + 1], pts[2 * i + 2], pts[2 * i + 3], 0.0016f, us_sat(0.2f + 0.8f * L), US_MAT_EARTH);
			/* wet grass on the crest, combed by the storm wind */
			if ((!ctx->crisp || (i & 1) == 0) && pts[2 * i + 1] < 0.95f)
				for (int q = 0; q < 3; q++) {
					float gx = x + (q + us_noise2(i * 1.3f, q * 2.1f)) * A / 96.0f / 3.0f;
					float gy = ridge_y(gx / A, side);
					float gh = 0.008f + 0.008f * (0.5f + 0.5f * us_noise2(gx * 9.0f, 3.3f));
					float lean = 0.006f + 0.01f * wind * 20.0f * (0.5f + 0.5f * us_noise2(gx * 3.0f - (float)ctx->t * 2.0f, 1.0f)) +
						     0.016f * gust_front(s, gx) + 0.003f * s->gust;
					wline(c, gx, gy, gx - lean, gy - gh, 0.001f, us_sat(0.1f + 0.75f * L), US_MAT_LEAF);
				}
		}
		/* the oak on the crest: limbs thrash a little in the gusts */
		float ou = side > 0 ? 0.8f : 0.2f;
		float ox = ou * A, oy = ridge_y(ou, side) + 0.004f;
		float dxo = ox - hx;
		float Lo = Lg * (0.45f + 0.55f * expf(-dxo * dxo / 0.15f)) + 0.2f * s->heat_v;
		tree_t *t = &s->oak;
		float sway = (0.4f + 30.0f * wind) * 0.012f;
		float push = -(0.05f * gust_front(s, ox) + 0.012f * s->gust); /* the gust front bends the crown downwind */
		for (int k = 0; k < t->nseg; k++) {
			float h0 = -t->sy0[k], h1 = -t->sy1[k];
			float ws = sway * us_noise2(k * 0.2f, (float)ctx->t * 0.9f) + push;
			float x0 = ox + t->sx0[k] * 1.2f + ws * h0 * 6.0f, y0 = oy + t->sy0[k] * 1.2f;
			float x1 = ox + t->sx1[k] * 1.2f + ws * h1 * 6.0f, y1 = oy + t->sy1[k] * 1.2f;
			float wd = 0.0075f * t->sw[k] + 0.001f;
			float v = us_sat((ctx->crisp ? 0.2f : 0.1f) + 0.85f * Lo);
			(wd * c->s > 1.6f || ctx->crisp ? us_line : wline)(c, x0, y0, x1, y1, wd, v, US_MAT_STEM);
		}
		/* its crown follows the season: full in summer, thin and fiery in autumn, bare in winter;
		 * a dark mass until a flash shows the leaves (tossed by the gusts) */
		{
			float sp = us_world_season_w(w, 0), su = us_world_season_w(w, 1), au = us_world_season_w(w, 2);
			float wi = us_world_season_w(w, 3);
			float leafk = us_sat(0.55f * sp + 1.0f * su + 0.6f * au) * (1.0f - wi);
			if (leafk > 0.02f)
				for (int k = 0; k < t->nseg; k++) {
					if (t->sw[k] > 0.3f)
						continue;
					float h1 = -t->sy1[k];
					float ws = sway * us_noise2(k * 0.2f, (float)ctx->t * 0.9f) + push;
					float ex = ox + t->sx1[k] * 1.2f + ws * h1 * 6.0f, ey = oy + t->sy1[k] * 1.2f;
					float bx = ox + t->sx0[k] * 1.2f + ws * (-t->sy0[k]) * 6.0f, byy = oy + t->sy0[k] * 1.2f;
					uint32_t hh = (uint32_t)k * 2654435761u;
					int nl = 2 + (int)(leafk * 3.0f);
					for (int q = 0; q < nl; q++) {
						float hq = ((hh >> (q * 3)) & 255) / 255.0f;
						if (hq > leafk + 0.15f)
							continue;
						float f = 0.35f + 0.65f * (float)q / nl;
						float lx = us_lerp(bx, ex, f), ly = us_lerp(byy, ey, f);
						float toss = us_noise2(k * 0.7f + q, (float)ctx->t * (1.5f + 40.0f * wind)) - 6.0f * push;
						float la = -US_PI * 0.5f + (hq - 0.5f) * 3.0f + toss * (0.4f + 10.0f * wind);
						float ll = 0.009f + 0.007f * hq;
						uint8_t lm = au > 0.5f ? (hq < 0.4f ? US_MAT_LIGHT : (hq < 0.75f ? US_MAT_BLOOM : US_MAT_EARTH)) : US_MAT_LEAF;
						float lv = us_sat((ctx->crisp ? 0.3f : 0.2f) * (0.7f + 0.3f * hq) + 0.8f * Lo * (0.7f + 0.3f * hq));
						float dx = cosf(la) * ll, dy = sinf(la) * ll, nx = -dy * 0.35f, ny = dx * 0.35f;
						if (ctx->crisp) {
							us_tri(c, lx, ly, lx + dx * 0.5f + nx, ly + dy * 0.5f + ny, lx + dx, ly + dy, lv, lm);
							us_tri(c, lx, ly, lx + dx * 0.5f - nx, ly + dy * 0.5f - ny, lx + dx, ly + dy, lv, lm);
						} else {
							wline(c, lx, ly, lx + dx * 0.5f + nx, ly + dy * 0.5f + ny, 0.001f, lv, lm);
							wline(c, lx + dx * 0.5f + nx, ly + dy * 0.5f + ny, lx + dx, ly + dy, 0.001f, lv, lm);
							wline(c, lx + dx, ly + dy, lx + dx * 0.5f - nx, ly + dy * 0.5f - ny, 0.001f, lv, lm);
							wline(c, lx + dx * 0.5f - nx, ly + dy * 0.5f - ny, lx, ly, 0.001f, lv, lm);
						}
					}
				}
		}
	}
	draw_birds(s, ctx, c, A, s->flash * s->bolt_amp + 0.1f * s->heat_v);
	draw_splashes(s, c, A, rdt);
}

const us_scene_vt us_scene_storm = {"storm", "Storm Front", 0.05f, create, destroy, frame};
