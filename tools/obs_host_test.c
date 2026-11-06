/* obs_host_test — loads the built plugin into the real libobs (OBS.app on macOS, an OBS install or portable
 * zip on Windows: run the exe from OBS's bin/64bit folder), headless,
 * with its own throwaway config dir (never touches the user's OBS profile). Creates a source per style,
 * runs OBS's own video loop for a while, renders through the GPU pipeline and writes PPM screenshots.
 *
 *   obs_host_test <path/to/understory.plugin> <out_dir> [scene] [seconds]          (macOS)
 *   obs_host_test <path/to/understory.dll> <out_dir> [scene] [seconds] <data_dir> (Windows; OBS_ROOT=<obs dir>)
 * US_BIG=1 renders at 1920x1080; US_NO_LAG_CHECK=1 skips the lag check (software GPU on CI).
 */
#include <obs.h>
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 960
#define H 540
#define OBS_APP "/Applications/OBS.app/Contents"

static int fails;

static void snap(obs_source_t *src, const char *path)
{
	obs_enter_graphics();
	gs_texrender_t *tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *stage = gs_stagesurface_create(W, H, GS_RGBA);
	if (gs_texrender_begin(tr, W, H)) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
		gs_ortho(0, (float)obs_source_get_width(src), 0, (float)obs_source_get_height(src), -100, 100);
		obs_source_video_render(src);
		gs_texrender_end(tr);
		gs_stage_texture(stage, gs_texrender_get_texture(tr));
		uint8_t *data;
		uint32_t ls;
		if (gs_stagesurface_map(stage, &data, &ls)) {
			FILE *f = fopen(path, "wb");
			if (!f) {
				printf("FAIL: cannot write %s\n", path);
				fails++;
				gs_stagesurface_unmap(stage);
				goto done;
			}
			long sum = 0;
			fprintf(f, "P6\n%d %d\n255\n", W, H);
			for (int y = 0; y < H; y++)
				for (int x = 0; x < W; x++) {
					uint8_t *p = data + y * ls + x * 4;
					fputc(p[0], f);
					fputc(p[1], f);
					fputc(p[2], f);
					sum += p[0] + p[1] + p[2];
				}
			fclose(f);
			gs_stagesurface_unmap(stage);
			printf("snap %s  mean=%.2f\n", path, sum / (double)(W * H * 3));
			if (sum == 0) {
				printf("FAIL: blank frame\n");
				fails++;
			}
		} else {
			printf("FAIL: map\n");
			fails++;
		}
	} else {
		printf("FAIL: texrender_begin\n");
		fails++;
	}
done:
	gs_stagesurface_destroy(stage);
	gs_texrender_destroy(tr);
	obs_leave_graphics();
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s understory.plugin out_dir [scene] [seconds]\n", argv[0]);
		return 2;
	}
	const char *plugin = argv[1], *out = argv[2];
	const char *scene = argc > 3 ? argv[3] : "meadow";
	int secs = argc > 4 ? atoi(argv[4]) : 4;
	char cfg[1024];
	snprintf(cfg, sizeof(cfg), "%s/obs-config", out);
	os_mkdirs(cfg);

	if (!obs_startup("en-US", cfg, NULL)) {
		printf("FAIL obs_startup\n");
		return 1;
	}
#ifdef _WIN32
	const char *root = getenv("OBS_ROOT");
	if (!root) {
		printf("FAIL: set OBS_ROOT to the OBS folder (the one containing bin/ and data/)\n");
		return 1;
	}
	char dp[1024];
	snprintf(dp, sizeof(dp), "%s/data/libobs/", root);
	obs_add_data_path(dp);
#else
	obs_add_data_path(OBS_APP "/Resources/data/libobs/");
#endif

	struct obs_audio_info ai = {.samples_per_sec = 48000, .speakers = SPEAKERS_STEREO};
	obs_reset_audio(&ai);
	struct obs_video_info vi = {0};
#ifdef _WIN32
	vi.graphics_module = "libobs-d3d11";
#else
	vi.graphics_module = OBS_APP "/Frameworks/libobs-opengl.dylib";
