#include "us_world.h"
#include "us_math.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static us_world g_shared;
static int g_shared_init;

us_world *us_world_shared(void)
{
	if (!g_shared_init) {
		us_world_init(&g_shared);
		g_shared_init = 1;
	}
	return &g_shared;
}

void us_world_init(us_world *w)
{
	memset(w, 0, sizeof(*w));
	w->moisture = 0.35f;
	w->water_level = 0.4f;
	w->growth = 0.15f;
	w->mycelium = 0.1f;
	w->insects = 0.2f;
	w->flock = 0.3f;
	w->reef = 0.1f;
	w->season = 0.15f; /* early spring */
	w->day_phase = 0.8f; /* evening */
	w->wind = 0.3f;
	w->last_step_t = -1;
}

void us_world_add(us_world *w, float *field, float rate, float dt)
{
	*field = us_sat(*field + rate * dt);
	w->dirty = 1;
}

float us_world_season_w(const us_world *w, int s)
{
	float x = fmodf(w->season, 4.0f);
	if (x < 0)
		x += 4.0f;
	float d = fabsf(x - (float)s);
	if (d > 2.0f)
		d = 4.0f - d;
	/* triangular-ish smooth window of width 1 */
	return us_smooth(1.0f, 0.0f, d);
}

int us_world_step(us_world *w, const us_audio *a, double t, float dt, float speed)
{
	if (w->last_step_t >= 0 && fabs(t - w->last_step_t) < 1e-6)
		return 0;
	w->last_step_t = t;
	if (dt <= 0)
		return 0;
	float sdt = dt * (speed < 0 ? 0 : speed);
	w->age += sdt;
	float e = a->level * a->active;
	w->energy += e * sdt;

	/* seasons: ~40 min of full-energy music per year, plus a slow drift even in silence */
	w->season = fmodf(w->season + sdt * (e * 4.0f / 2400.0f + 4.0f / 14400.0f), 4.0f);
	/* day: ~12 minutes per day, faster when the music is busy */
	w->day_phase = fmodf(w->day_phase + sdt * (1.0f / 720.0f) * (0.6f + 0.8f * e), 1.0f);
	w->daylight = us_sat(0.5f - 0.5f * cosf(US_TAU * w->day_phase) * 1.25f);

	float winter = us_world_season_w(w, 3), summer = us_world_season_w(w, 1);
	float spring = us_world_season_w(w, 0), autumn = us_world_season_w(w, 2);

	/* calm: rises in quiet, falls in density */
	float quiet = 1.0f - us_sat(a->level * 1.4f + a->onset * 0.5f);
	w->calm = us_approach(w->calm, quiet, quiet > w->calm ? 25.0f : 8.0f, sdt);

	/* wind: follows highs slowly, gusty in autumn */
	w->wind = us_approach(w->wind, us_sat(0.15f + 0.6f * a->high + 0.3f * autumn), 6.0f, sdt);

	/* moisture dries in sun/summer; feeds water level */
	w->moisture = us_sat(w->moisture - sdt * (0.0006f + 0.0012f * w->daylight * (0.5f + summer)) + sdt * 0.0004f * winter);
	float target_water = us_sat(0.15f + 0.85f * w->moisture);
	w->water_level = us_approach(w->water_level, target_water, 90.0f, sdt);

	/* storm charge: sustained energy (swell) builds it, decays otherwise */
	w->storm_charge = us_sat(w->storm_charge + sdt * (0.012f * us_smooth(0.45f, 0.85f, a->swell) - 0.004f));

	/* growth: needs moisture, boosted by mycelium, slowed in winter; winter dieback */
	float grow_rate = 0.0025f * (0.3f + w->moisture) * (0.6f + 1.4f * w->mycelium) * (1.0f - 0.85f * winter);
	w->growth = us_sat(w->growth + sdt * (grow_rate * (0.3f + a->bass) - 0.004f * winter));
	/* bloom: spring/summer, gated by growth */
	float bloom_target = us_sat(w->growth * 1.3f - 0.25f) * (spring + summer + 0.25f * autumn);
	w->bloom = us_approach(w->bloom, bloom_target, 40.0f, sdt);
	/* pollen: released by bloom + wind, settles */
	w->pollen = us_sat(w->pollen + sdt * (0.01f * w->bloom * (0.3f + w->wind)) - sdt * 0.006f);
	/* mycelium: slow, loves moisture, decays a little when dry */
	w->mycelium = us_sat(w->mycelium + sdt * (0.0015f * w->moisture - 0.0008f * (1.0f - w->moisture)));
	/* insects: calm + warm nights; die back in winter */
	float ins_t = us_sat((0.3f + 0.7f * w->calm) * (1.0f - w->daylight * 0.6f) * (1.0f - 0.9f * winter) * (0.4f + 0.6f * w->growth));
	w->insects = us_approach(w->insects, ins_t, 60.0f, sdt);
	/* flock: follows food (pollen, insects), migrates out in winter */
	float flock_t = us_sat((0.25f + 0.5f * w->pollen + 0.4f * w->insects) * (1.0f - 0.7f * winter));
	w->flock = us_approach(w->flock, flock_t, 80.0f, sdt);
	/* reef: very slow, fed by pollen (nutrients) and water */
	w->reef = us_sat(w->reef + sdt * (0.0006f * (0.2f + w->pollen) * (0.3f + w->water_level)) - sdt * 0.0001f);
	w->dirty = 1;
	return 1;
}

/* ---- persistence: flat JSON ----------------------------------------- */

#define FIELDS(X)                                                                                            \
	X(moisture) X(water_level) X(storm_charge) X(calm) X(growth) X(bloom) X(mycelium) X(pollen) X(insects) \
	X(flock) X(reef) X(season) X(daylight) X(day_phase) X(wind)

int us_world_save(const us_world *w, const char *path)
{
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE *f = fopen(tmp, "w");
	if (!f)
		return 0;
	fprintf(f, "{\n  \"version\": 1,\n  \"age\": %.3f,\n  \"energy\": %.3f", w->age, w->energy);
#define W(n) fprintf(f, ",\n  \"" #n "\": %.6f", (double)w->n);
	FIELDS(W)
#undef W
	fprintf(f, "\n}\n");
	int ok = fclose(f) == 0;
	if (ok)
		ok = rename(tmp, path) == 0;
	return ok;
}

static int find_num(const char *buf, const char *key, double *out)
{
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(buf, pat);
	if (!p)
		return 0;
	p = strchr(p + strlen(pat), ':');
	if (!p)
		return 0;
	char *end;
	double v = strtod(p + 1, &end);
	if (end == p + 1)
		return 0;
	*out = v;
	return 1;
}

int us_world_load(us_world *w, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return 0;
	char buf[4096];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;
	us_world tmp;
	us_world_init(&tmp);
	double v;
	if (find_num(buf, "age", &v)) tmp.age = v;
	if (find_num(buf, "energy", &v)) tmp.energy = v;
#define R(n)                                  \
	if (find_num(buf, #n, &v) && v == v) \
		tmp.n = (float)v;
	FIELDS(R)
#undef R
	tmp.season = fmodf(fabsf(tmp.season), 4.0f);
	tmp.day_phase = fmodf(fabsf(tmp.day_phase), 1.0f);
	tmp.last_step_t = -1;
	*w = tmp;
	return 1;
}
