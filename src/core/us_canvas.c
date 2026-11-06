#include "us_canvas.h"
#include "us_math.h"
#include <stdlib.h>
#include <string.h>

/* Scenes pass floats straight from their simulations. A NaN, an infinity or an absurd size must never
 * reach an int conversion (undefined behaviour) or turn a loop into billions of iterations, so every
 * primitive checks its inputs and clips its loops to the canvas. */
static inline int fin(float x) { return x - x == 0.0f; } /* false for NaN and +-inf */
static inline int fin3(float a, float b, float c) { return fin(a) && fin(b) && fin(c); }
#define US_MAX_EXTENT 8.0f /* normalized units: nothing a scene draws needs to be wider than 8 canvas heights */

int us_canvas_init(us_canvas *c, int w, int h, int crisp)
{
	memset(c, 0, sizeof(*c));
	if (w < 1 || h < 1)
		return 0;
	c->w = w;
	c->h = h;
	c->s = (float)h;
	c->aspect = (float)w / (float)h;
	c->crisp = crisp;
	c->ink = calloc((size_t)w * h, 1);
	c->mat = calloc((size_t)w * h, 1);
	return c->ink && c->mat;
}

void us_canvas_free(us_canvas *c)
{
	free(c->ink);
	free(c->mat);
	memset(c, 0, sizeof(*c));
}

void us_canvas_clear(us_canvas *c)
{
	memset(c->ink, 0, (size_t)c->w * c->h);
	memset(c->mat, 0, (size_t)c->w * c->h);
}

void us_canvas_fade(us_canvas *c, float keep)
{
	if (keep <= 0.001f) {
		us_canvas_clear(c);
		return;
	}
	if (keep >= 0.999f)
		return;
	unsigned k = (unsigned)(keep * 256.0f);
	size_t n = (size_t)c->w * c->h;
	uint8_t *p = c->ink;
	for (size_t i = 0; i < n; i++)
		p[i] = (uint8_t)((p[i] * k) >> 8);
}

void us_put(us_canvas *c, int px, int py, float v, uint8_t mat)
{
	if ((unsigned)px >= (unsigned)c->w || (unsigned)py >= (unsigned)c->h || !(v > 0.0f))
		return;
	size_t i = (size_t)py * c->w + px;
	int iv = (int)(v * 255.0f + 0.5f);
	if (iv > 255)
		iv = 255;
	if (iv <= 0)
		return;
	int old = c->ink[i];
	if (c->blend == US_BLEND_ADD) {
		int n = old + iv;
		c->ink[i] = (uint8_t)(n > 255 ? 255 : n);
		if (iv * 2 >= old)
			c->mat[i] = mat;
	} else if (iv > old) {
		c->ink[i] = (uint8_t)iv;
		c->mat[i] = mat;
	}
}

/* ---- points / discs ------------------------------------------------- */

void us_disc(us_canvas *c, float x, float y, float r, float v, uint8_t mat)
{
	if (!fin3(x, y, r) || !(v > 0) || r < 0)
		return;
	if (r > US_MAX_EXTENT)
		r = US_MAX_EXTENT;
	float px = x * c->s, py = y * c->s, rp = r * c->s;
	/* entirely off the canvas (also keeps the int conversions below in range) */
	if (px + rp < -2 || py + rp < -2 || px - rp > c->w + 2 || py - rp > c->h + 2)
		return;
	if (c->crisp) {
		if (rp < 0.75f) {
			us_put(c, (int)floorf(px), (int)floorf(py), v, mat);
			return;
		}
		int x0 = (int)floorf(px - rp), x1 = (int)ceilf(px + rp);
		int y0 = (int)floorf(py - rp), y1 = (int)ceilf(py + rp);
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 >= c->w) x1 = c->w - 1;
		if (y1 >= c->h) y1 = c->h - 1;
		float r2 = rp * rp;
		for (int j = y0; j <= y1; j++)
			for (int i = x0; i <= x1; i++) {
				float dx = i + 0.5f - px, dy = j + 0.5f - py;
				if (dx * dx + dy * dy <= r2)
					us_put(c, i, j, v, mat);
			}
		return;
	}
	if (rp < 0.7f) {
		/* sub-pixel: bilinear splat, energy scaled by area so tiny things stay faint-but-visible */
		float fx = px - 0.5f, fy = py - 0.5f;
		int ix = (int)floorf(fx), iy = (int)floorf(fy);
		float ax = fx - ix, ay = fy - iy;
		float e = us_clampf(rp / 0.7f, 0.35f, 1.0f) * v;
		us_put(c, ix, iy, e * (1 - ax) * (1 - ay), mat);
		us_put(c, ix + 1, iy, e * ax * (1 - ay), mat);
		us_put(c, ix, iy + 1, e * (1 - ax) * ay, mat);
		us_put(c, ix + 1, iy + 1, e * ax * ay, mat);
		return;
	}
	int x0 = (int)floorf(px - rp - 1), x1 = (int)ceilf(px + rp + 1);
	int y0 = (int)floorf(py - rp - 1), y1 = (int)ceilf(py + rp + 1);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= c->w) x1 = c->w - 1;
	if (y1 >= c->h) y1 = c->h - 1;
	for (int j = y0; j <= y1; j++)
		for (int i = x0; i <= x1; i++) {
			float dx = i + 0.5f - px, dy = j + 0.5f - py;
			float d = sqrtf(dx * dx + dy * dy);
			float cov = us_sat(rp + 0.5f - d);
			if (cov > 0)
				us_put(c, i, j, v * cov, mat);
		}
}

