/* Understory scene: Tide — a moonlit shoreline at a low angle.
 *
 * Composition: a wide beach in the foreground, a calm open sea above it, a headland with a red-and-white
 * lighthouse across the bay, the moon and its glitter path. Under the lines lies a ground-plane tone (the sea
 * glowing toward the horizon and under the moon, dark wet sand holding the moon, pale dry sand sinking into
 * shadow toward the viewer) — two coarse fields in Ink, solid areas through an ordered dither in Pixel.
 * The sea is a height field drawn near -> far with hidden-line removal: a few perspective swell lines
 * (denser toward the horizon, broken into long strokes) and the waves themselves, so incoming swells rise
 * as 3D humps that hide the water behind them. Perspective: y = HORIZON + K/z, a height h lifts by K*h/z.
 * Each wave is born far out as a low hump, grows and steepens as it shoals (speed ~ depth, so it slows
 * near shore): a moonlit crest over a face hatched in fine vertical strokes. It breaks (bigger waves break
 * farther out) and the break peels along the crest from one point, a lip curling over at the peel; behind
 * it a wall of whitewater billows and tumbles down the face, foam trailing on the water it passed. Then it
 * runs up the sand as a lacy swash front, stalls, slides back and leaves a wet band that slowly dries.
 * At night the moon (phase from the world's season clock, position from the time of day) lays a twinkling
 * glitter path; by day the sun.
 * Above: the Milky Way, a haloed moon, banks of cumulus lit on the moon's side, horizon haze. On the beach:
 * long ripple marks catching the moonlight, shells and pebbles, a driftwood log, and marram grass on the
 * dunes framing the foreground corners, combed by the wind. Small lives: a rock outcrop at the waterline with
 * rock pools at its foot, the lighthouse sweeping its beam, sandpipers sprinting ahead of each
 * uprush and probing the wet sand behind the backwash, ghost crabs scuttling sideways on the dry sand.
 *
 * Audio: each drum has its own answer. Kick -> a wave breaks on the shore: the water just off the beach heaves
 *        and lights up, the wet sand flashes, and a tongue of whitewater races up the sand (a harder kick
 *        breaks along more of the beach and runs farther), stalls and soaks in, wetting the sand. Snare -> the shore break bursts white and throws spray
 *        plumes, a breaker bursts on the rocks (spray fan, drips ringing the rock pools), the lighthouse lamp
 *        flares and its beam sweeps a pale swath across the swells. Hats -> the moon path glitters (glints
 *        re-roll and flare) and foam fizzes along the water's edge. Loudness lights the water (louder = brighter
 *        sea, foam and moon path; quiet sinks to a dim flat calm with small lapping) and stirs the chop; phrase
 *        loudness + a slow ~50 s set rhythm -> wave size, how often they come, how far up the beach the tide
 *        sits. Sub lifts the long ground swell; mids make the moon path shimmer and widen; highs -> foam +
 *        spindrift; air -> the swash sheet fizzes; single bands set individual ghost crabs scuttling; stereo
 *        width is the wind in the marram; centroid clears the sky (moon halo, Milky Way). Silence = small lapping
 *        waves still rolling in, the beam still turning, birds still working the wash line, crabs darting.
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
#define FLW 128 /* ground-plane tone grid (ink) */
#define FLH 72
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
#define Z_HD 7.0f  /* the headland's foot, out across the bay */
#define HD_W 0.26f /* its width (screen fraction) */
#define HD_H 0.085f

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
	float pd, xp0;        /* the break peels along the crest: direction (+-1) and where it starts (screen fraction) */
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

/* drum answers: a kick surges whitewater up the beach, a snare throws spray plumes off the shore break */
#define MAX_SURGE 6
#define MAX_PLUME 4
typedef struct {
	int on;
	float age, k, seed;
} burst_t;

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
	float fsea[FLW * FLH], fsand[FLW * FLH];               /* ground-plane tone + material (ink) */
	float cl_x[N_CLOUDS], cl_y[N_CLOUDS], cl_w[N_CLOUDS], cl_s[N_CLOUDS];
	float sh_x[N_SHELLS], sh_z[N_SHELLS], sh_a[N_SHELLS], sh_k[N_SHELLS];
	float du_x[N_DUNE], du_h[N_DUNE], du_p[N_DUNE];
	float mw_a, mw_b, wind_t, log_x, log_z, log_a;
	/* eased audio envelopes, one per musical meaning */
	float sub_env, mid_env, air_env, width_env, cent_env, gs_t, gl_t;
	float crash, crash_k, crash_age, crash_n;
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
	burst_t sg[MAX_SURGE], pl[MAX_PLUME];
	float lvl_env, lvl_slow, hat_env, snare_env, kick_env, heave, beam_sw, beam_x, beam_v;
	uint32_t hat_seed;
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

/* overwrite a pixel (pixel styles: solid silhouettes must hide the tone painted under them) */
static inline void put_set(us_canvas *c, int px, int py, float v, uint8_t mat)
{
	if ((unsigned)px >= (unsigned)c->w || (unsigned)py >= (unsigned)c->h)
		return;
	size_t i = (size_t)py * c->w + px;
	c->ink[i] = (uint8_t)(us_sat(v) * 255.0f + 0.5f);
	c->mat[i] = mat;
}

/* pixel art: ordered-dither a continuous shade (0..4 palette steps) onto the 4 ramp steps */
static inline float pix_step(float s, int px, int py)
{
	static const float rep[5] = {0.0f, 0.17f, 0.4f, 0.62f, 0.9f};
	int k = (int)(s + bayer4(px, py));
	return rep[k < 0 ? 0 : k > 4 ? 4 : k];
}
/* the same for broad areas: mostly solid bands, dithering only across narrow transitions */
static inline float pix_fill(float s, int px, int py)
{
	float k = floorf(s), f = s - k;
	return pix_step(k + us_smooth(0.35f, 0.65f, f), px, py);
}

/* the far headland, out across the bay on the side opposite the rock: its top y at screen fraction xf
 * (2 where there is no headland). u runs from the cliff at its seaward tip to the frame edge. */
static float hd_top(const st_t *st, float xf)
{
	float u = st->rk_side ? (HD_W - xf) / HD_W : (xf - (1.0f - HD_W)) / HD_W;
	if (u <= 0.0f)
		return 2.0f;
	float ft = us_sat(u) * RKN;
	int i0 = (int)ft < RKN ? (int)ft : RKN - 1;
	return proj_y(Z_HD) - HD_H * us_lerp(st->hd_prof[i0], st->hd_prof[i0 + 1], ft - i0);
}
static inline float lh_xf(const st_t *st) { return st->rk_side ? HD_W * 0.8f : 1.0f - HD_W * 0.8f; }

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
	float h = (0.0035f + 0.004f * level) * fsin(z * 2.2f - st->chop_t * 2.0f + 1.1f * fsin(xf * 7.3f + z * 0.41f) + 0.7f * fsin(xf * 17.9f - z * 0.27f));
	h += (0.004f + 0.008f * level) * us_noise2(X * 0.7f + st->chop_t * 0.13f, z * 2.2f - st->chop_t * 1.5f);
	/* ground swell: long, slow undulations from far out; the sub-bass lifts them */
	h += (0.004f + 0.012f * st->sub_env) * fsin(z * 0.9f - st->gs_t + 0.6f * fsin(xf * 2.3f + z * 0.13f));
	/* a kick heaves the water just off the beach: the swell lifting under the break */
	if (st->heave > 1e-4f) {
		float u = (z - st->tide_s * 1.3f) * 2.2f;
		h += st->heave * expf(-u * u);
	}
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
	float a = 0.3f + 0.4f * st->swell_env + 0.4f * w->storm_charge + 0.2f * sets * sets + 0.12f * st->bass_env;
	wv->amp = us_clampf(a * us_randr(r, 0.8f, 1.15f), 0.12f, 1.0f);
	wv->z = Z_SPAWN;
	wv->seed = us_rand(r) * 100.0f;
	wv->state = ROLL;
	wv->zb = st->tide_s * (1.15f + 1.1f * wv->amp);
	wv->run_max = 0.08f + 0.22f * wv->amp;
	wv->pd = us_rand(r) < 0.5f ? -1.0f : 1.0f;
	wv->xp0 = us_randr(r, 0.3f, 0.7f) - 0.15f * wv->pd;
}

