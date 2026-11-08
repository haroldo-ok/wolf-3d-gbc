#!/usr/bin/env python3
"""randwalk.py LEVEL SEED SEGMENTS > script.gbt: a long random walk for the DIRECT test ROM (god mode); every
segment checks the game still draws frames (refresh_count changes) and that it did not crash."""
import random, sys
lv, seed, n = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
r = random.Random(seed)
print('run 400\nset dbg_god 1\nset dbg_startmap %d\nset dbg_go 1\nrun 400' % lv)
for i in range(n):
    k = r.choice(['UP', 'UP', 'UP', 'LEFT', 'RIGHT', 'DOWN'])
    print('hold %s\nrun %d\nrelease %s' % (k, r.randint(20, 120), k))
    if r.random() < 0.3:
        print('press B 10')
    if r.random() < 0.2:
        print('press A 10')
    print('run 10\nprogress refresh_count 120')
print('shot walk_end\nscreen_ok walk_end')