void us_point(us_canvas *c, float x, float y, float r, float v, uint8_t mat) { us_disc(c, x, y, r, v, mat); }

/* ---- lines ---------------------------------------------------------- */

static void bresenham(us_canvas *c, int x0, int y0, int x1, int y1, float v, uint8_t mat)
{
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	int guard = 0;
	for (;;) {
		us_put(c, x0, y0, v, mat);
		if ((x0 == x1 && y0 == y1) || ++guard > 20000)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

/* clip segment to an expanded canvas box (Liang–Barsky); returns 0 if fully outside */
static int clip_seg(float *x0, float *y0, float *x1, float *y1, float xmin, float ymin, float xmax, float ymax)
{
	float t0 = 0, t1 = 1, dx = *x1 - *x0, dy = *y1 - *y0;
	float p[4] = {-dx, dx, -dy, dy};
	float q[4] = {*x0 - xmin, xmax - *x0, *y0 - ymin, ymax - *y0};
	for (int i = 0; i < 4; i++) {
		if (p[i] == 0) {
			if (q[i] < 0)
				return 0;
		} else {
			float t = q[i] / p[i];
			if (p[i] < 0) {
				if (t > t1) return 0;
				if (t > t0) t0 = t;
			} else {
				if (t < t0) return 0;
				if (t < t1) t1 = t;
			}
		}
	}
	float nx0 = *x0 + t0 * dx, ny0 = *y0 + t0 * dy;
	float nx1 = *x0 + t1 * dx, ny1 = *y0 + t1 * dy;
	*x0 = nx0; *y0 = ny0; *x1 = nx1; *y1 = ny1;
	return 1;
}

void us_line(us_canvas *c, float ax, float ay, float bx, float by, float width, float v, uint8_t mat)
{
	if (!fin3(ax, ay, bx) || !fin3(by, width, v) || !(v > 0))
		return;
	if (width < 0)
		width = 0;
	if (width > US_MAX_EXTENT)
		width = US_MAX_EXTENT;
	float x0 = ax * c->s, y0 = ay * c->s, x1 = bx * c->s, y1 = by * c->s;
	float wp = width * c->s;
	float r = wp * 0.5f;
	float pad = r + 2.0f;
	if (!clip_seg(&x0, &y0, &x1, &y1, -pad, -pad, c->w + pad, c->h + pad))
		return;

	if (c->crisp && wp <= 1.5f) {
		bresenham(c, (int)floorf(x0), (int)floorf(y0), (int)floorf(x1), (int)floorf(y1), v, mat);
		return;
	}
	/* thin AA lines: keep a ~1px core but scale brightness with width so hairlines fade rather than vanish */
	float vv = v;
	if (!c->crisp && r < 0.5f) {
		vv = v * us_clampf(wp, 0.35f, 1.0f);
		r = 0.5f;
	}
	if (c->crisp && r < 0.5f)
		r = 0.5f;

	float dx = x1 - x0, dy = y1 - y0;
	float len2 = dx * dx + dy * dy;
	int steep = fabsf(dy) > fabsf(dx);
	/* iterate along major axis, scan minor axis window */
	float a0 = steep ? y0 : x0, a1 = steep ? y1 : x1;
	float b0 = steep ? x0 : y0, b1 = steep ? x1 : y1;
	if (a0 > a1) {
		float t = a0; a0 = a1; a1 = t;
		t = b0; b0 = b1; b1 = t;
	}
	int ia0 = (int)floorf(a0 - r - 1), ia1 = (int)ceilf(a1 + r + 1);
	int amax = steep ? c->h - 1 : c->w - 1; /* the major axis: clip to the canvas too */
	if (ia0 < 0)
		ia0 = 0;
	if (ia1 > amax)
		ia1 = amax;
	float slope = (a1 - a0) > 1e-6f ? (b1 - b0) / (a1 - a0) : 0.0f;
	float spread = r * sqrtf(1 + slope * slope) + 1.5f;
	for (int ia = ia0; ia <= ia1; ia++) {
		float ac = ia + 0.5f;
		float bc = b0 + slope * us_clampf(ac - a0, 0, a1 - a0);
		int ib0 = (int)floorf(bc - spread), ib1 = (int)ceilf(bc + spread);
		int bmax = steep ? c->w - 1 : c->h - 1; /* the minor axis: clip the scan to the canvas */
		if (ib0 < 0)
			ib0 = 0;
		if (ib1 > bmax)
			ib1 = bmax;
		for (int ib = ib0; ib <= ib1; ib++) {
			int px = steep ? ib : ia, py = steep ? ia : ib;
			if ((unsigned)px >= (unsigned)c->w || (unsigned)py >= (unsigned)c->h)
				continue;
			float cx = px + 0.5f - x0, cy = py + 0.5f - y0;
			float t = len2 > 1e-9f ? us_sat((cx * dx + cy * dy) / len2) : 0;
			float ex = cx - t * dx, ey = cy - t * dy;
			float d = sqrtf(ex * ex + ey * ey);
			if (c->crisp) {
				if (d <= r)
					us_put(c, px, py, vv, mat);
			} else {
				float cov = us_sat(r + 0.5f - d);
				if (cov > 0)
					us_put(c, px, py, vv * cov, mat);
			}
		}
	}
}

void us_polyline(us_canvas *c, const float *xy, int n, float width, float v, uint8_t mat)
{
	for (int i = 0; i + 1 < n; i++)
		us_line(c, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], width, v, mat);
}

void us_arc(us_canvas *c, float x, float y, float r, float a0, float a1, float width, float v, uint8_t mat)
{
	if (!fin3(x, y, r) || !fin3(a0, a1, width) || !(v > 0))
		return;
	if (r > US_MAX_EXTENT)
		r = US_MAX_EXTENT;
	float rp = r * c->s;
	if (rp <= 0)
		return;
	float span = fabsf(a1 - a0);
	int n = (int)(span * rp / 3.0f);
	if (n < 8) n = 8;
	if (n > 720) n = 720;
	float px = x + cosf(a0) * r, py = y + sinf(a0) * r;
	for (int i = 1; i <= n; i++) {
		float a = a0 + (a1 - a0) * (float)i / n;
		float qx = x + cosf(a) * r, qy = y + sinf(a) * r;
		us_line(c, px, py, qx, qy, width, v, mat);
		px = qx;
		py = qy;
	}
}

void us_ring(us_canvas *c, float x, float y, float r, float width, float v, uint8_t mat)
{
	us_arc(c, x, y, r, 0, US_TAU, width, v, mat);
}

/* ---- triangles ------------------------------------------------------ */

void us_tri(us_canvas *c, float ax, float ay, float bx, float by, float cx_, float cy_, float v, uint8_t mat)
{
	if (!fin3(ax, ay, bx) || !fin3(by, cx_, cy_) || !(v > 0))
		return;
	float lim = US_MAX_EXTENT * 2.0f; /* keep vertices near the canvas so the int box below stays sane */
	ax = us_clampf(ax, -lim, lim);
	ay = us_clampf(ay, -lim, lim);
	bx = us_clampf(bx, -lim, lim);
	by = us_clampf(by, -lim, lim);
	cx_ = us_clampf(cx_, -lim, lim);
	cy_ = us_clampf(cy_, -lim, lim);
	float x0 = ax * c->s, y0 = ay * c->s, x1 = bx * c->s, y1 = by * c->s, x2 = cx_ * c->s, y2 = cy_ * c->s;
	float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
	if (fabsf(area) < 1e-6f)
		return;
	if (area < 0) {
		float t = x1; x1 = x2; x2 = t;
		t = y1; y1 = y2; y2 = t;
	}
	int minx = (int)floorf(fminf(x0, fminf(x1, x2))) - 1, maxx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2))) + 1;
	int miny = (int)floorf(fminf(y0, fminf(y1, y2))) - 1, maxy = (int)ceilf(fmaxf(y0, fmaxf(y1, y2))) + 1;
	if (minx < 0) minx = 0;
	if (miny < 0) miny = 0;
	if (maxx >= c->w) maxx = c->w - 1;
	if (maxy >= c->h) maxy = c->h - 1;
	/* normalized edge functions -> signed distance (px) to each edge, inside positive */
	float ex[3] = {x0, x1, x2}, ey[3] = {y0, y1, y2};
	float nx[3], ny[3], nd[3];
	for (int k = 0; k < 3; k++) {
		float dx = ex[(k + 1) % 3] - ex[k], dy = ey[(k + 1) % 3] - ey[k];
		float l = sqrtf(dx * dx + dy * dy);
		if (l < 1e-6f)
			return;
		nx[k] = -dy / l;
		ny[k] = dx / l;
		nd[k] = -(nx[k] * ex[k] + ny[k] * ey[k]);
	}
	for (int j = miny; j <= maxy; j++)
		for (int i = minx; i <= maxx; i++) {
			float px = i + 0.5f, py = j + 0.5f;
			float dmin = 1e9f;
			for (int k = 0; k < 3; k++) {
				float d = nx[k] * px + ny[k] * py + nd[k];
				if (d < dmin)
					dmin = d;
			}
			if (c->crisp) {
				if (dmin >= 0)
					us_put(c, i, j, v, mat);
			} else {
				float cov = us_sat(dmin + 0.5f);
				if (cov > 0)
					us_put(c, i, j, v * cov, mat);
			}
		}
}

