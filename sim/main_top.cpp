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

static Vtb_top_wrapper* dut;
static VerilatedVcdC* tfp_p = nullptr;
static uint64_t cycles = 0;
static int g_verbose = 0;
#define MEM_WORDS 65536
static uint16_t mem[MEM_WORDS];
#define ROM_SIZE 4096

static void dump_mem_hex(const char* tag, uint32_t word_off, uint32_t nwords) {
    printf("=== MEM DUMP %s @0x%04X (%u words) ===\n", tag, word_off, nwords);
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
    printf("=== ROM DUMP %s @0x%03X (%u words) ===\n", tag, word_off, nwords);
    for (uint32_t i = 0; i < nwords; i += 4) {
        printf("  %03X:", word_off + i);
        for (uint32_t j = 0; j < 4 && i + j < nwords; j++) {
            uint32_t w = word_off + i + j;
            uint32_t v = (w < ROM_SIZE)
                ? (uint32_t)dut->rootp->tb_top_wrapper__DOT__u_top__DOT__rom__DOT__mem[w]
                : 0;
            printf(" %08X", v);
        }
        printf("\n");
    }
}

static void tick(int clk, bool log) {
    dut->clk = clk;
    dut->eval();
    if (tfp_p) tfp_p->dump(cycles);
    ++cycles;  

    if (log && g_verbose) {
        printf("t=%llu pc=%03X halted=%d error=%d ack=%d\n",
            (unsigned long long)cycles,
            (unsigned)dut->rootp->tb_top_wrapper__DOT__u_top__DOT__pc,
            (int)dut->halted, (int)dut->error,
            (int)dut->rootp->tb_top_wrapper__DOT__u_top__DOT__ack);
    }
}

static void step() {
    dut->wb_ack = 0;
    dut->wb_dat_i = 0;
    if (dut->wb_cyc && dut->wb_stb) {
        uint32_t a = (dut->wb_adr >> 1) & (MEM_WORDS - 1);
        if (dut->wb_we) mem[a] = dut->wb_dat_o;
        else           dut->wb_dat_i = mem[a];
        dut->wb_ack = 1;
    }

    tick(0, false);
    tick(1, true);    // posedge
    tick(1, false);   // hold high (не фронт)
    tick(0, false);   // negedge
}

static void load_rom(uint16_t addr, uint32_t w) {
    dut->rom_we = 1; dut->rom_addr = addr; dut->rom_wdata = w;
    step();
    dut->rom_we = 0;
}

// Load hex file into RAM at word offset (16-bit words, one per line).
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
    printf("loaded %s into RAM @word 0x%04X (%u words)\n", path, word_off, a - word_off);
}

static void load_file(const char* path, uint32_t word_off) {
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
    printf("loaded %s (%ld bytes) at word 0x%X\n", path, sz, word_off);
}

static void save_file(const char* path, uint32_t word_off, uint32_t nwords) {
    FILE* f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return; }
    for (uint32_t i = 0; i < nwords; i++) {
        uint32_t w = word_off + i;
        uint16_t v = (w < MEM_WORDS) ? mem[w] : 0;
        fputc(v & 0xFF, f); fputc(v >> 8, f);
    }
    fclose(f);
    printf("saved %s (%u words) from word 0x%X\n", path, nwords, word_off);
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
                              uint32_t word_off,
                              int expect_w, int expect_h,
                              size_t& out_words) {
    Image img;
    if (!read_image(path, img)) return false;

    if (expect_w && img.width  != expect_w) {
        fprintf(stderr, "load_png: width mismatch (%d != %d)\n", img.width, expect_w);
        return false;
    }
    if (expect_h && img.height != expect_h) {
        fprintf(stderr, "load_png: height mismatch (%d != %d)\n", img.height, expect_h);
        return false;
    }

    std::string err;
    if (!check_resolution(img.width, img.height, fmt, err)) {
        fprintf(stderr, "load_png: %s\n", err.c_str());
        return false;
    }

    int pal_n = 0;
    const uint32_t* pal = palette_for(fmt, pal_n);

    std::vector<uint8_t>  idx   = quantize(img, fmt, pal, pal_n);
    std::vector<uint16_t> words = pack_pixels(idx, img.width, img.height, fmt);

    if (word_off + words.size() > MEM_WORDS) {
        fprintf(stderr, "load_png: PNG overflows memory (%zu words at 0x%X)\n",
                words.size(), word_off);
        return false;
    }
    for (size_t i = 0; i < words.size(); i++) mem[word_off + i] = words[i];

    printf("loaded %s (%dx%d, %s) at word offset 0x%X (%zu words)\n",
           path, img.width, img.height, format_name(fmt),
           word_off, words.size());
    out_words = words.size();
    return true;
}

