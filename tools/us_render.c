/* us_render — headless harness. Renders a scene with real or synthetic audio to PPM frames.
 *
 *   us_render --scene meadow [--style ink|color|pixel|mono] [--audio file.f32 | --synth]
 *             [--start S] [--seconds N] [--prewarm S] [--fps 30] [--size 1280x720] [--pixel 4]
 *             [--at 5,10,20 | --every K] [--out dir] [--tint r,g,b] [--palette i] [--invert]
 *             [--density d] [--speed s] [--react r] [--trails b] [--seed n]
 *             [--world in.json] [--world-out out.json] [--world-speed k] [--stats]
 *
 * --audio expects raw float32 stereo interleaved 48 kHz:
 *     ffmpeg -i song.wav -f f32le -ac 2 -ar 48000 song.f32
 * --synth uses a built-in 120 bpm test track (64 s loop: intro / build / drop / breakdown).
 * --prewarm runs the simulation for S seconds (with the same audio, from --start - S) before writing.
 * Frames are written at OUTPUT size (pixel styles upscaled nearest-neighbour, as OBS shows them).
 */
#include "../src/core/us_scene.h"
#include "../src/core/us_math.h"
#include "../src/core/us_journey.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ---- synthetic test track ------------------------------------------- */
typedef struct {
	double t;
	us_rng rng;
	float bph, hph, padph[3], lp;
} synth_t;

static void synth_block(synth_t *s, float *L, float *R, int n, int rate)
{
	const float bpm = 120.0f, beat = 60.0f / bpm;
	for (int i = 0; i < n; i++) {
		double t = s->t;
		float loop = fmodf((float)t, 64.0f);
		int sec = (int)(loop / 16.0f); /* 0 intro, 1 build, 2 drop, 3 breakdown */
		float bt = fmodf((float)t, beat);
		float st16 = fmodf((float)t, beat / 4);
		float out = 0, side = 0;
		/* kick */
		if (sec == 1 || sec == 2) {
			float env = expf(-bt * 9.0f);
			float f = 45.0f + 120.0f * expf(-bt * 30.0f);
			s->bph += f / rate;
			out += 0.9f * env * sinf(US_TAU * s->bph);
		}
		/* bass: offbeat sub stabs in drop */
		if (sec == 2) {
			float off = fmodf((float)t + beat / 2, beat);
			float env = expf(-off * 5.0f);
			float notes[4] = {55, 55, 65.4f, 49};
			float f = notes[((int)(t / (beat * 4))) & 3];
			out += 0.5f * env * sinf(US_TAU * f * (float)t) * (1.0f + 0.3f * sinf(US_TAU * f * 2 * (float)t));
		}
		/* hats */
		if (sec >= 1 && sec <= 2) {
			float env = expf(-st16 * 60.0f) * (sec == 2 ? 0.25f : 0.12f * (loop - 16) / 16);
			float nz = us_randr(&s->rng, -1, 1);
			s->lp += 0.6f * (nz - s->lp);
			float hp = nz - s->lp;
			out += env * hp;
			side += env * hp * 0.5f * sinf((float)t * 3);
		}
		/* snare on 2 & 4 in drop */
		if (sec == 2) {
			float bt2 = fmodf((float)t + beat, beat * 2);
			float env = expf(-bt2 * 18.0f);
			out += 0.35f * env * us_randr(&s->rng, -1, 1);
		}
		/* pad: always, louder in intro/breakdown */
		{
			float amp = (sec == 0 || sec == 3) ? 0.07f : 0.04f;
			float ch[3] = {220.0f, 277.2f, 329.6f};
			if (sec == 3) { ch[0] = 196; ch[1] = 246.9f; ch[2] = 293.7f; }
			float p = 0;
			for (int k = 0; k < 3; k++) {
				s->padph[k] += ch[k] * (1.0f + 0.002f * sinf((float)t * (0.3f + k))) / rate;
				p += sinf(US_TAU * s->padph[k]);
			}
			float sw = 0.5f + 0.5f * sinf((float)t * 0.25f);
			out += amp * p / 3 * sw;
			side += amp * 0.4f * sinf(US_TAU * s->padph[1] * 1.003f) * sw;
		}
		L[i] = out + side;
		R[i] = out - side;
		s->t += 1.0 / rate;
	}
}

