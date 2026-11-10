/* Understory scene: Snowfall — a winter fir forest at blue hour / night, a cabin in a clearing.
 *
 * Four stands of firs recede into mist behind a snowy clearing; a log cabin with lit windows is the warm
 * focal point, its light pooling on the snow; the moon glows through thin snow cloud; two big laden firs
 * frame the view (a snowy owl sits on the left one), a lone fir stands in the clearing, now and then a deer
 * wanders across leaving tracks that slowly fill in. Snow falls in three depths: fine far flakes (behind
 * the forest edge), mid flakes, and big soft near ones.
 * Every fir is drawn the same way: tiers of sloping boughs, each carrying its own load of snow on its upper
 * surface (bright, lumpy caps over dark boughs), filled as anti-aliased scanline spans that occlude (set,
 * not max), so nearer layers hide farther ones. Sky, mist and the snow field are smooth tone grids.
 * Audio: kick -> every near bough is knocked down and twangs back, and laden boughs shed their snow (a
 *        clump drops, bursts into powder, the bough springs up and slowly reloads as it snows);
 *        snare -> a gust bends the trees downwind and lets them whip back, whirls a flurry of powder across
 *        the clearing and slants the falling snow (a strong crack can send the owl off); hats -> flakes,
 *        snow crystals and the bough snow glint as they catch the light; loudness -> heavier snowfall, the
 *        window and its pool of light glow brighter, warmer and wider, the sky lifts; swell -> snowfall;
 *        lowmid -> the hearth (window warmth); bands -> the far mist lights along the valley; width -> the
 *        wind spreads the fall; high/air -> sparkle.
 * World: season (winter = deep snow, heavy boughs, thick fall), daylight (night -> moon; blue hour; pale
 *        day), wind (slant + sway), moisture + storm_charge (heavier fall), calm (the deer comes more often);
 *        writes moisture (snow).
 * Life: a red fox mouses in the clearing (trots, stops to listen head-down, cocking its head -- the kick flips
 *        the tilt; a snare launches the pounce: a high arc, a nose-first dive and a burst of powder, the tail
 *        waving while it digs, then it backs out and shakes; a snare while it trots freezes it, ears up);
 *        snowshoe hares (white in winter, brown otherwise) sit and nibble, hop on the kick, sit bolt upright on
 *        the snare and bolt from a pouncing fox; bullfinches on the frame firs' boughs hop and flick their
 *        wings on the kick (flit off when their bough sheds; open their wings in the snare's gust; a big crack
 *        can flush them; fluffed and roosting at night; more with the shared flock); frosted grass pokes
 *        through the snow in the foreground, bending in the gust, its seed heads glinting with the hats.
 * Silence: snow keeps falling, mist drifts, smoke curls, the window flickers, the owl blinks and turns its
 *        head, the deer browses, crystals twinkle, the fox listens and pounces, hares nibble and hop.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAXT 14         /* tiers per fir */
#define NLAYER 4        /* far stands */
#define MAX_TREES 230   /* far stands + the dynamic trees */
#define MAX_FAR 760
#define MAX_MID 300
#define MAX_NEAR 64
#define MAX_PART 1200
#define MAX_CRYS 320
#define MAX_PRINT 120
#define MAX_DRIFT 7
#define MAX_CG 64
#define MAXCOL 8192
#define SGW 40 /* sky tone grid */
#define SGH 18
#define GGW 72 /* ground tone grid */
#define GGH 30
#define SKY_Y1 0.70f
#define GY0 0.62f
#define HORIZ 0.664f
#define MAX_HARE 3
#define MAX_BIRD 6
#define MAX_GRASS 7

typedef struct {
	float x;            /* fraction of width */
	float base, h, w;   /* base y, height, half width at the bottom tier */
	int nt, layer, dyn; /* tiers; layer 0..3 far stands, 4 lone fir, 5 near frame */
	float ph, dip; /* dip: the kick thump bending the boughs down (and back) */
	float len[MAXT][2], load[MAXT][2], sp[MAXT][2], spv[MAXT][2];
} fir_t;

typedef struct {
	float x, y, vy, ph, r, a, gl, v; /* x fraction of width */
} flake_t;

typedef struct {
	float x, y, vx, vy, life, max, r, v, drag, floor; /* floor: the snow it lands on */
	int kind; /* 0 clump, 1 powder puff, 2 flurry */
} part_t;

typedef struct {
	float x, y, gl, m; /* x fraction */
} crys_t;

typedef struct {
	float x, y, age, s; /* x fraction */
} print_t;

typedef struct {
	float z, x0, x1, ph, f;
} drift_t;

/* a snowshoe hare: sits, nibbles, hops (kick), sits up on alert (snare), bolts from the fox */
typedef struct {
	float x, y, d;  /* x fraction of width, ground y, facing */
	float hx0, ht;  /* hop: start x, time into the hop (< 0: sitting) */
	float up;       /* 0 crouched .. 1 sitting bolt upright */
	float alert_t, next, ear, away;
	int bolt;
} hare_t;

/* a bullfinch on a bough of a frame fir: hops on the kick, flits off when its bough sheds */
typedef struct {
	int tree, k, sd, state; /* state: 0 perched, 1 flying to a perch, 2 flying away, 3 away */
	float u;                /* position along the bough */
	float x, y, fx0, fy0, t, dur, hop, wing, flap, look, cool, away;
} bird_t;

/* a clump of frosted grass poking through the snow in the foreground */
typedef struct {
	float x, y, h, lean, ph;
	int n;
} grass_t;

typedef struct {
	us_rng rng;
	us_rng lrng; /* the creatures' own stream, so the rest of the scene runs as before */
	fir_t tr[MAX_TREES];
	int ntr;
	int lone, nearL, nearR; /* indices of the dynamic trees */
	flake_t ff[MAX_FAR], fm[MAX_MID], fn[MAX_NEAR];
	part_t pt[MAX_PART];
	int pti;
	crys_t cr[MAX_CRYS];
	/* glints on the bough snow (hats): tree, tier, side, position along the bough, life */
	struct { int t, k, sd; float u, gl; } cg[MAX_CG];
	int cgi;
	print_t pr[MAX_PRINT];
	int pri;
	drift_t dr[MAX_DRIFT];
	float cab_x; /* fraction */
	/* envelopes */
	float lvl, swl, lm, hi, wid, kd, kdv, gs, gsv, bandv[US_NBANDS];
	float gust, gust_dir, kick_cool, snare_cool, hat_cool;
	float T, mistT, cloudT;
	float snow; /* snowfall rate 0..1 (smoothed) */
	float warm; /* window warmth (smoothed) */
	/* deer */
	int deer_on, deer_stag;
	float deer_x, deer_y, deer_dir, deer_ph, deer_t, deer_head, deer_head_to, next_deer, deer_step;
	int deer_mode; /* 0 walk, 1 graze, 2 alert */
	/* owl */
	int owl_state; /* 0 perched, 1 leaving, 2 away, 3 returning */
	float owl_x, owl_y, owl_vx, owl_vy, owl_t, owl_flap, owl_blink, owl_head, owl_head_to, owl_away, owl_look_t;
	float owl_px, owl_py; /* perch point (normalized, refreshed per frame) */
	/* red fox, mousing in the clearing */
	int fox_on, fox_mode, fox_n; /* mode: 0 trot, 1 listen, 2 pounce, 3 dug in, 4 emerge + shake, 5 alert */
	float fox_x, fox_y, fox_dir, fox_ph, fox_t, fox_dur, fox_tgt, next_fox;
	float fox_head, fox_alert, fox_tilt, fox_tilt_to, fox_x0, fox_hole;
	hare_t hr[MAX_HARE];
	bird_t bd[MAX_BIRD];
	float fir_sway[2]; /* this frame's sway of the two frame firs (the birds ride it) */
	grass_t gr[MAX_GRASS];
	float frost_gl;
	/* scratch */
	float sky[SGW * SGH];
	float gnd[3 * GGW * GGH];
	float topS[MAXCOL];
	float H[GGH + 1][GGW + 1], H_aspect; /* the snow surface height field, cached per aspect */
} st_t;

/* ------------------------------------------------------------------ raster helpers */

static const float BAYER[16] = {0.03f, 0.53f, 0.16f, 0.66f, 0.78f, 0.28f, 0.91f, 0.41f,
				0.22f, 0.72f, 0.09f, 0.59f, 0.97f, 0.47f, 0.84f, 0.34f};

/* blend a pixel toward v by coverage (set, not max): nearer things occlude farther ones */
static inline void pset(us_canvas *c, int px, int py, int iv, float cov, uint8_t mat)
{
	size_t i = (size_t)py * c->w + px;
	int o = c->ink[i];
	c->ink[i] = (uint8_t)(o + (int)((iv - o) * cov));
	if (cov >= 0.5f)
		c->mat[i] = mat;
}

/* one row of an anti-aliased filled span: the left edge crosses this row between l0..l1 px, the right
 * edge between r0..r1 (so shallow edges ramp smoothly instead of stair-stepping) */
static void span_aa(us_canvas *c, int py, float l0, float l1, float r0, float r1, float v, uint8_t mat)
{
	if (py < 0 || py >= c->h)
		return;
	if (l0 > l1) { float t = l0; l0 = l1; l1 = t; }
	if (r0 > r1) { float t = r0; r0 = r1; r1 = t; }
	float lm = 0.5f * (l0 + l1), rm = 0.5f * (r0 + r1);
	if (!(rm > lm))
		return;
	int iv = (int)(us_sat(v) * 255.0f + 0.5f);
	if (c->crisp) {
		int a = (int)floorf(lm + 0.5f), b = (int)floorf(rm + 0.5f);
		if (a < 0) a = 0;
		if (b > c->w) b = c->w;
		uint8_t *ink = c->ink + (size_t)py * c->w, *mt = c->mat + (size_t)py * c->w;
		for (int i = a; i < b; i++) {
			ink[i] = (uint8_t)iv;
			mt[i] = mat;
		}
		return;
	}
	float wl = l1 - l0 > 1.0f ? l1 - l0 : 1.0f, wr = r1 - r0 > 1.0f ? r1 - r0 : 1.0f;
	float iwl = 1.0f / wl, iwr = 1.0f / wr;
	int a = (int)floorf(lm - 0.5f * wl), b = (int)ceilf(rm + 0.5f * wr);
	if (a < 0) a = 0;
	if (b > c->w) b = c->w;
	int fa = (int)ceilf(lm + 0.5f * wl), fb = (int)floorf(rm - 0.5f * wr); /* fully covered run */
	uint8_t *ink = c->ink + (size_t)py * c->w, *mt = c->mat + (size_t)py * c->w;
	for (int i = a; i < b; i++) {
		if (i >= fa && i < fb) {
			ink[i] = (uint8_t)iv;
			mt[i] = mat;
			continue;
		}
		float xc = i + 0.5f;
		float cov = us_sat((xc - lm) * iwl + 0.5f) * us_sat((rm - xc) * iwr + 0.5f);
		if (cov > 0.004f)
			pset(c, i, py, iv, cov, mat);
	}
}

/* filled polygon (normalized coords), anti-aliased spans, occluding */
static void poly_set(us_canvas *c, const float *p, int n, float v, uint8_t mat)
{
	if (n < 3)
		return;
	float S = c->s, ymin = 1e9f, ymax = -1e9f;
	for (int i = 0; i < n; i++) {
		if (!(p[2 * i] == p[2 * i]) || !(p[2 * i + 1] == p[2 * i + 1]))
			return;
		if (p[2 * i + 1] < ymin) ymin = p[2 * i + 1];
		if (p[2 * i + 1] > ymax) ymax = p[2 * i + 1];
	}
	if (ymax < -1.0f || ymin > 3.0f)
		return;
	int py0 = (int)floorf(ymin * S), py1 = (int)ceilf(ymax * S);
	if (py0 < 0) py0 = 0;
	if (py1 > c->h) py1 = c->h;
	for (int py = py0; py < py1; py++) {
		float yc = (py + 0.5f) / S, yt = py / S, yb = (py + 1.0f) / S;
		float xs[32], xt[32], xb[32];
		int m = 0;
		for (int i = 0; i < n && m < 32; i++) {
			int j = (i + 1) % n;
			float x0 = p[2 * i], y0 = p[2 * i + 1], x1 = p[2 * j], y1 = p[2 * j + 1];
			if ((y0 <= yc && y1 > yc) || (y1 <= yc && y0 > yc)) {
				float k = (x1 - x0) / (y1 - y0);
				float lo = y0 < y1 ? y0 : y1, hi = y0 < y1 ? y1 : y0;
				float ta = us_clampf(yt, lo, hi), tb = us_clampf(yb, lo, hi);
				xs[m] = (x0 + (yc - y0) * k) * S;
				xt[m] = (x0 + (ta - y0) * k) * S;
				xb[m] = (x0 + (tb - y0) * k) * S;
				m++;
			}
		}
		for (int i = 1; i < m; i++) { /* insertion sort by centre crossing */
			float a = xs[i], b = xt[i], d = xb[i];
			int j = i - 1;
			while (j >= 0 && xs[j] > a) {
				xs[j + 1] = xs[j];
				xt[j + 1] = xt[j];
				xb[j + 1] = xb[j];
				j--;
			}
			xs[j + 1] = a;
			xt[j + 1] = b;
			xb[j + 1] = d;
		}
		for (int i = 0; i + 1 < m; i += 2)
			span_aa(c, py, xt[i], xb[i], xt[i + 1], xb[i + 1], v, mat);
	}
}

static void ellipse_pts(float *p, int n, float x, float y, float rx, float ry, float rot)
{
	float cr = cosf(rot), sr = sinf(rot);
	for (int i = 0; i < n; i++) {
		float a = US_TAU * i / n;
		float ex = cosf(a) * rx, ey = sinf(a) * ry;
		p[2 * i] = x + ex * cr - ey * sr;
		p[2 * i + 1] = y + ex * sr + ey * cr;
	}
}

static void ellipse_set(us_canvas *c, float x, float y, float rx, float ry, float rot, float v, uint8_t mat)
{
	float p[2 * 20];
	ellipse_pts(p, 20, x, y, rx, ry, rot);
	poly_set(c, p, 20, v, mat);
}

/* darken toward v inside a soft ellipse (footprints, hollows in the snow) */
static void dab_min(us_canvas *c, float x, float y, float rx, float ry, float v)
{
	float S = c->s, px = x * S, py = y * S, rpx = rx * S, rpy = ry * S;
	if (c->crisp) {
		if (rpx < 0.6f) rpx = 0.6f;
		if (rpy < 0.5f) rpy = 0.5f;
	}
	int x0 = (int)floorf(px - rpx - 1), x1 = (int)ceilf(px + rpx + 1);
	int y0 = (int)floorf(py - rpy - 1), y1 = (int)ceilf(py + rpy + 1);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > c->w) x1 = c->w;
	if (y1 > c->h) y1 = c->h;
	int iv = (int)(us_sat(v) * 255.0f);
	float m = rpx < rpy ? rpx : rpy;
	for (int j = y0; j < y1; j++)
		for (int i = x0; i < x1; i++) {
			float dx = (i + 0.5f - px) / rpx, dy = (j + 0.5f - py) / rpy;
			float d = sqrtf(dx * dx + dy * dy);
			float cov = c->crisp ? (d < 1.0f ? 1.0f : 0.0f) : us_sat((1.0f - d) * (m + 0.5f));
			if (cov <= 0.0f)
				continue;
			size_t q = (size_t)j * c->w + i;
			int o = c->ink[q];
			if (o > iv)
				c->ink[q] = (uint8_t)(o + (iv - o) * cov);
		}
}

/* paint a smooth tone (1 or 2 channels, each its own material) over rows [ry0,ry1) with MAX blend. The second
 * channel's material wins where its share passes a band, ordered-dithered only inside the band, so hues meet
 * without a hard contour. topS: optional per-column top edge (pixels), anti-aliased. Grid: g[(k*gh + j)*gw + i],
 * columns span the canvas width, rows span [ry0, ry1]. Walks each grid cell with incremental steps. */
static void tone(us_canvas *c, const float *g, int K, int gw, int gh, const uint8_t *mats, float ry0, float ry1,
		 const float *topS)
{
	float blo = c->crisp ? 0.42f : 0.36f, bhi = c->crisp ? 0.4201f : 0.5f;
	float S = c->s;
	int W = c->w < MAXCOL ? c->w : MAXCOL;
	int py0 = (int)floorf(ry0 * S), py1 = (int)ceilf(ry1 * S);
	if (py0 < 0) py0 = 0;
	if (py1 > c->h) py1 = c->h;
	if (K > 2) K = 2;
	float cpx = (float)W / (gw - 1), icpx = 1.0f / cpx;
	float row[2][128];
	for (int py = py0; py < py1; py++) {
		float fy = ((py + 0.5f) / S - ry0) / (ry1 - ry0) * (gh - 1);
		int iy = (int)floorf(fy);
		if (iy < 0) iy = 0;
		if (iy > gh - 2) iy = gh - 2;
		float ty = us_sat(fy - iy);
		for (int k = 0; k < K; k++) {
			const float *r0 = g + (k * gh + iy) * gw, *r1 = r0 + gw;
			for (int i = 0; i < gw; i++)
				row[k][i] = r0[i] + (r1[i] - r0[i]) * ty;
		}
		if (K == 1)
			for (int i = 0; i < gw; i++)
				row[1][i] = 0.0f;
		uint8_t *ink = c->ink + (size_t)py * c->w, *mt = c->mat + (size_t)py * c->w;
		const float *bay = BAYER + (py & 3) * 4;
		float fpy = py + 1.0f;
		for (int i = 0; i < gw - 1; i++) {
			int pa = (int)ceilf(i * cpx - 0.5f), pb = (int)ceilf((i + 1) * cpx - 0.5f);
			if (pa < 0) pa = 0;
			if (pb > W) pb = W;
			if (pb <= pa)
				continue;
			float t0 = (pa + 0.5f) * icpx - i;
			float d0 = (row[0][i + 1] - row[0][i]) * icpx, d1 = (row[1][i + 1] - row[1][i]) * icpx;
			float a0 = row[0][i] + (row[0][i + 1] - row[0][i]) * t0, a1 = row[1][i] + (row[1][i + 1] - row[1][i]) * t0;
			for (int px = pa; px < pb; px++, a0 += d0, a1 += d1) {
				float cov = 1.0f;
				if (topS) {
					cov = fpy - topS[px];
					if (cov <= 0.0f)
						continue;
					if (cov > 1.0f) cov = 1.0f;
				}
				float c0 = a0 > 0.0f ? a0 : 0.0f, c1 = a1 > 0.0f ? a1 : 0.0f, sum = c0 + c1;
				int iv = (int)((sum < 1.0f ? sum : 1.0f) * cov * 255.0f);
				if (iv <= ink[px])
					continue;
				uint8_t m = mats[0];
				if (c1 > 0.0f && c1 > (blo + bay[px & 3] * (bhi - blo)) * sum)
					m = mats[1];
				ink[px] = (uint8_t)iv;
				mt[px] = m;
			}
		}
	}
}

