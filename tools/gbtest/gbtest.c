/* gbtest.c - headless end-to-end test driver built on the SameBoy core.
 *
 *   gbtest [options] ROM SCRIPT
 *     --boot FILE   CGB boot ROM (default: cgb_boot.bin next to the executable)
 *     --sav FILE    battery file: loaded at start if it exists, written at the end
 *     --sym FILE    SDCC/ld map or "ADDR NAME" symbol file, lets scripts use names
 *     --outdir DIR  where "shot" writes its files (default .)
 *
 * Script commands (one per line, '#' starts a comment):
 *   run N                 run N frames
 *   press KEYS N          hold KEYS (A+B+START+SELECT+UP+DOWN+LEFT+RIGHT) for N frames, then release
 *   hold KEYS / release KEYS
 *   shot NAME             write NAME.ppm of the current screen
 *   screen_ok [LABEL [MINCOLOURS]]  FAIL unless the screen is neither (nearly) all black nor all white, has several
 *                         distinct colours and is not one flat colour
 *   screen_changed        FAIL unless the screen differs from the one at the previous screen_changed/shot
 *   expect ADDR VAL       FAIL unless the byte at ADDR (number or symbol[+off]) equals VAL
 *   expect16 ADDR VAL     same, little endian 16 bit
 *   expect_ne ADDR VAL    byte differs from VAL
 *   expect_ge ADDR VAL    byte >= VAL (unsigned)
 *   expect16_ge ADDR VAL  16 bit >= VAL
 *   set ADDR VAL          write a byte (RAM poke, for test set up)
 *   poke16 ADDR VAL       write 16 bits
 *   wait_until ADDR VAL N run up to N frames until the byte at ADDR equals VAL, FAIL when it never does
 *   wait_ne ADDR VAL N    run up to N frames until the byte at ADDR differs from VAL, FAIL when it never does
 *   wait_screen N         run up to N frames until the screen is ok (as screen_ok), FAIL otherwise
 *   audio_ok              FAIL unless sound samples were produced since the last audio_reset (non silent)
 *   audio_reset
 *   save_state / load_state NAME  (battery ram is also available through --sav)
 *   print ADDR            print the byte
 *   log TEXT
 * Exit status 0: all checks passed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <Core/gb.h>

static GB_gameboy_t gb;
static uint32_t pixels[256 * 224];
static uint32_t prev_pixels[160 * 144];
static int fails = 0;
static char outdir[512] = ".";
static unsigned long frames_run = 0;
static uint64_t audio_energy = 0;
static unsigned long audio_samples = 0;

typedef struct { char name[96]; unsigned addr; } Sym;
static Sym *syms; static int nsyms;

static uint32_t rgb_encode(GB_gameboy_t *g, uint8_t r, uint8_t b2, uint8_t b) { (void)g; return (r << 16) | (b2 << 8) | b; }
static void log_cb(GB_gameboy_t *g, const char *s, GB_log_attributes_t a) { (void)g; (void)a; fprintf(stderr, "[gb] %s", s); }
static void audio_cb(GB_gameboy_t *g, GB_sample_t *s)
{
    (void)g;
    audio_samples++;
    audio_energy += (uint64_t)abs(s->left) + (uint64_t)abs(s->right);
}

static void load_syms(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    if (!f) { fprintf(stderr, "cannot read symbols %s\n", path); exit(2); }
    while (fgets(line, sizeof line, f))
    {
        unsigned a; char n[160];
        /* "ADDR NAME" (hex, no 0x) or sdld map "     0000C000  _name" */
        if (sscanf(line, " %x %159s", &a, n) == 2 && isxdigit((unsigned char)line[strspn(line, " ")]))
        {
            syms = realloc(syms, sizeof(Sym) * (nsyms + 1));
            snprintf(syms[nsyms].name, sizeof syms[nsyms].name, "%s", n);
            syms[nsyms].addr = a;
            nsyms++;
        }
    }
    fclose(f);
    /* the static functions too, from the debug file of the link (sdcc --debug), when there is one */
    {
        char cdb[1024]; size_t n = strlen(path);
        snprintf(cdb, sizeof cdb, "%.*s.cdb", (int)(n > 4 ? n - 4 : n), path);
        f = fopen(cdb, "r");
        if (f)
        {
            while (fgets(line, sizeof line, f))
            {
                char *d1, *d2, *colon; unsigned a;
                if (strncmp(line, "L:F", 3)) continue;              /* L:F<file>$<func>$..:ADDR */
                d1 = strchr(line, '$'); if (!d1) continue;
                d2 = strchr(d1 + 1, '$'); colon = strrchr(line, ':');
                if (!d2 || !colon || sscanf(colon + 1, "%x", &a) != 1) continue;
                syms = realloc(syms, sizeof(Sym) * (nsyms + 1));
                snprintf(syms[nsyms].name, sizeof syms[nsyms].name, "_%.*s", (int)(d2 - d1 - 1), d1 + 1);
                syms[nsyms].addr = a;
                nsyms++;
            }
            fclose(f);
        }
    }
}

