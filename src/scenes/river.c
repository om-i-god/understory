/* Understory scene: River — a winding river seen from above, drawn as thousands of fine streamlines.
 *
 * Particles are advected through a flow field built from:
 *   - the channel itself (follows the meandering centreline, fastest mid-stream, slow at the banks),
 *   - potential flow around a few stones (standing bow waves, a quiet wake),
 *   - point vortices (eddies) shed behind the stones — steadily in silence, in bursts on onsets,
 *   - a little curl noise so nothing is ever laminar.
 * The canvas trails turn each particle's motion into a short streak, so the picture is the flow.
 * Beyond the banks lies a quiet patchwork countryside: soft-toned fields with a faint ribbing of furrows and
 * drills, no outlines, only a few gappy hedgerows (shaded bushes) and lines of hedgerow trees, wildflower
 * meadows, pastures with grazing sheep, a towpath, and slow cloud shadows drifting over it all; it turns gold
 * and baled in autumn and white in winter. Riverside trees overhang the water: crowns of domed leaf clumps,
 * lit from the upper left, casting their shadows on the stream. The focal point is a plank footbridge with
 * paths trodden to it; sand and shingle bars lie on the inside of each bend, reed beds along the margins, and
 * a raft of lily pads (a couple in flower) in the slack water of the strongest inside bend.
 *
 * On the water: a duck and her ducklings paddling the slack edge, beds of water-crowfoot streaming in the
 * current, mayfly swarms dancing over the margins, and the rings of rising and leaping trout. A kingfisher
 * watches from a dead branch over the water (bobbing on the hats; a snare can send it plunging in a crown of
 * spray, back up with a minnow it beats on the kicks); a grey heron fishes the shallows (a hard kick is her
 * strike; a run of snares and she lifts off downstream on slow, broad wings, coming back later); dragonflies
 * hawk the margins, hovering and darting (a hat sends one darting, wings flashing; a snare scatters them).
 * Tree crowns are built of leaf bunches: proud, lit on their upper left, with dark crevices and a leafy rim.
 *
 * Under the streamlines the body of the water is shaded: dark in the deep channel (which swings to the outside
 * of each bend), lighter over the sandy shallows, under the cloud shadows, with broken foam along the margins.
 *
 * Audio: kick -> light swells through the whole body of the water (most mid-stream), a surge rolls downstream
 *        from the upper reach, the bow waves flare, and sometimes a trout leaps clear in a splash ring;
 *        snare -> something slaps the water: crowns of white spray and wide rings running out over smoothed
 *        patches, a patter of drops shaken off the leaves all down the reach, and the duck startles and beats
 *        her wings, and the jolt runs through the trees, their leaves flashing pale; hats -> the sun flashes off a scatter of ripple crests for a frame or three (glitter);
 *        level -> the water brightens (quiet passages sink to dark, glassy water); mids + level -> flow speed;
 *        bass -> meadow flowers open; low-mids + sub -> the weed streamers wave; each reed clump nods to its
 *        own mid band (the melody runs along the bank); highs -> wind, more mayflies dancing faster (each lit
 *        by its own treble band) and more rises; air -> slow sun glints, sharpened by the centroid; phrase
 *        swell -> the clouds thin and their shadows slide faster; stereo width -> the ducklings fan out.
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
#define MAX_TREES 16
#define NLOBE 18  /* domed leaf clumps in one tree crown */
#define MAX_PAD 16 /* lily pads in the slack water of an inside bend */
#define MAX_COL 10 /* field columns per bank (between hedgerows) */
#define NBAND 3    /* fields stacked back from the river in each column */
#define MAX_SHEEP 14
#define MAX_RING 72 /* rise rings, leap splashes, snare slaps and the patter of drops */
#define MAX_GLIT 160 /* hat glitter: sun flashing off ripple crests for a frame or three */
#define MAX_FLY 26  /* mayflies dancing over the water */
#define NDUCK 5     /* a duck and her ducklings */
#define DHIST 96    /* the duck's wake history the ducklings follow */
#define MAX_WEED 7  /* beds of water-crowfoot streaming in the current */
#define NDRAG 4    /* dragonflies */
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
	uint8_t kind; /* 0 a rise (a trout sipping a fly), 1 a leap splash, 2 a slap (snare): a crown of spray,
		       * 3 a drop shaken off the leaves */
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
	float u, y, age, life, v, sz;
	float dx_, dy_, dr_; /* where it was drawn last frame */
} glit_t;

typedef struct {
	float u, s;         /* position: along (0..1), lateral (-1..1) */
	float hu, hs;       /* station the fish holds against the current */
	float len, ph, dart, hd;
	float gone; /* 1 just after a leap: the shadow is out of the water */
} fish_t;

typedef struct {
	float x, y, r, h; /* centre offset, radius and height of the dome, in units of the crown radius */
} lobe_t;

typedef struct {
	float u, side, off, r, ph;
	lobe_t lb[NLOBE];
	int nl;
} tree_t;

typedef struct {
	float u, lat, r, notch, ph; /* along the river, |lateral| on the inside bank, radius, notch angle */
	int side, flower;
} pad_t;

/* a field seen from above: rows at an angle (plough furrows / crop drills), or a flowery meadow,
 * or a pasture with sheep. Bounded by the hedgerows either side and by band edges back from the river. */
enum { F_PLOUGH, F_CROP, F_MEADOW, F_PASTURE };
typedef struct {
	uint8_t kind, edge; /* edge: its far boundary is 0 open, 1 hedge, 2 fence */
	float ang, sp, ph, ca, sa, tone;
} field_t;

typedef struct {
	int side, col, band;
	float p, q, hd, ph, step;
	float flee; /* startled by a big hit, eases off */
} sheep_t;

typedef struct {
	us_rng rng;
	us_rng crng; /* the animals' own random stream */
	int inited;
	/* countryside, per bank (0 = far bank / top, 1 = near bank / bottom) */
	int ncol[2];
	float hx[2][MAX_COL + 1], hlean[2][MAX_COL + 1], hgap[2][MAX_COL + 1];
	uint8_t hk[2][MAX_COL + 1]; /* 1: this boundary is a hedgerow */
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
	uint8_t *wf; /* the water body as drawn last frame, so it can dim faster than the trails */
	float *wcol; /* per-column fill parameters */
	int wf_w, wf_h;
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
	float cv[NX]; /* curvature of the centreline: its sign says which bank is the inside of the bend */
	float br_u; /* the footbridge, along the river */
	pad_t pad[MAX_PAD];
	int npad;
	float *cz; /* canopy scratch: height, shade, cover, shadow mask per pixel of the crown's box */
	int cz_n;
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
	float kglow, lvl_env, flap; /* kick light in the water body, eased loudness, the duck's startled flap */
	glit_t gt[MAX_GLIT];
	int ngt;
	float hi_env, lm_env; /* eased highs (mayflies) and low-mids + sub (weed) */
	float shake; /* snare: the jolt through the trees, leaves flipping their pale undersides to the sun */
	/* a kingfisher on a dead branch reaching out over the water from one bankside tree */
	int kf_tree, kf_mode; /* 0 perched, 1 diving, 2 under, 3 back to the branch, 4 away (roosting) */
	float kf_x, kf_y, kf_ang, kf_R, kf_t, kf_tx, kf_ty, kf_fish, kf_bob, kf_look, kf_look_t, kf_cd, kf_beat, kf_hd;
	/* a grey heron fishing the shallows; it lifts off on big wings when the snares pile up */
	int hn_mode; /* 0 standing, 1 flying off, 2 away, 3 flying in */
	float hn_px, hn_py, hn_pa; /* where she stands and faces, held between steps */
	int hn_pin;
	float hn_scd; /* strike cooldown */
	float hn_u, hn_side, hn_hd, hn_neck, hn_strike, hn_t, hn_x0, hn_y0, hn_x1, hn_y1, hn_step, hn_cd, hn_alarm, hn_gulp;
	/* dragonflies hawking over the margins: hover, dart, hover */
	float dr_x[NDRAG], dr_y[NDRAG], dr_x0[NDRAG], dr_y0[NDRAG], dr_tx[NDRAG], dr_ty[NDRAG], dr_hd[NDRAG], dr_t[NDRAG],
		dr_dart[NDRAG], dr_glint[NDRAG];
	uint8_t dr_on[NDRAG];
	/* where the moving animals were drawn last frame (wiped like the floaters, so they fly instead of smear) */
	float wp_x[8], wp_y[8], wp_r[8];
	uint8_t wp_m[8];
	int nwp;
} st_t;

static float fish_w(float l);
static float hsh(float a, float b);

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x72697672u);
	us_rng_seed(&s->crng, seed ^ 0x6c696665u);
	s->seed_off = us_rand(&s->rng) * 100.0f;
	return s;
}

static void destroy(void *p)
{
	st_t *st = p;
	free(st->lc_ink);
	free(st->lc_mat);
	free(st->wf);
	free(st->wcol);
	free(st->cz);
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
	/* curvature, smoothed: > 0 bends the inside toward the top bank (y down), < 0 toward the bottom */
	float tmp[NX], dx = A / (NX - 1);
	for (int i = 0; i < NX; i++) {
		int a = i > 1 ? i - 2 : 0, b = i < NX - 2 ? i + 2 : NX - 1, m = (a + b) / 2;
		float hh = dx * (b - a) * 0.5f;
		tmp[i] = (st->cy[b] - 2.0f * st->cy[m] + st->cy[a]) / (hh * hh);
	}
	for (int i = 0; i < NX; i++) {
		float sm = 0;
		int n = 0;
		for (int k = i - 4; k <= i + 4; k++)
			if (k >= 0 && k < NX) {
				sm += tmp[k];
				n++;
			}
		st->cv[i] = -sm / n; /* y grows downward: flip so + means the inside is the top bank */
	}
}

