/* Core unit tests: analyser, world, rasteriser, compose, every scene survives every style. */
#include "../src/core/us_scene.h"
#include "../src/core/us_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond, ...)                                  \
	do {                                              \
		if (!(cond)) {                            \
			fails++;                          \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__);              \
			printf("\n");                     \
		}                                         \
	} while (0)

static long ink_sum(const us_canvas *c)
{
	long s = 0;
	for (int i = 0; i < c->w * c->h; i++)
		s += c->ink[i];
	return s;
}

static void test_raster(void)
{
	us_canvas c;
	us_canvas_init(&c, 320, 180, 0);
	us_line(&c, 0.1f, 0.1f, 1.5f, 0.9f, 0.003f, 1.0f, US_MAT_STEM);
	CHECK(ink_sum(&c) > 0, "AA line drew nothing");
	us_canvas_clear(&c);
	/* far off-canvas line must not hang or write */
	us_line(&c, -50, -50, -40, -40, 0.01f, 1, 1);
	CHECK(ink_sum(&c) == 0, "offscreen line wrote pixels");
	us_line(&c, -1e6f, 0.5f, 1e6f, 0.5f, 0.002f, 1, 1);
	CHECK(ink_sum(&c) > 0, "huge line clipped away");
	us_canvas_clear(&c);
	us_disc(&c, 0.5f, 0.5f, 0.1f, 1, US_MAT_BLOOM);
	CHECK(c.mat[(90) * 320 + 90] == US_MAT_BLOOM, "disc material missing at centre");
	us_canvas_fade(&c, 0.5f);
	CHECK(c.ink[90 * 320 + 90] > 100 && c.ink[90 * 320 + 90] < 140, "fade 0.5 wrong: %d", c.ink[90 * 320 + 90]);
	us_canvas_free(&c);

	us_canvas_init(&c, 64, 36, 1);
	us_line(&c, 0, 0, 1.7f, 1, 0.001f, 1, 1);
	CHECK(ink_sum(&c) >= 255 * 50, "crisp line too short");
	us_tri(&c, 0.2f, 0.2f, 0.8f, 0.2f, 0.5f, 0.8f, 1, 2);
	us_ring(&c, 0.9f, 0.5f, 0.3f, 0.01f, 1, 3);
	us_canvas_free(&c);
}

static void test_compose(void)
{
	us_canvas c;
	us_canvas_init(&c, 8, 8, 1);
	c.ink[0] = 255;
	c.mat[0] = US_MAT_BLOOM;
	uint8_t rgba[8 * 8 * 4];
	us_look look = {.style = US_STYLE_MONO, .tint = {255, 176, 60}};
	us_compose(&c, &look, rgba);
	CHECK(rgba[0] == 255 && rgba[1] == 176 && rgba[2] == 60, "mono full ink != tint");
	CHECK(rgba[4] == 0 && rgba[7] == 255, "mono bg not black/opaque");
	c.ink[1] = 120;
	us_compose(&c, &look, rgba);
	CHECK(rgba[4] % 17 == 0 || rgba[4] == (255 * ((rgba[4] * 15 + 127) / 255) * 17 / 255), "mono not quantized");
	for (int p = 0; p < us_palette_count; p++) {
		look.style = US_STYLE_PIXEL;
		look.palette = p;
		us_compose(&c, &look, rgba);
		CHECK(rgba[3] == 255, "pixel alpha");
	}
	us_canvas_free(&c);
}

static void test_audio(void)
{
	us_analyzer *an = us_analyzer_create();
	us_audio a;
	float L[800], R[800];
	/* silence */
	memset(L, 0, sizeof(L));
	for (int i = 0; i < 60; i++) {
		us_analyzer_push(an, L, L, 800, 48000);
		us_analyzer_update(an, 1.0f / 60, 1, 1, 1, 1, &a);
	}
	CHECK(a.level < 0.05f && a.beat == 0, "silence not silent: lvl %.2f", a.level);
	CHECK(a.silence > 0.5f, "silence timer %.2f", a.silence);
	/* periodic kick at 2 Hz for 4 s: expect beats and bass */
	int beats = 0;
	float maxbass = 0;
	double t = 0;
	for (int f = 0; f < 240; f++) {
		for (int i = 0; i < 800; i++) {
			float bt = fmodf((float)t, 0.5f);
			L[i] = R[i] = 0.8f * expf(-bt * 10) * sinf(US_TAU * 55.0f * (float)t) + 0.01f * sinf((float)t * 3000);
			t += 1.0 / 48000;
		}
		us_analyzer_push(an, L, R, 800, 48000);
		us_analyzer_update(an, 1.0f / 60, 1, 1, 1, 1, &a);
		beats += a.beat;
		if (a.bass > maxbass) maxbass = a.bass;
	}
	CHECK(beats >= 5 && beats <= 12, "expected ~8 beats in 4 s, got %d", beats);
	CHECK(maxbass > 0.5f, "bass never rose: %.2f", maxbass);
	us_analyzer_destroy(an);
}