static long parse_addr(const char *s)
{
    char name[160]; long off = 0; const char *p;
    if (isdigit((unsigned char)s[0]) || (s[0] == '-' ) ) return strtol(s, NULL, 0);
    snprintf(name, sizeof name, "%s", s);
    p = strchr(name, '+');
    if (p) { off = strtol(p + 1, NULL, 0); *(char *)p = 0; }
    for (int i = 0; i < nsyms; i++)
        if (!strcmp(syms[i].name, name) || (syms[i].name[0] == '_' && !strcmp(syms[i].name + 1, name)) ||
            (name[0] == '_' && !strcmp(syms[i].name, name + 1)))
            return syms[i].addr + off;
    fprintf(stderr, "unknown symbol '%s'\n", s);
    exit(2);
}

static unsigned keys_from(const char *s)
{
    unsigned m = 0; char buf[128]; char *t;
    snprintf(buf, sizeof buf, "%s", s);
    for (t = strtok(buf, "+,"); t; t = strtok(NULL, "+,"))
    {
        if (!strcasecmp(t, "A")) m |= 1u << GB_KEY_A;
        else if (!strcasecmp(t, "B")) m |= 1u << GB_KEY_B;
        else if (!strcasecmp(t, "START")) m |= 1u << GB_KEY_START;
        else if (!strcasecmp(t, "SELECT")) m |= 1u << GB_KEY_SELECT;
        else if (!strcasecmp(t, "UP")) m |= 1u << GB_KEY_UP;
        else if (!strcasecmp(t, "DOWN")) m |= 1u << GB_KEY_DOWN;
        else if (!strcasecmp(t, "LEFT")) m |= 1u << GB_KEY_LEFT;
        else if (!strcasecmp(t, "RIGHT")) m |= 1u << GB_KEY_RIGHT;
        else if (strcasecmp(t, "NONE")) { fprintf(stderr, "bad key %s\n", t); exit(2); }
    }
    return m;
}
static void set_keys(unsigned m, bool on)
{
    for (int k = 0; k < GB_KEY_MAX; k++)
        if (m & (1u << k)) GB_set_key_state(&gb, (GB_key_t)k, on);
}


static Sym *code_syms; static int ncode;
static int sym_cmp(const void *x, const void *y) { unsigned a = ((const Sym *)x)->addr, b = ((const Sym *)y)->addr; return a < b ? -1 : a > b; }
static const char *sym_at(unsigned pc, unsigned bank, unsigned *off)
{
    unsigned want = pc < 0x4000 ? pc : (bank << 16) | pc;
    int lo = 0, hi, bi = -1;
    if (!code_syms)
    {
        code_syms = malloc(sizeof(Sym) * (nsyms + 1));
        for (int i = 0; i < nsyms; i++)
        {
            unsigned a = syms[i].addr;
            if (syms[i].name[0] == '_' && (a < 0x4000 || (a & 0xFFFF) >= 0x4000) && (a & 0xFFFF) < 0x8000)
                code_syms[ncode++] = syms[i];
        }
        qsort(code_syms, ncode, sizeof(Sym), sym_cmp);
    }
    hi = ncode - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (code_syms[m].addr <= want) { bi = m; lo = m + 1; } else hi = m - 1; }
    if (bi < 0) return "?";
    if (pc >= 0x4000 && (code_syms[bi].addr >> 16) != bank) return "?";
    *off = want - code_syms[bi].addr;
    return code_syms[bi].name;
}

/* run until the CPU leaves the code (RAM, data bank): prints the last instructions executed */
static void crash_hunt(long frames)
{
    enum { N = 400 };
    unsigned rp[N], rb[N]; int ri = 0; unsigned long steps = 0;
    unsigned long limit = (unsigned long)frames * 140000;
        (void)limit;
    for (long f = 0; f < frames; f++)
    {
        unsigned long c = 0;
        while (c < 140000)
        {
            unsigned pc = gb.pc, bank = gb.mbc_rom_bank;
            rp[ri % N] = pc; rb[ri % N] = bank; ri++;
            if ((gb.sp >= 0xC000 && gb.sp < 0xC800) || (pc >= 0xC000 && pc < 0xFE00) || (pc >= 0x4000 && pc < 0x8000 && bank >= 40) || (steps > 100 && pc == 0x38))
            {
                printf("CRASH at PC=%04x bank=%u SP=%04x after %lu frames\n", pc, bank, gb.sp, (unsigned long)f);
                for (int i = ri > N ? ri - N : 0; i < ri; i++)
                {
                    unsigned off = 0; const char *n = sym_at(rp[i % N], rb[i % N], &off);
                    printf("  %04x/%02x %s+%u\n", rp[i % N], rb[i % N], n, off);
                }
                return;
            }
            c += GB_run(&gb) / (gb.cgb_double_speed ? 2 : 1);
            steps++;
        }
    }
    printf("no crash in %ld frames\n", frames);
}