/* ---- fields --------------------------------------------------------- */

void us_field(us_canvas *c, const float *f, int fw, int fh, float gain, uint8_t mat, float rx0, float ry0,
	      float rx1, float ry1)
{
	if (!f || !fin3(rx0, ry0, rx1) || !fin3(ry1, gain, 0) || !(gain > 0))
		return;
	float lim = US_MAX_EXTENT * 2.0f;
	rx0 = us_clampf(rx0, -lim, lim);
	ry0 = us_clampf(ry0, -lim, lim);
	rx1 = us_clampf(rx1, -lim, lim);
	ry1 = us_clampf(ry1, -lim, lim);
	int x0 = (int)floorf(rx0 * c->s), x1 = (int)ceilf(rx1 * c->s);
	int y0 = (int)floorf(ry0 * c->s), y1 = (int)ceilf(ry1 * c->s);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > c->w) x1 = c->w;
	if (y1 > c->h) y1 = c->h;
	float sw = (rx1 - rx0) * c->s, sh = (ry1 - ry0) * c->s;
	if (sw <= 0 || sh <= 0 || fw < 2 || fh < 2)
		return;
	for (int j = y0; j < y1; j++) {
		float fy = ((j + 0.5f) - ry0 * c->s) / sh * fh - 0.5f;
		int iy = (int)floorf(fy);
		float ty = fy - iy;
		int iy0 = iy < 0 ? 0 : (iy >= fh ? fh - 1 : iy);
		int iy1 = iy + 1 < 0 ? 0 : (iy + 1 >= fh ? fh - 1 : iy + 1);
		for (int i = x0; i < x1; i++) {
			float fx = ((i + 0.5f) - rx0 * c->s) / sw * fw - 0.5f;
			int ix = (int)floorf(fx);
			float tx = fx - ix;
			int ix0 = ix < 0 ? 0 : (ix >= fw ? fw - 1 : ix);
			int ix1 = ix + 1 < 0 ? 0 : (ix + 1 >= fw ? fw - 1 : ix + 1);
			float a = us_lerp(f[iy0 * fw + ix0], f[iy0 * fw + ix1], tx);
			float b = us_lerp(f[iy1 * fw + ix0], f[iy1 * fw + ix1], tx);
			float val = us_lerp(a, b, ty) * gain;
			if (val > 0.004f) /* false for NaN too */
				us_put(c, i, j, val, mat);
		}
	}
}
