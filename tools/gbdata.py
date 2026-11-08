"""gbdata.py - shared loaders and colour helpers for the Game Boy Color converters.

Reads the shareware v1.4 data (see paths.py), offers pictures, fonts, sprites, walls and the colour
math (RGB555, Lab distance, k-means palette fitting, tile quantisation with per-tile palettes).
"""
import struct
import numpy as np
import wolfdata, wl_names, paths

_c = {}


def data():
    if 'd' not in _c:
        _c['d'] = wolfdata.Data(paths.DATA, paths.SRC)
    return _c['d']


# ------------------------------------------------------------------ colours
def rgb555(rgb):
    r, g, b = [int(v) for v in rgb]
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def from555(c):
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return ((r * 255 + 15) // 31, (g * 255 + 15) // 31, (b * 255 + 15) // 31)


def snap555(rgb):
    return from555(rgb555(rgb))


def _lin(c):
    c = np.asarray(c, dtype=np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lab(rgb):
    """rgb array (..., 3) 0..255 -> Lab"""
    rgb = np.asarray(rgb, dtype=np.float64)
    l = _lin(rgb)
    x = l[..., 0] * 0.4124 + l[..., 1] * 0.3576 + l[..., 2] * 0.1805
    y = l[..., 0] * 0.2126 + l[..., 1] * 0.7152 + l[..., 2] * 0.0722
    z = l[..., 0] * 0.0193 + l[..., 1] * 0.1192 + l[..., 2] * 0.9505
    x /= 0.95047
    z /= 1.08883

    def f(t):
        return np.where(t > 0.008856, np.cbrt(t), 7.787 * t + 16.0 / 116)
    fx, fy, fz = f(x), f(y), f(z)
    return np.stack([116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)], axis=-1)


def luma(rgb):
    rgb = np.asarray(rgb, dtype=np.float64)
    return rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114


# ----------------------------------------------------------------- pictures
def pic_table():
    if 'pt' not in _c:
        _c['pt'] = data().vga_chunk(0)
    return _c['pt']


def pic_index(name):
    if 'names' not in _c:
        _c['names'] = dict(wl_names.gfx_names())
    return _c['names'][name]


def pic256(name):
    d = data()
    c = pic_index(name)
    w, h = struct.unpack_from('<HH', pic_table(), (c - 3) * 4)
    raw = d.vga_chunk(c)
    assert len(raw) == w * h, (name, w, h, len(raw))
    return np.array(wolfdata.deplane(raw, w, h), dtype=np.uint8)


def pic_rgb(name):
    return np.array(data().pal8, dtype=np.uint8)[pic256(name)]


class Font:
    def __init__(self, chunk):
        f = data().vga_chunk(chunk)
        self.height = struct.unpack_from('<H', f, 0)[0]
        self.loc = struct.unpack_from('<256H', f, 2)
        self.wid = f[2 + 512:2 + 512 + 256]
        self.f = f

    def glyph(self, c):
        w = self.wid[c]
        o = self.loc[c]
        a = np.zeros((self.height, w), bool)
        for y in range(self.height):
            for x in range(w):
                a[y, x] = self.f[o + y * w + x] != 0
        return a


def font(n):
    if ('font', n) not in _c:
        _c[('font', n)] = Font(1 + n)
    return _c[('font', n)]


def cell_glyph(c):
    """8x8 cell of the original small font (rows 1..8, widest columns merged when > 8 wide)"""
    g = font(0).glyph(c)[1:9]
    w = g.shape[1]
    if w == 0:
        return np.zeros((8, 8), bool)
    cols = [x for x in range(w) if g[:, x].any()]
    last = max(cols) if cols else -1
    g = g[:, :last + 1]
    while g.shape[1] > 8:
        best = None
        for x in range(1, g.shape[1] - 1):
            cost = min(int((g[:, x] != g[:, x - 1]).sum()), int((g[:, x] != g[:, x + 1]).sum()))
            if best is None or cost < best[0]:
                best = (cost, x)
        g = np.delete(g, best[1], axis=1)
    out = np.zeros((8, 8), bool)
    out[:, :g.shape[1]] = g
    return out


# ------------------------------------------------------------ tile helpers
def tile_bytes(idx8):
    """8x8 array of 2 bit values -> 16 bytes (GB planar)"""
    out = bytearray()
    for y in range(8):
        lo = hi = 0
        for x in range(8):
            v = int(idx8[y, x])
            lo |= (v & 1) << (7 - x)
            hi |= ((v >> 1) & 1) << (7 - x)
        out += bytes([lo, hi])
    return bytes(out)


def resize_rgb(img, w, h):
    """area-average resize of an RGB uint8 array, using PIL (box/lanczos by direction)"""
    from PIL import Image
    im = Image.fromarray(np.asarray(img, dtype=np.uint8))
    return np.array(im.resize((w, h), Image.LANCZOS if (w > im.width or h > im.height) else Image.BOX))


def kmeans(points, k, iters=30, seed=1, weights=None):
    """plain k-means on (n, d) float points; returns (centers, labels)"""
    pts = np.asarray(points, dtype=np.float64)
    n = len(pts)
    rng = np.random.RandomState(seed)
    k = max(1, min(k, n))
    # k-means++ seeding
    cent = [pts[rng.randint(n)]]
    for _ in range(1, k):
        d2 = np.min([((pts - c) ** 2).sum(1) for c in cent], axis=0)
        s = d2.sum()
        if s <= 0:
            cent.append(pts[rng.randint(n)])
        else:
            cent.append(pts[rng.choice(n, p=d2 / s)])
    cent = np.array(cent)
    w = np.ones(n) if weights is None else np.asarray(weights, dtype=np.float64)
    lab_ = np.zeros(n, int)
    for _ in range(iters):
        d = ((pts[:, None, :] - cent[None, :, :]) ** 2).sum(2)
        nl = d.argmin(1)
        if _ and (nl == lab_).all():
            break
        lab_ = nl
        for j in range(k):
            m = lab_ == j
            if m.any():
                cent[j] = (pts[m] * w[m, None]).sum(0) / w[m].sum()
            else:
                cent[j] = pts[rng.randint(n)]
    return cent, lab_


def fit_palette(pixels_rgb, k=4, weights=None, seed=1):
    """k representative colours (RGB555 snapped) for an (n,3) array, sorted dark to light"""
    pts = lab(pixels_rgb)
    uniq, inv = np.unique(np.asarray(pixels_rgb).reshape(-1, 3), axis=0, return_inverse=True)
    cnt = np.bincount(inv.reshape(-1), minlength=len(uniq)).astype(np.float64)
    if len(uniq) <= k:
        cols = [tuple(int(v) for v in u) for u in uniq]
    else:
        cent, lb = kmeans(lab(uniq), k, weights=cnt, seed=seed)
        cols = []
        for j in range(k):
            m = lb == j
            if m.any():
                c = (uniq[m] * cnt[m, None]).sum(0) / cnt[m].sum()
                cols.append(tuple(int(round(v)) for v in c))
    cols = sorted(set(rgb555(c) for c in cols), key=lambda c: luma(from555(c)))
    return cols


def quantize_tiles(img, npal=8, ncol=4, first_fixed=None, tw=None, th=None, max_iter=12, seed=1):
    """Quantise an RGB image (h, w multiples of 8) into 8x8 tiles, each with one of `npal` palettes of
    `ncol` colours (RGB555).  Returns dict(pal=[[rgb555 x ncol]]*npal, tiles=(ty,tx,8,8) idx array, tpal=(ty,tx)).
    Palettes are found by clustering the tiles' own best colour sets (greedy merge of colour sets by Lab error)."""
    img = np.asarray(img, dtype=np.uint8)
    h, w = img.shape[:2]
    ty, tx = h // 8, w // 8
    labimg = lab(img.reshape(-1, 3)).reshape(h, w, 3)
    # per tile: its own best ncol palette
    tiles = []
    for j in range(ty):
        for i in range(tx):
            px = img[j * 8:j * 8 + 8, i * 8:i * 8 + 8].reshape(-1, 3)
            tiles.append(px)
    # candidate palettes: per tile, then merged into npal by k-means over concatenated palette descriptors
    tp = [fit_palette(px, ncol, seed=seed) for px in tiles]
    desc = []
    for p in tp:
        p = list(p) + [p[-1]] * (ncol - len(p))
        desc.append(np.concatenate([lab(np.array(from555(c)))[None, :].reshape(-1) for c in p]))
    desc = np.array(desc)
    # palette candidates = k-means centres over all pixel colours of tiles assigned together
    cent, lb = kmeans(desc, npal, seed=seed)
    pals = []
    for g in range(len(cent)):
        members = [t for t in range(len(tiles)) if lb[t] == g]
        if not members:
            pals.append([0] * ncol)
            continue
        allpx = np.concatenate([tiles[t] for t in members])
        pals.append(fit_palette(allpx, ncol, seed=seed))
    while len(pals) < npal:
        pals.append(pals[0])
    pals = [list(p) + [p[-1]] * (ncol - len(p)) for p in pals]
    # iterate: assign tiles to the palette with least error, refit
    for it in range(max_iter):
        assign = []
        errs = []
        for t, px in enumerate(tiles):
            lp = lab(px)
            best = None
            for g, p in enumerate(pals):
                pc = lab(np.array([from555(c) for c in p]))
                e = ((lp[:, None, :] - pc[None, :, :]) ** 2).sum(2).min(1).sum()
                if best is None or e < best[0]:
                    best = (e, g)
            assign.append(best[1])
            errs.append(best[0])
        changed = False
        for g in range(npal):
            members = [t for t in range(len(tiles)) if assign[t] == g]
            if not members:
                continue
            allpx = np.concatenate([tiles[t] for t in members])
            np_ = fit_palette(allpx, ncol, seed=seed + it)
            np_ = list(np_) + [np_[-1]] * (ncol - len(np_))
            if np_ != pals[g]:
                pals[g] = np_
                changed = True
        if not changed:
            break
    # final mapping
    idx = np.zeros((ty, tx, 8, 8), np.uint8)
    tpal = np.zeros((ty, tx), np.uint8)
    for t, px in enumerate(tiles):
        lp = lab(px)
        best = None
        for g, p in enumerate(pals):
            pc = lab(np.array([from555(c) for c in p]))
            d = ((lp[:, None, :] - pc[None, :, :]) ** 2).sum(2)
            e = d.min(1).sum()
            if best is None or e < best[0]:
                best = (e, g, d.argmin(1))
        j, i = divmod(t, tx)
        tpal[j, i] = best[1]
        idx[j, i] = best[2].reshape(8, 8)
    return dict(pal=pals, tiles=idx, tpal=tpal)


def dedup_tiles(idx, tpal, flips=True):
    """unique tiles with H/V flip folding.  Returns (tile_list [16 byte strings], map (ty,tx) of (tile, hflip, vflip))"""
    ty, tx = idx.shape[:2]
    seen = {}
    tiles = []
    tmap = []
    for j in range(ty):
        row = []
        for i in range(tx):
            t = idx[j, i]
            best = None
            for hf in (0, 1):
                for vf in (0, 1):
                    if not flips and (hf or vf):
                        continue
                    tt = t[::-1] if vf else t
                    tt = tt[:, ::-1] if hf else tt
                    b = tile_bytes(tt)
                    if b in seen:
                        best = (seen[b], hf, vf)
                        break
                if best:
                    break
            if best is None:
                b = tile_bytes(t)
                seen[b] = len(tiles)
                tiles.append(b)
                best = (len(tiles) - 1, 0, 0)
            row.append(best)
        tmap.append(row)
    return tiles, tmap