/* run N frames single stepping and report the lowest SP seen in WRAM0 (stack high water mark) */
static unsigned min_sp_seen = 0xFFFF;
static void minsp_run(long frames)
{
    for (long f = 0; f < frames; f++)
    {
        unsigned long c = 0;
        while (c < 140000)
        {
            unsigned sp = gb.sp;
            if (sp >= 0xC000 && sp <= 0xD000 && sp < min_sp_seen) min_sp_seen = sp;
            c += GB_run(&gb) / (gb.cgb_double_speed ? 2 : 1);
        }
    }
    printf("minsp: lowest SP = %04x (%u bytes below D000)\n", min_sp_seen, 0xD000 - min_sp_seen);
}


/* profile FRAMES: where the CPU time goes (cycles per function, from the map file) */
static void prof_run(long frames)
{
    enum { MAXF = 4096 };
    static char names[MAXF][40]; static unsigned long cyc[MAXF]; int nf = 0; unsigned long total = 0, idle = 0;
    for (long f = 0; f < frames; f++)
    {
        unsigned long c = 0;
        while (c < 140000)
        {
            unsigned pc = gb.pc, bank = gb.mbc_rom_bank, off; const char *nm;
            unsigned n = GB_run(&gb) / (gb.cgb_double_speed ? 2 : 1);
            c += n; total += n;
            nm = sym_at(pc, bank, &off);
            if (!nm) nm = "?";
            if (gb.halted) { idle += n; continue; }
            int i; for (i = 0; i < nf; i++) if (!strcmp(names[i], nm)) break;
            if (i == nf && nf < MAXF) { snprintf(names[nf], 40, "%s", nm); cyc[nf++] = 0; }
            if (i < nf) cyc[i] += n;
        }
    }
    for (int k = 0; k < 45; k++)
    {
        int b = -1; for (int i = 0; i < nf; i++) if (cyc[i] && (b < 0 || cyc[i] > cyc[b])) b = i;
        if (b < 0) break;
        printf("  %5.1f%%  %s\n", 100.0 * cyc[b] / total, names[b]); cyc[b] = 0;
    }
    printf("  %5.1f%%  (halted: waiting for the frame interrupt)\n", 100.0 * idle / total);
}


/* isrtime FRAMES: the LY at which the frame interrupt starts and ends (is the work done inside VBlank?) */
static void isrtime_run(long frames)
{
    int in = 0, entry = 0, shown = 0;
    for (long f = 0; f < frames; f++)
    {
        unsigned long c = 0;
        while (c < 140000)
        {
            unsigned pc = gb.pc;
            if (!in && pc == 0x40) { in = 1; entry = GB_read_memory(&gb, 0xFF44); }
            else if (in && GB_read_memory(&gb, pc) == 0xD9 && gb.sp > 0xCE00) { /* reti of the outermost handler */
                if (shown < 12) printf("  isr: LY %d -> %d\n", entry, GB_read_memory(&gb, 0xFF44)), shown++;
                in = 0;
            }
            {
                int dma = GB_read_memory(&gb, pc) == 0xE0 && GB_read_memory(&gb, pc + 1) == 0x55;
                unsigned n = GB_run(&gb);
                if (dma && shown < 40) printf("  hdma5 write: %u cycles (raw), ds=%d\n", n, gb.cgb_double_speed), shown++;
                c += n / (gb.cgb_double_speed ? 2 : 1);
            }
        }
    }
}


/* dmacost FRAMES: cycles (in dots) from each write to HDMA5 to the next instruction, and the LY then */
static void dmacost_run(long frames)
{
    for (long f = 0; f < frames; f++)
    {
        unsigned long c = 0; unsigned blocks = 0, chunks = 0, lymax = 0;
        while (c < 140000)
        {
            unsigned pc = gb.pc;
            if (GB_read_memory(&gb, pc) == 0xE0 && GB_read_memory(&gb, pc + 1) == 0x55)
            {
                blocks += ((gb.af >> 8) & 0x7F) + 1; chunks++;
                unsigned ly = GB_read_memory(&gb, 0xFF44); if (ly < 144) ly += 154; if (ly > lymax) lymax = ly;
            }
            c += GB_run(&gb) / (gb.cgb_double_speed ? 2 : 1);
        }
        printf("  frame %ld: %u blocks in %u chunks, last chunk at LY %u\n", f, blocks, chunks, lymax);
    }
}


/* vbltrace: one frame interrupt, the functions it goes through with the LY at each change */
static void vbltrace_run(void)
{
    const char *last = ""; unsigned long steps = 0;
    while (gb.pc != 0x40) GB_run(&gb);
    while (steps++ < 200000)
    {
        unsigned off; const char *nm = sym_at(gb.pc, gb.mbc_rom_bank, &off);
        if (gb.pc >= 0xFF80) nm = "(hram)";
        if (strcmp(nm, last)) { printf("  LY %3d  %s\n", GB_read_memory(&gb, 0xFF44), nm); last = nm; }
        if (GB_read_memory(&gb, gb.pc) == 0xD9 && gb.pc < 0x100) break;    /* reti of the vector */
        GB_run(&gb);
    }
}


