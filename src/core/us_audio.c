#include "us_audio.h"
#include "us_math.h"
#include "us_thread.h"
#include <stdlib.h>
#include <string.h>

#define FFT_N 2048
#define RING 16384 /* power of two */
#define DRUM_BLOCK 32 /* samples per drum-detector decision */

/* ---- time-domain drum detectors ------------------------------------------------------------------
 * Each drum is a filtered energy envelope: a fast follower (a few ms) against a slow one (~a quarter
 * second). A hit is a sharp rise of fast over slow that is also loud enough relative to the whole mix,
 * so steady noise (wind, a brook, tape hiss) does not read as drums. Works per sample, so timing is
 * exact to ~1 ms instead of one FFT hop. */
typedef struct {
	float b0, b1, b2, a1, a2, z1, z2;
} us_bq;

typedef struct {
	us_bq f[3];
	float fast1, fast, slow;
	float inst; /* ~2 ms energy, used to tell which band a click really belongs to */
	int armed;     /* ratio has fallen back since the last hit */
	float pending; /* >0: seconds left in the window measuring a hit's peak */
	float peak;    /* peak fast/slow ratio inside that window */
	float peak_inst; /* loudest instant energy around the latest hit */
	float refr;    /* seconds until another hit may fire */
	float best;    /* strongest hit in this update, 0 = none */
} us_drum;

enum { DRUM_KICK, DRUM_SNARE, DRUM_HAT, DRUM_N };

struct us_analyzer {
	us_mutex mu;
	float ringM[RING], ringS[RING];
	uint64_t wpos;     /* total samples written */
	uint64_t consumed; /* wpos at last update */
	int rate;

	float win[FFT_N];
	float re[FFT_N], im[FFT_N];
	float prevlog[FFT_N / 2];
	float prevpw[3][FFT_N / 2]; /* the last frames' power per bin, for telling drums from notes */
	int prevpw_i;

	/* adaptive normalisation state per feature (log energy) */
	float lo[6 + US_NBANDS], hi[6 + US_NBANDS];
	float lvl_lo, lvl_hi;
	float flux_mean, flux_var;
	float refractory;

	us_audio st; /* smoothed state */
	float swell_acc;

	/* drums */
	us_drum drum[DRUM_N];
	int drum_rate, blk;
	float tot_slow;      /* whole-mix energy, slow */
	float since[DRUM_N]; /* seconds since each drum last hit */
	float cross[DRUM_N]; /* seconds since each band last crossed its threshold (hit or not) */
	float proc[RING];    /* fresh samples copied out of the ring */

	/* how much of the mix each region / band really holds (0..1), smoothed */
	float presence[6], band_presence[US_NBANDS];
};

static void bq_design(us_bq *f, int highpass, float fc, float q, float rate)
{
	if (fc <= 0) { /* pass-through */
		f->b0 = 1;
		f->b1 = f->b2 = f->a1 = f->a2 = f->z1 = f->z2 = 0;
		return;
	}
	if (fc > 0.45f * rate)
		fc = 0.45f * rate;
	float w0 = US_TAU * fc / rate, cw = cosf(w0), al = sinf(w0) / (2.0f * q);
	float a0 = 1.0f + al;
	if (highpass) {
		f->b0 = (1.0f + cw) * 0.5f / a0;
		f->b1 = -(1.0f + cw) / a0;
	} else {
		f->b0 = (1.0f - cw) * 0.5f / a0;
		f->b1 = (1.0f - cw) / a0;
	}
	f->b2 = f->b0;
	f->a1 = -2.0f * cw / a0;
	f->a2 = (1.0f - al) / a0;
	f->z1 = f->z2 = 0;
}

static inline float bq_run(us_bq *f, float x)
{
	float y = f->b0 * x + f->z1;
	f->z1 = f->b1 * x - f->a1 * y + f->z2;
	f->z2 = f->b2 * x - f->a2 * y;
	return y;
}