static void test_world(void)
{
	us_world w;
	us_world_init(&w);
	us_audio a;
	memset(&a, 0, sizeof(a));
	a.level = 0.8f;
	a.swell = 0.8f;
	a.bass = 0.8f;
	a.active = 1;
	int steps = 0;
	for (int i = 0; i < 3600; i++)
		steps += us_world_step(&w, &a, i / 60.0, 1.0f / 60, 1);
	CHECK(steps == 3600, "steps %d", steps);
	CHECK(us_world_step(&w, &a, 3599 / 60.0, 1.0f / 60, 1) == 0, "double step not deduped");
	CHECK(w.storm_charge > 0.1f, "storm didn't charge: %.3f", w.storm_charge);
	CHECK(w.growth > 0.15f, "growth didn't rise: %.3f", w.growth);
	const char *path = "/tmp/us_world_test.json";
	CHECK(us_world_save(&w, path), "save");
	us_world r;
	CHECK(us_world_load(&r, path), "load");
	CHECK(fabsf(r.growth - w.growth) < 1e-4f && fabsf(r.season - w.season) < 1e-4f, "roundtrip");
	float sum = 0;
	for (int s = 0; s < 4; s++)
		sum += us_world_season_w(&w, s);
	CHECK(sum > 0.7f && sum < 1.3f, "season weights sum %.2f", sum);
}

static void test_scenes(void)
{
	us_analyzer *an = us_analyzer_create();
	for (int si = 0; si < us_scene_count; si++) {
		const us_scene_vt *vt = us_scene_list[si];
		CHECK(us_scene_find(vt->id) == vt, "find %s", vt->id);
		for (int style = 0; style < US_STYLE_COUNT; style++) {
			us_engine e;
			us_engine_init(&e);
			us_engine_set_scene(&e, vt->id, 3);
			us_look look = {.style = style, .tint = {200, 220, 255}};
			us_engine_configure(&e, &look, 640, 360, 4, 1.0f);
			us_audio a;
			memset(&a, 0, sizeof(a));
			double t = 0;
			for (int f = 0; f < 90; f++) {
				a.level = a.bass = a.high = 0.5f + 0.5f * sinf(f * 0.3f);
				a.beat = (f % 15) == 0;
				a.onset = a.beat ? 1 : a.onset * 0.8f;
				a.active = 1;
				t += 1.0 / 30;
				us_engine_frame(&e, &a, 1.0f / 30, t + si * 1000 + style * 100);
			}
			/* switch style live mid-run */
			look.style = (style + 1) % US_STYLE_COUNT;
			us_engine_configure(&e, &look, 1280, 720, 6, 0.5f);
			for (int f = 0; f < 10; f++)
				us_engine_frame(&e, &a, 1.0f / 30, t + f / 30.0 + 0.5);
			/* density extremes + aspect change */
			e.params.density = 1;
			us_engine_configure(&e, &look, 1080, 1080, 6, 0.5f);
			for (int f = 0; f < 10; f++)
				us_engine_frame(&e, &a, 1.0f / 30, t + f / 30.0 + 2);
			e.params.density = 0;
			for (int f = 0; f < 10; f++)
				us_engine_frame(&e, &a, 1.0f / 30, t + f / 30.0 + 4);
			us_engine_free(&e);
		}
	}
	us_analyzer_destroy(an);
}

int main(void)
{
	test_raster();
	test_compose();
	test_audio();
	test_world();
	test_scenes();
	if (fails) {
		printf("%d failure(s)\n", fails);
		return 1;
	}
	printf("all core tests passed (%d scenes)\n", us_scene_count);
	return 0;
}
