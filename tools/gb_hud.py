"""gb_hud.py - the status panel under the 3-D view (160 x 48 pixels = 20 x 6 cells) in the Game Boy Color format.

All eight BG palettes are replaced below line 96 (see hw_a.s): P6/P7 from line 96 (first cell row only),
P0..P5 from line 98.  The panel uses the tile data at 0x9000 (LCDC bit 4 clear, tile numbers 0..127).

  P7 panel / labels    P6 digits    P5..P3 faces    P2 gold key    P1..P0 weapon icons
Cells (col,row):  floor 0-1/1-2, score 2-7/1-2, face 8-11/1-4, health 12-14/1-2 + '%' 15, ammo 16-17/1-2,
                  weapon icon 0-5/3-5, lives 7/3-4, keys 12/3-4 (gold) and 14/3-4 (silver).
"""
import numpy as np
import gbdata as G

COLS, ROWS = 20, 6
FACE_C, FACE_R = 8, 1
ICON_C, ICON_R, ICON_W, ICON_H = 0, 3, 6, 3
NUMS = {'floor': (0, 1, 2), 'score': (2, 1, 6), 'health': (12, 1, 3), 'ammo': (16, 1, 2), 'lives': (7, 3, 1)}
KEYC = [(12, 3), (14, 3)]
LABELS = [('FL', 0, 0), ('SCORE', 2, 0), ('HP', 12, 0), ('AM', 16, 0), ('LV', 6, 5), ('KEYS', 12, 5)]
FACES = ['FACE%d%sPIC' % (i, l) for i in range(1, 8) for l in 'ABC'] + ['FACE8APIC', 'GOTGATLINGPIC']
ICONS = ['KNIFEPIC', 'GUNPIC', 'MACHINEGUNPIC', 'GATLINGGUNPIC']

BG = G.snap555((0, 0, 150))
LIGHT = G.snap555((140, 140, 255))
DARK = G.snap555((0, 0, 70))
WHITE = (255, 255, 255)


def err(px, pal):
    lp = G.lab(px)
    pc = G.lab(np.array([G.from555(c) if isinstance(c, int) else c for c in pal], dtype=np.float64))
    d = ((lp[:, None, :] - pc[None, :, :]) ** 2).sum(2)
    return d.min(1).sum(), d.argmin(1)


def with_bg(pic, bgrgb):
    """replace the near-background colours of an original picture by the panel colour"""
    return pic


