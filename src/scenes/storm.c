/* Understory scene: Storm Front — a storm rolls in over open farmland; the drums are its weather.
 *
 * The focal point is a cumulonimbus in the middle distance: a tower of cauliflower billows on a flat dark
 * base, an overshooting dome, an anvil sheared flat at its top streaming downwind with mammatus under it,
 * and a flanking line of younger towers stepping down on the upwind side. Around it, at parallax depths:
 * ragged scud overhead at the top corners, a middle bank of flat-based cumulus masses, a pale far bank on
 * the horizon. Billows vary in size and stretch (wider than tall), and slots are skipped, so no deck
 * reads as a row of circles.
 *   All looks share one sky raster: the front billow per cell, the light reaching it through the clouds
 *   above (a slanted shadow sweep: lit crowns, dark bases), a bright gap of sky under the storm on the
 *   horizon, rain shafts hanging from the bases (a precipitation core under the thunderhead), and the far
 *   fields as a patchwork in perspective with a wet sheen near the horizon.
 *   Ink:   that raster is a soft wash under the line work: each mass's silhouette, its inner folds only
 *          along their crowns, flat bases as one line, each edge as bright as the light that reaches it.
 *   Crisp: the raster painted with ordered dither: slate bodies, lilac/cream only where light catches.
 * Below: far fields with hedgerows, a telegraph line, birds on the wire and a farmhouse; a near ridge with
 * a lone oak in the foreground.
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
#define MASK_MAX (640 * 400) /* sky raster cells (crisp canvases up to this size get one cell per pixel) */
#define GROW 1024   /* max sky-raster width */
#define NOPUFF 0xFFFF
#define GPAINT 4096 /* widest Ink canvas the sky wash covers */
#define TOWER_B 0.6f /* the thunderhead's flat base */
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
	float ax;      /* horizontal stretch (>= 1: flatter billows) */
	float top, cut; /* the billow only exists between these y (flat cloud bases, the anvil's sheared top) */
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
	float rw[GW], rb[GW]; /* rain-shaft weight + base per column (smoothed) */

	/* the billows, back to front, binned by x for occlusion tests */
	puff_t puff[MAX_PUFF];
	int npuff;
	uint16_t bin[NBIN][BINCAP];
	int nbin[NBIN];
	float bin_k; /* bins per normalized x */
	float ax_next; /* stretch given to the billows being generated */

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
	/* the sky raster: front billow per cell, light reaching it, its value + material */
	uint16_t gid[MASK_MAX];
	float gT[MASK_MAX];
	float gv[MASK_MAX];
	uint8_t gm[MASK_MAX];
	float srow[2][GROW];
	float ghz[GROW];
	uint16_t pxa[GPAINT], pxb[GPAINT];
	float ptx[GPAINT];
	int gw, gh;
	float gs;

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
	float lumf;    /* quicker still: how much light the clouds hold right now */
	float deck_y;  /* the near deck's base */
	int inited;

	/* the distant thunderhead on the horizon */
	float tower_u, tower_h, tower_rise;
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

static void add_puff(st_t *s, float x, float y, float r, float v, float m, float hgt, int layer, int occ, float top, float cut)
{
	if (s->npuff >= MAX_PUFF || !(r > 0.004f))
		return;
	if (r > 0.2f)
		r = 0.2f;
	puff_t *p = &s->puff[s->npuff++];
	p->x = x;
	p->y = y;
	p->r = r;
	p->ax = s->ax_next > 0.0f ? s->ax_next : 1.0f;
	p->v = v;
	p->m = m;
	p->h = hgt;
	p->top = top;
	p->cut = cut;
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
	int flat;      /* 1: flat bases (clouds seen side-on); 0: a hanging, lumpy underside (overhead scud) */
	float avoid;   /* thin the deck out around the thunderhead */
	float ax;      /* billow stretch: wider than tall */
} deck_t;

/* one deck: rows of billows (the top rows only where the mass is thick, so thin masses dome over),
 * grouped into masses by a drifting noise field; coverage thins it into separate clouds. Billow sizes vary
 * a lot (small tight heads beside big rolling ones) and slots are skipped, so no row reads as a pattern. */
