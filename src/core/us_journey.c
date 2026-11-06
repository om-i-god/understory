#include "us_journey.h"
#include "us_math.h"
#include "us_scene.h"
#include <math.h>
#include <string.h>

/* What kind of moment each scene suits: target energy, brightness, percussiveness (0..1). */
typedef struct {
	const char *id;
	float energy, bright, perc;
} profile;

static const profile PROFILES[] = {
	{"meadow", 0.55f, 0.50f, 0.50f},     {"wind", 0.55f, 0.75f, 0.35f},
	{"river", 0.50f, 0.45f, 0.40f},      {"rain", 0.60f, 0.55f, 0.80f},
	{"storm", 0.90f, 0.40f, 0.85f},      {"pond", 0.40f, 0.50f, 0.70f},
	{"fireflies", 0.15f, 0.55f, 0.30f},  {"mycelium", 0.65f, 0.25f, 0.75f},
	{"murmuration", 0.70f, 0.60f, 0.55f}, {"tide", 0.30f, 0.35f, 0.20f},
	{"pollen", 0.35f, 0.80f, 0.30f},     {"coral", 0.25f, 0.30f, 0.20f},
	{"aurora", 0.20f, 0.70f, 0.10f},
	{"abyss", 0.30f, 0.25f, 0.20f},
	{"dunes", 0.50f, 0.60f, 0.50f},
	{"snowfall", 0.35f, 0.65f, 0.25f},
};

static const profile *find_profile(const char *id)
{
	for (size_t i = 0; i < sizeof(PROFILES) / sizeof(PROFILES[0]); i++)
		if (!strcmp(PROFILES[i].id, id))
			return &PROFILES[i];
	return NULL;
}

/* the world nudges the choice: what has been happening in the hidden ecosystem */
static float world_bonus(const char *id, const us_world *w)
{
	float night = 1.0f - w->daylight;
	float winter = us_world_winter(w);
	float summer = us_world_season_w(w, 1), spring = us_world_season_w(w, 0);
	if (!strcmp(id, "storm"))
		return 0.5f * w->storm_charge;
	if (!strcmp(id, "rain"))
		return 0.25f * w->moisture + 0.15f * winter;
	if (!strcmp(id, "river"))
		return 0.35f * w->water_level;
	if (!strcmp(id, "fireflies"))
		return 0.3f * w->insects * night + 0.1f * summer;
	if (!strcmp(id, "aurora"))
		return 0.25f * night + 0.25f * winter;
	if (!strcmp(id, "meadow"))
		return 0.2f * (spring + summer) * w->growth + 0.1f * w->bloom;
	if (!strcmp(id, "pollen"))
		return 0.3f * w->pollen;
	if (!strcmp(id, "murmuration"))
		return 0.25f * w->flock;
	if (!strcmp(id, "coral"))
		return 0.2f * w->reef;
	if (!strcmp(id, "mycelium"))
		return 0.15f * w->mycelium + 0.1f * w->moisture;
	if (!strcmp(id, "wind"))
		return 0.3f * w->wind;
	if (!strcmp(id, "tide"))
		return 0.15f * w->water_level + 0.1f * night;
	if (!strcmp(id, "abyss"))
		return 0.2f * night + 0.15f * w->reef + 0.1f * w->water_level;
	if (!strcmp(id, "pond"))
		return 0.1f * w->water_level + 0.1f * summer;
	if (!strcmp(id, "dunes"))
		return 0.15f * summer + 0.15f * (1.0f - w->moisture) + 0.1f * w->wind;
	if (!strcmp(id, "snowfall"))
		return 0.45f * winter + 0.1f * night * winter;
	return 0;
}

void us_journey_init(us_journey *j, uint32_t seed)
{
	memset(j, 0, sizeof(*j));
	for (int i = 0; i < 4; i++)
		j->recent[i] = -1;
	j->rng = seed * 2654435761u + 12345u;
	j->energy = j->fast_energy = j->settle = 0.4f;
	j->bright = 0.5f;
	j->lo = 0.2f;
	j->hi = 0.6f;
	j->calm = 0.5f;
}