#endif
	vi.fps_num = 30;
	vi.fps_den = 1;
	vi.base_width = vi.output_width = W;
	vi.base_height = vi.output_height = H;
	vi.output_format = VIDEO_FORMAT_NV12;
	vi.colorspace = VIDEO_CS_709;
	vi.range = VIDEO_RANGE_PARTIAL;
	vi.scale_type = OBS_SCALE_BICUBIC;
	vi.gpu_conversion = true;
	int r = obs_reset_video(&vi);
	if (r != OBS_VIDEO_SUCCESS) {
		printf("FAIL obs_reset_video %d\n", r);
		return 1;
	}

	char bin[1024], data[1024];
#ifdef _WIN32
	/* libobs names a module after the text past the last '/', as OBS's own loader passes it */
	snprintf(bin, sizeof(bin), "%s", plugin);
	snprintf(data, sizeof(data), "%s", argc > 5 ? argv[5] : "data");
	for (char *q = bin; *q; q++)
		if (*q == '\\')
			*q = '/';
	for (char *q = data; *q; q++)
		if (*q == '\\')
			*q = '/';
#else
	snprintf(bin, sizeof(bin), "%s/Contents/MacOS/understory", plugin);
	snprintf(data, sizeof(data), "%s/Contents/Resources", plugin);
#endif
	obs_module_t *mod = NULL;
	r = obs_open_module(&mod, bin, data);
	if (r != MODULE_SUCCESS) {
		printf("FAIL obs_open_module %d\n", r);
		return 1;
	}
	if (!obs_init_module(mod)) {
		printf("FAIL obs_init_module\n");
		return 1;
	}
	printf("module loaded: %s\n", obs_get_module_name(mod) ? obs_get_module_name(mod) : "(unnamed)");

	const char *styles[4] = {"ink", "pixel", "mono", "color"};
	for (int style = 0; style < 4; style++) {
		obs_data_t *st = obs_data_create();
		obs_data_set_string(st, "scene", scene);
		obs_data_set_int(st, "style", style);
		const char *big = getenv("US_BIG"); /* stress: render the source at 1920x1080 */
		obs_data_set_int(st, "width", big ? 1920 : W);
		obs_data_set_int(st, "height", big ? 1080 : H);
		obs_data_set_int(st, "pixel_size", 4);
		if (style == 2)
			obs_data_set_int(st, "tint", 0xff3cb0ff); /* amber, ABGR */
		obs_source_t *src = obs_source_create("understory_source", "understory test", st, NULL);
		obs_data_release(st);
		if (!src) {
			printf("FAIL source create\n");
			return 1;
		}
		obs_properties_t *props = obs_source_properties(src);
		int nprops = 0;
		for (obs_property_t *p = obs_properties_first(props); p; obs_property_next(&p))
			nprops++;
		obs_properties_destroy(props);
		printf("source %s: %ux%u, %d top-level properties\n", styles[style], obs_source_get_width(src),
		       obs_source_get_height(src), nprops);

		obs_set_output_source(0, src); /* makes it 'showing', OBS's video thread now ticks it */
		uint32_t lag0 = obs_get_lagged_frames(), tot0 = obs_get_total_frames();
		os_sleep_ms((uint32_t)secs * 1000);
		uint32_t lag = obs_get_lagged_frames() - lag0, tot = obs_get_total_frames() - tot0;
		printf("video frames %u, lagged %u\n", tot, lag);
		if (!getenv("US_NO_LAG_CHECK") && tot > 0 && lag * 10 > tot) {
			printf("FAIL: OBS video thread lagging\n");
			fails++;
		}
		char path[1024];
		snprintf(path, sizeof(path), "%s/obs_%s_%s.ppm", out, scene, styles[style]);
		snap(src, path);

		/* live edits: switch scene + style while running */
		obs_data_t *st2 = obs_source_get_settings(src);
		obs_data_set_string(st2, "scene", "river");
		obs_source_update(src, st2);
		obs_data_release(st2);
		os_sleep_ms(300);

		obs_set_output_source(0, NULL);
		obs_source_release(src);
	}
	obs_shutdown();
	char wp[1100];
	snprintf(wp, sizeof(wp), "%s/understory/world.json", cfg);
	printf("world file %s: %s\n", wp, os_file_exists(wp) ? "written" : "MISSING");
	if (!os_file_exists(wp))
		fails++;
	printf(fails ? "OBS HOST TEST FAILED (%d)\n" : "OBS HOST TEST PASSED\n", fails);
	return fails ? 1 : 0;
}
