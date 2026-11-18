#include "gpu_top.h"
#include "memory.h"
#include "image_io.h"
#include "disasm.h"
#include "asm.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

#define CLR_RST  "\033[0m"
#define CLR_GRN  "\033[32m"
#define CLR_RED  "\033[31m"
#define CLR_YEL  "\033[33m"
#define CLR_CYN  "\033[36m"
#define CLR_BLD  "\033[1m"

using namespace lx;

static uint64_t cycles = 0;
static int g_verbose = 0;
static int g_fails = 0;

#define MEM_WORDS  65536
#define ROM_SIZE   4096

static Memory16 mem_obj(MEM_WORDS * 2);

static inline uint16_t mem_read(uint32_t word_addr) {
    if (word_addr >= MEM_WORDS) return 0;
    return mem_obj.read16(word_addr << 1);
}

static inline void mem_write(uint32_t word_addr, uint16_t val) {
    if (word_addr >= MEM_WORDS) return;
    mem_obj.write16(word_addr << 1, val);
}

static bool load_file_smart(const char* path, uint32_t mem_off, uint32_t gpu_addr, bool has_gpu_addr) {
    std::string p(path);
    size_t dot = p.find_last_of('.');
    std::string ext = (dot != std::string::npos) ? p.substr(dot) : "";

    if (ext == ".asm") {
        printf("Loading ASM file to Z80 memory...\n");
        uint16_t asm_org = 0;
        auto words = asm_assemble_file(path, &asm_org);
        if (words.empty()) { fprintf(stderr, "asm empty or error\n"); return false; }

        uint32_t a = mem_off;
        printf("  ASM org = 0x%04X\n", asm_org);
        uint16_t gpu_start_addr = has_gpu_addr ? (uint16_t)gpu_addr : asm_org;

        mem_write(a++, (uint16_t)((words.size() * 2) & 0xFFFF)); // SIZE
        mem_write(a++, gpu_start_addr);                          // START
        uint32_t gpu_pc = gpu_start_addr;
        for (auto w : words) {
            auto cur_a = a;
            mem_write(a++, (uint16_t)(w & 0xFFFF));
            mem_write(a++, (uint16_t)((w >> 16) & 0xFFFF));
            printf("  Z80_RAM[%04X] GPU_ROM[%04X] %08X  %s\n", cur_a, gpu_pc++, w, disasm32(w));
        }
        return true;

    } else if (ext == ".pkg") {
        printf("Loading PKG file...\n");
        FILE* f = fopen(path, "r");
        if (!f) { fprintf(stderr, "cannot open %s\n", path); return false; }
        char line[256];
        uint32_t a = mem_off;
        while (fgets(line, sizeof line, f)) {
            char* ptr = line;
            while (*ptr == ' ' || *ptr == '\t') ptr++;
            if (*ptr == '\0' || *ptr == '\n' || *ptr == ';' || *ptr == '#') continue;
            uint32_t w = (uint32_t)strtoul(ptr, nullptr, 16);
            mem_write(a, (uint16_t)w);
            a++;
        }
        fclose(f);
        if (has_gpu_addr && mem_off + 1 < MEM_WORDS) {
            mem_write(mem_off + 1, (uint16_t)gpu_addr);
        }
        printf("loaded %s (.pkg) -> pkg @ RAM 0x%04X (GPU START=0x%04X)\n",
               path, mem_off, mem_read(mem_off + 1));
        return true;

    } else if (ext == ".hex") {
        printf("Loading HEX file...\n");
        FILE* f = fopen(path, "r");
        if (!f) { fprintf(stderr, "cannot open %s\n", path); return false; }
        char line[256];
        uint32_t a = mem_off;
        while (fgets(line, sizeof line, f)) {
            char* ptr = line;
            while (*ptr == ' ' || *ptr == '\t') ptr++;
            if (*ptr == '\0' || *ptr == '\n' || *ptr == ';' || *ptr == '#') continue;
            uint32_t w = (uint32_t)strtoul(ptr, nullptr, 16);
            mem_write(a,     (uint16_t)(w & 0xFFFF));
            mem_write(a + 1, (uint16_t)((w >> 16) & 0xFFFF));
            a += 2;
        }
        fclose(f);
        printf("loaded %s (.hex) -> raw 32-bit @ RAM 0x%04X (gpu_addr=0x%04X)\n",
               path, mem_off, gpu_addr);
        return false;

    } else {
        fprintf(stderr, "unknown extension %s\n", ext.c_str());
        return false;
    }
}

