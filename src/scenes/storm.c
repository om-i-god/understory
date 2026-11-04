/* Understory scene: Storm Front — cloud masses roll over a low horizon; the music charges the sky.
 *
 * Clouds are a drifting, sheared, domain-warped fbm density field sampled on a coarse grid.
 *   Ink:   drawn as marching-squares isolines (a topographic map of the storm) with etched
 *          horizontal hatching in the dense cores.
 *   Crisp: drawn as posterised filled bands (dark slate -> storm blue), edges picked out.
 * Sustained energy (swell) builds storm_charge: the cloud deck lowers, thickens and darkens and
 * rain curtains fall from the heaviest columns. The biggest hits at high charge fire branching
 * lightning (recursive midpoint displacement with forks, 1-3 return strokes) which lights the
 * cloud undersides around the channel, then lingers as an afterimage. Medium hits at moderate
 * charge become cloud-to-cloud crawlers. In silence: distant heat lightning flickers inside the
 * clouds now and then, and the clouds keep drifting.
 * Under a heavy charge the deck's base sags into mammatus pouches and a tiered shelf cloud leads the
 * front. Smaller hits flicker deep inside the clouds (distant, unseen strokes). A near ridge with a
 * lone oak stands in the foreground, a black silhouette until a flash reveals it.
 * World: reads storm_charge, moisture, wind, daylight; writes storm_charge (up with swell, down
 *        with every strike) and moisture (rain).
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define GW 112
#define GH 48
#define SKY_Y 0.80f /* bottom of the cloud grid */
#define MAX_SEG 900
#define MAX_DROP 1400
#define MAX_TREES 7
#define MAX_TSEG 120
#define MAX_STROKES 4

typedef struct {
	float x0, y0, x1, y1, v, w;
} seg_t;

typedef struct {
	float x, y, vx, vy, len, v; /* x as fraction of aspect */
} drop_t;

typedef struct {
	float x, h;
	int nseg;
	float sx0[MAX_TSEG], sy0[MAX_TSEG], sx1[MAX_TSEG], sy1[MAX_TSEG], sw[MAX_TSEG];
} tree_t;

typedef struct {
	us_rng rng;
	float D[GW * GH];
	float col[GW];   /* column cloud weight near the base (rain source) */
	float colb[GW];  /* y of the cloud base per column */

	/* lightning */
	seg_t seg[MAX_SEG];
	int nseg;
	float bolt_t;    /* time since the strike */
	float stroke_t[MAX_STROKES];
	int nstrokes;
	float bolt_x, bolt_y; /* where the channel touches (for cloud lighting) */
	float bolt_amp;  /* 1 = cloud-to-ground, ~0.6 crawler */
	float cool;
	float flash;     /* current flash brightness 0..1+ */

	/* heat lightning */
	float heat_next, heat_t, heat_x, heat_y, heat_v;

	drop_t drop[MAX_DROP];
	int ndrop;

	tree_t tree[MAX_TREES];
	int ntrees;
	tree_t oak;   /* the big tree on the near ridge */
	float ridge_side; /* +1: the ridge rises on the right */
	float hit_x;  /* where the last ground strike landed */
	float heat_amp, small_cool;

	float drift, evo;
	float A; /* aspect cached for the recursive bolt builder */
	float ch_vis;  /* smoothed charge used for drawing */
	float rain;    /* smoothed rain intensity */
	int inited;
} st_t;

static float horizon_y(float u)
{
	/* u = x fraction 0..1 */
	return 0.835f + 0.018f * us_noise2(u * 2.3f, 7.7f) + 0.007f * us_noise2(u * 9.0f, 3.1f);
}

/* ---- trees on the horizon (tiny static silhouettes, they give scale) ---- */
static void tree_branch_d(tree_t *t, us_rng *r, float x, float y, float ang, float len, float w, int depth, int maxd)
{
	if (t->nseg >= MAX_TSEG || depth > maxd || len < 0.003f)
		return;
	float ex = x + cosf(ang) * len, ey = y + sinf(ang) * len;
	int i = t->nseg++;
	t->sx0[i] = x;
	t->sy0[i] = y;
	t->sx1[i] = ex;
	t->sy1[i] = ey;
	t->sw[i] = w;
	int kids = depth == 0 ? 3 : 2;
	for (int k = 0; k < kids; k++) {
		float a = ang + us_randr(r, -0.75f, 0.75f) + (k - (kids - 1) * 0.5f) * 0.45f;
		tree_branch_d(t, r, ex, ey, a, len * us_randr(r, 0.55f, 0.78f), w * 0.65f, depth + 1, maxd);
	}
}

static void tree_branch(tree_t *t, us_rng *r, float x, float y, float ang, float len, float w, int depth)
{
	tree_branch_d(t, r, x, y, ang, len, w, depth, 4);
}

