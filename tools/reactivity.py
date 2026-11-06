"""Reactivity report: how visibly a scene answers each part of the music.

  python3 tools/reactivity.py <scene> [--style color] [--tracks DIR] [--bin build/us_render] [--seconds 50]

Renders the scene against the test tracks (tools/testaudio/gen_tracks.py makes them; field.f32 is optional)
with us_render --measure, then reports, per track:
  kickX / snareX / hatX  mean picture change in the 3 frames after that drum / the typical frame change.
                         1.0 = the drum is invisible; 2+ reads clearly; 3+ is a strong, obvious answer.
  rLvl                   correlation of overall brightness with loudness (should be clearly positive on
                         ambient / arps: louder passages glow brighter, quiet ones sink).
  gapCalm                frame change during the techno track's 2 s of silence / typical (should be < 1).
  dMed                   typical per-frame change (0..255 luma); ~0.5-4 is alive without being frantic.
"""
import csv, os, subprocess, sys, argparse
import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument('scene')
ap.add_argument('--style', default='color')
ap.add_argument('--tracks', default=os.environ.get('US_TRACKS', 'out/tracks'))
ap.add_argument('--bin', default='build/us_render')
ap.add_argument('--seconds', type=float, default=50)
ap.add_argument('--out', default='out/react')
ap.add_argument('--size', default='640x360')
a = ap.parse_args()

tracks = [t for t in ['breaks', 'hiphop', 'techno', 'ambient', 'arps', 'field']
          if os.path.exists(os.path.join(a.tracks, t + '.f32'))]
os.makedirs(a.out, exist_ok=True)
procs = []
for t in tracks:
    csvp = os.path.join(a.out, f'{a.scene}_{a.style}_{t}.csv')
    procs.append((t, csvp, subprocess.Popen(
        [a.bin, '--scene', a.scene, '--style', a.style, '--audio', os.path.join(a.tracks, t + '.f32'),
         '--start', '8', '--seconds', str(a.seconds), '--size', a.size, '--every', '1000000',
         '--out', os.path.join(a.out, 'frames'), '--measure', csvp], stdout=subprocess.DEVNULL)))
for t, csvp, p in procs:
    p.wait()


def load(p):
    rows = list(csv.DictReader(open(p)))
    return {k: np.array([float(r[k]) for r in rows]) for k in rows[0]}


def corr(x, y):
    return float(np.corrcoef(x, y)[0, 1]) if x.std() > 1e-9 and y.std() > 1e-9 else 0.0


def resp(delta, hit):
    idx = np.where(hit > 0)[0]
    if len(idx) < 3:
        return float('nan')
    w = np.concatenate([idx + k for k in range(3)])
    w = w[w < len(delta)]
    return float(delta[w].mean() / (np.median(delta) + 0.05))


print(f"{a.scene} ({a.style})")
print(f"{'track':8s} {'kickX':>6s} {'snareX':>6s} {'hatX':>6s} {'rLvl':>6s} {'gapCalm':>7s} {'dMed':>6s}")
for t, csvp, _ in procs:
    m = load(csvp)
    d = m['delta']
    gap = ''
    if t == 'techno':
        g = (m['t'] > 46.3) & (m['t'] < 47.9)
        gap = f"{d[g].mean() / (np.median(d) + 0.05):7.2f}" if g.any() else ''
    print(f"{t:8s} {resp(d, m['kick_hit']):6.2f} {resp(d, m['snare_hit']):6.2f} {resp(d, m['hat_hit']):6.2f} "
          f"{corr(m['luma'], m['level']):6.2f} {gap:>7s} {np.median(d):6.2f}")
