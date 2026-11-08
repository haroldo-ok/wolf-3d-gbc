"""gb_ui.py - front end graphics for the Game Boy Color: font and solid tiles, pictures (title, headers, faces),
text pages (help / end text, 256 x 192 panned on screen), the cursor sprite, the UI palettes.

PIC blob (little endian):  u8 w, h (cells) | u8 npal, palbase | u16 ntiles | u16 pal[npal*4] |
                           u16 map[w*h] (tile of the blob) | u8 attr[w*h] (pal | hflip<<5 | vflip<<6) | tiles (16 bytes each)
"""
import os
import re
import struct
import numpy as np
import gbdata as G
import wl_names

BORD, BORD2, DEACT, BKGD, STRIPE = 0x29, 0x27, 0x2B, 0x2D, 0x2C
READ, READH, VIEW, TEXT, HILITE = 0x4A, 0x47, 0x7F, 0x17, 0x13
BLACK, WHITE = 0x00, 0x0F


def pal8():
    return np.array(G.data().pal8, np.uint8)


def rgb_of(i):
    return tuple(int(v) for v in pal8()[i])


def c555(i):
    return G.rgb555(rgb_of(i))


def to_rgb(idx):
    return pal8()[np.asarray(idx, np.uint8)]


# ----------------------------------------------------------------- text / font tiles
def font_tiles():
    """95 glyphs (ink = colour 1) + three solid tiles (colour 1, 2, 3)"""
    out = []
    for c in range(32, 127):
        g = G.cell_glyph(c)
        out.append(G.tile_bytes(g.astype(np.uint8)))
    for v in (1, 2, 3):
        out.append(G.tile_bytes(np.full((8, 8), v, np.uint8)))
    return b''.join(out)


def ui_palettes():
    """[2 kinds][8 palettes][4 colours]: kind 0 menus (red), kind 1 intermission (view colour)"""
    res = []
    for bg, outer in ((BORD, BORD), (VIEW, VIEW)):
        p = [
            [BKGD if bg == BORD else VIEW, TEXT, BORD2, BORD],
            [BKGD if bg == BORD else VIEW, HILITE, BORD2, BORD],
            [BKGD if bg == BORD else VIEW, READH, BORD2, BORD],
            [BKGD if bg == BORD else VIEW, BORD if bg == BORD else DEACT, BORD2, BORD],
            [outer, TEXT, HILITE, BLACK],
            [outer, READH, HILITE, BLACK],
            [outer, TEXT, HILITE, BLACK],
            [outer, TEXT, HILITE, BLACK],
        ]
        res.append([[c555(i) for i in q] for q in p])
    return res


# ----------------------------------------------------------------- picture blobs
def _box(a, w, h):
    return G.resize_rgb(a, w, h)


def make_pic(rgb, npal, palbase, seed=1, max_iter=6, limit=512):
    """RGB (h, w multiples of 8) -> PIC blob"""
    rgb = np.asarray(rgb, np.uint8)
    h, w = rgb.shape[:2]
    ty, tx = h // 8, w // 8
    # quantise only the distinct tiles
    uniq = {}
    order = []
    for j in range(ty):
        for i in range(tx):
            t = rgb[j * 8:j * 8 + 8, i * 8:i * 8 + 8]
            k = t.tobytes()
            if k not in uniq:
                uniq[k] = len(uniq)
                order.append(t)
            order_idx = uniq[k]
    cells = []
    for j in range(ty):
        for i in range(tx):
            cells.append(uniq[rgb[j * 8:j * 8 + 8, i * 8:i * 8 + 8].tobytes()])
    strip = np.concatenate(order, axis=1) if len(order) > 1 else order[0]
    if len(order) == 1:
        strip = np.concatenate([order[0], order[0]], axis=1)
        order = order * 2
    q = G.quantize_tiles(strip, npal=npal, ncol=4, max_iter=max_iter, seed=seed)
    idx = np.zeros((ty, tx, 8, 8), np.uint8)
    tpal = np.zeros((ty, tx), np.uint8)
    for j in range(ty):
        for i in range(tx):
            u = cells[j * tx + i]
            idx[j, i] = q['tiles'][0, u]
            tpal[j, i] = q['tpal'][0, u]
    tiles, tmap = G.dedup_tiles(idx, tpal)
    while len(tiles) > limit:
        idx = _merge(idx, len(tiles) - limit)
        tiles, tmap = G.dedup_tiles(idx, tpal)
    pal = q['pal']
    blob = struct.pack('<BBBBH', tx, ty, npal, palbase, len(tiles))
    blob += b''.join(struct.pack('<H', c) for p in pal for c in p)
    blob += b''.join(struct.pack('<H', tmap[j][i][0]) for j in range(ty) for i in range(tx))
    blob += bytes(int(tpal[j, i]) | (tmap[j][i][1] << 5) | (tmap[j][i][2] << 6) for j in range(ty) for i in range(tx))
    blob += bytes(-len(blob) % 16)
    blob += b''.join(tiles)
    return blob, len(tiles)


