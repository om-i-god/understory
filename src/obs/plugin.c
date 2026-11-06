/* Understory — OBS module: a video source of living, audio-reactive nature scenes. */
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <util/bmem.h>
#include "core/us_thread.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "core/us_scene.h"
#include "core/us_math.h"
#include "core/us_journey.h"

/* from libobs util/threading.h (not included: on Windows it drags in w32-pthreads) */
void os_set_thread_name(const char *name);

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("understory", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Understory: living, generative, nature-inspired audio-reactive visuals";
}

#define T_(s) obs_module_text(s)
#define WORLD_SAVE_INTERVAL 15.0

static char *g_world_path;
static double g_last_save;
static us_mutex g_world_mu = US_MUTEX_INIT;

typedef struct us_src {
	obs_source_t *self;
	us_mutex mu;
	us_engine eng;
	us_analyzer *an;

	/* audio hookup */
	char *audio_name;
	obs_weak_source_t *audio_weak;
	double rehook_timer;

	/* settings mirrors */
	char *applied_scene_setting; /* last scene value seen in settings (hotkeys may diverge) */
	int scene_idx;
	float sensitivity, bass_gain, mid_gain, high_gain;
	float auto_cycle_min;
	int scene_mode; /* 0 manual, 1 in order, 2 follow the music */
	us_journey journey;
	double cycle_timer;
	int out_w, out_h;

	/* gpu */
	gs_texture_t *tex;
	gs_samplerstate_t *point, *linear;
	int tex_w, tex_h;

	obs_hotkey_id hk_next, hk_prev, hk_style;
	us_audio last_audio;

	/* worker thread: simulation + raster + compose run here, never on OBS's video thread */
	us_thread thr;
	int thr_ok;
	us_mutex job_mu;
	us_cond job_cv;
	float job_dt;
	double job_clock;
	int job, quit;
	us_mutex audio_mu; /* guards audio_name / audio_weak */

	/* finished frame handed to the renderer */
	us_mutex front_mu;
	uint8_t *front;
	int front_w, front_h, front_crisp, fresh;
} us_src;

/* ---------------------------------------------------------------- audio */

static void audio_cb(void *param, obs_source_t *source, const struct audio_data *ad, bool muted)
{
	(void)source;
	us_src *s = param;
	if (muted || !ad || !ad->data[0] || ad->frames == 0)
		return;
	struct obs_audio_info oai;
	int rate = obs_get_audio_info(&oai) ? (int)oai.samples_per_sec : 48000;
	const float *L = (const float *)ad->data[0];
	const float *R = ad->data[1] ? (const float *)ad->data[1] : NULL;
	us_analyzer_push(s->an, L, R, (int)ad->frames, rate);
}

static void unhook_audio(us_src *s)
{
	if (s->audio_weak) {
		obs_source_t *src = obs_weak_source_get_source(s->audio_weak);
		if (src) {
			obs_source_remove_audio_capture_callback(src, audio_cb, s);
			obs_source_release(src);
		}
		obs_weak_source_release(s->audio_weak);
		s->audio_weak = NULL;
	}
}

static void hook_audio(us_src *s)
{
	unhook_audio(s);
	if (!s->audio_name || !*s->audio_name)
		return;
	obs_source_t *src = obs_get_source_by_name(s->audio_name);
	if (!src)
		return;
	if (src != s->self && (obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO)) {
		obs_source_add_audio_capture_callback(src, audio_cb, s);
		s->audio_weak = obs_source_get_weak_source(src);
	}
	obs_source_release(src);
}

/* ---------------------------------------------------------------- world */

static void world_save(void)
{
	if (!g_world_path)
		return;
	us_mutex_lock(&g_world_mu);
	int ok = us_world_save(us_world_shared(), g_world_path);
	us_mutex_unlock(&g_world_mu);
	static int warned;
	if (!ok && !warned) {
		warned = 1;
		blog(LOG_WARNING, "[understory] could not save the world to %s", g_world_path);
	}
}

/* ---------------------------------------------------------------- settings */

static const char *src_get_name(void *unused)
{
	(void)unused;
	return T_("Understory");
}