static void gen_deck(st_t *s, float A, int L, const deck_t *d, float cov)
{
	float off = s->drift * d->speed;
	int n0 = (int)floorf((off - 0.25f) / d->sp) - 1, n1 = (int)ceilf((off + A + 0.25f) / d->sp) + 1;
	float thr = 0.62f - 0.95f * cov;
	float tcx = s->tower_u * A;
	s->ax_next = d->ax;
	for (int row = d->nrows - 1; row >= 0; row--)
		for (int n = n0; n <= n1; n++) {
			uint32_t h = hash3(n, row, L);
			if (row > 0 && hf(h, 2) < 0.22f)
				continue;
			float xw = (n + 0.5f * (row & 1) + 0.9f * (hf(h, 0) - 0.5f)) * d->sp;
			float x = xw - off;
			float mf = 0.5f + 0.5f * us_fbm2(xw * d->fq + L * 13.1f, s->evo * 0.3f + L * 3.3f, 2);
			if (d->avoid > 0.0f)
				mf -= d->avoid * (1.0f - us_smooth(0.12f, 0.55f, fabsf(x - tcx)));
			float tr = thr + 0.07f * row;
			float m = us_smooth(tr, tr + 0.28f, mf);
			if (m < 0.06f)
				continue;
			float boil = 1.0f + 0.12f * us_noise2(n * 0.71f + row * 3.1f, s->evo * 1.5f + L);
			float sc = 0.7f + 0.55f * (0.5f + 0.5f * us_noise2(xw * d->fq * 0.55f + 4.0f * L, 9.3f + s->evo * 0.1f));
			float hz = hf(h, 1);
			float r = d->r0 * sc * (0.45f + 1.0f * hz * hz) * (0.4f + 0.6f * m) * boil;
			if (row > 0 && hf(h, 0) > 0.82f)
				r *= 1.45f; /* now and then a big rolling head */
			if (L == LY_NEAR || L == LY_MID)
				r *= 1.0f + 0.16f * s->heave * m; /* the kick: masses swell as they drop */
			if (x < -r * d->ax || x > A + r * d->ax)
				continue;
			float drop = d->heave * s->heave * (0.4f + 0.6f * m); /* heavier masses heave further */
			float y, cut = 9.0f;
			if (d->flat) {
				/* side-on cumulus: heads pile up from a flat, slightly ragged base */
				y = d->yb - 0.5f * r - row * d->rowstep * sc + d->rowstep * 0.8f * (hf(h, 2) - 0.5f) + drop;
				/* each mass has its own base height; thin edges ride higher */
				cut = d->yb + drop + d->rowstep * (1.0f * us_noise2(xw * d->fq * 1.3f, L * 1.7f + 5.0f) - 0.25f * (1.0f - m)) +
				      0.003f * us_noise2(xw * 9.0f, L * 1.7f);
			} else {
				y = d->yb - 0.8f * r - row * d->rowstep * sc + d->rowstep * 0.9f * (hf(h, 2) - 0.5f) + drop;
				if (row == 0) {
					/* heavy masses hang lower; the base rolls */
					y += (0.05f * m + 0.03f * us_noise2(xw * d->fq * 1.7f, 2.1f + L)) * d->rowstep / 0.06f;
					y += s->rumble * 0.014f * (0.5f + 0.5f * sinf(xw * 4.0f + 1.3f)); /* sub: sags where the bass sits */
				}
			}
			add_puff(s, x, y, r, d->v0 * (0.75f + 0.25f * m), m, (float)row, L, 0, -9.0f, cut);
		}
}

/* the thunderhead: the scene's focal point. A cumulonimbus rising from a flat, dark base: tiers of billows
 * (broad foot, a waist, a swelling cauliflower crown), an overshooting dome, and an anvil sheared flat at its
 * top, streaming downwind (left) with mammatus pouches hanging under it. Outline-less cores fill each tier. */
