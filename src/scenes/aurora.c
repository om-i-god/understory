/* Understory scene: Aurora — curtains of light over a still northern lake.
 *
 * Each curtain is a ribbon path through the sky whose travelling folds double it back on itself,
 * with hundreds of fine vertical rays hanging upward from a sharp, bright lower rim, drawn
 * additively so folds and overlaps glow. Rays are graded: a bright lower rim, a green body, a faint magenta
 * fringe at the top (BLOOM in Pixel palettes). Ripples travel along the ribbon.
 * Audio: stereo width + highs/air -> ripple speed and ray flicker; bass/kick -> brightness surge
 *        (the curtain "breathes" brighter on every low hit); centroid -> curtain height.
 *        kick -> a pulse of light running outward along each curtain; bands[] -> each stretch of a
 *        curtain lights and lengthens with its own band; sub -> a red lower border below the rim, and
 *        the lake ice booms (a crack glints); air -> stars scintillate; strong onsets -> the odd
 *        shooting star; lowmid -> the cabin hearth (window + smoke); width -> curtain spread;
 *        swell -> curtain count + the quiet arc.
 * World: daylight (by day only a pale washed ghost remains), season (strongest in winter),
 *        storm_charge (a geomagnetic storm: more curtains, taller red fringes, faster motion).
 * Silence: curtains keep folding slowly with a 9 s breath, a quiet arc glows low over the ridge,
 *        stars twinkle, the odd meteor falls, a fox sometimes trots along the shore past the lit cabin.
 * The scene is reflected (compressed, wobbling) in the lake below the treeline: curtains, their
 * rims, the far ridge and the spruce stand. The reflection carries the whole curtain (fringe colour
 * included); the sky's glow lights the snowy crests, the foothills and a glimmer on the water.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAX_CURT 5
#define MAX_RAYS 560
#define MAX_STARS 500
#define MAX_TREES 110

#define SHORE 0.75f
#define MAX_CRACK 7
#define CRACK_PTS 9
#define MAX_PULSE 3

typedef struct {
	float seed;      /* noise offset */
	float y0;        /* lower edge altitude */
	float xc, span;  /* centre (fraction of aspect) + span (fraction) */
	float fold;      /* fold amplitude */
	float life;      /* 0..1 fade in/out */
	float target;    /* 1 = should exist */
	float ph;        /* ripple phase */
	float drift;
} curtain_t;

typedef struct {
	float x, y, m, tw; /* x fraction 0..1 */
} star_t;

typedef struct {
	float x, h, w; /* x fraction */
} spruce_t;

typedef struct {
	us_rng rng;
	curtain_t cu[MAX_CURT];
	star_t st[MAX_STARS];
	spruce_t tr[MAX_TREES];
	int ntr;
	float T;          /* slow time */
	float surge;      /* bass envelope */
	float shimmer;    /* highs/width envelope */
	float height;     /* centroid envelope */
	float flick_t;
	float twk; /* star twinkle phase: runs faster with air */
	float meteor_t, meteor_x, meteor_y, meteor_vx, meteor_vy, next_meteor;
	int inited;
	/* reactive envelopes */
	float bandv[US_NBANDS]; /* per-band envelopes: each band lights its own stretch of curtain */
	float sub_env, air_env, lm_env, spread;
	float pulse_age[MAX_PULSE]; /* kick pulses running outward along the curtains */
	int pulse_i;
	float kick_cool, met_cool;
	/* ice cracks along the near lake edge; sub hits make the ice boom and a crack glints */
	float crack[MAX_CRACK][2 * CRACK_PTS];
	float crack_g[MAX_CRACK];
	/* a lit cabin in a clearing on the far shore; a fox that sometimes trots along the shore */
	float cab_x;
	float fox_x, fox_dir, fox_ph, next_fox;
	int fox_on;
} st_t;

static float ridge_y(float u)
{
	/* far mountains: ridged noise */
	float n = 1.0f - fabsf(us_noise2(u * 3.1f, 11.3f));
	float n2 = 1.0f - fabsf(us_noise2(u * 7.7f, 2.9f));
	return 0.705f - 0.14f * n * n * (0.7f + 0.3f * n2) - 0.03f * n2 * n2 + 0.02f * us_noise2(u * 1.3f, 4.1f);
}

static float hash1(int k, float seed)
{
	uint32_t h = (uint32_t)k * 2654435761u ^ (uint32_t)(seed * 1000.0f) * 40503u;
	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return (h & 0xffffff) / 16777216.0f;
}

/* One ray, rasterised row by row: smooth brightness profile from a sharp lower rim fading upward,
 * green body (LEAF) turning to a magenta fringe (BLOOM) above fringe_t. dir = -1 draws upward (sky),
 * +1 draws downward (reflection). */
static void ray(us_canvas *c, float x, float yb, float ln, float lean, float b, float fringe_t, int dir, uint8_t body, float flat)
{
	float S = c->s;
	float py0 = yb * S, len = ln * S;
	if (len < 1.0f || b < 0.004f)
		return;
	int n = (int)len;
	float e = 1.0f, ek = expf(-2.6f / len);
	for (int i = 0; i <= n; i++, e *= ek) {
		float t = i / len;
		float py = py0 + dir * i;
		int iy = (int)floorf(py);
		if (iy < 0 || iy >= c->h) {
			if ((dir < 0 && iy < 0) || (dir > 0 && iy >= c->h))
				break;
			continue;
		}
		float v = b * (0.16f + 0.84f * (flat + (1.0f - flat) * e));
		if (t < 0.05f)
			v *= us_smooth(0.0f, 0.05f, t);
		else if (t > 0.8f)
			v *= us_smooth(1.0f, 0.8f, t);
		uint8_t m = t > fringe_t ? US_MAT_BLOOM : body;
		if (t > fringe_t)
			v *= 1.25f;
		float px = (x + lean * t) * S - 0.5f;
		if (c->crisp) {
			us_put(c, (int)floorf(px + 0.5f), iy, v, m);
		} else {
			int ix = (int)floorf(px);
			float fx = px - ix;
			us_put(c, ix, iy, v * (1.0f - fx), m);
			us_put(c, ix + 1, iy, v * fx, m);
		}
	}
}