static void init_scene(st_t *st, const us_ctx *ctx)
{
	us_rng *r = &st->rng;
	st->tide_s = 1.6f - 0.35f * ctx->w->water_level;
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
	st->rk_w = us_randr(r, 0.12f, 0.16f);
	st->rk_x = st->rk_side ? us_randr(r, 0.8f, 0.86f) : us_randr(r, 0.14f, 0.2f);
	st->rk_h = us_randr(r, 0.2f, 0.26f);
	st->rk_z = (1.6f - 0.35f * ctx->w->water_level) * us_randr(r, 1.06f, 1.1f); /* in the shallows */
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
			/* headland: a sheer cliff at its seaward tip, then a rolling top climbing toward the frame edge */
			float hu = us_smooth(0.0f, 0.06f, u);
			st->hd_prof[k] = hu * (0.42f + 0.58f * us_smooth(0.12f, 0.75f, u) + 0.06f * us_noise2(u * 5.0f, 9.3f)) + 0.03f * us_noise2(u * 19.0f, 1.7f) * hu;
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

/* ground-plane tone at (x, y) below the horizon on an ink scale (~0..0.25), 0 where an object stands;
 * *m gets the material. pl = how lit the water is (loudness). */
static float plane_tone(const st_t *st, const float *ysh, const float *ywt, const float *wab, float x, float y, float A, float bx, float bv,
			float pl, uint8_t *m)
{
	float xf = x / A, fc = us_clampf(xf * NX - 0.5f, 0.0f, NX - 1.001f);
	int col = (int)fc, c1 = col + 1 < NX ? col + 1 : col;
	float ct = fc - col;
	float ys = us_lerp(ysh[col], ysh[c1], ct), yw = us_lerp(ywt[col], ywt[c1], ct), wa = us_lerp(wab[col], wab[c1], ct);
	*m = US_MAT_EARTH;
	if (y <= HOR || (y < proj_y(Z_HD) && y > hd_top(st, xf)))
		return 0.0f;
	if (rock_prof(st, xf) > 0.0f && y > rock_top(st, xf) && y < rock_base(st))
		return 0.0f;
	float dm = bx >= 0.0f ? x - bx : 1e3f;
	if (y < ys) {
		/* the sea: brighter toward the horizon (the sky in it), a broad soft column under the moon */
		float d = us_sat((y - HOR) / (ys - HOR));
		float v = (0.045f + 0.06f * (1.0f - d) * (1.0f - d)) * pl + 0.06f * st->lvl_slow;
		float gw = 0.012f + 0.3f * (y - HOR);
		v += 0.13f * bv * expf(-dm * dm / (gw * gw)) * pl;
		/* a kick: the shore break lights the water just off the beach */
		v += 0.2f * st->kick_env * us_smooth(0.5f, 1.0f, d);
		/* a snare: the lighthouse beam sweeps a pale swath across the water */
		if (st->beam_v > 0.02f) {
			float bw = 0.05f + 0.5f * (y - HOR), dx = (x - st->beam_x) / bw;
			v += 0.2f * st->beam_v * expf(-dx * dx);
		}
		*m = US_MAT_WATER;
		return v;
	}
	float sd = us_sat((y - ys) / (1.02f - ys));
	if (y < yw) {
		/* wet sand: dark glass with the moon in it */
		float gw = 0.05f + 0.12f * sd;
		*m = US_MAT_WATER;
		return 0.04f + 0.14f * bv * expf(-dm * dm / (gw * gw)) * wa * pl + 0.12f * st->kick_env;
	}
	/* dry sand: pale under the moon near the water, sinking into shadow toward the viewer */
	return (0.1f - 0.05f * sd + 0.02f * bv) * (0.55f + 0.5f * pl) + 0.03f * st->lvl_slow;
}

/* one wave, drawn when the near->far sea march reaches its crest (so nearer water and waves hide it and it
 * hides what lies behind). Unbroken: a moonlit crest line over a glassy face. Breaking: the break peels
 * along the crest from where it began, a lip curling over at the peel point; behind the peel a wall of
 * whitewater tumbles down the face, and foam trails on the water it has passed. */
static void draw_wave(st_t *st, const us_ctx *ctx, us_canvas *c, int wi, int ns, float lk, float bx, float bv)
{
	const wave_t *wv = &st->wv[wi];
	float A = ctx->aspect, t = st->t;
	int crisp = ctx->crisp;
	float yt[NS + 1], yb[NS + 1], sv[NS + 1], br[NS + 1], fp[NS + 1], ocv[NS + 1], hsa[NS + 1];
	int brk = wv->state == BREAK;
	float xp = brk ? wv->xp0 + wv->pd * (0.05f + 0.1f * wv->amp) * wv->brk_age : -9.0f;
	float wf = brk ? us_sat(wv->foam * 1.2f) : 0.0f;
	float pl = 0.5f + 0.6f * lk;
	for (int i = 0; i <= ns; i++) {
		float xf = (float)i / ns, ft = xf * TW;
		int k0 = (int)ft < TW ? (int)ft : TW - 1;
		float zc = us_lerp(st->cz_tab[wi][k0], st->cz_tab[wi][k0 + 1], ft - k0);
		float al = us_lerp(st->al_tab[wi][k0], st->al_tab[wi][k0 + 1], ft - k0);
		float X = (xf - 0.5f) * A * zc / KP;
		float zf = fmaxf(zc - 0.8f * (0.2f + 0.16f * zc), 0.3f);
		float Xf = (xf - 0.5f) * A * zf / KP;
		yt[i] = proj_y(zc) - KP * sea_h(st, xf, zc, X, st->lvl_env) / zc;
		yb[i] = proj_y(zf) - KP * sea_h(st, xf, zf, Xf, st->lvl_env) / zf;
		float hs = KP * wv->hf * al / zc; /* how tall this stretch of crest stands on screen */
		hsa[i] = hs;
		sv[i] = us_smooth(0.0015f, crisp ? 0.016f : 0.012f, hs);
		br[i] = brk ? us_smooth(-0.02f, 0.02f, wv->pd * (xp - xf)) * (0.3f + 0.7f * us_smooth(0.1f, 0.5f, al)) : 0.0f;
		/* whitewater piles up above the old crest line */
		fp[i] = br[i] * KP * wv->amp * 0.5f * (0.4f + 0.6f * al) / zc * (0.25f + 0.2f * fabsf(us_noise2(X * 4.0f + wv->seed, t * 0.4f)));
		ocv[i] = st->occ[i];
	}
	if (crisp) {
		float sc = c->s;
		for (int px = 0; px < c->w; px++) {
			float xf = (px + 0.5f) / sc / A;
			int i = (int)(xf * ns + 0.5f);
			if (i > ns)
				i = ns;
			float w = br[i] * wf, s = sv[i] * (1.0f - br[i]);
			if (s < 0.08f && w < 0.05f)
				continue;
			int top = (int)((yt[i] - fp[i]) * sc), bot = (int)(yb[i] * sc), oc = (int)(ocv[i] * sc);
			float x = (px + 0.5f) / sc;
			float rim = bx >= 0.0f ? bv * expf(-(x - bx) * (x - bx) / (0.09f * A * A)) : 0.0f;
			for (int py = top; py <= bot && py < oc; py++) {
				float f = bot > top ? (float)(py - top) / (bot - top) : 0.0f;
				if (w > 0.05f) {
					/* whitewater: solid at the top, breaking into dither down the face */
					float n = st->ntile[((py * 2) & (NT - 1)) * NT + ((px + (int)(t * 6.0f)) & (NT - 1))];
					us_put(c, px, py, pix_step(w * pl * (4.2f - 2.6f * f + 1.2f * n), px, py), US_MAT_WATER);
				} else if (py == top) {
					us_put(c, px, py, pix_fill(s * pl * (2.8f + 2.0f * rim), px, py), rim > 0.4f ? US_MAT_LIGHT : US_MAT_WATER);
				} else {
					/* the glassy face: lit through the lip, darkening into the trough */
					us_put(c, px, py, pix_fill(s * pl * (2.3f - 1.1f * f), px, py), US_MAT_WATER);
				}
			}
		}
	} else {
		/* unbroken stretches: the crest line, the lip just under it, and the steep face hatched in fine
		 * vertical strokes (an engraver's wall of water, against the horizontal sea) */
		for (int j = 0; j < 2; j++) {
			float px = 0, py = 0, pv = 0;
			int pvis = 0;
			for (int i = 0; i <= ns; i++) {
				float x = (float)i / ns * A;
				float y = yt[i] + (yb[i] - yt[i]) * (j ? 0.14f : 0.0f);
				int vis = y < ocv[i];
				float s = sv[i] * (1.0f - br[i]);
				float rim = bx >= 0.0f ? bv * expf(-(x - bx) * (x - bx) / (0.09f * A * A)) : 0.0f;
				float v = j == 0 ? (0.2f + 0.6f * s + 0.4f * rim * s) * s * pl : 0.3f * s * pl;
				if (i && vis && pvis && (v > 0.01f || pv > 0.01f))
					wu_line(c, px, py, x, y, 0.5f * (v + pv), j == 0 && rim > 0.45f && s > 0.4f ? US_MAT_LIGHT : US_MAT_WATER);
				px = x, py = y, pv = v, pvis = vis;
			}
		}
		{
			const int NH = 80;
			for (int q = 0; q < NH; q++) {
				float hq = hash1(q * 0.613f + wv->seed * 0.17f), hl = hash1(hq * 37.1f + 0.3f);
				float xf = (q + 0.1f + 0.8f * hq) / NH;
				int i = (int)(xf * ns + 0.5f);
				float s = sv[i] * (1.0f - br[i]) * us_smooth(0.3f, 0.8f, wv->steep);
				if (s < 0.2f || hl < 0.35f || yt[i] >= ocv[i])
					continue;
				if (hsa[i] < 0.008f)
					continue;
				float face = fminf(yb[i] - yt[i], 1.5f * hsa[i]);
				float y0 = yt[i] + face * (0.04f + 0.1f * hq), y1 = yt[i] + face * (0.3f + 0.7f * hl * hl);
				if (y1 >= ocv[i])
					y1 = ocv[i];
				float x = xf * A;
				wu_line(c, x, y0, x - wv->pd * face * 0.06f, y1, 0.26f * s * pl, US_MAT_WATER);
			}
		}
		/* broken stretches: whitewater as billows (rows of rounded bumps), streaks of foam falling down the
		 * face, and a broken skirt of foam spreading in front */
		if (brk) {
			for (int row = 0; row < 2; row++) {
				float xa = 0.0f;
				for (int q = 0; xa < A && q < 400; q++) {
					float hq = hash1(q * 0.731f + row * 3.3f + wv->seed * 0.29f);
					float wb = A * (row ? 0.009f + 0.012f * hq : 0.012f + 0.018f * hq);
					float xb = xa + wb, xm = 0.5f * (xa + xb);
					int im = (int)us_clampf(xm / A * ns + 0.5f, 0, ns);
					float w = br[im] * wf;
					if (w > 0.05f && !(row && hq < 0.3f)) {
						float face = yb[im] - yt[im];
						float bh = (row ? 0.25f : 0.45f) * (0.5f + 0.5f * hq) * fmaxf(fp[im] * 2.0f, 0.003f) *
							   (0.85f + 0.15f * us_noise2(xm * 9.0f, t * 0.8f + row));
						float v = (row ? 0.5f : 0.95f) * w * pl;
						float qx = 0, qy = 0;
						int qvis = 0;
						for (int k = 0; k <= 6; k++) {
							float u = k / 6.0f, x = xa + wb * u;
							int i = (int)us_clampf(x / A * ns + 0.5f, 0, ns);
							float y = yt[i] - fp[i] * (row ? 0.0f : 0.5f) + (row ? face * 0.42f : 0.0f) - bh * sinf(US_PI * u);
							int vis = y < ocv[i];
							if (k && vis && qvis)
								wu_line(c, qx, qy, x, y, v, US_MAT_WATER);
							qx = x, qy = y, qvis = vis;
						}
						/* foam falling down the face below this billow */
						if (!row && hq > 0.7f && face > 0.012f && fp[im] > 0.003f && yt[im] < ocv[im]) {
							float y0 = yt[im] + face * 0.05f, y1 = yt[im] + face * (0.4f + 1.6f * (hq - 0.7f));
							float mx = xm + wv->pd * face * 0.04f, my = 0.5f * (y0 + y1);
							wu_line(c, xm, y0, mx, my, 0.45f * w * pl, US_MAT_WATER);
							wu_line(c, mx, my, xm + wv->pd * face * 0.05f, fminf(y1, ocv[im]), 0.3f * w * pl, US_MAT_WATER);
						}
					}
					xa = xb;
				}
			}
			float px = 0, py = 0, pv = 0;
			int pvis = 0;
			for (int i = 0; i <= ns; i++) {
				float x = (float)i / ns * A;
				float n = us_noise2(x * 21.0f + wv->seed, t * 0.35f);
				float y = yb[i] + 0.002f + 0.003f * n;
				float v = 0.4f * br[i] * wf * us_smooth(-0.1f, 0.2f, n) * pl;
				int vis = y < ocv[i];
				if (i && vis && pvis && (v > 0.02f || pv > 0.02f))
					wu_line(c, px, py, x, y, 0.5f * (v + pv), US_MAT_WATER);
				px = x, py = y, pv = v, pvis = vis;
			}
		}
	}
	/* the curl at the peel point: the lip pitching over, seen side-on as a hook into the barrel */
	if (brk && xp > 0.01f && xp < 0.99f) {
		int i = (int)(xp * ns + 0.5f);
		float R0 = 0.6f * fminf(yb[i] - yt[i], 1.5f * hsa[i]) * us_sat(1.2f - wv->brk_age / 5.0f);
		if (R0 > 0.008f && yt[i] < ocv[i]) {
			float cx = xp * A, cy = yt[i] + R0;
			for (int ring = 0; ring < (crisp ? 1 : 2); ring++) {
				float qx = 0, qy = 0;
				const int NQ = 22;
				for (int q = 0; q <= NQ; q++) {
					float th = 1.75f * US_PI * q / NQ;
					float R = R0 * (1.0f - 0.55f * th / (1.75f * US_PI)) * (ring ? 0.82f : 1.0f);
					float x = cx + wv->pd * R * sinf(th), y = cy - R * cosf(th) + (ring ? R0 * 0.12f : 0.0f);
					float v = (0.95f - 0.4f * q / NQ) * pl * (ring ? 0.55f : 1.0f);
					if (q) {
						if (crisp)
							us_line(c, qx, qy, x, y, 1.0f / c->s, us_sat(v + 0.2f), US_MAT_WATER);
						else
							wu_line(c, qx, qy, x, y, v, US_MAT_WATER);
					}
					qx = x, qy = y;
				}
			}
		}
	}
	/* this wave now hides what lies behind it */
	for (int i = 0; i <= ns; i++)
		if (yt[i] - fp[i] < st->occ[i])
			st->occ[i] = yt[i] - fp[i];
	/* foam left on the water the break has passed over: broken dotted bands trailing behind */
	if (brk) {
		for (int j = 0; j < 3; j++) {
			float px = 0, py = 0;
			int pvis = 0;
			for (int i = 0; i <= ns; i++) {
				float xf = (float)i / ns, ft = xf * TW;
				int k0 = (int)ft < TW ? (int)ft : TW - 1;
				float zc = us_lerp(st->cz_tab[wi][k0], st->cz_tab[wi][k0 + 1], ft - k0);
				float z = zc * (1.12f + (0.08f + 0.06f * wv->amp) * j * (1.0f + 0.4f * us_sat(wv->brk_age * 0.3f)));
				float X = (xf - 0.5f) * A * z / KP;
				float x = xf * A, y = proj_y(z) - KP * sea_h(st, xf, z, X, st->lvl_env) / z;
				float n = us_noise2(X * 2.2f + wv->seed + j * 5.0f, z * 3.0f);
				float v = (0.42f - 0.1f * j) * wf * br[i] * pl * us_smooth(0.0f, 0.25f, n);
				int vis = y < st->occ[i];
				if (i && vis && pvis && v > 0.02f) {
					if (crisp)
						us_line(c, px, py, x, y, 1.0f / c->s, us_sat(v + 0.15f), US_MAT_WATER);
					else
						wu_line(c, px, py, x, y, v, US_MAT_WATER);
				}
				px = x, py = y, pvis = vis;
			}
		}
	}
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *st = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	us_rng *r = &st->rng;
	float dt = ctx->dt, A = ctx->aspect, dens = ctx->p->density;
	if (!st->inited) {
		/* start from the music already playing (no slow ramp-up of the surf and the light) */
		st->swell_env = us_sat(0.45f * a->swell + 0.6f * a->level);
		st->lvl_slow = st->lvl_env = us_sat(a->level);
		init_scene(st, ctx);
	}
	st->t += dt;
	float t = st->t;

	st->bass_env = us_approach(st->bass_env, us_sat(a->bass * 0.8f + a->kick * 0.5f), 0.4f, ctx->real_dt);
	st->hi_env = us_approach(st->hi_env, us_sat(a->high + 0.7f * a->air), 0.3f, ctx->real_dt);
	/* phrase loudness: the analyzer's slow swell blended with a smoothed level */
	st->swell_env = us_approach(st->swell_env, us_sat(0.45f * a->swell + 0.6f * a->level), 3.0f, ctx->real_dt);
	/* loudness: the moonlit water brightens and the chop stirs with it; quiet = flat calm */
	st->lvl_env = us_approach(st->lvl_env, us_sat(a->level * 1.15f), 0.35f, ctx->real_dt);
	st->lvl_slow = us_approach(st->lvl_slow, us_sat(a->level), 0.7f, ctx->real_dt);
	st->chop_t += dt * (0.1f + 0.2f * st->lvl_env + 0.15f * w->wind);
	/* one envelope per musical meaning: sub = ground swell, mids = the moon path, air = fizz,
	 * width = wind in the marram, centroid = how clear the sky is */
	float rdt = ctx->real_dt;
	st->sub_env = us_approach(st->sub_env, us_sat(a->sub * 1.1f), 2.0f, rdt);
	st->mid_env = us_approach(st->mid_env, us_sat(a->mid * 0.7f + a->lowmid * 0.4f), 0.25f, rdt);
	st->air_env = us_approach(st->air_env, us_sat(a->air * 0.8f + a->high * 0.4f), 0.12f, rdt);
	st->width_env = us_approach(st->width_env, us_sat(a->width * 1.3f), 1.2f, rdt);
	st->cent_env = us_approach(st->cent_env, us_sat(a->centroid), 2.5f, rdt);
	st->gs_t += dt * (0.55f + 0.35f * st->sub_env);
	/* tide: world water level + a slow tidal breath; loud phrases push the sea up the beach */
	float tide_target = 1.6f - 0.35f * w->water_level + 0.05f * sinf(t * US_TAU / 200.0f) - 0.08f * st->swell_env;
	st->tide_s = us_approach(st->tide_s, tide_target, 10.0f, dt);
	/* kick: a wave breaks on the shore and its whitewater surges up the sand (bigger kick, farther run) */
	if (a->kick_hit) {
		burst_t *b = &st->sg[0];
		for (int i = 0; i < MAX_SURGE; i++) {
			if (!st->sg[i].on) {
				b = &st->sg[i];
				break;
			}
			if (st->sg[i].age > b->age)
				b = &st->sg[i];
		}
		b->on = 1;
		b->age = 0.0f;
		b->k = us_sat(0.3f + 0.8f * a->kick);
		b->seed = us_rand(r) * 50.0f;
	}
	/* snare: spray bursts off the rocks and plumes off the shore break; the lighthouse lamp flares */
	if (a->snare_hit) {
		float k = us_sat(0.35f + 0.75f * a->snare);
		burst_t *b = &st->pl[0];
		for (int i = 0; i < MAX_PLUME; i++) {
			if (!st->pl[i].on) {
				b = &st->pl[i];
				break;
			}
			if (st->pl[i].age > b->age)
				b = &st->pl[i];
		}
		b->on = 1;
		b->age = 0.0f;
		b->k = k;
		b->seed = us_rand(r) * 50.0f;
		st->crash = fmaxf(st->crash, k);
		st->crash_k = k;
		st->crash_age = 0.0f;
		st->crash_n += 1.0f;
		st->crash_spawn = 8 + (int)(20.0f * k);
		st->pool_t[0] = st->pool_t[1] = -0.3f; /* the spray lands in the rock pools a moment later */
		st->beam_sw = 0.0f; /* the lighthouse beam swings down across the water */
	}
	st->beam_sw = fminf(st->beam_sw + rdt / 0.45f, 2.0f);
	/* hats: the moon path glitters (glints re-roll and flare), foam fizzes */
	if (a->hat_hit) {
		st->gl_t = floorf(st->gl_t) + 1.0f;
		st->hat_seed = us_rng_u32(r);
	}
	st->hat_env = fmaxf(a->hat, us_approach(st->hat_env, 0.0f, 0.08f, rdt));
	st->snare_env = fmaxf(a->snare, us_approach(st->snare_env, 0.0f, 0.1f, rdt));
	{
		float tx = lh_xf(st) * ctx->aspect, e = us_sat(st->beam_sw);
		e = 1.0f - (1.0f - e) * (1.0f - e);
		st->beam_x = us_lerp(tx, ctx->aspect - tx, 0.15f + 0.7f * e);
		st->beam_v = st->snare_env * (0.5f + 0.5f * us_sat(1.0f - ctx->w->daylight * 2.2f));
	}
	/* the heave: lifts on the kick and settles quickly */
	st->kick_env = a->kick_hit ? fmaxf(st->kick_env, us_sat(0.4f + 0.7f * a->kick)) : st->kick_env * expf(-rdt / 0.06f);
	st->heave = (ctx->crisp ? 0.06f : 0.05f) * st->kick_env;
	for (int i = 0; i < MAX_SURGE; i++)
		if (st->sg[i].on && (st->sg[i].age += rdt) > 2.6f)
			st->sg[i].on = 0;
	for (int i = 0; i < MAX_PLUME; i++)
		if (st->pl[i].on && (st->pl[i].age += rdt) > 1.0f)
			st->pl[i].on = 0;
	st->crash = us_approach(st->crash, 0.0f, 0.3f, rdt);
	st->crash_age += rdt;
	st->pool_t[0] += rdt;
	st->pool_t[1] += rdt * 0.8f;

	float day = w->daylight, night = us_sat(1.0f - day * 2.2f);
	float storm = w->storm_charge;
	float foam_k = 0.35f + 0.65f * st->hi_env;
	float lk = 0.15f + 1.0f * st->lvl_slow; /* loudness lights the water: louder = brighter sea and foam */

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
			float k = 0.1f * (0.85f + 0.2f * st->swell_env);
			wv->z -= wv->z * k * dt;
			float shoal = us_smooth(Z_SPAWN, wv->zb, wv->z);
			wv->steep = shoal * shoal;
			float grow = us_sat(wv->age / 3.0f); /* rises out of the sea, no pop */
			if (wv->state == ROLL) {
				wv->hf = wv->amp * 1.1f * (0.2f + 0.8f * shoal * shoal) * grow;
				if (wv->z < wv->zb) {
					wv->state = BREAK;
					wv->foam = 1.0f;
					wv->brk_age = 0;
				}
			} else {
				wv->brk_age += dt;
				wv->hf = wv->amp * 1.1f * expf(-wv->brk_age * 0.5f);
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
			float mr = 0.042f;
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
	/* ---- the ground plane: a tone under the lines. The sea glows toward the horizon and under the moon,
	 * wet sand is dark and holds the moon's reflection, dry sand is pale. Ink: two coarse fields; pixel
	 * art: solid areas through an ordered dither. ---- */
	{
		/* the wet band, smoothed across columns so its edge is a soft wash line, not a comb */
		float ysh[NX], ywt[NX], wd[NX], wab[NX];
		for (int i = 0; i < NX; i++) {
			float xf = (i + 0.5f) / NX, zs = shore_z(st, xf);
			ysh[i] = proj_y(zs);
			int wet = st->wet_a[i] > 0.02f && st->wet_z[i] < zs;
			wd[i] = wet ? (proj_y(st->wet_z[i]) - ysh[i]) * us_sat(st->wet_a[i] * 3.0f) : 0.0f;
			wab[i] = wet ? st->wet_a[i] : 0.0f;
		}
		for (int pass = 0; pass < 3; pass++) {
			float pd_ = wd[0], pa = wab[0];
			for (int i = 0; i < NX; i++) {
				float nd = i + 1 < NX ? wd[i + 1] : wd[i], na = i + 1 < NX ? wab[i + 1] : wab[i];
				float cd = wd[i], ca = wab[i];
				wd[i] = 0.25f * pd_ + 0.5f * cd + 0.25f * nd;
				wab[i] = 0.25f * pa + 0.5f * ca + 0.25f * na;
				pd_ = cd, pa = ca;
			}
		}
		for (int i = 0; i < NX; i++)
			ywt[i] = ysh[i] + wd[i];
		float pl = 0.3f + 0.9f * lk;
		if (ctx->crisp) {
			int y0 = (int)(HOR * c->s) + 1;
			for (int py = y0; py < c->h; py++) {
				float y = (py + 0.5f) / c->s;
				for (int px = 0; px < c->w; px++) {
					float x = (px + 0.5f) / c->s;
					uint8_t m;
					float v = plane_tone(st, ysh, ywt, wab, x, y, A, body_x, body_v, pl, &m);
					if (v > 0.0f)
						us_put(c, px, py, pix_fill(m == US_MAT_EARTH ? v * 24.0f : fmaxf(v * 13.0f, 1.05f), px, py), m);
				}
			}
		} else {
			float y0 = HOR, y1 = 1.0f;
			for (int j = 0; j < FLH; j++) {
				float y = y0 + (y1 - y0) * (j + 0.5f) / FLH;
				for (int i = 0; i < FLW; i++) {
					float x = A * (i + 0.5f) / FLW;
					/* three samples down the cell: thin bands (the wet sand) stay smooth */
					float v = 0.0f, mw = 0.0f, hy = (y1 - y0) / FLH / 3.0f;
					for (int q = -1; q <= 1; q++) {
						uint8_t m;
						v += plane_tone(st, ysh, ywt, wab, x, y + q * hy, A, body_x, body_v, pl, &m);
						mw += m == US_MAT_WATER;
					}
					st->fsea[j * FLW + i] = v * (1.0f / 3.0f);
					st->fsand[j * FLW + i] = mw >= 2.0f ? 1.0f : 0.0f;
				}
			}
			/* one bilinear pass: tone interpolated, material from the nearest cell */
			int py0 = (int)(y0 * c->s), py1 = c->h;
			float sy = FLH / ((y1 - y0) * c->s), sx = FLW / (A * c->s);
			for (int py = py0 < 0 ? 0 : py0; py < py1; py++) {
				float fy = ((py + 0.5f) - y0 * c->s) * sy - 0.5f;
				int iy = (int)floorf(fy);
				float ty = fy - iy;
				int r0 = iy < 0 ? 0 : iy >= FLH ? FLH - 1 : iy, r1 = iy + 1 >= FLH ? FLH - 1 : iy + 1 < 0 ? 0 : iy + 1;
				const float *a0 = st->fsea + r0 * FLW, *a1 = st->fsea + r1 * FLW;
				const float *mr = st->fsand + (ty < 0.5f ? r0 : r1) * FLW;
				uint8_t *ink = c->ink + (size_t)py * c->w, *mat = c->mat + (size_t)py * c->w;
				for (int px = 0; px < c->w; px++) {
					float fx = (px + 0.5f) * sx - 0.5f;
					int ix = (int)floorf(fx);
					float tx = fx - ix;
					int q0 = ix < 0 ? 0 : ix >= FLW ? FLW - 1 : ix, q1 = ix + 1 >= FLW ? FLW - 1 : ix + 1 < 0 ? 0 : ix + 1;
					float v = (a0[q0] + (a0[q1] - a0[q0]) * tx) * (1.0f - ty) + (a1[q0] + (a1[q1] - a1[q0]) * tx) * ty;
					int iv = (int)(v * 255.0f + 0.5f);
					if (iv > ink[px]) {
						ink[px] = (uint8_t)(iv > 255 ? 255 : iv);
						mat[px] = mr[tx < 0.5f ? q0 : q1] > 0.5f ? US_MAT_WATER : US_MAT_EARTH;
					}
				}
			}
		}
	}
	/* ---- the headland across the bay, a lighthouse on it: the beam turning slowly over the sea ---- */
	{
		int right = !st->rk_side;
		float hx0 = right ? (1.0f - HD_W) * A : 0.0f, hx1 = right ? A : HD_W * A;
		float hb = proj_y(Z_HD);
		float tx = lh_xf(st) * A; /* tower */
		float ty = hd_top(st, lh_xf(st)) + 0.001f;
		float th = 0.07f, tw0 = 0.0075f, tw1 = 0.0048f;
		float lx = tx, ly = ty - th - 0.008f;
		float lv = 0.3f + 0.1f * day + 0.1f * body_v;
		if (ctx->crisp) {
			int p0 = (int)(hx0 * c->s), p1 = (int)(hx1 * c->s) + 1;
			int yb = (int)(hb * c->s);
			for (int px = p0 < 0 ? 0 : p0; px < p1 && px < c->w; px++) {
				float top = hd_top(st, (px + 0.5f) / c->s / A);
				if (top > 1.5f)
					continue;
				int y0 = (int)(top * c->s);
				for (int py = y0; py <= yb; py++) {
					/* a moonlit rim, then a dark mass with a few dithered strata */
					float n = st->ntile[((py * 3) & (NT - 1)) * NT + ((px + py / 2) & (NT - 1))];
					float v = py == y0 ? 0.62f : py == y0 + 1 ? 0.4f : (n > 0.3f && bayer4(px, py) < 0.5f ? 0.4f : 0.17f);
					put_set(c, px, py, v, US_MAT_EARTH);
				}
			}
			/* the tower: white with two dark bands, a gallery and a bright lantern */
			int tp = (int)(tx * c->s), ty0 = (int)(ty * c->s), ty1 = (int)((ty - th) * c->s);
			for (int py = ty1; py <= ty0; py++) {
				float f = (float)(ty0 - py) / (ty0 - ty1 + 1);
				int hw = f < 0.5f ? 1 : 0;
				int band = (f > 0.3f && f < 0.45f) || (f > 0.65f && f < 0.8f);
				for (int dx = -hw - 1; dx <= hw; dx++)
					put_set(c, tp + dx, py, band ? (dx < 0 ? 0.62f : 0.4f) : (dx < 0 ? 0.9f : 0.62f), band ? US_MAT_BLOOM : US_MAT_ACCENT);
			}
			for (int dx = -2; dx <= 1; dx++)
				put_set(c, tp + dx, ty1 - 1, 0.62f, US_MAT_ACCENT);
			put_set(c, tp - 1, ty1 - 4, 0.62f, US_MAT_ACCENT);
		} else {
			float px = 0, py = 0;
			for (int k = 0; k <= 60; k++) {
				float x = hx0 + (hx1 - hx0) * k / 60.0f, y = fminf(hd_top(st, x / A), hb);
				if (k)
					wu_line(c, px, py, x, y, lv, US_MAT_EARTH);
				/* strata hatched down the cliff and the slopes, shading away from the moon */
				if (k % 3 == 1 && y < hb - 0.004f)
					wu_line(c, x, y + 0.003f, x + (right ? 0.008f : -0.008f), hb - 0.001f, lv * 0.3f, US_MAT_EARTH);
				px = x, py = y;
			}
			wu_line(c, hx0, hb, hx1, hb, lv * 0.5f, US_MAT_WATER); /* where it meets the sea */
			/* tapering tower with painted bands, gallery, lantern and cap */
			wu_line(c, tx - tw0, ty, tx - tw1, ty - th, 0.55f, US_MAT_ACCENT);
			wu_line(c, tx + tw0, ty, tx + tw1, ty - th, 0.55f, US_MAT_ACCENT);
			for (int b = 0; b < 2; b++) {
				float f0 = 0.3f + 0.35f * b, f1 = f0 + 0.15f;
				for (float f = f0; f <= f1; f += 0.03f) {
					float hwb = us_lerp(tw0, tw1, f);
					wu_line(c, tx - hwb, ty - th * f, tx + hwb, ty - th * f, 0.4f, US_MAT_BLOOM);
				}
			}
			wu_line(c, tx - tw1 * 1.8f, ty - th, tx + tw1 * 1.8f, ty - th, 0.55f, US_MAT_ACCENT);
			wu_line(c, tx - tw1, ty - th, tx - tw1, ty - th - 0.009f, 0.45f, US_MAT_ACCENT);
			wu_line(c, tx + tw1, ty - th, tx + tw1, ty - th - 0.009f, 0.45f, US_MAT_ACCENT);
			wu_line(c, tx - tw1 * 1.2f, ty - th - 0.009f, tx, ty - th - 0.014f, 0.45f, US_MAT_ACCENT);
			wu_line(c, tx + tw1 * 1.2f, ty - th - 0.009f, tx, ty - th - 0.014f, 0.45f, US_MAT_ACCENT);
		}
		/* the beam: a slow steady rotation (keeps turning in silence); phrases lengthen its reach */
		st->lh_ph += dt * US_TAU / 11.0f;
		float cph = cosf(st->lh_ph), sph = sinf(st->lh_ph);
		/* flashes as it swings past us; a snare makes the lamp flare and the beam blaze */
		float lamp = us_sat(0.35f + 0.6f * powf(us_sat(sph), 12.0f) + 0.6f * st->snare_env);
		float lit = (0.4f + 0.6f * night) * (0.5f + 0.5f * sph) * (1.0f + 1.4f * st->snare_env);
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
		int rows = ctx->crisp ? 7 + (int)(dens * 4) : 10 + (int)(dens * 8);
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
				float v = us_smooth(-0.15f, 0.35f, s) * (0.2f + 0.16f * s + 0.05f * day + 0.25f * shine) * (0.6f + 0.4f * scale);
				if (i > 0 && (v > 0.03f || pv > 0.03f)) {
					if (ctx->crisp) {
						if (v > 0.12f)
							us_line(c, px, py, x, yy, 0.001f, pix_step(1.6f + 4.0f * v, (int)(x * c->s), k), US_MAT_EARTH);
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

	/* ---- the sea: a few perspective swell lines, denser toward the horizon, broken into long strokes,
	 * and the waves; drawn near -> far with hidden-line removal ---- */
	{
		int ns = ctx->crisp ? NS / 2 : NS;
		int rows = ctx->crisp ? 8 + (int)(dens * 4) : 9 + (int)(dens * 8);
		/* louder = brighter moonlit water; the hats make it flare */
		float glit = body_x >= 0 ? (0.35f + 0.65f * body_v) * lk : 0.0f;
		float gfl = 0.6f + 1.5f * st->hat_env;
		/* the melody (mids) makes the moon path shimmer: glints re-roll faster and the path widens */
		st->gl_t += dt * (0.5f + 1.2f * st->mid_env);
		float tq = floorf(st->gl_t);
		float gwk = 0.85f + 0.6f * st->mid_env;
		/* snare: the lighthouse beam sweeps a pale swath of light across the swells */
		float sb = st->snare_env * (0.5f + 0.5f * night);
		float bsx = 0;
		if (sb > 0.02f) {
			float tx = lh_xf(st) * A, e = us_sat(st->beam_sw);
			e = 1.0f - (1.0f - e) * (1.0f - e);
			bsx = us_lerp(tx, A - tx, 0.15f + 0.7f * e);
		}
		for (int i = 0; i <= ns; i++)
			st->occ[i] = proj_y(shore_z(st, (float)i / ns)) + 0.002f;
		float zs0 = st->tide_s * 1.3f, zfar = KP / 0.006f;
		int rk_done = 0, hd_done = 0, wdone[MAX_WAVES] = {0};
		for (int k = 0; k <= rows; k++) {
			float f = (float)k / rows;
			float z = k == rows ? 1e9f : zs0 * powf(zfar / zs0, powf(f, 0.85f));
			/* the waves lying between the previous row and this one, nearest first */
			for (;;) {
				int best = -1;
				for (int i = 0; i < MAX_WAVES; i++)
					if (!wdone[i] && (st->wv[i].state == ROLL || st->wv[i].state == BREAK) && st->wv[i].z < z &&
					    (best < 0 || st->wv[i].z < st->wv[best].z))
						best = i;
				if (best < 0)
					break;
				wdone[best] = 1;
				draw_wave(st, ctx, c, best, ns, lk, body_x, body_v);
			}
			if (k == rows)
				break;
			if (!rk_done && z > st->rk_z) { /* the rock hides the water behind it */
				rk_done = 1;
				for (int i = 0; i <= ns; i++) {
					float xf = (float)i / ns;
					if (rock_prof(st, xf) > 0.0f)
						st->occ[i] = fminf(st->occ[i], rock_top(st, xf));
				}
			}
			if (!hd_done && z > Z_HD) { /* and the headland */
				hd_done = 1;
				for (int i = 0; i <= ns; i++)
					st->occ[i] = fminf(st->occ[i], hd_top(st, (float)i / ns));
			}
			float base = (0.2f + 0.12f * day) * lk * (1.0f - 0.4f * f);
			float px = 0, py = 0, pv = 0;
			uint8_t pm = US_MAT_WATER;
			int pvis = 0;
			for (int i = 0; i <= ns; i++) {
				float xf = (float)i / ns;
				int sea = z > shore_z(st, xf) * 1.01f;
				float X = (xf - 0.5f) * A * z / KP;
				float h = sea ? sea_h(st, xf, z, X, st->lvl_env) : 0.0f;
				float x = xf * A, y = proj_y(z) - KP * h / z;
				int vis = sea && y < st->occ[i];
				/* long strokes with gaps: an engraver's sea, not a ruled page; far rows run nearly unbroken */
				float n = us_noise2(X * 0.3f + k * 7.31f, z * 0.4f + st->chop_t * 0.12f);
				float on = us_smooth(0.12f - 0.45f * f, 0.3f - 0.45f * f, n);
				float v = (base + 1.4f * fmaxf(h, 0.0f) * (1.0f - 0.5f * f)) * on;
				uint8_t m = US_MAT_WATER;
				if (sb > 0.02f) {
					float bw = (0.05f + 0.12f * KP / z) * A;
					float dx = (x - bsx) / bw;
					if (dx * dx < 9.0f)
						v += 0.45f * sb * expf(-dx * dx) * (1.0f - 0.3f * f);
				}
				/* glitter path: glints re-rolled ~7x a second, wider toward the viewer */
				if (glit > 0) {
					float gw = (0.015f + 0.22f * KP / z) * gwk;
					float dx = (x - body_x) / gw;
					float pg = glit * expf(-dx * dx);
					float hsh = hash1(tq * 0.131f + k * 1.37f + i * 0.711f);
					if (hsh < pg * (0.2f + 0.2f * st->mid_env + 0.35f * st->hat_env)) {
						v += pg * gfl * (0.5f + 0.5f * hash1(hsh * 17.0f));
						m = US_MAT_LIGHT;
					}
				}
				v = us_sat(v);
				if (i > 0 && vis && pvis && (v > 0.015f || pv > 0.015f)) {
					float vv = fmaxf(v, pv) * (v > 0.015f && pv > 0.015f ? 1.0f : 0.5f);
					uint8_t mm = m == US_MAT_LIGHT || pm == US_MAT_LIGHT ? US_MAT_LIGHT : US_MAT_WATER;
					if (ctx->crisp) {
						if (mm == US_MAT_LIGHT || vv > 0.06f)
							us_line(c, px, py, x, y, 1.0f / c->s, pix_fill(vv * 6.0f + 0.9f, (int)(x * c->s), k), mm);
					} else
						wu_line(c, px, py, x, y, vv, mm);
				}
				if (vis)
					st->occ[i] = y;
				px = x, py = y, pv = v, pm = m, pvis = vis;
			}
		}
		/* glitter path: loose glints scattered over the water under the moon / sun, wider toward the viewer,
		 * re-rolled ~7x a second; highs make it sparkle harder */
		if (glit > 0) {
			int ng = (int)(MAX_GLINT * (0.15f + 0.15f * dens + 0.3f * st->lvl_slow + 0.7f * us_sat(st->hat_env * 1.3f)) * us_sat(glit * 1.3f) * (ctx->crisp ? 0.4f : 1.0f));
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
				if (z < shore_z(st, xf) * 1.01f || (y > hd_top(st, xf) && z > Z_HD))
					continue;
				float X = (xf - 0.5f) * A * z / KP;
				float yy = y - KP * sea_h(st, xf, z, X, st->lvl_env) / z;
				float pg = glit * expf(-gx * gx);
				float len = (0.0015f + 0.009f * KP / z) * (0.4f + h4) * (0.8f + 0.9f * st->hat_env);
				float v = us_sat(pg * gfl * (0.5f + 0.55f * h3) * (0.8f + 0.25f * st->hi_env + 0.3f * st->mid_env));
				if (ctx->crisp) {
					int pxl = (int)(x * c->s), pyl = (int)(yy * c->s), nl = 1 + (int)(len * c->s * 0.8f);
					for (int q = 0; q < nl; q++)
						us_put(c, pxl + q - nl / 2, pyl, us_sat(v + 0.25f), US_MAT_LIGHT);
				} else
					wu_line(c, x - len, yy, x + len, yy, v, US_MAT_LIGHT);
			}
		}
		/* the water's edge: a thin lapping line of foam along the sand, flaring on the kick */
		{
			float px = 0, py = 0;
			for (int i = 0; i <= ns; i++) {
				float xf = (float)i / ns, zs = shore_z(st, xf);
				float lap = 0.5f + 0.5f * fsin(st->chop_t * 6.0f + xf * 13.0f + 0.7f * fsin(xf * 5.0f - st->chop_t * 2.0f));
				float x = xf * A, y = proj_y(zs * (1.004f + 0.012f * lap));
				float v = (0.22f + 0.14f * lap + 0.55f * st->kick_env) * (0.5f + 0.6f * lk);
				if (i) {
					if (ctx->crisp)
						us_line(c, px, py, x, y, 1.0f / c->s, pix_step(v * 4.0f + 0.5f, (int)(x * c->s), 0), US_MAT_WATER);
					else
						wu_line(c, px, py, x, y, v, US_MAT_WATER);
				}
				px = x, py = y;
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
						v = side > 0.0f && body_v > 0.3f ? 0.9f : 0.62f; /* moonlit crown */
					else if (n + 0.5f * us_sat(side) > 0.2f && bayer4(px, py) < 0.5f)
						v = 0.4f; /* crags catching light */
					else
						v = 0.17f; /* dark body hides the sand/sea behind */
					put_set(c, px, py, v, US_MAT_EARTH);
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

	/* ---- spray off the breaking lips ---- */
	for (int wi = 0; wi < MAX_WAVES; wi++) {
		wave_t *wv = &st->wv[wi];
		if (wv->state != BREAK)
			continue;
		/* spray thrown off the broken crest on hits; spindrift on highs */
		if (a->snare_hit || us_rand(r) < dt * (0.5f + 4.0f * st->hi_env)) {
			int nsp = a->snare_hit ? 6 + (int)(18 * a->snare * wv->amp) : 1;
			for (int k = 0; k < nsp && st->nsp < MAX_SPRAY; k++) {
				float xp = wv->xp0 + wv->pd * (0.05f + 0.1f * wv->amp) * wv->brk_age;
				float xf = us_clampf(xp - wv->pd * 0.5f * us_rand(r) * us_rand(r), 0.0f, 1.0f);
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
		/* sheen: under the moon, horizontal hairlines across the wet band make a soft reflected column
		 * (steady, unlike the twinkling glitter on the water) */
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
				float v = sqrtf(wa) * (0.04f * day + 1.1f * refl) * (1.0f - 0.3f * f) * lk;
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
					float v = sqrtf(st->wet_a[i]) * body_v * expf(-dx * dx) * shimmer * (1.0f - 0.4f * f) * lk;
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
		float vbase = (0.2f + 0.15f * foam_k) * (0.6f + 0.4f * wv->amp) * lk;
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
		float fv = us_sat((0.6f + 0.4f * foam_k) * fade * (0.5f + 0.5f * lk));
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
		int nfz = (int)(us_sat(st->air_env * st->air_env + 0.8f * st->hat_env) * (40.0f + 70.0f * dens) * fade * (ctx->crisp ? 0.4f : 1.0f));
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

	/* ---- kick surges: each kick breaks a wave on the shore; its whitewater rushes up the sand (fast at
	 * first, a bigger kick runs farther), stalls, and slides back leaving the sand wet ---- */
	for (int si = 0; si < MAX_SURGE; si++) {
		burst_t *b = &st->sg[si];
		if (!b->on)
			continue;
		const float Tu = 0.15f + 0.12f * b->k, Td = 0.75f;
		float u = b->age / Tu, p, fade, flash;
		int back = u >= 1.0f;
		if (!back) {
			p = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
			fade = 1.0f;
		} else {
			float ub = us_sat((b->age - Tu) / Td);
			p = 1.0f; /* it stalls at the top and soaks into the sand */
			fade = (1.0f - ub) * (1.0f - ub);
		}
		if (fade < 0.01f)
			continue;
		flash = us_sat(1.0f - b->age / 0.35f); /* the fresh break is brightest */
		float reach = (0.04f + 0.11f * b->k) * (0.8f + 0.4f * st->swell_env);
		const int N = ctx->crisp ? 48 : 96;
		/* the run-up comes in tongues: where the wave broke hardest it runs farther and whiter; a bigger kick
		 * breaks along more of the beach. Front + trailing lines of the whitewater sheet behind it. */
		int NL = ctx->crisp ? 3 : 4;
		float cov = 0.25f + 0.55f * b->k;
		for (int L = 0; L < NL; L++) {
			float lag = L * (0.2f + 0.04f * L);
			float pl_ = us_sat(p - lag * (1.0f - 0.3f * p));
			float v0 = (L == 0 ? 0.6f + 0.4f * b->k : (0.42f - 0.05f * L) * (0.6f + 0.6f * b->k)) * fade * (0.7f + 0.6f * flash);
			if (back && L > 0)
				v0 *= 0.5f; /* the trailing sheet sinks into the sand first */
			if (v0 < 0.02f)
				continue;
			float qx = 0, qy = 0, qv = 0;
			for (int i = 0; i <= N; i++) {
				float xf = (float)i / N;
				float zs = shore_z(st, xf);
				float lobe = us_sat(cov + 0.9f * us_noise2(xf * 3.1f + b->seed, 7.7f));
				float cusp = (0.3f + 0.7f * lobe) * (1.0f + 0.25f * us_noise2(xf * 9.0f + b->seed, 2.1f + L * 0.3f));
				float z0 = zs * (1.03f + 0.06f * b->k);
				float z1 = zs * (1.0f - reach * cusp);
				float z = z0 + (z1 - z0) * pl_;
				float x = xf * A, y = proj_y(z) + 0.0012f * us_noise2(xf * 70.0f + b->seed, L * 1.3f);
				float v = us_sat(v0 * (0.2f + 0.8f * lobe));
				if (L == 0 && !back && lobe > 0.15f) {
					int col = (int)us_clampf(xf * NX, 0, NX - 1);
					if (z < st->edge_z[col])
						st->edge_z[col] = z;
					if (z < zs && (z < st->wet_z[col] || st->wet_a[col] < 0.3f)) {
						st->wet_z[col] = z;
						st->wet_a[col] = fmaxf(st->wet_a[col], 0.8f);
					}
				}
				if (i) {
					/* foam is broken: gaps along the trailing lines */
					float gap = L ? us_noise2(xf * 13.0f + b->seed * 1.7f, L * 1.9f) : 1.0f;
					float vv = 0.5f * (v + qv);
					if (gap > -0.25f && vv > 0.03f) {
						if (ctx->crisp)
							us_line(c, qx, qy, x, y, L == 0 ? 0.003f : 0.002f, vv, US_MAT_WATER);
						else {
							wu_line(c, qx, qy, x, y, vv, US_MAT_WATER);
							if (L == 0 && !back) /* the doubled lip of the racing front */
								wu_line(c, qx, qy + 0.0025f, x, y + 0.0025f, vv * 0.45f, US_MAT_WATER);
						}
					}
				}
				qx = x, qy = y, qv = v;
			}
		}
	}

	/* ---- snare plumes: spray thrown up off the shore break in fans of ballistic streaks ---- */
	for (int pi = 0; pi < MAX_PLUME; pi++) {
		burst_t *b = &st->pl[pi];
		if (!b->on)
			continue;
		float ca = b->age / 0.5f;
		if (ca >= 1.0f)
			continue;
		ca = 1.0f - (1.0f - ca) * (1.0f - ca) * (1.0f - ca); /* thrown hard: most of the rise in the first moments */
		/* the shore break bursts white along its length, then the spray rises off it */
		float fl = b->age < 0.1f ? 1.0f : us_sat(1.0f - (b->age - 0.1f) / 0.15f);
		fl *= fl * b->k;
		float lift = 1.0f - expf(-b->age / 0.07f); /* the white sheet is flung upward and tears apart */
		if (fl > 0.02f) {
			const int N = ctx->crisp ? 48 : 96;
			for (int row = 0; row < (ctx->crisp ? 2 : 3); row++) {
				float qx = 0, qy = 0;
				for (int i = 0; i <= N; i++) {
					float xf = (float)i / N;
					float zb = shore_z(st, xf) * (1.11f + 0.02f * row);
					float x = xf * A, y = proj_y(zb) - (0.003f + 0.011f * (row + 1) * lift * b->k) * (1.0f + 0.6f * us_noise2(xf * 23.0f + row, b->seed));
					float n = us_noise2(xf * 11.0f + b->seed, 4.0f + row);
					if (i && n > -0.3f) {
						float v = us_sat(fl * (0.85f + 0.4f * n) * (row ? 0.7f : 1.1f));
						if (ctx->crisp)
							us_line(c, qx, qy, x, y, 0.003f, v, US_MAT_WATER);
						else
							wu_line(c, qx, qy, x, y, v, US_MAT_WATER);
					}
					qx = x, qy = y;
				}
			}
		}
		int NF = ctx->crisp ? 10 : 12 + (int)(dens * 8);
		for (int f = 0; f < NF; f++) {
			float h0 = hash1(b->seed * 0.731f + f * 0.377f), h1 = hash1(h0 * 31.7f + f * 0.11f);
			float xf = 0.04f + 0.92f * hash1(h1 * 7.9f + b->seed * 0.37f);
			if (h0 * h1 > 0.6f * b->k) /* a softer snare throws fewer plumes */
				continue;
			float zs = shore_z(st, xf), zb = zs * (1.11f + 0.02f * h0);
			float sc = KP / zb, x0_ = xf * A, yb = proj_y(zb);
			float H = b->k * sc * (0.12f + 0.42f * h0 * h0);
			int NQ = ctx->crisp ? 2 : 3;
			/* droplets flung along their own arcs: the plume reads as spray, not a tuft */
			int ND = ctx->crisp ? 5 : 12;
			for (int q = 0; q < ND; q++) {
				float hq = hash1(h1 * 23.1f + q * 0.193f), hr = hash1(hq * 41.3f + 0.7f);
				float spread = ((hq - 0.5f) * 1.1f + 0.3f * w->wind) * H;
				float Hq = H * (0.5f + 0.8f * hr);
				float ss = us_sat(ca * (0.7f + 0.5f * hr));
				float v = (0.7f + 0.3f * b->k) * powf(us_sat(1.0f - ss * (0.8f + 0.2f * hq)), 1.1f);
				if (v < 0.03f)
					continue;
				us_point(c, x0_ + spread * ss, yb - Hq * (2.4f * ss - 1.7f * ss * ss), ctx->crisp ? 1.0f / c->s : 0.0009f + 0.0007f * hr, v,
					 US_MAT_WATER);
			}
			for (int q = 0; q < NQ; q++) {
				float hq = hash1(h1 * 17.3f + q * 0.29f);
				float spread = ((q - 0.5f * (NQ - 1)) * 0.22f + (hq - 0.5f) * 0.25f + 0.3f * w->wind) * H;
				float Hq = H * (0.6f + 0.6f * hq);
				float sq = ca * (0.85f + 0.3f * hq);
				float s0 = fmaxf(0.0f, sq - 0.22f);
				float v = (0.75f + 0.3f * b->k) * powf(us_sat(1.0f - sq), 1.2f);
				if (v < 0.02f)
					continue;
				float qx = 0, qy = 0;
				for (int g = 0; g <= 3; g++) {
					float ss = us_lerp(s0, sq, g / 3.0f);
					float x = x0_ + spread * ss, y = yb - Hq * (2.4f * ss - 1.7f * ss * ss);
					if (g) {
						float vv = v * (0.4f + 0.2f * g);
						if (ctx->crisp)
							us_line(c, qx, qy, x, y, 1.0f / c->s, vv, US_MAT_WATER);
						else
							wu_line(c, qx, qy, x, y, vv, US_MAT_WATER);
					}
					qx = x, qy = y;
				}
				us_point(c, qx, qy, ctx->crisp ? 1.0f / c->s : 0.0012f, v, US_MAT_WATER);
			}
		}
	}

	/* ---- hats: foam fizz, bright pin-pricks winking along the water's edge and the break ---- */
	if (st->hat_env > 0.08f) {
		/* one scatter per hat (fixed until the next one), fading with it */
		us_rng hr;
		us_rng_seed(&hr, st->hat_seed);
		int nf = (int)((ctx->crisp ? 32.0f : 100.0f + 80.0f * dens) * us_sat(st->hat_env));
		for (int k = 0; k < nf; k++) {
			float xf = us_rand(&hr), d = us_rand(&hr) * us_rand(&hr), jy = us_rand(&hr), vb = us_rand(&hr);
			int col = (int)us_clampf(xf * NX, 0, NX - 1);
			float zs = shore_z(st, xf), e = st->edge_z[col];
			float z = us_lerp(e, zs * 1.12f, d);
			float x = xf * A, y = proj_y(z) - 0.002f * jy;
			float v = (0.5f + 0.5f * vb) * us_sat(st->hat_env * 1.2f);
			if (ctx->crisp)
				us_put(c, (int)(x * c->s), (int)(y * c->s), v, US_MAT_WATER);
			else
				us_point(c, x, y, 0.0008f + 0.0006f * jy, v, US_MAT_WATER);
		}
	}

	/* ---- sandpipers working the wash line: they sprint up ahead of each uprush, chase the backwash down,
	 * probe the wet sand while it's still; a hard hit sometimes makes one flutter up and settle ---- */
	{
		int nbd = 2 + (int)(dens * (N_BIRDS - 2) + 0.5f);
		int startle = a->snare_hit && a->snare > 0.6f;
		for (int i = 0; i < nbd; i++) {
			int col = (int)us_clampf(st->bd_x[i] * NX, 0, NX - 1);
			float zt = st->edge_z[col] * 0.972f; /* a stride up the beach from the water */
			float dz = zt - st->bd_z[i];
			float vmax = (dz < 0 ? 0.22f : 0.08f) * dt; /* flee the uprush, saunter after the backwash */
			float step = us_clampf(dz, -vmax, vmax);
			st->bd_z[i] = us_clampf(st->bd_z[i] + step, 0.62f, 1.8f);
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
