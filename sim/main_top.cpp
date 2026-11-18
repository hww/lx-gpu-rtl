// sim/main_top.cpp — top.sv test (Vtop, без tb_top_wrapper)
#include "Vtop.h"
#include "Vtop___024root.h"     // ← вернуть, он нужен для rootp
#include "verilated.h"
#include "verilated_vcd_c.h"
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

static Vtop* dut;
static VerilatedVcdC* tfp_p = nullptr;
static uint64_t cycles = 0;
static int g_verbose = 0;
static int g_fails = 0;

#define MEM_WORDS  65536
#define ROM_SIZE   4096

static uint16_t mem[MEM_WORDS];
static uint32_t rom[ROM_SIZE];


// Умная загрузка программы:
// Возвращает true, если загружен пакет [SIZE, START] (.asm / .pkg),
// и false, если загружен сырой код без заголовка (.hex).
static bool load_file_smart(const char* path, uint32_t mem_off, uint32_t gpu_addr, bool has_gpu_addr) {
    std::string p(path);
    size_t dot = p.find_last_of('.');
    std::string ext = (dot != std::string::npos) ? p.substr(dot) : "";

    if (ext == ".asm") {
        printf("Loading ASM file to Z80 memory...\n");
         uint16_t asm_org = 0; // Инициализируем нулем, ассемблер сам заполнит
        auto words = asm_assemble_file(path, &asm_org);
        if (words.empty()) { fprintf(stderr, "asm empty or error\n"); return false; }
        
        // Z80 memory address
        uint32_t a = mem_off;

        // GPU Memiry address
        // Если в файле не было .org, asm_org останется 0. 
        // Если был - он будет равен значению из .org (например, 0x20 или 0x100).
        printf("  ASM org = 0x%04X\n", asm_org);
        uint16_t gpu_start_addr = has_gpu_addr ? (uint16_t)gpu_addr : asm_org;

        // Fill memory with file
        mem[a++] = (uint16_t)((words.size() * 2) & 0xFFFF); // SIZE
        mem[a++] = gpu_start_addr;                              // START
        uint32_t gpu_pc = gpu_start_addr;
        for (auto w : words) {
            auto cur_a = a;
            mem[a++] = (uint16_t)(w & 0xFFFF);
            mem[a++] = (uint16_t)((w >> 16) & 0xFFFF);
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
            if (a < MEM_WORDS) mem[a] = (uint16_t)w;
            a++;
        }
        fclose(f);
        if (has_gpu_addr && mem_off + 1 < MEM_WORDS) {
            mem[mem_off + 1] = (uint16_t)gpu_addr;
        }
        printf("loaded %s (.pkg) -> pkg @ RAM 0x%04X (GPU START=0x%04X)\n",
               path, mem_off, mem[mem_off + 1]);
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
            if (a + 1 < MEM_WORDS) {
                mem[a]     = (uint16_t)(w & 0xFFFF);
                mem[a + 1] = (uint16_t)((w >> 16) & 0xFFFF);
            }
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
            uint32_t v = (w < ROM_SIZE) ? (uint32_t)dut->rootp->top__DOT__u_rom__DOT__mem[w] : 0;

            printf(" %08X", v);
        }
        printf("\n");
    }
}

// ===== clock =====
static void tick(int clk, bool log) {
    dut->clk = clk;
    dut->eval();
    if (tfp_p) tfp_p->dump(cycles);
    ++cycles;

    // Печатаем инструкцию только в момент её завершения (x_do == 1)
    // и только на переднем фронте клока (чтобы не дублировать)
    if (log && g_verbose && clk == 1 && dut->rootp->top__DOT__x_do) {
        uint32_t insn = (uint32_t)dut->rootp->top__DOT__x_ins_r;
        
        // Вычисляем адрес текущей инструкции. 
        // fu_pc уже указывает на следующую, поэтому вычитаем 1.
        // Если был переход (t_fu_x_jump), то fu_pc - это target, и вычитать 1 нельзя.
        // Но для лога просто покажем fu_pc (адрес следующей) и текущую инструкцию.
        uint32_t pc_cur = (uint32_t)dut->pc_o; 
        if (dut->rootp->top__DOT__t_fu_x_jump) {
            // Если это JMP по регистру, pc_o уже target. 
            // Оставим как есть, чтобы видеть, куда прыгнули.
        } else {
            pc_cur = (pc_cur > 0) ? (pc_cur - 1) : 0;
        }

        // Форматируем вывод: t=..., PC=..., insn, disasm
        printf("[dbg] t=%llu PC=%03X  %08X  %s\n",
               (unsigned long long)cycles,
               pc_cur,
               insn,
               disasm32(insn));
    }
}