static float rnd(us_journey *j)
{
	j->rng = j->rng * 1664525u + 1013904223u;
	return (float)(j->rng >> 8) / 16777216.0f;
}

float us_journey_score(const us_journey *j, const us_world *w, int idx)
{
	if (idx < 0 || idx >= us_scene_count)
		return -9;
	const us_scene_vt *vt = us_scene_list[idx];
	const profile *p = find_profile(vt->id);
	float s = 1.0f;
	if (p) {
		s -= 1.6f * fabsf(j->energy - p->energy);
		s -= 0.6f * fabsf(j->bright - p->bright);
		s -= 0.8f * fabsf(j->perc - p->perc);
	}
	return s + world_bonus(vt->id, w);
}

int us_journey_step(us_journey *j, const us_audio *a, const us_world *w, float dt, int cur, float min_dwell,
		    float max_dwell)
{
	if (dt <= 0)
		return -1;
	j->dwell += dt;
	/* mood: only learn while music is playing, so a pause doesn't erase the character of the set */
	if (a->active > 0.5f) {
		/* loudness relative to this set's own range, so a quiet ambient set still has peaks and valleys */
		float lv = a->level;
		j->lo = lv < j->lo ? us_approach(j->lo, lv, 2.0f, dt) : us_approach(j->lo, lv, 90.0f, dt);
		j->hi = lv > j->hi ? us_approach(j->hi, lv, 2.0f, dt) : us_approach(j->hi, lv, 90.0f, dt);
		float range = fmaxf(j->hi - j->lo, 0.2f);
		float rel = us_sat((lv - j->lo) / range);
		float rel_sw = us_sat((a->swell - j->lo) / range);
		j->energy = us_approach(j->energy, 0.6f * rel_sw + 0.4f * rel, 15.0f, dt);
		j->fast_energy = us_approach(j->fast_energy, rel, 3.0f, dt);
		j->bright = us_approach(j->bright, a->centroid, 20.0f, dt);
		j->onset_rate = us_approach(j->onset_rate, a->beat ? 1.0f / dt : 0.0f, 8.0f, dt);
		j->perc = us_approach(j->perc, us_sat(j->onset_rate / 3.0f), 10.0f, dt);
	}
	j->calm = us_approach(j->calm, a->active > 0.5f ? 1.0f - j->fast_energy : 1.0f, 20.0f, dt);

	if (j->dwell < min_dwell)
		return -1;
	/* turning points: the music's energy has moved well away from where it was when this scene began,
	 * and the fast envelope confirms it (a drop has landed, or a breakdown has opened) */
	float moved = fabsf(j->fast_energy - j->settle);
	int turning = moved > 0.28f && fabsf(j->fast_energy - j->energy) > 0.12f;
	/* or the current scene simply no longer fits the mood */
	float here = us_journey_score(j, w, cur);
	int best = -1;
	float bestv = -1e9f;
	for (int i = 0; i < us_scene_count; i++) {
		if (i == cur)
			continue;
		float v = us_journey_score(j, w, i);
		/* while a turn is happening, judge by where the music is going, not the slow average */
		if (turning) {
			us_journey tmp = *j;
			tmp.energy = j->fast_energy;
			v = us_journey_score(&tmp, w, i);
		}
		for (int r = 0; r < 4; r++)
			if (j->recent[r] == i)
				v -= 0.35f * (4 - r) / 4.0f; /* keep the journey moving */
		v += 0.25f * rnd(j);
		if (v > bestv) {
			bestv = v;
			best = i;
		}
	}
	int drifted = bestv > here + 0.45f;
	int stale = max_dwell > 0 && j->dwell > max_dwell;
	if (best < 0 || !(turning || drifted || stale))
		return -1;
	memmove(&j->recent[1], &j->recent[0], 3 * sizeof(int));
	j->recent[0] = cur;
	j->dwell = 0;
	j->settle = j->fast_energy;
	return best;
}
