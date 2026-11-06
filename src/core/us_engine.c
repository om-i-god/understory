#include "us_scene.h"
#include "us_math.h"
#include <stdlib.h>
#include <string.h>

const us_scene_vt *us_scene_find(const char *id)
{
	if (id)
		for (int i = 0; i < us_scene_count; i++)
			if (strcmp(us_scene_list[i]->id, id) == 0)
				return us_scene_list[i];
	return us_scene_list[0];
}

void us_engine_init(us_engine *e)
{
	memset(e, 0, sizeof(*e));
	e->look.style = US_STYLE_INK;
	e->look.tint[0] = e->look.tint[1] = e->look.tint[2] = 235;
	e->look.glow = 0.3f;
	e->out_w = 1920;
	e->out_h = 1080;
	e->pixel_size = 6;
	e->ink_scale = 1.0f;
	e->linked = 1;
	e->world_speed = 1.0f;
	e->xfade_len = 2.0f;
	e->params.density = e->params.speed = e->params.reactivity = 0.5f;
	us_world_init(&e->private_world);
}

void us_engine_free(us_engine *e)
{
	if (e->vt && e->scene)
		e->vt->destroy(e->scene);
	us_canvas_free(&e->canvas);
	us_canvas_free(&e->disp);
	free(e->rgba);
	free(e->xf_ink);
	free(e->xf_mat);
	free(e->xf_thr);
	free(e->bloom_buf);
	e->bloom_buf = NULL;
	e->bloom_n = 0;
	memset(e, 0, sizeof(*e));
}

/* Organic dissolve order: low-frequency fbm on a coarse grid, bilinearly upsampled. The new scene
 * spreads in from the low places like moss, with soft edges. */
static void fill_dissolve_order(us_engine *e)
{
	if (!e->xf_thr)
		return;
	int w = e->canvas.w, h = e->canvas.h;
	enum { G = 33 };
	float grid[G][G];
	float ox = 17.3f * (float)(e->xf_count % 97), oy = 5.1f * (float)(e->xf_count % 89);
	e->xf_count++;
	float lo = 1e9f, hi = -1e9f;
	for (int j = 0; j < G; j++)
		for (int i = 0; i < G; i++) {
			float v = us_fbm2(ox + i * 0.16f * e->canvas.aspect, oy + j * 0.16f, 3);
			grid[j][i] = v;
			lo = v < lo ? v : lo;
			hi = v > hi ? v : hi;
		}
	float sc = hi > lo ? 255.0f / (hi - lo) : 0;
	for (int y = 0; y < h; y++) {
		float gy = (float)y / (h > 1 ? h - 1 : 1) * (G - 1);
		int j = (int)gy;
		if (j > G - 2)
			j = G - 2;
		float ty = gy - j;
		uint8_t *row = e->xf_thr + (size_t)y * w;
		for (int x = 0; x < w; x++) {
			float gx = (float)x / (w > 1 ? w - 1 : 1) * (G - 1);
			int i = (int)gx;
			if (i > G - 2)
				i = G - 2;
			float tx = gx - i;
			float a = grid[j][i] + (grid[j][i + 1] - grid[j][i]) * tx;
			float b = grid[j + 1][i] + (grid[j + 1][i + 1] - grid[j + 1][i]) * tx;
			row[x] = (uint8_t)((a + (b - a) * ty - lo) * sc);
		}
	}
}

