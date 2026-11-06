/* Understory scene: Pollen & Seeds — drifting grains, spores and dandelion parachutes in warm air.
 *
 * Three depth layers of airborne particles ride a curl-noise wind (divergence-free, so they swirl rather
 * than bunch up): far motes are tiny and slow, near dandelion seeds are large, slow-turning starbursts.
 *   highs / air   -> thermals: lift everything, near seeds rise and sparkle
 *   sub / bass    -> the slow vortices: curl strength and eddy size swell with the low end
 *   level         -> overall breeze
 *   onsets        -> seed heads along the bottom release new parachutes (and pollen puffs from flowers)
 * Near seeds are full 3-D dandelion parachutes (achene, beak, a turning umbrella of barbed bristles); seed clocks
 * are spheres of tiny parachutes. The wind itself is drawn: streamlines (stream-function contours) ring each eddy
 * in drifting patches. Slanted light shafts (warm at dusk, pale by day) catch the particles inside them.
 * Small living detail: a sycamore bough reaching in from the top-left (hanging flower tassels, paired seed keys that
 * whirl down by autorotation), thistle down, dust motes adrift in the shafts, a big dandelion clock close to us in
 * the lower right, a couple of bumblebees working the heads and hoverflies hanging in the air.
 * The air moves in currents: most grains and seeds ride a few meandering streams of breeze across the frame
 * (they are pulled back into their ribbon after every disturbance), the rest drift free on the eddies. Depth is
 * real: far motes are tiny pin-pricks, near seeds are large, soft and slightly out of focus. A distant row of
 * small clocks stands behind the meadow, and the floor is a textured band of tufts, leaf rosettes and soil.
 * The drums, each with its own natural cause (fast reactions are springs / eased envelopes, never on-off):
 *   kick          -> a puff of wind strikes one clock: a ring of its seeds is stripped and flies outward, a wispy
 *                    ring of breath expands from it, nearby air is pushed away, the ground thumps, the bough jolts
 *                    (every fourth kick it is the big foreground clock)
 *   snare         -> a sideways gust: another clock shatters in a spray downwind, a curl of wind spins the seed
 *                    stream above it, and grass and stems whip sideways
 *   hats          -> a fresh scatter of seed tips and dust motes glints in the light, and clock tips sparkle
 *   level         -> the light: louder = brighter, fuller air; quiet = a few seeds hanging dim and still
 *   beat / onset  -> (melodic music) clocks let the odd parachute go
 * More of the music:
 *   lowmid        -> the bumblebees' drone: flight speed, looping and wing blur
 *   mid bands     -> each hoverfly darts to a new station when its own band rises (melody)
 *   bands[]       -> each row head fluffs out with its own band (bass on the left, treble on the right); every
 *                    seed of the foreground clock trembles with its own band
 *   air           -> thistle down flares and is held aloft; motes brighten
 *   swell         -> the bough heaves, its tassels sway wider and shed pollen; more seeds take to the air
 *   centroid      -> sun height: brighter music stands the light shafts up
 *   width         -> a wide stereo image brings more of the wind's threads to the surface
 * World: reads bloom (flowers vs seed heads), pollen (how much is airborne), daylight, season, wind;
 *        writes pollen. In winter the air holds slow snow motes instead and the heads are bare.
 */
#include "../core/us_scene.h"
#include "../core/us_math.h"
#include <stdlib.h>
#include <string.h>

#define MAXP 1300
#define NEV 8     /* live wind events (kick puffs, snare gusts) */
#define NSTREAM 3 /* currents of breeze the seeds ride */
#define NBACK 18  /* distant clocks behind the meadow */
#define NTUFT 90  /* foreground grass on the floor */
#define NSTIP 260 /* soil stipple */
#define NHEADS 9
#define NBLADE 120
#define CGX 36
#define CGY 20
#define NBEAM 3
#define KX 52 /* streamline (contour) grid */
#define KY 29
#define NFLY 3   /* hoverflies */
#define NBEE 2   /* bumblebees */
#define NKEY 3   /* sycamore keys hanging from the bough */
#define NCAT 6   /* flower tassels hanging from the bough */
#define FGN 44   /* seeds on the big foreground clock */
#define NMOTE 22 /* dust motes per light shaft */

enum { K_GRAIN = 0, K_SPORE, K_SEED, K_SNOW, K_SAMARA, K_THISTLE };

typedef struct {
	float x, y, vx, vy; /* x stored as fraction of width */
	float z;            /* depth 0 far .. 1 near */
	float rot, vr;
	float life;
	float age, ttl; /* seconds alive, lifespan */
	float seedv;        /* per-particle variation */
	float kvx, kvy;     /* impulse from a puff / gust, decays fast */
	float off;          /* offset across its stream's ribbon */
	int kind, alive, stream; /* stream: -1 drifts free */
} part_t;

typedef struct {
	float x, y, age, str, r0, dir; /* x already * aspect */
	int kind, live;                /* kind 0 kick puff, 1 snare gust */
} ev_t;

typedef struct {
	float x;       /* fraction of width */
	float h;       /* stem height */
	float seeds;   /* 0..1 seeds left on the clock */
	float flower;  /* 1 = flower (bloom), 0 = seed clock */
	float ph;      /* sway phase */
	float hx, hy;  /* current head position (normalized, x already * aspect) */
	float top;     /* y of the top of the head (where a bee lands) */
	int petals;
} head_t;

typedef struct {
	float x, y, hx, hy; /* position, current hover station */
	float cool, dir, ph, idle, env; /* env: slow memory of its band, so only a fresh rise makes it dart */
	int band; /* the mid band this hoverfly answers */
} fly_t;

typedef struct {
	float x, y, vx, vy, visit, ph, dir;
	int tgt, state; /* state 0 flying, 1 on a head; tgt >= 0 a head, -1 an air waypoint, -2 choose next */
	float wx, wy;
} bee_t;

typedef struct {
	us_rng rng;
	int inited;
	part_t p[MAXP];
	head_t hd[NHEADS];
	float blade_x[NBLADE], blade_h[NBLADE], blade_ph[NBLADE];
	float psi[(CGX + 1) * (CGY + 1)], psi_t[(KX + 1) * (KY + 1)];
	float fvx[CGX * CGY], fvy[CGX * CGY];
	float bass_s, high_s, lvl_s, swirl_t, wind_t;
	float spawn_acc;
	float beam_x[NBEAM], beam_w[NBEAM], beam_ph[NBEAM];
	/* reactive envelopes */
	float bslow[US_NBANDS], bdev[US_NBANDS]; /* per-band slow mean + eased rise above it */
	float hat, lowmid_s, air_s, swell_s, slant, width_s, kick_prev;
	float thump, thump_v; /* ground spring (kicks) */
	float bough, bough_v; /* bough spring (kicks) */
	float key_on[NKEY];   /* 1 = key hanging, regrows from 0 */
	float fg_seeds, fg_ph;
	fly_t fly[NFLY];
	bee_t bee[NBEE];
	/* drums */
	ev_t ev[NEV];
	int ev_i, kick_n, snare_n, hat_n, last_kick_head;
	float flare[NHEADS], fg_flare, fg_hx, fg_hy, fg_on;
	float gust, gust_v, gust_dir; /* lateral spring (snares) */
	float lvl_f, lvl_slow; /* loudness: the light, and how full the air is */
	float since_drum;
	float kage, sage, hage, kstr, sstr; /* seconds since each drum, and its strength */
	int nseed_air, seed_lim; /* parachutes aloft, and how many the air may hold */
	/* currents + meadow */
	float sy0[NSTREAM], stream_t;
	float bk_x[NBACK], bk_h[NBACK], bk_r[NBACK];
	float tf_x[NTUFT], tf_y[NTUFT], tf_h[NTUFT], tf_ph[NTUFT];
	float sp_x[NSTIP], sp_y[NSTIP], sp_v[NSTIP];
} st_t;

static void *create(uint32_t seed)
{
	st_t *s = calloc(1, sizeof(*s));
	us_rng_seed(&s->rng, seed ^ 0x706f6c6cu);
	return s;
}

static void destroy(void *p) { free(p); }

static part_t *free_slot(st_t *st)
{
	for (int j = 0; j < MAXP; j++)
		if (!st->p[j].alive)
			return &st->p[j];
	return NULL;
}

static inline float fract1(float x) { return x - floorf(x); }
static inline float hash1(float x) { return fract1(sinf(x) * 43758.5453f); }

/* the sycamore bough reaching in from the top-left: point at u (0 at the frame edge .. 1 at its tip) */
static void bough_pt(const st_t *st, float t, float u, float *x, float *y)
{
	float heave = 0.004f * sinf(t * 0.35f) * (0.4f + st->swell_s) + 0.02f * st->bough;
	*x = -0.03f + 0.40f * u;
	*y = 0.045f + 0.07f * u * u + 0.006f * sinf(u * 7.0f + 1.0f) + heave * u * u;
}

static float ground_y(float xf) { return 0.93f + 0.015f * us_noise2(xf * 2.3f, 8.1f); }

/* centre line of current `k` at fraction of width xf: a slow meander that drifts downstream */
static float stream_y(const st_t *st, int k, float xf)
{
	return st->sy0[k] + 0.17f * us_noise2(xf * 1.3f - st->stream_t * 0.04f, k * 7.3f + 1.0f) +
	       0.045f * sinf(xf * 3.7f - st->stream_t * 0.08f + k * 2.0f);
}

static void spawn(st_t *st, part_t *q, float winter, int anywhere, float A)
{
	us_rng *r = &st->rng;
	(void)A;
	memset(q, 0, sizeof(*q));
	q->alive = 1;
	q->stream = -1;
	/* most of the air is far away: depth skews toward small, slow, dim */
	float zr = us_rand(r);
	q->z = zr * (0.35f + 0.65f * zr);
	float u = us_rand(r);
	if (us_rand(r) < winter)
		q->kind = K_SNOW;
	else if (u < 0.03f + 0.04f * q->z)
		q->kind = K_SEED;
	else if (u < 0.05f + 0.04f * q->z)
		q->kind = K_THISTLE;
	else if (u < 0.35f)
		q->kind = K_SPORE;
	else
		q->kind = K_GRAIN;
	q->x = us_rand(r);
	if (q->kind != K_SNOW && us_rand(r) < 0.72f) {
		/* most of the air rides one of the currents */
		q->stream = (int)(us_rand(r) * NSTREAM) % NSTREAM;
		q->off = us_randn(r) * (0.01f + 0.014f * q->z);
	}
	if (anywhere)
		q->y = q->stream >= 0 ? stream_y(st, q->stream, q->x) + q->off : us_randr(r, 0.02f, 0.9f);
	else if (q->kind == K_SNOW)
		q->y = -0.02f; /* snow sifts down from above */
	else {
		/* most join a current (drifting in from the windward edge, or fading in along it); the rest appear
		 * anywhere, enter from the edge or rise from below, so the free air fills evenly */
		float e = us_rand(r);
		if (q->stream >= 0) {
			q->x = us_rand(r) < 0.4f ? -0.02f : us_rand(r);
			q->y = stream_y(st, q->stream, q->x) + q->off;
		} else if (e < 0.6f) {
			q->y = us_randr(r, 0.04f, 0.86f);
		} else if (e < 0.8f) {
			q->x = -0.02f;
			q->y = us_randr(r, 0.05f, 0.88f);
		} else if (e < 0.92f) {
			q->y = 0.95f;
		} else {
			q->y = -0.02f;
		}
	}
	q->rot = us_rand(r) * US_TAU;
	q->vr = us_randn(r) * 0.25f;
	q->life = 0.0f;
	q->seedv = us_rand(r);
	q->ttl = us_randr(r, 25.0f, 60.0f);
}

/* a wind event slot: a free one, else the oldest */
static ev_t *new_ev(st_t *st)
{
	ev_t *e = &st->ev[st->ev_i];
	st->ev_i = (st->ev_i + 1) % NEV;
	memset(e, 0, sizeof(*e));
	e->live = 1;
	return e;
}

/* push every airborne thing near (cx, cy): kind 0 radially outward, kind 1 round in a curl (dir = sense) */
static void air_push(st_t *st, float A, float cx, float cy, float R, float str, int kind, float dir)
{
	for (int i = 0; i < MAXP; i++) {
		part_t *q = &st->p[i];
		if (!q->alive)
			continue;
		float dx = q->x * A - cx, dy = q->y - cy, d = sqrtf(dx * dx + dy * dy);
		if (d >= R || d < 1e-4f)
			continue;
		float ux = dx / d, uy = dy / d, f = 1.0f - d / R;
		float par = 0.4f + 0.6f * q->z;
		if (kind == 0) {
			f *= f;
			q->kvx += ux * str * f * par;
			q->kvy += uy * str * f * par;
		} else {
			float g = sinf(US_PI * d / R); /* still at the eye, strongest mid-way */
			q->kvx += (-uy * dir * g - ux * 0.25f * f) * str * par;
			q->kvy += (ux * dir * g - uy * 0.25f * f) * str * par;
		}
	}
}