/* how much the bank on `side` (-1 top, +1 bottom) is the inside of a bend at x: slack water, a sandbar */
static float inside_at(const st_t *st, float x, float side)
{
	float f = us_clampf(x / st->A, 0, 1) * (NX - 1.001f);
	int i = (int)f;
	float cv = us_lerp(st->cv[i], st->cv[i + 1], f - i);
	return us_smooth(0.05f, 0.35f, -side * cv);
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
	/* the footbridge: the focal point, a little downstream of the middle of the reach */
	st->br_u = us_randr(r, 0.56f, 0.66f);
	for (int k = 0; k < MAX_ROCKS; k++) /* no stone right under the bridge */
		if (fabsf(st->rk[k].u - st->br_u) < 0.05f)
			st->rk[k].u += st->rk[k].u < st->br_u ? -0.05f : 0.05f;
	for (int i = 0; i < MAX_TREES; i++) {
		tree_t *tr = &st->tree[i];
		/* alternate banks, spread along the river; every other riverside tree stands with a neighbour,
		 * so the banks carry clumps and open stretches rather than a row */
		float gu = 0.41f + 0.618034f * (i / 2 * 2);
		tr->u = gu - floorf(gu);
		tr->side = ((i / 2) & 1) ? 1.0f : -1.0f;
		int far = (i % 8) == 5 || (i % 8) == 7; /* a few stand back in the fields */
		if (i & 1)
			tr->u += us_randr(r, 0.035f, 0.06f) * (i & 2 ? 1.0f : -1.0f);
		if (fabsf(tr->u - st->br_u) < 0.07f && !far) /* keep the bridge in the open */
			tr->u += tr->u < st->br_u ? -0.06f : 0.06f;
		tr->off = far ? us_randr(r, 0.1f, 0.42f) : us_randr(r, -0.028f, 0.0f);
		tr->r = far ? us_randr(r, 0.035f, 0.055f) : us_randr(r, 0.045f, 0.075f) * ((i & 1) ? 0.8f : 1.0f);
		tr->ph = us_rand(r) * 20.0f;
		/* the crown: a high central dome, a ring of big clumps around it, then small clumps along the rim
		 * (a leafy, broken edge) and a few on top */
		lobe_t *L = tr->lb;
		int n = 0;
		L[n++] = (lobe_t){us_randr(r, -0.1f, 0.1f), us_randr(r, -0.1f, 0.1f), us_randr(r, 0.4f, 0.5f), 0.45f};
		int ring = 5 + (int)(us_rand(r) * 3.0f);
		float a0 = us_rand(r) * US_TAU;
		for (int k = 0; k < ring && n < NLOBE; k++) {
			float th = a0 + US_TAU * (k + us_randr(r, -0.25f, 0.25f)) / ring;
			float lr = us_randr(r, 0.28f, 0.4f), dd = us_randr(r, 0.78f, 0.92f) - lr;
			L[n++] = (lobe_t){cosf(th) * dd, sinf(th) * dd, lr, us_randr(r, 0.1f, 0.32f)};
		}
		int rim = 7;
		for (int k = 0; k < rim && n < NLOBE - 2; k++) {
			float th = a0 + US_TAU * (k + us_randr(r, 0.2f, 0.8f)) / rim;
			float lr = us_randr(r, 0.13f, 0.2f), dd = us_randr(r, 0.88f, 1.02f) - lr;
			L[n++] = (lobe_t){cosf(th) * dd, sinf(th) * dd, lr, us_randr(r, 0.1f, 0.28f)};
		}
		while (n < NLOBE) {
			float th = us_rand(r) * US_TAU, dd = us_randr(r, 0.2f, 0.42f);
			L[n++] = (lobe_t){cosf(th) * dd, sinf(th) * dd, us_randr(r, 0.18f, 0.26f), us_randr(r, 0.38f, 0.52f)};
		}
		tr->nl = n;
	}
	/* lily pads: a raft or two in the slack water of the strongest inside bends */
	st->npad = 0;
	for (int g = 0; g < 2; g++) {
		float best = -1.0f, bu = 0.0f;
		for (int i = NX / 7; i < NX - NX / 7; i++) {
			float u = (float)i / (NX - 1);
			if (fabsf(u - st->br_u) < 0.08f || (g && fabsf(u - st->pad[0].u) < 0.25f))
				continue;
			if (fabsf(st->cv[i]) > best) {
				best = fabsf(st->cv[i]);
				bu = u;
			}
		}
		int side = st->cv[(int)(bu * (NX - 1))] > 0 ? -1 : 1;
		int n = g ? 5 : 8;
		for (int k = 0; k < n && st->npad < MAX_PAD; k++) {
			pad_t *pd = &st->pad[st->npad++];
			pd->u = bu + us_randn(r) * 0.03f;
			pd->side = side;
			pd->lat = us_randr(r, 0.62f, 0.9f);
			pd->r = us_randr(r, 0.011f, 0.017f) * (k < 2 ? 1.2f : 1.0f);
			pd->notch = us_rand(r) * US_TAU;
			pd->ph = us_rand(r) * US_TAU;
			pd->flower = k == 1 || k == 4;
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
			st->hk[sd][n] = hsh(n * 5.7f + sd, st->seed_off) > 0.5f; /* about half the boundaries are hedged */
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
				f->kind = u < 0.2f ? F_PLOUGH : u < 0.62f ? F_CROP : u < 0.82f ? F_MEADOW : F_PASTURE;
				/* rows mostly along or across the field, sometimes on the slant */
				float ar = us_rand(r);
				f->ang = ar < 0.4f ? 0.0f : ar < 0.75f ? US_PI * 0.5f : us_randr(r, -0.9f, 0.9f);
				f->ang += us_randr(r, -0.12f, 0.12f);
				f->ca = cosf(f->ang);
				f->sa = sinf(f->ang);
				f->sp = us_randr(r, 0.85f, 1.2f);
				f->ph = us_rand(r) * 50.0f;
				f->tone = us_randr(r, 0.78f, 1.25f);
				u = us_rand(r);
				/* most fields just meet; a few are hedged or fenced */
				f->edge = u < 0.24f ? 1 : u < 0.36f ? 2 : 0;
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
	/* the kingfisher's tree: the biggest bankside crown among the first few (always drawn), clear of the bridge */
	st->kf_tree = 0;
	{
		float best = -1.0f;
		for (int i = 0; i < 5; i++) {
			const tree_t *tr = &st->tree[i];
			if (tr->off > 0.05f || fabsf(tr->u - st->br_u) < 0.1f || tr->u < 0.08f || tr->u > 0.92f)
				continue;
			if (tr->r > best) {
				best = tr->r;
				st->kf_tree = i;
			}
		}
	}
	st->kf_cd = 3.0f;
	/* the heron's first spot, and her heading (hashed from the seed: the scene's random stream stays as it was) */
	st->hn_u = 0.15f + 0.7f * hsh(st->seed_off, 1.0f);
	st->hn_side = hsh(st->seed_off, 2.0f) < 0.5f ? -1.0f : 1.0f;
	st->hn_hd = hsh(st->seed_off, 3.0f) - 0.5f;
	st->hn_step = 3.0f + 5.0f * hsh(st->seed_off, 4.0f);
	st->hn_cd = 20.0f;
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

/* darken the canvas inside a disc (keep = what remains): shade under a floating thing */
static void carve_crown(us_canvas *c, float x, float y, float R, float keep)
{
	float r2 = R * R, inv = 1.0f / c->s;
	int y0 = (int)floorf((y - R) * c->s), y1 = (int)ceilf((y + R) * c->s);
	int k8 = (int)(keep * 256.0f);
	for (int j = y0 < 0 ? 0 : y0; j <= y1 && j < c->h; j++) {
		float dy = (j + 0.5f) * inv - y, dy2 = dy * dy;
		if (dy2 > r2)
			continue;
		float ho = sqrtf(r2 - dy2);
		int i0 = (int)ceilf((x - ho) * c->s - 0.5f), i1 = (int)floorf((x + ho) * c->s - 0.5f);
		if (i0 < 0)
			i0 = 0;
		if (i1 > c->w - 1)
			i1 = c->w - 1;
		uint8_t *row = c->ink + (size_t)j * c->w;
		if (k8 == 0 && i1 >= i0)
			memset(row + i0, 0, (size_t)(i1 - i0 + 1));
		else
			for (int i = i0; i <= i1; i++)
				row[i] = (uint8_t)((row[i] * k8) >> 8);
	}
}

/* foliage seen from above: a cluster of overlapping domed clumps. Each pixel shows the highest clump over
 * it, lit from the upper left, so every clump has a bright shoulder and a shaded underside and the low ones
 * sit in the shade of the high ones; the whole crown first casts its shadow down and to the right (skeep:
 * what the shadow leaves of whatever is under it). Lobes in units of R around (x, y); wob nudges the high
 * clumps with the wind. Overwrites what is under it (alpha: how solid). */
static float dome_z[256], dome_n[256];
static int dome_ready;

/* leaf bunches: a tiling texture of overlapping little domes (a jittered cell pattern) that breaks every
 * clump into bunches of leaves: it raises their tops, notches the rim between them and shades the crevices,
 * so a crown reads as foliage seen from above rather than as smooth balls. Gradients light each bunch. */
#define LTN 64
static float leaf_t[LTN * LTN], leaf_gx[LTN * LTN], leaf_gy[LTN * LTN];
static int leaf_ready;

static void leaf_init(void)
{
	float px[160], py[160], pr[160], pk[160];
	int np = 0;
	uint32_t h = 0x9e3779b9u;
	for (int cy = 0; cy < 8; cy++)
		for (int cx = 0; cx < 8; cx++)
			for (int k = 0; k < 2 && np < 160; k++) {
				float f[4];
				for (int q = 0; q < 4; q++) {
					h ^= h << 13;
					h ^= h >> 17;
					h ^= h << 5;
					f[q] = (h & 0xffff) / 65536.0f;
				}
				if (k == 1 && f[3] < 0.45f)
					continue;
				px[np] = (cx + f[0]) * 8.0f;
				py[np] = (cy + f[1]) * 8.0f;
				pr[np] = 4.2f + 2.6f * f[2];
				pk[np] = 0.65f + 0.35f * f[3];
				np++;
			}
	for (int j = 0; j < LTN; j++)
		for (int i = 0; i < LTN; i++) {
			float best = 0.0f;
			for (int q = 0; q < np; q++) {
				float dx = fabsf(i + 0.5f - px[q]), dy = fabsf(j + 0.5f - py[q]);
				if (dx > LTN * 0.5f)
					dx = LTN - dx;
				if (dy > LTN * 0.5f)
					dy = LTN - dy;
				float b = 1.0f - (dx * dx + dy * dy) / (pr[q] * pr[q]);
				if (b > 0) {
					float z = sqrtf(b) * pk[q];
					if (z > best)
						best = z;
				}
			}
			leaf_t[j * LTN + i] = best;
		}
	for (int j = 0; j < LTN; j++)
		for (int i = 0; i < LTN; i++) {
			int ip = (i + 1) & (LTN - 1), im = (i - 1) & (LTN - 1), jp = (j + 1) & (LTN - 1), jm = (j - 1) & (LTN - 1);
			leaf_gx[j * LTN + i] = 0.5f * (leaf_t[j * LTN + ip] - leaf_t[j * LTN + im]);
			leaf_gy[j * LTN + i] = 0.5f * (leaf_t[jp * LTN + i] - leaf_t[jm * LTN + i]);
		}
	leaf_ready = 1;
}

static void canopy(us_canvas *c, st_t *st, const lobe_t *lb, int n, float x, float y, float R, float wx, float wy, float v,
		   float alpha, uint8_t mat, float sx, float sy, float skeep)
{
	float s = c->s, inv = 1.0f / s;
	float lx[NLOBE], ly[NLOBE], lr[NLOBE], lh[NLOBE];
	if (n > NLOBE)
		n = NLOBE;
	float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f, hmin = 1e9f, hmax = -1e9f;
	for (int k = 0; k < n; k++) {
		lx[k] = x + (lb[k].x + wx * lb[k].h) * R;
		ly[k] = y + (lb[k].y + wy * lb[k].h) * R;
		lr[k] = lb[k].r * R;
		lh[k] = lb[k].h * R;
		float re = lr[k] * 1.14f; /* leaf bunches stand out past the clump's rim */
		bx0 = fminf(bx0, lx[k] - re);
		by0 = fminf(by0, ly[k] - re);
		bx1 = fmaxf(bx1, lx[k] + re + (skeep < 1.0f ? sx : 0));
		by1 = fmaxf(by1, ly[k] + re + (skeep < 1.0f ? sy : 0));
		hmin = fminf(hmin, lh[k]);
		hmax = fmaxf(hmax, lh[k]);
	}
	int i0 = (int)floorf(bx0 * s) - 1, j0 = (int)floorf(by0 * s) - 1, i1 = (int)ceilf(bx1 * s) + 1, j1 = (int)ceilf(by1 * s) + 1;
	if (i0 < 0) i0 = 0;
	if (j0 < 0) j0 = 0;
	if (i1 > c->w - 1) i1 = c->w - 1;
	if (j1 > c->h - 1) j1 = c->h - 1;
	if (i1 < i0 || j1 < j0)
		return;
	int bw = i1 - i0 + 1, bh = j1 - j0 + 1, N = bw * bh;
	if (N > st->cz_n) {
		float *nz = realloc(st->cz, sizeof(float) * 4 * (size_t)N);
		if (!nz)
			return;
		st->cz = nz;
		st->cz_n = N;
	}
	float *zb = st->cz, *sh = zb + N, *cov = sh + N, *msk = cov + N;
	for (int k = 0; k < N; k++) {
		zb[k] = -1e9f;
		cov[k] = 0;
		msk[k] = 0;
	}
	if (!dome_ready) {
		for (int q = 0; q < 256; q++) {
			float z = sqrtf(fmaxf(0.0f, 1.0f - (q + 0.5f) / 255.0f));
			dome_z[q] = q == 255 ? 0.0f : z;
			/* normal (x, y, 1.6 z) / |.|, with x^2 + y^2 = q: the domes are flattened a little */
			dome_n[q] = 1.0f / sqrtf((q + 0.5f) / 255.0f + 2.56f * dome_z[q] * dome_z[q]);
		}
		dome_ready = 1;
	}
	if (!leaf_ready)
		leaf_init();
	float hr = 1.0f / fmaxf(hmax - hmin, 1e-4f);
	int crisp = c->crisp;
	/* texels per unit: a bunch is ~10 texels across, ~a fifth of the crown radius (coarser on the pixel grid) */
	float lsc = (crisp ? 26.0f : 46.0f) / fmaxf(R, 1e-3f);
	if (lsc * inv < 0.35f) /* never finer than ~3 px a bunch */
		lsc = 0.35f * s;
	for (int k = 0; k < n; k++) {
		float r = lr[k], r2 = r * r, ir = 1.0f / r, re = r * 1.14f + inv;
		/* the shadow of this clump */
		if (skeep < 1.0f && lb[k].r > 0.22f) { /* the big clumps make the silhouette of the shadow */
			float cx = lx[k] + sx, cy = ly[k] + sy;
			int a0 = (int)floorf((cx - r) * s) - i0, a1 = (int)ceilf((cx + r) * s) - i0;
			int b0 = (int)floorf((cy - r) * s) - j0, b1 = (int)ceilf((cy + r) * s) - j0;
			for (int j = b0 < 0 ? 0 : b0; j <= b1 && j < bh; j++) {
				float dy = (j + j0 + 0.5f) * inv - cy;
				for (int i = a0 < 0 ? 0 : a0; i <= a1 && i < bw; i++) {
					float dx = (i + i0 + 0.5f) * inv - cx;
					if (dx * dx + dy * dy < r2)
						msk[j * bw + i] = 1.0f;
				}
			}
		}
		float ao = 0.62f + 0.38f * (lh[k] - hmin) * hr; /* the low clumps sit in the shade of the high */
		float re2 = re * re, ir2 = ir * ir;
		float toff = k * 13.7f + lb[k].r * 91.0f; /* each clump its own patch of the leaf texture */
		/* pinned to where the clump sits at rest, in whole pixels: as the wind sways the clumps the bunches
		 * stay put under them instead of shimmering */
		float lpx = floorf((x + lb[k].x * R) * s + 0.5f), lpy = floorf((y + lb[k].y * R) * s + 0.5f), tsc = lsc * inv;
		int a0 = (int)floorf((lx[k] - re) * s) - i0, a1 = (int)ceilf((lx[k] + re) * s) - i0;
		int b0 = (int)floorf((ly[k] - re) * s) - j0, b1 = (int)ceilf((ly[k] + re) * s) - j0;
		for (int j = b0 < 0 ? 0 : b0; j <= b1 && j < bh; j++) {
			float dy = (j + j0 + 0.5f) * inv - ly[k];
			int tj = ((int)floorf((j + j0 - lpy) * tsc + toff * 0.7f)) & (LTN - 1);
			for (int i = a0 < 0 ? 0 : a0; i <= a1 && i < bw; i++) {
				float dx = (i + i0 + 0.5f) * inv - lx[k];
				float d2 = dx * dx + dy * dy;
				if (d2 > re2)
					continue;
				int ti = tj * LTN + (((int)floorf((i + i0 - lpx) * tsc + toff)) & (LTN - 1));
				float tq = leaf_t[ti];
				/* the rim follows the bunches: proud where a bunch sits, notched between them */
				float rf = crisp ? 0.9f + 0.17f * tq : 0.8f + 0.34f * tq, qe = d2 * ir2 / (rf * rf);
				if (qe >= 1.0f && crisp)
					continue;
				float cv = 1.0f;
				if (qe > 0.8f) { /* only the rim needs a distance */
					if (!crisp) {
						cv = us_sat((1.0f - sqrtf(qe)) * r * rf * s + 0.5f);
						if (cv <= 0)
							continue;
					}
				}
				int o = j * bw + i;
				if (cv > cov[o])
					cov[o] = cv;
				/* dome height and normal from a table over (d / r)^2, the bunch standing proud of it */
				int qi = qe < 1.0f ? (int)(qe * 255.0f) : 255;
				float hz = lh[k] + r * (dome_z[qi] + 0.16f * tq);
				if (hz > zb[o]) {
					zb[o] = hz;
					float nl = dome_n[qi];
					float df = us_sat(0.25f + 0.75f * ((-0.5f * dx - 0.62f * dy) * ir * nl + 0.96f * dome_z[qi] * nl));
					/* each bunch lit on its upper left, its lower right in shade, dark crevices between */
					float tl = (-0.5f * leaf_gx[ti] - 0.62f * leaf_gy[ti]) * 4.0f;
					float bl = crisp ? us_clampf(0.84f + 0.16f * tq + 0.25f * tl, 0.75f, 1.08f)
							 : us_clampf(0.42f + 0.58f * tq + tl, 0.2f, 1.25f);
					sh[o] = (0.14f + 0.86f * df * df) * ao * bl;
					if (crisp && sh[o] < 0.3f) /* the pixel palette: a lone bunch on the shaded rim, never a black hole */
						sh[o] = 0.3f;
				}
			}
		}
	}
	int k8 = (int)(skeep * 256.0f);
	for (int j = 0; j < bh; j++) {
		uint8_t *ink = c->ink + (size_t)(j + j0) * c->w + i0, *mt = c->mat + (size_t)(j + j0) * c->w + i0;
		for (int i = 0; i < bw; i++) {
			int o = j * bw + i;
			if (msk[o] > 0) {
				int sv = (ink[i] * k8) >> 8;
				if (crisp && sv < 34) /* the pixel palette: shade a step down, never to the ground colour */
					sv = ink[i] < 34 ? ink[i] : 34;
				ink[i] = (uint8_t)sv;
			}
			float a = cov[o] * alpha;
			if (a <= 0.0f)
				continue;
			float val = v * sh[o];
			if (!crisp) { /* leaf grain: a fixed speckle, brighter on the lit side */
				uint32_t hh = (uint32_t)(i + i0) * 0x9E3779B1u ^ (uint32_t)(j + j0) * 0x85EBCA77u;
				hh ^= hh >> 13;
				hh *= 0x5bd1e995u;
				hh ^= hh >> 15;
				val *= 0.8f + 0.4f * (float)(hh & 255) / 255.0f;
			}
			float nv = val * 255.0f;
			if (crisp) {
				if (a >= 0.5f) {
					ink[i] = (uint8_t)(nv > 255 ? 255 : nv);
					mt[i] = mat;
				}
			} else {
				float o2 = ink[i] + (nv - ink[i]) * a;
				ink[i] = (uint8_t)(o2 > 255 ? 255 : o2 < 0 ? 0 : o2 + 0.5f);
				if (a > 0.4f)
					mt[i] = mat;
			}
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

/* a hedge bush from above: two or three domed clumps, lit on the upper left, with a short shadow */
static void bush(us_canvas *c, st_t *st, float x, float y, float r, float v, uint8_t mat, float hj)
{
	if (y < -r * 2 || y > 1.0f + r * 2)
		return;
	float th = hj * 40.0f, h2 = hj * 7.3f - floorf(hj * 7.3f);
	lobe_t lb[3] = {{0, 0, 0.62f, 0.45f},
			{cosf(th) * 0.45f, sinf(th) * 0.4f, 0.45f + 0.1f * h2, 0.2f},
			{-cosf(th) * 0.4f, -sinf(th) * 0.45f, 0.42f, 0.3f * h2}};
	canopy(c, st, lb, h2 < 0.3f ? 2 : 3, x, y, r * 1.5f, 0, 0, v, 1.0f, mat, r * 0.55f, r * 0.45f, c->crisp ? 0.8f : 0.55f);
}

static void draw_land(us_canvas *c, st_t *st, const us_ctx *ctx, float wind, float bloom)
{
	const us_world *w = ctx->w;
	int crisp = ctx->crisp;
	float dens = ctx->p->density, t = (float)ctx->t;
	float winter = us_world_winter(w), autumn = us_world_season_w(w, 2), spring = us_world_season_w(w, 0),
	      summer = us_world_season_w(w, 1);
	float snow = us_smooth(0.5f, 0.85f, winter);
	uint8_t hedge_m = snow > 0.5f ? US_MAT_STEM : US_MAT_LEAF;
	float lush = 1.0f - 0.5f * snow;

	for (int sd = 0; sd < 2; sd++)
		for (int k = 0; k < st->ncol[sd]; k++)
			for (int b = 0; b < NBAND; b++) {
				const field_t *f = &st->fd[sd][k][b];
				/* meadow and pasture: grass ticks, wildflowers, sheep */
				if (f->kind == F_MEADOW || f->kind == F_PASTURE) {
					const float(*e)[2] = st->bd[sd][k];
					float D = 0.5f * ((e[b + 1][0] - e[b][0]) + (e[b + 1][1] - e[b][1]));
					float W = st->hx[sd][k + 1] - st->hx[sd][k];
					if (b == NBAND - 1)
						D = fminf(D, 0.4f);
					int meadow = f->kind == F_MEADOW;
					if (!meadow) /* the pasture is the sheep's: its tone is in the land fill */
						continue;
					int n = (int)(W * D * 700.0f * (0.45f + 0.8f * dens));
					if (n > 260)
						n = 260;
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
							float v = (0.36f + 0.26f * hp) * sh * (0.75f + 0.25f * bloom);
							if (crisp)
								us_put(c, (int)(x * c->s), (int)(y * c->s), 0.62f + 0.3f * hp, fm);
							else
								us_point(c, x, y, rr, v, fm);
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
	float br = 0.0093f, bv = (crisp ? 0.8f : 0.36f) * (0.8f + 0.2f * lush);
	if (crisp && snow > 0.5f)
		bv = 0.8f; /* bare hedges must stand above the bright snowfield fill */
	float step = 0.012f; /* fence posts */
	for (int sd = 0; sd < 2; sd++) {
		for (int k = 0; k <= st->ncol[sd]; k++) {
			float hx = st->hx[sd][k];
			if (hx < -0.05f || hx > st->A + 0.05f)
				continue;
			/* the hedge itself is in the land fill; here only the odd taller tree standing in it */
			if (!st->hk[sd][k])
				continue;
			for (float d = D0 + 0.03f; d < 0.7f; d += 0.055f) {
				int j = (int)(d / 0.055f);
				float hj = hsh(j + k * 17.0f, sd + 3.0f), hj2 = hsh(j * 1.9f + k, sd + 11.0f);
				if (hj < 0.78f)
					continue;
				float dd = d + (hj2 - 0.5f) * 0.03f;
				if (us_noise2(dd * 7.0f, (k * 7 + sd * 3 + 1) * 2.11f + 9.0f) <= -0.2f) /* not in a gap */
					continue;
				float x, y;
				land_xy(st, sd, hx + st->hlean[sd][k] * dd, dd, &x, &y);
				if (y < -0.05f || y > 1.05f)
					continue;
				float sh = shade_at(st, x, y);
				float rr = br * (1.9f + 1.0f * hj2);
				float sx = 0.0015f * wind * lgrid(st, st->lgust, x, y);
				bush(c, st, x + sx, y, rr, bv * sh, hedge_m, hj);
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
					if (f->edge == 1) { /* hedged: drawn in the land fill */
						break;
					} else if ((i & 1) == 0) {
						/* post and wire */
						us_point(c, x, y, crisp ? 0.001f : 0.0012f, 0.3f * sh, US_MAT_EARTH);
						if (i > 1 && !crisp)
							hl(c, px, py, x, y, 0.1f * sh, US_MAT_STEM);
						px = x;
						py = y;
						continue;
					} else {
						continue;
					}
					px = x;
					py = y;
				}
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

/* the land filled per pixel, softly: every field its own tone, crop rows a faint ribbing, no outlines; the
 * riverside grass strip, the towpath and the path to the bridge trodden into it, and sand on the inside of
 * each bend. The fields barely move (the channel drifts over minutes), so the classification is cached and a
 * quarter of the columns are refreshed each frame; only the water edge is exact every frame. Cloud shadows
 * slide over the cached tone. */
static float bridge_path_x(const st_t *st, float d)
{
	return st->br_u * st->A + 0.012f * sinf(d * 11.0f + st->seed_off) * us_sat(d * 8.0f);
}

static void land_fill(us_canvas *c, st_t *st, const us_ctx *ctx)
{
	int crisp = c->crisp;
	float winter = us_world_winter(ctx->w), autumn = us_world_season_w(ctx->w, 2), summer = us_world_season_w(ctx->w, 1);
	float snow = us_smooth(0.5f, 0.85f, winter);
	float ripe = us_smooth(0.3f, 0.7f, 0.6f * summer + autumn);
	float inv = 1.0f / c->s;
	float spc = crisp ? 3.0f * inv : 0.0062f;
	/* tone scale: the pixel palette needs a few steps of headroom, ink sits low and dark */
	float K = crisp ? 0.82f : 0.4f;
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
	st->lc_phase = (st->lc_phase + 1) & 15;
	int tow = (int)st->path_side;
	for (int px = 0; px < c->w; px++) {
		if (!(full || ((px + st->lc_phase) & 15) == 0))
			continue;
		float x = (px + 0.5f) * inv, cc, h, sl, hb;
		chan(st, x, &cc, &h, &sl);
		bank_at(st, x, &cc, &hb);
		float ins[2] = {inside_at(st, x, -1.0f), inside_at(st, x, 1.0f)};
		float dtow = 0.027f + 0.009f * us_noise2(x * 2.3f, st->seed_off + 7.0f);
		float sn = us_noise2(x * 9.0f, st->seed_off + 2.0f);
		int nearb = fabsf(x - st->br_u * st->A) < 0.02f;
		int kk[2] = {0, 0};
		uint8_t *ci = st->lc_ink + px, *cm = st->lc_mat + px;
		for (int py = 0; py < c->h; py++, ci += c->w, cm += c->w) {
			float v;
			uint8_t m;
			float y = (py + 0.5f) * inv;
			int sd = y > cc;
			/* the cloud shadows are baked in: they drift slowly next to the refresh */
			float shf = 0.62f + 0.38f * shade_at(st, x, y);
			float dist = sd ? y - cc : cc - y;
			float d = dist - hb, dw = dist - h;
			if (dw < 0) { /* the water, as it was when this column was refreshed: left to the water fill */
				*ci = crisp ? 23 : 0;
				*cm = US_MAT_WATER;
				continue;
			}
			/* sand and shingle on the inside of the bend, a muddy lip elsewhere */
			float sw = (0.006f + 0.032f * ins[sd]) * (0.8f + 0.3f * sn);
			if (dw < sw && snow < 0.5f) {
				float f = dw / sw;
				if (crisp)
					v = ins[sd] > 0.25f ? 0.58f - 0.14f * f : 0.2f;
				else {
					uint32_t hh = (uint32_t)px * 0x9E3779B1u ^ (uint32_t)py * 0x85EBCA77u;
					hh ^= hh >> 13;
					hh *= 0x5bd1e995u;
					hh ^= hh >> 15;
					/* sand and shingle: pale, grainy, fading into the grass */
					v = (0.08f + 0.17f * ins[sd]) * (0.8f + 0.4f * (float)(hh & 255) / 255.0f);
					/* grass creeps over the top of the bar */
					float g = us_smooth(0.45f, 1.0f, f + 0.25f * ((float)((hh >> 8) & 255) / 255.0f - 0.5f));
					v = us_lerp(v, 0.26f * K, g);
					if (g > 0.6f) {
						*ci = (uint8_t)(v * shf * 255.0f + 0.5f);
						*cm = US_MAT_LEAF;
						continue;
					}
				}
				m = US_MAT_EARTH;
			} else if (d < D0) {
				int path = (sd == tow && fabsf(d - dtow) < 0.0045f) ||
					   (nearb && d < (sd == tow ? dtow : 0.4f) && fabsf(x - bridge_path_x(st, d)) < 0.0032f);
				if (snow > 0.5f) {
					v = (path ? 0.5f : 0.62f) * (crisp ? 1.0f : 0.5f);
					m = US_MAT_ACCENT;
				} else if (path) {
					v = crisp ? 0.36f : 0.15f;
					m = US_MAT_EARTH;
				} else {
					v = 0.26f * K;
					m = US_MAT_LEAF;
				}
			} else {
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
				float fr = pr - floorf(pr);
				/* rows: a hard stripe on the pixel grid, a soft ribbing in ink */
				float rib = crisp ? (fr < 0.42f ? 1.0f : 0.0f) : (2.0f * fr - 1.0f) * (2.0f * fr - 1.0f);
				int path = nearb && d < 0.4f && fabsf(x - bridge_path_x(st, d)) < 0.0032f;
				/* neighbouring fields differ gently: a narrow tone range, and on the pixel grid every field sits
				 * inside one palette step */
				float tn = 1.0f + 0.45f * (f->tone - 1.0f);
				if (snow > 0.5f) { /* snowfields: furrows show as a faint brighter ribbing */
					v = (f->kind != F_MEADOW && f->kind != F_PASTURE ? 0.62f + 0.2f * rib : 0.62f) * (crisp ? 1.0f : 0.5f);
					m = US_MAT_ACCENT;
				} else if (path) {
					v = crisp ? 0.36f : 0.15f;
					m = US_MAT_EARTH;
				} else if (f->kind == F_PLOUGH) {
					/* brown furrows between green strips: a ploughed field reads as a texture, not a dark block */
					v = crisp ? (rib > 0.5f ? 0.46f : 0.22f * tn) : (rib > 0.5f ? 0.36f * tn * K : 0.23f * tn * K);
					m = rib < 0.5f ? US_MAT_LEAF : US_MAT_EARTH;
				} else if (f->kind == F_CROP) {
					v = crisp ? 0.22f * tn : (0.22f + 0.05f * rib) * tn * K;
					m = ripe > 0.5f ? US_MAT_LIGHT : US_MAT_LEAF;
					if (ripe > 0.5f && !crisp)
						v *= 0.8f;
				} else if (f->kind == F_MEADOW) {
					v = crisp ? 0.23f * tn : 0.24f * tn * K;
					m = US_MAT_LEAF;
				} else {
					v = crisp ? 0.21f * tn : 0.21f * tn * K;
					m = US_MAT_LEAF;
				}
				/* hedgerows: one soft, lumpy band along a boundary, lit on its upper-left edge, its shadow
				 * falling on the field below-right; gaps here and there */
				{
					float hw0 = crisp ? 0.0125f : 0.0115f, best = 2.0f, sgn = 0, along = 0;
					int hid = 0;
					/* the hedges either side of this column (screen-horizontal offset) */
					if (X < hw0 * 1.7f && st->hk[sd][k]) {
						best = X / hw0;
						sgn = 1.0f; /* the field lies to the right: the hedge's shadow side */
						along = d;
						hid = k * 7 + sd * 3 + 1;
					}
					if (xr - x < hw0 * 1.7f && st->hk[sd][k + 1] && (xr - x) / hw0 < best) {
						best = (xr - x) / hw0;
						sgn = -1.0f;
						along = d;
						hid = (k + 1) * 7 + sd * 3 + 1;
					}
					/* along the band edges: the near edge belongs to the band below, the far to this one */
					float db = e[b + 1][0] + (e[b + 1][1] - e[b + 1][0]) * p;
					float ys = sd ? 1.0f : -1.0f; /* d grows down the screen on the near bank */
					if (b > 0 && st->fd[sd][k][b - 1].edge == 1 && Y < hw0 * 1.7f && Y / hw0 < best) {
						best = Y / hw0;
						sgn = ys; /* the field is further from the river than the hedge */
						along = x * 1.3f;
						hid = k * 11 + b * 5 + sd * 2 + 40;
					}
					if (b < NBAND - 1 && f->edge == 1 && db - d < hw0 * 1.7f && (db - d) / hw0 < best) {
						best = (db - d) / hw0;
						sgn = -ys;
						along = x * 1.3f;
						hid = k * 11 + (b + 1) * 5 + sd * 2 + 40;
					}
					if (best < 1.7f) {
						float n1 = us_noise2(along * 55.0f, hid * 1.37f + st->seed_off);
						float gap = us_noise2(along * 7.0f, hid * 2.11f + 9.0f);
						float wl = 0.62f + 0.38f * n1 + 0.25f * us_noise2(along * 14.0f, hid + 3.0f); /* lumps + width */
						if (gap > -0.3f && wl > 0.15f) {
							float u = best / wl;
							/* the side facing the sun (sgn < 0: lit side toward the field) */
							float uu = u * (sgn > 0 ? 1.0f : -1.0f); /* -1 lit edge .. +1 shadow edge */
							if (u < 1.0f) {
								float dome = sqrtf(1.0f - u * u);
								float lit = us_sat(0.45f + 0.5f * dome - 0.45f * uu);
								float hv = (crisp ? 0.36f + 0.4f * lit : 0.08f + 0.17f * lit) * (0.85f + 0.3f * n1);
								if (!crisp) {
									uint32_t hh = (uint32_t)px * 0x9E3779B1u ^ (uint32_t)py * 0x85EBCA77u;
									hh ^= hh >> 13;
									hh *= 0x5bd1e995u;
									hh ^= hh >> 15;
									hv *= 0.82f + 0.36f * (float)(hh & 255) / 255.0f;
								}
								if (snow > 0.5f) { /* bare and dark against the snow */
									hv = crisp ? 0.3f + 0.15f * lit : 0.08f + 0.1f * lit;
									m = US_MAT_STEM;
								} else
									m = US_MAT_LEAF;
								v = hv;
							} else if (u < 1.6f && sgn > 0) {
								v *= crisp ? 0.8f : 0.6f; /* its shadow on the field */
							}
						}
					}
				}
			}
			*ci = (uint8_t)(v * shf * 255.0f + 0.5f);
			*cm = m;
		}
	}
	/* composite, row by row, with the cloud shadows */
	int W = c->w;
	for (int py = 0; py < c->h; py++) {
		uint8_t *restrict ink = c->ink + (size_t)py * W, *restrict mt = c->mat + (size_t)py * W;
		const uint8_t *restrict li = st->lc_ink + (size_t)py * W, *restrict lm = st->lc_mat + (size_t)py * W;
		for (int px = 0; px < W; px++) { /* branch-free so it vectorises */
			uint8_t iv = li[px], o = ink[px];
			uint8_t gt = (uint8_t)-(iv > o);
			ink[px] = (uint8_t)((iv & gt) | (o & ~gt));
			mt[px] = (uint8_t)((lm[px] & gt) | (mt[px] & ~gt));
		}
	}
}

/* the body of the water, under the streamlines: dark in the deep channel, lighter over the shallows, under
 * the slow cloud shadows; a sheen of sky drifts along it as the music swells, and every kick lifts the light
 * in the whole reach (most mid-stream, where the surge runs). Per column, only the rows of the channel. */
static void water_fill(us_canvas *c, st_t *st, const us_ctx *ctx)
{
	float inv = 1.0f / c->s;
	int crisp = c->crisp, W = c->w;
	float deep = crisp ? 0.08f : 0.02f, shal = crisp ? 0.18f : 0.095f;
	float lift = 0.35f + 1.15f * st->lvl_env;
	float ice = st->ice, icel2 = (0.86f - 0.28f * ice) * (0.86f - 0.28f * ice), icev = 0.25f * ice;
	float kg = st->kglow * (crisp ? 3.2f : 1.0f); /* the pixel palette steps coarsely: lift it further */
	float t = (float)ctx->t;
	if (!st->wf || st->wf_w != c->w || st->wf_h != c->h) {
		free(st->wf);
		free(st->wcol);
		st->wf = calloc((size_t)c->w * c->h, 1);
		st->wcol = malloc(sizeof(float) * 7 * (size_t)c->w);
		if (!st->wf || !st->wcol) {
			free(st->wf);
			free(st->wcol);
			st->wf = NULL;
			st->wcol = NULL;
			st->wf_w = st->wf_h = 0;
			return;
		}
		st->wf_w = c->w;
		st->wf_h = c->h;
	}
	uint8_t *wf = st->wf;
	float *col = st->wcol; /* per column: deep-line row, 1 / half-width in rows, a + b lat^2, margin, rows, centre row */
	int ymin = c->h, ymax = -1;
	for (int px = 0; px < W; px++) {
		float x = (px + 0.5f) * inv, cc, h, sl;
		chan(st, x, &cc, &h, &sl);
		float sh = shade_at(st, x, cc);
		float sg = surge_at(st, x);
		/* sky sheen: a soft brighter reach sliding slowly downstream, wider when the music swells */
		float sheen = us_smooth(0.15f - 0.35f * st->lvl_env, 0.85f, us_noise2(x * 1.3f - t * 0.03f, st->seed_off + 5.0f));
		float g = lift * (0.45f + 0.55f * sh) * (1.0f + 0.5f * sheen * st->lvl_env);
		float d0 = deep * g, d1 = shal * g;
		/* the kick: light swells through the body, strongest mid-stream */
		float k0 = (0.06f + 0.1f * st->lvl_env) * kg + 0.07f * sg, k1 = (crisp ? 0.11f : 0.035f) * kg + 0.02f * sg;
		int yt = (int)ceilf((cc - h) * c->s - 0.5f), yb = (int)floorf((cc + h) * c->s - 0.5f);
		if (yt < 0)
			yt = 0;
		if (yb > c->h - 1)
			yb = c->h - 1;
		/* rows that were water last frame but are bank now (the channel drifts) forget their fill */
		for (int k = 1; k <= 6; k++) {
			if (yt - k >= 0)
				wf[(size_t)(yt - k) * W + px] = 0;
			if (yb + k < c->h)
				wf[(size_t)(yb + k) * W + px] = 0;
		}
		float *q = col + 7 * px;
		/* the deep channel swings to the outside of each bend; the inside shelves into a wide shallow */
		float fi = us_clampf(x / st->A, 0, 1) * (NX - 1.001f);
		int ii = (int)fi;
		float cvx = us_lerp(st->cv[ii], st->cv[ii + 1], fi - ii);
		q[0] = (cc + 0.38f * h * us_clampf(cvx / 0.3f, -1.0f, 1.0f)) * c->s - 0.5f;
		q[1] = 1.0f / (h * c->s);
		q[2] = (d0 + k0) * 255.0f;
		q[3] = ((d1 - d0) + (k1 - k0)) * 255.0f;
		q[4] = (crisp ? 0.04f : 0.035f) * g * 255.0f; /* the sandy margin, lit through a few inches of water */
		q[5] = (float)(yt | (yb << 16));
		q[6] = cc * c->s - 0.5f; /* the true centre: shelf ice grows evenly from both banks */
		if (yb >= yt) {
			if (yt < ymin)
				ymin = yt;
			if (yb > ymax)
				ymax = yb;
		}
	}
	/* row-major, so the writes stream through memory */
	for (int py = ymin; py <= ymax; py++) {
		uint8_t *ink = c->ink + (size_t)py * W, *mat = c->mat + (size_t)py * W, *wr = wf + (size_t)py * W;
		for (int px = 0; px < W; px++) {
			const float *q = col + 7 * px;
			int rows = (int)q[5], yt = rows & 0xffff, yb = rows >> 16;
			if (py < yt || py > yb)
				continue;
			float lat = (py - q[0]) * q[1], l2 = fminf(lat * lat * 0.85f, 1.0f);
			float v = q[2] + q[3] * l2;
			if (l2 > 0.72f) {
				float m = us_sat((l2 - 0.72f) * (1.0f / 0.18f));
				v += q[4] * m * m * (3.0f - 2.0f * m);
			}
			float li0 = (py - q[6]) * q[1];
			if (ice > 0.05f && li0 * li0 > icel2)
				v = fmaxf(v, icev * 255.0f); /* shelf ice */
			int iv = (int)(v + 0.5f);
			if (iv > 255)
				iv = 255;
			/* a pixel still holding only last frame's (faded) body takes the new value even when it is darker:
			 * the kick's light ebbs at its own pace, not the trails' */
			if (iv > ink[px] || (ink[px] <= wr[px] && mat[px] == US_MAT_WATER)) {
				ink[px] = (uint8_t)iv;
				mat[px] = US_MAT_WATER;
			}
			wr[px] = (uint8_t)iv;
		}
	}
}

/* hat glitter: the sun flashing off ripple crests. Each flash lasts a few frames and is wiped before the next
 * frame (only its own material), so it twinkles instead of smearing under the trails. */
static void glitter_wipe(us_canvas *c, st_t *st)
{
	for (int i = 0; i < st->ngt; i++)
		if (st->gt[i].dr_ > 0)
			wipe_disc(c, st->gt[i].dx_, st->gt[i].dy_, st->gt[i].dr_, US_MAT_LIGHT);
}

static void glitter_step(us_canvas *c, st_t *st, const us_ctx *ctx)
{
	float A = st->A;
	for (int i = 0; i < st->ngt;) {
		glit_t *g = &st->gt[i];
		g->age += ctx->real_dt;
		if (g->age >= g->life) {
			st->gt[i] = st->gt[--st->ngt];
			continue;
		}
		i++;
		float x = g->u * A, y = g->y;
		float e = 1.0f - g->age / g->life;
		float v = g->v * (0.35f + 0.65f * e);
		float L = g->sz * (0.6f + 0.4f * e);
		g->dx_ = x;
		g->dy_ = y;
		g->dr_ = L * 1.1f + 1.5f / c->s;
		if (c->crisp) {
			int ix = (int)(x * c->s), iy = (int)(y * c->s);
			put1(c, ix, iy, fmaxf(v, 0.8f), US_MAT_LIGHT);
			if (e > 0.4f) { /* a little cross of light */
				put1(c, ix - 1, iy, v * 0.75f, US_MAT_LIGHT);
				put1(c, ix + 1, iy, v * 0.75f, US_MAT_LIGHT);
				if (g->sz > 0.008f) {
					put1(c, ix, iy - 1, v * 0.6f, US_MAT_LIGHT);
					put1(c, ix, iy + 1, v * 0.6f, US_MAT_LIGHT);
				}
			}
		} else {
			/* a four-point star, long across the current (ripple crests run across it) */
			hl(c, x - L, y, x + L, y, v, US_MAT_LIGHT);
			hl(c, x, y - L * 0.55f, x, y + L * 0.55f, v * 0.8f, US_MAT_LIGHT);
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
		float q3 = q * q * q;
		/* a slap's ring starts wide and runs out fast */
		float r = g->kind == 2 ? g->r1 * (0.45f + 0.55f * (1.0f - q3 * q3))
			  : g->kind == 3 ? g->r1 * (0.3f + 0.7f * (1.0f - q3 * q3))
					 : 0.0015f + g->r1 * (1.0f - q3);
		float v = g->v * powf(q, 1.6f) * us_sat(g->age / (g->kind == 2 ? 0.02f : 0.06f));
		/* the disturbed water inside the ring is smoothed flat for a moment: a darker disc that lets the
		 * bright crest read against the streamlines */
		float calm = (g->kind == 2 ? 0.9f : g->kind == 1 ? 0.6f : g->kind == 3 ? 0.0f : 0.4f) * q * q;
		if (g->kind == 2) { /* the slap: a white crown of spray stands up at once, then the rings run out */
			if (g->age < 0.2f) {
				float e = 1.0f - g->age / 0.2f, cr = g->r1 * (0.3f + 0.2f * (1.0f - e));
				if (ctx->crisp)
					us_disc(c, x, g->y, cr * 0.95f, 0.95f * e, US_MAT_WATER);
				else {
					us_disc(c, x, g->y, cr * 0.85f, 0.6f * e, US_MAT_WATER); /* white water where it went in */
					for (int k = 0; k < 14; k++) {
						float a = g->ph + k * (US_TAU / 14.0f);
						float ca = cosf(a), sa = sinf(a);
						hl(c, x + ca * cr * 0.35f, g->y + sa * cr * 0.35f, x + ca * cr, g->y + sa * cr, e, US_MAT_WATER);
					}
				}
			}
			if (r > g->r1 * 0.45f && !ctx->crisp) /* a second, slower ring inside the first */
				harc(c, x, g->y, r * 0.62f, 0.0f, US_TAU, v * 0.6f, US_MAT_WATER);
		}
		if (calm > 0.05f)
			carve_crown(c, x, g->y, r * 0.92f, 1.0f - calm);
		if (ctx->crisp) {
			us_ring(c, x, g->y, r, 0.001f, v, US_MAT_WATER);
			if (g->kind == 2 && r * c->s > 3.0f) /* a slap's crest: two pixels deep */
				us_ring(c, x, g->y, r - 1.0f / c->s, 0.001f, v * 0.7f, US_MAT_WATER);
			if (g->kind == 1 && g->age < 0.3f)
				us_disc(c, x, g->y, 0.004f, 0.9f * (1.0f - g->age / 0.3f), US_MAT_WATER);
		} else {
			harc(c, x, g->y, r, 0.0f, US_TAU, v, US_MAT_WATER);
			harc(c, x, g->y, r + 1.0f / c->s, 0.0f, US_TAU, v * (g->kind == 2 ? 0.75f : 0.4f), US_MAT_WATER);
			if (g->kind == 2) /* a slap's crest is a proper wave: wider, with a trough behind it */
				harc(c, x, g->y, r - 1.0f / c->s, 0.0f, US_TAU, v * 0.5f, US_MAT_WATER);
			if ((g->kind == 0 || g->kind == 3) && r > 0.004f)
				harc(c, x, g->y, r * 0.55f, 0.0f, US_TAU, v * 0.55f, US_MAT_WATER);
		}
		/* spray: droplets flung outward from the leap, falling back within half a second */
		if ((g->kind == 1 || g->kind == 2) && g->age < 0.45f) {
			float sp = g->age / 0.45f, e = 1.0f - (1.0f - sp) * (1.0f - sp);
			float dv = 0.9f * (1.0f - sp);
			int nd = g->kind == 2 ? 18 : 11;
			for (int k = 0; k < nd; k++) {
				float hk = hsh(k * 1.7f, g->ph);
				float a = g->ph + k * (US_TAU / nd) + 0.4f * hk;
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
static void duck_draw(us_canvas *c, int crisp, float x, float y, float hd, float L, float v, float wake, float flap)
{
	float ax = cosf(hd), ay = sinf(hd);
	/* startled: she rears up and beats her wings, slapping spray off the water either side */
	if (flap > 0.05f) {
		float beat = 0.5f + 0.5f * cosf((1.0f - flap) * 18.0f);
		for (int sd = -1; sd <= 1; sd += 2) {
			float wa = hd + sd * (1.25f + 0.5f * beat);
			float wl = L * (0.6f + 0.9f * flap);
			float bx = x + ax * L * 0.1f, by = y + ay * L * 0.1f;
			float ex = bx + cosf(wa) * wl, ey = by + sinf(wa) * wl;
			if (crisp)
				us_line(c, bx, by, ex, ey, 0.001f, v * flap, US_MAT_ACCENT);
			else {
				hl(c, bx, by, ex, ey, v * flap, US_MAT_ACCENT);
				hl(c, bx - ax * L * 0.2f, by - ay * L * 0.2f, ex - ax * L * 0.3f, ey - ay * L * 0.3f, 0.6f * v * flap, US_MAT_ACCENT);
			}
			for (int k = 0; k < 4; k++) { /* droplets thrown off the wingtip */
				float dd = wl * (1.15f + 0.25f * k) * (1.3f - 0.3f * flap);
				float da = wa - sd * 0.25f * k;
				us_point(c, bx + cosf(da) * dd, by + sinf(da) * dd, crisp ? 0.001f : 0.0012f, 0.8f * flap, US_MAT_WATER);
			}
		}
	}
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
	carve_crown(c, x + ax * L * 0.08f, y + ay * L * 0.08f, L * 0.42f, 0.0f);
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

/* lily pads in the slack water of an inside bend: notched discs lying flat, a rim catching the light, veins
 * fanning from the notch; a couple in flower. They ride the surface (a slow bob) over the streamlines. */
static void pads_draw(us_canvas *c, st_t *st, const us_ctx *ctx)
{
	float alive = (1.0f - us_smooth(0.4f, 0.8f, us_world_winter(ctx->w))) * (1.0f - st->ice);
	if (alive < 0.05f)
		return;
	float A = st->A, t = (float)ctx->t, s = c->s, inv = 1.0f / s;
	int crisp = c->crisp;
	int n = (int)(st->npad * (0.45f + 0.55f * ctx->p->density) + 0.5f);
	float bloom = us_world_season_w(ctx->w, 1) + 0.6f * us_world_season_w(ctx->w, 0);
	for (int k = 0; k < n; k++) {
		pad_t *pd = &st->pad[k];
		float x = pd->u * A + 0.0015f * sinf(t * 0.31f + pd->ph), cc, h, sl;
		chan(st, x, &cc, &h, &sl);
		float y = cc + pd->side * pd->lat * h * 0.97f + 0.0012f * sinf(t * 0.4f + pd->ph * 2.0f);
		float r = pd->r * (0.6f + 0.4f * alive);
		int clear = 1;
		for (int m = 0; m < st->nrocks && clear; m++) {
			float ddx = x - st->rock_x[m], ddy = y - st->rock_y[m], rr = st->rk[m].r * 1.4f + r;
			if (ddx * ddx + ddy * ddy < rr * rr)
				clear = 0;
		}
		if (!clear)
			continue;
		carve_crown(c, x + r * 0.2f, y + r * 0.25f, r * 1.12f, 0.35f);
		float nx = cosf(pd->notch + 0.2f * sinf(t * 0.1f + pd->ph)), ny = sinf(pd->notch + 0.2f * sinf(t * 0.1f + pd->ph));
		/* the kick's swell lifts the pads and the light under them */
		float base = (crisp ? 0.5f : 0.3f) * (0.6f + 0.4f * alive) * (1.0f + 0.5f * st->kglow);
		int i0 = (int)floorf((x - r) * s) - 1, i1 = (int)ceilf((x + r) * s) + 1;
		int j0 = (int)floorf((y - r) * s) - 1, j1 = (int)ceilf((y + r) * s) + 1;
		for (int j = j0 < 0 ? 0 : j0; j <= j1 && j < c->h; j++)
			for (int i = i0 < 0 ? 0 : i0; i <= i1 && i < c->w; i++) {
				float dx = (i + 0.5f) * inv - x, dy = (j + 0.5f) * inv - y;
				float d = sqrtf(dx * dx + dy * dy);
				float cov = crisp ? (d < r ? 1.0f : 0.0f) : us_sat((r - d) * s + 0.5f);
				if (cov <= 0)
					continue;
				float cs = (dx * nx + dy * ny) / (d + 1e-6f);
				if (cs > 0.94f && d > r * 0.1f) /* the notch */
					continue;
				float v = base * (0.9f - 0.25f * (dx + dy) / r);
				if (!crisp) {
					if (d > r - 1.3f * inv) /* the rim, turned up a little */
						v *= 1.45f;
					else {
						/* veins radiate from the centre */
						float an = atan2f(dy, dx) * (9.0f / US_TAU);
						float fv = an - floorf(an);
						if (fv < 0.14f && d > r * 0.2f)
							v *= 1.22f;
					}
				} else if (d > r - 1.0f * inv && dx + dy > 0)
					v *= 0.75f;
				size_t o = (size_t)j * c->w + i;
				float nv = v * 255.0f;
				c->ink[o] = (uint8_t)(c->ink[o] + (nv - c->ink[o]) * cov);
				if (cov > 0.4f)
					c->mat[o] = US_MAT_LEAF;
			}
		if (pd->flower && bloom > 0.2f) {
			float fr = r * 0.42f;
			for (int p = 0; p < 7; p++) {
				float a = pd->ph + US_TAU * p / 7.0f;
				if (crisp)
					us_put(c, (int)((x + cosf(a) * fr) * s), (int)((y + sinf(a) * fr) * s), 0.9f, US_MAT_BLOOM);
				else
					us_disc(c, x + cosf(a) * fr * 0.8f, y + sinf(a) * fr * 0.8f, fr * 0.55f, 0.75f * bloom, US_MAT_BLOOM);
			}
			us_disc(c, x, y, fr * 0.45f, 0.95f, US_MAT_LIGHT);
		}
	}
}

/* the footbridge: a plank deck straight across the river, rails along both sides, posts at the banks; it
 * throws its shadow on the water down and to the right */
static void bridge_draw(us_canvas *c, st_t *st, const us_ctx *ctx)
{
	float A = st->A, s = c->s, inv = 1.0f / s;
	int crisp = c->crisp;
	float xb = st->br_u * A, cc, h, sl;
	chan(st, xb, &cc, &h, &sl);
	float nn = 1.0f / sqrtf(1.0f + sl * sl);
	float lx = nn, ly = sl * nn;    /* along the river */
	float ax = -sl * nn, ay = nn;   /* across the river: along the deck */
	float hbk = st->hb[(int)(us_clampf(st->br_u, 0, 1) * (NX - 1))];
	float Lh = fmaxf(h, hbk - 0.012f) + 0.022f, bw = crisp ? 0.022f : 0.016f;
	float ox = 0.0075f, oy = 0.009f; /* shadow offset */
	float snow = us_smooth(0.5f, 0.85f, us_world_winter(ctx->w));
	float base = (crisp ? 0.8f : 0.46f) * (0.8f + 0.2f * ctx->w->daylight);
	uint8_t dm = snow > 0.5f ? US_MAT_ACCENT : US_MAT_EARTH;
	float ex = fabsf(ax) * Lh + fabsf(lx) * bw, ey = fabsf(ay) * Lh + fabsf(ly) * bw;
	int i0 = (int)floorf((xb - ex) * s) - 1, i1 = (int)ceilf((xb + ex + ox) * s) + 1;
	int j0 = (int)floorf((cc - ey) * s) - 1, j1 = (int)ceilf((cc + ey + oy) * s) + 1;
	float plank = crisp ? 2.0f * inv : 0.0048f;
	for (int j = j0 < 0 ? 0 : j0; j <= j1 && j < c->h; j++)
		for (int i = i0 < 0 ? 0 : i0; i <= i1 && i < c->w; i++) {
			float px = (i + 0.5f) * inv - xb, py = (j + 0.5f) * inv - cc;
			float a = px * ax + py * ay, b = px * lx + py * ly;
			size_t o = (size_t)j * c->w + i;
			float cov;
			if (crisp)
				cov = (fabsf(a) < Lh && fabsf(b) < bw) ? 1.0f : 0.0f;
			else
				cov = us_sat((Lh - fabsf(a)) * s + 0.5f) * us_sat((bw - fabsf(b)) * s + 0.5f);
			if (cov < 1.0f) {
				float qx = px - ox, qy = py - oy;
				float a2 = qx * ax + qy * ay, b2 = qx * lx + qy * ly;
				if (fabsf(a2) < Lh - 0.01f && fabsf(b2) < bw && fabsf(a) < h) /* its shadow, on the water */
					c->ink[o] = (uint8_t)(c->ink[o] * 0.4f);
			}
			if (cov <= 0)
				continue;
			float fa = a / plank;
			float g = fa - floorf(fa);
			float v = base * (0.82f + 0.18f * cosf(a / Lh * 1.5f)); /* a slight arch catches the light mid-span */
			if (fabsf(b) > bw - (crisp ? 1.0f : 1.6f) * inv)
				v *= 1.3f; /* the rails */
			else if (g < (crisp ? 0.5f : 0.22f))
				v *= crisp ? 0.82f : 0.55f; /* the gaps between planks */
			if (b > 0 && fabsf(b) > bw - 2.5f * inv && !crisp)
				v *= 0.8f; /* the shaded side of the rail */
			v = fminf(v + 0.3f * snow, 1.0f);
			float nv = v * 255.0f;
			c->ink[o] = (uint8_t)(c->ink[o] + (nv - c->ink[o]) * cov);
			if (cov > 0.4f)
				c->mat[o] = dm;
		}
	/* posts where the deck meets each bank, and at mid-span */
	float pa[5] = {-h, h, -Lh + 0.003f, Lh - 0.003f, 0.0f};
	for (int k = 0; k < 5; k++)
		for (int sd = -1; sd <= 1; sd += 2) {
			float px = xb + ax * pa[k] + lx * sd * (bw - 0.0012f), py = cc + ay * pa[k] + ly * sd * (bw - 0.0012f);
			if (crisp)
				us_put(c, (int)(px * s), (int)(py * s), 0.9f, US_MAT_STEM);
			else
				us_disc(c, px, py, 0.0018f, 0.7f, US_MAT_STEM);
		}
}

/* ---- animals ------------------------------------------------------------------
 * Seen from above, drawn as solid silhouettes (they hide the water lines beneath) with a fine rim in ink,
 * from point lists in their own frame: x forward (the way they face), y to their right, in units of size. */

/* solid polygon (scanline, even-odd) that overwrites what is under it */
static void fill_poly(us_canvas *c, const float *xy, int n, float v, uint8_t mat)
{
	if (n < 3 || n > 40)
		return;
	float y0 = 1e9f, y1 = -1e9f;
	for (int i = 0; i < n; i++) {
		y0 = fminf(y0, xy[2 * i + 1]);
		y1 = fmaxf(y1, xy[2 * i + 1]);
	}
	int j0 = (int)floorf(y0 * c->s), j1 = (int)ceilf(y1 * c->s);
	if (j0 < 0)
		j0 = 0;
	if (j1 > c->h - 1)
		j1 = c->h - 1;
	int iv = (int)(us_sat(v) * 255.0f + 0.5f);
	for (int j = j0; j <= j1; j++) {
		float yc = (j + 0.5f) / c->s, xs[40];
		int k = 0;
		for (int i = 0; i < n; i++) {
			int i2 = i + 1 == n ? 0 : i + 1;
			float ay = xy[2 * i + 1], by = xy[2 * i2 + 1];
			if ((ay <= yc) != (by <= yc))
				xs[k++] = (xy[2 * i] + (yc - ay) * (xy[2 * i2] - xy[2 * i]) / (by - ay)) * c->s;
		}
		for (int a = 1; a < k; a++)
			for (int b = a; b > 0 && xs[b - 1] > xs[b]; b--) {
				float tq = xs[b];
				xs[b] = xs[b - 1];
				xs[b - 1] = tq;
			}
		for (int q = 0; q + 1 < k; q += 2) {
			int i0 = (int)ceilf(xs[q] - 0.5f), i1 = (int)floorf(xs[q + 1] - 0.5f);
			if (i0 < 0)
				i0 = 0;
			if (i1 > c->w - 1)
				i1 = c->w - 1;
			uint8_t *ink = c->ink + (size_t)j * c->w, *mt = c->mat + (size_t)j * c->w;
			for (int i = i0; i <= i1; i++) {
				ink[i] = (uint8_t)iv;
				mt[i] = mat;
			}
		}
	}
}

typedef struct {
	float x, y, ca, sa, s;
} xf_t;

static xf_t xf_make(float x, float y, float hd, float s)
{
	xf_t T = {x, y, cosf(hd), sinf(hd), s};
	return T;
}

static void xf_pt(const xf_t *T, float lx, float ly, float *ox, float *oy)
{
	*ox = T->x + (lx * T->ca - ly * T->sa) * T->s;
	*oy = T->y + (lx * T->sa + ly * T->ca) * T->s;
}

static void xf_line(us_canvas *c, const xf_t *T, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
	float x0, y0, x1, y1;
	xf_pt(T, ax, ay, &x0, &y0);
	xf_pt(T, bx, by, &x1, &y1);
	hl(c, x0, y0, x1, y1, v, mat);
}

static void wipe_later(st_t *st, float x, float y, float r, unsigned mats)
{
	if (st->nwp >= 8)
		return;
	st->wp_x[st->nwp] = x;
	st->wp_y[st->nwp] = y;
	st->wp_r[st->nwp] = r;
	st->wp_m[st->nwp++] = (uint8_t)mats;
}

/* a silhouette: solid fill, then (ink) a closed hairline rim. mirror: the list is a half outline from the
 * nose round one side to the tail, and the other side is its reflection (points on the midline shared) */
static void shape(us_canvas *c, const xf_t *T, const float *loc, int n, int mirror, float fill, float line, uint8_t mat)
{
	float p[2 * 41];
	int m = 0;
	for (int i = 0; i < n && m < 40; i++, m++)
		xf_pt(T, loc[2 * i], loc[2 * i + 1], &p[2 * m], &p[2 * m + 1]);
	if (mirror)
		for (int i = n - 1; i >= 0 && m < 40; i--) {
			if (loc[2 * i + 1] == 0.0f)
				continue;
			xf_pt(T, loc[2 * i], -loc[2 * i + 1], &p[2 * m], &p[2 * m + 1]);
			m++;
		}
	fill_poly(c, p, m, fill, mat);
	if (!c->crisp && line > 0) {
		p[2 * m] = p[0];
		p[2 * m + 1] = p[1];
		hpoly(c, p, m + 1, line, mat);
	}
}

/* a ring from an animal: drawn from the animals' own random stream, so the scene's stays as it was */
static void ring_add_own(st_t *st, us_rng *r, float x, float y, float r1, float life, float v, int kind, float delay)
{
	us_rng keep = st->rng;
	int n0 = st->nrg;
	ring_add(st, x, y, r1, life, v, kind, delay);
	st->rng = keep;
	if (st->nrg > n0)
		st->rg[st->nrg - 1].ph = us_rand(r) * US_TAU;
}

/* the kingfisher from above: a big head on a dagger bill, a stubby body, short tail; electric blue with a
 * pale stripe down the back and orange cheeks. wing < 0: folded, else the flap phase (short whirring wings) */
static const float KF[] = {1.0f, 0.0f, 0.56f, 0.06f, 0.48f, 0.18f, 0.36f, 0.25f, 0.2f, 0.21f, 0.0f, 0.26f, -0.3f, 0.21f,
			   -0.5f, 0.11f, -0.8f, 0.08f, -0.8f, 0.0f};

static void kf_draw(us_canvas *c, float x, float y, float hd, float s, float wing, float fish, float v)
{
	int crisp = c->crisp;
	xf_t T = xf_make(x, y, hd, s);
	if (wing >= 0) {
		float sp = 0.45f + 0.55f * fabsf(cosf(wing));
		static const float W[] = {0.2f, 0.18f, 0.12f, 0.75f, -0.08f, 0.85f, -0.2f, 0.62f, -0.22f, 0.18f};
		for (int sd = -1; sd <= 1; sd += 2) {
			float wp[10];
			for (int i = 0; i < 5; i++) {
				wp[2 * i] = W[2 * i];
				wp[2 * i + 1] = sd * (W[2 * i + 1] > 0.2f ? 0.18f + (W[2 * i + 1] - 0.18f) * sp : W[2 * i + 1]);
			}
			shape(c, &T, wp, 5, 0, crisp ? 0.62f : 0.32f * v, 0.8f * v, US_MAT_WATER);
		}
	}
	shape(c, &T, KF, 10, 1, crisp ? 0.85f : 0.42f * v, 1.0f * v, US_MAT_WATER);
	/* the bill, dark; orange cheeks either side of the head; the pale stripe down the back */
	static const float BILL[] = {1.0f, 0.0f, 0.55f, 0.055f, 0.55f, 0.0f};
	shape(c, &T, BILL, 3, 1, crisp ? 0.25f : 0.08f, 0.55f * v, US_MAT_EARTH);
	float ax, ay;
	for (int sd = -1; sd <= 1; sd += 2) {
		xf_pt(&T, 0.42f, sd * 0.17f, &ax, &ay);
		if (crisp)
			us_put(c, (int)(ax * c->s), (int)(ay * c->s), 0.9f, US_MAT_BLOOM);
		else
			us_disc(c, ax, ay, 0.07f * s, 0.85f * v, US_MAT_BLOOM);
	}
	if (crisp) {
		float bx, by;
		xf_pt(&T, 0.2f, 0.0f, &ax, &ay);
		xf_pt(&T, -0.55f, 0.0f, &bx, &by);
		us_line(c, ax, ay, bx, by, 0.001f, 1.0f, US_MAT_LIGHT);
	} else {
		xf_line(c, &T, 0.25f, 0.0f, -0.62f, 0.0f, 1.0f * v, US_MAT_LIGHT);
		xf_line(c, &T, 0.1f, 0.02f, -0.5f, 0.02f, 0.6f * v, US_MAT_LIGHT);
	}
	if (fish > 0.05f) { /* a minnow crosswise in the bill, flashing silver */
		float fx, fy, gx, gy;
		xf_pt(&T, 0.86f, -0.22f, &fx, &fy);
		xf_pt(&T, 0.82f, 0.24f, &gx, &gy);
		if (crisp)
			us_line(c, fx, fy, gx, gy, 0.001f, 0.95f * fish, US_MAT_LIGHT);
		else {
			hl(c, fx, fy, gx, gy, 0.95f * fish * v, US_MAT_LIGHT);
			us_point(c, gx, gy, 0.0012f, 0.8f * fish * v, US_MAT_LIGHT);
		}
	}
}

/* the kingfisher's day: perched at the tip of its branch, bill over the water, head turning; it bobs on the
 * hats; a snare (sometimes) sends it plunging in a crown of spray, out again with a minnow that it beats
 * against the branch on the kicks before it swallows. At dusk it goes to roost. */
static void kingfisher(us_canvas *c, st_t *st, const us_ctx *ctx)
{
	const us_audio *a = ctx->a;
	us_rng *r = &st->crng; /* its own stream: the scene's stays as it was */
	float dt = ctx->dt, rdt = ctx->real_dt, t = (float)ctx->t;
	float day = ctx->w->daylight;
	float s = 0.026f;
	float px = st->kf_x, py = st->kf_y; /* the perch: the branch tip, set when the trees are drawn */
	float base = st->kf_ang;            /* the branch's direction, out over the water */
	st->kf_cd -= dt;
	st->kf_bob = us_approach(st->kf_bob, 0.0f, 0.09f, rdt);
	st->kf_beat = us_approach(st->kf_beat, 0.0f, 0.1f, rdt);
	if (st->kf_R <= 0)
		return;
	float x = px, y = py, hd = base + st->kf_hd, wing = -1.0f, sz = s;
	switch (st->kf_mode) {
	case 0: {
		st->kf_look_t -= dt;
		if (st->kf_look_t <= 0) { /* scanning the water: the bill swings to a new bearing */
			st->kf_look_t = us_randr(r, 2.5f, 6.0f);
			st->kf_look = us_randr(r, -0.7f, 0.7f);
		}
		st->kf_hd = us_approach(st->kf_hd, st->kf_look, 0.12f, dt);
		if (a->hat_hit && us_rand(r) < 0.3f)
			st->kf_bob = 1.0f;
		if (a->kick_hit && st->kf_fish > 0.5f)
			st->kf_beat = 1.0f;
		else if (a->kick_hit && a->kick > 0.5f) /* the kick: it bobs, head down to the water */
			st->kf_bob = 1.0f;
		if (st->kf_fish > 0)
			st->kf_fish -= dt * 0.3f; /* beaten, then swallowed */
		int dive = st->ice < 0.6f && st->kf_cd <= 0 && st->kf_fish <= 0 &&
			   ((a->snare_hit && a->snare > 0.4f && us_rand(r) < 0.35f) || us_rand(r) < dt * 0.025f);
		if (dive) {
			st->kf_mode = 1;
			st->kf_t = 0;
			float da = base + us_randr(r, -0.5f, 0.5f), dd = us_randr(r, 0.035f, 0.07f);
			st->kf_tx = px + cosf(da) * dd;
			st->kf_ty = py + sinf(da) * dd;
			st->kf_cd = us_randr(r, 7.0f, 14.0f);
		}
		if (day < 0.07f) {
			st->kf_mode = 4;
			st->kf_t = 0;
		}
		hd = base + st->kf_hd + 0.5f * st->kf_beat;
		sz = s * (1.0f - 0.08f * st->kf_bob); /* a bob: it dips its head toward the water */
		x += cosf(hd) * s * 0.2f * st->kf_bob;
		y += sinf(hd) * s * 0.2f * st->kf_bob;
		break;
	}
	case 1: { /* the plunge: straight down at the water, shrinking away from us, wings swept back */
		st->kf_t += dt;
		float u = us_sat(st->kf_t / 0.28f);
		x = us_lerp(px, st->kf_tx, u * u);
		y = us_lerp(py, st->kf_ty, u * u);
		hd = atan2f(st->kf_ty - py, st->kf_tx - px);
		sz = s * (1.0f - 0.35f * u);
		wing = u < 0.4f ? t * 40.0f : -1.0f;
		if (u >= 1.0f) {
			ring_add_own(st, r, st->kf_tx, st->kf_ty, 0.045f + 0.02f * us_sat(a->snare), 0.6f, 1.0f, 2, 0.0f);
			st->kf_mode = 2;
			st->kf_t = 0;
		}
		break;
	}
	case 2: /* under: nothing to see but the rings */
		st->kf_t += dt;
		if (st->kf_t > 0.35f) {
			ring_add_own(st, r, st->kf_tx, st->kf_ty, 0.018f, 0.9f, 0.7f, 1, 0.0f);
			st->kf_mode = 3;
			st->kf_t = 0;
			st->kf_fish = us_rand(r) < 0.6f ? 1.0f : 0.0f;
		}
		return;
	case 3: { /* back up to the branch on whirring wings */
		st->kf_t += dt;
		float u = us_sat(st->kf_t / 0.55f);
		float e = 1.0f - (1.0f - u) * (1.0f - u);
		x = us_lerp(st->kf_tx, px, e);
		y = us_lerp(st->kf_ty, py, e);
		hd = atan2f(py - st->kf_ty, px - st->kf_tx);
		sz = s * (0.7f + 0.3f * u);
		wing = t * 45.0f;
		if (u >= 1.0f) { /* lands, then turns to face the water */
			st->kf_mode = 0;
			float rel = hd - base;
			st->kf_hd = atan2f(sinf(rel), cosf(rel));
			st->kf_look = 0;
		}
		break;
	}
	default: /* roosting */
		if (day > 0.12f)
			st->kf_mode = 0;
		return;
	}
	if (c->crisp) /* a few pixels more, or it is only a fleck */
		sz *= 1.35f;
	kf_draw(c, x, y, hd, sz, wing, st->kf_fish > 0 ? us_sat(st->kf_fish * 3.0f) : 0.0f, 0.7f + 0.3f * st->lvl_env);
	if (st->kf_mode != 0) /* only in flight: wiping under it as it sits would flicker the water lines */
		wipe_later(st, x, y, sz * 1.1f + 2.0f / c->s,
			   (1u << US_MAT_WATER) | (1u << US_MAT_EARTH) | (1u << US_MAT_BLOOM) | (1u << US_MAT_LIGHT));
}

/* the grey heron from above: folded grey wings with dark flight feathers, a neck that tucks in or reaches
 * out, a small head with a black plume and a long yellow bill. In flight: great broad wings, neck drawn in,
 * legs trailing. neck: 0 tucked .. 1 reaching (a strike goes past it) */
static const float HN_BODY[] = {0.26f, 0.0f, 0.2f, 0.15f, 0.02f, 0.22f, -0.3f, 0.2f, -0.55f, 0.1f, -0.64f, 0.0f};

static void heron_draw(us_canvas *c, float x, float y, float hd, float s, float neck, float flap, int flying, float v)
{
	int crisp = c->crisp;
	xf_t T = xf_make(x, y, hd, s);
	float gv = crisp ? 0.6f : 0.24f * v;
	if (flying) {
		/* broad fingered wings; from above they sweep forward and back and shorten on the upstroke */
		float sp = 0.62f + 0.38f * cosf(flap), sw = 0.12f * sinf(flap);
		for (int sd = -1; sd <= 1; sd += 2) {
			float wp[] = {0.12f, sd * 0.14f, 0.1f + sw, sd * (0.15f + 0.8f * sp), -0.08f + 2.0f * sw, sd * (0.15f + 1.45f * sp),
				      -0.3f + 2.0f * sw, sd * (0.15f + 1.55f * sp), -0.42f + sw, sd * (0.15f + 1.0f * sp), -0.36f, sd * 0.14f};
			shape(c, &T, wp, 6, 0, gv, 0.8f * v, US_MAT_ACCENT);
			/* the dark primaries along the outer trailing edge */
			float dk[] = {-0.08f + 2.0f * sw, sd * (0.15f + 1.45f * sp), -0.3f + 2.0f * sw, sd * (0.15f + 1.55f * sp),
				      -0.42f + sw, sd * (0.15f + 1.0f * sp), -0.2f + sw, sd * (0.15f + 1.0f * sp)};
			shape(c, &T, dk, 4, 0, crisp ? 0.3f : 0.06f, 0.0f, US_MAT_ACCENT);
		}
		xf_line(c, &T, -0.6f, 0.04f, -1.15f, 0.05f, 0.7f * v, US_MAT_EARTH); /* legs trailing */
		xf_line(c, &T, -0.6f, -0.04f, -1.15f, -0.05f, 0.7f * v, US_MAT_EARTH);
	}
	shape(c, &T, HN_BODY, 6, 1, gv, 0.6f * v, US_MAT_ACCENT);
	if (!flying)
		for (int sd = -1; sd <= 1; sd += 2) { /* folded primaries: dark wedges down the back */
			float dk[] = {0.02f, sd * 0.225f, -0.3f, sd * 0.2f, -0.6f, sd * 0.07f, -0.5f, sd * 0.03f, -0.25f, sd * 0.12f};
			shape(c, &T, dk, 5, 0, crisp ? 0.3f : 0.02f, 0.0f, US_MAT_ACCENT);
		}
	/* the neck: a pale column from the shoulders to the head */
	float hx = 0.36f + 0.62f * neck;
	float nk[] = {0.18f, 0.1f, hx - 0.04f, 0.05f, hx - 0.04f, -0.05f, 0.18f, -0.1f};
	shape(c, &T, nk, 4, 0, crisp ? 0.8f : 0.42f * v, 0.65f * v, US_MAT_ACCENT);
	float ox, oy;
	xf_pt(&T, hx, 0.0f, &ox, &oy);
	xf_t H = xf_make(ox, oy, atan2f(T.sa, T.ca), s);
	static const float HEAD[] = {0.07f, 0.0f, 0.04f, 0.06f, -0.04f, 0.065f, -0.08f, 0.0f};
	shape(c, &H, HEAD, 4, 1, crisp ? 0.85f : 0.5f * v, 0.95f * v, US_MAT_ACCENT);
	/* black plume trailing from the crown, the dagger bill */
	static const float PL[] = {0.05f, 0.02f, -0.45f, 0.035f, -0.45f, 0.005f, 0.05f, -0.02f};
	shape(c, &H, PL, 4, 0, crisp ? 0.25f : 0.03f, 0.0f, US_MAT_ACCENT);
	static const float BL[] = {0.43f, 0.0f, 0.06f, 0.035f, 0.06f, 0.0f};
	shape(c, &H, BL, 3, 1, crisp ? 0.85f : 0.55f * v, 1.0f * v, US_MAT_LIGHT);
	if (crisp) { /* on the pixel grid the neck and bill are single pixel lines */
		float ax, ay, bx, by;
		xf_pt(&T, 0.2f, 0.0f, &ax, &ay);
		us_line(c, ax, ay, ox, oy, 0.001f, 0.85f, US_MAT_ACCENT);
		xf_pt(&H, 0.45f, 0.0f, &bx, &by);
		us_line(c, ox, oy, bx, by, 0.001f, 0.9f, US_MAT_LIGHT);
	}
}

static void heron_fly(st_t *st, float x, float y, float x1, float y1)
{
	st->hn_mode = 1;
	st->hn_t = 0;
	st->hn_x0 = x;
	st->hn_y0 = y;
	st->hn_x1 = x1;
	st->hn_y1 = y1;
}

/* the heron's day: she stands in the shallows facing upstream, neck slowly reaching as she watches; a hard
 * kick is her strike (the bill stabs the water); the snares put her on edge, neck drawn in, and a run of
 * them sends her off downstream on slow wingbeats; she comes back up the river later and lands elsewhere */
static void heron(us_canvas *c, st_t *st, const us_ctx *ctx)
{
	const us_audio *a = ctx->a;
	us_rng *r = &st->crng; /* its own stream: the scene's stays as it was */
	float dt = ctx->dt, rdt = ctx->real_dt, t = (float)ctx->t, A = st->A;
	float s = 0.058f;
	st->hn_cd -= dt;
	st->hn_scd -= dt;
	st->hn_alarm = us_approach(st->hn_alarm, 0.0f, 6.0f, rdt);
	st->hn_gulp = us_approach(st->hn_gulp, 0.0f, 0.3f, rdt);
	if (st->hn_mode == 0) {
		float cc, h, sl;
		chan(st, st->hn_u * A, &cc, &h, &sl);
		/* just inside the water's edge. She stands still between steps; if the river rises or falls (or the
		 * channel shifts) far enough, she steps to keep to the margin */
		float ty = cc + st->hn_side * h * 0.84f;
		if (!st->hn_pin || fabsf(ty - st->hn_py) > 0.008f) {
			if (st->hn_pin)
				st->hn_step = 0;
			st->hn_pin = 1;
			st->hn_px = st->hn_u * A;
			st->hn_py = ty;
			st->hn_pa = US_PI + atanf(sl) - st->hn_side * 0.55f;
		}
		float x = st->hn_px, y = st->hn_py, hd = st->hn_pa + st->hn_hd;
		float reach = 0.06f + 0.42f * us_sat(0.5f + 0.5f * sinf(st->hn_u * 97.0f + st->hn_hd * 13.0f));
		int striking = st->hn_strike > 0;
		/* on edge: the neck snaps in at once; at ease it reaches out slowly again (it holds still in between) */
		st->hn_neck = us_approach(st->hn_neck, striking ? 1.4f : (st->hn_alarm > 0.35f ? 0.05f : reach), striking ? 0.04f : (st->hn_alarm > 0.35f || st->hn_scd > 1.0f ? 0.1f : 1.5f), dt);
		if (a->kick_hit && a->kick > 0.55f && !striking && st->hn_scd <= 0 && us_rand(r) < 0.35f) {
			st->hn_strike = 0.25f;
			st->hn_scd = us_randr(r, 1.5f, 4.0f);
		}
		if (striking) {
			st->hn_strike -= dt;
			if (st->hn_strike <= 0) {
				float bx = x + cosf(hd) * s * 1.45f, by = y + sinf(hd) * s * 1.45f;
				ring_add_own(st, r, bx, by, 0.018f, 0.5f, 0.75f, 1, 0.0f);
				if (us_rand(r) < 0.4f)
					st->hn_gulp = 1.0f;
			}
		}
		/* now and then a slow step along the margin; rings run out from her legs */
		st->hn_step -= dt;
		if (st->hn_step <= 0) {
			st->hn_step = us_randr(r, 6.0f, 14.0f);
			st->hn_u = us_clampf(st->hn_u + us_randr(r, -0.012f, 0.004f), 0.05f, 0.95f);
			st->hn_pin = 0; /* re-placed on the margin next frame */
			st->hn_hd = us_randr(r, -0.45f, 0.45f);
			ring_add_own(st, r, x - cosf(hd) * s * 0.2f, y - sinf(hd) * s * 0.2f, 0.016f, 1.4f, 0.5f, 0, 0.0f);
		}
		if (a->snare_hit) {
			if (st->hn_alarm > 0.7f && a->snare > 0.5f && st->hn_cd <= 0 && us_rand(r) < 0.15f) {
				heron_fly(st, x, y, A + 0.2f, y + us_randr(r, -0.25f, 0.25f));
				ring_add_own(st, r, x, y, 0.05f, 0.8f, 0.8f, 1, 0.0f);
			}
			st->hn_alarm = us_sat(st->hn_alarm + 0.45f + 0.4f * a->snare);
		}
		if (st->hn_mode == 0 && (ctx->w->daylight < 0.05f || st->ice > 0.8f) && us_rand(r) < dt * 0.05f)
			heron_fly(st, x, y, A + 0.2f, y - 0.2f); /* off to roost, or to open water */
		float gulp = st->hn_gulp;
		heron_draw(c, x, y, hd + 0.15f * gulp * sinf(t * 18.0f), s, st->hn_neck * (1.0f - 0.4f * gulp), 0.0f, 0, 0.65f + 0.35f * st->lvl_env);
		return;
	}
	if (st->hn_mode == 2) {
		st->hn_t -= dt;
		if (st->hn_t <= 0 && ctx->w->daylight > 0.08f && st->ice < 0.8f) {
			/* back upstream from the left, landing at a new spot */
			st->hn_u = us_randr(r, 0.15f, 0.85f);
			st->hn_side = us_rand(r) < 0.5f ? -1.0f : 1.0f;
			float cc, h, sl, x = st->hn_u * A;
			chan(st, x, &cc, &h, &sl);
			heron_fly(st, -0.2f, cc + us_randr(r, -0.3f, 0.3f), x, cc + st->hn_side * h * 0.84f);
			st->hn_mode = 3;
			st->hn_alarm = 0;
		}
		return;
	}
	/* flying: slow deep wingbeats */
	float dur = st->hn_mode == 1 ? 2.6f : 3.4f;
	st->hn_t += dt;
	float u = us_sat(st->hn_t / dur);
	float e = st->hn_mode == 1 ? u * u * (1.5f - 0.5f * u) : 1.0f - (1.0f - u) * (1.0f - u);
	float x = us_lerp(st->hn_x0, st->hn_x1, e), y = us_lerp(st->hn_y0, st->hn_y1, e);
	float hd = atan2f(st->hn_y1 - st->hn_y0, st->hn_x1 - st->hn_x0);
	float flap = t * 7.5f;
	float sz = s * (1.0f + 0.25f * sinf(US_PI * u)); /* higher (nearer) mid-flight */
	if (st->hn_mode == 3 && u > 0.85f) /* landing: wings spread wide, backpedalling */
		flap = 0.3f * sinf(t * 10.0f);
	carve_crown(c, x + 0.04f, y + 0.035f, s * 0.55f, 0.5f); /* her shadow, down and to the right */
	heron_draw(c, x, y, hd, sz, 0.1f, flap, 1, 0.95f);
	wipe_later(st, x, y, sz * 1.75f + 2.0f / c->s, (1u << US_MAT_ACCENT) | (1u << US_MAT_EARTH) | (1u << US_MAT_LIGHT));
	if (u >= 1.0f) {
		if (st->hn_mode == 1) {
			st->hn_mode = 2;
			st->hn_t = us_randr(r, 20.0f, 40.0f);
		} else {
			st->hn_mode = 0;
			st->hn_pin = 0;
			st->hn_hd = 0;
			st->hn_neck = 0;
			st->hn_cd = us_randr(r, 25.0f, 45.0f); /* settled: not easily flushed again */
			ring_add_own(st, r, x, y, 0.03f, 1.2f, 0.7f, 0, 0.0f);
		}
	}
}

/* dragonflies hawking along the margins: they hover, wings a shimmer, then dart to a new spot. A hat sends
 * one darting (its wings flash); a snare scatters them; more of them on warm summer days */
static void dragonflies(us_canvas *c, st_t *st, const us_ctx *ctx, float dens)
{
	const us_audio *a = ctx->a;
	us_rng *r = &st->crng; /* its own stream: the scene's stays as it was */
	us_world *w = ctx->w;
	float dt = ctx->dt, t = (float)ctx->t, A = st->A;
	float warm = (us_world_season_w(w, 1) + 0.6f * us_world_season_w(w, 0) + 0.3f * us_world_season_w(w, 2)) * (1.0f - st->ice);
	float day = us_smooth(0.08f, 0.35f, w->daylight);
	int want = (int)((0.6f + 3.4f * warm) * day * (0.5f + 0.7f * dens) * (0.6f + 0.4f * us_sat(w->insects + 0.4f)) + 0.35f);
	if (want > NDRAG)
		want = NDRAG;
	int hat_one = a->hat_hit ? (int)(us_rand(r) * NDRAG) : -1;
	for (int i = 0; i < NDRAG; i++) {
		st->dr_glint[i] = us_approach(st->dr_glint[i], 0.0f, 0.08f, ctx->real_dt);
		if (a->kick_hit) /* the thump of the kick: every wing flashes in the sun at once */
			st->dr_glint[i] = fmaxf(st->dr_glint[i], us_sat(0.5f + 0.5f * a->kick));
		if (!st->dr_on[i]) {
			if (i >= want)
				continue;
			st->dr_on[i] = 1;
			st->dr_x[i] = us_rand(r) < 0.5f ? -0.03f : A + 0.03f;
			st->dr_y[i] = us_randr(r, 0.2f, 0.8f);
			st->dr_t[i] = 0;
			st->dr_dart[i] = 0;
			st->dr_tx[i] = st->dr_x0[i] = st->dr_x[i];
			st->dr_ty[i] = st->dr_y0[i] = st->dr_y[i];
		}
		int dart = st->dr_dart[i] <= 0 && (st->dr_t[i] -= dt) <= 0;
		if (i == hat_one) { /* a hat: its wings flash, and sometimes it darts */
			st->dr_glint[i] = 1.0f;
			if (us_rand(r) < 0.4f)
				dart = 1;
		}
		if (a->snare_hit && us_rand(r) < 0.7f)
			dart = 1;
		if (dart) {
			float nx, ny;
			if (i >= want) { /* leaving: off along the river */
				nx = st->dr_x[i] < A * 0.5f ? -0.1f : A + 0.1f;
				ny = st->dr_y[i];
			} else { /* somewhere over a margin */
				float ux = us_clampf(st->dr_x[i] + us_randr(r, -0.16f, 0.16f), 0.04f * A, 0.96f * A);
				float cc, h, sl;
				chan(st, ux, &cc, &h, &sl);
				nx = ux;
				ny = cc + (us_rand(r) < 0.5f ? -1.0f : 1.0f) * h * us_randr(r, 0.45f, 1.05f);
			}
			st->dr_x0[i] = st->dr_x[i];
			st->dr_y0[i] = st->dr_y[i];
			st->dr_tx[i] = nx;
			st->dr_ty[i] = ny;
			st->dr_dart[i] = 1.0f;
			st->dr_hd[i] = atan2f(ny - st->dr_y[i], nx - st->dr_x[i]);
			st->dr_t[i] = us_randr(r, 2.5f, 7.0f);
		}
		if (st->dr_dart[i] > 0) { /* a dart: quick, on a slight curve */
			float ddx = st->dr_tx[i] - st->dr_x0[i], ddy = st->dr_ty[i] - st->dr_y0[i];
			st->dr_dart[i] -= dt / fmaxf(0.07f, sqrtf(ddx * ddx + ddy * ddy) / 2.2f);
			float u = us_sat(1.0f - st->dr_dart[i]), e = u * u * (3.0f - 2.0f * u);
			st->dr_x[i] = us_lerp(st->dr_x0[i], st->dr_tx[i], e) - ddy * 0.15f * sinf(US_PI * u);
			st->dr_y[i] = us_lerp(st->dr_y0[i], st->dr_ty[i], e) + ddx * 0.15f * sinf(US_PI * u);
			if (st->dr_dart[i] <= 0 && i >= want) {
				st->dr_on[i] = 0;
				continue;
			}
		} else { /* hovering: a tiny drift, the body yawing */
			st->dr_x[i] = st->dr_tx[i] + 0.0012f * us_noise2(t * 0.6f, i * 4.0f);
			st->dr_y[i] = st->dr_ty[i] + 0.0012f * us_noise2(i * 4.0f, t * 0.5f);
			st->dr_hd[i] += 0.3f * us_noise2(t * 0.5f, i * 7.0f) * dt;
		}
		float x = st->dr_x[i], y = st->dr_y[i];
		if (x < -0.05f || x > A + 0.05f)
			continue;
		float s = 0.024f, hd = st->dr_hd[i], gl = st->dr_glint[i];
		xf_t T = xf_make(x, y, hd, s);
		float lv = 0.6f + 0.4f * st->lvl_env;
		float wv = ((c->crisp ? 0.45f : 0.3f) + 0.6f * gl) * lv;
		for (int sd = -1; sd <= 1; sd += 2) {
			if (c->crisp) {
				float ax, ay, bx, by;
				xf_pt(&T, 0.15f, 0.0f, &ax, &ay);
				xf_pt(&T, 0.22f, sd * 0.75f, &bx, &by);
				us_line(c, ax, ay, bx, by, 0.001f, wv, US_MAT_LIGHT);
				xf_pt(&T, -0.06f, sd * 0.78f, &bx, &by);
				us_line(c, ax, ay, bx, by, 0.001f, wv * 0.8f, US_MAT_LIGHT);
				continue;
			}
			float fw[] = {0.2f, sd * 0.03f, 0.3f, sd * 0.4f, 0.22f, sd * 0.78f, 0.1f, sd * 0.42f};
			float hw[] = {0.06f, sd * 0.03f, 0.08f, sd * 0.44f, -0.08f, sd * 0.8f, -0.1f, sd * 0.36f};
			shape(c, &T, fw, 4, 0, 0.12f + 0.35f * gl, wv, US_MAT_LIGHT);
			shape(c, &T, hw, 4, 0, 0.1f + 0.3f * gl, wv * 0.85f, US_MAT_LIGHT);
		}
		/* the long abdomen, the thorax, the big eyes */
		float ax, ay, bx, by;
		xf_pt(&T, 0.12f, 0.0f, &ax, &ay);
		xf_pt(&T, -1.0f, 0.0f, &bx, &by);
		if (c->crisp)
			us_line(c, ax, ay, bx, by, 0.001f, 0.85f, US_MAT_ACCENT);
		else {
			us_line(c, ax, ay, bx, by, 0.0024f, 0.6f, US_MAT_ACCENT);
			hl(c, ax, ay, bx, by, 0.9f, US_MAT_ACCENT);
		}
		us_disc(c, ax, ay, s * 0.1f, 0.85f, US_MAT_ACCENT);
		xf_pt(&T, 0.3f, 0.0f, &ax, &ay);
		us_disc(c, ax, ay, s * 0.09f, 0.95f, US_MAT_ACCENT);
		wipe_later(st, x, y, s * 1.05f + 2.0f / c->s, (1u << US_MAT_ACCENT) | (1u << US_MAT_LIGHT));
	}
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
	/* the kick's light in the water: up at once, ebbing over a quarter second */
	st->kglow = us_approach(st->kglow, 0.0f, 0.075f, ctx->real_dt);
	if (a->kick_hit) /* a kick is a kick: even a soft one lifts the water clearly */
		st->kglow = fmaxf(st->kglow, us_sat(0.6f + 0.5f * a->kick));
	st->lvl_env = us_approach(st->lvl_env, us_sat(1.05f * a->level + 0.1f * a->swell - 0.1f), 0.4f, ctx->real_dt);
	st->flap = us_approach(st->flap, 0.0f, 0.18f, ctx->real_dt);
	st->shake = us_approach(st->shake, 0.0f, 0.12f, ctx->real_dt);
	st->leap_cd -= dt;
	st->startle_cd -= dt;

	us_world_add(w, &w->moisture, 0.0012f * (0.25f + a->level) * (1.0f - 0.7f * st->ice), dt);

	build_channel(st, A, t);
	build_grid(st, A, t);

	/* ---- surges: every kick sends a pulse of faster, brighter water downstream ---- */
	if (a->kick_hit && st->nsg < MAX_SURGE) {
		surge_t *g = &st->sg[st->nsg++];
		/* the bore rises somewhere in the upper reach and rolls down from there */
		g->x = us_randr(r, -0.05f, 0.45f) * A;
		g->amp = us_sat(0.45f + 0.7f * a->kick);
	}
	memset(st->sgx, 0, sizeof(st->sgx));
	for (int k = 0; k < st->nsg;) {
		surge_t *g = &st->sg[k];
		g->x += (0.45f + 2.6f * st->speed) * dt;
		g->amp *= expf(-dt * 1.6f);
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
	int burst_rock = ((a->kick_hit || a->snare_hit) && us_rand(r) < 0.45f + 0.55f * hit) ? (int)(us_rand(r) * st->nrocks) : -1;
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
	glitter_wipe(c, st);
	for (int i = 0; i < st->nwp; i++)
		for (int m = 0; m < 8; m++)
			if (st->wp_m[i] & (1u << m))
				wipe_disc(c, st->wp_x[i], st->wp_y[i], st->wp_r[i], (uint8_t)m);
	st->nwp = 0;

	/* ---- land (crisp fill: a patchwork of fields) + water fill ---- */
	st->shade_t += dt * (0.012f + 0.04f * wind + 0.03f * a->swell);
	build_land_grids(st);
	land_fill(c, st, ctx);
	water_fill(c, st, ctx);

	/* ---- banks: water edge + terraced contours (the floodplain remembers higher water) ---- */
	{
		float top[2 * NX], bot[2 * NX];
		/* the water's edge: a soft lip, brighter where the sand shelves in */
		for (int i = 0; i < NX; i++) {
			float x = A * i / (NX - 1);
			top[2 * i] = x;
			top[2 * i + 1] = st->cy[i] - st->hw[i];
			bot[2 * i] = x;
			bot[2 * i + 1] = st->cy[i] + st->hw[i];
		}
		if (ctx->crisp) {
			us_polyline(c, top, NX, 0.0017f, 0.36f, US_MAT_EARTH);
			us_polyline(c, bot, NX, 0.0017f, 0.36f, US_MAT_EARTH);
		} else {
			hpoly(c, top, NX, 0.3f, US_MAT_EARTH);
			hpoly(c, bot, NX, 0.3f, US_MAT_EARTH);
		}
		/* pebbles and shingle: thick on the sandbars, a scatter along the muddy lip */
		if (st->ice < 0.7f)
			for (int i = 0; i < 3 * NX; i++) {
				float hx = hsh(i * 0.37f, st->seed_off), hy = hsh(i * 1.71f, 3.0f), hr = hsh(i * 2.9f, 8.0f);
				float side = (i & 1) ? 1.0f : -1.0f;
				float x = A * (i + hx) / (3 * NX), cc, h, sl;
				chan(st, x, &cc, &h, &sl);
				float in = inside_at(st, x, side);
				if (hr > 0.12f + 0.75f * in)
					continue;
				float dw = (0.004f + 0.038f * in) * hy * hy;
				float y = cc + side * (h + dw + 0.0015f);
				float pr = (0.0011f + 0.0016f * hr) * (1.0f - 0.5f * hy);
				float pv = (0.3f + 0.25f * hx) * (1.0f - st->ice);
				if (ctx->crisp)
					us_put(c, (int)(x * c->s), (int)(y * c->s), pv + 0.25f, US_MAT_EARTH);
				else {
					us_disc(c, x, y, pr, pv * 0.8f, US_MAT_EARTH);
					us_point(c, x - pr * 0.3f, y - pr * 0.3f, pr * 0.4f, pv * 1.3f, US_MAT_EARTH);
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
		float sv = 0.12f + 0.5f * st->ice;
		hpoly(c, top, NX, sv, st->ice > 0.3f ? US_MAT_ACCENT : US_MAT_WATER);
		hpoly(c, bot, NX, sv, st->ice > 0.3f ? US_MAT_ACCENT : US_MAT_WATER);
		/* white water along the margins: broken foam lines drifting with the current, livelier when it runs */
		if (st->ice < 0.6f) {
			float fv = (0.12f + 0.9f * st->speed + (ctx->crisp ? 0.6f : 0.2f) * st->kglow) * (1.0f - st->ice);
			for (int side = -1; side <= 1; side += 2) {
				float px = 0, py = 0;
				for (int i = 0; i < NX; i++) {
					float x = A * i / (NX - 1);
					float o = st->hw[i] * (0.93f + 0.03f * us_noise2(x * 7.0f, side * 3.0f + t * 0.3f));
					float y = st->cy[i] + side * o;
					float n = us_noise2(x * 11.0f - st->flow_t * 0.9f, side * 5.3f + st->seed_off);
					float v = fv * us_smooth(0.1f, 0.6f, n) * (0.6f + 0.6f * st->sgx[i]);
					if (i > 0 && v > 0.03f)
						hl(c, px, py, x, y, fminf(v, 0.75f), US_MAT_WATER);
					px = x;
					py = y;
				}
			}
		}
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
		float bw = us_sat(st->speed * 5.0f + sg + 0.6f * st->kglow);
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
		float frate = (1.5f + 14.0f * st->speed + 30.0f * sg) * (0.4f + 0.8f * dens);
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
		float v = (0.42f + 0.4f * st->kglow) * us_sat(f->life * 1.6f) * (1.0f - 0.3f * st->ice);
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
	/* fewer, finer lines than a fill: the current reads as threads over the dark body of the water */
	int want = 320 + (int)(dens * 1500.0f);
	if (ctx->crisp) /* a 1px streak covers far more of a low-res canvas */
		want = (450 + (int)(dens * 2300.0f)) * 9 / 20;
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
		float l2 = us_sat(lat * lat);
		float v;
		if (ctx->crisp)
			/* on the coarse grid the current is a calm grain: speed barely changes a streak's step, the eddies'
			 * white water and the drums still do */
			v = (0.14f + 0.1f * us_sat(sp) + 0.1f * energy + 0.45f * us_sat(foam) + 0.22f * sg + 0.47f * st->kglow + 0.3f * st->lvl_env) * fade;
		else /* the deep channel stays dark; the shallows, the surge and white water catch the light */
			v = (0.08f + 0.11f * us_sat(sp) + 0.08f * energy + 0.16f * st->lvl_env + 0.4f * us_sat(foam) + 0.3f * sg +
			     0.14f * st->kglow) *
			    (0.55f + 0.45f * l2) * fade;
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

	/* ---- hats: the sun flashes off a scatter of ripple crests, all at once ---- */
	if (a->hat_hit && st->np > 0 && st->ice < 0.9f) {
		float sunv = 0.55f + 0.45f * w->daylight;
		int n = (int)((30.0f + 70.0f * us_sat(a->hat)) * (0.45f + 0.9f * dens) * (ctx->crisp ? 1.9f : 1.0f));
		for (int k = 0; k < n && st->ngt < MAX_GLIT; k++) {
			const prt_t *q = &st->p[(int)(us_rand(r) * st->np) % st->np];
			float x = q->u * A;
			if (x < 0 || x > A || shade_at(st, x, q->y) < 0.62f) /* no sun under a cloud */
				continue;
			glit_t *g = &st->gt[st->ngt++];
			g->u = q->u;
			g->y = q->y;
			g->age = 0;
			g->life = us_randr(r, 0.05f, 0.13f);
			g->v = us_randr(r, 0.65f, 1.0f) * sunv;
			g->sz = us_randr(r, 0.006f, 0.014f) * (0.75f + 0.6f * a->centroid);
			g->dr_ = 0;
		}
	}

	/* ---- snare: something slaps the water — a crown of spray, rings running out over a smoothed patch ---- */
	if (a->snare_hit && st->ice < 0.7f) {
		/* the low-res canvas shows fewer drops, so it gets one more slap */
		int n = 2 + (dens > 0.3f) + ctx->crisp;
		for (int k = 0; k < n; k++) {
			float x = 0, y = 0;
			int ok = 0;
			for (int tries = 0; tries < 5 && !ok; tries++) {
				x = us_randr(r, 0.06f, 0.94f) * A;
				float cc, h, sl;
				chan(st, x, &cc, &h, &sl);
				y = cc + us_randr(r, -0.6f, 0.6f) * h;
				ok = 1;
				for (int m = 0; m < st->nrocks; m++) {
					float ddx = x - st->rock_x[m], ddy = y - st->rock_y[m];
					if (ddx * ddx + ddy * ddy < 16.0f * st->rk[m].r * st->rk[m].r)
						ok = 0;
				}
			}
			if (ok)
				ring_add(st, x, y, (0.08f + 0.045f * us_sat(a->snare)) * (1.0f - 0.2f * k), 0.6f, 1.0f, 2, 0.0f);
		}
		/* ...and the jolt shakes a patter of drops off the overhanging leaves: rings all down the reach */
		int nd = (int)((9.0f + 9.0f * us_sat(a->snare)) * (0.55f + 0.8f * dens) * (ctx->crisp ? 1.7f : 1.0f));
		for (int k = 0; k < nd; k++) {
			float x = us_randr(r, 0.02f, 0.98f) * A, cc, h, sl;
			chan(st, x, &cc, &h, &sl);
			float y = cc + us_randr(r, -0.8f, 0.8f) * h;
			ring_add(st, x, y, us_randr(r, 0.014f, 0.028f), us_randr(r, 0.3f, 0.45f), us_randr(r, 0.75f, 1.0f), 3, 0.0f);
		}
		st->flap = 1.0f;
		st->shake = us_sat(0.7f + 0.4f * a->snare);
	}

	/* ---- trout: shadows holding station against the current, darting upstream on hits ---- */
	{
		int nfish = 1 + (int)(dens * 3.0f + 0.5f);
		float vis = 1.0f - 0.65f * st->ice;
		/* a kick: a trout leaps clear and falls back in a splash */
		int leap = -1;
		if (a->kick_hit && st->leap_cd <= 0 && us_rand(r) < 0.35f + 0.4f * a->kick) {
			leap = (int)(us_rand(r) * (nfish < MAX_FISH ? nfish : MAX_FISH));
			st->leap_cd = 0.9f;
		}
		for (int i = 0; i < nfish && i < MAX_FISH; i++) {
			fish_t *f = &st->fish[i];
			/* each fish has a favourite lie it slips back to after a dart */
			float home = (i + 0.5f) / nfish + 0.12f * us_noise2(t * 0.02f, i * 7.1f);
			f->hu = us_approach(f->hu, home, 7.0f, dt);
			f->hs = us_clampf(f->hs + dt * 0.08f * us_noise2(t * 0.07f + i * 3.0f, 1.3f), -0.55f, 0.55f);
			if ((a->kick_hit || a->snare_hit) && us_rand(r) < 0.1f + 0.25f * a->onset) {
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

	pads_draw(c, st, ctx);
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
		float vel = 0.012f * (1.0f - st->ice) * (1.0f + 2.5f * st->flap);
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
				  us_sat(0.5f + 0.5f * st->dk_dir * -1.0f + 3.0f * st->speed - 0.3f + st->flap),
				  k == 0 ? st->flap : 0.6f * st->flap);
		}
	}

	/* ---- sun glints: sparkles on the moving water, livelier with the highs ---- */
	{
		float sun = 0.25f + 0.75f * w->daylight;
		float grate = (4.0f + 18.0f * us_sat(0.5f * a->high + 0.6f * a->air) + 10.0f * energy) * sun *
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

	glitter_step(c, st, ctx);
	bridge_draw(c, st, ctx);

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
			float sway = us_noise2(x * 2.5f - st->wind_t * 0.5f, d->ph) * (0.08f + 0.4f * wind) +
				     0.06f * sinf(st->wind_t * 1.6f + d->ph) * wind;
			sway += d->side * 0.5f * d->nod;
			float base = (d->side > 0 ? -US_PI * 0.5f : US_PI * 0.5f) + d->lean * 0.5f;
			if (ctx->crisp) {
				/* at pixel scale: a few long blades from roots spread along the bank, leaning apart */
				float span = 0.01f + 0.016f * hsh(i * 3.7f, 1.9f);
				for (int b = 0; b < 4; b++) {
					float h1 = hsh(i * 13.1f + b, 4.4f);
					float rx = x + (b / 3.0f - 0.5f) * span;
					float L = d->len * (0.7f + 0.5f * h1) * (0.75f + 0.25f * green);
					float a0 = base + (b / 3.0f - 0.5f) * 0.7f + (h1 - 0.5f) * 0.3f + sway * 0.8f;
					us_line(c, rx, by, rx + cosf(a0) * L, by + sinf(a0) * L, 0.001f, 0.5f + 0.12f * green + 0.1f * h1, rm);
					if (b == 2 && (i % 2) == 0)
						us_point(c, rx + cosf(a0) * L * 0.8f, by + sinf(a0) * L * 0.8f, 0.0012f, 0.7f, US_MAT_STEM);
				}
				continue;
			}
			/* a reed bed from above: blades rooted along a stretch of the bank, leaning out over the water,
			 * a few dark cattail heads among them */
			int nb = 10 + (i % 6);
			float span = 0.012f + 0.022f * hsh(i * 3.7f, 1.9f);
			for (int b = 0; b < nb; b++) {
				float h1 = hsh(i * 13.1f + b, 4.4f), h2 = hsh(i * 5.3f + b * 1.7f, 9.1f);
				float rx = x + (b / (float)(nb - 1) - 0.5f) * span + (h2 - 0.5f) * 0.003f;
				float ry = by + s * (rx - x) + d->side * 0.004f * h2;
				float L = d->len * (0.45f + 0.55f * h1) * (0.75f + 0.25f * green) * 0.85f;
				float a0 = base + (h2 - 0.5f) * 1.1f + sway * (0.6f + 0.4f * h1);
				float bend = 0.3f * (h2 > 0.5f ? 1.0f : -1.0f) * h1 + sway * 0.5f;
				float mx = rx + cosf(a0) * L * 0.55f, my = ry + sinf(a0) * L * 0.55f;
				float tx = mx + cosf(a0 + bend) * L * 0.45f, ty = my + sinf(a0 + bend) * L * 0.45f;
				float pts[6] = {rx, ry, mx, my, tx, ty};
				hpoly(c, pts, 3, 0.2f + 0.1f * green + 0.12f * h1, rm);
				if (h1 > 0.82f && (i % 3) != 1) {
					float hx = rx + cosf(a0) * L * 0.62f, hy = ry + sinf(a0) * L * 0.62f;
					us_line(c, hx, hy, hx + cosf(a0) * 0.006f, hy + sinf(a0) * 0.006f, 0.0026f, 0.5f, US_MAT_STEM);
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
		float rr = 0.15f + 0.7f * he; /* rises per second */
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
		int ntree = 5 + (int)(dens * 11.0f + 0.5f);
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
			float sw = 0.12f * (0.3f + wind);
			float ox = sw * us_noise2(st->wind_t * 0.6f, tr->ph), oy = sw * 0.6f * us_noise2(tr->ph, st->wind_t * 0.6f);
			/* the snare's jolt: the crown lurches and its leaves flash pale */
			ox += 0.1f * st->shake * sinf(tr->ph * 7.0f + t * 40.0f);
			oy += 0.06f * st->shake * cosf(tr->ph * 5.0f + t * 37.0f);
			float R = tr->r * (0.75f + 0.25f * (1.0f - bare));
			float crown = 1.0f - bare;
			if (crown > 0.05f) {
				/* sun from the upper left: the shadow falls down and to the right, on the water too */
				float lit = ctx->crisp ? 1.0f : 0.46f;
				canopy(c, st, tr->lb, tr->nl, x, y, R, ox, oy, lit * (0.75f + 0.25f * w->daylight) * (0.82f + 0.3f * st->lvl_env) * (1.0f + 0.65f * st->shake), crown, cm, R * 0.42f,
				       R * 0.36f, us_lerp(1.0f, ctx->crisp ? 0.6f : 0.42f, crown));
			}
			/* limbs: the whole picture in winter */
			float lv = (ctx->crisp ? 0.72f : 0.5f) * bare;
			if (lv > 0.02f) {
				float cx = x + ox * R, cy = y + oy * R;
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
			if (i == st->kf_tree) {
				/* the kingfisher's dead branch, reaching out of the crown over the water, bare all year */
				float ba = (tr->side > 0 ? -US_PI * 0.5f : US_PI * 0.5f) + 0.45f * (hsh(tr->ph, 2.0f) - 0.5f);
				float bx0 = x + ox * R + cosf(ba) * R * 0.55f, by0 = y + oy * R + sinf(ba) * R * 0.55f;
				float bx1 = x + cosf(ba) * R * 1.45f, by1 = y + sinf(ba) * R * 1.45f; /* the dead tip barely sways */
				float bv = 0.5f + 0.05f * ctx->crisp;
				if (ctx->crisp)
					us_line(c, bx0, by0, bx1, by1, 0.002f, bv, US_MAT_STEM);
				else {
					hl(c, bx0, by0, bx1, by1, bv, US_MAT_STEM);
					hl(c, bx0 + 0.001f, by0, bx1 + 0.001f, by1, bv * 0.7f, US_MAT_STEM);
				}
				float tw = ba + 0.6f, mx = us_lerp(bx0, bx1, 0.6f), my = us_lerp(by0, by1, 0.6f); /* a twig off the side */
				hl(c, mx, my, mx + cosf(tw) * R * 0.3f, my + sinf(tw) * R * 0.3f, bv * 0.8f, US_MAT_STEM);
				st->kf_x = us_lerp(bx0, bx1, 0.86f);
				st->kf_y = us_lerp(by0, by1, 0.86f);
				st->kf_ang = ba;
				st->kf_R = R;
			}
		}
	}

	/* ---- the animals: the heron in the shallows, the kingfisher on its branch, dragonflies over all ---- */
	heron(c, st, ctx);
	kingfisher(c, st, ctx);
	dragonflies(c, st, ctx, dens);
}

const us_scene_vt us_scene_river = {"river", "River", 0.42f, create, destroy, frame};
