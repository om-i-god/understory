/* Understory scene: Dunes — a sea of sand under a vast sky.
 *
 * Five ranges of dunes, far to near, each a chain of humps: a rounded windward back rising to a sharp
 * summit, a crest that sweeps down toward the viewer, and a steep slip face in the wedge between the crest
 * and the outline. The sun (or moon) lights one side of every crest and leaves the other in shadow, so the
 * light swings across the sand through the day. Far ranges fade into the haze; the near faces carry wind
 * ripples that follow the form of the sand. A lone umbrella acacia stands on the hero dune's saddle, a
 * camel caravan walks the middle crests, dry grass frames the near corners, and sand smokes off the crests
 * downwind. Night brings a dense Milky Way rising out of the dunes; dusk a low sun and a band of afterglow.
 * Music: loudness is the wind and the light (more sand streams off the crests, the faces glow);
 *        kick -> a sand avalanche slides down a slip face and the dunes' light pulses;
 *        snare -> a gust lifts a sheet of sand off the crests (a strong one spins up a dust devil);
 *        hats -> sand grains glitter on the lit faces and stars twinkle;
 *        bands -> the near ripples light where their band sounds; swell -> afterglow + haze glow;
 *        width -> the spread of the streaming sand; centroid -> how high the sand is thrown.
 *        Veils of blown sand drift low over the dunes with the wind.
 * World: daylight + day_phase (sun / moon position and the light's side), season (spring acacia blossom,
 *        summer heat haze, crisp winter air with a brighter Milky Way and a sparser crown), wind, moisture
 *        (after rain the valleys flower). Writes: dries moisture a little (desert air).
 * Life: a fennec fox trots the near range's outline, stops to sniff and look about; a snare freezes it
 *        mid-step, head up and ears pricked. Jerboas sit up in the valley under the acacia and spring away
 *        on the kick (tail streaming as a rudder); a snare sits them bolt upright. A darkling beetle trundles
 *        over the near sand leaving a two-row track the wind slowly fills in. By night an eagle-owl watches
 *        from the acacia, snaps its head round and puffs up at a snare, and on a hard one now and then
 *        stoops at a jerboa and sweeps back to its perch (it flies in at dusk and leaves at dawn). Hats flash
 *        their eyeshine. A far oasis (palms over a pool that glints on the hats); succulent rosettes and
 *        desert lilies (open by night, more after rain and in spring) in the hollows. Louder music lights
 *        them more; when the music stops they settle and listen.
 * Silence: the wind still smokes the crests and drifts the veils, the caravan walks, the odd avalanche
 *        slides, stars twinkle and the odd meteor falls.
 * Pixel styles: posterised shades with narrow dithered seams; silhouettes (camels, acacia, grass) overwrite
 *        the sand so they read against a bright day.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define NLAY 5
#define MAXH 8
#define MAXCOL 4096
#define HY 0.565f /* the horizon */
#define NC 40     /* crest samples */
#define NCT 96    /* crest lookup table */
#define MAXP 1100 /* sand particles */
#define MAX_AV 6  /* avalanches */
#define MAX_DD 3  /* dust devils */
#define MAX_GL 64 /* glitter */
#define NDUST 2600
#define MWW 96
#define MWH 48
#define NSTAR 420
#define NCAM 5
#define NTUFT 4
#define NFLOW 40 /* desert flowers after rain */

#define STEP1 0.17f
#define STEP2 0.38f
#define STEP3 0.62f
#define STEP4 0.92f

static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static const float QV[5] = {0.0f, STEP1, STEP2, STEP3, STEP4};

/* per layer: brightness, haze */
static const float LBRI[NLAY] = {0.34f, 0.46f, 0.62f, 0.86f, 1.0f};
static const float LHAZE[NLAY] = {0.7f, 0.5f, 0.28f, 0.08f, 0.0f};

typedef struct {
	float cx, cy;   /* summit: x as a fraction of the width, y */
	float wl, wr;   /* windward / slip-side half widths (fractions of the width) */
	float dxe, yend; /* where the crest ends: x offset (fraction), y */
	float bend;     /* crest sinuosity */
	float rph;      /* ripple phase */
	float tu[NCT + 1], ty[NCT + 1]; /* the crest, tabulated (built once) */
} hump_t;

typedef struct {
	float base;
	int n;
	hump_t h[MAXH];
} layer_t;

typedef struct {
	float x, y, vx, vy, life, max, v;
	int lay;
} grain_t;

typedef struct {
	int on, lay, hi;
	float s, age;
} aval_t;

typedef struct {
	int on, lay;
	float u, age, life, h, r, ph, amp;
} devil_t;

typedef struct {
	float x, y, age;
	int lay;
} glint_t;

/* ---- life ---- */
#define NJB 3     /* jerboas */
#define NTK 160   /* beetle footprints */
#define NPALM 5   /* the far oasis */
#define NSUC 6    /* succulent rosettes */
#define NLILY 6   /* desert lilies */

typedef struct {
	float u, d, dtg;          /* position (fraction of width), facing (animated), wanted facing */
	float ph, spd, amp, tst;  /* gait phase, speed 0..1, stride amplitude, state timer */
	float hold, alert, sniff, eye, bob;
	int st;                   /* 0 trotting, 1 standing (sniffing, looking about) */
} fox_t;

typedef struct {
	float u, f, d;            /* along the valley, depth fraction across the floor, facing */
	float hp, hdur, hlen, hh, hdf; /* hop progress (<0 idle), duration, length, height, depth change */
	float wait, groom, ear, al; /* ear: seconds since it last landed; al: startled, sitting bolt upright */
} jerb_t;

typedef struct {
	int st;                   /* 0 away, 1 arriving, 2 perched, 3 swooping, 4 leaving */
	float t, dur, x0, y0, tx, ty, x2, y2; /* flight: from, through, to */
	float look, look_tg, look_t, flare, blink, eye, cool, flap, px, py, vx;
} owl_t;

typedef struct {
	us_rng rng;
	layer_t lay[NLAY];
	/* per frame, per canvas column: each range's outline and the row below which nearer ranges cover it */
	float sil[NLAY][MAXCOL];
	short occ[NLAY][MAXCOL];
	grain_t g[MAXP];
	int gi;
	aval_t av[MAX_AV];
	devil_t dd[MAX_DD];
	glint_t gl[MAX_GL];
	int gli;
	/* sky */
	float mw[MWW * MWH];
	float du[NDUST], dy[NDUST], dm[NDUST];
	float su[NSTAR], sy[NSTAR], sm[NSTAR], stw[NSTAR];
	/* acacia */
	float ac_u, ac_h;
	float limb[4][4]; /* fork -> crown, as fractions of the tree height: x0,y0,x1,y1 */
	/* caravan */
	float car_u, car_dir, car_ph, car_wait;
	int car_on, car_n;
	/* grass tufts in the near corners */
	float tu[NTUFT], th[NTUFT];
	float fl_u[NFLOW], fl_f[NFLOW];
	/* envelopes */
	float lvl, wind, swl, wid, cen, kickp, snare_e;
	float bandv[US_NBANDS];
	float T, emit_acc, av_wait, av_cool, dd_cool, sway, met_t, met_x, met_y, met_vx, met_vy, met_wait;
	float drift;
	uint32_t hat_seed, gust_seed;
	int over;
	/* life */
	fox_t fx;
	jerb_t jb[NJB];
	owl_t ow;
	float bt_u, bt_y, bt_a, bt_spd, bt_step, bt_pause, bt_glint;
	float tk_u[NTK], tk_y[NTK], tk_age[NTK];
	int tk_i, tk_side;
	float pm_du[NPALM], pm_h[NPALM], pm_lean[NPALM], oasis_u;
	float su_u[NSUC], su_f[NSUC], su_sz[NSUC];
	float li_u[NLILY], li_f[NLILY], li_ph[NLILY];
	float hat_e;
	int life_init;
} st_t;

static float hashf(uint32_t h)
{
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	h ^= h >> 16;
	return (h & 0xffffff) / 16777216.0f;
}

/* ---- dune geometry (x in fractions of the width) ---------------------------------------------- */

static float base_y(const layer_t *l, int li, float u)
{
	return l->base + (0.0025f + 0.0015f * li) * us_noise2(u * (9.0f - li), 3.7f * li + 1.0f);
}

static float hump_sil(const hump_t *h, float base, float u)
{
	float f;
	if (u < h->cx) { /* the windward back: a whaleback shoulder rising to a sharp summit */
		float t = (h->cx - u) / h->wl;
		if (t >= 1.0f)
			return 9.0f;
		f = 1.0f - 0.12f * t - 0.88f * powf(t, 1.7f);
		f *= 1.0f - 0.25f * us_smooth(0.6f, 1.0f, t) * (1.0f - t); /* ease into the floor */
	} else { /* the lee side: a steep drop, flattening into a long tail */
		float t = (u - h->cx) / h->wr;
		if (t >= 1.0f)
			return 9.0f;
		float a = 1.0f - t;
		f = a * a * (0.55f + 0.45f * a);
	}
	return base - (base - h->cy) * f;
}

static float crest_ex(const hump_t *h) { return h->cx + h->dxe; }

static void crest_at(const hump_t *h, float s, float *u, float *y)
{
	float a = 1.0f - s;
	*u = h->cx + h->dxe * (1.0f - a * sqrtf(a) + h->bend * 0.18f * sinf(US_TAU * s) * a);
	*y = h->cy + (h->yend - h->cy) * powf(s, 1.12f);
}

static void crest_table(hump_t *h)
{
	for (int i = 0; i <= NCT; i++) {
		crest_at(h, (float)i / NCT, &h->tu[i], &h->ty[i]);
		if (i > 0 && h->tu[i] < h->tu[i - 1] + 1e-6f)
			h->tu[i] = h->tu[i - 1] + 1e-6f; /* keep it monotonic for the lookup */
	}
}

/* the crest's y where it crosses column u (u between the summit and the crest's end) */
static float crest_y(const hump_t *h, float u)
{
	if (u <= h->tu[0])
		return h->ty[0];
	if (u >= h->tu[NCT])
		return h->ty[NCT];
	int lo = 0, hi = NCT;
	while (hi - lo > 1) {
		int m = (lo + hi) >> 1;
		if (h->tu[m] < u)
			lo = m;
		else
			hi = m;
	}
	float t = (u - h->tu[lo]) / (h->tu[hi] - h->tu[lo]);
	return us_lerp(h->ty[lo], h->ty[hi], t);
}

/* the dune's foot, curving out toward the viewer to the crest's end */
static float hump_toe(const hump_t *h, float base, float u)
{
	float ul = h->cx - h->wl, ue = crest_ex(h), ur = h->cx + h->wr;
	if (u <= ul || u >= ur)
		return base;
	if (u < ue) {
		float t = (u - ul) / (ue - ul);
		return base + (h->yend - base) * sinf(t * US_PI * 0.5f) * (0.6f + 0.4f * t);
	}
	float t = (u - ue) / (ur - ue);
	return h->yend + (base - h->yend) * us_smooth(0.0f, 1.0f, t);
}

/* ---- drawing helpers -------------------------------------------------------------------------- */

static inline void oput(const st_t *s, us_canvas *c, int px, int py, float v, uint8_t m, const short *occ)
{
	if ((unsigned)px >= (unsigned)c->w || (unsigned)py >= (unsigned)c->h)
		return;
	if (occ && px < MAXCOL && py >= occ[px])
		return;
	if (s->over) { /* pixel silhouettes: stand out against bright sand (overwrite, not MAX) */
		size_t i = (size_t)py * c->w + px;
		c->ink[i] = (uint8_t)(us_sat(v) * 255.0f);
		c->mat[i] = m;
		return;
	}
	us_put(c, px, py, v, m);
}

/* a fine line hidden behind nearer dunes (occ: per-column row limit, or NULL). Ink: Wu spans; crisp: 1 px */
static void oline(const st_t *s, us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t m, const short *occ)
{
	if (v < 0.004f)
		return;
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
	if (i1 - i0 > 2000)
		return;
	for (int i = i0; i <= i1; i++) {
		float yy = y0 + g * (i - x0);
		if (c->crisp) {
			int j = (int)floorf(yy + 0.5f);
			if (steep)
				oput(s, c, j, i, v, m, occ);
			else
				oput(s, c, i, j, v, m, occ);
			continue;
		}
		int j = (int)floorf(yy);
		float f = yy - j;
		if (steep) {
			oput(s, c, j, i, v * (1.0f - f), m, occ);
			oput(s, c, j + 1, i, v * f, m, occ);
		} else {
			oput(s, c, i, j, v * (1.0f - f), m, occ);
			oput(s, c, i, j + 1, v * f, m, occ);
		}
	}
}

/* a pixel-art value: quantised to the palette's shade steps with an ordered dither between them */
static inline float qd(float T, int px, int py)
{
	float q = us_clampf(T, 0.0f, 1.0f) * 4.0f;
	int i = (int)q;
	if (i > 3)
		return QV[4];
	if (us_smooth(0.4f, 0.6f, q - i) * 16.0f > BAYER[(py & 3) * 4 + (px & 3)] + 0.5f)
		i++;
	return QV[i];
}

/* the same, dithered all the way (for glows) */
static inline float qg(float T, int px, int py)
{
	float q = us_clampf(T, 0.0f, 1.0f) * 4.0f;
	int i = (int)q;
	if (i > 3)
		return QV[4];
	if ((q - i) * 16.0f > BAYER[(py & 3) * 4 + (px & 3)] + 0.5f)
		i++;
	return QV[i];
}

/* A low-res field drawn bilinearly over a normalized rect with MAX blend, skipping dark spans (much cheaper
 * than us_field for mostly-empty skies). dith: 0 = smooth (ink), 1 = stepped (qd), 2 = dithered glow (qg). */
static void rast(us_canvas *c, const float *f, int fw, int fh, float gain, uint8_t mat, float rx0, float ry0, float rx1, float ry1,
		 int dith)
{
	float S = c->s;
	int py0 = (int)(ry0 * S), py1 = (int)ceilf(ry1 * S), px0 = (int)(rx0 * S), px1 = (int)ceilf(rx1 * S);
	if (py0 < 0) py0 = 0;
	if (py1 > c->h) py1 = c->h;
	if (px0 < 0) px0 = 0;
	if (px1 > c->w) px1 = c->w;
	float row[256];
	if (fw > 255)
		return;
	float cpx = (rx1 - rx0) * S / fw; /* pixels per cell */
	for (int py = py0; py < py1; py++) {
		float fy = ((py + 0.5f) / S - ry0) / (ry1 - ry0) * fh - 0.5f;
		int iy = (int)floorf(fy);
		float ty = fy - iy;
		int i0 = iy < 0 ? 0 : iy > fh - 1 ? fh - 1 : iy, i1 = iy + 1 < 0 ? 0 : iy + 1 > fh - 1 ? fh - 1 : iy + 1;
		float mx = 0.0f;
		for (int i = 0; i < fw; i++) {
			row[i] = (f[i0 * fw + i] + (f[i1 * fw + i] - f[i0 * fw + i]) * ty) * gain;
			if (row[i] > mx)
				mx = row[i];
		}
		if (mx < 0.004f)
			continue;
		uint8_t *ink = c->ink + (size_t)py * c->w, *mt = c->mat + (size_t)py * c->w;
		for (int i = -1; i < fw; i++) {
			float va = row[i < 0 ? 0 : i], vb = row[i + 1 > fw - 1 ? fw - 1 : i + 1];
			if (va < 0.004f && vb < 0.004f)
				continue;
			int a = px0 + (int)ceilf((i + 0.5f) * cpx - 0.5f), b = px0 + (int)ceilf((i + 1.5f) * cpx - 0.5f);
			if (a < px0) a = px0;
			if (b > px1) b = px1;
			for (int px = a; px < b; px++) {
				float t = ((px + 0.5f) - px0) / cpx - 0.5f - i;
				float v = va + (vb - va) * us_clampf(t, 0.0f, 1.0f);
				if (dith == 1)
					v = qd(v, px, py);
				else if (dith == 2)
					v = qg(v, px, py);
				int iv = (int)(us_sat(v) * 255.0f);
				if (iv > ink[px]) {
					ink[px] = (uint8_t)iv;
					mt[px] = mat;
				}
			}
		}
	}
}

/* ---- create ------------------------------------------------------------------------------------ */