static void gen_tower(st_t *s, float A)
{
	static const float prof[9] = {1.0f, 0.93f, 0.88f, 0.87f, 0.9f, 0.95f, 1.0f, 0.97f, 0.72f};
	float H = s->tower_h, cx = s->tower_u * A;
	float hv = s->heave;
	float yB = TOWER_B + 0.006f * hv, yT = yB - H;
	float W = 0.12f + 0.025f * s->ch_vis + 0.025f * s->swellv + 0.02f * s->build;
	float anv = 0.3f + 0.7f * us_smooth(0.1f, 0.75f, s->ch_vis + 0.6f * s->swellv + 0.4f * s->build);
	float boil = s->evo * 4.0f + s->tower_rise * 8.0f; /* the thunderhead boils, even in a lull */
	float sc = 1.0f + 0.05f * hv;
	/* the anvil, behind the crown */
	s->ax_next = 1.8f;
	{
		float x0 = cx + W * (0.7f + 0.5f * anv), x1 = cx - W * (1.3f + 2.4f * anv);
		float th = 0.03f + 0.03f * anv, yA = yT - 0.006f;
		int n = (int)((x0 - x1) / 0.02f) + 1;
		if (n > 70)
			n = 70;
		for (int k = 0; k < n; k++) {
			float f = (float)k / (n > 1 ? n - 1 : 1), x = us_lerp(x0, x1, f);
			uint32_t h = hash3(k, 9, 78);
			float dc = fabsf(x - cx) / W;
			float tk = th * (0.3f + 0.7f * expf(-dc / 1.8f));
			float top = yA + 0.004f * us_noise2(x * 11.0f, boil * 0.2f) + 0.02f * f * f;
			float cut = top + tk + 0.005f * us_noise2(x * 17.0f, 3.0f + boil * 0.3f);
			float r = tk * (0.8f + 0.5f * hf(h, 1)) + 0.006f;
			s->ax_next = 1.8f;
			add_puff(s, x, top + r * 0.85f, r, 0.58f, 0.5f, 9.0f, LY_TOWER, 0, top, cut);
			/* mammatus: pouches hanging from the anvil's underside, near the crown */
			if (hf(h, 0) < 0.55f && dc > 0.6f && dc < 2.6f) {
				float rm = 0.007f + 0.009f * hf(h, 2);
				s->ax_next = 1.2f;
				add_puff(s, x + 0.006f, cut + rm * 0.15f, rm, 0.42f, 0.5f, 9.0f, LY_TOWER, 0, cut - 0.003f, 9.0f);
			}
		}
	}
	s->anv_n = s->npuff;
	s->ax_next = 1.05f;
	/* the overshooting top: a dome punching up through the anvil above the updraft */
	add_puff(s, cx + 0.2f * W, yT + 0.012f, (0.03f + 0.025f * anv) * sc, 0.62f, 0.8f, 8.0f, LY_TOWER, 0, -9.0f, 9.0f);
	/* the flanking line: younger cumulus towers stepping down on the upwind side, behind the main tower */
	s->ax_next = 1.0f;
	for (int j = 3; j >= 1; j--) {
		uint32_t hj = hash3(j, 5, 79);
		float fx = cx + W * (0.75f + 0.62f * j) + 0.02f * (hf(hj, 0) - 0.5f);
		float fh = H * (0.62f - 0.14f * j) * (0.85f + 0.3f * hf(hj, 1)) * (1.0f + 0.06f * us_noise2(j * 2.3f, boil * 0.1f));
		float fw = W * (0.5f - 0.08f * j);
		int nt = 4;
		for (int k = nt - 1; k >= 0; k--) {
			float q = (float)k / (nt - 1);
			float rk = (0.026f + 0.016f * q) * (1.15f - 0.1f * j) * sc;
			float y = yB - rk * 0.45f - q * (fh - rk * 1.2f);
			float hw = fw * (1.0f - 0.25f * q * q);
			int n = 2 + (k < 2);
			for (int i = 0; i < n; i++) {
				uint32_t h = hash3(i + 10 * j, k, 80);
				float f = n > 1 ? (float)i / (n - 1) : 0.5f;
				float rr = rk * (0.7f + 0.55f * hf(h, 1)) * (1.0f + 0.15f * us_noise2(i + j * 3.0f + k, boil));
				float x = fx - hw + f * 2.0f * hw + 0.3f * rk * (hf(h, 0) - 0.5f);
				add_puff(s, x, y + 0.4f * rk * (hf(h, 2) - 0.5f), rr, 0.6f, 0.7f, (float)k, LY_TOWER, 0, -9.0f,
					 yB + 0.003f * us_noise2(x * 25.0f, 2.0f));
			}
		}
	}
	s->ax_next = 1.05f;
	/* tiers, top first (lower tiers sit in front) */
	enum { NT = 9 };
	float cy_[NT], hw_[NT], r_[NT];
	for (int k = NT - 1; k >= 0; k--) {
		float q = (float)k / (NT - 1);
		float hw = W * prof[k] * (1.0f + 0.08f * us_noise2(k * 1.3f, boil * 0.15f));
		float rk = (0.04f + 0.028f * q) * sc;
		float y = yB - rk * 0.45f - q * (H - rk * 1.4f);
		cy_[k] = y;
		hw_[k] = hw;
		r_[k] = rk;
		int n = (int)(2.0f * hw / (0.95f * rk)) + 1;
		if (n < 2)
			n = 2;
		if (n > 12)
			n = 12;
		for (int j = 0; j < n; j++) {
			uint32_t h = hash3(j, k, 77);
			float f = (float)j / (n - 1);
			float rr = rk * (0.6f + 0.7f * hf(h, 1)) * (1.0f + 0.15f * us_noise2(j * 0.9f + k * 3.1f, boil));
			float x = cx - hw + rr * 0.6f + f * (2.0f * hw - 1.2f * rr) + 0.4f * rk * (hf(h, 0) - 0.5f);
			float cut = yB + 0.004f * us_noise2(x * 25.0f, 1.0f);
			add_puff(s, x, y + 0.45f * rk * (hf(h, 2) - 0.5f), rr, 0.62f, 0.8f, (float)k, LY_TOWER, 0, -9.0f, cut);
		}
	}
	/* cores (no outline): fill each tier so the inside reads as one mass */
	for (int k = 0; k < NT; k++) {
		float rc = r_[k] * 0.75f, span = hw_[k] - r_[k];
		int n = span > 0.0f ? (int)(2.0f * span / rc) + 1 : 1;
		if (n > 10)
			n = 10;
		for (int j = 0; j < n; j++) {
			float x = n > 1 ? cx - span + 2.0f * span * j / (n - 1) : cx;
			add_puff(s, x, cy_[k] + 0.3f * rc, rc, 0.6f, 0.8f, (float)k, LY_TOWER, 1, -9.0f, yB);
		}
	}
}

static void bin_puffs(st_t *s, float A)
{
	s->bin_k = NBIN / A;
	memset(s->nbin, 0, sizeof(s->nbin));
	for (int i = 0; i < s->npuff; i++) {
		const puff_t *p = &s->puff[i];
		int b0 = (int)floorf((p->x - p->r * p->ax) * s->bin_k), b1 = (int)floorf((p->x + p->r * p->ax) * s->bin_k);
		if (b0 < 0)
			b0 = 0;
		if (b1 > NBIN - 1)
			b1 = NBIN - 1;
		for (int b = b0; b <= b1; b++)
			if (s->nbin[b] < BINCAP)
				s->bin[b][s->nbin[b]++] = (uint16_t)i;
	}
}

/* is (x,y) inside another billow? front=1: one drawn after (in front of) `from` (-1: any billow);
 * front=0: any other billow of the same layer (to tell a mass's silhouette from its inner folds).
 * mg shrinks the billows' flat cuts, so billows sharing a base don't hide each other's base line. */
static int cover(const st_t *s, int from, float x, float y, float mg, int front)
{
	int b = (int)floorf(x * s->bin_k);
	if (b < 0 || b >= NBIN)
		return 0;
	int lay = from >= 0 ? s->puff[from].layer : -1;
	const uint16_t *bl = s->bin[b];
	for (int k = s->nbin[b] - 1; k >= 0; k--) { /* indices ascend: walk back from the front */
		int i = bl[k];
		if (front && i <= from)
			break;
		if (i == from)
			continue;
		const puff_t *p = &s->puff[i];
		if (!front && p->layer != lay)
			continue;
		if (y < p->top + mg || y > p->cut - mg)
			continue;
		float dx = (x - p->x) / p->ax, dy = y - p->y;
		if (dx * dx + dy * dy < p->r * p->r * 0.96f)
			return 1;
	}
	return 0;
}

