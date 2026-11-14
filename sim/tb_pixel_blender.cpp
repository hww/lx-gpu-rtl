#include "Vpixel_blender.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <string>

static Vpixel_blender* dut;
static int errors = 0;

static void check(const char* name, uint16_t got, uint16_t exp) {
    if (got != exp) {
        printf("FAIL %s: got %04X exp %04X\n", name, got, exp);
        errors++;
    }
}

// one read: pixel_out = unpack(word_in)
static void do_read(uint8_t mode, uint8_t pos, uint16_t word,
                    uint16_t exp_pixel, const char* name) {
    dut->read = 1;
    dut->mode = mode;
    dut->pos = pos;
    dut->word_in = word;
    dut->pixel_in = 0;
    dut->eval();
    check((std::string(name) + ".pix").c_str(), dut->pixel_out, exp_pixel);
}

// one write: word_out = blend(word_in, pixel_in)
static void do_write(uint8_t mode, uint8_t pos, uint16_t word, uint16_t pixel,
                     uint16_t exp_word, const char* name) {
    dut->read = 0;
    dut->mode = mode;
    dut->pos = pos;
    dut->word_in = word;
    dut->pixel_in = pixel;
    dut->eval();
    check((std::string(name) + ".word").c_str(), dut->word_out, exp_word);
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vpixel_blender;

    printf("=== pixel_blender tests ===\n");

    // ===== 4 bpp (mode=2) =====
    // word=0xABCD, pos=0 -> high nibble = 0xA
    do_read(2, 0, 0xABCD, 0x000A, "4bpp.p0.read");
    do_read(2, 1, 0xABCD, 0x000B, "4bpp.p1.read");
    do_read(2, 2, 0xABCD, 0x000C, "4bpp.p2.read");
    do_read(2, 3, 0xABCD, 0x000D, "4bpp.p3.read");

    // write pixel=0x5 at pos=0 -> 0x5BCD
    do_write(2, 0, 0xABCD, 0x0005, 0x5BCD, "4bpp.p0.write");
    do_write(2, 1, 0xABCD, 0x0005, 0xA5CD, "4bpp.p1.write");
    do_write(2, 2, 0xABCD, 0x0005, 0xAB5D, "4bpp.p2.write");
    do_write(2, 3, 0xABCD, 0x0005, 0xABC5, "4bpp.p3.write");

    // ===== 8 bpp (mode=3) =====
    // word=0xABCD, pos=0 -> high byte = 0xAB
    do_read(3, 0, 0xABCD, 0x00AB, "8bpp.p0.read");
    do_read(3, 1, 0xABCD, 0x00CD, "8bpp.p1.read");

    // write pixel=0x12 at pos=0 -> 0x12CD
    do_write(3, 0, 0xABCD, 0x0012, 0x12CD, "8bpp.p0.write");
    do_write(3, 1, 0xABCD, 0x0012, 0xAB12, "8bpp.p1.write");

    // ===== 1 bpp (mode=0) =====
    // word=0x8000, pos=0 -> bit 15 = 1
    do_read(0, 0, 0x8000, 0x0001, "1bpp.p0.read");
    do_read(0, 15, 0x0001, 0x0001, "1bpp.p15.read");
    do_read(0, 0, 0x0000, 0x0000, "1bpp.p0.read.zero");

    // 1 bpp: write forbidden -> word_out = word_in
    do_write(0, 0, 0x8000, 0x0000, 0x8000, "1bpp.p0.write_forbidden");
    do_write(0, 15, 0x0001, 0x0000, 0x0001, "1bpp.p15.write_forbidden");

    // ===== 2 bpp (mode=1) =====
    // word=0xE400, pos=0 -> 2 bits at 15:14 = 0b11 = 3
    do_read(1, 0, 0xE400, 0x0003, "2bpp.p0.read");
    do_read(1, 3, 0xE400, 0x0000, "2bpp.p3.read");

    // 2 bpp: write forbidden
    do_write(1, 0, 0xE400, 0x0000, 0xE400, "2bpp.p0.write_forbidden");

    // ===== 16 bpp (mode=4) =====
    do_read(4, 0, 0x1234, 0x1234, "16bpp.read");
    do_write(4, 0, 0xABCD, 0x1234, 0x1234, "16bpp.write");

    // ===== PASS_LO8 (mode=5) =====
    do_read(5, 0, 0xABCD, 0x00CD, "pass_lo8.read");
    do_write(5, 0, 0xABCD, 0x0012, 0xAB12, "pass_lo8.write");

    // ===== PASS_HI8 (mode=6) =====
    do_read(6, 0, 0xABCD, 0x00AB, "pass_hi8.read");
    do_write(6, 0, 0xABCD, 0x0012, 0x12CD, "pass_hi8.write");

    // ===== PASS16 (mode=7) =====
    do_read(7, 0, 0x1234, 0x1234, "pass16.read");
    do_write(7, 0, 0xABCD, 0x1234, 0x1234, "pass16.write");

    printf("=== pixel_blender tests done: %d errors ===\n", errors);
    delete dut;
    return errors ? 1 : 0;
}