/* Understory scene: Rain — rain over a wet meadow at eye level; snow in winter.
 *
 * Back to front: drifting rain clouds (soft in Ink, ordered-dither in Pixel), a far ridge, a treeline of
 * conifers and round-headed trees along the horizon with a farmhouse whose lit windows glow (and shine
 * back from the puddles) and whose smoke leans with the wind, a hedgerow across the middle distance, then
 * the meadow running toward us in perspective: a farm track whose wheel ruts hold rainwater, a fence whose
 * wires bead with drops that drip off, wildflowers that nod when drops strike them, and puddles. Every drop has a depth: far drops are short faint hairlines that vanish just under
 * the horizon, near drops are long bright streaks that fall the whole height of the frame. Where a drop
 * lands in a puddle it opens a ripple ring (two crests, flattened by perspective, clipped by the puddle);
 * on open ground it throws up a little crown of droplets. Sheets of heavier rain sweep across with the
 * gusts. The puddles grow and merge as the world gets wetter and shrink as it dries.
 * Winter turns the drops into slow flakes that whiten the meadow, freeze the puddles, cap the trees and
 * build a drift along the foreground that buries the grass.
 *
 * Living detail: a crow on a fence post, a frog on the lip of the near puddle, an orb web in the corner of
 * the fence beaded with rain, rainwater running down the ruts, mist lying along the horizon.
 *
 * Audio: level -> steady rain; onsets -> bursts; highs (+ world wind) -> wind slant + gusts;
 *        bass -> heavier, faster near drops; kick -> a gust that throws the rain, grass and flowers sideways
 *        and a heavy drop (crown + jet) into the near puddle; beats -> the crow shuffles its wings, the
 *        first hit after a lull startles it into a loop over the meadow; hat bands 24..29 -> crowns of
 *        spray off the near fence posts (one band per post); bands 10..19 -> slow triple-crest rings, one
 *        puddle per band, so a melody rings across the field; low mids -> the frog's throat sac, a low-mid
 *        hit off the kick sends it leaping in; sub -> the horizon mist; swell -> lamplight in the farmhouse
 *        (a gable window lights), stoked smoke, thicker mist; centroid -> light in the clouds; width -> the
 *        slant; air -> drops glinting on the wire and the web; level -> the rut streams run faster.
 *        Silence keeps a light drizzle / a few flakes, a breathing frog, drifting mist.
 * Density: 0 = a sparse drizzle even at the loudest, 1 = a downpour.
 * World: reads season (snow), wind, moisture (puddle size), daylight; writes moisture + water_level.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAX_DROPS 2000
#define MAX_SPL 420
#define MAX_RINGS 240
#define MAX_PUD 10
#define MAX_TREES 46
#define MAX_TUFT 640
#define MAX_FLW 110   /* meadow flowers */
#define NPOST 14      /* fence posts along the track */
#define MAX_BEAD 48   /* raindrops beading on the fence wire */
#define NSMOKE 26
#define MAX_BLADES 110
#define NDRIFT 160
#define CFW 96
#define CFH 26
#define HZ 0.47f      /* horizon */
#define CLOUD_Y HZ    /* clouds (and a mist band) fill the sky down to the horizon */
#define SKY_WMAX 4096
#define US_MAT_SNOW US_MAT_ACCENT /* cream-white at the bright end of most palettes */

typedef struct {
	float u, y, z;   /* u = x / aspect; z = nearness 0 far .. 1 at the bottom edge */
	float vy, land, ph;
	float wx, wy, wr; /* where a flake was drawn last frame (wiped so flakes do not smear into comets) */
	uint8_t snow;
} drop_t;

typedef struct {
	float u, y, vx, vy, life, z;
} spl_t;

typedef struct {
	float u, y, rmax, age, life, z, flat;
	uint8_t kind; /* 0 raindrop, 1 a note (slow triple crest), 2 a heavy drop (big crown + jet) */
} ring_t;

typedef struct {
	float u, z, size, ph; /* a hollow in the ground where water collects */
} pud_t;

typedef struct {
	float u, h, lean;
	uint8_t kind; /* 0 conifer, 1 round-headed */
} tree_t;

typedef struct {
	float u, z, h, ph, nod, nodv;
	uint8_t kind; /* 0 buttercup, 1 campion, 2 ox-eye daisy */
} flw_t;

typedef struct {
	int span;     /* fence span (post k .. k+1) */
	float f, size, wire;
} bead_t;

typedef struct {
	us_rng rng;
	int inited;
	flw_t fw[MAX_FLW];
	bead_t bd[MAX_BEAD];
	int nbd;
	float bead_acc, track_u0, track_u1, track_bend, house_u, smoke_t, fence_side;
	float sx[NSMOKE], sy[NSMOKE], sa[NSMOKE];
	float so; /* seed offset for terrain noise */
	drop_t d[MAX_DROPS];
	int nd;
	spl_t sp[MAX_SPL];
	int nsp;
	ring_t rg[MAX_RINGS];
	int nrg;
	pud_t pd[MAX_PUD];
	tree_t tr[MAX_TREES];
	float tu[MAX_TUFT], tz[MAX_TUFT];
	float bx[MAX_BLADES], bh[MAX_BLADES], bp[MAX_BLADES];
	float drift[NDRIFT];
	float cloud[CFW * CFH];
	float spawn_acc, rain_amt, wind_s, gust_t, cloud_t, snowfrac, snow_cov, heavy, wet, freeze;
	/* per-frame puddle geometry */
	float px[MAX_PUD], py[MAX_PUD], prx[MAX_PUD], pry[MAX_PUD];
	int npud;
	uint16_t sky_x[SKY_WMAX], sky_t[SKY_WMAX]; /* sky_blit column cache */
	uint16_t ridge_row[SKY_WMAX];              /* pixel clouds stop at the ridge */
	/* musical envelopes (real time) */
	float kprev, kg, kgs, swell_s, cent_s, width_s, sub_s, lm_s, air_s, since_beat;
	float bavg[US_NBANDS], bcool[US_NBANDS], pacc[MAX_PUD];
	float crown[NPOST], crown_acc[NPOST];
	float riv_t, mist_t;
	/* a crow on a fence post: shuffles its wings on hits, is startled into a loop by the drop */
	float crow_flap, crow_fly, crow_cool, crow_wing, crow_fdir;
	/* a frog at the near puddle: throat sac sings the low mids, leaps in on snare hits */
	int frog_st;
	float frog_t, frog_cool, frog_side, frog_th;
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x7261696eu);
	s->so = us_rand(&s->rng) * 50.0f;
	return s;
}

static void destroy(void *p) { free(p); }

/* ---- fast hairlines (see river.c): Wu-style 1px AA in ink, Bresenham when crisp ---- */
static void hl(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t mat)
{
	if (c->crisp) {
		us_line(c, ax, ay, bx, by, 0.001f, v, mat);
		return;
	}
	float x0 = ax * c->s - 0.5f, y0 = ay * c->s - 0.5f, x1 = bx * c->s - 0.5f, y1 = by * c->s - 0.5f;
	float dx = x1 - x0, dy = y1 - y0;
	int steep = fabsf(dy) > fabsf(dx);
	if (steep) {
		float t = x0; x0 = y0; y0 = t;
		t = x1; x1 = y1; y1 = t;
		t = dx; dx = dy; dy = t;
	}
	if (x0 > x1) {
		float t = x0; x0 = x1; x1 = t;
		t = y0; y0 = y1; y1 = t;
	}
	float g = dx != 0.0f ? (y1 - y0) / (x1 - x0) : 0.0f;
	float vv = v * sqrtf(1.0f + g * g) * 0.92f;
	int W = steep ? c->h : c->w, H = steep ? c->w : c->h;
	int i0 = (int)floorf(x0 + 0.5f), i1 = (int)floorf(x1 + 0.5f);
	if (i0 < 0) i0 = 0;
	if (i1 > W - 1) i1 = W - 1;
	if (i1 < i0) {
		if (x1 - x0 < 1.0f && i1 + 1 == i0)
			i1 = i0;
		else
			return;
	}
	for (int i = i0; i <= i1; i++) {
		float y = y0 + g * (i - x0);
		int j = (int)floorf(y);
		float f = y - j;
		if (j < -1 || j >= H)
			continue;
		float e = vv;
		if (i == i0 && i == i1)
			e *= us_clampf(x1 - x0, 0.3f, 1.0f);
		if (steep) {
			us_put(c, j, i, e * (1.0f - f), mat);
			us_put(c, j + 1, i, e * f, mat);
		} else {
			us_put(c, i, j, e * (1.0f - f), mat);
			us_put(c, i, j + 1, e * f, mat);
		}
	}
}

static void hpoly(us_canvas *c, const float *xy, int n, float v, uint8_t mat)
{
	for (int i = 0; i + 1 < n; i++)
		hl(c, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], v, mat);
}

/* ellipse outline; only the arc between a0..a1 (radians) when partial */
static void ellipse(us_canvas *c, float x, float y, float rx, float ry, float a0, float a1, float v, uint8_t mat)
{
	float pts[2 * 33];
	int n = rx * c->s < 5.0f ? 8 : (rx * c->s < 20.0f ? 16 : 32);
	n = (int)(n * (a1 - a0) / US_TAU + 0.99f);
	if (n < 3)
		n = 3;
	for (int i = 0; i <= n; i++) {
		float a = a0 + (a1 - a0) * i / n;
		pts[2 * i] = x + cosf(a) * rx;
		pts[2 * i + 1] = y + sinf(a) * ry;
	}
	hpoly(c, pts, n + 1, v, mat);
}

/* bilinear blit of the low-res cloud field over the full width, rows 0..y1 (normalized). Row-separable
 * with a per-column cache and 8.8 fixed point: several times cheaper than the generic us_field over a
 * region this large (see pond.c's field_blit). */
static void sky_blit(us_canvas *c, const float *f, int fw, int fh, float gain, uint8_t mat, float y1n, uint16_t *bx,
		     uint16_t *bt)
{
	int tmp[CFW + 1];
	int H = (int)(y1n * c->s);
	if (H > c->h)
		H = c->h;
	int W = c->w < SKY_WMAX ? c->w : SKY_WMAX;
	float kx = (float)fw / c->w, ky = (float)fh / (y1n * c->s);
	for (int i = 0; i < W; i++) {
		float fx = (i + 0.5f) * kx - 0.5f;
		if (fx < 0)
			fx = 0;
		int ix = (int)fx;
		float tx = fx - ix;
		if (ix >= fw - 1) {
			ix = fw - 1;
			tx = 0;
		}
		bx[i] = (uint16_t)ix;
		bt[i] = (uint16_t)(tx * 256.0f + 0.5f);
	}
	for (int j = 0; j < H; j++) {
		float fy = (j + 0.5f) * ky - 0.5f;
		int iy = (int)floorf(fy);
		float ty = fy - iy;
		int y0 = iy < 0 ? 0 : (iy >= fh ? fh - 1 : iy), yb = iy + 1 >= fh ? fh - 1 : (iy + 1 < 0 ? 0 : iy + 1);
		const float *r0 = f + y0 * fw, *r1 = f + yb * fw;
		int any = 0;
		for (int i = 0; i < fw; i++) {
			tmp[i] = (int)((r0[i] + (r1[i] - r0[i]) * ty) * gain * 255.0f * 256.0f);
			any |= tmp[i] > 0;
		}
		if (!any)
			continue;
		tmp[fw] = tmp[fw - 1];
		uint8_t *ink = c->ink + (size_t)j * c->w, *mt = c->mat + (size_t)j * c->w;
		for (int i = 0; i < W; i++) {
			int ix = bx[i], a0 = tmp[ix];
			int iv = (a0 + (((tmp[ix + 1] - a0) * bt[i]) >> 8)) >> 8;
			if (iv > 255)
				iv = 255;
			if (iv > ink[i]) {
				ink[i] = (uint8_t)iv;
				mt[i] = mat;
			}
		}
	}
}

/* pixel styles: a silhouette darker than what is behind it (the MAX blend can only brighten), so write it */
static void fill_dark(us_canvas *c, const float *xy, int n, uint8_t ink, uint8_t mat)
{
	float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
	for (int i = 0; i < n; i++) {
		x0 = fminf(x0, xy[2 * i]);
		x1 = fmaxf(x1, xy[2 * i]);
		y0 = fminf(y0, xy[2 * i + 1]);
		y1 = fmaxf(y1, xy[2 * i + 1]);
	}
	int px0 = (int)floorf(x0 * c->s), px1 = (int)ceilf(x1 * c->s), py0 = (int)floorf(y0 * c->s), py1 = (int)ceilf(y1 * c->s);
	for (int py = py0 < 0 ? 0 : py0; py <= py1 && py < c->h; py++)
		for (int px = px0 < 0 ? 0 : px0; px <= px1 && px < c->w; px++) {
			float X = (px + 0.5f) / c->s, Y = (py + 0.5f) / c->s;
			int in = 0;
			for (int i = 0, j = n - 1; i < n; j = i++) {
				float yi = xy[2 * i + 1], yj = xy[2 * j + 1];
				if ((yi > Y) != (yj > Y) && X < xy[2 * j] + (xy[2 * i] - xy[2 * j]) * (Y - yj) / (yi - yj))
					in = !in;
			}
			if (in) {
				size_t o = (size_t)py * c->w + px;
				c->ink[o] = ink;
				c->mat[o] = mat;
			}
		}
}