static void drums_design(us_analyzer *an, int rate)
{
	us_drum *k = &an->drum[DRUM_KICK], *s = &an->drum[DRUM_SNARE], *h = &an->drum[DRUM_HAT];
	bq_design(&k->f[0], 0, 110.0f, 0.707f, (float)rate);
	bq_design(&k->f[1], 0, 110.0f, 0.707f, (float)rate);
	bq_design(&k->f[2], 0, 0, 0, (float)rate);
	bq_design(&s->f[0], 1, 320.0f, 0.707f, (float)rate);
	bq_design(&s->f[1], 1, 320.0f, 0.707f, (float)rate);
	bq_design(&s->f[2], 0, 4500.0f, 0.707f, (float)rate);
	bq_design(&h->f[0], 1, 7000.0f, 0.707f, (float)rate);
	bq_design(&h->f[1], 1, 7000.0f, 0.707f, (float)rate);
	bq_design(&h->f[2], 0, 0, 0, (float)rate);
	an->drum_rate = rate;
}

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
	memset(an->prevpw, 0, sizeof(an->prevpw));
	an->prevpw_i = 0;
	for (int i = 0; i < 6 + US_NBANDS; i++) {
		an->lo[i] = -9.0f;
		an->hi[i] = -4.0f;
	}
	an->lvl_lo = -9.0f;
	an->lvl_hi = -2.5f;
	an->flux_mean = an->flux_var = 0;
	an->refractory = 0;
	memset(&an->st, 0, sizeof(an->st));
	an->swell_acc = 0;
	for (int d = 0; d < DRUM_N; d++) {
		an->drum[d].armed = 1;
		an->drum[d].pending = an->drum[d].peak = an->drum[d].peak_inst = an->drum[d].inst = 0;
		an->drum[d].fast1 = an->drum[d].fast = an->drum[d].slow = 0;
		an->drum[d].refr = an->drum[d].best = 0;
		an->since[d] = an->cross[d] = 10.0f;
	}
	an->drum_rate = 0;
	an->tot_slow = 0;
	for (int b = 0; b < 6; b++)
		an->presence[b] = 1.0f;
	for (int b = 0; b < US_NBANDS; b++)
		an->band_presence[b] = 1.0f;
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
		if (!(l > -8.0f && l < 8.0f)) /* NaN / inf / absurd values from a broken source */
			l = 0;
		if (!(r > -8.0f && r < 8.0f))
			r = 0;
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