static void apply_settings(us_src *s, obs_data_t *st)
{
	us_mutex_lock(&s->mu);
	const char *scene = obs_data_get_string(st, "scene");
	uint32_t seed = (uint32_t)obs_data_get_int(st, "seed");
	/* only follow the dropdown when it actually changed, so hotkey / auto-cycle choices survive other edits */
	if (!s->applied_scene_setting || strcmp(s->applied_scene_setting, scene) != 0) {
		bfree(s->applied_scene_setting);
		s->applied_scene_setting = bstrdup(scene);
		const us_scene_vt *vt = us_scene_find(scene);
		for (int i = 0; i < us_scene_count; i++)
			if (us_scene_list[i] == vt)
				s->scene_idx = i;
	}
	s->eng.xfade_len = (float)obs_data_get_double(st, "xfade");
	us_engine_set_scene(&s->eng, us_scene_list[s->scene_idx]->id, seed);

	us_look look = {0};
	look.style = (int)obs_data_get_int(st, "style");
	uint32_t col = (uint32_t)obs_data_get_int(st, "tint"); /* 0xAABBGGRR */
	look.tint[0] = col & 0xff;
	look.tint[1] = (col >> 8) & 0xff;
	look.tint[2] = (col >> 16) & 0xff;
	look.palette = (int)obs_data_get_int(st, "palette");
	look.invert = obs_data_get_bool(st, "invert");
	look.glow = (float)obs_data_get_double(st, "glow");
	look.bloom = (float)obs_data_get_double(st, "bloom");
	s->out_w = (int)obs_data_get_int(st, "width");
	s->out_h = (int)obs_data_get_int(st, "height");
	us_engine_configure(&s->eng, &look, s->out_w, s->out_h, (int)obs_data_get_int(st, "pixel_size"),
			    (float)obs_data_get_double(st, "ink_scale"));

	s->eng.params.density = (float)obs_data_get_double(st, "density");
	s->eng.params.speed = (float)obs_data_get_double(st, "speed");
	s->eng.params.reactivity = (float)obs_data_get_double(st, "reactivity");
	s->eng.trails_bias = (float)obs_data_get_double(st, "trails");
	s->eng.linked = obs_data_get_bool(st, "link_world");
	s->eng.world_speed = (float)obs_data_get_double(st, "world_speed");
	s->sensitivity = (float)obs_data_get_double(st, "sensitivity");
	s->bass_gain = (float)obs_data_get_double(st, "bass_gain");
	s->mid_gain = (float)obs_data_get_double(st, "mid_gain");
	s->high_gain = (float)obs_data_get_double(st, "high_gain");
	s->auto_cycle_min = (float)obs_data_get_double(st, "auto_cycle");
	s->scene_mode = (int)obs_data_get_int(st, "scene_mode");

	const char *aname = obs_data_get_string(st, "audio_source");
	us_mutex_lock(&s->audio_mu);
	int rehook = !s->audio_name || strcmp(s->audio_name, aname) != 0 || !s->audio_weak;
	if (rehook) {
		bfree(s->audio_name);
		s->audio_name = bstrdup(aname);
		hook_audio(s);
		us_analyzer_reset(s->an);
	}
	us_mutex_unlock(&s->audio_mu);
	us_mutex_unlock(&s->mu);
}

static void src_update(void *data, obs_data_t *settings) { apply_settings(data, settings); }

static void src_defaults(obs_data_t *st)
{
	obs_data_set_default_string(st, "scene", "meadow");
	obs_data_set_default_string(st, "audio_source", "Desktop Audio");
	obs_data_set_default_int(st, "style", US_STYLE_INK);
	obs_data_set_default_int(st, "tint", 0xffebebeb);
	obs_data_set_default_int(st, "palette", 0);
	obs_data_set_default_bool(st, "invert", false);
	obs_data_set_default_double(st, "glow", 0.3);
	obs_data_set_default_double(st, "bloom", 0.5);
	obs_data_set_default_int(st, "pixel_size", 6);
	obs_data_set_default_double(st, "ink_scale", 0.67);
	obs_data_set_default_int(st, "width", 1920);
	obs_data_set_default_int(st, "height", 1080);
	obs_data_set_default_double(st, "density", 0.5);
	obs_data_set_default_double(st, "speed", 0.5);
	obs_data_set_default_double(st, "reactivity", 0.5);
	obs_data_set_default_double(st, "trails", 0.0);
	obs_data_set_default_double(st, "sensitivity", 1.0);
	obs_data_set_default_double(st, "bass_gain", 1.0);
	obs_data_set_default_double(st, "mid_gain", 1.0);
	obs_data_set_default_double(st, "high_gain", 1.0);
	obs_data_set_default_bool(st, "link_world", true);
	obs_data_set_default_double(st, "world_speed", 1.0);
	obs_data_set_default_double(st, "xfade", 2.0);
	obs_data_set_default_int(st, "scene_mode", 0);
	obs_data_set_default_double(st, "auto_cycle", 1.5);
	obs_data_set_default_int(st, "seed", 1);
}