static bool dump_mem_to_png(const char* path, PixelFormat fmt,
                            uint32_t word_off, int w, int h) {
    if (w <= 0 || h <= 0) {
        fprintf(stderr, "dump_png: invalid dimensions %dx%d\n", w, h);
        return false;
    }
    std::string err;
    if (!check_resolution(w, h, fmt, err)) {
        fprintf(stderr, "dump_png: %s\n", err.c_str());
        return false;
    }

    int bpp = bits_per_pixel(fmt);
    size_t n_bits  = (size_t)w * h * bpp;
    size_t n_words = (n_bits + 15) / 16;

    std::vector<uint16_t> words(n_words);
    for (size_t i = 0; i < n_words; i++) {
        words[i] = (word_off + i < MEM_WORDS) ? mem[word_off + i] : 0;
    }

    std::vector<uint8_t> idx = unpack_pixels(words, w, h, fmt);
    int pal_n = 0;
    const uint32_t* pal = palette_for(fmt, pal_n);
    Image out = dequantize(idx, w, h, fmt, pal);
    if (!write_image(path, out)) return false;

    printf("dumped %s (%dx%d, %s) from word offset 0x%X\n",
           path, w, h, format_name(fmt), word_off);
    return true;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vtb_top_wrapper;

    VerilatedVcdC tfp;
    tfp_p = &tfp;
    Verilated::traceEverOn(true);
    dut->trace(&tfp, 99);
    tfp.open("sim/trace.vcd");

    const char* rom_prog = nullptr;   // -rom: direct ROM load
    const char* s1 = nullptr;         // -s1: raw binary into RAM
    uint32_t s1_off = 0;              // word offset
    const char* s1in_path = nullptr;  // -s1in: PNG into RAM
    PixelFormat s1fmt = PixelFormat::BPP8;
    int s1in_w = 0, s1in_h = 0;

    const char* dst_raw = nullptr;
    uint32_t dst_off = 0, dst_size = 0x1000;

    const char* dout_path = nullptr;
    PixelFormat dfmt = PixelFormat::BPP8;
    int dout_w = 0, dout_h = 0;

    uint32_t gpu_addr = 0;
    bool use_wb = true;               // default: WB load

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "-rom") && i+1 < argc) { rom_prog = argv[++i]; use_wb = false; }
        else if (!strcmp(argv[i], "-s1")  && i+1 < argc) s1 = argv[++i];
        else if (!strcmp(argv[i], "-s1off") && i+1 < argc) s1_off = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-s1in")  && i+1 < argc) s1in_path = argv[++i];
        else if (!strcmp(argv[i], "-s1fmt") && i+1 < argc) {
            if (!parse_format(argv[++i], s1fmt)) { fprintf(stderr, "bad -s1fmt\n"); return 1; }
        }
        else if (!strcmp(argv[i], "-s1w") && i+1 < argc) s1in_w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s1h") && i+1 < argc) s1in_h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-d")    && i+1 < argc) dst_raw = argv[++i];
        else if (!strcmp(argv[i], "-doff") && i+1 < argc) dst_off = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-dsize")&& i+1 < argc) dst_size = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-dout") && i+1 < argc) dout_path = argv[++i];
        else if (!strcmp(argv[i], "-dfmt") && i+1 < argc) {
            if (!parse_format(argv[++i], dfmt)) { fprintf(stderr, "bad -dfmt\n"); return 1; }
        }
        else if (!strcmp(argv[i], "-dw") && i+1 < argc) dout_w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-dh") && i+1 < argc) dout_h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-gpuaddr") && i+1 < argc) gpu_addr = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-v")) g_verbose = 1;
        else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return 1;
        }
    }

    memset(mem, 0, sizeof(mem));

    dut->rst = 1; dut->start = 0; dut->gpu_start = 0; dut->gpu_addr = 0;
    dut->rom_we = 0; dut->wb_ack = 0; dut->wb_dat_i = 0;
    step(); step();
    dut->rst = 0; step();

    dump_rom_hex("initial (bootloader)", 0x000, 16);

    if (rom_prog) {
        // 3.1: direct ROM load (erases bootloader)
        FILE* f = fopen(rom_prog, "r");
        if (!f) { fprintf(stderr, "cannot open %s\n", rom_prog); return 1; }
        char line[256]; uint16_t a = 0;
        while (fgets(line, sizeof(line), f)) {
            char* p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\0' || *p == '\n' || *p == ';' || *p == '#') continue;
            uint32_t w = strtoul(p, nullptr, 16);
            load_rom(a++, w);
        }
        fclose(f);
        printf("loaded ROM directly from %s (%u words)\n", rom_prog, a);
        dump_rom_hex("after direct load", 0x000, 16);
        dut->start = 1; step(); dut->start = 0;
    } else {
        // 3.2: WB load from RAM package
        if (s1)     load_hex_into_mem(s1, s1_off); 
        if (s1in_path) {
            size_t n = 0;
            if (!load_png_into_mem(s1in_path, s1fmt, s1_off, s1in_w, s1in_h, n))
                return 1;
        }
        dump_mem_hex("RAM (package)", 0x0000, 0x20);
        dut->gpu_addr  = gpu_addr;
        dut->gpu_start = 1; step(); dut->gpu_start = 0;
    }

    uint64_t limit = 100000000ULL;
    while (!dut->halted && !dut->error && cycles < limit) {
        step();
    }

    dump_rom_hex("after run 0x00", 0x000, 16);
    dump_rom_hex("after run 0x20", 0x020, 16);
    dump_mem_hex("RAM after run", 0x0000, 0x40);

    if (!dut->halted) { printf("FAIL: not halted (cycles=%llu)\n", (unsigned long long)cycles); return 1; }
    if (dut->error)   { printf("FAIL: error\n"); return 1; }
    printf("HALTED: cycles=%llu\n", (unsigned long long)cycles);

    printf("registers R0..R31:\n");
    for (int i = 0; i < 32; i++) {
        printf("  R%02d = %04X\n", i,
            (unsigned)dut->rootp->tb_top_wrapper__DOT__u_top__DOT__rf__DOT__mem[i]);
    }

    if (dst_raw) save_file(dst_raw, dst_off, dst_size);
    if (dout_path) {
        if (!dump_mem_to_png(dout_path, dfmt, dst_off, dout_w, dout_h))
            return 1;
    }

    tfp_p = nullptr;
    tfp.close();
    delete dut;
    return 0;
}