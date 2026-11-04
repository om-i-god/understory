# Understory — design

An OBS video source that renders **living, generative, nature-inspired visuals driven by audio**.
Author: om.i.god. Platform: macOS first (Apple Silicon, OBS 32.x), Windows later from the same code.

## Intent

The coolest living nature visualizer possible, as a native OBS source, for live visual sets.
Music *shapes* the nature rather than strobing it: bass grows and blooms, highs are wind,
transients are rain and lightning, slow loudness is tide and season. Nothing ever freezes —
every scene keeps living through silence.

## Decisions made with the user

- **Separate scenes**, each a full-screen view, chosen per source instance.
- **One shared, hidden ecosystem ("the world")** under all scenes. Scenes remember each other:
  a long storm swells the river; calm stretches bring fireflies; mycelium grown earlier makes
  the meadow bloom faster; blooms release pollen that feeds the reef and the flock.
  The world persists across OBS restarts; a *Reset world* button starts fresh.
- **Looks** (per-source `style` setting, switchable live without resetting the scene):
  - **Ink** — fine single-weight lines at full resolution on black.
  - **Pixel** — low internal resolution, crisp nearest-neighbour upscale, small curated palettes.
  - **Mono-pixel** — pixel resolution, one ink colour, 16 brightness levels (norns OLED blown up).
- **Tint** is its own setting (colours Ink and Mono-pixel). Pixel uses a palette selector.
- Lots of options. Native C plugin; GPU only presents; world saved to a small JSON file.

## Scenes (13) and their audio roles

| id | Scene | Audio role | World role |
|----|-------|-----------|------------|
| meadow | Meadow — plants grow & bloom | bass → growth/bloom, highs → sway | reads growth, mycelium, moisture, season; writes growth, bloom |
| wind | Wind — grass field / gusts | highs → gust strength, centroid → gust direction | reads growth, season; writes pollen (shakes seeds loose) |
| river | River — flowing water lines | mids/level → flow speed | reads water_level; writes moisture |
| rain | Rain — falling rain (snow in winter) | onsets → drops, level → density | writes moisture, water_level; reads season (snow) |
| storm | Storm front — clouds + lightning | sustained energy charges, biggest hits strike | writes storm_charge, moisture |
| pond | Pond — ripples & caustics | onsets → rings, bass → caustic swell | reads water_level, insects (water striders) |
| fireflies | Fireflies — bioluminescent swarm | quiet → few drift; density → swarm & sync flashing | reads calm, insects, daylight; writes insects |
| mycelium | Mycelium — underground network | bass lines → spread, kicks → nutrient pulses | writes mycelium; reads moisture |
| murmuration | Murmuration — flock | melody/centroid → wheel, big transients → scatter | reads flock, pollen; writes flock |
| tide | Tide — shoreline waves | swell (slow loudness) → rolling waves, highs → foam | reads water_level, daylight (moon) |
| pollen | Pollen & seeds — drifting spores | highs lift seeds, sub sways spores | reads bloom, pollen; writes pollen |
| coral | Coral / lichen — reaction-diffusion | low-mids feed growth, very slow | reads pollen, water_level; writes reef |
| aurora | Aurora — curtains of light | stereo width + shimmer ripple | reads daylight, season, storm_charge |

**Seasons/time-of-day** are not a scene: they are a global world layer every scene reads.
`season` advances with cumulative musical energy (a full year ≈ 40 min of active music);
`daylight` cycles slowly. Winter turns rain to snow, autumn reddens/sheds leaves, etc.

## Architecture

```
OBS audio source ──capture cb──► us_analyzer (FFT, bands, onsets, swell, width)
                                       │ us_audio (per frame)
us_world (singleton, JSON persisted) ◄─┼─► scene.frame(ctx, canvas)   (sim + draw, normalized coords)
                                       ▼
                                   us_canvas (ink + material, 2×u8 per px)
                                       ▼ us_compose(style, tint, palette)
                                   RGBA buffer ──► gs texture ──► obs_source_draw (point or linear)
```

- **`src/core`** — portable C11, no OBS dependency: canvas + rasteriser, composer, audio
  analyser, world, scene registry, small RNG/noise helpers. Everything here is testable headless.
- **`src/scenes`** — one file per scene, implementing `us_scene_vt`.
- **`src/obs`** — the thin OBS module: properties, audio capture hookup, tick, texture upload.
- **`tools/us_render`** — headless harness: renders any scene with real audio (raw f32 stereo,
  via ffmpeg) to PPM frames → ffmpeg → PNG/GIF/MP4. Used to verify every scene visually.
- **`tests/`** — unit tests for analyser, world, rasteriser, compose.

### Coordinate contract (scenes)

Scenes draw in **normalized units**: y ∈ [0,1] top→bottom, x ∈ [0, aspect]. Sizes and line widths
are in the same units. Scenes never touch pixel sizes, so the same scene state renders identically
(at different detail) in Ink (e.g. 1920×1080) and Pixel (e.g. 320×180) canvases, and style can be
switched live without resetting simulation.

### Materials (for Pixel palettes)

Each pixel carries `ink` (0–255 brightness) and `mat` (what it is): BG, EARTH, STEM, LEAF, BLOOM,
WATER, LIGHT, ACCENT. Ink/Mono-pixel ignore `mat`; Pixel maps (mat, ink) through a palette ramp.

### Shared controls on every source

Scene · Audio source · Style · Tint · Palette · Pixel size · Output width/height · Ink resolution scale ·
**Density / Speed / Reactivity** (3 macros every scene interprets) · Trails · Sensitivity ·
Bass/Mid/High gain · Link to shared world · World speed · Reset world · Seed.

## Builder's calls (made autonomously; user said "just make the coolest one")

- CPU rasterisation + CPU compose; GPU only scales/presents. Keeps OBS and the headless harness
  pixel-identical and makes every scene verifiable without OBS.
- 13 scenes listed above; seasons are a world layer.
- 3-macro contract (density/speed/reactivity), echoing the modkit convention.
- World steps once per video frame regardless of how many sources are live; each live scene adds
  its own contributions.
- macOS bundle built against `libobs.framework` inside `/Applications/OBS.app`, headers from the
  matching obs-studio tag (fetched into `deps/`, not committed).