/* calls SYMBOL FRAMES: each call of the function with the frame number, its caller and DE/HL/BC */
static void calls_run(const char *name, long frames)
{
    unsigned a = (unsigned)parse_addr(name) & 0xFFFF; unsigned long fr = 0, c = 0;
    while (fr < (unsigned long)frames)
    {
        if (gb.pc == a)
        {
            unsigned sp = gb.sp, ret = GB_read_memory(&gb, sp) | (GB_read_memory(&gb, sp + 1) << 8), off;
            const char *who = sym_at(ret, gb.mbc_rom_bank, &off);
            if (!strcmp(who, "___sdcc_bcall_ehl") || !strcmp(who, "___sdcc_call_hl"))
            {
                unsigned r2 = GB_read_memory(&gb, sp + 4) | (GB_read_memory(&gb, sp + 5) << 8);
                who = sym_at(r2, GB_read_memory(&gb, sp + 3), &off);
            }
            printf("  vbl %lu LY %u: %s from %s  HL=%04x DE=%04x BC=%04x\n", fr, GB_read_memory(&gb, 0xFF44), name, who, gb.hl, gb.de, gb.bc);
        }
        unsigned n = GB_run(&gb) / (gb.cgb_double_speed ? 2 : 1);
        c += n; if (c >= 70224) { c -= 70224; fr++; }
    }
}


/* incl FUNC FRAMES: the share of the CPU time spent inside FUNC and what it calls (interrupts included) */
static void incl_run(const char *name, long frames)
{
    unsigned long full = (unsigned long)parse_addr(name); unsigned a = full & 0xFFFF, bank = full >> 16;
    unsigned long total = 0, in = 0, ncalls = 0; int inside = 0; unsigned sp0 = 0;
    while (total < (unsigned long)frames * 70224)
    {
        if (!inside && gb.pc == a && (a < 0x4000 || gb.mbc_rom_bank == bank)) { inside = 1; sp0 = gb.sp; ncalls++; }
        unsigned n = GB_run(&gb) / (gb.cgb_double_speed ? 2 : 1);
        total += n;
        if (inside) { in += n; if (gb.sp > sp0) inside = 0; }
    }
    printf("  %-24s %5.1f%%  (%lu calls, %.2f frames each)\n", name, 100.0 * in / total, ncalls, ncalls ? (double)in / ncalls / 70224 : 0);
}


/* hot FUNC FRAMES: the most executed addresses (cycles) in FUNC's bank between FUNC and FUNC + 0x1000 */
static void hot_run(const char *name, long frames)
{
    unsigned long full = (unsigned long)parse_addr(name); unsigned a = full & 0xFFFF, bank = full >> 16;
    static unsigned long hist[0x1000]; unsigned long total = 0;
    memset(hist, 0, sizeof hist);
    while (total < (unsigned long)frames * 70224)
    {
        unsigned pc = gb.pc, b = gb.mbc_rom_bank;
        unsigned n = GB_run(&gb) / (gb.cgb_double_speed ? 2 : 1);
        total += n;
        if (pc >= a && pc < a + 0x1000 && (a < 0x4000 || a >= 0x8000 || b == bank)) hist[pc - a] += n;
    }
    if (getenv("HOTALL")) { FILE *hf = fopen(getenv("HOTALL"), "a"); for (int i = 0; i < 0x1000; i++) if (hist[i]) fprintf(hf, "%x %04x %lu %lu\n", bank, a + i, hist[i], total); fclose(hf); return; }
    for (int k = 0; k < 40; k++)
    {
        int bi = -1; for (int i = 0; i < 0x1000; i++) if (hist[i] && (bi < 0 || hist[i] > hist[bi])) bi = i;
        if (bi < 0) break;
        printf("  %04x (+%03x) %5.2f%%\n", a + bi, bi, 100.0 * hist[bi] / total); hist[bi] = 0;
    }
}


/* gdmalog FRAMES: every write to HDMA5: source, destination, VRAM bank, ROM bank, length, LY */
static void gdmalog_run(long frames)
{
    unsigned long total = 0, lastt = 0; unsigned h[4] = {0};
    while (total < (unsigned long)frames * 70224)
    {
        unsigned pc = gb.pc, op = GB_read_memory(&gb, pc), arg = GB_read_memory(&gb, pc + 1);
        if (op == 0xE0 && arg >= 0x51 && arg <= 0x54) h[arg - 0x51] = gb.af >> 8;
        if (op == 0xE0 && arg == 0x55)
            printf("  pc %04x vbl %lu LY %3d (+%lu dots): %02x%02x -> %02x%02x vbk %d rom %02x blocks %d\n", pc, total / 70224, GB_read_memory(&gb, 0xFF44), total - lastt,
                   h[0], h[1], h[2] | 0x80, h[3], GB_read_memory(&gb, 0xFF4F) & 1, gb.mbc_rom_bank, ((gb.af >> 8) & 0x7F) + 1);
        if (op == 0xE0 && arg == 0x55) lastt = total;
        if (op == 0xE0 && arg == 0x55 && getenv("JOBS"))
        {
            unsigned jb = strtol(getenv("JOBS"), NULL, 0), jh = GB_read_memory(&gb, jb + 0xA0);
            printf("    A=%02x jhead %u jtail %u job:", gb.af >> 8, jh, GB_read_memory(&gb, jb + 0xA1));
            for (int i = 0; i < 10; i++) printf(" %02x", GB_read_memory(&gb, jb + jh * 10 + i));
            printf("\n");
        }
        total += GB_run(&gb) / (gb.cgb_double_speed ? 2 : 1);
    }
}