/* ---- output --------------------------------------------------------- */
static int write_ppm(const char *path, const uint8_t *rgba, int cw, int ch, int ow, int oh)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return 0;
	fprintf(f, "P6\n%d %d\n255\n", ow, oh);
	uint8_t *row = malloc((size_t)ow * 3);
	for (int y = 0; y < oh; y++) {
		int sy = y * ch / oh;
		for (int x = 0; x < ow; x++) {
			int sx = x * cw / ow;
			const uint8_t *p = rgba + ((size_t)sy * cw + sx) * 4;
			row[x * 3] = p[0];
			row[x * 3 + 1] = p[1];
			row[x * 3 + 2] = p[2];
		}
		fwrite(row, 1, (size_t)ow * 3, f);
	}
	free(row);
	fclose(f);
	return 1;
}

int main(int argc, char **argv)
{
	const char *scene = "meadow", *audio = NULL, *out = "out", *world_in = NULL, *world_out = NULL, *at = NULL;
	int style = US_STYLE_INK, fps = 30, ow = 1280, oh = 720, pixel = 4, every = 0, palette = 0, invert = 0, stats = 0;
	int synth = 0, stats_every = 0;
	float bloom = 0.5f, xfade = 2.0f;
	float journey = 0; /* >0: follow-the-music director, value = min dwell seconds */
	float start = 0, seconds = 10, prewarm = 0, wspeed = 1;
	us_params prm = {0.5f, 0.5f, 0.5f};
	float trails = 0;
	uint8_t tint[3] = {235, 235, 235};
	uint32_t seed = 1;

	for (int i = 1; i < argc; i++) {
		const char *k = argv[i], *v = i + 1 < argc ? argv[i + 1] : "";
#define ARG(name) (strcmp(k, name) == 0 && ++i)
		if (ARG("--scene")) scene = v;
		else if (ARG("--style")) style = !strcmp(v, "pixel") ? US_STYLE_PIXEL : !strcmp(v, "mono") ? US_STYLE_MONO : !strcmp(v, "color") ? US_STYLE_COLOR : US_STYLE_INK;
		else if (ARG("--audio")) audio = v;
		else if (strcmp(k, "--synth") == 0) synth = 1;
		else if (strcmp(k, "--invert") == 0) invert = 1;
		else if (strcmp(k, "--stats") == 0) stats = 1;
		else if (ARG("--start")) start = (float)atof(v);
		else if (ARG("--stats-every")) { stats = 1; stats_every = atoi(v); }
		else if (ARG("--seconds")) seconds = (float)atof(v);
		else if (ARG("--prewarm")) prewarm = (float)atof(v);
		else if (ARG("--fps")) fps = atoi(v);
		else if (ARG("--size")) sscanf(v, "%dx%d", &ow, &oh);
		else if (ARG("--pixel")) pixel = atoi(v);
		else if (ARG("--at")) at = v;
		else if (ARG("--every")) every = atoi(v);
		else if (ARG("--out")) out = v;
		else if (ARG("--tint")) { int r, g, b; if (sscanf(v, "%d,%d,%d", &r, &g, &b) == 3) { tint[0] = r; tint[1] = g; tint[2] = b; } }
		else if (ARG("--palette")) palette = atoi(v);
		else if (ARG("--density")) prm.density = (float)atof(v);
		else if (ARG("--speed")) prm.speed = (float)atof(v);
		else if (ARG("--react")) prm.reactivity = (float)atof(v);
		else if (ARG("--trails")) trails = (float)atof(v);
		else if (ARG("--seed")) seed = (uint32_t)atoi(v);
		else if (ARG("--world")) world_in = v;
		else if (ARG("--world-out")) world_out = v;
		else if (ARG("--world-speed")) wspeed = (float)atof(v);
		else if (ARG("--xfade")) xfade = (float)atof(v);
		else if (ARG("--bloom")) bloom = (float)atof(v);
		else if (ARG("--journey")) journey = (float)atof(v);
		else { fprintf(stderr, "unknown arg %s\n", k); return 2; }
#undef ARG
	}
	if (!audio)
		synth = 1;

	float *pcm = NULL;
	long nframes = 0;
	if (!synth) {
		FILE *f = fopen(audio, "rb");
		if (!f) { fprintf(stderr, "cannot open %s\n", audio); return 1; }
		fseek(f, 0, SEEK_END);
		long bytes = ftell(f);
		fseek(f, 0, SEEK_SET);
		nframes = bytes / 8;
		pcm = malloc((size_t)bytes);
		if (fread(pcm, 1, (size_t)bytes, f) != (size_t)bytes) { fprintf(stderr, "short read\n"); return 1; }
		fclose(f);
	}

	{
		char tmp[1024];
		snprintf(tmp, sizeof(tmp), "%s", out);
		for (char *q = tmp + 1; *q; q++)
			if (*q == '/') {
				*q = 0;
				mkdir(tmp, 0755);
				*q = '/';
			}
		mkdir(tmp, 0755);
	}
	us_analyzer *an = us_analyzer_create();
	us_engine e;
	us_engine_init(&e);
	e.params = prm;
	e.trails_bias = trails;
	e.world_speed = wspeed;
	if (world_in && !us_world_load(us_world_shared(), world_in))
		fprintf(stderr, "warning: could not load world %s\n", world_in);
	us_look look = {.style = style, .palette = palette, .invert = invert, .glow = 0.3f, .bloom = bloom};
	memcpy(look.tint, tint, 3);
	us_engine_set_scene(&e, scene, seed);
	e.xfade_len = xfade;
	us_engine_configure(&e, &look, ow, oh, pixel, 1.0f);

	const int rate = 48000;
	float dt = 1.0f / fps;
	int spf = rate / fps;
	float *L = malloc(sizeof(float) * spf), *R = malloc(sizeof(float) * spf);
	synth_t sy = {0};
	us_rng_seed(&sy.rng, 7);

	float t0 = start - prewarm;
	if (t0 < 0) t0 = 0;
	long apos = (long)(t0 * rate);
	if (synth)
		sy.t = t0;
	int total = (int)((start + seconds - t0) * fps);
	double clock = 0, render_time = 0;
	int written = 0, rendered = 0;
	float at_list[256];
	int nat = 0;
	if (at) {
		char buf[2048];
		strncpy(buf, at, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = 0;
		for (char *tok = strtok(buf, ","); tok && nat < 256; tok = strtok(NULL, ","))
			at_list[nat++] = (float)atof(tok);
	}
	us_audio feat;
	for (int fi = 0; fi < total; fi++) {
		float t = t0 + fi * dt;
		if (synth) {
			synth_block(&sy, L, R, spf, rate);
		} else {
			for (int k = 0; k < spf; k++) {
				long p = apos + k;
				L[k] = p < nframes ? pcm[2 * p] : 0;
				R[k] = p < nframes ? pcm[2 * p + 1] : 0;
			}
			apos += spf;
		}
		us_analyzer_push(an, L, R, spf, rate);
		us_analyzer_update(an, dt, 1.0f, 1.0f, 1.0f, 1.0f, &feat);
		clock += dt;
		double r0 = now_s();
		if (journey > 0) {
			static us_journey jn;
			static int jinit;
			if (!jinit) {
				us_journey_init(&jn, seed);
				jinit = 1;
			}
			int cur = 0;
			for (int k = 0; k < us_scene_count; k++)
				if (us_scene_list[k] == e.vt)
					cur = k;
			int nx = us_journey_step(&jn, &feat, us_engine_world(&e), dt, cur, journey, journey * 5);
			if (nx >= 0) {
				printf("journey t=%6.1f %s -> %s  (mood e=%.2f fast=%.2f bright=%.2f perc=%.2f)\n", t,
				       e.vt->id, us_scene_list[nx]->id, jn.energy, jn.fast_energy, jn.bright, jn.perc);
				us_engine_set_scene(&e, us_scene_list[nx]->id, e.seed);
			}
		}
		us_engine_frame(&e, &feat, dt, clock);
		render_time += now_s() - r0;
		rendered++;

		if (t + 1e-4f < start)
			continue;
		int want = 0;
		if (nat) {
			for (int k = 0; k < nat; k++)
				if (fabsf(t - at_list[k]) < dt * 0.5f)
					want = 1;
		} else if (every > 0) {
			want = (fi % every) == 0;
		} else {
			want = 1;
		}
		if (stats && (fi % (stats_every > 0 ? stats_every : fps)) == 0) {
			us_world *w = us_world_shared();
			printf("t=%6.1f lvl=%.2f bass=%.2f high=%.2f onset=%.2f kick=%.2f swell=%.2f | grow=%.2f bloom=%.2f moist=%.2f water=%.2f calm=%.2f season=%.2f\n",
			       t, feat.level, feat.bass, feat.high, feat.onset, feat.kick, feat.swell, w->growth, w->bloom,
			       w->moisture, w->water_level, w->calm, w->season);
		}
		if (want) {
			char path[1024];
			snprintf(path, sizeof(path), "%s/%s_%06d.ppm", out, scene, written);
			write_ppm(path, e.rgba, e.canvas.w, e.canvas.h, ow, oh);
			written++;
		}
	}
	printf("scene=%s style=%d canvas=%dx%d frames=%d written=%d avg_frame=%.2f ms\n", e.vt->id, style, e.canvas.w,
	       e.canvas.h, rendered, written, rendered ? render_time / rendered * 1000.0 : 0);
	if (world_out)
		us_world_save(us_world_shared(), world_out);
	us_engine_free(&e);
	us_analyzer_destroy(an);
	free(pcm);
	free(L);
	free(R);
	return 0;
}