static void make_layers(st_t *s)
{
	us_rng *r = &s->rng;
	static const float BASE[NLAY] = {0.578f, 0.604f, 0.648f, 0.735f, 0.905f};
	static const int N[3] = {7, 5, 4};
	static const float H0[3] = {0.010f, 0.022f, 0.040f}, H1[3] = {0.024f, 0.046f, 0.075f};
	static const float W0[3] = {0.07f, 0.10f, 0.13f}, W1[3] = {0.10f, 0.14f, 0.18f};
	for (int li = 0; li < 3; li++) {
		layer_t *l = &s->lay[li];
		l->base = BASE[li];
		l->n = N[li];
		for (int i = 0; i < l->n; i++) {
			hump_t *h = &l->h[i];
			h->cx = (i + 0.25f + 0.5f * us_rand(r)) / l->n * 1.1f - 0.03f;
			float H = us_randr(r, H0[li], H1[li]);
			h->cy = l->base - H;
			h->wl = us_randr(r, W0[li], W1[li]);
			h->wr = h->wl * us_randr(r, 0.55f, 0.75f);
			h->dxe = h->wr * us_randr(r, 0.4f, 0.58f);
			h->yend = l->base + H * us_randr(r, 0.45f, 0.8f);
			h->bend = us_randr(r, -1.0f, 1.0f);
			h->rph = us_rand(r) * 50.0f;
		}
	}
	/* the hero range: a tall dune on the left third, a lower one on the right; a saddle between */
	layer_t *l = &s->lay[3];
	l->base = BASE[3];
	l->n = 2;
	hump_t *h = &l->h[0];
	h->cx = us_randr(r, 0.27f, 0.32f);
	h->cy = us_randr(r, 0.47f, 0.49f);
	h->wl = us_randr(r, 0.4f, 0.45f);
	h->wr = us_randr(r, 0.27f, 0.31f);
	h->dxe = us_randr(r, 0.15f, 0.18f);
	h->yend = us_randr(r, 0.86f, 0.9f);
	h->bend = us_randr(r, 0.4f, 1.0f);
	h->rph = us_rand(r) * 50.0f;
	h = &l->h[1];
	h->cx = us_randr(r, 0.82f, 0.88f);
	h->cy = us_randr(r, 0.55f, 0.58f);
	h->wl = us_randr(r, 0.2f, 0.24f);
	h->wr = us_randr(r, 0.22f, 0.26f);
	h->dxe = us_randr(r, 0.09f, 0.11f);
	h->yend = us_randr(r, 0.79f, 0.82f);
	h->bend = us_randr(r, -1.0f, -0.3f);
	h->rph = us_rand(r) * 50.0f;
	/* the near range: a broad slope rising out of the bottom-left corner, a low swell on the right */
	l = &s->lay[4];
	l->base = BASE[4];
	l->n = 2;
	h = &l->h[0];
	h->cx = us_randr(r, 0.08f, 0.13f);
	h->cy = us_randr(r, 0.755f, 0.775f);
	h->wl = 0.3f;
	h->wr = us_randr(r, 0.5f, 0.56f);
	h->dxe = us_randr(r, 0.3f, 0.34f);
	h->yend = 1.1f;
	h->bend = us_randr(r, 0.3f, 0.8f);
	h->rph = us_rand(r) * 50.0f;
	h = &l->h[1];
	h->cx = us_randr(r, 0.9f, 0.95f);
	h->cy = us_randr(r, 0.84f, 0.86f);
	h->wl = us_randr(r, 0.18f, 0.22f);
	h->wr = 0.2f;
	h->dxe = us_randr(r, 0.08f, 0.1f);
	h->yend = 1.04f;
	h->bend = us_randr(r, -0.5f, 0.5f);
	h->rph = us_rand(r) * 50.0f;
}

static void life_create(st_t *s);

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng *r = &s->rng;
	us_rng_seed(r, seed ^ 0x64756e65u);
	make_layers(s);
	for (int li = 0; li < NLAY; li++)
		for (int i = 0; i < s->lay[li].n; i++)
			crest_table(&s->lay[li].h[i]);
	/* the Milky Way: a band rising out of the dunes on the left and arching up to the right, with a dark
	 * dust lane down its middle and knots of brighter star clouds (fractions of the width; A ~ 16:9) */
	const float A0 = 16.0f / 9.0f;
	float p0u = 0.2f, p0y = 0.66f, p1u = 1.02f, p1y = -0.12f;
	float bx = (p1u - p0u) * A0, by = p1y - p0y, bl = sqrtf(bx * bx + by * by);
	float nx = -by / bl, ny = bx / bl;
	for (int j = 0; j < MWH; j++)
		for (int i = 0; i < MWW; i++) {
			float x = (i + 0.5f) / MWW * A0, y = (j + 0.5f) / MWH * HY;
			float dx = x - p0u * A0, dyy = y - p0y;
			float along = (dx * bx + dyy * by) / (bl * bl), off = dx * nx + dyy * ny;
			float wd = 0.075f + 0.03f * us_noise2(along * 3.0f, 4.2f) + 0.04f * (1.0f - along);
			float wob = 0.018f * us_noise2(along * 4.0f, 9.1f);
			float g = expf(-(off - wob) * (off - wob) / (wd * wd));
			float clump = 0.6f + 0.55f * us_fbm2(x * 7.0f, y * 7.0f, 3);
			float rift = expf(-(off - wob - 0.008f) * (off - wob - 0.008f) / (0.012f * 0.012f)) * us_smooth(0.05f, 0.25f, along) *
				     (1.0f - us_smooth(0.7f, 0.9f, along)) * (0.6f + 0.4f * us_noise2(along * 9.0f, 1.3f));
			float core = 1.0f + 0.8f * expf(-(along - 0.12f) * (along - 0.12f) / 0.02f);
			s->mw[j * MWW + i] = us_sat(g * clump * core * (1.0f - 0.85f * rift)) * 0.5f;
		}
	for (int i = 0; i < NDUST; i++) {
		float along = us_rand(r);
		float g = us_randn(r) * 0.42f;
		float wd = 0.075f + 0.03f * us_noise2(along * 3.0f, 4.2f) + 0.04f * (1.0f - along);
		float wob = 0.018f * us_noise2(along * 4.0f, 9.1f);
		float off = g * wd + wob;
		if (fabsf(off - wob - 0.008f) < 0.009f && along > 0.08f && along < 0.85f && us_rand(r) < 0.85f)
			off += (off > wob ? 0.012f : -0.012f); /* thin the dust lane */
		float x = p0u * A0 + bx * along + nx * off, y = p0y + by * along + ny * off;
		s->du[i] = x / A0;
		s->dy[i] = y;
		float m = us_rand(r);
		s->dm[i] = (0.2f + 0.8f * m * m * m) * expf(-g * g * 1.2f) * (0.7f + 0.5f * expf(-(along - 0.12f) * (along - 0.12f) / 0.03f));
	}
	for (int i = 0; i < NSTAR; i++) {
		s->su[i] = us_rand(r);
		s->sy[i] = us_rand(r) * HY;
		float m = us_rand(r);
		s->sm[i] = 0.12f + 0.88f * m * m * m;
		s->stw[i] = us_rand(r) * 60.0f;
	}
	/* the acacia: on the saddle between the hero dunes, right third */
	s->ac_u = us_randr(r, 0.63f, 0.67f);
	s->ac_h = us_randr(r, 0.17f, 0.19f);
	for (int k = 0; k < 4; k++) {
		float sp = (k - 1.5f) / 1.5f; /* -1..1 */
		s->limb[k][0] = 0.012f * sp;
		s->limb[k][1] = -us_randr(r, 0.3f, 0.42f);
		s->limb[k][2] = sp * us_randr(r, 0.32f, 0.44f) + us_randr(r, -0.04f, 0.04f);
		s->limb[k][3] = -us_randr(r, 0.8f, 0.86f) + 0.06f * fabsf(sp);
	}
	s->car_on = 1;
	s->car_u = us_randr(r, 0.42f, 0.55f);
	s->car_dir = 1.0f;
	s->car_n = 4;
	for (int i = 0; i < NTUFT; i++) {
		s->tu[i] = i < 2 ? us_randr(r, 0.02f, 0.2f) : us_randr(r, 0.8f, 0.98f);
		s->th[i] = us_randr(r, 0.045f, 0.075f);
	}
	for (int i = 0; i < NFLOW; i++) {
		s->fl_u[i] = us_randr(r, 0.45f, 0.82f);
		s->fl_f[i] = us_rand(r);
	}
	for (int i = 0; i < MAX_GL; i++)
		s->gl[i].age = 9.0f;
	s->T = us_rand(r) * 100.0f;
	s->av_wait = us_randr(r, 4.0f, 10.0f);
	s->met_wait = us_randr(r, 8.0f, 25.0f);
	s->lvl = 0.3f;
	life_create(s);
	return s;
}

static void destroy(void *p) { free(p); }

/* ---- the light ---------------------------------------------------------------------------------- */

typedef struct {
	float sun_u, sun_y, sun_e, moon_u, moon_y, moon_e;
	float sunk, moonk, tw, lx, Ik, Ia, warm, night;
	float Tw, Ts, Tf; /* windward / slip / floor tones, 0..1 */
} light_t;

static void light(const us_world *w, float gain, light_t *L)
{
	float sph = w->day_phase, mph = fmodf(sph + 0.5f, 1.0f);
	L->sun_e = -cosf(sph * US_TAU);
	L->moon_e = -cosf(mph * US_TAU);
	L->sun_u = 0.06f + 0.88f * (sph - 0.25f) / 0.5f;
	L->moon_u = 0.06f + 0.88f * (mph - 0.25f) / 0.5f;
	L->sun_y = HY - 0.5f * L->sun_e;
	L->moon_y = HY - 0.5f * L->moon_e;
	L->sunk = us_smooth(-0.1f, 0.3f, L->sun_e);
	L->tw = expf(-((L->sun_e + 0.04f) / 0.14f) * ((L->sun_e + 0.04f) / 0.14f));
	L->moonk = us_smooth(-0.05f, 0.25f, L->moon_e) * (1.0f - L->sunk) * 0.55f;
	L->night = us_sat(1.0f - 2.0f * w->daylight);
	float lxs = us_clampf((L->sun_u - 0.5f) * 2.4f, -1.0f, 1.0f) * (1.0f - 0.7f * us_smooth(0.25f, 0.95f, L->sun_e));
	float lxm = us_clampf((L->moon_u - 0.5f) * 2.4f, -1.0f, 1.0f) * (1.0f - 0.7f * us_smooth(0.25f, 0.95f, L->moon_e));
	float ws = L->sunk + 0.6f * L->tw, wm = L->moonk;
	L->lx = (ws * lxs + wm * lxm) / (ws + wm + 1e-4f);
	L->Ik = L->sunk + L->moonk + 0.3f * L->tw;
	L->Ia = 0.2f + 0.3f * w->daylight + 0.08f * L->tw;
	L->warm = us_sat(1.3f * L->sunk + 0.9f * L->tw);
	float Ik = L->Ik * gain, Ia = L->Ia * (0.8f + 0.2f * gain);
	L->Tw = Ia + Ik * us_sat(0.55f - 0.75f * L->lx);
	L->Ts = Ia + Ik * us_sat(0.35f + 0.9f * L->lx);
	L->Tf = Ia + Ik * 0.55f;
}

/* ---- the sand: fill each range with its faces' tones (one write per pixel, near to far) --------- */

static void fill(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L, float kick)
{
	float A = ctx->aspect, S = c->s;
	int W = c->w < MAXCOL ? c->w : MAXCOL, H = c->h, crisp = ctx->crisp;
	float fk = crisp ? 1.0f : 0.09f; /* ink: a faint wash of tone under the lines */
	for (int px = 0; px < W; px++) {
		float u = (px + 0.5f) / S / A;
		int lim = H;
		for (int li = NLAY - 1; li >= 0; li--) {
			s->occ[li][px] = (short)lim;
			int r0 = (int)ceilf(s->sil[li][px] * S - 0.5f);
			if (r0 < 0)
				r0 = 0;
			if (r0 >= lim)
				continue;
			const layer_t *l = &s->lay[li];
			float hz = LHAZE[li], lb = 0.55f + 0.45f * LBRI[li];
			float Thz = (L->Ia + 0.45f * L->Ik) * 0.9f;
			float tw = us_lerp(L->Tw, Thz, hz) * lb, ts = us_lerp(L->Ts, Thz, hz) * lb, tf = us_lerp(L->Tf, Thz, hz) * lb;
			/* which face is lit: it takes the warm light (LIGHT); the other stays sand-dark (EARTH) */
			int wlit = L->Tw >= L->Ts;
			float base = base_y(l, li, u);
			/* row tones, as segments [ra, rb) with a top + bottom tone and a face id (0 floor, 1 windward, 2 slip) */
			float segT0[8], segT1[8];
			int segA[8], segB[8], segF[8], ns = 0;
			segA[0] = r0;
			segB[0] = lim;
			segT0[0] = tf * 1.0f;
			segT1[0] = tf * 1.1f;
			segF[0] = 0;
			ns = 1;
			for (int hi = 0; hi < l->n && ns < 7; hi++) {
				const hump_t *h = &l->h[hi];
				if (u <= h->cx - h->wl || u >= h->cx + h->wr)
					continue;
				float top = hump_sil(h, base, u), bot = hump_toe(h, base, u);
				int ra = (int)ceilf(top * S - 0.5f), rb = (int)ceilf(bot * S - 0.5f);
				if (ra < r0)
					ra = r0;
				if (rb > lim)
					rb = lim;
				if (rb <= ra)
					continue;
				int rc;
				if (u <= h->cx)
					rc = ra;
				else if (u >= crest_ex(h))
					rc = rb;
				else {
					float cyv = crest_y(h, u);
					rc = (int)ceilf(cyv * S - 0.5f);
					rc = rc < ra ? ra : rc > rb ? rb : rc;
				}
				/* the slip face: core shadow under the crest, a little bounce light toward the foot */
				if (rc > ra && ns < 8) {
					segA[ns] = ra;
					segB[ns] = rc;
					segT0[ns] = ts * (wlit ? 0.8f : 1.1f);
					segT1[ns] = ts * (wlit ? 1.05f : 0.85f);
					segF[ns++] = 2;
				}
				/* the windward back: brightest just under the crest, easing toward the foot */
				if (rb > rc && ns < 8) {
					segA[ns] = rc;
					segB[ns] = rb;
					segT0[ns] = tw * 1.1f;
					segT1[ns] = tw * 0.82f;
					segF[ns++] = 1;
				}
			}
			for (int k = 0; k < ns; k++) {
				int ra = segA[k], rb = segB[k];
				float T0 = segT0[k], dT = (segT1[k] - T0) / (float)(rb - ra > 1 ? rb - ra : 1);
				int face = segF[k];
				int lit = (face == 1 && wlit) || (face == 2 && !wlit);
				float warm = lit ? (0.3f + 0.7f * L->warm) * (1.0f - 0.6f * hz) : 0.0f;
				if (k == 0)
					warm = (0.2f + 0.8f * L->warm) * (1.0f - hz);
				if (!crisp && (lit || k == 0))
					warm = 1.0f; /* ink: the lit sand carries its warm hue at any hour */
				float pulse = 1.0f + (crisp ? 0.22f + 0.55f * s->lvl : 0.17f + 0.55f * s->lvl) * kick * (lit ? 1.0f : 0.4f) * (0.3f + 0.7f * LBRI[li]);
				uint8_t *ink = c->ink + (size_t)ra * c->w + px, *mat = c->mat + (size_t)ra * c->w + px;
				float T = T0 * pulse;
				dT *= pulse;
				if (crisp) {
					uint8_t m = warm > 0.4f ? US_MAT_LIGHT : US_MAT_EARTH;
					for (int py = ra; py < rb; py++, ink += c->w, mat += c->w, T += dT) {
						/* flat shades, with a narrow dithered seam where one shade gives way to the next */
						float qf = us_sat(T * 0.8f) * 3.0f + 0.25f;
						int qi = (int)qf;
						if (us_smooth(0.38f, 0.62f, qf - qi) * 16.0f > BAYER[(py & 3) * 4 + (px & 3)] + 0.5f)
							qi++;
						float q = QV[qi < 1 ? 1 : qi > 4 ? 4 : qi];
						*ink = (uint8_t)(q * 255.0f);
						*mat = m;
					}
				} else {
					uint8_t m = warm > 0.5f ? US_MAT_LIGHT : US_MAT_EARTH;
					for (int py = ra; py < rb; py++, ink += c->w, mat += c->w, T += dT) {
						int iv = (int)(us_sat(T) * fk * 255.0f);
						*ink = (uint8_t)(iv < 2 ? 2 : iv > 255 ? 255 : iv);
						*mat = m;
					}
				}
			}
			lim = r0;
		}
	}
	for (int li = 0; li < NLAY; li++)
		for (int px = W; px < MAXCOL && px < c->w; px++)
			s->occ[li][px] = 0;
}

/* ---- per-layer lines: outline, crest, ripples ----------------------------------------------------- */

