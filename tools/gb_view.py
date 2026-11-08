"""gb_view.py - wall textures, world sprites, scalers and per level palettes for the 3-D view.

Wall texture format (one 16 KB ROM bank per texture, mapped at 0x4000):
    for u in 0..63: a 256 byte page at 0x4000 + u*256
        +0x00 + 2*t : plane 0, plane 1 of texel row t for a LEFT ray  (4 pixel strip in the high nibble)
        +0x80 + 2*t : the same for a RIGHT ray (low nibble)
    texel value 1..3 (value 0 is the ceiling / floor colour of the palette, never used by a wall)
Sprite format: columns x0..x1 of 64 bytes (row t = value 0 transparent, 1..3 shade).
Scalers: for a projected height h the table lists 2*t (texture row *2) for each visible screen row.
"""
import numpy as np
import gbdata as G
import wolfdata

VIEW_W, VIEW_H, VIEW_TW, VIEW_TH = 160, 96, 20, 12
NRAYS = 40
RAYW = VIEW_W // NRAYS
NUM_WALLPAGES = 56
DOORWALL = 98
TEX_PAGES = list(range(NUM_WALLPAGES)) + list(range(DOORWALL, DOORWALL + 8))
NFAM = 6
WEAPON_SPRITES = list(range(416, 436))
NPOOL = 12


def page_tex(p):
    if p < NUM_WALLPAGES:
        return p
    if DOORWALL <= p < DOORWALL + 8:
        return NUM_WALLPAGES + (p - DOORWALL)
    return 0xFF


def wall_rgb(p):
    d = G.data()
    cols = d.wall(p)
    pal = np.array(d.pal8, dtype=np.uint8)
    return pal[np.array(cols, dtype=np.uint8)]       # [u][t] -> rgb