static void step() {
    // WB slave
    dut->wb_ack_i = 0;
    dut->wb_dat_i = 0;

    // --- ЛОГ WISHBONE (только при -v) ---
    if (g_verbose && dut->wb_cyc_o && dut->wb_stb_o) {
        uint32_t addr = (dut->wb_adr_o >> 1) & (MEM_WORDS - 1);
        // Достаем IDX и STEP из pixel_engine (канал 0)
        uint32_t idx_cur  = dut->rootp->top__DOT__u_ch__DOT__u_pe__DOT__reg_idx;
        uint32_t step_cur = dut->rootp->top__DOT__u_ch__DOT__u_pe__DOT__reg_step;
        printf("[WB] %s adr=0x%04X dat=0x%04X (IDX=0x%05X STEP=0x%05X)\n",
               dut->wb_we_o ? "WR" : "RD",
               (unsigned)dut->wb_adr_o,
               (unsigned)dut->wb_we_o ? (unsigned)dut->wb_dat_o : (unsigned)mem[addr],
               idx_cur, step_cur);
    }


    if (dut->wb_cyc_o && dut->wb_stb_o) {
        uint32_t a = (dut->wb_adr_o >> 1) & (MEM_WORDS - 1);
        if (dut->wb_we_o) mem[a] = dut->wb_dat_o;
        else              dut->wb_dat_i = mem[a];
        dut->wb_ack_i = 1;
    }

    tick(0, false);
    tick(1, true);
    tick(1, false);
    tick(0, false);
}

// ===== checkers =====
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

// ===== ROM loader (C++, без $readmemh) =====
static void load_hex_into_rom(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return; }
    char line[256];
    uint32_t a = 0;
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == ';' || *p == '#') continue;
        uint32_t w = (uint32_t)strtoul(p, nullptr, 16);
        if (a < ROM_SIZE) rom[a] = w;
        a++;
    }
    fclose(f);
    printf(CLR_GRN "loaded" CLR_RST " %s into ROM (%u words)\n", path, a);

    // записать в ROM через иерархию (Verilator public)
    for (uint32_t i = 0; i < ROM_SIZE; i++)
        dut->rootp->top__DOT__u_rom__DOT__mem[i] = rom[i];
}

// ===== RAM loader =====
static void load_hex_into_mem(const char* path, uint32_t word_off) {
    FILE* f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return; }
    char line[256];
    uint32_t a = word_off;
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == ';' || *p == '#') continue;
        uint32_t w = (uint32_t)strtoul(p, nullptr, 16);
        if (a < MEM_WORDS) mem[a] = (uint16_t)w;
        a++;
    }
    fclose(f);
    printf(CLR_GRN "loaded" CLR_RST " %s into RAM @word 0x%04X (%u words)\n",
           path, word_off, a - word_off);
}

// ===== PNG loaders =====
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
    for (size_t i = 0; i < words.size(); i++) mem[word_off + i] = words[i];
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

// Вспомогательная функция: "path/to/dst.png" -> "path/to/dst_out.png"
static std::string make_default_out_path(const char* in_path) {
    std::string p(in_path);
    size_t dot = p.find_last_of('.');
    size_t slash = p.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        return p.substr(0, dot) + "_out" + p.substr(dot);
    }
    return p + "_out.png";
}