static void layer_lines(st_t *s, const us_ctx *ctx, us_canvas *c, int li, const light_t *L, float kick, float dens)
{
	const layer_t *l = &s->lay[li];
	float A = ctx->aspect, S = c->s;
	int W = c->w < MAXCOL ? c->w : MAXCOL;
	const short *occ = s->occ[li];
	float lb = LBRI[li], hz = LHAZE[li];
	int wlit = L->Tw >= L->Ts;
	float Tl = us_sat(wlit ? L->Tw : L->Ts) * 0.85f, Td = us_sat(wlit ? L->Ts : L->Tw) * 0.85f;
	uint8_t lm = (L->warm > 0.45f || !ctx->crisp) ? US_MAT_LIGHT : US_MAT_EARTH;
	int crisp = ctx->crisp;
	float lk = crisp ? 0.75f : 1.0f;

	/* the outline against what lies behind: bright on the lit side of each summit */
	{
		int step = crisp ? 1 : 2;
		float pu = -1, py = 0, pv = 0;
		for (int px = 0; px <= W; px += step) {
			int q = px < W ? px : W - 1;
			float u = (q + 0.5f) / S / A, y = s->sil[li][q];
			/* the side of the nearest summit this column belongs to */
			float side = 0.5f;
			float best = 9.0f;
			float base = base_y(l, li, u);
			for (int hi = 0; hi < l->n; hi++) {
				float hy = hump_sil(&l->h[hi], base, u);
				if (hy < best) {
					best = hy;
					side = u < l->h[hi].cx ? 1.0f : 0.0f;
				}
			}
			float lit = best < base - 0.002f ? (wlit ? side : 1.0f - side) : 0.5f;
			float v = lb * (0.22f + 0.38f * us_lerp(Td, Tl, lit) + 0.2f * hz) * (1.0f + 0.4f * kick);
			if (pu >= 0)
				oline(s, c, pu * A, py, u * A, y, lk * 0.5f * (v + pv), lit > 0.6f ? lm : US_MAT_EARTH, occ);
			pu = u;
			py = y;
			pv = v;
		}
	}
	if (li < 1)
		return;
	for (int hi = 0; hi < l->n; hi++) {
		const hump_t *h = &l->h[hi];
		/* the crest: a fine bright line from the summit sweeping down to the foot */
		float cv = lb * (0.3f + 0.55f * Tl) * (1.0f + (0.5f + 0.4f * s->lvl) * kick) * (1.0f - 0.4f * hz);
		float pu, pyy;
		crest_at(h, 0.0f, &pu, &pyy);
		for (int q = 1; q <= NC; q++) {
			float sq = (float)q / NC, cu, cyv;
			crest_at(h, sq, &cu, &cyv);
			float sil = hump_sil(h, base_y(l, li, cu), cu);
			if (cyv < sil)
				cyv = sil;
			float fade = 1.0f - us_smooth(0.8f, 1.0f, sq);
			oline(s, c, pu * A, pyy, cu * A, cyv, cv * fade, lm, occ);
			if (!crisp && li >= 3) /* the hero crests: a second strand, a knife-edge of light */
				oline(s, c, pu * A + 0.6f / S, pyy, cu * A + 0.6f / S, cyv, cv * fade * 0.45f, lm, occ);
			pu = cu;
			pyy = cyv;
		}
		if (li < 2)
			continue;
		/* a gust (snare): a sheet of spindrift tears off the crest and the summit's outline, downwind */
		if (s->snare_e > 0.03f) {
			float sn = s->snare_e;
			float sv = lb * sn * (0.4f + 0.5f * (L->Ia + L->Ik)) * (crisp ? 2.0f : 1.15f);
			float len0 = (0.025f + 0.025f * (li - 2)) * (0.4f + 0.8f * sn) * (0.8f + 0.4f * s->wid) * (crisp ? 1.5f : 1.0f);
			for (int q = 0; q < 26; q++) {
				uint32_t hh = (uint32_t)(q * 977 + hi * 131 + li * 7919) ^ s->gust_seed;
				float x, y;
				if (q < 16) {
					float cu, cyv;
					crest_at(h, 0.7f * q / 16.0f, &cu, &cyv);
					float sil = hump_sil(h, base_y(l, li, cu), cu);
					x = cu * A;
					y = cyv < sil ? sil : cyv;
				} else {
					float u = h->cx - h->wl * 0.3f * (q - 16) / 10.0f;
					x = u * A;
					y = hump_sil(h, base_y(l, li, u), u);
				}
				float ln = len0 * (0.4f + 0.9f * hashf(hh));
				float rise = ln * (0.12f + 0.25f * hashf(hh + 1) + 0.2f * s->cen);
				float mx = x + ln * 0.5f, my = y - rise * 0.8f;
				oline(s, c, x, y - 0.001f, mx, my, sv, lm, occ);
				oline(s, c, mx, my, x + ln, y - rise + ln * 0.1f, sv * 0.45f, lm, occ);
			}
		}
		/* wind ripples on the windward back, following the dune's form; the slip face stays smooth */
		int nr = li == 2 ? 3 : li == 3 ? 10 : 12;
		nr = (int)(nr * (0.55f + 0.9f * dens));
		if (crisp)
			nr = nr * 2 / 3;
		float rv0 = lb * (wlit ? 0.1f + 0.24f * us_sat(L->Tw) : 0.05f + 0.16f * us_sat(L->Tw));
		uint8_t rm = wlit ? lm : US_MAT_EARTH;
		float u0 = h->cx - h->wl, u1 = crest_ex(h);
		int nsamp = (int)((u1 - u0) * A * S / (crisp ? 2.0f : 5.0f)) + 2;
		if (nsamp > 260)
			nsamp = 260;
		for (int k = 0; k < nr; k++) {
			float f = powf((k + 0.6f + 0.35f * hashf((uint32_t)(k * 31 + hi * 7 + li * 131))) / (nr + 0.4f), 0.85f);
			float wl = 0.018f + 0.02f * (li - 2); /* wavelength of the sinuous ripple crests */
			float amp = (0.0016f + 0.0015f * (li - 2)) * (0.6f + 0.6f * f);
			float px_ = 0, py_ = 0;
			int have = 0;
			for (int q = 0; q <= nsamp; q++) {
				float u = u0 + (u1 - u0) * q / nsamp;
				float base = base_y(l, li, u);
				float top = hump_sil(h, base, u), bot = hump_toe(h, base, u);
				if (u > h->cx) /* past the summit the windward face starts at the crest */
					top = crest_y(h, u);
				float y = top + (bot - top) * f + amp * sinf(u * A / wl + h->rph + k * 1.7f + 0.6f * us_noise2(u * 6.0f, k * 0.7f + h->rph)) +
					  0.003f * us_noise2(u * 3.0f + k * 0.3f, h->rph);
				float vis = us_smooth(0.0f, 0.12f, (u - u0) / (u1 - u0));
				vis *= us_smooth(0.004f, 0.03f + 0.02f * (li - 2), bot - top); /* fade where the face narrows to a point */
				/* the near ranges light where their band sounds */
				float bf = us_sat((u * A) / A) * (US_NBANDS - 2);
				int bi = (int)bf;
				float bnd = us_lerp(s->bandv[bi], s->bandv[bi + 1], bf - bi);
				float v = rv0 * vis * (1.0f - 0.35f * f) * (li >= 3 ? 0.75f + 0.7f * bnd : 1.0f);
				/* ripples come and go along their length (they bifurcate and die out) */
				v *= us_sat(0.55f + 0.9f * us_noise2(u * 14.0f + k * 3.1f, h->rph + k));
				float x = u * A;
				if (have && v > 0.01f)
					oline(s, c, px_, py_, x, y, v * lk, rm, occ);
				px_ = x;
				py_ = y;
				have = 1;
			}
		}
	}
	/* floor ripples between the near dunes: long low waves, wider apart toward the viewer */
	if (li >= 3) {
		int nr = (int)((li == 3 ? 5 : 7) * (0.6f + 0.8f * dens));
		float y0 = l->base + 0.004f;
		float rv = lb * (0.1f + 0.26f * L->Tf);
		for (int k = 0; k < nr; k++) {
			float y = y0 + 0.006f * (li - 2) * (k + 0.5f) * (1.0f + 0.25f * k);
			float px_ = 0, py_ = 0;
			int have = 0, nsamp = (int)(A * S / (crisp ? 2.0f : 6.0f));
			for (int q = 0; q <= nsamp; q++) {
				float u = (float)q / nsamp;
				float yy = y + 0.0018f * (li - 2) * sinf(u * A * 140.0f / (li - 1) + k * 2.1f + us_noise2(u * 5.0f, k));
				/* hidden where a dune of this range stands on the floor */
				int inside = 0;
				for (int hi = 0; hi < l->n; hi++) {
					const hump_t *h = &l->h[hi];
					if (u > h->cx - h->wl && u < h->cx + h->wr && yy < hump_toe(h, l->base, u) + 0.004f)
						inside = 1;
				}
				float v = inside ? 0.0f : rv * us_sat(0.4f + 0.9f * us_noise2(u * 9.0f + k * 2.0f, 5.0f + k));
				float x = u * A;
				if (have && v > 0.01f)
					oline(s, c, px_, py_, x, yy, v * lk, lm, occ);
				px_ = x;
				py_ = yy;
				have = 1;
			}
		}
	}
}

/* ---- the acacia --------------------------------------------------------------------------------- */

static void acacia_ground(const st_t *s, float *x, float *y, float A)
{
	const layer_t *l = &s->lay[3];
	*x = s->ac_u * A;
	*y = l->base + 0.012f;
}

/* the crown's outline: a flat umbrella of foliage, a lumpy top over a flat underside (a in -1..1) */
static float crown_top(float a, int ti)
{
	float env = sqrtf(us_sat(1.0f - a * a));
	float lump = 0.0f;
	for (int k = 0; k < 6; k++) {
		float ak = -0.85f + 0.34f * k + 0.05f * ti, d = (a - ak) / 0.17f;
		lump += (0.22f + 0.1f * ((k * 7 + ti) % 3)) * expf(-d * d);
	}
	return powf(env, 0.35f) * (0.55f + 0.45f * env) + 0.35f * lump * env;
}

static void acacia(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L, float sway)
{
	float A = ctx->aspect, x0, y0;
	acacia_ground(s, &x0, &y0, A);
	float h = s->ac_h, S = c->s;
	const short *occ = s->occ[3];
	int crisp = ctx->crisp;
	float spring = us_world_season_w(ctx->w, 0), winter = us_world_season_w(ctx->w, 3);
	float bv = 0.26f + 0.22f * (L->Ia + 0.5f * L->Ik);
	float lead = L->lx;
	float sw = sway * h;
	/* trunk: leaning a little, forking low into limbs that fan up under the crown */
	float fx = x0 + 0.02f * h, fy = y0 - 0.34f * h;
	float tw = crisp ? 0.5f / S : 0.0022f;
	for (int q = -1; q <= 1; q++)
		oline(s, c, x0 + q * tw * (1.3f + 0.4f * (q > 0)), y0, fx + q * tw * 0.7f, fy, bv * (q == 0 ? 0.9f : 0.75f), US_MAT_STEM, occ);
	/* the crown */
	float cw = 0.62f * h, cy = y0 - 0.86f * h + 0.0f, th = 0.085f * h;
	float ccx = x0 + 0.05f * h + sw;
	/* limbs: a fan of five, each with a kink, ending under the crown */
	for (int k = 0; k < 4; k++) {
		float ax = x0 + s->limb[k][0] * h + 0.02f * h, ay = y0 + s->limb[k][1] * h;
		float bx = ccx + s->limb[k][2] * cw * 1.55f, by = cy + th * 0.2f;
		float mx = us_lerp(ax, bx, 0.55f) - 0.03f * h * (k - 1.5f), my = us_lerp(ay, by, 0.5f);
		oline(s, c, fx, fy, ax, ay, bv * 0.85f, US_MAT_STEM, occ);
		oline(s, c, ax, ay, mx, my, bv * 0.75f, US_MAT_STEM, occ);
		oline(s, c, mx, my, bx, by, bv * 0.65f, US_MAT_STEM, occ);
		oline(s, c, mx, my, us_lerp(mx, bx, 0.8f) + 0.12f * h * (k < 2 ? -1.0f : 1.0f), by + th * 0.1f, bv * 0.45f, US_MAT_STEM, occ);
	}
	/* two tiers: the broad main umbrella and a smaller one lower down on one side */
	struct {
		float dx, dy, w, t;
	} tier[2] = {{0.0f, 0.0f, 1.0f, 1.0f}, {-0.62f, 0.13f, 0.42f, 0.7f}};
	float fol = 1.0f - 0.4f * winter;
	for (int ti = 0; ti < 2; ti++) {
		float cx = ccx + tier[ti].dx * cw, ty0 = cy + tier[ti].dy * h, hw = tier[ti].w * cw, tt = tier[ti].t * th;
		if (crisp) { /* solid canopy: shade under, lit top edge */
			int r0 = (int)((ty0 - tt * 1.5f) * S), r1 = (int)((ty0 + tt * 0.5f) * S) + 1;
			int c0 = (int)((cx - hw) * S), c1 = (int)((cx + hw) * S);
			for (int px = c0; px <= c1; px++) {
				float a = ((px + 0.5f) / S - cx) / hw;
				if (a <= -1.0f || a >= 1.0f)
					continue;
				float top = ty0 - tt * crown_top(a, ti), bot = ty0 + tt * 0.35f * sqrtf(us_sat(1.0f - a * a));
				for (int py = r0; py <= r1; py++) {
					float y = (py + 0.5f) / S;
					if (y < top || y > bot)
						continue;
					float v = y < top + 1.2f / S ? STEP3 : (y > bot - 1.2f / S ? STEP1 : STEP2);
					if (y < top + 1.2f / S && a * lead < -0.2f)
						v = STEP2;
					oput(s, c, px, py, v * (0.75f + 0.5f * bv), US_MAT_LEAF, occ);
				}
			}
		} else {
			int N = 44;
			float ptx = 0, pty = 0, pby = 0;
			for (int q = 0; q <= N; q++) {
				float a = -1.0f + 2.0f * q / N;
				float xx = cx + a * hw;
				float top = ty0 - tt * crown_top(a, ti) + 0.0012f * us_noise2(a * 6.0f, (float)ctx->t * 0.7f + ti);
				float bot = ty0 + tt * 0.35f * sqrtf(us_sat(1.0f - a * a));
				float litk = 0.8f + 0.35f * a * lead;
				if (q > 0) {
					oline(s, c, ptx, pty, xx, top, bv * 1.1f * litk, US_MAT_LEAF, occ);
					oline(s, c, ptx, pby, xx, bot, bv * 0.5f, US_MAT_LEAF, occ);
				}
				ptx = xx;
				pty = top;
				pby = bot;
			}
			/* foliage: rows of short strokes, denser toward the lit top */
			int nst = (int)(120 * tier[ti].w * fol);
			for (int k = 0; k < nst; k++) {
				uint32_t hh = (uint32_t)(k * 7919 + ti * 104729);
				float a = hashf(hh) * 1.9f - 0.95f, b = hashf(hh + 1);
				float top = ty0 - tt * crown_top(a, ti), bot = ty0 + tt * 0.35f * sqrtf(us_sat(1.0f - a * a));
				float yy = us_lerp(top + 0.0015f, bot - 0.001f, b * b);
				float xx = cx + a * hw;
				float ln = hw * (0.03f + 0.05f * hashf(hh + 2));
				float shim = 0.0012f * us_noise2(k * 0.37f, (float)ctx->t * 1.1f);
				oline(s, c, xx - ln + shim, yy, xx + ln + shim, yy + 0.0008f * (hashf(hh + 3) - 0.5f),
				      bv * (0.7f - 0.4f * b) * (0.85f + 0.3f * a * lead), US_MAT_LEAF, occ);
			}
		}
		/* spring: golden puffball blossom along the crown top */
		if (spring > 0.15f) {
			int nb = (int)(30 * tier[ti].w * spring);
			for (int k = 0; k < nb; k++) {
				uint32_t hh = (uint32_t)(k * 31337 + ti * 7);
				float a = hashf(hh) * 1.8f - 0.9f;
				float yy = ty0 - tt * crown_top(a, ti) * (0.6f + 0.4f * hashf(hh + 1));
				oput(s, c, (int)((cx + a * hw) * S), (int)(yy * S), (crisp ? STEP4 : 0.75f) * spring, US_MAT_LIGHT, occ);
			}
		}
	}
}

/* the tree's shadow on the sand, thrown away from the light (written straight into the fill) */
static void acacia_shadow(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L)
{
	float A = ctx->aspect, x0, y0;
	acacia_ground(s, &x0, &y0, A);
	float k = us_sat(L->Ik * 1.4f);
	if (k < 0.05f)
		return;
	float cx = x0 - L->lx * s->ac_h * 0.55f, cy = y0 + 0.006f, rx = s->ac_h * 0.5f * (1.0f + 0.6f * fabsf(L->lx)), ry = 0.008f;
	int x0p = (int)((cx - rx) * c->s), x1p = (int)((cx + rx) * c->s), y0p = (int)((cy - ry) * c->s), y1p = (int)((cy + ry) * c->s);
	for (int py = y0p; py <= y1p; py++) {
		if (py < 0 || py >= c->h)
			continue;
		for (int px = x0p; px <= x1p; px++) {
			if (px < 0 || px >= c->w || px >= MAXCOL || py >= s->occ[3][px])
				continue;
			float dx = ((px + 0.5f) / c->s - cx) / rx, dy = ((py + 0.5f) / c->s - cy) / ry;
			float d = dx * dx + dy * dy;
			if (d >= 1.0f)
				continue;
			/* a dappled shade: the crown's gaps let light through */
			float dap = us_noise2(dx * 4.0f, dy * 3.0f + 3.0f);
			if (dap > 0.35f)
				continue;
			size_t i = (size_t)py * c->w + px;
			float f = 1.0f - 0.55f * k * us_smooth(1.0f, 0.6f, d);
			c->ink[i] = (uint8_t)(c->ink[i] * f);
			if (ctx->crisp) {
				c->ink[i] = (uint8_t)(qd(c->ink[i] / 255.0f, px, py) * 255.0f);
				if (c->ink[i] < (uint8_t)(STEP1 * 255.0f))
					c->ink[i] = (uint8_t)(STEP1 * 255.0f);
				c->mat[i] = US_MAT_EARTH;
			}
		}
	}
}

/* ---- the caravan: camels walking the middle crests --------------------------------------------- */

static void camel(const st_t *s, us_canvas *c, float x, float y, float k, float d, float ph, float v, const short *occ, int crisp)
{
	/* long legs in a pacing gait, a deep body under one hump, a long neck dipping forward to a small head */
#define P(ax, ay) (x + d * (ax) * k), (y - (ay) * k)
	static const float back[][2] = {{-0.46f, 0.6f}, {-0.42f, 0.72f}, {-0.26f, 0.86f}, {-0.08f, 0.97f}, {0.08f, 0.9f},
					{0.24f, 0.76f}, {0.34f, 0.72f}, {0.5f, 0.66f}, {0.6f, 0.72f}, {0.66f, 0.86f}, {0.76f, 0.88f}, {0.84f, 0.84f}};
	static const float belly[][2] = {{-0.46f, 0.6f}, {-0.36f, 0.5f}, {0.0f, 0.47f}, {0.3f, 0.52f}, {0.44f, 0.58f}, {0.58f, 0.64f},
					 {0.68f, 0.8f}, {0.84f, 0.84f}};
	for (int i = 0; i + 1 < 12; i++)
		oline(s, c, P(back[i][0], back[i][1]), P(back[i + 1][0], back[i + 1][1]), v, US_MAT_ACCENT, occ);
	for (int i = 0; i + 1 < 8; i++)
		oline(s, c, P(belly[i][0], belly[i][1]), P(belly[i + 1][0], belly[i + 1][1]), v * 0.8f, US_MAT_ACCENT, occ);
	if (crisp) /* fill the body at pixel scale */
		for (int q = 0; q < 4; q++)
			oline(s, c, P(-0.36f, 0.56f + 0.08f * q), P(0.3f - 0.1f * q, 0.58f + 0.08f * q), v, US_MAT_ACCENT, occ);
	oline(s, c, P(-0.46f, 0.62f), P(-0.5f, 0.42f), v * 0.6f, US_MAT_ACCENT, occ); /* tail */
	for (int l = 0; l < 4; l++) {
		float lx = l < 2 ? 0.26f + 0.06f * l : -0.36f + 0.07f * (l - 2);
		float sw = sinf(ph + ((l & 1) ? US_PI : 0.0f)); /* pacing: both legs of a side move together */
		float foot = lx + 0.13f * sw, knee = lx + 0.03f * sw + (l < 2 ? 0.02f : -0.02f);
		float lift = 0.05f * us_sat(sw);
		oline(s, c, P(lx, 0.52f), P(knee, 0.26f + lift), v * 0.9f, US_MAT_ACCENT, occ);
		oline(s, c, P(knee, 0.26f + lift), P(foot, lift), v * 0.9f, US_MAT_ACCENT, occ);
	}
#undef P
}

