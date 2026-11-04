/* Understory scene: Fireflies — a bioluminescent swarm at the edge of a dusk field.
 *
 * Every firefly is an agent with its own flash oscillator (phase th, natural frequency w_i).
 * The oscillators are coupled Kuramoto-style through a coarse spatial grid (each firefly feels the mean
 * phase of its neighbourhood) plus a weak global field. Coupling strength follows the music's energy:
 *   quiet  -> weak coupling, wide frequency spread: a few fireflies blink independently;
 *   dense  -> strong coupling: neighbourhoods lock, and a slow drifting frequency gradient across the field
 *             turns the locked swarm into waves of synchronised flashing that roll across the meadow.
 * Beats nudge every phase toward its local mean (and gently entrain the swarm's mean frequency to the tempo).
 * Each flash leaves a short additive trail; low fireflies are mirrored as broken streaks in a still pool.
 * World: reads calm, insects (population at open), daylight (by day far fewer and dimmer), season;
 *        writes insects (calm, dark music breeds them).
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAXF 520
#define TREE_N 320
#define NBLADES 170
#define NCAT 7
#define NSTARS 140
#define NRIP 22
#define GX 14
#define GY 7
#define FW 112
#define FH 64

#define POOL_Y 0.885f
#define FLY_TOP 0.36f
#define FLY_BOT 0.875f
#define FIELD_Y0 0.0f
#define FIELD_Y1 0.80f

typedef struct {
	float x, y;   /* x as 0..1 fraction of width, y normalized */
	float vx, vy; /* in the same units per second */
	float z;      /* depth 0 far .. 1 near */
	float th;     /* flash phase, flash at th == 0 (mod 2pi) */
	float dev;    /* natural-frequency deviation (gaussian) */
	float life;   /* 0..1 fade in/out */
	float px, py; /* previous drawn position (normalized, x already * aspect) */
	float nz;     /* noise offset */
	int state;    /* 0 dead, 1 alive, 2 leaving */
	int cell;
} fly_t;

typedef struct {
	us_rng rng;
	int inited;
	fly_t f[MAXF];
	float tree[TREE_N]; /* treeline top y per x fraction */
	float blade_x[NBLADES], blade_h[NBLADES], blade_lean[NBLADES], blade_ph[NBLADES];
	float cat_x[NCAT], cat_h[NCAT], cat_ph[NCAT];
	float star_x[NSTARS], star_y[NSTARS], star_b[NSTARS], star_ph[NSTARS];
	float rip_x[NRIP], rip_y[NRIP], rip_len[NRIP], rip_sp[NRIP];
	float field[FW * FH];
	float gc[GX * GY], gs[GX * GY], gn[GX * GY];
	float lc[GX * GY], ls[GX * GY];
	float energy;     /* smoothed musical energy 0..1 */
	float pulse;
	float omega;      /* swarm mean angular frequency */
	float beat_iv;    /* tracked beat interval (s) */
	float since_beat;
	float spawn_acc, kill_acc;
	float glow;       /* smoothed total flash light (lights the grass a touch) */
	float wind_t;
	float grad_t;
	float sync;       /* global order parameter (smoothed), for diagnostics + glow */
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x66697265u);
	return s;
}

static void destroy(void *p) { free(p); }

static float tree_at(const st_t *st, float xf)
{
	float u = us_clampf(xf, 0.0f, 1.0f) * (TREE_N - 1);
	int i = (int)u;
	if (i >= TREE_N - 1)
		return st->tree[TREE_N - 1];
	float t = u - i;
	return st->tree[i] * (1 - t) + st->tree[i + 1] * t;
}

