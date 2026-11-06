# Understory

Living, generative, nature-inspired visuals for OBS that listen to your music.

![All thirteen scenes, alternating Ink in colour and Pixel looks across the six palettes](docs/scenes.png)

Understory adds a video source to OBS. Point it at any audio (Desktop Audio, a mic, a media source)
and it grows a small world on screen: plants that bloom with the bass and sway in the highs, rivers
that run faster when the mids push, rain that falls on the drum hits, storms that charge through long
builds and break with lightning on the biggest hits. Nothing ever freezes — in silence the world keeps
breathing.

## Scenes

| Scene | What the music does |
|---|---|
| Meadow | bass grows stems and opens flowers; a row of daisies each opens to its own frequency; kicks roll gusts and burst seed heads; hats send bees darting; the swell flares the sun |
| Wind | kicks sweep gust fronts and bow the poplars one by one; each depth row of grass flutters to its own band; the melody lifts the kite and the swallows |
| River | kicks make trout leap; reed clumps nod along the bank to the melody; highs bring dancing mayflies and rises; big hits startle the sheep; width fans the ducklings out |
| Rain | kicks throw gusts and heavy eave drops; each puddle rings to its own band; hats splash off the fence posts; the swell warms the farmhouse windows |
| Storm Front | long loud passages charge the clouds; the biggest hits release lightning; sub rolls thunder along the cloud base; kicks push gust fronts through the oak; highs hang virga |
| Pond | every hit drops a ripple; kicks flick the koi; big hits make the frog leap; reeds and lotus each answer one band; width is the wind on the water |
| Fireflies | six colonies each flash on their own part of the spectrum; quiet brings a few; dense music synchronises the swarm; kicks ring the pool and shiver the web |
| Mycelium | bass lines spread the network; kicks send nutrient pulses and squash the caps; regions glow with the melody; hats make springtails leap; sub drives the mole |
| Murmuration | melody steers the flock; kicks ripple density waves through it; hats shimmer the wingbeats; width splits it in two; big transients scatter it |
| Tide | phrase-length swells roll waves in; kicks burst breakers on the rocks; mids shimmer the moon path; crabs scuttle to their own bands |
| Pollen & Seeds | highs lift seeds, sub-bass turns slow vortices; hits release the clocks; each clock fluffs to its band; hoverflies dart on the melody |
| Coral | low-mids feed a slowly creeping reaction-diffusion reef; kicks dart the fish; hats flinch the polyps; anemones open to the mids; snares puff bubbles |
| Aurora | stereo width and shimmer ripple the curtains; bass surges their light; kicks run pulses along them; each stretch of curtain answers a band; hits throw shooting stars |

In Ink looks the glow itself breathes with the music: kicks flare it, the phrase swell lifts it.

### Follow the music

Set **Scene changes → Follow the music** and Understory becomes a director: it learns the character
of your set (how loud, how bright, how percussive, relative to the set itself) and the state of the
living world, and moves to a fitting scene at musical turning points — a drop landing might bring the
murmuration or the storm, a breakdown opens onto the tide, coral or aurora. A long storm charge makes
Storm Front more likely, a wet world favours the river, a summer night favours fireflies. Scenes
dissolve into each other in organic patches, the new world spreading in like moss, and always stay at least the minutes you set, so each has time to grow.

### One living world

All Understory sources share a hidden ecosystem that remembers your set. A long storm swells the
river. A calm stretch breeds fireflies. Blooming meadows release pollen that feeds the reef and the
flock. Seasons turn with the music you play (about forty minutes of active music is a year): autumn
reddens and sheds leaves, winter turns rain to snow and strengthens the aurora. The world is saved
between OBS sessions; **Reset world** in the source settings starts fresh.

## Looks

- **Ink** — fine single-weight lines at full resolution.
- **Ink in colour** — the same fine lines, each part of nature in its palette hue: green stems and
  ferns, rose blooms, blue water, amber light.
- **Pixel** — low resolution, crisp pixels, six palettes (Moss, Dusk, Riverbed, Ember, Glacier, Lichen).
- **Mono-pixel** — pixel resolution, one colour, sixteen brightness levels.

