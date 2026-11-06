/* Understory scene: Deep Sea — the midnight zone, lit only by the animals that live in it.
 *
 * Far below the reach of the sun, moon jellies hang in black water. Each bell is a surface of revolution
 * seen a little from the side: a domed silhouette with a bright margin ring, faint radial canals and the
 * four horseshoe gonads of an Aurelia; it contracts (narrower, taller, the margin pulling in) and the jet
 * pushes it up along its axis, then it relaxes and sinks a little. Its long tentacles are follow-the-leader
 * chains that trail behind each surge and settle back; four frilled oral arms hang from the centre.
 * Around them: a siphonophore chain (float, swimming bells, then a long stem of bracts with fishing
 * filaments) curving through the upper water, comb jellies whose comb rows run with travelling rainbows,
 * a cloud of dinoflagellate plankton that is invisible until disturbed, marine snow falling through it all,
 * an anglerfish far down whose lure is the only thing that reveals it, and on the floor a black-smoker vent
 * with its plume and a colony of tube worms. The light the animals make spills into the water as a low-res
 * haze, and the snow, the plume and the sea floor are lit by it.
 * Audio: kick  — every bell contracts at once and the jellies surge upward together; their bells flash
 *                and a wave of glow runs down every tentacle;
 *        snare — a shockwave of bioluminescence: a ring of sparks bursts from a point and every plankton
 *                cell the front passes flashes (a ring of light spreading through the dark);
 *        hats  — the comb jellies' rainbows ripple and flare, and a fresh scatter of marine snow sparkles;
 *        loudness — how much of the abyss lights up (bells, haze, plankton glow, snow);
 *        bands[] — each jelly listens to its own band; the siphonophore's stem carries the spectrum along it;
 *        swell — the haze and the plankton's resting glow; sub — the vent mouth glows and the plume billows;
 *        mid/centroid — the anglerfish lure; air — snow twinkle; width — the current that sways everything.
 * World: daylight (by day a faint blue from far above, at night only the animals), pollen + bloom (fallout
 *        from the world above: heavier marine snow), winter (more snow), spring/summer + reef (more
 *        plankton and comb jellies), water_level (current strength), calm (the jellies glow more).
 * Silence: the bells keep their own slow pulse, tentacles sway, snow falls, the lure bobs, the plume rises.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAXJ 7
#define MAXT 16 /* tentacles per jelly */
#define TP 18   /* points per tentacle */
#define NARM 4
#define AP 10 /* points per oral arm */
#define MAXC 3
#define MAXSN 1000
#define MAXPL 1500
#define MAXSP 320
#define MAXW 4
#define HW 80
#define HH 45
#define SIPH_N 56

#define TILT 0.30f

typedef struct {
	float x[TP], y[TP], px[TP], py[TP];
} chain_t;

typedef struct {
	float x, y, vx, vy; /* apex of the bell */
	float th, R, z;     /* axis tilt, radius, depth 0 far .. 1 near */
	float age, per, amp; /* pulse: seconds since contraction began, natural period, strength */
	float c;            /* contraction 0..1 */
	float glow, wave, wave_a; /* kick glow envelope; age + strength of the glow wave running down the tentacles */
	float hx, hy, seed, band_k;
	int band, nt;
	float tl[MAXT];     /* tentacle segment length */
	chain_t t[MAXT];
	chain_t arm[NARM];
	int init;
} jelly_t;

typedef struct {
	float x, y, vx, vy, h, rot, rotv, ph, seed, z;
	int tent;
} comb_t;

typedef struct {
	float x, y, z, s;
} snow_t;

typedef struct {
	float x, y, ph, fl;
} plank_t;

typedef struct {
	float x, y, vx, vy, age, life, v;
} spark_t;

typedef struct {
	float x, y, age, str, rmax;
} wave_t;

typedef struct {
	us_rng rng;
	jelly_t j[MAXJ];
	int order[MAXJ];
	comb_t cb[MAXC];
	snow_t sn[MAXSN];
	plank_t pl[MAXPL];
	spark_t sp[MAXSP];
	int sp_i;
	wave_t wv[MAXW];
	int wv_i;
	float T;
	float lvl, bass_env, sub_env, mid_env, air_env, swell_env, cur;
	float bandv[US_NBANDS];
	float hat_env, comb_ph, kick_cool, snare_cool;
	uint32_t hat_seed;
	/* anglerfish */
	float an_x, an_y, an_dir, an_ph, an_turn, lure;
	/* siphonophore */
	float si_ph, si_seed;
	float vent_x;
	float haze[HH * HW];
	int inited;
} st_t;

static float hash1(int k, float seed)
{
	uint32_t h = (uint32_t)k * 2654435761u ^ (uint32_t)(seed * 1000.0f) * 40503u;
	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return (h & 0xffffff) / 16777216.0f;
}