static void line_(us_canvas *c, float ax, float ay, float bx, float by, float w, float v, uint8_t m)
{
	us_line(c, ax, ay, bx, by, c->crisp ? (w > 0.0045f ? w : 0.0045f) : w, v, m);
}

/* ------------------------------------------------------------------ firs */

static inline float tier_t(int k, int nt)
{
	float f = (float)k / nt;
	return f * (0.62f + 0.38f * f); /* tiers smaller near the tip */
}

typedef struct {
	float a[MAXT][2], b[MAXT][2], E[MAXT][2], th[MAXT][2];
} tiers_t;

static void fir_tiers(const fir_t *f, float snowk, tiers_t *T)
{
	for (int k = 0; k < f->nt; k++) {
		float t0 = tier_t(k, f->nt), t1 = tier_t(k + 1, f->nt);
		float a = k == 0 ? 0.0f : t0 - 0.32f * (t1 - t0);
		for (int sd = 0; sd < 2; sd++) {
			float sp = f->sp[k][sd] - f->dip * (0.4f + 0.6f * (float)k / f->nt) * (sd ? 1.0f : 0.85f);
			T->a[k][sd] = a - sp;
			T->b[k][sd] = t1 - sp;
			T->E[k][sd] = f->len[k][sd] * f->w * (0.05f + 0.95f * t1);
			T->th[k][sd] = f->load[k][sd] * snowk * (t1 - a) * 0.55f;
		}
	}
}

/* the bough profile: out nearly level from the trunk, drooping at the tip */
static inline float bough(float u)
{
	/* a straight sloping top (where the snow lies), easing out, with a short drooping tip */
	float q = u < 0.86f ? u / 0.86f : 1.0f;
	return 0.06f + 0.94f * q * (1.15f - 0.15f * q);
}

/* silhouette half-width on one side at height fraction t; returns the tier making the edge */
static float fir_hw(const fir_t *f, const tiers_t *T, float t, int sd, int *kk, float *uu)
{
	float best = f->w * 0.025f;
	*kk = -1;
	*uu = 0.0f;
	for (int k = 0; k < f->nt; k++) {
		float a = T->a[k][sd], b = T->b[k][sd];
		if (t < a || t > b)
			continue;
		float u = (t - a) / (b - a);
		float hw = T->E[k][sd] * bough(u);
		if (hw > best) {
			best = hw;
			*kk = k;
			*uu = u;
		}
	}
	return best;
}

/* inner edge of the snow cap on tier k at t (the bough edge one snow-thickness higher up) */
static float snow_inner(const tiers_t *T, int k, int sd, float t, float u, float hw)
{
	float a = T->a[k][sd], b = T->b[k][sd];
	float th = T->th[k][sd] * (0.45f + 0.4f * sinf(US_PI * us_sat(u * 1.05f)) + 0.15f * sinf(u * 17.0f + k * 2.3f + sd));
	float u2 = (t - th - a) / (b - a);
	float in = u2 <= 0.0f ? T->E[k][sd] * 0.06f : T->E[k][sd] * bough(u2);
	float lim = T->E[k][sd] * 0.12f;
	if (in < lim) in = lim;
	if (in > hw) in = hw;
	return in;
}

/* draw one fir: dark tiers with bright snow caps along each bough's upper edge (moonward side brighter) */
static void fir_draw(us_canvas *c, const fir_t *f, float A, float sway, float snowk, float v, float sv, uint8_t mat,
		     float trunkv, uint8_t smat)
{
	if (snowk < 0.0f) snowk = 0.0f;
	tiers_t T;
	fir_tiers(f, snowk, &T);
	float S = c->s, x0 = f->x * A;
	float ytop = f->base - f->h, hf = f->h * 0.9f;
	if (x0 + f->w * 1.3f < -0.05f || x0 - f->w * 1.3f > A + 0.05f)
		return;
	int py0 = (int)floorf((ytop - 0.05f * f->h) * S), py1 = (int)ceilf((ytop + hf) * S) + 1;
	if (py0 < 0) py0 = 0;
	if (py1 > c->h) py1 = c->h;
	int cr = c->crisp;
	/* the silhouette + snow edges at each row boundary (each boundary is shared by two rows) */
	float hwb[2] = {0, 0}, inb[2] = {0, 0}, hwa[2], ina[2];
	int have = 0;
	for (int py = py0; py < py1; py++) {
		float ya = py / S, yb = (py + 1.0f) / S;
		float ta = (ya - ytop) / hf, tb = (yb - ytop) / hf, tm = 0.5f * (ta + tb);
		if (tb < -0.06f)
			continue;
		if (ta > 1.0f)
			break;
		float sa = (1.0f - us_sat(ta)), sb = (1.0f - us_sat(tb));
		float xa = (x0 + sway * sa * sa * f->h) * S, xb = (x0 + sway * sb * sb * f->h) * S;
		for (int sd = 0; sd < 2; sd++) {
			int k;
			float u;
			if (have) {
				hwa[sd] = hwb[sd];
				ina[sd] = inb[sd];
			} else {
				hwa[sd] = fir_hw(f, &T, ta, sd, &k, &u);
				ina[sd] = k >= 0 && u > 0.08f ? snow_inner(&T, k, sd, ta, u, hwa[sd]) : hwa[sd];
			}
			hwb[sd] = fir_hw(f, &T, tb, sd, &k, &u);
			inb[sd] = k >= 0 && u > 0.08f ? snow_inner(&T, k, sd, tb, u, hwb[sd]) : hwb[sd];
			/* above its tip a tier contributes nothing at the row top: keep the edge from jumping */
			if (ta < 0.0f) {
				hwa[sd] = 0.0f;
				ina[sd] = 0.0f;
			}
		}
		have = 1;
		float l0 = xa - hwa[0] * S, l1 = xb - hwb[0] * S, r0 = xa + hwa[1] * S, r1 = xb + hwb[1] * S;
		/* a tier's flat underside: the edge jumps inward inside this row -> keep the row's outer edge (no
		 * smeared ramp), so the boughs' undersides stay crisp */
		if (fabsf(l1 - l0) > 3.0f) l0 = l1 = fminf(l0, l1);
		if (fabsf(r1 - r0) > 3.0f) r0 = r1 = fmaxf(r0, r1);
		if (tm < 0.0f && !cr)
			continue;
		span_aa(c, py, l0, l1, r0, r1, v, mat);
		if (sv > 0.0f && snowk > 0.02f) {
			for (int sd = 0; sd < 2; sd++) {
				float ia = ina[sd] * S, ib = inb[sd] * S, oa = hwa[sd] * S, ob = hwb[sd] * S;
				if (oa - ia < 0.3f && ob - ib < 0.3f)
					continue;
				float vv = sv * (sd ? 1.0f : 0.62f);
				if (fabsf(ob - oa) > 3.0f) oa = ob = fmaxf(oa, ob);
				if (fabsf(ib - ia) > 3.0f) ia = ib = fmaxf(ia, ib);
				if (sd == 0)
					span_aa(c, py, xa - oa, xb - ob, xa - ia, xb - ib, vv, smat);
				else
					span_aa(c, py, xa + ia, xb + ib, xa + oa, xb + ob, vv, smat);
			}
		}
	}
	/* trunk below the foliage */
	float tw = f->w * 0.07f + (cr ? 0.5f / S : 0.0f);
	float ty0 = ytop + hf * 0.96f, ty1 = f->base;
	int q0 = (int)floorf(ty0 * S), q1 = (int)ceilf(ty1 * S);
	if (q0 < 0) q0 = 0;
	if (q1 > c->h) q1 = c->h;
	for (int py = q0; py < q1; py++)
		span_aa(c, py, (x0 - tw) * S, (x0 - tw) * S, (x0 + tw) * S, (x0 + tw) * S, trunkv, US_MAT_STEM);
}

/* where a bough's tip is (for shedding snow), normalized */
static void fir_tip(const fir_t *f, float A, int k, int sd, float *x, float *y)
{
	tiers_t T;
	fir_tiers(f, 1.0f, &T);
	float ytop = f->base - f->h, hf = f->h * 0.9f;
	float a = T.a[k][sd], b = T.b[k][sd];
	float t = a + 0.75f * (b - a);
	float hw = T.E[k][sd] * bough(0.75f);
	*x = f->x * A + (sd ? hw : -hw);
	*y = ytop + t * hf;
}

static void fir_init(st_t *s, fir_t *f, float x, float base, float h, float w, int nt, int layer)
{
	us_rng *r = &s->rng;
	f->x = x;
	f->base = base;
	f->h = h;
	f->w = w;
	f->nt = nt > MAXT ? MAXT : nt;
	f->layer = layer;
	f->ph = us_rand(r) * US_TAU;
	for (int k = 0; k < MAXT; k++)
		for (int sd = 0; sd < 2; sd++) {
			f->len[k][sd] = us_randr(r, 0.78f, 1.14f);
			f->load[k][sd] = us_randr(r, 0.7f, 1.0f);
			f->sp[k][sd] = f->spv[k][sd] = 0.0f;
		}
}

/* ------------------------------------------------------------------ create */

static void new_flake(us_rng *r, flake_t *f, int depth, int anywhere)
{
	f->x = us_rand(r);
	float ymax = depth == 0 ? SKY_Y1 : 1.05f;
	f->y = anywhere ? us_randr(r, -0.02f, ymax) : us_randr(r, -0.06f, -0.01f);
	f->ph = us_rand(r) * US_TAU;
	if (depth == 0) {
		f->r = us_randr(r, 0.0008f, 0.0013f);
		f->vy = us_randr(r, 0.022f, 0.04f);
		f->v = us_randr(r, 0.32f, 0.5f);
	} else if (depth == 1) {
		f->r = us_randr(r, 0.0018f, 0.003f);
		f->vy = us_randr(r, 0.045f, 0.075f);
		f->v = us_randr(r, 0.5f, 0.75f);
	} else {
		f->r = us_randr(r, 0.0042f, 0.0085f);
		f->vy = us_randr(r, 0.09f, 0.15f);
		f->v = us_randr(r, 0.45f, 0.7f);
	}
	f->gl = 0.0f;
}

/* a drift's crest: a long low swell across part of the clearing */
static float crest_y(const drift_t *d, float xf, float *ext)
{
	float s = (xf - d->x0) / (d->x1 - d->x0);
	if (s <= 0.0f || s >= 1.0f) {
		*ext = 0.0f;
		return 0.0f;
	}
	float yd = us_lerp(HORIZ + 0.02f, 0.97f, d->z * d->z);
	float bul = (0.004f + 0.03f * d->z) * sinf(US_PI * s);
	*ext = sqrtf(sinf(US_PI * s));
	return yd - bul + (0.002f + 0.01f * d->z) * us_noise2(xf * d->f * 3.0f + d->ph, 1.3f);
}

static int cmp_base(const void *a, const void *b)
{
	const fir_t *x = a, *y = b;
	if (x->layer != y->layer)
		return x->layer - y->layer;
	return x->base < y->base ? -1 : x->base > y->base;
}

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	us_rng *r = &s->rng;
	us_rng_seed(r, seed ^ 0x736e6f77u);
	s->cab_x = us_randr(r, 0.27f, 0.33f);
	/* far stands: four receding layers, smaller and hazier with distance */
	static const int cnt[NLAYER] = {64, 50, 36, 26};
	static const float base[NLAYER] = {0.585f, 0.612f, 0.64f, 0.672f};
	static const float h0[NLAYER] = {0.045f, 0.065f, 0.09f, 0.13f}, h1[NLAYER] = {0.085f, 0.115f, 0.16f, 0.235f};
	int n = 0;
	for (int L = 0; L < NLAYER; L++)
		for (int i = 0; i < cnt[L] && n < MAX_TREES - 3; i++) {
			float x = (i + us_randr(r, 0.0f, 1.0f)) / cnt[L] * 1.08f - 0.04f;
			if (L == 3 && fabsf(x - (s->cab_x + 0.06f)) < 0.03f && us_rand(r) < 0.6f)
				continue;
			float stand = 0.5f + 0.5f * us_noise2(x * 5.0f + L * 7.1f, 3.3f);
			float h = us_lerp(h0[L], h1[L], 0.35f * us_rand(r) + 0.65f * stand);
			fir_t *f = &s->tr[n++];
			fir_init(s, f, x, base[L] + us_randr(r, -0.006f, 0.006f), h, h * us_randr(r, 0.2f, 0.27f),
				 L < 2 ? 6 : (L == 2 ? 7 : 9), L);
		}
	qsort(s->tr, n, sizeof(fir_t), cmp_base);
	/* the lone fir in the clearing, and the two big frame firs */
	s->lone = n;
	fir_init(s, &s->tr[n++], us_randr(r, 0.62f, 0.66f), 0.80f, 0.36f, 0.075f, 9, 4);
	s->nearL = n;
	fir_init(s, &s->tr[n++], 0.015f, 1.06f, 1.0f, 0.19f, 12, 5);
	s->nearR = n;
	fir_init(s, &s->tr[n++], 0.985f, 1.1f, 0.86f, 0.18f, 11, 5);
	s->ntr = n;
	for (int i = 0; i < MAX_FAR; i++)
		new_flake(r, &s->ff[i], 0, 1);
	for (int i = 0; i < MAX_MID; i++)
		new_flake(r, &s->fm[i], 1, 1);
	for (int i = 0; i < MAX_NEAR; i++)
		new_flake(r, &s->fn[i], 2, 1);
	for (int i = 0; i < MAX_CRYS; i++) {
		crys_t *k = &s->cr[i];
		k->x = us_rand(r);
		float z = us_rand(r);
		k->y = us_lerp(HORIZ + 0.01f, 0.995f, z * z);
		k->m = us_randr(r, 0.5f, 1.0f);
	}
	for (int i = 0; i < MAX_DRIFT; i++) {
		drift_t *d = &s->dr[i];
		d->z = (i + us_randr(r, 0.1f, 0.9f)) / MAX_DRIFT;
		float c0 = us_randr(r, 0.1f, 0.9f), span = us_randr(r, 0.25f, 0.5f) * (0.6f + 0.8f * d->z);
		d->x0 = c0 - span * 0.5f;
		d->x1 = c0 + span * 0.5f;
		d->ph = us_randr(r, 0, 50);
		d->f = us_randr(r, 1.5f, 3.0f);
	}
	for (int i = 0; i < MAX_PRINT; i++)
		s->pr[i].age = 999.0f;
	s->next_deer = us_randr(r, 8.0f, 20.0f);
	s->owl_head = 0.0f;
	s->owl_look_t = us_randr(r, 2, 6);
	s->gust_dir = 1.0f;
	s->T = us_rand(r) * 50.0f;
	/* the fox is usually about when the scene opens */
	us_rng_seed(&s->lrng, seed ^ 0x666f7865u);
	r = &s->lrng;
	s->next_fox = us_randr(r, 0.0f, 3.0f);
	s->fox_tilt_to = 0.12f;
	for (int i = 0; i < MAX_HARE; i++) {
		hare_t *h = &s->hr[i];
		h->x = us_randr(r, 0.12f, 0.88f);
		h->y = us_randr(r, 0.715f, 0.775f);
		h->d = us_rand(r) < 0.5f ? 1.0f : -1.0f;
		h->ht = -1.0f;
		h->next = us_randr(r, 2.0f, 8.0f);
	}
	for (int i = 0; i < MAX_BIRD; i++) {
		s->bd[i].state = 3;
		s->bd[i].away = i < 4 ? -1.0f : us_randr(r, 4.0f, 12.0f); /* most start perched */
		s->bd[i].look = us_rand(r) < 0.5f ? 1.0f : -1.0f;
	}
	for (int i = 0; i < MAX_GRASS; i++) {
		grass_t *g = &s->gr[i];
		g->x = us_randr(r, 0.13f, 0.87f);
		g->y = us_randr(r, 0.935f, 0.995f);
		g->h = us_randr(r, 0.035f, 0.07f) * (0.7f + 0.6f * (g->y - 0.93f) / 0.07f);
		g->lean = us_randr(r, -0.25f, 0.25f);
		g->ph = us_randr(r, 0.0f, US_TAU);
		g->n = 4 + (int)(us_rand(r) * 5.0f);
	}
	return s;
}

static void destroy(void *p) { free(p); }

/* ------------------------------------------------------------------ particles */

static part_t *spawn(st_t *s)
{
	part_t *p = &s->pt[s->pti];
	s->pti = (s->pti + 1) % MAX_PART;
	p->floor = 1.1f;
	return p;
}

/* a bough sheds its snow: a clump drops from the tip and breaks into powder */
static void shed(st_t *s, fir_t *f, int k, int sd, float A, float amt)
{
	us_rng *r = &s->rng;
	float x, y;
	fir_tip(f, A, k, sd, &x, &y);
	float load = f->load[k][sd];
	f->load[k][sd] = load * 0.3f;
	f->spv[k][sd] += 0.18f + 0.25f * load * amt;
	float scale = f->h; /* bigger trees, bigger clumps */
	float floor_y = f->layer == 4 ? f->base + 0.004f : 1.1f; /* the lone fir's snow lands at its foot */
	int first = s->pti;
	int nc = (int)(22 + 40 * load * amt);
	for (int i = 0; i < nc; i++) {
		part_t *p = spawn(s);
		float u = us_rand(r);
		p->x = x + (sd ? -1.0f : 1.0f) * u * f->w * 0.5f * (0.4f + 0.6f * scale);
		p->y = y - us_randr(r, 0.0f, 0.012f) * scale;
		p->vx = us_randn(r) * 0.03f * scale + (sd ? 0.02f : -0.02f);
		p->vy = us_randr(r, 0.2f, 0.45f) * (0.5f + 0.5f * scale);
		p->r = us_randr(r, 0.0016f, 0.0042f) * (0.5f + 0.5f * scale);
		p->v = us_randr(r, 0.6f, 0.9f);
		p->max = p->life = us_randr(r, 0.7f, 1.4f);
		p->drag = 0.6f;
		p->kind = 0;
	}
	/* the soft cloud of the slump: a few big faint discs that swell and fade */
	for (int i = 0; i < 5; i++) {
		part_t *p = spawn(s);
		p->x = x + (sd ? -1.0f : 1.0f) * us_randr(r, 0.0f, 0.4f) * f->w * scale + us_randn(r) * 0.01f;
		p->y = y + us_randr(r, -0.01f, 0.03f) * scale;
		p->vx = us_randn(r) * 0.02f;
		p->vy = us_randr(r, 0.01f, 0.05f);
		p->r = us_randr(r, 0.035f, 0.065f) * (0.5f + 0.7f * scale);
		p->v = us_randr(r, 0.3f, 0.42f) * (0.6f + 0.4f * load);
		p->max = p->life = us_randr(r, 0.22f, 0.38f);
		p->drag = 3.0f;
		p->kind = 3;
	}
	int np = (int)(30 + 40 * load * amt);
	for (int i = 0; i < np; i++) {
		part_t *p = spawn(s);
		float a = us_rand(r) * US_TAU, sp = us_randr(r, 0.02f, 0.12f) * (0.5f + 0.5f * scale);
		p->x = x + us_randn(r) * 0.01f * scale;
		p->y = y + us_randn(r) * 0.006f * scale;
		p->vx = cosf(a) * sp;
		p->vy = sinf(a) * sp * 0.6f + 0.02f;
		p->r = us_randr(r, 0.0008f, 0.0018f);
		p->v = us_randr(r, 0.4f, 0.7f);
		p->max = p->life = us_randr(r, 0.6f, 1.6f);
		p->drag = 2.5f;
		p->kind = 1;
	}
	for (int i = first; i != s->pti; i = (i + 1) % MAX_PART)
		s->pt[i].floor = floor_y;
}