static void dark_line(us_canvas *c, float ax, float ay, float bx, float by, uint8_t ink, uint8_t mat)
{
	float dx = (bx - ax) * c->s, dy = (by - ay) * c->s;
	int n = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy))) + 1;
	for (int i = 0; i <= n; i++) {
		int px = (int)(ax * c->s + dx * i / n), py = (int)(ay * c->s + dy * i / n);
		if ((unsigned)px < (unsigned)c->w && (unsigned)py < (unsigned)c->h) {
			c->ink[(size_t)py * c->w + px] = ink;
			c->mat[(size_t)py * c->w + px] = mat;
		}
	}
}

/* ---- landscape ---- */
static float ridge_y(const st_t *st, float u) { return HZ - 0.06f - 0.05f * us_fbm2(u * 2.2f + st->so, 1.3f, 3); }
static float ground_y(float z) { return HZ + (1.0f - HZ) * z; }
static float drift_at(const st_t *st, float u)
{
	float f = us_clampf(u, 0, 1) * (NDRIFT - 1);
	int i = (int)f;
	if (i >= NDRIFT - 1)
		return st->drift[NDRIFT - 1];
	return us_lerp(st->drift[i], st->drift[i + 1], f - i);
}

/* the farm track: a pair of wheel ruts running from our feet to the horizon (x in normalized units) */
static float track_x(const st_t *st, float A, float z)
{
	return A * (us_lerp(st->track_u1, st->track_u0, z) + st->track_bend * sinf(US_PI * z) * (1.0f - 0.5f * z));
}
static float track_w(float z) { return 0.46f * z; }

/* 4x4 Bayer matrix for dithered pixel clouds */
static const uint8_t bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static void init_scene(st_t *st, const us_ctx *ctx)
{
	us_rng *r = &st->rng;
	float winter = us_world_winter(ctx->w);
	/* memory: open onto whatever snow the season has laid down, and puddles as wet as the world is */
	st->snow_cov = us_smooth(0.5f, 0.95f, winter) * 0.85f;
	for (int i = 0; i < NDRIFT; i++)
		st->drift[i] = st->snow_cov * 0.07f * (0.7f + 0.3f * us_noise2(i * 0.09f, 4.0f + st->so));
	st->snowfrac = us_smooth(0.35f, 0.65f, winter);
	st->freeze = us_smooth(0.6f, 0.9f, winter);
	st->rain_amt = us_sat(0.15f + ctx->w->moisture * 0.4f);
	st->wet = us_sat(ctx->w->moisture);
	/* treeline: groves of conifers and round-headed trees along the horizon */
	float grove[4] = {us_randr(r, 0.02f, 0.22f), us_randr(r, 0.3f, 0.48f), us_randr(r, 0.55f, 0.75f), us_randr(r, 0.8f, 0.98f)};
	for (int i = 0; i < MAX_TREES; i++) {
		tree_t *t = &st->tr[i];
		float gc = grove[i % 4];
		int lone = i % 5 == 4;
		t->u = lone ? us_rand(r) : us_clampf(gc + us_randn(r) * 0.05f, 0.0f, 1.0f);
		t->kind = us_rand(r) < 0.3f;
		t->h = us_randr(r, 0.035f, 0.1f) * (lone ? 0.7f : 1.0f) * (t->kind ? 0.8f : 1.0f);
		t->lean = us_randr(r, -0.04f, 0.04f);
	}
	/* puddles: spread across the meadow, more toward the front */
	float A = ctx->aspect;
	for (int i = 0; i < MAX_PUD; i++) {
		pud_t *p = &st->pd[i];
		p->size = us_randr(r, 0.6f, 1.15f);
		p->ph = us_rand(r) * 10.0f;
		/* keep hollows apart even at their wettest, so rims never cross */
		for (int tries = 0; tries < 40; tries++) {
			float gu = 0.23f + 0.618034f * (i + tries * 0.37f);
			p->u = 0.06f + 0.88f * (gu - floorf(gu));
			p->z = us_randr(r, 0.12f, 0.92f);
			float rx = (0.045f + 0.25f * p->z) * p->size * 1.3f, ry = rx * (0.08f + 0.26f * p->z);
			int ok = 1;
			for (int k = 0; k < i && ok; k++) {
				const pud_t *q = &st->pd[k];
				float qrx = (0.045f + 0.25f * q->z) * q->size * 1.3f, qry = qrx * (0.08f + 0.26f * q->z);
				float dx = fabsf(p->u - q->u) * A, dy = fabsf(ground_y(p->z) - ground_y(q->z));
				if (dx < (rx + qrx) * 1.05f && dy < (ry + qry) * 1.4f)
					ok = 0;
			}
			if (ok)
				break;
		}
	}
	for (int i = 0; i < MAX_TUFT; i++) {
		st->tu[i] = us_rand(r);
		st->tz[i] = us_rand(r);
	}
	st->track_u0 = us_randr(r, 0.28f, 0.72f);
	st->track_u1 = us_randr(r, 0.4f, 0.6f);
	st->track_bend = us_randr(r, -0.12f, 0.12f);
	st->fence_side = us_rand(r) < 0.5f ? -1.0f : 1.0f;
	/* the farmhouse stands away from the track's vanishing point */
	st->house_u = st->track_u1 + (us_rand(r) < 0.5f ? -1.0f : 1.0f) * us_randr(r, 0.12f, 0.3f);
	st->frog_side = us_rand(r) < 0.5f ? -1.0f : 1.0f;
	st->crow_cool = 6.0f;
	for (int i = 0; i < MAX_FLW; i++) {
		flw_t *f = &st->fw[i];
		f->u = us_rand(r);
		f->z = 0.12f + 0.86f * sqrtf(us_rand(r));
		f->h = us_randr(r, 0.55f, 1.0f);
		f->ph = us_rand(r) * US_TAU;
		f->kind = (uint8_t)(us_rand(r) * 3.0f);
		f->nod = f->nodv = 0;
	}
	for (int i = 0; i < NSMOKE; i++) {
		st->sx[i] = 0;
		st->sy[i] = -1.0f;
		st->sa[i] = 0;
	}
	for (int i = 0; i < MAX_BLADES; i++) {
		st->bx[i] = us_rand(r);
		st->bh[i] = us_randr(r, 0.25f, 1.0f);
		st->bp[i] = us_rand(r) * 20.0f;
	}
	st->inited = 1;
}

static void spawn_drop(st_t *st, float A, float slant, float sheet_t, int top)
{
	if (st->nd >= MAX_DROPS)
		return;
	us_rng *r = &st->rng;
	drop_t *d = &st->d[st->nd];
	d->snow = us_rand(r) < st->snowfrac;
	/* nearness: uniform in distance, biased a little toward the front */
	float dist = 1.0f + 22.0f * powf(us_rand(r), 1.6f);
	d->z = 1.0f / dist;
	d->ph = us_rand(r) * US_TAU;
	/* sheets of heavier rain sweep across with the gusts: thin out the gaps between them */
	float x = 0;
	for (int k = 0; k < 4; k++) {
		x = us_randr(r, -slant * (d->snow ? 0.4f : 1.0f) - 0.02f, A + 0.02f);
		float sh = 0.5f + 0.5f * us_noise2(x * 1.3f - sheet_t, st->so + 2.0f);
		if (us_rand(r) < 0.35f + 0.65f * sh)
			break;
	}
	d->u = x / A;
	d->wr = 0;
	d->land = ground_y(d->z);
	d->y = top ? us_randr(r, -0.12f, -0.01f) : us_randr(r, -0.05f, d->land);
	if (d->snow)
		d->vy = (0.04f + 0.11f * d->z) * us_randr(r, 0.8f, 1.2f);
	else
		d->vy = (0.55f + 1.6f * d->z) * us_randr(r, 0.9f, 1.1f) * (1.0f + 0.35f * st->heavy);
	st->nd++;
}

static ring_t *add_ring(st_t *st, float u, float y, float z, float rmax)
{
	if (st->nrg >= MAX_RINGS || rmax < 0.002f)
		return NULL;
	ring_t *g = &st->rg[st->nrg++];
	g->kind = 0;
	g->u = u;
	g->y = y;
	g->z = z;
	g->rmax = rmax;
	g->flat = 0.1f + 0.3f * z;
	g->age = 0;
	g->life = 0.7f + 0.9f * z;
	return g;
}

/* a ring somewhere inside puddle k (kind 1 = a note, 2 = a heavy drop), never past its rim */
static void pud_ring(st_t *st, int k, float A, float size, uint8_t kind)
{
	us_rng *r = &st->rng;
	float ang = us_rand(r) * US_TAU, rad = 0.55f * sqrtf(us_rand(r));
	float x = st->px[k] + cosf(ang) * rad * st->prx[k], y = st->py[k] + sinf(ang) * rad * st->pry[k];
	float z = st->pd[k].z;
	ring_t *g = add_ring(st, x / A, y, z, fminf((1.0f - rad) * st->prx[k], st->prx[k] * size));
	if (!g)
		return;
	g->kind = kind;
	if (kind == 1)
		g->life = 1.3f + 0.9f * z;
	else if (kind == 2) {
		g->life = 1.1f + 0.9f * z;
		if (st->nsp < MAX_SPL - 6)
			for (int i = 0; i < 5; i++) {
				spl_t *s = &st->sp[st->nsp++];
				s->u = x / A;
				s->y = y;
				s->z = z;
				s->vx = us_randr(r, -0.25f, 0.25f) * z;
				s->vy = -us_randr(r, 0.3f, 0.5f) * z;
				s->life = 1.0f;
			}
	}
}

