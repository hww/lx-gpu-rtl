// sim/tb_channel.cpp
//
// Интеграционный тест: pixel_engine + pixel_blender + gpu_cache.
// Проверяет, что при последовательных чтениях через blender
// приходят РАЗНЫЕ слова (idx инкрементируется), а не одно и то же.
//
// Запуск:
//   make sim-channel
// или:
//   verilator --cc --exe --build -DSIMULATION \
//       --top-module tb_channel \
//       rtl/pixel_engine.sv rtl/pixel_blender.sv rtl/gpu_cache.sv \
//       sim/tb_channel.cpp
//   ./obj_dir/Vtb_channel

#include "Vtb_channel.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <cstring>

static Vtb_channel* dut;
static int errors = 0;
static int checks = 0;

#define CLR_RST  "\033[0m"
#define CLR_GRN  "\033[32m"
#define CLR_RED  "\033[31m"
#define CLR_YEL  "\033[33m"
#define CLR_CYN  "\033[36m"
#define CLR_BLD  "\033[1m"

// ================================================================
// RAM-модель для Wishbone master (внутри gpu_cache)
// ================================================================
#define WB_MEM_WORDS  (64 * 1024)
static uint16_t wb_mem[WB_MEM_WORDS];

static void wb_clear() {
    for (int i = 0; i < WB_MEM_WORDS; i++) wb_mem[i] = 0;
}