/* a gust whirls a flurry of powder across the clearing */
static void flurry(st_t *s, float A, float amt, float dir)
{
	us_rng *r = &s->rng;
	float cx = us_randr(r, 0.2f, 0.8f) * A, cy = us_randr(r, 0.70f, 0.9f);
	float rad = us_randr(r, 0.06f, 0.12f);
	int n = (int)(130 + 220 * amt);
	float U = dir * us_randr(r, 0.5f, 0.8f);
	for (int i = 0; i < 6; i++) { /* the gust's veil of blown powder */
		part_t *p = spawn(s);
		p->x = cx + us_randr(r, -1.5f, 1.5f) * rad;
		p->y = cy + us_randn(r) * rad * 0.25f;
		p->vx = U * us_randr(r, 0.7f, 1.1f);
		p->vy = -0.02f;
		p->r = us_randr(r, 0.03f, 0.055f);
		p->v = us_randr(r, 0.22f, 0.32f) * (0.6f + 0.6f * amt);
		p->max = p->life = us_randr(r, 0.25f, 0.45f);
		p->drag = 0.8f;
		p->kind = 3;
	}
	for (int i = 0; i < n; i++) {
		part_t *p = spawn(s);
		float a = us_rand(r) * US_TAU, rr = rad * sqrtf(us_rand(r));
		p->x = cx + cosf(a) * rr * 1.8f;
		p->y = cy + sinf(a) * rr * 0.55f;
		float sw = 2.4f * dir; /* swirl */
		p->vx = U * us_randr(r, 0.6f, 1.3f) - sw * (p->y - cy) * 1.6f;
		p->vy = sw * (p->x - cx) * 0.35f - us_randr(r, 0.0f, 0.1f);
		p->r = us_randr(r, 0.0015f, 0.004f);
		p->v = us_randr(r, 0.55f, 0.95f);
		p->max = p->life = us_randr(r, 0.3f, 0.6f);
		p->drag = 1.2f;
		p->kind = 2;
	}
}

static void parts_step_draw(st_t *s, us_canvas *c, float dt, float A, float wind)
{
	for (int i = 0; i < MAX_PART; i++) {
		part_t *p = &s->pt[i];
		if (p->life <= 0.0f)
			continue;
		p->life -= dt;
		float k = expf(-p->drag * dt);
		p->vx = p->vx * k + wind * (1.0f - k);
		p->vy = p->vy * k + (p->kind == 0 ? 0.5f : (p->kind == 1 ? 0.02f : (p->kind == 3 ? 0.0f : 0.03f))) * dt * (p->kind == 0 ? 1.0f : 30.0f) * (1.0f - k) +
			(p->kind == 0 ? 1.1f * dt : 0.0f);
		p->x += p->vx * dt;
		p->y += p->vy * dt;
		if (p->y > 1.04f || p->x < -0.1f || p->x > A + 0.1f) {
			p->life = 0.0f;
			continue;
		}
		if (p->y > p->floor) { /* landed: settle into the snow quickly */
			p->y = p->floor;
			p->vy = 0.0f;
			p->vx *= 0.5f;
			if (p->life > 0.12f)
				p->life = 0.12f;
		}
		float f = us_sat(p->life / p->max);
		float fade = f < 0.4f ? f / 0.4f : 1.0f;
		float rr = p->r;
		float v = p->v * fade;
		if (p->kind == 3) { /* soft cloud: swells as it fades */
			float sw = rr * (0.35f + 1.3f * sqrtf(1.0f - f));
			float vv = p->v * f * f;
			if (c->crisp)
				us_disc(c, p->x, p->y, sw * 0.75f, vv * 1.6f, US_MAT_WATER);
			else {
				us_disc(c, p->x, p->y, sw, vv * 0.4f, US_MAT_WATER);
				us_disc(c, p->x, p->y, sw * 0.72f, vv * 0.6f, US_MAT_WATER);
				us_disc(c, p->x, p->y, sw * 0.45f, vv * 0.8f, US_MAT_WATER);
			}
			continue;
		}
		if (c->crisp)
			us_disc(c, p->x, p->y, rr > 0.004f ? rr : 0.0025f, v, p->kind == 0 ? US_MAT_ACCENT : US_MAT_WATER);
		else
			us_disc(c, p->x, p->y, rr, v, p->kind == 1 && v < 0.35f ? US_MAT_WATER : US_MAT_ACCENT);
	}
}

/* ------------------------------------------------------------------ cabin */

static void cabin(st_t *s, const us_ctx *ctx, us_canvas *c, float gy, float warm, float day, float snowk)
{
	float A = ctx->aspect, cx = s->cab_x * A, u = 0.125f;
	int cr = ctx->crisp;
	float wallv = cr ? 0.22f : 0.12f, logv = cr ? 0.4f : 0.2f, darkv = cr ? 0.0f : 0.035f;
	float snv = (cr ? 0.75f : 0.7f) * (0.85f + 0.15f * day);
	float L = cx - 0.5f * u, R = cx + 0.5f * u, top = gy - 0.4f * u, ridge = top - 0.36f * u;
	float eL = cx - 0.64f * u, eR = cx + 0.64f * u, ey = top + 0.03f * u;
	/* chimney behind the right slope */
	float chx = cx + 0.27f * u, chw = 0.055f * u;
	{
		float p[8] = {chx - chw, ridge + 0.06f * u, chx + chw, ridge + 0.06f * u, chx + chw, top, chx - chw, top};
		poly_set(c, p, 4, logv * 0.9f, US_MAT_EARTH);
		float sc[8] = {chx - chw * 1.35f, ridge + 0.065f * u, chx + chw * 1.35f, ridge + 0.065f * u, chx + chw * 1.1f,
			       ridge + 0.03f * u, chx - chw * 1.1f, ridge + 0.03f * u};
		poly_set(c, sc, 4, snv * 0.9f, US_MAT_ACCENT);
	}
	/* walls (log front with a gable) */
	{
		float p[10] = {L, gy + 0.01f, L, top, cx, ridge + 0.05f * u, R, top, R, gy + 0.01f};
		poly_set(c, p, 5, wallv, US_MAT_EARTH);
	}
	/* log courses */
	for (int k = 1; k < 7; k++) {
		float y = gy - 0.4f * u * k / 7.0f;
		if (cr)
			us_line(c, L, y, R, y, 0.003f, logv, US_MAT_EARTH);
		else
			us_line(c, L, y, R, y, 0.0011f, logv, US_MAT_EARTH);
	}
	/* log ends at the corners */
	for (int k = 0; k < 7 && !cr; k++) {
		float y = gy - 0.4f * u * (k + 0.5f) / 7.0f;
		us_disc(c, L - 0.004f, y, 0.0026f, logv * 1.1f, US_MAT_EARTH);
		us_disc(c, R + 0.004f, y, 0.0026f, logv * 1.1f, US_MAT_EARTH);
	}
	/* roof: dark eaves, then a deep snow blanket along both slopes, overhanging at the eaves */
	{
		float rf[12] = {eL, ey, cx, ridge, eR, ey, eR, ey + 0.035f * u, cx, ridge + 0.07f * u, eL, ey + 0.035f * u};
		poly_set(c, rf, 6, darkv + (cr ? 0.12f : 0.03f), US_MAT_EARTH);
		float th = (0.05f + 0.09f * snowk) * u;
		float sn[2 * 12];
		int n = 0;
		/* upper edge: rounded crest */
		sn[n++] = eL - 0.02f * u; sn[n++] = ey - th * 0.2f + 0.012f * u;
		sn[n++] = eL + 0.01f * u; sn[n++] = ey - th * 0.95f;
		sn[n++] = cx - 0.02f * u; sn[n++] = ridge - th;
		sn[n++] = cx + 0.02f * u; sn[n++] = ridge - th;
		sn[n++] = eR - 0.01f * u; sn[n++] = ey - th * 0.95f;
		sn[n++] = eR + 0.02f * u; sn[n++] = ey - th * 0.2f + 0.012f * u;
		/* lower edge: the roofline, with the overhang curling down at the eaves */
		sn[n++] = eR + 0.005f * u; sn[n++] = ey + 0.02f * u;
		sn[n++] = eR - 0.04f * u; sn[n++] = ey - 0.005f * u;
		sn[n++] = cx; sn[n++] = ridge + 0.005f * u;
		sn[n++] = eL + 0.04f * u; sn[n++] = ey - 0.005f * u;
		sn[n++] = eL - 0.005f * u; sn[n++] = ey + 0.02f * u;
		poly_set(c, sn, n / 2, snv, US_MAT_ACCENT);
		/* icicles under the eaves */
		for (int k = 0; k < 9 && !cr; k++) {
			float t = (k + 0.5f) / 9.0f;
			float x = us_lerp(eL + 0.01f * u, cx - 0.06f * u, t);
			float y = us_lerp(ey + 0.03f * u, ridge + 0.08f * u, t);
			float len = (0.012f + 0.02f * fabsf(sinf(k * 2.7f))) * u * 3.0f;
			us_line(c, x, y, x, y + len, 0.0009f, 0.35f, US_MAT_WATER);
			float x2 = 2.0f * cx - x;
			us_line(c, x2, y, x2, y + len * 0.8f, 0.0009f, 0.35f, US_MAT_WATER);
		}
	}
	/* windows: warm, flickering with the hearth */
	float flick = 0.88f + 0.12f * us_noise2((float)ctx->t * 2.3f, 7.7f) + 0.06f * us_noise2((float)ctx->t * 7.1f, 1.3f);
	float wv = us_sat((0.72f + 0.28f * warm) * flick * (1.0f - 0.45f * us_smooth(0.35f, 0.85f, day)));
	float wx = cx - 0.2f * u, wy = gy - 0.21f * u, ww = 0.085f * u, wh = 0.075f * u;
	/* warm spill on the logs around the window */
	if (!cr)
		us_disc(c, wx, wy, 0.17f * u, wv * 0.14f, US_MAT_LIGHT);
	{
		float p[8] = {wx - ww, wy - wh, wx + ww, wy - wh, wx + ww, wy + wh, wx - ww, wy + wh};
		poly_set(c, p, 4, wv, US_MAT_LIGHT);
		/* mullions */
		float mv = cr ? 0.25f : 0.1f;
		line_(c, wx, wy - wh, wx, wy + wh, 0.0012f, 0.0f, US_MAT_EARTH);
		{
			float m1[8] = {wx - 0.006f * u, wy - wh, wx + 0.006f * u, wy - wh, wx + 0.006f * u, wy + wh, wx - 0.006f * u, wy + wh};
			float m2[8] = {wx - ww, wy - 0.006f * u, wx + ww, wy - 0.006f * u, wx + ww, wy + 0.006f * u, wx - ww, wy + 0.006f * u};
			if (!cr) {
				poly_set(c, m1, 4, mv, US_MAT_EARTH);
				poly_set(c, m2, 4, mv, US_MAT_EARTH);
			}
		}
		/* snow on the sill */
		float sl[8] = {wx - ww * 1.25f, wy + wh, wx + ww * 1.25f, wy + wh, wx + ww * 1.15f, wy + wh + 0.025f * u, wx - ww * 1.15f,
			       wy + wh + 0.025f * u};
		poly_set(c, sl, 4, snv * 0.95f, US_MAT_ACCENT);
	}
	/* gable window: a small round glow */
	{
		float gx = cx, gyy = ridge + 0.17f * u;
		ellipse_set(c, gx, gyy, 0.045f * u, 0.045f * u, 0.0f, wv * 0.85f, US_MAT_LIGHT);
	}
	/* door: dark planks, a warm crack of light */
	{
		float dx = cx + 0.2f * u, dw = 0.07f * u, dh = 0.25f * u;
		float p[8] = {dx - dw, gy + 0.01f, dx - dw, gy - dh, dx + dw, gy - dh, dx + dw, gy + 0.01f};
		poly_set(c, p, 4, cr ? 0.12f : 0.06f, US_MAT_EARTH);
		line_(c, dx + dw * 0.75f, gy - dh * 0.95f, dx + dw * 0.75f, gy, 0.0012f, wv * 0.7f, US_MAT_LIGHT);
	}
	/* snow drifted against the walls */
	{
		float p[2 * 9];
		for (int i = 0; i < 9; i++) {
			float t = i / 8.0f;
			p[2 * i] = us_lerp(L - 0.05f * u, R + 0.05f * u, t);
			p[2 * i + 1] = gy - (0.035f + 0.02f * sinf(t * 9.0f + 1.0f)) * u * (0.5f + snowk) * sinf(US_PI * t) + 0.002f;
		}
		float q[2 * 11];
		memcpy(q, p, sizeof(p));
		q[18] = R + 0.05f * u;
		q[19] = gy + 0.012f;
		q[20] = L - 0.05f * u;
		q[21] = gy + 0.012f;
		poly_set(c, q, 11, (cr ? 0.42f : 0.3f), US_MAT_WATER);
	}
	/* smoke: soft puffs rising from the chimney, leaning downwind and curling */
	{
		float sx = chx, sy = ridge + 0.03f * u;
		float lean = 0.15f + 0.6f * ctx->w->wind;
		float tt = (float)ctx->t;
		for (int k = 0; k < 16; k++) {
			float ph = (k + fmodf(tt * 0.5f, 1.0f)) / 16.0f; /* puffs travel up the plume */
			float y = sy - 0.17f * ph;
			float x = sx + lean * 0.09f * ph * ph + 0.012f * ph * us_noise2(ph * 3.0f - tt * 0.3f, 4.0f);
			float rr = 0.004f + 0.016f * ph;
			float v = (0.2f + 0.08f * warm) * (1.0f - ph) * us_smooth(0.0f, 0.12f, ph);
			if (cr)
				us_disc(c, x, y, rr, v * 1.4f, US_MAT_WATER);
			else
				us_disc(c, x, y, rr, v * 0.55f, US_MAT_WATER);
		}
	}
}

/* ------------------------------------------------------------------ deer */

/* a tapered segment, filled (occluding): legs, neck, muzzle */
static void seg_set(us_canvas *c, float ax, float ay, float bx, float by, float w0, float w1, float v, uint8_t m)
{
	float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy);
	if (l < 1e-6f)
		return;
	float nx = -dy / l, ny = dx / l;
	if (c->crisp) { /* keep thin limbs at least a pixel wide */
		float mn = 0.55f / c->s;
		if (w0 < mn) w0 = mn;
		if (w1 < mn) w1 = mn;
	}
	float p[8] = {ax + nx * w0, ay + ny * w0, bx + nx * w1, by + ny * w1, bx - nx * w1, by - ny * w1, ax - nx * w0, ay - ny * w0};
	poly_set(c, p, 4, v, m);
}

static void deer_draw(st_t *s, us_canvas *c, float A, float day)
{
	int cr = c->crisp;
	float d = s->deer_dir;
	float sc = 0.046f * (0.75f + 1.3f * (s->deer_y - 0.69f) / 0.08f); /* shoulder height, nearer = bigger */
	float x = s->deer_x * A, gy = s->deer_y;
	float v = cr ? 0.16f : 0.11f, rimv = (cr ? 0.5f : 0.34f) * (0.8f + 0.2f * day);
	float walk = s->deer_mode == 0 ? 1.0f : 0.0f;
	float ph = s->deer_ph;
	float by = gy - 0.74f * sc + 0.012f * sc * sinf(ph * 2.0f) * walk; /* body centre */
	/* soft blue shadow on the snow */
	dab_min(c, x, gy + 0.002f, 0.5f * sc, 0.06f * sc, cr ? 0.22f : 0.12f);
	/* legs: front straight, hind with a hock; diagonal pairs swing in a walk (far pair dimmer, drawn first) */
	for (int l = 0; l < 4; l++) {
		int front = l < 2, far = (l & 1) == 0;
		float sw = 0.16f * sc * sinf(ph + ((l == 0 || l == 3) ? 0.0f : US_PI)) * walk;
		float hx = x + d * (front ? 0.3f : -0.3f) * sc + (far ? d * 0.04f * sc : 0.0f);
		float hy = by + 0.06f * sc;
		float lv = far ? v * 0.75f : v;
		if (front) {
			float kx = hx + sw * 0.4f, ky = by + 0.42f * sc;
			seg_set(c, hx, hy, kx, ky, 0.07f * sc, 0.035f * sc, lv, US_MAT_EARTH);
			seg_set(c, kx, ky, hx + sw, gy, 0.03f * sc, 0.022f * sc, lv, US_MAT_EARTH);
		} else {
			float kx = hx - d * 0.08f * sc + sw * 0.4f, ky = by + 0.38f * sc; /* hock, set back */
			seg_set(c, hx, hy - 0.04f * sc, kx, ky, 0.1f * sc, 0.035f * sc, lv, US_MAT_EARTH);
			seg_set(c, kx, ky, hx + sw, gy, 0.03f * sc, 0.022f * sc, lv, US_MAT_EARTH);
		}
	}
	/* body: barrel, chest, rump */
	ellipse_set(c, x, by, 0.42f * sc, 0.17f * sc, 0.0f, v, US_MAT_EARTH);
	ellipse_set(c, x + d * 0.24f * sc, by - 0.01f * sc, 0.17f * sc, 0.19f * sc, 0.0f, v, US_MAT_EARTH);
	ellipse_set(c, x - d * 0.26f * sc, by - 0.02f * sc, 0.18f * sc, 0.18f * sc, 0.0f, v, US_MAT_EARTH);
	/* neck + head: up and alert, or lowered to graze */
	float hd = s->deer_head; /* 0 up .. 1 down */
	float nx0 = x + d * 0.3f * sc, ny0 = by - 0.08f * sc;
	float hx = x + d * us_lerp(0.5f, 0.66f, hd) * sc, hy = us_lerp(by - 0.55f * sc, gy - 0.1f * sc, hd);
	seg_set(c, nx0, ny0, hx, hy, 0.12f * sc, 0.06f * sc, v, US_MAT_EARTH);
	float ma = us_lerp(0.45f, 1.35f, hd); /* muzzle angle below the horizontal */
	float mx = hx + d * cosf(ma) * 0.26f * sc, my = hy + sinf(ma) * 0.26f * sc;
	seg_set(c, hx, hy, mx, my, 0.075f * sc, 0.03f * sc, v, US_MAT_EARTH);
	/* ears, angled back */
	for (int e = 0; e < 2; e++) {
		float ex = hx - d * (0.02f + 0.04f * e) * sc, ey = hy - 0.03f * sc;
		seg_set(c, ex, ey, ex - d * 0.13f * sc, ey - (0.12f - 0.03f * e) * sc, 0.035f * sc, 0.012f * sc, e ? v * 0.8f : v, US_MAT_EARTH);
	}
	/* antlers on a stag */
	if (s->deer_stag) {
		float ax = hx - d * 0.02f * sc, ay = hy - 0.05f * sc;
		float pts[][4] = {{0, 0, -0.08f, -0.24f}, {-0.08f, -0.24f, -0.04f, -0.42f}, {-0.05f, -0.15f, 0.06f, -0.24f},
				  {-0.07f, -0.3f, -0.2f, -0.36f}, {-0.04f, -0.42f, 0.04f, -0.5f}};
		for (int k = 0; k < 5; k++)
			seg_set(c, ax + d * pts[k][0] * sc, ay + pts[k][1] * sc, ax + d * pts[k][2] * sc, ay + pts[k][3] * sc, 0.018f * sc,
				0.012f * sc, v * 1.3f, US_MAT_EARTH);
	}
	/* white tail flag */
	us_disc(c, x - d * 0.44f * sc, by - 0.1f * sc, (cr ? 0.06f : 0.045f) * sc, cr ? 0.55f : 0.45f, US_MAT_ACCENT);
	/* moonlight along the back and neck */
	if (!cr) {
		float p[2 * 8];
		for (int i = 0; i < 7; i++) {
			float t = (float)i / 6.0f;
			p[2 * i] = x + d * (-0.42f + 0.72f * t) * sc;
			p[2 * i + 1] = by - (0.15f + 0.06f * sinf(US_PI * t)) * sc;
		}
		p[14] = hx;
		p[15] = hy - 0.05f * sc;
		us_polyline(c, p, 8, 0.0008f, rimv, US_MAT_WATER);
	}
}