static void build_treeline(st_t *st)
{
	us_rng *r = &st->rng;
	for (int i = 0; i < TREE_N; i++)
		st->tree[i] = 0.64f - 0.03f * us_fbm2(i * 0.012f, 3.7f, 3);
	/* conifers (triangles) and rounded crowns (domes) */
	int ntrees = 64;
	for (int k = 0; k < ntrees; k++) {
		float cx = us_rand(r);
		int conifer = us_rand(r) < 0.55f;
		float h = conifer ? us_randr(r, 0.08f, 0.2f) : us_randr(r, 0.06f, 0.15f);
		/* a gap in the middle-left makes the line breathe */
		if (cx > 0.3f && cx < 0.42f)
			h *= 0.45f;
		float wdt = conifer ? h * us_randr(r, 0.32f, 0.45f) : h * us_randr(r, 0.6f, 1.0f);
		for (int i = 0; i < TREE_N; i++) {
			float xf = (float)i / (TREE_N - 1);
			float d = fabsf(xf - cx) * 1.7778f; /* roughly isotropic at 16:9 */
			float base = 0.64f;
			float top;
			if (conifer) {
				/* jagged conifer: triangle with branch-tier notches */
				float edge = d / wdt;
				if (edge > 1.0f)
					continue;
				float tier = 0.06f * fabsf(sinf(edge * 9.0f + cx * 40.0f)) * (0.3f + edge);
				top = base - h * (1.0f - edge) + h * tier;
			} else {
				float e = d / wdt;
				if (e > 1.0f)
					continue;
				top = base - h * 0.55f - h * 0.45f * sqrtf(1.0f - e * e) +
				      0.012f * us_noise2(xf * 90.0f, cx * 13.0f);
			}
			if (top < st->tree[i])
				st->tree[i] = top;
		}
	}
}

