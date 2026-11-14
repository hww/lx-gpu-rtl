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

#define MEM_WORDS 32768
static uint16_t mem[MEM_WORDS];

// ---------------------------------------------------------------------------
// Тактирование + эмуляция WB-памяти
// ---------------------------------------------------------------------------
static void step() {
    dut->wb_ack = 0;
    dut->wb_dat_i = 0;
    if (dut->wb_cyc && dut->wb_stb) {
        uint32_t a = (dut->wb_adr >> 1) & (MEM_WORDS - 1);
        printf("WB: adr=%06X a=%04X we=%d dat_o=%04X mem=%04X\n",
               (unsigned)dut->wb_adr, a, (int)dut->wb_we,
               (unsigned)dut->wb_dat_o, mem[a]);
        if (dut->wb_we) mem[a] = dut->wb_dat_o;
        else           dut->wb_dat_i = mem[a];
        dut->wb_ack = 1;
    }
    dut->eval();
    dut->clk = 1; dut->eval();
    if (tfp_p) tfp_p->dump(cycles);
    dut->clk = 0; dut->eval();
    ++cycles;
}

static void load_rom(uint16_t addr, uint32_t w) {
    dut->rom_we = 1; dut->rom_addr = addr; dut->rom_wdata = w;
    step();
    dut->rom_we = 0;
}

static void load_file(const char* path, uint32_t byte_off) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); return; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(sz);
    fread(buf.data(), 1, sz, f); fclose(f);
    for (long i = 0; i + 1 < sz; i += 2) {
        uint32_t w = (byte_off + i) / 2;
        if (w < MEM_WORDS) mem[w] = buf[i] | (buf[i+1] << 8);
    }
    printf("loaded %s (%ld bytes) at 0x%X\n", path, sz, byte_off);
}

static void save_file(const char* path, uint32_t byte_off, uint32_t size) {
    FILE* f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return; }
    for (uint32_t i = 0; i < size; i += 2) {
        uint32_t w = (byte_off + i) / 2;
        uint16_t v = (w < MEM_WORDS) ? mem[w] : 0;
        fputc(v & 0xFF, f); fputc(v >> 8, f);
    }
    fclose(f);
    printf("saved %s (%u bytes) from 0x%X\n", path, size, byte_off);
}

