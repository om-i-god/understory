#include "../core/us_scene.h"

extern const us_scene_vt us_scene_meadow;
extern const us_scene_vt us_scene_wind;
extern const us_scene_vt us_scene_river;
extern const us_scene_vt us_scene_rain;
extern const us_scene_vt us_scene_storm;
extern const us_scene_vt us_scene_pond;
extern const us_scene_vt us_scene_fireflies;
extern const us_scene_vt us_scene_mycelium;
extern const us_scene_vt us_scene_murmuration;
extern const us_scene_vt us_scene_tide;
extern const us_scene_vt us_scene_pollen;
extern const us_scene_vt us_scene_coral;
extern const us_scene_vt us_scene_aurora;
extern const us_scene_vt us_scene_abyss;

const us_scene_vt *const us_scene_list[] = {
	&us_scene_meadow,
	&us_scene_wind,
	&us_scene_river,
	&us_scene_rain,
	&us_scene_storm,
	&us_scene_pond,
	&us_scene_fireflies,
	&us_scene_mycelium,
	&us_scene_murmuration,
	&us_scene_tide,
	&us_scene_pollen,
	&us_scene_coral,
	&us_scene_aurora,
	&us_scene_abyss,
};
const int us_scene_count = (int)(sizeof(us_scene_list) / sizeof(us_scene_list[0]));
