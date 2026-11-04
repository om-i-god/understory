/* Understory scene: Coral / lichen — reaction-diffusion growth.
 *
 * A Gray-Scott reaction-diffusion runs on a low-res grid (240x135, reflecting edges) and creeps across the screen.
 * The feed/kill parameters are not global constants: a slow drifting parameter map puts different
 * regions in different regimes (branching coral, labyrinth/fingerprint, dividing spots), and the whole map
 * cycles slowly, so the reef keeps reorganising — spots split, mazes break into coral fronds, grazed
 * patches die back and are recolonised. Drawn as iso-contours (fine single lines) over a faint body,
 * with the actively growing front highlighted.
 *
 * Audio: low-mids feed growth (feed rate + iterations/frame), bass/kicks make the coral spawn — bright
 *        bundles rise from the living edge, drift with the current and settle to seed new colonies;
 *        onsets nudge the kill rate; highs/air make the plankton sparkle; level stirs the current.
 * World: reads pollen + water_level (nutrients -> feed, growth speed), reef (initial coverage);
 *        writes reef (follows the measured coverage).
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define GW 240
#define GH 135
#define CW 48 /* coarse parameter map */
#define CH 27
#define MAX_PL 420
#define MAX_SP 160

typedef struct {
	float x, y; /* x as fraction of width 0..1, y 0..1 */
	float vx, vy, ph, size;
} plank_t;

typedef struct {
	float x, y, vx, vy, life, age;
} spawn_t;

typedef struct {
	us_rng rng;
	float u[2][GW * GH], v[2][GW * GH];
	float prev[GW * GH]; /* v a little while ago: growth front */
	float fmap[CW * CH], kmap[CW * CH];
	uint8_t mmap[CW * CH]; /* "species" material per region (pixel palettes) */
	int cur, inited;
	float iter_acc, prev_t;
	float cycle; /* regime cycle phase */
	float cover, cover_s;
	float low_env, bass_env, nudge, hi_env;
	float graze_x, graze_y, graze_vx, graze_vy;
	float seed_t;
	plank_t pl[MAX_PL];
	spawn_t sp[MAX_SP];
	int nsp;
	float flow_t;
	float fill[GW * GH];
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x636f7261u);
	return s;
}

static void destroy(void *p) { free(p); }

/* drop a colony seed at grid cell (gx, gy) */
static void seed_at(st_t *st, float gx, float gy, float r)
{
	float *U = st->u[st->cur], *V = st->v[st->cur];
	int ir = (int)ceilf(r);
	for (int dy = -ir; dy <= ir; dy++)
		for (int dx = -ir; dx <= ir; dx++) {
			float d2 = (float)(dx * dx + dy * dy);
			if (d2 > r * r)
				continue;
			int x = (int)gx + dx, y = (int)gy + dy;
			if (x < 0 || y < 0 || x >= GW || y >= GH)
				continue;
			int i = y * GW + x;
			float k = 1.0f - 0.4f * d2 / (r * r);
			if (V[i] < 0.25f * k + 0.05f) {
				U[i] = 0.5f - 0.2f * us_rand(&st->rng);
				V[i] = (0.25f + 0.1f * us_rand(&st->rng)) * k;
			}
		}
}

/* regime path: coral -> labyrinth -> dividing spots -> coral (r in [0,3)) */
static void regime(float r, float *F, float *K)
{
	static const float P[4][2] = {
		{0.0545f, 0.0620f}, /* branching coral       */
		{0.0300f, 0.0565f}, /* labyrinth / fingerprint */
		{0.0367f, 0.0649f}, /* mitosis spots          */
		{0.0545f, 0.0620f},
	};
	r = fmodf(r, 3.0f);
	if (r < 0)
		r += 3.0f;
	int i = (int)r;
	float t = r - i;
	t = t * t * (3 - 2 * t);
	*F = us_lerp(P[i][0], P[i + 1][0], t);
	*K = us_lerp(P[i][1], P[i + 1][1], t);
}