def _merge(idx, k):
    """remap the k least used tile patterns to their nearest other pattern (flips folded)"""
    ty, tx = idx.shape[:2]
    pats = {}
    for j in range(ty):
        for i in range(tx):
            pats.setdefault(idx[j, i].tobytes(), []).append((j, i))
    keys = sorted(pats, key=lambda p: len(pats[p]))
    k = min(len(keys) - 1, 2 * k + 8)
    keep = np.array([np.frombuffer(p, np.uint8).reshape(8, 8) for p in keys[k:]])
    for p in keys[:k]:
        a = np.frombuffer(p, np.uint8).reshape(8, 8)
        best = None
        for v in (keep, keep[:, ::-1], keep[:, :, ::-1], keep[:, ::-1, ::-1]):
            d = (v != a).sum(axis=(1, 2))
            m = int(d.argmin())
            if best is None or d[m] < best[0]:
                best = (int(d[m]), v[m])
        for (j, i) in pats[p]:
            idx[j, i] = best[1]
    return idx


def pic_rgb(name, w=None, h=None, bg=None):
    a = to_rgb(G.pic256(name))
    if w is not None:
        a = _box(a, w, h)
    ph, pw = a.shape[:2]
    if ph % 8 or pw % 8:
        nh, nw = (ph + 7) // 8 * 8, (pw + 7) // 8 * 8
        b = np.zeros((nh, nw, 3), np.uint8)
        b[:] = a[-1, -1] if bg is None else bg
        b[:ph, :pw] = a
        a = b
    return a