/* launch one seed (or pollen grain) from a head at (cx, cy) along angle `an` */
static void launch(st_t *st, float A, float cx, float cy, float an, float rad, float spd, float z0, float z1, int kind)
{
	if (kind == K_SEED && st->nseed_air >= st->seed_lim)
		return;
	part_t *q = free_slot(st);
	if (!q)
		return;
	st->nseed_air += kind == K_SEED;
	spawn(st, q, 0, 1, A);
	q->kind = kind;
	q->x = (cx + cosf(an) * rad) / A;
	q->y = cy + sinf(an) * rad;
	q->z = us_randr(&st->rng, z0, z1);
	q->kvx = cosf(an) * spd;
	q->kvy = sinf(an) * spd;
	q->vx = 0.02f;
	q->vy = -0.01f;
	q->life = 1.0f;
	q->ttl = us_randr(&st->rng, 2.5f, 5.5f);
	q->rot = an + US_PI * 0.5f;
}

/* Ink bloom keeps each pixel's material, so a seed's glow spreading over stale wind-line pixels came out in
 * patches. Pollen's air is full of seeds, so every faint background / wind pixel simply takes the seeds' hue:
 * one branchless pass, cheaper than working out where the halos are. */
static void tint_air(us_canvas *c, uint8_t to, int ink_max, unsigned from_mask)
{
	size_t n = (size_t)c->w * c->h;
	const uint8_t *ink = c->ink;
	uint8_t *m = c->mat;
	for (size_t i = 0; i < n; i++) {
		unsigned mm = m[i], ok = (unsigned)(ink[i] <= ink_max) & (from_mask >> (mm & 31u));
		m[i] = (uint8_t)(ok & 1u ? to : mm);
	}
}

/* cheap anti-aliased hairline (Wu-style: two pixels per step along the major axis), brightness v0 -> v1 */
static void wline2(us_canvas *c, float ax, float ay, float bx, float by, float v0, float v1, uint8_t m)
{
	if (c->crisp) {
		us_line(c, ax, ay, bx, by, 0.002f, 0.5f * (v0 + v1), m);
		return;
	}
	float x0 = ax * c->s - 0.5f, y0 = ay * c->s - 0.5f, x1 = bx * c->s - 0.5f, y1 = by * c->s - 0.5f;
	float dx = x1 - x0, dy = y1 - y0;
	int steep = fabsf(dy) > fabsf(dx);
	float len = steep ? fabsf(dy) : fabsf(dx);
	int n = (int)len + 1;
	if (n > 4000)
		return;
	float sx = dx / n, sy = dy / n, dv = (v1 - v0) / n;
	for (int i = 0; i <= n; i++) {
		float x = x0 + sx * i, y = y0 + sy * i, v = v0 + dv * i;
		if (steep) {
			int ix = (int)floorf(x);
			float f = x - ix;
			int iy = (int)(y + 0.5f);
			us_put(c, ix, iy, v * (1 - f), m);
			us_put(c, ix + 1, iy, v * f, m);
		} else {
			int iy = (int)floorf(y);
			float f = y - iy;
			int ix = (int)(x + 0.5f);
			us_put(c, ix, iy, v * (1 - f), m);
			us_put(c, ix, iy + 1, v * f, m);
		}
	}
}
static inline void wline(us_canvas *c, float ax, float ay, float bx, float by, float v, uint8_t m)
{
	wline2(c, ax, ay, bx, by, v, v, m);
}

/* a tiny hairline circle as bilinear splats (much cheaper than us_ring for spores) */
static void hring(us_canvas *c, float x, float y, float r, float v, uint8_t m)
{
	float rp = r * c->s;
	int n = (int)(US_TAU * rp * 0.9f) + 6;
	if (n > 400)
		n = 400;
	float cx = x * c->s - 0.5f, cy = y * c->s - 0.5f;
	float ca = cosf(US_TAU / n), sa = sinf(US_TAU / n), dx = rp, dy = 0;
	for (int i = 0; i < n; i++) {
		float fx = cx + dx, fy = cy + dy;
		int ix = (int)floorf(fx), iy = (int)floorf(fy);
		float ax = fx - ix, ay = fy - iy;
		us_put(c, ix, iy, v * (1 - ax) * (1 - ay), m);
		us_put(c, ix + 1, iy, v * ax * (1 - ay), m);
		us_put(c, ix, iy + 1, v * (1 - ax) * ay, m);
		us_put(c, ix + 1, iy + 1, v * ax * ay, m);
		float ndx = dx * ca - dy * sa;
		dy = dx * sa + dy * ca;
		dx = ndx;
	}
}

/* a dandelion seed in flight, seen in 3-D: spindle-shaped achene below, a long thin beak, and the pappus —
 * an open umbrella of bristles around the beak's tip. `tilt` leans the whole parachute, `spin` turns the
 * bristle ring about its axis so the umbrella foreshortens and its near side brightens; near seeds show the
 * fine side-barbs on each bristle. */
static void pappus(us_canvas *c, int crisp, float x, float y, float sz, float tilt, float spin, float v, uint8_t mat,
		   int maxn)
{
	float axx = sinf(tilt), axy = -cosf(tilt); /* up along the beak */
	float pxx = cosf(tilt), pxy = sinf(tilt);  /* across */
	float cx = x + axx * sz * 1.05f, cy = y + axy * sz * 1.05f; /* crown */
	float bx = x - axx * sz * 0.3f, by = y - axy * sz * 0.3f;   /* achene base */
	if (crisp) {
		us_line(c, bx, by, cx, cy, 0.002f, v * 0.6f, mat);
		int n = sz > 0.016f ? 7 : 5;
		for (int k = 0; k < n; k++) {
			float ph = spin + US_TAU * k / n;
			float sp = sinf(1.0f) * cosf(ph);
			float el = 0.38f * sinf(1.0f) * sinf(ph);
			float ex = cx + (axx * (cosf(1.0f) + el) + pxx * sp) * sz, ey = cy + (axy * (cosf(1.0f) + el) + pxy * sp) * sz;
			us_line(c, cx, cy, ex, ey, 0.002f, v * (0.7f + 0.3f * sinf(ph)), mat);
		}
		return;
	}
	/* achene: a short brighter spindle, then the hair-fine beak */
	wline(c, bx, by, x, y, v * 0.9f, mat);
	us_disc(c, x - axx * sz * 0.15f, y - axy * sz * 0.15f, sz * 0.05f + 0.0006f, v * 0.6f, mat);
	wline2(c, x, y, cx, cy, v * 0.55f, v * 0.35f, mat);
	int n = sz > 0.02f ? 19 : (sz > 0.012f ? 14 : 9);
	if (n > maxn)
		n = maxn;
	int barbs = sz > 0.024f;
	const float cone = 1.0f; /* half-angle of the umbrella */
	float cc = cosf(cone), sc = sinf(cone);
	for (int k = 0; k < n; k++) {
		float ph = spin + US_TAU * (k + 0.37f * (k & 1)) / n;
		float lat = sc * cosf(ph), dep = sinf(ph);
		/* seen from a little below: the far rim of the ring rides higher than the near rim */
		float el = 0.38f * sc * dep;
		float dx = axx * (cc + el) + pxx * lat, dy = axy * (cc + el) + pxy * lat;
		float L = sz * (0.92f + 0.08f * sinf(k * 2.3f));
		/* bristles droop a touch at their tips */
		float mx = cx + dx * L * 0.6f, my = cy + dy * L * 0.6f;
		float ex = cx + dx * L - axx * L * 0.1f, ey = cy + dy * L - axy * L * 0.1f;
		float hv = v * (0.45f + 0.35f * (0.5f + 0.5f * dep));
		if (maxn < 12) /* tiny: one straight stroke */
			wline2(c, cx, cy, ex, ey, hv * 0.8f, hv * 0.8f, mat);
		else {
			wline2(c, cx, cy, mx, my, hv * 0.8f, hv, mat);
			wline2(c, mx, my, ex, ey, hv, hv * 0.7f, mat);
		}
		if (barbs && dep > -0.3f) {
			/* feathery side barbs near the tip */
			for (int q = 0; q < 1; q++) {
				float u = 0.72f;
				float qx = cx + dx * L * u, qy = cy + dy * L * u;
				float bl = L * 0.16f;
				wline(c, qx, qy, qx + (dx - dy * 0.9f) * bl, qy + (dy + dx * 0.9f) * bl, hv * 0.4f, mat);
				wline(c, qx, qy, qx + (dx + dy * 0.9f) * bl, qy + (dy - dx * 0.9f) * bl, hv * 0.4f, mat);
			}
		}
	}
}


/* a sycamore key whirling down: nutlet + one veined wing turning about the vertical (autorotation), so the wing
 * swings side to side and foreshortens as it turns edge-on */
static void samara(us_canvas *c, int crisp, float x, float y, float L, float rot, float v, uint8_t m)
{
	float cr = cosf(rot), sr = sinf(rot);
	float tx = x + cr * L, ty = y - L * (0.42f - 0.12f * sr);
	float ux = tx - x, uy = ty - y, ul = sqrtf(ux * ux + uy * uy) + 1e-6f;
	float nx = -uy / ul, ny = ux / ul;
	float chord = L * (0.07f + 0.26f * fabsf(sr)); /* broad face-on, a sliver edge-on */
	float bx = x + ux * 0.62f + nx * chord, by = y + uy * 0.62f + ny * chord;
	if (crisp) {
		us_disc(c, x, y, 0.004f, v, m);
		us_line(c, x, y, tx, ty, 0.003f, v * 0.85f, m);
		if (chord > L * 0.18f)
			us_line(c, x, y, bx, by, 0.003f, v * 0.6f, m);
		return;
	}
	us_disc(c, x, y, L * 0.11f + 0.0006f, v, m);
	wline2(c, x, y, tx, ty, v, v * 0.7f, m);          /* thick leading edge */
	wline2(c, x, y, bx, by, v * 0.5f, v * 0.45f, m);  /* trailing edge bellies out ... */
	wline2(c, bx, by, tx, ty, v * 0.45f, v * 0.55f, m); /* ... and sweeps back to the tip */
	for (int k = 1; k <= 3; k++) {                    /* fine veins fanning from the nut */
		float f = k / 4.0f;
		float ex = us_lerp(us_lerp(x, bx, 0.8f), tx, f), ey = us_lerp(us_lerp(y, by, 0.8f), ty, f);
		wline(c, x + ux * 0.08f, y + uy * 0.08f, ex, ey, v * 0.22f, m);
	}
}

/* thistle down: a seed hanging under a wide, soft tuft of feathery silk, rocking as it drifts.
 * `open` (the air band) spreads the tuft wider and lifts the silk. */
static void thistle(us_canvas *c, int crisp, float x, float y, float sz, float rock, float open, float v, uint8_t m)
{
	int n = crisp ? 6 : (sz > 0.02f ? 17 : 11);
	float spread = 1.15f + 0.45f * open; /* half-angle of the tuft about straight up */
	float sx = x - sinf(rock) * sz * 0.35f, sy = y + cosf(rock) * sz * 0.35f; /* the seed, hanging below */
	if (crisp) {
		us_point(c, sx, sy, 0.003f, v, m);
		for (int k = 0; k < n; k++) {
			float an = -US_PI * 0.5f + rock + spread * (2.0f * k / (n - 1) - 1.0f);
			us_line(c, x, y, x + cosf(an) * sz * 0.8f, y + sinf(an) * sz * 0.7f, 0.002f, v * 0.45f, m);
		}
		return;
	}
	us_disc(c, sx, sy, 0.0008f + sz * 0.035f, v * 0.9f, m);
	wline2(c, sx, sy, x, y, v * 0.6f, v * 0.4f, m);
	int plume = sz > 0.018f;
	for (int k = 0; k < n; k++) {
		float f = 2.0f * (k + 0.5f) / n - 1.0f; /* -1 .. 1 across the tuft */
		float an = -US_PI * 0.5f + rock + spread * f + 0.12f * sinf(k * 2.9f);
		float L = sz * (0.75f + 0.25f * sinf(k * 3.1f + 1.0f)) * (1.0f - 0.2f * f * f);
		float ca = cosf(an), sa = sinf(an);
		/* each silk bows outward and its tip droops */
		float mx = x + ca * L * 0.55f, my = y + sa * L * 0.55f;
		float ex = x + ca * L, ey = y + sa * L + L * 0.18f * fabsf(f);
		float hv = v * (0.32f + 0.2f * open) * (0.75f + 0.25f * (1.0f - fabsf(f)));
		wline2(c, x, y, mx, my, hv, hv * 0.85f, m);
		wline2(c, mx, my, ex, ey, hv * 0.85f, hv * 0.2f, m);
		if (plume && (k & 1)) { /* feathery: a pair of fine side hairs on every other silk */
			float bl = L * 0.18f;
			wline2(c, mx, my, mx + (ca - sa) * bl, my + (sa + ca) * bl, hv * 0.4f, 0.0f, m);
			wline2(c, mx, my, mx + (ca + sa) * bl, my + (sa - ca) * bl, hv * 0.4f, 0.0f, m);
		}
	}
}