void us_engine_set_scene(us_engine *e, const char *id, uint32_t seed)
{
	const us_scene_vt *vt = us_scene_find(id);
	if (vt == e->vt && seed == e->seed && e->scene)
		return;
	int had = e->vt && e->scene;
	if (had)
		e->vt->destroy(e->scene);
	e->vt = vt;
	e->seed = seed;
	e->scene = vt->create(seed);
	if (e->canvas.ink) {
		size_t n = (size_t)e->canvas.w * e->canvas.h;
		if (had && e->xfade_len > 0.01f && e->xf_ink && e->xf_mat) {
			/* snapshot what is on screen now and dissolve it out */
			memcpy(e->xf_ink, e->xf_t > 0 && e->disp.ink ? e->disp.ink : e->canvas.ink, n);
			memcpy(e->xf_mat, e->xf_t > 0 && e->disp.mat ? e->disp.mat : e->canvas.mat, n);
			e->xf_t = 1.0f;
			fill_dissolve_order(e);
		}
		us_canvas_clear(&e->canvas);
	}
}

void us_engine_configure(us_engine *e, const us_look *look, int out_w, int out_h, int pixel_size, float ink_scale)
{
	e->look = *look;
	e->out_w = out_w > 16 ? out_w : 16;
	e->out_h = out_h > 9 ? out_h : 9;
	e->pixel_size = pixel_size;
	e->ink_scale = ink_scale;
	int cw, ch;
	us_style_canvas_size(look->style, e->out_w, e->out_h, pixel_size, ink_scale, &cw, &ch);
	int crisp = !us_style_is_ink(look->style);
	if (e->canvas.ink && e->canvas.w == cw && e->canvas.h == ch && e->canvas.crisp == crisp)
		return;
	us_canvas_free(&e->canvas);
	us_canvas_free(&e->disp);
	free(e->rgba);
	free(e->xf_ink);
	free(e->xf_mat);
	free(e->xf_thr);
	e->rgba = NULL;
	e->xf_ink = e->xf_mat = e->xf_thr = NULL;
	e->xf_t = 0; /* a canvas change cancels any dissolve */
	if (us_canvas_init(&e->canvas, cw, ch, crisp) && us_canvas_init(&e->disp, cw, ch, crisp)) {
		e->rgba = calloc((size_t)cw * ch * 4, 1);
		e->xf_ink = calloc((size_t)cw * ch, 1);
		e->xf_mat = calloc((size_t)cw * ch, 1);
		e->xf_thr = calloc((size_t)cw * ch, 1);
	}
}

us_world *us_engine_world(us_engine *e) { return e->linked ? us_world_shared() : &e->private_world; }

static float scale_feat(float x, float k) { return us_sat(x * k); }