def cursor_blob():
    """2 frames of 3 x 2 OBJ tiles (row major), colour 0 transparent, plus the 4 colour OBJ palette"""
    fr = [G.pic256('C_CURSOR1PIC'), G.pic256('C_CURSOR2PIC')]
    fr = [f[::2][:, [(i * 3) // 2 for i in range(16)]] for f in fr]     # 16 x 8: two OBJ tiles
    bgc = int(np.bincount(fr[0].reshape(-1)).argmax())
    cols = []
    for f in fr:
        for v in np.unique(f):
            if int(v) != bgc and int(v) not in cols:
                cols.append(int(v))
    rgbs = np.array([rgb_of(c) for c in cols])
    # at most three colours: k-means onto the used ones weighted by frequency
    cnt = {c: 0 for c in cols}
    for f in fr:
        for v in f.reshape(-1):
            if int(v) != bgc:
                cnt[int(v)] += 1
    pal = G.fit_palette(np.repeat(rgbs, [cnt[c] for c in cols], axis=0), 3)
    pal = list(pal) + [pal[-1]] * (3 - len(pal))
    prgb = np.array([G.from555(c) for c in pal])
    out = b''
    for f in fr:
        for cy in range(1):
            for cx in range(2):
                t = np.zeros((8, 8), np.uint8)
                for y in range(8):
                    for x in range(8):
                        v = int(f[cy * 8 + y, cx * 8 + x])
                        if v != bgc:
                            d = ((prgb - np.array(rgb_of(v))) ** 2).sum(1)
                            t[y, x] = 1 + int(d.argmin())
                out += G.tile_bytes(t)
    return out, [0] + list(pal)


# ----------------------------------------------------------------- text pages
BACKCOLOR = 0x11
TOPMARGIN, LEFTMARGIN, RIGHTMARGIN, FONTHEIGHT = 16, 16, 16, 10
TEXTROWS = (200 - TOPMARGIN - 32) // FONTHEIGHT
PICMARGIN, SPACEWIDTH, SCREENMID = 8, 7, 160

PAD_PAGES = {
    1: """^PAGE 1
^c7eQUICK COMMAND REFERENCE
^c00
The D-pad moves and turns. A shoots.

B:\t\t\tuse (doors, push walls)
B + Left/Right:\tstrafe
B + Down:\t\tnext weapon
SELECT:\t\tnext weapon
START:\t\tpause, the menu
In menus: Up/Down moves,
A selects, B backs out

Read on for more information.
""",
    6: """^PAGE 6
^G16,16,6
^c7eCONTROLS
^c00
In Wolfenstein 3-D for the Game Boy Color, use the control pad.

To move around the menus, press ^c6eUp ^c00and ^c6eDown ^c00to move from item to item, then press ^c6eA ^c00to select that item.  ^c6eB ^c00always backs out.

The ^c6eSTART ^c00button opens the menu at any time during the game.
""",
    7: """^PAGE 7
Once you are in the game, use the D-pad to move: up and down walk, left and right turn.

Press ^c6eA ^c00to fire your weapon.

Hold ^c6eB ^c00and press left or right to slide side to side instead of turning left or right.  This is called "Strafe Mode."

Press ^c6eB ^c00to Open Doors and activate elevators to go to the next floor of the keep.
""",
    8: """^PAGE 8
^G16,16,6
Hold ^c6eB ^c00and press ^c6eDown ^c00(or press ^c6eSELECT^c00) to step through the weapons you are carrying.



Your best weapon is selected automatically when you pick one up.
""",
    9: """^PAGE 9
^G50,110,12
Once you have more than one weapon, ^c6eSELECT ^c00chooses the next one:







Holding down the fire button is fun, but short bursts save bullets.
""",
}


def help_text():
    t = G.data().vga_chunk(150).decode('latin1').replace('\r', '')
    parts = re.split(r'(?=^\^PAGE ?\d+\n)', t, flags=re.M)
    out = []
    for p in parts:
        m = re.match(r'\^PAGE ?(\d+)\n', p)
        if m and int(m.group(1)) in PAD_PAGES:
            out.append(PAD_PAGES[int(m.group(1))])
        else:
            out.append(p)
    t = ''.join(out)
    t = t.replace("Hold down Space to push the wall in front of you. Just think of Space as the Use key.",
                  "Press B to push the wall in front of you. Just think of B as the Use key.")
    t = t.replace("(ESC to return to menu.)", "(B to return to menu.)")
    return t


def end_text():
    return G.data().vga_chunk(155).decode('latin1').replace('\r', '')


class Canvas:
    def __init__(self, bg, h=200, w=320):
        self.p = np.full((h, w), bg, np.uint8)

    def bar(self, x, y, w, h, c):
        self.p[max(0, y):max(0, y + h), max(0, x):max(0, x + w)] = c

    def pic(self, x, y, name_or_arr):
        a = G.pic256(name_or_arr) if isinstance(name_or_arr, str) else name_or_arr
        h, w = a.shape
        ys, xs = np.mgrid[0:h, 0:w]
        ok = (ys + y >= 0) & (ys + y < self.p.shape[0]) & (xs + x >= 0) & (xs + x < self.p.shape[1])
        self.p[(ys + y)[ok], (xs + x)[ok]] = a[ok]

    def text(self, x, y, s, col):
        f = G.font(0)
        for ch in s:
            g = f.glyph(ord(ch))
            ys, xs = np.nonzero(g)
            ok = (ys + y < self.p.shape[0]) & (xs + x < self.p.shape[1])
            self.p[(ys + y)[ok], (xs + x)[ok]] = col
            x += g.shape[1]
        return x


def text_width(s):
    f = G.font(0)
    return sum(int(f.wid[ord(c)]) for c in s)


_names = {}


def gfx_names():
    if not _names:
        _names.update(dict(wl_names.gfx_names()))
    return _names


class Layout:
    def __init__(self, text):
        self.text = text
        self.pos = 0

    def number(self):
        while self.pos < len(self.text) and not self.text[self.pos].isdigit():
            self.pos += 1
        n = 0
        while self.pos < len(self.text) and self.text[self.pos].isdigit():
            n = n * 10 + int(self.text[self.pos])
            self.pos += 1
        return n

    def rip_eol(self):
        while self.pos < len(self.text) and self.text[self.pos] != '\n':
            self.pos += 1
        self.pos += 1

    def count_pages(self):
        return len(re.findall(r'\^[Pp]', self.text))

    def page(self, num, total):
        cv = Canvas(BACKCOLOR)
        cv.pic(0, 0, 'H_TOPWINDOWPIC')
        cv.pic(0, 8, 'H_LEFTWINDOWPIC')
        cv.pic(312, 8, 'H_RIGHTWINDOWPIC')
        cv.pic(8, 176, 'H_BOTTOMINFOPIC')
        strip = G.pic256('H_BOTTOMINFOPIC')
        box = int(strip[4, 70])
        region = strip[6:18, 66:110]
        lum = pal8()[region].astype(int).sum(2)
        ink = int(region.reshape(-1)[int(np.argmin(lum))])
        cv.bar(8 + 66, 176 + 4, 44, 14, box)
        cv.text(8 + 69, 176 + 7, 'B=EXIT', ink)
        self.cv = cv
        self.left = [LEFTMARGIN] * TEXTROWS
        self.right = [320 - RIGHTMARGIN] * TEXTROWS
        self.px, self.py, self.row = LEFTMARGIN, TOPMARGIN, 0
        self.done = False
        self.color = 0
        t = self.text
        while self.pos < len(t) and ord(t[self.pos]) <= 32:
            self.pos += 1
        self.pos += 2
        self.rip_eol()
        while not self.done and self.pos < len(t):
            ch = t[self.pos]
            if ch == '^':
                self.command()
            elif ch == '\t':
                self.px = (self.px + 8) & ~7
                self.pos += 1
            elif ord(ch) <= 32:
                self.pos += 1
                if ch == '\n':
                    self.newline()
            else:
                self.word()
        cv.text(213, 183, 'pg %d of %d' % (num, total), 0x4F)
        return cv.p.copy()

    def newline(self):
        self.row += 1
        if self.row == TEXTROWS:
            self.done = True
            t = self.text
            while self.pos < len(t):
                if t[self.pos] == '^' and t[self.pos + 1:self.pos + 2] in ('E', 'e', 'P', 'p'):
                    return
                self.pos += 1
            return
        self.px = self.left[self.row]
        self.py += FONTHEIGHT

    def command(self):
        t = self.text
        self.pos += 1
        c = t[self.pos].upper()
        if c == 'B':
            self.pos += 1
            y, x, w, h = self.number(), self.number(), self.number(), self.number()
            self.cv.bar(x, y, w, h, BACKCOLOR)
            self.rip_eol()
        elif c == ';':
            self.rip_eol()
        elif c in 'PE':
            self.done = True
            self.pos -= 1
        elif c == 'C':
            self.color = int(t[self.pos + 1:self.pos + 3], 16)
            self.pos += 3
        elif c == '>':
            self.px = 160
            self.pos += 1
        elif c == 'L':
            self.pos += 1
            y = self.number()
            self.row = (y - TOPMARGIN) // FONTHEIGHT
            self.py = TOPMARGIN + self.row * FONTHEIGHT
            self.px = self.number()
            self.rip_eol()
        elif c in 'TG':
            self.pos += 1
            y, x, pic = self.number(), self.number(), self.number()
            if c == 'T':
                self.number()
            self.rip_eol()
            name = [n for n, v in gfx_names().items() if v == pic][0]
            a = G.pic256(name)
            self.cv.pic(x & ~7, y, a)
            h, w = a.shape
            mid = x + w // 2
            margin = x - PICMARGIN if mid > SCREENMID else x + w + PICMARGIN
            top = max(0, (y - TOPMARGIN) // FONTHEIGHT)
            bottom = min(TEXTROWS - 1, (y + h - TOPMARGIN) // FONTHEIGHT)
            for i in range(top, bottom + 1):
                if mid > SCREENMID:
                    self.right[i] = margin
                else:
                    self.left[i] = margin
            if self.px < self.left[self.row]:
                self.px = self.left[self.row]
        else:
            self.pos += 1

    def word(self):
        t = self.text
        a = self.pos
        self.pos += 1
        while self.pos < len(t) and ord(t[self.pos]) > 32 and self.pos - a < 79:
            self.pos += 1
        w = t[a:self.pos]
        width = text_width(w)
        while self.px + width > self.right[self.row]:
            self.newline()
            if self.done:
                return
        self.cv.text(self.px, self.py, w, self.color)
        self.px += width
        while self.pos < len(t) and t[self.pos] == ' ':
            self.px += SPACEWIDTH
            self.pos += 1


def render_pages(text):
    lay = Layout(text)
    total = lay.count_pages()
    return [lay.page(i + 1, total) for i in range(total)]


def scale_page(idx):
    """320 x 200 indices -> 256 x 192 RGB: columns/rows dropped where they cost least (never blended)"""
    import scrcore_lite as S
    s, _, _ = S.scale_screen(idx)
    return to_rgb(s)
