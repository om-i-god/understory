/* Understory scene: Wind — a prairie of grass in perspective, combed by gusts.
 *
 * Thousands of blades and seed stalks stand in depth rows (far rows small and faint, near rows tall).
 * The wind is a travelling noise field in ground-plane coordinates, so gust fronts are visible as
 * bright sheen waves sweeping across the field (bent blades catch the light). Cloud shadows slide
 * over the grass with the wind.
 * Audio: highs/air -> gust strength, centroid -> gust direction (across vs diagonally toward/away
 *        from the viewer), onsets -> a sudden gust front sweeping the field, bass -> a slow swell of
 *        the whole field. Strong gusts shake seeds/pollen loose from the seed heads.
 * World: reads growth (grass height), season (green spring, seeding summer, golden autumn, sparse
 *        snow-bent winter), daylight (sun/moon, brightness), wind (prevailing breeze); writes pollen.
 * Silence: the prevailing breeze keeps slow waves rolling through.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAX_ROWS 34
#define MAX_BLADES 3600
#define MAX_SEEDS 700
#define MAX_FRONTS 6
#define HZ 0.47f /* horizon */

typedef struct {
	float u;    /* 0..1 across the row */
	float h;    /* height jitter */
	float ph;   /* phase / hash */
	uint8_t head; /* 1 = seed stalk */
	uint8_t tint; /* 0..3 material variation */
} blade_t;

typedef struct {
	float x, y, vx, vy, life, ph, sz;
} seed_t;

typedef struct {
	float pos, amp, speed, width; /* pos along the wind axis in world units */
} front_t;

typedef struct {
	us_rng rng;
	int row_start[MAX_ROWS + 1];
	blade_t bl[MAX_BLADES];
	seed_t sd[MAX_SEEDS];
	int nsd;
	front_t fr[MAX_FRONTS];
	float flow;   /* integrated wind travel */
	float gust;   /* smoothed gust strength */
	float dir;    /* smoothed direction angle */
	float swell;  /* bass swell */
	float cloud_t;
	float seed_acc;
	float last_beat_t;
	double t;
} st_t;

static float row_s(int r)
{
	/* perspective scale of row r: 0 (far) .. 1 (near); rows bunch toward the horizon */
	float f = (r + 0.6f) / MAX_ROWS;
	return 0.04f + 0.96f * f * f * f;
}

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng *r = &s->rng;
	us_rng_seed(r, seed ^ 0x77696e64u);
	int n = 0;
	for (int row = 0; row < MAX_ROWS; row++) {
		s->row_start[row] = n;
		float sc = row_s(row);
		/* far rows hold more (smaller) blades across the same screen width */
		int cnt = (int)(105.0f - 62.0f * sc);
		for (int k = 0; k < cnt && n < MAX_BLADES; k++) {
			blade_t *b = &s->bl[n++];
			b->u = us_randr(r, -0.03f, 1.03f);
			b->h = us_randr(r, 0.55f, 1.0f);
			b->ph = us_rand(r) * 100.0f;
			b->head = us_rand(r) < 0.22f;
			b->tint = (uint8_t)(us_rand(r) * 4);
		}
	}
	s->row_start[MAX_ROWS] = n;
	s->dir = 0;
	s->cloud_t = us_rand(r) * 50;
	return s;
}

static void destroy(void *p) { free(p); }

/* Fast tapered blade: a quadratic curve rasterised as one horizontal span per pixel row
 * (blades are mostly upright; strongly bent tips still connect because each row spans from the
 * previous x to the current one). Coverage-weighted edges in ink, hard pixels in crisp.
 * (x0,y0) base, tip offset bx*h at full height h*fs, base width w (normalized). */