/* progress ADDR FRAMES: FAIL unless the 16 bit value at ADDR changes within FRAMES frames (the game is not hung);
 * on a hang prints where the CPU is */
static bool progress_run(const char *name, long frames)
{
    unsigned a = (unsigned)parse_addr(name) & 0xFFFF;
    unsigned v0 = GB_read_memory(&gb, a) | (GB_read_memory(&gb, a + 1) << 8);
    for (long f = 0; f < frames; f++)
    {
        unsigned long c = 0;
        while (c < 70224) c += GB_run(&gb) / (gb.cgb_double_speed ? 2 : 1);
        if ((GB_read_memory(&gb, a) | (GB_read_memory(&gb, a + 1) << 8)) != v0) return true;
    }
    {
        unsigned off; const char *nm = sym_at(gb.pc, gb.mbc_rom_bank, &off);
        printf("  HUNG: pc %04x bank %d (%s+%u) sp %04x ly %d ie %02x if %02x ime %d halted %d\n", gb.pc, gb.mbc_rom_bank, nm, off,
               gb.sp, GB_read_memory(&gb, 0xFF44), GB_read_memory(&gb, 0xFFFF), GB_read_memory(&gb, 0xFF0F), gb.ime, gb.halted);
        for (int i = 0; i < 12; i++)
        {
            unsigned w = GB_read_memory(&gb, gb.sp + 2 * i) | (GB_read_memory(&gb, gb.sp + 2 * i + 1) << 8), o2;
            printf("    sp+%02d %04x %s\n", 2 * i, w, sym_at(w, gb.mbc_rom_bank, &o2));
        }
        /* where the main program is (interrupts enabled), over 20 frames */
        {
            enum { M = 64 }; char nm2[M][40]; unsigned long cnt[M] = {0}; int k = 0; unsigned long c = 0;
            while (c < 20 * 70224UL)
            {
                if (gb.ime)
                {
                    unsigned o3; const char *n3 = sym_at(gb.pc, gb.mbc_rom_bank, &o3); int i;
                    for (i = 0; i < k; i++) if (!strcmp(nm2[i], n3)) break;
                    if (i == k && k < M) { snprintf(nm2[k], 40, "%s", n3); k++; }
                    if (i < k) cnt[i]++;
                }
                c += GB_run(&gb) / (gb.cgb_double_speed ? 2 : 1);
            }
            for (int i = 0; i < k; i++) printf("    main in %s: %lu\n", nm2[i], cnt[i]);
            printf("    pc now %04x bank %d sp %04x\n", gb.pc, gb.mbc_rom_bank, gb.sp);
        }
    }
    return false;
}

/* report when the bank variable and the mapped ROM bank disagree for several instructions in a row */
static void bankwatch(long frames)
{
    enum { N = 48 };
    unsigned rp[N], rb[N], rs[N]; int ri = 0, bad = 0; unsigned var = 0;
    for (int i = 0; i < nsyms; i++) if (!strcmp(syms[i].name, "_cur_rom_bank")) var = syms[i].addr & 0xFFFF;
    for (long f = 0; f < frames; f++)
    {
        unsigned long c = 0;
        while (c < 140000)
        {
            unsigned pc = gb.pc, bank = gb.mbc_rom_bank;
            rp[ri % N] = pc; rb[ri % N] = bank; rs[ri % N] = gb.sp; ri++;
            if (var && pc >= 0x200 && !(pc >= 0x40 && pc < 0x100))
            {
                unsigned v = GB_read_memory(&gb, var);
                if (v != bank && v != 0xFF) bad++; else bad = 0;
                if (bad > 6)
                {
                    printf("BANK MISMATCH var=%u mapped=%u frame %ld\n", v, bank, f);
                    for (int i = ri > N ? ri - N : 0; i < ri; i++)
                    {
                        unsigned off = 0; const char *n = sym_at(rp[i % N], rb[i % N], &off);
                        printf("  %04x/%02x sp=%04x %s+%u\n", rp[i % N], rb[i % N], rs[i % N], n, off);
                    }
                    return;
                }
            }
            c += GB_run(&gb) / (gb.cgb_double_speed ? 2 : 1);
        }
    }
    printf("no bank mismatch in %ld frames\n", frames);
}

static int maxhits_w = 12;
/* single step and stop when the byte at addr changes: prints the instruction that did it */
static void watchmem(unsigned addr, long frames, int expect_nonzero)
{
    unsigned prev = GB_read_memory(&gb, addr);
    unsigned lastpc = gb.pc, lastbank = gb.mbc_rom_bank;
    int hits = 0;
    for (long f = 0; f < frames; f++)
    {
        unsigned long c = 0;
        while (c < 140000)
        {
            lastpc = gb.pc; lastbank = gb.mbc_rom_bank;
            c += GB_run(&gb) / (gb.cgb_double_speed ? 2 : 1);
            unsigned v = GB_read_memory(&gb, addr);
            if (v != prev)
            {
                unsigned off = 0; const char *n = sym_at(lastpc, lastbank, &off);
                printf("WATCH %04x: %02x -> %02x by %04x/%02x %s+%u (frame %ld sp=%04x)\n", addr, prev, v, lastpc, lastbank, n, off, f, gb.sp);
                prev = v;
                if (++hits >= maxhits_w) return;
            }
        }
    }
    printf("watch: %d changes\n", hits);
    (void)expect_nonzero;
}