static float gT_at(const st_t *s, float x, float y)
{
	int gx = (int)(x * s->gs), gy = (int)(y * s->gs);
	if (gx < 0) gx = 0;
	if (gy < 0) gy = 0;
	if (gx > s->gw - 1) gx = s->gw - 1;
	if (gy > s->gh - 1) gy = s->gh - 1;
	return s->gT[(size_t)gy * s->gw + gx];
}

static const uint8_t bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

/* the far fields: a patchwork of fields in perspective (crops in rows, plough, pasture), a wet sheen
 * near the horizon where they mirror the bright gap under the storm */
static float ground_val(float xn, float d, float A, uint8_t *mat, int crisp)
{
	float Z = 1.0f / (d + 0.006f);
	float X = (xn - 0.5f * A) * Z;
	uint32_t h = hash3((int)floorf(X * 0.32f + 1000.0f), (int)floorf(Z * 0.09f), 5);
	float tone = 0.72f + 0.28f * hf(h, 0);
	int crop = hf(h, 1) < 0.5f;
	float rows = crop && !crisp ? 0.5f + 0.5f * sinf(X * 7.0f + hf(h, 2) * 6.0f) : 0.5f;
	float amp = 0.5f * us_smooth(40.0f, 10.0f, Z);
	*mat = crop && !crisp ? US_MAT_LEAF : US_MAT_EARTH;
	return tone * (0.75f + 0.5f * amp * (rows - 0.5f)) + 1.3f * expf(-d / 0.008f);
}

/* The sky as one raster (canvas resolution when crisp, ~216 rows in Ink): the front billow of each cell,
 * the light reaching it through the clouds above (a slanted shadow sweep: lit crowns, dark bases), the gap
 * of bright sky under the storm on the horizon, rain shafts hanging from the bases, and the far fields.
 * Crisp canvases get it painted with ordered dither; Ink gets it as a soft wash under its line work. */