static void caravan(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L)
{
	float A = ctx->aspect, S = c->s;
	if (!s->car_on) {
		s->car_wait -= ctx->dt;
		if (s->car_wait > 0)
			return;
		s->car_on = 1;
		s->car_dir = us_rand(&s->rng) < 0.6f ? 1.0f : -1.0f;
		s->car_u = s->car_dir > 0 ? -0.08f : 1.08f;
		s->car_n = 3 + (int)(us_rand(&s->rng) * 3.0f);
	}
	s->car_u += s->car_dir * 0.0042f * ctx->dt;
	s->car_ph += ctx->dt * 3.4f;
	if (s->car_u < -0.15f || s->car_u > 1.15f) {
		s->car_on = 0;
		s->car_wait = us_randr(&s->rng, 25.0f, 70.0f);
		return;
	}
	float k = ctx->crisp ? 0.034f : 0.03f, gap = k * 1.45f / A;
	float v = 0.36f + 0.25f * (L->Ia + 0.4f * L->Ik);
	const short *occ = s->occ[2];
	int W = c->w < MAXCOL ? c->w : MAXCOL;
	for (int i = 0; i <= s->car_n; i++) {
		float u = s->car_u - s->car_dir * gap * i;
		int px = (int)(u * A * S);
		if (px < 0 || px >= W)
			continue;
		float y = s->sil[2][px] + 0.0015f;
		if (i == 0) { /* the guide, on foot, a little ahead */
			float hx = u * A + s->car_dir * 0.01f;
			float bob = 0.0008f * fabsf(sinf(s->car_ph * 1.3f));
			oline(s, c, hx, y, hx, y - 0.016f - bob, v, US_MAT_ACCENT, occ);
			oline(s, c, hx - 0.003f, y, hx, y - 0.01f, v * 0.8f, US_MAT_ACCENT, occ); /* robe */
			oline(s, c, hx + 0.003f, y, hx, y - 0.01f, v * 0.8f, US_MAT_ACCENT, occ);
			oput(s, c, (int)(hx * S), (int)((y - 0.019f - bob) * S), v, US_MAT_ACCENT, occ);
			/* the lead rope back to the first camel */
			oline(s, c, hx - s->car_dir * 0.002f, y - 0.01f, (u - s->car_dir * gap) * A + s->car_dir * k * 0.75f, y - 0.019f, v * 0.4f,
			      US_MAT_ACCENT, occ);
			continue;
		}
		float ph = s->car_ph + i * 1.9f;
		camel(s, c, u * A, y - 0.0007f * fabsf(sinf(ph)), k, s->car_dir, ph, v, occ, ctx->crisp);
		if (i < s->car_n) /* a rope to the next */
			oline(s, c, (u - s->car_dir * 0.45f * k / A) * A, y - 0.6f * k, (u - s->car_dir * gap) * A + s->car_dir * 0.8f * k, y - 0.9f * k,
			      v * 0.35f, US_MAT_ACCENT, occ);
	}
}

/* ---- sand in the air ---------------------------------------------------------------------------- */

static void emit(st_t *s, int lay, float x, float y, float vx, float vy, float life, float v)
{
	grain_t *g = &s->g[s->gi];
	s->gi = (s->gi + 1) % MAXP;
	g->x = x;
	g->y = y;
	g->vx = vx;
	g->vy = vy;
	g->life = g->max = life;
	g->v = v;
	g->lay = lay;
}

/* a point on a hump's crest or upper outline where the wind can pick sand up */
static void crest_point(st_t *s, const layer_t *l, int li, const hump_t *h, float *x, float *y, float A)
{
	us_rng *r = &s->rng;
	if (us_rand(r) < 0.55f) {
		float sq = us_rand(r);
		sq = sq * sq * 0.6f;
		float cu, cyv;
		crest_at(h, sq, &cu, &cyv);
		float sil = hump_sil(h, base_y(l, li, cu), cu);
		*x = cu * A;
		*y = (cyv < sil ? sil : cyv) - 0.001f;
	} else { /* the windward outline just below the summit */
		float u = h->cx - h->wl * us_rand(r) * 0.35f;
		*x = u * A;
		*y = hump_sil(h, base_y(l, li, u), u) - 0.001f;
	}
}

static void sand(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L, float wind, float dens, float A)
{
	us_rng *r = &s->rng;
	float dt = ctx->dt;
	/* steady streaming off the crests: wind + loudness */
	s->emit_acc += dt * (70.0f + 200.0f * wind * wind) * (0.5f + dens);
	while (s->emit_acc >= 1.0f) {
		s->emit_acc -= 1.0f;
		int li = 1 + (int)(us_rand(r) * 3.999f);
		const layer_t *l = &s->lay[li];
		const hump_t *h = &l->h[(int)(us_rand(r) * l->n) % l->n];
		float x, y;
		crest_point(s, l, li, h, &x, &y, A);
		float ds = 0.4f + 0.2f * li;
		emit(s, li, x, y, (0.03f + 0.09f * wind) * ds * us_randr(r, 0.6f, 1.4f) * (0.8f + 0.4f * s->wid),
		     -us_randr(r, 0.0f, 0.02f + 0.03f * s->cen) * ds, us_randr(r, 0.9f, 2.4f), us_randr(r, 0.35f, 0.75f));
	}
	float lightv = 0.45f + 0.5f * (L->Ia + 0.6f * L->Ik);
	for (int i = 0; i < MAXP; i++) {
		grain_t *g = &s->g[i];
		if (g->life <= 0)
			continue;
		g->life -= dt;
		float n = us_noise3(g->x * 9.0f, g->y * 9.0f, s->T * 0.7f);
		g->vx += dt * (0.02f * n);
		g->vy += dt * (0.018f + 0.02f * us_noise3(g->x * 7.0f, g->y * 7.0f + 4.0f, s->T * 0.6f));
		g->x += g->vx * dt;
		g->y += g->vy * dt;
		float age = 1.0f - g->life / g->max;
		float v = g->v * lightv * us_smooth(0.0f, 0.12f, age) * (1.0f - us_smooth(0.45f, 1.0f, age)) * LBRI[g->lay];
		float len = ctx->crisp ? 0.06f : 0.05f;
		oline(s, c, g->x, g->y, g->x - g->vx * len, g->y - g->vy * len, v, L->warm > 0.45f ? US_MAT_LIGHT : US_MAT_EARTH,
		      s->occ[g->lay]);
	}
}

/* ---- kick: avalanches down the slip faces -------------------------------------------------------- */

static void avalanches(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L, float A)
{
	for (int i = 0; i < MAX_AV; i++) {
		aval_t *a = &s->av[i];
		if (!a->on)
			continue;
		a->age += ctx->real_dt;
		if (a->age > 4.0f) {
			a->on = 0;
			continue;
		}
		const layer_t *l = &s->lay[a->lay];
		const hump_t *h = &l->h[a->hi];
		float pu, py;
		crest_at(h, a->s, &pu, &py);
		float sil = hump_sil(h, base_y(l, a->lay, pu), pu);
		if (py < sil)
			py = sil;
		/* down the fall line to the foot on the slip side */
		float qu = us_lerp(h->cx + h->wr, crest_ex(h), a->s), qy = hump_toe(h, l->base, qu);
		float dx = (qu - pu) * A, dy = qy - py;
		float front = 1.0f - expf(-a->age / 0.45f);
		float bright = (a->age < 0.05f ? a->age / 0.05f : 1.0f) * expf(-a->age / 0.9f) + 0.18f * expf(-a->age / 3.0f);
		float lb = LBRI[a->lay] * (0.55f + 0.4f * us_sat(L->Ia + L->Ik));
		const short *occ = s->occ[a->lay];
		uint8_t m = L->warm > 0.45f ? US_MAT_LIGHT : US_MAT_EARTH;
		float wdt = 0.02f + 0.012f * (a->lay - 2);
		float nx = -dy, ny = dx, nl = sqrtf(nx * nx + ny * ny) + 1e-6f;
		nx /= nl;
		ny /= nl;
		/* a tongue of grain-flow: near-parallel strands, sinuous, the middle ones running furthest */
		int ns = ctx->crisp ? 3 : 6;
		for (int k = -ns; k <= ns; k++) {
			float kf = (float)k / ns;
			uint32_t hh = (uint32_t)(i * 13 + k + 7 + a->hi * 31 + a->lay * 101);
			float lk = front * (1.0f - 0.45f * kf * kf) * (0.75f + 0.25f * hashf(hh));
			float px_ = 0, py_ = 0;
			enum { SEG = 7 };
			for (int q = 0; q <= SEG; q++) {
				float t = lk * q / SEG;
				float o = kf * wdt * (0.55f + 0.45f * t / (lk + 1e-4f)) + 0.004f * sinf(t * 14.0f + kf * 3.0f + a->s * 20.0f) * t;
				float xx = pu * A + dx * t + nx * o, yy = py + dy * t + ny * o;
				if (q > 0)
					oline(s, c, px_, py_, xx, yy, lb * bright * (1.0f - 0.35f * kf * kf) * (0.6f + 0.4f * (1.0f - (float)q / SEG)), m, occ);
				px_ = xx;
				py_ = yy;
			}
		}
	}
}

static void start_avalanche(st_t *s, float strength, float A)
{
	us_rng *r = &s->rng;
	int slot = -1;
	for (int i = 0; i < MAX_AV; i++)
		if (!s->av[i].on) {
			slot = i;
			break;
		}
	if (slot < 0) {
		float oldest = -1;
		for (int i = 0; i < MAX_AV; i++)
			if (s->av[i].age > oldest) {
				oldest = s->av[i].age;
				slot = i;
			}
	}
	aval_t *a = &s->av[slot];
	float pick = us_rand(r);
	a->lay = pick < 0.5f ? 3 : pick < 0.8f ? 2 : 4;
	a->hi = (int)(us_rand(r) * s->lay[a->lay].n) % s->lay[a->lay].n;
	a->s = us_randr(r, 0.06f, a->lay == 4 ? 0.35f : 0.55f);
	a->age = 0.0f;
	a->on = 1;
	/* a puff of sand thrown up where it breaks away */
	const layer_t *l = &s->lay[a->lay];
	const hump_t *h = &l->h[a->hi];
	float pu, py;
	crest_at(h, a->s, &pu, &py);
	float sil = hump_sil(h, base_y(l, a->lay, pu), pu);
	if (py < sil)
		py = sil;
	float ds = 0.4f + 0.2f * a->lay;
	int n = (int)(10 + 22 * strength);
	for (int k = 0; k < n; k++)
		emit(s, a->lay, pu * A + us_randr(r, -0.004f, 0.004f), py - 0.002f, us_randr(r, 0.02f, 0.12f) * ds, -us_randr(r, 0.01f, 0.05f) * ds,
		     us_randr(r, 0.5f, 1.2f), us_randr(r, 0.6f, 1.0f));
	s->av_cool = 0.1f;
}

/* ---- snare: dust devils ------------------------------------------------------------------------- */

static void devils(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L, float A)
{
	for (int i = 0; i < MAX_DD; i++) {
		devil_t *d = &s->dd[i];
		if (!d->on)
			continue;
		d->age += ctx->dt;
		if (d->age > d->life) {
			d->on = 0;
			continue;
		}
		d->u += ctx->dt * 0.012f;
		d->ph += ctx->dt * 7.0f;
		const layer_t *l = &s->lay[d->lay];
		float env = us_smooth(0.0f, 0.5f, d->age) * (1.0f - us_smooth(d->life * 0.6f, d->life, d->age));
		float x0 = d->u * A, y0 = l->base + 0.012f * (d->lay - 2);
		float H = d->h * (0.4f + 0.6f * us_smooth(0.0f, 0.8f, d->age));
		float lb = LBRI[d->lay] * (0.5f + 0.5f * (L->Ia + L->Ik)) * env * d->amp;
		const short *occ = s->occ[d->lay];
		uint8_t m = L->warm > 0.45f ? US_MAT_LIGHT : US_MAT_EARTH;
		int NR = ctx->crisp ? 8 : 16;
		for (int k = 0; k < NR; k++) {
			float t = (k + 0.5f) / NR;
			float y = y0 - H * t;
			float rr = d->r * (0.15f + 0.85f * powf(t, 1.4f));
			float lean = 0.25f * H * t * t + 0.008f * us_noise2(t * 3.0f, (float)ctx->t * 0.9f + i);
			float cx = x0 + lean;
			/* a ring seen from the side: the front arc bright, the back faint */
			int NA = ctx->crisp ? 8 : 18;
			float ppx = 0, ppy = 0;
			for (int q = 0; q <= NA; q++) {
				float a = d->ph * (1.0f - 0.4f * t) + q * US_TAU / NA + k * 0.7f;
				float xx = cx + cosf(a) * rr, yy = y + sinf(a) * rr * 0.22f;
				float front = sinf(a) > 0 ? 1.0f : 0.35f;
				if (q > 0 && (q + k) % 3 != 0)
					oline(s, c, ppx, ppy, xx, yy, lb * front * (0.35f + 0.5f * (1.0f - t)), m, occ);
				ppx = xx;
				ppy = yy;
			}
		}
	}
}

/* ---- the sky ------------------------------------------------------------------------------------ */

