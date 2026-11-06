#include "us_audio.h"
#include "us_math.h"
#include "us_thread.h"
#include <stdlib.h>
#include <string.h>

#define FFT_N 2048
#define RING 16384 /* power of two */

struct us_analyzer {
	us_mutex mu;
	float ringM[RING], ringS[RING];
	uint64_t wpos;     /* total samples written */
	uint64_t consumed; /* wpos at last update */
	int rate;

	float win[FFT_N];
	float re[FFT_N], im[FFT_N];
	float prevlog[FFT_N / 2];

	/* adaptive normalisation state per feature (log energy) */
	float lo[6 + US_NBANDS], hi[6 + US_NBANDS];
	float lvl_lo, lvl_hi;
	float flux_mean, flux_var;
	float kflux_mean, kflux_var;
	float refractory;

	us_audio st; /* smoothed state */
	float swell_acc;
};

us_analyzer *us_analyzer_create(void)
{
	us_analyzer *an = calloc(1, sizeof(*an));
	if (!an)
		return NULL;
	us_mutex_init(&an->mu);
	for (int i = 0; i < FFT_N; i++)
		an->win[i] = 0.5f - 0.5f * cosf(US_TAU * i / (FFT_N - 1));
	us_analyzer_reset(an);
	return an;
}

void us_analyzer_reset(us_analyzer *an)
{
	us_mutex_lock(&an->mu);
	memset(an->ringM, 0, sizeof(an->ringM));
	memset(an->ringS, 0, sizeof(an->ringS));
	an->wpos = an->consumed = 0;
	an->rate = 48000;
	us_mutex_unlock(&an->mu);
	memset(an->prevlog, 0, sizeof(an->prevlog));
	for (int i = 0; i < 6 + US_NBANDS; i++) {
		an->lo[i] = -9.0f;
		an->hi[i] = -4.0f;
	}
	an->lvl_lo = -9.0f;
	an->lvl_hi = -2.5f;
	an->flux_mean = an->flux_var = 0;
	an->kflux_mean = an->kflux_var = 0;
	an->refractory = 0;
	memset(&an->st, 0, sizeof(an->st));
	an->swell_acc = 0;
}

void us_analyzer_destroy(us_analyzer *an)
{
	if (!an)
		return;
	us_mutex_destroy(&an->mu);
	free(an);
}

void us_analyzer_push(us_analyzer *an, const float *L, const float *R, int frames, int rate)
{
	if (!an || !L || frames <= 0)
		return;
	us_mutex_lock(&an->mu);
	an->rate = rate > 0 ? rate : 48000;
	for (int i = 0; i < frames; i++) {
		float l = L[i], r = R ? R[i] : l;
		size_t p = (size_t)(an->wpos & (RING - 1));
		an->ringM[p] = 0.5f * (l + r);
		an->ringS[p] = 0.5f * (l - r);
		an->wpos++;
	}
	us_mutex_unlock(&an->mu);
}

static void fft(float *re, float *im, int n)
{
	for (int i = 1, j = 0; i < n; i++) {
		int bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j) {
			float t = re[i]; re[i] = re[j]; re[j] = t;
			t = im[i]; im[i] = im[j]; im[j] = t;
		}
	}
	for (int len = 2; len <= n; len <<= 1) {
		float ang = -US_TAU / len;
		float wr = cosf(ang), wi = sinf(ang);
		for (int i = 0; i < n; i += len) {
			float cr = 1, ci = 0;
			for (int k = 0; k < len / 2; k++) {
				int a = i + k, b = i + k + len / 2;
				float tr = re[b] * cr - im[b] * ci;
				float ti = re[b] * ci + im[b] * cr;
				re[b] = re[a] - tr;
				im[b] = im[a] - ti;
				re[a] += tr;
				im[a] += ti;
				float ncr = cr * wr - ci * wi;
				ci = cr * wi + ci * wr;
				cr = ncr;
			}
		}
	}
}