static void sky_raster(st_t *s, const us_ctx *ctx, us_canvas *c, float A, float slope)
{
	int crisp = c->crisp;
	int gw, gh;
	float gs;
	if (crisp && (size_t)c->w * c->h <= MASK_MAX && c->w <= GROW) {
		gw = c->w;
		gh = c->h;
		gs = c->s;
	} else {
		gs = 216.0f;
		for (;;) {
			gw = (int)ceilf(A * gs);
			gh = (int)ceilf(gs);
			if (gw <= GROW && (size_t)gw * gh <= MASK_MAX)
				break;
			gs *= 0.9f;
		}
	}
	s->gw = gw;
	s->gh = gh;
	s->gs = gs;
	float cell = 1.0f / gs;
	for (int gx = 0; gx < gw; gx++)
		s->ghz[gx] = horizon_y(us_sat((gx + 0.5f) * cell / A));
	/* front billow per cell (front to back, first hit wins) */
	memset(s->gid, 0xFF, (size_t)gw * gh * sizeof(uint16_t));
	for (int i = s->npuff - 1; i >= 0; i--) {
		const puff_t *p = &s->puff[i];
		float ylo = fmaxf(p->y - p->r, p->top), yhi = fminf(p->y + p->r, p->cut);
		int x0 = (int)floorf((p->x - p->r * p->ax) * gs), x1 = (int)ceilf((p->x + p->r * p->ax) * gs);
		int y0 = (int)floorf(ylo * gs), y1 = (int)ceilf(yhi * gs);
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > gw) x1 = gw;
		if (y1 > gh) y1 = gh;
		float r2 = p->r * p->r * gs * gs, cx = p->x * gs, cy = p->y * gs;
		int clip = p->layer == LY_FAR;
		for (int gy = y0; gy < y1; gy++) {
			float yc = gy + 0.5f, yn = yc * cell;
			if (yn < p->top || yn > p->cut)
				continue;
			float dy = yc - cy, q = r2 - dy * dy;
			if (q <= 0.0f)
				continue;
			float hwx = sqrtf(q) * p->ax;
			int a = (int)ceilf(cx - hwx - 0.5f), b = (int)floorf(cx + hwx - 0.5f);
			if (a < x0) a = x0;
			if (b > x1 - 1) b = x1 - 1;
			uint16_t *row = &s->gid[(size_t)gy * gw];
			for (int gx = a; gx <= b; gx++) {
				if (row[gx] != NOPUFF)
					continue;
				if (clip && yn > s->ghz[gx] - 0.002f)
					continue;
				row[gx] = (uint16_t)i;
			}
		}
	}
	/* light + values, row by row from the top; light comes from the upper right */
	float kemp = expf(-cell / 0.012f);
	float *prev = s->srow[0], *cur = s->srow[1];
	for (int gx = 0; gx < gw; gx++)
		prev[gx] = 0.0f;
	float sl = 0.4f + 0.85f * s->lumf;
	float hv = s->heave;
	int any_light = s->flash > 0.001f || s->sf > 0.003f || s->heat_v > 0.001f || s->glow_a > 0.004f;
	float skyflash = 0.3f * s->flash * s->bolt_amp + 0.08f * s->sf + 0.06f * s->heat_v;
	float tt = (float)ctx->t;
	float ck = 0.36f; /* Ink: a soft wash, the lines carry the light */
	for (int gy = 0; gy < gh; gy++) {
		float yn = (gy + 0.5f) * cell;
		for (int gx = 0; gx < gw; gx++) {
			size_t k = (size_t)gy * gw + gx;
			int id = s->gid[k];
			float xn = (gx + 0.5f) * cell;
			float up = 0.35f * prev[gx] + 0.65f * prev[gx + 1 < gw ? gx + 1 : gx];
			const puff_t *p = id != NOPUFF ? &s->puff[id] : NULL;
			float S = p ? up + cell * (p->layer == LY_FAR ? 0.4f : p->h > 8.5f ? 0.2f : 1.0f) : up * kemp;
			cur[gx] = S;
			float T = expf(-S / 0.12f);
			s->gT[k] = T;
			float v;
			uint8_t m;
			int dith = 0;
			float hz = s->ghz[gx];
			int front_cloud = p && p->layer != LY_FAR;
			if (p) {
				float dy = (yn - p->y) / p->r, dx = (xn - p->x) / (p->r * p->ax);
				float crown = p->occ ? 0.45f : us_sat(0.55f - 0.75f * dy + 0.25f * dx);
				if (p->layer == LY_NEAR)
					crown = 0.3f + 0.4f * crown;
				float basek = p->cut < 5.0f ? us_smooth(p->cut - (p->layer == LY_TOWER ? 0.14f : 0.05f), p->cut, yn) : 0.0f;
				float lt = (0.22f + 0.78f * T) * (0.74f + 0.36f * crown) * (1.0f - 0.5f * basek) * (p->layer == LY_NEAR ? 0.75f : 1.0f);
				dith = 1;
				m = US_MAT_ACCENT;
				if (crisp) {
					/* pixel: slate bodies, lilac/cream only where the light catches (dithered edge) */
					float dth = (bayer4[(gy & 3) * 4 + (gx & 3)] + 0.5f) / 16.0f;
					v = p->v * sl * (0.18f + 0.95f * lt * lt + 0.1f * lt) * (p->layer == LY_NEAR ? 1.5f : 1.0f);
					if (lt + 0.25f * dth < (p->h > 8.5f ? 0.74f : 0.84f) || p->layer == LY_NEAR)
						m = US_MAT_BG;
				} else
					v = p->v * sl * lt * ck;
				if (any_light) {
					float L = lit(s, xn, yn);
					L = L / (1.0f + 1.2f * L) * (0.35f + 0.25f * T + 0.4f * crown); /* lit from within, the billows still show */
					if (L > 0.04f) {
						v += (crisp ? 0.85f : 0.7f) * L;
						if (L > 0.15f)
							m = L > 0.4f ? US_MAT_LIGHT : US_MAT_ACCENT;
					}
				}
			} else if (yn < hz) {
				/* sky: dark overhead, opening to a bright gap under the storm on the horizon */
				float glow = expf(-(hz - yn) / 0.03f);
				v = (crisp ? 0.1f : 0.025f + 0.06f * yn) + (crisp ? 0.3f : 0.17f) * glow * (0.5f + 0.9f * s->lumf) + skyflash * (0.5f + 0.5f * glow);
				m = US_MAT_BG;
				dith = glow > 0.05f;
				if (glow > 0.3f)
					m = !crisp || v > 0.3f ? US_MAT_ACCENT : US_MAT_BG;
			} else {
				float d = yn - hz;
				float L = s->flash * s->bolt_amp, dxh = xn - s->hit_x;
				v = ground_val(xn, d, A, &m, crisp) * (crisp ? 0.22f : 0.1f) * (0.7f + 0.3f * T) +
				    L * 0.36f * (0.3f + 0.7f * expf(-dxh * dxh / 0.2f)) + 0.06f * s->heat_v;
				if (crisp && d < 0.006f && v > 0.3f)
					m = US_MAT_ACCENT;
			}
			/* rain shafts hanging from the bases, in front of the far sky, far clouds and fields */
			if (!front_cloud && s->rain > 0.02f) {
				float xs = xn - slope * (yn - TOWER_B);
				float fi = xs / A * GW - 0.5f;
				int i = (int)floorf(fi);
				float ti = fi - i;
				int i1 = i + 1;
				if (i < 0) i = 0;
				if (i1 > GW - 1) i1 = GW - 1;
				float cb = us_lerp(s->rb[i], s->rb[i1], ti);
				if (fi > -1.0f && fi < GW && cb > 0.0f && yn > cb) {
					float reach = 0.08f + 0.5f * s->rain;
					float fall = 1.0f - us_smooth(cb + reach * 0.35f, cb + reach, yn);
					float cw = us_lerp(s->rw[i], s->rw[i1], ti);
					float w = cw * fall * us_smooth(0.0f, 0.02f, yn - cb) * us_sat(s->rain * 1.4f);
					if (w > 0.01f) {
						float st = us_smooth(-0.35f, 0.55f, us_noise2(xs * 60.0f, yn * 2.2f - tt * 5.0f));
						float rv = w * st;
						if (crisp) {
							if (rv * (1.1f + 0.5f * hv) > (bayer4[(gy & 3) * 4 + (gx & 3)] + 0.5f) / 16.0f + 0.15f) {
								v = fmaxf(v * 0.7f, 0.3f + 0.12f * hv + 0.3f * skyflash);
								m = US_MAT_WATER;
								dith = 1;
							}
						} else {
							float r2v = rv * (0.2f + 0.12f * hv) + skyflash * 0.3f * w;
							v = v * (1.0f - 0.35f * w) + r2v + 0.03f * w;
							if (st > 0.8f && r2v > 0.75f * v)
								m = US_MAT_WATER;
						}
					}
				}
			}
			if (crisp && dith)
				v += (bayer4[(gy & 3) * 4 + (gx & 3)] * (1.0f / 16.0f) - 0.47f) * 0.1f;
			s->gv[k] = v;
			s->gm[k] = m;
		}
		float *tmp = prev;
		prev = cur;
		cur = tmp;
	}
	/* paint */
	if (crisp) {
		int same = gw == c->w && gh == c->h;
		for (int py = 0; py < c->h; py++) {
			int gy = same ? py : (int)((py + 0.5f) / c->s * gs);
			if (gy > gh - 1) gy = gh - 1;
			for (int px = 0; px < c->w; px++) {
				int gx = same ? px : (int)((px + 0.5f) / c->s * gs);
				if (gx > gw - 1) gx = gw - 1;
				size_t k = (size_t)gy * gw + gx, o = (size_t)py * c->w + px;
				c->ink[o] = (uint8_t)(us_sat(s->gv[k]) * 255.0f);
				c->mat[o] = s->gm[k];
			}
		}
	} else {
		float f = gs / c->s;
		int cw = c->w < GPAINT ? c->w : GPAINT;
		for (int px = 0; px < cw; px++) {
			float fx = (px + 0.5f) * f - 0.5f;
			int x0 = (int)floorf(fx);
			float tx = fx - x0;
			s->pxa[px] = (uint16_t)(x0 < 0 ? 0 : (x0 > gw - 1 ? gw - 1 : x0));
			s->pxb[px] = (uint16_t)(x0 + 1 > gw - 1 ? gw - 1 : (x0 + 1 < 0 ? 0 : x0 + 1));
			s->ptx[px] = tx < 0.0f ? 0.0f : tx;
		}
		for (int py = 0; py < c->h; py++) {
			float fy = (py + 0.5f) * f - 0.5f;
			int y0 = (int)floorf(fy);
			float ty = fy - y0;
			int ya = y0 < 0 ? 0 : (y0 > gh - 1 ? gh - 1 : y0), yb = y0 + 1 > gh - 1 ? gh - 1 : (y0 + 1 < 0 ? 0 : y0 + 1);
			const float *ra = &s->gv[(size_t)ya * gw], *rb = &s->gv[(size_t)yb * gw];
			const uint8_t *ma = &s->gm[(size_t)(ty < 0.5f ? ya : yb) * gw];
			uint8_t *ink = &c->ink[(size_t)py * c->w], *mt = &c->mat[(size_t)py * c->w];
			for (int px = 0; px < cw; px++) {
				int xa = s->pxa[px], xb = s->pxb[px];
				float tx = s->ptx[px];
				float va = ra[xa] + (rb[xa] - ra[xa]) * ty, vb = ra[xb] + (rb[xb] - ra[xb]) * ty;
				float v = va + (vb - va) * tx;
				int iv = (int)(v * 255.0f + 0.5f);
				if (iv > 255)
					iv = 255;
				if (iv > ink[px]) {
					ink[px] = (uint8_t)iv;
					mt[px] = ma[tx < 0.5f ? xa : xb];
				}
			}
		}
	}
}