/* run the drum detectors over fresh mono samples; leaves each drum's strongest hit in .best */
static void drums_run(us_analyzer *an, const float *x, int n, int rate, float sens)
{
	static const float fast_tau[DRUM_N] = {0.005f, 0.003f, 0.0015f};
	static const float slow_tau[DRUM_N] = {0.30f, 0.25f, 0.15f};
	static const float thr0[DRUM_N] = {2.5f, 2.5f, 3.0f};
	static const float rel_gate[DRUM_N] = {0.1f, 0.02f, 0.002f};
	static const float refr_s[DRUM_N] = {0.14f, 0.11f, 0.06f};
	if (rate < 8000)
		rate = 8000;
	if (an->drum_rate != rate)
		drums_design(an, rate);
	float af[DRUM_N], as[DRUM_N];
	for (int d = 0; d < DRUM_N; d++) {
		af[d] = 1.0f - expf(-1.0f / (fast_tau[d] * rate));
		as[d] = 1.0f - expf(-1.0f / (slow_tau[d] * rate));
		an->drum[d].best = 0;
	}
	float at = 1.0f - expf(-1.0f / (0.4f * rate));
	float ai = 1.0f - expf(-1.0f / (0.002f * rate));
	float sk = powf(1.4f, 1.0f - us_clampf(sens, 0.05f, 2.0f)); /* more sensitive -> lower thresholds */
	float bt = (float)DRUM_BLOCK / rate;
	for (int i = 0; i < n; i++) {
		float v = x[i];
		if (!(v > -8.0f && v < 8.0f)) /* NaN / inf / absurd from a broken source */
			v = 0;
		an->tot_slow += (v * v - an->tot_slow) * at;
		for (int d = 0; d < DRUM_N; d++) {
			us_drum *dr = &an->drum[d];
			float y = bq_run(&dr->f[2], bq_run(&dr->f[1], bq_run(&dr->f[0], v)));
			dr->fast1 += (y * y - dr->fast1) * af[d];
			dr->fast += (dr->fast1 - dr->fast) * af[d];
			dr->slow += (dr->fast - dr->slow) * as[d];
			dr->inst += (y * y - dr->inst) * ai;
		}
		if (++an->blk < DRUM_BLOCK)
			continue;
		an->blk = 0;
		for (int d = 0; d < DRUM_N; d++) {
			us_drum *dr = &an->drum[d];
			an->since[d] += bt;
			an->cross[d] += bt;
			dr->refr -= bt;
			float thr = thr0[d] * sk;
			float ratio = dr->fast / (dr->slow + 1e-12f);
			int loud = dr->fast > 1e-7f && dr->fast > rel_gate[d] * an->tot_slow;
			if (dr->pending > 0) {
				/* inside a hit: follow it to its peak, then report it */
				if (ratio > dr->peak)
					dr->peak = ratio;
				if (dr->inst > dr->peak_inst)
					dr->peak_inst = dr->inst;
				dr->pending -= bt;
				if (dr->pending <= 0) {
					/* one drum's leak into its neighbour's band: kick clicks in the snare band, snare wash in hats */
					const us_drum *K = &an->drum[DRUM_KICK], *S = &an->drum[DRUM_SNARE], *H = &an->drum[DRUM_HAT];
					int masked = (d == DRUM_SNARE && an->cross[DRUM_KICK] < 0.05f && K->peak_inst > 12.0f * dr->peak_inst) ||
						     (d == DRUM_SNARE && an->cross[DRUM_HAT] < 0.05f && H->peak_inst > 6.0f * dr->peak_inst) ||
						     (d == DRUM_HAT && an->cross[DRUM_SNARE] < 0.05f && S->peak_inst > 8.0f * dr->peak_inst);
					if (!masked) {
						float strength = us_sat(0.4f + 0.25f * log2f(dr->peak / thr));
						if (strength > dr->best)
							dr->best = strength;
						an->since[d] = 0;
					}
				}
			} else if (dr->armed && ratio > thr && loud && dr->refr <= 0) {
				dr->armed = 0;
				dr->pending = 0.02f;
				dr->peak = ratio;
				dr->peak_inst = dr->inst;
				dr->refr = refr_s[d];
				an->cross[d] = 0;
			}
			if (an->cross[d] < 0.05f && dr->inst > dr->peak_inst)
				dr->peak_inst = dr->inst; /* keep the click's loudness while it can still mask a neighbour */
			if (ratio < 0.8f * thr)
				dr->armed = 1;
		}
	}
	/* a denormal-proof floor: long silences must not leave the filters crawling through tiny floats */
	for (int d = 0; d < DRUM_N; d++)
		if (an->drum[d].slow < 1e-20f) {
			an->drum[d].slow = an->drum[d].fast = an->drum[d].fast1 = 0;
			an->drum[d].f[0].z1 = an->drum[d].f[0].z2 = an->drum[d].f[1].z1 = an->drum[d].f[1].z2 = 0;
		}
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
	int nproc = 0;

	us_mutex_lock(&an->mu);
	rate = an->rate;
	fresh = an->wpos - an->consumed;
	an->consumed = an->wpos;
	{
		/* the newest samples, oldest first, for the drum detectors (at most what the ring holds) */
		uint64_t take = fresh < RING ? fresh : RING;
		for (uint64_t i = 0; i < take; i++)
			an->proc[i] = an->ringM[(an->wpos - take + i) & (RING - 1)];
		nproc = (int)take;
	}
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
	drums_run(an, an->proc, nproc, rate, sens);

	fft(an->re, an->im, FFT_N);

	float binhz = (float)rate / FFT_N;
	static const float edges[7] = {20, 60, 250, 800, 2500, 8000, 16000};
	float e[6] = {0};
	float flux = 0, csum = 0, cw = 0;
	float bandE[US_NBANDS] = {0};
	float pw[FFT_N / 2];
	pw[0] = 0;
	for (int k = 1; k < FFT_N / 2; k++) {
		float f = k * binhz;
		float p = an->re[k] * an->re[k] + an->im[k] * an->im[k];
		pw[k] = p;
		for (int b = 0; b < 6; b++)
			if (f >= edges[b] && f < edges[b + 1]) {
				e[b] += p;
				break;
			}
		/* log-compressed magnitude for flux: near-silent bins contribute ~0 instead of log noise */
		float lg = log1pf(p * 0.05f);
		float d = lg - an->prevlog[k];
		an->prevlog[k] = lg;
		if (d > 0 && f > 30 && f < 12000)
			flux += d;
		if (f > 30 && f < 16000) {
			float lf = log2f(f / 30.0f) / log2f(16000.0f / 30.0f);
			int bi = (int)(lf * US_NBANDS);
			if (bi >= 0 && bi < US_NBANDS)
				bandE[bi] += p;
			csum += lf * p;
			cw += p;
		}
	}

	/* the lowest log bands are narrower than one FFT bin and catch no bins: give them the spectrum's
	 * power interpolated at their centre, scaled by their width, so they track the music like the rest */
	for (int b = 0; b < US_NBANDS; b++) {
		if (bandE[b] > 0)
			continue;
		float r = 16000.0f / 30.0f;
		float f0 = 30.0f * powf(r, (float)b / US_NBANDS), f1 = 30.0f * powf(r, (float)(b + 1) / US_NBANDS);
		float x = sqrtf(f0 * f1) / binhz;
		int k0 = (int)x;
		if (k0 < 1 || k0 + 1 >= FFT_N / 2)
			continue;
		float fr = x - k0;
		bandE[b] = (pw[k0] * (1 - fr) + pw[k0 + 1] * fr) * (f1 - f0) / binhz;
	}

	/* drums are noise: a hit lifts most of the bins in its band at once, while a new note lifts only its
	 * harmonics (and their window skirts). rise = share of the band's bins that jumped by 6 dB or more. */
	{
		static const float lo_hz[2] = {200.0f, 6000.0f}, hi_hz[2] = {4000.0f, 16000.0f};
		static const float jump[2] = {4.0f, 4.0f}, need[2] = {0.18f, 0.18f};
		const int which[2] = {DRUM_SNARE, DRUM_HAT};
		for (int q = 0; q < 2; q++) {
			if (an->drum[which[q]].best <= 0)
				continue;
			int k0 = (int)(lo_hz[q] / binhz), k1 = (int)(hi_hz[q] / binhz);
			if (k0 < 1)
				k0 = 1;
			if (k1 > FFT_N / 2 - 1)
				k1 = FFT_N / 2 - 1;
			double mean = 0;
			for (int k = k0; k < k1; k++)
				mean += pw[k];
			mean /= (k1 > k0 ? k1 - k0 : 1);
			float floor_p = (float)mean * 1e-3f + 1e-12f;
			int up = 0, n = 0;
			double lsum = 0, asum = 0;
			for (int k = k0; k < k1; k++, n++) {
				/* before the hit: the quietest of the last frames (a hit is reported ~20 ms late, so the
				 * newest previous frame may already hold its start) */
				float before = an->prevpw[0][k];
				before = an->prevpw[1][k] < before ? an->prevpw[1][k] : before;
				before = an->prevpw[2][k] < before ? an->prevpw[2][k] : before;
				if (pw[k] > floor_p && pw[k] > jump[q] * before)
					up++;
				/* flatness of the new energy: noise spreads it evenly, a synth note piles it into harmonics */
				float dpos = pw[k] - before;
				dpos = (dpos > 0 ? dpos : 0) + floor_p;
				lsum += log(dpos);
				asum += dpos;
			}
			float rise = n > 0 ? (float)up / n : 0.0f;
			float flat = n > 0 && asum > 0 ? (float)(exp(lsum / n) / (asum / n)) : 0.0f;
			if (flat < 0.12f)
				rise = 0;
			if (rise < need[q])
				an->drum[which[q]].best = 0;
		}
	}
	memcpy(an->prevpw[an->prevpw_i], pw, sizeof(pw));
	an->prevpw_i = (an->prevpw_i + 1) % 3;

	/* presence: how much of the mix each region holds, after a +3 dB/octave tilt (so a normal mix reads
	 * roughly level). A region 34 dB under the strongest one reads 0, within 14 dB reads 1. */
	if (have_audio) {
		float dens[6], top = 1e-20f;
		for (int b = 0; b < 6; b++) {
			float oct = log2f(edges[b + 1] / edges[b]);
			float fc = sqrtf(edges[b] * edges[b + 1]);
			dens[b] = e[b] / oct * (fc / 1000.0f);
			top = dens[b] > top ? dens[b] : top;
		}
		for (int b = 0; b < 6; b++) {
			float db = 10.0f * log10f(dens[b] / top + 1e-12f);
			float pr = us_smooth(-34.0f, -14.0f, db);
			an->presence[b] = follow(an->presence[b], pr, 0.08f, 0.6f, dt);
		}
		float btop = 1e-20f, bd[US_NBANDS];
		float r = 16000.0f / 30.0f;
		for (int b = 0; b < US_NBANDS; b++) {
			float fc = 30.0f * powf(r, (b + 0.5f) / US_NBANDS);
			bd[b] = bandE[b] * (fc / 1000.0f);
			btop = bd[b] > btop ? bd[b] : btop;
		}
		for (int b = 0; b < US_NBANDS; b++) {
			float db = 10.0f * log10f(bd[b] / btop + 1e-12f);
			an->band_presence[b] = follow(an->band_presence[b], us_smooth(-36.0f, -14.0f, db), 0.06f, 0.5f, dt);
		}
	}

	float gains[6] = {bass_gain, bass_gain, mid_gain, mid_gain, high_gain, high_gain};
	float target[6];
	for (int b = 0; b < 6; b++) {
		float lg = log10f(e[b] + 1e-10f);
		float v = have_audio ? adapt(lg, &an->lo[b], &an->hi[b], dt) : 0;
		/* sensitivity shapes the curve: >1 lifts quiet, <1 pushes it down */
		float shape = powf(2.0f, 1.0f - us_clampf(sens, 0.05f, 2.0f));
		target[b] = us_sat(powf(v * an->presence[b], shape) * gains[b]);
	}
	st->sub = follow(st->sub, target[0], 0.02f, 0.25f, dt);
	st->bass = follow(st->bass, target[1], 0.02f, 0.22f, dt);
	st->lowmid = follow(st->lowmid, target[2], 0.03f, 0.25f, dt);
	st->mid = follow(st->mid, target[3], 0.03f, 0.2f, dt);
	st->high = follow(st->high, target[4], 0.015f, 0.15f, dt);
	st->air = follow(st->air, target[5], 0.015f, 0.15f, dt);

	for (int b = 0; b < US_NBANDS; b++) {
		float lg = log10f(bandE[b] + 1e-10f);
		float v = have_audio ? adapt(lg, &an->lo[6 + b], &an->hi[6 + b], dt) * an->band_presence[b] : 0;
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
	float a = 1.0f - expf(-dt / 1.0f);
	if (have_audio) {
		float d = flux - an->flux_mean;
		an->flux_mean += a * d;
		an->flux_var = (1 - a) * (an->flux_var + a * d * d);
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
	/* drums: each envelope jumps to the hit's strength and eases away */
	float gk[DRUM_N] = {bass_gain, mid_gain, high_gain};
	float *envs[DRUM_N] = {&st->kick, &st->snare, &st->hat};
	int *hits[DRUM_N] = {&st->kick_hit, &st->snare_hit, &st->hat_hit};
	static const float decay[DRUM_N] = {0.15f, 0.14f, 0.07f};
	for (int d = 0; d < DRUM_N; d++) {
		*envs[d] = us_approach(*envs[d], 0, decay[d], dt);
		*hits[d] = 0;
		float hs = us_sat(an->drum[d].best * gk[d]);
		if (have_audio && hs > 0.05f) {
			*hits[d] = 1;
			if (hs > *envs[d])
				*envs[d] = hs;
		}
	}

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