static void blade(us_canvas *c, float x0, float y0, float bx, float h, float fs, float w, float v, uint8_t m)
{
	float S = c->s;
	float hp = h * fs * S;
	int n = (int)ceilf(hp);
	if (n < 1)
		n = 1;
	float k = 0.3f * fabsf(bx);
	float px_prev = x0 * S;
	int last_row = (int)floorf(y0 * S);
	for (int i = 1; i <= n; i++) {
		float t = (float)i / n;
		float px = (x0 + bx * h * t * t) * S;
		float py = (y0 - h * fs * t * (1.0f - k * t)) * S;
		int iy = (int)floorf(py);
		if (iy == last_row && i < n)
			continue;
		last_row = iy;
		if ((unsigned)iy >= (unsigned)c->h) {
			px_prev = px;
			continue;
		}
		float hw = 0.5f * w * S * (1.0f - 0.75f * t);
		if (hw < 0.5f)
			hw = 0.5f;
		float a = (px < px_prev ? px : px_prev) - hw, b = (px < px_prev ? px_prev : px) + hw;
		px_prev = px;
		if (c->crisp) {
			int ia = (int)floorf(a + 0.5f), ib = (int)floorf(b - 0.5f);
			if (ib < ia)
				ib = ia;
			for (int ix = ia; ix <= ib; ix++)
				us_put(c, ix, iy, v, m);
		} else {
			int ia = (int)floorf(a), ib = (int)floorf(b);
			float vt = v * (1.0f - 0.35f * t);
			if (ia == ib) {
				us_put(c, ia, iy, vt * (b - a), m);
				continue;
			}
			us_put(c, ia, iy, vt * (ia + 1 - a), m);
			for (int ix = ia + 1; ix < ib; ix++)
				us_put(c, ix, iy, vt, m);
			us_put(c, ib, iy, vt * (b - ib), m);
		}
	}
}

