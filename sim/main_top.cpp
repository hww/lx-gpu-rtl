#include "Vtb_top_wrapper.h"
#include "Vtb_top_wrapper___024root.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "image_io.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

// ===== ANSI =====
#define CLR_RST  "\033[0m"
#define CLR_GRN  "\033[32m"
#define CLR_RED  "\033[31m"
#define CLR_YEL  "\033[33m"
#define CLR_CYN  "\033[36m"
#define CLR_BLD  "\033[1m"

static Vtb_top_wrapper* dut;
static VerilatedVcdC* tfp_p = nullptr;
static uint64_t cycles = 0;
static int g_verbose = 0;

#define MEM_WORDS  65536
#define ROM_SIZE   4096

static uint16_t mem[MEM_WORDS];

// ===== dumps =====
static void dump_mem_hex(const char* tag, uint32_t word_off, uint32_t nwords) {
    printf(CLR_CYN "=== RAM DUMP %s @0x%04X (%u words) ===" CLR_RST "\n",
           tag, word_off, nwords);
    for (uint32_t i = 0; i < nwords; i += 8) {
        printf("  %04X:", word_off + i);
        for (uint32_t j = 0; j < 8 && i + j < nwords; j++) {
            uint32_t w = word_off + i + j;
            printf(" %04X", (w < MEM_WORDS) ? mem[w] : 0);
        }
        printf("\n");
    }
}

static void dump_rom_hex(const char* tag, uint32_t word_off, uint32_t nwords) {
    printf(CLR_CYN "=== ROM DUMP %s @0x%03X (%u words) ===" CLR_RST "\n",
           tag, word_off, nwords);
    for (uint32_t i = 0; i < nwords; i += 4) {
        printf("  %03X:", word_off + i);
        for (uint32_t j = 0; j < 4 && i + j < nwords; j++) {
            uint32_t w = word_off + i + j;
            uint32_t v = (w < ROM_SIZE)
                ? (uint32_t)dut->rootp->tb_top_wrapper__DOT__u_top__DOT__u_rom__DOT__mem[w]
                : 0;
            printf(" %08X", v);
        }
        printf("\n");
    }
}

// ===== clock =====
static void tick(int clk, bool log) {
    dut->clk_i = clk;
    dut->eval();
    if (tfp_p) tfp_p->dump(cycles);
    ++cycles;

    if (log && g_verbose) {
        printf("t=%llu pc=%03X halted=%d error=%d\n",
            (unsigned long long)cycles,
            (unsigned)dut->pc_o,
            (int)dut->halted_o, (int)dut->error_o);
    }
}

static void step() {
    // WB slave: ack=1 for any cyc&stb
    dut->wb_ack_i = 0;
    dut->wb_dat_i = 0;
    if (dut->wb_cyc_o && dut->wb_stb_o) {
        uint32_t a = (dut->wb_adr_o >> 1) & (MEM_WORDS - 1);
        if (dut->wb_we_o) mem[a] = dut->wb_dat_o;
        else           dut->wb_dat_i = mem[a];
        dut->wb_ack_i = 1;
    }

    tick(0, false);
    tick(1, true);
    tick(1, false);
    tick(0, false);
}

// ===== loaders =====
static void load_rom_direct(uint16_t addr, uint32_t w) {
    // (removed: no external ROM port in top anymore)
    (void)addr; (void)w;
}

static void load_hex_into_mem(const char* path, uint32_t word_off) {
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return; }
    char line[256];
    uint32_t a = word_off;
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == ';' || *p == '#') continue;
        uint32_t w = strtoul(p, nullptr, 16);
        if (a < MEM_WORDS) mem[a] = (uint16_t)w;
        a++;
    }
    fclose(f);
    printf(CLR_GRN "loaded" CLR_RST " %s into RAM @word 0x%04X (%u words)\n",
           path, word_off, a - word_off);
}

static void load_bin_into_mem(const char* path, uint32_t word_off) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); return; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(sz);
    if (fread(buf.data(), 1, sz, f) != (size_t)sz) { /* ignore */ }
    fclose(f);
    for (long i = 0; i + 1 < sz; i += 2) {
        uint32_t w = word_off + i / 2;
        if (w < MEM_WORDS) mem[w] = buf[i] | (buf[i+1] << 8);
    }
    printf(CLR_GRN "loaded" CLR_RST " %s (%ld bytes) at word 0x%X\n", path, sz, word_off);
}

