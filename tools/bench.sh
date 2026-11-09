#!/bin/sh
# bench.sh [ROM MAP]: how many 3-D frames the DIRECT test ROM draws in fixed scenes of floor 1 (SameBoy, tools/gbtest).
#   guard / guardturn: the room of the guard at 21,47 (standing still / turning left), start / startturn: the start
#   of the floor, walk: forward from the start (the guards make it vary from build to build), hall: a hall, turning;
#   each 600 screen refreshes (10 s).  poses: 10 views close to items, 50 refreshes each.
cd "$(dirname "$0")/.." || exit 1
ROM=${1:-rom/direct.gbc}; MAP=${2:-build_d/game.map}
T=${TMPDIR:-/tmp}/wolfbench.$$; mkdir -p "$T"
GB="tools/gbtest/gbtest --boot tools/gbtest/cgb_boot.bin --sym $MAP --outdir $T $ROM"
GUARD='poke16 0xA00C 0x8000\npoke16 0xA00E 0x0017\npoke16 0xA010 0x8000\npoke16 0xA012 0x002F\nset 0xA014 23\nset 0xA015 47\npoke16 0xA01F 180\n'
HALL='poke16 0xA00C 0x8000\npoke16 0xA00E 0x001D\npoke16 0xA010 0x8000\npoke16 0xA012 0x0039\nset 0xA014 29\nset 0xA015 57\npoke16 0xA01F 90\n'
start() { printf 'run 400\nset dbg_startmap 0\nset dbg_go 1\nrun 300\nset godmode 1\n'; }
count() { $GB "$1" 2>&1 | grep -E '^[0-9a-f]{4}: ' | while read -r a lo hi; do echo $((0x$hi$lo)); done |
          { read -r x; read -r y; echo $((y - x)); }; }
scene() { { start; printf "%b" "$2"; echo "run 30"; [ -n "$3" ] && echo "hold $3"
            echo "dump refresh_count 2"; echo "run 600"; echo "dump refresh_count 2"; } > "$T/$1.gbt"
          printf '%-10s %s\n' "$1" "$(count "$T/$1.gbt")"; }
scene guard "$GUARD" ""
scene guardturn "$GUARD" LEFT
scene start "" ""
scene startturn "" RIGHT
scene walk "" UP
scene hall "$HALL" LEFT
{ start; echo "dump refresh_count 2"
  for p in "29 9 90" "39 9 90" "31 10 90" "34 10 90" "29 10 90" "39 10 90" "31 11 90" "34 11 90" "37 11 90" "8 11 90"; do
    set -- $p
    printf 'poke16 0xA00C 0x8000\npoke16 0xA00E %s\npoke16 0xA010 0x8000\npoke16 0xA012 %s\n' "$1" "$2"
    printf 'set 0xA014 %s\nset 0xA015 %s\npoke16 0xA01F %s\nrun 50\n' "$1" "$2" "$3"
  done
  echo "dump refresh_count 2"; } > "$T/poses.gbt"
printf '%-10s %s\n' poses "$(count "$T/poses.gbt")"
rm -rf "$T"
