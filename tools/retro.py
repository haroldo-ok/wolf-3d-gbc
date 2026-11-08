"""retro.py - a tiny headless libretro frontend for cross-checking the ROM in other emulator cores (gambatte, mgba).

    python3 tools/retro.py CORE.so ROM SCRIPT OUTDIR

SCRIPT commands (one per line): run N | hold KEY | release KEY | press KEY N | shot NAME | screen_ok NAME |
screen_changed | peek ADDR N (system RAM 0xC000-0xDFFF only, bank 1 view)
KEY: UP DOWN LEFT RIGHT A B START SELECT.  Exit status 0 when every check passed.
"""
import ctypes as C, sys, os
import numpy as np

KEYS = dict(B=0, Y=1, SELECT=2, START=3, UP=4, DOWN=5, LEFT=6, RIGHT=7, A=8)
ENV_GET_SYSTEM_DIRECTORY, ENV_SET_PIXEL_FORMAT, ENV_GET_VARIABLE, ENV_GET_LOG = 9, 10, 15, 27


class GameInfo(C.Structure):
    _fields_ = [('path', C.c_char_p), ('data', C.c_void_p), ('size', C.c_size_t), ('meta', C.c_char_p)]


class Frontend:
    def __init__(self, core, rom):
        self.lib = C.CDLL(core)
        self.held = set()
        self.frame = None
        self.fmt = 0
        ENV = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
        VID = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
        AUD = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
        AUDB = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
        POLL = C.CFUNCTYPE(None)
        STATE = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)
        self.sysdir = C.c_char_p(b'/tmp')

        def env(cmd, data):
            cmd &= 0xFFFF
            if cmd == ENV_SET_PIXEL_FORMAT:
                self.fmt = C.cast(data, C.POINTER(C.c_int))[0]
                return True
            if os.environ.get('RETRO_DEBUG'):
                print('env', cmd)
            if cmd == ENV_GET_SYSTEM_DIRECTORY:
                C.cast(data, C.POINTER(C.c_char_p))[0] = self.sysdir.value
                return True
            return False

        def vid(data, w, h, pitch):
            if not data:
                return
            if self.fmt == 1:          # XRGB8888
                buf = (C.c_uint8 * (pitch * h)).from_address(data)
                a = np.frombuffer(buf, np.uint8).reshape(h, pitch // 4, 4)[:, :w]
                self.frame = a[:, :, [2, 1, 0]].copy()
            else:                       # RGB565 (2) / 0RGB1555 (0)
                buf = (C.c_uint16 * (pitch // 2 * h)).from_address(data)
                a = np.frombuffer(buf, np.uint16).reshape(h, pitch // 2)[:, :w].astype(np.uint32)
                if self.fmt == 2:
                    r, g, b = (a >> 11) & 31, (a >> 5) & 63, a & 31
                    self.frame = np.dstack([r * 255 // 31, g * 255 // 63, b * 255 // 31]).astype(np.uint8)
                else:
                    r, g, b = (a >> 10) & 31, (a >> 5) & 31, a & 31
                    self.frame = (np.dstack([r, g, b]) * 255 // 31).astype(np.uint8)

        def state(port, dev, idx, id_):
            if port == 0 and dev == 1:
                return 1 if id_ in self.held else 0
            return 0

        self.cbs = [ENV(env), VID(vid), AUD(lambda l, r: None), AUDB(lambda d, n: n), POLL(lambda: None), STATE(state)]
        self.lib.retro_set_environment(self.cbs[0])
        self.lib.retro_set_video_refresh(self.cbs[1])
        self.lib.retro_set_audio_sample(self.cbs[2])
        self.lib.retro_set_audio_sample_batch(self.cbs[3])
        self.lib.retro_set_input_poll(self.cbs[4])
        self.lib.retro_set_input_state(self.cbs[5])
        self.lib.retro_init()
        data = open(rom, 'rb').read()
        self.rom = C.create_string_buffer(data, len(data))
        gi = GameInfo(rom.encode(), C.cast(self.rom, C.c_void_p), len(data), None)
        if not self.lib.retro_load_game(C.byref(gi)):
            gi = GameInfo(os.path.abspath(rom).encode(), None, 0, None)     # cores that want the path
            assert self.lib.retro_load_game(C.byref(gi)), 'load failed'
        self.lib.retro_get_memory_data.restype = C.c_void_p
        self.lib.retro_get_memory_size.restype = C.c_size_t

    def run(self, n):
        for _ in range(n):
            self.lib.retro_run()

    def wram(self):
        p, n = self.lib.retro_get_memory_data(2), self.lib.retro_get_memory_size(2)
        return bytes((C.c_uint8 * n).from_address(p)) if p else b''


def screen_ok(f):
    px = f.reshape(-1, 3).astype(int)
    lum = px.sum(1)
    black = (lum < 40).mean()
    white = (lum > 720).mean()
    distinct = len(np.unique(px[:, 0] * 65536 + px[:, 1] * 256 + px[:, 2]))
    ok = black < 0.9 and white < 0.9 and distinct >= 4
    return ok, 'black=%.1f%% white=%.1f%% colours=%d' % (100 * black, 100 * white, distinct)


def main():
    core, rom, script, outdir = sys.argv[1:5]
    os.makedirs(outdir, exist_ok=True)
    fe = Frontend(core, rom)
    prev = None
    fails = 0
    for ln, line in enumerate(open(script), 1):
        w = line.split('#')[0].split()
        if not w:
            continue
        c = w[0]
        if c == 'run':
            fe.run(int(w[1]))
        elif c == 'hold':
            fe.held.add(KEYS[w[1]])
        elif c == 'release':
            fe.held.discard(KEYS[w[1]])
        elif c == 'press':
            fe.held.add(KEYS[w[1]]); fe.run(int(w[2])); fe.held.discard(KEYS[w[1]])
        elif c == 'shot':
            from PIL import Image
            Image.fromarray(fe.frame).save(os.path.join(outdir, w[1] + '.png'))
        elif c == 'screen_ok':
            ok, s = screen_ok(fe.frame)
            print('%s screen_ok: %s -> %s' % (w[1], s, 'ok' if ok else 'FAIL'))
            fails += not ok
        elif c == 'screen_changed':
            if prev is not None and np.array_equal(prev, fe.frame):
                print('FAIL: line %d: screen did not change' % ln); fails += 1
            prev = fe.frame.copy()
        elif c == 'sram':
            p, n = fe.lib.retro_get_memory_data(0), fe.lib.retro_get_memory_size(0)
            m = bytes((C.c_uint8 * n).from_address(p))
            a, k = int(w[1], 0), int(w[2])
            print('sram %05x:' % a, ' '.join('%02x' % b for b in m[a:a + k]))
        elif c == 'set':
            p, n = fe.lib.retro_get_memory_data(0), fe.lib.retro_get_memory_size(0)
            pass
        elif c == 'peek':
            a, n = int(w[1], 0), int(w[2])
            m = fe.wram()
            print('%04x:' % a, ' '.join('%02x' % b for b in m[a - 0xC000:a - 0xC000 + n]))
        else:
            print('line %d: unknown %s' % (ln, c)); return 2
    print('FAILED' if fails else 'PASSED')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