/* a Wu hairline (two pixels per step, MAX blend): much cheaper than us_line for the many fine lines */
static void wu(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
	float x0 = ax * c->s - 0.5f, y0 = ay * c->s - 0.5f, x1 = bx * c->s - 0.5f, y1 = by * c->s - 0.5f;
	if (!(x0 == x0) || !(y0 == y0) || !(x1 == x1) || !(y1 == y1))
		return;
	int steep = fabsf(y1 - y0) > fabsf(x1 - x0);
	if (steep) {
		float t = x0; x0 = y0; y0 = t;
		t = x1; x1 = y1; y1 = t;
	}
	if (x0 > x1) {
		float t = x0; x0 = x1; x1 = t;
		t = y0; y0 = y1; y1 = t;
	}
	int lim = steep ? c->h : c->w;
	if (x1 < -1.0f || x0 > lim + 1.0f)
		return;
	float g = fabsf(x1 - x0) > 1e-6f ? (y1 - y0) / (x1 - x0) : 0.0f;
	int i0 = (int)floorf(x0 + 0.5f), i1 = (int)floorf(x1 + 0.5f);
	if (i0 < 0) i0 = 0;
	if (i1 > lim) i1 = lim;
	for (int i = i0; i <= i1 && i - i0 < 600; i++) {
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

/* a line that stays visible on crisp (pixel) canvases; fine ink lines go through the Wu hairline */
static void ln(us_canvas *c, int cr, float x0, float y0, float x1, float y1, float w, float v, uint8_t m)
{
	if (v < 0.008f)
		return;
	if (v > 1.0f)
		v = 1.0f;
	if (!cr && w * c->s < 1.6f) {
		float k = w * c->s;
		wu(c, x0, y0, x1, y1, v * (k < 0.6f ? 0.6f : k > 1.0f ? 1.0f : k) * (k > 1.0f ? 1.0f + 0.3f * (k - 1.0f) : 1.0f), m);
		return;
	}
	us_line(c, x0, y0, x1, y1, cr ? (w > 0.0045f ? w : 0.0045f) : w, v, m);
}

/* ink: the haze, bilinear, written straight into the freshly faded canvas (cheaper than us_field) */
static void haze_raster(const st_t *s, us_canvas *c, float A)
{
	float row[HW];
	float cpx = c->w / (float)HW; /* canvas pixels per haze cell */
	(void)A;
	for (int py = 0; py < c->h; py++) {
		float fy = (py + 0.5f) / c->s * HH - 0.5f;
		int j = (int)floorf(fy);
		float ty = fy - j;
		if (j < 0) { j = 0; ty = 0.0f; }
		if (j > HH - 2) { j = HH - 2; ty = 1.0f; }
		const float *a = s->haze + j * HW, *b = a + HW;
		for (int i = 0; i < HW; i++)
			row[i] = (a[i] + (b[i] - a[i]) * ty) * 255.0f;
		uint8_t *ink = c->ink + (size_t)py * c->w, *mat = c->mat + (size_t)py * c->w;
		for (int i = -1; i < HW; i++) {
			int ia = i < 0 ? 0 : i, ib = i + 1 > HW - 1 ? HW - 1 : i + 1;
			int px0 = (int)ceilf((i + 0.5f) * cpx - 0.5f), px1 = (int)ceilf((i + 1.5f) * cpx - 0.5f);
			if (px0 < 0) px0 = 0;
			if (px1 > c->w) px1 = c->w;
			if (px1 <= px0)
				continue;
			float va = row[ia], dv = (row[ib] - va) / cpx;
			float v = va + dv * ((px0 + 0.5f) / cpx - 0.5f - i);
			for (int px = px0; px < px1; px++, v += dv) {
				int iv = (int)(v + 0.5f);
				if (iv > 255) iv = 255;
				if (iv > ink[px]) {
					ink[px] = (uint8_t)iv;
					mat[px] = US_MAT_WATER;
				}
			}
		}
	}
}

/* soft glow: nested discs (MAX blend, so it never piles up under the trails), v at the centre */
static void halo(us_canvas *c, float x, float y, float r, float v, uint8_t m)
{
	if (v < 0.004f || r <= 0.0f)
		return;
	for (int k = 0; k < 6; k++) {
		float f = 1.0f - k / 6.0f; /* radius fraction */
		us_disc(c, x, y, r * f, v * expf(-3.0f * f * f), m);
	}
}

static void haze_blob(st_t *s, float A, float x, float y, float r, float amp)
{
	if (amp < 1e-4f || r <= 0.0f)
		return;
	float cw = A / HW, ch = 1.0f / HH;
	int i0 = (int)((x - 2.2f * r) / cw), i1 = (int)((x + 2.2f * r) / cw) + 1;
	int j0 = (int)((y - 2.2f * r) / ch), j1 = (int)((y + 2.2f * r) / ch) + 1;
	if (i0 < 0) i0 = 0;
	if (j0 < 0) j0 = 0;
	if (i1 > HW - 1) i1 = HW - 1;
	if (j1 > HH - 1) j1 = HH - 1;
	float ir2 = 1.0f / (r * r);
	for (int j = j0; j <= j1; j++) {
		float dy = (j + 0.5f) * ch - y;
		for (int i = i0; i <= i1; i++) {
			float dx = (i + 0.5f) * cw - x;
			s->haze[j * HW + i] += amp * expf(-(dx * dx + dy * dy) * ir2);
		}
	}
}

static float haze_at(const st_t *s, float A, float x, float y)
{
	float fx = x / A * HW - 0.5f, fy = y * HH - 0.5f;
	int i = (int)floorf(fx), j = (int)floorf(fy);
	float tx = fx - i, ty = fy - j;
	if (i < 0) { i = 0; tx = 0; }
	if (j < 0) { j = 0; ty = 0; }
	if (i > HW - 2) { i = HW - 2; tx = 1; }
	if (j > HH - 2) { j = HH - 2; ty = 1; }
	const float *a = s->haze + j * HW + i;
	return us_lerp(us_lerp(a[0], a[1], tx), us_lerp(a[HW], a[HW + 1], tx), ty);
}

/* pixel canvases: the haze through a 4x4 ordered dither, so the glow pools fade out in a pixel-art way
 * instead of ending in a flat blob */
static void haze_dither(const st_t *s, us_canvas *c, float A)
{
	static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
	for (int py = 0; py < c->h; py++) {
		float y = (py + 0.5f) / c->s;
		for (int px = 0; px < c->w; px++) {
			float h = haze_at(s, A, (px + 0.5f) / c->s, y);
			float v = (h - 0.045f) * 255.0f * 1.4f + (bayer[(py & 3) * 4 + (px & 3)] / 16.0f - 0.47f) * 22.0f;
			if (v < 24.0f)
				continue;
			size_t i = (size_t)py * c->w + px;
			int iv = v > 255.0f ? 255 : (int)v;
			if (iv > c->ink[i]) {
				c->ink[i] = (uint8_t)iv;
				c->mat[i] = US_MAT_WATER;
			}
		}
	}
}

static float floor_y(float u)
{
	return 0.915f + 0.03f * us_noise2(u * 2.1f, 1.7f) + 0.01f * us_noise2(u * 8.0f, 3.3f) - 0.025f * us_smooth(0.6f, 0.0f, u);
}

static float ridge_y(float u)
{
	float n = 1.0f - fabsf(us_noise2(u * 2.6f, 7.1f));
	return 0.86f - 0.05f * n * n + 0.012f * us_noise2(u * 9.0f, 5.5f);
}

/* ---- jellies ---------------------------------------------------------- */

/* project a point of the bell surface (polar phi from the apex, azimuth al, al = pi/2 faces the viewer) */
static void bell_pt(const jelly_t *j, float W, float H, float phi, float al, float *ox, float *oy)
{
	float sp = sinf(phi);
	float lx = j->R * W * sp * cosf(al);
	float ld = j->R * H * (1.0f - cosf(phi)) + j->R * W * sp * sinf(al) * TILT;
	float cs = cosf(j->th), sn = sinf(j->th);
	/* right = (cos, sin), down = (-sin, cos) */
	*ox = j->x + lx * cs - ld * sn;
	*oy = j->y + lx * sn + ld * cs;
}

static void jelly_shape(const jelly_t *j, float *W, float *H, float *PH)
{
	*W = 1.0f - 0.3f * j->c;
	*H = 0.62f + 0.22f * j->c;
	*PH = 1.72f + 0.3f * j->c;
}

static void jelly_reset_chains(jelly_t *j)
{
	float W, H, PH;
	jelly_shape(j, &W, &H, &PH);
	for (int k = 0; k < j->nt; k++) {
		float al = US_TAU * (k + 0.5f) / j->nt, ax, ay;
		bell_pt(j, W, H, PH, al, &ax, &ay);
		for (int i = 0; i < TP; i++) {
			j->t[k].x[i] = j->t[k].px[i] = ax;
			j->t[k].y[i] = j->t[k].py[i] = ay + i * j->tl[k];
		}
	}
	for (int k = 0; k < NARM; k++) {
		float ax, ay;
		bell_pt(j, W * 0.25f, H, 1.2f, US_TAU * k / NARM + 0.4f, &ax, &ay);
		for (int i = 0; i < AP; i++) {
			j->arm[k].x[i] = j->arm[k].px[i] = ax;
			j->arm[k].y[i] = j->arm[k].py[i] = ay + i * j->R * 0.16f;
		}
	}
}

static void new_jelly(st_t *s, jelly_t *j, int idx)
{
	us_rng *r = &s->rng;
	/* the first is the focal jelly: near, big, left of centre */
	static const float zs[MAXJ] = {1.0f, 0.62f, 0.5f, 0.3f, 0.18f, 0.72f, 0.1f};
	static const float hxs[MAXJ] = {0.36f, 0.74f, 0.13f, 0.58f, 0.88f, 0.95f, 0.22f};
	static const float hys[MAXJ] = {0.27f, 0.4f, 0.3f, 0.14f, 0.55f, 0.12f, 0.52f};
	j->z = zs[idx];
	j->R = 0.02f + 0.09f * j->z * j->z + us_randr(r, -0.004f, 0.004f);
	j->hx = hxs[idx] + us_randr(r, -0.04f, 0.04f);
	j->hy = hys[idx] + us_randr(r, -0.03f, 0.03f);
	j->x = 0;
	j->y = j->hy;
	j->vx = j->vy = 0;
	j->th = us_randr(r, -0.25f, 0.25f);
	j->per = us_randr(r, 2.2f, 3.4f);
	j->age = us_randr(r, 0, j->per);
	j->amp = 0.6f;
	j->seed = us_randr(r, 0, 100);
	j->band = 3 + (int)(us_rand(r) * 14.0f);
	j->nt = j->z > 0.8f ? MAXT : j->z > 0.45f ? 12 : 8;
	for (int k = 0; k < MAXT; k++)
		j->tl[k] = j->R * us_randr(r, 2.6f, 4.6f) / TP;
	j->init = 0;
}

static float pulse_c(float age)
{
	const float tc = 0.32f, tr = 1.1f;
	if (age < tc)
		return us_smooth(0.0f, tc, age);
	return 1.0f - us_smooth(tc, tc + tr, age);
}

static void jelly_step(st_t *s, jelly_t *j, const us_ctx *ctx, float A, float dt, float cur)
{
	if (!j->init) {
		j->x = j->hx * A;
		j->init = 1;
		jelly_reset_chains(j);
	}
	float c0 = j->c;
	j->age += dt;
	if (j->age > j->per) {
		j->age = 0;
		j->amp = 0.68f + 0.12f * us_rand(&s->rng);
		/* each natural beat sends a softer glow down the tentacles */
		j->wave = 0.0f;
		j->wave_a = 0.45f;
	}
	j->c = j->amp * pulse_c(j->age);
	j->glow = us_approach(j->glow, 0.0f, 0.4f, ctx->real_dt);
	j->wave += ctx->real_dt;
	/* thrust while contracting, along the axis; less when already high in the water */
	float dc = (j->c - c0);
	float up = us_clampf(1.0f + 6.0f * (j->y - j->hy), 0.0f, 1.4f);
	if (j->y < 0.06f + j->R)
		up = 0.0f;
	if (dc > 0.0f) {
		float imp = dc * j->R * 0.9f * up;
		j->vx += sinf(j->th) * imp;
		j->vy -= cosf(j->th) * imp;
	}
	float drag = expf(-dt * 1.5f);
	j->vx *= drag;
	j->vy *= drag;
	float sink = us_clampf(0.09f * j->R + 0.3f * (j->hy - j->y), -0.02f, 0.03f);
	j->x += (j->vx + cur * (0.4f + 0.6f * j->z)) * dt;
	j->y += (j->vy + sink) * dt;
	if (j->y > 0.86f - j->R)
		j->y = 0.86f - j->R;
	/* steer: the axis leans toward home, with a slow wander */
	float hx = (j->hx + 0.08f * us_noise2(s->T * 0.02f, j->seed)) * A;
	float tt = us_clampf(1.6f * (hx - j->x), -0.5f, 0.5f) + 0.3f * us_noise2(s->T * 0.05f, j->seed + 3.0f);
	j->th = us_approach(j->th, tt, 3.0f, dt);

	/* tentacles: verlet + follow-the-leader, trailing behind each surge */
	float W, H, PH;
	jelly_shape(j, &W, &H, &PH);
	float damp = expf(-dt * 7.0f);
	float g = 0.5f * j->R * dt * dt * 30.0f;
	float t = (float)ctx->t;
	for (int k = 0; k < j->nt; k++) {
		chain_t *ch = &j->t[k];
		float al = US_TAU * (k + 0.5f) / j->nt;
		bell_pt(j, W, H, PH, al, &ch->x[0], &ch->y[0]);
		float L = j->tl[k];
		for (int i = 1; i < TP; i++) {
			float vx = (ch->x[i] - ch->px[i]) * damp, vy = (ch->y[i] - ch->py[i]) * damp;
			ch->px[i] = ch->x[i];
			ch->py[i] = ch->y[i];
			float sway = 0.05f * j->R * sinf(i * 0.55f - t * 1.3f + k * 1.7f + j->seed);
			ch->x[i] += vx + (sway + cur * 0.15f * (float)i / TP) * dt;
			ch->y[i] += vy + g;
			/* an elastic pull toward hanging straight below the point above it */
			float hk = us_sat(1.8f * dt);
			float curl = L * 0.8f * sinf(i * 0.45f - t * 1.5f + k * 1.3f + j->seed) * (float)i / TP;
			ch->x[i] += (ch->x[i - 1] + curl - ch->x[i]) * hk * 0.5f;
			ch->y[i] += (ch->y[i - 1] + L - ch->y[i]) * hk;
		}
		for (int i = 1; i < TP; i++) {
			float dx = ch->x[i] - ch->x[i - 1], dy = ch->y[i] - ch->y[i - 1];
			float d = sqrtf(dx * dx + dy * dy);
			if (d > L) {
				/* move the point, and most of its implied velocity with it (water damps the whip) */
				float nx = ch->x[i - 1] + dx * L / d, ny = ch->y[i - 1] + dy * L / d;
				ch->px[i] += (nx - ch->x[i]) * 0.8f;
				ch->py[i] += (ny - ch->y[i]) * 0.8f;
				ch->x[i] = nx;
				ch->y[i] = ny;
			}
		}
	}
	for (int k = 0; k < NARM; k++) {
		chain_t *ch = &j->arm[k];
		bell_pt(j, W * 0.25f, H, 1.2f, US_TAU * k / NARM + 0.4f, &ch->x[0], &ch->y[0]);
		float L = j->R * (0.14f + 0.03f * (k & 1));
		for (int i = 1; i < AP; i++) {
			float vx = (ch->x[i] - ch->px[i]) * damp, vy = (ch->y[i] - ch->py[i]) * damp;
			ch->px[i] = ch->x[i];
			ch->py[i] = ch->y[i];
			ch->x[i] += vx + 0.0006f * j->R * sinf(i * 0.8f - t * 0.9f + k * 2.1f);
			ch->y[i] += vy + g * 1.4f;
			float hk = us_sat(1.8f * dt);
			ch->x[i] += (ch->x[i - 1] - ch->x[i]) * hk * 0.5f;
			ch->y[i] += (ch->y[i - 1] + L - ch->y[i]) * hk;
		}
		for (int i = 1; i < AP; i++) {
			float dx = ch->x[i] - ch->x[i - 1], dy = ch->y[i] - ch->y[i - 1];
			float d = sqrtf(dx * dx + dy * dy);
			if (d > L) {
				/* move the point, and most of its implied velocity with it (water damps the whip) */
				float nx = ch->x[i - 1] + dx * L / d, ny = ch->y[i - 1] + dy * L / d;
				ch->px[i] += (nx - ch->x[i]) * 0.8f;
				ch->py[i] += (ny - ch->y[i]) * 0.8f;
				ch->x[i] = nx;
				ch->y[i] = ny;
			}
		}
	}
}

static void jelly_draw(st_t *s, jelly_t *j, const us_ctx *ctx, us_canvas *c, float gain)
{
	int cr = ctx->crisp;
	float W, H, PH;
	jelly_shape(j, &W, &H, &PH);
	float zb = 0.5f + 0.5f * j->z;
	float bk = j->band_k;
	float B = gain * zb * (0.62f + 0.3f * bk + 0.6f * j->glow);
	float lw = (0.0011f + 0.0009f * j->z) * (1.0f + 0.4f * j->glow);
	float cx, cy;
	bell_pt(j, 0.0f, H, 1.0f, 0.0f, &cx, &cy); /* on the axis, about halfway down the bell */

	/* the bell's inner light */
	if (cr) {
		/* filled body: a fan of triangles over the silhouette + front margin */
		float px = 0, py = 0;
		const int N = 20;
		for (int i = 0; i <= N; i++) {
			float al = US_PI * i / N; /* the front half of the margin, from right to left */
			float qx, qy;
			bell_pt(j, W, H, PH, al, &qx, &qy);
			if (i > 0)
				us_tri(c, cx, cy, px, py, qx, qy, 0.2f * B + 0.06f, US_MAT_WATER);
			px = qx;
			py = qy;
		}
		for (int side = 0; side < 2; side++) {
			float al = side ? US_PI : 0.0f;
			for (int i = 0; i < 8; i++) {
				float p0 = PH * i / 8, p1 = PH * (i + 1) / 8, x0, y0, x1, y1;
				bell_pt(j, W, H, p0, al, &x0, &y0);
				bell_pt(j, W, H, p1, al, &x1, &y1);
				us_tri(c, cx, cy, x0, y0, x1, y1, 0.2f * B + 0.06f, US_MAT_WATER);
			}
		}
	} else {
		halo(c, cx, cy, j->R * (1.5f + 0.5f * j->glow), 0.16f * B, US_MAT_WATER);
	}

	/* oral arms: frilled ribbons */
	for (int k = 0; k < NARM; k++) {
		chain_t *ch = &j->arm[k];
		for (int i = 0; i + 1 < AP; i++) {
			float f = (float)i / (AP - 1);
			float v = B * (0.42f - 0.25f * f);
			if (cr) {
				if (j->z > 0.4f)
					ln(c, 1, ch->x[i], ch->y[i], ch->x[i + 1], ch->y[i + 1], 0.0045f, v, US_MAT_ACCENT);
				continue;
			}
			float dx = ch->x[i + 1] - ch->x[i], dy = ch->y[i + 1] - ch->y[i];
			float d = sqrtf(dx * dx + dy * dy) + 1e-6f;
			float nx = -dy / d, ny = dx / d;
			float w0 = j->R * 0.07f * (1.0f - 0.6f * f) * (1.0f + 0.5f * sinf(i * 2.3f + (float)ctx->t * 1.7f + k));
			float w1 = j->R * 0.07f * (1.0f - 0.6f * (f + 1.0f / (AP - 1))) *
				   (1.0f + 0.5f * sinf((i + 1) * 2.3f + (float)ctx->t * 1.7f + k));
			ln(c, 0, ch->x[i] + nx * w0, ch->y[i] + ny * w0, ch->x[i + 1] + nx * w1, ch->y[i + 1] + ny * w1, lw * 0.8f, v,
			   US_MAT_ACCENT);
			ln(c, 0, ch->x[i] - nx * w0, ch->y[i] - ny * w0, ch->x[i + 1] - nx * w1, ch->y[i + 1] - ny * w1, lw * 0.8f, v * 0.7f,
			   US_MAT_ACCENT);
		}
	}

	/* tentacles, with the kick's glow wave running down them */
	float wf = j->wave * 1.6f; /* front position along the tentacle, 0..1 */
	float wa = j->wave < 1.2f ? j->wave_a * expf(-j->wave / 0.5f) : 0.0f;
	int step = cr ? 2 : 1;
	for (int k = 0; k < j->nt; k += step) {
		chain_t *ch = &j->t[k];
		float al = US_TAU * (k + 0.5f) / j->nt;
		float front = 0.75f + 0.25f * sinf(al);
		for (int i = 0; i + 1 < TP; i++) {
			float f = (float)i / (TP - 1);
			float dd = (f - wf) * 5.0f;
			float v = B * front * (0.62f * (1.0f - f) + 0.08f) + gain * zb * 0.9f * wa * expf(-dd * dd);
			ln(c, cr, ch->x[i], ch->y[i], ch->x[i + 1], ch->y[i + 1], lw * 0.7f, v, US_MAT_WATER);
		}
	}

	/* the bell: silhouette meridians, the margin ring (front bright, back dim), canals, gonads */
	const int NM = 14;
	for (int side = 0; side < 2; side++) {
		float al = side ? US_PI : 0.0f;
		float px, py;
		bell_pt(j, W, H, 0.0f, al, &px, &py);
		for (int i = 1; i <= NM; i++) {
			float qx, qy;
			bell_pt(j, W, H, PH * i / NM, al, &qx, &qy);
			ln(c, cr, px, py, qx, qy, lw * 1.2f, B * 0.95f, US_MAT_WATER);
			px = qx;
			py = qy;
		}
	}
	const int NR = cr ? 16 : 36;
	{
		float px, py;
		bell_pt(j, W, H, PH, 0.0f, &px, &py);
		for (int i = 1; i <= NR; i++) {
			float al = US_TAU * i / NR, qx, qy;
			bell_pt(j, W, H, PH, al, &qx, &qy);
			float fr = 0.5f + 0.5f * sinf(al - US_TAU * 0.5f / NR);
			ln(c, cr, px, py, qx, qy, lw * (0.8f + 0.6f * fr), B * (0.35f + 0.7f * fr), US_MAT_WATER);
			px = qx;
			py = qy;
		}
	}
	if (!cr || j->z > 0.5f) {
		/* inner subumbrella line (a smaller dome inside) */
		float px, py;
		bell_pt(j, W * 0.84f, H * 0.95f, 0.35f, 0.0f, &px, &py);
		for (int side = 0; side < 2 && !cr; side++) {
			float al = side ? US_PI : 0.0f;
			bell_pt(j, W * 0.84f, H * 0.95f, 0.35f, al, &px, &py);
			for (int i = 1; i <= 8; i++) {
				float qx, qy;
				bell_pt(j, W * 0.84f, H * 0.95f, 0.35f + (PH - 0.45f) * i / 8, al, &qx, &qy);
				qy += j->R * 0.06f;
				ln(c, 0, px, py, qx, qy, lw * 0.6f, B * 0.25f, US_MAT_WATER);
				px = qx;
				py = qy;
			}
		}
		/* radial canals */
		if (!cr)
			for (int m = 0; m < 8; m++) {
				float al = US_TAU * (m + 0.5f) / 8 + j->seed + s->T * 0.06f;
				float fr = 0.5f + 0.5f * sinf(al);
				bell_pt(j, W, H, 0.12f, al, &px, &py);
				for (int i = 1; i <= 6; i++) {
					float qx, qy;
					bell_pt(j, W, H, 0.12f + (PH - 0.2f) * i / 6, al, &qx, &qy);
					ln(c, 0, px, py, qx, qy, lw * 0.5f, B * (0.1f + 0.2f * fr), US_MAT_WATER);
					px = qx;
					py = qy;
				}
			}
		/* four horseshoe gonads */
		for (int m = 0; m < 4; m++) {
			float al = US_TAU * m / 4 + j->seed + s->T * 0.06f; /* the bell turns slowly */
			float fr = 0.5f + 0.5f * sinf(al);
			float gx, gy;
			bell_pt(j, W * 0.92f, H, 0.82f, al, &gx, &gy);
			float rx = j->R * W * 0.15f, ry = rx * (0.4f + 0.2f * fabsf(sinf(al)));
			float ax_, ay_;
			bell_pt(j, W, H, 0.0f, 0.0f, &ax_, &ay_);
			float gap = atan2f((ay_ - gy) / ry, (ax_ - gx) / rx); /* the horseshoe opens toward the bell's crown */
			float gv = B * (0.3f + 0.3f * fr) * (1.0f + 0.4f * j->glow);
			if (cr) {
				us_disc(c, gx, gy, j->R * 0.09f + 0.002f, gv, US_MAT_BLOOM);
				continue;
			}
			float qx = 0, qy = 0;
			for (int i = 0; i <= 12; i++) {
				float a = gap + 0.5f + (US_TAU - 1.0f) * i / 12.0f;
				float x = gx + rx * cosf(a), y = gy + ry * sinf(a);
				if (i > 0)
					ln(c, 0, qx, qy, x, y, lw * 1.1f, gv, US_MAT_BLOOM);
				qx = x;
				qy = y;
			}
		}
	}
	/* bulbs at the margin where each tentacle springs */
	if (!cr)
		for (int k = 0; k < j->nt; k++) {
			float al = US_TAU * (k + 0.5f) / j->nt;
			float fr = 0.5f + 0.5f * sinf(al);
			us_point(c, j->t[k].x[0], j->t[k].y[0], lw * 1.4f, B * (0.3f + 0.6f * fr), US_MAT_WATER);
		}
	(void)s;
}

/* ---- comb jellies ------------------------------------------------------ */

static const uint8_t RAINBOW[5] = {US_MAT_WATER, US_MAT_LEAF, US_MAT_LIGHT, US_MAT_BLOOM, US_MAT_ACCENT};

static void comb_draw(st_t *s, comb_t *cb, const us_ctx *ctx, us_canvas *c, float gain, float hat)
{
	int cr = ctx->crisp;
	float A = ctx->aspect;
	float x = cb->x * A, y = cb->y, h = cb->h, a = h * 0.36f;
	float cs = cosf(cb->rot), sn = sinf(cb->rot);
	float zb = 0.55f + 0.45f * cb->z;
	float B = gain * zb;
#define CPT(lx, ly, ox, oy)                                     \
	do {                                                        \
		float _lx = (lx), _ly = (ly);                           \
		*(ox) = x + _lx * cs - _ly * sn;                        \
		*(oy) = y + _lx * sn + _ly * cs;                        \
	} while (0)
	/* body outline: a lobed ovoid, faint and glassy */
	float px = 0, py = 0;
	const int N = cr ? 16 : 32;
	for (int i = 0; i <= N; i++) {
		float t = US_TAU * i / N;
		float lx = a * sinf(t) * (1.0f + 0.12f * cosf(t)), ly = -0.5f * h * cosf(t);
		float qx, qy;
		CPT(lx, ly, &qx, &qy);
		if (i > 0)
			ln(c, cr, px, py, qx, qy, 0.0011f, B * 0.3f, US_MAT_ACCENT);
		px = qx;
		py = qy;
	}
	if (!cr)
		halo(c, x, y, h * 0.9f, 0.08f * B * (1.0f + 1.5f * hat), US_MAT_LIGHT);
	/* 8 comb rows; the near ones bright, the far ones dim; travelling rainbow along each */
	for (int r = 0; r < 8; r++) {
		float al = US_TAU * (r + 0.5f) / 8 + cb->ph * 0.05f;
		float fr = sinf(al);
		float vis = fr > 0 ? 0.45f + 0.55f * fr : 0.12f;
		float ca = cosf(al);
		const int NP = cr ? 6 : 12;
		for (int i = 0; i < NP; i++) {
			float t0 = 0.45f + (US_PI - 0.9f) * i / NP, t1 = t0 + (US_PI - 0.9f) / NP * 0.62f;
			float wavef = 0.5f + 0.5f * sinf(i * 1.3f - s->comb_ph * 1.0f + r * 0.6f + cb->seed);
			float v = B * vis * (0.15f + 1.0f * wavef * wavef + 1.2f * hat * wavef);
			int mi = (int)floorf(i * 0.6f - s->comb_ph * 0.35f + r * 0.5f + cb->seed);
			mi = ((mi % 5) + 5) % 5;
			float x0, y0, x1, y1;
			CPT(a * 1.02f * sinf(t0) * ca, -0.5f * h * cosf(t0), &x0, &y0);
			CPT(a * 1.02f * sinf(t1) * ca, -0.5f * h * cosf(t1), &x1, &y1);
			ln(c, cr, x0, y0, x1, y1, 0.0017f, v, RAINBOW[mi]);
		}
	}
	/* a sea gooseberry's two long tentacles, with side filaments */
	if (cb->tent && !cr) {
		for (int side = -1; side <= 1; side += 2) {
			float bx, by;
			CPT(side * a * 0.5f, 0.1f * h, &bx, &by);
			float qx = bx, qy = by;
			for (int i = 1; i <= 14; i++) {
				float f = i / 14.0f;
				float lx = side * (a * 0.5f + h * 0.4f * f) + h * 0.25f * sinf(f * 6.0f - (float)ctx->t * 0.9f + side);
				float ly = 0.1f * h + h * 2.6f * f;
				float nx, ny;
				CPT(lx, ly, &nx, &ny);
				ln(c, cr, qx, qy, nx, ny, 0.0008f, B * 0.3f * (1.0f - 0.7f * f), US_MAT_ACCENT);
				if (!cr && (i & 1))
					ln(c, 0, nx, ny, nx + side * 0.006f, ny + 0.004f, 0.0007f, B * 0.16f * (1.0f - f), US_MAT_ACCENT);
				qx = nx;
				qy = ny;
			}
		}
	}
#undef CPT
}

/* ---- anglerfish ---------------------------------------------------------- */

static void angler_draw(st_t *s, const us_ctx *ctx, us_canvas *c, float gain)
{
	int cr = ctx->crisp;
	float A = ctx->aspect;
	float d = s->an_dir;
	float x = s->an_x * A, y = s->an_y;
	float L = 0.09f;
	float lure = s->lure;
	/* the lure on its rod, bobbing ahead of the head */
	float lx = x + d * L * 0.62f + 0.006f * sinf(s->an_ph * 0.7f), ly = y - L * 0.42f + 0.005f * sinf(s->an_ph * 1.1f);
	float rv = gain * (0.1f + 0.25f * lure);
	/* body: revealed only by its own light */
	float bv = gain * (0.12f + 0.3f * lure);
	float hx = x + d * L * 0.28f, hy = y - L * 0.12f; /* top of the head */
	ln(c, cr, hx, hy, (hx + lx) * 0.5f - d * 0.004f, ly - 0.012f, 0.0009f, rv, US_MAT_ACCENT);
	ln(c, cr, (hx + lx) * 0.5f - d * 0.004f, ly - 0.012f, lx, ly, 0.0009f, rv, US_MAT_ACCENT);
	float body[2 * 13] = {
		x + d * L * 0.5f,  y - L * 0.02f, /* upper jaw tip */
		x + d * L * 0.3f,  y - L * 0.14f,
		x + d * L * 0.05f, y - L * 0.2f,
		x - d * L * 0.2f,  y - L * 0.13f,
		x - d * L * 0.36f, y - L * 0.03f,
		x - d * L * 0.5f,  y - L * 0.1f, /* tail fin top */
		x - d * L * 0.48f, y + L * 0.1f, /* tail fin bottom */
		x - d * L * 0.34f, y + L * 0.04f,
		x - d * L * 0.15f, y + L * 0.13f,
		x + d * L * 0.15f, y + L * 0.16f,
		x + d * L * 0.42f, y + L * 0.1f,
		x + d * L * 0.54f, y + L * 0.02f, /* lower jaw, jutting */
		x + d * L * 0.3f,  y + L * 0.03f, /* gape */
	};
	if (cr) {
		for (int i = 0; i + 1 < 13; i++)
			ln(c, 1, body[2 * i], body[2 * i + 1], body[2 * i + 2], body[2 * i + 3], 0.0045f, bv * 1.3f, US_MAT_ACCENT);
	} else {
		for (int i = 0; i + 1 < 13; i++)
			ln(c, 0, body[2 * i], body[2 * i + 1], body[2 * i + 2], body[2 * i + 3], 0.001f, bv, US_MAT_ACCENT);
		/* teeth: fine fangs along both jaws */
		for (int k = 0; k < 5; k++) {
			float f = k / 5.0f;
			float tx = x + d * L * (0.48f - 0.16f * f), ty = y - L * (0.02f - 0.0f * f);
			ln(c, 0, tx, ty, tx - d * 0.001f, ty + 0.007f, 0.0007f, bv * 1.2f, US_MAT_ACCENT);
			float bx2 = x + d * L * (0.52f - 0.18f * f), by2 = y + L * (0.03f + 0.06f * f);
			ln(c, 0, bx2, by2, bx2 - d * 0.001f, by2 - 0.006f, 0.0007f, bv * 1.1f, US_MAT_ACCENT);
		}
		/* eye: a small dull ring */
		us_ring(c, x + d * L * 0.25f, y - L * 0.06f, 0.0028f, 0.0008f, bv * 1.2f, US_MAT_ACCENT);
		/* pectoral fin */
		ln(c, 0, x, y + L * 0.02f, x - d * L * 0.12f, y + L * 0.1f + 0.004f * sinf(s->an_ph * 2.0f), 0.0008f, bv * 0.8f,
		   US_MAT_ACCENT);
		halo(c, lx, ly, 0.02f + 0.012f * lure, gain * (0.1f + 0.25f * lure), US_MAT_LIGHT);
	}
	us_disc(c, lx, ly, cr ? 0.006f : 0.0032f + 0.0012f * lure, us_sat(gain * (0.5f + 0.6f * lure)), US_MAT_LIGHT);
}

/* ---- siphonophore -------------------------------------------------------- */

static void siph_draw(st_t *s, const us_ctx *ctx, us_canvas *c, float gain)
{
	int cr = ctx->crisp;
	float A = ctx->aspect;
	float t = s->T;
	float hx = A * (0.62f + 0.22f * us_noise2(t * 0.006f, s->si_seed)), hy = 0.13f + 0.05f * us_noise2(t * 0.008f, s->si_seed + 4.0f);
	float ang = US_PI * 0.93f + 0.35f * us_noise2(t * 0.01f, s->si_seed + 9.0f);
	float x = hx, y = hy;
	float ds = 0.017f;
	float px[SIPH_N], py[SIPH_N];
	for (int i = 0; i < SIPH_N; i++) {
		float f = (float)i / SIPH_N;
		px[i] = x;
		py[i] = y;
		ang += 0.2f * us_noise2(f * 2.0f + t * 0.03f, s->si_seed + 2.0f) + 0.07f * sinf(f * 7.0f - s->si_ph * 0.6f);
		x += cosf(ang) * ds;
		y += sinf(ang) * ds;
	}
	/* the stem */
	for (int i = 0; i + 1 < SIPH_N; i++) {
		float f = (float)i / SIPH_N;
		ln(c, cr, px[i], py[i], px[i + 1], py[i + 1], 0.0008f, gain * (0.22f - 0.12f * f), US_MAT_STEM);
	}
	/* the float (pneumatophore) and swimming bells (nectophores) */
	us_disc(c, px[0], py[0], cr ? 0.0045f : 0.0035f, us_sat(gain * 0.8f), US_MAT_LIGHT);
	if (!cr)
		halo(c, px[0], py[0], 0.02f, gain * 0.25f, US_MAT_LIGHT);
	for (int i = 1; i < 6; i++) {
		float dx = px[i + 1] - px[i - 1], dy = py[i + 1] - py[i - 1];
		float d = sqrtf(dx * dx + dy * dy) + 1e-6f;
		float nx = -dy / d, ny = dx / d;
		float r = 0.0065f;
		for (int sd = -1; sd <= 1; sd += 2) {
			float bx = px[i] + sd * nx * r * 0.9f, by = py[i] + sd * ny * r * 0.9f;
			if (cr)
				us_disc(c, bx, by, 0.0045f, gain * 0.35f, US_MAT_WATER);
			else
				us_ring(c, bx, by, r, 0.0009f, gain * (0.45f + 0.15f * sinf(s->si_ph * 2.0f + i)), US_MAT_WATER);
		}
	}
	/* the siphosome: bracts lit by their own band (the spectrum along the stem), fishing filaments hang */
	for (int i = 6; i < SIPH_N; i++) {
		float f = (float)(i - 6) / (SIPH_N - 6);
		float bf = 3.0f + 26.0f * f;
		int bi = (int)bf;
		float bk = us_lerp(s->bandv[bi], s->bandv[bi + 1], bf - bi);
		float trav = 0.5f + 0.5f * sinf(f * 14.0f - s->si_ph * 1.7f);
		float v = gain * (1.0f - 0.5f * f) * (0.2f + 0.35f * trav * trav + 0.6f * bk);
		if (i % 2 == 0)
			us_disc(c, px[i], py[i], cr ? 0.0045f : 0.0022f + 0.0012f * bk, us_sat(v), US_MAT_LEAF);
		if (i % 4 == 1 && !cr) {
			float fl = 0.03f + 0.03f * hash1(i, s->si_seed);
			float qx = px[i], qy = py[i];
			for (int k = 1; k <= 5; k++) {
				float ff = k / 5.0f;
				float nx = px[i] + 0.008f * sinf(ff * 3.0f + s->si_ph * 0.5f + i) * ff, ny = py[i] + fl * ff;
				ln(c, 0, qx, qy, nx, ny, 0.0006f, v * 0.45f * (1.0f - ff), US_MAT_LEAF);
				qx = nx;
				qy = ny;
			}
		}
	}
}

/* ---- the sea floor, vent, plume and tube worms -------------------------- */

static void floor_draw(st_t *s, const us_ctx *ctx, us_canvas *c, float gain, float day)
{
	int cr = ctx->crisp;
	float A = ctx->aspect;
	float t = (float)ctx->t;
	/* far ridge */
	{
		float px = 0, py = ridge_y(0);
		for (int i = 1; i <= 64; i++) {
			float u = i / 64.0f, y = ridge_y(u), x = u * A;
			float lit = haze_at(s, A, x, y);
			ln(c, cr, px, py, x, y, 0.0009f, 0.08f + 0.04f * day + 0.9f * lit, US_MAT_EARTH);
			px = x;
			py = y;
		}
	}
	/* near floor: a black mass, its edge lit by the haze above */
	for (int px = 0; px < c->w; px++) {
		float u = (px + 0.5f) / c->s / A;
		int y0 = (int)ceilf(floor_y(u) * c->s);
		uint8_t fill = cr ? (uint8_t)(255 * (0.1f + 0.03f * day)) : 2;
		for (int py = y0 < 0 ? 0 : y0; py < c->h; py++) {
			size_t ii = (size_t)py * c->w + px;
			c->ink[ii] = fill;
			c->mat[ii] = US_MAT_EARTH;
		}
	}
	{
		float px = 0, py = floor_y(0);
		for (int i = 1; i <= 96; i++) {
			float u = i / 96.0f, y = floor_y(u), x = u * A;
			float lit = haze_at(s, A, x, y - 0.02f);
			ln(c, cr, px, py, x, y, 0.0013f, 0.16f + 0.05f * day + 1.4f * lit, US_MAT_EARTH);
			px = x;
			py = y;
		}
		/* sediment: a few soft ripples and pebbles on the near floor (ink) */
		if (!cr)
			for (int k = 0; k < 40; k++) {
				float u = hash1(k, 3.3f), x = u * A, y = floor_y(u) + 0.012f + 0.06f * hash1(k, 4.4f);
				float lit = haze_at(s, A, x, y - 0.04f);
				float w = 0.006f + 0.014f * hash1(k, 5.5f);
				ln(c, 0, x - w, y, x + w, y + 0.0015f * (hash1(k, 6.6f) - 0.5f), 0.0008f, 0.05f + 0.6f * lit, US_MAT_EARTH);
			}
	}
	/* the black smoker: a lumpy chimney of stacked mineral */
	float vx = s->vent_x * A, vb = floor_y(s->vent_x) + 0.004f, vt = vb - 0.2f;
	float glow = gain * (0.25f + 0.75f * s->sub_env);
	{
		float lxp = 0, lyp = 0, rxp = 0, ryp = 0;
		for (int i = 0; i <= 12; i++) {
			float f = i / 12.0f, y = vb - (vb - vt) * f;
			float w = 0.03f * (1.0f - 0.55f * f) * (1.0f + 0.25f * us_noise2(f * 7.0f, 2.0f)) + 0.006f * fabsf(us_noise2(f * 13.0f, 8.0f));
			float off = 0.006f * us_noise2(f * 3.0f, 5.0f);
			float lx = vx + off - w, rx = vx + off + w;
			if (cr && i > 0) {
				us_tri(c, lxp, lyp, rxp, ryp, lx, y, 0.12f, US_MAT_EARTH);
				us_tri(c, rxp, ryp, rx, y, lx, y, 0.12f, US_MAT_EARTH);
			} else if (!cr) {
				for (int py = (int)(y * c->s); py < (int)(lyp * c->s) + 1 && i > 0; py++)
					for (int pxx = (int)(lx * c->s); pxx <= (int)(rx * c->s); pxx++)
						if ((unsigned)pxx < (unsigned)c->w && (unsigned)py < (unsigned)c->h) {
							c->ink[(size_t)py * c->w + pxx] = 2;
							c->mat[(size_t)py * c->w + pxx] = US_MAT_EARTH;
						}
			}
			if (i > 0) {
				float lit = 0.12f + 0.5f * glow * f * f;
				ln(c, cr, lxp, lyp, lx, y, 0.0012f, lit, US_MAT_EARTH);
				ln(c, cr, rxp, ryp, rx, y, 0.0012f, lit * 0.8f, US_MAT_EARTH);
				/* mineral ledges */
				if (!cr && (i % 3 == 1))
					ln(c, 0, lx, y, lx + (rx - lx) * 0.5f, y + 0.002f, 0.0008f, lit * 0.6f, US_MAT_EARTH);
			}
			lxp = lx;
			lyp = y;
			rxp = rx;
			ryp = y;
		}
		/* the mouth: a hot glow */
		float mx = vx + 0.006f * us_noise2(3.0f, 5.0f);
		us_disc(c, mx, vt + 0.002f, cr ? 0.006f : 0.004f, us_sat(glow * 0.9f), US_MAT_BLOOM);
		if (!cr)
			halo(c, mx, vt, 0.03f, glow * 0.35f, US_MAT_BLOOM);
		/* the plume: dark smoke rising, curling, billowing with the sub */
		int nw = cr ? 0 : 9;
		for (int w = 0; w < nw; w++) {
			float sd = (w - (nw - 1) * 0.5f) / nw;
			float qx = mx, qy = vt;
			for (int k = 1; k <= 18; k++) {
				float f = k / 18.0f;
				float spread = (0.006f + 0.05f * f) * (1.0f + 0.5f * s->sub_env);
				float nx = mx + sd * spread * 1.6f + spread * 1.1f * us_noise2(f * 4.0f - t * 0.5f, w * 0.6f) +
					   s->cur * 2.0f * f;
				float ny = vt - 0.3f * f;
				float v = (0.13f + 0.1f * s->sub_env + 0.6f * glow * (1.0f - f) * (1.0f - f)) * (1.0f - 0.85f * f);
				ln(c, cr, qx, qy, nx, ny, 0.001f, v, US_MAT_WATER);
				qx = nx;
				qy = ny;
			}
		}
	}
	/* tube worms at the chimney's foot: white tubes, red plumes swaying with the bass */
	for (int k = 0; k < 14; k++) {
		float side = k & 1 ? 1.0f : -1.0f;
		float bx = vx + side * (0.03f + 0.05f * hash1(k, 1.1f)), by = floor_y(bx / A) + 0.003f;
		float hgt = 0.02f + 0.03f * hash1(k, 2.2f);
		float sw = 0.004f * sinf(t * 0.8f + k) + 0.006f * s->sub_env * sinf(t * 3.0f + k * 0.7f);
		float tx = bx + sw + side * 0.004f, ty = by - hgt;
		ln(c, cr, bx, by, tx, ty, cr ? 0.0045f : 0.0016f, 0.22f + 0.4f * glow * 0.5f, US_MAT_ACCENT);
		float pv = us_sat(0.3f + 0.5f * glow);
		if (cr)
			us_disc(c, tx, ty, 0.0045f, pv, US_MAT_BLOOM);
		else
			for (int f = 0; f < 5; f++) {
				float a = -US_PI * 0.5f + (f - 2) * 0.35f + sw * 20.0f;
				ln(c, 0, tx, ty, tx + cosf(a) * 0.006f, ty + sinf(a) * 0.006f, 0.0009f, pv, US_MAT_BLOOM);
			}
	}
}

/* sea pens on the left of the floor: feathered stalks that sway with the bass; a slow glow climbs them */
static void seapens_draw(st_t *s, const us_ctx *ctx, us_canvas *c, float gain, float bass)
{
	int cr = ctx->crisp;
	float A = ctx->aspect, t = (float)ctx->t;
	for (int k = 0; k < 7; k++) {
		float u = 0.03f + 0.3f * hash1(k, 7.7f);
		float bx = u * A, by = floor_y(u) + 0.006f;
		float hgt = 0.045f + 0.045f * hash1(k, 8.8f);
		float sw = 0.01f * sinf(t * 0.5f + k * 1.9f) + 0.012f * bass * sinf(t * 2.2f + k);
		float lit = haze_at(s, A, bx, by - hgt);
		float base = gain * (0.26f + 1.4f * lit);
		float qx = bx, qy = by;
		const int N = 10;
		for (int i = 1; i <= N; i++) {
			float f = (float)i / N;
			float x = bx + sw * f * f, y = by - hgt * f;
			float climb = 0.5f + 0.5f * sinf(f * 5.0f - t * 0.9f + k * 2.0f);
			float v = base * (0.6f + 0.7f * climb * climb);
			ln(c, cr, qx, qy, x, y, 0.0011f, v * 0.8f, US_MAT_STEM);
			if (f > 0.3f && (!cr || (i & 1))) {
				/* the feather: polyp leaves on both sides, sweeping up */
				float lw_ = 0.011f * sinf(US_PI * (f - 0.3f) / 0.7f) + 0.002f;
				for (int sd = -1; sd <= 1; sd += 2)
					ln(c, cr, x, y, x + sd * lw_, y - lw_ * 0.7f, 0.0008f, v * 0.75f, US_MAT_LEAF);
			}
			qx = x;
			qy = y;
		}
	}
}

/* near drifting flakes, out of focus: soft rings close to the eye */
static void bokeh_draw(st_t *s, const us_ctx *ctx, us_canvas *c, float gain)
{
	if (ctx->crisp)
		return;
	float A = ctx->aspect;
	int n = 6 + (int)(10 * ctx->p->density);
	for (int k = 0; k < n; k++) {
		float h1 = hash1(k, 11.0f), h2 = hash1(k, 12.0f), h3 = hash1(k, 13.0f), h4 = hash1(k, 14.0f);
		float y = h3 + s->T * (0.008f + 0.008f * h2);
		y -= floorf(y);
		float x = h1 + s->T * 0.002f * (h2 - 0.5f) + 0.02f * us_noise2(s->T * 0.05f, k * 3.0f);
		x -= floorf(x);
		float r = 0.008f + 0.016f * h4;
		if (y > floor_y(x) - 0.03f)
			continue;
		float lit = haze_at(s, A, x * A, y);
		float v = gain * (0.08f + 0.9f * lit) * (1.0f - 0.8f * fabsf(y - 0.5f) * 2.0f * 0.5f);
		us_disc(c, x * A, y, r, v * 0.35f, US_MAT_ACCENT);
		us_ring(c, x * A, y, r, 0.0012f, v * 0.6f, US_MAT_ACCENT);
	}
}

/* ---- create / destroy ----------------------------------------------------- */

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng *r = &s->rng;
	us_rng_seed(r, seed ^ 0xab755eaau);
	for (int i = 0; i < MAXJ; i++)
		new_jelly(s, &s->j[i], i);
	for (int i = 0; i < MAXC; i++) {
		comb_t *cb = &s->cb[i];
		static const float cx[MAXC] = {0.58f, 0.12f, 0.8f}, cy[MAXC] = {0.58f, 0.6f, 0.66f}, cz[MAXC] = {0.8f, 0.5f, 0.35f};
		cb->x = cx[i] + us_randr(r, -0.03f, 0.03f);
		cb->y = cy[i] + us_randr(r, -0.03f, 0.03f);
		cb->z = cz[i];
		cb->h = 0.04f + 0.05f * cb->z;
		cb->rot = us_randr(r, -0.4f, 0.4f);
		cb->seed = us_randr(r, 0, 10);
		cb->tent = i == 1;
	}
	for (int i = 0; i < MAXSN; i++) {
		snow_t *n = &s->sn[i];
		n->x = us_rand(r);
		n->y = us_rand(r);
		float z = us_rand(r);
		n->z = z * z;
		n->s = us_rand(r);
	}
	for (int i = 0; i < MAXPL; i++) {
		plank_t *p = &s->pl[i];
		p->x = us_rand(r);
		p->y = us_randr(r, 0.04f, 0.9f);
		p->ph = us_rand(r) * 50.0f;
	}
	for (int i = 0; i < MAXW; i++)
		s->wv[i].age = 99;
	for (int i = 0; i < MAXSP; i++)
		s->sp[i].age = s->sp[i].life = 1;
	s->an_x = us_randr(r, 0.14f, 0.3f);
	s->an_y = 0.76f;
	s->an_dir = 1.0f;
	s->an_turn = us_randr(r, 30, 60);
	s->vent_x = us_randr(r, 0.78f, 0.86f);
	s->si_seed = us_randr(r, 0, 50);
	s->T = us_rand(r) * 100;
	return s;
}

static void destroy(void *p) { free(p); }

/* ---- frame ------------------------------------------------------------ */

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *s = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, rdt = ctx->real_dt, A = ctx->aspect;
	if (dt > 0.1f) dt = 0.1f;
	if (rdt > 0.1f) rdt = 0.1f;
	float dens = ctx->p->density;
	int cr = ctx->crisp;
	us_rng *r = &s->rng;
	float day = w->daylight;
	float winter = us_world_winter(w), spring = us_world_season_w(w, 0), summer = us_world_season_w(w, 1);
	float hat = us_sat(a->hat);

	/* ---- envelopes ---- */
	s->lvl = us_approach(s->lvl, us_sat(a->level), a->level > s->lvl ? 0.12f : 0.6f, rdt);
	s->sub_env = us_approach(s->sub_env, a->sub, a->sub > s->sub_env ? 0.08f : 0.5f, rdt);
	s->mid_env = us_approach(s->mid_env, us_sat(0.6f * a->mid + 0.5f * a->centroid * a->active), 0.3f, rdt);
	s->air_env = us_approach(s->air_env, a->air, 0.25f, rdt);
	s->swell_env = us_approach(s->swell_env, a->swell, 1.0f, rdt);
	s->hat_env = us_approach(s->hat_env, hat, hat > s->hat_env ? 0.01f : 0.12f, rdt);
	for (int i = 0; i < US_NBANDS; i++) {
		float bv = a->bands[i];
		s->bandv[i] = us_approach(s->bandv[i], bv, bv > s->bandv[i] ? 0.06f : 0.4f, rdt);
	}
	float curT = (0.006f + 0.01f * w->water_level) * us_noise2(s->T * 0.02f, 2.0f) + 0.008f * (a->width - 0.3f) * sinf(s->T * 0.3f);
	s->cur = us_approach(s->cur, curT, 2.0f, rdt);
	s->T += dt;
	s->si_ph += dt * (0.5f + 0.4f * s->lvl);
	s->comb_ph += dt * (2.2f + 2.0f * s->air_env) + (a->hat_hit ? 1.2f : 0.0f);
	if (a->hat_hit)
		s->hat_seed = s->hat_seed * 1664525u + 1013904223u;
	float quiet = 1.0f - us_sat(1.6f * s->lvl);
	float breath = 0.5f + 0.5f * sinf((float)ctx->t * (US_TAU / 8.0f));
	/* how much of the abyss is lit */
	float gain = 0.78f + 0.5f * s->lvl + 0.12f * quiet * breath + 0.08f * w->calm;

	/* ---- jellies ---- */
	int nj = 3 + (int)(4.0f * dens + 0.5f);
	if (nj > MAXJ) nj = MAXJ;
	s->kick_cool -= rdt;
	int kick_now = a->kick_hit && s->kick_cool <= 0;
	if (kick_now)
		s->kick_cool = 0.12f;
	for (int i = 0; i < nj; i++) {
		jelly_t *j = &s->j[i];
		if (kick_now) {
			/* every bell contracts together (a bell mid-stroke still flashes) */
			if (j->age > 0.4f) {
				j->age = 0.0f;
				j->amp = 1.0f;
			}
			j->glow = 1.0f;
			j->wave = 0.0f;
			j->wave_a = 1.0f;
		}
		float bk = us_lerp(s->bandv[j->band], s->bandv[j->band + 1], 0.5f);
		j->band_k = us_approach(j->band_k, bk, 0.1f, rdt);
		jelly_step(s, j, ctx, A, dt, s->cur);
	}

	/* ---- snare: a shockwave of bioluminescence ---- */
	s->snare_cool -= rdt;
	if (a->snare_hit && s->snare_cool <= 0) {
		s->snare_cool = 0.12f;
		wave_t *wv = &s->wv[s->wv_i];
		s->wv_i = (s->wv_i + 1) % MAXW;
		wv->x = us_randr(r, 0.2f, 0.8f) * A;
		wv->y = us_randr(r, 0.3f, 0.7f);
		wv->age = 0;
		wv->str = 0.7f + 0.3f * us_sat(a->snare);
		wv->rmax = us_randr(r, 0.36f, 0.5f);
		int ns = 110 + (int)(70 * dens);
		for (int k = 0; k < ns; k++) {
			spark_t *p = &s->sp[s->sp_i];
			s->sp_i = (s->sp_i + 1) % MAXSP;
			float an = US_TAU * (k + us_rand(r) * 0.6f) / ns;
			float sp0 = wv->rmax / 0.28f * us_randr(r, 1.2f, 1.7f);
			p->x = wv->x + cosf(an) * 0.012f;
			p->y = wv->y + sinf(an) * 0.012f;
			p->vx = cosf(an) * sp0;
			p->vy = sinf(an) * sp0;
			p->age = 0;
			p->life = us_randr(r, 0.6f, 1.3f);
			p->v = us_randr(r, 0.8f, 1.2f) * wv->str;
		}
	}
	for (int i = 0; i < MAXW; i++)
		s->wv[i].age += rdt;

	/* ---- the haze: the water lit by the animals' light ---- */
	{
		float base = 0.018f + 0.03f * s->lvl + 0.03f * s->swell_env;
		for (int jy = 0; jy < HH; jy++) {
			float y = (jy + 0.5f) / HH;
			float top = (0.02f + 0.07f * day) * (1.0f - us_smooth(0.0f, 0.55f, y)); /* faint light from far above */
			float v = base * (0.6f + 0.4f * y) + top;
			for (int i = 0; i < HW; i++)
				s->haze[jy * HW + i] = v;
		}
		for (int i = 0; i < nj; i++) {
			jelly_t *j = &s->j[i];
			float zb = 0.5f + 0.5f * j->z;
			haze_blob(s, A, j->x, j->y + j->R * 0.9f, j->R * 2.6f,
				  zb * (0.03f + 0.04f * s->lvl + 0.03f * j->band_k + 0.09f * j->glow));
		}
		float lx = s->an_x * A + s->an_dir * 0.045f, ly = s->an_y - 0.03f;
		haze_blob(s, A, lx, ly, 0.05f, 0.05f + 0.07f * s->lure);
		haze_blob(s, A, s->vent_x * A, floor_y(s->vent_x) - 0.15f, 0.05f, 0.03f + 0.07f * s->sub_env);
		for (int i = 0; i < MAXW; i++) {
			wave_t *wv = &s->wv[i];
			if (wv->age < 1.0f)
				haze_blob(s, A, wv->x, wv->y, 0.08f + 0.8f * wv->rmax * (1.0f - expf(-wv->age / 0.2f)), 0.16f * wv->str * expf(-wv->age / 0.4f));
		}
		if (cr)
			haze_dither(s, c, A);
		else
			haze_raster(s, c, A);
	}

	/* ---- marine snow (far half first) ---- */
	float snowk = us_sat(0.35f + 0.35f * dens + 0.3f * (w->pollen + w->bloom) * 0.5f + 0.25f * winter);
	int nsn = (int)(MAXSN * snowk);
	float hs = (float)(s->hat_seed & 0xffff);
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < nsn; i++) {
			snow_t *n = &s->sn[i];
			if ((n->z > 0.45f) != pass)
				continue;
			float fall = (0.01f + 0.026f * n->z) * (1.0f + 0.3f * winter);
			n->y += fall * dt;
			n->x += (s->cur * (0.5f + n->z) + 0.002f * us_noise2(n->s * 30.0f, s->T * 0.1f)) * dt / A;
			if (n->y > 1.02f) {
				n->y -= 1.04f;
				n->x = us_rand(r);
			}
			if (n->x < 0) n->x += 1;
			if (n->x > 1) n->x -= 1;
			float x = n->x * A;
			float lit = haze_at(s, A, x, n->y);
			float v = (0.08f + 0.19f * n->z) + 2.4f * lit * (0.4f + 0.6f * n->z);
			v *= 0.85f + 0.3f * s->air_env * us_noise2(n->s * 50.0f, s->T * 2.0f);
			if (hat > 0.02f && hash1(i, hs) < 0.16f)
				v += 1.3f * hat * (0.4f + 0.6f * n->z);
			if (cr && (v < 0.22f || (i % 3 && hat < 0.05f)))
				continue;
			float rad = 0.0008f + 0.0018f * n->z;
			us_point(c, x, n->y, cr ? 0.003f : rad, v, US_MAT_ACCENT);
			if (pass == 1 && !cr && n->z > 0.7f) /* near flakes: a soft streak of fall */
				ln(c, 0, x, n->y, x, n->y - fall * 0.25f, rad, v * 0.4f, US_MAT_ACCENT);
		}

	/* ---- the sea floor, vent, plume, tube worms ---- */
	floor_draw(s, ctx, c, gain, day);
	s->bass_env = us_approach(s->bass_env, a->bass, 0.3f, rdt);
	seapens_draw(s, ctx, c, gain, s->bass_env);

	/* ---- the anglerfish ---- */
	{
		s->an_ph += dt;
		s->an_x += s->an_dir * 0.004f * dt / A;
		s->an_turn -= dt;
		if (s->an_turn <= 0 || s->an_x < 0.06f || s->an_x > 0.55f) {
			s->an_dir = s->an_x < 0.06f ? 1.0f : s->an_x > 0.55f ? -1.0f : -s->an_dir;
			s->an_turn = us_randr(r, 30, 70);
		}
		s->an_y = 0.74f + 0.012f * us_noise2(s->T * 0.05f, 7.0f);
		/* the lure: a slow luring flicker, brighter with the mids */
		float flick = 0.5f + 0.5f * sinf(s->an_ph * 1.3f) * sinf(s->an_ph * 0.37f);
		s->lure = us_approach(s->lure, us_sat(0.3f + 0.3f * flick + 0.6f * s->mid_env), 0.25f, rdt);
		angler_draw(s, ctx, c, gain);
	}

	/* ---- plankton: invisible until a shockwave front passes ---- */
	{
		float pk = us_sat(0.4f + 0.6f * dens) * (0.7f + 0.3f * (spring + summer) + 0.2f * w->reef);
		int np = (int)(MAXPL * us_sat(pk));
		float restv = 0.07f + 0.1f * s->lvl + 0.06f * s->swell_env;
		for (int i = 0; i < np; i++) {
			plank_t *p = &s->pl[i];
			p->x += s->cur * 0.3f * dt / A;
			if (p->x < 0) p->x += 1;
			if (p->x > 1) p->x -= 1;
			float x = p->x * A, y = p->y;
			for (int k = 0; k < MAXW; k++) {
				wave_t *wv = &s->wv[k];
				if (wv->age > 1.6f)
					continue;
				float rr = wv->rmax * (1.0f - expf(-wv->age / 0.2f));
				float dx = x - wv->x, dy = y - wv->y;
				float d = sqrtf(dx * dx + dy * dy);
				float e = (d - rr) / 0.035f;
				if (e > -1.0f && d < wv->rmax * 1.05f) {
					/* the front flashes hardest; the cells it has passed keep a fading glow */
					float f = e < 1.0f ? 1.2f * (1.0f - fabsf(e)) : 0.0f;
					f = wv->str * f * (1.0f - 0.5f * wv->age);
					if (f > p->fl)
						p->fl = f;
				} else if (e <= -1.0f && wv->age < 0.15f) {
					float f = 0.6f * wv->str * (1.0f - wv->age / 0.15f) * hash1((int)(p->ph * 100.0f), (float)k);
					if (f > p->fl)
						p->fl = f;
				}
			}
			p->fl = us_approach(p->fl, 0.0f, 0.35f, rdt);
			float tw = restv * (0.5f + 0.5f * us_noise2(p->ph, s->T * 1.5f));
			float v = tw + p->fl;
			if (v < 0.03f || (cr && p->fl < 0.12f))
				continue;
			us_point(c, x, y, cr ? 0.003f : 0.0009f + 0.001f * p->fl, v, US_MAT_WATER);
		}
	}

	/* ---- far jellies, siphonophore, comb jellies, near jellies ---- */
	for (int i = 0; i < nj; i++)
		s->order[i] = i;
	for (int i = 1; i < nj; i++) { /* far to near */
		int k = s->order[i], m = i - 1;
		while (m >= 0 && s->j[s->order[m]].z > s->j[k].z) {
			s->order[m + 1] = s->order[m];
			m--;
		}
		s->order[m + 1] = k;
	}
	int di = 0;
	for (; di < nj && s->j[s->order[di]].z < 0.4f; di++)
		jelly_draw(s, &s->j[s->order[di]], ctx, c, gain);
	siph_draw(s, ctx, c, gain);
	{
		int nc = 1 + (int)(2.0f * dens + 0.3f * w->reef + 0.4f);
		if (nc > MAXC) nc = MAXC;
		for (int i = 0; i < nc; i++) {
			comb_t *cb = &s->cb[i];
			cb->ph += dt;
			cb->x += (0.003f * us_noise2(s->T * 0.03f, cb->seed) + s->cur * 0.5f) * dt / A;
			cb->y += 0.004f * us_noise2(s->T * 0.04f, cb->seed + 5.0f) * dt;
			cb->x = us_clampf(cb->x, 0.05f, 0.95f);
			cb->y = us_clampf(cb->y, 0.15f, 0.75f);
			cb->rot = 0.5f * us_noise2(s->T * 0.03f, cb->seed + 9.0f);
			comb_draw(s, cb, ctx, c, gain, s->hat_env);
		}
	}
	for (; di < nj; di++)
		jelly_draw(s, &s->j[s->order[di]], ctx, c, gain);

	bokeh_draw(s, ctx, c, gain);

	/* ---- the snare's sparks, bursting outward and fading ---- */
	if (!cr) {
		for (int i = 0; i < MAXW; i++) {
			wave_t *wv = &s->wv[i];
			if (wv->age > 0.9f)
				continue;
			float rr = wv->rmax * (1.0f - expf(-wv->age / 0.2f));
			us_ring(c, wv->x, wv->y, rr, 0.0012f, 0.22f * wv->str * (1.0f - wv->age / 0.9f), US_MAT_WATER);
			if (wv->age < 0.3f)
				halo(c, wv->x, wv->y, 0.09f, 0.75f * wv->str * (1.0f - wv->age / 0.3f), US_MAT_WATER);
		}
	}
	for (int i = 0; i < MAXSP; i++) {
		spark_t *p = &s->sp[i];
		if (p->age >= p->life)
			continue;
		p->age += rdt;
		float drag = expf(-rdt / 0.22f);
		float ox = p->x, oy = p->y;
		p->vx *= drag;
		p->vy *= drag;
		p->x += p->vx * rdt * 0.42f;
		p->y += p->vy * rdt * 0.42f;
		float f = p->age / p->life;
		float v = p->v * (1.0f - f) * (0.75f + 0.25f * sinf(p->age * 40.0f + i));
		if (cr)
			us_point(c, p->x, p->y, 0.003f, v, US_MAT_WATER);
		else {
			ln(c, 0, ox, oy, p->x, p->y, 0.0012f, v * 0.7f, US_MAT_WATER);
			us_point(c, p->x, p->y, 0.002f, v, US_MAT_WATER);
		}
	}
	c->blend = US_BLEND_MAX;
}

const us_scene_vt us_scene_abyss = {"abyss", "Deep Sea", 0.04f, create, destroy, frame};