static void save_bin(const char* path, uint32_t word_off, uint32_t nwords) {
    FILE* f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return; }
    for (uint32_t i = 0; i < nwords; i++) {
        uint32_t w = word_off + i;
        uint16_t v = (w < MEM_WORDS) ? mem[w] : 0;
        fputc(v & 0xFF, f); fputc(v >> 8, f);
    }
    fclose(f);
    printf(CLR_GRN "saved" CLR_RST " %s (%u words) from word 0x%X\n", path, nwords, word_off);
}

// ===== PNG =====
static const uint32_t* palette_for(PixelFormat fmt, int& out_n) {
    switch (fmt) {
        case PixelFormat::BPP1:  out_n = 2;   return vga16_palette;
        case PixelFormat::BPP4:  out_n = 16;  return vga16_palette;
        case PixelFormat::BPP8:  out_n = 256; return get_websafe216_palette();
        default:                 out_n = 0;   return nullptr;
    }
}

static bool load_png_into_mem(const char* path, PixelFormat fmt,
                              uint32_t word_off,
                              int expect_w, int expect_h,
                              size_t& out_words) {
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

    if (word_off + words.size() > MEM_WORDS) {
        fprintf(stderr, "PNG overflows memory\n"); return false;
    }
    for (size_t i = 0; i < words.size(); i++) mem[word_off + i] = words[i];

    printf(CLR_GRN "loaded" CLR_RST " %s (%dx%d, %s) at word 0x%X (%zu words)\n",
           path, img.width, img.height, format_name(fmt), word_off, words.size());
    out_words = words.size();
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
        words[i] = (word_off + i < MEM_WORDS) ? mem[word_off + i] : 0;

    std::vector<uint8_t> idx = unpack_pixels(words, w, h, fmt);
    int pal_n = 0;
    const uint32_t* pal = palette_for(fmt, pal_n);
    Image out = dequantize(idx, w, h, fmt, pal);
    if (!write_image(path, out)) return false;

    printf(CLR_GRN "dumped" CLR_RST " %s (%dx%d, %s) from word 0x%X\n",
           path, w, h, format_name(fmt), word_off);
    return true;
}

// ===== result check =====
static int g_fails = 0;