static bool add_audio_source(void *param, obs_source_t *src)
{
	obs_property_t *list = param;
	uint32_t flags = obs_source_get_output_flags(src);
	if (flags & OBS_SOURCE_AUDIO) {
		const char *name = obs_source_get_name(src);
		if (name && *name)
			obs_property_list_add_string(list, name, name);
	}
	return true;
}

static bool reset_world_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	(void)props;
	(void)p;
	us_src *s = data;
	us_mutex_lock(&g_world_mu);
	us_world_init(us_world_shared());
	us_mutex_unlock(&g_world_mu);
	if (s) {
		us_mutex_lock(&s->mu);
		us_world_init(&s->eng.private_world);
		us_mutex_unlock(&s->mu);
	}
	world_save();
	return false;
}

static bool reseed_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	(void)props;
	(void)p;
	us_src *s = data;
	if (!s)
		return false;
	obs_data_t *st = obs_source_get_settings(s->self);
	obs_data_set_int(st, "seed", (int64_t)(os_gettime_ns() % 100000));
	obs_source_update(s->self, st);
	obs_data_release(st);
	return true;
}

static bool style_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *st)
{
	(void)p;
	int style = (int)obs_data_get_int(st, "style");
	int ink = us_style_is_ink(style);
	obs_property_set_visible(obs_properties_get(props, "palette"), style == US_STYLE_PIXEL || style == US_STYLE_COLOR);
	obs_property_set_visible(obs_properties_get(props, "tint"), style == US_STYLE_INK || style == US_STYLE_MONO);
	obs_property_set_visible(obs_properties_get(props, "pixel_size"), !ink);
	obs_property_set_visible(obs_properties_get(props, "ink_scale"), ink);
	obs_property_set_visible(obs_properties_get(props, "bloom"), ink);
	return true;
}

static bool mode_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *st)
{
	(void)p;
	obs_property_set_visible(obs_properties_get(props, "auto_cycle"), obs_data_get_int(st, "scene_mode") != 0);
	return true;
}

