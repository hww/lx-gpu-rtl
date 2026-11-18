#include "Vpixel_engine.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

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

static void set_step(uint8_t ch, int16_t step) {
    dut->ch_i = ch;
    dut->step_we_i = 1; dut->step_i = (uint16_t)step;
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

// common check for one index -> word / frac
static void expect_idx(const char* name, uint8_t ch, uint8_t mode,
                       uint16_t idx, int idxh, int use_idxh,
                       uint16_t exp_word, uint8_t exp_frac) {
    set_mode(ch, mode);
    set_idx(ch, idx, idxh, use_idxh);
    int fails_before = errors;
    check("word_o", dut->word_o, exp_word, 16);
    check("frac_o", dut->frac_o, exp_frac, 4);
    check("mode_o", dut->mode_o, mode, 4);

    printf(CLR_CYN "  %-22s" CLR_RST " ch=%d mode=%d idx=%04X%s -> ",
           name, ch, mode, idx, use_idxh ? "+idxh" : "");
    if (errors == fails_before)
        printf(CLR_GRN "OK" CLR_RST " (word=%04X frac=0x%X)\n",
               dut->word_o, dut->frac_o);
    else
        printf("\n");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vpixel_engine;

    printf(CLR_BLD "pixel_engine testbench (New Architecture: frac_o & integer STEP)" CLR_RST "\n");

    dut->rst = 1;
    dut->mode_we_i = 0; dut->idxh_we_i = 0; dut->idx_we_i = 0;
    dut->step_we_i = 0; dut->access_i = 0; dut->access_done_i = 0;
    tick(); tick();
    dut->rst = 0;
    tick();

    // Коды форматов: 0=16bpp(k=0), 1=8bpp(k=1), 2=4bpp(k=2), 3=2bpp(k=3), 4=1bpp(k=4)

    // ===== 16 bpp (mode=0, k=0) =====
    header("16 bpp (mode=0, k=0)");
    expect_idx("16bpp", 0, 0, 0x1234, 0, 0, 0x1234, 0x0);

    // ===== 8 bpp (mode=1, k=1) =====
    header("8 bpp (mode=1, k=1)");
    expect_idx("8bpp.p0", 0, 1, 0x1234, 0, 0, 0x091A, 0x0);
    expect_idx("8bpp.p1", 0, 1, 0x1235, 0, 0, 0x091A, 0x8);

    // ===== 4 bpp (mode=2, k=2) =====
    header("4 bpp (mode=2, k=2)");
    expect_idx("4bpp.p0", 0, 2, 0x1234, 0, 0, 0x048D, 0x0);
    expect_idx("4bpp.p1", 0, 2, 0x1235, 0, 0, 0x048D, 0x4);
    expect_idx("4bpp.p2", 0, 2, 0x1236, 0, 0, 0x048D, 0x8);
    expect_idx("4bpp.p3", 0, 2, 0x1237, 0, 0, 0x048D, 0xC);

    // ===== 2 bpp (mode=3, k=3) =====
    header("2 bpp (mode=3, k=3)");
    expect_idx("2bpp.p4", 0, 3, 0x1234, 0, 0, 0x0246, 0x8);
    expect_idx("2bpp.p7", 0, 3, 0x123F, 0, 0, 0x0247, 0xE);

    // ===== 1 bpp (mode=4, k=4) =====
    header("1 bpp (mode=4, k=4)");
    expect_idx("1bpp.p4",  0, 4, 0x1234, 0, 0, 0x0123, 0x4);
    expect_idx("1bpp.p15", 0, 4, 0x123F, 0, 0, 0x0123, 0xF);

    // ===== IDXH (upper 3 bits) =====
    header("IDXH (19-bit index)");
    expect_idx("idxh.1bpp", 0, 4, 0x1234, 1, 1, 0x1123, 0x4);

    // IDXH cleared by next IDX write
    set_mode(0, 4);
    set_idx(0, 0x1234, 0, 1);   // idxh=0
    set_idx(0, 0x1234, 0, 0);   // no idxh
    int f_idxh = errors;
    check("idxh.clear.word", dut->word_o, 0x0123, 16);
    check("idxh.clear.frac", dut->frac_o, 0x4, 4);
    printf(CLR_CYN "  %-22s" CLR_RST " -> ", "idxh.clear");
    if (errors == f_idxh) printf(CLR_GRN "OK" CLR_RST " (idxh cleared)\n"); else printf("\n");

    // ===== STEP / auto-increment (STEP задаётся в целых пикселях!) =====
    header("STEP (integer pixels) + auto-increment");

    // 4 bpp, IDX=0x1234, STEP=1 пикс -> next IDX=0x1235 (frac 0x0 -> 0x4)
    set_mode(0, 2);
    set_idx(0, 0x1234, 0, 0);
    set_step(0, 1);
    access_done();
    int f = errors;
    check("step1.word", dut->word_o, 0x048D, 16);
    check("step1.frac", dut->frac_o, 0x4, 4);
    printf(CLR_CYN "  %-22s" CLR_RST " 4bpp idx=1234 step=1 -> ", "step1");
    if (errors == f) printf(CLR_GRN "OK" CLR_RST " (word=048D frac=0x4)\n"); else printf("\n");

    // 4 bpp, IDX=0x1237, STEP=1 пикс -> next word 0x048E, frac 0x0
    set_mode(0, 2);
    set_idx(0, 0x1237, 0, 0);
    set_step(0, 1);
    access_done();
    f = errors;
    check("step2.word", dut->word_o, 0x048E, 16);
    check("step2.frac", dut->frac_o, 0x0, 4);
    printf(CLR_CYN "  %-22s" CLR_RST " 4bpp idx=1237 step=1 -> ", "step2");
    if (errors == f) printf(CLR_GRN "OK" CLR_RST " (word=048E frac=0x0)\n"); else printf("\n");

    // 16 bpp, IDX=0x1234, STEP=8 пикс -> next word 0x123C, frac 0x0
    set_mode(0, 0);
    set_idx(0, 0x1234, 0, 0);
    set_step(0, 8);
    access_done();
    f = errors;
    check("step16.word", dut->word_o, 0x123C, 16);
    check("step16.frac", dut->frac_o, 0x0, 4);
    printf(CLR_CYN "  %-22s" CLR_RST " 16bpp idx=1234 step=8 -> ", "step16");
    if (errors == f) printf(CLR_GRN "OK" CLR_RST " (word=123C frac=0x0)\n"); else printf("\n");

    // negative STEP: 8bpp, IDX=0x1234, STEP=-1 пикс -> IDX=0x1233 (word 0x0919, frac 0x8)
    set_mode(0, 1);
    set_idx(0, 0x1234, 0, 0);
    set_step(0, -1);
    access_done();
    f = errors;
    check("stepneg.word", dut->word_o, 0x0919, 16);
    check("stepneg.frac", dut->frac_o, 0x8, 4);
    printf(CLR_CYN "  %-22s" CLR_RST " 8bpp idx=1234 step=-1 -> ", "stepneg");
    if (errors == f) printf(CLR_GRN "OK" CLR_RST " (word=0919 frac=0x8)\n"); else printf("\n");

    // ===== prefetch =====
    header("prefetch");
    set_mode(0, 0);
    set_idx(0, 0x0100, 0, 0);
    set_step(0, 1);

    int f_pref = errors;
    dut->access_i = 1; tick();
    check_bit("prefetch.before", dut->prefetch_o, 0);

    // По контракту кэша access_i снимается в тот же такт, когда приходит ack (access_done_i)
    dut->access_i = 0; dut->access_done_i = 1; tick();
    dut->access_done_i = 0;
    tick(); // На следующем такте поднимается однотактовый импульс prefetch_o
    check_bit("prefetch.pulse", dut->prefetch_o, 1);

    tick();
    check_bit("prefetch.after", dut->prefetch_o, 0);

    printf(CLR_CYN "  %-22s" CLR_RST " -> ", "prefetch");
    if (errors == f_pref) printf(CLR_GRN "OK" CLR_RST " (pulse on word change)\n"); else printf("\n");



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
