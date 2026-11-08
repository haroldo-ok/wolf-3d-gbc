/* gbc.h - Game Boy Color hardware layer (SDCC sm83, no GBDK) */
#ifndef GBC_H
#define GBC_H

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef signed int s16;
typedef unsigned long u32;
typedef signed long s32;
#ifndef NULL
#define NULL ((void *)0)
#endif

/* ---- I/O registers (ldh) */
__sfr __at(0x00) rP1;
__sfr __at(0x01) rSB;
__sfr __at(0x02) rSC;
__sfr __at(0x04) rDIV;
__sfr __at(0x05) rTIMA;
__sfr __at(0x06) rTMA;
__sfr __at(0x07) rTAC;
__sfr __at(0x0F) rIF;
__sfr __at(0x10) rNR10;
__sfr __at(0x11) rNR11;
__sfr __at(0x12) rNR12;
__sfr __at(0x13) rNR13;
__sfr __at(0x14) rNR14;
__sfr __at(0x16) rNR21;
__sfr __at(0x17) rNR22;
__sfr __at(0x18) rNR23;
__sfr __at(0x19) rNR24;
__sfr __at(0x1A) rNR30;
__sfr __at(0x1B) rNR31;
__sfr __at(0x1C) rNR32;
__sfr __at(0x1D) rNR33;
__sfr __at(0x1E) rNR34;
__sfr __at(0x20) rNR41;
__sfr __at(0x21) rNR42;
__sfr __at(0x22) rNR43;
__sfr __at(0x23) rNR44;
__sfr __at(0x24) rNR50;
__sfr __at(0x25) rNR51;
__sfr __at(0x26) rNR52;
__sfr __at(0x40) rLCDC;
__sfr __at(0x41) rSTAT;
__sfr __at(0x42) rSCY;
__sfr __at(0x43) rSCX;
__sfr __at(0x44) rLY;
__sfr __at(0x45) rLYC;
__sfr __at(0x46) rDMA;
__sfr __at(0x47) rBGP;
__sfr __at(0x48) rOBP0;
__sfr __at(0x49) rOBP1;
__sfr __at(0x4A) rWY;
__sfr __at(0x4B) rWX;
__sfr __at(0x4D) rKEY1;
__sfr __at(0x4F) rVBK;
__sfr __at(0x51) rHDMA1;
__sfr __at(0x52) rHDMA2;
__sfr __at(0x53) rHDMA3;
__sfr __at(0x54) rHDMA4;
__sfr __at(0x55) rHDMA5;
__sfr __at(0x68) rBCPS;
__sfr __at(0x69) rBCPD;
__sfr __at(0x6A) rOCPS;
__sfr __at(0x6B) rOCPD;
__sfr __at(0x70) rSVBK;
__sfr __at(0xFF) rIE;

/* MBC5 (memory mapped) */
volatile __at(0x0000) u8 mRAMG;      /* 0x0A enables cart RAM */
volatile __at(0x2000) u8 mROMB0;
volatile __at(0x3000) u8 mROMB1;
volatile __at(0x4000) u8 mRAMB;

#define LCDC_ON      0x80
#define LCDC_WIN9C00 0x40
#define LCDC_WINON   0x20
#define LCDC_BGDATA8000 0x10
#define LCDC_BG9C00  0x08
#define LCDC_OBJ16   0x04
#define LCDC_OBJON   0x02
#define LCDC_BGON    0x01

#define STAT_LYC_INT 0x40
#define STAT_HBL_INT 0x08
#define STAT_VBL_INT 0x10
#define STAT_OAM_INT 0x20

#define IE_VBLANK 0x01
#define IE_STAT   0x02
#define IE_TIMER  0x04
#define IE_SERIAL 0x08
#define IE_JOYPAD 0x10

/* joypad bits: low nibble d-pad, high nibble buttons (active HIGH after pad_read) */
#define K_RIGHT  0x01
#define K_LEFT   0x02
#define K_UP     0x04
#define K_DOWN   0x08
#define K_A      0x10
#define K_B      0x20
#define K_SELECT 0x40
#define K_START  0x80

#define VRAM_TILES0   0x8000
#define VRAM_TILES1   0x8800
#define VRAM_TILES2   0x9000
#define VRAM_MAP0     0x9800
#define VRAM_MAP1     0x9C00

#define di() __asm__("di")
#define ei() __asm__("ei")
#define halt() __asm__("halt\n nop")
#define nop() __asm__("nop")

/* ---- banking (hw.c / crt0.s) */
extern volatile u8 cur_rom_bank;     /* mirror of the ROM bank in 0x4000-0x7FFF */
extern volatile u8 cur_ram_bank;     /* mirror of the SRAM bank */
extern volatile u8 cur_wram_bank;
void rom_bank(u8 b);                 /* bank 0 */
u8   rom_bank_swap(u8 b);            /* returns the old bank */
void ram_bank(u8 b);
void wram_bank(u8 b);
#define ROMB(b) do { cur_rom_bank = (b); mROMB0 = (b); } while (0)

extern volatile u8 vbl_count;        /* incremented at every VBlank */

#endif