/* thin additive line for the curtain rim (Wu spans: two pixels per step, respects the ADD blend) */
static void rim(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
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
	for (int i = i0; i <= i1 && i - i0 < 400; i++) {
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

/* the curtain's lower edge, as a fraction of the width: span + travelling folds (doubling back where
 * their sway outruns the span) */
static float curtain_u2x(const curtain_t *cu, float u, float T, float spread)
{
	float fa = u * US_TAU * 1.3f + T * 0.55f + cu->seed;
	float fb = u * US_TAU * 2.7f - T * 0.4f + cu->seed * 1.7f;
	float env = 0.35f + 0.65f * sinf(US_PI * us_sat(u));
	float fold = cu->fold * (env * (0.42f * sinf(fa) + 0.18f * sinf(fb)) + 0.9f * us_fbm2(u * 1.8f + T * 0.3f, cu->seed, 2));
	return cu->xc + cu->span * spread * (u - 0.5f) + fold;
}

/* hairline polyline for ink (Wu spans, ~0.8 px): cheaper than us_polyline for the treeline */
static void hpoly(us_canvas *c, const float *xy, int n, float v, uint8_t mat)
{
	for (int i = 0; i + 1 < n; i++)
		rim(c, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], v * 0.8f, mat);
}

static void wline_(us_canvas *c, int crisp, float ax, float ay, float bx, float by, float v, uint8_t m)
{
	if (crisp)
		us_line(c, ax, ay, bx, by, 0.004f, v, m);
	else
		rim(c, ax, ay, bx, by, v, m);
}

static void new_curtain(st_t *s, curtain_t *c)
{
	us_rng *r = &s->rng;
	c->seed = us_randr(r, 0, 100);
	c->y0 = us_randr(r, 0.36f, 0.54f);
	c->xc = us_randr(r, 0.3f, 0.7f);
	c->span = us_randr(r, 0.9f, 1.6f);
	c->fold = us_randr(r, 0.16f, 0.3f);
	c->ph = us_rand(r) * US_TAU;
	c->drift = us_randr(r, -0.012f, 0.012f);
	c->life = 0;
	c->target = 1;
}

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng *r = &s->rng;
	us_rng_seed(r, seed ^ 0x6175726fu);
	for (int i = 0; i < MAX_STARS; i++) {
		star_t *t = &s->st[i];
		t->x = us_rand(r);
		t->y = us_randr(r, 0.0f, 0.68f);
		float m = us_rand(r);
		t->m = 0.08f + 0.92f * m * m * m * m;
		t->tw = us_rand(r) * 50;
	}
	s->ntr = MAX_TREES;
	for (int i = 0; i < MAX_TREES; i++) {
		spruce_t *t = &s->tr[i];
		t->x = us_rand(r);
		/* clustered stands: taller where a low noise says so */
		float stand = 0.5f + 0.5f * us_noise2(t->x * 6.0f, 1.7f);
		t->h = us_randr(r, 0.022f, 0.04f) + 0.035f * stand * us_rand(r);
		t->w = t->h * us_randr(r, 0.28f, 0.38f);
	}
	/* sort trees far-to-near is irrelevant (same depth); sort by height so tall ones draw last */
	for (int i = 1; i < MAX_TREES; i++) {
		spruce_t k = s->tr[i];
		int j = i - 1;
		while (j >= 0 && s->tr[j].h > k.h) {
			s->tr[j + 1] = s->tr[j];
			j--;
		}
		s->tr[j + 1] = k;
	}
	for (int i = 0; i < MAX_CURT; i++) {
		new_curtain(s, &s->cu[i]);
		s->cu[i].target = 0;
	}
	s->T = us_rand(r) * 100;
	s->next_meteor = us_randr(r, 5, 20);
	for (int i = 0; i < MAX_PULSE; i++)
		s->pulse_age[i] = 9.0f;
	s->spread = 1.0f;
	/* ice cracks: wandering, mostly shore-parallel lines in the near water at both edges */
	for (int i = 0; i < MAX_CRACK; i++) {
		float x = i & 1 ? us_randr(r, 0.0f, 0.28f) : us_randr(r, 0.72f, 1.0f);
		float y = us_randr(r, 0.84f, 0.985f);
		float ang = (x < 0.5f ? 0.0f : US_PI) + us_randr(r, -0.5f, 0.5f);
		float step = us_randr(r, 0.012f, 0.022f);
		for (int k = 0; k < CRACK_PTS; k++) {
			s->crack[i][2 * k] = x;
			s->crack[i][2 * k + 1] = y;
			ang += us_randr(r, -0.7f, 0.7f);
			x += cosf(ang) * step / 1.78f; /* x is a width fraction */
			y += sinf(ang) * step * 0.35f;  /* foreshortened on the flat lake */
		}
	}
	s->cab_x = us_randr(r, 0.58f, 0.8f);
	s->next_fox = us_randr(r, 15, 35);
	return s;
}

static void destroy(void *p) { free(p); }

static void spruce(us_canvas *c, int crisp, float x, float y, float h, float w, float v)
{
	/* 4 tiers of drooping boughs */
	if (crisp) {
		for (int k = 0; k < 4; k++) {
			float t0 = k / 4.0f, t1 = (k + 1.4f) / 4.0f;
			float ty = y - h + h * t0;
			float by = y - h + h * (t1 > 1 ? 1 : t1) - h * 0.08f;
			float bw = w * (0.35f + 0.65f * t1);
			us_tri(c, x, ty, x - bw, by, x + bw, by, v, US_MAT_LEAF);
		}
		us_line(c, x, y - h * 0.1f, x, y, 0.004f, v, US_MAT_STEM);
		return;
	}
	float pts[2 * 11];
	int n = 0;
	pts[n++] = x - w * 0.15f;
	pts[n++] = y;
	for (int k = 3; k >= 0; k--) {
		float t = (k + 1) / 4.0f;
		pts[n++] = x - w * (0.3f + 0.7f * t);
		pts[n++] = y - h + h * t - h * 0.04f;
		pts[n++] = x - w * 0.2f * t;
		pts[n++] = y - h + h * t - h * 0.2f;
	}
	pts[n++] = x;
	pts[n++] = y - h;
	hpoly(c, pts, n / 2, v, US_MAT_LEAF);
	n = 0;
	pts[n++] = x;
	pts[n++] = y - h;
	for (int k = 0; k < 4; k++) {
		float t = (k + 1) / 4.0f;
		pts[n++] = x + w * 0.2f * t;
		pts[n++] = y - h + h * t - h * 0.2f;
		pts[n++] = x + w * (0.3f + 0.7f * t);
		pts[n++] = y - h + h * t - h * 0.04f;
	}
	pts[n++] = x + w * 0.15f;
	pts[n++] = y;
	hpoly(c, pts, n / 2, v, US_MAT_LEAF);
}

