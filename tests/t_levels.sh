#!/bin/sh
# DIRECT test ROM (make direct): start every one of the ten levels, check the view, turn and walk
cd "$(dirname "$0")/.." || exit 1
OUT=${OUT:-/tmp/gbtest}; mkdir -p "$OUT"
rc=0
for L in 0 1 2 3 4 5 6 7 8 9; do
cat > "$OUT/level$L.gbt" <<EOT
run 400
set dbg_startmap $L
set dbg_go 1
run 400
shot level$L
screen_ok level$L
hold LEFT
run 60
screen_changed
release LEFT
shot level${L}_turn
screen_ok level${L}_turn
hold UP
run 40
screen_ok level${L}_walk
release UP
audio_ok
EOT
echo "level $((L+1))"
tools/gbtest/gbtest --boot tools/gbtest/cgb_boot.bin --sym build_d/game.map --outdir "$OUT" rom/direct.gbc "$OUT/level$L.gbt" > "$OUT/level$L.log" 2>&1 || { rc=1; tail -3 "$OUT/level$L.log"; }
done
exit $rc