static void rd_iterate(st_t *st)
{
	const float *U = st->u[st->cur], *V = st->v[st->cur];
	float *U2 = st->u[st->cur ^ 1], *V2 = st->v[st->cur ^ 1];
	for (int y = 0; y < GH; y++) {
		const float *Ur = U + y * GW, *Vr = V + y * GW;
		const float *Uu = U + (y ? y - 1 : 1) * GW, *Vu = V + (y ? y - 1 : 1) * GW;
		const float *Ud = U + (y + 1 < GH ? y + 1 : GH - 2) * GW, *Vd = V + (y + 1 < GH ? y + 1 : GH - 2) * GW;
		const float *fm = st->fmap + (y * CH / GH) * CW, *km = st->kmap + (y * CH / GH) * CW;
		for (int x = 0; x < GW; x++) {
			int xl = x ? x - 1 : 1, xr = x + 1 < GW ? x + 1 : GW - 2; /* reflecting edges */
			float u = Ur[x], v = Vr[x];
			float lu = -u + 0.2f * (Ur[xl] + Ur[xr] + Uu[x] + Ud[x]) + 0.05f * (Uu[xl] + Uu[xr] + Ud[xl] + Ud[xr]);
			float lv = -v + 0.2f * (Vr[xl] + Vr[xr] + Vu[x] + Vd[x]) + 0.05f * (Vu[xl] + Vu[xr] + Vd[xl] + Vd[xr]);
			float F = fm[x * CW / GW], K = km[x * CW / GW];
			float uvv = u * v * v;
			float nu = u + (0.55f * lu - uvv + F * (1.0f - u));
			float nv = v + (0.275f * lv + uvv - (F + K) * v);
			U2[y * GW + x] = nu < 0 ? 0 : (nu > 1 ? 1 : nu);
			V2[y * GW + x] = nv < 0 ? 0 : (nv > 1 ? 1 : nv);
		}
	}
	st->cur ^= 1;
}

static void init_scene(st_t *st, const us_ctx *ctx)
{
	us_rng *r = &st->rng;
	for (int i = 0; i < GW * GH; i++) {
		st->u[0][i] = st->u[1][i] = 1.0f;
		st->v[0][i] = st->v[1][i] = 0.0f;
	}
	st->cur = 0;
	/* memory: a reef that was well fed elsewhere opens with more colonies */
	float reef = ctx->w->reef;
	int n = 5 + (int)(reef * 40.0f);
	for (int k = 0; k < n; k++)
		seed_at(st, us_rand(r) * GW, us_rand(r) * GH, us_randr(r, 2.0f, 4.5f));
	st->cycle = us_rand(r) * 3.0f;
	st->graze_x = us_rand(r);
	st->graze_y = us_rand(r);
	float ga = us_rand(r) * US_TAU;
	st->graze_vx = cosf(ga) * 0.006f;
	st->graze_vy = sinf(ga) * 0.004f;
	for (int i = 0; i < MAX_PL; i++) {
		plank_t *p = &st->pl[i];
		p->x = us_rand(r);
		p->y = us_rand(r);
		p->ph = us_rand(r) * 50.0f;
		p->size = us_rand(r);
	}
	/* a little pre-growth so the colonies aren't bare dots when the scene opens (more for a big reef) */
	for (int i = 0; i < CW * CH; i++)
		regime(st->cycle, &st->fmap[i], &st->kmap[i]);
	int pre = 150 + (int)(reef * 900.0f);
	for (int k = 0; k < pre; k++)
		rd_iterate(st);
	memcpy(st->prev, st->v[st->cur], sizeof(st->prev));
	st->inited = 1;
}

/* cheap anti-aliased hairline (Xiaolin Wu): 2 pixels per step, no sqrt. Used for the thousands of
 * short contour segments in Ink, where us_line's general-width coverage loop is too costly. */
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
	/* partial coverage at the ends keeps joined segments seamless */
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