/* an insect seen side-on. kind 0 = hoverfly (slim, striped), 1 = bumblebee (round, furry).
 * wing 0..1 = how hard the wings are going (drawn as a translucent beat-blur). */
static void insect(us_canvas *c, int crisp, int kind, float x, float y, float dir, float sc, float wing, float wph, float v)
{
	float L = (kind ? 0.024f : 0.021f) * sc;
	if (crisp)
		L *= 1.7f;
	float hx = x + dir * L * 0.5f, hy = y - L * 0.06f;   /* head */
	float tx = x - dir * L * 0.55f, ty = y + L * 0.12f;  /* tail */
	if (crisp) {
		/* body, head, and a wing pixel or two that flicker with the beat */
		us_line(c, tx, ty, hx, hy, kind ? 0.006f : 0.004f, v * 0.85f, US_MAT_LIGHT);
		us_point(c, hx, hy, 0.003f, v, US_MAT_EARTH);
		float wa = 0.6f + 0.5f * sinf(wph);
		us_line(c, x, y - L * 0.15f, x - dir * L * 0.35f, y - L * (0.6f + 0.35f * wa * wing), 0.003f,
			v * (0.35f + 0.3f * wing), US_MAT_ACCENT);
		return;
	}
	/* wing blur: a faint fan swept between the up- and down-stroke */
	float wl = L * (kind ? 0.75f : 0.9f);
	float sweep = 0.3f + 0.7f * wing; /* harder beating = wider blur */
	float bx = x + dir * L * 0.08f, by = y - L * 0.18f;
	float ux = bx - dir * wl * 0.45f, uy = by - wl * 0.8f;                    /* top of the stroke */
	float lx = bx - dir * wl * 0.95f, ly = by - wl * (0.55f - 0.45f * sweep); /* bottom of the stroke */
	us_tri(c, bx, by, ux, uy, lx, ly, v * (0.05f + 0.07f * wing), US_MAT_ACCENT);
	float f = 0.5f + 0.5f * sinf(wph); /* the wing itself, caught somewhere in its stroke */
	wline2(c, bx, by, us_lerp(ux, lx, f), us_lerp(uy, ly, f), v * 0.4f, v * 0.15f, US_MAT_ACCENT);
	if (kind) {
		/* bumblebee: round brown body, yellow collar and tail band, a dark head */
		us_disc(c, x - dir * L * 0.22f, y + L * 0.06f, L * 0.34f, v * 0.42f, US_MAT_EARTH);
		us_disc(c, x + dir * L * 0.14f, y, L * 0.28f, v * 0.4f, US_MAT_EARTH);
		us_arc(c, x + dir * L * 0.14f, y, L * 0.2f, -2.6f, -0.5f, L * 0.12f, v * 0.6f, US_MAT_LIGHT);
		float sx = x - dir * L * 0.3f;
		wline(c, sx, y - L * 0.22f, sx - dir * L * 0.04f, y + L * 0.36f, v * 0.6f, US_MAT_LIGHT);
		wline(c, sx - dir * L * 0.03f, y - L * 0.2f, sx - dir * L * 0.06f, y + L * 0.34f, v * 0.48f, US_MAT_LIGHT);
		us_disc(c, hx, hy, L * 0.15f, v * 0.45f, US_MAT_EARTH);
		/* legs dangling a little */
		wline(c, x, y + L * 0.25f, x - dir * L * 0.1f, y + L * 0.6f, v * 0.4f, US_MAT_EARTH);
	} else {
		/* hoverfly: slim dark body, yellow wasp bands, big eye */
		us_line(c, tx, ty, hx, hy, L * 0.17f, v * 0.42f, US_MAT_EARTH);
		for (int k = 0; k < 3; k++) {
			float g = 0.12f + 0.15f * k;
			float sx = us_lerp(tx, hx, g), sy = us_lerp(ty, hy, g);
			wline(c, sx, sy - L * 0.1f, sx, sy + L * 0.1f, v * 0.6f, US_MAT_LIGHT);
		}
		us_disc(c, hx, hy, L * 0.13f, v * 0.55f, US_MAT_EARTH);
	}
}