// ---------------------------------------------------------------------------
// Выбор палитры по формату
// ---------------------------------------------------------------------------
static const uint32_t* palette_for(PixelFormat fmt, int& out_n) {
    switch (fmt) {
        case PixelFormat::BPP1:  out_n = 2;   return vga16_palette;
        case PixelFormat::BPP4:  out_n = 16;  return vga16_palette;
        case PixelFormat::BPP8:  out_n = 256; return get_websafe216_palette();
        default:                 out_n = 0;   return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Загрузка PNG в mem[]
// ---------------------------------------------------------------------------
static bool load_png_into_mem(const char* path, PixelFormat fmt,
                              uint32_t byte_off,
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

    uint32_t base_word = byte_off / 2;
    if (base_word + words.size() > MEM_WORDS) {
        fprintf(stderr, "load_png: PNG overflows memory (%zu words at 0x%X)\n",
                words.size(), byte_off);
        return false;
    }
    for (size_t i = 0; i < words.size(); i++) mem[base_word + i] = words[i];

    printf("loaded %s (%dx%d, %s) at byte offset 0x%X (%zu words)\n",
           path, img.width, img.height, format_name(fmt),
           byte_off, words.size());
    out_words = words.size();
    return true;
}

// ---------------------------------------------------------------------------
// Дамп mem[] в PNG
// ---------------------------------------------------------------------------
static bool dump_mem_to_png(const char* path, PixelFormat fmt,
                            uint32_t byte_off, int w, int h) {
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
    uint32_t base_word = byte_off / 2;
    for (size_t i = 0; i < n_words; i++) {
        words[i] = (base_word + i < MEM_WORDS) ? mem[base_word + i] : 0;
    }

    std::vector<uint8_t> idx = unpack_pixels(words, w, h, fmt);

    int pal_n = 0;
    const uint32_t* pal = palette_for(fmt, pal_n);

    Image out = dequantize(idx, w, h, fmt, pal);
    if (!write_image(path, out)) return false;

    printf("dumped %s (%dx%d, %s) from byte offset 0x%X\n",
           path, w, h, format_name(fmt), byte_off);
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vtb_top_wrapper;

    VerilatedVcdC tfp;      // локальный
    tfp_p = &tfp;
    Verilated::traceEverOn(true);
    dut->trace(&tfp, 99);
    tfp.open("sim/trace.vcd");

    // ---- старые аргументы (raw binary) ----
    const char* prog = nullptr;
    const char* s1 = nullptr; uint32_t s1_off_raw = 0x1000;
    const char* s2 = nullptr; uint32_t s2_off_raw = 0x2000;
    const char* dst_raw = nullptr; uint32_t dst_off_raw = 0x3000, dst_size_raw = 0x1000;

    // ---- новые аргументы (PNG) ----
    const char* s1in_path = nullptr;
    PixelFormat s1fmt = PixelFormat::BPP8;
    uint32_t    s1in_off = 0;
    int         s1in_w = 0, s1in_h = 0;

    const char* dout_path = nullptr;
    PixelFormat dfmt = PixelFormat::BPP8;
    uint32_t    dout_off = 0;
    int         dout_w = 0, dout_h = 0;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "-p")     && i+1 < argc) prog = argv[++i];
        else if (!strcmp(argv[i], "-s1")    && i+1 < argc) s1 = argv[++i];
        else if (!strcmp(argv[i], "-s2")    && i+1 < argc) s2 = argv[++i];
        else if (!strcmp(argv[i], "-d")     && i+1 < argc) dst_raw = argv[++i];
        else if (!strcmp(argv[i], "-s1off") && i+1 < argc) s1_off_raw = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-s2off") && i+1 < argc) s2_off_raw = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-doff")  && i+1 < argc) dst_off_raw = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-dsize") && i+1 < argc) dst_size_raw = strtoul(argv[++i], 0, 0);

        else if (!strcmp(argv[i], "-s1in")  && i+1 < argc) s1in_path = argv[++i];
        else if (!strcmp(argv[i], "-s1fmt") && i+1 < argc) {
            if (!parse_format(argv[++i], s1fmt)) { fprintf(stderr, "bad -s1fmt\n"); return 1; }
        }
        else if (!strcmp(argv[i], "-s1off") && i+1 < argc) { /* уже разобрано выше как raw */ }
        else if (!strcmp(argv[i], "-s1woff")&& i+1 < argc) s1in_off = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-s1w")   && i+1 < argc) s1in_w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s1h")   && i+1 < argc) s1in_h = atoi(argv[++i]);

        else if (!strcmp(argv[i], "-dout")  && i+1 < argc) dout_path = argv[++i];
        else if (!strcmp(argv[i], "-dfmt")  && i+1 < argc) {
            if (!parse_format(argv[++i], dfmt)) { fprintf(stderr, "bad -dfmt\n"); return 1; }
        }
        else if (!strcmp(argv[i], "-doff")  && i+1 < argc) { /* уже разобрано выше как raw */ }
        else if (!strcmp(argv[i], "-dwoff") && i+1 < argc) dout_off = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-dw")    && i+1 < argc) dout_w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-dh")    && i+1 < argc) dout_h = atoi(argv[++i]);

        else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return 1;
        }
    }

    if (s1in_path) s1in_off = s1_off_raw;
    if (dout_path) dout_off = dst_off_raw;

    memset(mem, 0, sizeof(mem));

    dut->rst = 1; dut->start = 0;
    dut->rom_we = 0; dut->wb_ack = 0; dut->wb_dat_i = 0;
    step(); step();
    dut->rst = 0; step();

    if (prog) {
        FILE* f = fopen(prog, "r");
        if (!f) { fprintf(stderr, "cannot open %s\n", prog); return 1; }
        char line[256]; uint16_t a = 0;
        while (fgets(line, sizeof(line), f)) {
            char* p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\0' || *p == '\n' || *p == ';' || *p == '#') continue;
            uint32_t w = strtoul(p, nullptr, 16);
            load_rom(a++, w);
            printf("  [%03X] %08X\n", (unsigned)(a-1), w);
        }
        fclose(f);
        printf("loaded microcode %s (%u words)\n", prog, a);
    }

    if (s1) load_file(s1, s1_off_raw);
    if (s2) load_file(s2, s2_off_raw);

    if (s1in_path) {
        size_t n_words = 0;
        if (!load_png_into_mem(s1in_path, s1fmt, s1in_off, s1in_w, s1in_h, n_words))
            return 1;
    }

    dut->start = 1; step(); dut->start = 0;

    // ---- прогон с отладкой первых 30 тактов ----
    uint64_t limit = 100000000ULL;
    while (!dut->halted && !dut->error && cycles < limit) {
        if (cycles >= 0 && cycles < 1000) {
printf("cyc=%2llu pc=%03X insn=%08X ad=%d hit=%d we=%d\n",
    (unsigned long long)cycles,
    (unsigned)dut->rootp->tb_top_wrapper__DOT__u_top__DOT__pc,
    (unsigned)dut->rootp->tb_top_wrapper__DOT__u_top__DOT__rom__DOT__data_r,
    (int)dut->rootp->tb_top_wrapper__DOT__u_top__DOT__gc__DOT__access_done_r,
    (int)dut->rootp->tb_top_wrapper__DOT__u_top__DOT__gc_hit,
    (int)dut->rootp->tb_top_wrapper__DOT__u_top__DOT__we);
        }
        step();
    }


    if (!dut->halted) {
        printf("FAIL: not halted (cycles=%llu)\n", (unsigned long long)cycles);
        return 1;
    }
    if (dut->error) {
        printf("FAIL: error\n");
        return 1;
    }
    printf("HALTED: cycles=%llu\n", (unsigned long long)cycles);

    printf("registers R0..R31:\n");
    for (int i = 0; i < 32; i++) {
        printf("  R%02d = %04X\n", i, (unsigned)dut->rootp->tb_top_wrapper__DOT__u_top__DOT__rf__DOT__mem_a[i]);
    }

    printf("mem[0x000]=%04X mem[0x100]=%04X mem[0x180]=%04X mem[0x200]=%04X\n",
           mem[0x000], mem[0x100], mem[0x180], mem[0x200]);

    if (dst_raw) save_file(dst_raw, dst_off_raw, dst_size_raw);

    if (dout_path) {
        if (!dump_mem_to_png(dout_path, dfmt, dout_off, dout_w, dout_h))
            return 1;
    }

    tfp_p = nullptr;
    tfp.close();
    delete dut;

   return 0;
}