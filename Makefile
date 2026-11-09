# Wolfenstein 3D for the Game Boy Color (SDCC sm83, no GBDK)
CC      = sdcc
AS      = sdasgb
OUT     = build
ROM     = rom/wolf3d.gbc
CFLAGS  = $(EXTRA) -msm83 --opt-code-speed -Isrc -Isrc/game -Igen --max-allocs-per-node 20000 --peep-asm
DATA_BANK = 40
SB      ?= /home/claude/ext/SameBoy

FIXED   = src/main.c src/hw.c src/mathz.c src/level.c src/plat0.c src/fastc.c gen/assets_tab.c src/game/states.c
BANK1   = src/game/wl_play.c src/game/wl_main.c src/game/wl_game.c gen/rndtable.c
BANK2   = src/game/wl_agent.c
BANK3   = src/game/wl_state.c src/game/wl_act1.c
BANK4   = src/game/wl_act2.c
BANK5   = src/render.c gen/assets_r.c
BANK6   = src/vid.c src/hud.c src/weapon.c src/input.c gen/assets_h.c
BANK7   = src/ui.c src/menu.c src/inter.c gen/assets_u.c
BANK8   = src/rspr.c gen/assets_p.c
BANK9   = src/save.c
BANK10  = src/snd.c gen/assets_s.c
BANKS   = $(foreach b,1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20,$(if $(BANK$(b)),$(b)))

rel = $(OUT)/$(notdir $(1:.c=.rel))
FIXED_REL  = $(foreach f,$(FIXED),$(call rel,$(f)))
BANKED_REL = $(foreach b,$(BANKS),$(foreach f,$(BANK$(b)),$(call rel,$(f))))
ASM_REL    = $(OUT)/crt0.rel $(OUT)/hw_a.rel $(OUT)/cartsyms.rel $(OUT)/mathz_a.rel $(OUT)/render_a.rel $(OUT)/snd_a.rel \
             $(OUT)/cast_a.rel
HDRS       = $(wildcard src/*.h src/game/*.h) gen/assets.h

all: $(ROM)

$(OUT):
	mkdir -p $(OUT) rom

gen/assets.h gen/assets_tab.c gen/assets_r.c gen/assets_p.c gen/assets_h.c gen/assets_u.c gen/assets_s.c gen/rndtable.c gen/data.bin: tools/*.py
	cd tools && python3 build_assets.py

$(OUT)/%.rel: src/%.s | $(OUT)
	$(AS) -plosgff -o $@ $<

define FIXED_RULE
$(call rel,$(1)): $(1) $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) -c -o $$@ $$<
endef
define BANK_RULE
$(call rel,$(1)): $(1) $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) --codeseg BANK$(2) --constseg BANK$(2) -c -o $$@ $$<
endef
$(foreach f,$(FIXED),$(eval $(call FIXED_RULE,$(f))))
$(OUT)/level.rel $(OUT)/plat0.rel: CFLAGS += --opt-code-size
$(foreach b,$(BANKS),$(foreach f,$(BANK$(b)),$(eval $(call BANK_RULE,$(f),$(b)))))

BANK_LOCS = $(foreach b,$(BANKS),-Wl-b_BANK$(b)=0x$(shell printf "%x" $(b))4000)

$(OUT)/game.ihx: $(ASM_REL) $(FIXED_REL) $(BANKED_REL)
	$(CC) -msm83 $(LDEXTRA) --no-std-crt0 --code-loc 0x0200 --data-loc 0xC3A0 $(BANK_LOCS) -o $@ $(ASM_REL) $(FIXED_REL) $(BANKED_REL)

$(ROM): $(OUT)/game.ihx gen/data.bin
	python3 tools/checkmap.py $(OUT)/game.map
	python3 tools/mkrom.py $(OUT)/game.ihx $(ROM) --data=gen/data.bin@$(DATA_BANK)

clean:
	rm -rf $(OUT) $(ROM)

direct:
	$(MAKE) EXTRA=-DDIRECT OUT=build_d ROM=rom/direct.gbc

prof:
	$(MAKE) EXTRA="-DDIRECT --debug" LDEXTRA="--debug" OUT=build_p ROM=rom/prof.gbc

test: all direct
	sh tests/run_all.sh