static int maxhits = 8;
static void trace_at(unsigned addr, long frames, int nbytes)
{
    unsigned want = addr & 0xFFFF, bank = addr >> 16;
    int hits = 0;
    for (long f = 0; f < frames; f++)
    {
        unsigned long c = 0;
        while (c < 140000)
        {
            if (gb.pc == want && (want < 0x4000 || gb.mbc_rom_bank == bank))
            {
                printf("hit PC=%04x SP=%04x AF=%04x BC=%04x DE=%04x HL=%04x stack:", gb.pc, gb.sp, gb.af, gb.bc, gb.de, gb.hl);
                for (int i = 0; i < nbytes; i++) printf(" %02x", GB_read_memory(&gb, gb.sp + i));
                printf("\n");
                if (++hits >= maxhits) return;
            }
            c += GB_run(&gb) / (gb.cgb_double_speed ? 2 : 1);
        }
    }
}

static void run_frames(long n) { while (n-- > 0) { GB_run_frame(&gb); frames_run++; } }

typedef struct { unsigned black, white, distinct, top; unsigned long n; } ScreenStat;
static ScreenStat screen_stat(void)
{
    ScreenStat st = {0};
    enum { HS = 4096 };
    static uint32_t hk[HS]; static unsigned hc[HS];
    memset(hk, 0xFF, sizeof hk); memset(hc, 0, sizeof hc);
    st.n = 160 * 144;
    for (unsigned i = 0; i < 160 * 144; i++)
    {
        uint32_t c = pixels[i] & 0xFFFFFF;
        unsigned h = (c * 2654435761u) >> 20 & (HS - 1);
        while (hk[h] != 0xFFFFFFFF && hk[h] != c) h = (h + 1) & (HS - 1);
        if (hk[h] == 0xFFFFFFFF) { hk[h] = c; st.distinct++; }
        hc[h]++;
        if (hc[h] > st.top) st.top = hc[h];
        unsigned r = c >> 16, g = (c >> 8) & 255, b = c & 255;
        if (r < 24 && g < 24 && b < 24) st.black++;
        if (r > 232 && g > 232 && b > 232) st.white++;
    }
    return st;
}
static unsigned min_colours = 4;       /* screen_ok LABEL [MINCOLOURS]: a plain text screen has only 3 */
static bool screen_good(const char *label, bool verbose)
{
    ScreenStat s = screen_stat();
    bool ok = true;
    const char *why = "";
    if (s.black * 100 >= s.n * 90) { ok = false; why = "black screen"; }
    else if (s.white * 100 >= s.n * 90) { ok = false; why = "white screen"; }
    else if (s.distinct < min_colours) { ok = false; why = "too few distinct colours"; }
    else if (s.top * 100 >= s.n * 97) { ok = false; why = "one flat colour"; }
    if (verbose)
        printf("%s screen_ok: black=%.1f%% white=%.1f%% colours=%u top=%.1f%% -> %s %s\n", label,
               100.0 * s.black / s.n, 100.0 * s.white / s.n, s.distinct, 100.0 * s.top / s.n,
               ok ? "ok" : "FAIL", why);
    return ok;
}

