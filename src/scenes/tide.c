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
 *
 * Audio: swell (phrase loudness) + a slow ~50 s set rhythm -> wave size and how often they come; bass
 *        pushes the run-up; highs/air -> foam lace density + glitter twinkle + spindrift; onsets throw spray
 *        off breaking crests; level stirs the chop. Silence = small lapping waves, still rolling in.
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
#define N_STARS 140
#define FW 220 /* foam lace grid */
#define FH 22

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
} wave_t;

typedef struct {
	float x, y, vx, vy, life;
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
	float lx[FH][FW + 1], ly[FH][FW + 1], lf[FH][FW + 1]; /* foam lace scratch */
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
	return st->tide_s * (1.0f + 0.03f * us_noise2(xf * 3.0f, 7.7f) + 0.06f * (xf - 0.5f) * (xf - 0.5f));
}

/* sea surface height at screen column xf, depth z */
static float sea_h(const st_t *st, float xf, float z, float X, float level)
{
	float h = 0.006f * sinf(z * 2.2f - st->chop_t * 2.0f + 1.7f * us_noise2(xf * 3.0f, z * 0.3f));
	h += (0.006f + 0.008f * level) * us_noise3(X * 0.7f, z * 2.2f - st->chop_t * 1.5f, st->chop_t * 0.2f);
	for (int i = 0; i < MAX_WAVES; i++) {
		const wave_t *w = &st->wv[i];
		if ((w->state != ROLL && w->state != BREAK) || w->hf < 0.002f)
			continue;
		float zc = crest_z(w, xf, st->t);
		float wid = 0.2f + 0.16f * zc;
		float u = (z - zc) / wid;
		if (u < -3.0f || u > 5.0f)
			continue;
		/* asymmetric: steep shoreward face, long back slope */
		float p = u < 0 ? expf(-u * u * (1.0f + 4.0f * w->steep) * 1.5f) : expf(-u * u * 0.3f);
		/* the crest isn't uniform along its length */
		float along = 0.15f + 0.85f * us_smooth(-0.5f, 0.45f, us_noise2(xf * 3.2f + w->seed, 3.3f));
		h += w->hf * p * along;
	}
	return h;
}

