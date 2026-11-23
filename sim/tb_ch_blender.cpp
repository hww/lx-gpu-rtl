#include "Vch_blender.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static Vch_blender* dut;
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

static void test_fail(const char* name, uint32_t got, uint32_t exp, int bits) {
    checks++;
    if (got != exp) {
        printf(CLR_RED "    [TEST FAIL] %-22s got=0x%0*X exp=0x%0*X" CLR_RST "\n",
               name, bits/4, got, bits/4, exp);
        errors++;
    }
}

static void check_bit(const char* name, int got, int exp) {
    checks++;
    if ((!!got) != (!!exp)) {
        printf(CLR_RED "    [TEST FAIL] %-22s got=%d exp=%d" CLR_RST "\n",
               name, !!got, !!exp);
        errors++;
    }
}

static void header(const char* t) {
    printf("\n" CLR_BLD CLR_YEL "=== %s ===" CLR_RST "\n", t);
}

static void result_line(const char* name, int fails_before, const char* info) {
    if (errors == fails_before)
        printf(CLR_CYN "  %-20s" CLR_RST " -> " CLR_GRN "OK" CLR_RST " (%s)\n",
               name, info);
    else
        printf(CLR_CYN "  %-20s" CLR_RST " -> " CLR_RED "TEST FAIL" CLR_RST " (%s)\n",
               name, info);
}

static void reset() {
    dut->rst = 1;
    dut->cyc_i = 0; dut->stb_i = 0; dut->we_i = 0;
    dut->mode_i = 0; dut->frac_i = 0;
    dut->dat_i = 0; dut->pixel_i = 0; dut->ack_i = 0;
    tick(); tick();
    dut->rst = 0;
    tick();
}

// read: cyc&stb&~we -> dat_o = pixel (comb), ack_o = ack_i (от кэша)
static void do_read(uint8_t mode, uint8_t frac, uint16_t word,
                    uint16_t exp_pixel, const char* name) {
    int fb = errors;
    dut->cyc_i   = 1;
    dut->stb_i   = 1;
    dut->we_i    = 0;
    dut->mode_i  = mode;
    dut->frac_i  = frac;
    dut->dat_i   = word;
    dut->pixel_i = 0;
    dut->ack_i   = 0;
    dut->eval();

    // Пока кэш не ответил (ack_i=0), ack_o должен быть 0
    check_bit("ack_wait", dut->ack_o, 0);

    // Пришёл ответ от кэша (ack_i=1)
    dut->ack_i = 1;
    dut->eval();

    uint16_t got_pixel = dut->dat_o;
    int      got_ack   = dut->ack_o;

    test_fail("dat_o", got_pixel, exp_pixel, 16);
    check_bit("ack_o", got_ack, 1);

    tick();
    dut->cyc_i = 0; dut->stb_i = 0; dut->ack_i = 0;
    dut->eval();
    check_bit("ack_idle", dut->ack_o, 0);

    char buf[32]; snprintf(buf, sizeof buf, "pixel=%04X", got_pixel);
    result_line(name, fb, buf);
}

