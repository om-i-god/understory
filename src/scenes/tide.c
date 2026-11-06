/* Understory scene: Tide — a moonlit shoreline at a low angle.
 *
 * The sea is a height field drawn as a stack of ridge lines (rows of constant depth z, nearest first)
 * with hidden-line removal, so incoming swells rise as real 3D humps that hide the water behind them.
 * Perspective: y = HORIZON + K/z for still water, and a surface height h lifts a point by K*h/z.
 * Each wave is born far out as a low hump, grows and steepens as it shoals (speed ~ depth, so it slows
 * near shore), breaks (bigger waves break farther out) into a band of foam lace — iso-lines around the
 * ridges of a noise field, i.e. organic loops and holes — then runs up the sand as a lacy swash front,
 * stalls, slides back and leaves a dotted wet line that slowly dries. At night the moon (phase from the
 * world's season clock, position from the time of day) lays a twinkling glitter path; by day the sun.
 * Above: the Milky Way, a haloed moon, banks of cumulus lit on the moon's side, horizon haze. On the beach:
 * long ripple marks catching the moonlight, shells and pebbles, a driftwood log, and marram grass on the
 * dunes framing the foreground corners, combed by the wind. Small lives: a rock outcrop at the waterline with
 * rock pools at its foot, a lighthouse on a far headland sweeping its beam, sandpipers sprinting ahead of each
 * uprush and probing the wet sand behind the backwash, ghost crabs scuttling sideways on the dry sand.
 *
 * Audio: swell (phrase loudness) + a slow ~50 s set rhythm -> wave size and how often they come, how far up
 *        the beach the tide sits and how far the lighthouse beam reaches; kick -> a breaker bursts on the rocks
 *        (ballistic spray fan, drips ringing the rock pools); bass pushes the run-up; sub lifts the long ground
 *        swell; mids (melody) make the moon path shimmer and widen; highs -> foam lace + glitter + spindrift;
 *        air -> the swash sheet fizzes; single bands set individual ghost crabs scuttling; hard onsets startle
 *        a sandpiper into a short flutter; stereo width is the wind in the marram; centroid clears the sky
 *        (moon halo, Milky Way); level stirs the chop. Silence = small lapping waves still rolling in, the beam
 *        still turning, birds still working the wash line, crabs making the odd dart.
 * World: reads water_level (tide height = where the shoreline sits), daylight/day_phase (moon/sun),
 *        season (moon phase), storm_charge (storms elsewhere -> bigger, more frequent surf), wind.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAX_WAVES 8
#define MAX_SPRAY 320
#define NX 128 /* wet-line columns */
#define NS 192 /* surface samples across the screen (ink) */
#define N_STARS 520 /* scattered + the Milky Way band */
#define N_CLOUDS 5
#define N_SHELLS 46
#define N_DUNE 70  /* marram grass blades framing the foreground corners */
#define FW 220 /* foam lace grid */
#define FH 22
#define TW 192 /* per-column crest tables */
#define LXN 176 /* sand lace grid: columns x rows (shore -> reach) */
#define LZN 24
#define MAX_GLINT 260
#define RKN 64     /* rock outcrop profile samples */
#define N_BIRDS 5  /* sandpipers working the wash line */
#define N_CRABS 3  /* ghost crabs on the dry sand */
#define NT 64      /* tileable noise table for per-pixel pixel-art textures (clouds, rock) */

#define HOR 0.34f /* horizon y */
#define KP 0.38f  /* perspective constant: z = 1 -> y = HOR + KP */
#define Z_FAR 16.0f
#define Z_SPAWN 9.0f

enum { ROLL = 0, BREAK, SWASH, DONE };

typedef struct {
	float z, amp, seed, age, brk_age;
	int state;
	float foam;           /* whitewater amount after breaking */
	float run_t, run_max; /* swash clock + reach */
	float zb;             /* break depth */
	float hf;             /* current height (world units, camera height = 1) */
	float steep;
	float reach;          /* swash reach, frozen when the swash starts */
	int rock_hit;         /* already broke over the rocks */
} wave_t;

/* foam lace left on the sand by one swash: a cellular web of thin lines that rides the sheet up the beach,
 * is stranded as the backwash uncovers it, then breaks up and fades as the bubbles pop */
typedef struct {
	int on, wave;
	float clock, Tu, Td, reach, seed;
} lace_t;

typedef struct {
	float x, y, vx, vy, life, g;
} spray_t;

typedef struct {
	us_rng rng;
	int inited;
	float t;
	wave_t wv[MAX_WAVES];
	float next_wave;
	spray_t sp[MAX_SPRAY];
	int nsp;
	float wet_z[NX], wet_a[NX]; /* dotted wet line left by the run-ups (z per column) */
	float star_x[N_STARS], star_y[N_STARS], star_p[N_STARS];
	float bass_env, hi_env, swell_env, tide_s;
	float chop_t;
	float occ[NS + 1];                                     /* hidden-line horizon */
	float cz_tab[MAX_WAVES][TW + 1], al_tab[MAX_WAVES][TW + 1]; /* crest depth + along-crest strength */
	lace_t lace[2];
	float shore_n[TW + 1];
	float cusp_tab[LXN + 1];
	int32_t llab[(LXN + 1) * LZN];
	float lu[(LXN + 1) * LZN], lv[(LXN + 1) * LZN], lsx[(LXN + 1) * LZN], lsy[(LXN + 1) * LZN], lal[(LXN + 1) * LZN];
	float lx[FH][FW + 1], ly[FH][FW + 1], lf[FH][FW + 1]; /* foam lace scratch */
	float cl_x[N_CLOUDS], cl_y[N_CLOUDS], cl_w[N_CLOUDS], cl_s[N_CLOUDS];
	float sh_x[N_SHELLS], sh_z[N_SHELLS], sh_a[N_SHELLS], sh_k[N_SHELLS];
	float du_x[N_DUNE], du_h[N_DUNE], du_p[N_DUNE];
	float mw_a, mw_b, wind_t, log_x, log_z, log_a;
	/* eased audio envelopes, one per musical meaning */
	float sub_env, mid_env, air_env, width_env, cent_env, gs_t, gl_t;
	float kick_prev, crash, crash_cd, crash_k, crash_age, crash_n;
	int crash_spawn; /* spray particles owed by a fresh crash */
	/* a rock outcrop at the waterline (kicks break on it) and a far headland with a lighthouse */
	int rk_side;
	float rk_x, rk_w, rk_h, rk_z, rk_prof[RKN + 1];
	float hd_prof[RKN + 1], lh_ph;
	float edge_z[NX]; /* where the water's edge is right now, per column */
	float bd_x[N_BIRDS], bd_z[N_BIRDS], bd_dir[N_BIRDS], bd_leg[N_BIRDS], bd_hop[N_BIRDS], bd_peck[N_BIRDS], bd_turn[N_BIRDS];
	float cr_x[N_CRABS], cr_z[N_CRABS], cr_dir[N_CRABS], cr_e[N_CRABS], cr_leg[N_CRABS], cr_idle[N_CRABS];
	float pool_t[2];
	float ntile[NT * NT];
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x74696465u);
	return s;
}

static void destroy(void *p) { free(p); }

static inline float proj_y(float z) { return HOR + KP / z; }
static float hash1(float x) { return x * 12.9898f - floorf(x * 12.9898f); }

/* crest depth for screen x (fraction 0..1) of a wave at nominal depth z: undulation + a shallow bay */
static float crest_z(const wave_t *w, float xf, float t)
{
	float u = xf - 0.5f;
	return w->z * (1.0f + 0.1f * us_noise2(xf * 2.6f + w->seed, t * 0.03f + w->seed) + 0.14f * u * u);
}

/* shoreline (still-water edge) depth per column */
static float shore_z(const st_t *st, float xf)
{
	/* the beach's shape is fixed: its noise comes from a table filled once */
	float ft = us_clampf(xf, 0.0f, 1.0f) * TW;
	int i0 = (int)ft < TW ? (int)ft : TW - 1;
	float n = us_lerp(st->shore_n[i0], st->shore_n[i0 + 1], ft - i0);
	return st->tide_s * (1.0f + 0.03f * n + 0.06f * (xf - 0.5f) * (xf - 0.5f));
}

/* rock outcrop: profile height 0..1 at screen fraction xf (0 outside the rock) */
static float rock_prof(const st_t *st, float xf)
{
	float u = (xf - (st->rk_x - st->rk_w * 0.5f)) / st->rk_w;
	if (u <= 0.0f || u >= 1.0f)
		return 0.0f;
	float ft = u * RKN;
	int i0 = (int)ft < RKN ? (int)ft : RKN - 1;
	return us_lerp(st->rk_prof[i0], st->rk_prof[i0 + 1], ft - i0);
}
static inline float rock_base(const st_t *st) { return proj_y(st->rk_z) + 0.004f; }
static inline float rock_top(const st_t *st, float xf)
{
	return rock_base(st) - st->rk_h * (KP / st->rk_z) * rock_prof(st, xf);
}