static void splash(st_t *st, float u, float y, float z, float vx, int n)
{
	us_rng *r = &st->rng;
	for (int k = 0; k < n && st->nsp < MAX_SPL; k++) {
		spl_t *s = &st->sp[st->nsp++];
		s->u = u;
		s->y = y;
		s->z = z;
		s->vx = us_randr(r, -0.16f, 0.16f) * z + vx * 0.08f;
		s->vy = -us_randr(r, 0.12f, 0.32f) * z;
		s->life = 1.0f;
	}
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *st = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, A = ctx->aspect, t = (float)ctx->t;
	float dens = ctx->p->density;
	float winter = us_world_winter(w), autumn = us_world_season_w(w, 2);
	us_rng *r = &st->rng;

	if (!st->inited)
		init_scene(st, ctx);

	/* ---- musical envelopes: each part of the music has its own place in the field ----
	 * kick -> a gust that throws the rain sideways + a heavy drop in the near puddle; beat -> the crow;
	 * hats (bands 24..29) -> crowns of spray off the fence posts; mid bands -> notes ringing in the puddles;
	 * low mids -> the frog's throat; sub -> the distant mist; swell -> lamplight + mist; centroid -> the cloud
	 * light; width -> the slant; air -> drops glinting on the wire and the spider web. */
	float rdt = ctx->real_dt;
	int kick_hit = a->kick > 0.55f && st->kprev <= 0.55f;
	st->kprev = a->kick;
	st->kg = us_approach(st->kg, 0.0f, 0.4f, rdt);
	if (kick_hit)
		st->kg = us_sat(st->kg + 0.5f + 0.5f * a->kick);
	st->kgs = us_approach(st->kgs, st->kg, 0.07f, rdt); /* the gust rises in ~0.1 s, dies over ~0.5 s */
	st->swell_s = us_approach(st->swell_s, a->swell, 1.5f, rdt);
	st->cent_s = us_approach(st->cent_s, a->centroid, 1.2f, rdt);
	st->width_s = us_approach(st->width_s, a->width, 1.0f, rdt);
	st->sub_s = us_approach(st->sub_s, a->sub, a->sub > st->sub_s ? 0.15f : 0.7f, rdt);
	st->lm_s = us_approach(st->lm_s, a->lowmid, a->lowmid > st->lm_s ? 0.08f : 0.3f, rdt);
	st->air_s = us_approach(st->air_s, a->air, a->air > st->air_s ? 0.05f : 0.4f, rdt);
	float lull = st->since_beat; /* how long the music held off before this beat */
	st->since_beat = a->beat ? 0.0f : st->since_beat + rdt;
	uint32_t btrig = 0; /* per-band rising edges */
	for (int b = 9; b < US_NBANDS; b++) {
		float v = a->bands[b];
		st->bcool[b] -= rdt;
		if (v - st->bavg[b] > 0.14f && v > 0.3f && st->bcool[b] <= 0.0f) {
			btrig |= 1u << b;
			st->bcool[b] = 0.14f;
		}
		st->bavg[b] = us_approach(st->bavg[b], v, 0.5f, rdt);
	}

	/* ---- drivers ---- */
	st->snowfrac = us_approach(st->snowfrac, us_smooth(0.35f, 0.65f, winter), 4.0f, dt);
	st->freeze = us_approach(st->freeze, us_smooth(0.6f, 0.9f, winter), 8.0f, dt);
	float wind_t = us_sat(0.25f * w->wind + 0.8f * a->high + 0.35f * a->air + 0.15f * autumn);
	st->wind_s = us_approach(st->wind_s, wind_t, 1.2f, dt);
	st->gust_t += dt * (0.2f + 0.8f * st->wind_s);
	float gust = 0.5f + 0.5f * us_noise2(st->gust_t * 0.7f, st->so);
	float slant = 0.04f + 0.6f * st->wind_s * (0.55f + 0.8f * gust) + 0.3f * st->width_s + 0.45f * st->kgs;
	st->heavy = us_approach(st->heavy, us_sat(0.7f * a->bass + 0.5f * a->kick), 0.3f, ctx->real_dt);
	float target = us_sat(0.06f + 0.85f * a->level + 0.25f * a->swell);
	st->rain_amt = us_approach(st->rain_amt, target, target > st->rain_amt ? 0.8f : 3.0f, dt);
	st->cloud_t += dt * (0.02f + 0.06f * st->wind_s);
	float liquid = 1.0f - st->snowfrac;
	/* puddles fill with the rain falling here and with how wet the world already is */
	float wet_t = us_sat(0.25f + 0.75f * w->moisture + 0.35f * st->rain_amt * liquid * (0.4f + dens));
	st->wet = us_approach(st->wet, wet_t, wet_t > st->wet ? 12.0f : 40.0f, dt);
	/* snow cover builds while it snows, melts out of winter (faster in rain) */
	st->snow_cov = us_clampf(st->snow_cov + dt * (0.03f * st->snowfrac * (0.2f + st->rain_amt) * (0.3f + dens) -
						      0.012f * (1.0f - winter) * (1.0f + 2.0f * st->rain_amt * liquid)),
				 0.0f, 1.0f);

	us_world_add(w, &w->moisture, 0.006f * st->rain_amt * (1.0f - 0.6f * st->snowfrac), dt);
	us_world_add(w, &w->water_level, 0.0025f * st->rain_amt * (1.0f - 0.8f * st->snowfrac), dt);

	/* ---- spawn: density sets the ceiling (0 = a drizzle even in a drop), level fills toward it ---- */
	float maxrate = 16.0f + 1150.0f * powf(dens, 1.5f);
	float rate = maxrate * (0.06f + 0.94f * st->rain_amt * st->rain_amt);
	rate *= 1.0f - 0.7f * st->snowfrac; /* flakes live much longer, fewer per second */
	float sheet_t = st->gust_t * 1.4f;
	st->spawn_acc += rate * dt;
	while (st->spawn_acc >= 1.0f) {
		spawn_drop(st, A, slant, sheet_t, 1);
		st->spawn_acc -= 1.0f;
	}
	if (a->beat) {
		int burst = (int)(maxrate * (0.03f + 0.1f * a->onset) * (1.0f - 0.6f * st->snowfrac));
		for (int i = 0; i < burst; i++)
			spawn_drop(st, A, slant, sheet_t, 1);
	}
	/* when the scene opens, start mid-shower instead of an empty sky */
	if (st->nd == 0)
		for (int i = 0; i < (int)(rate * (st->snowfrac > 0.5f ? 4.0f : 0.6f)); i++)
			spawn_drop(st, A, slant, sheet_t, 0);

	/* ---- flakes: wipe last frame's drawing (only snow-coloured pixels) ---- */
	for (int i = 0; i < st->nd; i++) {
		drop_t *d = &st->d[i];
		if (!d->snow || d->wr <= 0)
			continue;
		int x0 = (int)floorf((d->wx - d->wr) * c->s), x1 = (int)ceilf((d->wx + d->wr) * c->s);
		int y0 = (int)floorf((d->wy - d->wr) * c->s), y1 = (int)ceilf((d->wy + d->wr) * c->s);
		for (int py = y0 < 0 ? 0 : y0; py <= y1 && py < c->h; py++)
			for (int px = x0 < 0 ? 0 : x0; px <= x1 && px < c->w; px++) {
				size_t o = (size_t)py * c->w + px;
				if (c->mat[o] == US_MAT_SNOW) {
					c->ink[o] = 0;
					c->mat[o] = US_MAT_BG; /* else the ink bloom relights the wiped flake */
				}
			}
	}

	/* ---- clouds: a slow low-res field over the sky ---- */
	{
		float cov = 0.3f + 0.7f * st->rain_amt;
		for (int j = 0; j < CFH; j++) {
			float y = (j + 0.5f) / CFH;
			float vf = us_smooth(0.95f, 0.35f, y) * us_smooth(0.0f, 0.1f, y);
			float mist = 0.3f * us_smooth(0.7f, 1.0f, y) * (0.4f + 0.6f * st->rain_amt);
			for (int i = 0; i < CFW; i++) {
				float u = (i + 0.5f) / CFW;
				float n = us_fbm3(u * 3.0f * A - st->cloud_t * 2.0f, y * 2.4f, st->cloud_t * 0.5f + st->so, 4);
				st->cloud[j * CFW + i] = us_smooth(-0.2f + 0.3f * (1 - cov), 0.6f, n) * vf + mist * (0.8f + 0.2f * n);
			}
		}
		/* bright music lifts the light in the clouds */
		float gain = 0.22f + 0.1f * w->daylight + 0.1f * st->cent_s;
		float cl = 0.85f + 0.35f * st->cent_s;
		if (ctx->crisp) {
			/* ordered dither: soft cloud edges without blobby 4-step bands; stops at the ridge */
			int yh = (int)(CLOUD_Y * c->s);
			int W = c->w < SKY_WMAX ? c->w : SKY_WMAX;
			for (int px = 0; px < W; px++)
				st->ridge_row[px] = (uint16_t)(ridge_y(st, (px + 0.5f) / c->w) * c->s);
			for (int py = 0; py < yh && py < c->h; py++) {
				float fy = (py + 0.5f) / (CLOUD_Y * c->s) * CFH - 0.5f;
				int j = (int)us_clampf(fy, 0, CFH - 2);
				float ty = us_clampf(fy - j, 0, 1);
				for (int px = 0; px < W; px++) {
					if (py >= st->ridge_row[px])
						continue;
					float fx = (px + 0.5f) / c->w * CFW - 0.5f;
					int i = (int)us_clampf(fx, 0, CFW - 2);
					float tx = us_clampf(fx - i, 0, 1);
					const float *q = st->cloud + j * CFW + i;
					float v = us_lerp(us_lerp(q[0], q[1], tx), us_lerp(q[CFW], q[CFW + 1], tx), ty);
					float th = (bayer4[(py & 3) * 4 + (px & 3)] + 0.5f) / 16.0f;
					float lv = v * 4.0f * cl; /* 0..~3 shade levels */
					int base = (int)lv;
					if (lv - base > th)
						base++;
					if (base > 0)
						us_put(c, px, py, 0.14f + 0.18f * (base > 3 ? 3 : base), US_MAT_BG);
				}
			}
		} else {
			sky_blit(c, st->cloud, CFW, CFH, gain, US_MAT_WATER, CLOUD_Y, st->sky_x, st->sky_t);
		}
	}

	float snowy = us_smooth(0.15f, 0.6f, st->snow_cov);

	/* ---- far ridge ---- */
	{
		const int NT = 64;
		float pts[2 * 65];
		for (int i = 0; i <= NT; i++) {
			float u = (float)i / NT;
			pts[2 * i] = u * A;
			pts[2 * i + 1] = ridge_y(st, u);
		}
if (ctx->crisp)
			for (int i = 0; i < NT; i++) {
				float x0 = pts[2 * i], x1 = pts[2 * i + 2];
				us_tri(c, x0, pts[2 * i + 1], x1, pts[2 * i + 3], x0, HZ, snowy > 0.5f ? 0.22f : 0.13f, snowy > 0.5f ? US_MAT_WATER : US_MAT_EARTH);
				us_tri(c, x1, pts[2 * i + 3], x1, HZ, x0, HZ, snowy > 0.5f ? 0.22f : 0.13f, snowy > 0.5f ? US_MAT_WATER : US_MAT_EARTH);
			}
		hpoly(c, pts, NT + 1, 0.2f + 0.25f * snowy, snowy > 0.5f ? US_MAT_SNOW : US_MAT_EARTH);
	}

	/* ---- mist lying in the distance: thickens with the swell, breathes with the sub ---- */
	st->mist_t += dt * (0.01f + 0.03f * st->wind_s);
	{
		static const float mband[3] = {HZ - 0.03f, HZ - 0.012f, HZ + 0.034f};
		float m = 0.07f + 0.13f * st->swell_s + 0.28f * st->sub_s;
		for (int j = 0; j < 3; j++) {
			float by = mband[j], mv = m * (j == 2 ? 0.75f : 1.0f);
			float drift = st->mist_t * (1.0f + 0.6f * j);
			if (ctx->crisp) {
				/* ordered-dither haze, a few rows thick, thinning at its edges */
				int W = c->w, row = (int)(by * c->s), hr = 1 + (int)(0.008f * c->s);
				for (int py = row - hr; py <= row + hr; py++) {
					if (py < 0 || py >= c->h)
						continue;
					float rw = 1.0f - (float)abs(py - row) / (hr + 1);
					for (int px0 = 0; px0 < W; px0 += 4) {
						float u = (px0 + 2.0f) / c->w;
						float n = us_smooth(0.15f, 0.8f, us_noise2(u * 2.5f * A - drift, j * 3.7f + st->so));
						float d = mv * n * rw * 2.2f;
						for (int px = px0; px < px0 + 4 && px < W; px++) {
							float th = (bayer4[(py & 3) * 4 + (px & 3)] + 0.5f) / 16.0f;
							if (d > th)
								us_put(c, px, py, 0.22f, US_MAT_SNOW);
						}
					}
				}
				continue;
			}
			const int NS = 64;
			float px = 0, py = 0, pv = 0;
			for (int i = 0; i <= NS; i++) {
				float u = (float)i / NS;
				float x = u * A, y = by + 0.004f * us_noise2(u * 3.0f + drift * 0.5f, j * 1.3f);
				float v = mv * us_smooth(-0.2f, 0.6f, us_noise2(u * 2.5f * A - drift, j * 3.7f + st->so));
				if (i > 0 && v + pv > 0.02f) {
					float vv = 0.5f * (v + pv);
					hl(c, px, py, x, y, vv, US_MAT_WATER);
					hl(c, px, py + 1.3f / c->s, x, y + 1.3f / c->s, vv * 0.55f, US_MAT_WATER);
					hl(c, px, py - 1.3f / c->s, x, y - 1.3f / c->s, vv * 0.35f, US_MAT_WATER);
				}
				px = x;
				py = y;
				pv = v;
			}
		}
	}

	/* ---- the meadow: ground fill (crisp), perspective texture ---- */
	/* in snow the tufts are dry stalks poking through */
	uint8_t gm = snowy > 0.5f ? US_MAT_STEM : US_MAT_LEAF;
	if (snowy > 0.05f && !ctx->crisp) {
		/* snowfield contours: wind-shaped ridges receding in perspective */
		for (int k = 0; k < 14; k++) {
			float z = powf((k + 0.6f) / 14.0f, 1.7f);
			float pts[2 * 49];
			for (int i = 0; i <= 48; i++) {
				float u = i / 48.0f;
				pts[2 * i] = u * A;
				pts[2 * i + 1] = ground_y(z) + 0.012f * z * us_noise2(u * (3.0f + 4.0f * (1.0f - z)) + k * 3.1f, st->so);
			}
			/* drifts, not ruled lines: each ridge comes and goes along its length; blue-shadowed lee,
			 * a brighter wind-polished crest where it rises */
			float base_v = (0.08f + 0.22f * snowy) * (0.45f + 0.55f * z);
			for (int i = 0; i < 48; i++) {
				float u = (i + 0.5f) / 48.0f;
				float n = us_noise2(u * (4.0f + 6.0f * (1.0f - z)) + k * 7.3f, st->so + 11.0f);
				float m = us_smooth(-0.15f, 0.35f, n);
				if (m < 0.05f)
					continue;
				int crest = n > 0.45f;
				hl(c, pts[2 * i], pts[2 * i + 1], pts[2 * i + 2], pts[2 * i + 3], base_v * m * (crest ? 1.6f : 1.0f),
				   crest ? US_MAT_SNOW : US_MAT_WATER);
			}
		}
	}
	if (ctx->crisp) {
		int y0 = (int)(HZ * c->s);
		for (int py = y0; py < c->h; py++) {
			float z = ((py + 0.5f) / c->s - HZ) / (1.0f - HZ);
			for (int px = 0; px < c->w; px++) {
				if (snowy > 0.5f) {
					/* snowfield: dithered from a bright distance down to a shaded foreground,
					 * dark enough that the falling flakes still read against it */
					float th = (bayer4[(py & 3) * 4 + (px & 3)] + 0.5f) / 16.0f;
						float g = us_sat(1.25f - z * 1.1f) * (0.6f + 0.4f * snowy);
						if (g > th)
							us_put(c, px, py, 0.62f, US_MAT_SNOW);
						else
							us_put(c, px, py, 0.62f, US_MAT_WATER);
				} else {
					us_put(c, px, py, 0.12f + 0.04f * z, US_MAT_EARTH);
				}
			}
		}
	}
	{
		/* a pixel canvas fills in fast: fewer, dimmer tufts there */
		int nt = (int)(MAX_TUFT * (0.35f + 0.65f * dens) * (ctx->crisp ? 0.4f : 1.0f));
		float green = 1.0f - 0.6f * snowy;
		for (int i = 0; i < nt; i++) {
			float z = 0.02f + 0.98f * powf(st->tz[i], 1.35f);
			float x = st->tu[i] * A, y = ground_y(z);
			float hv = st->tu[(i * 7 + 3) % MAX_TUFT]; /* a second per-tuft random */
			float h = (0.003f + 0.03f * z * z * green) * (0.55f + 0.7f * hv);
			float sw = (0.2f + st->wind_s + 0.8f * st->kgs) * h * 0.7f * (0.6f + 0.4f * us_noise2(x * 3.0f - st->gust_t, i * 0.3f));
			float v = (0.14f + 0.2f * z) * (ctx->crisp ? 0.8f : 1.0f);
			float bend = (hv - 0.5f) * h * 0.5f;
			hl(c, x, y, x + sw + bend, y - h, v, gm);
			if (z > 0.3f && hv > 0.25f)
				hl(c, x + h * 0.25f, y, x + h * 0.25f + sw * 1.2f + h * 0.3f, y - h * 0.8f, v * 0.85f, gm);
			if (z > 0.3f && hv < 0.75f)
				hl(c, x - h * 0.25f, y, x - h * 0.25f + sw * 0.8f - h * 0.3f, y - h * 0.65f, v * 0.85f, gm);
		}
	}

	/* ---- a hedgerow across the meadow in the middle distance, gapped where the track goes through ---- */
	{
		const float zh = 0.14f;
		float gx = track_x(st, A, zh), gw = track_w(zh) * 0.75f;
		float r0 = 0.0045f + 0.03f * zh;
		uint8_t hm = snowy > 0.5f ? US_MAT_STEM : US_MAT_LEAF;
		for (float x = -0.01f; x < A + 0.01f; x += r0 * 1.15f) {
			if (fabsf(x - gx) < gw)
				continue;
			float n = us_noise2(x * 9.0f, st->so + 5.0f), n2 = us_noise2(x * 23.0f, st->so);
			if (n < -0.45f) /* field gateways and gaps */
				continue;
			float rr = r0 * (0.8f + 0.35f * n2 + 0.3f * n);
			float y = ground_y(zh) - rr * 0.6f;
			if (ctx->crisp) {
				us_disc(c, x, y, rr, 0.3f, hm);
				us_disc(c, x - rr * 0.3f, y - rr * 0.35f, rr * 0.55f, 0.45f, hm);
			} else {
				ellipse(c, x, y, rr, rr * 0.8f, US_PI * 0.95f, US_TAU + 0.15f, 0.3f, hm);
				ellipse(c, x - rr * 0.2f, y - rr * 0.25f, rr * 0.5f, rr * 0.4f, US_PI, US_TAU, 0.2f, hm);
				if (snowy > 0.2f)
					ellipse(c, x, y - rr * 0.1f, rr * 0.85f, rr * 0.6f, US_PI * 1.15f, US_PI * 1.85f, 0.6f * snowy, US_MAT_SNOW);
			}
			/* now and then a hedgerow tree stands up out of it */
			if (n2 > 0.42f && !ctx->crisp) {
				float th = rr * 5.0f;
				hl(c, x, y, x, y - th * 0.6f, 0.3f, US_MAT_STEM);
				float pts[2 * 17];
				for (int k = 0; k <= 16; k++) {
					float aa = US_TAU * k / 16;
					float q = th * 0.42f * (1.0f + 0.1f * sinf(aa * 5.0f + x * 50.0f));
					pts[2 * k] = x + cosf(aa) * q;
					pts[2 * k + 1] = y - th * 0.75f + sinf(aa) * q * 0.85f;
				}
				hpoly(c, pts, 17, 0.28f, hm);
			}
		}
	}

	/* ---- the farm track: two wheel ruts holding rainwater, grass down the middle ---- */
	{
		const int NZ = 48;
		float wet_r = us_sat(st->wet * 1.3f - 0.2f) * (1.0f - snowy);
		/* rainwater running down the ruts toward us: faster and brighter the harder it rains */
		st->riv_t += dt * (0.025f + 0.11f * st->rain_amt * liquid);
		float riv_v = wet_r * (1.0f - st->freeze) * (0.45f + 0.55f * st->rain_amt);
		for (int sd = -1; sd <= 1; sd += 2) {
			float pts[2 * 49];
			for (int i = 0; i <= NZ; i++) {
				float z = 1.03f * i / NZ;
				pts[2 * i] = track_x(st, A, z) + sd * 0.32f * track_w(z);
				pts[2 * i + 1] = ground_y(z);
			}
			for (int k = 0; riv_v > 0.08f && k < 10; k++) {
				float ph = k * 0.618034f + (sd > 0 ? 0.31f : 0.0f) + st->riv_t * (0.8f + 0.05f * k);
				ph -= floorf(ph);
				float z = 0.1f + 0.95f * ph * ph, z2 = z - 0.02f - 0.03f * z;
				float x0 = track_x(st, A, z) + sd * 0.32f * track_w(z), y0 = ground_y(z);
				float x1 = track_x(st, A, z2) + sd * 0.32f * track_w(z2), y1 = ground_y(z2);
				float v = (0.35f + 0.5f * z) * riv_v * sinf(US_PI * ph);
				if (ctx->crisp)
					us_put(c, (int)(x0 * c->s), (int)(y0 * c->s), v > 0.25f ? 0.75f : 0.5f, US_MAT_WATER);
				else
					hl(c, x1, y1, x0, y0, v, US_MAT_WATER);
			}
			if (ctx->crisp) {
				for (int i = 0; i < NZ; i++) {
					float z = 1.03f * (i + 0.5f) / NZ;
					int wv = wet_r > 0.3f && z > 0.12f;
					us_line(c, pts[2 * i], pts[2 * i + 1], pts[2 * i + 2], pts[2 * i + 3], 0.0011f + 0.004f * z,
						wv ? 0.36f : (snowy > 0.5f ? 0.4f : 0.3f), wv ? US_MAT_WATER : (snowy > 0.5f ? US_MAT_STEM : US_MAT_EARTH));
				}
			} else {
				/* the rut's two lips, then the sky caught in its water */
				for (int e = -1; e <= 1; e += 2) {
					float q[2 * 49];
					for (int i = 0; i <= NZ; i++) {
						float z = 1.03f * i / NZ;
						q[2 * i] = pts[2 * i] + e * 0.035f * track_w(z);
						q[2 * i + 1] = pts[2 * i + 1];
					}
					hpoly(c, q, NZ + 1, 0.2f + 0.08f * snowy, snowy > 0.5f ? US_MAT_WATER : US_MAT_EARTH);
				}
				for (int i = 0; i < NZ; i++) {
					float z = 1.03f * (i + 0.5f) / NZ;
					if (z < 0.08f)
						continue;
					float h1 = sinf(i * 12.9898f + sd * 31.7f) * 43758.5f;
					h1 -= floorf(h1);
					if (h1 > wet_r * 1.2f)
						continue;
					float trem = 0.6f + 0.4f * sinf(t * (2.0f + 3.0f * st->rain_amt) + h1 * 20.0f);
					float v = (0.22f + 0.4f * z) * trem * wet_r * (1.0f - 0.5f * st->freeze);
					float f0 = 0.15f + 0.3f * h1, f1 = f0 + 0.35f + 0.2f * h1;
					hl(c, us_lerp(pts[2 * i], pts[2 * i + 2], f0), us_lerp(pts[2 * i + 1], pts[2 * i + 3], f0),
					   us_lerp(pts[2 * i], pts[2 * i + 2], f1), us_lerp(pts[2 * i + 1], pts[2 * i + 3], f1), v, US_MAT_WATER);
				}
			}
		}
	}

	/* ---- treeline along the horizon (snow-laden in winter) ---- */
	{
		int ntr = 16 + (int)(dens * (MAX_TREES - 16));
		for (int i = 0; i < ntr; i++) {
			tree_t *tr = &st->tr[i];
			float x = tr->u * A, base = HZ + 0.002f;
			float sway = 0.005f * st->wind_s * us_noise2(st->gust_t * 1.5f, i * 1.7f);
			float tx = x + tr->lean * tr->h + sway, ty = base - tr->h;
			if (tr->kind == 0) {
				hl(c, x, base, tx, ty, 0.38f, US_MAT_STEM);
				int tiers = 4 + (int)(tr->h * 55.0f);
				for (int k = 0; k < tiers; k++) {
					float f = (k + 1.0f) / (tiers + 1.0f);
					float px = us_lerp(tx, x, f), py = us_lerp(ty, base, f);
					float bw = tr->h * 0.34f * f;
					float droop = tr->h * 0.16f * (1.0f + 0.5f * snowy);
					if (ctx->crisp)
						us_tri(c, px, py - droop * 0.5f, px - bw, py + droop * 0.5f,px + bw, py + droop * 0.5f, k & 1 ? 0.45f : 0.62f, US_MAT_LEAF);
					else {
						hl(c, px, py - droop * 0.3f, px - bw, py + droop * 0.5f, 0.36f, US_MAT_LEAF);
						hl(c, px, py - droop * 0.3f, px + bw, py + droop * 0.5f, 0.36f, US_MAT_LEAF);
					}
					if (snowy > 0.1f)
						hl(c, px - bw * 0.7f, py + droop * 0.15f, px + bw * 0.7f, py + droop * 0.15f, 0.6f * snowy, US_MAT_SNOW);
				}
			} else {
				/* round-headed tree: trunk + a lumpy crown */
				float cr = tr->h * 0.42f;
				float cx = tx, cy = ty + cr;
				hl(c, x, base, cx, cy, 0.4f, US_MAT_STEM);
				if (ctx->crisp) {
us_disc(c, cx, cy, cr, 0.45f, US_MAT_LEAF);
						us_disc(c, cx - cr * 0.3f, cy - cr * 0.3f, cr * 0.5f, 0.66f, US_MAT_LEAF);
				} else {
					float pts[2 * 17];
					for (int k = 0; k <= 16; k++) {
						float aa = US_TAU * k / 16;
						float rr = cr * (1.0f + 0.12f * sinf(aa * 5.0f + i));
						pts[2 * k] = cx + cosf(aa) * rr;
						pts[2 * k + 1] = cy + sinf(aa) * rr * 0.85f;
					}
					hpoly(c, pts, 17, 0.36f, US_MAT_LEAF);
				}
				if (snowy > 0.1f)
					ellipse(c, cx, cy, cr * 0.9f, cr * 0.75f, US_PI * 1.15f, US_PI * 1.85f, 0.65f * snowy, US_MAT_SNOW);
			}
		}
	}

	/* ---- the farmhouse on the horizon: a lit window, smoke leaning with the wind ---- */
	{
		float hx = st->house_u * A, base = HZ + 0.003f;
		float hw = 0.034f, wh = 0.02f, rh = 0.019f;
		/* the lamps burn brighter as the music swells, even by day */
		float lit = us_sat(1.05f - 0.75f * w->daylight + 0.55f * st->swell_s) * (0.85f + 0.15f * us_noise2(t * 1.3f, 7.7f));
		float gable = lit * us_smooth(0.35f, 0.7f, st->swell_s); /* someone goes upstairs */
		if (ctx->crisp) {
			us_tri(c, hx - hw, base, hx + hw, base, hx + hw, base - wh, 0.16f, US_MAT_EARTH);
			us_tri(c, hx - hw, base, hx + hw, base - wh, hx - hw, base - wh, 0.16f, US_MAT_EARTH);
			us_tri(c, hx - hw * 1.15f, base - wh, hx + hw * 1.15f, base - wh, hx, base - wh - rh, 0.26f,
			       snowy > 0.5f ? US_MAT_SNOW : US_MAT_EARTH);
			us_disc(c, hx - hw * 0.45f, base - wh * 0.5f, 0.0035f, 0.6f + 0.4f * lit, US_MAT_LIGHT);
			us_disc(c, hx + hw * 0.4f, base - wh * 0.5f, 0.0035f, 0.45f + 0.4f * lit, US_MAT_LIGHT);
			if (gable > 0.2f)
				us_put(c, (int)(hx * c->s), (int)((base - wh - rh * 0.4f) * c->s), 0.5f + 0.5f * gable, US_MAT_LIGHT);
		} else {
			float pts[] = {hx - hw, base, hx - hw, base - wh, hx - hw * 1.18f, base - wh + 0.002f, hx, base - wh - rh,
				       hx + hw * 1.18f, base - wh + 0.002f, hx + hw, base - wh, hx + hw, base};
			hpoly(c, pts, 7, 0.36f, US_MAT_EARTH);
			hl(c, hx - hw, base - wh, hx + hw, base - wh, 0.2f, US_MAT_EARTH);
			hl(c, hx - 0.004f, base, hx - 0.004f, base - wh * 0.6f, 0.3f, US_MAT_EARTH);
			hl(c, hx + 0.004f, base, hx + 0.004f, base - wh * 0.6f, 0.3f, US_MAT_EARTH);
			hl(c, hx - 0.004f, base - wh * 0.6f, hx + 0.004f, base - wh * 0.6f, 0.3f, US_MAT_EARTH);
			if (snowy > 0.3f) {
				hl(c, hx - hw * 1.1f, base - wh + 0.0012f, hx, base - wh - rh + 0.0012f, 0.7f * snowy, US_MAT_SNOW);
				hl(c, hx, base - wh - rh + 0.0012f, hx + hw * 1.1f, base - wh + 0.0012f, 0.7f * snowy, US_MAT_SNOW);
			}
			/* two windows, warm; the glow is the ink bloom's */
			for (int k = 0; k < 2; k++) {
				float wx = hx + (k ? 0.4f : -0.45f) * hw, wy = base - wh * 0.5f;
				float wv = (k ? 0.7f : 0.95f) * lit;
				us_tri(c, wx - 0.004f, wy - 0.0045f, wx + 0.004f, wy - 0.0045f, wx + 0.004f, wy + 0.0045f, wv, US_MAT_LIGHT);
				us_tri(c, wx - 0.004f, wy - 0.0045f, wx + 0.004f, wy + 0.0045f, wx - 0.004f, wy + 0.0045f, wv, US_MAT_LIGHT);
				hl(c, wx, wy - 0.0045f, wx, wy + 0.0045f, wv * 0.4f, US_MAT_EARTH); /* glazing bar */
			}
			if (gable > 0.02f) { /* a small window in the gable */
				float gy = base - wh - rh * 0.38f;
				us_tri(c, hx - 0.0028f, gy - 0.003f, hx + 0.0028f, gy - 0.003f, hx + 0.0028f, gy + 0.003f, gable, US_MAT_LIGHT);
				us_tri(c, hx - 0.0028f, gy - 0.003f, hx + 0.0028f, gy + 0.003f, hx - 0.0028f, gy + 0.003f, gable, US_MAT_LIGHT);
			}
		}
		/* chimney + smoke: puffs rise, swell and lean downwind */
		float chx = hx + hw * 0.55f, chy = base - wh - rh * 0.45f;
		hl(c, chx, chy, chx, chy - 0.006f, 0.36f, US_MAT_EARTH);
		hl(c, chx + 0.003f, chy - 0.002f, chx + 0.003f, chy - 0.006f, 0.36f, US_MAT_EARTH);
		st->smoke_t += dt * (0.5f + st->wind_s);
		/* smoke: three curling wisps from the chimney, swelling and leaning downwind as they rise */
		float smv = 0.6f + 0.4f * (1.0f - w->daylight) + 0.35f * st->swell_s; /* the fire is stoked */
		for (int k = 0; k < 3; k++) {
			float pts[2 * NSMOKE];
			for (int i = 0; i < NSMOKE; i++) {
				float age = (float)i / (NSMOKE - 1);
				float rise = 0.12f * age * (1.0f - 0.35f * st->wind_s);
				float spread = 0.004f + 0.02f * age;
				float curl = us_noise2(age * 3.0f - st->smoke_t * 0.35f, k * 4.1f + st->so);
				pts[2 * i] = chx + 0.0015f + (0.02f + 0.22f * st->wind_s) * age * age + spread * curl;
				pts[2 * i + 1] = chy - 0.007f - rise + 0.3f * spread * us_noise2(k * 2.3f, age * 3.0f - st->smoke_t * 0.3f);
			}
			if (ctx->crisp) {
				for (int i = 2; i < NSMOKE - 6; i += 3)
					us_put(c, (int)(pts[2 * i] * c->s), (int)(pts[2 * i + 1] * c->s), 0.3f, US_MAT_SNOW);
			} else
				for (int i = 0; i + 1 < NSMOKE; i++) {
					float age = (float)i / (NSMOKE - 1);
					float sv = 0.26f * smv * us_smooth(0.0f, 0.1f, age) * (1.0f - age) * (k == 1 ? 1.0f : 0.6f);
					hl(c, pts[2 * i], pts[2 * i + 1], pts[2 * i + 2], pts[2 * i + 3], sv, US_MAT_SNOW);
				}
		}
	}

	/* ---- puddles: hollows that fill with the wet; frozen in deep winter ---- */
	{
		int np = 3 + (int)(dens * (MAX_PUD - 3) + 0.5f);
		st->npud = np;
		float grow = 0.25f + 1.0f * st->wet;
		for (int i = 0; i < np; i++) {
			pud_t *p = &st->pd[i];
			float x = p->u * A, y = ground_y(p->z);
			float rx = (0.045f + 0.25f * p->z) * p->size * grow;
			float ry = rx * (0.08f + 0.26f * p->z);
			st->px[i] = x;
			st->py[i] = y;
			st->prx[i] = rx;
			st->pry[i] = ry;
			if (ctx->crisp) {
				/* filled water, rim of mud */
				int y0 = (int)floorf((y - ry) * c->s), y1 = (int)ceilf((y + ry) * c->s);
				for (int py = y0; py <= y1; py++) {
					float fy = ((py + 0.5f) / c->s - y) / ry;
					if (fy * fy >= 1.0f)
						continue;
					float hw = rx * sqrtf(1.0f - fy * fy);
					int x0 = (int)floorf((x - hw) * c->s + 0.5f), x1 = (int)floorf((x + hw) * c->s - 0.5f);
					for (int px = x0; px <= x1; px++)
						if ((unsigned)px < (unsigned)c->w && (unsigned)py < (unsigned)c->h) {
							size_t o = (size_t)py * c->w + px;
							c->ink[o] = (uint8_t)(255.0f * (st->freeze > 0.5f ? 0.8f : 0.2f));
							c->mat[o] = st->freeze > 0.5f ? US_MAT_SNOW : US_MAT_WATER;
						}
				}
			} else {
				/* far rim only (the near rim is hidden by the lip of the hollow) + the near lip */
				ellipse(c, x, y, rx, ry, US_PI, US_TAU, 0.3f + 0.15f * st->freeze, US_MAT_WATER);
				ellipse(c, x, y, rx, ry, 0.15f, US_PI - 0.15f, 0.18f, US_MAT_EARTH);
			}
			/* sky reflected in the water: a few trembling horizontal strokes */
			int nref = 2 + (int)(p->z * 7.0f);
			float trem = (0.4f + st->rain_amt * liquid) * (1.0f - st->freeze);
			for (int k = 0; k < nref; k++) {
				float h1 = sinf(k * 12.9898f + i * 78.233f) * 43758.5f;
				h1 -= floorf(h1);
				float fy = -0.75f + 1.5f * (k + 0.5f) / nref;
				float hw = sqrtf(us_sat(1.0f - fy * fy)) * rx;
				float len = hw * (0.25f + 0.5f * h1) * (0.7f + 0.3f * sinf(t * (1.0f + 2.0f * trem) + h1 * 9.0f + p->ph));
				float off = (h1 - 0.5f) * (hw - len) * 1.6f + 0.004f * trem * us_noise2(t * 3.0f, k + i * 5.0f);
				float yy = y + fy * ry;
				float v = (0.16f + 0.2f * (1.0f - fabsf(fy))) * (1.0f + 0.8f * st->freeze);
				hl(c, x + off - len, yy, x + off + len, yy, v, US_MAT_WATER);
			}
			/* the farmhouse's lit windows caught in the water: a broken warm column under the house */
			{
				float hx = st->house_u * A;
				float lit = us_sat(1.05f - 0.75f * w->daylight + 0.55f * st->swell_s);
				if (fabsf(hx - x) < rx * 0.8f && lit > 0.1f) {
					float hw = rx * sqrtf(us_sat(1.0f - ((hx - x) / rx) * ((hx - x) / rx)));
					(void)hw;
					int nk = 3 + (int)(p->z * 6.0f);
					for (int k = 0; k < nk; k++) {
						float fy = -0.8f + 1.6f * (k + 0.5f) / nk;
						float wob = 0.003f * trem * us_noise2(t * 4.0f + k, i * 3.0f);
						float len = (0.002f + 0.006f * p->z) * (0.6f + 0.4f * sinf(t * 3.0f + k * 1.7f));
						float yy = y + fy * ry;
						hl(c, hx + wob - len, yy, hx + wob + len, yy, (0.55f - 0.25f * fabsf(fy)) * lit, US_MAT_LIGHT);
					}
				}
			}
			/* ice: a couple of crack lines */
			if (st->freeze > 0.3f && rx * c->s > 6.0f) {
				for (int k = 0; k < 3; k++) {
					float h1 = sinf(k * 7.13f + i * 3.7f) * 43758.5f;
					h1 -= floorf(h1);
					float a0 = h1 * US_TAU;
					float cx0 = x + cosf(a0) * rx * 0.3f, cy0 = y + sinf(a0) * ry * 0.3f;
					float cx1 = x + cosf(a0 + 2.5f) * rx * 0.85f, cy1 = y + sinf(a0 + 2.5f) * ry * 0.85f;
					hl(c, cx0, cy0, cx1, cy1, 0.8f * st->freeze, US_MAT_WATER);
				}
			}
		}
	}

	/* the nearest puddle: the frog's, and where the heavy drops fall */
	int near_p = 0;
	for (int k = 1; k < st->npud; k++)
		if (st->pd[k].z > st->pd[near_p].z)
			near_p = k;
	if (st->freeze < 0.5f && liquid > 0.3f) {
		/* each puddle listens to its own band of the mids: the melody rings across the field like keys */
		for (int k = 0; k < st->npud && k < 10; k++) {
			int b = 10 + k;
			float bv = a->bands[b];
			st->pacc[k] += rdt * 0.4f * bv * bv * bv;
			if (((btrig >> b) & 1u) || st->pacc[k] >= 1.0f) {
				st->pacc[k] = 0.0f;
				pud_ring(st, k, A, 0.14f + 0.16f * bv, 1);
			}
		}
		/* the kick: a heavy drop lets go into the near puddle */
		if (kick_hit)
			pud_ring(st, near_p, A, 0.5f, 2);
	}

	/* ---- ripple rings in the puddles (perspective ellipses, clipped to their puddle) ---- */
	for (int i = 0; i < st->nrg;) {
		ring_t *g = &st->rg[i];
		g->age += dt;
		float f = g->age / g->life;
		if (f >= 1.0f) {
			*g = st->rg[--st->nrg];
			continue;
		}
		float e = 1.0f - (1.0f - f) * (1.0f - f);
		float rr = g->rmax * e;
		float v = (1.0f - f) * (0.5f + 0.5f * g->z);
		float x = g->u * A;
		uint8_t m = US_MAT_WATER;
		if (g->kind == 1) {
			/* a note: three slow crests spreading one after another */
			float fv = (1.0f - f) * sqrtf(1.0f - f) * (0.25f + 0.3f * g->z);
			for (int q = 0; q < 3; q++) {
				float fq = f * 1.3f - q * 0.15f;
				if (fq <= 0.0f)
					break;
				float rq = g->rmax * (1.0f - (1.0f - us_sat(fq)) * (1.0f - us_sat(fq)));
				if (rq * c->s < 1.5f)
					continue;
				ellipse(c, x, g->y, rq, rq * g->flat, 0, US_TAU, fv * (1.0f - 0.25f * q), m);
			}
			i++;
			continue;
		}
		if (g->kind == 2) {
			v = us_sat(v * 1.3f);
			if (g->age < 0.14f) { /* a full crown of spray */
				float k = 1.0f - g->age / 0.14f, hh = 0.03f * g->z * k, cr = g->rmax * 0.18f;
				for (int q = 0; q < 7; q++) {
					float aa = US_PI * (0.1f + 0.8f * q / 6.0f);
					float bx = x - cosf(aa) * cr, by = g->y + 0.3f * sinf(aa) * cr * g->flat;
					hl(c, bx, by, bx - cosf(aa) * hh * 0.5f, by - hh * (0.6f + 0.4f * sinf(aa)), 0.8f * k + 0.2f, m);
				}
			} else if (g->age < 0.45f) { /* the jet rises and falls back */
				float jh = 0.04f * g->z * sinf(US_PI * (g->age - 0.14f) / 0.31f);
				hl(c, x, g->y, x, g->y - jh, 0.7f, m);
				us_point(c, x, g->y - jh - 0.002f * g->z, 0.0012f + 0.0018f * g->z, 0.9f, m);
			}
		}
		if (ctx->crisp && rr * c->s < 1.0f)
			us_point(c, x, g->y, 0.001f, v, m);
		else
			ellipse(c, x, g->y, rr, rr * g->flat, 0, US_TAU, v, m);
		if (g->z > 0.35f && rr > 0.006f)
			ellipse(c, x, g->y, rr * 0.62f, rr * 0.62f * g->flat, 0, US_TAU, v * 0.55f, m);
		/* first instant: the drop's crown, then a little jet */
		if (g->kind == 2) {
		} else if (g->age < 0.09f && g->z > 0.3f) {
			float hh = 0.012f * g->z * (1.0f - g->age / 0.09f);
			hl(c, x - hh * 0.4f, g->y, x - hh * 0.6f, g->y - hh, 0.75f, m);
			hl(c, x + hh * 0.4f, g->y, x + hh * 0.6f, g->y - hh, 0.75f, m);
		} else if (g->age < 0.25f && g->z > 0.6f) {
			float jh = 0.014f * g->z * sinf(US_PI * (g->age - 0.09f) / 0.16f);
			us_point(c, x, g->y - jh, 0.0012f + 0.001f * g->z, 0.8f, m);
		}
		i++;
	}

	/* ---- meadow flowers: stems and heads that nod when the drops strike them and lean with the wind ---- */
	{
		float bloomy = us_sat(1.0f - snowy * 1.5f) * (0.35f + 0.65f * us_sat(us_world_season_w(w, 0) + us_world_season_w(w, 1) + 0.6f * autumn));
		int nf = (int)(MAX_FLW * (0.3f + 0.7f * dens) * bloomy);
		float hit_p = (0.4f + 6.0f * st->rain_amt * liquid) * dt;
		float seed = us_smooth(0.4f, 0.9f, autumn);
		for (int i = 0; i < nf; i++) {
			flw_t *f = &st->fw[i];
			float z = f->z, x = f->u * A, by = ground_y(z);
			/* not in a puddle, not on the track */
			int skip = fabsf(x - track_x(st, A, z)) < track_w(z) * 0.5f;
			for (int k = 0; k < st->npud && !skip; k++) {
				float qx = (x - st->px[k]) / (st->prx[k] * 1.1f), qy = (by - st->py[k]) / (st->pry[k] * 1.3f);
				skip = qx * qx + qy * qy < 1.0f;
			}
			if (skip)
				continue;
			/* a drop strikes the head: a kick, then a damped nod back */
			if (us_rand(r) < hit_p * z)
				f->nodv += us_randr(r, 4.0f, 9.0f) * (0.5f + 0.5f * st->heavy);
			f->nodv += (-28.0f * f->nod - 3.5f * f->nodv) * dt;
			f->nod += f->nodv * dt;
			float h = (0.01f + 0.085f * z * z) * f->h;
			float sw = (0.1f + 0.7f * st->wind_s * (0.5f + gust) + 0.6f * st->kgs) * 0.4f * (0.6f + 0.4f * us_noise2(x * 3.0f - st->gust_t, f->ph));
			float a = sw + 0.35f * f->nod;
			float hx = x + sinf(a) * h, hy = by - cosf(a) * h;
			float mx = x + sinf(a * 0.4f) * h * 0.5f, my = by - cosf(a * 0.4f) * h * 0.5f;
			float sv = 0.22f + 0.25f * z;
			if (ctx->crisp) {
				us_line(c, x, by, mx, my, 0.001f, sv + 0.1f, US_MAT_STEM);
				us_line(c, mx, my, hx, hy, 0.001f, sv + 0.1f, US_MAT_STEM);
			} else {
				float pts[6] = {x, by, mx, my, hx, hy};
				hpoly(c, pts, 3, sv, US_MAT_STEM);
				if (z > 0.45f) { /* a leaf */
					float lx = us_lerp(x, mx, 0.6f), ly = us_lerp(by, my, 0.6f);
					float side = (i & 1) ? 1.0f : -1.0f;
					hl(c, lx, ly, lx + side * h * 0.22f, ly - h * 0.12f, sv * 0.9f, US_MAT_LEAF);
				}
			}
			float hr = 0.0016f + 0.0075f * z;
			if (seed > 0.5f) { /* autumn: seed heads */
				us_point(c, hx, hy, hr * 0.35f, 0.4f, US_MAT_STEM);
				continue;
			}
			uint8_t pm = f->kind == 0 ? US_MAT_LIGHT : f->kind == 1 ? US_MAT_BLOOM : US_MAT_ACCENT;
			float pv = 0.62f + 0.3f * z;
			if (ctx->crisp || hr * c->s < 3.0f) {
				us_point(c, hx, hy, ctx->crisp ? 0.0012f + 0.004f * z : hr * 0.5f, pv, pm);
				if (ctx->crisp && f->kind == 2 && z > 0.5f)
					us_put(c, (int)(hx * c->s), (int)(hy * c->s), 0.8f, US_MAT_LIGHT);
				continue;
			}
			/* petals; the head tips over with the nod */
			int np = f->kind == 2 ? 9 : 5;
			float tilt = 0.55f + 0.25f * cosf(a * 2.0f);
			for (int k = 0; k < np; k++) {
				float pa = f->ph + US_TAU * k / np;
				float pr = hr * (f->kind == 0 ? 0.6f : 1.0f);
				hl(c, hx + cosf(pa) * pr * 0.25f, hy + sinf(pa) * pr * 0.25f * tilt, hx + cosf(pa) * pr, hy + sinf(pa) * pr * tilt,
				   pv, pm);
			}
			if (f->kind == 0)
				us_point(c, hx, hy, hr * 0.45f, pv, pm);
			else
				us_point(c, hx, hy, hr * 0.22f, 0.85f, f->kind == 2 ? US_MAT_LIGHT : pm);
		}
	}

	/* ---- a frog on the lip of the near puddle: its throat sac sings with the low mids, it leaps in on a
	 * snare and swims back with only its eyes above the water ---- */
	if (snowy < 0.3f && st->freeze < 0.3f && st->npud > 0) {
		const pud_t *p = &st->pd[near_p];
		float z = p->z, F = 0.045f * z, side = st->frog_side, dir = -side;
		float sx = st->px[near_p] + side * st->prx[near_p] * 1.0f, sy = st->py[near_p] + st->pry[near_p] * 0.25f;
		float ex = st->px[near_p] + side * st->prx[near_p] * 0.45f, ey = st->py[near_p] - st->pry[near_p] * 0.1f;
		st->frog_cool -= dt;
		st->frog_t += dt;
		/* a hit in the low mids that is not the kick (snare body, a bass stab, a tom) */
		int lm_hit = (btrig & (0x3fu << 10)) && !kick_hit && a->kick < 0.4f;
		if (st->frog_st == 0 && lm_hit && st->frog_cool <= 0.0f && us_rand(r) < 0.15f) {
			st->frog_st = 1;
			st->frog_t = 0;
		}
		/* throat: breathes slowly in silence, balloons with the low mids */
		float th_t = 0.12f + 0.06f * sinf(t * 2.3f) + 0.9f * st->lm_s * st->lm_s;
		st->frog_th = us_approach(st->frog_th, th_t, th_t > st->frog_th ? 0.06f : 0.2f, rdt);
		if (st->frog_st == 1) {
			float u = st->frog_t / 0.4f;
			if (u >= 1.0f) {
				ring_t *g = add_ring(st, ex / A, ey, z, fminf(st->prx[near_p] * 0.4f, 0.01f + 0.04f * z));
				if (g)
					g->kind = 2;
				st->frog_st = 2;
				st->frog_t = 0;
			} else {
				/* stretched out mid-leap */
				float jx = us_lerp(sx, ex, u), jy = us_lerp(sy, ey, u) - 0.07f * z * sinf(US_PI * u);
				float ang = (u - 0.5f) * 1.2f;
				float cx = cosf(ang) * dir, cy = sinf(ang);
				hl(c, jx - cx * F * 0.45f, jy - cy * F * 0.45f, jx + cx * F * 0.45f, jy + cy * F * 0.45f, 0.6f, US_MAT_LEAF);
				hl(c, jx - cx * F * 0.45f, jy - cy * F * 0.45f, jx - cx * F * 1.1f, jy - cy * F * 0.8f + F * 0.15f, 0.5f, US_MAT_LEAF);
				us_point(c, jx, jy, ctx->crisp ? 0.002f : F * 0.22f, 0.55f, US_MAT_LEAF);
			}
		} else if (st->frog_st == 2) {
			float u = st->frog_t / 3.2f;
			if (u >= 1.0f) {
				st->frog_st = 0;
				st->frog_cool = 4.0f;
			} else {
				/* two eyes gliding back to the bank, a V of wake behind them */
				float q = us_smooth(0.0f, 1.0f, u);
				float hx = us_lerp(ex, sx - side * F * 0.4f, q), hy = us_lerp(ey, sy, q);
				float wv = 0.5f * (1.0f - u) + 0.15f;
				us_point(c, hx - F * 0.12f, hy - F * 0.08f, ctx->crisp ? 0.001f : F * 0.07f, 0.8f, US_MAT_LEAF);
				us_point(c, hx + F * 0.12f, hy - F * 0.08f, ctx->crisp ? 0.001f : F * 0.07f, 0.8f, US_MAT_LEAF);
				if (!ctx->crisp) {
					float wl = F * (1.2f + 0.6f * sinf(t * 5.0f));
					hl(c, hx, hy, hx + side * wl, hy - wl * 0.18f, wv, US_MAT_WATER);
					hl(c, hx, hy, hx + side * wl, hy + wl * 0.18f, wv, US_MAT_WATER);
				}
			}
		} else {
			/* sitting on the bank in profile, facing the water */
			float X = sx, Y = sy;
			if (ctx->crisp) {
				us_disc(c, X - dir * F * 0.08f, Y - F * 0.2f, F * 0.32f, 0.75f, US_MAT_LEAF);
				us_disc(c, X + dir * F * 0.25f, Y - F * 0.28f, F * 0.18f, 0.8f, US_MAT_LEAF);
				us_put(c, (int)((X + dir * F * 0.22f) * c->s), (int)((Y - F * 0.45f) * c->s), 0.9f, US_MAT_LIGHT);
				if (st->frog_th > 0.3f)
					us_disc(c, X + dir * F * 0.32f, Y - F * 0.08f, F * (0.06f + 0.18f * st->frog_th), 0.75f, US_MAT_BLOOM);
			} else {
				static const float body[] = {-0.45f, -0.02f, -0.5f, -0.16f, -0.34f, -0.36f, -0.06f, -0.42f, 0.12f, -0.4f,
							     0.3f,  -0.34f, 0.46f, -0.22f, 0.38f, -0.14f, 0.24f, -0.1f};
				static const float leg[] = {-0.38f, -0.06f, -0.06f, -0.2f, -0.06f, 0.0f, 0.1f, 0.0f};
				static const float arm[] = {0.18f, -0.12f, 0.22f, 0.0f, 0.32f, 0.0f};
				float pts[18];
				for (int k = 0; k < 9; k++) {
					pts[2 * k] = X + dir * body[2 * k] * F;
					pts[2 * k + 1] = Y + body[2 * k + 1] * F;
				}
				hpoly(c, pts, 9, 0.55f, US_MAT_LEAF);
				for (int k = 0; k < 4; k++) {
					pts[2 * k] = X + dir * leg[2 * k] * F;
					pts[2 * k + 1] = Y + leg[2 * k + 1] * F;
				}
				hpoly(c, pts, 4, 0.45f, US_MAT_LEAF);
				for (int k = 0; k < 3; k++) {
					pts[2 * k] = X + dir * arm[2 * k] * F;
					pts[2 * k + 1] = Y + arm[2 * k + 1] * F;
				}
				hpoly(c, pts, 3, 0.45f, US_MAT_LEAF);
				ellipse(c, X + dir * 0.17f * F, Y - 0.44f * F, 0.08f * F, 0.07f * F, US_PI, US_TAU, 0.6f, US_MAT_LEAF);
				us_point(c, X + dir * 0.18f * F, Y - 0.43f * F, F * 0.035f, 0.9f, US_MAT_LIGHT);
				float tr = F * (0.04f + 0.15f * st->frog_th);
				ellipse(c, X + dir * (0.3f + 0.3f * tr / F) * F, Y - 0.1f * F + tr * 0.3f, tr, tr * 0.8f, 0, US_TAU,
					0.35f + 0.5f * us_sat(st->frog_th), US_MAT_BLOOM);
			}
		}
	}

	/* ---- the fence along the track: posts receding, wires beaded with rain that drips off ---- */
	{
		float zp[NPOST], xp[NPOST], yp[NPOST], hp[NPOST];
		for (int k = 0; k < NPOST; k++) {
			float z = 1.08f / (1.0f + k * 0.5f);
			zp[k] = z;
			xp[k] = track_x(st, A, z) + st->fence_side * (0.62f * track_w(z) + 0.012f * z);
			yp[k] = ground_y(z);
			hp[k] = 0.13f * z;
		}
		uint8_t pm = US_MAT_EARTH;
		for (int k = 0; k < NPOST; k++) {
			float z = zp[k];
			if (ctx->crisp)
				us_line(c, xp[k], yp[k], xp[k], yp[k] - hp[k], 0.001f + 0.006f * z, 0.42f, pm);
			else {
				us_line(c, xp[k], yp[k], xp[k], yp[k] - hp[k], 0.0008f + 0.0045f * z, 0.3f + 0.15f * z, pm);
				hl(c, xp[k] - 0.0025f * z, yp[k] - hp[k], xp[k] + 0.0025f * z, yp[k] - hp[k], 0.5f, pm);
			}
			if (snowy > 0.3f)
				us_point(c, xp[k], yp[k] - hp[k] - 0.002f * z, 0.001f + 0.003f * z, 0.75f * snowy, US_MAT_SNOW);
		}
		/* hats: each near post answers its own band of the highs with a crown of spray off its cap */
		const int CP = 2; /* the crow's post */
		for (int k = 1; k <= 6; k++) {
			int b = 23 + k;
			float z = zp[k], top = yp[k] - hp[k];
			/* busy hats keep the posts spraying; a sharp one fires at once */
			st->crown_acc[k] += rdt * 3.0f * a->bands[b] * a->bands[b] * a->bands[b];
			int fire = ((btrig >> b) & 1u) || st->crown_acc[k] >= 1.0f;
			if (fire)
				st->crown_acc[k] = 0.0f;
			if (fire && liquid > 0.5f && !(k == CP && st->crow_fly <= 0.0f)) {
				st->crown[k] = 1.0f;
				splash(st, xp[k] / A, top, z * 0.8f, 0.0f, 3);
			}
			st->crown[k] = us_approach(st->crown[k], 0.0f, 0.12f, rdt);
			if (st->crown[k] < 0.04f)
				continue;
			float cv = st->crown[k], hh = 0.011f * z * (1.2f - cv), sp = 0.003f * z;
			if (ctx->crisp) {
				us_put(c, (int)((xp[k] - sp) * c->s), (int)((top - hh) * c->s), 0.8f * cv, US_MAT_WATER);
				us_put(c, (int)((xp[k] + sp) * c->s), (int)((top - hh) * c->s), 0.8f * cv, US_MAT_WATER);
			} else
				for (int q = -2; q <= 2; q++) {
					float bx = xp[k] + q * sp * 0.6f;
					hl(c, bx, top, bx + q * sp * 0.7f, top - hh * (1.0f - 0.12f * q * q), 0.85f * cv, US_MAT_WATER);
				}
		}

		/* an orb web in the corner of the near span, beaded with rain that the air band lights */
		if (!ctx->crisp && snowy < 0.4f) {
			float wy1 = yp[1] - hp[1] * 0.55f; /* the lower wire at the post */
			float R = 0.32f * (yp[1] - wy1);
			if (fabsf(xp[2] - xp[1]) > R * 2.5f) {
				float ox = (xp[2] > xp[1] ? 1.0f : -1.0f) * R * 1.15f; /* centre sits off the post */
				float cx0 = xp[1] + ox, cy0 = us_lerp(yp[1], wy1, 0.52f);
				cx0 += 0.0015f * us_noise2(t * 0.7f, 3.3f) + 0.004f * st->kgs * (ox > 0 ? 1.0f : -1.0f);
				const int NSP = 11;
				float sv = 0.13f + 0.06f * st->wet;
				for (int k = 0; k < NSP; k++) {
					float aa = US_TAU * k / NSP + 0.2f;
					float rr = R * (0.85f + 0.15f * sinf(k * 2.7f));
					hl(c, cx0, cy0, cx0 + cosf(aa) * rr, cy0 + sinf(aa) * rr * 0.92f, sv, US_MAT_STEM);
				}
				/* anchor threads: to the post, the wire and the grass */
				hl(c, cx0 - ox * 0.85f, cy0 - R * 0.2f, xp[1], cy0 - R * 0.3f, sv, US_MAT_STEM);
				hl(c, cx0, cy0 - R * 0.9f, cx0 + ox * 0.2f, wy1, sv, US_MAT_STEM);
				hl(c, cx0 + ox * 0.3f, cy0 + R * 0.85f, cx0 + ox * 0.5f, yp[1] - 0.004f, sv * 0.8f, US_MAT_STEM);
				float pts[2 * 61];
				int n = 0;
				for (int k = 0; k <= 60; k++) {
					float aa = US_TAU * k / 12.0f + 0.2f, rr = R * (0.18f + 0.72f * k / 60.0f);
					pts[2 * n] = cx0 + cosf(aa) * rr;
					pts[2 * n + 1] = cy0 + sinf(aa) * rr * 0.92f;
					n++;
				}
				hpoly(c, pts, n, sv * 0.9f, US_MAT_STEM);
				for (int k = 0; k < 14; k++) {
					float h1 = sinf(k * 12.9898f + 4.1f) * 43758.5f;
					h1 -= floorf(h1);
					float aa = US_TAU * (int)(h1 * NSP) / NSP + 0.2f, rr = R * (0.3f + 0.6f * (k / 14.0f));
					float gl = st->air_s * us_sat(sinf(t * 1.9f + k * 2.3f) * 1.4f);
					us_point(c, cx0 + cosf(aa) * rr, cy0 + sinf(aa) * rr * 0.92f, 0.0011f + 0.0008f * gl,
						 us_sat(0.3f + 0.7f * gl + 0.15f * st->wet), US_MAT_WATER);
				}
			}
		}

		/* two wires, sagging between the posts */
		float wl[2] = {0.9f, 0.55f};
		for (int wi = 0; wi < 2; wi++)
			for (int k = 0; k + 1 < NPOST; k++) {
				float pts[2 * 7];
				float sag = 0.012f * zp[k + 1];
				for (int i = 0; i <= 6; i++) {
					float f = i / 6.0f;
					pts[2 * i] = us_lerp(xp[k], xp[k + 1], f);
					pts[2 * i + 1] = us_lerp(yp[k] - hp[k] * wl[wi], yp[k + 1] - hp[k + 1] * wl[wi], f) + sag * 4.0f * f * (1.0f - f);
				}
				if (ctx->crisp)
					us_polyline(c, pts, 7, 0.001f, 0.3f, US_MAT_STEM);
				else
					hpoly(c, pts, 7, 0.22f + 0.12f * zp[k], US_MAT_STEM);
			}
		/* beads: gather on the wires while it rains, grow, slide to the sag and drip */
		st->bead_acc += dt * (1.5f + 30.0f * st->rain_amt) * liquid * (1.0f - st->freeze);
		while (st->bead_acc >= 1.0f && st->nbd < MAX_BEAD) {
			st->bead_acc -= 1.0f;
			bead_t *b = &st->bd[st->nbd++];
			b->span = (int)(powf(us_rand(r), 1.8f) * (NPOST - 1));
			b->f = us_randr(r, 0.05f, 0.95f);
			b->size = 0.1f;
			b->wire = us_rand(r) < 0.6f ? 0.0f : 1.0f;
		}
		if (st->bead_acc > 1.0f)
			st->bead_acc = 1.0f;
		for (int i = 0; i < st->nbd;) {
			bead_t *b = &st->bd[i];
			int k = b->span;
			b->size += dt * (0.15f + 0.8f * st->rain_amt) * liquid;
			b->f += (0.5f - b->f) * dt * 0.25f * b->size;
			float wv = b->wire ? wl[1] : wl[0];
			float sag = 0.012f * zp[k + 1];
			float x = us_lerp(xp[k], xp[k + 1], b->f);
			float y = us_lerp(yp[k] - hp[k] * wv, yp[k + 1] - hp[k + 1] * wv, b->f) + sag * 4.0f * b->f * (1.0f - b->f);
			float z = us_lerp(zp[k], zp[k + 1], b->f);
			if (b->size >= 1.0f || st->freeze > 0.5f) {
				/* it lets go */
				if (st->nsp < MAX_SPL && st->freeze < 0.5f) {
					spl_t *s = &st->sp[st->nsp++];
					s->u = x / A;
					s->y = y + 0.002f;
					s->z = z;
					s->vx = 0;
					s->vy = 0.05f;
					s->life = 1.0f;
				}
				*b = st->bd[--st->nbd];
				continue;
			}
			float br = (0.0007f + 0.0026f * z) * (0.5f + 0.5f * b->size);
			/* the air band catches them: a glint runs along the wire */
			float glint = st->air_s * (0.5f + 0.5f * sinf(t * 2.7f + i * 1.9f));
			us_point(c, x, y + br * 0.6f, ctx->crisp ? 0.001f : br, us_sat(0.5f + 0.35f * b->size + 0.5f * glint), US_MAT_WATER);
			i++;
		}

		/* the crow: perched on a post it looks about and shuffles its wings on the beats; the first hit
		 * after a lull (the drop landing) startles it into a wide loop over the meadow before it settles */
		{
			float z = zp[CP], L = 0.07f * z;
			float px0 = xp[CP], py0 = yp[CP] - hp[CP] - 0.0015f * z;
			st->crow_cool -= dt;
			st->crow_flap = us_approach(st->crow_flap, 0.0f, 0.22f, rdt);
			if (st->crow_fly <= 0.0f) {
				if (a->beat)
					st->crow_flap = us_sat(st->crow_flap + 0.3f + 0.4f * a->onset);
				if (a->beat && st->crow_cool <= 0.0f && (lull > 2.5f || (a->onset > 0.95f && us_rand(r) < 0.02f))) {
					st->crow_fly = 1e-4f;
					st->crow_cool = 20.0f;
					st->crow_fdir = us_noise2(t * 0.13f, st->so + 9.0f) > 0.0f ? 1.0f : -1.0f;
				}
			}
			if (st->crow_fly > 0.0f) {
				st->crow_fly += dt / 7.0f;
				st->crow_wing += dt * (10.0f + 5.0f * a->level) * (st->crow_fly < 0.85f ? 1.0f : 0.6f);
				if (st->crow_fly >= 1.0f) {
					st->crow_fly = 0.0f;
					st->crow_flap = 1.0f; /* folds its wings on landing */
				}
			}
			if (st->crow_fly > 0.0f) {
				float sf = st->crow_fly, e = us_smooth(0.0f, 0.12f, sf) * us_smooth(1.0f, 0.88f, sf);
				float ox = st->crow_fdir * 0.32f * sinf(US_TAU * sf) * (A / 1.78f);
				float oy = -0.3f * sinf(US_PI * sf) - 0.04f * sinf(US_TAU * sf);
				float bx = px0 + ox, by = py0 + oy - 0.01f * e;
				float vx = st->crow_fdir * cosf(US_TAU * sf);
				float dir = vx >= 0.0f ? 1.0f : -1.0f;
				float wv = sinf(st->crow_wing) * (sf > 0.9f ? 0.4f : 1.0f);
				float Lf = L * (1.0f - 0.25f * sinf(US_PI * sf)); /* a little farther off at the top of the loop */
				float ex = Lf * 0.28f, tx = Lf * 0.6f;
				uint8_t cm = US_MAT_ACCENT;
				if (ctx->crisp)
					dark_line(c, bx - dir * Lf * 0.3f, by, bx + dir * Lf * 0.3f, by - Lf * 0.04f, 10, US_MAT_BG);
				else {
					hl(c, bx - dir * Lf * 0.3f, by, bx + dir * Lf * 0.28f, by - Lf * 0.04f, 0.6f, cm);
					us_point(c, bx + dir * Lf * 0.3f, by - Lf * 0.05f, Lf * 0.06f, 0.6f, cm);
				}
				for (int sd = -1; sd <= 1; sd += 2) {
					float elx = bx + sd * ex, ely = by - Lf * (0.12f * wv + 0.04f);
					float tpx = bx + sd * tx, tpy = by - Lf * (0.42f * wv - 0.06f);
					if (ctx->crisp) {
						dark_line(c, bx, by, elx, ely, 10, US_MAT_BG);
						dark_line(c, elx, ely, tpx, tpy, 10, US_MAT_BG);
					} else {
						hl(c, bx, by, elx, ely, 0.6f, cm);
						hl(c, elx, ely, tpx, tpy, 0.5f, cm);
					}
				}
			} else {
				/* looks one way, then the other */
				float dir = us_noise2(t * 0.25f, st->so + 3.0f) > -0.1f ? 1.0f : -1.0f;
				float bob = 0.05f * st->crow_flap;
				static const float cb[] = {-0.62f, -0.16f, -0.3f, -0.3f, -0.05f, -0.44f, 0.16f, -0.56f, 0.42f, -0.49f,
							   0.19f,  -0.44f, 0.14f, -0.28f, -0.02f, -0.11f, -0.28f, -0.17f, -0.62f, -0.16f};
				float pts[20];
				for (int k = 0; k < 10; k++) {
					float hx = cb[2 * k], hy = cb[2 * k + 1];
					if (k >= 3 && k <= 5) { /* head bobs forward on a shuffle */
						hx += bob;
						hy += bob * 0.5f;
					}
					pts[2 * k] = px0 + dir * hx * L;
					pts[2 * k + 1] = py0 + hy * L;
				}
				uint8_t cm = US_MAT_ACCENT;
				float th = st->crow_flap * 1.5f; /* wing lifts off the back */
				float wx0 = -0.44f, wy0 = 0.2f;
				float wtx = (-0.04f + wx0 * cosf(th) - wy0 * sinf(th)), wty = (-0.4f + wx0 * sinf(th) + wy0 * cosf(th));
				if (ctx->crisp) {
					/* a black bird: darker than the field behind it */
					fill_dark(c, pts, 9, 10, US_MAT_BG);
					if (st->crow_flap > 0.25f) {
						float wp[8] = {px0 - dir * 0.04f * L, py0 - 0.42f * L, px0 + dir * wtx * L, py0 + wty * L,
							       px0 - dir * 0.3f * L, py0 - 0.2f * L, px0 - dir * 0.04f * L, py0 - 0.3f * L};
						fill_dark(c, wp, 4, 10, US_MAT_BG);
					}
				} else {
					hpoly(c, pts, 10, 0.6f, cm);
					hl(c, px0 - dir * 0.04f * L, py0 - 0.4f * L, px0 + dir * wtx * L, py0 + wty * L, 0.5f, cm);
					hl(c, px0 + dir * wtx * L, py0 + wty * L, px0 - dir * 0.3f * L, py0 - 0.2f * L, 0.35f, cm);
					hl(c, px0, py0 - 0.11f * L, px0 - dir * 0.02f * L, py0, 0.5f, cm);
					hl(c, px0 + dir * 0.07f * L, py0 - 0.11f * L, px0 + dir * 0.07f * L, py0, 0.5f, cm);
					us_point(c, px0 + dir * (0.22f + bob) * L, py0 - (0.5f - bob * 0.5f) * L, L * 0.03f, 0.95f, US_MAT_LIGHT);
				}
			}
		}
	}

	/* ---- rain + snow ---- */
	for (int i = 0; i < st->nd;) {
		drop_t *d = &st->d[i];
		float x = d->u * A;
		float vx;
		if (d->snow) {
			float flutter = sinf(t * (1.2f + d->z) + d->ph) * (0.015f + 0.03f * d->z);
			vx = slant * 0.3f * (0.3f + 0.7f * d->z) + flutter + 0.03f * us_noise2(x * 2.0f, t * 0.3f + d->ph);
		} else {
			vx = slant * d->vy;
		}
		x += vx * dt;
		d->y += d->vy * dt;
		d->u = x / A;
		if (d->y >= d->land) {
			float nu = d->u;
			if (nu >= -0.02f && nu <= 1.02f) {
				int hit = -1;
				for (int k = 0; k < st->npud; k++) {
					float qx = (x - st->px[k]) / st->prx[k], qy = (d->land - st->py[k]) / st->pry[k];
					if (qx * qx + qy * qy < 1.0f) {
						hit = k;
						break;
					}
				}
				if (d->snow) {
					if (d->z > 0.55f) {
						int k = (int)(us_clampf(nu, 0, 1) * (NDRIFT - 1) + 0.5f);
						st->drift[k] = us_clampf(st->drift[k] + 0.0016f * d->z * (0.5f + st->snow_cov), 0, 0.1f);
					}
				} else if (hit >= 0 && st->freeze < 0.5f) {
					/* the ring may not run past the puddle's edge */
					float qx = (x - st->px[hit]) / st->prx[hit], qy = (d->land - st->py[hit]) / st->pry[hit];
					float room = (1.0f - sqrtf(qx * qx + qy * qy)) * st->prx[hit];
					float rmax = fminf((0.012f + 0.055f * d->z) * us_randr(r, 0.7f, 1.2f), room + 0.004f * d->z);
					add_ring(st, nu, d->land, d->z, rmax);
					} else if (st->freeze < 0.5f && st->wet > 0.35f && d->z > 0.1f &&
						   fabsf(fabsf(x - track_x(st, A, d->z)) - 0.32f * track_w(d->z)) < 0.03f * track_w(d->z) + 0.002f) {
						/* into the water standing in a wheel rut */
						add_ring(st, nu, d->land, d->z, 0.004f + 0.014f * d->z);
					} else if (d->z > 0.25f && us_rand(r) < 0.5f + 0.5f * d->z) {
					splash(st, nu, d->land, d->z, vx, d->z > 0.6f ? 3 : 2);
				}
			}
			*d = st->d[--st->nd];
			continue;
		}
		if (x < -0.7f || x > A + 0.7f) {
			*d = st->d[--st->nd];
			continue;
		}
		if (d->y > -0.02f) {
			if (d->snow) {
					float sz = 0.001f + 0.004f * d->z * d->z;
					float v = 0.62f + 0.38f * d->z;
					if (ctx->crisp && snowy > 0.5f && d->y > HZ)
						v = 1.0f; /* the top step: still reads against the bright snowfield */
				d->wx = x;
				d->wy = d->y;
				d->wr = sz * 1.8f + 1.5f / c->s;
				if (!ctx->crisp && d->z > 0.7f) {
					/* a six-armed star, turning slowly */
					float rot = d->ph + t * 0.5f;
					for (int k = 0; k < 3; k++) {
						float aa = rot + k * US_PI / 3.0f;
						float cx = cosf(aa) * sz * 1.6f, cy = sinf(aa) * sz * 1.6f;
						hl(c, x - cx, d->y - cy, x + cx, d->y + cy, v, US_MAT_SNOW);
					}
				} else {
					us_point(c, x, d->y, sz, v, US_MAT_SNOW);
				}
			} else {
				/* streak = distance travelled in a short exposure, along the wind */
				float ex = 0.014f + 0.022f * d->z;
				float len_x = vx * ex, len_y = d->vy * ex;
				float v = 0.18f + 0.65f * d->z * d->z;
				if (d->z > 0.7f && !ctx->crisp)
					us_line(c, x - len_x, d->y - len_y, x, d->y, 0.0011f + 0.0009f * d->z, v, US_MAT_WATER);
				else
					hl(c, x - len_x, d->y - len_y, x, d->y, v, US_MAT_WATER);
			}
		}
		i++;
	}

	/* ---- splash droplets ---- */
	for (int i = 0; i < st->nsp;) {
		spl_t *s = &st->sp[i];
		s->vy += 1.8f * dt;
		s->u += s->vx * dt / A;
		s->y += s->vy * dt;
		s->life -= dt * 3.2f;
		if (s->life <= 0) {
			*s = st->sp[--st->nsp];
			continue;
		}
		float v = (0.4f + 0.5f * s->z) * s->life;
		if (ctx->crisp)
			us_point(c, s->u * A, s->y, 0.001f, v, US_MAT_WATER);
		else
			hl(c, s->u * A, s->y, s->u * A - s->vx * 0.02f, s->y - s->vy * 0.02f, v, US_MAT_WATER);
		i++;
	}

	/* ---- the snow drift along the foreground: settles, smooths, melts out of winter ---- */
	{
		float melt = 0.0012f * (1.0f - winter) * (1.0f + 2.0f * st->rain_amt * liquid);
		float any = 0;
		for (int i = 0; i < NDRIFT; i++) {
			st->drift[i] = us_clampf(st->drift[i] - melt * dt, 0, 0.1f);
			if (i > 0 && i < NDRIFT - 1)
				st->drift[i] = us_lerp(st->drift[i], 0.5f * (st->drift[i - 1] + st->drift[i + 1]), dt * 0.8f);
			any += st->drift[i];
		}
		if (any > 0.002f) {
			float top[2 * NDRIFT];
			for (int i = 0; i < NDRIFT; i++) {
				float u = (float)i / (NDRIFT - 1);
				top[2 * i] = u * A;
				top[2 * i + 1] = 1.0f - st->drift[i];
			}
			if (ctx->crisp) {
				for (int i = 0; i < NDRIFT - 1; i++) {
					us_tri(c, top[2 * i], top[2 * i + 1], top[2 * i + 2], top[2 * i + 3], top[2 * i], 1.01f, 0.62f, US_MAT_SNOW);
					us_tri(c, top[2 * i + 2], top[2 * i + 3], top[2 * i + 2], 1.01f, top[2 * i], 1.01f, 0.62f, US_MAT_SNOW);
				}
			} else {
				for (int i = 0; i < NDRIFT - 1; i++) {
					float dp = st->drift[i];
					if (dp < 0.0008f)
						continue;
					hl(c, top[2 * i], top[2 * i + 1], top[2 * i + 2], top[2 * i + 3], us_sat(0.4f + 20.0f * dp), US_MAT_SNOW);
					/* wind-carved strata in the drift */
					hl(c, top[2 * i], 1.0f - dp * 0.55f, top[2 * i + 2], 1.0f - st->drift[i + 1] * 0.55f, 0.22f, US_MAT_SNOW);
				}
			}
		}
	}

	/* ---- foreground grass at the bottom edge, swaying with the wind, buried by the drift ---- */
	{
		int nb = (int)(MAX_BLADES * (0.3f + 0.7f * dens));
		uint8_t bm = winter > 0.6f ? US_MAT_STEM : US_MAT_LEAF;
		for (int i = 0; i < nb; i++) {
			float u = st->bx[i], x = u * A;
			float base = 1.005f;
			float h = (0.03f + 0.09f * st->bh[i]) * (1.0f - 0.4f * winter) - drift_at(st, u) * 1.1f;
			if (h < 0.006f)
				continue;
			float sw = (0.12f + 1.1f * st->wind_s * (0.5f + gust) + 0.9f * st->kgs) * (0.6f + 0.4f * us_noise2(x * 3.0f - st->gust_t, st->bp[i]));
			float pts[10];
			for (int k = 0; k < 5; k++) {
				float f = k / 4.0f;
				pts[2 * k] = x + sw * h * f * f * 0.8f;
				pts[2 * k + 1] = base - h * f * (1.0f - 0.2f * sw * f);
			}
			float v = 0.3f + 0.25f * st->bh[i];
			if (ctx->crisp)
				us_polyline(c, pts, 5, 0.001f, v * 0.8f, bm);
			else
				hpoly(c, pts, 5, v, bm);
		}
	}
}

const us_scene_vt us_scene_rain = {"rain", "Rain", 0.06f, create, destroy, frame};
