"""Contrasting test tracks for Understory (48 kHz stereo float32 raw), drums deliberately decoupled.

  python3 tools/testaudio/gen_tracks.py [out/tracks]   (needs numpy)
"""
import numpy as np, sys, os

SR = 48000
rng = np.random.default_rng(3)
out = sys.argv[1] if len(sys.argv) > 1 else "out/tracks"
os.makedirs(out, exist_ok=True)


def env_exp(n, k):
    return np.exp(-np.arange(n) / SR * k)


def kick(n=int(0.35 * SR), f0=50, f1=160):
    t = np.arange(n) / SR
    f = f0 + (f1 - f0) * np.exp(-t * 35)
    ph = 2 * np.pi * np.cumsum(f) / SR
    return np.sin(ph) * env_exp(n, 9)


def snare(n=int(0.25 * SR)):
    t = np.arange(n) / SR
    nz = rng.uniform(-1, 1, n)
    return (0.7 * nz * env_exp(n, 18) + 0.4 * np.sin(2 * np.pi * 190 * t) * env_exp(n, 30))


def hat(n=int(0.06 * SR), open_=False):
    nz = rng.uniform(-1, 1, n if not open_ else int(0.3 * SR))
    nz = nz - np.convolve(nz, np.ones(4) / 4, 'same')
    return nz * env_exp(len(nz), 25 if open_ else 80)


def place(buf, s, t, g=1.0, pan=0.0):
    i = int(t * SR)
    if i >= buf.shape[1]:
        return
    j = min(buf.shape[1], i + len(s))
    buf[0, i:j] += s[: j - i] * g * (1 - pan) * 0.9
    buf[1, i:j] += s[: j - i] * g * (1 + pan) * 0.9


def tone(f, n, kind='saw', decay=3.0, sr=SR):
    t = np.arange(n) / sr
    if kind == 'sin':
        x = np.sin(2 * np.pi * f * t)
    elif kind == 'saw':
        x = 2 * ((f * t) % 1) - 1
    elif kind == 'tri':
        x = 2 * np.abs(2 * ((f * t) % 1) - 1) - 1
    else:
        x = np.sin(2 * np.pi * f * t) + 0.5 * np.sin(4 * np.pi * f * t) * np.exp(-t * 6)
    return x * np.exp(-t * decay)


def lp(x, a):
    y = np.empty_like(x); s = 0.0
    for i in range(len(x)):
        s += a * (x[i] - s); y[i] = s
    return y


def mtof(m):
    return 440 * 2 ** ((m - 69) / 12)


def save(name, buf):
    buf = buf / max(1e-6, np.abs(buf).max()) * 0.85
    inter = np.empty(buf.shape[1] * 2, np.float32)
    inter[0::2] = buf[0]; inter[1::2] = buf[1]
    inter.tofile(os.path.join(out, name + '.f32'))
    print(name, buf.shape[1] / SR, 's')


def pad(buf, chords, bar, amp, start=0, end=None):
    end = end or buf.shape[1] / SR
    t = start; ci = 0
    while t < end:
        n = int(bar * SR)
        for k, m in enumerate(chords[ci % len(chords)]):
            x = tone(mtof(m), n, 'tri', 0.2)
            x *= np.sin(np.linspace(0, np.pi, n)) ** 0.7
            place(buf, x, t, amp, pan=(k - 1) * 0.5)
        t += bar; ci += 1


# 1. breakbeat 172 bpm: syncopated kick/snare, independent 16th hats, rolling sub
def breaks():
    L = 64; buf = np.zeros((2, L * SR)); b = 60 / 172
    pat_k = [0, 2.5, 4 + 1.75, 4 + 2.5]       # in beats within 2 bars (8 beats)
    pat_s = [1, 3, 5, 7, 7.75]
    for bar2 in range(int(L / (8 * b))):
        t0 = bar2 * 8 * b
        sec = int(t0 / 16)
        if sec == 2:  # breakdown: no drums
            continue
        for k in pat_k: place(buf, kick(), t0 + k * b, 0.9)
        for s in pat_s: place(buf, snare(), t0 + s * b, 0.55)
        for h in range(32):
            if rng.random() < 0.8: place(buf, hat(), t0 + h * b / 4, 0.12 + 0.1 * (h % 2), pan=0.4)
    # sub bass
    notes = [38, 38, 41, 36]
    for bar in range(int(L / (4 * b))):
        n = int(4 * b * SR); f = mtof(notes[bar % 4])
        place(buf, tone(f, n, 'sin', 0.5), bar * 4 * b, 0.5)
    pad(buf, [[62, 65, 69], [60, 64, 67], [57, 60, 64]], 4 * 4 * b, 0.06)
    save('breaks', buf)