/* Ink: the visible part of each billow's outline: a mass's silhouette all round, its inner folds only
 * along their crowns (so they read as cauliflower heads, not stacked circles), flat bases as one line.
 * Each edge is as bright as the light reaching it: lit crowns bright, bases dim. Big sunlit crowns get a
 * second, inner contour stroke (hatching that rounds the form). */
static void draw_puffs_ink(st_t *s, us_canvas *c, float A)
{
	float sl = 0.55f + 0.6f * s->lumf;
	float in = 1.5f / s->gs;
	for (int i = 0; i < s->npuff; i++) {
		const puff_t *p = &s->puff[i];
		if (p->occ)
			continue;
		float lw, lf;
		switch (p->layer) {
		case LY_NEAR: lw = 0.0014f; lf = 0.85f; break;
		case LY_MID: lw = 0.0013f; lf = 0.9f; break;
		case LY_FAR: lw = 0.001f; lf = 0.75f; break;
		default: lw = 0.0015f; lf = 1.0f; break;
		}
		int clip_hz = p->layer == LY_FAR;
		float a0 = (float)(i % 7) * 0.9f, lp = (float)(i % 11) * 0.57f;
		for (int pass = 0; pass < 2; pass++) {
			float rs = pass ? 0.8f : 1.0f;
			if (pass && (p->r < 0.022f || p->h > 8.5f || p->layer == LY_NEAR))
				break;
			int N = (int)(p->r * p->ax * rs * c->s * 0.45f);
			if (N < 12)
				N = 12;
			if (N > 64)
				N = 64;
			float px = 0, py = 0;
			int pv = 0;
			for (int q = 0; q <= N; q++) {
				float ang = a0 + q * US_TAU / N;
				float ca = cosf(ang), sa = sinf(ang);
				float rl = p->r * rs * (1.0f + 0.04f * sinf(3.0f * ang + lp) + 0.025f * sinf(5.0f * ang - 2.0f * lp));
				float x = p->x + ca * rl * p->ax, y = p->y + sa * rl;
				int flat = 0;
				if (y > p->cut) {
					y = p->cut;
					flat = 1;
				} else if (y < p->top) {
					y = p->top;
					flat = 1;
				}
				int vis = y > -0.02f && x > -0.02f && x < A + 0.02f && !cover(s, i, x, y, flat ? 0.002f : 0.0f, 1);
				if (vis && clip_hz && y > horizon_y(us_sat(x / A)) - 0.002f)
					vis = 0;
				int sil = 1;
				if (vis && !pass) {
					sil = !cover(s, i, x, y, flat ? 0.002f : 0.0f, 0);
					if (!sil && (sa > -0.05f || p->h > 8.5f)) /* inner folds: only the crowns (none in the anvil) */
						vis = 0;
				}
				if (vis && pass && sa > -0.4f)
					vis = 0;
				if (vis && pv) {
					float mx = (x + px) * 0.5f, my = (y + py) * 0.5f;
					float T = gT_at(s, mx - ca * in, my - sa * in);
					float L = lit(s, mx, my);
					float v = 1.5f * p->v * sl * lf * (0.12f + 0.88f * T) * (sil ? 1.0f : 0.62f) * (pass ? 0.45f : 1.0f) + 0.9f * L;
					if (v > 0.06f)
						wline(c, px, py, x, y, lw, us_sat(v), L > 0.35f ? US_MAT_LIGHT : US_MAT_ACCENT);
				}
				px = x;
				py = y;
				pv = vis;
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
		s->lumf = us_approach(s->lumf, bt, bt > s->lumf ? 0.08f : 0.2f, rdt);
	}
	float bd = s->build;
	float wind = 0.008f + 0.025f * w->wind + 0.02f * cv + 0.02f * bd;
	s->drift += dt * (0.02f + 0.012f * w->wind + 0.01f * cv + 0.012f * bd); /* slow: the drums should be what moves it */
	s->evo += dt * (0.02f + 0.015f * cv + 0.02f * bd);

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
	float yb = 0.04f + 0.1f * cov + 0.04f * cv; /* the overhead scud's base */
	s->deck_y = TOWER_B - 0.05f;
	s->npuff = 0;
	{
		deck_t far = {HZ - 0.035f - 0.012f * cov, 0.02f, 0.034f, 0.012f, 0.25f, 0.5f, 2.2f, 0.006f, 2, 1, 0.0f, 2.3f};
		deck_t mid = {TOWER_B + 0.04f + 0.02f * cov, 0.05f, 0.055f, 0.032f, 0.55f, 0.46f, 0.95f, 0.026f,
			      2 + (dens > 0.35f) + (dens > 0.75f), 1, 0.75f, 1.3f};
		int nr = 2 + (int)((yb + 0.06f) / 0.06f);
		if (nr > 5)
			nr = 5;
		deck_t nearl = {yb, 0.06f, 0.07f, 0.045f, 1.6f, 0.3f, 0.9f, 0.046f, nr, 0, 0.55f, 1.9f};
		gen_deck(s, A, LY_FAR, &far, us_sat(cov * 0.35f + 0.15f));
		gen_deck(s, A, LY_MID, &mid, 0.06f + 0.24f * cov);
		gen_tower(s, A);
		gen_deck(s, A, LY_NEAR, &nearl, us_sat(cov * 0.85f));
	}
	bin_puffs(s, A);
	/* the visible underside per column (thunderhead, middle bank, scud): rain source + strike origins */
	for (int i = 0; i < GW; i++) {
		s->col[i] = 0;
		s->colb[i] = 0;
	}
	for (int k = 0; k < s->npuff; k++) {
		const puff_t *p = &s->puff[k];
		if (p->layer == LY_FAR || p->h > 8.5f)
			continue;
		int i0 = (int)floorf((p->x - p->r * p->ax) / A * GW), i1 = (int)ceilf((p->x + p->r * p->ax) / A * GW);
		if (i0 < 0) i0 = 0;
		if (i1 > GW - 1) i1 = GW - 1;
		for (int i = i0; i <= i1; i++) {
			float dx = ((i + 0.5f) / GW * A - p->x) / p->ax;
			float q = p->r * p->r - dx * dx;
			if (q <= 0)
				continue;
			float bot = fminf(p->y + sqrtf(q), p->cut);
			if (bot > s->colb[i]) {
				s->colb[i] = bot;
				s->col[i] = p->m * (p->layer == LY_MID ? 0.8f : 1.0f);
			}
		}
	}

	/* mids: a soft light wanders inside the deck; brighter music lifts it higher into the cloud */
	s->glow_x = A * (0.5f + 0.45f * us_noise2(t * 0.045f, 1.7f));
	s->glow_y = us_approach(s->glow_y, us_clampf(TOWER_B - 0.06f - 0.3f * a->centroid, 0.1f, 0.6f), 1.0f, rdt);
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

	/* ---- rain: how much, how slanted, where the shafts hang ---- */
	float rain_t = us_sat(0.15f * cv + 1.0f * s->lum * (0.35f + 0.65f * cov) + 0.12f * w->moisture - 0.15f);
	s->rain = us_approach(s->rain, rain_t, 0.6f, rdt);
	us_world_add(w, &w->moisture, 0.006f * s->rain, dt);
	float hv = s->heave;
	float slant = 0.15f + 1.2f * wind + 0.45f * hv; /* the kick's gust leans the rain over */
	float shaft[GW];
	for (int i = 0; i < GW; i++)
	{
		float u = (i + 0.5f) / GW * A;
		shaft[i] = us_smooth(-0.1f, 0.45f, us_noise3(u * 2.6f + s->drift * 1.2f, 3.3f, s->evo * 0.6f)) *
			   (0.25f + 0.75f * us_smooth(-0.2f, 0.35f, us_noise2(u * 9.0f + s->drift * 1.2f, s->evo * 0.4f + 7.0f)));
	}

	/* ---- the sky: clouds, the bright gap on the horizon, rain shafts, far fields ---- */
	/* where the shafts hang: the heavy bases, softened across columns so curtains fade at their edges */
	{
		float tmp[GW];
		for (int i = 0; i < GW; i++) {
			s->rw[i] = s->colb[i] > 0.0f ? us_smooth(0.3f, 0.75f, s->col[i]) * shaft[i] : 0.0f;
			{
				float dxt = ((i + 0.5f) / GW - s->tower_u) * A;
				float core = expf(-dxt * dxt / 0.006f) * (0.45f + 0.55f * s->build);
				if (s->colb[i] > TOWER_B - 0.03f && core > s->rw[i])
					s->rw[i] = core;
			}
			float b = s->colb[i];
			for (int k = 1; k < 5 && b <= 0.0f; k++) {
				if (i - k >= 0 && s->colb[i - k] > 0.0f)
					b = s->colb[i - k];
				else if (i + k < GW && s->colb[i + k] > 0.0f)
					b = s->colb[i + k];
			}
			s->rb[i] = b > 0.0f ? b : 2.0f;
		}
		for (int pass = 0; pass < 2; pass++) {
			for (int i = 0; i < GW; i++) {
				float a = 0, wsum = 0;
				for (int k = -2; k <= 2; k++)
					if (i + k >= 0 && i + k < GW) {
						float wk = 3.0f - abs(k);
						a += s->rw[i + k] * wk;
						wsum += wk;
					}
				tmp[i] = a / wsum;
			}
			memcpy(s->rw, tmp, sizeof(tmp));
		}
	}
	sky_raster(s, ctx, c, A, slant);
	if (!ctx->crisp)
		draw_puffs_ink(s, c, A);

	/* ---- stars in the gaps of a thin deck (gone once it closes over) ---- */
	{
		float sv = (1.0f - us_smooth(0.35f, 0.85f, cov)) * (1.0f - us_smooth(0.2f, 0.6f, w->daylight));
		if (sv > 0.02f)
			for (int i = 0; i < (ctx->crisp ? 50 : 110); i++) {
				uint32_t h = (uint32_t)i * 2654435761u + 77u;
				float u = ((h >> 8) & 4095) / 4095.0f, y = ((h >> 20) & 1023) / 1023.0f * 0.55f;
				if (cover(s, -1, u * A, y, 0.0f, 1))
					continue;
				float tw = 0.7f + 0.3f * us_noise2(i * 1.3f, t * 1.5f);
				float m = ((h >> 3) & 31) / 31.0f;
				us_point(c, u * A, y, ctx->crisp ? 0.003f : 0.001f + 0.0006f * m, (0.12f + 0.4f * m * m) * tw * sv, US_MAT_LIGHT);
			}
	}

	/* ---- rain drops under the heavy masses ---- */
	{
		int spawn = (int)(s->rain * (160.0f + 900.0f * dens) * rdt * (1.0f + 1.5f * hv) + us_rand(r));
		for (int k = 0; k < spawn && s->ndrop < MAX_DROP; k++) {
			int i = (int)(us_rand(r) * GW) % GW;
			if (s->colb[i] <= 0.0f)
				continue;
			if (us_rand(r) > us_smooth(0.3f, 0.75f, s->col[i]) * (0.3f + 0.7f * shaft[i]))
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
		float hzv = 0.4f + 0.5f * L + 0.15f * s->heat_v;
		us_polyline(c, pts, 65, 0.0016f, us_sat(hzv), US_MAT_EARTH);
		if (!ctx->crisp) {
			/* Ink: the patchwork's hedgerows and field edges, in perspective (rows crowd toward the horizon,
			 * edges run to a vanishing point), so the fields read as a plain going away; the flash lights them */
			float vx = 0.5f * A;
			for (int k = 1; k <= 7; k++) {
				float d = 1.0f / (k / 0.09f) - 0.006f;
				if (d < 0.004f)
					continue;
				float fp[2 * 33];
				for (int i = 0; i <= 32; i++) {
					float u = i / 32.0f;
					fp[2 * i] = u * A;
					fp[2 * i + 1] = horizon_y(u) + d;
				}
				float fv = 0.1f + 0.6f * d + 0.6f * L * (0.4f + 0.6f * expf(-(fp[2 * 16] - s->hit_x) * (fp[2 * 16] - s->hit_x) / 0.2f));
				us_polyline(c, fp, 33, 0.001f, us_sat(fv), US_MAT_EARTH);
			}
			for (int n = -9; n <= 9; n++) {
				float X = n / 0.32f;
				float d1 = 0.17f, x1 = vx + X / (1.0f / (d1 + 0.006f));
				float d0 = 0.012f, x0 = vx + X / (1.0f / (d0 + 0.006f));
				float lv = 0.12f + 0.5f * L;
				wline(c, x0, HZ + d0, x1, HZ + d1, 0.001f, us_sat(lv), US_MAT_EARTH);
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