static void deer_step(st_t *s, const us_ctx *ctx, float dens)
{
	us_rng *r = &s->rng;
	float dt = ctx->dt;
	if (!s->deer_on) {
		s->next_deer -= dt * (0.6f + 0.8f * dens + 0.6f * ctx->w->calm);
		if (s->next_deer > 0.0f)
			return;
		s->deer_on = 1;
		s->deer_dir = us_rand(r) < 0.5f ? 1.0f : -1.0f;
		s->deer_x = s->deer_dir > 0 ? -0.05f : 1.05f;
		s->deer_y = us_randr(r, 0.705f, 0.745f);
		s->deer_mode = 0;
		s->deer_t = us_randr(r, 6.0f, 14.0f);
		s->deer_head = s->deer_head_to = 0.0f;
		s->deer_stag = us_rand(r) < 0.5f;
		s->deer_step = 0.0f;
	}
	s->deer_t -= dt;
	if (s->deer_t <= 0.0f) {
		/* walk -> graze or look up -> walk ... */
		if (s->deer_mode == 0 && s->deer_x > 0.08f && s->deer_x < 0.92f) {
			s->deer_mode = us_rand(r) < 0.65f ? 1 : 2;
			s->deer_t = us_randr(r, 4.0f, 9.0f);
		} else {
			s->deer_mode = 0;
			s->deer_t = us_randr(r, 5.0f, 12.0f);
		}
	}
	s->deer_head_to = s->deer_mode == 1 ? 1.0f : 0.0f;
	s->deer_head = us_approach(s->deer_head, s->deer_head_to, 0.6f, dt);
	if (s->deer_mode == 0) {
		float sp = 0.022f;
		s->deer_x += s->deer_dir * sp * dt;
		float oph = s->deer_ph;
		s->deer_ph += dt * 4.2f;
		/* a print each half stride, filling with snow over a minute or two */
		if (floorf(oph / US_PI) != floorf(s->deer_ph / US_PI)) {
			print_t *p = &s->pr[s->pri];
			s->pri = (s->pri + 1) % MAX_PRINT;
			p->x = s->deer_x + s->deer_dir * 0.004f * ((int)floorf(s->deer_ph / US_PI) & 1 ? 1.0f : -1.0f);
			p->y = s->deer_y + 0.0015f * (((int)floorf(s->deer_ph / US_PI) & 1) ? 1.0f : -1.0f);
			p->age = 0.0f;
			p->s = 0.7f + 1.6f * (s->deer_y - 0.69f) / 0.08f;
		}
	}
	if (s->deer_x < -0.08f || s->deer_x > 1.08f) {
		s->deer_on = 0;
		s->next_deer = us_randr(r, 40.0f, 100.0f);
	}
}

/* ------------------------------------------------------------------ owl */

static void owl_draw(us_canvas *c, float x, float y, float sc, int flying, float flap, float head, float blink, float dir, float day)
{
	int cr = c->crisp;
	float wv = cr ? 0.85f : 0.52f;
	float dark = cr ? 0.35f : 0.22f;
	if (!flying) {
		/* perched snowy owl: a soft egg of a body, round head, yellow eyes, dark flecks */
		float p[2 * 24];
		for (int i = 0; i < 24; i++) {
			float t = US_TAU * i / 24.0f, cy = cosf(t), sy = sinf(t);
			float wdt = 0.36f + 0.08f * sy; /* wider low down */
			p[2 * i] = x + cy * wdt * sc;
			p[2 * i + 1] = y - 0.58f * sc + sy * 0.58f * sc;
		}
		poly_set(c, p, 24, wv, US_MAT_ACCENT);
		float hx = x + head * 0.06f * sc, hy = y - 1.13f * sc;
		ellipse_set(c, hx, hy, 0.31f * sc, 0.27f * sc, 0.0f, wv, US_MAT_ACCENT);
		if (!cr) {
			/* folded wings: a soft edge down each side, flecked */
			for (int sd = -1; sd <= 1; sd += 2) {
				float px0 = x + sd * 0.2f * sc, py0 = y - 0.9f * sc;
				for (int k = 0; k < 6; k++) {
					float t = k / 5.0f;
					float fx = px0 + sd * 0.1f * sc * sinf(t * 2.5f), fy = py0 + t * 0.75f * sc;
					dab_min(c, fx, fy, 0.05f * sc, 0.02f * sc, 0.2f + 0.08f * (k & 1));
				}
			}
			for (int k = 0; k < 7; k++) {
				float fx = x + (((k * 37) % 7) / 6.0f - 0.5f) * 0.32f * sc;
				float fy = y - (0.18f + 0.07f * (k % 4)) * sc;
				dab_min(c, fx, fy, 0.045f * sc, 0.016f * sc, 0.26f);
			}
		}
		/* eyes (blink) and beak */
		float ex = hx + head * 0.1f * sc, ey = hy + 0.01f * sc;
		float er = cr ? 0.06f * sc : 0.085f * sc;
		if (blink < 0.5f) {
			if (!cr) {
				dab_min(c, ex - 0.12f * sc, ey, er * 1.5f, er * 1.4f, 0.12f);
				dab_min(c, ex + 0.12f * sc, ey, er * 1.5f, er * 1.4f, 0.12f);
			}
			ellipse_set(c, ex - 0.12f * sc, ey, er, er, 0.0f, 0.9f, US_MAT_LIGHT);
			ellipse_set(c, ex + 0.12f * sc, ey, er, er, 0.0f, 0.9f, US_MAT_LIGHT);
			if (!cr) {
				dab_min(c, ex - 0.12f * sc, ey, er * 0.45f, er * 0.45f, 0.02f);
				dab_min(c, ex + 0.12f * sc, ey, er * 0.45f, er * 0.45f, 0.02f);
			}
		} else if (!cr) {
			dab_min(c, ex - 0.12f * sc, ey, er, er * 0.25f, dark);
			dab_min(c, ex + 0.12f * sc, ey, er, er * 0.25f, dark);
		}
		if (!cr)
			dab_min(c, ex, ey + 0.1f * sc, 0.035f * sc, 0.05f * sc, dark * 0.6f);
		return;
	}
	/* flying, seen from the side: the far wing behind the body, the near wing in front, both beating */
	float bx = x, by = y - 0.5f * sc;
	for (int w = 0; w < 2; w++) { /* w = 0: far wing (dimmer, half a beat behind) */
		float th = sinf(flap + (w ? 0.0f : 0.5f)) * (w ? 1.0f : 0.8f); /* +1 up .. -1 down */
		float L = 1.15f * sc * (0.35f + 0.65f * fabsf(th)), sg = th >= 0.0f ? -1.0f : 1.0f;
		float rx = bx + dir * 0.12f * sc, ry = by - 0.06f * sc;
		float tx = bx - dir * 0.3f * sc, ty = ry + sg * L;
		float p[2 * 6] = {rx + dir * 0.12f * sc, ry,
				  rx + dir * 0.05f * sc + (tx - rx) * 0.3f, ry + (ty - ry) * 0.55f,
				  tx + dir * 0.06f * sc, ty,
				  tx - dir * 0.12f * sc, ty - sg * 0.06f * sc,
				  rx - dir * 0.3f * sc + (tx - rx) * 0.2f, ry + (ty - ry) * 0.45f,
				  rx - dir * 0.3f * sc, ry + 0.04f * sc};
		poly_set(c, p, 6, w ? wv : wv * 0.7f, US_MAT_ACCENT);
		if (w == 0) {
			ellipse_set(c, bx, by, 0.42f * sc, 0.17f * sc, dir * 0.08f, wv, US_MAT_ACCENT);
			ellipse_set(c, bx + dir * 0.42f * sc, by - 0.06f * sc, 0.19f * sc, 0.17f * sc, 0.0f, wv, US_MAT_ACCENT);
			us_disc(c, bx + dir * 0.5f * sc, by - 0.09f * sc, (c->crisp ? 0.05f : 0.06f) * sc, 0.9f, US_MAT_LIGHT);
		}
	}
	(void)day;
}

/* ------------------------------------------------------------------ small creatures: a local drawing frame */

/* a creature's drawing frame: local units of its size sc, x forward (mirrored by facing d), y down, pitched by
 * the frame's angle (+ = nose down); everything clipped below clip (the snow surface, when it dives in) */
typedef struct {
	us_canvas *c;
	float ox, oy, cs, sn, d, sc, clip;
} fx_t;

static void fx_init(fx_t *F, us_canvas *c, float ox, float oy, float pitch, float d, float sc, float clip)
{
	F->c = c;
	F->ox = ox;
	F->oy = oy;
	F->cs = cosf(pitch);
	F->sn = sinf(pitch);
	F->d = d;
	F->sc = sc;
	F->clip = clip;
}

static void fx_pt(const fx_t *F, float lx, float ly, float *x, float *y)
{
	*x = F->ox + F->d * (lx * F->cs - ly * F->sn) * F->sc;
	*y = F->oy + (lx * F->sn + ly * F->cs) * F->sc;
}

/* a world polygon, clipped below y = clip (Sutherland-Hodgman against one horizontal edge) */
static void poly_clip(us_canvas *c, const float *p, int n, float clip, float v, uint8_t m)
{
	float q[2 * 40];
	int k = 0;
	for (int i = 0; i < n && k < 38; i++) {
		int j = (i + 1) % n;
		float ax = p[2 * i], ay = p[2 * i + 1], bx = p[2 * j], by = p[2 * j + 1];
		int ain = ay <= clip, bin = by <= clip;
		if (ain) {
			q[2 * k] = ax;
			q[2 * k + 1] = ay;
			k++;
		}
		if (ain != bin) {
			float t = (clip - ay) / (by - ay);
			q[2 * k] = ax + (bx - ax) * t;
			q[2 * k + 1] = clip;
			k++;
		}
	}
	if (k >= 3)
		poly_set(c, q, k, v, m);
}

static void fx_poly(const fx_t *F, const float *lp, int n, float v, uint8_t m)
{
	float p[2 * 24];
	if (n > 24)
		n = 24;
	for (int i = 0; i < n; i++)
		fx_pt(F, lp[2 * i], lp[2 * i + 1], &p[2 * i], &p[2 * i + 1]);
	poly_clip(F->c, p, n, F->clip, v, m);
}

static void fx_ell(const fx_t *F, float lx, float ly, float rx, float ry, float rot, float v, uint8_t m)
{
	float lp[2 * 16], cr = cosf(rot), sr = sinf(rot);
	for (int i = 0; i < 16; i++) {
		float a = US_TAU * i / 16.0f, ex = cosf(a) * rx, ey = sinf(a) * ry;
		lp[2 * i] = lx + ex * cr - ey * sr;
		lp[2 * i + 1] = ly + ex * sr + ey * cr;
	}
	fx_poly(F, lp, 16, v, m);
}

/* a tapered limb between two WORLD points (keeps a pixel's width on crisp canvases), clipped */
static void limb(us_canvas *c, float ax, float ay, float bx, float by, float w0, float w1, float clip, float v, uint8_t m)
{
	float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy);
	if (l < 1e-6f)
		return;
	float nx = -dy / l, ny = dx / l;
	if (c->crisp) {
		float mn = 0.55f / c->s;
		if (w0 < mn) w0 = mn;
		if (w1 < mn) w1 = mn;
	}
	float p[8] = {ax + nx * w0, ay + ny * w0, bx + nx * w1, by + ny * w1, bx - nx * w1, by - ny * w1, ax - nx * w0, ay - ny * w0};
	poly_clip(c, p, 4, clip, v, m);
}

static void fx_seg(const fx_t *F, float ax, float ay, float bx, float by, float w0, float w1, float v, uint8_t m)
{
	float x0, y0, x1, y1;
	fx_pt(F, ax, ay, &x0, &y0);
	fx_pt(F, bx, by, &x1, &y1);
	limb(F->c, x0, y0, x1, y1, w0 * F->sc, w1 * F->sc, F->clip, v, m);
}

/* ------------------------------------------------------------------ red fox, mousing */

static inline float fox_sc(float y) { return 0.25f * (y - HORIZ); } /* shoulder height: nearer = bigger */

/* a powder burst where something hits the snow */
static void burst(st_t *s, float x, float y, float sc, float amt)
{
	us_rng *r = &s->lrng;
	int first = s->pti;
	int nc = (int)(16 + 26 * amt);
	for (int i = 0; i < nc; i++) { /* clods thrown up, falling back */
		part_t *p = spawn(s);
		p->x = x + us_randn(r) * 0.15f * sc;
		p->y = y - us_randr(r, 0.0f, 0.1f) * sc;
		p->vx = us_randn(r) * 0.12f * (0.5f + sc * 12.0f);
		p->vy = -us_randr(r, 0.12f, 0.35f) * (0.6f + sc * 10.0f);
		p->r = us_randr(r, 0.0012f, 0.003f) * (0.6f + sc * 12.0f);
		p->v = us_randr(r, 0.6f, 0.9f);
		p->max = p->life = us_randr(r, 0.5f, 0.9f);
		p->drag = 0.7f;
		p->kind = 0;
	}
	for (int i = 0; i < 3; i++) { /* the soft puff */
		part_t *p = spawn(s);
		p->x = x + us_randn(r) * 0.3f * sc;
		p->y = y - us_randr(r, 0.1f, 0.4f) * sc;
		p->vx = us_randn(r) * 0.02f;
		p->vy = -0.03f;
		p->r = us_randr(r, 0.6f, 1.0f) * sc;
		p->v = us_randr(r, 0.3f, 0.42f) * amt;
		p->max = p->life = us_randr(r, 0.3f, 0.5f);
		p->drag = 3.0f;
		p->kind = 3;
	}
	for (int i = first; i != s->pti; i = (i + 1) % MAX_PART)
		s->pt[i].floor = y + 0.002f;
}

static void fox_trot_to(st_t *s, float tgt)
{
	s->fox_mode = 0;
	s->fox_tgt = tgt;
	s->fox_dir = tgt >= s->fox_x ? 1.0f : -1.0f;
	s->fox_t = 0.0f;
}

