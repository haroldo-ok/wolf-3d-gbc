"""gb_hicolor.py - a 160x144 picture in "hi-colour" for the Game Boy Color: the BG palettes are rewritten in every
HBlank, so each palette is only used for two lines.

  palettes 0-2 (group A): the tile columns 0-9, rewritten after the odd lines: each covers lines 2k, 2k+1
  palettes 3-5 (group B): the tile columns 10-19, rewritten after the even lines: lines 0, then 2k+1, 2k+2
  3 palettes = 24 bytes a line, ~200 dots in double speed: well inside HBlank + the OAM scan of the next line.

Blob (one ROM bank):
  u16 ntiles; pad to 16;  tiles[360][16];  map[360] (tile number, low 8 bits);  attr[360];
  stream: 48 bytes (both groups for line 0, written in VBlank) + 143 * 24 bytes (after line L = 0..142)
"""
import numpy as np

W, H = 160, 144
COLS_A = [0, 0, 0, 1, 1, 1, 1, 2, 2, 2]          # tile column -> palette in its group
NPG = 3                                          # palettes in a group


def _pal_of_col(c):
    return COLS_A[c] if c < 10 else NPG + COLS_A[c - 10]


def _strips(group):
    """the line ranges a palette of the group holds, in update order"""
    if group == 0:
        return [(y, y + 2) for y in range(0, H, 2)]
    return [(0, 1)] + [(y, min(y + 2, H)) for y in range(1, H, 2)]


def _to555(c):
    c = np.clip(np.round(c.astype(np.float64) * 31 / 255), 0, 31).astype(int)
    return c


def _kmeans4(px, iters=12, seed=0):
    """px: (n, 3) in 0..31 -> (4, 3) centres (ints 0..31)"""
    u = np.unique(px, axis=0)
    if len(u) <= 4:
        c = np.zeros((4, 3), int)
        c[:len(u)] = u
        c[len(u):] = u[0]
        return c
    r = np.random.RandomState(seed)
    # farthest point start
    c = [px[r.randint(len(px))].astype(float)]
    for _ in range(3):
        d = np.min([((px - k) ** 2).sum(1) for k in c], axis=0)
        c.append(px[int(np.argmax(d))].astype(float))
    c = np.array(c)
    for _ in range(iters):
        d = ((px[:, None, :] - c[None]) ** 2).sum(2)
        a = d.argmin(1)
        for k in range(4):
            if (a == k).any():
                c[k] = px[a == k].mean(0)
    return np.clip(np.round(c), 0, 31).astype(int)


def make_hicolor(rgb):
    rgb = np.asarray(rgb, np.uint8)
    assert rgb.shape[:2] == (H, W)
    c5 = _to555(rgb)                                       # (H, W, 3) 0..31
    idx = np.zeros((H, W), np.uint8)
    pal_at = {}                                            # (palette, strip index) -> 4 colours
    for p in range(2 * NPG):
        grp = 0 if p < NPG else 1
        cols = [c for c in range(20) if _pal_of_col(c) == p]
        xs = np.concatenate([np.arange(c * 8, c * 8 + 8) for c in cols])
        for si, (y0, y1) in enumerate(_strips(grp)):
            blk = c5[y0:y1][:, xs].reshape(-1, 3)
            cen = _kmeans4(blk, seed=p * 1000 + si)
            pal_at[(p, si)] = cen
            d = ((c5[y0:y1][:, xs][:, :, None, :] - cen[None, None]) ** 2).sum(3)
            idx[y0:y1, xs] = d.argmin(2)

    def word(c):
        return int(c[0]) | (int(c[1]) << 5) | (int(c[2]) << 10)

    def group_bytes(grp, si):
        out = bytearray()
        for p in range(grp * NPG, grp * NPG + NPG):
            for c in pal_at[(p, si)]:
                w = word(c)
                out += bytes([w & 255, w >> 8])
        return out

    stream = bytearray()
    stream += group_bytes(0, 0) + group_bytes(1, 0)       # line 0 (palettes 0-5, written in VBlank)
    for L in range(H - 1):
        if L % 2 == 0:                                     # group B for lines L+1, L+2: strip (L // 2) + 1
            stream += group_bytes(1, L // 2 + 1)
        else:                                              # group A for lines L+1, L+2
            stream += group_bytes(0, (L + 1) // 2)
    assert len(stream) == 48 + 143 * 24

    tiles = bytearray()
    for ty in range(18):
        for tx in range(20):
            t = idx[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
            for row in t:
                lo = hi = 0
                for x, v in enumerate(row):
                    lo |= (v & 1) << (7 - x)
                    hi |= ((v >> 1) & 1) << (7 - x)
                tiles += bytes([lo, hi])
    mp = bytearray()
    at = bytearray()
    for ty in range(18):
        for tx in range(20):
            n = ty * 20 + tx
            mp.append(n & 255)
            at.append(_pal_of_col(tx) | (0x08 if n >= 256 else 0))
    blob = bytearray([360 & 255, 360 >> 8])
    blob += bytes(14)
    blob += tiles + mp + at + stream
    return bytes(blob), idx, pal_at


def preview(rgb):
    """what the Game Boy shows (RGB 8 bit), for checking the conversion"""
    blob, idx, pal_at = make_hicolor(rgb)
    out = np.zeros((H, W, 3), np.uint8)
    for p in range(2 * NPG):
        grp = 0 if p < NPG else 1
        cols = [c for c in range(20) if _pal_of_col(c) == p]
        xs = np.concatenate([np.arange(c * 8, c * 8 + 8) for c in cols])
        for si, (y0, y1) in enumerate(_strips(grp)):
            cen = pal_at[(p, si)]
            out[y0:y1][:, xs] = (cen[idx[y0:y1][:, xs]] * 255 // 31).astype(np.uint8)
    return out