// write: one cache access. The blender shifts the pixel into place and gives
// the bit mask; the cache merges it into its word. The test merges it into
// `word` the same way and checks the result.
// Write-protected formats: no cache request, ack after one cycle.
static void do_write(uint8_t mode, uint8_t frac, uint16_t word, uint16_t pixel,
                     uint16_t exp_word, const char* name) {
    int fb = errors;
    bool prot = (mode >= 3);

    dut->cyc_i   = 1;
    dut->stb_i   = 1;
    dut->we_i    = 1;
    dut->mode_i  = mode;
    dut->frac_i  = frac;
    dut->dat_i   = 0;
    dut->pixel_i = pixel;
    dut->ack_i   = 0;
    dut->eval();

    uint16_t merged = (word & ~dut->rmw_msk_o) | (dut->rmw_dat_o & dut->rmw_msk_o);
    if (prot) {
        check_bit("rmw_req.prot", dut->rmw_req_o, 0);
        test_fail("msk.prot", dut->rmw_msk_o, 0, 16);
        check_bit("ack.wait", dut->ack_o, 0);
        tick();
        check_bit("ack.prot", dut->ack_o, 1);
    } else {
        check_bit("rmw_req", dut->rmw_req_o, 1);
        check_bit("rmw_we",  dut->rmw_we_o, 1);
        check_bit("ack.wait", dut->ack_o, 0);
        dut->ack_i = 1;                 // the cache takes it
        dut->eval();
        check_bit("ack.write", dut->ack_o, 1);
    }
    test_fail("merged", merged, exp_word, 16);

    tick();
    dut->cyc_i = 0; dut->stb_i = 0; dut->we_i = 0; dut->ack_i = 0;
    dut->eval();
    check_bit("ack.after",     dut->ack_o, 0);
    check_bit("rmw_req.after", dut->rmw_req_o, 0);

    char buf[32]; snprintf(buf, sizeof buf, "word=%04X", merged);
    result_line(name, fb, buf);
    tick();
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vch_blender;

    printf(CLR_BLD "ch_blender testbench (New Architecture: frac_i & gpu_fmt_pkg)" CLR_RST "\n");
    reset();

    // Коды форматов по gpu_fmt_pkg:
    // 0: 16bpp (k=0) | 1: 8bpp (k=1) | 2: 4bpp (k=2)
    // 3: 2bpp  (k=3) | 4: 1bpp (k=4) | 5..7: резерв (как 16bpp, без записи)
    // Порядок: байты little-endian, внутри байта со старших битов
    // (8bpp p0 = [7:0]; 4bpp p0..p3 = [7:4], [3:0], [15:12], [11:8])

    // ===== reads =====
    header("Read (unpack from frac_i, ack_o = ack_i)");
    do_read(0, 0x0, 0x1234, 0x1234, "16bpp.rd");
    do_read(1, 0x0, 0xABCD, 0x00CD, "8bpp.p0.rd");
    do_read(1, 0x8, 0xABCD, 0x00AB, "8bpp.p1.rd");
    do_read(2, 0x0, 0xABCD, 0x000C, "4bpp.p0.rd");
    do_read(2, 0x4, 0xABCD, 0x000D, "4bpp.p1.rd");
    do_read(2, 0x8, 0xABCD, 0x000A, "4bpp.p2.rd");
    do_read(2, 0xC, 0xABCD, 0x000B, "4bpp.p3.rd");
    do_read(3, 0x0, 0x00E4, 0x0003, "2bpp.p0.rd");
    do_read(3, 0x2, 0x00E4, 0x0002, "2bpp.p1.rd");
    do_read(3, 0x8, 0xE400, 0x0003, "2bpp.p4.rd");
    do_read(4, 0x0, 0x0080, 0x0001, "1bpp.p0.rd");
    do_read(4, 0x1, 0x0080, 0x0000, "1bpp.p1.rd");
    do_read(4, 0x8, 0x8000, 0x0001, "1bpp.p8.rd");
    do_read(5, 0x0, 0xABCD, 0xABCD, "rsv5.rd");

    // ===== writes (RMW) =====
    header("Write (masked, one access; write-protect for 1bpp/2bpp)");
    do_write(0, 0x0, 0xABCD, 0x1234, 0x1234, "16bpp.wr");
    do_write(1, 0x0, 0xABCD, 0x0012, 0xAB12, "8bpp.p0.wr");
    do_write(1, 0x8, 0xABCD, 0x0012, 0x12CD, "8bpp.p1.wr");
    do_write(2, 0x0, 0xABCD, 0x0005, 0xAB5D, "4bpp.p0.wr");
    do_write(2, 0x8, 0xABCD, 0x0005, 0x5BCD, "4bpp.p2.wr");
    do_write(2, 0xC, 0xABCD, 0x0005, 0xA5CD, "4bpp.p3.wr");
    do_write(5, 0x0, 0xABCD, 0x0012, 0xABCD, "rsv5.wr_prot");

    // Проверка запрета записи (wr_en = 0) для 2bpp (mode=3) и 1bpp (mode=4)
    do_write(3, 0x0, 0xE400, 0x0000, 0xE400, "2bpp.wr_prot");
    do_write(4, 0x0, 0x8001, 0x0000, 0x8001, "1bpp.wr_prot");

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