static void fox_step(st_t *s, const us_ctx *ctx, float dens)
{
	us_rng *r = &s->lrng;
	const us_audio *a = ctx->a;
	float dt = ctx->dt, A = ctx->aspect;
	if (!s->fox_on) {
		s->next_fox -= dt * (0.6f + 0.8f * dens);
		if (s->next_fox > 0.0f)
			return;
		s->fox_on = 1;
		s->fox_x = us_rand(r) < 0.5f ? -0.06f : 1.06f;
		s->fox_y = us_randr(r, 0.80f, 0.875f);
		s->fox_n = 0;
		s->fox_head = s->fox_alert = 0.0f;
		fox_trot_to(s, us_randr(r, 0.25f, 0.75f));
	}
	float sc = fox_sc(s->fox_y);
	s->fox_t += dt;
	float head_to = 0.0f, alert_to = 0.0f;
	switch (s->fox_mode) {
	case 0: { /* trot: a light, quick gait; brisker when the music is loud */
		float sp = 0.035f * (1.0f + 0.4f * s->lvl);
		s->fox_x += s->fox_dir * sp * dt;
		float oph = s->fox_ph;
		s->fox_ph += dt * 8.0f * (1.0f + 0.3f * s->lvl);
		if (floorf(oph / US_PI) != floorf(s->fox_ph / US_PI)) { /* neat single-file prints */
			print_t *p = &s->pr[s->pri];
			s->pri = (s->pri + 1) % MAX_PRINT;
			p->x = s->fox_x;
			p->y = s->fox_y + 0.0006f;
			p->age = 0.0f;
			p->s = 0.45f + 0.9f * (s->fox_y - 0.8f) / 0.08f;
		}
		if (s->fox_x < -0.08f || s->fox_x > 1.08f) {
			s->fox_on = 0;
			s->next_fox = us_randr(r, 8.0f, 25.0f);
			return;
		}
		if (s->fox_dir * (s->fox_x - s->fox_tgt) >= 0.0f) {
			s->fox_mode = 1;
			s->fox_t = 0.0f;
			s->fox_dur = us_randr(r, 1.5f, 4.5f);
		}
		if (a->snare_hit && a->snare > 0.35f) { /* a crack: it freezes, head up, ears pricked */
			s->fox_mode = 5;
			s->fox_t = 0.0f;
		}
		break;
	}
	case 1: /* listening: head down over the snow, ears forward, cocking its head (the kick flips the tilt) */
		head_to = 1.0f;
		if (a->kick_hit)
			s->fox_tilt_to = -s->fox_tilt_to;
		if ((a->snare_hit && s->fox_t > 0.4f && us_rand(r) < 0.9f) || (s->fox_t > s->fox_dur && us_rand(r) < 0.7f)) {
			s->fox_mode = 2; /* the snare launches the pounce */
			s->fox_t = 0.0f;
			s->fox_x0 = s->fox_x;
		} else if (s->fox_t > s->fox_dur) {
			float t = s->fox_x + s->fox_dir * us_randr(r, 0.06f, 0.16f);
			if (t < 0.12f || t > 0.88f)
				t = s->fox_x - s->fox_dir * us_randr(r, 0.06f, 0.16f);
			fox_trot_to(s, t);
		}
		break;
	case 5: /* alert, then it listens */
		alert_to = 1.0f;
		if (s->fox_t > 1.1f) {
			s->fox_mode = 1;
			s->fox_t = 0.0f;
			s->fox_dur = us_randr(r, 1.0f, 3.0f);
		}
		break;
	case 2: { /* the pounce: a high arc, diving nose-first into the snow */
		float u = s->fox_t / 0.75f;
		s->fox_x = s->fox_x0 + s->fox_dir * 1.5f * sc / A * us_sat(u);
		if (u >= 1.0f) {
			s->fox_mode = 3;
			s->fox_t = 0.0f;
			s->fox_dur = us_randr(r, 0.7f, 1.4f);
			s->fox_hole = s->fox_x + s->fox_dir * 0.45f * sc / A;
			burst(s, s->fox_hole * A, s->fox_y, sc, 1.0f);
			/* any hare nearby bolts */
			for (int i = 0; i < MAX_HARE; i++) {
				hare_t *h = &s->hr[i];
				if (h->away <= 0.0f && fabsf(h->x - s->fox_hole) < 0.3f) {
					h->d = h->x < s->fox_hole ? -1.0f : 1.0f;
					h->bolt = 6;
					h->up = 0.0f;
				}
			}
			/* the dive leaves a hole */
			print_t *p = &s->pr[s->pri];
			s->pri = (s->pri + 1) % MAX_PRINT;
			p->x = s->fox_hole;
			p->y = s->fox_y + 0.002f;
			p->age = 0.0f;
			p->s = 4.0f + 6.0f * (s->fox_y - 0.8f) / 0.08f;
		}
		break;
	}
	case 3: /* dug in: only the hindquarters and the waving tail show */
		if (s->fox_t > s->fox_dur) {
			s->fox_mode = 4;
			s->fox_t = 0.0f;
			s->fox_x0 = s->fox_x;
		}
		break;
	case 4: { /* backs out, shakes the snow off */
		float e = us_smooth(0.0f, 0.55f, s->fox_t);
		s->fox_x = s->fox_x0 - s->fox_dir * 0.25f * sc / A * e;
		if (s->fox_t > 0.55f && s->fox_t - dt <= 0.55f)
			burst(s, s->fox_x * A, s->fox_y - 0.6f * sc, sc * 0.6f, 0.5f);
		if (s->fox_t > 1.05f) {
			s->fox_n++;
			if (s->fox_n >= 3 || us_rand(r) < 0.2f)
				fox_trot_to(s, s->fox_dir > 0.0f ? 1.1f : -0.1f);
			else if (us_rand(r) < 0.5f) {
				s->fox_mode = 1;
				s->fox_t = 0.0f;
				s->fox_dur = us_randr(r, 1.5f, 4.0f);
			} else {
				float t = s->fox_x + (us_rand(r) < 0.7f ? s->fox_dir : -s->fox_dir) * us_randr(r, 0.05f, 0.14f);
				fox_trot_to(s, us_clampf(t, 0.12f, 0.88f));
			}
		}
		break;
	}
	}
	s->fox_head = us_approach(s->fox_head, head_to, 0.25f, dt);
	s->fox_alert = us_approach(s->fox_alert, alert_to, 0.12f, dt);
	s->fox_tilt = us_approach(s->fox_tilt, s->fox_tilt_to, 0.08f, ctx->real_dt);
}

static void fox_draw(st_t *s, us_canvas *c, float A, float dayk, float t)
{
	int cr = c->crisp;
	float d = s->fox_dir, gy = s->fox_y, sc = fox_sc(gy);
	float x = s->fox_x * A;
	float vb = cr ? 0.2f + 0.25f * s->lvl + 0.05f * dayk : 0.3f * (0.8f + 0.2f * dayk + 0.25f * s->lvl), vd = cr ? 0.1f : 0.05f, vw = cr ? 0.62f : 0.55f;
	int m = s->fox_mode;
	float pitch = 0.0f, cy = gy - 0.62f * sc, clip = 9.0f, air = 0.0f;
	float walk = m == 0 ? 1.0f : 0.0f;
	float tailA = 0.38f + 0.1f * sinf(t * 1.3f) - 0.45f * s->fox_alert + 0.25f * s->fox_head; /* + = droops */
	float ha = us_lerp(0.35f, 1.2f, s->fox_head) - 0.25f * s->fox_alert + s->fox_tilt * s->fox_head;
	float hx = us_lerp(us_lerp(0.66f, 0.62f, s->fox_alert), 0.8f, s->fox_head);
	float hy = us_lerp(us_lerp(-0.32f, -0.5f, s->fox_alert), -0.02f, s->fox_head);
	if (m == 0)
		cy += 0.025f * sc * sinf(s->fox_ph * 2.0f);
	cy += 0.06f * sc * s->fox_head; /* a crouch while it listens */
	if (m == 2) {
		float u = us_sat(s->fox_t / 0.75f);
		air = 1.5f * sc * 4.0f * u * (1.0f - u);
		pitch = us_lerp(-0.65f, 1.3f, us_smooth(0.1f, 0.95f, u));
		cy = gy - 0.62f * sc - air + 0.3f * sc * us_smooth(0.7f, 1.0f, u);
		tailA = -0.35f + 0.5f * u;
		ha = 0.2f;
		hx = 0.72f;
		hy = -0.18f;
		if (u > 0.6f)
			clip = gy + 0.002f;
	} else if (m == 3) {
		pitch = 1.3f + 0.05f * sinf(s->fox_t * 9.0f);
		cy = gy - 0.32f * sc;
		tailA = 0.1f + 0.45f * sinf(s->fox_t * 6.5f); /* the tail waves */
		clip = gy + 0.002f;
	} else if (m == 4) {
		float e = us_smooth(0.0f, 0.55f, s->fox_t);
		pitch = 1.3f * (1.0f - e);
		cy = us_lerp(gy - 0.32f * sc, gy - 0.62f * sc, e);
		if (s->fox_t < 0.55f)
			clip = gy + 0.002f;
		else /* the shake: the whole body shudders */
			x += 0.035f * sc * sinf(s->fox_t * 95.0f) * us_smooth(1.05f, 0.75f, s->fox_t);
	}
	/* shadow on the snow (shrinks while it is in the air) */
	if (m != 3)
		dab_min(c, x, gy + 0.0015f, (0.55f - 0.25f * us_sat(air / sc)) * sc, 0.07f * sc, cr ? 0.24f : 0.13f);
	fx_t F;
	fx_init(&F, c, x, cy, pitch, d, sc, clip);
	/* legs: on the ground, a diagonal-pair trot; in the air, forelegs tucked and hind legs trailing */
	for (int l = 0; l < 4; l++) {
		int front = l < 2, far = (l & 1) == 0;
		float lv = far ? vd * 0.7f : vd;
		float ax = front ? 0.3f : -0.3f, ay = front ? 0.1f : 0.08f;
		if (far)
			ax += front ? -0.05f : 0.05f;
		float wx, wy;
		fx_pt(&F, ax, ay, &wx, &wy);
		if (m == 2) {
			float u = us_sat(s->fox_t / 0.75f);
			float fx_ = front ? us_lerp(0.55f, 0.75f, u) : us_lerp(-0.6f, -0.85f, u), fy_ = front ? 0.4f : 0.32f;
			float kx, ky, ex, ey;
			fx_pt(&F, (ax + fx_) * 0.5f + (front ? 0.06f : -0.04f), (ay + fy_) * 0.5f + (front ? 0.08f : -0.05f), &kx, &ky);
			fx_pt(&F, fx_, fy_, &ex, &ey);
			limb(c, wx, wy, kx, ky, 0.06f * sc, 0.035f * sc, clip, lv, US_MAT_EARTH);
			limb(c, kx, ky, ex, ey, 0.035f * sc, 0.025f * sc, clip, lv, US_MAT_EARTH);
			continue;
		}
		if (wy > gy - 0.08f * sc)
			continue; /* this leg is in the snow */
		float ph = s->fox_ph + ((l == 0 || l == 3) ? 0.0f : US_PI);
		float sw = 0.17f * sc * sinf(ph) * walk;
		float lift = 0.05f * sc * us_sat(sinf(ph + 1.5f)) * walk;
		float fxw = wx + d * sw, fyw = gy - lift;
		float kx = 0.5f * (wx + fxw) + (front ? d * 0.03f : -d * 0.07f) * sc, ky = 0.5f * (wy + fyw);
		limb(c, wx, wy, kx, ky, (front ? 0.06f : 0.09f) * sc, 0.035f * sc, clip, lv, US_MAT_EARTH);
		limb(c, kx, ky, fxw, fyw, 0.033f * sc, 0.025f * sc, clip, lv, US_MAT_EARTH);
	}
	/* the brush: a thick bushy tail, white-tipped */
	{
		float sp[2 * 5], hw[5] = {0.06f, 0.12f, 0.14f, 0.12f, 0.02f};
		float px = -0.46f, py = -0.06f;
		for (int i = 0; i < 5; i++) {
			sp[2 * i] = px;
			sp[2 * i + 1] = py;
			float aa = tailA + 0.12f * i * (m == 3 ? -1.0f : 1.0f);
			px -= cosf(aa) * 0.24f;
			py += sinf(aa) * 0.24f;
		}
		float L[2 * 10];
		for (int i = 0; i < 5; i++) {
			int i0 = i > 0 ? i - 1 : 0, i1 = i < 4 ? i + 1 : 4;
			float tx = sp[2 * i1] - sp[2 * i0], ty = sp[2 * i1 + 1] - sp[2 * i0 + 1], tl = sqrtf(tx * tx + ty * ty) + 1e-6f;
			float nx = -ty / tl, ny = tx / tl;
			L[2 * i] = sp[2 * i] + nx * hw[i];
			L[2 * i + 1] = sp[2 * i + 1] + ny * hw[i];
			L[2 * (9 - i)] = sp[2 * i] - nx * hw[i];
			L[2 * (9 - i) + 1] = sp[2 * i + 1] - ny * hw[i];
		}
		fx_poly(&F, L, 10, vb * 0.92f, US_MAT_LIGHT);
		/* the white tip: the last stretch */
		float tp[2 * 4] = {L[6] * 0.5f + L[8] * 0.5f, L[7] * 0.5f + L[9] * 0.5f, L[8], L[9], L[10], L[11],
				   L[12] * 0.5f + L[10] * 0.5f, L[13] * 0.5f + L[11] * 0.5f};
		fx_poly(&F, tp, 4, vw, US_MAT_ACCENT);
	}
	/* body: barrel, chest, haunch */
	fx_ell(&F, 0.0f, 0.0f, 0.5f, 0.17f, 0.0f, vb, US_MAT_LIGHT);
	fx_ell(&F, 0.33f, -0.02f, 0.19f, 0.19f, 0.0f, vb, US_MAT_LIGHT);
	fx_ell(&F, -0.3f, -0.03f, 0.2f, 0.18f, 0.0f, vb, US_MAT_LIGHT);
	/* neck + head */
	if (m != 3) {
		fx_seg(&F, 0.34f, -0.08f, hx, hy, 0.15f, 0.1f, vb, US_MAT_LIGHT);
		fx_seg(&F, 0.38f, 0.02f, hx + 0.02f, hy + 0.08f, 0.09f, 0.06f, vw * 0.9f, US_MAT_ACCENT); /* white throat */
		float ca = cosf(ha), sa = sinf(ha);
		fx_ell(&F, hx, hy, 0.15f, 0.115f, ha * 0.5f, vb, US_MAT_LIGHT);
		fx_seg(&F, hx, hy, hx + ca * 0.28f, hy + sa * 0.28f, 0.075f, 0.022f, vb, US_MAT_LIGHT);
		fx_ell(&F, hx + ca * 0.28f, hy + sa * 0.28f, 0.03f, 0.03f, 0.0f, vd, US_MAT_EARTH); /* nose */
		/* ears: tall, pointed, dark-backed; pricked forward when listening */
		float ea = ha - 1.95f + 0.25f * s->fox_head;
		for (int e = 0; e < 2; e++) {
			float bx = hx - ca * (0.06f - 0.05f * e) + sa * 0.08f, by = hy - sa * (0.06f - 0.05f * e) - ca * 0.08f;
			float tx = bx + cosf(ea) * 0.22f, ty = by + sinf(ea) * 0.22f;
			float nx = -sinf(ea) * 0.055f, ny = cosf(ea) * 0.055f;
			float ep[6] = {bx + nx, by + ny, tx, ty, bx - nx, by - ny};
			fx_poly(&F, ep, 3, e ? vd : vd * 0.7f, US_MAT_EARTH);
		}
		if (!cr) /* eye */
			fx_ell(&F, hx + ca * 0.06f + sa * 0.03f, hy + sa * 0.06f - ca * 0.03f, 0.022f, 0.016f, ha, vd * 0.5f, US_MAT_EARTH);
	}
	/* the snow thrown up round the hole while it is in */
	if (m == 3 || (m == 4 && s->fox_t < 0.6f)) {
		float hxw = s->fox_hole * A;
		float sv = cr ? 0.62f : 0.42f;
		ellipse_set(c, hxw - d * 0.12f * sc, gy - 0.01f * sc, 0.2f * sc, 0.07f * sc, 0.0f, sv, US_MAT_ACCENT);
		ellipse_set(c, hxw + d * 0.16f * sc, gy - 0.005f * sc, 0.16f * sc, 0.06f * sc, 0.0f, sv * 0.9f, US_MAT_ACCENT);
	}
}

/* ------------------------------------------------------------------ snowshoe hares */

static inline float hare_sc(float y) { return 0.36f * (y - HORIZ); }

static void hare_hop(hare_t *h)
{
	if (h->x < 0.08f) h->d = 1.0f;
	if (h->x > 0.92f && !h->bolt) h->d = -1.0f;
	h->hx0 = h->x;
	h->ht = 0.0f;
	h->up = 0.0f;
	h->alert_t = 0.0f;
}

static void hares_step(st_t *s, const us_ctx *ctx, int nh)
{
	us_rng *r = &s->lrng;
	const us_audio *a = ctx->a;
	float dt = ctx->dt, A = ctx->aspect;
	int kicked = 0;
	for (int i = 0; i < MAX_HARE; i++) {
		hare_t *h = &s->hr[i];
		if (h->away > 0.0f) { /* off in the trees; it hops back in later */
			h->away -= dt;
			if (h->away <= 0.0f && i < nh) {
				h->d = us_rand(r) < 0.5f ? 1.0f : -1.0f;
				h->x = h->d > 0.0f ? -0.03f : 1.03f;
				h->y = us_randr(r, 0.715f, 0.775f);
				h->bolt = 3;
				hare_hop(h);
			} else if (h->away <= 0.0f)
				h->away = 2.0f;
			continue;
		}
		float hs = hare_sc(h->y);
		if (h->ht >= 0.0f) {
			float dur = h->bolt ? 0.24f : 0.32f, len = (h->bolt ? 2.4f : 1.3f) * hs / A;
			h->ht += dt;
			float u = us_sat(h->ht / dur);
			h->x = h->hx0 + h->d * len * u;
			if (u >= 1.0f) {
				h->ht = -1.0f;
				h->next = us_randr(r, 3.0f, 9.0f);
				if (h->x < -0.04f || h->x > 1.04f) {
					h->away = us_randr(r, 8.0f, 20.0f);
					h->bolt = 0;
					continue;
				}
				if (h->bolt > 0) {
					h->bolt--;
					if (h->bolt > 0 || h->x < 0.0f || h->x > 1.0f)
						hare_hop(h);
				}
			}
			continue;
		}
		if (i >= nh) { /* density lowered: this one hops off */
			h->bolt = 8;
			hare_hop(h);
			continue;
		}
		/* sitting: the kick sends one hare hopping on; a snare sits them bolt upright, ears high */
		h->next -= dt * (1.0f + s->lvl);
		if (a->kick_hit && !kicked && us_rand(r) < 0.7f) {
			kicked = 1;
			if (us_rand(r) < 0.25f)
				h->d = -h->d;
			hare_hop(h);
			continue;
		}
		if (a->snare_hit && a->snare > 0.3f)
			h->alert_t = us_randr(r, 0.9f, 1.6f);
		if (h->next <= 0.0f) {
			if (us_rand(r) < 0.3f)
				h->d = -h->d;
			hare_hop(h);
			continue;
		}
		h->alert_t -= dt;
		h->up = us_approach(h->up, h->alert_t > 0.0f ? 1.0f : 0.0f, 0.07f, ctx->real_dt);
		h->ear = us_approach(h->ear, 0.0f, 0.2f, dt);
		if (us_rand(r) < dt * 0.4f || (a->hat_hit && us_rand(r) < 0.5f))
			h->ear = 1.0f; /* an ear twitch (the hats catch its ear) */
	}
}