static void dump_mem_hex(const char* tag, uint32_t word_off, uint32_t nwords) {
    printf(CLR_CYN "=== RAM DUMP %s @0x%04X (%u words) ===" CLR_RST "\n",
           tag, word_off, nwords);
    for (uint32_t i = 0; i < nwords; i += 8) {
        printf("  %04X:", word_off + i);
        for (uint32_t j = 0; j < 8 && i + j < nwords; j++) {
            uint32_t w = word_off + i + j;
            printf(" %04X", mem_read(w));
        }
        printf("\n");
    }
}

static void dump_rom_hex(GpuTop& gpu, const char* tag, uint32_t word_off, uint32_t nwords) {
    printf(CLR_CYN "=== ROM DUMP %s @0x%03X (%u words) ===" CLR_RST "\n",
           tag, word_off, nwords);
    for (uint32_t i = 0; i < nwords; i += 4) {
        printf("  %03X:", word_off + i);
        for (uint32_t j = 0; j < 4 && i + j < nwords; j++) {
            uint32_t w = word_off + i + j;
            uint32_t v = (w < ROM_SIZE) ? gpu.read_rom((uint16_t)w) : 0;
            printf(" %08X", v);
        }
        printf("\n");
    }
}

static void check_eq(const char* name, uint32_t got, uint32_t exp, int bits) {
    if (got != exp) {
        printf(CLR_RED "  [FAIL] %-32s got=0x%0*X exp=0x%0*X" CLR_RST "\n",
               name, bits/4, got, bits/4, exp);
        g_fails++;
    } else if (g_verbose) {
        printf(CLR_GRN "  [ OK ]" CLR_RST " %-32s = 0x%0*X\n",
               name, bits/4, got);
    }
}

static void check_bit(const char* name, int got, int exp) {
    if ((!!got) != (!!exp)) {
        printf(CLR_RED "  [FAIL] %-32s got=%d exp=%d" CLR_RST "\n",
               name, !!got, !!exp);
        g_fails++;
    } else if (g_verbose) {
        printf(CLR_GRN "  [ OK ]" CLR_RST " %-32s = %d\n", name, !!got);
    }
}

static void check_nonzero(const char* name, uint32_t got, int bits) {
    if (got == 0) {
        printf(CLR_RED "  [FAIL] %-32s is 0 (expected non-zero)" CLR_RST "\n",
               name);
        g_fails++;
    } else if (g_verbose) {
        printf(CLR_GRN "  [ OK ]" CLR_RST " %-32s = 0x%0*X\n",
               name, bits/4, got);
    }
}

static void header(const char* t) {
    printf("\n" CLR_BLD CLR_YEL "=== %s ===" CLR_RST "\n", t);
}

static void load_asm_into_rom(GpuTop& gpu, const char* path) {
    uint16_t org = 0;
    auto words = asm_assemble_file(path, &org);
    if (words.empty()) {
        fprintf(stderr, "cannot assemble bootloader %s\n", path);
        return;
    }
    for (size_t i = 0; i < words.size() && (org + i) < ROM_SIZE; ++i) {
        gpu.write_rom(static_cast<uint16_t>(org + i), words[i]);
    }
    printf(CLR_GRN "loaded" CLR_RST " %s into ROM @0x%03X (%zu words)\n", path, org, words.size());
}

static void load_hex_into_rom(GpuTop& gpu, const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return; }
    char line[256];
    uint32_t a = 0;
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == ';' || *p == '#') continue;
        uint32_t w = (uint32_t)strtoul(p, nullptr, 16);
        if (a < ROM_SIZE) gpu.write_rom((uint16_t)a, w);
        a++;
    }
    fclose(f);
    printf(CLR_GRN "loaded" CLR_RST " %s into ROM (%u words)\n", path, a);
}

static const uint32_t* palette_for(PixelFormat fmt, int& out_n) {
    switch (fmt) {
        case PixelFormat::BPP1:  out_n = 2;   return vga16_palette;
        case PixelFormat::BPP4:  out_n = 16;  return vga16_palette;
        case PixelFormat::BPP8:  out_n = 256; return get_websafe216_palette();
        default:                 out_n = 0;   return nullptr;
    }
}

