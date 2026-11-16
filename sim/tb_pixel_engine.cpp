#include "Vpixel_engine.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <string>

static Vpixel_engine* dut;
static int errors = 0;
static int checks = 0;

#define CLR_RST  "\033[0m"
#define CLR_GRN  "\033[32m"
#define CLR_RED  "\033[31m"
#define CLR_YEL  "\033[33m"
#define CLR_CYN  "\033[36m"
#define CLR_BLD  "\033[1m"

static void tick() {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

static void check(const char* name, uint32_t got, uint32_t exp, int bits) {
    checks++;
    if (got != exp) {
        printf(CLR_RED "    [FAIL] %-26s got=0x%0*X exp=0x%0*X" CLR_RST "\n",
               name, bits/4, got, bits/4, exp);
        errors++;
    }
}

static void check_bit(const char* name, int got, int exp) {
    checks++;
    if ((!!got) != (!!exp)) {
        printf(CLR_RED "    [FAIL] %-26s got=%d exp=%d" CLR_RST "\n",
               name, !!got, !!exp);
        errors++;
    }
}

// write MODE for channel ch
static void set_mode(uint8_t ch, uint8_t mode) {
    dut->ch_i = ch;
    dut->mode_we_i = 1; dut->mode_i = mode;
    dut->idxh_we_i = 0; dut->idx_we_i = 0; dut->step_we_i = 0;
    dut->access_i = 0; dut->access_done_i = 0;
    tick();
    dut->mode_we_i = 0;
}

// write IDXH (optional) and IDX for channel ch
static void set_idx(uint8_t ch, uint16_t idx, int idxh, int use_idxh) {
    dut->ch_i = ch;
    if (use_idxh) {
        dut->idxh_we_i = 1; dut->idxh_i = idxh & 7;
        dut->mode_we_i = 0; dut->idx_we_i = 0; dut->step_we_i = 0;
        dut->access_i = 0; dut->access_done_i = 0;
        tick();
        dut->idxh_we_i = 0;
    }
    dut->idx_we_i = 1; dut->idx_i = idx;
    tick();
    dut->idx_we_i = 0;
}

static void set_step(uint8_t ch, uint16_t step) {
    dut->ch_i = ch;
    dut->step_we_i = 1; dut->step_i = step;
    dut->mode_we_i = 0; dut->idx_we_i = 0; dut->idxh_we_i = 0;
    dut->access_i = 0; dut->access_done_i = 0;
    tick();
    dut->step_we_i = 0;
}

// one access: cache says done -> IDX advances
static void access_done() {
    dut->access_i = 1;
    tick();
    dut->access_i = 0;
    dut->access_done_i = 1;
    tick();
    dut->access_done_i = 0;
}

static void header(const char* t) {
    printf("\n" CLR_BLD CLR_YEL "=== %s ===" CLR_RST "\n", t);
}

// common check for one index -> word/pos
static void expect_idx(const char* name, uint8_t ch, uint8_t mode,
                       uint16_t idx, int idxh, int use_idxh,
                       uint16_t exp_word, uint8_t exp_pos) {
    set_mode(ch, mode);
    set_idx(ch, idx, idxh, use_idxh);
    int fails_before = errors;
    check("word_o", dut->word_o, exp_word, 15);
    check("pos_o",  dut->pos_o,  exp_pos, 4);
    check("mode_o", dut->mode_o, mode, 3);

    printf(CLR_CYN "  %-22s" CLR_RST " ch=%d mode=%d idx=%04X%s -> ",
           name, ch, mode, idx, use_idxh ? "+idxh" : "");
    if (errors == fails_before)
        printf(CLR_GRN "OK" CLR_RST " (word=%04X pos=%d)\n",
               dut->word_o, dut->pos_o);
    else
        printf("\n");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vpixel_engine;

    printf(CLR_BLD "pixel_engine testbench" CLR_RST "\n");

    dut->rst = 1;
    dut->mode_we_i = 0; dut->idxh_we_i = 0; dut->idx_we_i = 0;
    dut->step_we_i = 0; dut->access_i = 0; dut->access_done_i = 0;
    tick(); tick();
    dut->rst = 0;
    tick();

    // ===== 16 bpp (mode=4, k=0) =====
    header("16 bpp (mode=4)");
    expect_idx("16bpp", 0, 4, 0x1234, 0, 0, 0x1234, 0);

    // ===== 8 bpp (mode=3, k=1) =====
    header("8 bpp (mode=3)");
    expect_idx("8bpp.p0", 0, 3, 0x1234, 0, 0, 0x091A, 0);
    expect_idx("8bpp.p1", 0, 3, 0x1235, 0, 0, 0x091A, 1);

    // ===== 4 bpp (mode=2, k=2) =====
    header("4 bpp (mode=2)");
    expect_idx("4bpp.p0", 0, 2, 0x1234, 0, 0, 0x048D, 0);
    expect_idx("4bpp.p1", 0, 2, 0x1235, 0, 0, 0x048D, 1);
    expect_idx("4bpp.p2", 0, 2, 0x1236, 0, 0, 0x048D, 2);
    expect_idx("4bpp.p3", 0, 2, 0x1237, 0, 0, 0x048D, 3);

    // ===== 2 bpp (mode=1, k=3) =====
    header("2 bpp (mode=1)");
    expect_idx("2bpp.p0", 0, 1, 0x1234, 0, 0, 0x0246, 4);
    expect_idx("2bpp.p7", 0, 1, 0x123F, 0, 0, 0x0247, 7);

    // ===== 1 bpp (mode=0, k=4) =====
    header("1 bpp (mode=0)");
    expect_idx("1bpp.p4", 0, 0, 0x1234, 0, 0, 0x0123, 4);
    expect_idx("1bpp.p15", 0, 0, 0x123F, 0, 0, 0x0123, 15);

    // ===== IDXH (upper 3 bits) =====
    header("IDXH (19-bit index)");
    expect_idx("idxh.1bpp", 0, 0, 0x1234, 1, 1, 0x1123, 4);
    // IDXH cleared by next IDX write
    set_mode(0, 0);
    set_idx(0, 0x1234, 0, 1);   // idxh=0
    set_idx(0, 0x1234, 0, 0);   // no idxh
    check("idxh.clear.word", dut->word_o, 0x0123, 15);
    check("idxh.clear.pos",  dut->pos_o,  4, 4);
    printf(CLR_CYN "  %-22s" CLR_RST " -> " CLR_GRN "OK" CLR_RST " (idxh cleared)\n",
           "idxh.clear");

    // ===== STEP / auto-increment =====
    header("STEP + auto-increment");

    // 4 bpp, IDX=0x1234, STEP=1.0 (0x10) -> next IDX=0x1235
    set_mode(0, 2);
    set_idx(0, 0x1234, 0, 0);
    set_step(0, 0x10);
    access_done();
    int f = errors;
    check("step1.word", dut->word_o, 0x048D, 15);
    check("step1.pos",  dut->pos_o,  1, 4);
    printf(CLR_CYN "  %-22s" CLR_RST " 4bpp idx=1234 step=1.0 -> ", "step1");
    if (errors == f) printf(CLR_GRN "OK" CLR_RST " (word=048D pos=1)\n"); else printf("\n");

    // 4 bpp, IDX=0x1237, STEP=1.0 -> next word 0x48E, pos 0
    set_mode(0, 2);
    set_idx(0, 0x1237, 0, 0);
    set_step(0, 0x10);
    access_done();
    f = errors;
    check("step2.word", dut->word_o, 0x048E, 15);
    check("step2.pos",  dut->pos_o,  0, 4);
    printf(CLR_CYN "  %-22s" CLR_RST " 4bpp idx=1237 step=1.0 -> ", "step2");
    if (errors == f) printf(CLR_GRN "OK" CLR_RST " (word=048E pos=0)\n"); else printf("\n");

    // 16 bpp, IDX=0x1234, STEP=8.0 (0x80) -> next IDX=0x123C
    set_mode(0, 4);
    set_idx(0, 0x1234, 0, 0);
    set_step(0, 0x80);
    access_done();
    f = errors;
    check("step16.word", dut->word_o, 0x123C, 15);
    check("step16.pos",  dut->pos_o,  0, 4);
    printf(CLR_CYN "  %-22s" CLR_RST " 16bpp idx=1234 step=8.0 -> ", "step16");
    if (errors == f) printf(CLR_GRN "OK" CLR_RST " (word=123C pos=0)\n"); else printf("\n");

    // negative STEP: 8bpp, IDX=0x1234, STEP=-1 (0xFFFF) -> 0x1232 (2 pixels)
    set_mode(0, 3);
    set_idx(0, 0x1234, 0, 0);
    set_step(0, 0xFFFF);
    access_done();
    f = errors;
    check("stepneg.word", dut->word_o, 0x0919, 15);
    check("stepneg.pos",  dut->pos_o,  1, 4);
    printf(CLR_CYN "  %-22s" CLR_RST " 8bpp idx=1234 step=-1 -> ", "stepneg");
    if (errors == f) printf(CLR_GRN "OK" CLR_RST " (word=0919 pos=0)\n"); else printf("\n");

    // ===== prefetch =====
    header("prefetch");
    set_mode(0, 4);
    set_idx(0, 0x0100, 0, 0);
    set_step(0, 0x10);
    // access_done: word 0x100 -> 0x101, prefetch should pulse
    dut->access_i = 1; tick();
    check_bit("prefetch.before", dut->prefetch_o, 0);
    dut->access_done_i = 1; tick();
    dut->access_done_i = 0; dut->access_i = 0;
    check_bit("prefetch.pulse", dut->prefetch_o, 1);
    tick();
    check_bit("prefetch.after", dut->prefetch_o, 0);
    f = errors;
    printf(CLR_CYN "  %-22s" CLR_RST " -> ", "prefetch");
    if (errors == f) printf(CLR_GRN "OK" CLR_RST " (pulse on word change)\n"); else printf("\n");

    // ===== summary =====
    printf("\n" CLR_BLD);
    if (errors == 0)
        printf(CLR_GRN "=== ALL %d CHECKS PASSED ===" CLR_RST "\n", checks);
    else
        printf(CLR_RED "=== %d/%d CHECKS FAILED ===" CLR_RST "\n", errors, checks);
    printf(CLR_RST);

    delete dut;
    return errors ? 1 : 0;
}