static void hare_draw(const hare_t *h, us_canvas *c, float A, float winter, float t, int i, float lvl)
{
	int cr = c->crisp;
	float hs = hare_sc(h->y), d = h->d, x = h->x * A, gy = h->y;
	int white = winter > 0.45f;
	float vb = white ? (cr ? 0.5f + 0.45f * lvl : 0.5f + 0.25f * lvl) : (cr ? 0.16f : 0.09f);
	uint8_t mb = white ? US_MAT_ACCENT : US_MAT_EARTH;
	float vd = cr ? 0.12f : 0.04f;
	float pitch = us_lerp(-0.25f, -1.15f, h->up), cy = gy - us_lerp(0.36f, 0.5f, h->up) * hs, stretch = 0.0f;
	int hop = h->ht >= 0.0f;
	if (hop) {
		float u = us_sat(h->ht / (h->bolt ? 0.24f : 0.32f));
		stretch = sinf(US_PI * u);
		pitch = us_lerp(-0.45f, 0.4f, u);
		cy = gy - 0.36f * hs - (h->bolt ? 0.7f : 0.5f) * hs * stretch;
	}
	dab_min(c, x, gy + 0.001f, 0.42f * hs * (1.0f - 0.3f * stretch), 0.07f * hs, cr ? 0.24f : 0.13f);
	fx_t F;
	fx_init(&F, c, x, cy, pitch, d, hs, 9.0f);
	if (hop) { /* hind legs flung back, forelegs reaching */
		fx_seg(&F, -0.3f, 0.1f, -0.55f - 0.4f * stretch, 0.3f + 0.1f * stretch, 0.13f, 0.05f, vb, mb);
		fx_seg(&F, 0.3f, 0.15f, 0.45f + 0.25f * stretch, 0.42f, 0.07f, 0.04f, vb, mb);
	} else { /* the long snowshoe foot flat on the snow */
		ellipse_set(c, x - d * 0.04f * hs, gy - 0.035f * hs, 0.28f * hs, 0.05f * hs, 0.0f, vb, mb);
	}
	fx_ell(&F, 0.0f, 0.0f, 0.48f + 0.2f * stretch, 0.33f - 0.06f * stretch, 0.0f, vb, mb);
	fx_ell(&F, 0.28f, -0.04f, 0.24f, 0.26f, 0.0f, vb, mb);
	fx_ell(&F, -0.47f, -0.12f, 0.1f, 0.1f, 0.0f, cr ? 0.85f : 0.66f, US_MAT_ACCENT); /* scut */
	/* head: kept level as the body pitches; nibbling bobs it when it sits */
	float nib = hop || h->up > 0.3f ? 0.0f : 0.035f * us_sat(us_noise2(t * 0.3f, i * 5.0f) * 3.0f) * sinf(t * 6.0f);
	float hx = 0.5f, hy = -0.3f + nib;
	float hr = -pitch * 0.85f + 0.25f;
	fx_ell(&F, hx, hy, 0.21f, 0.15f, hr, vb, mb);
	/* ears: long, up and back, black-tipped; laid back in a hop, straight up when alert */
	for (int e = 0; e < 2; e++) {
		float ea = -1.75f - pitch - 0.45f * stretch - 0.15f * e + 0.25f * h->ear * (e ? 0.0f : 1.0f) + 0.15f * h->up;
		float ex = hx - 0.06f - 0.04f * e, ey = hy - 0.1f;
		float L = 0.46f;
		fx_ell(&F, ex + cosf(ea) * L * 0.5f, ey + sinf(ea) * L * 0.5f, L * 0.5f, 0.055f, ea, e ? vb * 0.85f : vb, mb);
		float tx, ty;
		fx_pt(&F, ex + cosf(ea) * L * 0.92f, ey + sinf(ea) * L * 0.92f, &tx, &ty);
		if (white)
			us_disc(c, tx, ty, (cr ? 0.07f : 0.05f) * hs, vd, US_MAT_EARTH);
	}
	/* the eye */
	float ex, ey;
	fx_pt(&F, hx + 0.08f * cosf(hr) - 0.02f, hy - 0.02f, &ex, &ey);
	if (cr) {
		if (white)
			us_point(c, ex, ey, 0.5f / c->s, vd, US_MAT_EARTH);
	} else
		dab_min(c, ex, ey, 0.04f * hs, 0.035f * hs, 0.02f);
}

/* ------------------------------------------------------------------ bullfinches on the frame firs */

static void perch_pos(st_t *s, const bird_t *b, float A, float snowk, float *x, float *y)
{
	fir_t *f = &s->tr[b->tree ? s->nearR : s->nearL];
	tiers_t T;
	fir_tiers(f, snowk, &T);
	int k = b->k, sd = b->sd;
	float ytop = f->base - f->h, hf = f->h * 0.9f;
	float t = T.a[k][sd] + b->u * (T.b[k][sd] - T.a[k][sd]) - 0.0015f / hf;
	float hw = T.E[k][sd] * bough(b->u) * 0.98f;
	float sa = 1.0f - us_sat(t);
	*x = f->x * A + (sd ? hw : -hw) + s->fir_sway[b->tree] * sa * sa * f->h;
	*y = ytop + t * hf;
}

static int choose_perch(st_t *s, bird_t *b, float A, float snowk)
{
	us_rng *r = &s->lrng;
	for (int tries = 0; tries < 12; tries++) {
		b->tree = us_rand(r) < 0.5f ? 1 : 0;
		fir_t *f = &s->tr[b->tree ? s->nearR : s->nearL];
		b->sd = b->tree ? 0 : 1; /* the clearing side */
		b->k = 3 + (int)(us_rand(r) * (f->nt - 4));
		b->u = us_randr(r, 0.45f, 0.82f);
		if (!b->tree && abs(b->k - (f->nt - 5)) <= 1) /* the owl's bough */
			continue;
		int clash = 0;
		for (int j = 0; j < MAX_BIRD; j++) {
			bird_t *o = &s->bd[j];
			if (o != b && o->state <= 1 && o->tree == b->tree && o->k == b->k && fabsf(o->u - b->u) < 0.2f)
				clash = 1;
		}
		if (clash)
			continue;
		float x, y;
		perch_pos(s, b, A, snowk, &x, &y);
		if (x > 0.02f && x < A - 0.02f && y > 0.3f && y < 0.86f)
			return 1;
	}
	return 0;
}

static void bird_fly(st_t *s, bird_t *b, float A, float snowk, int away)
{
	us_rng *r = &s->lrng;
	b->fx0 = b->x;
	b->fy0 = b->y;
	b->t = 0.0f;
	if (away || !choose_perch(s, b, A, snowk)) {
		b->state = 2;
		b->dur = us_randr(r, 0.9f, 1.4f);
		return;
	}
	b->state = 1;
	b->dur = us_randr(r, 0.7f, 1.1f);
}

static void birds_step(st_t *s, const us_ctx *ctx, float snowk, int nb, float night)
{
	us_rng *r = &s->lrng;
	const us_audio *a = ctx->a;
	float dt = ctx->dt, rdt = ctx->real_dt, A = ctx->aspect;
	int hopped = 0;
	int flush = a->snare_hit && a->snare > 0.8f && us_rand(r) < 0.12f;
	for (int i = 0; i < MAX_BIRD; i++) {
		bird_t *b = &s->bd[i];
		b->cool -= rdt;
		b->hop = us_approach(b->hop, 0.0f, 0.06f, rdt);
		b->wing = us_approach(b->wing, 0.0f, 0.08f, rdt);
		if (b->state == 3) {
			if (i >= nb)
				continue;
			if (b->away < 0.0f) { /* at the start: already perched */
				if (choose_perch(s, b, A, snowk)) {
					b->state = 0;
					perch_pos(s, b, A, snowk, &b->x, &b->y);
				}
				b->away = 0.0f;
				continue;
			}
			b->away -= dt;
			if (b->away <= 0.0f) { /* flies back in from the side */
				b->x = us_rand(r) < 0.5f ? -0.05f : A + 0.05f;
				b->y = us_randr(r, 0.2f, 0.5f);
				bird_fly(s, b, A, snowk, 0);
				if (b->state == 2) {
					b->state = 3;
					b->away = 3.0f;
				}
			}
			continue;
		}
		if (b->state == 0) {
			perch_pos(s, b, A, snowk, &b->x, &b->y);
			if (i >= nb || flush) {
				bird_fly(s, b, A, snowk, 1);
				continue;
			}
			/* the kick: a hop and a flick of the wings (fewer at night, roosting) */
			if (a->kick_hit && hopped < 2 && b->cool <= 0.0f && us_rand(r) < 0.6f - 0.25f * night) {
				b->hop = 1.0f;
				b->wing = 0.7f;
				b->cool = 0.35f;
				hopped++;
				if (us_rand(r) < 0.12f) /* now and then it flits to another bough */
					bird_fly(s, b, A, snowk, 0);
			}
			if (a->snare_hit) { /* the gust: it opens its wings and bobs to hold on */
				b->wing = us_sat(b->wing + 0.6f + 0.4f * a->snare);
				b->hop = us_sat(b->hop + 0.5f);
			}
			if (a->hat_hit && us_rand(r) < 0.35f)
				b->look = -b->look;
			continue;
		}
		/* flying: a bounding finch flight, wings beating */
		b->t += dt;
		b->flap += dt * 26.0f;
		float u = us_sat(b->t / b->dur);
		float tx, ty;
		if (b->state == 1)
			perch_pos(s, b, A, snowk, &tx, &ty);
		else {
			tx = b->fx0 < A * 0.5f ? -0.08f : A + 0.08f;
			ty = b->fy0 - 0.2f;
		}
		float e = u * u * (3.0f - 2.0f * u);
		b->x = us_lerp(b->fx0, tx, e);
		b->y = us_lerp(b->fy0, ty, e) - 0.06f * sinf(US_PI * u) + 0.006f * sinf(b->flap * 0.25f);
		b->look = tx >= b->fx0 ? 1.0f : -1.0f;
		if (u >= 1.0f) {
			if (b->state == 1)
				b->state = 0;
			else {
				b->state = 3;
				b->away = us_randr(r, 5.0f, 14.0f);
			}
		}
	}
}

/* a bullfinch: a plump grey bird, rosy breast, black cap, wings and tail, a white rump */
static void bird_draw(const bird_t *b, us_canvas *c, float night, float lvl)
{
	int cr = c->crisp;
	float sz = cr ? 0.022f : 0.015f;
	int fl = b->state == 1 || b->state == 2;
	float d = b->look, x = b->x, y = b->y - (fl ? 0.0f : 0.9f * sz * sinf(US_PI * b->hop));
	float fluff = 1.0f + 0.15f * night * (fl ? 0.0f : 1.0f);
	float vg = cr ? 0.4f + 0.2f * lvl : 0.36f, vr = cr ? 0.4f + 0.35f * lvl : 0.5f + 0.15f * lvl, vk = cr ? 0.06f : 0.03f;
	fx_t F;
	fx_init(&F, c, x, y - 0.5f * sz, fl ? 0.15f : -0.1f, d, sz, 9.0f);
	fx_seg(&F, -0.4f, -0.05f, -1.05f, 0.25f, 0.13f, 0.09f, vk, US_MAT_EARTH); /* tail */
	float wa = fl ? sinf(b->flap) : b->wing;
	if (fl) { /* far wing */
		float wp[8] = {0.15f, -0.25f, -0.5f, -0.3f, -0.55f + 0.2f * fabsf(wa), -0.3f - 1.0f * wa, -0.05f, -0.3f - 0.7f * wa};
		fx_poly(&F, wp, 4, vk * 0.8f, US_MAT_EARTH);
	}
	fx_ell(&F, 0.0f, 0.0f, 0.6f * fluff, 0.48f * fluff, 0.0f, vg, US_MAT_WATER);
	fx_ell(&F, 0.16f, 0.12f, 0.42f * fluff, 0.36f * fluff, 0.0f, vr, US_MAT_BLOOM); /* rosy breast */
	fx_ell(&F, -0.48f, -0.05f, 0.13f, 0.13f, 0.0f, cr ? 0.85f : 0.66f, US_MAT_ACCENT); /* white rump */
	if (!fl) { /* folded wing, with its pale bar */
		fx_ell(&F, -0.12f, -0.12f, 0.42f, 0.2f, 0.12f, vk, US_MAT_EARTH);
		if (!cr)
			fx_seg(&F, -0.2f, -0.24f, 0.12f, -0.2f, 0.035f, 0.035f, 0.5f, US_MAT_ACCENT);
	}
	/* head: tucked in at night */
	float hy = night > 0.6f && !fl ? -0.35f : -0.48f;
	fx_ell(&F, 0.46f, hy, 0.3f, 0.28f, 0.0f, vr, US_MAT_BLOOM);
	fx_ell(&F, 0.42f, hy - 0.13f, 0.32f, 0.18f, 0.0f, vk, US_MAT_EARTH); /* black cap */
	fx_ell(&F, 0.76f, hy + 0.04f, 0.12f, 0.09f, 0.0f, vk, US_MAT_EARTH);  /* stubby beak */
	if (fl || b->wing > 0.1f) { /* near wing: beating, or flicked open */
		float up = fl ? wa : b->wing;
		float wp[8] = {0.2f, -0.2f, -0.45f, -0.2f, -0.6f + 0.2f * fabsf(up), -0.15f - 1.15f * up, 0.0f, -0.2f - 0.8f * up};
		fx_poly(&F, wp, 4, vk, US_MAT_EARTH);
	}
}

/* ------------------------------------------------------------------ frosted grass */

static void grass_draw(st_t *s, us_canvas *c, float A, float sway, float t)
{
	int cr = c->crisp;
	float vd = cr ? 0.05f : 0.045f, vf = (cr ? 0.75f : 0.55f) * (0.75f + 0.5f * s->lvl);
	for (int i = 0; i < MAX_GRASS; i++) {
		grass_t *g = &s->gr[i];
		float x0 = g->x * A, y0 = g->y;
		for (int j = 0; j < g->n; j++) {
			if (cr && (j & 1)) /* pixel: every blade is a whole pixel wide, so fewer of them */
				continue;
			float fj = (j + 0.5f) / g->n - 0.5f;
			float hh = g->h * (0.6f + 0.4f * fabsf(sinf(j * 2.3f + g->ph)));
			float a = g->lean + 0.9f * fj + sway * (0.8f + 0.4f * sinf(j * 1.7f)) + 0.04f * sinf(t * 1.1f + g->ph + j);
			float bx = x0 + fj * 0.012f, by = y0;
			float mx = bx + sinf(a * 0.6f) * hh * 0.5f, my = by - cosf(a * 0.6f) * hh * 0.5f;
			float tx = mx + sinf(a) * hh * 0.5f, ty = my - cosf(a) * hh * 0.5f;
			uint8_t gm = cr ? US_MAT_EARTH : US_MAT_STEM; /* dry, not green */
			limb(c, bx, by, mx, my, 0.0011f, 0.0008f, 9.0f, vd, gm);
			limb(c, mx, my, tx, ty, 0.0008f, 0.0003f, 9.0f, vd, gm);
			if ((j + i) % 3 == 0) { /* a seed head, frosted: it glints with the hats */
				float ca = cosf(a), sa = sinf(a);
				float hx = tx + sa * 0.005f, hy = ty - ca * 0.005f;
				if (!cr)
					ellipse_set(c, hx, hy, 0.0022f, 0.0065f, a, vd * 1.4f, gm);
				float gl = s->frost_gl * (0.5f + 0.5f * sinf(j * 3.1f + i * 1.3f));
				if (cr)
					us_point(c, hx, hy - 0.003f, 0.0025f, us_sat(0.3f + gl), US_MAT_ACCENT);
				else
					us_disc(c, hx + sa * 0.003f, hy - ca * 0.004f, 0.0014f + 0.0012f * gl, us_sat(vf * (0.45f + gl)), US_MAT_ACCENT);
			}
		}
		/* the snow heaped where it pokes through */
		ellipse_set(c, x0, y0 + 0.002f, cr ? 0.012f : 0.016f, cr ? 0.003f : 0.004f, 0.0f, (cr ? 0.45f : 0.27f) + 0.12f * s->lvl, US_MAT_WATER);
	}
}

