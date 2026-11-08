#!/bin/sh
# end-to-end tests with SameBoy (tools/gbtest): every screen must be neither black nor white.
#   make            -> rom/wolf3d.gbc
#   make direct     -> rom/direct.gbc  (test ROM that starts on a chosen level)
cd "$(dirname "$0")/.." || exit 1
OUT=${OUT:-/tmp/gbtest}; mkdir -p "$OUT"
GB="tools/gbtest/gbtest --boot tools/gbtest/cgb_boot.bin --outdir $OUT"
rc=0
for t in boot_menu menu_screens newgame pause_menu; do
  printf '%-16s' "$t"
  if $GB --sym build/game.map rom/wolf3d.gbc tests/t_$t.gbt > "$OUT/$t.log" 2>&1; then echo PASS; else echo FAIL; tail -4 "$OUT/$t.log"; rc=1; fi
done
printf '%-16s' controls
if $GB --sym build_d/game.map rom/direct.gbc tests/t_controls.gbt > "$OUT/controls.log" 2>&1; then echo PASS; else echo FAIL; tail -4 "$OUT/controls.log"; rc=1; fi
printf '%-16s' enemies
if $GB --sym build_d/game.map rom/direct.gbc tests/t_enemies.gbt > "$OUT/enemies.log" 2>&1; then echo PASS; else echo FAIL; tail -4 "$OUT/enemies.log"; rc=1; fi
printf '%-16s' walk
if $GB --sym build_d/game.map rom/direct.gbc tests/t_walk.gbt > "$OUT/walk.log" 2>&1; then echo PASS; else echo FAIL; grep -A3 "HUNG\|FAIL" "$OUT/walk.log" | head; rc=1; fi
printf '%-16s' intermission
if $GB --sym build_d/game.map rom/direct.gbc tests/t_intermission.gbt > "$OUT/intermission.log" 2>&1; then echo PASS; else echo FAIL; tail -4 "$OUT/intermission.log"; rc=1; fi
sh tests/t_levels.sh || rc=1
[ $rc = 0 ] && echo "ALL PASSED" || echo "FAILURES"
exit $rc