void us_engine_frame(us_engine *e, const us_audio *a_in, float dt, double clock_t)
{
	if (!e->scene || !e->canvas.ink || !e->rgba)
		return;
	if (dt <= 0)
		dt = 1.0f / 60.0f;
	if (dt > 0.1f)
		dt = 0.1f;

	/* reactivity: 0 -> x0.25, 0.5 -> x1, 1 -> x2 (curve) */
	float r = us_sat(e->params.reactivity);
	float k = r < 0.5f ? us_lerp(0.25f, 1.0f, r * 2) : us_lerp(1.0f, 2.0f, (r - 0.5f) * 2);
	us_audio a = *a_in;
	a.sub = scale_feat(a.sub, k);
	a.bass = scale_feat(a.bass, k);
	a.lowmid = scale_feat(a.lowmid, k);
	a.mid = scale_feat(a.mid, k);
	a.high = scale_feat(a.high, k);
	a.air = scale_feat(a.air, k);
	a.level = scale_feat(a.level, k);
	a.onset = scale_feat(a.onset, k);
	a.kick = scale_feat(a.kick, k);
	for (int i = 0; i < US_NBANDS; i++)
		a.bands[i] = scale_feat(a.bands[i], k);

	/* speed: 0 -> x0.2, 0.5 -> x1, 1 -> x2.5 */
	float s = us_sat(e->params.speed);
	float sk = s < 0.5f ? us_lerp(0.2f, 1.0f, s * 2) : us_lerp(1.0f, 2.5f, (s - 0.5f) * 2);
	float sdt = dt * sk;
	e->sim_t += sdt;

	us_world *w = us_engine_world(e);
	/* shared world steps once per wall-clock frame; private world with our own clock */
	us_world_step(w, &a, e->linked ? clock_t : e->sim_t, dt, e->world_speed);

	/* trails: per-frame keep factor from half-life */
	float tr = us_sat(e->vt->trails + e->trails_bias);
	float keep = 0;
	if (tr > 0.001f) {
		float half = 0.02f + tr * tr * 3.0f; /* seconds */
		keep = powf(0.5f, dt / half);
	}
	us_canvas_fade(&e->canvas, keep);
	e->canvas.blend = US_BLEND_MAX;

	us_ctx ctx = {
		.dt = sdt,
		.real_dt = dt,
		.t = e->sim_t,
		.a = &a,
		.w = w,
		.p = &e->params,
		.aspect = e->canvas.aspect,
		.crisp = e->canvas.crisp,
	};
	e->vt->frame(e->scene, &ctx, &e->canvas);

	if (e->xf_t > 0 && e->xf_ink && e->disp.ink) {
		e->xf_t -= dt / (e->xfade_len > 0.01f ? e->xfade_len : 0.01f);
		if (e->xf_t < 0)
			e->xf_t = 0;
		/* per-pixel progress: lp = (p - 0.55*h) / 0.45, so low places turn first and every pixel is done at p=1 */
		float prog = 1.0f - e->xf_t;
		unsigned kos[256];
		for (int hh = 0; hh < 256; hh++) {
			float lp = us_sat((prog - 0.55f * hh / 255.0f) / 0.45f);
			kos[hh] = (unsigned)((1.0f - us_smooth(0, 1, lp)) * 256);
		}
		size_t n = (size_t)e->canvas.w * e->canvas.h;
		for (size_t i = 0; i < n; i++) {
			unsigned ko = e->xf_thr ? kos[e->xf_thr[i]] : kos[128], kn = 256 - ko;
			unsigned o = (e->xf_ink[i] * ko) >> 8, nw = (e->canvas.ink[i] * kn) >> 8;
			if (o > nw) {
				e->disp.ink[i] = (uint8_t)o;
				e->disp.mat[i] = e->xf_mat[i];
			} else {
				e->disp.ink[i] = (uint8_t)nw;
				e->disp.mat[i] = e->canvas.mat[i];
			}
		}
	} else if (us_style_is_ink(e->look.style) && e->look.bloom > 0.001f && e->disp.ink) {
		size_t n = (size_t)e->canvas.w * e->canvas.h;
		memcpy(e->disp.ink, e->canvas.ink, n);
		memcpy(e->disp.mat, e->canvas.mat, n);
	} else {
		us_compose(&e->canvas, &e->look, e->rgba);
		return;
	}
	if (us_style_is_ink(e->look.style) && e->look.bloom > 0.001f) {
		size_t need = us_bloom_scratch(e->disp.w, e->disp.h);
		if (need > e->bloom_n) {
			free(e->bloom_buf);
			e->bloom_buf = malloc(need * sizeof(float));
			e->bloom_n = e->bloom_buf ? need : 0;
		}
		/* the light breathes with the music: kicks flare the glow, the phrase swell lifts it, and
		 * silence leaves it exactly as set. Eased (≈0.25 s decay) so it never strobes. */
		float kp = us_sat(a.kick * 0.8f + a.onset * 0.3f);
		e->light_pulse = kp > e->light_pulse ? us_lerp(e->light_pulse, kp, 0.6f)
						     : e->light_pulse * powf(0.5f, dt / 0.12f);
		float act = us_sat(a.active);
		float amt = e->look.bloom * (1.0f + act * (0.45f * e->light_pulse + 0.25f * (a.swell - 0.4f)));
		if (e->bloom_buf)
			us_bloom(&e->disp, us_clampf(amt, 0.0f, 1.6f * e->look.bloom + 0.05f), e->bloom_buf);
	}
	us_compose(&e->disp, &e->look, e->rgba);
}
