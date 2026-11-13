#include "Vpixel_engine.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <string>

static Vpixel_engine* dut;
static int errors = 0;

static void tick() {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
    dut->clk = 0; dut->eval();
}

static void check(const char* name, uint16_t got, uint16_t exp) {
    if (got != exp) {
        printf("FAIL %s: got %04X exp %04X\n", name, got, exp);
        errors++;
    }
}

static void check_bit(const char* name, int got, int exp) {
    if (got != exp) {
        printf("FAIL %s: got %d exp %d\n", name, got, exp);
        errors++;
    }
}

// Записать IDX (с опциональным IDXH)
static void set_idx(uint8_t ch, uint16_t idx, uint8_t idxh, uint8_t mode) {
    dut->ch = ch;
    dut->mode_we = 1; dut->mode_in = mode;
    dut->idxh_we = 0; dut->idx_we = 0; dut->step_we = 0;
    dut->access = 0;
    tick();

    if (idxh != 0xFF) {
        dut->idxh_we = 1; dut->idxh_in = idxh & 7;
        dut->mode_we = 0;
        tick();
    }

    dut->idx_we = 1; dut->idx_in = idx;
    dut->idxh_we = 0;
    tick();

    dut->idx_we = 0;
    dut->eval();
}

static void set_step(uint8_t ch, uint16_t step) {
    dut->ch = ch;
    dut->step_we = 1; dut->step_in = step;
    dut->mode_we = 0; dut->idx_we = 0; dut->idxh_we = 0; dut->access = 0;
    tick();
    dut->step_we = 0;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vpixel_engine;

    dut->rst = 1; tick(); tick();
    dut->rst = 0;

    printf("=== pixel_engine tests ===\n");

    // 16 bpp (mode=4, k=0): IDX=0x1234 -> word=0x1234, pos=0
    set_idx(0, 0x1234, 0, 4);
    check("16bpp.IDX1234.word", dut->word_out, 0x1234);
    check("16bpp.IDX1234.pos",  dut->pos, 0);

    // 8 bpp (mode=3, k=1): IDX=0x1234 -> word=0x91A, pos=0
    set_idx(0, 0x1234, 0, 3);
    check("8bpp.IDX1234.word", dut->word_out, 0x091A);
    check("8bpp.IDX1234.pos",  dut->pos, 0);

    // 8 bpp: IDX=0x1235 -> word=0x91A, pos=1
    set_idx(0, 0x1235, 0, 3);
    check("8bpp.IDX1235.word", dut->word_out, 0x091A);
    check("8bpp.IDX1235.pos",  dut->pos, 1);

    // 4 bpp (mode=2, k=2): IDX=0x1234 -> word=0x48D, pos=0
    set_idx(0, 0x1234, 0, 2);
    check("4bpp.IDX1234.word", dut->word_out, 0x048D);
    check("4bpp.IDX1234.pos",  dut->pos, 0);

    // 4 bpp: IDX=0x1235 -> word=0x48D, pos=1
    set_idx(0, 0x1235, 0, 2);
    check("4bpp.IDX1235.word", dut->word_out, 0x048D);
    check("4bpp.IDX1235.pos",  dut->pos, 1);

    // 4 bpp: IDX=0x1236 -> word=0x48D, pos=2
    set_idx(0, 0x1236, 0, 2);
    check("4bpp.IDX1236.word", dut->word_out, 0x048D);
    check("4bpp.IDX1236.pos",  dut->pos, 2);

    // 4 bpp: IDX=0x1237 -> word=0x48D, pos=3
    set_idx(0, 0x1237, 0, 2);
    check("4bpp.IDX1237.word", dut->word_out, 0x048D);
    check("4bpp.IDX1237.pos",  dut->pos, 3);

    // 1 bpp (mode=0, k=4): IDX=0x1234 -> word=0x123, pos=4
    set_idx(0, 0x1234, 0, 0);
    check("1bpp.IDX1234.word", dut->word_out, 0x0123);
    check("1bpp.IDX1234.pos",  dut->pos, 4);

    // 1 bpp: IDX=0x123F -> word=0x123, pos=15
    set_idx(0, 0x123F, 0, 0);
    check("1bpp.IDX123F.word", dut->word_out, 0x0123);
    check("1bpp.IDX123F.pos",  dut->pos, 15);

    // IDXH: IDX=0x1234, IDXH=1 -> IDX = 0x11234
    // 16 bpp: word = 0x11234, pos = 0 (но word_out 15 бит -> 0x1234? Нет, 0x11234 > 0x7FFF)
    // Проверим с 1 bpp: IDX=0x1234, IDXH=1 -> IDX = 0x11234, word = 0x1123, pos = 4
    set_idx(0, 0x1234, 1, 0);
    check("IDXH.word", dut->word_out, 0x1123);
    check("IDXH.pos",  dut->pos, 4);

    // IDXH обнуляется после IDX: следующий IDX=0x1234 (без IDXH) -> IDX = 0x1234
    set_idx(0, 0x1234, 0xFF, 0);   // 0xFF = не писать IDXH
    check("IDXH.reset.word", dut->word_out, 0x0123);
    check("IDXH.reset.pos",  dut->pos, 4);

    // STEP: 4 bpp, IDX=0x1234, STEP=1 -> после access IDX=0x1235
    set_idx(0, 0x1234, 0, 2);
    set_step(0, 0x10);
    dut->access = 1; tick(); dut->access = 0;
    check("STEP1.word", dut->word_out, 0x048D);
    check("STEP1.pos",  dut->pos, 1);

    // STEP: 4 bpp, IDX=0x1237, STEP=1 -> после access IDX=0x1238 -> word=0x48E, pos=0
    set_idx(0, 0x1237, 0, 2);
    set_step(0, 0x10);
    dut->access = 1; tick(); dut->access = 0;
    check("STEP2.word", dut->word_out, 0x048E);
    check("STEP2.pos",  dut->pos, 0);

    // STEP: 16 bpp, IDX=0x1234, STEP=8 (8 пикселей = 8 слов) -> IDX=0x123C
    set_idx(0, 0x1234, 0, 4);
    set_step(0, 0x80);
    dut->access = 1; tick(); dut->access = 0;
    check("STEP16.word", dut->word_out, 0x123C);
    check("STEP16.pos",  dut->pos, 0);

    printf("=== pixel_engine tests done: %d errors ===\n", errors);
    delete dut;
    return errors ? 1 : 0;
}