def build():
    pals = [None] * 8
    bg = BG
    # ---- the panel colours
    pals[7] = [G.rgb555(bg), G.rgb555(WHITE), G.rgb555(LIGHT), G.rgb555(DARK)]
    # ---- digits
    dig_pics = [G.pic_rgb('N_%dPIC' % i) for i in range(10)]
    px = np.concatenate([p.reshape(-1, 3) for p in dig_pics])
    px = px[(px.sum(1) > 60)]
    c = G.fit_palette(px, 3)
    pals[6] = [G.rgb555(bg)] + list(c) + [G.rgb555(bg)] * (3 - len(c))
    # ---- faces: 32x32 canvases on the panel colour
    canv = []
    sb = np.array(bg, dtype=np.uint8)
    for n in FACES:
        pic = G.pic_rgb(n)
        cv = np.zeros((32, 32, 3), np.uint8)
        cv[:] = sb
        cv[:, 4:28] = pic
        canv.append(cv)
    sheet = np.concatenate(canv, axis=1)
    q = G.quantize_tiles(sheet, npal=3, ncol=4)
    pals[3], pals[4], pals[5] = [list(p) for p in q['pal']]
    face_tiles, face_attr = [], []
    for f in range(len(FACES)):
        tl, at = [], []
        for j in range(4):
            for i in range(4):
                tl.append(G.tile_bytes(q['tiles'][j, f * 4 + i]))
                at.append(3 + int(q['tpal'][j, f * 4 + i]))
        face_tiles.append(tl)
        face_attr.append(at)
    # ---- weapon icons (48 x 24) on 2 palettes
    isheet = np.concatenate([G.pic_rgb(n) for n in ICONS], axis=1)
    qi = G.quantize_tiles(isheet, npal=2, ncol=4)
    pals[0], pals[1] = [list(p) for p in qi['pal']]
    icon_tiles, icon_attr = [], []
    for k in range(4):
        tl, at = [], []
        for j in range(3):
            for i in range(6):
                tl.append(G.tile_bytes(qi['tiles'][j, k * 6 + i]))
                at.append(int(qi['tpal'][j, k * 6 + i]))
        icon_tiles.append(tl)
        icon_attr.append(at)
    # ---- keys: gold in P2, silver with the panel palette
    gold = G.pic_rgb('GOLDKEYPIC')
    gpx = gold.reshape(-1, 3)
    gpx = gpx[gpx.sum(1) > 60]
    pals[2] = [G.rgb555(bg)] + list(G.fit_palette(gpx, 3))
    pals[2] += [pals[2][-1]] * (4 - len(pals[2]))
    # ---- tiles list: static first
    tiles = []
    tid = {}

    def add(b):
        if b not in tid:
            tid[b] = len(tiles)
            tiles.append(b)
        return tid[b]

    blank = add(G.tile_bytes(np.zeros((8, 8), np.uint8)))
    # digit glyphs in P6: nearest of the palette
    def map_img(img, pal):
        h, w = img.shape[:2]
        e, a = err(img.reshape(-1, 3), pal)
        return a.reshape(h, w).astype(np.uint8)
    dig0 = len(tiles)
    dig_ids = []
    for i in range(10):
        m = map_img(dig_pics[i], pals[6])
        dig_ids += [add(G.tile_bytes(m[:8])), add(G.tile_bytes(m[8:]))]
    m = map_img(G.pic_rgb('N_BLANKPIC'), pals[6])
    dig_ids += [add(G.tile_bytes(m[:8])), add(G.tile_bytes(m[8:]))]
    # a '%' sign made from the big font
    f = G.font(1).glyph(ord('%'))
    pc = np.zeros((16, 8), np.uint8)
    h, w = f.shape
    wide = np.zeros((16, 12), np.uint8)
    for y in range(min(h, 15)):
        for x in range(min(w, 12)):
            if f[y, x]:
                wide[y + 1, x] = 1
    for x in range(8):
        a = wide[:, int(x * 12 / 8)]
        b = wide[:, min(11, int(x * 12 / 8) + 1)]
        pc[:, x] = a | b if x % 2 == 0 else a
    pc = np.where(pc > 0, 1, 0)   # colour 1 of P6 (its lightest after the background)
    pct = [add(G.tile_bytes(pc[:8])), add(G.tile_bytes(pc[8:]))]

    # ---- static panel image (palette indices of P7 plus tiles of keys/labels)
    img = np.zeros((48, 160), np.uint8)            # indices into P7
    img[0, :] = 2
    img[47, :] = 3
    img[:, 0] = 2
    img[:, 159] = 3
    for text, c, r in LABELS:
        for i, chh in enumerate(text):
            g = G.cell_glyph(ord(chh))
            blk = img[r * 8:r * 8 + 8, (c + i) * 8:(c + i) * 8 + 8]
            blk[g] = 1
    # frames (bevel boxes) around the digit groups
    def box(c0, r0, c1, r1):
        x0, y0, x1, y1 = c0 * 8, r0 * 8, c1 * 8 + 7, r1 * 8 + 7
        img[y0, x0:x1 + 1] = 3
        img[y0:y1 + 1, x0] = 3
        img[y1, x0:x1 + 1] = np.where(img[y1, x0:x1 + 1] == 0, 2, img[y1, x0:x1 + 1])
        img[y0:y1 + 1, x1] = np.where(img[y0:y1 + 1, x1] == 0, 2, img[y0:y1 + 1, x1])
    cmap = np.full((ROWS, COLS), blank, np.uint8)
    amap = np.full((ROWS, COLS), 7, np.uint8)
    for r in range(ROWS):
        for c in range(COLS):
            cell = img[r * 8:r * 8 + 8, c * 8:c * 8 + 8]
            cmap[r, c] = add(G.tile_bytes(cell))
    # dynamic regions initial content
    for name, (c0, r0, n) in NUMS.items():
        for k in range(n):
            cmap[r0, c0 + k] = dig_ids[20]
            cmap[r0 + 1, c0 + k] = dig_ids[21]
            amap[r0, c0 + k] = amap[r0 + 1, c0 + k] = 6
    cmap[1, 15], cmap[2, 15] = pct
    amap[1, 15] = amap[2, 15] = 6
    # keys: tiles of the gold / silver key and of the empty slot
    nokey = G.pic_rgb('NOKEYPIC')
    gk = G.pic_rgb('GOLDKEYPIC')
    sk = G.pic_rgb('SILVERKEYPIC')
    key_tiles = {}
    for nm, im, pl, an in [('no', nokey, pals[7], 7), ('gold', gk, pals[2], 2), ('silver', sk, pals[7], 7)]:
        m = map_img(im, pl)
        key_tiles[nm] = (add(G.tile_bytes(m[:8])), add(G.tile_bytes(m[8:])), an)
    for (c0, r0), nm in zip(KEYC, ['no', 'no']):
        t0, t1, an = key_tiles[nm]
        cmap[r0, c0], cmap[r0 + 1, c0] = t0, t1
        amap[r0, c0] = amap[r0 + 1, c0] = an
    static_n = len(tiles)
    # dynamic tile slots after the static ones
    face0 = static_n
    icon0 = face0 + 16
    total = icon0 + 18
    assert total <= 128, total
    for j in range(4):
        for i in range(4):
            cmap[FACE_R + j, FACE_C + i] = face0 + j * 4 + i
            amap[FACE_R + j, FACE_C + i] = 3
    for j in range(ICON_H):
        for i in range(ICON_W):
            cmap[ICON_R + j, ICON_C + i] = icon0 + j * 6 + i
            amap[ICON_R + j, ICON_C + i] = 0
    # row 0 only uses P6/P7 (the other palettes arrive at line 98); the static cells all use P7
    return dict(pals=pals, tiles=tiles, cmap=cmap, amap=amap, dig=dig_ids, face0=face0, icon0=icon0, total=total,
                face_tiles=face_tiles, face_attr=face_attr, icon_tiles=icon_tiles, icon_attr=icon_attr,
                key_tiles=key_tiles, pct=pct, static_n=static_n)