/* 4x4 ordered-dither threshold 0..1 for pixel-art textures */
static inline float bayer4(int x, int y)
{
	static const uint8_t m[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
	return (m[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f;
}

/* fast sine (parabolic, ~0.1% error) for the per-sample surface chop */
static inline float fsin(float x)
{
	x *= 1.0f / US_TAU;
	x -= floorf(x + 0.5f); /* [-0.5, 0.5) turns */
	float y = 8.0f * x - 16.0f * x * fabsf(x);
	return y * (0.775f + 0.225f * fabsf(y));
}

/* sea surface height at screen column xf, depth z */
static float sea_h(const st_t *st, float xf, float z, float X, float level)
{
	float h = 0.006f * fsin(z * 2.2f - st->chop_t * 2.0f + 1.1f * fsin(xf * 7.3f + z * 0.41f) + 0.7f * fsin(xf * 17.9f - z * 0.27f));
	h += (0.006f + 0.008f * level) * us_noise2(X * 0.7f + st->chop_t * 0.13f, z * 2.2f - st->chop_t * 1.5f);
	/* ground swell: long, slow undulations from far out; the sub-bass lifts them */
	h += (0.004f + 0.022f * st->sub_env) * fsin(z * 0.9f - st->gs_t + 0.6f * fsin(xf * 2.3f + z * 0.13f));
	float ft = us_clampf(xf, 0.0f, 1.0f) * TW;
	int i0 = (int)ft;
	if (i0 >= TW)
		i0 = TW - 1;
	float tt = ft - i0;
	for (int i = 0; i < MAX_WAVES; i++) {
		const wave_t *w = &st->wv[i];
		if ((w->state != ROLL && w->state != BREAK) || w->hf < 0.002f)
			continue;
		float zc = us_lerp(st->cz_tab[i][i0], st->cz_tab[i][i0 + 1], tt);
		float wid = 0.2f + 0.16f * zc;
		float u = (z - zc) / wid;
		if (u < -3.0f || u > 5.0f)
			continue;
		/* asymmetric: steep shoreward face, long back slope */
		float p = u < 0 ? expf(-u * u * (1.0f + 4.0f * w->steep) * 1.5f) : expf(-u * u * 0.3f);
		/* the crest isn't uniform along its length */
		float along = us_lerp(st->al_tab[i][i0], st->al_tab[i][i0 + 1], tt);
		h += w->hf * p * along;
	}
	return h;
}

/* fast thin AA line (Wu) — many short row segments in Ink */
static void wu_line(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
	/* hairlines in Ink read too dim on projectors: lift them (pixel styles are already bold) */
	if (!c->crisp)
		v = v * 1.8f > 1.0f ? 1.0f : v * 1.8f;
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
		/* full coverage at the ends: rows are chains of short segments max-blended, and partial end
		 * coverage left a darker pixel at every joint (a faint vertical grid down the stacked rows) */
		float cov = 1.0f;
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

/* jittered feature point of lace cell (cx, cz) in cell units — fixed on the sand for a given swash */
static inline uint32_t lace_feature(int cx, int cz, uint32_t seed, float *fx, float *fz)
{
	uint32_t h = (uint32_t)cx * 0x8da6b343u ^ (uint32_t)cz * 0xd8163841u ^ seed * 0x9e3779b9u;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	*fx = cx + 0.12f + 0.76f * ((h & 1023) / 1023.0f);
	*fz = cz + 0.12f + 0.76f * (((h >> 10) & 1023) / 1023.0f);
	return h;
}

static void spawn_wave(st_t *st, const us_ctx *ctx)
{
	us_rng *r = &st->rng;
	const us_world *w = ctx->w;
	wave_t *wv = NULL;
	for (int i = 0; i < MAX_WAVES; i++)
		if (st->wv[i].state == DONE) {
			wv = &st->wv[i];
			break;
		}
	if (!wv)
		return;
	memset(wv, 0, sizeof(*wv));
	/* sets: every ~50 s a group of bigger waves rolls through, even in silence */
	float sets = 0.5f + 0.5f * sinf(st->t * US_TAU / 52.0f);
	float a = 0.18f + 0.45f * st->swell_env + 0.4f * w->storm_charge + 0.2f * sets * sets + 0.12f * st->bass_env;
	wv->amp = us_clampf(a * us_randr(r, 0.8f, 1.15f), 0.12f, 1.0f);
	wv->z = Z_SPAWN;
	wv->seed = us_rand(r) * 100.0f;
	wv->state = ROLL;
	wv->zb = st->tide_s * (1.15f + 1.1f * wv->amp);
	wv->run_max = 0.08f + 0.22f * wv->amp;
}

static void init_scene(st_t *st, const us_ctx *ctx)
{
	us_rng *r = &st->rng;
	for (int i = 0; i < MAX_WAVES; i++)
		st->wv[i].state = DONE;
	/* stars: a third scattered, the rest crowded into a Milky Way band slanting across the sky */
	st->mw_a = us_randr(r, -0.35f, 0.35f);
	st->mw_b = us_randr(r, 0.35f, 0.65f);
	for (int i = 0; i < N_STARS; i++) {
		st->star_x[i] = us_rand(r);
		if (i % 3 == 0)
			st->star_y[i] = us_rand(r);
		else {
			float g = (us_rand(r) + us_rand(r) + us_rand(r) - 1.5f) * 0.22f; /* ~gaussian across the band */
			st->star_y[i] = us_clampf(st->mw_b + st->mw_a * (st->star_x[i] - 0.5f) + g, 0.0f, 1.0f);
		}
		st->star_p[i] = us_rand(r) * 30.0f;
	}
	for (int i = 0; i < N_CLOUDS; i++) {
		st->cl_x[i] = (i + us_rand(r) * 0.6f) / N_CLOUDS * 1.3f - 0.15f;
		st->cl_y[i] = us_randr(r, 0.08f, HOR - 0.08f);
		st->cl_w[i] = us_randr(r, 0.12f, 0.3f);
		st->cl_s[i] = us_rand(r) * 50.0f;
	}
	for (int i = 0; i < N_SHELLS; i++) {
		st->sh_x[i] = us_rand(r);
		st->sh_z[i] = us_rand(r);
		st->sh_a[i] = us_rand(r) * US_PI;
		st->sh_k[i] = us_rand(r);
	}
	for (int i = 0; i < N_DUNE; i++) {
		/* two clumps: bottom-left and bottom-right corners */
		float side = (i & 1) ? 1.0f : 0.0f;
		float o = powf(us_rand(r), 1.6f) * 0.24f;
		st->du_x[i] = side ? 1.0f - o : o;
		st->du_h[i] = us_randr(r, 0.35f, 1.0f) * (1.0f - 1.6f * o);
		st->du_p[i] = us_rand(r) * 20.0f;
	}
	st->log_x = us_randr(r, 0.3f, 0.7f);
	st->log_z = us_randr(r, 0.66f, 0.8f);
	st->log_a = us_randr(r, -0.25f, 0.25f);
	/* periodic noise tile (torus blend of four offset samples) so pixel textures cost a lookup, not a noise call */
	for (int y = 0; y < NT; y++)
		for (int x = 0; x < NT; x++) {
			float u = (float)x / NT, v = (float)y / NT, sx = x * 0.15f, sy = y * 0.19f, W = NT * 0.15f, H = NT * 0.19f;
			st->ntile[y * NT + x] = us_noise2(sx, sy) * (1 - u) * (1 - v) + us_noise2(sx - W, sy) * u * (1 - v) +
						us_noise2(sx, sy - H) * (1 - u) * v + us_noise2(sx - W, sy - H) * u * v;
			st->ntile[y * NT + x] *= 1.4f; /* the blend flattens contrast a little */
		}
	/* rock outcrop on one side at the waterline, the lighthouse headland on the far horizon opposite */
	st->rk_side = us_rand(r) < 0.5f;
	st->rk_w = us_randr(r, 0.1f, 0.14f);
	st->rk_x = st->rk_side ? us_randr(r, 0.8f, 0.86f) : us_randr(r, 0.14f, 0.2f);
	st->rk_h = us_randr(r, 0.12f, 0.16f);
	st->rk_z = us_randr(r, 0.98f, 1.06f);
	{
		float lc[4], lw[4], lh[4];
		for (int j = 0; j < 4; j++) {
			lc[j] = us_randr(r, 0.2f, 0.8f);
			lw[j] = us_randr(r, 0.18f, 0.4f);
			lh[j] = j == 0 ? 1.0f : us_randr(r, 0.35f, 0.8f);
		}
		for (int k = 0; k <= RKN; k++) {
			float u = (float)k / RKN, hmax = 0;
			for (int j = 0; j < 4; j++) {
				float d = (u - lc[j]) / lw[j];
				if (d * d < 1.0f) {
					float hh = lh[j] * powf(1.0f - d * d, 0.45f);
					if (hh > hmax)
						hmax = hh;
				}
			}
			float crag = 0.08f * us_noise2(u * 9.0f, 5.1f) + 0.04f * us_noise2(u * 23.0f, 2.3f);
			float env = us_smooth(0.0f, 0.12f, u) * us_smooth(1.0f, 0.88f, u);
			st->rk_prof[k] = fmaxf(0.0f, (hmax + crag * hmax) * env);
			/* headland: rises from its tip toward the frame edge, a low crumpled ridge */
			float hu = us_smooth(0.0f, 0.7f, u);
			st->hd_prof[k] = hu * (0.75f + 0.25f * us_noise2(u * 6.0f, 9.3f)) + 0.08f * us_noise2(u * 19.0f, 1.7f) * hu;
		}
	}
	for (int i = 0; i < N_BIRDS; i++) {
		st->bd_x[i] = us_randr(r, 0.3f, 0.7f);
		st->bd_z[i] = st->tide_s * 0.97f;
		st->bd_dir[i] = us_rand(r) < 0.5f ? -1.0f : 1.0f;
		st->bd_leg[i] = us_rand(r) * 6.0f;
		st->bd_peck[i] = us_rand(r) * 10.0f;
		st->bd_turn[i] = us_randr(r, 2.0f, 8.0f);
	}
	for (int i = 0; i < N_CRABS; i++) {
		st->cr_x[i] = us_randr(r, 0.28f, 0.72f);
		st->cr_z[i] = us_randr(r, 0.6f, 0.82f);
		st->cr_dir[i] = us_rand(r) < 0.5f ? -1.0f : 1.0f;
		st->cr_idle[i] = us_randr(r, 1.0f, 6.0f);
	}
	st->tide_s = 1.15f - 0.4f * ctx->w->water_level;
	for (int k = 0; k <= TW; k++)
		st->shore_n[k] = us_noise2((float)k / TW * 3.0f, 7.7f);
	for (int i = 0; i < NX; i++) {
		st->wet_z[i] = shore_z(st, (i + 0.5f) / NX);
		st->wet_a[i] = 0;
	}
	/* waves already on their way in when the scene opens */
	for (int k = 0; k < 3; k++) {
		spawn_wave(st, ctx);
		for (int i = 0; i < MAX_WAVES; i++)
			if (st->wv[i].state == ROLL && st->wv[i].z == Z_SPAWN) {
				st->wv[i].z = Z_SPAWN * powf(st->wv[i].zb * 1.2f / Z_SPAWN, (k + 0.5f) / 3.0f);
				st->wv[i].age = 5.0f;
				break;
			}
	}
	st->next_wave = 2.0f;
	st->inited = 1;
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
	float t = st->t;

	st->bass_env = us_approach(st->bass_env, us_sat(a->bass * 0.8f + a->kick * 0.5f), 0.4f, ctx->real_dt);
	st->hi_env = us_approach(st->hi_env, us_sat(a->high + 0.7f * a->air), 0.3f, ctx->real_dt);
	st->swell_env = us_approach(st->swell_env, us_sat(a->swell * 1.1f), 3.0f, ctx->real_dt);
	st->chop_t += dt * (0.5f + 0.6f * a->level + 0.3f * w->wind);
	/* one envelope per musical meaning: sub = ground swell, mids = the moon path, air = fizz,
	 * width = wind in the marram, centroid = how clear the sky is */
	float rdt = ctx->real_dt;
	st->sub_env = us_approach(st->sub_env, us_sat(a->sub * 1.1f), 0.6f, rdt);
	st->mid_env = us_approach(st->mid_env, us_sat(a->mid * 0.7f + a->lowmid * 0.4f), 0.25f, rdt);
	st->air_env = us_approach(st->air_env, us_sat(a->air * 0.8f + a->high * 0.4f), 0.12f, rdt);
	st->width_env = us_approach(st->width_env, us_sat(a->width * 1.3f), 1.2f, rdt);
	st->cent_env = us_approach(st->cent_env, us_sat(a->centroid), 2.5f, rdt);
	st->gs_t += dt * (0.55f + 0.35f * st->sub_env);
	/* tide: world water level + a slow tidal breath; loud phrases push the sea up the beach */
	float tide_target = 1.15f - 0.4f * w->water_level + 0.04f * sinf(t * US_TAU / 200.0f) - 0.06f * st->swell_env;
	st->tide_s = us_approach(st->tide_s, tide_target, 10.0f, dt);
	/* kicks: a breaker slams into the rocks (rising edge of the kick envelope, eased out over ~0.4 s) */
	st->crash_cd -= rdt;
	if (a->kick > 0.4f && a->kick > st->kick_prev + 0.1f && st->crash_cd <= 0.0f) {
		st->crash = 1.0f;
		st->crash_k = us_sat(0.4f + 0.7f * a->kick);
		st->crash_age = 0.0f;
		st->crash_n += 1.0f;
		st->crash_cd = 0.9f; /* breakers need time to gather: at most one burst every ~2 beats */
		st->crash_spawn = 10 + (int)(26.0f * st->crash_k);
		st->pool_t[0] = st->pool_t[1] = -0.35f; /* the spray lands in the rock pools a moment later */
	}
	st->kick_prev = a->kick;
	st->crash = us_approach(st->crash, 0.0f, 0.3f, rdt);
	st->crash_age += rdt;
	st->pool_t[0] += rdt;
	st->pool_t[1] += rdt * 0.8f;

	float day = w->daylight, night = us_sat(1.0f - day * 2.2f);
	float storm = w->storm_charge;
	float foam_k = 0.35f + 0.65f * st->hi_env;

	/* ---- spawn waves on phrase-length swells ---- */
	st->next_wave -= dt;
	if (st->next_wave <= 0) {
		spawn_wave(st, ctx);
		float period = (8.5f - 3.0f * st->swell_env - 1.5f * storm) * us_randr(r, 0.8f, 1.2f);
		st->next_wave = us_clampf(period, 3.5f, 11.0f);
	}

	/* ---- wave lifecycle ---- */
	for (int i = 0; i < MAX_WAVES; i++) {
		wave_t *wv = &st->wv[i];
		if (wv->state == DONE)
			continue;
		wv->age += dt;
		if (wv->state == ROLL || wv->state == BREAK) {
			/* shoaling: speed ~ depth, so swells slow as they come in */
			float k = 0.1f * (0.85f + 0.3f * a->level + 0.2f * st->swell_env);
			wv->z -= wv->z * k * dt;
			float shoal = us_smooth(Z_SPAWN, wv->zb, wv->z);
			wv->steep = shoal * shoal;
			float grow = us_sat(wv->age / 3.0f); /* rises out of the sea, no pop */
			if (wv->state == ROLL) {
				wv->hf = wv->amp * 0.5f * (0.25f + 0.75f * shoal) * grow;
				if (wv->z < wv->zb) {
					wv->state = BREAK;
					wv->foam = 1.0f;
					wv->brk_age = 0;
				}
			} else {
				wv->brk_age += dt;
				wv->hf = wv->amp * 0.5f * expf(-wv->brk_age * 0.7f);
				wv->foam = us_approach(wv->foam, 0.5f, 5.0f, dt);
			}
			/* every sizeable wave also bursts on the rocks as it passes them (the surf breathes in silence) */
			if (!wv->rock_hit && wv->z < st->rk_z * 1.08f) {
				wv->rock_hit = 1;
				float k = 0.3f + 0.6f * wv->amp;
				if (k > st->crash) {
					st->crash = k;
					st->crash_k = k;
					st->crash_age = 0.0f;
					st->crash_n += 1.0f;
					st->crash_spawn = 6 + (int)(14.0f * k);
				}
			}
			if (wv->z <= st->tide_s * 1.02f) {
				wv->state = SWASH;
				wv->run_t = 0;
				wv->hf = 0;
				wv->reach = wv->run_max * (1.0f + 0.5f * st->bass_env);
				/* a fresh lace layer for this swash (reuse the older slot) */
				lace_t *L = !st->lace[0].on ? &st->lace[0]
					    : !st->lace[1].on ? &st->lace[1]
					    : st->lace[0].clock > st->lace[1].clock ? &st->lace[0] : &st->lace[1];
				L->on = 1;
				L->wave = i;
				L->clock = 0;
				L->Tu = 1.6f + 1.2f * wv->amp;
				L->Td = 3.0f + 1.5f * wv->amp;
				L->reach = wv->reach;
				L->seed = wv->seed;
			}
		}
	}

	/* per-column crest tables (noise once per column per wave, not per sample) */
	for (int i = 0; i < MAX_WAVES; i++) {
		const wave_t *wv = &st->wv[i];
		if (wv->state != ROLL && wv->state != BREAK)
			continue;
		for (int k = 0; k <= TW; k++) {
			float xf = (float)k / TW;
			st->cz_tab[i][k] = crest_z(wv, xf, t);
			st->al_tab[i][k] = 0.15f + 0.85f * us_smooth(-0.5f, 0.45f, us_noise2(xf * 3.2f + wv->seed, 3.3f));
		}
	}

	/* ---- sky: stars, moon or sun, a few cloud streaks ---- */
	if (night > 0.02f) {
		int ns = (int)(N_STARS * (0.4f + 0.6f * dens) * (ctx->crisp ? 0.5f : 1.0f));
		for (int i = 0; i < ns; i++) {
			float sy = 0.02f + st->star_y[i] * (HOR - 0.05f);
			float tw = 0.5f + 0.5f * sinf(t * (0.7f + st->star_p[i] * 0.05f) + st->star_p[i]);
			float hb = hash1(st->star_p[i]);
			int band = i % 3 != 0;
			/* the band is dust: many faint pinpricks; a few scattered stars are bright and twinkle */
			float v = night * (band ? (0.08f + 0.08f * st->cent_env + 0.16f * hb) * (0.7f + 0.3f * tw) : (0.12f + 0.4f * tw * tw) * (0.4f + 0.6f * hb));
			/* haze near the horizon swallows the faintest */
			v *= us_smooth(HOR - 0.005f, HOR - 0.07f, sy);
			if (band && !ctx->crisp) /* dust: a single pixel each (hundreds of them) */
				us_put(c, (int)(st->star_x[i] * A * c->s), (int)(sy * c->s), v * 1.3f, (i & 4) ? US_MAT_WATER : US_MAT_LIGHT);
			else
				us_point(c, st->star_x[i] * A, sy, ctx->crisp ? 0.0016f : 0.001f + 0.0006f * hb, v, US_MAT_LIGHT);
		}
	}
	/* horizon haze: faint strata where the sky meets the sea */
	for (int k = 0; k < 5; k++) {
		float y = HOR - 0.006f - 0.011f * k * (1.0f + 0.3f * k);
		float v = (0.08f - 0.014f * k) * (0.5f + 0.5f * night + 0.6f * day);
		float x0 = (0.03f * k + 0.05f * us_noise2(k * 1.7f, t * 0.01f)) * A;
		us_line(c, x0, y, A - x0 * 0.7f, y, 0.001f, v, US_MAT_WATER);
	}
	float body_x = -1, body_v = 0;
	{
		float np = fmodf(w->day_phase + 0.5f, 1.0f); /* 0.5 = midnight */
		float mt = (np - 0.22f) / 0.56f;
		float lp = fmodf(w->season * 3.0f + 0.37f, 1.0f); /* ~12 lunar months a world-year */
		float illum = 0.5f - 0.5f * cosf(lp * US_TAU);
		if (mt > 0.0f && mt < 1.0f && night > 0.05f) {
			float mx = A * (0.15f + 0.7f * mt);
			float my = HOR - 0.05f - 0.2f * sinf(US_PI * mt);
			float mr = 0.034f;
			float mv = night * (0.75f + 0.25f * illum);
			us_ring(c, mx, my, mr, 0.001f, 0.14f * night, US_MAT_LIGHT); /* earthshine */
			/* a soft halo in the thin high haze */
			if (!ctx->crisp) {
				/* brighter music clears the air: the halo widens and lifts */
				float hk = 0.85f + 0.45f * st->cent_env;
				us_ring(c, mx, my, mr * 2.6f * hk, 0.0012f, (0.05f + 0.05f * st->cent_env) * mv, US_MAT_LIGHT);
				us_ring(c, mx, my, mr * 4.2f * hk, 0.0012f, (0.025f + 0.035f * st->cent_env) * mv, US_MAT_WATER);
			}
			float cphi = cosf(lp * US_TAU);
			int waxing = lp < 0.5f;
			if (ctx->crisp) {
				float step = 1.0f / c->s;
				for (float dy = -mr; dy <= mr; dy += step) {
					float hw = sqrtf(fmaxf(0.0f, mr * mr - dy * dy));
					float x0 = waxing ? hw * cphi : -hw, x1 = waxing ? hw : -hw * cphi;
					if (x1 > x0)
						us_line(c, mx + x0, my + dy, mx + x1, my + dy, step, mv, US_MAT_LIGHT);
				}
			} else {
				float pts[2 * 25], tpts[2 * 25];
				for (int k = 0; k <= 24; k++) {
					float u = -US_PI * 0.5f + US_PI * k / 24.0f;
					float dy = sinf(u) * mr, hw = cosf(u) * mr;
					pts[2 * k] = mx + (waxing ? hw : -hw);
					pts[2 * k + 1] = my + dy;
					tpts[2 * k] = mx + (waxing ? hw * cphi : -hw * cphi);
					tpts[2 * k + 1] = my + dy;
				}
				us_polyline(c, pts, 25, 0.0016f, mv, US_MAT_LIGHT);
				us_polyline(c, tpts, 25, 0.0012f, mv * 0.7f, US_MAT_LIGHT);
				/* engraved hatching across the lit part */
				for (float dy = -mr * 0.9f; dy <= mr * 0.9f; dy += mr * 0.15f) {
					float hw = sqrtf(fmaxf(0.0f, mr * mr - dy * dy));
					float x0 = waxing ? hw * cphi : -hw, x1 = waxing ? hw : -hw * cphi;
					if (x1 > x0 + 0.002f)
						us_line(c, mx + x0 + 0.002f, my + dy, mx + x1 - 0.002f, my + dy, 0.001f, mv * 0.35f, US_MAT_LIGHT);
				}
			}
			body_x = mx, body_v = night * (0.3f + 0.7f * illum);
		}
		float sp_ = (w->day_phase - 0.22f) / 0.56f;
		if (day > 0.25f && sp_ > 0.0f && sp_ < 1.0f) {
			float sx = A * (0.15f + 0.7f * sp_);
			float sy = HOR - 0.05f - 0.24f * sinf(US_PI * sp_);
			float sv = us_sat((day - 0.25f) * 2.0f);
			if (ctx->crisp)
				us_disc(c, sx, sy, 0.026f, 0.6f * sv, US_MAT_LIGHT);
			else {
				us_ring(c, sx, sy, 0.026f, 0.0016f, 0.7f * sv, US_MAT_LIGHT);
				us_ring(c, sx, sy, 0.034f + 0.003f * st->bass_env, 0.001f, 0.25f * sv, US_MAT_LIGHT);
			}
			if (sv > body_v)
				body_x = sx, body_v = sv * 0.9f;
		}
		/* clouds: slow banks of cumulus drifting with the wind, flat-bottomed, their tops a row of billows;
		 * the side toward the moon (sun) is lit, engraved hatching shades the rest */
		st->wind_t += dt * (0.3f + 1.5f * st->hi_env + 0.5f * w->wind);
		int ncl = 2 + (int)(dens * (N_CLOUDS - 2) + 0.5f);
		for (int k = 0; k < ncl; k++) {
			float cxf = fmodf(st->cl_x[k] + t * (0.0035f + 0.006f * w->wind) + 10.0f, 1.5f) - 0.25f;
			float cw = st->cl_w[k] * A, x0 = cxf * A, yb = st->cl_y[k];
			int nb = 3 + (int)(st->cl_w[k] * 14.0f);
			float lv = 0.1f + 0.1f * day + 0.1f * body_v;
			float lit_dir = body_x >= 0 ? (body_x > x0 + cw * 0.5f ? 1.0f : -1.0f) : 0.0f;
			/* billows: overlapping circles along the top; the outline is their upper envelope */
			float bc[24], br[24];
			if (nb > 24)
				nb = 24;
			for (int q = 0; q < nb; q++) {
				float f = (q + 0.5f) / nb;
				br[q] = cw / nb * (0.9f + 0.6f * hash1(st->cl_s[k] + q * 0.71f)) * (0.7f + 0.6f * sinf(US_PI * f));
				bc[q] = x0 + cw * f;
			}
#define NE 56
			float ex[NE + 1], ey[NE + 1];
			for (int e = 0; e <= NE; e++) {
				float x = x0 + cw * e / NE, top = yb;
				for (int q = 0; q < nb; q++) {
					float dx = x - bc[q];
					if (fabsf(dx) < br[q]) {
						float fq = (q + 0.5f) / nb;
						float yy = yb - sqrtf(br[q] * br[q] - dx * dx) * (0.6f + 0.9f * sinf(US_PI * fq)) + br[q] * 0.15f;
						if (yy < top)
							top = yy;
					}
				}
				ex[e] = x;
				ey[e] = fminf(top, yb);
			}
			if (ctx->crisp) {
				/* pixel art: a density field inside the envelope (dense under the billow tops, thinning to a
				 * ragged base, torn by noise) rendered through an ordered dither, so edges break up into
				 * wisps and the body shows its billows; each billow is lit on the moon's side */
				int p0 = (int)(x0 * c->s), p1 = (int)((x0 + cw) * c->s);
				float drift = t * 0.02f + st->cl_s[k];
				int dpx = (int)(drift / 26.0f * c->s) + k * 23, dpy = k * 17;
				for (int px = p0 < 0 ? 0 : p0; px <= p1 && px < c->w; px++) {
					float xx = (px + 0.5f) / c->s;
					float fe = (xx - x0) / cw * NE;
					int e = (int)us_clampf(fe, 0, NE - 1);
					float top = us_lerp(ey[e], ey[e + 1], us_clampf(fe - e, 0, 1));
					float fx = us_clampf(fe / NE, 0.0f, 1.0f);
					float ends = us_smooth(0.0f, 0.12f, fx) * us_smooth(1.0f, 0.88f, fx);
					/* ragged, wind-torn base */
					float bot = yb + 0.008f * us_noise2(xx * 22.0f + drift, k * 3.1f) - 0.004f;
					/* which billow is overhead: its centre sets the per-billow shading */
					int qb = (int)us_clampf(fx * nb, 0, nb - 1);
					float bs = lit_dir * (xx - bc[qb]) / (br[qb] + 1e-4f);
					int y0 = (int)(top * c->s) - 1, y1 = (int)(bot * c->s);
					float hh = fmaxf(bot - top, 1e-3f);
					for (int py = y0 < 0 ? 0 : y0; py <= y1 && py < c->h; py++) {
						float yy = (py + 0.5f) / c->s;
						float d_top = (yy - top) / hh; /* 0 at the top .. 1 at the base */
						float n = st->ntile[((py + dpy) & (NT - 1)) * NT + ((px + dpx) & (NT - 1))];
						float dn = (1.05f - 0.75f * d_top + 0.5f * n) * ends;
						if (d_top < 0.0f) /* a fringe of wisps just above the outline */
							dn = (0.55f + 0.9f * n) * ends + d_top * hh * c->s * 0.4f;
						if (dn <= bayer4(px, py))
							continue;
						float v;
						if (d_top < 0.14f && n > -0.25f) /* lit rim, broken where the noise tears it */
							v = 0.3f + 0.14f * body_v + 0.12f * us_sat(bs) * body_v;
						else
							v = 0.11f + 0.05f * day + 0.08f * (1.0f - d_top) * us_sat(0.4f + bs);
						us_put(c, px, py, v, US_MAT_WATER);
					}
				}
			} else {
				for (int e = 0; e < NE; e++) {
					float side = lit_dir * ((ex[e] + ex[e + 1]) * 0.5f - (x0 + cw * 0.5f)) / (cw * 0.5f);
					float v = lv * (1.0f + 1.4f * us_sat(side));
					wu_line(c, ex[e], ey[e], ex[e + 1], ey[e + 1], v, side > 0.5f && body_v > 0.3f ? US_MAT_LIGHT : US_MAT_WATER);
				}
				wu_line(c, x0 + cw * 0.04f, yb, x0 + cw * 0.96f, yb, lv * 0.6f, US_MAT_WATER);
				/* shading: horizontal strokes inside, following the billows down */
				for (int h = 1; h <= 4; h++) {
					float px = 0, py = 0;
					int on = 0;
					for (int e = 0; e <= NE; e += 2) {
						float yy = yb - (yb - ey[e]) * h / 5.0f;
						if (on && yb - ey[e] > 0.01f)
							wu_line(c, px, py, ex[e], yy, lv * 0.35f, US_MAT_WATER);
						px = ex[e];
						py = yy;
						on = yb - ey[e] > 0.01f;
					}
				}
			}
		}
	}

#undef NE
	/* ---- a far headland with a lighthouse, its beam turning slowly over the sea ---- */
	{
		int right = !st->rk_side;
		float hx0 = right ? 0.8f * A : 0.0f, hx1 = right ? A : 0.2f * A, hw = hx1 - hx0;
		const float HH = 0.024f;
		float tx = right ? A * 0.915f : A * 0.085f; /* tower */
		float pu = (tx - hx0) / hw;
		float pu_ = right ? pu : 1.0f - pu;
		int ti = (int)(pu_ * RKN);
		float ty = HOR - HH * st->hd_prof[ti < RKN ? ti : RKN] - 0.0005f;
		float th = 0.034f, tw0 = 0.0045f, tw1 = 0.0028f;
		float lx = tx, ly = ty - th - 0.003f;
		float lv = 0.22f + 0.1f * day;
		if (ctx->crisp) {
			int p0 = (int)(hx0 * c->s), p1 = (int)(hx1 * c->s);
			for (int px = p0; px < p1 && px < c->w; px++) {
				float u = ((px + 0.5f) / c->s - hx0) / hw;
				float uu = right ? u : 1.0f - u;
				float top = HOR - HH * st->hd_prof[(int)(us_sat(uu) * RKN)];
				int y0 = (int)(top * c->s), y1 = (int)(HOR * c->s);
				for (int py = y0; py < y1; py++)
					us_put(c, px, py, py == y0 ? lv : (bayer4(px, py) < 0.35f ? 0.12f : 0.07f), US_MAT_EARTH);
			}
			us_line(c, tx, ty, tx, ly + 0.003f, 2.0f / c->s, 0.42f, US_MAT_STEM);
		} else {
			float px = 0, py = 0;
			for (int k = 0; k <= 40; k++) {
				float u = k / 40.0f, uu = right ? u : 1.0f - u;
				float x = hx0 + hw * u, y = HOR - HH * st->hd_prof[(int)(uu * RKN)];
				if (k)
					wu_line(c, px, py, x, y, lv, US_MAT_EARTH);
				/* a few strata hatched into the cliff face */
				if (k % 3 == 1 && y < HOR - 0.004f)
					wu_line(c, x, y + 0.003f, x + (right ? 0.006f : -0.006f), HOR - 0.001f, lv * 0.35f, US_MAT_EARTH);
				px = x, py = y;
			}
			/* tapering tower, gallery and lantern */
			wu_line(c, tx - tw0, ty, tx - tw1, ty - th, 0.4f, US_MAT_STEM);
			wu_line(c, tx + tw0, ty, tx + tw1, ty - th, 0.4f, US_MAT_STEM);
			for (int b = 1; b <= 2; b++) { /* painted bands */
				float f = b / 3.0f;
				float hwb = us_lerp(tw0, tw1, f);
				wu_line(c, tx - hwb, ty - th * f, tx + hwb, ty - th * f, 0.25f, US_MAT_STEM);
			}
			wu_line(c, tx - tw1 * 1.6f, ty - th, tx + tw1 * 1.6f, ty - th, 0.4f, US_MAT_STEM);
			wu_line(c, tx - tw1, ty - th - 0.006f, tx + tw1, ty - th - 0.006f, 0.35f, US_MAT_STEM);
			wu_line(c, tx, ty - th - 0.006f, tx, ty - th - 0.009f, 0.35f, US_MAT_STEM);
		}
		/* the beam: a slow steady rotation (keeps turning in silence); phrases lengthen its reach */
		st->lh_ph += dt * US_TAU / 11.0f;
		float cph = cosf(st->lh_ph), sph = sinf(st->lh_ph);
		float lamp = 0.35f + 0.6f * powf(us_sat(sph), 12.0f); /* flashes as it swings past us */
		float lit = (0.4f + 0.6f * night) * (0.5f + 0.5f * sph);
		if (ctx->crisp)
			us_point(c, lx, ly, (lamp > 0.6f ? 2.0f : 1.0f) / c->s, lamp, US_MAT_LIGHT);
		else {
			us_point(c, lx, ly, 0.0016f, lamp, US_MAT_LIGHT);
			if (lamp > 0.5f)
				us_ring(c, lx, ly, 0.004f + 0.012f * (lamp - 0.5f), 0.001f, 0.6f * (lamp - 0.35f), US_MAT_LIGHT);
		}
		float len = (0.08f + 0.5f * fabsf(cph)) * A * (0.65f + 0.35f * st->swell_env + 0.1f * night);
		if (lit > 0.05f && len > 0.02f) {
			float ex_ = lx + cph * len, eyc = ly - 0.012f * fabsf(cph);
			if (ctx->crisp) {
				/* a dithered shaft that thins out with distance */
				int n = (int)(len * c->s);
				for (int g = 1; g < n; g++) {
					float f = (float)g / n;
					float x = lx + (ex_ - lx) * f, y = us_lerp(ly, eyc, f);
					int px = (int)(x * c->s);
					for (int dy = -1; dy <= 1; dy++) {
						int py = (int)(y * c->s) + dy;
						float in = lit * (1.0f - f) * (1.0f - f) * (dy ? 0.35f + 0.5f * f : 1.0f);
						if (in * 1.4f > bayer4(px, py) + 0.05f)
							us_put(c, px, py, 0.16f + 0.2f * in, US_MAT_LIGHT);
					}
				}
			} else {
				const int SEG = 12;
				for (int ray = -2; ray <= 2; ray++) {
					float ey_ = eyc + ray * (0.002f + 0.012f * len);
					float qx = lx, qy = ly;
					for (int g = 1; g <= SEG; g++) {
						float f = (float)g / SEG;
						float x = us_lerp(lx, ex_, f), y = us_lerp(ly, ey_, f);
						float v = lit * (ray == 0 ? 0.24f : ray == 1 || ray == -1 ? 0.13f : 0.06f) * (1.0f - f) * (1.0f - f);
						if (x < 0 || x > A)
							break;
						wu_line(c, qx, qy, x, y, v, US_MAT_LIGHT);
						qx = x, qy = y;
					}
				}
			}
		}
	}
	/* ---- sand: ripple dashes ---- */
	{
		/* ripple marks: long wavy ridges across the beach, parallel to the water, breaking up and rejoining;
		 * moonlight catches the crests */
		int rows = ctx->crisp ? 9 + (int)(dens * 6) : 16 + (int)(dens * 14);
		const int NR = ctx->crisp ? 32 : 64;
		for (int k = 0; k < rows; k++) {
			float f = (k + 0.5f) / rows;
			float z = st->tide_s * powf(0.56f / st->tide_s, f) * 0.97f;
			float y = proj_y(z);
			if (y > 1.02f)
				continue;
			float scale = KP / z;
			float px = 0, py = 0, pv = 0;
			for (int i = 0; i <= NR; i++) {
				float xf = (float)i / NR;
				float X = (xf - 0.5f) * A / scale;
				float s = us_noise2(X * 0.7f + 11.0f, z * 4.0f + k * 0.37f);
				float x = xf * A, yy = y + 0.006f * scale * us_noise2(X * 1.6f, z * 5.0f + k);
				float shine = body_x >= 0 ? body_v * expf(-((x - body_x) * (x - body_x)) / (0.09f * A * A)) : 0.0f;
				float v = us_smooth(-0.15f, 0.35f, s) * (0.14f + 0.12f * s + 0.05f * day + 0.15f * shine) * (0.6f + 0.4f * scale);
				if (i > 0 && (v > 0.03f || pv > 0.03f)) {
					if (ctx->crisp) {
						if (v > 0.1f)
							us_line(c, px, py, x, yy, 0.001f, v + 0.1f, US_MAT_EARTH);
					} else
						wu_line(c, px, py, x, yy, 0.5f * (v + pv), shine > 0.35f && s > 0.3f ? US_MAT_LIGHT : US_MAT_EARTH);
				}
				px = x, py = yy, pv = v;
			}
		}
		/* shells and pebbles, sized by distance */
		int nsh = (int)(N_SHELLS * (0.4f + 0.6f * dens));
		float zfar = st->tide_s * 0.9f, znear = 0.56f;
		for (int i = 0; i < nsh; i++) {
			float z = znear + (zfar - znear) * st->sh_z[i];
			float x = st->sh_x[i] * A, y = proj_y(z);
			if (y > 1.01f)
				continue;
			float sc = KP / z;
			float rr = (0.0025f + 0.004f * st->sh_k[i]) * sc;
			int shell = st->sh_k[i] > 0.55f;
			uint8_t m = shell ? US_MAT_ACCENT : US_MAT_EARTH;
			float v = (shell ? 0.4f : 0.28f) + 0.15f * day;
			if (ctx->crisp) {
				us_point(c, x, y, 0.0012f + rr * 0.5f, v, m);
				continue;
			}
			float ca = cosf(st->sh_a[i]), sa = sinf(st->sh_a[i]);
			float pts[2 * 11];
			for (int k = 0; k <= 10; k++) {
				float a = US_TAU * k / 10.0f;
				float ex = cosf(a) * rr * (shell ? 1.0f : 1.3f), ey = sinf(a) * rr * 0.45f * (shell ? 1.0f - 0.25f * cosf(a) : 1.0f);
				pts[2 * k] = x + ex * ca - ey * sa * 0.4f;
				pts[2 * k + 1] = y + ex * sa * 0.3f + ey;
			}
			for (int k = 0; k < 10; k++)
				wu_line(c, pts[2 * k], pts[2 * k + 1], pts[2 * k + 2], pts[2 * k + 3], v, m);
			if (shell) /* ribs fanning from the hinge */
				for (int k = -1; k <= 1; k++)
					wu_line(c, x - rr * ca * 0.8f, y, x + rr * cosf(st->sh_a[i] + k * 0.5f) * 0.7f, y - rr * 0.3f + k * rr * 0.12f, v * 0.6f, m);
		}
		/* a driftwood log stranded above the tideline */
		{
			float z = st->log_z, sc = KP / z;
			float L = 0.42f * sc, th = 0.016f * sc;
			float cx = st->log_x * A, cy = proj_y(z);
			if (cy < 1.0f) {
				float ca = cosf(st->log_a), sa = sinf(st->log_a) * 0.3f;
				float ax = cx - ca * L * 0.5f, ay = cy - sa * L * 0.5f, bx = cx + ca * L * 0.5f, by = cy + sa * L * 0.5f;
				float v = 0.36f + 0.12f * day;
				if (ctx->crisp) {
					us_line(c, ax, ay, bx, by, th * 1.6f, v * 0.8f, US_MAT_EARTH);
				} else {
					for (int k = -2; k <= 2; k++) { /* grain */
						float o = k * th * 0.45f;
						float s0 = 0.04f * (k & 1), s1 = 1.0f - 0.05f * ((k + 2) & 1);
						wu_line(c, us_lerp(ax, bx, s0), us_lerp(ay, by, s0) + o, us_lerp(ax, bx, s1), us_lerp(ay, by, s1) + o,
							k == -2 || k == 2 ? v : v * 0.45f, US_MAT_EARTH);
					}
					/* the root end */
					for (int k = 0; k < 4; k++) {
						float ra = US_PI + (k - 1.5f) * 0.55f;
						wu_line(c, ax, ay, ax + cosf(ra) * th * 2.2f, ay + sinf(ra) * th * 1.6f, v * 0.8f, US_MAT_EARTH);
					}
					us_ring(c, bx, by, th * 0.9f, 0.001f, v, US_MAT_EARTH);
				}
			}
		}
	}

	/* ---- the sea: ridge lines near -> far with hidden-line removal ---- */
	{
		int ns = ctx->crisp ? NS / 2 : NS;
		int rows = ctx->crisp ? 26 + (int)(dens * 16) : 32 + (int)(dens * 32);
		float glit = body_x >= 0 ? 0.35f + 0.65f * body_v : 0.0f;
		/* the melody (mids) makes the moon path shimmer: glints re-roll faster and the path widens */
		st->gl_t += dt * (5.0f + 9.0f * st->mid_env);
		float tq = floorf(st->gl_t);
		float gwk = 0.85f + 0.6f * st->mid_env;
		for (int i = 0; i <= ns; i++)
			st->occ[i] = proj_y(shore_z(st, (float)i / ns)) + 0.002f;
		float zs0 = st->tide_s * 0.98f;
		int rk_done = 0;
		for (int k = 0; k < rows; k++) {
			float f = (float)k / (rows - 1);
			/* rows evenly spaced on screen (a ridge plot), from the shoreline up to just below the horizon */
			float ys = proj_y(zs0), ye = HOR + 0.006f;
			float z = KP / (us_lerp(ys, ye, f) - HOR);
			if (!rk_done && z > st->rk_z) { /* the rock hides the water behind it */
				rk_done = 1;
				for (int i = 0; i <= ns; i++) {
					float xf = (float)i / ns;
					if (rock_prof(st, xf) > 0.0f)
						st->occ[i] = fminf(st->occ[i], rock_top(st, xf));
				}
			}
			float base = (0.27f + 0.16f * day) * (1.0f - 0.7f * f * f);
			float px = 0, py = 0;
			int pvis = 0;
			for (int i = 0; i <= ns; i++) {
				float xf = (float)i / ns;
				float zz = z;
				float zsh = shore_z(st, xf);
				if (zz < zsh) /* this row is on the sand at this column */
					zz = zsh;
				float X = (xf - 0.5f) * A * zz / KP;
				float h = sea_h(st, xf, zz, X, a->level);
				float x = xf * A, y = proj_y(zz) - KP * h / zz;
				int vis = y < st->occ[i] && zz == z;
				if (vis && pvis && i > 0) {
					float v = base + 1.4f * fmaxf(h, 0.0f) * (1.0f - 0.5f * f);
					uint8_t m = US_MAT_WATER;
					/* glitter path: glints re-rolled ~7x a second, wider toward the viewer */
					if (glit > 0) {
						float gw = (0.015f + 0.22f * KP / zz) * gwk;
						float dx = (x - body_x) / gw;
						float pg = glit * expf(-dx * dx);
						float hsh = hash1(tq * 0.131f + k * 1.37f + i * 0.711f);
						if (hsh < pg * (0.2f + 0.3f * st->mid_env + 0.12f * st->hi_env)) {
							v += pg * (0.5f + 0.5f * hash1(hsh * 17.0f));
							m = US_MAT_LIGHT;
						}
					}
					v = us_sat(v);
					if (ctx->crisp)
						us_line(c, px, py, x, y, 0.002f, v, m);
					else
						wu_line(c, px, py, x, y, v, m);
				}
				if (y < st->occ[i])
					st->occ[i] = y;
				px = x, py = y;
				pvis = vis;
			}
		}
		/* glitter path: loose glints scattered over the water under the moon / sun, wider toward the viewer,
		 * re-rolled ~7x a second; highs make it sparkle harder */
		if (glit > 0) {
			int ng = (int)(MAX_GLINT * (0.35f + 0.35f * dens + 0.3f * st->hi_env) * us_sat(glit * 1.3f) * (ctx->crisp ? 0.35f : 1.0f));
			for (int g = 0; g < ng; g++) {
				float h1 = hash1(tq * 0.173f + g * 0.618f + 0.11f), h2 = hash1(h1 * 91.3f + g * 0.37f);
				float h3 = hash1(h2 * 53.7f + 0.5f), h4 = hash1(h3 * 17.1f + g * 0.07f);
				float ysn = proj_y(st->tide_s) - 0.004f;
				float y = HOR + 0.004f + (ysn - HOR - 0.004f) * h1 * h1 * 0.98f + 0.02f * h1;
				float z = KP / (y - HOR);
				float gw = (0.015f + 0.22f * KP / z) * gwk;
				float gx = (h2 + h3 + h4 - 1.5f) * 1.4f; /* ~gaussian */
				float x = body_x + gx * gw;
				if (x < 0 || x > A)
					continue;
				float xf = x / A;
				if (z < shore_z(st, xf) * 1.01f)
					continue;
				float X = (xf - 0.5f) * A * z / KP;
				float yy = y - KP * sea_h(st, xf, z, X, a->level) / z;
				float pg = glit * expf(-gx * gx);
				float len = (0.0015f + 0.009f * KP / z) * (0.4f + h4);
				float v = us_sat(pg * (0.45f + 0.55f * h3) * (0.75f + 0.25f * st->hi_env + 0.3f * st->mid_env));
				if (ctx->crisp)
					us_put(c, (int)(x * c->s), (int)(yy * c->s), v, US_MAT_LIGHT);
				else
					wu_line(c, x - len, yy, x + len, yy, v, US_MAT_LIGHT);
			}
		}
		/* horizon */
		us_line(c, 0, HOR, A, HOR, 0.0012f, 0.3f + 0.2f * day, US_MAT_WATER);
	}

	/* ---- the rock outcrop at the waterline, rock pools at its foot, and the kick breaking over it ---- */
	{
		float sc = KP / st->rk_z, yb = rock_base(st);
		float xa = (st->rk_x - st->rk_w * 0.5f) * A, xw = st->rk_w * A, xc = st->rk_x * A;
		float lit_dir = body_x >= 0 ? (body_x > xc ? 1.0f : -1.0f) : 0.0f;
		float rv = 0.2f + 0.1f * day;
		if (ctx->crisp) {
			int p0 = (int)(xa * c->s), p1 = (int)((xa + xw) * c->s);
			for (int px = p0 < 0 ? 0 : p0; px <= p1 && px < c->w; px++) {
				float xx = (px + 0.5f) / c->s, xf = xx / A;
				if (rock_prof(st, xf) <= 0.0f)
					continue;
				float top = rock_top(st, xf);
				float side = lit_dir * (xx - xc) / (xw * 0.5f);
				int y0 = (int)(top * c->s), y1 = (int)(yb * c->s);
				for (int py = y0 < 0 ? 0 : y0; py <= y1 && py < c->h; py++) {
					float n = st->ntile[((py * 2 + 7) & (NT - 1)) * NT + ((px * 2 + 31) & (NT - 1))];
					float v;
					if (py == y0)
						v = rv + 0.1f + 0.3f * body_v * us_sat(0.3f + side);
					else if (n + 0.4f * us_sat(side) > 0.25f && bayer4(px, py) < 0.5f)
						v = rv * (0.8f + 0.5f * us_sat(side) * body_v); /* crags catching light */
					else
						v = 0.09f; /* dark body hides the sand/sea behind */
					us_put(c, px, py, v, US_MAT_EARTH);
				}
			}
		} else {
			const int RS = 48;
			float px = 0, py = 0;
			for (int k = 0; k <= RS; k++) {
				float x = xa + xw * k / RS, xf = x / A, y = rock_top(st, xf);
				float side = lit_dir * (x - xc) / (xw * 0.5f);
				if (k)
					wu_line(c, px, py, x, y, rv + 0.3f * body_v * us_sat(0.2f + side),
						side > 0.3f && body_v > 0.4f ? US_MAT_LIGHT : US_MAT_EARTH);
				px = x, py = y;
			}
			/* strata and cracks: broken contours following the crown down */
			for (int h = 1; h <= 4; h++) {
				float qx = 0, qy = 0;
				for (int k = 1; k < RS; k++) {
					float x = xa + xw * k / RS, xf = x / A;
					float pr = rock_prof(st, xf);
					float y = yb - (yb - rock_top(st, xf)) * (1.0f - h * 0.2f) + 0.002f * us_noise2(x * 80.0f, h);
					float side = lit_dir * (x - xc) / (xw * 0.5f);
					float n = us_noise2(x * 30.0f, h * 2.7f);
					if (k > 1 && pr > 0.08f && n > -0.1f)
						wu_line(c, qx, qy, x, y, (0.1f + 0.12f * us_sat(side) * body_v) * (1.0f - 0.15f * h), US_MAT_EARTH);
					qx = x, qy = y;
				}
			}
			/* wash swirling at its foot */
			for (int k = 0; k < 3; k++) {
				float y = yb + 0.0015f * k + 0.001f * us_noise2(k * 3.0f, t * 0.7f);
				float sw = 0.04f * us_noise2(k * 1.3f, t * 0.4f);
				wu_line(c, xa + xw * (0.08f + sw), y, xa + xw * (0.92f + sw), y, 0.12f + 0.15f * st->crash, US_MAT_WATER);
			}
		}
		/* rock pools on the sand in front of the rock: they hold the moon, and ring when the spray lands */
		for (int q = 0; q < 2; q++) {
			float pz = st->rk_z * (q ? 0.86f : 0.92f);
			if (pz > st->tide_s * 0.995f) /* under the sea at this tide */
				continue;
			float pxc = xc + (q ? 0.35f : -0.2f) * xw * (st->rk_side ? -1.0f : 1.0f);
			float psc = KP / pz, py_ = proj_y(pz);
			float rx = (q ? 0.055f : 0.04f) * psc, ry = rx * 0.18f;
			float pv = 0.2f + 0.08f * day;
			if (ctx->crisp) {
				us_line(c, pxc - rx, py_, pxc + rx, py_, 1.0f / c->s, pv, US_MAT_WATER);
				us_line(c, pxc - rx * 0.7f, py_ - ry, pxc + rx * 0.7f, py_ - ry, 1.0f / c->s, pv * 0.8f, US_MAT_EARTH);
			} else {
				float qx = 0, qy = 0;
				for (int k = 0; k <= 20; k++) {
					float an = US_TAU * k / 20.0f;
					float x = pxc + cosf(an) * rx * (1.0f + 0.12f * sinf(an * 3.0f + q)), y = py_ + sinf(an) * ry;
					if (k)
						wu_line(c, qx, qy, x, y, pv, US_MAT_EARTH);
					qx = x, qy = y;
				}
			}
			/* the moon's glint in the still water */
			if (body_x >= 0 && fabsf(body_x - pxc) < A * 0.35f) {
				float gx = pxc + (body_x - pxc) * 0.08f;
				us_line(c, gx - rx * 0.25f, py_, gx + rx * 0.25f, py_, 1.0f / c->s, 0.25f + 0.5f * body_v, US_MAT_LIGHT);
			}
			/* drip rings after a crash: expand and fade over ~0.8 s */
			float pt = st->pool_t[q];
			if (pt > 0.0f && pt < 0.8f) {
				float rr = 0.15f + 0.85f * pt / 0.8f, rvv = (1.0f - pt / 0.8f) * 0.5f;
				float qx = 0, qy = 0;
				for (int k = 0; k <= 16; k++) {
					float an = US_TAU * k / 16.0f;
					float x = pxc + cosf(an) * rx * 0.6f * rr, y = py_ + sinf(an) * ry * 0.6f * rr;
					if (k) {
						if (ctx->crisp)
							us_line(c, qx, qy, x, y, 1.0f / c->s, rvv + 0.1f, US_MAT_WATER);
						else
							wu_line(c, qx, qy, x, y, rvv, US_MAT_WATER);
					}
					qx = x, qy = y;
				}
			}
		}
		/* the crash: water flung up off the rock in a fan of ballistic streaks that rise, hang and fall back,
		 * each burst shaped differently; it all plays out over ~0.9 s */
		float ca = st->crash_age / 0.9f;
		if (ca < 1.0f && st->crash_k > 0.05f) {
			int NQ = ctx->crisp ? 7 : 14;
			for (int q = 0; q < NQ; q++) {
				float h0 = hash1(q * 0.377f + st->crash_n * 0.913f + 0.05f), h1 = hash1(h0 * 31.7f + q * 0.11f);
				float u = (q + h1) / NQ;
				float xf = st->rk_x + (u - 0.5f) * st->rk_w * 0.85f;
				float pr = rock_prof(st, xf);
				if (pr < 0.12f)
					continue;
				float top = rock_top(st, xf);
				float H = st->crash_k * st->rk_h * sc * (0.5f + 1.8f * h0 * h0) * (0.4f + 0.6f * pr);
				float spread = ((u - 0.5f) * 3.2f + (h1 - 0.5f) * 1.2f + 0.4f * w->wind) * H;
				float sq = ca * (0.85f + 0.3f * h1); /* streaks don't all move in lockstep */
				float s0 = fmaxf(0.0f, sq - 0.14f);
				float v = 0.85f * powf(us_sat(1.0f - sq), 1.3f);
				if (v < 0.02f)
					continue;
				float x0_ = xf * A, qx = 0, qy = 0;
				for (int g = 0; g <= 3; g++) {
					float ss = us_lerp(s0, sq, g / 3.0f);
					float x = x0_ + spread * ss, y = top - H * (2.4f * ss - 1.7f * ss * ss);
					if (g) {
						float vv = v * (0.4f + 0.2f * g);
						if (ctx->crisp)
							us_line(c, qx, qy, x, y, 1.0f / c->s, vv, US_MAT_WATER);
						else
							wu_line(c, qx, qy, x, y, vv, US_MAT_WATER);
					}
					qx = x, qy = y;
				}
				us_point(c, qx, qy, ctx->crisp ? 1.0f / c->s : 0.0012f, v, US_MAT_WATER); /* the leading drop */
			}
			/* white water boiling over the crown */
			if (!ctx->crisp) {
				float qx = 0, qy = 0;
				for (int k = 0; k <= 24; k++) {
					float xf = st->rk_x + (k / 24.0f - 0.5f) * st->rk_w * 0.9f;
					float x = xf * A, y = rock_top(st, xf) - 0.004f * st->crash * (1.0f + us_noise2(k * 0.7f, st->crash_n));
					if (k)
						wu_line(c, qx, qy, x, y, 0.5f * st->crash, US_MAT_WATER);
					qx = x, qy = y;
				}
			}
		}
		/* spray owed by a fresh crash: thrown high, falling back in arcs */
		for (; st->crash_spawn > 0 && st->nsp < MAX_SPRAY; st->crash_spawn--) {
			float xf = st->rk_x + us_randr(r, -0.4f, 0.4f) * st->rk_w;
			float pr = rock_prof(st, xf);
			spray_t *s = &st->sp[st->nsp++];
			s->x = xf * A;
			s->y = rock_top(st, xf) - 0.004f * us_rand(r);
			s->vx = us_randn(r) * 0.03f * sc + 0.04f * w->wind * sc;
			s->vy = -us_randr(r, 0.15f, 0.45f) * sc * (0.5f + st->crash_k) * (0.5f + pr);
			s->g = 0.9f;
			s->life = fminf(us_randr(r, 0.5f, 1.1f), 2.0f * -s->vy / s->g); /* gone when it falls back to the rock */
		}
		st->crash_spawn = 0;
	}

	/* ---- foam lace behind breaking crests ---- */
	for (int wi = 0; wi < MAX_WAVES; wi++) {
		wave_t *wv = &st->wv[wi];
		if (wv->state != BREAK)
			continue;
		/* iso-lines around the ridges of a noise field (|n| < thr): organic loops and holes like real
		 * foam lace; dense at the broken front, thinning out behind it */
		float(*lx)[FW + 1] = st->lx, (*ly)[FW + 1] = st->ly, (*lf)[FW + 1] = st->lf;
		int fw = ctx->crisp ? 80 : 140, fh = ctx->crisp ? 10 : 12 + (int)(dens * (FH - 12));
		float band = (0.06f + 0.25f * wv->amp) * (1.0f + 1.5f * us_sat(wv->brk_age * 0.15f));
		for (int i = 0; i <= fw; i++) {
			float xf = (float)i / fw;
			float ft = xf * TW;
			int k0 = (int)ft < TW ? (int)ft : TW - 1;
			float zc = us_lerp(st->cz_tab[wi][k0], st->cz_tab[wi][k0 + 1], ft - k0);
			float X = (xf - 0.5f) * A * zc / KP;
			for (int j = 0; j < fh; j++) {
				float fj = (float)j / (fh - 1);
				float fz = zc * (1.0f + band * fj * fj);
				float lace = us_noise2(X * 2.0f + 0.07f * t + wv->seed, fz * 8.0f - 0.2f * t);
				float thr = (0.08f + 0.14f * foam_k) * wv->foam * (1.2f - 0.8f * fj) + (j == 0 ? 0.6f : 0.0f);
				float h = sea_h(st, xf, fz, X, a->level);
				lx[j][i] = xf * A;
				ly[j][i] = proj_y(fz) - KP * h / fz;
				lf[j][i] = thr - fabsf(lace); /* > 0 inside the foam */
			}
		}
		static const int8_t ms[16][4] = {
			{-1, -1, -1, -1}, {3, 0, -1, -1}, {0, 1, -1, -1}, {3, 1, -1, -1},
			{1, 2, -1, -1},   {3, 0, 1, 2},   {0, 2, -1, -1}, {3, 2, -1, -1},
			{2, 3, -1, -1},   {2, 0, -1, -1}, {0, 1, 2, 3},   {2, 1, -1, -1},
			{1, 3, -1, -1},   {1, 0, -1, -1}, {0, 3, -1, -1}, {-1, -1, -1, -1},
		};
		for (int j = 0; j + 1 < fh; j++) {
			float fj = (float)j / (fh - 1);
			float v = us_sat((1.0f - 0.55f * fj) * wv->foam * (0.75f + 0.35f * foam_k));
			for (int i = 0; i < fw; i++) {
				float a0 = lf[j][i], b0 = lf[j][i + 1], d0 = lf[j + 1][i], e0 = lf[j + 1][i + 1];
				int cs = (a0 > 0) | ((b0 > 0) << 1) | ((e0 > 0) << 2) | ((d0 > 0) << 3);
				if (cs == 0 || cs == 15)
					continue;
				float px[4], py[4];
#define TT(p, q) us_clampf((p) / ((p) - (q) + 1e-9f), 0, 1)
				px[0] = us_lerp(lx[j][i], lx[j][i + 1], TT(a0, b0));
				py[0] = us_lerp(ly[j][i], ly[j][i + 1], TT(a0, b0));
				px[1] = us_lerp(lx[j][i + 1], lx[j + 1][i + 1], TT(b0, e0));
				py[1] = us_lerp(ly[j][i + 1], ly[j + 1][i + 1], TT(b0, e0));
				px[2] = us_lerp(lx[j + 1][i], lx[j + 1][i + 1], TT(d0, e0));
				py[2] = us_lerp(ly[j + 1][i], ly[j + 1][i + 1], TT(d0, e0));
				px[3] = us_lerp(lx[j][i], lx[j + 1][i], TT(a0, d0));
				py[3] = us_lerp(ly[j][i], ly[j + 1][i], TT(a0, d0));
#undef TT
				for (int q = 0; q < 4 && ms[cs][q] >= 0; q += 2) {
					if (ctx->crisp)
						us_line(c, px[ms[cs][q]], py[ms[cs][q]], px[ms[cs][q + 1]], py[ms[cs][q + 1]], 0.002f, v,
							US_MAT_WATER);
					else
						wu_line(c, px[ms[cs][q]], py[ms[cs][q]], px[ms[cs][q + 1]], py[ms[cs][q + 1]], v,
							US_MAT_WATER);
				}
			}
		}
		/* spray thrown off the broken crest on hits; spindrift on highs */
		if (a->beat || us_rand(r) < dt * (0.5f + 4.0f * st->hi_env)) {
			int nsp = a->beat ? 6 + (int)(18 * a->onset * wv->amp) : 1;
			for (int k = 0; k < nsp && st->nsp < MAX_SPRAY; k++) {
				float xf = us_rand(r);
				float zc = crest_z(wv, xf, t);
				spray_t *s = &st->sp[st->nsp++];
				float sc = KP / zc;
				s->x = xf * A;
				s->y = proj_y(zc) - sc * wv->hf;
				s->vx = us_randn(r) * 0.01f * sc + 0.03f * w->wind * sc;
				s->vy = -us_randr(r, 0.05f, 0.18f) * sc * (0.5f + wv->amp);
				s->life = us_randr(r, 0.5f, 1.0f);
				s->g = 0.25f;
			}
		}
	}

	/* ---- wet sand: a dotted drying line, and a mirror sheen that reflects the moon / sun ---- */
	for (int i = 0; i < NX; i++) {
		st->wet_a[i] -= dt / 14.0f;
		if (st->wet_a[i] < 0)
			st->wet_a[i] = 0;
	}
	{
		/* sheen: horizontal hairlines across the wet band; under the moon they become a soft reflected
		 * column (steady, unlike the twinkling glitter on the water) */
		int rows = ctx->crisp ? 4 : 7;
		float gwid = 0.07f + 0.05f * day;
		for (int k = 0; k < rows; k++) {
			float f = (k + 0.5f) / rows;
			float px = 0, py = 0, pv = 0;
			for (int i = 0; i < NX; i++) {
				float xf = (i + 0.5f) / NX;
				float zs = shore_z(st, xf);
				float zw = st->wet_z[i];
				float wa = st->wet_a[i];
				float z = zs + (zw - zs) * f;
				float x = xf * A, y = proj_y(z) + 0.0008f * us_noise2(x * 30.0f, k * 3.0f);
				float refl = 0.0f;
				if (body_x >= 0) {
					float dx = (x - body_x) / (gwid * (1.0f + 1.5f * f));
					refl = body_v * expf(-dx * dx) * (0.75f + 0.25f * sinf(k * 1.7f + t * 0.6f + x * 9.0f));
				}
				float v = sqrtf(wa) * (0.06f + 0.06f * day + 1.1f * refl) * (1.0f - 0.3f * f);
				if (i > 0 && (v > 0.02f || pv > 0.02f) && zw < zs - 0.002f) {
					uint8_t m = refl > 0.25f ? US_MAT_LIGHT : US_MAT_WATER;
					if (ctx->crisp)
						us_line(c, px, py, x, y, 0.002f, fmaxf(v, pv), m);
					else
						wu_line(c, px, py, x, y, 0.5f * (v + pv), m);
				}
				px = x, py = y, pv = v;
			}
		}
		/* the moon's (sun's) reflection on the wet sand: a soft column of fine engraved hatching */
		if (body_x >= 0 && body_v > 0.05f) {
			int hr = ctx->crisp ? 5 : 16;
			for (int k = 0; k < hr; k++) {
				float f = (k + 0.5f) / hr;
				float span = gwid * (1.0f + 1.5f * f) * 2.2f;
				int c0 = (int)((body_x - span) / A * NX), c1 = (int)((body_x + span) / A * NX) + 1;
				c0 = c0 < 0 ? 0 : c0;
				c1 = c1 > NX ? NX : c1;
				float px = 0, py = 0, pv = 0;
				for (int i = c0; i < c1; i++) {
					float xf = (i + 0.5f) / NX;
					float zs = shore_z(st, xf), zw = st->wet_z[i];
					float z = zs + (zw - zs) * f;
					float x = xf * A, y = proj_y(z);
					float dx = (x - body_x) / (span * 0.45f);
					float shimmer = 0.7f + 0.3f * us_noise2(x * 25.0f + t * 0.8f, k * 1.9f);
					float v = sqrtf(st->wet_a[i]) * body_v * expf(-dx * dx) * shimmer * (1.0f - 0.4f * f);
					if (i > c0 && zw < zs - 0.002f && (v > 0.03f || pv > 0.03f)) {
						if (ctx->crisp)
							us_line(c, px, py, x, y, 0.002f, fmaxf(v, pv), US_MAT_LIGHT);
						else
							wu_line(c, px, py, x, y, 0.5f * (v + pv), US_MAT_LIGHT);
					}
					px = x, py = y, pv = v;
				}
			}
		}
		/* the wrack line itself: dotted, drying */
		for (int i = 0; i < NX; i++) {
			if (st->wet_a[i] <= 0)
				continue;
			float xf = (i + 0.5f) / NX;
			float y = proj_y(st->wet_z[i]);
			int dots = ctx->crisp ? 1 : 3;
			for (int k = 0; k < dots; k++) {
				float xx = (xf + (k - 1) * 0.33f / NX) * A;
				float jit = 0.0015f * us_noise2(xx * 40.0f, 3.0f);
				us_point(c, xx, y + jit, ctx->crisp ? 0.0016f : 0.0012f, 0.5f * st->wet_a[i], US_MAT_EARTH);
			}
		}
	}

	/* ---- foam lace on the sand: cellular web riding the sheet, stranded by the backwash, popping ---- */
	for (int li = 0; li < 2; li++) {
		lace_t *L = &st->lace[li];
		if (!L->on)
			continue;
		L->clock += dt;
		if (L->clock > L->Tu + L->Td + 9.0f) {
			L->on = 0;
			continue;
		}
		const wave_t *wv = &st->wv[L->wave];
		int nxl = ctx->crisp ? LXN / 3 : LXN, nzl = ctx->crisp ? LZN / 3 : LZN;
		/* cells in sand-plane units (X across, z along the beach) */
		float cxs = ctx->crisp ? 0.13f : 0.065f, czs = ctx->crisp ? 0.045f : 0.024f;
		float pnow; /* sheet extent as a fraction of the reach */
		if (L->clock < L->Tu) {
			float u = L->clock / L->Tu;
			pnow = 1.0f - (1.0f - u) * (1.0f - u);
		} else {
			float u = us_sat((L->clock - L->Tu) / L->Td);
			pnow = 1.0f - u * u;
		}
		uint32_t sk = (uint32_t)(L->seed * 1000.0f);
		int any = 0;
		for (int i = 0; i <= nxl; i++) {
			float xf = (float)i / nxl;
			st->cusp_tab[i] = shore_z(st, xf) * L->reach * (1.0f + 0.3f * us_noise2(xf * 4.0f + L->seed, 1.3f));
		}
		for (int j = 0; j < nzl; j++) {
			float sfr = (float)j / (nzl - 1);
			float t_reach = L->Tu * (1.0f - sqrtf(1.0f - sfr)), t_unc = L->Tu + L->Td * sqrtf(1.0f - sfr);
			float alpha, off; /* foam rides with the water while it's under the sheet */
			if (L->clock < t_reach) {
				alpha = 0;
				off = 0;
			} else if (L->clock < t_unc) {
				alpha = 1.0f;
				off = pnow;
			} else {
				alpha = expf(-(L->clock - t_unc) / (1.4f + 2.6f * sfr)); /* the high-water lace lasts longest */
				off = sfr;
			}
			for (int i = 0; i <= nxl; i++) {
				float xf = (float)i / nxl;
				float zs = shore_z(st, xf);
				float span = st->cusp_tab[i];
				float z = zs - sfr * span;
				float X = (xf - 0.5f) * A * z / KP;
				float zp = z + 0.35f * off * span; /* sample the lace where this foam came from */
				float uu = X / cxs + L->seed * 7.0f, vv = zp / czs;
				int ix = (int)floorf(uu), iz = (int)floorf(vv);
				float best = 1e9f;
				int32_t lab = 0;
				for (int dz = -1; dz <= 1; dz++)
					for (int dx = -1; dx <= 1; dx++) {
						int cx = ix + dx, cz2 = iz + dz;
						float fx, fz;
						lace_feature(cx, cz2, sk, &fx, &fz);
						float d = (fx - uu) * (fx - uu) + (fz - vv) * (fz - vv);
						if (d < best) {
							best = d;
							lab = (int32_t)((uint32_t)(cx & 0xffff) << 16 | (uint32_t)(cz2 & 0xffff));
						}
					}
				int id = j * (LXN + 1) + i;
				st->llab[id] = lab;
				st->lu[id] = uu;
				st->lv[id] = vv;
				st->lsx[id] = xf * A;
				st->lsy[id] = proj_y(z);
				st->lal[id] = alpha;
				any |= alpha > 0.02f;
			}
		}
		if (!any)
			continue;
		float vbase = (0.5f + 0.35f * foam_k) * (0.6f + 0.4f * wv->amp);
		for (int j = 0; j + 1 < nzl; j++)
			for (int i = 0; i < nxl; i++) {
				int id[4] = {j * (LXN + 1) + i, j * (LXN + 1) + i + 1, (j + 1) * (LXN + 1) + i + 1,
					     (j + 1) * (LXN + 1) + i};
				float al = 0.25f * (st->lal[id[0]] + st->lal[id[1]] + st->lal[id[2]] + st->lal[id[3]]);
				if (al < 0.02f)
					continue;
				int32_t l0 = st->llab[id[0]];
				if (l0 == st->llab[id[1]] && l0 == st->llab[id[2]] && l0 == st->llab[id[3]])
					continue;
				float vx[4], vy[4];
				int keep[4];
				int nv = 0;
				for (int e = 0; e < 4; e++) {
					int p = id[e], q = id[(e + 1) & 3];
					int32_t la = st->llab[p], lb = st->llab[q];
					if (la == lb)
						continue;
					/* both features, rebuilt from their labels; vertex where their bisector crosses this edge */
					float ax, ay, bx, by;
					uint32_t ha = lace_feature((int16_t)((uint32_t)la >> 16), (int16_t)(la & 0xffff), sk, &ax, &ay);
					uint32_t hb = lace_feature((int16_t)((uint32_t)lb >> 16), (int16_t)(lb & 0xffff), sk, &bx, &by);
					float Px = st->lu[p], Py = st->lv[p], Qx = st->lu[q], Qy = st->lv[q];
					float nx = bx - ax, ny = by - ay;
					float den = nx * (Qx - Px) + ny * (Qy - Py);
					float tt = 0.5f;
					if (fabsf(den) > 1e-9f)
						tt = us_sat((0.5f * (bx * bx + by * by - ax * ax - ay * ay) - (nx * Px + ny * Py)) / den);
					vx[nv] = us_lerp(st->lsx[p], st->lsx[q], tt);
					vy[nv] = us_lerp(st->lsy[p], st->lsy[q], tt);
					/* neighbouring "hole" cells merge into bigger holes: the wall between two of them is gone */
					float hole = 0.3f + 0.25f * (1.0f - al);
					keep[nv] = !(((ha >> 22) & 1023) < hole * 1023.0f && ((hb >> 22) & 1023) < hole * 1023.0f);
					nv++;
				}
				/* foam lies in rafts: a smooth density field; as the lace dries the rafts shrink to islands */
				float fd = 0.5f + 0.5f * us_noise2(st->lu[id[0]] * 0.12f + L->seed, st->lv[id[0]] * 0.22f);
				float edge = fd - (0.93f - 0.38f * al * (0.55f + 0.45f * foam_k));
				if (edge <= 0)
					continue;
				float v = vbase * (0.35f + 0.65f * al) * us_smooth(0.0f, 0.12f, edge);
				if (nv == 2) {
					if (!keep[0] || !keep[1])
						continue;
					if (ctx->crisp)
						us_line(c, vx[0], vy[0], vx[1], vy[1], 0.002f, v, US_MAT_WATER);
					else
						wu_line(c, vx[0], vy[0], vx[1], vy[1], v, US_MAT_WATER);
				} else if (nv > 2) {
					float mx = 0, my = 0;
					for (int k = 0; k < nv; k++)
						mx += vx[k], my += vy[k];
					mx /= nv, my /= nv;
					for (int k = 0; k < nv; k++) {
						if (!keep[k])
							continue;
						if (ctx->crisp)
							us_line(c, vx[k], vy[k], mx, my, 0.002f, v, US_MAT_WATER);
						else
							wu_line(c, vx[k], vy[k], mx, my, v, US_MAT_WATER);
					}
				}
			}
	}

	/* ---- swash: run up the sand, stall, slide back ---- */
	for (int i = 0; i < NX; i++)
		st->edge_z[i] = shore_z(st, (i + 0.5f) / NX);
	for (int wi = 0; wi < MAX_WAVES; wi++) {
		wave_t *wv = &st->wv[wi];
		if (wv->state != SWASH)
			continue;
		const int N = ctx->crisp ? 64 : 96;
		wv->run_t += dt;
		float Tu = 1.6f + 1.2f * wv->amp, Td = 3.0f + 1.5f * wv->amp;
		float reach = wv->reach;
		float p;
		int back = wv->run_t > Tu;
		if (!back) {
			float u = wv->run_t / Tu;
			p = 1.0f - (1.0f - u) * (1.0f - u);
		} else {
			float u = us_sat((wv->run_t - Tu) / Td);
			p = 1.0f - u * u;
		}
		float fade = back ? us_sat(1.0f - (wv->run_t - Tu) / Td) : 1.0f;
		float pts[2 * 97], spts[2 * 97];
		for (int i = 0; i <= N; i++) {
			float xf = (float)i / N;
			float zs = shore_z(st, xf);
			float cusp = 1.0f + 0.3f * us_noise2(xf * 4.0f + wv->seed, 1.3f);
			float zr = zs - reach * p * cusp * zs;
			pts[2 * i] = xf * A;
			pts[2 * i + 1] = proj_y(zr) + 0.0012f * us_noise2(xf * 90.0f, t * 2.0f);
			spts[2 * i] = xf * A;
			spts[2 * i + 1] = proj_y(zs - (zs - zr) * 0.45f);
		}
		/* wet sand: every wet-line column follows the uprush; the birds watch the water's edge */
		for (int col = 0; col < NX; col++) {
			float xf = (col + 0.5f) / NX;
			float zs = shore_z(st, xf);
			float zr = zs - reach * p * (1.0f + 0.3f * us_noise2(xf * 4.0f + wv->seed, 1.3f)) * zs;
			if (zr < st->edge_z[col])
				st->edge_z[col] = zr;
			if (!back && (zr < st->wet_z[col] || st->wet_a[col] < 0.3f)) {
				st->wet_z[col] = zr;
				st->wet_a[col] = 1.0f;
			}
		}
		/* the leading edge: a continuous frothy line (doubled on the uprush) */
		float fv = us_sat((0.6f + 0.4f * foam_k) * fade);
		if (ctx->crisp) {
			us_polyline(c, pts, N + 1, 0.0025f, fv, US_MAT_WATER);
		} else {
			for (int i = 0; i < N; i++) {
				float fr = 0.75f + 0.25f * us_noise2(i * 0.45f + wv->seed, t * 0.9f);
				wu_line(c, pts[2 * i], pts[2 * i + 1], pts[2 * i + 2], pts[2 * i + 3], fv * fr, US_MAT_WATER);
				if (!back)
					wu_line(c, pts[2 * i], pts[2 * i + 1] + 0.0022f, pts[2 * i + 2], pts[2 * i + 3] + 0.0022f,
						fv * fr * 0.55f, US_MAT_WATER);
			}
		}
		us_polyline(c, spts, N + 1, 0.001f, 0.15f * fade, US_MAT_WATER);
		/* bubbles in the sheet, popping on the backwash */
		int nb = (int)((20 + 50 * dens) * (0.5f + foam_k));
		for (int k = 0; k < nb; k++) {
			float xf = hash1(k * 3.17f + wv->seed);
			float d = hash1(k * 5.3f + wv->seed * 0.3f);
			float zs = shore_z(st, xf);
			float cusp = 1.0f + 0.3f * us_noise2(xf * 4.0f + wv->seed, 1.3f);
			float zr = zs - reach * p * cusp * zs;
			float z = zs + (zr - zs) * d;
			float life = back ? us_sat(fade * 1.5f - hash1(k * 0.7f)) : 1.0f;
			if (life <= 0)
				continue;
			us_point(c, xf * A, proj_y(z), ctx->crisp ? 0.0016f : 0.001f, 0.55f * life, US_MAT_WATER);
		}
		/* fizz: the hats and air make the thin sheet hiss, bubbles winking in and out every frame */
		int nfz = (int)(st->air_env * st->air_env * (40.0f + 70.0f * dens) * fade * (ctx->crisp ? 0.4f : 1.0f));
		for (int k = 0; k < nfz; k++) {
			float xf = us_rand(r), d = us_rand(r);
			float zs = shore_z(st, xf);
			float zr = zs - reach * p * (1.0f + 0.3f * us_noise2(xf * 4.0f + wv->seed, 1.3f)) * zs;
			float z = zs + (zr - zs) * (0.15f + 0.85f * d * d);
			float y = proj_y(z) - 0.002f * us_rand(r);
			if (ctx->crisp)
				us_put(c, (int)(xf * A * c->s), (int)(y * c->s), 0.5f + 0.3f * us_rand(r), US_MAT_WATER);
			else
				us_point(c, xf * A, y, 0.0007f + 0.0006f * us_rand(r), 0.45f + 0.4f * us_rand(r), US_MAT_WATER);
		}
		if (back && wv->run_t > Tu + Td)
			wv->state = DONE;
	}

	/* ---- sandpipers working the wash line: they sprint up ahead of each uprush, chase the backwash down,
	 * probe the wet sand while it's still; a hard hit sometimes makes one flutter up and settle ---- */
	{
		int nbd = 2 + (int)(dens * (N_BIRDS - 2) + 0.5f);
		int startle = a->beat && a->onset > 0.7f;
		for (int i = 0; i < nbd; i++) {
			int col = (int)us_clampf(st->bd_x[i] * NX, 0, NX - 1);
			float zt = st->edge_z[col] * 0.972f; /* a stride up the beach from the water */
			float dz = zt - st->bd_z[i];
			float vmax = (dz < 0 ? 0.22f : 0.08f) * dt; /* flee the uprush, saunter after the backwash */
			float step = us_clampf(dz, -vmax, vmax);
			st->bd_z[i] = us_clampf(st->bd_z[i] + step, 0.62f, 1.3f);
			float spd = dt > 0 ? fabsf(step) / dt : 0.0f;
			st->bd_turn[i] -= dt;
			if (st->bd_turn[i] <= 0) {
				if (us_rand(r) < 0.5f)
					st->bd_dir[i] = -st->bd_dir[i];
				st->bd_turn[i] = us_randr(r, 2.0f, 7.0f);
			}
			int running = spd > 0.04f;
			st->bd_x[i] += st->bd_dir[i] * (running ? 0.02f : 0.006f) * dt;
			if (st->bd_x[i] < 0.27f)
				st->bd_dir[i] = 1.0f;
			if (st->bd_x[i] > 0.73f)
				st->bd_dir[i] = -1.0f;
			st->bd_leg[i] += dt * (running ? 34.0f : 9.0f);
			st->bd_peck[i] += dt;
			if (startle && us_rand(r) < 0.3f && st->bd_hop[i] < 0.1f)
				st->bd_hop[i] = 1.0f;
			st->bd_hop[i] = us_approach(st->bd_hop[i], 0.0f, 0.16f, ctx->real_dt);

			float z = st->bd_z[i], sc = KP / z;
			float L = fmaxf(0.04f * sc, 6.0f / c->s), D = st->bd_dir[i];
			float hb = st->bd_hop[i];
			float x = st->bd_x[i] * A, yg = proj_y(z), y = yg - hb * (1.0f - hb) * 4.0f * 2.2f * L;
			int peck = !running && hb < 0.05f && fmodf(st->bd_peck[i] + i * 0.7f, 1.6f) < 0.35f;
			float v = 0.6f + 0.15f * day;
			float sw = running ? sinf(st->bd_leg[i]) : 0.35f * sinf(st->bd_leg[i] * 0.2f);
			float seg[][4] = {
				{x - D * L * 0.65f, y - L * 0.5f, x - D * L * 0.1f, y - L * 0.68f},  /* tail -> back */
				{x - D * L * 0.1f, y - L * 0.68f, x + D * L * 0.32f, y - L * 0.6f}, /* back -> neck */
				{x + D * L * 0.32f, y - L * 0.6f, x + D * L * 0.28f, y - L * 0.38f},
				{x + D * L * 0.28f, y - L * 0.38f, x - D * L * 0.25f, y - L * 0.33f}, /* breast -> belly */
				{x - D * L * 0.25f, y - L * 0.33f, x - D * L * 0.65f, y - L * 0.5f},
				{x, y - L * 0.34f, x + sw * L * 0.3f, y},                           /* legs */
				{x, y - L * 0.34f, x - sw * L * 0.3f, y},
				{0, 0, 0, 0}, /* bill */
			};
			float hx, hy;
			if (peck) {
				hx = x + D * L * 0.5f, hy = y - L * 0.38f;
				seg[7][0] = hx, seg[7][1] = hy, seg[7][2] = hx + D * L * 0.12f, seg[7][3] = y - L * 0.02f;
			} else {
				hx = x + D * L * 0.42f, hy = y - L * 0.76f;
				seg[7][0] = hx, seg[7][1] = hy, seg[7][2] = hx + D * L * 0.42f, seg[7][3] = hy + L * 0.06f;
			}
			for (int k = 0; k < 8; k++) {
				if (ctx->crisp)
					us_line(c, seg[k][0], seg[k][1], seg[k][2], seg[k][3], 1.0f / c->s, v, US_MAT_ACCENT);
				else
					wu_line(c, seg[k][0], seg[k][1], seg[k][2], seg[k][3], k >= 5 ? v * 0.7f : v, US_MAT_ACCENT);
			}
			us_point(c, hx, hy, ctx->crisp ? 1.0f / c->s : L * 0.13f, v, US_MAT_ACCENT);
			if (hb > 0.08f) { /* wings beating while it's up */
				float fl = sinf(st->bd_leg[i] * 1.7f);
				for (int sd = -1; sd <= 1; sd += 2) {
					float wx = x - D * L * 0.15f + sd * L * 0.5f, wy = y - L * (0.9f + 0.35f * fl);
					if (ctx->crisp)
						us_line(c, x - D * L * 0.1f, y - L * 0.6f, wx, wy, 1.0f / c->s, v, US_MAT_ACCENT);
					else
						wu_line(c, x - D * L * 0.1f, y - L * 0.6f, wx, wy, v * 0.8f, US_MAT_ACCENT);
				}
			}
			/* a dim reflection on the wet sand under it */
			if (!ctx->crisp && st->wet_a[col] > 0.15f && hb < 0.3f) {
				float rv = 0.18f * st->wet_a[col];
				wu_line(c, x - D * L * 0.5f, yg + L * 0.5f, x + D * L * 0.3f, yg + L * 0.45f, rv, US_MAT_WATER);
				wu_line(c, x, yg + 0.001f, x, yg + L * 0.3f, rv * 0.7f, US_MAT_WATER);
			}
		}
	}

	/* ---- ghost crabs on the dry sand: each one listens to its own slice of the melody (a band in the
	 * low-mids/mids) and scuttles sideways while that note sounds; in silence they make the odd dart ---- */
	{
		int ncr = 1 + (int)(dens * (N_CRABS - 1) + 0.5f);
		for (int i = 0; i < ncr; i++) {
			float bnd = a->bands[9 + 3 * i];
			float tgt = us_sat((bnd - 0.35f) * 1.8f);
			st->cr_idle[i] -= dt;
			if (st->cr_idle[i] <= 0) {
				st->cr_idle[i] = us_randr(r, 3.0f, 9.0f);
				st->cr_e[i] = 0.9f;
				if (us_rand(r) < 0.4f)
					st->cr_dir[i] = -st->cr_dir[i];
			}
			st->cr_e[i] = us_approach(st->cr_e[i], tgt, 0.15f, ctx->real_dt);
			st->cr_x[i] += st->cr_dir[i] * st->cr_e[i] * 0.05f * dt;
			if (st->cr_x[i] < 0.26f)
				st->cr_dir[i] = 1.0f;
			if (st->cr_x[i] > 0.74f)
				st->cr_dir[i] = -1.0f;
			st->cr_leg[i] += dt * st->cr_e[i] * 45.0f;
			float z = st->cr_z[i];
			if (z > st->tide_s * 0.86f) /* the tide has its patch */
				continue;
			float sc = KP / z, R = fmaxf(0.011f * sc, 2.0f / c->s);
			float x = st->cr_x[i] * A, y = proj_y(z);
			float v = (0.28f + 0.2f * night + 0.1f * day) * (0.8f + 0.25f * st->cr_e[i]);
			if (ctx->crisp) {
				us_line(c, x - R, y - R * 0.5f, x + R, y - R * 0.5f, 1.0f / c->s, v, US_MAT_ACCENT);
				float lg = (sinf(st->cr_leg[i]) > 0) ? 1.0f / c->s : 0.0f;
				us_put(c, (int)((x - R * 1.6f) * c->s), (int)((y + lg) * c->s), v * 0.8f, US_MAT_ACCENT);
				us_put(c, (int)((x + R * 1.6f) * c->s), (int)((y - lg + 1.0f / c->s) * c->s), v * 0.8f, US_MAT_ACCENT);
				continue;
			}
			/* carapace: a low dome */
			float qx = 0, qy = 0;
			for (int k = 0; k <= 8; k++) {
				float an = US_PI * k / 8.0f;
				float px = x - cosf(an) * R, py = y - R * 0.35f - sinf(an) * R * 0.55f;
				if (k)
					wu_line(c, qx, qy, px, py, v, US_MAT_ACCENT);
				qx = px, qy = py;
			}
			wu_line(c, x - R, y - R * 0.35f, x + R, y - R * 0.35f, v * 0.6f, US_MAT_ACCENT);
			/* legs: three a side, stepping in alternation */
			for (int sd = -1; sd <= 1; sd += 2)
				for (int j = 0; j < 3; j++) {
					float ph = sinf(st->cr_leg[i] + j * 2.1f + (sd > 0 ? US_PI : 0));
					float kx = x + sd * R * (0.6f + 0.15f * j), ky = y - R * 0.4f;
					float fx = x + sd * R * (1.25f + 0.25f * j + 0.15f * ph), fy = y + R * 0.15f * (j - 1) * 0.5f + R * 0.1f;
					float mx = (kx + fx) * 0.5f + sd * R * 0.1f, my = ky - R * 0.35f;
					wu_line(c, kx, ky, mx, my, v * 0.7f, US_MAT_ACCENT);
					wu_line(c, mx, my, fx, fy, v * 0.7f, US_MAT_ACCENT);
				}
			/* eye stalks */
			wu_line(c, x - R * 0.3f, y - R * 0.85f, x - R * 0.35f, y - R * 1.3f, v, US_MAT_ACCENT);
			wu_line(c, x + R * 0.3f, y - R * 0.85f, x + R * 0.35f, y - R * 1.3f, v, US_MAT_ACCENT);
		}
	}

	/* ---- spray ---- */
	for (int i = 0; i < st->nsp;) {
		spray_t *s = &st->sp[i];
		s->vy += s->g * dt;
		s->x += s->vx * dt;
		s->y += s->vy * dt;
		s->life -= dt;
		if (s->life <= 0) {
			st->sp[i] = st->sp[--st->nsp];
			continue;
		}
		us_point(c, s->x, s->y, ctx->crisp ? 0.0016f : 0.001f, 0.8f * us_sat(s->life * 2.0f), US_MAT_WATER);
		i++;
	}

	/* ---- marram grass on the dunes framing the foreground corners, combed by the wind: stereo width is
	 * the breeze across the dunes (a wide mix bends them over), highs add flutter ---- */
	{
		int nd = (int)(N_DUNE * (0.5f + 0.5f * dens));
		float wnd = 0.15f + 0.2f * st->hi_env + 0.75f * st->width_env + 0.3f * w->wind;
		for (int i = 0; i < nd; i++) {
			float x = st->du_x[i] * A, H = 0.2f * st->du_h[i];
			if (H < 0.03f)
				continue;
			float gust = us_noise2(x * 2.5f - st->wind_t * 0.6f, st->du_p[i]);
			float bend = (0.25f + 0.5f * wnd) * (0.6f + 0.6f * gust) * ((i & 2) ? 1.0f : 0.8f);
			float lean = (st->du_x[i] < 0.5f ? 0.15f : -0.15f) + 0.1f * sinf(st->du_p[i]);
			float pts[2 * 7];
			for (int k = 0; k <= 6; k++) {
				float f = k / 6.0f;
				pts[2 * k] = x + (lean + bend) * H * f * f;
				pts[2 * k + 1] = 1.01f - H * f * (1.0f - 0.25f * bend * f);
			}
			float v = 0.3f + 0.25f * st->du_h[i];
			uint8_t m = (i % 5 == 0) ? US_MAT_STEM : US_MAT_LEAF;
			if (ctx->crisp)
				us_polyline(c, pts, 7, 0.001f, v, m);
			else
				for (int k = 0; k < 6; k++)
					wu_line(c, pts[2 * k], pts[2 * k + 1], pts[2 * k + 2], pts[2 * k + 3], v * (1.0f - 0.25f * k / 6.0f), m);
		}
	}
}

const us_scene_vt us_scene_tide = {"tide", "Tide", 0.12f, create, destroy, frame};