static void spawn_fly(st_t *st, fly_t *f, float A, float life0)
{
	us_rng *r = &st->rng;
	memset(f, 0, sizeof(*f));
	f->z = us_rand(r);
	f->x = us_rand(r);
	/* far ones hug the treeline, near ones roam low over the field */
	float ylo = us_lerp(0.50f, 0.66f, f->z), yhi = us_lerp(0.70f, FLY_BOT, f->z);
	f->y = us_randr(r, ylo, yhi);
	if (us_rand(r) < 0.15f) /* a few up in the trees */
		f->y = us_randr(r, FLY_TOP + 0.04f, 0.6f);
	f->th = us_randr(r, -US_PI, US_PI);
	f->dev = us_randn(r);
	f->life = life0;
	f->state = 1;
	f->nz = us_rand(r) * 100.0f;
	f->px = f->x * A;
	f->py = f->y;
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *st = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, A = ctx->aspect, rdt = ctx->real_dt;
	float dens = ctx->p->density;
	float t = (float)ctx->t;
	float winter = us_world_winter(w);
	float day = us_smooth(0.35f, 0.85f, w->daylight); /* 0 dusk/night .. 1 broad day */
	float dayf = 1.0f - 0.8f * day;
	float nmax = 80.0f + 440.0f * dens;
	if (nmax > MAXF)
		nmax = MAXF;

	/* ---- energy + tempo ---- */
	/* "pulse" = how much rhythmic drive there is (kicks averaged over a bar-ish window) */
	st->pulse = us_approach(st->pulse, us_sat(a->kick * 1.2f + 0.3f * a->onset), 1.2f, rdt);
	float e_in = us_sat(0.35f * a->level + 0.2f * a->swell + 0.75f * st->pulse);
	st->energy = us_approach(st->energy, e_in, e_in > st->energy ? 1.2f : 3.5f, rdt);
	float E = st->energy;

	if (!st->inited) {
		build_treeline(st);
		us_rng *r = &st->rng;
		for (int b = 0; b < NBLADES; b++) {
			st->blade_x[b] = us_rand(r);
			float hh = us_rand(r);
			st->blade_h[b] = 0.03f + 0.15f * hh * hh;
			st->blade_lean[b] = us_randn(r) * 0.12f;
			st->blade_ph[b] = us_rand(r) * 50.0f;
		}
		for (int k = 0; k < NCAT; k++) {
			st->cat_x[k] = us_rand(r) < 0.5f ? us_randr(r, 0.02f, 0.25f) : us_randr(r, 0.72f, 0.98f);
			st->cat_h[k] = us_randr(r, 0.13f, 0.24f);
			st->cat_ph[k] = us_rand(r) * 20.0f;
		}
		for (int k = 0; k < NSTARS; k++) {
			st->star_x[k] = us_rand(r);
			st->star_y[k] = 0.02f + 0.55f * us_rand(r) * us_rand(r);
			st->star_b[k] = 0.25f + 0.6f * us_rand(r) * us_rand(r);
			st->star_ph[k] = us_rand(r) * US_TAU;
		}
		for (int k = 0; k < NRIP; k++) {
			st->rip_x[k] = us_rand(r);
			st->rip_y[k] = us_randr(r, POOL_Y + 0.012f, 0.995f);
			st->rip_len[k] = us_randr(r, 0.03f, 0.16f);
			st->rip_sp[k] = us_randr(r, 0.004f, 0.012f) * (us_rand(r) < 0.5f ? -1 : 1);
		}
		/* population at open remembers the world's insects */
		float frac = (0.10f + 0.6f * w->insects) * dayf * (1.0f - 0.8f * winter);
		int n0 = (int)(nmax * frac);
		for (int i = 0; i < n0 && i < MAXF; i++)
			spawn_fly(st, &st->f[i], A, 1.0f);
		st->omega = US_TAU / 1.7f;
		st->beat_iv = 0.5f;
		st->inited = 1;
	}

	st->since_beat += rdt;
	if (a->beat) {
		if (st->since_beat > 0.22f && st->since_beat < 1.3f)
			st->beat_iv = us_lerp(st->beat_iv, st->since_beat, 0.25f);
		st->since_beat = 0;
	}
	{
		float P = st->beat_iv * 2.0f;
		while (P < 1.05f)
			P *= 2.0f;
		while (P > 2.3f)
			P *= 0.5f;
		float w_nat = US_TAU / 1.7f;
		float target = us_lerp(w_nat, US_TAU / P, us_sat(E * 1.4f));
		st->omega = us_approach(st->omega, target, 4.0f, dt);
	}

	/* ---- world ---- */
	us_world_add(w, &w->insects, 0.004f * w->calm * (1.0f - day) * (1.0f - winter), dt);

	/* ---- population ---- */
	int alive = 0;
	for (int i = 0; i < MAXF; i++)
		if (st->f[i].state == 1)
			alive++;
	float frac = (0.07f + 0.35f * w->insects + 0.9f * E * E) * dayf * (1.0f - 0.85f * winter);
	int target = (int)(nmax * us_sat(frac));
	if (alive < target) {
		st->spawn_acc += dt * (3.0f + 40.0f * E);
		while (st->spawn_acc >= 1.0f) {
			st->spawn_acc -= 1.0f;
			for (int i = 0; i < MAXF; i++)
				if (st->f[i].state == 0) {
					spawn_fly(st, &st->f[i], A, 0.0f);
					break;
				}
		}
	} else
		st->spawn_acc = 0;
	if (alive > target + 4) {
		st->kill_acc += dt * (1.5f + 0.05f * (alive - target));
		while (st->kill_acc >= 1.0f) {
			st->kill_acc -= 1.0f;
			int k = (int)(us_rand(&st->rng) * MAXF);
			for (int j = 0; j < MAXF; j++) {
				fly_t *f = &st->f[(k + j) % MAXF];
				if (f->state == 1) {
					f->state = 2;
					break;
				}
			}
		}
	} else
		st->kill_acc = 0;

	/* ---- Kuramoto mean fields on a coarse grid ---- */
	memset(st->gc, 0, sizeof(st->gc));
	memset(st->gs, 0, sizeof(st->gs));
	memset(st->gn, 0, sizeof(st->gn));
	float Cg = 0, Sg = 0, Ng = 0;
	for (int i = 0; i < MAXF; i++) {
		fly_t *f = &st->f[i];
		if (!f->state)
			continue;
		int gx = (int)(us_sat(f->x) * (GX - 0.001f));
		int gy = (int)(us_sat((f->y - FLY_TOP) / (FLY_BOT - FLY_TOP)) * (GY - 0.001f));
		f->cell = gy * GX + gx;
		float cw = cosf(f->th), sw = sinf(f->th);
		st->gc[f->cell] += cw * f->life;
		st->gs[f->cell] += sw * f->life;
		st->gn[f->cell] += f->life;
		Cg += cw * f->life;
		Sg += sw * f->life;
		Ng += f->life;
	}
	for (int gy = 0; gy < GY; gy++)
		for (int gx = 0; gx < GX; gx++) {
			float C = 0, S = 0, N = 0;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++) {
					int xx = gx + dx, yy = gy + dy;
					if (xx < 0 || yy < 0 || xx >= GX || yy >= GY)
						continue;
					float k = (dx == 0 && dy == 0) ? 1.0f : 0.5f;
					C += st->gc[yy * GX + xx] * k;
					S += st->gs[yy * GX + xx] * k;
					N += st->gn[yy * GX + xx] * k;
				}
			st->lc[gy * GX + gx] = C / (N + 1.5f);
			st->ls[gy * GX + gx] = S / (N + 1.5f);
		}
	float Rg = Ng > 0 ? sqrtf(Cg * Cg + Sg * Sg) / Ng : 0;
	st->sync = us_approach(st->sync, Rg, 1.0f, rdt);
	if (Ng > 0) {
		Cg /= Ng + 4.0f;
		Sg /= Ng + 4.0f;
	}

	float K = 0.15f + 7.0f * E * E;                 /* local coupling */
	float Kg = 0.4f * E * E;                        /* weak global coupling */
	float spread = 0.17f - 0.08f * E;               /* natural-frequency spread */
	st->grad_t += dt * 0.021f;
	float grad = 0.07f * sinf(st->grad_t * US_TAU); /* slowly reversing wave direction */
	float gradv = 0.03f * sinf(st->grad_t * 4.1f + 1.0f);
	/* travelling-wave lag: synced fireflies flash in a front that sweeps across the field */
	float wang = st->grad_t * 2.3f + 0.6f * sinf(st->grad_t * 5.0f);
	float wdx = cosf(wang), wdy = 0.5f * sinf(wang);
	float wave_k = US_TAU * 1.05f * us_smooth(0.15f, 0.6f, E);
	float nudge = a->beat ? (0.25f + 0.9f * E) * us_sat(0.4f + a->onset) : 0.0f;

	float wind = us_sat(0.2f * w->wind + 0.8f * a->high + 0.3f * a->air);
	st->wind_t += dt * (0.12f + 0.6f * wind);
	float agit = 0.6f + 0.8f * a->high + 0.5f * E;
	float lum = (0.55f + 0.45f * E) * (1.0f - 0.65f * day);

	/* ================= drawing: sky glow + mist ================= */
	{
		/* afterglow at dusk, a moonlit glow at night, a pale sky by day */
		float dusk = 0.16f + 0.26f * (1.0f - day) * us_smooth(0.02f, 0.3f, w->daylight) + 0.3f * day;
		if (ctx->crisp) {
			/* pixel styles: one low-res field (cheap at this size), sky masked by the treeline */
			float y0 = FIELD_Y0, y1 = FIELD_Y1;
			for (int j = 0; j < FH; j++) {
				float y = y0 + (y1 - y0) * (j + 0.5f) / FH;
				for (int i = 0; i < FW; i++) {
					float xf = (i + 0.5f) / FW;
					float top = tree_at(st, xf);
					float v = 0;
					if (y < top) {
						float h = 0.64f - y;
						/* pixel palettes quantise hard: lift the glow so it lands on real ramp steps */
						v = (0.5f + 0.7f * dusk) * (0.22f + 0.78f * expf(-h / 0.22f));
						v *= 0.8f + 0.2f * us_noise2(xf * 3.0f + t * 0.004f, y * 9.0f);
					} else {
						float band = (y > top + 0.01f) * us_smooth(0.57f, 0.645f, y) * (1.0f - us_smooth(0.68f, 0.79f, y));
						float n = us_fbm2(xf * 5.0f - t * 0.012f, y * 14.0f + t * 0.004f, 2);
						v = band * (0.05f + 0.25f * us_sat(0.5f + n)) * (0.5f + 0.8f * w->calm);
					}
					st->field[j * FW + i] = v;
				}
			}
			us_field(c, st->field, FW, FH, 1.0f, US_MAT_BG, 0, y0, A, y1);
		} else {
			/* ink: engraved horizontal hatching, denser and brighter toward the horizon, clipped by the
			 * treeline and broken where slow clouds drift across */
			const int NH = 34;
			for (int k = 0; k < NH; k++) {
				float u = (k + 0.5f) / NH;
				float y = 0.64f - 0.56f * powf(u, 1.7f);
				float v = 2.3f * dusk * (0.07f + 0.93f * expf(-(0.64f - y) / 0.17f));
				if (v < 0.02f)
					continue;
				int run0 = -1;
				for (int i = 0; i <= TREE_N; i++) {
					float xf = (float)i / (TREE_N - 1);
					int open = 0;
					if (i < TREE_N) {
						float cl = us_noise2(xf * 3.2f - t * 0.006f, y * 11.0f + k * 0.13f);
						open = st->tree[i] > y + 0.004f && cl > -0.28f;
					}
					if (open && run0 < 0)
						run0 = i;
					else if (!open && run0 >= 0) {
						float xa = (float)run0 / (TREE_N - 1) * A, xb = (float)(i - 1) / (TREE_N - 1) * A;
						if (xb > xa)
							us_line(c, xa, y, xb, y, 0.001f, v, US_MAT_BG);
						run0 = -1;
					}
				}
			}
			/* ground mist: a narrow low-res field only over the mist band */
			const float m0 = 0.46f, m1 = 0.80f;
			const int MH = 24;
			for (int j = 0; j < MH; j++) {
				float y = m0 + (m1 - m0) * (j + 0.5f) / MH;
				for (int i = 0; i < FW; i++) {
					float xf = (i + 0.5f) / FW;
					float top = tree_at(st, xf);
					float band = (y > top + 0.01f) * us_smooth(0.57f, 0.645f, y) * (1.0f - us_smooth(0.68f, 0.79f, y));
					float v = 0;
					if (band > 0) {
						float n = us_fbm2(xf * 5.0f - t * 0.012f, y * 14.0f + t * 0.004f, 2);
						v = band * (0.01f + 0.08f * us_sat(0.5f + n)) * (0.5f + 0.8f * w->calm);
					}
					st->field[j * FW + i] = v;
				}
			}
			us_field(c, st->field, FW, MH, 1.0f, US_MAT_BG, 0, m0, A, m1);
		}
	}

	/* stars (only above the treeline, fade by day) */
	{
		float sv = (1.0f - us_smooth(0.15f, 0.55f, w->daylight));
		if (sv > 0.01f)
			for (int k = 0; k < NSTARS; k++) {
				float xf = st->star_x[k];
				if (st->star_y[k] > tree_at(st, xf) - 0.01f)
					continue;
				float tw = 0.7f + 0.3f * sinf(t * (0.7f + k % 5 * 0.3f) + st->star_ph[k]);
				float sb = sv * st->star_b[k] * tw * (ctx->crisp ? 0.6f : 1.0f);
				if (ctx->crisp && st->star_b[k] < 0.4f)
					continue;
				us_point(c, xf * A, st->star_y[k], ctx->crisp ? 0.0025f : 0.0011f, sb,
					 US_MAT_ACCENT);
			}
	}

	/* moon on a night arc */
	float moon_x = -1, moon_v = 0;
	{
		float u = fmodf(w->day_phase + 0.25f, 1.0f) / 0.5f; /* 0..1 across the night */
		if (u >= 0.0f && u <= 1.0f) {
			moon_x = A * (0.12f + 0.76f * u);
			float my = 0.42f - 0.32f * sinf(US_PI * u);
			moon_v = (1.0f - day) * us_smooth(0.0f, 0.08f, u) * us_smooth(1.0f, 0.92f, u);
			if (moon_v > 0.02f && my < tree_at(st, moon_x / A) - 0.03f) {
				float rr = 0.026f;
				float mw = ctx->crisp ? 0.007f : 0.0016f;
				us_arc(c, moon_x, my, rr, 1.25f, 5.0f, mw, 0.8f * moon_v, US_MAT_LIGHT);
				us_arc(c, moon_x - rr * 0.32f, my, rr * 0.8f, 1.35f, 4.93f, mw * 0.8f, 0.45f * moon_v, US_MAT_LIGHT);
			} else
				moon_v = 0;
		}
	}

	/* treeline contour (silhouette edge) */
	{
		float pts[2 * 161];
		for (int i = 0; i <= 160; i++) {
			float xf = i / 160.0f;
			pts[2 * i] = xf * A;
			pts[2 * i + 1] = tree_at(st, xf);
		}
		if (!ctx->crisp)
			us_polyline(c, pts, 161, 0.0011f, 0.16f + 0.1f * day, US_MAT_LEAF);
	}

	/* ================= fireflies: simulate ================= */
	float gl_sum = 0;
	c->blend = US_BLEND_ADD;
	for (int i = 0; i < MAXF; i++) {
		fly_t *f = &st->f[i];
		if (!f->state)
			continue;
		if (f->state == 1)
			f->life = us_sat(f->life + dt * 0.5f);
		else {
			f->life -= dt * 0.35f;
			if (f->life <= 0) {
				f->state = 0;
				continue;
			}
		}
		/* phase dynamics */
		float lc = st->lc[f->cell], ls = st->ls[f->cell];
		float cw = cosf(f->th), sw = sinf(f->th);
		float om = st->omega * (1.0f + spread * f->dev + grad * (f->x - 0.5f) + gradv * (f->y - 0.62f) * 4.0f);
		float dth = om + K * (ls * cw - lc * sw) + Kg * (Sg * cw - Cg * sw);
		f->th += dth * dt;
		if (nudge > 0) {
			float rl = sqrtf(lc * lc + ls * ls) + 1e-4f;
			f->th += nudge * (ls * cw - lc * sw) / rl * 0.35f;
		}
		if (f->th > US_PI)
			f->th -= US_TAU;
		else if (f->th < -US_PI)
			f->th += US_TAU;

		/* flight: steer along a slow noise flow, stay over the field */
		float ang = us_noise3(f->x * 2.2f, f->y * 3.0f, t * 0.05f + f->nz) * US_TAU;
		float spd = (0.010f + 0.016f * (0.4f + f->z)) * agit;
		float tvx = cosf(ang) * spd + 0.01f * wind * (0.5f + 0.5f * us_noise2(t * 0.1f, 3.0f));
		float tvy = sinf(ang) * spd * 0.6f;
		float ylo = us_lerp(FLY_TOP, 0.62f, f->z), yhi = us_lerp(0.66f, FLY_BOT, f->z);
		if (f->y < ylo)
			tvy += (ylo - f->y) * 1.0f;
		if (f->y > yhi)
			tvy -= (f->y - yhi) * 1.0f;
		f->vx = us_approach(f->vx, tvx, 0.8f, dt);
		f->vy = us_approach(f->vy, tvy, 0.8f, dt);
		f->x += f->vx * dt / A;
		f->y += f->vy * dt + 0.004f * sinf(t * 1.3f + f->nz) * dt;
		if (f->x < -0.02f)
			f->x += 1.04f;
		if (f->x > 1.02f)
			f->x -= 1.04f;

		/* flash: short bright pulse at th ~ 0, a hint of body glow otherwise */
		float cth = cosf(f->th - wave_k * ((f->x - 0.5f) * A * wdx + (f->y - 0.62f) * wdy));
		float b = cth > 0 ? cth * cth : 0;
		b = b * b;
		b = b * b; /* cos^8 */
		b = b * (0.6f + 0.4f * b); /* between cos^8 and cos^16 */
		float x = f->x * A, y = f->y;
		float vis = f->life * lum;
		float size = 0.65f + 0.75f * f->z;
		float jump = fabsf(x - f->px) + fabsf(y - f->py);
		if (b > 0.015f) {
			float v = b * vis;
			gl_sum += v;
			if (ctx->crisp) {
				us_disc(c, x, y, 0.0075f * size, 0.22f * v, US_MAT_LIGHT);
				us_disc(c, x, y, 0.0036f * size, v, US_MAT_LIGHT);
			} else {
				us_disc(c, x, y, 0.0060f * size, 0.07f * v, US_MAT_LIGHT);
				us_disc(c, x, y, 0.0028f * size, 0.30f * v, US_MAT_LIGHT);
				us_disc(c, x, y, 0.0014f * size, v, US_MAT_LIGHT);
			}
			if (jump < 0.05f)
				us_line(c, f->px, f->py, x, y, (ctx->crisp ? 0.004f : 0.0018f) * size, 0.6f * v, US_MAT_LIGHT);
			/* reflection in the pool: mirrored, compressed, broken into a shimmering streak */
			float hgt = POOL_Y - y;
			if (hgt > 0 && hgt < 0.3f) {
				float ry = POOL_Y + hgt * 0.42f + 0.004f;
				float rv = v * (ctx->crisp ? 0.3f : 0.5f) * (1.0f - hgt / 0.3f);
				float wob = 0.003f * sinf(t * 2.3f + ry * 140.0f) + 0.002f * us_noise2(x * 30.0f, t * 0.8f);
				float rl = (0.004f + 0.012f * b) * size;
				us_line(c, x + wob, ry - rl, x - wob, ry + rl, (ctx->crisp ? 0.004f : 0.0016f) * size, rv,
					US_MAT_WATER);
			}
		} else if (!ctx->crisp) {
			us_point(c, x, y, 0.0009f * size, 0.05f * vis, US_MAT_LIGHT);
		}
		f->px = x;
		f->py = y;
	}
	c->blend = US_BLEND_MAX;
	st->glow = us_approach(st->glow, us_sat(gl_sum / 30.0f), 0.12f, rdt);

	/* ================= foreground: pool, cattails, grass ================= */
	{
		/* shoreline */
		float pts[2 * 97];
		for (int i = 0; i <= 96; i++) {
			float xf = i / 96.0f;
			pts[2 * i] = xf * A;
			pts[2 * i + 1] = POOL_Y + 0.003f * us_noise2(xf * 12.0f, 0.5f);
		}
		us_polyline(c, pts, 97, ctx->crisp ? 0.003f : 0.0012f, 0.32f + 0.25f * st->glow, US_MAT_WATER);
		/* still-water ripples: slow drifting dashes, a little livelier with highs */
		for (int k = 0; k < NRIP; k++) {
			st->rip_x[k] += st->rip_sp[k] * dt * (0.5f + wind);
			if (st->rip_x[k] < -0.2f)
				st->rip_x[k] += 1.4f;
			if (st->rip_x[k] > 1.2f)
				st->rip_x[k] -= 1.4f;
			if (ctx->crisp && (k & 1))
				continue;
			float yy = st->rip_y[k] + 0.0015f * sinf(t * 0.7f + k);
			float x0 = st->rip_x[k] * A, len = st->rip_len[k] * (0.6f + 0.4f * sinf(t * 0.23f + k * 1.7f));
			float depth = (yy - POOL_Y) / (1.0f - POOL_Y);
			us_line(c, x0, yy, x0 + len, yy, ctx->crisp ? 0.003f : 0.001f, (0.10f + 0.12f * depth) * (1.0f + st->glow),
				US_MAT_WATER);
		}
		/* moon reflection */
		if (moon_v > 0.02f && moon_x > 0)
			for (int k = 0; k < 6; k++) {
				float yy = POOL_Y + 0.015f + k * 0.017f;
				float hw = (0.012f + 0.006f * k) * (0.7f + 0.3f * sinf(t * 1.1f + k * 2.0f));
				float off = 0.004f * sinf(t * 0.8f + k * 1.3f);
				us_line(c, moon_x - hw + off, yy, moon_x + hw + off, yy, ctx->crisp ? 0.003f : 0.0012f,
					0.35f * moon_v * (1.0f - k * 0.12f), US_MAT_WATER);
			}
	}

	/* cattails at the pool edge */
	for (int k = 0; k < NCAT; k++) {
		float x0 = st->cat_x[k] * A, h = st->cat_h[k];
		float sw = (us_noise2(st->cat_ph[k], st->wind_t * 0.5f) * 0.6f + 0.25f) * (0.02f + 0.1f * wind) * h;
		float tx = x0 + sw, ty = POOL_Y - h;
		us_line(c, x0, POOL_Y + 0.004f, tx, ty, ctx->crisp ? 0.004f : 0.0016f, 0.32f + 0.25f * st->glow, US_MAT_STEM);
		float hx = x0 + sw * 0.82f, hy = POOL_Y - h * 0.82f;
		us_line(c, hx, hy, x0 + sw * 0.96f, POOL_Y - h * 0.96f, ctx->crisp ? 0.009f : 0.0055f,
			0.28f + 0.2f * st->glow, US_MAT_EARTH);
		/* a leaf blade curving off */
		float lp[8];
		for (int q = 0; q < 4; q++) {
			float u = q / 3.0f;
			lp[2 * q] = x0 + (0.02f * u * u + sw * 0.4f * u) * (k & 1 ? 1 : -1);
			lp[2 * q + 1] = POOL_Y - h * 0.7f * u;
		}
		us_polyline(c, lp, 4, ctx->crisp ? 0.003f : 0.0012f, 0.3f + 0.2f * st->glow, US_MAT_LEAF);
	}

	/* grass silhouettes along the near bank */
	{
		int nb = (int)(NBLADES * (0.45f + 0.55f * dens) * (ctx->crisp ? 0.6f : 1.0f));
		float gv = (ctx->crisp ? 0.3f : 0.24f) + 0.35f * st->glow;
		for (int b = 0; b < nb; b++) {
			float xf = st->blade_x[b];
			float x0 = xf * A, h = st->blade_h[b] * (1.0f - 0.35f * winter);
			float sw = (us_noise2(x0 * 1.6f - st->wind_t * 0.7f, st->blade_ph[b]) * 0.7f + 0.35f) *
				   (0.1f + 0.5f * wind);
			float pts[10];
			for (int q = 0; q < 5; q++) {
				float u = q / 4.0f;
				pts[2 * q] = x0 + (st->blade_lean[b] + sw * 0.5f) * h * u * u;
				pts[2 * q + 1] = POOL_Y + 0.002f - h * u;
			}
			us_polyline(c, pts, 5, ctx->crisp ? 0.002f : 0.001f, gv * (0.7f + 0.3f * h / 0.18f),
				    ctx->crisp ? US_MAT_EARTH : US_MAT_LEAF);
		}
	}

}

const us_scene_vt us_scene_fireflies = {"fireflies", "Fireflies", 0.3f, create, destroy, frame};
