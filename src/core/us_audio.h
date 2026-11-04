/* Understory — audio analysis. Thread-safe push from the audio thread; update() from the video thread.
 * All features are auto-gained to ~0..1 so scenes react the same way to quiet and loud mixes. */
#pragma once
#include <stdint.h>

#define US_NBANDS 32

typedef struct us_audio {
	/* smoothed, auto-gained 0..1 */
	float sub;      /* 20–60 Hz  */
	float bass;     /* 60–250 Hz */
	float lowmid;   /* 250–800   */
	float mid;      /* 800–2.5k  */
	float high;     /* 2.5k–8k   */
	float air;      /* 8k–16k    */
	float level;    /* overall loudness, 0..1 */
	float swell;    /* slow loudness (~8 s), phrase-scale */
	float onset;    /* transient strength, jumps on hits and decays (~0.15 s) */
	int beat;       /* 1 on the frame a transient is detected */
	float kick;     /* low-frequency transient envelope (kicks) */
	float centroid; /* spectral brightness 0..1 (log-frequency) */
	float width;    /* stereo width 0..1 */
	float silence;  /* seconds of near-silence */
	float active;   /* 0..1, eases to 1 while audio is present */
	float bands[US_NBANDS]; /* log-spaced spectrum, auto-gained */
} us_audio;

typedef struct us_analyzer us_analyzer;

us_analyzer *us_analyzer_create(void);
void us_analyzer_destroy(us_analyzer *an);
/* L/R planar float. R may be NULL (mono). Any thread. */
void us_analyzer_push(us_analyzer *an, const float *L, const float *R, int frames, int rate);
/* Video thread: compute features for this frame.
 * sensitivity 0..2 (1 = neutral); gains per region (1 = neutral). */
void us_analyzer_update(us_analyzer *an, float dt, float sensitivity, float bass_gain, float mid_gain,
			float high_gain, us_audio *out);
void us_analyzer_reset(us_analyzer *an);