/* wind bend at a ground point (world X lateral, Z depth) */
static float wind_at(const st_t *s, float X, float Z, float ax, float az)
{
	/* coordinate along the wind axis */
	float along = X * ax + Z * az;
	float across = -X * az + Z * ax;
	float g = us_noise2(along * 0.55f - s->flow, across * 0.35f + 3.7f);
	g += 0.5f * us_noise2(along * 1.4f - s->flow * 2.1f, across * 0.9f + 9.1f);
	float bend = us_smooth(-0.35f, 0.65f, g);
	for (int i = 0; i < MAX_FRONTS; i++) {
		const front_t *f = &s->fr[i];
		if (f->amp <= 0.001f)
			continue;
		float d = (along - f->pos) / f->width;
		if (d > -3.0f && d < 3.0f)
			bend += f->amp * expf(-d * d) * (d < 0 ? 1.0f : 0.6f);
	}
	return bend;
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *s = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, rdt = ctx->real_dt, A = ctx->aspect;
	float dens = ctx->p->density;
	us_rng *r = &s->rng;
	float spring = us_world_season_w(w, 0), summer = us_world_season_w(w, 1);
	float autumn = us_world_season_w(w, 2), winter = us_world_season_w(w, 3);
	float day = w->daylight;
	s->t = ctx->t;

	/* ---- wind state ---- */
	float tg = us_sat(0.25f * w->wind + 0.85f * a->high + 0.55f * a->air + 0.15f * a->level);
	s->gust = us_approach(s->gust, tg, tg > s->gust ? 0.35f : 1.5f, rdt);
	float tdir = a->active > 0.2f ? (a->centroid - 0.5f) * 2.4f : 0.35f * us_noise2((float)ctx->t * 0.02f, 1.3f);
	s->dir = us_approach(s->dir, us_clampf(tdir, -1.1f, 1.1f), 3.0f, rdt);
	s->swell = us_approach(s->swell, us_sat(a->bass * 0.7f + a->sub * 0.4f), 0.8f, rdt);
	float ax = cosf(s->dir), az = sinf(s->dir); /* ax > 0: blowing left -> right */
	float gstr = 0.12f + 0.55f * s->gust + 0.1f * winter;
	s->flow += dt * (0.25f + 1.4f * s->gust);
	s->cloud_t += dt * (0.02f + 0.06f * s->gust);

	/* onsets -> a sudden gust front sweeps in from upwind */
	if (a->beat && a->onset > 0.45f && ctx->t - s->last_beat_t > 0.8f) {
		s->last_beat_t = (float)ctx->t;
		for (int i = 0; i < MAX_FRONTS; i++)
			if (s->fr[i].amp <= 0.001f) {
				s->fr[i].amp = 0.35f + 0.65f * a->onset * (0.6f + 0.6f * s->gust);
				s->fr[i].pos = -14.0f;
				s->fr[i].speed = us_randr(r, 5.0f, 8.0f);
				s->fr[i].width = us_randr(r, 1.4f, 2.6f);
				break;
			}
	}
	for (int i = 0; i < MAX_FRONTS; i++) {
		front_t *f = &s->fr[i];
		if (f->amp <= 0.001f)
			continue;
		f->pos += f->speed * dt;
		f->amp *= expf(-dt * 0.45f);
		if (f->pos > 16.0f)
			f->amp = 0;
	}

	float dayv = 0.68f + 0.32f * us_smooth(0.0f, 0.6f, day);

	/* ---- sky: sun / moon, drifting cloud streaks ---- */
	{
		/* stars by night (stable hash positions, faint twinkle) */
		float sv = us_sat(1.0f - day * 2.0f);
		if (sv > 0.01f)
			for (int i = 0; i < 90; i++) {
				uint32_t hsh = (uint32_t)i * 2654435761u;
				float sx = ((hsh >> 8) & 1023) / 1023.0f * A;
				float sy = ((hsh >> 18) & 1023) / 1023.0f * (HZ - 0.06f);
				float tw = 0.6f + 0.4f * us_noise2(i * 1.37f, (float)ctx->t * 1.3f);
				us_point(c, sx, sy, 0.0011f, 0.45f * sv * tw * (0.3f + 0.7f * ((hsh >> 3) & 7) / 7.0f), US_MAT_LIGHT);
			}
		/* sun by day, moon (half a day out of phase) by night */
		float ph = w->day_phase;
		float cx = A * (0.1f + 0.8f * ph);
		float cy = HZ - 0.02f + cosf(ph * US_TAU) * 0.36f;
		float mph = fmodf(ph + 0.5f, 1.0f);
		float mx = A * (0.1f + 0.8f * mph), my = HZ - 0.02f + cosf(mph * US_TAU) * 0.36f;
		if (cy < HZ - 0.03f) {
			if (ctx->crisp)
				us_disc(c, cx, cy, 0.03f, 0.55f, US_MAT_LIGHT);
			else
				us_ring(c, cx, cy, 0.03f, 0.0015f, 0.55f, US_MAT_LIGHT);
		}
		if (my < HZ - 0.03f && day < 0.5f) {
			us_arc(c, mx, my, 0.022f, -1.9f, 1.9f, ctx->crisp ? 0.006f : 0.0015f, 0.5f, US_MAT_LIGHT);
			us_arc(c, mx + 0.008f, my, 0.019f, -1.6f, 1.6f, ctx->crisp ? 0.004f : 0.0012f, 0.3f, US_MAT_LIGHT);
		}
		for (int k = 0; k < 7; k++) {
			float yy = 0.07f + 0.045f * k + 0.01f * us_noise2(k * 3.3f, 1.0f);
			float len = A * (0.12f + 0.2f * (0.5f + 0.5f * us_noise2(k * 1.7f, 5.0f)));
			float x0 = fmodf(us_noise2(k * 2.1f, 2.0f) * 3.0f + s->cloud_t * (0.4f + 0.1f * k) * ax, 1.6f);
			if (x0 < 0)
				x0 += 1.6f;
			x0 = (x0 - 0.3f) * A;
			float cv = (0.16f + 0.1f * dayv) * (1.0f - 0.08f * k);
			uint8_t cm = US_MAT_BG;
			if (ctx->crisp && day > 0.3f) { /* pale clouds against the banded day sky */
				cm = US_MAT_WATER;
				cv = 0.55f + 0.3f * day;
			}
			us_line(c, x0, yy, x0 + len, yy, ctx->crisp ? 0.006f : 0.0012f, cv, cm);
			if (!ctx->crisp)
				us_line(c, x0 + len * 0.15f, yy + 0.006f, x0 + len * 0.8f, yy + 0.006f, 0.001f, cv * 0.6f, US_MAT_BG);
		}
		/* distant treeline / hills on the horizon */
		float pts[2 * 65];
		for (int i = 0; i <= 64; i++) {
			float u = i / 64.0f;
			pts[2 * i] = u * A;
			pts[2 * i + 1] = HZ - 0.012f - 0.018f * (0.5f + 0.5f * us_noise2(u * 5.0f, 8.0f)) - 0.006f * us_noise2(u * 31.0f, 2.0f);
		}
		us_polyline(c, pts, 65, 0.0013f, 0.32f * dayv, US_MAT_EARTH);
		us_line(c, 0, HZ, A, HZ, 0.0012f, 0.28f * dayv, US_MAT_EARTH);
		if (ctx->crisp)
			for (int i = 0; i < 64; i++) {
				float x0 = pts[2 * i], y0 = pts[2 * i + 1], x1 = pts[2 * i + 2], y1 = pts[2 * i + 3];
				us_tri(c, x0, y0, x1, y1, x0, HZ, 0.2f * dayv, US_MAT_EARTH);
				us_tri(c, x1, y1, x1, HZ, x0, HZ, 0.2f * dayv, US_MAT_EARTH);
			}
	}

	/* ---- pixel styles by day: banded pale sky (ink keeps its black ground) ---- */
	if (ctx->crisp && day > 0.2f) {
		float f[2 * 6];
		for (int j = 0; j < 6; j++)
			f[2 * j] = f[2 * j + 1] = (0.08f + 0.3f * j / 5.0f) * us_smooth(0.2f, 0.7f, day);
		int keep_blend = c->blend;
		us_field(c, f, 2, 6, 1.0f, US_MAT_WATER, 0, 0, A, HZ);
		c->blend = keep_blend;
	}

	/* ---- ground fill (pixel styles): dark meadow / snow ---- */
	if (ctx->crisp) {
		float gv = (0.12f + 0.06f * winter) * dayv;
		us_tri(c, 0, HZ, A, HZ, 0, 1.01f, gv, winter > 0.5f ? US_MAT_WATER : US_MAT_EARTH);
		us_tri(c, A, HZ, A, 1.01f, 0, 1.01f, gv, winter > 0.5f ? US_MAT_WATER : US_MAT_EARTH);
	}

	/* ---- grass ---- */
	float height = (0.30f + 0.95f * w->growth) * (1.0f - 0.45f * winter) * (1.0f + 0.12f * s->swell);
	float keep = (0.3f + 0.7f * dens) * (1.0f - 0.55f * winter);
	int rows_on = MAX_ROWS;
	float lean0 = 0.15f * winter; /* snow-bent */
	float shake = 0;
	for (int row = 0; row < rows_on; row++) {
		float sc = row_s(row);
		float y = HZ + (1.03f - HZ) * sc;
		float Z = 1.0f / sc;
		int b0 = s->row_start[row], b1 = s->row_start[row + 1];
		int nb = (int)((b1 - b0) * keep);
		float H = 0.42f * sc * height;
		float wid = ctx->crisp ? (sc > 0.55f ? 0.012f : 0.0f) : 0.001f + 0.0022f * sc;
		float vrow = (0.3f + 0.65f * sqrtf(sc)) * dayv;
		for (int k = 0; k < nb; k++) {
			blade_t *b = &s->bl[b0 + k];
			float x = b->u * A;
			float X = (x - A * 0.5f) * Z;
			float bend = wind_at(s, X * 0.25f, Z * 0.25f, ax, az);
			float flut = 0.08f * us_noise2(b->ph, (float)ctx->t * (1.5f + 3.0f * s->gust));
			float bb = (lean0 + gstr * bend + flut) * (ax >= 0 ? 1.0f : -1.0f) * (0.35f + 0.65f * fabsf(ax) + 0.2f);
			float h = H * b->h * (b->head ? 1.25f : 1.0f);
			/* blowing toward/away from the viewer foreshortens the blades */
			float fs = 1.0f - 0.35f * fabsf(az) * us_sat(bend);
			float shadow = 0.62f + 0.38f * us_smooth(-0.3f, 0.3f, us_noise2(X * 0.05f - s->cloud_t * 2.0f * ax, Z * 0.08f - s->cloud_t * 2.0f * az));
			float sheen = 0.75f + 0.45f * us_sat(bend);
			float v = us_sat(vrow * shadow * sheen * (0.75f + 0.25f * b->h));
			uint8_t m = US_MAT_LEAF;
			if (autumn > 0.5f)
				m = b->tint < 2 ? US_MAT_LIGHT : (b->tint == 2 ? US_MAT_EARTH : US_MAT_STEM); /* sun-gold, dry, straw */
			else if (winter > 0.5f)
				m = b->tint < 3 ? US_MAT_EARTH : US_MAT_STEM;
			else if (summer > 0.5f && b->tint == 0)
				m = US_MAT_STEM;
			float pts[8];
			float bx = bb * 0.9f;
			for (int q = 0; q < 4; q++) {
				float t = q / 3.0f;
				pts[2 * q] = x + bx * h * t * t;
				pts[2 * q + 1] = y - h * fs * t * (1.0f - 0.3f * fabsf(bx) * t);
			}
			blade(c, x, y, bx, h, fs, wid, v, m);
			float tx = pts[6], ty = pts[7];
			if (winter > 0.4f && b->tint == 3) /* snow caught on the tips */
				us_point(c, tx, ty, 0.0015f + 0.003f * sc, 0.6f * winter * dayv, US_MAT_WATER);
			if (b->head && winter < 0.6f && sc > 0.12f) {
				/* seed head: a short thick spike along the tip direction */
				float dx = pts[6] - pts[4], dy = pts[7] - pts[5];
				float dl = sqrtf(dx * dx + dy * dy) + 1e-6f;
				float hl = (0.012f + 0.018f * (summer + autumn)) * sc * height;
				float hx = tx + dx / dl * hl, hy = ty + dy / dl * hl;
				uint8_t hm = spring > 0.5f ? US_MAT_LEAF : (autumn > 0.5f ? US_MAT_BLOOM : US_MAT_ACCENT);
				float hv = us_sat(v * 1.25f + 0.1f);
				if (ctx->crisp)
					us_line(c, tx, ty, hx, hy, sc > 0.6f ? 0.0045f : 0.001f, hv, hm);
				else {
					/* ink: a slender spike with two fine awns */
					us_line(c, tx, ty, hx, hy, 0.0012f + 0.0016f * sc, hv, hm);
					float nx = -dy / dl * hl * 0.35f, ny = dx / dl * hl * 0.35f;
					float mx = (tx + hx) * 0.5f, my = (ty + hy) * 0.5f;
					us_line(c, mx, my, hx + nx, hy + ny, 0.0008f, hv * 0.6f, hm);
					us_line(c, mx, my, hx - nx, hy - ny, 0.0008f, hv * 0.6f, hm);
				}
				/* strong gusts shake seeds loose */
				float force = bend * gstr;
				if (force > 0.35f && s->nsd < MAX_SEEDS &&
				    us_rand(r) < dt * (force - 0.3f) * 3.0f * (0.4f + summer + autumn) * (0.5f + w->growth)) {
					seed_t *q = &s->sd[s->nsd++];
					q->x = hx;
					q->y = hy;
					q->vx = (ax >= 0 ? 1.0f : -1.0f) * us_randr(r, 0.05f, 0.18f) * (0.5f + s->gust);
					q->vy = -us_randr(r, 0.01f, 0.05f) * (0.3f + sc);
					q->life = 1.0f;
					q->ph = us_rand(r) * 10;
					q->sz = 0.0008f + 0.0018f * sc;
					shake += 1.0f;
				}
			}
		}
	}

	/* ---- seeds / pollen adrift ---- */
	s->seed_acc += shake;
	if (shake > 0)
		us_world_add(w, &w->pollen, 0.0006f * shake, 1.0f);
	for (int i = 0; i < s->nsd;) {
		seed_t *q = &s->sd[i];
		float dirx = ax >= 0 ? 1.0f : -1.0f;
		float lift = us_noise2(q->x * 4.0f + q->ph, (float)ctx->t * 0.7f);
		q->vx += (dirx * (0.06f + 0.25f * s->gust) - q->vx) * dt * 1.2f;
		q->vy += (-0.01f + 0.04f * lift - q->vy * 0.8f) * dt;
		q->x += q->vx * dt;
		q->y += q->vy * dt + 0.01f * sinf((float)ctx->t * 3.0f + q->ph) * dt;
		q->life -= dt * 0.2f;
		if (q->life <= 0 || q->x < -0.05f || q->x > A + 0.05f || q->y > 1.02f || q->y < 0.0f) {
			*q = s->sd[--s->nsd];
			continue;
		}
		float v = us_sat(q->life * 1.4f) * 0.75f * dayv + 0.15f;
		us_point(c, q->x, q->y, q->sz, v, US_MAT_ACCENT);
		if (!ctx->crisp && q->sz > 0.0016f) /* thistledown: a tiny tuft */
			us_line(c, q->x, q->y, q->x - q->vx * 0.04f, q->y - q->sz * 2.5f, 0.0008f, v * 0.5f, US_MAT_ACCENT);
		i++;
	}
}

const us_scene_vt us_scene_wind = {"wind", "Wind", 0.1f, create, destroy, frame};