**Bloom** adds a soft two-scale light around bright lines in Ink — fireflies get halos, aurora and
lightning glow. **Tint** colours Ink and Mono-pixel; **Palette** colours Pixel and Ink in colour. **Paper** inverts to dark ink on a light ground.
Style, tint and palette can change live without resetting the scene.

## Controls

- **Scene**, **Listen to** (any audio source).
- **Look:** style, tint, palette, pixel size, ink resolution, bloom, faint-line lift, paper, output size.
- **Nature:** density, speed, reactivity, trails, scene crossfade time, scene changes (stay / cycle in
  order / follow the music), minutes per scene, seed.
- **Listening:** sensitivity, bass / mids / highs gain. The analyser auto-levels, so quiet and loud mixes both work.
- **World:** share the living world (on by default), world speed, reset world.
- **Hotkeys** (Settings → Hotkeys): next scene, previous scene, cycle style.

## Performance

Each Understory source draws on its own worker thread; OBS's video thread only uploads finished
frames, so a busy moment in a scene never stalls your stream. Ink draws at 0.67× the output size by
default (a 1080p source draws at ~720p and upscales smoothly); raise **Ink resolution** for finer lines.

Ready-made downloads for macOS and Windows are on the
[Releases page](https://github.com/om-i-god/understory/releases/latest).

## Install (macOS, Apple Silicon, macOS 13+, OBS 32)

From a release: unzip `understory-macos-arm64.zip` and move `understory.plugin` into
`~/Library/Application Support/obs-studio/plugins/`. The plugin isn't notarized, so clear the download flag once:

```sh
xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/understory.plugin
```

Or build it from source:

```sh
sh tools/install.sh        # fetches headers, builds, copies understory.plugin into
                           # ~/Library/Application Support/obs-studio/plugins/
```

Restart OBS, then **Sources → + → Understory (living nature)**.

## Install (Windows x64, OBS 32)

Download `understory-windows-x64.zip` from the
[latest release](https://github.com/om-i-god/understory/releases/latest), unzip it, then in
an administrator PowerShell in that folder:

```powershell
powershell -ExecutionPolicy Bypass -File install.ps1   # copies understory\ into C:\ProgramData\obs-studio\plugins\
```

Or copy the `understory` folder into `C:\ProgramData\obs-studio\plugins\` by hand. Restart OBS, then
**Sources → + → Understory (living nature)**.

Every push builds the Windows plugin with MSVC on GitHub Actions, runs the core tests, renders every
scene in every look, and loads the DLL into the real OBS for Windows headless (checking frames, lag and
the saved world). To build it yourself: Visual Studio 2022, `sh tools/fetch-deps.sh` (Git Bash), then
in a Developer PowerShell
`tools/make-obs-lib.ps1 -Dll "C:\Program Files\obs-studio\bin\64bit\obs.dll" -Out deps\obs.lib` and
`cmake -S . -B build -A x64 -DOBS_LIB="$PWD\deps\obs.lib"; cmake --build build --config Release` — the
plugin is staged in `build\stage\understory`.

## Develop

```sh
sh tools/fetch-deps.sh                                   # OBS headers + SIMDe into deps/
cmake -S . -B build -DUNDERSTORY_BUILD_PLUGIN=OFF && cmake --build build -j
./build/us_tests                                          # core + every scene × every style
./build/us_render --scene storm --synth --style ink --prewarm 40 --start 40 --seconds 10 --every 3 --out out/storm
./build-obs/obs_host_test build-obs/understory.plugin out/host meadow 4   # loads into OBS.app's libobs headless
US_BIG=1 ./build-obs/obs_host_test build-obs/understory.plugin out/host storm 5  # 1080p: fails if OBS lags
```

`us_render` renders any scene with real audio (`ffmpeg -i song.wav -f f32le -ac 2 -ar 48000 song.f32`
then `--audio song.f32`) or a built-in test track (`--journey 30` runs the follow-the-music director with a 30 s minimum), to frames you can turn into GIFs or video.
See `docs/design.md` for the architecture and `docs/scene-guide.md` for writing new scenes.