static void sky(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L, float hat, float dens)
{
	float A = ctx->aspect;
	int crisp = ctx->crisp;
	us_world *w = ctx->w;
	float winter = us_world_season_w(w, 3), summer = us_world_season_w(w, 1);
	float night = L->night;
	float clear = 1.0f + 0.25f * winter - 0.2f * summer; /* crisp winter air, hazy summer */
	/* day: (pixel styles) a banded pale sky; ink keeps its black ground with a glow at the horizon */
	float dayk = us_smooth(0.25f, 0.7f, w->daylight);
	if (crisp && dayk > 0.0f) {
		int ph = (int)((HY + 0.02f) * c->s);
		for (int py = 0; py < ph && py < c->h; py++) {
			float y = (py + 0.5f) / ph;
			float v = (0.27f + 0.35f * y * y) * dayk;
			for (int px = 0; px < c->w; px++)
				us_put(c, px, py, qd(v, px, py), US_MAT_WATER);
		}
	}
	/* twilight + haze: a glowing band along the horizon, strongest toward the sun */
	{
		enum { TW = 48, TH = 20 };
		float f[TW * TH];
		float swk = 0.85f + 0.5f * s->swl;
		for (int j = 0; j < TH; j++) {
			float y = (j + 0.5f) / TH * (HY + 0.02f);
			float hd = (HY + 0.02f - y);
			for (int i = 0; i < TW; i++) {
				float u = (i + 0.5f) / TW;
				float near_sun = expf(-(u - L->sun_u) * (u - L->sun_u) / 0.06f);
				float glow = L->tw * (0.25f + 0.75f * near_sun) * expf(-hd / (0.09f + 0.08f * near_sun));
				float haze = (0.05f + 0.05f * summer) * (0.3f + 0.7f * w->daylight) * expf(-hd / 0.05f);
				float moon = L->moonk * 0.12f * expf(-((u - L->moon_u) * (u - L->moon_u) * A * A + (y - L->moon_y) * (y - L->moon_y)) / 0.004f);
				f[j * TW + i] = (glow * 0.55f + haze + moon) * swk;
			}
		}
		rast(c, f, TW, TH, crisp ? 1.2f : 1.0f, US_MAT_LIGHT, 0, 0, A, HY + 0.02f, crisp ? 1 : 0);
	}
	/* the Milky Way: diffuse glow + star dust, rising out of the dunes */
	if (night > 0.02f) {
		float mk = night * (0.55f + 0.2f * s->swl + 0.45f * s->lvl) * clear;
		rast(c, s->mw, MWW, MWH, (crisp ? 0.95f : 0.55f) * mk, US_MAT_ACCENT, 0, 0, A, HY, crisp ? 2 : 0);
		int nd = (int)(NDUST * (crisp ? 0.35f : 1.0f) * (0.55f + 0.45f * dens));
		for (int i = 0; i < nd; i++) {
			int px = (int)(s->du[i] * A * c->s), py = (int)(s->dy[i] * c->s);
			float v = s->dm[i] * mk * (crisp ? 0.75f : 0.95f);
			if (crisp)
				v = v > 0.3f ? STEP2 : STEP1 * (v > 0.12f);
			us_put(c, px, py, v, US_MAT_ACCENT);
		}
	}
	/* stars: twinkle; each hat makes a fresh handful flash */
	{
		float sv = us_sat(night * 1.2f + 0.05f) * clear * (1.0f - dayk);
		if (sv > 0.01f) {
			int ns = (int)(NSTAR * (0.5f + 0.5f * dens));
			for (int i = 0; i < ns; i++) {
				float tw = 0.75f + 0.25f * us_noise2(s->stw[i], (float)ctx->t * (0.5f + s->sm[i]));
				if (hat > 0.02f && hashf((uint32_t)i * 977u + s->hat_seed) < 0.12f)
					tw += 1.8f * hat;
				float v = s->sm[i] * sv * tw;
				float x = s->su[i] * A, y = s->sy[i];
				us_point(c, x, y, crisp ? 0.003f : (s->sm[i] > 0.75f ? 0.0016f : 0.001f), v, US_MAT_LIGHT);
				if (!crisp && s->sm[i] > 0.82f && v > 0.4f) {
					float gl = 0.004f + 0.004f * us_sat(v - 0.6f);
					oline(s, c, x - gl, y, x + gl, y, v * 0.3f, US_MAT_LIGHT, NULL);
					oline(s, c, x, y - gl, x, y + gl, v * 0.3f, US_MAT_LIGHT, NULL);
				}
			}
		}
	}
	/* a shooting star now and then, at night */
	s->met_wait -= ctx->dt;
	if (s->met_wait <= 0 && s->met_t <= 0 && night > 0.4f) {
		us_rng *r = &s->rng;
		s->met_t = 1.0f;
		s->met_x = us_randr(r, 0.15f, 0.85f) * A;
		s->met_y = us_randr(r, 0.04f, 0.22f);
		float ang = us_randr(r, 0.3f, 0.7f) * (us_rand(r) < 0.5f ? 1.0f : -1.0f);
		s->met_vx = sinf(ang) * 0.55f;
		s->met_vy = cosf(fabsf(ang)) * 0.22f;
		s->met_wait = us_randr(r, 15.0f, 45.0f);
	}
	if (s->met_t > 0) {
		s->met_t -= ctx->real_dt * 2.0f;
		s->met_x += s->met_vx * ctx->real_dt;
		s->met_y += s->met_vy * ctx->real_dt;
		oline(s, c, s->met_x, s->met_y, s->met_x - s->met_vx * 0.14f, s->met_y - s->met_vy * 0.14f, us_sat(s->met_t) * night * 0.8f, US_MAT_LIGHT,
		      NULL);
	}
	/* the sun: big and low at dusk, small and white by day */
	if (L->sun_y < HY + 0.08f && L->sun_e > -0.2f) {
		float x = L->sun_u * A, y = L->sun_y;
		float low = 1.0f - us_smooth(0.05f, 0.5f, L->sun_e);
		float R = 0.03f + 0.018f * low;
		float vk = us_smooth(-0.15f, 0.05f, L->sun_e);
		uint8_t m = low > 0.5f ? US_MAT_BLOOM : US_MAT_LIGHT;
		if (crisp) {
			us_disc(c, x, y, R * 2.1f, 0.14f * vk, US_MAT_LIGHT);
			us_disc(c, x, y, R * 1.4f, STEP2 * vk, US_MAT_LIGHT);
			us_disc(c, x, y, R, 0.9f * vk, m);
			/* a few bands across the low sun, the way heat haze slices it */
			if (low > 0.4f)
				for (int k = 1; k <= 3; k++)
					us_line(c, x - R, y + R * (0.15f + 0.22f * k), x + R, y + R * (0.15f + 0.22f * k), 0.0035f, 0.0f, US_MAT_BG);
		} else {
			us_disc(c, x, y, R, 0.2f * vk, m);
			us_ring(c, x, y, R, 0.0018f, 0.85f * vk, m);
			us_ring(c, x, y, R * 1.5f, 0.0012f, 0.2f * vk * (1.0f + 0.3f * s->swl), US_MAT_LIGHT);
			us_ring(c, x, y, R * 2.3f, 0.0012f, 0.09f * vk * (1.0f + 0.3f * s->swl), US_MAT_LIGHT);
			if (low > 0.3f) /* heat haze slices across the low disc */
				for (int k = 0; k < 4; k++) {
					float yy = y + R * (0.1f + 0.2f * k);
					float hw = sqrtf(us_sat(1.0f - ((yy - y) / R) * ((yy - y) / R))) * R;
					float sh = 0.004f * us_noise2(k * 3.1f, (float)ctx->t * 0.7f);
					oline(s, c, x - hw + sh, yy, x + hw + sh, yy, 0.45f * vk * low, m, NULL);
				}
		}
	}
	/* the moon */
	if (L->moon_e > -0.05f && L->moon_y < HY + 0.03f) {
		float x = L->moon_u * A, y = L->moon_y, R = 0.017f;
		float vk = us_smooth(-0.05f, 0.1f, L->moon_e) * (0.4f + 0.6f * night);
		if (crisp) {
			us_disc(c, x, y, R, 0.8f * vk, US_MAT_LIGHT);
		} else {
			us_disc(c, x, y, R, 0.25f * vk, US_MAT_LIGHT);
			us_ring(c, x, y, R, 0.0016f, 0.8f * vk, US_MAT_LIGHT);
			us_ring(c, x, y, R * 2.4f, 0.001f, 0.08f * vk, US_MAT_LIGHT);
		}
	}
}

/* ---- the near corners: dry grass tufts, and after rain, desert flowers ----------------------------- */

static void tufts(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L, float wind, float dens)
{
	float A = ctx->aspect, S = c->s;
	int W = c->w < MAXCOL ? c->w : MAXCOL;
	uint8_t lm = US_MAT_LIGHT; /* dry straw */
	float bv = 0.2f + 0.25f * (L->Ia + 0.5f * L->Ik);
	int nt = dens < 0.2f ? 2 : NTUFT;
	for (int i = 0; i < nt; i++) {
		float u = s->tu[i];
		int px = (int)(u * A * S);
		if (px < 0 || px >= W)
			continue;
		float x = u * A, y = s->sil[4][px] + 0.02f + 0.02f * (i & 1);
		if (y > 0.99f)
			y = 0.99f;
		int nb = ctx->crisp ? 4 : 11;
		for (int b = 0; b < nb; b++) {
			uint32_t h = (uint32_t)(i * 101 + b * 7);
			float a = (hashf(h) - 0.5f) * 1.1f; /* splay */
			float len = s->th[i] * (0.45f + 0.65f * hashf(h + 1));
			float ph = hashf(h + 2) * 6.0f;
			float bend = 0.08f + 0.28f * wind + 0.06f * us_noise2(ph, (float)ctx->t * (0.8f + 1.5f * wind));
			float x0 = x + (hashf(h + 3) - 0.5f) * (ctx->crisp ? 0.03f : 0.01f), y0 = y;
			float px_ = x0, py_ = y0;
			for (int q = 1; q <= 5; q++) {
				float t = q / 5.0f;
				float ang = a * 0.7f + bend * t * t;
				float xx = x0 + sinf(ang) * len * t, yy = y0 - cosf(ang) * len * t;
				oline(s, c, px_, py_, xx, yy, bv * (0.9f - 0.45f * t), lm, NULL);
				px_ = xx;
				py_ = yy;
			}
			/* a few carry a seed head */
			if (hashf(h + 5) < 0.3f)
				oline(s, c, px_, py_, px_ + sinf(a * 0.7f + bend) * 0.008f, py_ - cosf(a * 0.7f + bend) * 0.008f, bv * 0.7f, US_MAT_ACCENT, NULL);
			/* the wind scribes an arc in the sand where the tip touches */
		}
	}
	/* after rain the desert flowers: small blooms scattered on the floor between the near dunes */
	float wet = us_smooth(0.35f, 0.75f, ctx->w->moisture);
	if (wet > 0.02f)
		for (int i = 0; i < NFLOW; i++) {
			if (s->fl_f[i] > wet)
				continue;
			float u = s->fl_u[i];
			int px = (int)(u * A * S);
			if (px < 0 || px >= W)
				continue;
			float yb = s->lay[3].base + 0.01f + 0.06f * hashf((uint32_t)i * 13u);
			if (yb >= s->occ[3][px] / S)
				continue;
			us_point(c, u * A, yb, ctx->crisp ? 0.003f : 0.0018f, 0.7f * wet, US_MAT_BLOOM);
			oline(s, c, u * A, yb, u * A, yb + 0.006f, 0.35f * wet, US_MAT_LEAF, s->occ[3]);
		}
}

/* ---- blown sand: veils drifting low over the dunes, downwind (a slow low-res field) ---------------- */

static void veils(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L)
{
	enum { VW = 72, VH = 28 };
	float f[VW * VH];
	float A = ctx->aspect, y0 = HY - 0.03f;
	float amt = (0.04f + 0.08f * s->wind) * (0.5f + 0.5f * (L->Ia + L->Ik));
	for (int j = 0; j < VH; j++) {
		float y = y0 + (1.0f - y0) * (j + 0.5f) / VH;
		for (int i = 0; i < VW; i++) {
			float x = (i + 0.5f) / VW * A;
			float n = us_fbm3(x * 1.1f - s->drift, y * 22.0f + 1.2f * us_noise2(x * 0.9f - s->drift * 0.5f, 3.0f), s->T * 0.05f, 3);
			f[j * VW + i] = us_smooth(0.05f, 0.55f, n) * amt;
		}
	}
	if (!ctx->crisp) {
		rast(c, f, VW, VH, 1.0f, US_MAT_LIGHT, 0, y0, A, 1.0f, 0);
		return;
	}
	/* pixel styles: the veils dithered into the first shade step */
	int p0 = (int)(y0 * c->s);
	for (int py = p0; py < c->h; py++) {
		float fy = ((py + 0.5f) / c->s - y0) / (1.0f - y0) * VH - 0.5f;
		int iy = (int)floorf(fy);
		float ty = fy - iy;
		int i0 = iy < 0 ? 0 : iy, i1 = iy + 1 > VH - 1 ? VH - 1 : iy + 1;
		for (int px = 0; px < c->w; px++) {
			float fx = (px + 0.5f) / c->w * VW - 0.5f;
			int ix = (int)floorf(fx);
			float tx = fx - ix;
			int j0 = ix < 0 ? 0 : ix, j1 = ix + 1 > VW - 1 ? VW - 1 : ix + 1;
			float v = us_lerp(us_lerp(f[i0 * VW + j0], f[i0 * VW + j1], tx), us_lerp(f[i1 * VW + j0], f[i1 * VW + j1], tx), ty);
			size_t k = (size_t)py * c->w + px;
			/* lift the sand one shade where a veil passes (ordered dither) */
			if (v * 4.0f * 16.0f > BAYER[(py & 3) * 4 + (px & 3)] + 0.5f && c->ink[k] < 230) {
				int qi = (int)(c->ink[k] / 255.0f * 4.0f + 0.5f) + 1;
				c->ink[k] = (uint8_t)(QV[qi > 4 ? 4 : qi] * 255.0f);
			}
		}
	}
}

/* ---- life: shapes ------------------------------------------------------------------------------- */

/* an ellipse given by its centre and two (possibly skewed) axis vectors, with the inverse for inside tests */
typedef struct {
	float x, y, ax, ay, bx, by, i00, i01, i10, i11;
} ell_t;

static void ell_mk(ell_t *e, float x, float y, float ax, float ay, float bx, float by)
{
	float det = ax * by - bx * ay;
	if (fabsf(det) < 1e-10f)
		det = det < 0 ? -1e-10f : 1e-10f;
	e->x = x;
	e->y = y;
	e->ax = ax;
	e->ay = ay;
	e->bx = bx;
	e->by = by;
	e->i00 = by / det;
	e->i01 = -bx / det;
	e->i10 = -ay / det;
	e->i11 = ax / det;
}

static inline int ell_in(const ell_t *e, float x, float y, float g)
{
	float dx = x - e->x, dy = y - e->y;
	float u = e->i00 * dx + e->i01 * dy, v = e->i10 * dx + e->i11 * dy;
	return u * u + v * v < g;
}

/* a creature's local frame: x forward (units of its size), y up from its feet; the ground's slope shears it */
typedef struct {
	float X, Y, kx, ky, sl;
} xf_t;

static inline void xf_pt(const xf_t *t, float lx, float ly, float *x, float *y)
{
	*x = t->X + t->kx * lx;
	*y = t->Y - t->ky * ly + t->sl * t->kx * lx;
}

static void xf_ell(const xf_t *t, ell_t *e, float lx, float ly, float rx, float ry, float ang)
{
	float x, y;
	xf_pt(t, lx, ly, &x, &y);
	float ca = cosf(ang), sa = sinf(ang);
	float ux = ca * rx, uy = sa * rx, vx = -sa * ry, vy = ca * ry;
	ell_mk(e, x, y, t->kx * ux, -t->ky * uy + t->sl * t->kx * ux, t->kx * vx, -t->ky * vy + t->sl * t->kx * vx);
}

/* a limb-like ellipse from local p0 to p1, half-width w */
static void xf_cap(const xf_t *t, ell_t *e, float x0, float y0, float x1, float y1, float w)
{
	float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy) + 1e-6f;
	xf_ell(t, e, 0.5f * (x0 + x1), 0.5f * (y0 + y1), 0.5f * l + 0.3f * w, w, atan2f(dy, dx));
}

static void xf_line(const st_t *s, us_canvas *c, const xf_t *t, float x0, float y0, float x1, float y1, float v, uint8_t m, const short *occ)
{
	float ax, ay, bx, by;
	xf_pt(t, x0, y0, &ax, &ay);
	xf_pt(t, x1, y1, &bx, &by);
	oline(s, c, ax, ay, bx, by, v, m, occ);
}

/* A silhouette made of overlapping ellipses: filled solid (pixel: the body colour with a lit top rim; ink: a
 * near-black cut-out so stars and ripples do not show through), then (ink) outlined along the union's edge only. */
static void blob(const st_t *s, us_canvas *c, const ell_t *e, int n, float fill, float line, uint8_t m, const short *occ, int crisp)
{
	float S = c->s;
	float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
	for (int i = 0; i < n; i++) {
		float rx = fabsf(e[i].ax) + fabsf(e[i].bx), ry = fabsf(e[i].ay) + fabsf(e[i].by);
		x0 = fminf(x0, e[i].x - rx);
		x1 = fmaxf(x1, e[i].x + rx);
		y0 = fminf(y0, e[i].y - ry);
		y1 = fmaxf(y1, e[i].y + ry);
	}
	int px0 = (int)floorf(x0 * S), px1 = (int)ceilf(x1 * S), py0 = (int)floorf(y0 * S), py1 = (int)ceilf(y1 * S);
	if (px0 < 0) px0 = 0;
	if (py0 < 0) py0 = 0;
	if (px1 > c->w - 1) px1 = c->w - 1;
	if (py1 > c->h - 1) py1 = c->h - 1;
	if (px1 - px0 > 400 || py1 - py0 > 400)
		return;
	for (int py = py0; py <= py1; py++) {
		float y = (py + 0.5f) / S;
		for (int px = px0; px <= px1; px++) {
			if (occ && px < MAXCOL && py >= occ[px])
				continue;
			float x = (px + 0.5f) / S;
			int in = 0;
			for (int i = 0; i < n && !in; i++)
				in = ell_in(&e[i], x, y, 1.0f);
			if (!in)
				continue;
			float v = fill;
			if (crisp) { /* the top edge catches the light */
				int up = 0;
				for (int i = 0; i < n && !up; i++)
					up = ell_in(&e[i], x, y - 1.0f / S, 1.0f);
				if (!up)
					v = us_sat(fill * 1.35f + 0.05f);
			}
			size_t k = (size_t)py * c->w + px;
			c->ink[k] = (uint8_t)(us_sat(v) * 255.0f);
			c->mat[k] = m;
		}
	}
	if (crisp || line < 0.004f)
		return;
	for (int i = 0; i < n; i++) {
		const ell_t *q = &e[i];
		float per = (sqrtf(q->ax * q->ax + q->ay * q->ay) + sqrtf(q->bx * q->bx + q->by * q->by)) * US_PI * S;
		int ns = (int)(per / 3.0f);
		ns = ns < 10 ? 10 : ns > 48 ? 48 : ns;
		float pxp = q->x + q->ax, pyp = q->y + q->ay;
		for (int k = 1; k <= ns; k++) {
			float a = US_TAU * k / ns, ca = cosf(a), sa = sinf(a);
			float xx = q->x + ca * q->ax + sa * q->bx, yy = q->y + ca * q->ay + sa * q->by;
			float mx = 0.5f * (xx + pxp), my = 0.5f * (yy + pyp);
			int hid = 0;
			for (int j = 0; j < n && !hid; j++)
				if (j != i)
					hid = ell_in(&e[j], mx, my, 0.92f);
			if (!hid)
				oline(s, c, pxp, pyp, xx, yy, line, m, occ);
			pxp = xx;
			pyp = yy;
		}
	}
}

static float gnd(const st_t *s, int li, float x, const us_canvas *c)
{
	int W = c->w < MAXCOL ? c->w : MAXCOL;
	int px = (int)(x * c->s);
	px = px < 0 ? 0 : px >= W ? W - 1 : px;
	return s->sil[li][px];
}

/* the valley floor between the hero dunes: from the dunes' feet (top) to the near range's outline (bottom) */
static void valley(const st_t *s, float u, float A, const us_canvas *c, float *top, float *bot)
{
	const layer_t *l = &s->lay[3];
	float y = base_y(l, 3, u);
	for (int hi = 0; hi < l->n; hi++) {
		const hump_t *h = &l->h[hi];
		if (u > h->cx - h->wl && u < h->cx + h->wr) {
			float t = hump_toe(h, l->base, u);
			if (t > y)
				y = t;
		}
	}
	*top = y + 0.004f;
	*bot = gnd(s, 4, u * A, c) - 0.004f;
}

/* ---- life: create ------------------------------------------------------------------------------ */