def render_preview(h, path):
    """a PNG of the panel with sample content (for looking at the result)"""
    from PIL import Image
    out = np.zeros((48, 160, 3), np.uint8)
    pals = h['pals']
    tl = list(h['tiles'])
    tl += [b for t in h['face_tiles'][0] for b in [t]]
    tl += list(h['icon_tiles'][1])
    for r in range(ROWS):
        for c in range(COLS):
            t = h['cmap'][r, c]
            data = tl[t]
            a = h['amap'][r, c]
            if h['face0'] <= t < h['face0'] + 16:
                a = h['face_attr'][0][t - h['face0']]
            if h['icon0'] <= t < h['icon0'] + 18:
                a = h['icon_attr'][1][t - h['icon0']]
            for y in range(8):
                lo, hi = data[2 * y], data[2 * y + 1]
                for x in range(8):
                    v = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1)
                    out[r * 8 + y, c * 8 + x] = G.from555(pals[a][v])
    Image.fromarray(out).resize((640, 192), Image.NEAREST).save(path)


if __name__ == '__main__':
    h = build()
    print('tiles', h['total'], 'static', h['static_n'])
    render_preview(h, '/tmp/hud.png')


# ------------------------------------------------------------------ weapon sprites (OBJ, 8x8, 1:1)
WPN_CANVAS_X, WPN_CANVAS_Y = 48, 32          # the 64x64 shape sits at the bottom centre of the 160x96 view
WPN_FIRST = 416


def _opaque(d, i):
    sp = d.sprite(i)
    a = np.zeros((64, 64), bool)
    rgb = np.zeros((64, 64, 3), np.uint8)
    for x in range(64):
        for y in range(64):
            v = sp[x][y]
            if v is not None:
                a[y, x] = True
                rgb[y, x] = d.pal8[v]
    return a, rgb


