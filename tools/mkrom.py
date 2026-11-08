#!/usr/bin/env python3
"""mkrom.py - Intel hex (SDCC sdldgb, 24 bit addresses: bank << 16 | address) -> Game Boy ROM.

usage: mkrom.py game.ihx out.gb [--data blob@bank ...] [--map game.map --sym out.sym]
Addresses below 0x4000 are bank 0; 0xBBAAAA with AAAA in 0x4000..0x7FFF is bank BB.
Pads to a power of two number of banks, patches the header (ROM size, checksums).
"""
import sys, re

def read_ihx(path):
    mem = {}
    base = 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(':'):
            continue
        n = int(line[1:3], 16)
        addr = int(line[3:7], 16)
        t = int(line[7:9], 16)
        data = bytes.fromhex(line[9:9 + 2 * n])
        if t == 0:
            for i, b in enumerate(data):
                mem[base + addr + i] = b
        elif t == 4:
            base = int.from_bytes(data, 'big') << 16
        elif t == 1:
            break
    return mem

def main():
    ihx, out = sys.argv[1], sys.argv[2]
    mem = read_ihx(ihx)
    maxbank = 1
    img = {}
    extra = []
    for a in sys.argv[3:]:
        if a.startswith('--data='):
            f, b = a[7:].rsplit('@', 1)
            extra.append((open(f, 'rb').read(), int(b)))
        elif a == '--data':
            pass
    for blob, b in extra:
        for i, v in enumerate(blob):
            img[(b + i // 0x4000, i % 0x4000)] = v
        maxbank = max(maxbank, b + (len(blob) - 1) // 0x4000)
    for a, b in mem.items():          # sdld writes flat ROM offsets: bank n at n * 0x4000
        bank, off = a >> 14, a & 0x3FFF
        maxbank = max(maxbank, bank)
        img[(bank, off)] = b
    nb = 2
    while nb < maxbank + 1:
        nb *= 2
    rom = bytearray([0xFF]) * (nb * 0x4000)
    for (bank, off), b in img.items():
        rom[bank * 0x4000 + off] = b
    # header
    code = {2: 0, 4: 1, 8: 2, 16: 3, 32: 4, 64: 5, 128: 6, 256: 7, 512: 8}[nb]
    rom[0x148] = code
    c = 0
    for i in range(0x134, 0x14D):
        c = (c - rom[i] - 1) & 0xFF
    rom[0x14D] = c
    rom[0x14E] = rom[0x14F] = 0
    s = sum(rom) & 0xFFFF
    rom[0x14E] = s >> 8
    rom[0x14F] = s & 0xFF
    open(out, 'wb').write(rom)
    print('%s: %d banks (%d KB)' % (out, nb, nb * 16))

if __name__ == '__main__':
    main()