static void life_create(st_t *s)
{
	us_rng *r = &s->rng;
	s->fx.u = us_randr(r, 0.12f, 0.4f);
	s->fx.d = s->fx.dtg = us_rand(r) < 0.5f ? -1.0f : 1.0f;
	s->fx.tst = us_randr(r, 2.0f, 5.0f);
	for (int i = 0; i < NJB; i++) {
		jerb_t *j = &s->jb[i];
		j->u = 0.52f + 0.05f * i + us_randr(r, -0.01f, 0.01f);
		j->f = us_randr(r, 0.25f, 0.75f);
		j->d = us_rand(r) < 0.5f ? -1.0f : 1.0f;
		j->hp = -1.0f;
		j->wait = us_randr(r, 0.5f, 3.0f);
		j->groom = us_rand(r) * 10.0f;
	}
	s->bt_u = us_randr(r, 0.66f, 0.86f);
	s->bt_y = 0.96f;
	s->bt_a = us_rand(r) < 0.5f ? 0.0f : US_PI;
	for (int i = 0; i < NTK; i++)
		s->tk_age[i] = 99.0f;
	s->oasis_u = us_randr(r, 0.43f, 0.47f);
	for (int i = 0; i < NPALM; i++) {
		s->pm_du[i] = (i - 2) * 0.012f + us_randr(r, -0.004f, 0.004f);
		s->pm_h[i] = us_randr(r, 0.017f, 0.03f) * (i == 2 ? 1.15f : 1.0f);
		s->pm_lean[i] = us_randr(r, -0.25f, 0.25f) + (i - 2) * 0.06f;
	}
	for (int i = 0; i < NSUC; i++) {
		s->su_u[i] = i < 3 ? us_randr(r, 0.47f, 0.62f) : us_randr(r, 0.24f, 0.5f);
		s->su_f[i] = us_randr(r, 0.3f, 0.9f);
		s->su_sz[i] = us_randr(r, 0.7f, 1.2f);
	}
	for (int i = 0; i < NLILY; i++) {
		s->li_u[i] = i < 3 ? us_randr(r, 0.47f, 0.63f) : us_randr(r, 0.26f, 0.5f);
		s->li_f[i] = us_randr(r, 0.35f, 0.95f);
		s->li_ph[i] = us_rand(r) * 20.0f;
	}
	s->ow.look_t = 1.0f;
}

/* eyeshine: a hat makes an animal's eye flash (a tiny four-ray sparkle; a plus of pixels in pixel styles) */
static void eyeshine(const st_t *s, us_canvas *c, float x, float y, float e, int crisp, const short *occ)
{
	if (e < 0.15f)
		return;
	float S = c->s;
	if (crisp) {
		int px = (int)(x * S), py = (int)(y * S);
		if (e > 0.4f)
			for (int k = -1; k <= 1; k += 2) {
				oput(s, c, px + k, py, 0.62f * e, US_MAT_LIGHT, occ);
				oput(s, c, px, py + k, 0.62f * e, US_MAT_LIGHT, occ);
			}
		return;
	}
	float l = 0.002f + 0.006f * e;
	oline(s, c, x - l, y, x + l, y, 0.6f * e, US_MAT_LIGHT, occ);
	oline(s, c, x, y - l, x, y + l, 0.6f * e, US_MAT_LIGHT, occ);
}

/* ---- life: the fennec fox, trotting the near range's outline ------------------------------------ */

#define FOX_U0 0.04f
#define FOX_U1 0.56f

static void fox_update(st_t *s, const us_ctx *ctx, float snare, int snap, float lvl)
{
	fox_t *f = &s->fx;
	us_rng *r = &s->rng;
	float dt = ctx->dt, rdt = ctx->real_dt, A = ctx->aspect;
	/* snare: it freezes mid-step, head up, ears pricked toward the sound */
	if (snap && snare > 0.15f) {
		f->hold = 0.6f + 0.9f * snare;
		if (f->st == 0 && us_rand(r) < 0.15f)
			f->tst = 0.0f; /* now and then it decides to stop and look */
	}
	f->hold -= rdt;
	f->alert = us_approach(f->alert, f->hold > 0 ? 1.0f : 0.0f, f->hold > 0 ? 0.03f : 0.3f, rdt);
	f->tst -= dt * (1.0f - 0.8f * f->alert);
	if (f->tst <= 0) {
		if (f->st == 0) {
			f->st = 1;
			f->tst = us_randr(r, 5.0f, 11.0f) * (1.5f - 0.6f * lvl);
		} else {
			f->st = 0;
			f->tst = us_randr(r, 2.0f, 4.5f) * (0.7f + 0.6f * lvl);
			if (us_rand(r) < 0.3f)
				f->dtg = -f->dtg;
		}
	}
	if ((f->u < FOX_U0 && f->dtg < 0) || (f->u > FOX_U1 && f->dtg > 0)) {
		f->dtg = -f->dtg;
		if (f->st == 0) { /* a pause at the end of its beat, then back */
			f->st = 1;
			f->tst = us_randr(r, 1.0f, 2.5f);
		}
	}
	/* turning round: a quick swing through facing the viewer */
	f->d = us_approach(f->d, f->dtg, 0.09f, dt);
	int turning = fabsf(f->d - f->dtg) > 0.25f;
	float hush = us_smooth(0.15f, 0.5f, ctx->a->active); /* when the music stops it stops and listens */
	float tsp = (f->st == 0 && !turning) ? (0.75f + 0.5f * lvl) * (1.0f - f->alert) * hush : 0.0f;
	f->spd = us_approach(f->spd, tsp, f->alert > 0.3f ? 0.04f : 0.3f, dt);
	/* the stride's amplitude follows the speed, but a freeze holds the legs where they are */
	if (f->alert < 0.3f)
		f->amp = us_approach(f->amp, us_sat(f->spd * 1.4f), 0.15f, dt);
	float k = ctx->crisp ? 0.068f : 0.046f;
	float v = f->spd * 0.055f; /* normalized units per second */
	f->u += f->dtg * v * dt / A;
	f->ph += v * dt / (0.85f * k) * US_TAU;
	f->sniff = us_approach(f->sniff, (f->st == 1 && f->alert < 0.3f && us_noise2(f->u * 30.0f, (float)ctx->t * 0.4f) > -0.1f) ? 1.0f : 0.0f,
			       0.35f, dt);
	f->eye = fmaxf(f->eye - rdt * 4.0f, s->hat_e);
}

static void fox_draw(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L)
{
	fox_t *f = &s->fx;
	float A = ctx->aspect, S = c->s;
	int crisp = ctx->crisp;
	float k = crisp ? 0.068f : 0.046f;
	float X = f->u * A;
	float dsg = f->d >= 0 ? 1.0f : -1.0f, dm = fmaxf(fabsf(f->d), 0.18f);
	/* the ground under its hind and fore feet sets its pitch */
	float gb = gnd(s, 4, X - dsg * 0.3f * k * dm, c), gf = gnd(s, 4, X + dsg * 0.3f * k * dm, c);
	float sl = (gf - gb) / (dsg * 0.6f * k * dm);
	sl = us_clampf(sl, -0.7f, 0.7f);
	float bob = 0.025f * f->amp * fabsf(sinf(f->ph));
	xf_t t = {X, 0.5f * (gb + gf) + 0.6f / S, dsg * dm * k, k, sl};
	t.Y -= bob * k;
	float bv = (0.3f + 0.22f * (L->Ia + 0.4f * L->Ik)) * (0.65f + 0.6f * s->lvl); /* louder, more light on them */
	float fill = crisp ? 0.38f + 0.3f * bv : 0.012f;
	float line = 0.48f + 0.35f * bv;
	uint8_t m = US_MAT_ACCENT;
	/* legs: a trot (diagonal pairs together); the far pair dimmer, drawn behind the body */
	static const float hipx[4] = {-0.26f, 0.28f, -0.2f, 0.34f}; /* far hind, far fore, near hind, near fore */
	static const float lph[4] = {US_PI, 0.0f, 0.0f, US_PI};
	ell_t e[10];
	for (int pass = 0; pass < 2; pass++) {
		if (pass == 1) {
			/* the body: a slim trunk, deep chest, big-eared head, a full tail held low */
			float hang = -0.55f * f->sniff + 0.25f * f->alert + 0.04f * sinf(f->ph * 2.0f) * f->amp;
			float ear = 1.0f + 0.14f * f->alert;
			float tl = -0.15f + 0.22f * f->amp + 0.35f * f->alert - 0.1f * f->sniff;
			float ch = cosf(hang), sh = sinf(hang), ct = cosf(tl), st = sinf(tl);
#define FHX(x, y) (0.36f + ((x)-0.36f) * ch - ((y)-0.56f) * sh)
#define FHY(x, y) (0.56f + ((x)-0.36f) * sh + ((y)-0.56f) * ch)
#define FTX(x, y) (-0.36f + ((x) + 0.36f) * ct - ((y)-0.5f) * st)
#define FTY(x, y) (0.5f + ((x) + 0.36f) * st + ((y)-0.5f) * ct)
			int n = 0;
			float bk = crisp ? 1.25f : 1.0f; /* pixel: a sturdier build so it holds together at a few pixels */
			xf_ell(&t, &e[n++], -0.03f, 0.47f, 0.35f, 0.135f * bk, 0.0f);
			xf_ell(&t, &e[n++], 0.25f, 0.48f, 0.15f, 0.155f * bk, 0.0f);
			xf_cap(&t, &e[n++], 0.3f, 0.52f, FHX(0.42f, 0.66f), FHY(0.42f, 0.66f), 0.09f);
			xf_ell(&t, &e[n++], FHX(0.48f, 0.69f), FHY(0.48f, 0.69f), 0.12f, 0.1f, hang);
			xf_ell(&t, &e[n++], FHX(0.61f, 0.645f), FHY(0.61f, 0.645f), 0.1f, 0.042f, hang - 0.3f);
			xf_cap(&t, &e[n++], FHX(0.42f, 0.75f), FHY(0.42f, 0.75f), FHX(0.35f, 0.75f + 0.33f * ear), FHY(0.35f, 0.75f + 0.33f * ear), 0.06f);
			xf_cap(&t, &e[n++], FHX(0.5f, 0.76f), FHY(0.5f, 0.76f), FHX(0.54f + 0.04f * f->alert, 0.76f + 0.36f * ear),
			       FHY(0.54f + 0.04f * f->alert, 0.76f + 0.36f * ear), 0.065f);
			xf_cap(&t, &e[n++], FTX(-0.34f, 0.5f), FTY(-0.34f, 0.5f), FTX(-0.85f, 0.3f), FTY(-0.85f, 0.3f), 0.085f * bk);
			blob(s, c, e, n, fill, line, m, NULL, crisp);
			/* the eye (eyeshine at night, flashing on the hats) */
			float ex, ey;
			xf_pt(&t, FHX(0.53f, 0.71f), FHY(0.53f, 0.71f), &ex, &ey);
			float ev = (crisp ? 0.25f : 0.35f) + 0.25f * L->night + 0.6f * f->eye * (0.4f + 0.6f * L->night);
			oput(s, c, (int)(ex * S), (int)(ey * S), us_sat(ev), US_MAT_LIGHT, NULL);
			eyeshine(s, c, ex, ey, f->eye, crisp, NULL);
#undef FHX
#undef FHY
#undef FTX
#undef FTY
		}
		for (int l = 0; l < 4; l++) {
			if ((l < 2) != (pass == 0))
				continue;
			float sw = sinf(f->ph + lph[l]), lift = us_sat(cosf(f->ph + lph[l])) * f->amp;
			float hx = hipx[l], foot = hx + 0.17f * sw * f->amp, knee = hx + 0.05f * sw * f->amp + (l & 1 ? 0.04f : -0.05f);
			float lv = l < 2 ? line * 0.7f : line;
			if (crisp)
				lv = l < 2 ? fill * 0.8f : fill;
			xf_line(s, c, &t, hx, 0.4f, knee, 0.2f + 0.08f * lift, lv, m, NULL);
			xf_line(s, c, &t, knee, 0.2f + 0.08f * lift, foot, 0.12f * lift, lv, m, NULL);
		}
	}
}

/* ---- life: jerboas in the valley; each kick sends them hopping ----------------------------------- */

static void jerb_hop(st_t *s, jerb_t *j, float strength)
{
	us_rng *r = &s->rng;
	if (us_rand(r) < 0.3f)
		j->d = -j->d; /* they zigzag */
	/* but never into a neighbour */
	for (int q = 0; q < NJB; q++) {
		const jerb_t *o = &s->jb[q];
		if (o != j && fabsf(o->f - j->f) < 0.4f && fabsf(o->u - j->u) < 0.07f)
			j->d = o->u < j->u ? 1.0f : -1.0f;
	}
	j->hp = 0.0f;
	j->hdur = us_randr(r, 0.13f, 0.18f);
	j->hlen = us_randr(r, 0.012f, 0.03f) * (0.6f + 0.7f * strength);
	j->hh = us_randr(r, 0.012f, 0.022f) * (0.6f + 0.8f * strength);
	j->hdf = us_randr(r, -0.12f, 0.12f);
}

static void jerb_update(st_t *s, const us_ctx *ctx, us_canvas *c, int kicked, float kick, int snap, float snare, float lvl)
{
	us_rng *r = &s->rng;
	float A = ctx->aspect, dt = ctx->dt, rdt = ctx->real_dt;
	for (int i = 0; i < NJB; i++) {
		jerb_t *j = &s->jb[i];
		if (j->hp >= 0) {
			float p0 = j->hp;
			j->hp += rdt / j->hdur;
			float p1 = j->hp > 1.0f ? 1.0f : j->hp;
			j->u += j->d * j->hlen * (p1 - p0) / A;
			j->f = us_clampf(j->f + j->hdf * (p1 - p0), 0.15f, 0.85f);
			if (j->hp >= 1.0f) {
				j->hp = -1.0f;
				j->ear = 0.0f;
				j->wait = us_randr(r, 3.0f, 8.0f) * (1.3f - 0.6f * lvl);
			}
		} else {
			j->wait -= dt * us_smooth(0.15f, 0.5f, ctx->a->active); /* the quiet keeps them still */
			j->ear += rdt; /* (time since it landed) */
			if (kicked && j->ear > 2.4f && j->al < 0.3f && us_rand(r) < 0.6f)
				jerb_hop(s, j, kick);
			else if (j->wait <= 0)
				jerb_hop(s, j, 0.4f);
		}
		/* stay on the valley floor: turn back where it pinches out */
		float tp, bt;
		valley(s, j->u, A, c, &tp, &bt);
		if (j->u < 0.51f || (bt - tp < 0.03f && j->u < 0.56f))
			j->d = 1.0f;
		if (j->u > 0.635f)
			j->d = -1.0f;
		j->groom += dt * 0.6f * us_sat(us_noise2(i * 7.0f, (float)ctx->t * 0.2f) * 3.0f);
		/* a snare: they sit bolt upright, ears high, and hold it a moment */
		if (snap && snare > 0.15f && j->hp < 0)
			j->al = 1.0f + 0.6f * snare;
		j->al = fmaxf(j->al - rdt * 1.6f, 0.0f);
	}
}

static void jerb_draw(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L)
{
	float A = ctx->aspect, S = c->s;
	int crisp = ctx->crisp;
	float bv = (0.3f + 0.22f * (L->Ia + 0.4f * L->Ik)) * (0.65f + 0.6f * s->lvl); /* louder, more light on them */
	float fill = crisp ? 0.36f + 0.3f * bv : 0.012f, line = 0.42f + 0.3f * bv;
	uint8_t m = US_MAT_ACCENT;
	const short *occ = s->occ[3];
	for (int i = 0; i < NJB; i++) {
		jerb_t *j = &s->jb[i];
		float tp, bt;
		valley(s, j->u, A, c, &tp, &bt);
		if (bt - tp < 0.012f)
			continue;
		float gy = us_lerp(tp, bt, j->f);
		/* nearer the viewer a touch larger */
		float k = (crisp ? 0.05f : 0.028f) * (0.85f + 0.3f * j->f);
		float p = j->hp, st = p >= 0 ? sinf(US_PI * us_sat(p)) : 0.0f;
		float air = p >= 0 ? 4.0f * p * (1.0f - p) * j->hh : 0.0f;
		/* idle: sitting up on its long feet, a little grooming bob */
		float gr = p < 0 ? us_smooth(0.6f, 1.0f, sinf(j->groom * 2.3f)) : 0.0f;
		xf_t t = {j->u * A, gy - air + 0.5f / S, j->d * k, k, 0.0f};
		float up = us_smooth(0.0f, 0.3f, j->al); /* startled: up on its haunches */
		gr *= 1.0f - up;
		float tilt = (0.55f + 0.5f * up) * (1.0f - st) - 0.15f * st;
		ell_t e[8];
		int n = 0;
		xf_ell(&t, &e[n++], -0.04f - 0.05f * up, 0.36f + 0.04f * st + 0.1f * up, 0.27f, 0.2f, tilt);
		xf_ell(&t, &e[n++], -0.14f + 0.04f * st, 0.24f + 0.08f * st, 0.17f, 0.15f, 0.0f);
		float hx = 0.2f + 0.12f * st - 0.08f * up, hy = 0.6f - 0.12f * st - 0.06f * gr + 0.24f * up;
		xf_ell(&t, &e[n++], hx, hy, 0.14f, 0.12f, 0.0f);
		xf_ell(&t, &e[n++], hx + 0.12f, hy - 0.03f, 0.07f, 0.05f, -0.2f);
		float ea = 1.9f + 0.5f * st + 0.2f * gr - 0.25f * up; /* ears swept back in flight, pricked when startled */
		xf_cap(&t, &e[n++], hx - 0.02f, hy + 0.08f, hx - 0.02f + 0.36f * cosf(ea), hy + 0.08f + 0.36f * sinf(ea), 0.055f);
		blob(s, c, e, n, fill, line, m, occ, crisp);
		/* the long hind feet: flat on the sand when sitting, trailing behind in a hop */
		float fx0 = -0.18f + 0.0f * st, fy0 = 0.05f + 0.15f * st;
		float fx1 = us_lerp(0.2f, -0.55f, st), fy1 = us_lerp(0.0f, 0.06f, st);
		xf_line(s, c, &t, -0.14f, 0.2f, fx0, fy0, crisp ? fill : line, m, occ);
		xf_line(s, c, &t, fx0, fy0, fx1, fy1, crisp ? fill : line, m, occ);
		xf_line(s, c, &t, 0.12f, 0.34f, 0.17f + 0.05f * st, 0.22f, crisp ? fill * 0.8f : line * 0.7f, m, occ);
		/* the tail: as long as two bodies, a rudder in flight, ending in a black-and-white tuft */
		float px = 0, py = 0;
		for (int q = 0; q <= 8; q++) {
			float tt = q / 8.0f;
			float lx = -0.26f - 1.3f * tt;
			float ly = 0.3f - (0.26f - 0.2f * st) * tt + 0.1f * sinf(US_PI * tt) * (1.0f - st) + 0.03f * st * sinf(tt * 6.0f + p * 9.0f);
			float x, y;
			xf_pt(&t, lx, ly, &x, &y);
			if (q > 0)
				oline(s, c, px, py, x, y, crisp ? fill * 0.9f : line * 0.85f, m, occ);
			px = x;
			py = y;
		}
		ell_t tuft;
		xf_ell(&t, &tuft, -1.62f, 0.07f + 0.17f * st, 0.11f, 0.05f, 0.0f);
		blob(s, c, &tuft, 1, crisp ? us_sat(fill * 1.3f) : 0.01f, line, m, occ, crisp);
		float ex, ey;
		xf_pt(&t, hx + 0.05f, hy + 0.02f, &ex, &ey);
		oput(s, c, (int)(ex * S), (int)(ey * S), us_sat(0.3f + 0.3f * L->night + 0.5f * s->hat_e), US_MAT_LIGHT, occ);
		eyeshine(s, c, ex, ey, s->hat_e * 0.8f, crisp, occ);
	}
}

