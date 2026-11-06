# Writing an Understory scene

Read `docs/design.md` first (intent + scene table). The reference implementation is
`src/scenes/meadow.c` — match its quality bar and structure.

## The contract

```c
const us_scene_vt us_scene_<id> = {"<id>", "<Name>", <default trails 0..1>, create, destroy, frame};
```

- `create(seed)` → your state (calloc). Seed everything from `us_rng` (PCG32 in `core/us_math.h`).
- `frame(state, ctx, canvas)` → simulate one step AND draw. The host already faded the canvas
  according to trails (your default `trails` + the user's bias) and set `c->blend = US_BLEND_MAX`.
  You may set `c->blend = US_BLEND_ADD` for glowy accumulations (reset not needed; host resets).
- Draw in **normalized units**: y ∈ [0,1] top→bottom, x ∈ [0, ctx->aspect]. Radii/widths in the same units.
  Never use pixel counts. Store positions as normalized values (or 0..1 fractions of width, multiplied by
  aspect at draw time) so aspect/resolution changes mid-run are harmless.
- `ctx->crisp` is 1 for Pixel/Mono-pixel canvases (~320×180, no AA). Good scenes adapt: e.g. filled shapes
  (`us_tri`, `us_disc`) when crisp, outlines/hairlines when not (see `petal()` in meadow.c).
- Line widths: hairline ≈ 0.001, fine ≈ 0.0015, stem ≈ 0.0025. Ink is "fine single-weight lines" —
  restraint over glow. Brightness `v` is 0..1; keep backgrounds/context ~0.3–0.6, focal things 0.8–1.0.
- Materials (`enum us_mat`) only matter in Pixel style (palette colour per material). Choose honestly:
  EARTH ground/rock/sand, STEM threads/trunks/hyphae, LEAF foliage/grass/coral, BLOOM flowers/fruit,
  WATER water/rain/foam, LIGHT fireflies/aurora/lightning/moon, ACCENT birds/seeds/other.

## Inputs

- `ctx->dt` simulation dt (already × speed macro). `ctx->real_dt` wall dt (use for audio envelopes).
- `ctx->a` (`us_audio`, already × reactivity): `sub bass lowmid mid high air level swell onset beat kick
  snare hat kick_hit snare_hit hat_hit centroid width silence active bands[32]` — see `core/us_audio.h`.
  `kick`/`snare`/`hat` are drum envelopes from separate detectors (notes and steady noise don't fire them);
  `*_hit` and `beat` are 1-frame flags; `onset` is a broadband decaying envelope. `swell` is phrase-scale
  loudness (~8 s). Regions and bands read low where the mix holds nothing.
- Give kick, snare and hats each a distinct, visible answer, and make louder mean more light/life.
  Check with `python3 tools/reactivity.py <scene>` (targets: kickX ≥ 2, snareX ≥ 1.8, hatX ≥ 1.4,
  brightness correlating with loudness on the ambient and piano tracks, calm in the techno track's gap).
  Keep the baseline motion low (dMed ~0.5–4): constant churn hides every reaction.
- `ctx->p->density` (0..1, default 0.5): how much stuff. Must visibly change the scene and stay sane at 0 and 1.
- `ctx->w` the shared world (`core/us_world.h`). READ fields freely. WRITE only via
  `us_world_add(w, &w->field, rate_per_second, ctx->dt)`. Respect the design table's read/write roles.
  `us_world_season_w(w, s)` gives spring/summer/autumn/winter weights; `w->daylight`, `w->day_phase`.

## Principles (from the user)

- **Living, never frozen**: with zero audio the scene must still move gently and evolve. Test it.
- **Music shapes nature, it doesn't strobe it**: map features to natural causes (growth, wind, flow, rain),
  smoothed. No full-screen flashes except a scene's natural one (lightning).
- **Memory through the world**: when the scene opens, initialise from world state (e.g. river level from
  `water_level`, firefly count from `insects`) so returning to a scene reflects what happened elsewhere.
- Generative + nature-inspired; each scene should be beautiful on its own in both Ink and Pixel.

## Helpers available (`core/us_canvas.h`, `core/us_math.h`)

`us_point us_disc us_ring us_arc us_line us_polyline us_tri us_field us_put`,
`us_noise2/3 us_fbm2/3` (gradient noise ~[-1,1]), `us_rng/us_rand/us_randr/us_randn`,
`us_sat us_clampf us_lerp us_smooth us_approach(cur, target, tau, dt)`.
Do NOT modify `src/core/*` — if you need a helper, write it `static` inside your scene file.
Fixed-size arrays in your state struct; no per-frame malloc.

## Performance budget

Ink at 1280×720 ≤ ~4 ms/frame average (the harness prints `avg_frame`), pixel styles ≤ 1 ms.
`us_field` over the full canvas costs ~1–2 ms at 720p ink; fine once, not five times.
Full-canvas per-pixel loops in Ink should be avoided; compute fields at low res and draw with `us_field`.

## Build + verify (do this for every scene, iterate until it looks great)

```bash
cmake -S . -B build -DUNDERSTORY_BUILD_PLUGIN=OFF && cmake --build build -j
./build/us_tests                                   # must pass: every scene × every style × resizes
# render frames with the synthetic 120 bpm track (64 s loop: 0–16 intro pad, 16–32 build, 32–48 drop, 48–64 breakdown)
./build/us_render --scene <id> --synth --style ink   --prewarm 40 --start 40 --seconds 24 --at 41,50,58,63 --out out/<id>/ink --size 1280x720
./build/us_render --scene <id> --synth --style pixel --prewarm 40 --start 40 --seconds 24 --at 41,50,58,63 --out out/<id>/pixel --size 1280x720 --pixel 5 --palette 0
./build/us_render --scene <id> --synth --style mono  --tint 255,176,60 ...
for f in out/<id>/*/*.ppm; do ffmpeg -loglevel error -y -i $f ${f%.ppm}.png; done
# then LOOK at the PNGs with the Read tool. Also check silence: --audio /dev/null style runs, or --start 66 (intro).
# motion check: --every 3 --seconds 6 then: ffmpeg -framerate 10 -i out/<id>/ink/<id>_%06d.ppm -vf scale=640:-1 out/<id>.gif
```
Useful flags: `--density 0|1`, `--react`, `--speed`, `--palette 0..5`, `--stats` (prints audio + world each second),
`--world in.json` to start from a particular world (e.g. winter: write a JSON with `"season": 3.0`).
