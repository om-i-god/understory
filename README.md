# Understory

Living, generative, nature-inspired visuals for OBS that listen to your music.

![All seventeen scenes, alternating Ink in colour and Pixel looks across the six palettes (Aurora shown in both)](docs/scenes.png)

Understory adds a video source to OBS. Point it at any audio (Desktop Audio, a mic, a media source)
and it grows a small world on screen that plays along with your music. It hears the kick, the snare and
the hi-hats separately, and each scene answers each one in its own way: the kick lands, the snare
breaks, the hats glint. Loud passages fill the world with light and life, quiet ones let it dim and
settle. Nothing ever freezes: in silence the world keeps breathing.

## Scenes

| Scene | Kick | Snare | Hats | Loudness |
|---|---|---|---|---|
| Meadow | every stem bows together, flower heads swell | flowers twitch and burst pollen, the dandelion lets its seeds go | dew glints on the grass tips | opens the flowers, brightens the meadow |
| Wind | a gust wave rolls across the field, the tree shudders | seeds and petals tear off, birds startle out of the grass | sparkle on the grass tips | sets the wind and burns off the cloud shadows |
| River | light surges down the current, a trout may leap | splashes and rings, the ducks startle | sun glitter on the ripples | brightens the water; quiet leaves it dark and glassy |
| Rain | a sheet of heavy rain sweeps the field, the puddles ring | spray crowns all over the meadow, the crow flaps, the frog leaps | the drizzle glints, drips fall from the wire | lightens and weighs down the sky, heavier rain |
| Storm Front | the cloud decks drop and heave, the rain gusts | lightning inside the clouds; built-up storms fork to the ground | rain glints and splashes | gathers and lowers the clouds; quiet breaks them up |
| Pond | a lily pad dips, a ring and a sweep of light cross the pond | the frog leaps with a splash, a dragonfly darts | sun glints, a water strider strokes | light on the water; quiet leaves it glassy |
| Fireflies | a flash wave rolls through the swarm, which falls into step | a cluster bursts into sparks | far fireflies twinkle on the treeline | how many glow and how brightly |
| Mycelium | the cords throb and nutrient pulses race out | the web shudders, mushrooms puff spores | growing tips glint, springtails hop | the whole network glows and grows |
| Murmuration | the flock flinches inward and rebounds | the ribbon tears and birds peel off | wing glints along the edges | the flock grows broader and brighter |
| Tide | a wave runs up the beach | spray bursts off the rocks, the lighthouse beam sweeps | the moon path glitters, foam fizzes | swell height and moonlight |
| Pollen & Seeds | a puff strips a ring of seeds from a clock | a gust shatters another clock and curls the seed stream | seed tips glint | how many seeds are in the air, and the light |
| Coral | a surge sways the reef, the fish wheel | the reef spawns, bubbles rise, the clownfish dart | glints dance across the caustic light | sunlight reaching down through the water |
| Aurora | light pulses along the curtain folds | a corona flare, the curtains stretch and fold | rays flicker, stars glint | curtain height and brightness |
| Snowfall | laden boughs shed their snow in a puff and spring back up | a gust whirls a flurry across the clearing; a strong one sends the owl off | flakes and snow crystals glint in the light | heavier snowfall, a warmer, wider glow from the cabin window |
| Dunes | a sand avalanche slides down a slip face, the dunes' light pulses | a gust tears spindrift off every crest; a strong one spins up a dust devil | sand grains glitter on the lit faces, stars flash | more sand streams off the crests, the Milky Way brightens |
| Deep Sea | every jellyfish bell contracts and they surge upward together | a shockwave of bioluminescent plankton bursts outward | comb-jelly rainbows ripple, marine snow sparkles | how much of the abyss lights up |
| Misty Valley | the waterfall surges and mist billows down the valley | the cranes lift off with big wingbeats, the old pine sheds needles | spray and dew glint, glitter runs down the river | the mist lifts to reveal the far peaks, the light warms |

Every place is inhabited, and its creatures answer the music too: a heron strikes on the snare in the marsh, pond and
river; a fox pounces into the snow; caribou lift their heads under the aurora; jerboas hop on the kick in the dunes;
frogs call in turn by the firefly pond; an octopus jets off the reef; a kingfisher dives; horses shy from the lightning.
Many follow the living world, with butterflies in summer, bats and owls at night, and bare stalks in winter.

Every scene also keeps its finer mappings (bass, melody, individual frequency bands, stereo width,
the slow swell of a phrase). In Ink looks the glow itself breathes with the music.

### Follow the music

Set **Scene changes → Follow the music** and Understory becomes a director: it learns the character
of your set (how loud, how bright, how percussive, relative to the set itself) and the state of the
living world, and moves to a fitting scene at musical turning points — a drop landing might bring the
murmuration or the storm, a breakdown opens onto the tide, the deep sea or aurora. A long storm charge makes
Storm Front more likely, a wet world favours the river or the misty valley, a summer night favours fireflies, winter brings the snowfall and a dry summer the dunes. Scenes
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

**Bloom** adds a soft two-scale light, in the colour of whatever is shining, around bright lines in
Ink: fireflies get halos, aurora and lightning glow. **Tint** colours Ink and Mono-pixel; **Palette** colours Pixel and Ink in colour. **Paper** inverts to dark ink on a light ground.
Style, tint and palette can change live without resetting the scene.

## Controls

- **Scene**, **Listen to** (any audio source).
- **Look:** style, tint, palette, pixel size, ink resolution, bloom, faint-line lift, paper, output size.
- **Nature:** density, speed, reactivity, trails, scene crossfade time, scene changes (stay / cycle in
  order / follow the music), minutes per scene, seed.
- **Listening:** sensitivity, bass / mids / highs gain. The analyser auto-levels, so quiet and loud
  mixes both work; bass gain also scales the kick, mids the snare, highs the hats.
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

To see how visibly a scene answers the music, make the test tracks once and run the reactivity report:

```sh
python3 tools/testaudio/gen_tracks.py out/tracks          # breakbeat, hip-hop, techno, ambient, piano (needs numpy)
python3 tools/reactivity.py storm --tracks out/tracks     # picture change after each kick / snare / hat, brightness vs loudness
```
See `docs/design.md` for the architecture and `docs/scene-guide.md` for writing new scenes.
