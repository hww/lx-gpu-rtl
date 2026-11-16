#include "Vpixel_blender.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <string>

static Vpixel_blender* dut;
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

static void check(const char* name, uint16_t got, uint16_t exp) {
    checks++;
    if (got != exp) {
        printf(CLR_RED "    [FAIL] %-24s got=0x%04X exp=0x%04X" CLR_RST "\n",
               name, got, exp);
        errors++;
    }
}

// read: pixel_o = unpack(word_i)
static void do_read(uint8_t mode, uint8_t pos, uint16_t word,
                    uint16_t exp_pixel, const char* name) {
    dut->is_read_i = 1;
    dut->mode_i = mode;
    dut->pos_i  = pos;
    dut->word_i = word;
    dut->pixel_i = 0;
    dut->start_i = 1;
    tick();                    // ST_IDLE -> ST_COMPUTE (capture)
    dut->start_i = 0;
    tick();                    // ST_COMPUTE -> ST_DONE
    tick();                    // ST_DONE -> ST_IDLE, ack_o was high

    int fails_before = errors;
    check("pixel_o", dut->pixel_o, exp_pixel);

    printf(CLR_CYN "  %-18s" CLR_RST " mode=%d pos=%2d word=%04X -> ",
           name, mode, pos, word);
    if (errors == fails_before)
        printf(CLR_GRN "OK" CLR_RST " (pixel=%04X)\n", dut->pixel_o);
    else
        printf("\n");
}

// write: word_o = blend(word_i, pixel_i)
static void do_write(uint8_t mode, uint8_t pos, uint16_t word, uint16_t pixel,
                     uint16_t exp_word, const char* name) {
    dut->is_read_i = 0;
    dut->mode_i = mode;
    dut->pos_i  = pos;
    dut->word_i = word;
    dut->pixel_i = pixel;
    dut->start_i = 1;
    tick();
    dut->start_i = 0;
    tick();
    tick();

    int fails_before = errors;
    check("word_o", dut->word_o, exp_word);

    printf(CLR_CYN "  %-18s" CLR_RST " mode=%d pos=%2d word=%04X pix=%04X -> ",
           name, mode, pos, word, pixel);
    if (errors == fails_before)
        printf(CLR_GRN "OK" CLR_RST " (word=%04X)\n", dut->word_o);
    else
        printf("\n");
}

static void header(const char* t) {
    printf("\n" CLR_BLD CLR_YEL "=== %s ===" CLR_RST "\n", t);
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vpixel_blender;

    printf(CLR_BLD "pixel_blender testbench" CLR_RST "\n");

    dut->rst = 1; dut->start_i = 0; dut->is_read_i = 0;
    dut->mode_i = 0; dut->pos_i = 0; dut->word_i = 0; dut->pixel_i = 0;
    tick(); tick();
    dut->rst = 0; tick();

    // ===== 4 bpp =====
    header("4 bpp (mode=2)");
    do_read(2, 0, 0xABCD, 0x000A, "4bpp.p0.rd");
    do_read(2, 1, 0xABCD, 0x000B, "4bpp.p1.rd");
    do_read(2, 2, 0xABCD, 0x000C, "4bpp.p2.rd");
    do_read(2, 3, 0xABCD, 0x000D, "4bpp.p3.rd");
    do_write(2, 0, 0xABCD, 0x0005, 0x5BCD, "4bpp.p0.wr");
    do_write(2, 1, 0xABCD, 0x0005, 0xA5CD, "4bpp.p1.wr");
    do_write(2, 2, 0xABCD, 0x0005, 0xAB5D, "4bpp.p2.wr");
    do_write(2, 3, 0xABCD, 0x0005, 0xABC5, "4bpp.p3.wr");

    // ===== 8 bpp =====
    header("8 bpp (mode=3)");
    do_read(3, 0, 0xABCD, 0x00AB, "8bpp.p0.rd");
    do_read(3, 1, 0xABCD, 0x00CD, "8bpp.p1.rd");
    do_write(3, 0, 0xABCD, 0x0012, 0x12CD, "8bpp.p0.wr");
    do_write(3, 1, 0xABCD, 0x0012, 0xAB12, "8bpp.p1.wr");

    // ===== 1 bpp =====
    header("1 bpp (mode=0)");
    do_read(0, 0, 0x8000, 0x0001, "1bpp.p0.rd");
    do_read(0, 15, 0x0001, 0x0001, "1bpp.p15.rd");
    do_read(0, 0, 0x0000, 0x0000, "1bpp.zero.rd");
    // 1bpp write: pixel=1 at pos=0 -> word=0x8000
    do_write(0, 0, 0x0000, 0x0001, 0x8000, "1bpp.p0.wr");
    // pixel=1 at pos=15 -> word=0x0001
    do_write(0, 15, 0x0000, 0x0001, 0x0001, "1bpp.p15.wr");
    // pixel=0 clears bit
    do_write(0, 0, 0x8000, 0x0000, 0x0000, "1bpp.p0.clr");

    // ===== 2 bpp =====
    header("2 bpp (mode=1)");
    do_read(1, 0, 0xE400, 0x0003, "2bpp.p0.rd");
    do_read(1, 3, 0xE400, 0x0000, "2bpp.p3.rd");
    // 2bpp write: pixel=1 at pos=0 -> bits 15:14 = 01 -> 0x4000
    do_write(1, 0, 0x0000, 0x0001, 0x4000, "2bpp.p0.wr");
    // pixel=3 at pos=0 -> 0xC000
    do_write(1, 0, 0x0000, 0x0003, 0xC000, "2bpp.p0.wr3");

    // ===== 16 bpp =====
    header("16 bpp (mode=4)");
    do_read(4, 0, 0x1234, 0x1234, "16bpp.rd");
    do_write(4, 0, 0xABCD, 0x1234, 0x1234, "16bpp.wr");

    // ===== PASS_LO8 / HI8 / 16 =====
    header("PASS modes");
    do_read(5, 0, 0xABCD, 0x00CD, "pass_lo8.rd");
    do_write(5, 0, 0xABCD, 0x0012, 0xAB12, "pass_lo8.wr");
    do_read(6, 0, 0xABCD, 0x00AB, "pass_hi8.rd");
    do_write(6, 0, 0xABCD, 0x0012, 0x12CD, "pass_hi8.wr");
    do_read(7, 0, 0x1234, 0x1234, "pass16.rd");
    do_write(7, 0, 0xABCD, 0x1234, 0x1234, "pass16.wr");

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