/* the near ridge: a low rise toward one side of the frame */
static float ridge_y(float u, float side)
{
	float hill = us_smooth(0.3f, 0.92f, side > 0 ? u : 1.0f - u);
	return 0.975f - 0.15f * hill + 0.012f * us_noise2(u * 4.0f, 2.7f) + 0.004f * us_noise2(u * 17.0f, 1.3f);
}

/* fast thin antialiased line for ink (two-pixel Wu spans); us_line for crisp / wide lines */
static void wline(us_canvas *c, float ax, float ay, float bx, float by, float width, float v, uint8_t mat)
{
	float wp = width * c->s;
	if (c->crisp || wp > 1.6f) {
		us_line(c, ax, ay, bx, by, width, v, mat);
		return;
	}
	v *= us_clampf(wp, 0.35f, 1.0f);
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
	int lim = steep ? c->h : c->w;
	if (i0 < 0) i0 = 0;
	if (i1 > lim - 1) i1 = lim - 1;
	for (int i = i0; i <= i1; i++) {
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

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x73746f72u);
	s->ntrees = 2 + (int)(us_rand(&s->rng) * 4);
	for (int i = 0; i < s->ntrees; i++) {
		tree_t *t = &s->tree[i];
		t->x = us_randr(&s->rng, 0.05f, 0.95f);
		t->h = us_randr(&s->rng, 0.025f, 0.05f);
		tree_branch(t, &s->rng, 0, 0, -US_PI * 0.5f + us_randn(&s->rng) * 0.05f, t->h * 0.45f, 1.0f, 0);
	}
	s->ridge_side = us_rand(&s->rng) < 0.5f ? -1.0f : 1.0f;
	tree_branch_d(&s->oak, &s->rng, 0, 0, -US_PI * 0.5f + us_randn(&s->rng) * 0.06f, 0.085f, 1.0f, 0, 5);
	s->heat_amp = 1.0f;
	s->heat_next = us_randr(&s->rng, 2.0f, 6.0f);
	s->evo = us_rand(&s->rng) * 100.0f;
	s->drift = us_rand(&s->rng) * 50.0f;
	return s;
}

static void destroy(void *p) { free(p); }

/* ---- lightning ------------------------------------------------------- */
static void add_seg(st_t *s, float x0, float y0, float x1, float y1, float v, float w)
{
	if (s->nseg >= MAX_SEG)
		return;
	seg_t *g = &s->seg[s->nseg++];
	g->x0 = x0;
	g->y0 = y0;
	g->x1 = x1;
	g->y1 = y1;
	g->v = v;
	g->w = w;
}

static void bolt(st_t *s, float x0, float y0, float x1, float y1, float disp, int depth, float v, float w, float fork_p)
{
	if (depth <= 0) {
		add_seg(s, x0, y0, x1, y1, v, w);
		return;
	}
	float dx = x1 - x0, dy = y1 - y0;
	float len = sqrtf(dx * dx + dy * dy) + 1e-6f;
	float nx = -dy / len, ny = dx / len;
	float o = us_randn(&s->rng) * disp;
	float mx = (x0 + x1) * 0.5f + nx * o, my = (y0 + y1) * 0.5f + ny * o;
	bolt(s, x0, y0, mx, my, disp * 0.5f, depth - 1, v, w, fork_p);
	bolt(s, mx, my, x1, y1, disp * 0.5f, depth - 1, v, w, fork_p);
	/* forks peel off the midpoints, angled away from the main channel and drooping */
	if (depth >= 2 && us_rand(&s->rng) < fork_p) {
		float ang = atan2f(dy, dx) + (us_rand(&s->rng) < 0.5f ? -1.0f : 1.0f) * us_randr(&s->rng, 0.35f, 0.9f);
		float fl = len * us_randr(&s->rng, 0.45f, 0.9f);
		float fx = mx + cosf(ang) * fl, fy = my + sinf(ang) * fl;
		float lim = horizon_y(us_sat(fx / s->A)) - 0.004f;
		if (fy > lim)
			fy = lim;
		if (fy < my - 0.05f)
			fy = my - 0.05f;
		bolt(s, mx, my, fx, fy, fl * 0.22f, depth - 1, v * 0.55f, w * 0.6f, fork_p * 0.75f);
	}
}