static bool load_png_into_mem(const char* path, PixelFormat fmt,
                              uint32_t word_off, int expect_w, int expect_h) {
    Image img;
    if (!read_image(path, img)) return false;
    if (expect_w && img.width  != expect_w) { fprintf(stderr, "width mismatch\n"); return false; }
    if (expect_h && img.height != expect_h) { fprintf(stderr, "height mismatch\n"); return false; }
    std::string err;
    if (!check_resolution(img.width, img.height, fmt, err)) {
        fprintf(stderr, "load_png: %s\n", err.c_str()); return false;
    }
    int pal_n = 0;
    const uint32_t* pal = palette_for(fmt, pal_n);
    std::vector<uint8_t>  idx   = quantize(img, fmt, pal, pal_n);
    std::vector<uint16_t> words = pack_pixels(idx, img.width, img.height, fmt);
    if (word_off + words.size() > MEM_WORDS) { fprintf(stderr, "PNG overflows\n"); return false; }
    for (size_t i = 0; i < words.size(); i++) mem_write(word_off + i, words[i]);
    printf(CLR_GRN "loaded" CLR_RST " %s (%dx%d, %s) at word 0x%X (%zu words)\n",
           path, img.width, img.height, format_name(fmt), word_off, words.size());
    return true;
}

static bool dump_mem_to_png(const char* path, PixelFormat fmt,
                            uint32_t word_off, int w, int h) {
    if (w <= 0 || h <= 0) { fprintf(stderr, "invalid dims\n"); return false; }
    std::string err;
    if (!check_resolution(w, h, fmt, err)) { fprintf(stderr, "%s\n", err.c_str()); return false; }
    int bpp = bits_per_pixel(fmt);
    size_t n_bits  = (size_t)w * h * bpp;
    size_t n_words = (n_bits + 15) / 16;
    std::vector<uint16_t> words(n_words);
    for (size_t i = 0; i < n_words; i++)
        words[i] = mem_read(word_off + i);
    std::vector<uint8_t> idx = unpack_pixels(words, w, h, fmt);
    int pal_n = 0;
    const uint32_t* pal = palette_for(fmt, pal_n);
    Image out = dequantize(idx, w, h, fmt, pal);
    if (!write_image(path, out)) return false;
    printf(CLR_GRN "dumped" CLR_RST " %s (%dx%d, %s) from word 0x%X\n",
           path, w, h, format_name(fmt), word_off);
    return true;
}

static std::string make_default_out_path(const char* in_path) {
    std::string p(in_path);
    size_t dot = p.find_last_of('.');
    size_t slash = p.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        return p.substr(0, dot) + "_out" + p.substr(dot);
    }
    return p + "_out.png";
}