/* a log cabin in its clearing: walls, roof, a lit window (lowmid = the hearth's warmth) and curling smoke */
static void cabin(st_t *s, const us_ctx *ctx, us_canvas *c, float day)
{
	float A = ctx->aspect, x = s->cab_x * A, y = SHORE + 0.001f;
	float w = 0.026f, h = 0.014f, rh = 0.012f;
	float lv = 0.30f + 0.25f * day;
	int cr = ctx->crisp;
	if (cr) {
		us_tri(c, x - w * 0.5f, y - h, x + w * 0.5f, y - h, x - w * 0.5f, y, lv, US_MAT_EARTH);
		us_tri(c, x + w * 0.5f, y - h, x + w * 0.5f, y, x - w * 0.5f, y, lv, US_MAT_EARTH);
		us_tri(c, x - w * 0.62f, y - h, x + w * 0.62f, y - h, x, y - h - rh, lv, US_MAT_STEM);
	} else {
		float pts[] = {x - w * 0.5f, y, x - w * 0.5f, y - h, x - w * 0.62f, y - h, x, y - h - rh, x + w * 0.62f, y - h,
			       x + w * 0.5f, y - h, x + w * 0.5f, y};
		hpoly(c, pts, 7, lv * 1.2f, US_MAT_STEM);
		rim(c, x - w * 0.5f, y - h, x + w * 0.5f, y - h, lv * 0.7f, US_MAT_STEM);
		for (int k = 1; k < 3; k++) /* log courses */
			rim(c, x - w * 0.5f, y - h * k / 3.0f, x + w * 0.5f, y - h * k / 3.0f, lv * 0.35f, US_MAT_STEM);
	}
	/* chimney */
	float chx = x + w * 0.28f, chy = y - h - rh * 0.45f;
	wline_(c, cr, chx, chy, chx, chy - rh * 0.5f, lv, US_MAT_STEM);
	/* window: warm, flickering gently with the hearth (lowmid), dimmer by day */
	float fire = 0.55f + 0.3f * s->lm_env + 0.08f * us_noise2((float)ctx->t * 3.1f, 5.0f);
	float wv = fire * (1.0f - 0.6f * us_smooth(0.3f, 0.8f, day));
	float wx = x - w * 0.18f, wy = y - h * 0.5f;
	if (cr) {
		us_disc(c, wx, wy, 0.0045f, wv, US_MAT_LIGHT);
	} else {
		us_disc(c, wx, wy, 0.008f, wv * 0.07f, US_MAT_LIGHT); /* glow on the snow */
		us_line(c, wx - 0.0028f, wy, wx + 0.0028f, wy, 0.0042f, wv, US_MAT_LIGHT);
	}
	/* its reflection: a broken warm streak on the water */
	for (int k = 0; k < (cr ? 2 : 4); k++) {
		float ry = SHORE + 0.008f + 0.0085f * k + 0.002f * us_noise2(k * 3.0f, (float)ctx->t * 0.8f);
		float hw = 0.0035f * (1.0f + 0.4f * us_noise2(k * 1.7f, (float)ctx->t * 1.1f));
		wline_(c, cr, wx - hw, ry, wx + hw, ry, wv * (0.5f - 0.1f * k), US_MAT_LIGHT);
	}
	/* smoke: rises, leans with the wind and the stereo spread, curls; thicker as the hearth warms */
	float px = chx, py = chy - rh * 0.5f;
	float lean = 0.25f + 0.6f * ctx->w->wind + 0.3f * (s->spread - 0.8f);
	for (int k = 1; k <= 9; k++) {
		float t = k / 9.0f;
		float nx = chx + lean * 0.05f * t * t + 0.006f * t * us_noise2(t * 3.0f - (float)ctx->t * 0.4f, 9.0f);
		float ny = chy - rh * 0.5f - 0.055f * t;
		wline_(c, cr, px, py, nx, ny, (0.16f + 0.1f * s->lm_env) * (1.0f - t), US_MAT_WATER);
		px = nx;
		py = ny;
	}
}