// ===== main =====
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    printf("DEBUG argc=%d\n", argc);
    dut = new Vtop;

    // ===== args =====
    // 1. GPU Program
    const char* prog_path    = nullptr;
    uint32_t    prog_off     = 0;        // адрес в RAM (по умолчанию 0x0000)
    uint32_t    gpu_addr     = 0;        // адрес в GPU (если программа не пакет, либо переопределение)
    bool        has_gpu_addr = false;

    // 2. Source 1 (входная картинка 1)
    const char* s1in_path = nullptr;
    uint32_t    s1_off    = 0;
    PixelFormat s1fmt     = PixelFormat::BPP8;
    int         s1in_w    = 0, s1in_h = 0;

    // 3. Destination / Source 2 (входная картинка 2 — защищена от перезаписи на диске)
    const char* din_path  = nullptr;
    uint32_t    dst_off   = 0;
    uint32_t    dst_size  = 0x1000;
    PixelFormat dfmt      = PixelFormat::BPP8;
    int         dst_w     = 0, dst_h = 0;

    // 4. Результат симуляции (куда сохраняем итоговый буфер из dst_off)
    const char* out_png   = nullptr;
    const char* out_raw   = nullptr;

    // 5. Настройки симулятора
    uint64_t    max_cycles     = 200000ULL;
    uint64_t    progress_every = 0;
    int         trace          = 0;

    for (int i = 1; i < argc; i++) {
        // Program & GPU address
        if      ((!strcmp(argv[i], "-prog") || !strcmp(argv[i], "-p") || !strcmp(argv[i], "-s1")) && i+1 < argc)
            prog_path = argv[++i];
        else if (!strcmp(argv[i], "-poff")    && i+1 < argc) prog_off = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-gpuaddr") && i+1 < argc) {
            gpu_addr     = strtoul(argv[++i], 0, 0);
            has_gpu_addr = true;
        }

        // Source 1 image
        else if (!strcmp(argv[i], "-s1in")    && i+1 < argc) s1in_path = argv[++i];
        else if (!strcmp(argv[i], "-s1off")   && i+1 < argc) s1_off    = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-s1fmt")   && i+1 < argc) {
            if (!parse_format(argv[++i], s1fmt)) { fprintf(stderr, "bad -s1fmt\n"); return 1; }
        }
        else if (!strcmp(argv[i], "-s1w")     && i+1 < argc) s1in_w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s1h")     && i+1 < argc) s1in_h = atoi(argv[++i]);

        // Destination / Source 2 input image (read-only on disk)
        else if ((!strcmp(argv[i], "-din") || !strcmp(argv[i], "-s2in")) && i+1 < argc)
            din_path = argv[++i];
        else if (!strcmp(argv[i], "-doff")    && i+1 < argc) dst_off  = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-dsize")   && i+1 < argc) dst_size = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-dfmt")    && i+1 < argc) {
            if (!parse_format(argv[++i], dfmt)) { fprintf(stderr, "bad -dfmt\n"); return 1; }
        }
        else if (!strcmp(argv[i], "-dw")      && i+1 < argc) dst_w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-dh")      && i+1 < argc) dst_h = atoi(argv[++i]);

        // Output result paths
        else if ((!strcmp(argv[i], "-out") || !strcmp(argv[i], "-dout")) && i+1 < argc)
            out_png = argv[++i];
        else if ((!strcmp(argv[i], "-rawout") || !strcmp(argv[i], "-d")) && i+1 < argc)
            out_raw = argv[++i];

        // Simulation settings
        else if (!strcmp(argv[i], "-maxcycles") && i+1 < argc) max_cycles     = strtoull(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-progress")  && i+1 < argc) progress_every = strtoull(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-v"))        g_verbose = 1;
        else if (!strcmp(argv[i], "-trace"))    trace     = 1;
        else { fprintf(stderr, "unknown arg: %s\n", argv[i]); return 1; }
    }
    
    printf("DEBUG g_verbose=%d\n", g_verbose);

    // Если входной -din задан, а -out не указан — генерируем имя *_out.png автоматически,
    // чтобы не перезаписывать исходный файл din_path
    std::string auto_out_png;
    if (!out_png && din_path) {
        auto_out_png = make_default_out_path(din_path);
        out_png = auto_out_png.c_str();
    }

    // ===== VCD Trace =====
    VerilatedVcdC tfp;
    if (trace) {
        tfp_p = &tfp;
        Verilated::traceEverOn(true);
        dut->trace(&tfp, 99);
        tfp.open("sim/trace.vcd");
    }

    memset(mem, 0, sizeof(mem));
    memset(rom, 0, sizeof(rom));

    // ===== reset =====
    header("RESET");
    dut->rst = 1; dut->start_i = 0; dut->gpu_start_i = 0; dut->gpu_reset_i = 0;
    dut->gpu_addr_i = 0;
    dut->wb_ack_i = 0; dut->wb_dat_i = 0;
    step(); step();
    dut->rst = 0; step();

    check_bit("halted_after_reset", dut->halted_o, 1);
    check_eq ("pc_after_reset",     dut->pc_o, 0, 12);

    // ===== ROM load (C++, из rtl/bootloader.hex) =====
    header("ROM BOOTLOADER");
    load_hex_into_rom("rtl/bootloader.hex");
    {
        uint32_t r0 = dut->rootp->top__DOT__u_rom__DOT__mem[0];
        check_nonzero("rom[0] loaded", r0, 32);
        printf("  rom[0] = %08X  %s\n", r0, disasm32(r0));
    }
    dump_rom_hex("initial", 0x000, 16);

    // ===== RAM load =====
    header("LOAD PROGRAM & BUFFERS INTO RAM");
    bool is_pkg = false;
    if (prog_path) {
        is_pkg = load_file_smart(prog_path, prog_off, gpu_addr, has_gpu_addr);
        dump_mem_hex("RAM (program)", prog_off, 0x20);

        if (is_pkg) {
            check_nonzero("pkg SIZE",      mem[prog_off + 0], 16);
            check_nonzero("pkg START",     mem[prog_off + 1], 16);
            check_eq     ("pkg SIZE even", mem[prog_off + 0] & 1, 0, 1);
        }
    }

    if (s1in_path) {
        printf("  [S1IN] %s -> RAM @ 0x%04X (%dx%d)\n", s1in_path, s1_off, s1in_w, s1in_h);
        if (!load_png_into_mem(s1in_path, s1fmt, s1_off, s1in_w, s1in_h)) return 1;
    }

    if (din_path) {
        printf("  [DIN ] %s -> RAM @ 0x%04X (%dx%d) [protected]\n", din_path, dst_off, dst_w, dst_h);
        if (!load_png_into_mem(din_path, dfmt, dst_off, dst_w, dst_h)) return 1;
    }

    // ===== gpu_start =====
    header("GPU_START");
    // Если загружен пакет (.asm/.pkg) — стартовый адрес берётся из заголовка пакета (mem[prog_off + 1]).
    // Если загружен не пакет (.hex) — используем явно переданный -gpuaddr.
    dut->gpu_addr_i  = (is_pkg ? (prog_off * 2) : gpu_addr) & 0xFFFFFF;
    dut->gpu_start_i = 1; step(); dut->gpu_start_i = 0;
    check_bit("halted after start", dut->halted_o, 0);
    check_eq ("pc after start",     dut->pc_o, 0, 12);

    // ===== run =====
    header("RUN");
    uint64_t run_start = cycles;
    uint64_t last_prog = cycles;
    int timeout = 0;
    
    while (dut->halted_o) {
        step();
        if ((cycles - run_start) >= max_cycles) {
            timeout = 1;
            printf("Infinitive initial HALT state...");
            break;
        }
    }

    run_start = cycles;

    while (!dut->halted_o && !dut->error_o) {
        step();

        if (progress_every && (cycles - last_prog) >= progress_every) {
            printf(CLR_YEL "[progress]" CLR_RST " cycles=%llu pc=%03X R0=%04X R1=%04X R2=%04X R3=%04X\n",
                   (unsigned long long)cycles, (unsigned)dut->pc_o,
                   (unsigned)dut->rootp->top__DOT__u_rf__DOT__mem_a[0],
                   (unsigned)dut->rootp->top__DOT__u_rf__DOT__mem_a[1],
                   (unsigned)dut->rootp->top__DOT__u_rf__DOT__mem_a[2],
                   (unsigned)dut->rootp->top__DOT__u_rf__DOT__mem_a[3]);
            last_prog = cycles;
        }

        if ((cycles - run_start) >= max_cycles) {
            printf("Infinitive execution...");
            timeout = 1;
            break;
        }
    }

    // ===== result =====
    header("RUN RESULT");
    printf("  cycles  = %llu\n", (unsigned long long)cycles);
    printf("  limit   = %llu\n", (unsigned long long)max_cycles);
    printf("  halted  = %d\n", (int)dut->halted_o);
    printf("  error   = %d\n", (int)dut->error_o);
    printf("  pc      = %03X\n", (unsigned)dut->pc_o);
    printf("  errcode = %03X\n", (unsigned)dut->rootp->top__DOT__ch_error);

    if (timeout) {
        printf(CLR_RED "  TIMEOUT: %llu cycles exceeded\n" CLR_RST,
               (unsigned long long)max_cycles);
    }

    check_bit("not timeout",  !timeout,      1);
    check_bit("halted",       dut->halted_o, 1);
    check_bit("no error",     !dut->error_o, 1);

    // dump on failure
    if (g_fails) {
        header("CONTEXT AT FAILURE");
        printf("  pc = %03X, R0=%04X R1=%04X R2=%04X R3=%04X\n",
               (unsigned)dut->pc_o,
               (unsigned)dut->rootp->top__DOT__u_rf__DOT__mem_a[0],
               (unsigned)dut->rootp->top__DOT__u_rf__DOT__mem_a[1],
               (unsigned)dut->rootp->top__DOT__u_rf__DOT__mem_a[2],
               (unsigned)dut->rootp->top__DOT__u_rf__DOT__mem_a[3]);
        printf("  roma=%03X roml=%04X rom_we=%d\n",
               (unsigned)dut->rootp->top__DOT__t_roma_r,
               (unsigned)dut->rootp->top__DOT__t_roml_r,
               (int)dut->rootp->top__DOT__t_rom_acc_we);
        printf("  ch: error=%d status=%X\n",
            (int)dut->rootp->top__DOT__ch_error,
            (unsigned)dut->rootp->top__DOT__ch_status);
    }

    dump_rom_hex("after run 0x00", 0x000, 16);
    dump_rom_hex("after run 0x20", 0x020, 16);
    dump_mem_hex("RAM after run", dst_off, 0x40);

    printf("\n" CLR_BLD "registers R0..R31:" CLR_RST "\n");
    for (int i = 0; i < 32; i++) {
        printf("  R%02d = %04X\n", i,
            (unsigned)dut->rootp->top__DOT__u_rf__DOT__mem_a[i]);
    }

    // ===== Save outputs (исходный din_path остаётся нетронутым) =====
    if (out_raw) {
        printf("  [SAVE RAW] -> %s (from 0x%04X, %u words)\n", out_raw, dst_off, dst_size);
        FILE* f = fopen(out_raw, "wb");
        if (f) {
            for (uint32_t i = 0; i < dst_size; i++) {
                uint32_t w = dst_off + i;
                uint16_t v = (w < MEM_WORDS) ? mem[w] : 0;
                fputc(v & 0xFF, f); fputc(v >> 8, f);
            }
            fclose(f);
        }
    }

    if (out_png) {
        printf("  [SAVE PNG] -> %s (from 0x%04X, %dx%d)\n", out_png, dst_off, dst_w, dst_h);
        if (!dump_mem_to_png(out_png, dfmt, dst_off, dst_w, dst_h)) g_fails++;
    }

    printf("\n" CLR_BLD);
    if (g_fails == 0) printf(CLR_GRN "=== ALL CHECKS PASSED ===" CLR_RST "\n");
    else              printf(CLR_RED "=== %d CHECK(S) FAILED ===" CLR_RST "\n", g_fails);
    printf(CLR_RST);

    if (trace) {
        tfp_p = nullptr;
        tfp.close();
    }
    delete dut;
    return g_fails ? 1 : 0;
}