static void strike(st_t *s, float A, int ground, float dens, float energy)
{
	us_rng *r = &s->rng;
	s->nseg = 0;
	s->A = A;
	/* start under the heaviest column we can find from a few random tries */
	int best = (int)(us_rand(r) * GW);
	for (int k = 0; k < 6; k++) {
		int i = (int)(us_rand(r) * GW);
		if (s->col[i] > s->col[best])
			best = i;
	}
	float u = (best + 0.5f) / GW;
	float x0 = u * A, y0 = s->colb[best] - us_randr(r, 0.02f, 0.08f);
	float fork = 0.25f + 0.35f * dens + 0.15f * energy;
	if (ground) {
		float x1 = x0 + us_randn(r) * 0.12f;
		float u1 = us_sat(x1 / A);
		float y1 = horizon_y(u1);
		/* trees attract the strike */
		for (int i = 0; i < s->ntrees; i++)
			if (fabsf(s->tree[i].x * A - x1) < 0.06f) {
				x1 = s->tree[i].x * A;
				y1 = horizon_y(s->tree[i].x) - s->tree[i].h;
			}
		bolt(s, x0, y0, x1, y1, 0.16f * (y1 - y0 + 0.1f), 7, 1.0f, 1.0f, fork);
		s->hit_x = x1;
		s->bolt_x = x0;
		s->bolt_y = y0;
		s->bolt_amp = 1.0f;
	} else {
		/* cloud-to-cloud crawler: long, mostly horizontal, inside the deck */
		float dir = us_rand(r) < 0.5f ? -1.0f : 1.0f;
		float x1 = x0 + dir * us_randr(r, 0.3f, 0.7f);
		float y1 = y0 - us_randr(r, -0.05f, 0.1f);
		bolt(s, x0, y0, x1, y1, 0.07f, 6, 0.75f, 0.7f, fork * 1.3f);
		s->bolt_x = (x0 + x1) * 0.5f;
		s->bolt_y = y0;
		s->bolt_amp = 0.65f;
		s->hit_x = s->bolt_x;
	}
	s->bolt_t = 0;
	s->nstrokes = 1 + (int)(us_rand(r) * (ground ? 3.5f : 2.0f));
	if (s->nstrokes > MAX_STROKES)
		s->nstrokes = MAX_STROKES;
	s->stroke_t[0] = 0;
	for (int k = 1; k < s->nstrokes; k++)
		s->stroke_t[k] = s->stroke_t[k - 1] + us_randr(r, 0.05f, 0.14f);
}

/* ---- clouds ---------------------------------------------------------- */
static void cloud_field(st_t *s, float A, float ch, float cov)
{
	float base = 0.30f + 0.24f * ch; /* deck lowers as the charge builds */
	for (int j = 0; j < GH; j++) {
		float y = SKY_Y * j / (GH - 1);
		/* the deck drifts; lower layers lean ahead a little (bounded shear) so masses roll */
		float shear = s->drift + 0.35f * y * sinf(s->drift * 0.21f + 1.0f);
		/* density rises into the deck; noise carves it into rolling masses with scalloped bases */
		float grad = (base - y) * 1.1f;
		float prof = us_smooth(base + 0.2f, base - 0.06f, y);
		for (int i = 0; i < GW; i++) {
			float x = A * i / (GW - 1);
			float xx = x * 0.95f + shear;
			float wx = us_noise3(xx * 0.5f, y * 1.2f, s->evo * 0.5f) * 0.8f;
			float n = us_fbm3(xx + wx, y * 1.6f + 0.5f * wx, s->evo, 3);
			float d = us_sat(grad + n * 1.7f + cov) * prof;
			s->D[j * GW + i] = d;
		}
	}
	/* column weights near the base: rain source + strike origins */
	for (int i = 0; i < GW; i++) {
		float m = 0;
		int jb = 0;
		for (int j = 0; j < GH; j++) {
			float d = s->D[j * GW + i];
			float y = SKY_Y * j / (GH - 1);
			if (y > base + 0.16f)
				break;
			if (d > 0.35f)
				jb = j;
			if (d > m)
				m = d;
		}
		s->col[i] = m;
		s->colb[i] = SKY_Y * jb / (GH - 1);
	}
}

/* illumination from the active flash + heat lightning at (x,y) */
static float lit(const st_t *s, float x, float y, float base)
{
	float L = 0;
	if (s->flash > 0.001f) {
		float dx = x - s->bolt_x;
		float under = 0.45f + 0.55f * us_smooth(base - 0.35f, base + 0.05f, y);
		L += s->flash * s->bolt_amp * (0.3f + 1.2f * expf(-dx * dx / 0.12f)) * under;
	}
	if (s->heat_v > 0.001f) {
		float dx = x - s->heat_x, dy = (y - s->heat_y) * 1.6f;
		L += s->heat_v * expf(-(dx * dx + dy * dy) / 0.025f);
	}
	return L;
}