/* now and then a fox trots along the shore in front of the spruces, with its reflection */
static void fox(st_t *s, const us_ctx *ctx, us_canvas *c, float day)
{
	float A = ctx->aspect, rdt = ctx->real_dt;
	if (!s->fox_on) {
		s->next_fox -= ctx->dt;
		if (s->next_fox > 0)
			return;
		s->fox_on = 1;
		s->fox_dir = us_rand(&s->rng) < 0.5f ? 1.0f : -1.0f;
		s->fox_x = s->fox_dir > 0 ? -0.04f : A + 0.04f;
	}
	/* a steady trot; a beat-quick lope in loud passages */
	float sp = 0.045f * (1.0f + 0.4f * ctx->a->level);
	s->fox_x += s->fox_dir * sp * ctx->dt;
	s->fox_ph += ctx->dt * 9.0f * (1.0f + 0.4f * ctx->a->level);
	(void)rdt;
	if (s->fox_x < -0.06f || s->fox_x > A + 0.06f) {
		s->fox_on = 0;
		s->next_fox = us_randr(&s->rng, 45, 110);
		return;
	}
	float d = s->fox_dir, x = s->fox_x, y = SHORE - 0.0005f;
	float L = 0.016f, H = 0.006f, leg = 0.0045f;
	float v = 0.42f + 0.2f * day;
	int cr = ctx->crisp;
	for (int m = 0; m < 2; m++) { /* m = 1: the reflection, flipped and dim */
		float sg = m ? -1.0f : 1.0f, vv = m ? v * 0.35f : v;
		float by = y - sg * leg; /* body line */
		float bob = 0.0006f * sinf(s->fox_ph * 2.0f);
		by -= sg * bob;
		/* body + head + ears + snout */
		float hx = x + d * L * 0.55f, hy = by - sg * H * 0.6f;
		if (cr) {
			us_line(c, x - d * L * 0.5f, by, x + d * L * 0.4f, by, 0.004f, vv, US_MAT_ACCENT);
			us_line(c, hx, hy, hx + d * 0.004f, hy + sg * 0.001f, 0.004f, vv, US_MAT_ACCENT);
		} else {
			float body[] = {x - d * L * 0.5f, by, x - d * L * 0.1f, by - sg * H * 0.35f, x + d * L * 0.35f, by - sg * H * 0.3f,
					hx, hy, hx + d * 0.0045f, hy + sg * 0.0012f, hx, hy + sg * 0.0016f, x + d * L * 0.35f, by + sg * 0.0006f,
					x - d * L * 0.5f, by + sg * 0.0005f};
			hpoly(c, body, 8, vv * 1.2f, US_MAT_ACCENT);
			rim(c, hx - d * 0.0008f, hy, hx - d * 0.0005f, hy - sg * 0.0035f, vv, US_MAT_ACCENT); /* ear */
		}
		/* bushy tail, low behind */
		float tx = x - d * L * 0.5f;
		float tw = 0.0015f * sinf(s->fox_ph * 1.0f);
		wline_(c, cr, tx, by, tx - d * 0.009f, by + sg * (0.001f + tw), vv, US_MAT_ACCENT);
		wline_(c, cr, tx - d * 0.003f, by - sg * 0.0012f, tx - d * 0.011f, by + sg * (0.0002f + tw), vv * 0.8f, US_MAT_ACCENT);
		/* legs: a diagonal-pair trot */
		for (int l = 0; l < 4; l++) {
			float lx = x + d * (l < 2 ? 0.3f : -0.38f) * L + (l & 1) * d * 0.002f;
			float sw = 0.0022f * sinf(s->fox_ph + ((l == 0 || l == 3) ? 0.0f : US_PI));
			wline_(c, cr, lx, by, lx + d * sw, y, vv * 0.9f, US_MAT_ACCENT);
		}
	}
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *s = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, rdt = ctx->real_dt, A = ctx->aspect;
	float dens = ctx->p->density;
	us_rng *r = &s->rng;
	float winter = us_world_season_w(w, 3), summer = us_world_season_w(w, 1);
	float day = w->daylight, storm = w->storm_charge;
	float night = 1.0f - 0.6f * us_smooth(0.15f, 0.8f, day);

	/* ---- envelopes ---- */
	s->surge = us_approach(s->surge, us_sat(0.8f * a->bass + 0.7f * a->kick + 0.2f * a->sub), a->kick > s->surge ? 0.05f : 0.6f, rdt);
	float sh = us_sat(0.55f * a->width + 0.35f * a->high + 0.5f * a->air);
	s->shimmer = us_approach(s->shimmer, sh, 0.4f, rdt);
	s->height = us_approach(s->height, a->active > 0.2f ? a->centroid : 0.4f, 2.0f, rdt);
	s->T += dt * (0.05f + 0.06f * storm + 0.04f * a->swell);
	s->flick_t += rdt * (2.0f + 10.0f * s->shimmer);
	s->twk += rdt * (1.7f + 5.0f * s->air_env);
	/* each band: quick to light (~0.06 s), slow to let go (~0.35 s) */
	for (int i = 0; i < US_NBANDS; i++) {
		float bv = a->bands[i];
		s->bandv[i] = us_approach(s->bandv[i], bv, bv > s->bandv[i] ? 0.06f : 0.35f, rdt);
	}
	s->sub_env = us_approach(s->sub_env, a->sub, a->sub > s->sub_env ? 0.08f : 0.45f, rdt);
	s->air_env = us_approach(s->air_env, a->air, 0.25f, rdt);
	s->lm_env = us_approach(s->lm_env, a->lowmid, 0.8f, rdt);
	/* stereo width spreads the curtains across the sky (slowly) */
	s->spread = us_approach(s->spread, 0.82f + 0.4f * a->width, 2.5f, rdt);
	/* kick: a pulse of light runs outward along every curtain from its middle */
	s->kick_cool -= rdt;
	for (int i = 0; i < MAX_PULSE; i++)
		s->pulse_age[i] += rdt;
	if (a->beat && a->kick > 0.4f && s->kick_cool <= 0) {
		s->pulse_i = (s->pulse_i + 1) % MAX_PULSE;
		s->pulse_age[s->pulse_i] = 0;
		s->kick_cool = 0.22f;
	}
	float pamp[MAX_PULSE], pdist[MAX_PULSE];
	for (int i = 0; i < MAX_PULSE; i++) {
		float ag = s->pulse_age[i];
		/* eases in over ~40 ms, out over ~0.45 s; the front slows as it spreads */
		pamp[i] = ag < 1.5f ? us_smooth(0.0f, 0.04f, ag) * expf(-ag / 0.32f) : 0.0f;
		pdist[i] = 0.75f * (1.0f - expf(-ag / 0.45f));
	}
	/* sub: the ice booms; one crack glints and fades */
	for (int i = 0; i < MAX_CRACK; i++)
		s->crack_g[i] = us_approach(s->crack_g[i], 0.0f, 0.5f, rdt);
	if (a->beat && a->sub > 0.55f) {
		int ci = (int)(us_rand(r) * MAX_CRACK) % MAX_CRACK;
		s->crack_g[ci] = us_sat(s->crack_g[ci] + 0.5f + 0.5f * a->sub);
	}
	/* quiet passages: the sky still breathes, slowly (about a 9 s breath) */
	float quiet = 1.0f - us_sat(1.4f * s->surge);
	float breath = 0.5f + 0.5f * sinf((float)ctx->t * (US_TAU / 9.0f));

	/* ---- how many curtains: density + geomagnetic storm + loudness ---- */
	float want_f = 1.2f + 1.6f * dens + 2.2f * storm + 1.2f * a->swell;
	int want = (int)want_f;
	if (want > MAX_CURT)
		want = MAX_CURT;
	if (!s->inited) {
		for (int i = 0; i < want; i++) {
			s->cu[i].target = 1;
			s->cu[i].life = 1;
		}
		s->inited = 1;
	}
	for (int i = 0; i < MAX_CURT; i++) {
		curtain_t *cu = &s->cu[i];
		int on = i < want;
		if (on && cu->target == 0 && cu->life <= 0.01f)
			new_curtain(s, cu);
		cu->target = on ? 1.0f : 0.0f;
		cu->life = us_approach(cu->life, cu->target, 4.0f, dt);
		cu->xc += cu->drift * dt;
		if (cu->xc < -0.2f || cu->xc > 1.2f)
			cu->drift = -cu->drift;
	}

	/* pixel styles quantise a faint ghost away against the banded day sky: keep more of it there */
	float strength = (0.55f + 0.45f * winter - 0.15f * summer) * (ctx->crisp ? (night > 0.95f ? night : 0.95f) : night);
	float cov = us_clampf(c->s / 620.0f, 0.22f, 1.0f); /* rays per pixel compensation */

	/* ---- stars ---- */
	{
		int ns = (int)(MAX_STARS * (0.25f + 0.75f * dens));
		float sv = us_sat(1.0f - day * 1.6f);
		if (sv > 0.01f)
			for (int i = 0; i < ns; i++) {
				star_t *t = &s->st[i];
				float x = t->x * A;
				if (t->y > ridge_y(t->x) - 0.01f)
					continue;
				/* air (hats, breath, cymbal wash) sets the stars scintillating */
				float tw = 0.75f + (0.25f + 0.3f * s->air_env) * us_noise2(t->tw, s->twk);
				float v = t->m * tw * sv * (ctx->crisp ? 0.9f : 1.1f);
				us_point(c, x, t->y, t->m > 0.7f ? 0.0016f : 0.0011f, v, US_MAT_LIGHT);
			}
	}

	/* ---- meteor (rare, in quiet) ---- */
	s->next_meteor -= rdt * (1.0f - 0.7f * a->level);
	s->met_cool -= rdt;
	/* a strong onset can throw a shooting star (rarely: a hit is a chance, not a rule) */
	int met_hit = a->beat && a->onset > 0.8f && s->met_cool <= 0 && s->meteor_t <= 0 && us_rand(r) < 0.2f;
	if ((s->next_meteor <= 0 || met_hit) && s->meteor_t <= 0) {
		s->met_cool = 5.0f;
		s->meteor_t = 1.0f;
		s->meteor_x = us_randr(r, 0.1f, 0.9f) * A;
		s->meteor_y = us_randr(r, 0.03f, 0.25f);
		float ang = us_randr(r, 0.25f, 0.7f) * (us_rand(r) < 0.5f ? 1.0f : -1.0f);
		s->meteor_vx = sinf(ang) * 0.6f;
		s->meteor_vy = cosf(fabsf(ang)) * 0.25f;
		s->next_meteor = us_randr(r, 12, 40);
	}
	if (s->meteor_t > 0) {
		s->meteor_t -= rdt * 2.2f;
		s->meteor_x += s->meteor_vx * rdt;
		s->meteor_y += s->meteor_vy * rdt;
		float v = us_sat(s->meteor_t) * (1.0f - day);
		us_line(c, s->meteor_x, s->meteor_y, s->meteor_x - s->meteor_vx * 0.12f, s->meteor_y - s->meteor_vy * 0.12f,
			0.0012f, v * 0.8f, US_MAT_LIGHT);
	}

	/* ---- curtains ---- */
	float glow[64];
	memset(glow, 0, sizeof(glow));
	c->blend = US_BLEND_ADD;
	int nr = (int)(130 + 250 * dens);
	if (nr > MAX_RAYS)
		nr = MAX_RAYS;
	float rip_speed = 0.5f + 5.0f * s->shimmer + 2.0f * storm;
	float flick_amt = 0.12f + 0.75f * s->shimmer;
	float L0 = 0.17f + 0.24f * s->height + 0.06f * storm;
	float lift = 0.07f * (s->height - 0.4f);
	float nact = 0;
	for (int ci = 0; ci < MAX_CURT; ci++)
		nact += s->cu[ci].life;
	/* more curtains share the light: brightness grows sub-linearly with their number */
	/* quiet keeps a floor (and breathes) so a breakdown still glows; loud passages are unchanged */
	float bright = strength * (0.44f + 0.5f * quiet * (0.75f + 0.5f * breath) + 0.9f * s->surge + 0.15f * a->level) * (0.95f + 0.2f * storm) / powf(nact > 1.5f ? nact / 1.5f : 1.0f, 0.7f);
	float fringe = us_sat(0.62f - 0.25f * storm - 0.12f * s->surge);
	float subf = us_sat(1.3f * s->sub_env - 0.05f);
	for (int ci = 0; ci < MAX_CURT; ci++) {
		curtain_t *cu = &s->cu[ci];
		if (cu->life < 0.01f)
			continue;
		cu->ph += dt * rip_speed;
		float T = s->T + cu->seed;
		/* depth: curtains low over the horizon are farther away -> shorter, fainter */
		float near = us_sat((0.56f - cu->y0) / 0.2f);
		float lifev = cu->life * bright * (0.6f + 0.5f * near) * cov * sqrtf(255.0f / nr);
		float lscale = 0.6f + 0.7f * near;
		/* the lower rim is traced as we go: a sharp bright border, the curtain's signature */
		float rpx = 0, rpy = 0, rpb = 0, xprev = 0;
		int rprev = 0;
		float du = 1.0f / (nr - 1);
		for (int k = 0; k < nr; k++) {
			float h1 = hash1(k, cu->seed), h2 = hash1(k + 7919, cu->seed);
			float u = (k + 0.9f * (h1 - 0.5f)) / (nr - 1);
			/* travelling folds: the ribbon doubles back on itself where its sway outruns its span; rays
			 * pile up there and glow (additive), and the folded-back part sits a little higher (farther) */
			float fa = u * US_TAU * 1.3f + T * 0.55f + cu->seed;
			float xf = curtain_u2x(cu, u, T, s->spread);
			/* where the ribbon turns edge-on its rays pile up; let them glow, but not into a solid slab */
			float slope = k > 0 ? fabsf(xf - xprev) / (du * cu->span) : 1.0f;
			xprev = xf;
			/* rays bunch where the ribbon turns edge-on: thin them so a fold glows a little, never a slab */
			float pile = us_clampf(0.04f + 1.0f * slope, 0.04f, 1.0f);
			float ripple = 0.006f * sinf(u * 70.0f - cu->ph) + 0.004f * sinf(u * 23.0f - cu->ph * 0.6f + 1.0f);
			float x = xf * A + ripple * (0.4f + s->shimmer);
			if (x < -0.05f || x > A + 0.05f) {
				rprev = 0;
				continue;
			}
			float yb = cu->y0 - lift + 0.07f * us_noise2(u * 2.4f + T * 0.2f, cu->seed + 7.0f) +
				   0.03f * us_noise2(u * 5.0f + T * 0.5f, cu->seed + 9.0f) - 0.035f * cu->fold * 3.0f * cosf(fa) +
				   0.01f * sinf(u * 9.0f - cu->ph * 0.3f) + 0.007f * (0.4f + s->shimmer) * sinf(u * 17.0f - cu->ph * 0.5f + cu->seed);
			float ln = L0 * lscale * (0.45f + 0.9f * us_sat(0.55f + 0.7f * us_noise2(u * 5.0f - T * 0.4f, cu->seed + 3.0f)));
			ln *= 0.65f + 0.6f * h2;
			float patch = us_sat(0.55f + 0.9f * us_noise2(u * 7.0f - T * 0.9f, cu->seed + 13.0f));
			float ends = us_smooth(0.0f, 0.22f, u) * us_smooth(1.0f, 0.78f, u);
			ln *= 0.35f + 0.65f * ends; /* the curtain's ends shrink away, no hard vertical edge */
			float fl = 1.0f + flick_amt * us_noise2(k * 0.73f + cu->seed, s->flick_t);
			/* the spectrum laid along the ribbon (mids in the middle): each band lights and lifts its stretch */
			float bf = 4.0f + 23.0f * (ci & 1 ? 1.0f - u : u);
			int bi = (int)bf;
			float bandk = us_lerp(s->bandv[bi], s->bandv[bi + 1], bf - bi);
			/* kick pulses travelling out from the middle */
			float pul = 0;
			for (int pi = 0; pi < MAX_PULSE; pi++)
				if (pamp[pi] > 0.01f) {
					float d = fabsf(u - 0.5f) - pdist[pi];
					pul += pamp[pi] * expf(-d * d * 160.0f);
				}
			ln *= 0.84f + 0.12f * quiet + 0.38f * bandk + 0.18f * pul;
			float b = lifev * patch * ends * fl * (0.7f + 0.6f * h1) * pile * (0.74f + 0.22f * quiet + 0.55f * bandk) * (1.0f + 1.8f * pul);
			if (b < 0.01f) {
				rprev = 0;
				continue;
			}
			float yr = ridge_y(us_sat(x / A)) - 0.025f;
			if (yb > yr)
				yb = yr;
			float lean = 0.04f * ln * (0.5f + h2);
			ray(c, x, yb, ln * (0.9f + 0.2f * fl), lean, b * 0.3f, fringe, -1, US_MAT_LEAF, 0.0f);
			/* sub: a red-magenta lower border hangs below the rim (type-B aurora), deeper with the sub */
			if (subf > 0.03f && !(k & 1)) {
				float fln = (0.004f + 0.026f * subf) * lscale;
				if (fln > yr + 0.02f - yb)
					fln = yr + 0.02f - yb;
				if (fln > 0.002f)
					ray(c, x, yb + 1.0f / c->s, fln, 0.0f, b * (ctx->crisp ? 0.35f : 0.5f) * subf, -1.0f, 1, US_MAT_BLOOM, 0.0f);
			}
			{ /* how brightly this stretch of sky glows: lights the snow on the ridge and the lake */
				int gi = (int)(us_sat(x / A) * 63.0f);
				glow[gi] += b * 0.02f;
			}
			if (rprev) {
				float dx = x - rpx, dy = yb - rpy;
				if (dx * dx + dy * dy < 0.0009f) {
					float rv = 0.16f * (b + rpb) * (ctx->crisp ? 0.8f : 1.0f);
					if (ctx->crisp)
						us_line(c, rpx, rpy, x, yb, 0.001f, rv, US_MAT_LEAF);
					else
						rim(c, rpx, rpy, x, yb, rv, US_MAT_LEAF);
				}
			}
			rpx = x;
			rpy = yb;
			rpb = b;
			rprev = 1;
			/* reflection in the lake: compressed, wobbling */
			if (k % 3)
				continue;
			/* reflection in the still lake: the whole curtain, squashed, its fringe colour kept at the far end */
			float ry0 = SHORE + 0.012f + (SHORE - yb) * 0.22f;
			if (ry0 < 1.0f) {
				float wob = 0.006f * us_noise2(x * 7.0f, ry0 * 25.0f + (float)ctx->t * 0.7f);
				float rl = ln * 0.48f;
				ray(c, x + wob, ry0, rl, -lean * 0.4f + wob, b * (ctx->crisp ? 0.55f : 0.32f), fringe, 1, US_MAT_WATER, 0.45f);
				if (!ctx->crisp)
					us_put(c, (int)((x + wob) * c->s), (int)(ry0 * c->s), b * 0.3f, US_MAT_WATER);
			}
		}
	}

	/* ---- the quiet arc: a faint, homogeneous band low over the ridge, the aurora's resting form. It fills
	 * the sky above the mountains and carries the scene through breakdowns (brighter as the music thins,
	 * breathing in silence), and it lies on the lake too. ---- */
	{
		int nq = nr * 2 / 3;
		float qv = strength * cov * sqrtf(255.0f / (nq > 1 ? nq : 1)) * (0.13f + 0.2f * quiet * (0.7f + 0.6f * breath) + 0.06f * a->swell);
		float qT = s->T * 0.35f;
		float qpx = 0, qpy = 0;
		for (int k = 0; k < nq; k++) {
			float h1 = hash1(k, 3.7f), h2 = hash1(k + 4111, 3.7f), h3 = hash1(k + 977, 5.1f);
			float u = (k + 1.6f * (h3 - 0.5f)) / nq;
			float x = u * A;
			float yr = ridge_y(u) - 0.012f;
			float y = 0.56f - 0.035f * sinf(US_PI * u) + 0.012f * us_noise2(u * 2.2f + qT, 21.0f) - 0.03f * (s->height - 0.4f);
			if (y > yr)
				y = yr;
			float patch = us_sat(0.6f + 0.8f * us_noise2(u * 6.0f - qT * 1.6f, 23.0f));
			float b = qv * patch * (0.25f + 1.1f * h1 * h1);
			/* its soft lower edge (ink) */
			if (!ctx->crisp && k > 0)
				rim(c, qpx, qpy, x, y, qv * patch * 0.05f, US_MAT_LEAF);
			qpx = x;
			qpy = y;
			if (b < 0.01f)
				continue;
			float ln = (0.02f + 0.04f * patch) * (0.6f + 0.6f * h2);
			ray(c, x, y, ln, 0.002f, b * 0.3f, 2.0f, -1, US_MAT_LEAF, 0.15f);
			int gi = (int)(us_sat(u) * 63.0f);
			glow[gi] += b * 0.004f;
			if (k % 3 == 0) {
				float ry0 = SHORE + 0.012f + (SHORE - y) * 0.22f;
				float wob = 0.004f * us_noise2(x * 9.0f, (float)ctx->t * 0.7f);
				ray(c, x + wob, ry0, ln * 0.5f, wob, b * (ctx->crisp ? 0.35f : 0.18f), 2.0f, 1, US_MAT_WATER, 0.4f);
			}
		}
	}
	c->blend = US_BLEND_MAX;

	/* ---- pixel styles by day: banded pale-blue sky behind (MAX) the ghost aurora; ink stays black ---- */
	{
		float dayk = us_smooth(0.22f, 0.7f, day); /* none at night/twilight: mats would linger under the trails */
		uint8_t sm = ctx->crisp ? US_MAT_WATER : US_MAT_BG;
		(void)sm;
		float f[2 * 8];
		for (int j = 0; j < 8; j++) {
			float y = j / 7.0f;
			float v = (ctx->crisp ? 0.06f + 0.34f * y : 0.03f + 0.16f * y * y) * dayk;
			f[j * 2] = f[j * 2 + 1] = v;
		}
		if (dayk > 0.0f && ctx->crisp) {
			us_field(c, f, 2, 8, 1.0f, sm, 0, 0, A, SHORE);
			/* the lake mirrors the pale sky, darker */
			float g[2 * 4];
			for (int j = 0; j < 4; j++)
				g[j * 2] = g[j * 2 + 1] = f[14 - 2 * j] * 0.6f;
			us_field(c, g, 2, 4, 1.0f, sm, 0, SHORE, A, 1.0f);
		}
	}

	/* ---- mountains ---- */
	{
		float pts[2 * 97];
		for (int i = 0; i <= 96; i++) {
			float u = i / 96.0f;
			pts[2 * i] = u * A;
			pts[2 * i + 1] = ridge_y(u);
		}
		float mv = 0.32f + 0.15f * day;
		if (!ctx->crisp) { /* overwrite the mountain mass so faint stars do not show through it */
			for (int px = 0; px < c->w; px++) {
				float uu = (px + 0.5f) / c->s / A * 96.0f;
				int i = (int)uu;
				if (i > 95) i = 95;
				int y0 = (int)ceilf(us_lerp(pts[2 * i + 1], pts[2 * i + 3], uu - i) * c->s) + 1, y1 = (int)(SHORE * c->s);
				for (int py = y0 < 0 ? 0 : y0; py < y1 && py < c->h; py++) {
					size_t ii = (size_t)py * c->w + px;
					if (c->ink[ii] < 3) {
						c->ink[ii] = 3;
						c->mat[ii] = US_MAT_EARTH;
					}
				}
			}
		}
		us_polyline(c, pts, 97, 0.0013f, mv, US_MAT_EARTH);
		/* aurora light on the snowy crests */
		for (int i = 0; i < 96; i++) {
			float g = glow[(int)(i / 96.0f * 63.0f)] * strength;
			float gv = us_sat(g * 0.5f);
			if (gv > 0.02f)
				(ctx->crisp ? us_line : us_line)(c, pts[2 * i], pts[2 * i + 1] + (ctx->crisp ? 0.0f : 0.0025f), pts[2 * i + 2],
							  pts[2 * i + 3] + (ctx->crisp ? 0.0f : 0.0025f), ctx->crisp ? 0.004f : 0.0011f, gv, US_MAT_LEAF);
		}
		if (ctx->crisp)
			for (int i = 0; i < 96; i++) {
				float x0 = pts[2 * i], y0 = pts[2 * i + 1], x1 = pts[2 * i + 2], y1 = pts[2 * i + 3];
				us_tri(c, x0, y0, x1, y1, x0, SHORE, 0.14f + 0.3f * day, US_MAT_EARTH);
				us_tri(c, x1, y1, x1, SHORE, x0, SHORE, 0.14f + 0.3f * day, US_MAT_EARTH);
			}
		else {
			/* snow on the highest ridges: short hatch strokes down the flanks */
			for (int i = 0; i < 96; i += 2) {
				float u = i / 96.0f, y = pts[2 * i + 1];
				float g = us_sat(glow[(int)(u * 63.0f)] * strength * 0.35f);
				if (y < 0.65f) {
					float sl = pts[2 * i + 3] > y ? 1.0f : -1.0f; /* streak down the slope */
					us_line(c, u * A, y + 0.004f, u * A + 0.006f * sl, y + 0.02f + (0.65f - y) * 0.45f, 0.0009f,
						0.12f + 0.22f * winter + g, g > 0.08f ? US_MAT_LEAF : US_MAT_WATER);
				}
			}
		}
	}

	/* ---- foothills: a nearer, darker range between the peaks and the shore ---- */
	{
		float fp[2 * 65];
		for (int i = 0; i <= 64; i++) {
			float u = i / 64.0f;
			fp[2 * i] = u * A;
			fp[2 * i + 1] = SHORE - 0.022f - 0.022f * (0.5f + 0.5f * us_noise2(u * 4.2f, 6.3f)) - 0.004f * us_noise2(u * 19.0f, 2.0f);
		}
		for (int px = 0; px < c->w; px++) {
			float uu = (px + 0.5f) / c->s / A * 64.0f;
			int i = (int)uu;
			if (i > 63) i = 63;
			int y0 = (int)ceilf(us_lerp(fp[2 * i + 1], fp[2 * i + 3], uu - i) * c->s) + 1, y1 = (int)(SHORE * c->s);
			for (int py = y0 < 0 ? 0 : y0; py < y1 && py < c->h; py++) {
				size_t ii = (size_t)py * c->w + px;
				c->ink[ii] = ctx->crisp ? (uint8_t)(255 * (0.1f + 0.2f * day)) : 2;
				c->mat[ii] = US_MAT_EARTH;
			}
		}
		for (int i = 0; i < 64; i++) {
			float g = us_sat(glow[(int)(i / 64.0f * 63.0f)] * strength * 0.2f);
			wline_(c, ctx->crisp, fp[2 * i], fp[2 * i + 1], fp[2 * i + 2], fp[2 * i + 3], 0.24f + 0.1f * day + g, US_MAT_EARTH);
		}
	}

	/* ---- shore + spruce treeline ---- */
	{
		int nt = (int)(s->ntr * (0.35f + 0.65f * dens));
		float tv = 0.30f + 0.25f * day;
		for (int i = 0; i < s->ntr; i++) {
			spruce_t *t = &s->tr[i];
			/* keep the full stand ordering stable: skip by hash, not by index */
			if ((int)(t->x * 997.0f) % s->ntr >= nt)
				continue;
			if (fabsf(t->x - s->cab_x) * A < 0.03f) /* the cabin's clearing */
				continue;
			spruce(c, ctx->crisp, t->x * A, SHORE + 0.002f, t->h, t->w, tv);
			/* mirrored in the still water, dim and shortened */
			spruce(c, ctx->crisp, t->x * A, SHORE + 0.004f, -t->h * 0.7f, t->w * 0.9f, tv * (ctx->crisp ? 0.5f : 0.32f));
		}
		us_line(c, 0, SHORE, A, SHORE, 0.0012f, 0.35f, US_MAT_EARTH);
		cabin(s, ctx, c, day);
		fox(s, ctx, c, day);
		/* the far ridge, reflected and broken by slow ripples */
		if (!ctx->crisp) {
			float px = 0, py = 0;
			for (int i = 0; i <= 96; i++) {
				float u = i / 96.0f;
				float y = SHORE + (SHORE - ridge_y(u)) * 0.5f;
				float x = u * A + 0.003f * us_noise2(u * 40.0f, (float)ctx->t * 0.5f);
				if (i > 0 && us_noise2(u * 23.0f, (float)ctx->t * 0.3f) > -0.15f)
					rim(c, px, py, x, y, 0.12f + 0.05f * day, US_MAT_WATER);
				px = x;
				py = y;
			}
		}
		/* lake: where the sky glows, a broken glimmer lies on the water just off the shore */
		for (int g = 0; g < 64; g++) {
			float gv = us_sat(glow[g] * strength * 0.3f);
			if (gv < 0.03f)
				continue;
			float x0 = g / 63.0f * A;
			for (int k = 0; k < 3; k++) {
				float y = SHORE + 0.02f + 0.022f * k + 0.003f * us_noise2(g * 0.7f, k + (float)ctx->t * 0.3f);
				float off = 0.01f * us_noise2(g * 1.3f + k * 5.0f, (float)ctx->t * 0.4f);
				float len = A / 63.0f * (0.5f + 0.4f * us_noise2(g * 2.1f, k * 3.0f + (float)ctx->t * 0.5f));
				if (ctx->crisp)
					us_line(c, x0 + off, y, x0 + off + len, y, 0.004f, gv * (1.0f - 0.25f * k), US_MAT_WATER);
				else
					rim(c, x0 + off, y, x0 + off + len, y, gv * (1.0f - 0.25f * k), US_MAT_WATER);
			}
		}
		/* lake: a few faint still-water lines (ink only) */
		for (int k = 0; k < (ctx->crisp ? 0 : 5); k++) {
			float y = SHORE + 0.03f + k * k * 0.009f;
			float x0 = (0.1f + 0.5f * (0.5f + 0.5f * us_noise2(k * 3.1f, (float)ctx->t * 0.02f))) * A;
			us_line(c, x0, y, x0 + A * (0.1f + 0.06f * k), y, 0.0009f, 0.10f + 0.05f * day, US_MAT_WATER);
		}
		/* the brighter low stars, mirrored and trembling on the water */
		float sv = us_sat(1.0f - day * 1.6f);
		if (sv > 0.01f)
			for (int i = 0; i < MAX_STARS / 2; i++) {
				star_t *t = &s->st[i];
				if (t->m < 0.3f || t->y < 0.25f)
					continue;
				float ry = SHORE + (SHORE - t->y) * 0.5f;
				if (ry > 0.995f || t->y > ridge_y(t->x) - 0.01f)
					continue;
				float wob = 0.0025f * us_noise2(t->tw, (float)ctx->t * 0.9f);
				float v = t->m * sv * (0.3f + 0.15f * us_noise2(t->tw + 7.0f, s->twk));
				us_point(c, t->x * A + wob, ry, 0.0011f, v, US_MAT_WATER);
			}
		/* ice along the near edge: fine cracks, glinting when the sub makes the lake boom */
		float icev = 0.06f + 0.1f * winter;
		for (int i = 0; i < MAX_CRACK; i++) {
			float g = s->crack_g[i];
			float v = icev + 0.45f * g;
			float *p = s->crack[i];
			for (int k = 0; k + 1 < CRACK_PTS; k++)
				wline_(c, ctx->crisp, p[2 * k] * A, p[2 * k + 1], p[2 * k + 2] * A, p[2 * k + 3],
				       v * (1.0f - 0.08f * k), g > 0.4f ? US_MAT_LIGHT : US_MAT_WATER);
			/* a short side branch */
			int bk = 3 + i % 3;
			wline_(c, ctx->crisp, p[2 * bk] * A, p[2 * bk + 1], p[2 * bk] * A + 0.01f, p[2 * bk + 1] + (i & 2 ? 0.008f : -0.006f),
			       v * 0.7f, US_MAT_WATER);
		}
	}
}

const us_scene_vt us_scene_aurora = {"aurora", "Aurora", 0.45f, create, destroy, frame};