# 2. swung boom-bap 88 bpm, rhodes chords, bass
def hiphop():
    L = 64; buf = np.zeros((2, L * SR)); b = 60 / 88
    for bar in range(int(L / (4 * b))):
        t0 = bar * 4 * b
        for k in [0, 1.75, 2.5]: place(buf, kick(f0=45, f1=120), t0 + k * b, 0.95)
        for s in [1, 3]: place(buf, snare(), t0 + s * b, 0.6)
        for h in range(8):
            sw = 0.16 if h % 2 else 0
            place(buf, hat(), t0 + (h / 2 + sw) * b, 0.15, pan=-0.3)
        if bar % 4 == 3: place(buf, hat(open_=True), t0 + 3.5 * b, 0.12, pan=0.3)
        ch = [[57, 60, 64, 67], [53, 57, 60, 64], [55, 59, 62, 65], [52, 55, 59, 62]][bar % 4]
        for k, m in enumerate(ch):
            place(buf, tone(mtof(m), int(2 * b * SR), 'rh', 1.2), t0 + 0.02 * k, 0.12, pan=(k - 1.5) * 0.3)
        place(buf, tone(mtof(ch[0] - 24), int(1.5 * b * SR), 'sin', 1.0), t0, 0.5)
        place(buf, tone(mtof(ch[0] - 24), int(1.0 * b * SR), 'sin', 1.5), t0 + 2.5 * b, 0.4)
    save('hiphop', buf)


# 3. ambient: no beat at all, slow swelling chords + sparse bell melody, quiet-to-loud arc
def ambient():
    L = 64; buf = np.zeros((2, L * SR))
    pad(buf, [[50, 57, 62, 66], [47, 54, 59, 62], [45, 52, 57, 61], [43, 50, 55, 59]], 8.0, 0.25)
    arc = np.clip(np.sin(np.linspace(0, np.pi, buf.shape[1])) * 1.3, 0.15, 1)
    buf *= arc
    t = 2.0
    mel = [74, 76, 78, 81, 78, 76, 73, 71]
    i = 0
    while t < L - 2:
        place(buf, tone(mtof(mel[i % len(mel)]), int(2.5 * SR), 'sin', 1.5), t, 0.18, pan=rng.uniform(-0.7, 0.7))
        t += rng.choice([1.5, 2.0, 3.0, 4.5]); i += 1
    save('ambient', buf)


# 4. techno 128: four-on-floor, offbeat hats, acid line; build, drop, 2 s silence, breakdown
def techno():
    L = 72; buf = np.zeros((2, L * SR)); b = 60 / 128
    acid = [36, 36, 48, 36, 39, 36, 43, 36, 36, 46, 36, 41, 36, 48, 39, 36]
    for beat in range(int(L / b)):
        t = beat * b
        if 46 <= t < 48:  # hard silence gap
            continue
        sec = 0 if t < 16 else 1 if t < 46 else 2  # intro, drop, breakdown
        if sec != 2: place(buf, kick(f0=48, f1=150), t, 1.0)
        if sec >= 1 or t > 8: place(buf, hat(), t + b / 2, 0.22, pan=0.2)
        if sec == 1 and beat % 2 == 1: place(buf, snare(), t, 0.25)
        for s16 in range(4):
            if sec == 0 and t < 8:
                break
            m = acid[(beat * 4 + s16) % 16]
            n = int(b / 4 * SR)
            cut = 0.05 + 0.25 * (0.5 + 0.5 * np.sin(t * 0.3))
            x = lp(tone(mtof(m), n, 'saw', 8), cut)
            if t < 46: place(buf, x, t + s16 * b / 4, 0.35)
    pad(buf, [[60, 63, 67], [58, 62, 65]], 8 * b * 4, 0.12, start=48)
    save('techno', buf)


# 5. piano arpeggios across the whole keyboard, dynamics pp->ff->pp, no drums
def arps():
    L = 60; buf = np.zeros((2, L * SR))
    t = 0.0; step = 0.125; i = 0
    roots = [36, 41, 43, 39]
    while t < L - 1:
        bar = int(t / 4)
        r = roots[bar % 4]
        octs = 1 + int(3 * (0.5 - 0.5 * np.cos(2 * np.pi * t / 30)))
        seq = [r + 12 * o + iv for o in range(octs + 1) for iv in (0, 7, 12, 16)]
        m = seq[i % len(seq)]
        dyn = 0.15 + 0.85 * np.sin(np.pi * t / L)
        place(buf, tone(mtof(m), int(1.0 * SR), 'rh', 4), t, 0.3 * dyn, pan=(m - 60) / 40)
        t += step; i += 1
    save('arps', buf)


for f in [breaks, hiphop, ambient, techno, arps]:
    f()