/* ------------------------------------------------------------------ frame */

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *s = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	us_rng *r = &s->rng;
	float dt = ctx->dt, rdt = ctx->real_dt, A = ctx->aspect, S = c->s;
	float dens = ctx->p->density;
	int cr = ctx->crisp;
	float winter = us_world_winter(w), spring = us_world_season_w(w, 0), autumn = us_world_season_w(w, 2);
	float day = w->daylight, dayk = us_smooth(0.12f, 0.75f, day);
	float night = 1.0f - dayk;
	float hat = us_sat(a->hat);

	/* ---- envelopes ---- */
	s->lvl = us_approach(s->lvl, us_sat(a->level), a->level > s->lvl ? 0.12f : 0.45f, rdt);
	s->swl = us_approach(s->swl, us_sat(a->swell), 1.0f, rdt);
	s->lm = us_approach(s->lm, us_sat(a->lowmid), 0.5f, rdt);
	s->hi = us_approach(s->hi, us_sat(0.6f * a->high + 0.6f * a->air), 0.3f, rdt);
	s->wid = us_approach(s->wid, us_sat(a->width), 2.0f, rdt);
	for (int i = 0; i < US_NBANDS; i++) {
		float bv = a->bands[i];
		s->bandv[i] = us_approach(s->bandv[i], bv, bv > s->bandv[i] ? 0.08f : 0.5f, rdt);
	}
	/* snow on the ground + boughs: deep in winter, thinner in the other seasons (late or first snow) */
	float snowk = 0.45f + 0.55f * winter + 0.1f * w->moisture - 0.15f * (spring + autumn) * 0.5f;
	snowk = us_clampf(snowk, 0.3f, 1.05f);
	/* snowfall rate: density, winter, moisture, storm, loudness */
	float want = 0.22f + 0.18f * dens + 0.18f * winter + 0.12f * w->moisture + 0.3f * w->storm_charge + 0.3f * s->lvl + 0.2f * s->swl;
	s->snow = us_approach(s->snow, us_sat(want), 0.6f, rdt);
	s->warm = us_approach(s->warm, us_sat(0.15f + 0.85f * s->lvl + 0.3f * s->lm), 0.25f, rdt);
	us_world_add(w, &w->moisture, 0.004f * s->snow, dt);
	s->T += dt;
	s->mistT += dt * (0.012f + 0.02f * w->wind);
	s->cloudT += dt * (0.006f + 0.012f * w->wind);
	float wind = (0.006f + 0.035f * w->wind) * (0.7f + 0.6f * s->wid); /* fraction of width per s */

	/* ---- drums ---- */
	s->kick_cool -= rdt;
	s->snare_cool -= rdt;
	s->gust = us_approach(s->gust, 0.0f, 0.3f, rdt);
	fir_t *dyn[3] = {&s->tr[s->lone], &s->tr[s->nearL], &s->tr[s->nearR]};
	if (a->kick_hit && s->kick_cool <= 0.0f) {
		s->kick_cool = 0.1f;
		float amt = us_sat(0.5f + 0.6f * a->kick);
		/* pick a laden bough on a near tree whose tip is on screen */
		int nshed = a->kick > 0.6f ? 3 : 2;
		for (int q = 0; q < nshed; q++) {
			int bt = -1, bk = 0, bs = 0;
			float bestw = 0.0f;
			for (int ti = 0; ti < 3; ti++) {
				fir_t *f = dyn[ti];
				for (int k = 2; k < f->nt; k++)
					for (int sd = 0; sd < 2; sd++) {
						float x, y;
						fir_tip(f, A, k, sd, &x, &y);
						if (x < 0.02f || x > A - 0.02f || y > 0.97f || y < 0.05f)
							continue;
						float wgt = f->load[k][sd] * f->load[k][sd] * us_randr(r, 0.2f, 1.0f) * (ti == 0 ? 1.4f : 1.0f);
						if (wgt > bestw) {
							bestw = wgt;
							bt = ti;
							bk = k;
							bs = sd;
						}
					}
			}
			if (bt >= 0 && bestw > 0.01f) {
				shed(s, dyn[bt], bk, bs, A, amt);
				for (int i = 0; i < MAX_BIRD && bt > 0; i++) { /* a finch on that bough flits off */
					bird_t *b = &s->bd[i];
					if (b->state == 0 && b->tree == bt - 1 && b->k == bk && b->sd == bs)
						bird_fly(s, b, A, snowk, 0);
				}
			}
		}
	}
	if (a->snare_hit && s->snare_cool <= 0.0f) {
		s->snare_cool = 0.12f;
		s->gust_dir = us_rand(r) < 0.7f ? (w->wind >= 0.0f ? 1.0f : -1.0f) : -s->gust_dir;
		s->gust = us_sat(s->gust + 0.5f + 0.5f * a->snare);
		flurry(s, A, us_sat(0.4f + 0.6f * a->snare), s->gust_dir);
		/* powder blown off a few bough tips */
		for (int q = 0; q < 2; q++) {
			fir_t *f = dyn[1 + (us_rng_u32(r) & 1)];
			int k = 3 + (int)(us_rand(r) * (f->nt - 3));
			int sd = us_rng_u32(r) & 1;
			float x, y;
			fir_tip(f, A, k, sd, &x, &y);
			for (int i = 0; i < 14; i++) {
				part_t *p = spawn(s);
				p->x = x + us_randn(r) * 0.01f;
				p->y = y + us_randn(r) * 0.008f;
				p->vx = s->gust_dir * us_randr(r, 0.1f, 0.35f);
				p->vy = us_randr(r, -0.04f, 0.03f);
				p->r = us_randr(r, 0.0012f, 0.0025f);
				p->v = us_randr(r, 0.4f, 0.7f);
				p->max = p->life = us_randr(r, 0.5f, 1.1f);
				p->drag = 1.5f;
				p->kind = 2;
			}
		}
		/* a strong crack can startle the owl off its perch */
		if (s->owl_state == 0 && a->snare > 0.7f && s->owl_away <= 0.0f && us_rand(r) < 0.35f) {
			s->owl_state = 1;
			s->owl_x = s->owl_px;
			s->owl_y = s->owl_py;
			s->owl_vx = 0.12f;
			s->owl_vy = -0.05f;
			s->owl_t = 0.0f;
		}
	}
	s->frost_gl *= expf(-rdt / 0.12f);
	if (a->hat_hit) {
		s->frost_gl = us_sat(0.6f + 0.6f * hat); /* frost on the grass heads glints */
		/* a scatter of flakes and snow crystals catch the light */
		float pf = 0.25f + 0.3f * hat + 0.1f * s->hi;
		for (int i = 0; i < MAX_MID; i++)
			if (us_rand(r) < pf)
				s->fm[i].gl = 1.0f;
		for (int i = 0; i < MAX_NEAR; i++)
			if (us_rand(r) < pf)
				s->fn[i].gl = 1.0f;
		for (int i = 0; i < MAX_CRYS; i++)
			if (us_rand(r) < pf * 1.5f)
				s->cr[i].gl = 1.0f;
		int ng = (int)(8 + 14 * hat);
		for (int q = 0; q < ng; q++) {
			int ti = (int)(us_rand(r) * 3.0f) % 3;
			fir_t *f = ti == 0 ? &s->tr[s->lone] : &s->tr[ti == 1 ? s->nearL : s->nearR];
			s->cgi = (s->cgi + 1) % MAX_CG;
			s->cg[s->cgi].t = ti;
			s->cg[s->cgi].k = 1 + (int)(us_rand(r) * (f->nt - 1));
			s->cg[s->cgi].sd = us_rng_u32(r) & 1;
			s->cg[s->cgi].u = us_randr(r, 0.35f, 0.95f);
			s->cg[s->cgi].gl = us_randr(r, 0.7f, 1.0f);
		}
	}
	float gdecay = expf(-rdt / 0.075f);
	/* the kick's thump: every near bough is knocked down and twangs back, a quick stiff shiver */
	if (a->kick_hit)
		s->kdv -= (0.7f + 0.5f * a->kick + 0.4f * a->sub) * 40.0f;
	{
		float om = US_TAU * 6.5f, zeta = 0.3f, h = (rdt > 0.1f ? 0.1f : rdt) / 6.0f;
		for (int it = 0; it < 6; it++) {
			s->kdv += (-om * om * s->kd - 2.0f * zeta * om * s->kdv) * h;
			s->kd += s->kdv * h;
		}
		s->kd = us_clampf(s->kd, -1.5f, 1.5f);
	}
	for (int ti = 0; ti < 3; ti++)
		dyn[ti]->dip = -0.014f * s->kd * (ti == 0 ? 1.4f : 1.0f) * (cr ? 1.5f : 1.0f);
	float gustx = s->gust * s->gust_dir * 0.5f;
	/* the snare's gust bends every tree downwind and lets it whip back */
	if (a->snare_hit)
		s->gsv += s->gust_dir * (0.8f + 0.6f * a->snare) * 22.0f;
	{
		float om = US_TAU * 4.0f, zeta = 0.28f, h = (rdt > 0.1f ? 0.1f : rdt) / 6.0f;
		for (int it = 0; it < 6; it++) {
			s->gsv += (-om * om * s->gs - 2.0f * zeta * om * s->gsv) * h;
			s->gs += s->gsv * h;
		}
		s->gs = us_clampf(s->gs, -1.5f, 1.5f);
	}

	/* bough springs + snow regrowth on the dynamic firs */
	{
		float om = US_TAU * 1.9f, zeta = 0.42f;
		float grow = dt * 0.07f * (0.4f + s->snow) * (0.5f + 0.7f * winter);
		for (int ti = 0; ti < 3; ti++) {
			fir_t *f = dyn[ti];
			for (int k = 0; k < f->nt; k++)
				for (int sd = 0; sd < 2; sd++) {
					float x = f->sp[k][sd], v = f->spv[k][sd];
					float h = rdt > 0.05f ? 0.05f : rdt;
					for (int it = 0; it < 2; it++) {
						float acc = -om * om * x - 2.0f * zeta * om * v;
						v += acc * h * 0.5f;
						x += v * h * 0.5f;
					}
					f->sp[k][sd] = us_clampf(x, -0.05f, 0.05f);
					f->spv[k][sd] = v;
					f->load[k][sd] = us_sat(f->load[k][sd] + grow);
				}
		}
	}

	/* ---- sky: a tone field (blue sky + snow cloud, warm low glow, moon halo) ---- */
	float moonx = 0.79f * A, moony = 0.15f;
	float moonv = us_sat(1.0f - 1.6f * dayk);
	{
		float top = cr ? 0.13f : 0.07f, hor = cr ? 0.24f : 0.2f;
		top += dayk * (cr ? 0.22f : 0.09f);
		hor += dayk * (cr ? 0.18f : 0.07f) + (0.04f * (1.0f - fabsf(day - 0.3f) * 3.0f > 0 ? 1.0f - fabsf(day - 0.3f) * 3.0f : 0.0f));
		float lift = cr ? 0.0f : 0.05f * s->lvl + 0.03f * s->swl; /* pixel: no level wash (it would flip whole bands) */
		float *gs = s->sky;
		for (int j = 0; j < SGH; j++) {
			float y = SKY_Y1 * j / (SGH - 1);
			float yy = y / SKY_Y1;
			for (int i = 0; i < SGW; i++) {
				float x = A * i / (SGW - 1);
				float cl = us_fbm2(x * 2.2f + s->cloudT * 6.0f, y * 5.0f + 3.0f, 3);
				float cloud = us_sat(0.5f + 0.9f * cl);
				float base = us_lerp(top, hor, yy * yy) * (cr ? 1.0f : 0.85f + 0.3f * cloud) + lift * yy;
				float dx = x - moonx, dy = (y - moony) * 1.15f;
				float dm = sqrtf(dx * dx + dy * dy);
				float halo = moonv * (0.22f * expf(-dm / 0.06f) + 0.14f * expf(-dm / 0.2f)) * (cr ? 1.0f : 0.85f + 0.3f * cloud);
				gs[j * SGW + i] = base + halo * (cr ? 0.9f + 0.7f * s->lvl : 1.0f); /* pixel: the moon's halo widens with loudness */
			}
		}
		static const uint8_t mats[1] = {US_MAT_WATER};
		tone(c, s->sky, 1, SGW, SGH, mats, 0.0f, SKY_Y1, NULL);
		/* the moon through thin cloud */
		if (moonv > 0.01f) {
			float cl = us_sat(0.5f + 0.9f * us_fbm2(moonx * 2.2f + s->cloudT * 6.0f, moony * 5.0f + 3.0f, 3));
			float mv = moonv * (0.95f - 0.25f * cl);
			us_disc(c, moonx, moony, 0.026f, mv, US_MAT_ACCENT);
			if (!cr) { /* a few faint maria */
				dab_min(c, moonx - 0.007f, moony - 0.005f, 0.008f, 0.006f, mv * 0.8f);
				dab_min(c, moonx + 0.008f, moony + 0.006f, 0.006f, 0.005f, mv * 0.82f);
			}
		}
	}

	/* ---- far stands receding into mist; far flakes fall among them ---- */
	{
		static const float lv_i[NLAYER] = {0.2f, 0.15f, 0.1f, 0.055f}, lv_c[NLAYER] = {0.42f, 0.34f, 0.04f, 0.05f};
		static const float sv_i[NLAYER] = {0.26f, 0.3f, 0.36f, 0.46f}, sv_c[NLAYER] = {0.55f, 0.5f, 0.5f, 0.6f};
		static const uint8_t lm[NLAYER] = {US_MAT_WATER, US_MAT_WATER, US_MAT_WATER, US_MAT_LEAF};
		static const float mist_y[NLAYER] = {0.585f, 0.612f, 0.64f, 0.672f};
		float lift = 1.0f + 0.25f * dayk;
		int ti = 0;
		for (int L = 0; L < NLAYER; L++) {
			int nshow = 0, total = 0;
			for (int k = ti; k < s->ntr && s->tr[k].layer == L; k++)
				total++;
			nshow = (int)(total * (0.55f + 0.45f * dens) + 0.5f);
			float v = (cr ? lv_c[L] : lv_i[L]) * lift, sv = (cr ? sv_c[L] : sv_i[L]) * (0.85f + 0.15f * night);
			/* pixel canvases: the two farthest stands are a few pixels tall, so draw each as one soft
			 * silhouette band of small fir cones (a per-column skyline), not as hairline trees */
			int band_sil = cr && L < 3;
			int W = c->w < MAXCOL ? c->w : MAXCOL;
			if (band_sil)
				for (int px = 0; px < W; px++)
					s->topS[px] = 1e9f;
			for (int k = 0; k < total; k++) {
				fir_t *f = &s->tr[ti + k];
				/* thin by hash so the stand stays stable as density changes */
				if ((int)(f->x * 977.0f + L * 31) % (total > 0 ? total : 1) >= nshow)
					continue;
				if (band_sil) {
					float tx = f->x * A, wb = f->w * 1.15f + 1.0f / S, top = f->base - f->h;
					int p0 = (int)floorf((tx - wb) * S), p1 = (int)ceilf((tx + wb) * S);
					if (p0 < 0) p0 = 0;
					if (p1 > W) p1 = W;
					for (int px = p0; px < p1; px++) {
						float u = fabsf((px + 0.5f) / S - tx) / wb;
						float y = (top + f->h * 0.95f * u) * S;
						if (y < s->topS[px])
							s->topS[px] = y;
					}
					continue;
				}
				float sway = (0.006f * w->wind * sinf((float)ctx->t * 0.5f + f->ph) + 0.01f * s->gs) * (L + 1) / NLAYER;
				fir_draw(c, f, A, sway, snowk * (L == 0 ? 0.6f : 1.0f), v, sv, lm[L], cr ? 0.0f : v * 0.9f, (L < 2 || (cr && L < 3)) ? US_MAT_WATER : US_MAT_ACCENT);
			}
			if (band_sil) {
				int iv = (int)(v * 255.0f);
				int yb = (int)ceilf((L == 2 ? HORIZ + 0.012f : mist_y[L] + 0.02f) * S);
				if (yb > c->h) yb = c->h;
				for (int px = 0; px < W; px++) {
					int y0 = (int)floorf(s->topS[px] + 0.5f);
					if (s->topS[px] > 1e8f)
						continue;
					for (int py = y0 < 0 ? 0 : y0; py < yb; py++) {
						size_t q = (size_t)py * c->w + px;
						c->ink[q] = (uint8_t)iv;
						c->mat[q] = US_MAT_WATER;
					}
				}
			}
			ti += total;
			/* mist pooling at the feet of this stand (bands light the far mist along the valley) */
			{
				float mf[2 * 24 * 6];
				float mv = (cr ? 0.5f : 0.27f) - 0.04f * L;
				mv *= 0.85f + 0.3f * dayk;
				for (int j = 0; j < 6; j++)
					for (int i = 0; i < 24; i++) {
						float x = i / 23.0f;
						float n = 0.6f + 0.4f * us_noise2(x * 4.0f + s->mistT * (L + 1) * 2.0f, L * 3.3f + 1.0f);
						float yy = j / 5.0f;
						float band = 0.0f;
						if (L == 1) {
							float bf = 4.0f + 22.0f * x;
							int bi = (int)bf;
							band = 0.35f * us_lerp(s->bandv[bi], s->bandv[bi + 1], bf - bi);
						}
						mf[j * 24 + i] = mv * n * yy * yy * (1.0f + band);
					}
				static const uint8_t mm[1] = {US_MAT_WATER};
				tone(c, mf, 1, 24, 6, mm, mist_y[L] - 0.06f, mist_y[L] + 0.006f, NULL);
			}
			if (L == 1) {
				/* far flakes: fine, slow, behind the nearer stands */
				int nf = (int)(MAX_FAR * us_sat(0.15f + 0.85f * s->snow) * (cr ? 0.35f : 1.0f)); /* a pixel each: fewer on crisp canvases */
				for (int i = 0; i < MAX_FAR; i++) {
					flake_t *f = &s->ff[i];
					f->a = us_approach(f->a, i < nf ? 1.0f : 0.0f, 1.5f, rdt);
					f->y += f->vy * dt * (0.8f + 0.4f * s->snow);
					f->x += (wind * 0.6f + gustx * 0.4f + 0.004f * sinf(f->ph + (float)ctx->t * 0.7f)) * dt / A;
					if (f->y > SKY_Y1) {
						new_flake(r, f, 0, 0);
						f->a = i < nf ? 1.0f : 0.0f;
					}
					f->x -= floorf(f->x);
					if (f->a < 0.02f)
						continue;
					us_point(c, f->x * A, f->y, f->r, f->v * f->a * (cr ? 0.62f : 1.0f), US_MAT_WATER);
				}
			}
		}
	}

	/* ---- the clearing: snow ground as a tone field (drifts lit and shadowed, the window's warm pool) ---- */
	float gyc[129];
	for (int i = 0; i <= 128; i++) {
		float xf = i / 128.0f;
		gyc[i] = HORIZ + 0.005f * us_noise2(xf * 3.0f, 9.1f) + 0.003f * us_noise2(xf * 11.0f, 2.2f);
	}
	float cabgy = gyc[(int)(s->cab_x * 128.0f)] + 0.006f;
	float wpx = s->cab_x * A - 0.02f, wpy = cabgy + 0.03f; /* pool of window light */
	float warm = s->warm;
	{
		int W = c->w < MAXCOL ? c->w : MAXCOL;
		for (int px = 0; px < W; px++) {
			float u = (px + 0.5f) / W * 128.0f;
			int i = (int)u;
			if (i > 127) i = 127;
			s->topS[px] = us_lerp(gyc[i], gyc[i + 1], u - i) * S;
		}
		float *gs = s->gnd, *gl = s->gnd + GGW * GGH, *gw_ = s->gnd + 2 * GGW * GGH;
		float base0 = (cr ? 0.3f : 0.15f) + dayk * (cr ? 0.12f : 0.05f) + (cr ? 0.0f : 0.04f * s->lvl);
		float base1 = (cr ? 0.42f : 0.22f) + dayk * (cr ? 0.12f : 0.05f) + (cr ? 0.0f : 0.04f * s->lvl);
		float pool = (0.05f + 0.17f * warm) * (1.0f - 0.6f * dayk) * (cr ? 1.6f : 1.0f);
		/* the snow surface: a soft height field seen in perspective, lit from the moon (upper right) */
		float (*H)[GGW + 1] = s->H;
		if (s->H_aspect != A) {
			for (int j = 0; j <= GGH; j++) {
				float y = GY0 + (1.0f - GY0) * j / (GGH - 1);
				float D = 1.0f / (us_clampf(y - HORIZ, 0.0f, 1.0f) + 0.035f); /* depth */
				for (int i = 0; i <= GGW; i++) {
					float X = ((float)i / (GGW - 1) - 0.5f) * A * D * 0.55f;
					H[j][i] = us_fbm2(X * 0.6f + 3.1f, D * 0.42f, 3) + 0.5f * us_noise2(X * 0.25f, D * 0.15f + 7.0f);
				}
			}
			s->H_aspect = A;
		}
		for (int j = 0; j < GGH; j++) {
			float y = GY0 + (1.0f - GY0) * j / (GGH - 1);
			float z = us_sat((y - HORIZ) / (1.0f - HORIZ));
			for (int i = 0; i < GGW; i++) {
				float xf = (float)i / (GGW - 1), x = xf * A;
				float v = us_lerp(base0, base1, sqrtf(z));
				/* facing the moon (up the slope, toward the right) = lit; facing away = blue shade */
				float sl = (H[j][i] - H[j + 1][i]) * 1.0f + (H[j][i + 1] - H[j][i]) * 0.5f;
				float lit = us_sat(sl * 3.0f);
				v *= 1.0f + us_clampf(sl * 1.2f, -0.35f, 0.3f) * us_smooth(0.05f, 0.5f, z);
				for (int d = 0; d < MAX_DRIFT; d++) {
					float ext, cy = crest_y(&s->dr[d], xf, &ext);
					if (ext <= 0.0f)
						continue;
					float dz = s->dr[d].z, dy = y - cy, sc = 0.008f + 0.05f * dz * dz;
					if (dy < 0.0f && dy > -sc * 0.7f)
						v += 0.05f * ext * (1.0f + dy / (sc * 0.7f));
					else if (dy >= 0.0f)
						v -= 0.075f * ext * expf(-dy / sc) * (cr ? 1.3f : 1.0f);
				}
				float lf = us_sat(1.3f * lit - 0.1f + 0.5f * dayk) * 0.8f;
				gs[j * GGW + i] = v * (1.0f - lf);
				gl[j * GGW + i] = v * lf;
				float pr = 0.7f + 0.6f * warm; /* the pool spreads as the hearth (and the music) warms */
				float dx = (x - wpx) / (0.11f * pr), dyp = (y - wpy) / (0.03f * pr);
				float g = pool * expf(-(dx * dx + dyp * dyp));
				/* the window's light falls out onto the snow as a soft fan */
				float fx = (x - wpx) / (0.02f + 0.3f * us_sat(y - cabgy));
				if (y > cabgy)
					g += pool * 0.45f * expf(-fx * fx) * expf(-(y - cabgy) / 0.1f);
				gw_[j * GGW + i] = g;
			}
		}
		for (int q = 0; q < GGW * GGH; q++) { /* lit + shaded snow are one blue tone; white is kept for the glints */
			gs[q] += gl[q];
			gl[q] = gw_[q];
		}
		static const uint8_t mats[2] = {US_MAT_WATER, US_MAT_LIGHT};
		tone(c, s->gnd, 2, GGW, GGH, mats, GY0, 1.0f, s->topS);
		/* a fine moonlit line along the far edge of the clearing + along each drift crest */
		if (!cr) {
			float pts[2 * 65];
			for (int i = 0; i <= 64; i++) {
				float xf = i / 64.0f;
				pts[2 * i] = xf * A;
				pts[2 * i + 1] = gyc[i * 2] + 0.0008f;
			}
			us_polyline(c, pts, 65, 0.0009f, 0.34f + 0.1f * dayk, US_MAT_WATER);
		}
		/* each drift's crest catches the moon: a fine white line, tapering at its ends */
		for (int d = 0; d < MAX_DRIFT; d++) {
			drift_t *dr = &s->dr[d];
			float px = 0.0f, py = 0.0f;
			for (int i = 0; i <= 40; i++) {
				float xf = us_lerp(dr->x0, dr->x1, i / 40.0f), ext;
				float y = crest_y(dr, xf, &ext);
				if (i > 0 && ext > 0.05f) {
					float v = (0.22f + 0.2f * dr->z) * ext * (0.85f + 0.3f * dayk);
					if (cr)
						us_line(c, px, py, xf * A, y, 0.0045f, v * 1.6f, US_MAT_WATER);
					else
						us_line(c, px, py, xf * A, y, 0.0008f + 0.001f * dr->z * ext, v, US_MAT_ACCENT);
				}
				px = xf * A;
				py = y;
			}
		}
	}

	/* ---- tracks: a path of boot prints from the door down out of the picture, and the deer's ---- */
	{
		float dx0 = s->cab_x * A + 0.021f, dy0 = cabgy + 0.004f;
		float ex = s->cab_x * A + 0.32f, ey = 1.03f;
		float mx = s->cab_x * A + 0.05f, my = 0.82f;
		float sv = cr ? 0.18f : 0.12f;
		for (int k = 1; k < 40; k++) {
			float t = k / 40.0f;
			float bx = (1 - t) * (1 - t) * dx0 + 2 * (1 - t) * t * mx + t * t * ex;
			float by = (1 - t) * (1 - t) * dy0 + 2 * (1 - t) * t * my + t * t * ey;
			float tx = 2 * (1 - t) * (mx - dx0) + 2 * t * (ex - mx), ty = 2 * (1 - t) * (my - dy0) + 2 * t * (ey - my);
			float tl = sqrtf(tx * tx + ty * ty) + 1e-6f;
			float nx = -ty / tl, ny = tx / tl;
			float z = us_sat((by - HORIZ) / (1.0f - HORIZ));
			float sc = 0.25f + 1.6f * z;
			float side = (k & 1) ? 1.0f : -1.0f;
			dab_min(c, bx + nx * 0.004f * sc * side, by + ny * 0.0015f * sc * side, 0.0035f * sc, 0.0012f * sc, sv + 0.06f * (1.0f - z));
		}
		for (int i = 0; i < MAX_PRINT; i++) {
			print_t *p = &s->pr[i];
			p->age += dt * (0.4f + s->snow);
			if (p->age > 120.0f)
				continue;
			float f = 1.0f - p->age / 120.0f;
			float tgt = sv + (1.0f - f) * 0.25f;
			dab_min(c, p->x * A, p->y, 0.0016f * p->s, 0.0007f * p->s, tgt);
		}
	}

	/* ---- crystals in the snow (glint on hats) ---- */
	for (int i = 0; i < MAX_CRYS; i++) {
		crys_t *k = &s->cr[i];
		k->gl *= gdecay;
		float x = k->x * A, y = k->y;
		int px = (int)(x * S);
		if (px < 0 || px >= c->w || y * S < s->topS[px] + 1.0f)
			continue;
		float near_win = expf(-((x - wpx) * (x - wpx) / 0.04f + (y - wpy) * (y - wpy) / 0.006f));
		float base = cr ? 0.0f : 0.16f * (0.4f + 0.6f * us_noise2(i * 3.1f, (float)ctx->t * 0.6f)) * (0.4f + 0.6f * s->hi);
		float v = (base + 0.9f * k->gl) * k->m;
		if (v < 0.08f)
			continue;
		uint8_t m = near_win * warm > 0.25f ? US_MAT_LIGHT : US_MAT_ACCENT;
		float z = us_sat((y - HORIZ) / (1.0f - HORIZ));
		float ln = (0.003f + 0.006f * z) * (0.3f + 1.2f * sinf(US_PI * us_sat(1.1f - k->gl)));
		if (cr)
			us_point(c, x, y, k->gl > 0.3f ? 0.004f : 0.002f, v, m);
		else {
			us_point(c, x, y, 0.0009f, v, m);
			if (k->gl > 0.3f) {
				us_line(c, x - ln, y, x + ln, y, 0.0008f, v * 0.85f, m);
				us_line(c, x, y - ln * 0.8f, x, y + ln * 0.8f, 0.0008f, v * 0.85f, m);
			}
		}
	}

	/* ---- the cabin ---- */
	cabin(s, ctx, c, cabgy, warm, day, snowk);

	/* ---- hares and the deer, far to near ---- */
	deer_step(s, ctx, dens);
	hares_step(s, ctx, 1 + (int)(dens * 2.2f + 0.3f));
	for (int i = 0; i < MAX_HARE; i++)
		if (s->hr[i].away <= 0.0f && (!s->deer_on || s->hr[i].y < s->deer_y))
			hare_draw(&s->hr[i], c, A, winter, (float)ctx->t, i, s->lvl);
	if (s->deer_on)
		deer_draw(s, c, A, dayk);
	for (int i = 0; i < MAX_HARE; i++)
		if (s->hr[i].away <= 0.0f && s->deer_on && s->hr[i].y >= s->deer_y)
			hare_draw(&s->hr[i], c, A, winter, (float)ctx->t, i, s->lvl);

	/* ---- the lone fir in the clearing, with the snow mounded at its foot ---- */
	{
		fir_t *f = &s->tr[s->lone];
		float sway = 0.01f * w->wind * sinf((float)ctx->t * 0.6f + f->ph) + 0.006f * gustx - 0.012f * s->kd + 0.03f * s->gs;
		fir_draw(c, f, A, sway, snowk, cr ? 0.0f : 0.045f, cr ? 0.8f : 0.66f, US_MAT_LEAF, cr ? 0.0f : 0.06f, US_MAT_ACCENT);
		float x = f->x * A, y = f->base;
		float p[2 * 10];
		for (int i = 0; i < 10; i++) {
			float t = i / 9.0f;
			p[2 * i] = x + (t - 0.5f) * 0.11f;
			p[2 * i + 1] = y - 0.012f * sinf(US_PI * t) * (0.6f + 0.4f * snowk) + 0.001f;
		}
		float q[2 * 12];
		memcpy(q, p, sizeof(p));
		q[20] = x + 0.055f;
		q[21] = y + 0.008f;
		q[22] = x - 0.055f;
		q[23] = y + 0.008f;
		poly_set(c, q, 12, cr ? 0.5f : 0.33f, US_MAT_WATER);
		if (!cr)
			us_polyline(c, p, 10, 0.0009f, 0.45f, US_MAT_WATER);
	}

	/* ---- the fox, mousing in the clearing ---- */
	fox_step(s, ctx, dens);
	if (s->fox_on)
		fox_draw(s, c, A, dayk, (float)ctx->t);

	/* ---- mid flakes ---- */
	{
		int nf = (int)(MAX_MID * us_sat(0.12f + 0.88f * s->snow) * (cr ? 0.55f : 1.0f));
		for (int i = 0; i < MAX_MID; i++) {
			flake_t *f = &s->fm[i];
			f->a = us_approach(f->a, i < nf ? 1.0f : 0.0f, 1.2f, rdt);
			f->gl *= gdecay;
			f->y += f->vy * dt * (0.8f + 0.4f * s->snow);
			f->x += (wind + gustx + 0.008f * sinf(f->ph + (float)ctx->t * 0.9f)) * dt / A;
			if (f->y > 1.03f) {
				new_flake(r, f, 1, 0);
				f->a = i < nf ? 1.0f : 0.0f;
			}
			f->x -= floorf(f->x);
			if (f->a < 0.02f)
				continue;
			float x = f->x * A;
			float lw = expf(-((x - wpx) * (x - wpx) + (f->y - cabgy + 0.02f) * (f->y - cabgy + 0.02f)) / 0.012f) * warm;
			float v = f->v * f->a * (1.0f + 0.4f * lw) + f->gl * 0.6f;
			uint8_t m = lw > 0.3f ? US_MAT_LIGHT : US_MAT_WATER;
			us_point(c, x, f->y, cr ? 0.002f : f->r * (1.0f + 0.4f * f->gl), cr ? v * 0.7f : v, m);
			if (f->gl > 0.2f && !cr) {
				float ln = 0.007f * f->gl;
				us_line(c, x - ln, f->y, x + ln, f->y, 0.0007f, f->gl * 0.7f, US_MAT_ACCENT);
				us_line(c, x, f->y - ln, x, f->y + ln, 0.0007f, f->gl * 0.7f, US_MAT_ACCENT);
			}
		}
	}

	/* ---- frosted grass through the snow in the foreground: wind, the gust, the kick's thump ---- */
	grass_draw(s, c, A, 0.25f * w->wind * sinf((float)ctx->t * 0.7f) + 0.35f * s->gs - 0.1f * s->kd, (float)ctx->t);

	/* ---- the two big frame firs, and the owl on the left one ---- */
	{
		float nv = cr ? 0.0f : 0.022f, nsv = cr ? 0.85f : 0.74f;
		for (int q = 0; q < 2; q++) {
			fir_t *f = &s->tr[q ? s->nearR : s->nearL];
			float sway = (0.008f * w->wind + 0.003f) * sinf((float)ctx->t * 0.45f + f->ph) + 0.004f * gustx - (q ? -0.008f : 0.008f) * s->kd + 0.016f * s->gs; /* the thump rocks them, the gust bends them */
			fir_draw(c, f, A, sway, snowk, nv, nsv * (0.8f + 0.2f * night), US_MAT_LEAF, cr ? 0.0f : 0.04f, US_MAT_ACCENT);
			s->fir_sway[q] = sway;
		}
		/* bullfinches on the boughs (more with the shared flock); they roost fluffed up at night */
		birds_step(s, ctx, snowk, 2 + (int)(dens * 3.0f + 1.5f * us_sat(w->flock)), night);
		for (int i = 0; i < MAX_BIRD; i++)
			if (s->bd[i].state <= 2)
				bird_draw(&s->bd[i], c, night, s->lvl);
		/* perch: on a bough of the left fir, partway out from the trunk, riding the bough's spring */
		fir_t *f = &s->tr[s->nearL];
		int k = f->nt - 5, sd = 1;
		tiers_t T;
		fir_tiers(f, snowk, &T);
		float ytop = f->base - f->h, hf = f->h * 0.9f;
		float d = 0.6f * T.E[k][sd];
		float uu = 0.0f;
		while (uu < 1.0f && T.E[k][sd] * bough(uu) < d)
			uu += 0.02f;
		float te = T.a[k][sd] + uu * (T.b[k][sd] - T.a[k][sd]) - 0.004f / hf;
		s->owl_px = f->x * A + d;
		s->owl_py = ytop + te * hf;
		float sc = 0.042f;
		s->owl_blink -= rdt;
		if (s->owl_blink < -4.5f)
			s->owl_blink = 0.18f;
		s->owl_look_t -= dt;
		if (s->owl_look_t <= 0.0f) {
			s->owl_head_to = us_randr(r, -1.0f, 1.0f);
			s->owl_look_t = us_randr(r, 2.0f, 7.0f);
			if (us_rand(r) < 0.3f)
				s->owl_blink = 0.18f;
		}
		s->owl_head = us_approach(s->owl_head, s->owl_head_to, 0.15f, dt);
		s->owl_away -= dt;
		if (s->owl_state == 0) {
			owl_draw(c, s->owl_px, s->owl_py, sc, 0, 0.0f, s->owl_head, s->owl_blink > 0.0f ? 1.0f : 0.0f, 1.0f, dayk);
		} else if (s->owl_state == 1) { /* flying off to the right over the clearing */
			s->owl_t += dt;
			s->owl_flap += dt * 11.0f;
			s->owl_vx = us_approach(s->owl_vx, 0.32f, 0.8f, dt);
			s->owl_vy = us_approach(s->owl_vy, -0.03f, 1.0f, dt);
			s->owl_x += s->owl_vx * dt;
			s->owl_y += (s->owl_vy + 0.02f * sinf(s->owl_flap)) * dt;
			owl_draw(c, s->owl_x, s->owl_y, sc, 1, s->owl_flap, 0.0f, 0.0f, 1.0f, dayk);
			if (s->owl_x > A + 0.1f) {
				s->owl_state = 2;
				s->owl_t = us_randr(r, 20.0f, 45.0f);
			}
		} else if (s->owl_state == 2) {
			s->owl_t -= dt;
			if (s->owl_t <= 0.0f) {
				s->owl_state = 3;
				s->owl_x = -0.08f;
				s->owl_y = s->owl_py - 0.15f;
				s->owl_t = 0.0f;
			}
		} else { /* gliding back in to the perch */
			s->owl_t += dt;
			s->owl_flap += dt * (s->owl_t < 1.0f ? 9.0f : 3.0f);
			s->owl_x = us_approach(s->owl_x, s->owl_px, 0.9f, dt);
			s->owl_y = us_approach(s->owl_y, s->owl_py, 0.9f, dt);
			owl_draw(c, s->owl_x, s->owl_y, sc, 1, s->owl_flap, 0.0f, 0.0f, 1.0f, dayk);
			if (fabsf(s->owl_x - s->owl_px) < 0.004f && fabsf(s->owl_y - s->owl_py) < 0.004f) {
				s->owl_state = 0;
				s->owl_away = 40.0f;
			}
		}
	}

	/* ---- glints on the bough snow ---- */
	{
		for (int i = 0; i < MAX_CG; i++) {
			float g = s->cg[i].gl;
			if (g < 0.03f)
				continue;
			s->cg[i].gl = g * gdecay;
			int ti = s->cg[i].t;
			fir_t *f = ti == 0 ? &s->tr[s->lone] : &s->tr[ti == 1 ? s->nearL : s->nearR];
			int k = s->cg[i].k, sd = s->cg[i].sd;
			if (k >= f->nt)
				continue;
			tiers_t T;
			fir_tiers(f, snowk, &T);
			float u = s->cg[i].u, a0 = T.a[k][sd], b0 = T.b[k][sd];
			float t = a0 + u * (b0 - a0) - T.th[k][sd] * 0.4f;
			float hw = T.E[k][sd] * bough(u) * 0.97f;
			float x = f->x * A + (sd ? hw : -hw), y = f->base - f->h + t * f->h * 0.9f;
			float ln = (0.004f + 0.006f * f->h) * sinf(US_PI * us_sat(1.15f - g)) * 1.3f;
			if (cr)
				us_disc(c, x, y, 0.004f, g, US_MAT_ACCENT);
			else {
				us_disc(c, x, y, 0.0018f, g, US_MAT_ACCENT);
				us_line(c, x - ln, y, x + ln, y, 0.0009f, g * 0.9f, US_MAT_ACCENT);
				us_line(c, x, y - ln, x, y + ln, 0.0009f, g * 0.9f, US_MAT_ACCENT);
			}
		}
	}

	/* ---- clumps, powder, flurries ---- */
	parts_step_draw(s, c, rdt, A, (wind + gustx) * A);

	/* ---- near flakes: big and soft ---- */
	{
		int nf = (int)(MAX_NEAR * us_sat(0.1f + 0.9f * s->snow) * (0.5f + 0.5f * dens));
		for (int i = 0; i < MAX_NEAR; i++) {
			flake_t *f = &s->fn[i];
			f->a = us_approach(f->a, i < nf ? 1.0f : 0.0f, 1.0f, rdt);
			f->gl *= gdecay;
			f->y += f->vy * dt * (0.8f + 0.4f * s->snow);
			f->x += (wind * 1.6f + gustx * 1.5f + 0.02f * sinf(f->ph + (float)ctx->t * 0.8f)) * dt / A;
			if (f->y > 1.06f) {
				new_flake(r, f, 2, 0);
				f->a = i < nf ? 1.0f : 0.0f;
			}
			f->x -= floorf(f->x);
			if (f->a < 0.02f)
				continue;
			float x = f->x * A, v = f->v * f->a;
			if (cr) {
				us_disc(c, x, f->y, f->r * 0.9f, v * 0.7f + f->gl * 0.5f, US_MAT_ACCENT);
			} else {
				us_disc(c, x, f->y, f->r, v * 0.35f, US_MAT_WATER);
				us_disc(c, x, f->y, f->r * 0.55f, v * (0.75f + 0.5f * f->gl), US_MAT_ACCENT);
				if (f->gl > 0.2f) {
					float ln = f->r * 1.8f * f->gl;
					us_line(c, x - ln, f->y, x + ln, f->y, 0.0008f, f->gl * 0.8f, US_MAT_ACCENT);
					us_line(c, x, f->y - ln, x, f->y + ln, 0.0008f, f->gl * 0.8f, US_MAT_ACCENT);
				}
			}
		}
	}
}

const us_scene_vt us_scene_snowfall = {"snowfall", "Snowfall", 0.06f, create, destroy, frame};