static obs_properties_t *src_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;

	p = obs_properties_add_list(props, "scene", T_("Scene"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (int i = 0; i < us_scene_count; i++)
		obs_property_list_add_string(p, us_scene_list[i]->name, us_scene_list[i]->id);

	p = obs_properties_add_list(props, "audio_source", T_("AudioSource"), OBS_COMBO_TYPE_EDITABLE,
				    OBS_COMBO_FORMAT_STRING);
	obs_enum_sources(add_audio_source, p);
	obs_property_set_long_description(p, T_("AudioSource.Help"));

	/* look */
	obs_properties_t *look = obs_properties_create();
	p = obs_properties_add_list(look, "style", T_("Style"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("Style.Ink"), US_STYLE_INK);
	obs_property_list_add_int(p, T_("Style.Color"), US_STYLE_COLOR);
	obs_property_list_add_int(p, T_("Style.Pixel"), US_STYLE_PIXEL);
	obs_property_list_add_int(p, T_("Style.Mono"), US_STYLE_MONO);
	obs_property_set_modified_callback(p, style_changed);
	obs_properties_add_color(look, "tint", T_("Tint"));
	p = obs_properties_add_list(look, "palette", T_("Palette"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int i = 0; i < us_palette_count; i++)
		obs_property_list_add_int(p, us_palettes[i].name, i);
	obs_properties_add_int_slider(look, "pixel_size", T_("PixelSize"), 2, 16, 1);
	obs_properties_add_float_slider(look, "ink_scale", T_("InkScale"), 0.25, 1.0, 0.05);
	obs_properties_add_float_slider(look, "glow", T_("Glow"), 0.0, 1.0, 0.05);
	obs_properties_add_float_slider(look, "bloom", T_("Bloom"), 0.0, 1.0, 0.05);
	obs_properties_add_bool(look, "invert", T_("Invert"));
	obs_properties_add_int(look, "width", T_("Width"), 64, 7680, 1);
	obs_properties_add_int(look, "height", T_("Height"), 36, 4320, 1);
	obs_properties_add_group(props, "grp_look", T_("Group.Look"), OBS_GROUP_NORMAL, look);

	/* nature */
	obs_properties_t *nat = obs_properties_create();
	obs_properties_add_float_slider(nat, "density", T_("Density"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(nat, "speed", T_("Speed"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(nat, "reactivity", T_("Reactivity"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(nat, "trails", T_("Trails"), -0.5, 0.5, 0.01);
	obs_properties_add_float_slider(nat, "xfade", T_("Crossfade"), 0.0, 10.0, 0.1);
	p = obs_properties_add_list(nat, "scene_mode", T_("SceneMode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T_("SceneMode.Manual"), 0);
	obs_property_list_add_int(p, T_("SceneMode.Order"), 1);
	obs_property_list_add_int(p, T_("SceneMode.Follow"), 2);
	obs_property_set_long_description(p, T_("SceneMode.Help"));
	obs_property_set_modified_callback(p, mode_changed);
	p = obs_properties_add_float_slider(nat, "auto_cycle", T_("AutoCycle"), 0.25, 30.0, 0.25);
	obs_property_set_long_description(p, T_("AutoCycle.Help"));
	obs_properties_add_int(nat, "seed", T_("Seed"), 0, 99999, 1);
	obs_properties_add_button2(nat, "reseed", T_("Reseed"), reseed_clicked, data);
	obs_properties_add_group(props, "grp_nature", T_("Group.Nature"), OBS_GROUP_NORMAL, nat);

	/* listening */
	obs_properties_t *aud = obs_properties_create();
	obs_properties_add_float_slider(aud, "sensitivity", T_("Sensitivity"), 0.1, 2.0, 0.01);
	obs_properties_add_float_slider(aud, "bass_gain", T_("BassGain"), 0.0, 2.0, 0.01);
	obs_properties_add_float_slider(aud, "mid_gain", T_("MidGain"), 0.0, 2.0, 0.01);
	obs_properties_add_float_slider(aud, "high_gain", T_("HighGain"), 0.0, 2.0, 0.01);
	obs_properties_add_group(props, "grp_audio", T_("Group.Audio"), OBS_GROUP_NORMAL, aud);

	/* world */
	obs_properties_t *wor = obs_properties_create();
	p = obs_properties_add_bool(wor, "link_world", T_("LinkWorld"));
	obs_property_set_long_description(p, T_("LinkWorld.Help"));
	obs_properties_add_float_slider(wor, "world_speed", T_("WorldSpeed"), 0.0, 10.0, 0.1);
	obs_properties_add_button2(wor, "reset_world", T_("ResetWorld"), reset_world_clicked, data);
	obs_properties_add_group(props, "grp_world", T_("Group.World"), OBS_GROUP_NORMAL, wor);

	return props;
}

/* ---------------------------------------------------------------- hotkeys */

static void step_scene(us_src *s, int dir)
{
	us_mutex_lock(&s->mu);
	s->scene_idx = (s->scene_idx + dir + us_scene_count) % us_scene_count;
	us_engine_set_scene(&s->eng, us_scene_list[s->scene_idx]->id, s->eng.seed);
	s->cycle_timer = 0;
	us_mutex_unlock(&s->mu);
}

static void hk_next(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed)
{
	(void)id;
	(void)hk;
	if (pressed)
		step_scene(data, 1);
}

static void hk_prev(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed)
{
	(void)id;
	(void)hk;
	if (pressed)
		step_scene(data, -1);
}

static void hk_style(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed)
{
	(void)id;
	(void)hk;
	us_src *s = data;
	if (!pressed)
		return;
	obs_data_t *st = obs_source_get_settings(s->self);
	obs_data_set_int(st, "style", (obs_data_get_int(st, "style") + 1) % US_STYLE_COUNT);
	obs_source_update(s->self, st);
	obs_data_release(st);
}

/* ---------------------------------------------------------------- lifecycle */

static void *worker_main(void *data);

static void *src_create(obs_data_t *settings, obs_source_t *source)
{
	us_src *s = bzalloc(sizeof(*s));
	s->self = source;
	us_mutex_init(&s->mu);
	us_mutex_init(&s->job_mu);
	us_cond_init(&s->job_cv);
	us_mutex_init(&s->audio_mu);
	us_mutex_init(&s->front_mu);
	us_engine_init(&s->eng);
	s->an = us_analyzer_create();
	s->sensitivity = s->bass_gain = s->mid_gain = s->high_gain = 1.0f;
	us_journey_init(&s->journey, (uint32_t)(uintptr_t)s);

	obs_enter_graphics();
	struct gs_sampler_info si = {0};
	si.filter = GS_FILTER_POINT;
	si.address_u = si.address_v = si.address_w = GS_ADDRESS_CLAMP;
	s->point = gs_samplerstate_create(&si);
	si.filter = GS_FILTER_LINEAR;
	s->linear = gs_samplerstate_create(&si);
	obs_leave_graphics();

	s->hk_next = obs_hotkey_register_source(source, "understory.next", T_("Hotkey.Next"), hk_next, s);
	s->hk_prev = obs_hotkey_register_source(source, "understory.prev", T_("Hotkey.Prev"), hk_prev, s);
	s->hk_style = obs_hotkey_register_source(source, "understory.style", T_("Hotkey.Style"), hk_style, s);

	apply_settings(s, settings);
	s->thr_ok = us_thread_create(&s->thr, worker_main, s);
	return s;
}

static void src_destroy(void *data)
{
	us_src *s = data;
	us_mutex_lock(&s->audio_mu);
	unhook_audio(s);
	us_mutex_unlock(&s->audio_mu);
	if (s->thr_ok) {
		us_mutex_lock(&s->job_mu);
		s->quit = 1;
		us_cond_signal(&s->job_cv);
		us_mutex_unlock(&s->job_mu);
		us_thread_join(s->thr);
	}
	obs_enter_graphics();
	if (s->tex)
		gs_texture_destroy(s->tex);
	gs_samplerstate_destroy(s->point);
	gs_samplerstate_destroy(s->linear);
	obs_leave_graphics();
	us_engine_free(&s->eng);
	us_analyzer_destroy(s->an);
	bfree(s->audio_name);
	bfree(s->applied_scene_setting);
	bfree(s->front);
	us_mutex_destroy(&s->mu);
	us_mutex_destroy(&s->job_mu);
	us_cond_destroy(&s->job_cv);
	us_mutex_destroy(&s->audio_mu);
	us_mutex_destroy(&s->front_mu);
	bfree(s);
	world_save();
}

static uint32_t src_width(void *data) { return (uint32_t)((us_src *)data)->out_w; }
static uint32_t src_height(void *data) { return (uint32_t)((us_src *)data)->out_h; }

/* one simulation step on the worker thread */
static void step(us_src *s, float seconds, double clock)
{
	us_mutex_lock(&s->mu);
	us_analyzer_update(s->an, seconds, s->sensitivity, s->bass_gain, s->mid_gain, s->high_gain, &s->last_audio);

	float per_scene = fmaxf(s->auto_cycle_min, 0.25f) * 60.0f;
	if (s->scene_mode == 1) {
		s->cycle_timer += seconds;
		if (s->cycle_timer > per_scene) {
			s->cycle_timer = 0;
			s->scene_idx = (s->scene_idx + 1) % us_scene_count;
			us_engine_set_scene(&s->eng, us_scene_list[s->scene_idx]->id, s->eng.seed);
		}
	} else if (s->scene_mode == 2) {
		us_mutex_lock(&g_world_mu);
		int nx = us_journey_step(&s->journey, &s->last_audio, us_engine_world(&s->eng), seconds, s->scene_idx,
					 per_scene, per_scene * 4.0f);
		us_mutex_unlock(&g_world_mu);
		if (nx >= 0) {
			s->scene_idx = nx;
			us_engine_set_scene(&s->eng, us_scene_list[nx]->id, s->eng.seed);
		}
	}
	us_mutex_lock(&g_world_mu);
	us_engine_frame(&s->eng, &s->last_audio, seconds, clock);
	int save = clock - g_last_save > WORLD_SAVE_INTERVAL;
	if (save)
		g_last_save = clock;
	us_mutex_unlock(&g_world_mu);

	/* hand the finished frame to the renderer */
	int cw = s->eng.canvas.w, ch = s->eng.canvas.h;
	if (s->eng.rgba && cw > 0 && ch > 0) {
		us_mutex_lock(&s->front_mu);
		if (s->front_w != cw || s->front_h != ch || !s->front) {
			bfree(s->front);
			s->front = bmalloc((size_t)cw * ch * 4);
			s->front_w = cw;
			s->front_h = ch;
		}
		memcpy(s->front, s->eng.rgba, (size_t)cw * ch * 4);
		s->front_crisp = s->eng.canvas.crisp;
		s->fresh = 1;
		us_mutex_unlock(&s->front_mu);
	}
	us_mutex_unlock(&s->mu);

	if (save)
		world_save();
}

static void *worker_main(void *data)
{
	us_src *s = data;
	os_set_thread_name("understory");
	for (;;) {
		us_mutex_lock(&s->job_mu);
		while (!s->job && !s->quit)
			us_cond_wait(&s->job_cv, &s->job_mu);
		if (s->quit) {
			us_mutex_unlock(&s->job_mu);
			break;
		}
		float dt = s->job_dt;
		double clock = s->job_clock;
		s->job = 0;
		s->job_dt = 0;
		us_mutex_unlock(&s->job_mu);
		step(s, dt, clock);
	}
	return NULL;
}

/* OBS video thread: cheap. Queues time for the worker; if the worker is behind, time accumulates
 * (capped) and the next step simply covers it, so OBS never waits on us. */
static void src_tick(void *data, float seconds)
{
	us_src *s = data;
	if (!obs_source_showing(s->self))
		return;

	/* audio source may appear after us (scene collection load order) or be recreated */
	s->rehook_timer += seconds;
	if (s->rehook_timer > 2.0 && us_mutex_trylock(&s->audio_mu)) {
		s->rehook_timer = 0;
		obs_source_t *a = s->audio_weak ? obs_weak_source_get_source(s->audio_weak) : NULL;
		if (!a)
			hook_audio(s);
		else
			obs_source_release(a);
		us_mutex_unlock(&s->audio_mu);
	}

	double clock = (double)obs_get_video_frame_time() * 1e-9;
	if (!s->thr_ok) {
		step(s, seconds, clock);
		return;
	}
	us_mutex_lock(&s->job_mu);
	s->job_dt = fminf(s->job_dt + seconds, 0.25f);
	s->job_clock = clock;
	s->job = 1;
	us_cond_signal(&s->job_cv);
	us_mutex_unlock(&s->job_mu);
}

static void src_render(void *data, gs_effect_t *unused)
{
	(void)unused;
	us_src *s = data;
	us_mutex_lock(&s->front_mu);
	int cw = s->front_w, ch = s->front_h;
	if (!s->front || cw <= 0) {
		us_mutex_unlock(&s->front_mu);
		return;
	}
	if (!s->tex || s->tex_w != cw || s->tex_h != ch) {
		if (s->tex)
			gs_texture_destroy(s->tex);
		s->tex = gs_texture_create(cw, ch, GS_RGBA, 1, NULL, GS_DYNAMIC);
		s->tex_w = cw;
		s->tex_h = ch;
		s->fresh = 1;
	}
	if (s->tex && s->fresh) {
		gs_texture_set_image(s->tex, s->front, (uint32_t)cw * 4, false);
		s->fresh = 0;
	}
	int crisp = s->front_crisp;
	us_mutex_unlock(&s->front_mu);
	if (!s->tex)
		return;

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *img = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(img, s->tex);
	gs_effect_set_next_sampler(img, crisp ? s->point : s->linear);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(s->tex, 0, (uint32_t)s->out_w, (uint32_t)s->out_h);
}

static struct obs_source_info understory_info = {
	.id = "understory_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.icon_type = OBS_ICON_TYPE_COLOR,
	.get_name = src_get_name,
	.create = src_create,
	.destroy = src_destroy,
	.update = src_update,
	.get_defaults = src_defaults,
	.get_properties = src_properties,
	.get_width = src_width,
	.get_height = src_height,
	.video_tick = src_tick,
	.video_render = src_render,
};

bool obs_module_load(void)
{
	g_world_path = obs_module_config_path("world.json");
	blog(LOG_INFO, "[understory] world file: %s", g_world_path ? g_world_path : "(none)");
	if (g_world_path) {
		char *dir = obs_module_config_path("");
		if (dir) {
			os_mkdirs(dir);
			bfree(dir);
		}
		if (us_world_load(us_world_shared(), g_world_path))
			blog(LOG_INFO, "[understory] world restored (season %.2f, age %.0fs)", us_world_shared()->season,
			     us_world_shared()->age);
	}
	obs_register_source(&understory_info);
	blog(LOG_INFO, "[understory] loaded %d scenes", us_scene_count);
	return true;
}

void obs_module_unload(void)
{
	world_save();
	bfree(g_world_path);
	g_world_path = NULL;
}