int main(int argc, char** argv) {
    printf("DEBUG argc=%d (C++ Functional Emulator)\n", argc);

    const char* prog_path    = nullptr;
    uint32_t    prog_off     = 0;
    uint32_t    gpu_addr     = 0;
    bool        has_gpu_addr = false;

    const char* s1in_path = nullptr;
    uint32_t    s1_off    = 0;
    PixelFormat s1fmt     = PixelFormat::BPP8;
    int         s1in_w    = 0, s1in_h = 0;

    const char* din_path  = nullptr;
    uint32_t    dst_off   = 0;
    uint32_t    dst_size  = 0x2000;
    PixelFormat dfmt      = PixelFormat::BPP8;

int         dst_w     = 0, dst_h = 0;
const char* out_png   = nullptr;
const char* out_raw   = nullptr;
uint64_t    max_cycles     = 200000ULL;
uint64_t    progress_every = 0;
for (int i = 1; i < argc; i++) {
if      ((!strcmp(argv[i], "-prog") || !strcmp(argv[i], "-p") || !strcmp(argv[i], "-s1")) && i+1 < argc)
prog_path = argv[++i];
else if (!strcmp(argv[i], "-poff")    && i+1 < argc) prog_off = strtoul(argv[++i], 0, 0);
else if (!strcmp(argv[i], "-gpuaddr") && i+1 < argc) {
gpu_addr     = strtoul(argv[++i], 0, 0);
has_gpu_addr = true;
}
else if (!strcmp(argv[i], "-s1in")    && i+1 < argc) s1in_path = argv[++i];
else if (!strcmp(argv[i], "-s1off")   && i+1 < argc) s1_off    = strtoul(argv[++i], 0, 0);
else if (!strcmp(argv[i], "-s1fmt")   && i+1 < argc) {
if (!parse_format(argv[++i], s1fmt)) { fprintf(stderr, "bad -s1fmt\n"); return 1; }
}
else if (!strcmp(argv[i], "-s1w")     && i+1 < argc) s1in_w = atoi(argv[++i]);
else if (!strcmp(argv[i], "-s1h")     && i+1 < argc) s1in_h = atoi(argv[++i]);
else if ((!strcmp(argv[i], "-din") || !strcmp(argv[i], "-s2in")) && i+1 < argc)
din_path = argv[++i];
else if (!strcmp(argv[i], "-doff")    && i+1 < argc) dst_off  = strtoul(argv[++i], 0, 0);
else if (!strcmp(argv[i], "-dsize")   && i+1 < argc) dst_size = strtoul(argv[++i], 0, 0);
else if (!strcmp(argv[i], "-dfmt")    && i+1 < argc) {
if (!parse_format(argv[++i], dfmt)) { fprintf(stderr, "bad -dfmt\n"); return 1; }
}
else if (!strcmp(argv[i], "-dw")      && i+1 < argc) dst_w = atoi(argv[++i]);
else if (!strcmp(argv[i], "-dh")      && i+1 < argc) dst_h = atoi(argv[++i]);
else if ((!strcmp(argv[i], "-out") || !strcmp(argv[i], "-dout")) && i+1 < argc)
out_png = argv[++i];
else if ((!strcmp(argv[i], "-rawout") || !strcmp(argv[i], "-d")) && i+1 < argc)
out_raw = argv[++i];
else if (!strcmp(argv[i], "-maxcycles") && i+1 < argc) max_cycles     = strtoull(argv[++i], 0, 0);
else if (!strcmp(argv[i], "-progress")  && i+1 < argc) progress_every = strtoull(argv[++i], 0, 0);
else if (!strcmp(argv[i], "-v"))        g_verbose = 1;
else if (!strcmp(argv[i], "-trace"))    { }
else { fprintf(stderr, "unknown arg: %s\n", argv[i]); return 1; }
}
printf("DEBUG g_verbose=%d\n", g_verbose);
if ((prog_off & 1) || (s1_off & 1) || (dst_off & 1) || (dst_size & 1)) {
fprintf(stderr, "Error: -poff, -s1off, -doff, -dsize must be even byte addresses\n");
return 1;
}
std::string auto_out_png;
if (!out_png && din_path) {
auto_out_png = make_default_out_path(din_path);
out_png = auto_out_png.c_str();
}
std::memset(mem_obj.raw(), 0, mem_obj.size());
GpuTop gpu(mem_obj);
header("RESET");
gpu.reset();
check_bit("halted_after_reset", gpu.halted(), 1);
check_eq ("pc_after_reset",     gpu.pc(), 0, 12);
header("ROM BOOTLOADER");
load_asm_into_rom(gpu, "../boot/bootloader.asm");
{
uint32_t r0 = gpu.read_rom(0);
check_nonzero("rom loaded", r0, 32);
printf("  rom = %08X  %s\n", r0, disasm32(r0));
}
dump_rom_hex(gpu, "initial", 0x000, 16);
header("LOAD PROGRAM & BUFFERS INTO RAM");
bool is_pkg = false;
if (prog_path) {
is_pkg = load_file_smart(prog_path, prog_off >> 1, gpu_addr, has_gpu_addr);
dump_mem_hex("RAM (program)", prog_off >> 1, 0x20);
if (is_pkg) {
check_nonzero("pkg SIZE",      mem_read((prog_off >> 1) + 0), 16);
check_nonzero("pkg START",     mem_read((prog_off >> 1) + 1), 16);
check_eq     ("pkg SIZE even", mem_read((prog_off >> 1) + 0) & 1, 0, 1);
}
}
if (s1in_path) {
printf("  [S1IN] %s -> RAM @ byte 0x%05X (%dx%d)\n", s1in_path, s1_off, s1in_w, s1in_h);
if (!load_png_into_mem(s1in_path, s1fmt, s1_off >> 1, s1in_w, s1in_h)) return 1;
}
if (din_path) {
printf("  [DIN ] %s -> RAM @ byte 0x%05X (%dx%d) [protected]\n", din_path, dst_off, dst_w, dst_h);
if (!load_png_into_mem(din_path, dfmt, dst_off >> 1, dst_w, dst_h)) return 1;
}
header("GPU_START");
uint32_t start_addr = (is_pkg ? prog_off : gpu_addr) & 0xFFFFFF;
gpu.gpu_start(start_addr);
header("RUN");
uint64_t exec_steps = 0;
uint64_t last_prog  = 0;
int timeout = 0;
while (!gpu.halted() && !gpu.error()) {
uint16_t pc_before   = gpu.pc();
uint32_t insn_before = gpu.read_rom(pc_before);
if (g_verbose) {
printf("[dbg] step=%llu PC=%03X  %08X  %s\n",
(unsigned long long)exec_steps,
pc_before,
insn_before,
disasm32(insn_before));
}
gpu.step();
exec_steps++;
cycles++;
if (progress_every && (exec_steps - last_prog) >= progress_every) {
printf(CLR_YEL "[progress]" CLR_RST " steps=%llu pc=%03X R0=%04X R1=%04X R2=%04X R3=%04X\n",
(unsigned long long)exec_steps, (unsigned)gpu.pc(),
(unsigned)gpu.reg(0), (unsigned)gpu.reg(1),
(unsigned)gpu.reg(2), (unsigned)gpu.reg(3));
last_prog = exec_steps;
}
if (exec_steps >= max_cycles) {
printf(CLR_RED "Infinitive execution!\n" CLR_RST);
timeout = 1;
break;
}
}
header("RUN RESULT");
printf("\n" CLR_BLD CLR_CYN "  **************************************************\n");
printf("  *  GPU EXECUTED INSTRUCTIONS : %-16llu  *\n", (unsigned long long)exec_steps);
printf("  **************************************************" CLR_RST "\n");
printf("  limit           = %llu\n", (unsigned long long)max_cycles);
printf("  halted          = %d\n\n", (int)gpu.halted());
if (timeout) {
printf(CLR_RED "  TIMEOUT: %llu steps exceeded\n" CLR_RST,
(unsigned long long)max_cycles);
}
check_bit("not timeout",  !timeout,     1);
check_bit("halted",       gpu.halted(), 1);
check_bit("no error",     !gpu.error(), 1);
if (g_fails) {
header("CONTEXT AT FAILURE");
printf("  pc = %03X, halt_code = %u, R0=%04X R1=%04X R2=%04X R3=%04X\n",
(unsigned)gpu.pc(),
(unsigned)gpu.halt_code(),
(unsigned)gpu.reg(0), (unsigned)gpu.reg(1),
(unsigned)gpu.reg(2), (unsigned)gpu.reg(3));
printf("  roma=%03X roml=%04X\n",
(unsigned)gpu.roma(), (unsigned)gpu.roml());
printf("  ch: error=%d status=%X\n",
(int)gpu.ch_error(), (unsigned)gpu.ch_status());
}
dump_rom_hex(gpu, "after run 0x00", 0x000, 16);
dump_rom_hex(gpu, "after run 0x20", 0x020, 16);
dump_mem_hex("RAM after run", dst_off, 0x40);
printf("\n" CLR_BLD "registers R0..R31:" CLR_RST "\n");
for (int i = 0; i < 32; i++) {
printf("  R%02d = %04X\n", i, (unsigned)gpu.reg(i));
}
dump_mem_hex("RAM after run (word addr)", dst_off >> 1, 0x40);
if (out_raw) {
uint32_t raw_words = dst_size >> 1;
printf("  [SAVE RAW] -> %s (from byte 0x%05X, %u bytes)\n", out_raw, dst_off, dst_size);
FILE* f = fopen(out_raw, "wb");
if (f) {
for (uint32_t i = 0; i < raw_words; i++) {
uint16_t v = mem_read((dst_off >> 1) + i);
fputc(v & 0xFF, f);
fputc((v >> 8) & 0xFF, f);
}
fclose(f);
}
}
if (out_png) {
printf("  [SAVE PNG] -> %s (from byte 0x%05X, %dx%d)\n", out_png, dst_off, dst_w, dst_h);
if (!dump_mem_to_png(out_png, dfmt, dst_off >> 1, dst_w, dst_h)) g_fails++;
}
printf("\n" CLR_BLD);
if (g_fails == 0) printf(CLR_GRN "=== ALL CHECKS PASSED ===" CLR_RST "\n");
else              printf(CLR_RED "=== %d CHECK(S) FAILED ===" CLR_RST "\n", g_fails);
printf(CLR_RST);
return g_fails ? 1 : 0;
}