static void tick() {
    dut->wb_ack_i = 0;
    dut->wb_dat_i = 0;
    if (dut->wb_cyc_o && dut->wb_stb_o) {
        uint32_t a = (dut->wb_adr_o >> 1) & (WB_MEM_WORDS - 1);
        if (dut->wb_we_o) {
            wb_mem[a] = dut->wb_dat_o;
        } else {
            dut->wb_dat_i = wb_mem[a];
        }
        dut->wb_ack_i = 1;
    }
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

// ================================================================
// Утилиты
// ================================================================
static void check(const char* name, uint32_t got, uint32_t exp) {
    checks++;
    if (got != exp) {
        printf(CLR_RED "  [FAIL] %-32s got=0x%04X exp=0x%04X" CLR_RST "\n",
               name, got, exp);
        errors++;
    } else {
        printf(CLR_GRN "  [ OK ] %-32s = 0x%04X" CLR_RST "\n", name, got);
    }
}

static void header(const char* t) {
    printf("\n" CLR_BLD CLR_YEL "=== %s ===" CLR_RST "\n", t);
}

// ================================================================
// Настройка pixel_engine (mode, idx, step) для канала 0
// ================================================================
static void pe_set_mode(uint8_t mode) {
    dut->ch_i = 0;
    dut->mode_we_i = 1; dut->mode_i = mode;
    dut->idx_we_i = 0; dut->idxh_we_i = 0; dut->step_we_i = 0;
    dut->pe_access_i = 0; dut->pe_access_done_i = 0;
    tick();
    dut->mode_we_i = 0;
}

static void pe_set_idx(uint16_t idx) {
    dut->ch_i = 0;
    dut->idx_we_i = 1; dut->idx_i = idx;
    dut->mode_we_i = 0; dut->idxh_we_i = 0; dut->step_we_i = 0;
    tick();
    dut->idx_we_i = 0;
}

static void pe_set_step(uint16_t step) {
    dut->ch_i = 0;
    dut->step_we_i = 1; dut->step_i = step;
    dut->mode_we_i = 0; dut->idx_we_i = 0; dut->idxh_we_i = 0;
    tick();
    dut->step_we_i = 0;
}

static void pe_set_page(uint8_t page) {
    dut->ch_i = 0;
    dut->page_we_i = 1; dut->page_i = page;
    tick();
    dut->page_we_i = 0;
}

// ================================================================
// Один цикл доступа: blender -> cache -> engine
//   blender.cyc/stb/we = 1 (read)
//   top связывает: blender.ack_i = gpu_cache.access_done
//                  gpu_cache.access_i = blender cyc&stb
//                  gpu_cache.word_i = pe.word_o
//   после завершения engine.access_done_i = 1 -> idx++
// Возвращает: dat_o от blender (прочитанное слово/пиксель)
// ================================================================
static uint16_t do_blend_read() {
    // 1. Запуск: blender.cyc=1, stb=1, we=0
    dut->bl_cyc_i = 1;
    dut->bl_stb_i = 1;
    dut->bl_we_i  = 0;
    dut->bl_adr_i = 0;   // read
    dut->pe_access_i = 1;
    dut->gc_access_i = 1;

    // 2. Ждём ack от cache (access_done)
    int timeout = 200;
    while (!dut->gc_access_done_o && timeout-- > 0) tick();

    // 3. В этот такт blender.ack_o должен быть 1 (комбинационно)
    uint16_t dat = dut->bl_dat_o;
    int      ack = dut->bl_ack_o;
    if (!ack) {
        printf(CLR_RED "  [FAIL] blender ack_o = 0 во время gc_access_done" CLR_RST "\n");
        errors++;
    }

    // 4. Дать engine'у увидеть access_done и инкрементировать idx
    dut->pe_access_done_i = 1;
    tick();
    dut->pe_access_done_i = 0;

    // 5. Снять запрос
    dut->bl_cyc_i = 0;
    dut->bl_stb_i = 0;
    dut->pe_access_i = 0;
    dut->gc_access_i = 0;
    tick();

    return dat;
}

// ================================================================
// Main
// ================================================================
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vtb_channel;

    printf(CLR_BLD "=== tb_channel: pixel_engine + pixel_blender + gpu_cache ===" CLR_RST "\n");

    // ---- reset ----
    dut->rst = 1;
    dut->ch_i = 0;
    dut->mode_we_i = 0; dut->idxh_we_i = 0;
    dut->idx_we_i = 0; dut->step_we_i = 0;
    dut->mode_i = 0; dut->idxh_i = 0;
    dut->idx_i = 0; dut->step_i = 0;
    dut->page_we_i = 0; dut->page_i = 0;
    dut->pe_access_i = 0; dut->pe_access_done_i = 0;
    dut->gc_access_i = 0; dut->gc_we_i = 0; dut->gc_wdata_i = 0;
    dut->gc_flush_en_i = 0; dut->gc_flush_ch_i = 0;
    dut->gc_page_we_i = 0; dut->gc_page_i = 0;
    dut->bl_cyc_i = 0; dut->bl_stb_i = 0; dut->bl_we_i = 0;
    dut->bl_adr_i = 0; dut->bl_pixel_i = 0; dut->bl_ack_i = 0;
    dut->wb_ack_i = 0; dut->wb_dat_i = 0;
    for (int i = 0; i < 4; i++) tick();
    dut->rst = 0;
    tick();

    wb_clear();

    // ---- загружаем 4 слова в RAM ----
    // Адрес в wb_mem — это word-адрес: wb_adr_o = {page, word, 1'b0}
    // Для word=0..3, page=0: байтовый адрес 0, 2, 4, 6 -> word-адрес 0,1,2,3
    uint16_t expected[4] = {0x1111, 0x2222, 0x3333, 0x4444};
    for (int i = 0; i < 4; i++) wb_mem[i] = expected[i];

    // ---- настраиваем pixel_engine: mode=4 (16bpp), idx=0, step=1.0 ----
    header("Configure pixel_engine (mode=4, idx=0, step=1.0)");
    pe_set_mode(4);
    pe_set_idx(0);
    pe_set_step(0x0010);   // 1.0 в формате 15.4
    pe_set_page(0);
    check("pe.mode_r[0]", dut->pe_mode_o, 4);
    check("pe.word_o",    dut->pe_word_o, 0);
    check("pe.pos_o",     dut->pe_pos_o,  0);

    // ---- 4 последовательных чтения ----
    header("4 sequential reads through blender -> cache -> engine");
    for (int i = 0; i < 4; i++) {
        uint16_t dat = do_blend_read();
        check("bl_dat_o (word)", dat, expected[i]);
        check("pe.word_o (next)", dut->pe_word_o, i + 1);
        check("pe.pos_o",         dut->pe_pos_o,  0);
    }

    // ---- итог ----
    printf("\n" CLR_BLD);
    if (errors == 0)
        printf(CLR_GRN "=== ALL %d CHECKS PASSED ===" CLR_RST "\n", checks);
    else
        printf(CLR_RED "=== %d/%d CHECKS FAILED ===" CLR_RST "\n", errors, checks);
    printf(CLR_RST);

    delete dut;
    return errors ? 1 : 0;
}