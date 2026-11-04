/* Understory scene: Pollen & Seeds — STUB, to be implemented (see docs/design.md + docs/scene-guide.md). */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>

typedef struct {
	us_rng rng;
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed);
	return s;
}

static void destroy(void *p) { free(p); }

static void frame(void *p, const us_ctx *ctx, us_canvas *c)
{
	(void)p;
	us_line(c, 0, 0.8f, ctx->aspect, 0.8f, 0.002f, 0.3f + 0.7f * ctx->a->level, US_MAT_EARTH);
}

const us_scene_vt us_scene_pollen = {"pollen", "Pollen & Seeds", 0.5f, create, destroy, frame};