/* ---- life: a darkling beetle trundling over the near sand, leaving a track ------------------------ */

static void beetle(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L, float lvl)
{
	us_rng *r = &s->rng;
	float A = ctx->aspect, S = c->s, dt = ctx->dt;
	int crisp = ctx->crisp;
	/* wander: a slow meander, pausing now and then; louder music, a brisker walk */
	if (s->bt_pause > 0)
		s->bt_pause -= dt;
	else if (us_rand(r) < dt * 0.12f)
		s->bt_pause = us_randr(r, 0.8f, 2.5f);
	float go = s->bt_pause > 0 ? 0.0f : 0.3f + 0.7f * us_smooth(0.15f, 0.5f, ctx->a->active);
	s->bt_spd = us_approach(s->bt_spd, go * (0.006f + 0.01f * lvl), 0.3f, dt);
	float lo = gnd(s, 4, s->bt_u * A, c) + 0.03f, hi = 0.985f;
	s->bt_a += dt * 1.6f * us_noise2(s->bt_u * 20.0f, (float)ctx->t * 0.25f);
	/* steer back toward the middle of its patch of sand */
	float wx = cosf(s->bt_a), wy = sinf(s->bt_a);
	float tx = (s->bt_u < 0.62f ? 1.0f : s->bt_u > 0.95f ? -1.0f : wx), ty = (s->bt_y < lo + 0.005f ? 1.0f : s->bt_y > hi - 0.005f ? -1.0f : 0.0f);
	if (tx != wx || ty != 0.0f) {
		float want = atan2f(ty != 0.0f ? ty : wy * 0.3f, tx);
		float da = want - s->bt_a;
		da -= US_TAU * floorf((da + US_PI) / US_TAU);
		s->bt_a += da * us_sat(dt * 2.0f);
	}
	float step = s->bt_spd * dt;
	s->bt_u += cosf(s->bt_a) * step / A;
	s->bt_y += sinf(s->bt_a) * step * 0.6f; /* the sand slopes away: depth moves are foreshortened */
	s->bt_y = us_clampf(s->bt_y, lo - 0.01f, hi);
	/* footprints: two rows of tiny pits, laid as it goes */
	s->bt_step += step;
	if (s->bt_step > 0.0022f) {
		s->bt_step = 0;
		float nx = -sinf(s->bt_a) * 0.0016f, ny = cosf(s->bt_a) * 0.0016f * 0.6f;
		float sd = s->tk_side ? 1.0f : -1.0f;
		s->tk_side ^= 1;
		s->tk_u[s->tk_i] = s->bt_u + sd * nx / A;
		s->tk_y[s->tk_i] = s->bt_y + sd * ny;
		s->tk_age[s->tk_i] = 0.0f;
		s->tk_i = (s->tk_i + 1) % NTK;
	}
	/* the wind slowly fills the track in */
	float fade = dt * (0.012f + 0.03f * s->wind);
	float tv = (crisp ? 1.0f : 0.16f + 0.1f * (L->Ia + L->Ik));
	for (int i = 0; i < NTK; i++) {
		if (s->tk_age[i] >= 1.0f)
			continue;
		s->tk_age[i] += fade;
		float a = 1.0f - s->tk_age[i];
		int px = (int)(s->tk_u[i] * A * S), py = (int)(s->tk_y[i] * S);
		if ((unsigned)px >= (unsigned)c->w || (unsigned)py >= (unsigned)c->h)
			continue;
		size_t k = (size_t)py * c->w + px;
		if (crisp) { /* each pit a shade darker than the sand */
			if (a > 0.35f && c->ink[k] > (uint8_t)(STEP1 * 255.0f))
				c->ink[k] = (uint8_t)(c->ink[k] * 0.62f);
		} else
			us_put(c, px, py, tv * a, L->warm > 0.45f ? US_MAT_LIGHT : US_MAT_EARTH);
	}
	/* the beetle: a domed black oval on six fine legs, its shell catching the hats */
	float k = crisp ? 0.007f : 0.0055f;
	float x = s->bt_u * A, y = s->bt_y;
	float ca = cosf(s->bt_a), sa = sinf(s->bt_a) * 0.6f;
	float bv = 0.32f + 0.2f * (L->Ia + 0.4f * L->Ik);
	s->bt_glint = fmaxf(s->bt_glint - ctx->real_dt * 5.0f, s->hat_e);
	ell_t e[2];
	ell_mk(&e[0], x, y - k * 0.5f, ca * k, sa * k, -sa * k * 0.7f, ca * k * 0.7f - k * 0.0f);
	ell_mk(&e[1], x + ca * k * 1.15f, y - k * 0.5f + sa * k * 1.15f, ca * k * 0.35f, sa * k * 0.35f, -sa * k * 0.4f, ca * k * 0.4f);
	if (!crisp) {
		float wob = 0.6f * sinf((float)ctx->t * 18.0f) * us_sat(s->bt_spd * 150.0f);
		for (int l = -1; l <= 1; l++)
			for (int sd = -1; sd <= 1; sd += 2) {
				float ax = x + ca * k * 0.55f * l, ay = y - k * 0.3f + sa * k * 0.55f * l;
				float ll = k * (1.25f + 0.2f * wob * l * sd);
				oline(s, c, ax, ay, ax - sa * ll * sd + ca * k * 0.3f * l, ay + ca * ll * sd * 0.5f + k * 0.35f, 0.35f + 0.3f * bv, US_MAT_ACCENT,
				      NULL);
			}
	}
	blob(s, c, e, 2, crisp ? 0.12f : 0.012f, 0.4f + 0.35f * bv, US_MAT_ACCENT, NULL, crisp);
	if (s->bt_glint > 0.05f)
		oput(s, c, (int)((x - ca * k * 0.2f) * S), (int)((y - k * 1.1f) * S), us_sat(0.4f + 0.6f * s->bt_glint), US_MAT_LIGHT, NULL);
}

/* ---- life: an eagle-owl on the acacia by night: it watches, turns to the snare, and now and then
 *      stoops at a jerboa in the valley and sweeps back up to its perch ---------------------------- */

static void owl_perch(const st_t *s, const us_ctx *ctx, float *x, float *y)
{
	float A = ctx->aspect, x0, y0;
	acacia_ground(s, &x0, &y0, A);
	float h = s->ac_h, cw = 0.62f * h, th = 0.085f * h;
	float ccx = x0 + 0.05f * h + s->sway * h, cy = y0 - 0.86f * h;
	float a = 0.42f;
	*x = ccx + a * cw;
	*y = cy - th * crown_top(a, 0) + 0.0015f;
}

static float bez(float a, float b, float c, float t) { return (1 - t) * (1 - t) * a + 2 * (1 - t) * t * b + t * t * c; }

static void owl_fly(owl_t *o, int st, float dur, float x0, float y0, float tx, float ty, float x2, float y2)
{
	o->st = st;
	o->t = 0;
	o->dur = dur;
	o->x0 = x0;
	o->y0 = y0;
	/* the control point that makes the curve pass through (tx, ty) at its middle */
	o->tx = 2.0f * tx - 0.5f * (x0 + x2);
	o->ty = 2.0f * ty - 0.5f * (y0 + y2);
	o->x2 = x2;
	o->y2 = y2;
}

static void owl(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L, float snare, int snap, int draw_perched)
{
	owl_t *o = &s->ow;
	us_rng *r = &s->rng;
	float A = ctx->aspect, S = c->s, dt = ctx->dt, rdt = ctx->real_dt;
	int crisp = ctx->crisp;
	float px, py;
	owl_perch(s, ctx, &px, &py);
	float night = L->night;
	if (draw_perched) { /* (the simulation steps once, with the perched pass) */
		if (!s->life_init)
			o->st = night > 0.3f ? 2 : 0;
		o->cool -= dt;
		if (o->st == 0 && night > 0.35f && o->cool <= 0) /* dusk: it comes in to its perch */
			owl_fly(o, 1, 4.0f, A + 0.08f, 0.3f, us_lerp(px, A, 0.5f), py - 0.08f, px, py);
		else if (o->st == 2 && night < 0.12f) /* dawn: off to roost */
			owl_fly(o, 4, 4.0f, px, py, us_lerp(px, A, 0.5f), py - 0.1f, A + 0.08f, 0.25f);
		else if (o->st == 2 && snap && snare > 0.55f && o->cool <= 0 && us_rand(r) < 0.35f) {
			/* a hard snare: it stoops at a jerboa and sweeps back up (the jerboas scatter) */
			jerb_t *j = &s->jb[(int)(us_rand(r) * NJB) % NJB];
			float tp, bt;
			valley(s, j->u, A, c, &tp, &bt);
			owl_fly(o, 3, 3.2f, px, py, j->u * A, us_lerp(tp, bt, j->f) - 0.02f, px, py);
			o->cool = 14.0f;
		}
		if (o->st == 1 || o->st == 3 || o->st == 4) {
			o->t += dt / o->dur;
			if (o->st == 3 && o->t > 0.42f && o->t - dt / o->dur <= 0.42f)
				for (int i = 0; i < NJB; i++)
					if (s->jb[i].hp < 0)
						jerb_hop(s, &s->jb[i], 1.0f);
			if (o->t >= 1.0f)
				o->st = o->st == 4 ? 0 : 2;
		}
		/* watching: the head swings round, holds, swings back; the snare snaps it round, feathers flare */
		o->look_t -= dt;
		if (o->look_t <= 0) {
			o->look_tg = us_rand(r) < 0.45f ? 0.0f : us_randr(r, -1.0f, 1.0f);
			o->look_t = us_randr(r, 3.0f, 8.0f);
		}
		if (snap && snare > 0.2f) {
			o->look_tg = us_rand(r) < 0.5f ? -1.0f : 1.0f;
			o->look_t = us_randr(r, 0.8f, 1.6f);
			o->flare = fmaxf(o->flare, 0.5f + 0.5f * snare);
		}
		o->look = us_approach(o->look, o->look_tg, 0.06f, rdt);
		o->flare = us_approach(o->flare, 0.0f, 0.25f, rdt);
		o->blink -= dt;
		if (o->blink < -us_randr(r, 4.0f, 12.0f))
			o->blink = 0.12f;
		o->eye = fmaxf(o->eye - rdt * 4.0f, s->hat_e);
	}
	float bv = (0.3f + 0.22f * (L->Ia + 0.4f * L->Ik)) * (0.65f + 0.6f * s->lvl); /* louder, more light on them */
	float fill = crisp ? 0.36f + 0.3f * bv : 0.012f, line = 0.45f + 0.3f * bv;
	uint8_t m = US_MAT_ACCENT;
	if (draw_perched && o->st == 2) {
		float k = crisp ? 0.058f : 0.026f;
		xf_t t = {px, py, k, k, 0.0f};
		float fl = 1.0f + 0.3f * o->flare; /* a snare: feathers puffed out */
		float lk = o->look;
		ell_t e[6];
		int n = 0;
		float wk = crisp ? 1.2f : 1.0f;
		xf_ell(&t, &e[n++], 0.0f, 0.42f, 0.25f * fl * wk, 0.36f, 0.0f);
		xf_ell(&t, &e[n++], 0.03f * lk, 0.86f, 0.23f * fl * wk, 0.19f, 0.0f);
		for (int sd = -1; sd <= 1; sd += 2) {
			float bx = sd * 0.13f + 0.05f * lk;
			xf_cap(&t, &e[n++], bx, 0.95f, bx + sd * 0.06f + 0.03f * lk, 1.13f + 0.04f * o->flare, 0.045f);
		}
		xf_ell(&t, &e[n++], 0.0f, 0.06f, 0.1f, 0.1f, 0.0f);
		blob(s, c, e, n, fill, line, m, NULL, crisp);
		if (crisp) /* the ear tufts, a pixel each */
			for (int sd = -1; sd <= 1; sd += 2)
				xf_line(s, c, &t, sd * 0.16f + 0.05f * lk, 1.0f, sd * 0.24f + 0.05f * lk, 1.16f + 0.05f * o->flare, us_sat(fill * 1.2f), m, NULL);
		/* the eyes: two coals by night, one when it looks aside; the hats make them flash */
		if (o->blink <= 0)
			for (int sd = -1; sd <= 1; sd += 2) {
				if (lk * sd < -0.55f)
					continue;
				float ex = px + (sd * (crisp ? 0.11f : 0.085f) + 0.11f * lk) * k, ey = py - 0.88f * k;
				float ev = crisp ? 0.62f : 0.35f + 0.35f * night + 0.5f * o->eye;
				if (crisp)
					oput(s, c, (int)(ex * S), (int)(ey * S), us_sat(ev), US_MAT_LIGHT, NULL);
				else
					us_point(c, ex, ey, 0.0011f, us_sat(ev), US_MAT_LIGHT);
				eyeshine(s, c, ex, ey, o->eye * (0.4f + 0.6f * night), crisp, NULL);
			}
		return;
	}
	if (draw_perched || (o->st != 1 && o->st != 3 && o->st != 4))
		return;
	/* in flight: broad rounded wings, a short tail, a big round head */
	float tt = us_sat(o->t);
	float ex = o->st == 4 ? o->x2 : px, ey = o->st == 4 ? o->y2 : py;
	float x0 = o->st == 1 ? o->x0 : px, y0 = o->st == 1 ? o->y0 : py;
	float x = bez(x0, o->tx, ex, tt), y = bez(y0, o->ty, ey, tt);
	float vx = 2 * (1 - tt) * (o->tx - x0) + 2 * tt * (ex - o->tx), vy = 2 * (1 - tt) * (o->ty - y0) + 2 * tt * (ey - o->ty);
	if (fabsf(vx) > 1e-4f)
		o->vx = vx;
	float d = o->vx >= 0 ? 1.0f : -1.0f;
	/* glides down (wings held), beats on the climb */
	int climb = vy < -0.02f;
	o->flap += dt * (climb ? 11.0f : 2.0f);
	float wa = climb ? 0.9f * sinf(o->flap) : 0.2f + 0.08f * sinf(o->flap);
	float pitch = us_clampf(atan2f(vy, fabsf(vx) + 1e-4f), -0.6f, 0.9f);
	float k = crisp ? 0.048f : 0.028f;
	xf_t t = {x, y, d * k, k, 0.0f};
	float cp = cosf(-pitch), sp = sinf(-pitch);
#define RX(a, b) ((a)*cp - (b)*sp)
#define RY(a, b) ((a)*sp + (b)*cp)
	ell_t e[6];
	int n = 0;
	xf_ell(&t, &e[n++], 0.0f, 0.0f, 0.42f, 0.16f, -pitch);
	xf_ell(&t, &e[n++], RX(0.4f, 0.04f), RY(0.4f, 0.04f), 0.17f, 0.16f, 0.0f);
	xf_cap(&t, &e[n++], RX(-0.4f, -0.02f), RY(-0.4f, -0.02f), RX(-0.65f, -0.05f), RY(-0.65f, -0.05f), 0.09f);
	float wl = 1.05f;
	xf_cap(&t, &e[n++], RX(0.08f, 0.06f), RY(0.08f, 0.06f), RX(-0.1f, 0.06f + wl * sinf(wa)), RY(-0.1f, 0.06f + wl * sinf(wa)), 0.22f);
	blob(s, c, e, n, fill, line, m, NULL, crisp);
	float fw = crisp ? fill * 0.75f : 0.0f;
	ell_t w2;
	xf_cap(&t, &w2, RX(0.02f, 0.02f), RY(0.02f, 0.02f), RX(-0.2f, 0.02f + 0.7f * wl * sinf(wa - 0.25f)), RY(-0.2f, 0.02f + 0.7f * wl * sinf(wa - 0.25f)), 0.16f);
	if (crisp)
		blob(s, c, &w2, 1, fw, 0.0f, m, NULL, crisp);
	else
		blob(s, c, &w2, 1, 0.012f, line * 0.55f, m, NULL, crisp);
#undef RX
#undef RY
}