/* adaptive range: hi tracks peaks (fast up, slow down), lo tracks floor (fast down, slow up) */
static float adapt(float x, float *lo, float *hi, float dt)
{
	if (x > *hi)
		*hi = us_approach(*hi, x, 0.05f, dt);
	else
		*hi = us_approach(*hi, x, 30.0f, dt);
	/* floor follows the troughs of the last few seconds so features pump with the music */
	if (x < *lo)
		*lo = us_approach(*lo, x, 0.15f, dt);
	else
		*lo = us_approach(*lo, x, 3.0f, dt);
	float range = *hi - *lo;
	if (range < 1.2f) { /* never stretch tiny ranges (avoids amplifying noise floors) */
		*lo = *hi - 1.2f;
		range = 1.2f;
	}
	return us_sat((x - *lo) / range);
}

static float follow(float cur, float target, float atk, float rel, float dt)
{
	return us_approach(cur, target, target > cur ? atk : rel, dt);
}

void us_analyzer_update(us_analyzer *an, float dt, float sens, float bass_gain, float mid_gain, float high_gain,
			us_audio *out)
{
	if (dt <= 0)
		dt = 1.0f / 60.0f;
	if (dt > 0.25f)
		dt = 0.25f;
	float side_e = 0, mid_e = 0;
	int rate;
	uint64_t fresh;

	us_mutex_lock(&an->mu);
	rate = an->rate;
	fresh = an->wpos - an->consumed;
	an->consumed = an->wpos;
	uint64_t start = an->wpos >= FFT_N ? an->wpos - FFT_N : 0;
	for (int i = 0; i < FFT_N; i++) {
		uint64_t idx = start + i;
		float m = 0, s = 0;
		if (idx < an->wpos) {
			m = an->ringM[idx & (RING - 1)];
			s = an->ringS[idx & (RING - 1)];
		}
		an->re[i] = m * an->win[i];
		an->im[i] = 0;
		mid_e += m * m;
		side_e += s * s;
	}
	us_mutex_unlock(&an->mu);

	us_audio *st = &an->st;
	int have_audio = fresh > 0 && mid_e > 1e-9f;

	fft(an->re, an->im, FFT_N);

	float binhz = (float)rate / FFT_N;
	static const float edges[7] = {20, 60, 250, 800, 2500, 8000, 16000};
	float e[6] = {0};
	float flux = 0, kflux = 0, csum = 0, cw = 0;
	float bandE[US_NBANDS] = {0};
	for (int k = 1; k < FFT_N / 2; k++) {
		float f = k * binhz;
		float p = an->re[k] * an->re[k] + an->im[k] * an->im[k];
		for (int b = 0; b < 6; b++)
			if (f >= edges[b] && f < edges[b + 1]) {
				e[b] += p;
				break;
			}
		/* log-compressed magnitude for flux: near-silent bins contribute ~0 instead of log noise */
		float lg = log1pf(p * 0.05f);
		float d = lg - an->prevlog[k];
		an->prevlog[k] = lg;
		if (d > 0 && f > 30 && f < 12000) {
			flux += d;
			if (f < 180)
				kflux += d;
		}
		if (f > 30 && f < 16000) {
			float lf = log2f(f / 30.0f) / log2f(16000.0f / 30.0f);
			int bi = (int)(lf * US_NBANDS);
			if (bi >= 0 && bi < US_NBANDS)
				bandE[bi] += p;
			csum += lf * p;
			cw += p;
		}
	}

	float gains[6] = {bass_gain, bass_gain, mid_gain, mid_gain, high_gain, high_gain};
	float target[6];
	for (int b = 0; b < 6; b++) {
		float lg = log10f(e[b] + 1e-10f);
		float v = have_audio ? adapt(lg, &an->lo[b], &an->hi[b], dt) : 0;
		/* sensitivity shapes the curve: >1 lifts quiet, <1 pushes it down */
		float shape = powf(2.0f, 1.0f - us_clampf(sens, 0.05f, 2.0f));
		target[b] = us_sat(powf(v, shape) * gains[b]);
	}
	st->sub = follow(st->sub, target[0], 0.02f, 0.25f, dt);
	st->bass = follow(st->bass, target[1], 0.02f, 0.22f, dt);
	st->lowmid = follow(st->lowmid, target[2], 0.03f, 0.25f, dt);
	st->mid = follow(st->mid, target[3], 0.03f, 0.2f, dt);
	st->high = follow(st->high, target[4], 0.015f, 0.15f, dt);
	st->air = follow(st->air, target[5], 0.015f, 0.15f, dt);

	for (int b = 0; b < US_NBANDS; b++) {
		float lg = log10f(bandE[b] + 1e-10f);
		float v = have_audio ? adapt(lg, &an->lo[6 + b], &an->hi[6 + b], dt) : 0;
		st->bands[b] = follow(st->bands[b], v, 0.02f, 0.2f, dt);
	}

	/* level: RMS in dB-ish log domain, adaptive */
	float rms_lg = log10f(mid_e / FFT_N + 1e-10f);
	float lvl = have_audio ? adapt(rms_lg, &an->lvl_lo, &an->lvl_hi, dt) : 0;
	/* anchor to absolute loudness so a quiet pad never reads as "full" (-55 dBFS -> 0, -15 dBFS -> 1) */
	float dbfs = 10.0f * (rms_lg + 0.6f); /* +0.6 ~ compensates the Hann window's power loss */
	float absl = us_sat((dbfs + 55.0f) / 40.0f);
	lvl = us_sat(lvl * (0.3f + 0.7f * absl) * (0.6f + 0.4f * sens));
	st->level = follow(st->level, lvl, 0.03f, 0.3f, dt);
	st->swell = us_approach(st->swell, st->level, 8.0f, dt);

	/* onsets: spectral flux vs running mean + k*sd */
	float thr = an->flux_mean + 1.5f * sqrtf(an->flux_var) + 2.0f;
	float kthr = an->kflux_mean + 1.5f * sqrtf(an->kflux_var) + 0.6f;
	float a = 1.0f - expf(-dt / 1.0f);
	if (have_audio) {
		float d = flux - an->flux_mean;
		an->flux_mean += a * d;
		an->flux_var = (1 - a) * (an->flux_var + a * d * d);
		float kd = kflux - an->kflux_mean;
		an->kflux_mean += a * kd;
		an->kflux_var = (1 - a) * (an->kflux_var + a * kd * kd);
	}
	an->refractory -= dt;
	st->beat = 0;
	float onset_target = 0;
	if (have_audio && flux > thr && an->refractory <= 0) {
		float strength = us_sat((flux - thr) / (thr + 1e-3f) * 1.5f + 0.35f);
		onset_target = strength * (0.5f + 0.5f * sens);
		st->beat = 1;
		an->refractory = 0.09f;
	}
	if (onset_target > st->onset)
		st->onset = us_sat(onset_target);
	else
		st->onset = us_approach(st->onset, 0, 0.12f, dt);
	if (have_audio && kflux > kthr) {
		float ks = us_sat((kflux - kthr) / (kthr + 1e-3f) + 0.4f) * bass_gain;
		if (ks > st->kick)
			st->kick = us_sat(ks);
	}
	st->kick = us_approach(st->kick, 0, 0.15f, dt);

	float cen = cw > 0 ? csum / cw : 0;
	st->centroid = follow(st->centroid, have_audio ? cen : st->centroid, 0.1f, 0.4f, dt);
	float w = (mid_e + side_e) > 1e-9f ? us_sat(sqrtf(side_e / (mid_e + 1e-12f)) * 1.5f) : 0;
	st->width = follow(st->width, w, 0.2f, 0.6f, dt);

	/* silence is absolute (below ~-62 dBFS), so a soft pad still counts as music */
	if (!have_audio || dbfs < -62.0f)
		st->silence += dt;
	else
		st->silence = 0;
	st->active = us_approach(st->active, have_audio && st->silence < 1.5f ? 1.0f : 0.0f, 1.0f, dt);

	*out = *st;
}