/* marching squares on v at level lv; growth-front segments drawn brighter */
static void contours(st_t *st, us_canvas *c, float A, float lv, float base, float hot_gain, float wdt, uint8_t mat,
		     int stride)
{
	const float *V = st->v[st->cur];
	float sx = A / GW, sy = 1.0f / GH;
	for (int y = 0; y + stride < GH; y += stride) {
		for (int x = 0; x + stride < GW; x += stride) {
			int i00 = y * GW + x, i10 = i00 + stride, i01 = i00 + stride * GW, i11 = i01 + stride;
			float a = V[i00], b = V[i10], d = V[i01], e = V[i11];
			int cs = (a > lv) | ((b > lv) << 1) | ((e > lv) << 2) | ((d > lv) << 3);
			if (cs == 0 || cs == 15)
				continue;
			/* edge crossing points: 0 top (a-b), 1 right (b-e), 2 bottom (d-e), 3 left (a-d) */
			float px[4], py[4];
			float x0 = (x + 0.5f) * sx, y0 = (y + 0.5f) * sy, x1 = (x + stride + 0.5f) * sx, y1 = (y + stride + 0.5f) * sy;
#define T(p, q) us_clampf((lv - (p)) / ((q) - (p) + 1e-9f), 0, 1)
			px[0] = us_lerp(x0, x1, T(a, b)), py[0] = y0;
			px[1] = x1, py[1] = us_lerp(y0, y1, T(b, e));
			px[2] = us_lerp(x0, x1, T(d, e)), py[2] = y1;
			px[3] = x0, py[3] = us_lerp(y0, y1, T(a, d));
#undef T
			static const int8_t segs[16][4] = {
				{-1, -1, -1, -1}, {3, 0, -1, -1}, {0, 1, -1, -1}, {3, 1, -1, -1},
				{1, 2, -1, -1},   {3, 0, 1, 2},   {0, 2, -1, -1}, {3, 2, -1, -1},
				{2, 3, -1, -1},   {2, 0, -1, -1}, {0, 1, 2, 3},   {2, 1, -1, -1},
				{1, 3, -1, -1},   {1, 0, -1, -1}, {0, 3, -1, -1}, {-1, -1, -1, -1},
			};
			/* growth front: v rising here */
			float g = (V[i00] - st->prev[i00]) + (V[i11] - st->prev[i11]);
			float hot = us_sat(g * hot_gain);
			float val = base + (1.0f - base) * hot;
			uint8_t m = hot > 0.7f ? US_MAT_BLOOM : (mat ? mat : st->mmap[(y * CH / GH) * CW + x * CW / GW]);
			for (int s = 0; s < 4 && segs[cs][s] >= 0; s += 2) {
				int p = segs[cs][s], q = segs[cs][s + 1];
				if (c->crisp)
					us_line(c, px[p], py[p], px[q], py[q], wdt, val, m);
				else
					wu_line(c, px[p], py[p], px[q], py[q], val, m);
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
	if (!st->inited)
		init_scene(st, ctx);

	st->low_env = us_approach(st->low_env, us_sat(a->lowmid * 0.8f + a->mid * 0.3f), 1.5f, ctx->real_dt);
	st->bass_env = us_approach(st->bass_env, us_sat(a->bass * 0.7f + a->kick * 0.7f), 0.1f, ctx->real_dt);
	st->hi_env = us_approach(st->hi_env, us_sat(a->high + 0.6f * a->air), 0.2f, ctx->real_dt);
	st->nudge = us_approach(st->nudge, 0.0f, 2.5f, ctx->real_dt);
	if (a->beat)
		st->nudge = us_sat(st->nudge + 0.35f * a->onset);
	float nutrients = us_sat(0.4f * w->pollen + 0.6f * w->water_level);

	/* ---- parameter map: slow regime cycle + spatial drift + grazing patch ---- */
	st->cycle += dt * (1.0f / 70.0f) * (0.6f + 0.8f * st->low_env);
	st->flow_t += dt * (0.02f + 0.05f * a->level);
	st->graze_x += st->graze_vx * dt;
	st->graze_y += st->graze_vy * dt;
	st->graze_x -= floorf(st->graze_x);
	st->graze_y -= floorf(st->graze_y);
	if (us_rand(r) < dt * 0.03f) {
		float ga = us_rand(r) * US_TAU;
		st->graze_vx = cosf(ga) * 0.007f;
		st->graze_vy = sinf(ga) * 0.005f;
	}
	/* coverage target rises with density; overgrown reefs get grazed harder */
	float target = 0.16f + 0.16f * dens + 0.06f * nutrients;
	float over = us_clampf((st->cover_s - target) * 6.0f, -1.0f, 1.5f);
	float t = (float)ctx->t;
	for (int y = 0; y < CH; y++)
		for (int x = 0; x < CW; x++) {
			float fx = (float)x / CW, fy = (float)y / CH;
			float rr = st->cycle + 1.3f * us_noise3(fx * 1.6f, fy * 1.2f, t * 0.004f);
			float F, K;
			regime(rr, &F, &K);
			F += 0.004f * st->low_env + 0.003f * nutrients - 0.002f;
			K += 0.0012f * st->nudge * us_noise2(fx * 6.0f + t * 0.1f, fy * 6.0f);
			/* grazing patch (toroidal distance) */
			float gx = fabsf(fx - st->graze_x), gy = fabsf(fy - st->graze_y);
			gx = gx > 0.5f ? 1 - gx : gx;
			gy = gy > 0.5f ? 1 - gy : gy;
			float gd = (gx * gx * 3.0f + gy * gy * 3.0f);
			float graze = expf(-gd * 40.0f) * (0.6f + 0.9f * us_sat(over));
			K += 0.006f * graze + 0.0015f * us_sat(over);
			/* habitat: slowly wandering barren ground (sand channels) keeps open water between colonies */
			float hab = us_fbm3(fx * 2.6f + 3.3f, fy * 2.0f, t * 0.006f, 2);
			K += 0.014f * us_smooth(0.0f, 0.22f, hab - 0.25f * dens + 0.12f);
			st->mmap[y * CW + x] = fmodf(rr + 30.0f, 3.0f) > 1.6f && fmodf(rr + 30.0f, 3.0f) < 2.6f ? US_MAT_ACCENT : US_MAT_LEAF;
			st->fmap[y * CW + x] = F;
			st->kmap[y * CW + x] = K;
		}

	/* ---- run the RD: slow creep; low-mids + nutrients make it faster ---- */
	float ips = 70.0f + 160.0f * st->low_env + 60.0f * nutrients;
	st->iter_acc += ips * dt;
	int iters = (int)st->iter_acc;
	if (iters > 12)
		iters = 12;
	st->iter_acc -= (float)(int)st->iter_acc;
	for (int k = 0; k < iters; k++)
		rd_iterate(st);

	/* growth-front memory (~0.7 s) */
	{
		const float *V = st->v[st->cur];
		float kk = 1.0f - expf(-dt / 0.7f);
		double sum = 0;
		for (int i = 0; i < GW * GH; i++) {
			st->prev[i] += (V[i] - st->prev[i]) * kk;
			sum += V[i] > 0.18f;
		}
		st->cover = (float)(sum / (GW * GH));
		st->cover_s = us_approach(st->cover_s, st->cover, 3.0f, dt);
	}
	/* world: reef follows coverage (slowly) */
	us_world_add(w, &w->reef, 0.02f * (us_sat(st->cover_s * 2.2f) - w->reef), dt);

	/* ---- re-seeding: never dies out ---- */
	st->seed_t -= dt;
	if (st->seed_t <= 0 || st->cover < 0.02f) {
		seed_at(st, us_rand(r) * GW, us_rand(r) * GH, us_randr(r, 2.0f, 3.5f));
		st->seed_t = us_randr(r, 4.0f, 12.0f) * (st->cover < 0.05f ? 0.2f : 1.0f);
	}

	/* ---- spawning: kicks/bass release bundles from the living edge ---- */
	if (a->beat && st->bass_env > 0.25f) {
		int nb = 1 + (int)(st->bass_env * (2.0f + 5.0f * dens));
		const float *V = st->v[st->cur];
		for (int b = 0; b < nb && st->nsp < MAX_SP; b++) {
			/* find a cell on the edge: a few random probes */
			for (int tries = 0; tries < 12; tries++) {
				int x = (int)(us_rand(r) * GW), y = (int)(us_rand(r) * GH);
				float vv = V[y * GW + x];
				if (vv > 0.12f && vv < 0.3f) {
					spawn_t *q = &st->sp[st->nsp++];
					q->x = (x + 0.5f) / GW;
					q->y = (y + 0.5f) / GH;
					float an = us_rand(r) * US_TAU;
					float spd = us_randr(r, 0.01f, 0.04f) * (0.5f + st->bass_env);
					q->vx = cosf(an) * spd / A;
					q->vy = sinf(an) * spd - 0.01f;
					q->life = us_randr(r, 6.0f, 16.0f);
					q->age = 0;
					break;
				}
			}
		}
	}

	/* ---- draw ---- */
	if (ctx->crisp) {
		/* pixel canvases are small: a banded body (rim / flesh / core) + bright edge */
		const float *V = st->v[st->cur];
		for (int i = 0; i < GW * GH; i++) {
			float vv = V[i];
			float sw = 0.05f * st->bass_env;
			st->fill[i] = vv > 0.3f - sw ? 0.62f : (vv > 0.2f - sw ? 0.42f : (vv > 0.14f - sw ? 0.26f : 0.0f));
		}
		us_field(c, st->fill, GW, GH, 0.9f + 0.25f * st->bass_env, US_MAT_LEAF, 0, 0, A, 1);
		contours(st, c, A, 0.14f - 0.05f * st->bass_env, 0.7f + 0.2f * st->bass_env, 35.0f, 0.003f, 0, 2);
	} else {
		/* ink: topographic iso-lines — growth rings of the colony */
		contours(st, c, A, 0.12f - 0.05f * st->bass_env, 0.66f + 0.2f * st->bass_env, 60.0f, 0.0013f, 0, 1);
		contours(st, c, A, 0.22f - 0.04f * st->bass_env, 0.34f, 0.0f, 0.001f, 0, 1);
		contours(st, c, A, 0.30f, 0.18f + 0.1f * st->bass_env, 0.0f, 0.001f, 0, 1);
	}

	/* ---- spawn bundles: rise, wander, settle and seed ---- */
	for (int i = 0; i < st->nsp;) {
		spawn_t *q = &st->sp[i];
		float px = q->x * A, py = q->y;
		float fx = us_noise3(px * 2.0f, py * 2.0f, st->flow_t * 3.0f);
		float fy = us_noise3(px * 2.0f + 7.1f, py * 2.0f, st->flow_t * 3.0f);
		q->vx += (fx * 0.02f / A) * dt;
		q->vy += (fy * 0.02f - 0.006f) * dt;
		q->vx *= expf(-dt * 0.6f);
		q->vy *= expf(-dt * 0.6f);
		q->x += q->vx * dt;
		q->y += q->vy * dt;
		q->x -= floorf(q->x);
		q->y -= floorf(q->y);
		q->age += dt;
		if (q->age >= q->life) {
			seed_at(st, q->x * GW, q->y * GH, us_randr(r, 1.5f, 2.5f));
			*q = st->sp[--st->nsp];
			continue;
		}
		float fade = us_sat(q->age * 2.0f) * us_sat((q->life - q->age) * 0.5f);
		float v = (0.55f + 0.45f * st->bass_env) * fade;
		us_disc(c, q->x * A, q->y, ctx->crisp ? 0.004f : 0.0028f, v, US_MAT_BLOOM);
		if (!ctx->crisp)
			us_ring(c, q->x * A, q->y, 0.006f + 0.003f * st->bass_env, 0.0008f, 0.35f * v, US_MAT_BLOOM);
		i++;
	}

	/* ---- plankton / spores drifting in the current ---- */
	{
		int n = 60 + (int)(dens * (MAX_PL - 60));
		float sparkle = st->hi_env;
		float tt = st->flow_t;
		for (int i = 0; i < n; i++) {
			plank_t *p = &st->pl[i];
			float px = p->x * A;
			float fx = us_noise3(px * 1.4f, p->y * 1.4f, tt);
			float fy = us_noise3(px * 1.4f + 31.7f, p->y * 1.4f, tt);
			float spd = 0.012f + 0.03f * a->level;
			p->x += (fx * spd + 0.004f) * dt / A;
			p->y += fy * spd * dt;
			p->x -= floorf(p->x);
			p->y -= floorf(p->y);
			p->ph += dt * (0.5f + 3.0f * sparkle);
			float tw = 0.5f + 0.5f * sinf(p->ph + p->size * 9.0f);
			float v = 0.18f + 0.22f * p->size + (0.25f + 0.35f * sparkle) * tw * tw * tw + 0.2f * a->kick * p->size;
			us_point(c, p->x * A, p->y, (ctx->crisp ? 0.002f : 0.0011f) * (0.8f + 0.6f * p->size), us_sat(v),
				 p->size > 0.85f ? US_MAT_LIGHT : US_MAT_WATER);
		}
	}
}

const us_scene_vt us_scene_coral = {"coral", "Coral", 0.22f, create, destroy, frame};