/* ---- life: plants in the hollows, a far oasis ----------------------------------------------------- */

static void palms(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L)
{
	float A = ctx->aspect;
	const short *occ = s->occ[1];
	int crisp = ctx->crisp;
	float gy = s->lay[1].base + 0.002f;
	float v = (crisp ? 0.5f : 0.3f) + 0.12f * (L->Ia + 0.5f * L->Ik);
	float sway = 0.04f * s->wind;
	/* the pool: a thin bright line at their feet that shimmers on the hats */
	float cx = s->oasis_u * A;
	float pw = 0.035f;
	float wv = (crisp ? 0.5f : 0.22f) + 0.25f * L->night * 0.5f + 0.35f * s->hat_e;
	oline(s, c, cx - pw, gy + 0.0035f, cx + pw, gy + 0.0035f, wv, US_MAT_WATER, occ);
	if (!crisp)
		oline(s, c, cx - pw * 0.6f, gy + 0.0055f, cx + pw * 0.7f, gy + 0.0055f, wv * 0.5f, US_MAT_WATER, occ);
	for (int i = 0; i < NPALM; i++) {
		float x = cx + s->pm_du[i] * A * (crisp ? 1.4f : 1.0f), h = s->pm_h[i] * (crisp ? 1.6f : 1.0f);
		float lean = s->pm_lean[i] + sway * 2.0f;
		float px = x, py = gy;
		float tx = 0, ty = 0;
		for (int q = 1; q <= 4; q++) {
			float f = q / 4.0f;
			tx = x + lean * h * f * f * 0.6f;
			ty = gy - h * f;
			oline(s, c, px, py, tx, ty, v * 0.85f, US_MAT_STEM, occ);
			px = tx;
			py = ty;
		}
		/* fronds: arching out and drooping, tossing in the wind */
		int nfr = crisp ? 4 : 7;
		for (int k = 0; k < nfr; k++) {
			float a = crisp ? (k < 2 ? US_PI + 0.3f - 0.55f * k : 0.55f * (k - 2) - 0.3f) : -US_PI * 0.5f + (k - 3) * 0.48f + 0.06f * us_noise2(i * 3.0f + k, (float)ctx->t * (0.25f + 0.5f * s->wind));
			float L0 = h * (0.42f + 0.12f * ((k * 7 + i) % 3));
			float fx = tx, fy = ty;
			for (int q = 1; q <= 4; q++) {
				float f = q / 4.0f;
				float nx = tx + cosf(a) * L0 * f + sway * h * f * 3.0f, ny = ty + sinf(a) * L0 * f + L0 * 0.75f * f * f;
				oline(s, c, fx, fy, nx, ny, v * (1.0f - 0.25f * f), US_MAT_LEAF, occ);
				fx = nx;
				fy = ny;
			}
		}
	}
}

static void plants(st_t *s, const us_ctx *ctx, us_canvas *c, const light_t *L, int near)
{
	float A = ctx->aspect, S = c->s;
	int crisp = ctx->crisp;
	us_world *w = ctx->w;
	float spring = us_world_season_w(w, 0), winter = us_world_season_w(w, 3);
	float wet = us_smooth(0.25f, 0.6f, w->moisture);
	float bv = 0.26f + 0.2f * (L->Ia + 0.5f * L->Ik);
	const short *occ = near ? NULL : s->occ[3];
	float dens = ctx->p->density;
	/* succulent rosettes: stiff pointed leaves fanning up from the sand */
	int ns = (int)(NSUC * (0.5f + 0.6f * dens) + 0.5f);
	for (int i = 0; i < NSUC && i < ns; i++) {
		if ((i >= 3) != near)
			continue;
		float u = s->su_u[i], x = u * A, y;
		if (near)
			y = gnd(s, 4, x, c) + 0.025f + 0.04f * s->su_f[i];
		else {
			float tp, bt;
			valley(s, u, A, c, &tp, &bt);
			if (bt - tp < 0.015f)
				continue;
			y = us_lerp(tp, bt, s->su_f[i]);
		}
		if (y > 0.995f)
			continue;
		float sz = s->su_sz[i] * (near ? 0.03f : 0.016f) * (crisp ? 1.25f : 1.0f);
		for (int k = 0; k < 9; k++) {
			float a = -US_PI * 0.5f + (k - 4) * 0.32f;
			float ln = sz * (1.0f - 0.08f * fabsf((float)(k - 4))) * (0.75f + 0.25f * hashf((uint32_t)(i * 37 + k)));
			float bx = x + (k - 4) * sz * 0.05f;
			float mx = bx + cosf(a) * ln * 0.55f, my = y + sinf(a) * ln * 0.55f;
			float ex = bx + cosf(a * 1.0f - 0.15f * (k - 4) * 0.2f) * ln, ey = y + sinf(a) * ln * 0.95f + ln * 0.08f * fabsf((float)(k - 4)) / 4.0f;
			float lv = bv * (0.95f - 0.25f * fabsf((float)(k - 4)) / 4.0f) * (crisp ? 1.6f : 1.0f);
			oline(s, c, bx, y, mx, my, lv, US_MAT_LEAF, occ);
			oline(s, c, mx, my, ex, ey, lv * 0.85f, US_MAT_LEAF, occ);
			if (!crisp && k != 4)
				oline(s, c, bx + (k < 4 ? 0.0006f : -0.0006f), y, mx, my + 0.0006f, lv * 0.5f, US_MAT_LEAF, occ);
		}
	}
	/* desert lilies: white trumpets on short stalks, opening at night and after rain; they lean with the wind */
	float open = us_sat((0.35f + 0.5f * spring + 0.6f * wet) * (1.0f - winter)) * (0.6f + 0.4f * L->night);
	if (open < 0.05f)
		return;
	int nl = (int)(NLILY * (0.4f + 0.7f * dens) + 0.5f);
	for (int i = 0; i < NLILY && i < nl; i++) {
		if ((i >= 3) != near)
			continue;
		if (hashf((uint32_t)i * 71u + 5u) > open + 0.25f)
			continue;
		float u = s->li_u[i], x = u * A, y;
		if (near)
			y = gnd(s, 4, x, c) + 0.035f + 0.04f * s->li_f[i];
		else {
			float tp, bt;
			valley(s, u, A, c, &tp, &bt);
			if (bt - tp < 0.015f)
				continue;
			y = us_lerp(tp, bt, s->li_f[i]);
		}
		if (y > 0.995f)
			continue;
		float h = (near ? 0.04f : 0.022f) * (crisp ? 1.2f : 1.0f);
		float lean = 0.15f + 0.4f * s->wind * (0.6f + 0.4f * us_noise2(s->li_ph[i], (float)ctx->t * (0.3f + 0.5f * s->wind)));
		float tx = x + lean * h * 0.5f, ty = y - h;
		/* strap leaves at the foot */
		for (int k = -1; k <= 1; k += 2)
			oline(s, c, x, y, x + k * h * 0.35f + lean * h * 0.2f, y - h * 0.3f, bv * 0.9f * (crisp ? 1.5f : 1.0f), US_MAT_LEAF, occ);
		oline(s, c, x, y, tx, ty, bv * (crisp ? 1.4f : 1.0f), US_MAT_STEM, occ);
		float fv = us_sat((0.45f + 0.35f * open + 0.25f * s->swl) * (crisp ? 1.1f : 1.0f));
		float r0 = h * 0.28f * (0.5f + 0.5f * open);
		if (crisp) {
			oput(s, c, (int)(tx * S), (int)(ty * S), fv, US_MAT_BLOOM, occ);
			oput(s, c, (int)(tx * S) - 1, (int)(ty * S), fv * 0.8f, US_MAT_BLOOM, occ);
			oput(s, c, (int)(tx * S) + 1, (int)(ty * S), fv * 0.8f, US_MAT_BLOOM, occ);
			oput(s, c, (int)(tx * S), (int)(ty * S) - 1, fv * 0.8f, US_MAT_BLOOM, occ);
		} else
			for (int k = 0; k < 6; k++) {
				float a = US_TAU * k / 6 + s->li_ph[i];
				oline(s, c, tx, ty, tx + cosf(a) * r0, ty + sinf(a) * r0 * 0.7f, fv * 0.8f, US_MAT_BLOOM, occ);
			}
	}
}

/* ---- frame -------------------------------------------------------------------------------------- */

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *s = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	us_rng *r = &s->rng;
	float A = ctx->aspect, dt = ctx->dt, rdt = ctx->real_dt, S = c->s;
	float dens = ctx->p->density;
	float kick = us_sat(a->kick), snare = us_sat(a->snare), hat = us_sat(a->hat);
	int W = c->w < MAXCOL ? c->w : MAXCOL;

	/* ---- envelopes ---- */
	s->lvl = us_approach(s->lvl, us_sat(a->level), a->level > s->lvl ? 0.15f : 0.6f, rdt);
	s->swl = us_approach(s->swl, a->swell, 2.0f, rdt);
	s->wid = us_approach(s->wid, a->width, 1.5f, rdt);
	s->cen = us_approach(s->cen, a->active > 0.2f ? a->centroid : 0.4f, 1.5f, rdt);
	s->kickp = us_approach(s->kickp, kick, kick > s->kickp ? 0.02f : 0.19f, rdt);
	for (int i = 0; i < US_NBANDS; i++) {
		float bv = a->bands[i];
		s->bandv[i] = us_approach(s->bandv[i], bv, bv > s->bandv[i] ? 0.05f : 0.4f, rdt);
	}
	float breath = 0.5f + 0.5f * sinf((float)ctx->t * (US_TAU / 11.0f));
	float wind_t = us_sat(0.22f + 0.3f * w->wind + 0.55f * s->lvl + 0.2f * a->high + 0.1f * breath * (1.0f - s->lvl));
	s->wind = us_approach(s->wind, wind_t, 0.8f, rdt);
	s->T += dt * (0.3f + 0.6f * s->wind);
	if (a->hat_hit)
		s->hat_seed = s->hat_seed * 1664525u + 1013904223u;
	if (a->snare_hit)
		s->gust_seed = s->gust_seed * 22695477u + 1u;
	s->snare_e = snare;
	/* the desert dries the air */
	us_world_add(w, &w->moisture, -0.0015f * (0.4f + w->daylight), dt);

	/* the light: loudness lifts it, the kick pulses it, it breathes in the quiet */
	/* (pixel styles posterise the sand into a few shades: give loudness a wider swing there) */
	float gain = (ctx->crisp ? 0.45f + 1.0f * s->lvl : 0.62f + 0.62f * s->lvl) + 0.06f * breath * (1.0f - s->lvl);
	gain *= 1.0f + (0.1f + 0.28f * s->lvl) * s->kickp; /* the kick's light pulse, stronger when it is loud */
	light_t L;
	light(w, gain, &L);

	/* ---- outlines per column ---- */
	for (int li = 0; li < NLAY; li++) {
		const layer_t *l = &s->lay[li];
		for (int px = 0; px < W; px++) {
			float u = (px + 0.5f) / S / A;
			float base = base_y(l, li, u), y = base;
			for (int hi = 0; hi < l->n; hi++) {
				float hy = hump_sil(&l->h[hi], base, u);
				if (hy < y)
					y = hy;
			}
			s->sil[li][px] = y;
		}
	}

	/* ---- life: the creatures step (hats light their eyes; kicks set the jerboas hopping) ---- */
	s->hat_e = a->hat_hit ? fmaxf(s->hat_e, 0.6f + 0.4f * hat) : us_approach(s->hat_e, 0.0f, 0.12f, rdt);
	fox_update(s, ctx, snare, a->snare_hit, s->lvl);
	jerb_update(s, ctx, c, a->kick_hit, kick, a->snare_hit, snare, s->lvl);

	/* ---- sky, then the sand over it ---- */
	sky(s, ctx, c, &L, hat, dens);
	fill(s, ctx, c, &L, s->kickp);
	acacia_shadow(s, ctx, c, &L);

	/* ---- kick: an avalanche breaks off a crest; quiet still lets the odd one slide ---- */
	s->av_cool -= rdt;
	s->av_wait -= dt;
	if (a->kick_hit && s->av_cool <= 0)
		start_avalanche(s, kick, A);
	else if (s->av_wait <= 0) {
		start_avalanche(s, 0.4f, A);
		s->av_wait = us_randr(r, 7.0f, 16.0f);
	}
	/* ---- snare: a gust lifts a sheet of sand off the crests; a strong one spins up a dust devil ---- */
	s->dd_cool -= rdt;
	if (a->snare_hit) {
		int n = (int)(110 + 90 * snare);
		for (int k = 0; k < n; k++) {
			int li = 2 + (int)(us_rand(r) * 2.999f);
			const layer_t *l = &s->lay[li];
			const hump_t *h = &l->h[(int)(us_rand(r) * l->n) % l->n];
			float x, y;
			crest_point(s, l, li, h, &x, &y, A);
			float ds = 0.4f + 0.2f * li;
			emit(s, li, x, y, us_randr(r, 0.18f, 0.42f) * ds * (0.8f + 0.4f * s->wid), -us_randr(r, 0.02f, 0.09f + 0.06f * s->cen) * ds,
			     us_randr(r, 0.45f, 1.0f), us_randr(r, 1.2f, 1.8f));
		}
		if (s->dd_cool <= 0 && (snare > 0.6f || us_rand(r) < 0.25f)) {
			for (int i = 0; i < MAX_DD; i++)
				if (!s->dd[i].on) {
					devil_t *d = &s->dd[i];
					d->on = 1;
					d->lay = us_rand(r) < 0.6f ? 3 : 2;
					d->u = us_randr(r, 0.3f, 0.75f);
					d->age = 0;
					d->life = us_randr(r, 3.5f, 6.0f);
					d->h = d->lay == 3 ? us_randr(r, 0.12f, 0.18f) : us_randr(r, 0.06f, 0.09f);
					d->r = d->h * 0.22f;
					d->ph = us_rand(r) * US_TAU;
					d->amp = 0.7f + 0.3f * snare;
					s->dd_cool = 5.0f;
					break;
				}
		}
	}
	/* hats: a fresh scatter of sand grains glitters on the lit faces */
	if (a->hat_hit) {
		int wlit = L.Tw >= L.Ts;
		for (int k = 0; k < 24; k++) {
			int li = 2 + (int)(us_rand(r) * 2.999f);
			const layer_t *l = &s->lay[li];
			const hump_t *h = &l->h[(int)(us_rand(r) * l->n) % l->n];
			float u, y;
			if (wlit) {
				u = us_lerp(h->cx - h->wl * 0.8f, crest_ex(h), us_rand(r));
				float top = hump_sil(h, l->base, u), bot = hump_toe(h, l->base, u);
				if (u > h->cx)
					top = crest_y(h, u);
				y = us_lerp(top, bot, us_rand(r) * 0.8f + 0.05f);
			} else {
				float sq = us_rand(r) * 0.8f, cu, cyv;
				crest_at(h, sq, &cu, &cyv);
				u = us_lerp(cu, h->cx + h->wr, us_rand(r) * 0.6f);
				float top = hump_sil(h, l->base, u);
				y = us_lerp(top, cyv, us_rand(r));
			}
			glint_t *g = &s->gl[s->gli];
			s->gli = (s->gli + 1) % MAX_GL;
			g->x = u;
			g->y = y;
			g->age = 0;
			g->lay = li;
		}
	}

	/* ---- the dunes' lines, far to near, with what lives on each range ---- */
	for (int li = 0; li < NLAY; li++) {
		layer_lines(s, ctx, c, li, &L, s->kickp, dens);
		s->over = ctx->crisp;
		if (li == 1)
			palms(s, ctx, c, &L);
		if (li == 2)
			caravan(s, ctx, c, &L);
		if (li == 3) {
			s->sway = us_approach(s->sway, 0.02f * s->wind + 0.04f * snare, 0.5f, rdt);
			acacia(s, ctx, c, &L, s->sway);
			owl(s, ctx, c, &L, snare, a->snare_hit, 1);
			plants(s, ctx, c, &L, 0);
			jerb_draw(s, ctx, c, &L);
		}
		if (li == 4) {
			plants(s, ctx, c, &L, 1);
			beetle(s, ctx, c, &L, s->lvl);
			fox_draw(s, ctx, c, &L);
		}
		s->over = 0;
	}
	s->drift += dt * (0.08f + 0.35f * s->wind);
	veils(s, ctx, c, &L);
	avalanches(s, ctx, c, &L, A);
	devils(s, ctx, c, &L, A);
	sand(s, ctx, c, &L, s->wind, dens, A);
	/* glitter */
	for (int i = 0; i < MAX_GL; i++) {
		glint_t *g = &s->gl[i];
		if (g->age > 0.3f)
			continue;
		g->age += rdt;
		float v = (1.0f - g->age / 0.3f) * (0.6f + 0.4f * hat) * (0.6f + 0.5f * (L.Ia + L.Ik));
		float x = g->x * A, y = g->y;
		int px = (int)(x * S), py = (int)(y * S);
		oput(s, c, px, py, v, US_MAT_LIGHT, s->occ[g->lay]);
		if (!ctx->crisp) {
			float l = 0.0018f + 0.002f * v;
			oline(s, c, x - l, y, x + l, y, v * 0.5f, US_MAT_LIGHT, s->occ[g->lay]);
			oline(s, c, x, y - l, x, y + l, v * 0.5f, US_MAT_LIGHT, s->occ[g->lay]);
		}
	}
	s->over = ctx->crisp;
	tufts(s, ctx, c, &L, s->wind, dens);
	owl(s, ctx, c, &L, snare, a->snare_hit, 0); /* in flight, over everything */
	s->over = 0;
	s->life_init = 1;
}

const us_scene_vt us_scene_dunes = {"dunes", "Dunes", 0.3f, create, destroy, frame};