def _quant_masked(tiles, npal=8, ncol=3):
    """tiles: list of (rgb (8,8,3), mask (8,8)).  Returns palettes (ncol rgb555 each) and per tile (pal, idx 0..ncol with 0 = clear)"""
    pxs = [t[0][t[1]] for t in tiles]
    desc = []
    for px in pxs:
        p = G.fit_palette(px, ncol)
        p = list(p) + [p[-1]] * (ncol - len(p))
        desc.append(np.concatenate([G.lab(np.array(G.from555(c), dtype=np.float64)).reshape(-1) for c in p]))
    desc = np.array(desc)
    k = min(npal, len(tiles))
    cent, lb = G.kmeans(desc, k, seed=3)
    pals = []
    for g in range(k):
        mem = [t for t in range(len(tiles)) if lb[t] == g]
        if not mem:
            pals.append([0] * ncol)
            continue
        p = G.fit_palette(np.concatenate([pxs[t] for t in mem]), ncol)
        pals.append(list(p) + [p[-1]] * (ncol - len(p)))

    def cost(px, p):
        pc = G.lab(np.array([G.from555(c) for c in p], dtype=np.float64))
        d = ((G.lab(px)[:, None, :] - pc[None, :, :]) ** 2).sum(2)
        return d.min(1).sum(), d.argmin(1)
    for it in range(8):
        asg = [min(range(len(pals)), key=lambda g: cost(px, pals[g])[0]) for px in pxs]
        ch = False
        for g in range(len(pals)):
            mem = [t for t in range(len(tiles)) if asg[t] == g]
            if not mem:
                continue
            p = G.fit_palette(np.concatenate([pxs[t] for t in mem]), ncol)
            p = list(p) + [p[-1]] * (ncol - len(p))
            if p != pals[g]:
                pals[g] = p
                ch = True
        if not ch:
            break
    asg = [min(range(len(pals)), key=lambda g: cost(px, pals[g])[0]) for px in pxs]
    res = []
    for t, (rgb, mask) in enumerate(tiles):
        idx = np.zeros((8, 8), np.uint8)
        _, a = cost(rgb[mask], pals[asg[t]])
        idx[mask] = a + 1
        res.append((asg[t], idx))
    while len(pals) < npal:
        pals.append([0] * ncol)
    return pals, res


def build_weapons():
    """per weapon: 8 OBJ palettes; per frame: a blob  n, nt0, nt1, n * (y, x, tile, attr), tiles (nt0 for VRAM bank 0 then nt1 bank 1)"""
    d = G.data()
    wpal = []
    blobs = []
    for w in range(4):
        frames = []
        tiles = []
        keys = []
        for f in range(5):
            a, rgb = _opaque(d, WPN_FIRST + w * 5 + f)
            assert not a[:24].any()
            for ty in range(3, 8):
                for tx in range(8):
                    m = a[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
                    if m.any():
                        tiles.append((rgb[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8], m))
                        keys.append((f, ty, tx))
        pals, res = _quant_masked(tiles)
        wpal.append([[0] + p for p in pals])
        for f in range(5):
            ents = []
            uniq = {}
            slots = []
            for (kf, ty, tx), (pg, idx) in zip(keys, res):
                if kf != f:
                    continue
                b = G.tile_bytes(idx)
                if (b, pg) not in uniq:
                    uniq[(b, pg)] = len(slots)
                    slots.append(b)
                ents.append((ty, tx, uniq[(b, pg)], pg))
            assert len(slots) <= 32, (w, f, len(slots))
            nt0 = min(16, len(slots))
            nt1 = len(slots) - nt0
            oam = bytearray()
            for ty, tx, s, pg in ents:
                bank = 1 if s >= 16 else 0
                oam += bytes([WPN_CANVAS_Y + ty * 8 + 16, WPN_CANVAS_X + tx * 8 + 8, 240 + (s & 15), pg | (bank << 3)])
            hdr = bytes([len(ents), nt0, nt1]) + bytes(oam)
            hdr += bytes(-len(hdr) % 16)              # the tile data is a DMA source: 16 byte aligned
            blobs.append(hdr + b''.join(slots))
    return wpal, blobs
