SB ?= /home/claude/ext/SameBoy
all: gbtest cgb_boot.bin
gbtest: gbtest.c
	gcc -O2 -o $@ $< -I$(SB) -DGB_INTERNAL $(SB)/build/lib/libsameboy.a -lm -lpthread
cgb_boot.bin: $(SB)/build/bin/BootROMs/cgb_boot.bin
	cp $< $@
