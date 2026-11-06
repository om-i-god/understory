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
				float pulse = 1.0f + 0.35f * kick * (lit ? 1.0f : 0.4f) * (0.3f + 0.7f * LBRI[li]);
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
		float cv = lb * (0.3f + 0.55f * Tl) * (1.0f + 0.7f * kick) * (1.0f - 0.4f * hz);
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
			float sv = lb * sn * (0.35f + 0.45f * (L->Ia + L->Ik)) * (crisp ? 1.8f : 1.0f);
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
	int n = (int)(8 + 14 * strength);
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
	s->kickp = us_approach(s->kickp, kick, kick > s->kickp ? 0.02f : 0.25f, rdt);
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
	gain *= 1.0f + 0.25f * s->kickp;
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
		for (int k = 0; k < 18; k++) {
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
		if (li == 2)
			caravan(s, ctx, c, &L);
		if (li == 3) {
			s->sway = us_approach(s->sway, 0.02f * s->wind + 0.04f * snare, 0.5f, rdt);
			acacia(s, ctx, c, &L, s->sway);
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
	s->over = 0;
}

const us_scene_vt us_scene_dunes = {"dunes", "Dunes", 0.3f, create, destroy, frame};