static void frame(void *sp, const us_ctx *ctx, us_canvas *c)
{
	st_t *st = sp;
	const us_audio *a = ctx->a;
	us_world *w = ctx->w;
	float dt = ctx->dt, A = ctx->aspect, rdt = ctx->real_dt;
	float dens = ctx->p->density;
	float t = (float)ctx->t;
	float winter = us_world_winter(w), autumn = us_world_season_w(w, 2);
	float day = us_smooth(0.25f, 0.75f, w->daylight);
	if (dt > 0.05f)
		dt = 0.05f;

	if (!st->inited) {
		us_rng *r = &st->rng;
		for (int i = 0; i < NHEADS; i++) {
			head_t *h = &st->hd[i];
			h->x = (i + 0.5f) / NHEADS + us_randr(r, -0.35f, 0.35f) / NHEADS;
			h->h = us_randr(r, 0.10f, 0.30f);
			h->seeds = us_randr(r, 0.4f, 1.0f);
			h->flower = us_rand(r) < w->bloom ? 1.0f : 0.0f;
			h->ph = us_rand(r) * 30.0f;
			h->petals = 11 + (int)(us_rand(r) * 8);
		}
		for (int b = 0; b < NBLADE; b++) {
			st->blade_x[b] = us_rand(r);
			st->blade_h[b] = us_randr(r, 0.015f, 0.06f);
			st->blade_ph[b] = us_rand(r) * 20.0f;
		}
		for (int k = 0; k < NBEAM; k++) {
			st->beam_x[k] = us_randr(r, 0.1f, 1.1f);
			st->beam_w[k] = us_randr(r, 0.04f, 0.11f);
			st->beam_ph[k] = us_rand(r) * 10.0f;
		}
		st->slant = 0.42f;
		st->fg_seeds = us_randr(r, 0.75f, 1.0f);
		st->fg_ph = us_rand(r) * 10.0f;
		for (int k = 0; k < NKEY; k++)
			st->key_on[k] = us_randr(r, 0.6f, 1.0f);
		for (int f = 0; f < NFLY; f++) {
			st->fly[f].hx = st->fly[f].x = us_randr(r, 0.15f, 0.85f) * A;
			st->fly[f].hy = st->fly[f].y = us_randr(r, 0.32f, 0.72f);
			st->fly[f].dir = us_rand(r) < 0.5f ? -1.0f : 1.0f;
			st->fly[f].ph = us_rand(r) * 20.0f;
			st->fly[f].idle = us_randr(r, 2.0f, 8.0f);
			st->fly[f].band = 15 + 3 * f; /* ~1–2.5 kHz: the melody's register */
		}
		for (int b = 0; b < NBEE; b++) {
			st->bee[b].x = us_randr(r, 0.2f, 0.8f) * A;
			st->bee[b].y = us_randr(r, 0.4f, 0.7f);
			st->bee[b].tgt = (int)(us_rand(r) * NHEADS);
			st->bee[b].ph = us_rand(r) * 20.0f;
			st->bee[b].dir = 1;
		}
		for (int k = 0; k < NSTREAM; k++)
			st->sy0[k] = 0.19f + 0.17f * k + us_randr(r, -0.03f, 0.03f);
		st->stream_t = us_rand(r) * 50.0f;
		for (int k = 0; k < NBACK; k++) {
			st->bk_x[k] = (k + us_rand(r)) / NBACK;
			st->bk_h[k] = us_randr(r, 0.025f, 0.075f);
			st->bk_r[k] = us_randr(r, 0.005f, 0.009f);
		}
		for (int k = 0; k < NTUFT; k++) {
			st->tf_x[k] = us_rand(r);
			st->tf_y[k] = us_randr(r, 0.0f, 1.0f);
			st->tf_h[k] = us_randr(r, 0.02f, 0.075f);
			st->tf_ph[k] = us_rand(r) * 20.0f;
		}
		for (int k = 0; k < NSTIP; k++) {
			st->sp_x[k] = us_rand(r);
			st->sp_y[k] = us_rand(r);
			st->sp_v[k] = us_randr(r, 0.06f, 0.2f);
		}
		st->last_kick_head = -1;
		st->kage = st->sage = st->hage = 9.0f;
		st->fg_on = 0;
		/* airborne amount at open remembers the world's pollen */
		float nmax = 250.0f + 700.0f * dens;
		int n0 = (int)(nmax * us_sat(0.4f + 0.8f * w->pollen + 0.2f * w->bloom));
		for (int i = 0; i < n0 && i < MAXP; i++) {
			spawn(st, &st->p[i], winter, 1, A);
			st->p[i].life = 1.0f;
		}
		st->inited = 1;
	}

	/* ---- audio shaping ---- */
	st->bass_s = us_approach(st->bass_s, us_sat(0.7f * a->sub + 0.5f * a->bass + 0.3f * a->kick), 0.6f, rdt);
	st->high_s = us_approach(st->high_s, us_sat(0.8f * a->high + 0.6f * a->air), 0.35f, rdt);
	st->lvl_s = us_approach(st->lvl_s, a->level, 1.0f, rdt);
	float wind = us_sat(0.25f * w->wind + 0.5f * st->lvl_s);
	st->wind_t += dt * (0.2f + wind);
	st->swirl_t += dt * (0.05f + 0.12f * st->bass_s);

	/* each band's rise above its own slow mean, eased (attack ~50 ms, release ~0.35 s): the clocks, the
	 * hoverflies, the big foreground clock and the dust glints each listen to their own slice of the spectrum */
	for (int b = 0; b < US_NBANDS; b++) {
		float v = a->bands[b];
		st->bslow[b] = us_approach(st->bslow[b], v, 0.6f, rdt);
		float d = us_sat((v - st->bslow[b]) * 3.0f);
		st->bdev[b] = us_approach(st->bdev[b], d, d > st->bdev[b] ? 0.05f : 0.35f, rdt);
	}
	{
		float h = 0; /* hats / shakers: the top bands' flux */
		for (int b = 23; b <= 28; b++)
			h += st->bdev[b];
		h = us_sat(h / 6.0f * 1.6f);
		st->hat = us_approach(st->hat, h, h > st->hat ? 0.03f : 0.22f, rdt);
	}
	st->lowmid_s = us_approach(st->lowmid_s, a->lowmid, 0.5f, rdt);
	st->air_s = us_approach(st->air_s, a->air, 0.35f, rdt);
	st->swell_s = us_approach(st->swell_s, a->swell, 1.0f, rdt);
	st->width_s = us_approach(st->width_s, a->width, 1.5f, rdt);
	/* brightness of the music = height of the sun: brighter mixes stand the light shafts up */
	st->slant = us_approach(st->slant, 0.52f - 0.3f * a->centroid, 4.0f, rdt);
	/* loudness is the light (quick) and how full the air is (slow) */
	st->lvl_f = us_approach(st->lvl_f, a->level, 0.5f, rdt);
	st->lvl_slow = us_approach(st->lvl_slow, a->level, 2.5f, rdt);
	float glow = 0.4f + 0.85f * st->lvl_f;
	st->stream_t += dt * (0.4f + wind);
	st->since_drum += rdt;
	int kick_hit = a->kick_hit && winter < 0.8f;
	st->nseed_air = 0;
	for (int i = 0; i < MAXP; i++)
		st->nseed_air += st->p[i].alive && st->p[i].kind == K_SEED;
	st->seed_lim = 45 + (int)(80.0f * dens);
	if (a->kick_hit) {
		st->thump_v += 17.0f * a->kick;
		st->bough_v += 10.0f * a->kick;
	}
	st->kick_prev = a->kick;
	{
		float sdt = rdt > 0.05f ? 0.05f : rdt;
		const float wt = US_TAU * 2.8f, wb = US_TAU * 1.6f, wg = US_TAU * 1.3f;
		st->thump_v += (-wt * wt * st->thump - 2.0f * 0.3f * wt * st->thump_v) * sdt;
		st->thump += st->thump_v * sdt;
		st->bough_v += (-wb * wb * st->bough - 2.0f * 0.22f * wb * st->bough_v) * sdt;
		st->bough += st->bough_v * sdt;
		st->gust_v += (-wg * wg * st->gust - 2.0f * 0.28f * wg * st->gust_v) * sdt;
		st->gust += st->gust_v * sdt;
		st->gust = us_clampf(st->gust, -1.5f, 1.5f);
		for (int i = 0; i < NHEADS; i++)
			st->flare[i] = us_sat(st->flare[i] - rdt / 0.1f);
		st->fg_flare = us_sat(st->fg_flare - rdt / 0.11f);
	}
	if (a->kick_hit || a->snare_hit)
		st->since_drum = 0;
	if (a->hat_hit)
		st->hat_n++;
	if (a->kick_hit)
		st->kage = 0, st->kstr = 0.55f + 0.45f * us_sat(a->kick);
	if (a->snare_hit)
		st->sage = 0, st->sstr = 0.6f + 0.4f * us_sat(a->snare);
	if (a->hat_hit)
		st->hage = 0;
	/* each drum answers with a quick, clean gesture that is over in about a tenth of a second */
	float kp = st->kstr * us_sat(1.0f - st->kage / 0.085f), snp = st->sstr * us_sat(1.0f - st->sage / 0.11f);
	float hat = us_sat(1.0f - st->hage / 0.09f);

	/* ---- kick: a puff of wind strikes one clock and strips a ring of its seeds ---- */
	if (kick_hit && st->inited > 1) {
		float str = 0.55f + 0.45f * us_sat(a->kick);
		st->kick_n++;
		int fg = (st->kick_n % 4 == 0) && st->fg_on > 0.5f && st->fg_seeds > 0.3f;
		int best = -1;
		if (!fg) {
			float bs = -1;
			for (int k = 0; k < 4; k++) {
				int i = (int)(us_rand(&st->rng) * NHEADS) % NHEADS;
				head_t *h = &st->hd[i];
				float sc = (h->flower > 0.5f ? 0.3f : h->seeds) + 0.2f * us_rand(&st->rng);
				if (i != st->last_kick_head && h->top != 0 && sc > bs)
					bs = sc, best = i;
			}
			if (best < 0)
				fg = st->fg_on > 0.5f;
		}
		float cx, cy, cr;
		int nb, kind = K_SEED;
		if (fg) {
			cx = st->fg_hx, cy = st->fg_hy, cr = 0.1f;
			nb = 6 + (int)(4.0f * a->kick);
			st->fg_flare = 1.0f;
			st->fg_seeds = us_sat(st->fg_seeds - nb * 0.6f / FGN);
		} else if (best >= 0) {
			head_t *h = &st->hd[best];
			cx = h->hx, cy = h->hy, cr = 0.026f + 0.022f * (h->h / 0.3f);
			nb = 4 + (int)(4.0f * a->kick);
			if (h->flower > 0.5f)
				kind = K_GRAIN, nb += 4;
			else {
				int have = (int)(h->seeds * 34.0f);
				nb = nb < have ? nb : have;
				h->seeds = us_sat(h->seeds - nb / 34.0f);
			}
			st->flare[best] = 1.0f;
			st->last_kick_head = best;
		} else
			cx = cy = cr = 0, nb = -1;
		if (nb >= 0) {
			ev_t *e = new_ev(st);
			e->kind = 0, e->x = cx, e->y = cy, e->str = str, e->r0 = cr;
			float a0 = us_rand(&st->rng) * US_TAU;
			for (int k = 0; k < nb; k++) {
				float an = a0 + US_TAU * (k + 0.3f * us_rand(&st->rng)) / (nb > 0 ? nb : 1);
				float spd = str * us_randr(&st->rng, 0.5f, 0.8f) * (fg ? 1.4f : 1.0f);
				launch(st, A, cx, cy, an, cr * 0.8f, spd, fg ? 0.82f : 0.4f, fg ? 1.0f : 0.8f, kind);
			}
			air_push(st, A, cx, cy, fg ? 0.5f : 0.36f, 0.6f * str, 0, 0);
		}
	}

	/* ---- snare: a sideways gust; another clock shatters downwind and the stream above it curls ---- */
	if (a->snare_hit && winter < 0.8f && st->inited > 1) {
		float str = 0.6f + 0.4f * us_sat(a->snare);
		st->snare_n++;
		float dir = (st->snare_n & 1) ? 1.0f : -1.0f;
		int best = -1;
		float bs = -1;
		for (int i = 0; i < NHEADS; i++) {
			head_t *h = &st->hd[i];
			float sc = (h->flower > 0.5f ? 0.2f : h->seeds) + 0.35f * us_rand(&st->rng);
			if (i != st->last_kick_head && h->top != 0 && sc > bs)
				bs = sc, best = i;
		}
		st->gust_v += dir * 4.0f * str;
		st->gust_dir = dir;
		if (best >= 0) {
			head_t *h = &st->hd[best];
			float cr = 0.026f + 0.022f * (h->h / 0.3f);
			int kind = h->flower > 0.5f ? K_GRAIN : K_SEED;
			int nb = kind == K_GRAIN ? 10 : (int)(h->seeds * 34.0f * 0.7f);
			if (nb > 10)
				nb = 10;
			if (kind == K_SEED)
				h->seeds = us_sat(h->seeds - nb / 34.0f);
			float base = dir > 0 ? -0.5f : -US_PI + 0.5f; /* downwind and up */
			for (int k = 0; k < nb; k++)
				launch(st, A, h->hx, h->hy, base + us_randn(&st->rng) * 0.45f, cr * 0.7f,
				       str * us_randr(&st->rng, 0.5f, 0.9f), 0.35f, 0.85f, kind);
			st->flare[best] = 0.8f;
			ev_t *e = new_ev(st);
			e->kind = 1, e->dir = dir, e->str = str, e->r0 = 0.2f;
			e->x = us_clampf(h->hx + dir * 0.12f, 0.1f, A - 0.1f);
			e->y = us_clampf(h->hy - 0.17f, 0.15f, 0.7f);
			air_push(st, A, e->x, e->y, 0.32f, 0.8f * str, 1, dir);
		}
	}

	/* ---- world ---- */
	us_world_add(w, &w->pollen, 0.004f * (0.2f + st->high_s + a->onset * 0.5f) * (0.3f + w->bloom) * (1.0f - winter), dt);

	/* ---- curl-noise wind on a coarse grid ---- */
	float eddy = 1.6f + 1.4f * (1.0f - st->bass_s); /* big slow eddies when the low end swells */
	float swirl = 0.010f + 0.035f * st->bass_s;
	for (int j = 0; j <= CGY; j++)
		for (int i = 0; i <= CGX; i++) {
			float x = (float)i / CGX * A, y = (float)j / CGY;
			st->psi[j * (CGX + 1) + i] = us_noise3(x * eddy, y * eddy, st->swirl_t) +
						     0.4f * us_noise3(x * eddy * 2.3f, y * eddy * 2.3f, st->swirl_t * 1.7f + 5.0f);
		}
	{
		float hx = A / CGX, hy = 1.0f / CGY;
		for (int j = 0; j < CGY; j++)
			for (int i = 0; i < CGX; i++) {
				float p00 = st->psi[j * (CGX + 1) + i], p10 = st->psi[j * (CGX + 1) + i + 1];
				float p01 = st->psi[(j + 1) * (CGX + 1) + i], p11 = st->psi[(j + 1) * (CGX + 1) + i + 1];
				float dpdy = ((p01 + p11) - (p00 + p10)) * 0.5f / hy;
				float dpdx = ((p10 + p11) - (p00 + p01)) * 0.5f / hx;
				st->fvx[j * CGX + i] = dpdy * swirl / eddy;
				st->fvy[j * CGX + i] = -dpdx * swirl / eddy;
			}
	}

	/* ---- the wind made visible: streamlines are contours of the stream function (curl + breeze + lift),
	 * so closed loops ring each eddy; they surface in drifting patches like threads of warm air ---- */
	{
		float breeze0 = 0.008f + 0.025f * wind, lift0 = 0.004f + 0.03f * st->high_s;
		float k_sw = swirl / eddy;
		float lo = 1e9f, hi = -1e9f;
		int gx = ctx->crisp ? KX / 2 : KX, gy = ctx->crisp ? KY / 2 : KY;
		for (int j = 0; j <= gy; j++)
			for (int i = 0; i <= gx; i++) {
				float x = (float)i / gx * A, y = (float)j / gy;
				/* the drawn streamlines carry a softer second octave than the particle wind, so they read as
				 * long flowing threads rather than knots */
				float ps = us_noise3(x * eddy, y * eddy, st->swirl_t) +
					   0.22f * us_noise3(x * eddy * 2.3f, y * eddy * 2.3f, st->swirl_t * 1.7f + 5.0f);
				float P = ps * k_sw + breeze0 * y * 0.6f + lift0 * x * 0.5f;
				st->psi_t[j * (gx + 1) + i] = P;
				lo = P < lo ? P : lo;
				hi = P > hi ? P : hi;
			}
		int nlev = ctx->crisp ? 7 : 12;
		float step = (hi - lo) / (nlev + 1);
		/* mean slope of the field: contours where the slope dies away are the small closed loops around an
		 * eddy's eye; they fade out so only the long sweeping lines remain */
		float gmean = 1e-6f;
		for (int j = 0; j < gy; j++)
			for (int i = 0; i < gx; i++) {
				float p00 = st->psi_t[j * (gx + 1) + i], p11 = st->psi_t[(j + 1) * (gx + 1) + i + 1];
				float p10 = st->psi_t[j * (gx + 1) + i + 1], p01 = st->psi_t[(j + 1) * (gx + 1) + i];
				gmean += fabsf(p10 + p11 - p00 - p01) + fabsf(p01 + p11 - p00 - p10);
			}
		gmean /= (float)(gx * gy);
		float sv = (ctx->crisp ? 0.2f : 0.22f) * (0.55f + 0.6f * st->bass_s) * (1.0f - 0.5f * winter);
		float hx = A / gx, hy = 1.0f / gy;
		/* a wide stereo image opens the air: more of the wind's threads surface at once */
		float wopen = 0.08f - 0.4f * st->width_s;
		for (int j = 0; j < gy; j++)
			for (int i = 0; i < gx; i++) {
				float x0 = i * hx, y0 = j * hy;
				/* only patches of the field show at a time, and never right down in the grass */
				float patch = us_smooth(wopen, 0.45f, us_noise3(x0 * 1.4f, y0 * 1.6f, t * 0.04f)) *
					      (1.0f - us_smooth(0.72f, 0.9f, y0));
				if (patch < 0.02f)
					continue;
				float p00 = st->psi_t[j * (gx + 1) + i], p10 = st->psi_t[j * (gx + 1) + i + 1];
				float p01 = st->psi_t[(j + 1) * (gx + 1) + i], p11 = st->psi_t[(j + 1) * (gx + 1) + i + 1];
				float cmin = fminf(fminf(p00, p10), fminf(p01, p11)), cmax = fmaxf(fmaxf(p00, p10), fmaxf(p01, p11));
				float slope = (fabsf(p10 + p11 - p00 - p01) + fabsf(p01 + p11 - p00 - p10)) / gmean;
				float pv = sv * patch * us_smooth(0.55f, 1.2f, slope);
				if (pv < 0.008f)
					continue;
				int l0 = (int)ceilf((cmin - lo) / step), l1 = (int)floorf((cmax - lo) / step);
				for (int l = l0 < 1 ? 1 : l0; l <= l1 && l <= nlev; l++) {
					float L = lo + l * step;
					/* marching squares: edge crossings (bottom, right, top, left) */
					float ex[4], ey[4];
					int ne = 0;
					if ((p00 < L) != (p10 < L))
						ex[ne] = x0 + hx * (L - p00) / (p10 - p00), ey[ne++] = y0;
					if ((p10 < L) != (p11 < L))
						ex[ne] = x0 + hx, ey[ne++] = y0 + hy * (L - p10) / (p11 - p10);
					if ((p01 < L) != (p11 < L))
						ex[ne] = x0 + hx * (L - p01) / (p11 - p01), ey[ne++] = y0 + hy;
					if ((p00 < L) != (p01 < L))
						ex[ne] = x0, ey[ne++] = y0 + hy * (L - p00) / (p01 - p00);
					for (int e = 0; e + 1 < ne; e += 2)
						wline(c, ex[e], ey[e], ex[e + 1], ey[e + 1], pv, US_MAT_WATER);
				}
			}
	}

	/* ---- light shafts: slanted bundles of hairlines, warm at dusk, pale by day ---- */
	const float bdx = st->slant; /* beam slant: x shift per unit of y (spectral centroid = sun height) */
	float beam_v[NBEAM];
	for (int k = 0; k < NBEAM; k++) {
		float flick = 0.5f + 0.5f * us_noise2(t * 0.07f, st->beam_ph[k]);
		beam_v[k] = (0.35f + 0.65f * day) * (1.0f - winter * 0.5f) * us_smooth(0.15f, 0.75f, flick) * (0.6f + 0.4f * st->high_s) * (0.45f + 0.7f * st->lvl_f);
		st->beam_x[k] += dt * 0.004f;
		if (st->beam_x[k] > 1.3f)
			st->beam_x[k] -= 1.5f;
		if (beam_v[k] < 0.02f)
			continue;
		float x0 = st->beam_x[k] * A, wdt = st->beam_w[k];
		int nl = ctx->crisp ? 4 : 14;
		for (int l = 0; l < nl; l++) {
			/* uneven strand spacing, and dust drifting through each strand, so the shaft reads as light in air */
			float jit = 0.35f * sinf(l * 12.9f + k * 3.1f) / nl;
			float off = ((l + 0.5f) / nl - 0.5f + jit) * wdt;
			float e = fabsf(off) / (wdt * 0.5f + 1e-4f);
			float edge = us_sat(1.0f - e * e * 0.85f);
			float dust = 0.5f + 0.5f * us_noise2(t * 0.15f + l * 1.7f, k * 5.3f + 2.0f);
			float yb = 0.88f - 0.12f * us_noise2(l * 0.7f, st->beam_ph[k]); /* strands fade out at uneven depths */
			if (ctx->crisp) {
				/* pixel looks: a dithered strand that fades as it falls, never a hard wire */
				float top = us_sat(1.15f * beam_v[k] * edge * (0.6f + 0.4f * dust));
				int py0 = (int)(-0.02f * c->s), py1 = (int)(yb * c->s);
				for (int py = py0 < 0 ? 0 : py0; py < py1; py++) {
					float yy = (py + 0.5f) / c->s, u = (yy + 0.02f) / (yb + 0.02f);
					float vv = top * (1.0f - u) * (1.0f - u);
					int px = (int)((x0 + off + bdx * (yy + 0.02f)) * c->s);
					if (vv < 0.1f || (vv < 0.45f && ((px + py) & 1)))
						continue;
					us_put(c, px, py, vv, US_MAT_BG);
				}
			} else
				wline2(c, x0 + off, -0.02f, x0 + off + bdx * (yb + 0.02f), yb,
				       0.36f * beam_v[k] * edge * (0.35f + 0.65f * dust), 0.02f * beam_v[k], US_MAT_LIGHT);
		}
		/* dust motes adrift in the shaft: they turn slowly in the light, the air band brightens them all,
		 * and every hat makes a fresh half of them glitter */
		int nm = ctx->crisp ? NMOTE / 3 : NMOTE;
		float mb = beam_v[k] * (0.6f + 0.6f * st->air_s), mh = fmaxf(hat, 0.4f * st->hat);
		for (int m = 0; m < nm; m++) {
			float h1 = hash1(m * 12.9898f + k * 78.233f), h2 = hash1(m * 39.346f + k * 11.135f + 3.0f);
			float h3 = hash1(m * 73.156f + k * 52.235f + 7.0f);
			float u = fract1(h1 + t * (0.003f + 0.008f * h2));
			float yy = -0.02f + u * 0.86f;
			float off = (h3 - 0.5f) * wdt * 0.85f + 0.008f * us_noise2(t * 0.15f + m * 3.1f, k * 7.7f);
			float xx = x0 + off + bdx * (yy + 0.02f);
			float e = fabsf(off) / (wdt * 0.5f + 1e-4f);
			float tw = 0.5f + 0.5f * sinf(t * (0.8f + 1.6f * h2) + h1 * 40.0f);
			float sel = hash1(h1 * 91.7f + st->hat_n * 3.17f) < 0.5f ? 1.0f : 0.0f; /* a fresh half on each hit */
			float fade = us_sat(u * 8.0f) * us_sat((1.0f - u) * 4.0f);
			float vv = mb * us_sat(1.0f - e * e) * fade * (0.18f + 0.3f * tw * tw + 1.4f * mh * sel);
			if (vv < 0.03f || (ctx->crisp && vv < 0.12f))
				continue;
			us_point(c, xx, yy, ctx->crisp ? 0.003f : 0.0011f, us_sat(vv), US_MAT_LIGHT);
			if (!ctx->crisp && sel > 0 && mh * vv > 0.12f) {
				float gl = 0.0035f * us_sat(mh * 1.5f); /* a tiny four-point glint */
				float gv = us_sat(vv * 0.6f);
				wline2(c, xx, yy, xx - gl, yy, gv, 0.0f, US_MAT_LIGHT);
				wline2(c, xx, yy, xx + gl, yy, gv, 0.0f, US_MAT_LIGHT);
				wline2(c, xx, yy, xx, yy - gl, gv, 0.0f, US_MAT_LIGHT);
				wline2(c, xx, yy, xx, yy + gl, gv, 0.0f, US_MAT_LIGHT);
			}
		}
	}

	/* ---- a sycamore bough reaching in from the top-left: hanging flower tassels that sway wider and shed
	 * pollen as the phrase swells, and seed keys that a kick can shake loose to whirl down ---- */
	{
		const int NS = 12;
		float bx[13], by[13];
		for (int i = 0; i <= NS; i++)
			bough_pt(st, t, (float)i / NS, &bx[i], &by[i]);
		for (int i = 0; i < NS; i++) {
			float u = (float)i / NS;
			float wd = ctx->crisp ? (u < 0.5f ? 0.008f : 0.005f) : 0.007f - 0.0052f * u;
			us_line(c, bx[i], by[i], bx[i + 1], by[i + 1], wd, 0.42f, US_MAT_EARTH);
		}
		/* side twigs */
		for (int k = 0; k < 4; k++) {
			float u = 0.3f + 0.22f * k, px, py;
			bough_pt(st, t, u > 1.0f ? 1.0f : u, &px, &py);
			float tx = px + 0.035f + 0.01f * k, ty = py + 0.018f + 0.008f * (k & 1) - 0.03f * (k == 1);
			if (ctx->crisp)
				us_line(c, px, py, tx, ty, 0.003f, 0.45f, US_MAT_EARTH);
			else
				wline2(c, px, py, tx, ty, 0.5f, 0.3f, US_MAT_EARTH);
		}
		/* flower tassels (sycamore racemes): loose chains that whip at their tips */
		float tas = 1.0f - winter;
		if (tas > 0.1f)
			for (int i = 0; i < NCAT; i++) {
				float u = 0.14f + 0.15f * i, px, py;
				bough_pt(st, t, u, &px, &py);
				float len = (0.035f + 0.025f * hash1(i * 5.1f + 1.0f)) * tas;
				float th = (0.22f * us_noise2(st->wind_t * 0.5f + i * 1.7f, 3.0f) + 0.1f) * (0.3f + wind) +
					   0.3f * st->swell_s * sinf(t * 0.7f + i * 1.3f) + 0.8f * st->bough;
				const int n = 7;
				float cx = px, cy = py + 0.002f;
				for (int j = 0; j < n; j++) {
					float f = (j + 1.0f) / n, an = th * (0.25f + 0.75f * f);
					float nx = cx + sinf(an) * len / n, ny = cy + cosf(an) * len / n;
					if (ctx->crisp)
						us_point(c, nx + ((j & 1) ? 0.003f : -0.003f), ny, 0.003f, 0.5f, US_MAT_BLOOM);
					else {
						wline(c, cx, cy, nx, ny, 0.3f, US_MAT_STEM);
						/* little florets, alternate sides, smaller toward the tip */
						float side = (j & 1) ? 1.0f : -1.0f, fr = 0.0024f * (1.0f - 0.45f * f);
						us_disc(c, nx + side * cosf(an) * fr, ny - side * sinf(an) * fr, fr, 0.42f, US_MAT_BLOOM);
					}
					cx = nx, cy = ny;
				}
				/* the swelling phrase shakes pollen out of the tassels */
				float rate = (0.03f + 1.3f * st->swell_s * st->swell_s) * tas;
				if (us_rand(&st->rng) < dt * rate) {
					part_t *q = free_slot(st);
					if (q) {
						spawn(st, q, 0, 1, A);
						q->kind = K_GRAIN;
						q->x = cx / A;
						q->y = cy;
						q->z = us_randr(&st->rng, 0.55f, 0.9f);
						q->vx = us_randn(&st->rng) * 0.01f;
						q->vy = 0.01f;
						q->life = 0.5f;
					}
				}
			}
		/* seed keys: paired wings hanging in a V; a strong kick can shake a ripe one loose */
		for (int k = 0; k < NKEY; k++) {
			float u = 0.42f + 0.25f * k, px, py;
			bough_pt(st, t, u > 1.0f ? 1.0f : u, &px, &py);
			st->key_on[k] = us_sat(st->key_on[k] + dt * 0.02f);
			float kr = st->key_on[k];
			if (kr < 0.3f)
				continue;
			float sway = 0.15f * us_noise2(st->wind_t * 0.6f, k * 4.0f + 9.0f) + 0.7f * st->bough;
			float nx = px + sinf(sway) * 0.012f, ny = py + cosf(sway) * 0.012f;
			float L = 0.026f * kr;
			if (ctx->crisp)
				us_line(c, px, py, nx, ny, 0.003f, 0.4f, US_MAT_STEM);
			else
				wline(c, px, py, nx, ny, 0.35f, US_MAT_STEM);
			for (int sd = -1; sd <= 1; sd += 2) {
				float an = sway + sd * 0.42f;
				float dx = sinf(an), dy = cosf(an);
				float tx = nx + dx * L, ty = ny + dy * L;
				float mx = nx + dx * L * 0.6f + sd * dy * L * 0.22f, my = ny + dy * L * 0.6f - sd * dx * L * 0.22f;
				if (ctx->crisp) {
					us_line(c, nx, ny, tx, ty, 0.003f, 0.5f, US_MAT_EARTH);
					continue;
				}
				wline2(c, nx, ny, tx, ty, 0.7f, 0.5f, US_MAT_EARTH);
				wline2(c, nx, ny, mx, my, 0.45f, 0.4f, US_MAT_EARTH);
				wline2(c, mx, my, tx, ty, 0.4f, 0.5f, US_MAT_EARTH);
			}
			us_disc(c, nx, ny, ctx->crisp ? 0.004f : 0.0028f, 0.55f, US_MAT_EARTH);
			int drop = kr > 0.99f && ((kick_hit && a->kick > 0.5f && us_rand(&st->rng) < 0.3f) ||
						  us_rand(&st->rng) < dt * (0.004f + 0.03f * autumn));
			if (drop) {
				part_t *q = free_slot(st);
				if (q) {
					spawn(st, q, 0, 1, A);
					q->kind = K_SAMARA;
					q->x = nx / A;
					q->y = ny;
					q->z = us_randr(&st->rng, 0.7f, 1.0f);
					q->vr = (us_rand(&st->rng) < 0.5f ? -1.0f : 1.0f) * us_randr(&st->rng, 7.0f, 10.0f);
					q->life = 1.0f;
					q->ttl = 60.0f;
					st->key_on[k] = 0;
				}
			}
		}
	}

	/* ---- the meadow: a distant row of small clocks, the ground line, grass, and a textured floor ---- */
	/* snare gusts whip everything sideways (and spring back); kicks press the heads down for a moment */
	float lean = 0.06f * st->gust + 0.35f * st->gust_dir * snp;
	float dip = 0.2f * st->thump + 2.3f * kp;
	{
		/* far clocks: tiny, dim, swaying slowly; they catch the light with everything else */
		float bv = (0.2f + 0.1f * day) * glow * (1.0f - 0.7f * winter);
		for (int k = 0; k < NBACK; k++) {
			float xf = st->bk_x[k], x0 = xf * A, gy = ground_y(xf) - 0.012f, H = st->bk_h[k];
			float sw = (0.03f + 0.05f * wind) * us_noise2(st->wind_t * 0.3f, k * 3.7f) + 0.03f * lean;
			float hx = x0 + sw, hy = gy - H, r = st->bk_r[k];
			if (ctx->crisp) {
				us_line(c, x0, gy, hx, hy, 0.002f, bv * 0.8f, US_MAT_STEM);
				if (winter < 0.8f)
					us_point(c, hx, hy, 0.004f, us_sat(bv * 1.6f), US_MAT_ACCENT);
				continue;
			}
			wline(c, x0, gy, hx, hy, bv * 0.6f, US_MAT_STEM);
			if (winter > 0.8f)
				continue;
			for (int j = 0; j < 9; j++) {
				float an = US_TAU * j / 9.0f + k;
				wline2(c, hx, hy, hx + cosf(an) * r, hy + sinf(an) * r * 0.9f, bv * 0.5f, bv, US_MAT_ACCENT);
			}
		}
		float pts[2 * 65];
		for (int i = 0; i <= 64; i++) {
			float xf = i / 64.0f;
			pts[2 * i] = xf * A;
			pts[2 * i + 1] = ground_y(xf);
		}
		us_polyline(c, pts, 65, ctx->crisp ? 0.003f : 0.0012f, 0.35f + 0.2f * winter, US_MAT_EARTH);
		/* soil: a fine stipple under the line, denser near the top of the floor */
		int ns = ctx->crisp ? NSTIP / 3 : NSTIP;
		for (int k = 0; k < ns; k++) {
			float xf = st->sp_x[k], gy = ground_y(xf), u = st->sp_y[k];
			float y = gy + 0.004f + (1.0f - gy) * u * u;
			float v = st->sp_v[k] * (1.0f - 0.5f * u) * (1.0f + 0.6f * winter);
			if (ctx->crisp) {
				if (v > 0.12f)
					us_put(c, (int)(xf * A * c->s), (int)(y * c->s), 0.3f, US_MAT_EARTH);
			} else
				us_point(c, xf * A, y, 0.0009f, v, US_MAT_EARTH);
		}
		uint8_t gm = autumn > 0.5f ? US_MAT_EARTH : US_MAT_LEAF;
		int nb = (int)(NBLADE * (0.4f + 0.6f * dens) * (1.0f - 0.6f * winter));
		for (int b = 0; b < nb; b++) {
			float xf = st->blade_x[b], x0 = xf * A, gy = ground_y(xf), h = st->blade_h[b];
			float sw = (us_noise2(x0 * 1.5f - st->wind_t * 0.5f, st->blade_ph[b]) * 0.6f + 0.3f) * (0.2f + 0.8f * wind) +
				   1.4f * kp * (0.5f + st->blade_ph[b] * 0.05f) + 0.3f * st->thump + 2.0f * lean;
			float bp[8];
			for (int q = 0; q < 4; q++) {
				float u = q / 3.0f;
				bp[2 * q] = x0 + sw * h * u * u;
				bp[2 * q + 1] = gy - h * u * (1.0f - 0.3f * kp);
			}
			for (int q = 0; q < 3; q++)
				wline(c, bp[2 * q], bp[2 * q + 1], bp[2 * q + 2], bp[2 * q + 3], 0.3f, gm);
		}
		/* near tufts rooted down in the floor: taller, a little brighter, leaning with the same wind */
		int nt = (int)(NTUFT * (0.35f + 0.65f * dens) * (1.0f - 0.5f * winter));
		for (int b = 0; b < nt; b++) {
			float xf = st->tf_x[b], x0 = xf * A, gy = ground_y(xf);
			float y0 = gy + 0.01f + (1.02f - gy) * st->tf_y[b], h = st->tf_h[b] * (0.8f + 0.5f * st->tf_y[b]);
			float sw = (us_noise2(x0 * 1.2f - st->wind_t * 0.6f, st->tf_ph[b]) * 0.7f + 0.35f) * (0.25f + 0.8f * wind) +
				   1.6f * kp + 0.3f * st->thump + 2.4f * lean;
			float v = (ctx->crisp ? 0.16f : 0.22f) + 0.12f * st->tf_y[b];
			for (int bl = 0; bl < 3; bl++) {
				float spread = (bl - 1) * 0.35f, hh = h * (1.0f - 0.25f * (bl != 1));
				float px = x0 + bl * 0.003f, py = y0;
				for (int q = 1; q <= 3; q++) {
					float u = q / 3.0f;
					float nx = x0 + bl * 0.003f + (sw + spread) * hh * u * u * 0.8f, ny = y0 - hh * u * (1.0f - 0.3f * kp);
					if (ctx->crisp)
						us_line(c, px, py, nx, ny, 0.002f, v, gm);
					else
						wline(c, px, py, nx, ny, v, gm);
					px = nx, py = ny;
				}
				if (ctx->crisp)
					break;
			}
		}
	}

	/* ---- seed heads / flowers ---- */
	/* with no drums, a melodic note now and then lets a parachute go */
	int beat_ok = a->beat && a->onset > 0.35f && winter < 0.8f && st->since_drum > 1.5f;
	int nseeds = 0;
	for (int i = 0; i < MAXP; i++)
		nseeds += st->p[i].alive && st->p[i].kind == K_SEED;
	int seed_cap = 25 + (int)(70.0f * dens);
	if (nseeds >= seed_cap)
		beat_ok = 0;
	for (int i = 0; i < NHEADS; i++) {
		head_t *h = &st->hd[i];
		/* flowers follow the world's bloom slowly; spent flowers turn to clocks */
		if (h->flower > 0.5f && us_rand(&st->rng) < dt * 0.01f * (1.2f - w->bloom))
			h->flower = 0, h->seeds = 1.0f;
		else if (h->flower < 0.5f && h->seeds < 0.05f && us_rand(&st->rng) < dt * 0.02f * (0.2f + w->bloom))
			h->flower = 1;
		if (h->flower < 0.5f) /* bare clocks fill back in (faster the barer they are) */
			h->seeds = us_sat(h->seeds + dt * (0.03f + 0.09f * (1.0f - h->seeds)) * (1.0f - winter));

		int release = beat_ok && us_rand(&st->rng) < 0.15f + 0.3f * a->onset ? 1 : 0;
		float xf = h->x, x0 = xf * A, gy = ground_y(xf);
		float H = h->h * (1.0f - 0.4f * winter);
		float fl = st->flare[i];
		float sw = (us_noise2(st->wind_t * 0.4f, h->ph) * 0.8f + 0.3f) * (0.04f + 0.12f * wind + 0.06f * st->high_s) +
			   0.15f * (kp + 0.3f * st->thump) * (0.6f + 0.4f * sinf(h->ph)) + /* a kick shudders up through the stems */
			   0.35f * lean * (0.8f + 0.2f * sinf(h->ph * 3.0f));       /* a snare gust bends them over */
		/* every head listens to its own band, low on the left to high on the right; its clock fluffs out (or
		 * its petals open) when that band rises above its usual level */
		float bd = st->bdev[3 + i * 24 / (NHEADS - 1)];
		float hx = x0 + sw * H, hy = gy - H + 0.011f * dip * (H / 0.2f);
		h->hx = hx;
		h->hy = hy;
		float sp[8];
		for (int q = 0; q < 4; q++) {
			float u = q / 3.0f;
			sp[2 * q] = x0 + sw * H * u * u;
			sp[2 * q + 1] = gy - H * u + 0.011f * dip * (H / 0.2f) * u;
		}
		us_polyline(c, sp, 4, ctx->crisp ? 0.002f : 0.0013f, 0.45f, US_MAT_STEM);
		/* dandelion leaves: a low rosette of toothed blades at the foot of the stem */
		if (winter < 0.7f) {
			for (int l = 0; l < 4; l++) {
				float side = (l & 1) ? 1.0f : -1.0f, ang = 0.18f + 0.22f * (l >> 1) + 0.05f * sinf(h->ph + l);
				float L = (0.03f + 0.012f * (l >> 1)) * (0.7f + H * 1.5f);
				float dx = cosf(ang) * side, dy = -sinf(ang);
				float px = x0, py = gy;
				for (int q = 1; q <= 5; q++) {
					float u = q / 5.0f, tooth = (q & 1) ? 0.006f * (1.0f - u) : 0.0f;
					float nx = x0 + dx * L * u - dy * side * tooth;
					float ny = gy + dy * L * u * (1.0f - 0.3f * u) + fabsf(dx) * tooth * 0.6f;
					if (ctx->crisp)
						us_line(c, px, py, nx, ny, 0.002f, 0.32f, US_MAT_LEAF);
					else
						wline(c, px, py, nx, ny, 0.3f, autumn > 0.5f ? US_MAT_EARTH : US_MAT_LEAF);
					px = nx, py = ny;
				}
			}
		}
		h->top = hy - (h->flower > 0.5f ? 0.006f : 0.029f + 0.022f * (H / 0.3f));
		if (winter > 0.8f)
			continue;
		if (h->flower > 0.5f) {
			/* composite flower: ray petals + disc */
			float fr = (0.018f + 0.01f * (H / 0.22f)) * (0.92f + 0.16f * bd + 0.2f * fl);
			float pv = us_sat(0.7f * (0.7f + 0.4f * glow) + 0.3f * fl);
			for (int k = 0; k < h->petals; k++) {
				float ang = US_TAU * k / h->petals + h->ph;
				float ex = hx + cosf(ang) * fr, ey = hy + sinf(ang) * fr * 0.45f;
				us_line(c, hx + cosf(ang) * fr * 0.3f, hy + sinf(ang) * fr * 0.14f, ex, ey,
					ctx->crisp ? 0.003f : 0.0013f, pv, US_MAT_BLOOM);
			}
			us_disc(c, hx, hy, fr * 0.28f, 0.85f, US_MAT_LIGHT);
			/* onsets puff pollen out of open flowers */
			for (int k = 0; k < release * 2; k++)
				launch(st, A, hx, hy, us_randr(&st->rng, -US_PI, 0), 0.004f, us_randr(&st->rng, 0.05f, 0.12f), 0.5f, 1.0f,
				       K_GRAIN);
		} else {
			/* dandelion clock: a sphere of radial hairs, thinning as seeds blow away. The light (loudness) sets
			 * how brightly it glows; a puff that strikes it makes it flare and swell for a moment; hats make a
			 * fresh few of its tips sparkle */
			float cr = (0.026f + 0.022f * (H / 0.3f)) * (0.95f + 0.12f * bd + 0.12f * fl + 0.16f * kp);
			int nh = ctx->crisp ? 14 : 34;
			int keep = (int)(nh * h->seeds + 0.5f);
			float gl = (0.55f + 0.5f * glow) * (1.0f + 0.7f * fl);
			if (ctx->crisp) {
				/* pixel clock: a pale, fluffy ball: a soft disc of down, a rim of seed tips, a dark eye */
				float fill = h->seeds * (1.0f + 0.35f * kp); /* the thump fluffs the down out */
				if (fill > 0.08f) {
					us_disc(c, hx, hy, cr * (0.55f + 0.4f * fill), us_sat(0.38f * gl * fill), US_MAT_ACCENT);
					us_disc(c, hx, hy - cr * 0.15f, cr * 0.5f * fill, us_sat(0.62f * gl * fill), US_MAT_ACCENT);
				}
				for (int k = 0; k < keep; k++) {
					float ang = US_TAU * (k * 0.618034f) + h->ph * 0.1f + sw * 2.0f;
					float ux = cosf(ang), uy = sinf(ang) * 0.92f;
					float spk = hash1(k * 7.1f + i * 3.3f + st->hat_n * 1.37f) < 0.3f ? hat : 0.0f;
					us_point(c, hx + ux * cr, hy + uy * cr, 0.003f, us_sat(0.65f * gl + spk),
						 spk > 0.4f ? US_MAT_LIGHT : US_MAT_ACCENT);
				}
				us_point(c, hx, hy, 0.003f, 0.5f, US_MAT_EARTH);
			}
			for (int k = 0; k < keep && !ctx->crisp; k++) {
				/* a sphere of seeds: fibonacci directions, the far half dimmer and foreshortened */
				float zz = 1.0f - 2.0f * (k + 0.5f) / nh;
				float rr0 = sqrtf(1.0f - zz * zz);
				float ang = US_TAU * (k * 0.618034f) + h->ph * 0.1f + sw * 2.0f;
				float ux = cosf(ang) * rr0, uy = sinf(ang) * rr0 * 0.9f - zz * 0.1f;
				float rr = cr * (0.88f + 0.12f * sinf(k * 3.7f));
				float ex = hx + ux * rr * 0.55f, ey = hy + uy * rr * 0.55f; /* beak ends, pappus begins */
				float hv = 0.75f * (0.55f + 0.45f * (0.5f + 0.5f * zz)) * (0.9f + 0.35f * bd) * gl;
				wline(c, hx, hy, ex, ey, us_sat(0.32f * hv), US_MAT_ACCENT);
				/* each seed's own little umbrella, opening outward */
				float pl = sqrtf(ux * ux + uy * uy) + 1e-4f;
				float tilt = atan2f(ux / pl, -uy / pl);
				pappus(c, 0, ex - ux / pl * 0.003f, ey - uy / pl * 0.003f, rr * 0.42f * (0.6f + 0.4f * pl), tilt,
				       k * 1.3f, us_sat(hv), US_MAT_ACCENT, 8);
				if (hat > 0.05f && zz > -0.2f && hash1(k * 7.1f + i * 3.3f + st->hat_n * 1.37f) < 0.22f)
					us_point(c, hx + ux * rr, hy + uy * rr, 0.0016f, us_sat(hat * 0.9f), US_MAT_ACCENT);
			}
			us_disc(c, hx, hy, cr * 0.12f, 0.6f, US_MAT_EARTH);
			/* notes (and strong thermals) blow the odd seed free */
			int n = release + (us_rand(&st->rng) < dt * (0.05f + 0.5f * st->high_s * st->high_s) ? 1 : 0);
			for (int k = 0; k < n && h->seeds > 0.03f && nseeds < seed_cap + 4; k++, nseeds++) {
				float ang = us_randr(&st->rng, -US_PI, 0);
				launch(st, A, hx, hy, ang, cr, 0.06f, 0.55f, 1.0f, K_SEED);
				h->seeds -= 1.0f / nh;
			}
		}
	}

	/* ---- population: the swell and the level fill the air; quiet thins it to a few ---- */
	float nmax = 250.0f + 700.0f * dens;
	if (nmax > MAXP - 200)
		nmax = MAXP - 200;
	float amount = us_sat(0.2f + 0.6f * w->pollen + 0.15f * w->bloom + 0.3f * winter + 0.45f * st->lvl_slow);
	int target = (int)(nmax * amount);
	int alive = 0;
	for (int i = 0; i < MAXP; i++)
		alive += st->p[i].alive;
	if (alive < target) {
		st->spawn_acc += dt * (8.0f + 0.3f * (target - alive));
		while (st->spawn_acc >= 1.0f) {
			st->spawn_acc -= 1.0f;
			part_t *q = free_slot(st);
			if (q)
				spawn(st, q, winter, 0, A);
		}
	} else
		st->spawn_acc = 0;

	/* ---- particles ---- */
	float lift = 0.004f + 0.03f * st->high_s;
	float breeze = 0.008f + 0.025f * wind;
	float sflow = 0.016f + 0.035f * wind; /* the currents run faster than the free air */
	float light = (0.45f + 0.55f * day) * (0.75f + 0.25f * st->high_s) * glow;
	float kdec = expf(-rdt / 0.12f);     /* puff / gust impulses die away fast */
	if (!ctx->crisp)
		c->blend = US_BLEND_ADD;
	for (int i = 0; i < MAXP; i++) {
		part_t *q = &st->p[i];
		if (!q->alive)
			continue;
		float par = 0.35f + 0.65f * q->z; /* parallax: near things move more */
		float X = q->x * A;
		/* sample wind field */
		float gxf = us_clampf(q->x * CGX - 0.5f, 0, CGX - 1.001f), gyf = us_clampf(q->y * CGY - 0.5f, 0, CGY - 1.001f);
		int gi = (int)gxf, gj = (int)gyf;
		float tx = gxf - gi, ty = gyf - gj;
		int i1 = gi + 1 < CGX ? gi + 1 : gi, j1 = gj + 1 < CGY ? gj + 1 : gj;
		float fvx = us_lerp(us_lerp(st->fvx[gj * CGX + gi], st->fvx[gj * CGX + i1], tx),
				    us_lerp(st->fvx[j1 * CGX + gi], st->fvx[j1 * CGX + i1], tx), ty);
		float fvy = us_lerp(us_lerp(st->fvy[gj * CGX + gi], st->fvy[gj * CGX + i1], tx),
				    us_lerp(st->fvy[j1 * CGX + gi], st->fvy[j1 * CGX + i1], tx), ty);
		float fall, buoy;
		switch (q->kind) {
		case K_SEED: fall = 0.010f; buoy = 1.3f; break;
		case K_SPORE: fall = 0.004f; buoy = 0.8f; break;
		case K_SNOW: fall = 0.022f; buoy = 0.25f; break;
		case K_SAMARA: fall = 0.055f; buoy = 0.12f; break;
		case K_THISTLE: fall = 0.007f; buoy = 0.85f; break;
		default: fall = 0.006f; buoy = 1.0f; break;
		}
		float tvx = (fvx + breeze * (0.6f + 0.4f * q->seedv)) * par;
		float tvy = (fvy + fall - lift * buoy) * par;
		/* thermals are patchy: noisy updraft columns */
		tvy -= lift * buoy * 0.8f * us_sat(us_noise2(X * 2.0f + t * 0.05f, 3.3f)) * par;
		if (q->kind == K_THISTLE) /* the air band holds the silk aloft */
			tvy -= 0.01f * st->air_s * par;
		if (q->stream >= 0 && q->stream < NSTREAM) {
			/* riding a current: carried downstream along its meander, eased back into the ribbon */
			float x1 = q->x + 0.02f;
			float yc = stream_y(st, q->stream, q->x), slope = (stream_y(st, q->stream, x1) - yc) / (0.02f * A);
			float sx = sflow * (0.75f + 0.5f * q->seedv) * par;
			float sy = sx * slope + us_clampf((yc + q->off - q->y) * 1.2f, -0.06f, 0.06f);
			tvx = us_lerp(tvx, sx + 0.25f * fvx * par, 0.85f);
			tvy = us_lerp(tvy, sy + 0.25f * fvy * par, 0.85f);
		}
		float resp = q->kind == K_SEED || q->kind == K_THISTLE ? 1.2f : (q->kind == K_SAMARA ? 0.35f : 0.5f);
		q->vx = us_approach(q->vx, tvx, resp, dt);
		q->vy = us_approach(q->vy, tvy, resp, dt);
		q->kvx = us_clampf(q->kvx * kdec, -2.0f, 2.0f);
		q->kvy = us_clampf(q->kvy * kdec, -2.0f, 2.0f);
		q->x += (q->vx + q->kvx) * dt / A;
		q->y += (q->vy + q->kvy) * dt;
		if (q->kind == K_SAMARA) {
			/* autorotation: a whirl that also sways the key side to side as it spirals down */
			q->rot += q->vr * dt;
			q->x += 0.012f * cosf(q->rot) * fabsf(q->vr) * dt / A * 0.6f;
		} else {
			/* a puff tumbles a parachute; it rights itself again */
			q->rot += (q->vr + (q->vx + q->kvx) * 6.0f) * dt;
			q->rot = us_lerp(q->rot, 0.0f, dt * 0.3f);
		}
		q->age += dt;
		if (q->age > q->ttl)
			q->life -= dt * 0.4f; /* settles out of the air */
		else
			q->life = us_sat(q->life + dt * 0.5f);
		int out = q->x < -0.05f || q->x > 1.05f || q->y < -0.06f || q->y > 1.02f || !(q->x == q->x) || !(q->y == q->y);
		if ((q->kind == K_SNOW || q->kind == K_SAMARA) && q->y > ground_y(q->x))
			out = 1;
		/* a thinning air: surplus motes simply settle (fade) rather than vanish */
		if (alive > target + 20 && q->age < q->ttl && us_rand(&st->rng) < dt * 0.15f)
			q->ttl = q->age;
		if (out || q->life < 0) {
			q->alive = 0;
			alive--;
			continue;
		}
		X = q->x * A;
		/* sunbeam catch */
		float glint = 0;
		for (int k = 0; k < NBEAM; k++) {
			if (beam_v[k] < 0.02f)
				continue;
			float bx = st->beam_x[k] * A + bdx * (q->y + 0.02f);
			float d = fabsf(X - bx) / (st->beam_w[k] * 0.6f);
			if (d < 1.0f)
				glint += beam_v[k] * (1.0f - d * d);
		}
		/* depth of field: the nearest things are large, soft and a little out of focus */
		float soft = us_smooth(0.8f, 1.0f, q->z);
		float v = q->life * light * (0.3f + 0.7f * q->z) * (1.0f + 1.6f * glint) * (1.0f - 0.35f * soft);
		uint8_t mat = q->kind == K_SNOW ? US_MAT_WATER : (q->kind == K_SPORE ? US_MAT_LEAF : US_MAT_ACCENT);
		if (q->kind == K_GRAIN && glint > 0.2f)
			mat = US_MAT_LIGHT;
		/* hats: a fresh scatter of tips catches the light on every hit */
		float gsel = hat > 0.04f && q->z > 0.2f && q->kind != K_SAMARA &&
				     hash1(i * 1.731f + st->hat_n * 12.97f) < 0.3f ? hat * q->life : 0.0f;
		float gx = X, gyv = q->y;
		switch (q->kind) {
		case K_SEED: {
			float sz = (0.007f + 0.03f * q->z * q->z) * (1.0f + 0.25f * soft);
			float spin = q->seedv * US_TAU + q->age * (0.3f + 0.5f * q->seedv) + q->vx * 4.0f;
			float tl = q->rot * 0.4f;
			gx = X + sinf(tl) * sz * 1.05f, gyv = q->y - cosf(tl) * sz * 1.05f; /* the crown */
			if (q->z < 0.2f || (ctx->crisp && sz < 0.008f))
				us_point(c, X, q->y, ctx->crisp ? 0.003f : 0.0014f, us_sat(v * 0.9f), US_MAT_ACCENT);
			else {
				if (soft > 0.01f && !ctx->crisp) /* a soft halo of out-of-focus down */
					us_disc(c, gx, gyv + sz * 0.3f, sz * 0.85f, us_sat(0.05f * soft * v), US_MAT_ACCENT);
				pappus(c, ctx->crisp, X, q->y, sz, tl, spin, us_sat(v * (ctx->crisp ? 1.5f : 0.75f)), US_MAT_ACCENT,
				       24);
			}
			break;
		}
		case K_SPORE: {
			float r = 0.0012f + 0.0035f * q->z + 0.006f * soft;
			if (ctx->crisp)
				us_point(c, X, q->y, 0.003f, us_sat(v * 0.6f), mat);
			else {
				hring(c, X, q->y, r, us_sat(v * (0.4f - 0.2f * soft)), mat);
				us_point(c, X, q->y, 0.0007f, us_sat(v * 0.6f * (1.0f - soft)), mat);
			}
			break;
		}
		case K_SAMARA:
			samara(c, ctx->crisp, X, q->y, 0.016f + 0.014f * q->z, q->rot, us_sat(v * 0.8f), US_MAT_EARTH);
			break;
		case K_THISTLE: {
			float sz = 0.008f + 0.034f * q->z * q->z;
			if (q->z < 0.15f)
				us_point(c, X, q->y, ctx->crisp ? 0.003f : 0.0016f, us_sat(v * 0.7f), US_MAT_ACCENT);
			else
				thistle(c, ctx->crisp, X, q->y, sz, 0.35f * sinf(q->age * (0.5f + q->seedv) + q->seedv * 9.0f) + q->vx * 3.0f,
					st->air_s, us_sat(v * 0.8f), US_MAT_ACCENT);
			break;
		}
		case K_SNOW: {
			float r = 0.0012f + 0.004f * q->z;
			if (ctx->crisp)
				us_disc(c, X, q->y, r * 0.8f, us_sat(v * 0.8f), mat);
			else
				us_disc(c, X, q->y, r, us_sat(v * 0.55f), mat);
			break;
		}
		default: {
			if (ctx->crisp)
				us_point(c, X, q->y, 0.003f, us_sat(v * 0.55f), mat);
			else if (q->stream >= 0 && soft < 0.05f) {
				/* riding a current: a short streak along its flight, so the streams read as flowing threads */
				float vx = q->vx + q->kvx, vy = q->vy + q->kvy, sl = 0.3f;
				float sp2 = vx * vx + vy * vy;
				if (sp2 * sl * sl > 0.0004f)
					sl = 0.02f / sqrtf(sp2);
				us_disc(c, X, q->y, 0.0008f + 0.002f * q->z, us_sat(v * 0.8f), mat);
				wline2(c, X, q->y, X - vx * sl, q->y - vy * sl, us_sat(v * 0.45f), 0.0f, mat);
			} else if (soft > 0.05f) {
				/* a near grain: a big faint bokeh disc with a slightly brighter rim */
				float r = 0.003f + 0.012f * soft;
				us_disc(c, X, q->y, r, us_sat(v * 0.12f), mat);
				hring(c, X, q->y, r, us_sat(v * 0.22f), mat);
			} else
				us_disc(c, X, q->y, 0.0009f + 0.0026f * q->z, us_sat(v * 0.85f), mat);
			break;
		}
		}
		if (gsel > 0.02f) {
			/* a four-point glint: longer on near things */
			float gv = us_sat(gsel * (0.55f + 0.6f * q->z) * (0.6f + 0.5f * glow));
			if (ctx->crisp) {
				us_point(c, gx, gyv, 0.003f, gv, US_MAT_LIGHT);
				if (q->z > 0.55f && gv > 0.4f) {
					int px = (int)(gx * c->s), py = (int)(gyv * c->s);
					us_put(c, px - 1, py, gv * 0.7f, US_MAT_LIGHT);
					us_put(c, px + 1, py, gv * 0.7f, US_MAT_LIGHT);
					us_put(c, px, py - 1, gv * 0.7f, US_MAT_LIGHT);
					us_put(c, px, py + 1, gv * 0.7f, US_MAT_LIGHT);
				}
			} else {
				float gl = (0.003f + 0.009f * q->z) * (0.6f + 0.4f * gsel);
				us_point(c, gx, gyv, 0.0012f + 0.001f * q->z, gv, US_MAT_ACCENT);
				wline2(c, gx, gyv, gx - gl, gyv, gv, 0.0f, US_MAT_ACCENT);
				wline2(c, gx, gyv, gx + gl, gyv, gv, 0.0f, US_MAT_ACCENT);
				wline2(c, gx, gyv, gx, gyv - gl, gv, 0.0f, US_MAT_ACCENT);
				wline2(c, gx, gyv, gx, gyv + gl, gv, 0.0f, US_MAT_ACCENT);
			}
		}
	}

	/* ---- the wind events themselves, drawn as breath in the air ---- */
	for (int k = 0; k < NEV; k++) {
		ev_t *e = &st->ev[k];
		if (!e->live)
			continue;
		float age = e->age;
		e->age += rdt;
		if (age > (e->kind ? 0.2f : 0.15f)) {
			e->live = 0;
			continue;
		}
		if (e->kind == 0) {
			/* kick: a wispy ring of breath expanding from the struck clock */
			float u = age / 0.15f, grow = 1.0f - (1.0f - u) * (1.0f - u);
			float r = e->r0 * 1.05f + 0.16f * e->str * grow;
			float v = 0.55f * e->str * (1.0f - u) * (0.6f + 0.5f * glow);
			if (v < 0.02f)
				continue;
			const int NSEG = 48;
			for (int ring = 0; ring < 2; ring++) {
				float rr = r * (ring ? 0.78f : 1.0f), vv = v * (ring ? 0.5f : 1.0f);
				float px = e->x + rr, py = e->y;
				for (int j = 1; j <= NSEG; j++) {
					float an = US_TAU * j / NSEG;
					float wisp = us_sat(0.55f + 0.6f * us_noise2(cosf(an) * 1.7f + k * 3.1f + ring, sinf(an) * 1.7f + e->str));
					float nx = e->x + cosf(an) * rr * (1.0f + 0.06f * wisp), ny = e->y + sinf(an) * rr * 0.92f * (1.0f + 0.06f * wisp);
					if (ctx->crisp) {
						if (vv * wisp > 0.1f)
							us_line(c, px, py, nx, ny, 0.002f, us_sat(vv * wisp * 1.5f), US_MAT_ACCENT);
					} else
						wline(c, px, py, nx, ny, us_sat(vv * wisp), US_MAT_ACCENT);
					px = nx, py = ny;
				}
			}
		} else {
			/* snare: a gust curling round, three comet-tailed arcs spiralling in its sense */
			float v = 0.55f * e->str * (1.0f - age / 0.2f) * (0.6f + 0.5f * glow);
			if (v < 0.02f)
				continue;
			for (int j = 0; j < 3; j++) {
				float R0 = e->r0 * (0.4f + 0.27f * j) * (0.85f + 1.5f * age);
				float head = e->dir * (age * 13.0f + j * 2.1f) + k;
				const int NS2 = 16;
				float px = 0, py = 0;
				for (int q = 0; q <= NS2; q++) {
					float u = (float)q / NS2; /* 0 head .. 1 tail */
					float an = head - e->dir * u * 1.8f, rr = R0 * (1.0f + 0.25f * u);
					float nx = e->x + cosf(an) * rr, ny = e->y + sinf(an) * rr * 0.75f;
					float vv = v * (1.0f - u) * (1.0f - u);
					if (q > 0) {
						if (ctx->crisp) {
							if (vv > 0.08f)
								us_line(c, px, py, nx, ny, 0.002f, us_sat(vv * 1.6f), US_MAT_LIGHT);
						} else
							wline(c, px, py, nx, ny, us_sat(vv), US_MAT_ACCENT);
					}
					px = nx, py = ny;
				}
			}
		}
	}
	c->blend = US_BLEND_MAX;

	/* ---- hoverflies: hang motionless in the air, then dart to a new station when their slice of the melody
	 * (one mid band each) rises; with nothing to answer they still drift off every few seconds ---- */
	if (winter < 0.75f)
		for (int f = 0; f < NFLY; f++) {
			if (f == 2 && dens < 0.3f)
				break;
			fly_t *fl = &st->fly[f];
			fl->cool -= rdt;
			fl->idle -= dt;
			float d = st->bdev[fl->band] + 0.5f * st->bdev[fl->band + 1];
			int rise = d - fl->env > 0.22f;
			fl->env = us_approach(fl->env, d, 0.5f, rdt);
			if ((rise && fl->cool <= 0) || fl->idle <= 0) {
				float jump = us_randr(&st->rng, 0.04f, 0.13f) * (us_rand(&st->rng) < 0.5f ? -1.0f : 1.0f);
				float nhx = us_clampf(fl->hx + jump, 0.08f * A, 0.92f * A);
				fl->hy = us_clampf(fl->hy + us_randn(&st->rng) * 0.04f, 0.28f, 0.78f);
				if (fabsf(nhx - fl->hx) > 0.01f)
					fl->dir = nhx > fl->hx ? 1.0f : -1.0f;
				fl->hx = nhx;
				fl->cool = us_randr(&st->rng, 0.9f, 1.8f);
				fl->idle = us_randr(&st->rng, 3.0f, 9.0f);
			}
			float ox = fl->x, oy = fl->y;
			fl->x = us_approach(fl->x, fl->hx, 0.09f, dt);
			fl->y = us_approach(fl->y, fl->hy, 0.09f, dt);
			float spd = sqrtf((fl->x - ox) * (fl->x - ox) + (fl->y - oy) * (fl->y - oy)) / (dt + 1e-4f);
			float jx = 0.0018f * us_noise2(t * 2.3f, fl->ph), jy = 0.0014f * us_noise2(t * 2.0f, fl->ph + 5.0f);
			insect(c, ctx->crisp, 0, fl->x + jx, fl->y + jy, fl->dir, 1.0f, us_sat(0.4f + spd * 2.0f), t * 31.0f + fl->ph,
			       0.75f + 0.25f * day);
		}

	/* ---- bumblebees: the low-mid drone. They work the heads one by one; the more low-mid body in the
	 * music, the faster and loopier they fly and the harder the wings blur ---- */
	for (int b = 0; b < NBEE; b++) {
		if (b == 1 && dens < 0.35f)
			break;
		bee_t *be = &st->bee[b];
		float lm = st->lowmid_s;
		int away = winter > 0.6f;
		if (be->state == 1 && be->tgt >= 0 && !away) {
			head_t *h = &st->hd[be->tgt];
			be->x = us_approach(be->x, h->hx + 0.003f * sinf(t * 0.9f + be->ph), 0.15f, dt);
			be->y = us_approach(be->y, h->top, 0.15f, dt);
			be->visit -= dt * (0.7f + 0.8f * lm);
			if (be->visit <= 0 || (h->flower < 0.5f && h->seeds < 0.1f))
				be->state = 0, be->tgt = -2; /* lift off, then choose */
		} else {
			if (be->tgt == -2 || away) {
				if (away) {
					be->tgt = -1, be->wx = A + 0.2f, be->wy = 0.35f;
				} else if (us_rand(&st->rng) < 0.7f) {
					be->tgt = (int)(us_rand(&st->rng) * NHEADS);
				} else {
					be->tgt = -1;
					be->wx = us_randr(&st->rng, 0.1f, 0.9f) * A;
					be->wy = us_randr(&st->rng, 0.35f, 0.7f);
				}
			}
			float txp = be->tgt >= 0 ? st->hd[be->tgt].hx : be->wx, typ = be->tgt >= 0 ? st->hd[be->tgt].top : be->wy;
			float dx = txp - be->x, dy = typ - be->y, dist = sqrtf(dx * dx + dy * dy) + 1e-5f;
			float vmax = 0.05f + 0.13f * lm, sp = fminf(vmax, dist * 2.0f + 0.01f);
			float wob = (0.03f + 0.08f * lm) * us_noise2(t * (0.9f + 1.2f * lm), be->ph);
			float dvx = dx / dist * sp - dy / dist * wob, dvy = dy / dist * sp + dx / dist * wob;
			be->vx = us_approach(be->vx, dvx, 0.3f, dt);
			be->vy = us_approach(be->vy, dvy, 0.3f, dt);
			be->x += be->vx * dt;
			be->y += be->vy * dt;
			if (fabsf(be->vx) > 0.01f)
				be->dir = be->vx > 0 ? 1.0f : -1.0f;
			if (be->tgt >= 0 && dist < 0.006f)
				be->state = 1, be->visit = us_randr(&st->rng, 1.5f, 5.0f);
			else if (be->tgt == -1 && dist < 0.02f && !away)
				be->tgt = -2;
			if (be->tgt >= 0 && st->hd[be->tgt].flower < 0.5f && st->hd[be->tgt].seeds < 0.1f)
				be->tgt = -2;
		}
		if (away && be->x > A + 0.05f)
			continue;
		if (!away && be->x > A + 0.05f) /* came back after the winter */
			be->x = A + 0.04f;
		float bob = be->state == 1 ? 0 : 0.002f * sinf(t * 5.0f + be->ph);
		insect(c, ctx->crisp, 1, be->x, be->y - 0.004f + bob, be->dir, 1.0f + 0.15f * (b == 0),
		       be->state == 1 ? 0.15f : 0.45f + 0.55f * lm, t * 37.0f + be->ph, 0.8f + 0.2f * day);
	}

	/* ---- a big dandelion clock close to us in the lower right. Each of its seeds trembles with its own band,
	 * so the spectrum ripples over the sphere; a strong hit lets a few go, very near and large ---- */
	st->fg_on = winter < 0.8f;
	if (winter < 0.8f) {
		float fx = A - 0.19f, fby = 1.03f;
		float FH = 0.42f, ff = st->fg_flare;
		float fsw = (us_noise2(st->wind_t * 0.3f, st->fg_ph) * 0.6f + 0.2f) * (0.03f + 0.08f * wind) + 0.06f * (kp + 0.3f * st->thump) +
			    0.12f * lean;
		float hx = fx + fsw * FH, hy = fby - FH + 0.012f * dip;
		st->fg_hx = hx, st->fg_hy = hy;
		float sp[8];
		for (int q = 0; q < 4; q++) {
			float u = q / 3.0f;
			sp[2 * q] = fx + fsw * FH * u * u;
			sp[2 * q + 1] = fby - FH * u + 0.012f * dip * u;
		}
		us_polyline(c, sp, 4, ctx->crisp ? 0.004f : 0.0026f, 0.5f, US_MAT_STEM);
		float R = 0.1f * (1.0f + 0.1f * ff + 0.08f * kp);
		st->fg_seeds = us_sat(st->fg_seeds + dt * (0.008f + 0.05f * (1.0f - st->fg_seeds)) * (1.0f - winter));
		int keep = (int)(FGN * st->fg_seeds + 0.5f);
		float gl = (0.6f + 0.45f * glow) * (1.0f + 0.6f * ff);
		if (ctx->crisp && st->fg_seeds > 0.08f) {
			/* pixel: a pale ball of down behind the seed tips */
			us_disc(c, hx, hy, R * (0.5f + 0.35f * st->fg_seeds), us_sat(0.36f * gl * st->fg_seeds), US_MAT_ACCENT);
			us_disc(c, hx, hy - R * 0.12f, R * 0.45f * st->fg_seeds, us_sat(0.58f * gl * st->fg_seeds), US_MAT_ACCENT);
		}
		for (int k = 0; k < keep; k++) {
			float zz = 1.0f - 2.0f * (k + 0.5f) / FGN;
			float rr0 = sqrtf(1.0f - zz * zz);
			float ang = US_TAU * (k * 0.618034f) + st->fg_ph + fsw * 1.5f;
			float tremble = st->bdev[2 + (k * 11) % 27];
			float ux = cosf(ang) * rr0, uy = sinf(ang) * rr0 * 0.92f - zz * 0.1f;
			float rr = R * (0.9f + 0.1f * sinf(k * 3.7f)) * (1.0f + 0.07f * tremble);
			float hv = 0.8f * (0.5f + 0.25f * (1.0f + zz)) * (0.85f + 0.4f * tremble) * gl;
			float ex = hx + ux * rr * 0.55f, ey = hy + uy * rr * 0.55f;
			float spk = hat > 0.05f && zz > -0.3f && hash1(k * 5.3f + st->hat_n * 2.71f) < 0.25f ? hat : 0.0f;
			if (ctx->crisp) {
				us_line(c, hx, hy, hx + ux * rr * 0.8f, hy + uy * rr * 0.8f, 0.002f, us_sat(0.3f * hv), US_MAT_ACCENT);
				us_point(c, hx + ux * rr, hy + uy * rr, 0.003f, us_sat(hv + spk), spk > 0.4f ? US_MAT_LIGHT : US_MAT_ACCENT);
			} else {
				wline(c, hx, hy, ex, ey, us_sat(0.3f * hv), US_MAT_ACCENT);
				float pl = sqrtf(ux * ux + uy * uy) + 1e-4f;
				float tilt = atan2f(ux / pl, -uy / pl) + 0.12f * tremble * sinf(t * 7.0f + k);
				pappus(c, 0, ex - ux / pl * 0.004f, ey - uy / pl * 0.004f, rr * 0.42f * (0.6f + 0.4f * pl), tilt,
				       k * 1.3f, us_sat(hv), US_MAT_ACCENT, 12);
				if (spk > 0)
					us_point(c, hx + ux * rr, hy + uy * rr, 0.002f, us_sat(spk), US_MAT_ACCENT);
			}
		}
		us_disc(c, hx, hy, R * 0.1f, 0.55f, US_MAT_EARTH);
		/* with no drums about, a strong note now and then lets one go */
		int n = (a->beat && a->onset > 0.55f && st->since_drum > 1.5f && us_rand(&st->rng) < 0.3f) +
			(us_rand(&st->rng) < dt * 0.04f * (0.3f + st->high_s));
		for (int k = 0; k < n && st->fg_seeds > 0.15f; k++) {
			launch(st, A, hx, hy, us_randr(&st->rng, -US_PI * 0.9f, -US_PI * 0.1f), R, 0.05f, 0.9f, 1.0f, K_SEED);
			st->fg_seeds -= 1.0f / FGN;
		}
	}
	if (!ctx->crisp) {
		tint_air(c, US_MAT_ACCENT, 12, (1u << US_MAT_BG) | (1u << US_MAT_WATER));
	}
	st->inited = 2;
	st->kage = fminf(st->kage + rdt, 9.0f);
	st->sage = fminf(st->sage + rdt, 9.0f);
	st->hage = fminf(st->hage + rdt, 9.0f);
}

const us_scene_vt us_scene_pollen = {"pollen", "Pollen & Seeds", 0.12f, create, destroy, frame};