def pair_levels():
    """for each even page: (thresholds t1, t2) from the light+dark pair's luminance; returns dict
    pages -> level array [u][t] (1..3)"""
    out = {}
    for k in range(NUM_WALLPAGES // 2):
        a, b = wall_rgb(2 * k), wall_rgb(2 * k + 1)
        la, lb = G.luma(a), G.luma(b)
        both = np.concatenate([la.reshape(-1), lb.reshape(-1)])
        cent, _ = G.kmeans(both.reshape(-1, 1), 3, seed=3)
        c = sorted(cent.reshape(-1))
        t1, t2 = (c[0] + c[1]) / 2, (c[1] + c[2]) / 2
        for p, l in ((2 * k, la), (2 * k + 1, lb)):
            lv = np.ones(l.shape, np.uint8)
            lv[l >= t1] = 2
            lv[l >= t2] = 3
            out[p] = lv
    # door pages: 98 normal(+99), 100 frame(+101), 102 elevator(+103), 104 lock(+105) are pairs as well
    for k in range(4):
        a, b = wall_rgb(DOORWALL + 2 * k), wall_rgb(DOORWALL + 2 * k + 1)
        la, lb = G.luma(a), G.luma(b)
        both = np.concatenate([la.reshape(-1), lb.reshape(-1)])
        cent, _ = G.kmeans(both.reshape(-1, 1), 3, seed=3)
        c = sorted(cent.reshape(-1))
        t1, t2 = (c[0] + c[1]) / 2, (c[1] + c[2]) / 2
        for p, l in ((DOORWALL + 2 * k, la), (DOORWALL + 2 * k + 1, lb)):
            lv = np.ones(l.shape, np.uint8)
            lv[l >= t1] = 2
            lv[l >= t2] = 3
            out[p] = lv
    return out


def texture_blob(lv):
    """lv[u][t] in 1..3 -> 16 KB bank image"""
    buf = bytearray(16384)
    for u in range(64):
        base = u * 256
        for t in range(64):
            v = int(lv[u][t])
            buf[base + 2 * t] = 0xF0 if v & 1 else 0
            buf[base + 2 * t + 1] = 0xF0 if v & 2 else 0
            buf[base + 128 + 2 * t] = 0x0F if v & 1 else 0
            buf[base + 128 + 2 * t + 1] = 0x0F if v & 2 else 0
    return bytes(buf)


def wall_tabs():
    xs, ys = bytearray([0xFF] * 64), bytearray([0xFF] * 64)
    for n in range(1, 64):
        hp = 2 * (n - 1)
        xs[n] = page_tex(hp + 1)
        ys[n] = page_tex(hp)
    return bytes(xs), bytes(ys)


def level_pages(m):
    """textures (indexes) a level can show"""
    xs, ys = wall_tabs()
    used = set()
    for v in m['planes'][0]:
        if 0 < v < 64:
            for t in (xs[v], ys[v]):
                if t != 0xFF:
                    used.add(t)
    for k in range(4):                                  # the doors (types differ per level: take all that occur)
        pass
    for v in m['planes'][0]:
        if 90 <= v <= 101:
            used.add(page_tex(DOORWALL + 0)); used.add(page_tex(DOORWALL + 1))
            used.add(page_tex(DOORWALL + 2)); used.add(page_tex(DOORWALL + 3))
            if v in (92, 93, 94, 95, 96, 97, 98, 99, 100, 101):
                used.add(page_tex(DOORWALL + 4)); used.add(page_tex(DOORWALL + 5))
                used.add(page_tex(DOORWALL + 6)); used.add(page_tex(DOORWALL + 7))
    return sorted(used)


def family_palettes(levels, pages):
    """cluster the level's textures into NFAM families by mean colour, derive their 3 colour ramps.
    Returns (fam[ntex] (0..NFAM-1, default 0), pal[NFAM][3] rgb555)"""
    ntex = len(TEX_PAGES)
    # texture means (weighted by the light page)
    mean = {}
    for ti in pages:
        p = TEX_PAGES[ti]
        mean[ti] = wall_rgb(p).reshape(-1, 3).mean(0)
    # unify pairs: a pair shares a family, so cluster by pair
    pairs = sorted(set(ti & ~1 for ti in pages))
    pm = []
    for pr in pairs:
        ms = [mean[t] for t in (pr, pr + 1) if t in mean]
        pm.append(np.mean(ms, axis=0))
    fam = [0] * ntex
    k = min(NFAM, len(pairs))
    cent, lb = G.kmeans(G.lab(np.array(pm)), k, seed=5)
    for pr, l in zip(pairs, lb):
        fam[pr] = int(l)
        if pr + 1 < ntex:
            fam[pr + 1] = int(l)
    pal = []
    for f in range(NFAM):
        members = [ti for ti in pages if fam[ti] == f]
        if not members:
            pal.append([G.rgb555((40, 40, 40)), G.rgb555((100, 100, 100)), G.rgb555((200, 200, 200))])
            continue
        cols = {1: [], 2: [], 3: []}
        for ti in members:
            p = TEX_PAGES[ti]
            rgb = wall_rgb(p).reshape(-1, 3)
            lv = levels[p].reshape(-1)
            for v in (1, 2, 3):
                sel = rgb[lv == v]
                if len(sel):
                    cols[v].append(sel)
        ramp = []
        for v in (1, 2, 3):
            if cols[v]:
                c = np.concatenate(cols[v]).mean(0)
            else:
                c = ramp[-1] if ramp else np.array([30, 30, 30])
            ramp.append(np.array(c, dtype=np.float64))
        # keep the ramp ordered by luminance
        ramp = sorted(ramp, key=lambda c: G.luma(c))
        pal.append([G.rgb555(c) for c in ramp])
    return fam, pal


# ------------------------------------------------------------------ sprites
def sprite_levels(s):
    """sprite page s -> (x0, x1, array[x][t] 0..3, colours [3 rgb] ) or None"""
    d = G.data()
    sp = d.sprite(s)
    if sp is None:
        return None
    pal = np.array(d.pal8, dtype=np.uint8)
    vals = []
    for x in range(64):
        for t in range(64):
            if sp[x][t] is not None:
                vals.append(sp[x][t])
    if not vals:
        return None
    rgb = pal[np.array(vals)]
    lum = G.luma(rgb)
    if len(set(lum.round().tolist())) >= 3:
        cent, _ = G.kmeans(lum.reshape(-1, 1), 3, seed=2)
        c = sorted(cent.reshape(-1))
        t1, t2 = (c[0] + c[1]) / 2, (c[1] + c[2]) / 2
    else:
        t1 = t2 = 1e9
    lv = np.zeros((64, 64), np.uint8)
    cols = {1: [], 2: [], 3: []}
    for x in range(64):
        for t in range(64):
            v = sp[x][t]
            if v is None:
                continue
            l = G.luma(pal[v])
            k = 1 if l < t1 else (2 if l < t2 else 3)
            lv[x][t] = k
            cols[k].append(pal[v])
    ramp = []
    for k in (1, 2, 3):
        if cols[k]:
            ramp.append(np.mean(cols[k], axis=0))
        else:
            ramp.append(ramp[-1] if ramp else np.array([60, 60, 60.0]))
    ramp = sorted(ramp, key=lambda c: G.luma(c))
    nz = np.where(lv.any(axis=1))[0]
    x0, x1 = int(nz[0]), int(nz[-1])
    return x0, x1, lv, [G.rgb555(c) for c in ramp]


def build_sprites(pk):
    """pack all world sprites; returns tables dict"""
    d = G.data()
    n = d.num_sprites()
    bank = [0] * n
    off = [0] * n
    x0s = [0] * n
    x1s = [0] * n
    t0s = [0] * n
    t1s = [63] * n
    ramps = [None] * n
    blobs = {}
    for s in range(n):
        if s in WEAPON_SPRITES:
            continue
        r = sprite_levels(s)
        if r is None:
            continue
        x0, x1, lv, ramp = r
        blob = bytearray()
        for x in range(x0, x1 + 1):
            blob += bytes(int(v) for v in lv[x])
        blobs[s] = bytes(blob)
        x0s[s], x1s[s], ramps[s] = x0, x1, ramp
        rows = np.where(lv.any(axis=0))[0]          # the texel rows with something drawn
        t0s[s], t1s[s] = int(rows[0]), int(rows[-1])
    # pool the colours: 12 sprite palettes by Lab k-means over the ramps
    ids = sorted(blobs)
    vec = np.array([np.concatenate([G.lab(np.array(G.from555(c))) for c in ramps[s]]) for s in ids])
    cent, lb = G.kmeans(vec, NPOOL, seed=7)
    pool = [None] * NPOOL
    for j in range(NPOOL):
        mem = [ids[i] for i in range(len(ids)) if lb[i] == j]
        if not mem:
            pool[j] = [G.rgb555((50, 50, 50))] * 3
            continue
        cs = np.mean([[np.array(G.from555(c)) for c in ramps[s]] for s in mem], axis=0)
        pool[j] = [G.rgb555(c) for c in cs]
    poolof = [0] * n
    for i, s in enumerate(ids):
        poolof[s] = int(lb[i])
    # pack, big ones first for a tight fit
    for s in sorted(ids, key=lambda s: -len(blobs[s])):
        b, a = pk.add('spr%d' % s, blobs[s])
        bank[s], off[s] = b, a
    return dict(bank=bank, off=off, x0=x0s, x1=x1s, t0=t0s, t1=t1s, pool=poolof, poolpal=pool, count=len(ids))


# ------------------------------------------------------------------ scalers
MAXH_SMALL = 96
TALL_STEP = 8
TALL_MAX = 256


def build_scalers():
    """returns (blob bytes for bank 0, header dict).  Tables for h = 1..96 then tall tables for
    h = 104, 112, ..., 256 (96 rows each, the window centred).  Entries are 2*t."""
    blob = bytearray()
    small_off = [0] * (MAXH_SMALL + 1)
    for h in range(1, MAXH_SMALL + 1):
        small_off[h] = len(blob)
        for i in range(h):
            blob.append(2 * ((i * 64) // h))
        blob.append(0xFF)                   # end of the table
    tall_off = []
    hs = list(range(MAXH_SMALL + TALL_STEP, TALL_MAX + 1, TALL_STEP))
    for h in hs:
        tall_off.append(len(blob))
        start = (h - VIEW_H) // 2
        for i in range(VIEW_H):
            blob.append(2 * (((start + i) * 64) // h))
        blob.append(0xFF)
    return bytes(blob), small_off, tall_off, hs