static void check(const char* name, int cond) {
    if (cond) {
        printf(CLR_GRN "  [ OK ]" CLR_RST " %s\n", name);
    } else {
        printf(CLR_RED "  [FAIL]" CLR_RST " %s\n", name);
        g_fails++;
    }
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vtb_top_wrapper;

    VerilatedVcdC tfp;
    tfp_p = &tfp;
    Verilated::traceEverOn(true);
    dut->trace(&tfp, 99);
    tfp.open("sim/trace.vcd");

    // ===== args =====
    const char* s1 = nullptr;         uint32_t s1_off = 0;
    const char* s1in_path = nullptr;  PixelFormat s1fmt = PixelFormat::BPP8;
    int s1in_w = 0, s1in_h = 0;
    const char* dst_raw = nullptr;    uint32_t dst_off = 0, dst_size = 0x1000;
    const char* dout_path = nullptr;  PixelFormat dfmt = PixelFormat::BPP8;
    int dout_w = 0, dout_h = 0;
    uint32_t gpu_addr = 0;
    uint64_t max_cycles = 1000000ULL;   // default 1M
    uint64_t progress_every = 0;        // 0 = off

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "-s1")     && i+1 < argc) s1 = argv[++i];
        else if (!strcmp(argv[i], "-s1off")  && i+1 < argc) s1_off = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-s1in")   && i+1 < argc) s1in_path = argv[++i];
        else if (!strcmp(argv[i], "-s1fmt")  && i+1 < argc) {
            if (!parse_format(argv[++i], s1fmt)) { fprintf(stderr, "bad -s1fmt\n"); return 1; }
        }
        else if (!strcmp(argv[i], "-s1w")    && i+1 < argc) s1in_w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s1h")    && i+1 < argc) s1in_h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-d")      && i+1 < argc) dst_raw = argv[++i];
        else if (!strcmp(argv[i], "-doff")   && i+1 < argc) dst_off = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-dsize")  && i+1 < argc) dst_size = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-dout")   && i+1 < argc) dout_path = argv[++i];
        else if (!strcmp(argv[i], "-dfmt")   && i+1 < argc) {
            if (!parse_format(argv[++i], dfmt)) { fprintf(stderr, "bad -dfmt\n"); return 1; }
        }
        else if (!strcmp(argv[i], "-dw")     && i+1 < argc) dout_w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-dh")     && i+1 < argc) dout_h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-gpuaddr")&& i+1 < argc) gpu_addr = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-v"))     g_verbose = 1;
        else { fprintf(stderr, "unknown arg: %s\n", argv[i]); return 1; }
    }

    memset(mem, 0, sizeof(mem));

    // ===== reset =====
    dut->rst_i = 1; dut->start_i = 0; dut->gpu_start_i = 0; dut->gpu_addr_i = 0;
    dut->wb_ack_i = 0; dut->wb_dat_i = 0;
    step(); step();
    dut->rst_i = 0; step();

    dump_rom_hex("initial (bootloader)", 0x000, 16);

    // ===== load program =====
    if (s1)          load_hex_into_mem(s1, s1_off);
    if (s1in_path) {
        size_t n = 0;
        if (!load_png_into_mem(s1in_path, s1fmt, s1_off, s1in_w, s1in_h, n)) return 1;
    }
    dump_mem_hex("RAM (package)", 0x0000, 0x20);

    // ===== start (WB load) =====
    dut->gpu_addr_i  = gpu_addr;
    dut->gpu_start_i = 1; step(); dut->gpu_start_i = 0;

    // ===== run =====
    uint64_t run_start = cycles;
    uint64_t last_prog  = cycles;
    int      timeout    = 0;

    while (!dut->halted_o && !dut->error_o) {
        step();

        if (progress_every && (cycles - last_prog) >= progress_every) {
            printf(CLR_YEL "[progress]" CLR_RST " cycles=%llu pc=%03X halted=%d error=%d\n",
                   (unsigned long long)cycles, (unsigned)dut->pc_o,
                   (int)dut->halted_o, (int)dut->error_o);
            last_prog = cycles;
        }

        if ((cycles - run_start) >= max_cycles) {
            timeout = 1;
            break;
        }
    }

    // ===== result =====
    printf("\n" CLR_BLD CLR_YEL "=== RUN RESULT ===" CLR_RST "\n");
    printf("  cycles  = %llu\n", (unsigned long long)cycles);
    printf("  halted  = %d\n", (int)dut->halted_o);
    printf("  error   = %d\n", (int)dut->error_o);
    printf("  pc      = %03X\n", (unsigned)dut->pc_o);
    printf("  errcode = %03X\n", (unsigned)dut->error_code_o);

    check("halted (not timeout)",  dut->halted_o);
    check("no error",              !dut->error_o);

    dump_rom_hex("after run 0x00", 0x000, 16);
    dump_rom_hex("after run 0x20", 0x020, 16);
    dump_mem_hex("RAM after run", 0x0000, 0x40);

    printf("\n" CLR_BLD "registers R0..R31:" CLR_RST "\n");
    for (int i = 0; i < 32; i++) {
        printf("  R%02d = %04X\n", i,
            (unsigned)dut->rootp->tb_top_wrapper__DOT__u_top__DOT__u_rf__DOT__mem_a[i]);
    }

    if (dst_raw) save_bin(dst_raw, dst_off, dst_size);
    if (dout_path) {
        if (!dump_mem_to_png(dout_path, dfmt, dst_off, dout_w, dout_h)) g_fails++;
    }

    printf("\n" CLR_BLD);
    if (g_fails == 0) printf(CLR_GRN "=== ALL CHECKS PASSED ===" CLR_RST "\n");
    else              printf(CLR_RED "=== %d CHECK(S) FAILED ===" CLR_RST "\n", g_fails);
    printf(CLR_RST);

    tfp_p = nullptr;
    tfp.close();
    delete dut;
    return g_fails ? 1 : 0;
}