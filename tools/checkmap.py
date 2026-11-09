"""checkmap.py - fails the build when the fixed bank overflows into 0x4000, a bank is over 16 KB or WRAM0 data eats the stack."""
import re
import sys

t = open(sys.argv[1]).read()
v = {}
for m in re.finditer(r'^\s+([0-9A-F]{8})\s+(s__\w+|l__\w+)\s*$', t, re.M):
    v[m.group(2)] = int(m.group(1), 16)
ok = True
home_end = v['s__HOME'] + v['l__HOME']
tail = max(home_end, v.get('s__GSINIT', 0) + v.get('l__GSINIT', 0), v.get('s__GSFINAL', 0) + v.get('l__GSFINAL', 0))
print('bank 0 ends at 0x%04X (%d bytes free)' % (tail, 0x4000 - tail))
if tail > 0x4000:
    print('ERROR: the fixed bank overflows'); ok = False
for k, x in sorted(v.items()):
    if k.startswith('l__BANK') and x > 0x4000:
        print('ERROR: %s is %d bytes' % (k, x)); ok = False
data_end = v['s__DATA'] + v['l__DATA'] + v.get('l__INITIALIZED', 0)
print('WRAM0 data ends at 0x%04X: %d bytes for the stack' % (data_end, 0xD000 - data_end))
if 0xD000 - data_end < 340:
    print('ERROR: not enough stack'); ok = False
m = re.search(r'^\s+([0-9A-F]{8})\s+_cr_sq\b', t, re.M)       # src/cast_a.s: the quarter squares, page aligned
if m and int(m.group(1), 16) & 0xFFFF != 0x4000:
    print('ERROR: _cr_sq is at %s, not at 0x4000 of its bank (src/cast_a.s must be the first of bank 5)' % m.group(1)); ok = False
sys.exit(0 if ok else 1)