/* reveal = 1: only the lightning-lit parts of extra in-between levels (the flash shows the cloud's insides) */
static void draw_contours(st_t *s, us_canvas *c, float A, int nlev, float ch, float base, int reveal)
{
	float cw = A / (GW - 1), chh = SKY_Y / (GH - 1);
	for (int l = 0; l < nlev; l++) {
		float iso = (l + 1.0f) / (nlev + 1.0f);
		float lv = (float)l / (nlev > 1 ? nlev - 1 : 1);
		float vbase = reveal ? 0.0f : (0.22f + 0.45f * (1.0f - lv)) * (1.0f - 0.35f * ch);
		float wid = 0.0012f + 0.0006f * (1.0f - lv);
		if (reveal && ((l & 1) == 0))
			continue; /* odd levels only: they interleave with the regular set */
		for (int j = 0; j < GH - 1; j++) {
			for (int i = 0; i < GW - 1; i++) {
				float a = s->D[j * GW + i], b = s->D[j * GW + i + 1];
				float d = s->D[(j + 1) * GW + i + 1], e = s->D[(j + 1) * GW + i];
				int k = (a > iso) | ((b > iso) << 1) | ((d > iso) << 2) | ((e > iso) << 3);
				if (k == 0 || k == 15)
					continue;
				float x = i * cw, y = j * chh;
				/* edge points: top (a-b), right (b-d), bottom (e-d), left (a-e) */
				float px[4], py[4];
				px[0] = x + cw * (iso - a) / (b - a);
				py[0] = y;
				px[1] = x + cw;
				py[1] = y + chh * (iso - b) / (d - b);
				px[2] = x + cw * (iso - e) / (d - e);
				py[2] = y + chh;
				px[3] = x;
				py[3] = y + chh * (iso - a) / (e - a);
				static const signed char tab[16][4] = {
					{-1, -1, -1, -1}, {3, 0, -1, -1}, {0, 1, -1, -1}, {3, 1, -1, -1},
					{1, 2, -1, -1},   {3, 0, 1, 2},   {0, 2, -1, -1}, {3, 2, -1, -1},
					{3, 2, -1, -1},   {0, 2, -1, -1}, {0, 1, 2, 3},   {1, 2, -1, -1},
					{3, 1, -1, -1},   {0, 1, -1, -1}, {3, 0, -1, -1}, {-1, -1, -1, -1}};
				for (int q = 0; q < 4 && tab[k][q] >= 0; q += 2) {
					int e0 = tab[k][q], e1 = tab[k][q + 1];
					float mx = (px[e0] + px[e1]) * 0.5f, my = (py[e0] + py[e1]) * 0.5f;
					float L = lit(s, mx, my, base);
					if (reveal && L < 0.06f)
						continue;
					float v = vbase + L * (reveal ? 0.9f : 0.6f + 0.6f * (1.0f - lv));
					us_line(c, px[e0], py[e0], px[e1], py[e1], wid, us_sat(v), US_MAT_WATER);
				}
			}
		}
	}
}

/* etched hatching inside the dense cores (ink) */
static void draw_hatch(st_t *s, us_canvas *c, float A, float thr, int step, float ch, float base)
{
	float cw = A / (GW - 1), chh = SKY_Y / (GH - 1);
	for (int j = 1; j < GH; j += step) {
		float y = j * chh;
		int i = 0;
		while (i < GW) {
			while (i < GW && s->D[j * GW + i] <= thr)
				i++;
			if (i >= GW)
				break;
			int i0 = i;
			while (i < GW && s->D[j * GW + i] > thr)
				i++;
			float x0 = i0 * cw, x1 = (i - 1) * cw;
			if (x1 - x0 < cw * 1.5f)
				continue;
			float mx = (x0 + x1) * 0.5f;
			float v = 0.05f + 0.06f * ch + 0.6f * lit(s, mx, y, base);
			us_line(c, x0 + cw * 0.5f, y, x1 - cw * 0.5f, y, 0.0009f, us_sat(v), US_MAT_WATER);
		}
	}
}