/* fast thin AA line (Wu) — many short row segments in Ink */
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
			us_put(c, iy, i, v * cov * (1 - f), mat);
			us_put(c, iy + 1, i, v * cov * f, mat);
		} else {
			us_put(c, i, iy, v * cov * (1 - f), mat);
			us_put(c, i, iy + 1, v * cov * f, mat);
		}
	}
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
	for (int i = 0; i < N_STARS; i++) {
		st->star_x[i] = us_rand(r);
		st->star_y[i] = us_rand(r);
		st->star_p[i] = us_rand(r) * 30.0f;
	}
	st->tide_s = 1.15f - 0.4f * ctx->w->water_level;
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
	/* tide: world water level + a slow tidal breath */
	float tide_target = 1.15f - 0.4f * w->water_level + 0.04f * sinf(t * US_TAU / 200.0f);
	st->tide_s = us_approach(st->tide_s, tide_target, 20.0f, dt);

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
			if (wv->z <= st->tide_s * 1.02f) {
				wv->state = SWASH;
				wv->run_t = 0;
				wv->hf = 0;
			}
		}
	}

	/* ---- sky: stars, moon or sun, a few cloud streaks ---- */
	if (night > 0.02f) {
		int ns = (int)(N_STARS * (0.4f + 0.6f * dens));
		for (int i = 0; i < ns; i++) {
			float sy = 0.02f + st->star_y[i] * (HOR - 0.05f);
			float tw = 0.5f + 0.5f * sinf(t * (0.7f + st->star_p[i] * 0.05f) + st->star_p[i]);
			float v = night * (0.1f + 0.3f * tw * tw) * (0.4f + 0.6f * hash1(st->star_p[i]));
			us_point(c, st->star_x[i] * A, sy, ctx->crisp ? 0.0016f : 0.001f, v, US_MAT_LIGHT);
		}
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
		for (int k = 0; k < 4; k++) {
			float cy = 0.06f + 0.06f * k + 0.01f * us_noise2(k * 3.1f, t * 0.01f);
			float cx = fmodf(0.3f * k + t * (0.004f + 0.006f * w->wind) + 10.0f, 1.4f) - 0.2f;
			float len = 0.15f + 0.1f * hash1(k + 0.3f);
			float v = 0.04f + 0.1f * day + 0.08f * body_v;
			us_line(c, cx * A, cy, (cx + len) * A, cy + 0.002f, 0.001f, v, US_MAT_WATER); /* clouds: water vapour */
			us_line(c, (cx + 0.05f) * A, cy + 0.007f, (cx + len * 0.7f) * A, cy + 0.008f, 0.001f, v * 0.6f, US_MAT_WATER);
		}
	}

	/* ---- sand: ripple dashes ---- */
	{
		int rows = 12 + (int)(dens * 10);
		for (int k = 0; k < rows; k++) {
			float f = (k + 0.5f) / rows;
			float z = st->tide_s * powf(0.5f / st->tide_s, f) * 0.97f;
			float y = proj_y(z);
			if (y > 1.02f)
				continue;
			float scale = KP / z;
			int nd = 14 + (int)(dens * 16);
			for (int i = 0; i < nd; i++) {
				float xf = (i + hash1(k * 3.7f + i * 0.91f)) / nd;
				float X = (xf - 0.5f) * A / scale;
				float s = us_noise2(X * 0.8f + 11.0f, z * 3.0f);
				if (s < 0.05f)
					continue;
				float len = 0.03f * scale * (0.6f + s);
				float wob = 0.004f * scale * us_noise2(X * 2.0f, z * 5.0f);
				us_line(c, xf * A - len, y + wob, xf * A + len, y - wob, ctx->crisp ? 0.002f : 0.001f,
					0.08f + 0.12f * s + 0.05f * day, US_MAT_EARTH);
			}
		}
	}

	/* ---- the sea: ridge lines near -> far with hidden-line removal ---- */
	{
		int ns = ctx->crisp ? NS / 2 : NS;
		int rows = ctx->crisp ? 26 + (int)(dens * 16) : 36 + (int)(dens * 40);
		float glit = body_x >= 0 ? 0.35f + 0.65f * body_v : 0.0f;
		float tq = floorf(t * 7.0f);
		for (int i = 0; i <= ns; i++)
			st->occ[i] = proj_y(shore_z(st, (float)i / ns)) + 0.002f;
		float zs0 = st->tide_s * 0.98f;
		for (int k = 0; k < rows; k++) {
			float f = (float)k / (rows - 1);
			/* rows evenly spaced on screen (a ridge plot), from the shoreline up to just below the horizon */
			float ys = proj_y(zs0), ye = HOR + 0.006f;
			float z = KP / (us_lerp(ys, ye, f) - HOR);
			float base = (0.16f + 0.16f * day) * (1.0f - 0.8f * f * f);
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
						float gw = 0.015f + 0.22f * KP / zz;
						float dx = (x - body_x) / gw;
						float pg = glit * expf(-dx * dx);
						float hsh = hash1(tq * 0.131f + k * 1.37f + i * 0.711f);
						if (hsh < pg * (0.55f + 0.3f * st->hi_env)) {
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
		/* horizon */
		us_line(c, 0, HOR, A, HOR, 0.0012f, 0.3f + 0.2f * day, US_MAT_WATER);
	}

	/* ---- foam lace behind breaking crests ---- */
	for (int wi = 0; wi < MAX_WAVES; wi++) {
		wave_t *wv = &st->wv[wi];
		if (wv->state != BREAK)
			continue;
		/* iso-lines around the ridges of a noise field (|n| < thr): organic loops and holes like real
		 * foam lace; dense at the broken front, thinning out behind it */
		float(*lx)[FW + 1] = st->lx, (*ly)[FW + 1] = st->ly, (*lf)[FW + 1] = st->lf;
		int fw = ctx->crisp ? 80 : FW, fh = ctx->crisp ? 10 : 12 + (int)(dens * (FH - 12));
		float band = (0.06f + 0.25f * wv->amp) * (1.0f + 1.5f * us_sat(wv->brk_age * 0.15f));
		for (int i = 0; i <= fw; i++) {
			float xf = (float)i / fw;
			float zc = crest_z(wv, xf, t);
			float X = (xf - 0.5f) * A * zc / KP;
			for (int j = 0; j < fh; j++) {
				float fj = (float)j / (fh - 1);
				float fz = zc * (1.0f + band * fj * fj);
				float lace = us_noise3(X * 2.0f, fz * 8.0f, t * 0.2f + wv->seed);
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
			}
		}
	}

	/* ---- wet line: dotted, drying ---- */
	for (int i = 0; i < NX; i++) {
		st->wet_a[i] -= dt / 14.0f;
		if (st->wet_a[i] <= 0) {
			st->wet_a[i] = 0;
			continue;
		}
		float xf = (i + 0.5f) / NX;
		float y = proj_y(st->wet_z[i]);
		int dots = ctx->crisp ? 1 : 3;
		for (int k = 0; k < dots; k++) {
			float xx = (xf + (k - 1) * 0.33f / NX) * A;
			float jit = 0.0015f * us_noise2(xx * 40.0f, 3.0f);
			us_point(c, xx, y + jit, ctx->crisp ? 0.0016f : 0.001f, 0.4f * st->wet_a[i], US_MAT_EARTH);
		}
		/* wet sand shine under the moon */
		if (body_x >= 0 && fabsf(xf * A - body_x) < 0.06f)
			us_point(c, xf * A, y + 0.004f, 0.0012f, 0.35f * body_v * st->wet_a[i], US_MAT_LIGHT);
	}

	/* ---- swash: run up the sand, stall, slide back ---- */
	for (int wi = 0; wi < MAX_WAVES; wi++) {
		wave_t *wv = &st->wv[wi];
		if (wv->state != SWASH)
			continue;
		const int N = ctx->crisp ? 64 : 96;
		wv->run_t += dt;
		float Tu = 1.6f + 1.2f * wv->amp, Td = 3.0f + 1.5f * wv->amp;
		float reach = wv->run_max * (1.0f + 0.5f * st->bass_env);
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
			int col = (int)(xf * NX);
			if (col >= NX)
				col = NX - 1;
			if (!back && (zr < st->wet_z[col] || st->wet_a[col] < 0.3f)) {
				st->wet_z[col] = zr;
				st->wet_a[col] = 1.0f;
			}
		}
		if (ctx->crisp)
			us_polyline(c, pts, N + 1, 0.002f, (0.55f + 0.35f * foam_k) * fade, US_MAT_WATER);
		else
			for (int i = 0; i < N; i++) {
				float lace = us_noise2(i * 0.9f + wv->seed, t * 0.7f);
				if (lace > -0.25f)
					us_line(c, pts[2 * i], pts[2 * i + 1], pts[2 * i + 2], pts[2 * i + 3], 0.0014f,
						us_sat((0.55f + 0.4f * foam_k) * fade * (0.6f + 0.6f * lace)), US_MAT_WATER);
			}
		us_polyline(c, spts, N + 1, 0.001f, 0.18f * fade, US_MAT_WATER);
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
			us_point(c, xf * A, proj_y(z), ctx->crisp ? 0.0016f : 0.001f, 0.5f * life, US_MAT_WATER);
		}
		if (back && wv->run_t > Tu + Td)
			wv->state = DONE;
	}

	/* ---- spray ---- */
	for (int i = 0; i < st->nsp;) {
		spray_t *s = &st->sp[i];
		s->vy += 0.25f * dt;
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
}

const us_scene_vt us_scene_tide = {"tide", "Tide", 0.12f, create, destroy, frame};
