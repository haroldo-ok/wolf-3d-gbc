# Wolfenstein 3D for Game Boy Color

A port of the Sega Master System / Game Gear version
([haroldo-ok/wolf-3d-gg](https://github.com/haroldo-ok/wolf-3d-gg)) to the Game Boy Color: the shareware
Episode 1 (all ten floors), with the menus, help pages, high scores, three save slots, music, sound effects and
digitised voices.

* CGB only, MBC5, 4 MB ROM, 128 KB battery-backed save RAM, double speed CPU.
* Written in C with SDCC (sm83), plus assembly for the hot paths (wall and sprite columns, ray stepping,
  multiplication, VRAM copies, the line interrupts, the sampled voice).

## Controls

| Button | In the game | In the menus |
| --- | --- | --- |
| D-pad | Up/Down walk, Left/Right turn | move the cursor |
| A | fire | select / next help page |
| B | open doors, push walls; held with Left/Right: strafe | back |
| SELECT | next weapon | previous help page |
| START | the menu (save, load, sound, quit) | |

## Building

Requirements: `make`, `python3` with `numpy` and `Pillow`, SDCC 4.2 (`sdcc -msm83`), `gcc` (for the test harness),
`curl`, `unzip` and `git` (to fetch the data).

    tools/fetch_data.sh      # the shareware data (archive.org) and id's source release (names, palette) into ../work
    make                     # -> rom/wolf3d.gbc

The id Software data is not part of this source tree; the converters read it from `../work` (or from the
directories in `WL_DATA` and `WL_SRC`, see `tools/paths.py`) and generate `gen/` (about 70 seconds).

Other targets:

    make direct     # rom/direct.gbc: test ROM that waits for the test to choose a level (dbg_startmap, dbg_go, dbg_god)
    make prof       # build_p/: test ROM with every symbol (game.cdb), for the profiler commands of tools/gbtest
    make test       # rom/wolf3d.gbc and rom/direct.gbc, then tests/run_all.sh
    make clean

`tools/checkmap.py` runs on every link: it fails the build when the fixed bank (bank 0) or any switchable bank
overflows, or when too little WRAM is left for the stack.

## Tests

`tests/run_all.sh` drives the ROMs in SameBoy through `tools/gbtest` (a headless front end; build it with
`make -C tools/gbtest SB=/path/to/SameBoy`, after building SameBoy's static library). Every screenshot must be
neither black nor white, with enough distinct colours.

| Test | What it checks |
| --- | --- |
| `t_boot_menu` | boot, PG-13, the hi-colour title (at least 64 colours), music, the menu, the sound page |
| `t_menu_screens` | Load Game, Read This! (help pages), View Scores |
| `t_newgame` | New Game, Get Psyched, the 3D view, turning, walking, shooting, the stack |
| `t_pause_menu` | START in the game, back to the game |
| `t_controls` | Down walks back, Up forward, a short tap turns a little, the view past 90 / 180 / 270 degrees |
| `t_enemies` | a guard is drawn and can be killed |
| `t_walk` | a long random walk (`tools/randwalk.py`): the game keeps drawing frames |
| `t_intermission` | the floor-completed screen and the next floor |
| `t_levels.sh` | each of the ten floors starts, draws, turns and walks |

`tools/gbtest` also has debugging commands: `prof`, `incl`, `hot`, `calls` (profiling, with the static symbols of a
`make prof` build), `progress` (hang detection), `watch`, `gdmalog`, `vbltrace`, `crash`, `minsp`.
`tools/retro.py` runs the same kind of script in a libretro core (mGBA was used to cross-check).

## How it works

* **3D view**: 160x96 pixels, 40 rays of 4 pixels, drawn column-major into a frame buffer in WRAM bank 3 and
  copied to VRAM by general purpose DMA during VBlank (chunks of 12 blocks, started only while LY is 144..150), into
  the tile set and BG map that are not on screen; the two halves are flipped when the copy is done. Each 8x8 tile
  has one palette, so a wall family or a sprite gets a whole tile column (sprites only on the rows they cover).
* **Line interrupts** switch the ceiling colour to the floor colour at the horizon and load the HUD tiles and
  palettes below the view.
* **Hi-colour title**: the BG palettes are rewritten in every HBlank (3 palettes a line, `hc_frame` in
  `src/hw_a.s`), so each set of 4 colours covers only two lines of a few tiles: about 400 colours on screen.
  `tools/gb_hicolor.py` converts the picture.
* **Sound**: the Game Gear's SN76489 event streams play on the APU (tone channels to the wave channel and the two
  pulse channels, noise to noise); the digitised voices are 3-bit samples written to the master volume by the
  timer interrupt at 8 kHz.
* **Input** is sampled at every frame interrupt; turning and walking follow how long a direction was held since
  the last game frame, and a quick tap of A or B is not lost.
* The game logic (`src/game/`) is id Software's code by way of the Genesis and Game Gear ports.

Speed: the 3D view draws a new frame every ~9 screen refreshes (about 6-7 frames per second), limited by the CPU
(ray casting, wall columns and sprites).

### SDCC 4.2 pitfalls met on the way

* An inline `call` in C hides the call from the register allocator (it kept HL across it): call asm functions.
* Some `s32` expressions were miscompiled (an argument of `Thrust`, `obj->y >> 8`): rewritten with temporaries or
  byte access.
* The banked call trampoline (`___sdcc_bcall_ehl` in `src/crt0.s`) must keep BC and DE: with `sdcccall(1)` they
  hold 16 and 32 bit return values.
* `u16` products overflow quietly (degrees x 728 above 90 degrees).

## Licence notes

Wolfenstein 3D is (c) id Software. The game logic derives from id's source release (GPL); the graphics, maps,
sounds and music are id's shareware assets, which this repository does not contain: they are downloaded by
`tools/fetch_data.sh` and converted at build time. A built ROM contains them, so it is for personal use only.