/* posterised filled bands (crisp) — per-pixel bilinear on the grid */
static void draw_bands(st_t *s, us_canvas *c, float A, int nlev, float ch, float base)
{
	int ymax = (int)(SKY_Y * c->s);
	if (ymax > c->h)
		ymax = c->h;
	float fx_k = (GW - 1) / (A * c->s), fy_k = (GH - 1) / (SKY_Y * c->s);
	for (int py = 0; py < ymax; py++) {
		float fy = (py + 0.5f) * fy_k;
		int iy = (int)fy;
		if (iy >= GH - 1)
			iy = GH - 2;
		float ty = fy - iy;
		float y = (py + 0.5f) / c->s;
		for (int px = 0; px < c->w; px++) {
			float fx = (px + 0.5f) * fx_k;
			int ix = (int)fx;
			if (ix >= GW - 1)
				ix = GW - 2;
			float tx = fx - ix;
			const float *r0 = &s->D[iy * GW + ix], *r1 = r0 + GW;
			float d = us_lerp(us_lerp(r0[0], r0[1], tx), us_lerp(r1[0], r1[1], tx), ty);
			int band = (int)(d * (nlev + 1));
			if (band <= 0)
				continue;
			if (band > nlev)
				band = nlev;
			float bf = (float)band / nlev;
			float x = (px + 0.5f) / c->s;
			float L = lit(s, x, y, base);
			/* pale storm-blue rims, slate cores that darken as the charge builds */
			uint8_t m;
			float v;
			if (bf < 0.5f) {
				m = US_MAT_WATER;
				v = band == 1 ? 0.22f : 0.42f;
				v *= 1.0f - 0.35f * ch;
			} else {
				m = US_MAT_BG;
				v = 1.0f - (bf - 0.5f) * 1.3f * (0.6f + 0.6f * ch);
			}
			if (L > 0.08f) {
				m = US_MAT_WATER;
				v = (bf < 0.5f ? 0.35f : 0.2f) + 0.9f * L;
			}
			us_put(c, px, py, us_sat(v), m);
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

	/* ---- charge: swell builds it (world also builds it slowly), strikes discharge ---- */
	float drive = us_smooth(0.25f, 0.8f, a->swell) * 0.03f + 0.008f * a->level * a->active;
	us_world_add(w, &w->storm_charge, drive, dt);
	float ch = w->storm_charge;
	if (!s->inited) {
		s->ch_vis = ch;
		s->rain = us_sat(ch * 0.8f + w->moisture * 0.3f - 0.2f);
		s->inited = 1;
	}
	s->ch_vis = us_approach(s->ch_vis, ch, 2.5f, rdt);
	float cv = s->ch_vis;

	/* drift + slow evolution */
	float wind = 0.012f + 0.03f * w->wind + 0.03f * cv + 0.01f * a->level;
	s->drift += dt * wind;
	s->evo += dt * (0.025f + 0.05f * cv + 0.03f * a->swell);
	float cov = 0.10f + 0.25f * cv + 0.1f * w->moisture + 0.06f * dens;
	float base = 0.30f + 0.24f * cv;
	cloud_field(s, A, cv, cov);

	/* ---- lightning triggers ---- */
	s->cool -= rdt;
	s->bolt_t += rdt;
	int big = a->beat && (a->kick > 0.45f || a->onset > 0.75f);
	if (big && s->cool <= 0 && ch > 0.22f) {
		float pg = (ch - 0.18f) * 1.6f; /* cloud-to-ground odds */
		float u = us_rand(r);
		if (u < pg * 0.55f) {
			strike(s, A, 1, dens, a->onset);
			us_world_add(w, &w->storm_charge, -us_randr(r, 0.06f, 0.12f), 1.0f);
			s->cool = us_randr(r, 0.8f, 3.5f) * (1.25f - 0.6f * ch);
		} else if (u < pg) {
			strike(s, A, 0, dens, a->onset);
			us_world_add(w, &w->storm_charge, -0.03f, 1.0f);
			s->cool = us_randr(r, 0.5f, 2.0f) * (1.25f - 0.6f * ch);
		} else {
			s->cool = 0.3f;
		}
	}
	/* smaller hits: a flicker deep inside the deck (a stroke we do not see) */
	s->small_cool -= rdt;
	if (a->beat && !(big && s->cool > 0 && s->bolt_t < 0.05f) && s->bolt_t > 0.25f && a->onset > 0.2f &&
	    s->small_cool <= 0 && ch > 0.08f) {
		s->heat_t = 0;
		s->heat_x = us_randr(r, 0.05f, 0.95f) * A;
		s->heat_y = us_randr(r, 0.12f, base);
		s->heat_amp = (0.55f + 0.7f * a->onset) * (0.5f + 0.8f * ch);
		s->small_cool = us_randr(r, 0.25f, 0.6f);
	}
	/* flash envelope from the return strokes */
	s->flash = 0;
	if (s->nseg > 0)
		for (int k = 0; k < s->nstrokes; k++) {
			float tt = s->bolt_t - s->stroke_t[k];
			if (tt >= 0)
				s->flash += expf(-tt / 0.045f) * (k == 0 ? 1.0f : 0.8f);
		}
	if (s->flash > 1.2f)
		s->flash = 1.2f;

	/* ---- heat lightning: distant, in-cloud, the storm's breath in silence ---- */
	s->heat_next -= rdt;
	if (s->heat_next <= 0) {
		s->heat_t = 0;
		s->heat_x = us_randr(r, 0.1f, 0.9f) * A;
		s->heat_y = us_randr(r, 0.15f, base + 0.05f);
		float quiet = 1.0f - a->level;
		s->heat_next = us_randr(r, 3.0f, 12.0f) * (1.4f - 0.6f * quiet) * (1.2f - 0.5f * cv);
		s->heat_amp = 1.0f;
	}
	s->heat_t += rdt;
	{
		float ht = s->heat_t;
		float v = 0;
		if (ht < 0.7f)
			v = (expf(-ht / 0.06f) + 0.7f * (ht > 0.18f ? expf(-(ht - 0.18f) / 0.05f) : 0) +
			     0.5f * (ht > 0.4f ? expf(-(ht - 0.4f) / 0.08f) : 0)) *
			    0.5f;
		s->heat_v = v * (s->heat_amp < 1.0f ? s->heat_amp : (0.6f + 0.4f * (1.0f - a->level)) * s->heat_amp);
	}

	/* ---- clouds ---- */
	int nlev = 3 + (int)(dens * 4.0f + 0.5f);
	if (ctx->crisp) {
		draw_bands(s, c, A, nlev < 6 ? nlev : 6, cv, base);
	} else {
		draw_contours(s, c, A, nlev, cv, base, 0);
		if (s->flash > 0.12f || s->heat_v > 0.12f)
			draw_contours(s, c, A, nlev * 2 + 1, cv, base, 1);
		draw_hatch(s, c, A, 0.62f - 0.1f * cv, dens > 0.6f ? 1 : 2, cv, base);
	}

	/* ---- the deck's underside: mammatus pouches + a tiered shelf leading the front ---- */
	{
		float mam = us_smooth(0.3f, 0.75f, cv);
		/* the visible underside: lowest crossing of the outermost contour level, per column, smoothed */
		float vb[GW], sb[GW];
		float iso0 = 1.0f / (3 + (int)(dens * 4.0f + 0.5f) + 1.0f);
		for (int i = 0; i < GW; i++) {
			vb[i] = 0;
			for (int j = GH - 1; j > 0; j--) {
				float d1 = s->D[j * GW + i], d0 = s->D[(j - 1) * GW + i];
				if (d0 > iso0 && d1 <= iso0) {
					vb[i] = SKY_Y * (j - 1 + (d0 - iso0) / (d0 - d1 + 1e-6f)) / (GH - 1);
					break;
				}
			}
		}
		for (int i = 0; i < GW; i++) {
			float acc = 0, wsum = 0;
			for (int k = -2; k <= 2; k++) {
				int j = i + k < 0 ? 0 : (i + k >= GW ? GW - 1 : i + k);
				float wk = 1.0f - fabsf((float)k) / 3.0f;
				acc += vb[j] * wk;
				wsum += wk;
			}
			sb[i] = acc / wsum;
		}
		float cw = A / (GW - 1);
		if (mam > 0.02f) {
			int step = ctx->crisp ? 4 : 3;
			for (int i = 1; i < GW - 1; i += step) {
				float heavy = us_smooth(0.5f, 0.85f, s->col[i]);
				if (heavy < 0.05f)
					continue;
				float jit = us_noise2(i * 0.9f + s->drift * 2.0f, 4.0f);
				float rr = cw * step * (0.55f + 0.15f * jit) * (0.6f + 0.4f * mam) * (0.6f + 0.6f * heavy);
				float cx = (i + 0.5f + jit) * cw, cy = sb[i] - rr * 0.25f;
				float L = lit(s, cx, cy, base);
				float v = (0.14f + 0.2f * heavy) * mam + 0.8f * L;
				if (ctx->crisp)
					us_disc(c, cx, cy, rr, us_sat(0.22f * (1.0f - 0.35f * cv) * us_sat(mam * 2.0f) + 0.9f * L), US_MAT_WATER); /* scallops the outer band */
				else {
					/* pouches hang in two staggered rows: lit lower rims, fainter upper folds */
					us_arc(c, cx, cy, rr, 0.2f, US_PI - 0.2f, 0.0012f, us_sat(v), US_MAT_WATER);
					us_arc(c, cx + rr, cy - rr * 0.7f, rr * 0.8f, 0.4f, US_PI - 0.4f, 0.001f, us_sat(v * 0.6f), US_MAT_WATER);
				}
			}
		}
		/* shelf: stacked, sweeping tiers under the leading (left, the deck rolls leftward) edge */
		float shelf = us_smooth(0.15f, 0.55f, cv);
		if (shelf > 0.02f && !ctx->crisp)
			for (int k = 1; k <= 4; k++) {
				float px = 0, py = 0;
				for (int i = 0; i < GW; i += 2) {
					float u = (float)i / (GW - 1);
					float wedge = us_smooth(0.62f, 0.05f, u) * shelf;
					float x = i * cw;
					float y = sb[i] + wedge * (0.006f + 0.013f * k) + 0.003f * us_noise2(x * 6.0f + k, s->evo);
					if (i > 0 && wedge > 0.05f) {
						float L = lit(s, x, y, base);
						wline(c, px, py, x, y, 0.0011f, us_sat((0.1f + 0.25f * wedge) * (1.0f - 0.15f * k) + 0.7f * L),
						      US_MAT_WATER);
					}
					px = x;
					py = y;
				}
			}
	}

	/* ---- rain curtains under heavy columns ---- */
	float rain_t = us_sat(0.9f * cv + 0.35f * a->level * cv + 0.25f * w->moisture - 0.12f);
	s->rain = us_approach(s->rain, rain_t, 1.5f, rdt);
	us_world_add(w, &w->moisture, 0.006f * s->rain, dt);
	{
		float slant = 0.18f + 0.6f * wind * 10.0f * 0.3f;
		int spawn = (int)(s->rain * (300.0f + 1500.0f * dens) * rdt + us_rand(r));
		for (int k = 0; k < spawn && s->ndrop < MAX_DROP; k++) {
			int i = (int)(us_rand(r) * GW);
			float cw = s->col[i];
			float u = (i + 0.5f) / GW;
			float shaft = us_smooth(-0.1f, 0.45f, us_noise3(u * A * 2.6f + s->drift * 1.2f, 3.3f, s->evo * 0.6f));
			if (us_rand(r) > us_smooth(0.45f, 0.85f, cw) * shaft)
				continue;
			drop_t *d = &s->drop[s->ndrop++];
			d->x = (i + us_rand(r)) / GW;
			d->y = s->colb[i] + us_randr(r, -0.02f, 0.06f);
			d->vy = us_randr(r, 1.1f, 1.7f);
			d->vx = d->vy * slant * us_randr(r, 0.85f, 1.15f);
			d->len = us_randr(r, 0.02f, 0.05f);
			d->v = us_randr(r, 0.18f, 0.42f);
		}
		int kk = 0;
		for (int k = 0; k < s->ndrop;) {
			drop_t *d = &s->drop[k];
			d->x += d->vx * dt / A;
			d->y += d->vy * dt;
			float gy = horizon_y(d->x);
			if (d->y > gy || d->x > 1.05f || d->x < -0.05f) {
				*d = s->drop[--s->ndrop];
				continue;
			}
			float x = d->x * A;
			float tl = d->len / (d->vy + 1e-3f);
			float v = d->v + 0.5f * lit(s, x, d->y, base);
			if (ctx->crisp && (kk++ & 1)) {
				k++;
				continue;
			}
			wline(c, x, d->y, x - d->vx * tl, d->y - d->vy * tl, 0.0009f, us_sat(v), US_MAT_WATER);
			k++;
		}
	}

	/* ---- lightning channel ---- */
	if (s->nseg > 0) {
		float after = 0.32f * expf(-s->bolt_t / 0.5f); /* retinal afterimage */
		float fv = s->flash > after ? s->flash : after;
		if (fv < 0.01f) {
			s->nseg = 0;
		} else {
			for (int k = 0; k < s->nseg; k++) {
				seg_t *g = &s->seg[k];
				float v = us_sat(fv * g->v * (s->flash > after ? 1.0f : 0.8f));
				if (!ctx->crisp && s->flash > 0.2f) {
					us_line(c, g->x0, g->y0, g->x1, g->y1, 0.016f * g->w, v * 0.12f, US_MAT_LIGHT);
					us_line(c, g->x0, g->y0, g->x1, g->y1, 0.0065f * g->w, v * 0.35f, US_MAT_LIGHT);
				}
				us_line(c, g->x0, g->y0, g->x1, g->y1, (ctx->crisp ? 0.003f : 0.0032f) * g->w, us_sat(v * 1.15f), US_MAT_LIGHT);
			}
		}
	}

	/* ---- horizon, ground, trees (lit by the flash) ---- */
	{
		float L = s->flash * s->bolt_amp;
		float pts[2 * 65];
		for (int i = 0; i <= 64; i++) {
			float u = i / 64.0f;
			pts[2 * i] = u * A;
			pts[2 * i + 1] = horizon_y(u);
		}
		float hv = 0.4f + 0.5f * L + 0.15f * s->heat_v;
		us_polyline(c, pts, 65, 0.0016f, us_sat(hv), US_MAT_EARTH);
		if (ctx->crisp)
			for (int i = 0; i < 64; i++) {
				float x0 = pts[2 * i], y0 = pts[2 * i + 1], x1 = pts[2 * i + 2], y1 = pts[2 * i + 3];
				float gv = 0.16f + 0.3f * L;
				us_tri(c, x0, y0, x1, y1, x0, 1.01f, gv, US_MAT_EARTH);
				us_tri(c, x1, y1, x1, 1.01f, x0, 1.01f, gv, US_MAT_EARTH);
			}
		else {
			/* a few furrows in the foreground field, catching the flash */
			for (int k = 1; k <= 4; k++) {
				float yy = 0.84f + 0.04f * k * k * 0.6f;
				float fp[2 * 17];
				for (int i = 0; i <= 16; i++) {
					float u = i / 16.0f;
					fp[2 * i] = u * A;
					fp[2 * i + 1] = yy + 0.01f * us_noise2(u * 3.0f, k * 1.7f) * k;
				}
				us_polyline(c, fp, 17, 0.001f, us_sat((0.10f + 0.05f * k) + 0.5f * L), US_MAT_EARTH);
			}
		}
		for (int i = 0; i < s->ntrees; i++) {
			tree_t *t = &s->tree[i];
			float tx = t->x * A, ty = horizon_y(t->x) + 0.003f;
			for (int k = 0; k < t->nseg; k++)
				us_line(c, tx + t->sx0[k], ty + t->sy0[k], tx + t->sx1[k], ty + t->sy1[k],
					0.0022f * t->sw[k] + 0.0008f, us_sat(0.45f + 0.5f * L), US_MAT_STEM);
		}
	}

	/* ---- the near ridge + its oak: black until a flash reveals them ---- */
	{
		float side = s->ridge_side;
		float Lg = s->flash * s->bolt_amp;
		/* light falls off away from where the stroke came down */
		float hx = s->hit_x;
		int iv0 = 0;
		uint8_t fm = US_MAT_EARTH;
		if (ctx->crisp) { /* a dark silhouette against the far fields until the sky lights it */
			float lv = 0.45f * Lg + 0.1f * s->heat_v;
			fm = lv > 0.12f ? US_MAT_EARTH : US_MAT_BG;
			iv0 = (int)(us_sat(lv > 0.12f ? 0.1f + lv : 0.0f) * 255.0f);
		}
		for (int px = 0; px < c->w; px++) {
			float u = (px + 0.5f) / c->s / A;
			int y0 = (int)ceilf(ridge_y(u, side) * c->s);
			for (int py = y0 < 0 ? 0 : y0; py < c->h; py++) {
				size_t i = (size_t)py * c->w + px;
				c->ink[i] = (uint8_t)iv0;
				c->mat[i] = fm;
			}
		}
		float pts[2 * 97];
		for (int i = 0; i <= 96; i++) {
			float u = i / 96.0f;
			pts[2 * i] = u * A;
			pts[2 * i + 1] = ridge_y(u, side);
		}
		for (int i = 0; i < 96; i++) {
			float x = pts[2 * i];
			float dx = x - hx;
			float L = Lg * (0.4f + 0.6f * expf(-dx * dx / 0.15f)) + 0.15f * s->heat_v;
			wline(c, pts[2 * i], pts[2 * i + 1], pts[2 * i + 2], pts[2 * i + 3], 0.0016f, us_sat(0.2f + 0.8f * L), US_MAT_EARTH);
			/* wet grass on the crest, combed by the storm wind */
			if ((!ctx->crisp || (i & 1) == 0) && pts[2 * i + 1] < 0.95f)
				for (int q = 0; q < 3; q++) {
					float gx = x + (q + us_noise2(i * 1.3f, q * 2.1f)) * A / 96.0f / 3.0f;
					float gy = ridge_y(gx / A, side);
					float gh = 0.008f + 0.008f * (0.5f + 0.5f * us_noise2(gx * 9.0f, 3.3f));
					float lean = 0.006f + 0.01f * wind * 20.0f * (0.5f + 0.5f * us_noise2(gx * 3.0f - (float)ctx->t * 2.0f, 1.0f));
					wline(c, gx, gy, gx - lean, gy - gh, 0.001f, us_sat(0.1f + 0.75f * L), US_MAT_LEAF);
				}
		}
		/* the oak on the crest: limbs thrash a little in the gusts */
		float ou = side > 0 ? 0.8f : 0.2f;
		float ox = ou * A, oy = ridge_y(ou, side) + 0.004f;
		float dxo = ox - hx;
		float Lo = Lg * (0.45f + 0.55f * expf(-dxo * dxo / 0.15f)) + 0.2f * s->heat_v;
		tree_t *t = &s->oak;
		float sway = (0.4f + 30.0f * wind) * 0.012f;
		for (int k = 0; k < t->nseg; k++) {
			float h0 = -t->sy0[k], h1 = -t->sy1[k];
			float ws = sway * us_noise2(k * 0.2f, (float)ctx->t * 0.9f);
			float x0 = ox + t->sx0[k] * 1.2f + ws * h0 * 6.0f, y0 = oy + t->sy0[k] * 1.2f;
			float x1 = ox + t->sx1[k] * 1.2f + ws * h1 * 6.0f, y1 = oy + t->sy1[k] * 1.2f;
			float wd = 0.0075f * t->sw[k] + 0.001f;
			float v = us_sat((ctx->crisp ? 0.2f : 0.1f) + 0.85f * Lo);
			(wd * c->s > 1.6f || ctx->crisp ? us_line : wline)(c, x0, y0, x1, y1, wd, v, US_MAT_STEM);
		}
	}
}

const us_scene_vt us_scene_storm = {"storm", "Storm Front", 0.25f, create, destroy, frame};