static void write_ppm(const char *name)
{
    char path[1024]; FILE *f;
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fprintf(f, "P6\n160 144\n255\n");
    for (unsigned i = 0; i < 160 * 144; i++)
    {
        unsigned char px[3] = { pixels[i] >> 16, pixels[i] >> 8, pixels[i] };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    memcpy(prev_pixels, pixels, sizeof prev_pixels);
}

static unsigned rd8(long a) { return GB_read_memory(&gb, (uint16_t)a); }
static void fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void fail(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("FAIL: "); vprintf(fmt, ap); printf("\n");
    va_end(ap); fails++;
}

int main(int argc, char **argv)
{
    const char *rom = NULL, *script = NULL, *boot = NULL, *sav = NULL;
    char bootbuf[1024];
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--boot") && i + 1 < argc) boot = argv[++i];
        else if (!strcmp(argv[i], "--sav") && i + 1 < argc) sav = argv[++i];
        else if (!strcmp(argv[i], "--sym") && i + 1 < argc) load_syms(argv[++i]);
        else if (!strcmp(argv[i], "--outdir") && i + 1 < argc) snprintf(outdir, sizeof outdir, "%s", argv[++i]);
        else if (!rom) rom = argv[i];
        else script = argv[i];
    }
    if (!rom || !script) { fprintf(stderr, "usage: gbtest [--boot f] [--sav f] [--sym f] [--outdir d] ROM SCRIPT\n"); return 2; }
    if (!boot)
    {
        snprintf(bootbuf, sizeof bootbuf, "%s", argv[0]);
        char *sl = strrchr(bootbuf, '/');
        if (sl) strcpy(sl + 1, "cgb_boot.bin"); else strcpy(bootbuf, "cgb_boot.bin");
        boot = bootbuf;
    }
    GB_init(&gb, GB_MODEL_CGB_E);
    GB_set_boot_rom_load_callback(&gb, NULL);
    if (GB_load_boot_rom(&gb, boot)) { fprintf(stderr, "cannot load boot rom %s\n", boot); return 2; }
    if (GB_load_rom(&gb, rom)) { fprintf(stderr, "cannot load rom %s\n", rom); return 2; }
    if (sav) GB_load_battery(&gb, sav);
    GB_set_pixels_output(&gb, pixels);
    GB_set_rgb_encode_callback(&gb, rgb_encode);
    GB_set_log_callback(&gb, log_cb);
    GB_set_sample_rate(&gb, 44100);
    GB_apu_set_sample_callback(&gb, audio_cb);
    GB_set_rendering_disabled(&gb, false);

    FILE *f = fopen(script, "r");
    if (!f) { fprintf(stderr, "cannot read script %s\n", script); return 2; }
    char line[1024]; int ln = 0;
    while (fgets(line, sizeof line, f))
    {
        char *p = line, *c;
        char cmd[64] = "", a1[256] = "", a2[256] = "", a3[256] = "";
        ln++;
        if ((c = strchr(p, '#'))) *c = 0;
        int n = sscanf(p, "%63s %255s %255s %255s", cmd, a1, a2, a3);
        if (n < 1) continue;
        if (!strcmp(cmd, "run")) run_frames(strtol(a1, NULL, 0));
        else if (!strcmp(cmd, "press")) { unsigned m = keys_from(a1); set_keys(m, true); run_frames(strtol(a2, NULL, 0)); set_keys(m, false); run_frames(2); }
        else if (!strcmp(cmd, "hold")) set_keys(keys_from(a1), true);
        else if (!strcmp(cmd, "release")) set_keys(keys_from(a1), false);
        else if (!strcmp(cmd, "shot")) write_ppm(a1);
        else if (!strcmp(cmd, "screen_ok")) { min_colours = *a2 ? (unsigned)strtol(a2, NULL, 0) : 4; if (!screen_good(a1, true)) fail("line %d: screen_ok %s", ln, a1); min_colours = 4; }
        else if (!strcmp(cmd, "screen_changed"))
        {
            if (!memcmp(prev_pixels, pixels, sizeof prev_pixels)) fail("line %d: screen did not change", ln);
            else printf("screen_changed ok\n");
            memcpy(prev_pixels, pixels, sizeof prev_pixels);
        }
        else if (!strcmp(cmd, "expect") || !strcmp(cmd, "expect_ne") || !strcmp(cmd, "expect_ge"))
        {
            long a = parse_addr(a1), v = strtol(a2, NULL, 0); unsigned got = rd8(a);
            bool ok = !strcmp(cmd, "expect") ? got == (unsigned)v : !strcmp(cmd, "expect_ne") ? got != (unsigned)v : got >= (unsigned)v;
            if (!ok) fail("line %d: %s %s(%04lx)=%u, wanted %s%ld", ln, cmd, a1, a, got, strcmp(cmd, "expect") ? (strcmp(cmd, "expect_ne") ? ">= " : "!= ") : "", v);
            else printf("%s %s = %u ok\n", cmd, a1, got);
        }
        else if (!strcmp(cmd, "expect16") || !strcmp(cmd, "expect16_ge"))
        {
            long a = parse_addr(a1), v = strtol(a2, NULL, 0); unsigned got = rd8(a) | (rd8(a + 1) << 8);
            bool ok = !strcmp(cmd, "expect16") ? got == (unsigned)v : got >= (unsigned)v;
            if (!ok) fail("line %d: %s %s(%04lx)=%u, wanted %ld", ln, cmd, a1, a, got, v);
            else printf("%s %s = %u ok\n", cmd, a1, got);
        }
        else if (!strcmp(cmd, "set")) GB_write_memory(&gb, (uint16_t)parse_addr(a1), (uint8_t)strtol(a2, NULL, 0));
        else if (!strcmp(cmd, "poke16")) { long a = parse_addr(a1), v = strtol(a2, NULL, 0); GB_write_memory(&gb, a, v & 255); GB_write_memory(&gb, a + 1, v >> 8); }
        else if (!strcmp(cmd, "wait_until"))
        {
            long a = parse_addr(a1), v = strtol(a2, NULL, 0), lim = strtol(a3, NULL, 0); bool ok = false;
            while (lim-- > 0) { if (rd8(a) == (unsigned)v) { ok = true; break; } run_frames(1); }
            if (!ok) fail("line %d: wait_until %s == %ld timed out (now %u)", ln, a1, v, rd8(a)); else printf("wait_until %s == %ld ok\n", a1, v);
        }
        else if (!strcmp(cmd, "wait_ne"))
        {
            long a = parse_addr(a1), v = strtol(a2, NULL, 0), lim = strtol(a3, NULL, 0); bool ok = false;
            while (lim-- > 0) { if (rd8(a) != (unsigned)v) { ok = true; break; } run_frames(1); }
            if (!ok) fail("line %d: wait_ne %s != %ld timed out", ln, a1, v); else printf("wait_ne %s = %u ok\n", a1, rd8(a));
        }
        else if (!strcmp(cmd, "wait_screen"))
        {
            long lim = strtol(a1, NULL, 0); bool ok = false;
            while (lim-- > 0) { if (screen_good("wait", false)) { ok = true; break; } run_frames(1); }
            if (!ok) fail("line %d: wait_screen timed out", ln); else printf("wait_screen ok\n");
        }
        else if (!strcmp(cmd, "audio_reset")) { audio_energy = 0; audio_samples = 0; }
        else if (!strcmp(cmd, "audio_ok"))
        {
            printf("audio: %lu samples, energy %llu\n", audio_samples, (unsigned long long)audio_energy);
            if (audio_samples == 0 || audio_energy < 20000) fail("line %d: audio is silent", ln);
        }
        else if (!strcmp(cmd, "print")) printf("%s = %u\n", a1, rd8(parse_addr(a1)));
        else if (!strcmp(cmd, "maxhits")) maxhits = strtol(a1, NULL, 0);
        else if (!strcmp(cmd, "trace_at")) trace_at(parse_addr(a1), strtol(a2, NULL, 0), strtol(a3, NULL, 0));
        else if (!strcmp(cmd, "crash")) crash_hunt(strtol(a1, NULL, 0));
        else if (!strcmp(cmd, "watch")) watchmem((unsigned)parse_addr(a1), strtol(a2, NULL, 0), 0);
        else if (!strcmp(cmd, "bankwatch")) bankwatch(strtol(a1, NULL, 0));
        else if (!strcmp(cmd, "isrtime")) isrtime_run(strtol(a1, NULL, 0));
        else if (!strcmp(cmd, "fillram")) { size_t sz; uint16_t bk; uint8_t *r = GB_get_direct_access(&gb, GB_DIRECT_ACCESS_RAM, &sz, &bk); memset(r, (int)strtol(a1, NULL, 0), sz); r = GB_get_direct_access(&gb, GB_DIRECT_ACCESS_CART_RAM, &sz, &bk); memset(r, (int)strtol(a1, NULL, 0), sz); printf("fillram done\n"); }
        else if (!strcmp(cmd, "dmacost")) dmacost_run(strtol(a1, NULL, 0));
        else if (!strcmp(cmd, "vbltrace")) vbltrace_run();
        else if (!strcmp(cmd, "calls")) calls_run(a1, strtol(a2, NULL, 0));
        else if (!strcmp(cmd, "incl")) incl_run(a1, strtol(a2, NULL, 0));
        else if (!strcmp(cmd, "hot")) hot_run(a1, strtol(a2, NULL, 0));
        else if (!strcmp(cmd, "gdmalog")) gdmalog_run(strtol(a1, NULL, 0));
        else if (!strcmp(cmd, "progress")) { if (!progress_run(a1, strtol(a2, NULL, 0))) fail("line %d: no progress of %s", ln, a1); }
        else if (!strcmp(cmd, "save_state")) { if (GB_save_state(&gb, a1)) fail("line %d: save_state", ln); }
        else if (!strcmp(cmd, "load_state")) { if (GB_load_state(&gb, a1)) fail("line %d: load_state", ln); }
        else if (!strcmp(cmd, "prof")) prof_run(strtol(a1, NULL, 0));
        else if (!strcmp(cmd, "minsp")) minsp_run(strtol(a1, NULL, 0));
        else if (!strcmp(cmd, "regs")) printf("PC=%04x SP=%04x AF=%04x BC=%04x DE=%04x HL=%04x ROMB=%02x\n", gb.pc, gb.sp, gb.af, gb.bc, gb.de, gb.hl, gb.mbc_rom_bank);
        else if (!strcmp(cmd, "vram")) { size_t sz; uint16_t bk; uint8_t *v = GB_get_direct_access(&gb, GB_DIRECT_ACCESS_VRAM, &sz, &bk); long b = strtol(a1, NULL, 0), a = parse_addr(a2), n = strtol(a3, NULL, 0); printf("vram%ld %04lx:", b, a); for (long i = 0; i < n; i++) printf(" %02x", v[(b ? 0x2000 : 0) + (a - 0x8000) + i]); printf("\n"); }
        else if (!strcmp(cmd, "dump")) { long a = parse_addr(a1), n = strtol(a2, NULL, 0); printf("%04lx:", a); for (long i = 0; i < n; i++) printf(" %02x", rd8(a + i)); printf("\n"); }
        else if (!strcmp(cmd, "log")) printf("%s\n", p + 4);
        else { fprintf(stderr, "line %d: unknown command %s\n", ln, cmd); return 2; }
    }
    fclose(f);
    if (sav) GB_save_battery(&gb, sav);
    printf("%s (%lu frames)\n", fails ? "FAILED" : "PASSED", frames_run);
    return fails ? 1 : 0;
}
