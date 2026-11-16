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
        printf(CLR_CYN "  %-18s" CLR_RST " -> " CLR_GRN "OK" CLR_RST " (%s)\n",
               name, info);
    else
        printf(CLR_CYN "  %-18s" CLR_RST " -> " CLR_RED "TEST FAIL" CLR_RST " (%s)\n",
               name, info);
}

static void reset() {
    dut->rst = 1;
    dut->cyc_i = 0; dut->stb_i = 0; dut->we_i = 0;
    dut->adr_i = 0; dut->mode_i = 0; dut->pos_i = 0;
    dut->dat_i = 0; dut->pixel_i = 0; dut->ack_i = 0;
    tick(); tick();
    dut->rst = 0;
    tick();
}

// read: cyc&stb&~we -> dat_o = pixel, ack_o = 1 (combinational)
static void do_read(uint8_t mode, uint8_t pos, uint16_t word,
                    uint16_t exp_pixel, const char* name) {
    int fb = errors;
    dut->cyc_i = 1;
    dut->stb_i = 1;
    dut->we_i  = 0;
    dut->adr_i = 0;      // [0]=0 read, [1]=ch (0)
    dut->mode_i = mode;
    dut->pos_i  = pos;
    dut->dat_i  = word;
    dut->pixel_i = 0;
    dut->eval();            // combinational

    uint16_t got_pixel = dut->dat_o;
    int      got_ack   = dut->ack_o;

    test_fail("dat_o", got_pixel, exp_pixel, 16);
    check_bit("ack_o", got_ack, 1);

    tick();
    dut->cyc_i = 0; dut->stb_i = 0;
    dut->eval();
    check_bit("ack_idle", dut->ack_o, 0);

    char buf[32]; snprintf(buf, sizeof buf, "pixel=%04X", got_pixel);
    result_line(name, fb, buf);
}

// write: RMW, 2 cache accesses via ack_i handshake
static void do_write(uint8_t mode, uint8_t pos, uint16_t word, uint16_t pixel,
                     uint16_t exp_word, const char* name) {
    int fb = errors;

    // start write: cyc&stb&we, adr[0]=1
    dut->cyc_i = 1;
    dut->stb_i = 1;
    dut->we_i  = 1;
    dut->adr_i = 1;      // [0]=1 write, [1]=ch (0)
    dut->mode_i = mode;
    dut->pos_i  = pos;
    dut->dat_i  = 0;        // old word arrives later
    dut->pixel_i = pixel;
    dut->ack_i   = 0;
    tick();                 // IDLE -> RMW_READ
    dut->cyc_i = 0; dut->stb_i = 0; dut->we_i = 0;
    dut->eval();

    check_bit("rmw_req.read",  dut->rmw_req_o, 1);
    check_bit("rmw_we.read",   dut->rmw_we_o, 0);

    // RMW_READ done: old word arrives via dat_i, ack_i pulse
    dut->dat_i = word;
    dut->ack_i = 1;
    tick();                 // RMW_READ -> RMW_WRITE
    dut->ack_i = 0;
    dut->eval();

    check_bit("rmw_req.write", dut->rmw_req_o, 1);
    check_bit("rmw_we.write",  dut->rmw_we_o, 1);
    test_fail("rmw_dat", dut->rmw_dat_o, exp_word, 16);

    // RMW_WRITE done: ack_i pulse -> ack_o
    dut->ack_i = 1;
    tick();
    dut->ack_i = 0;
    dut->eval();

    check_bit("ack.write",  dut->ack_o, 1);
    check_bit("rmw_req.after", dut->rmw_req_o, 0);
    check_bit("rmw_we.after",  dut->rmw_we_o, 0);

    char buf[32]; snprintf(buf, sizeof buf, "word=%04X", exp_word);
    result_line(name, fb, buf);
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vpixel_blender;

    printf(CLR_BLD "pixel_blender testbench (Wishbone slave)" CLR_RST "\n");
    reset();

    // ===== reads (combinational) =====
    header("Read (combinational, ack = cyc&stb&~we)");
    do_read(2, 0, 0xABCD, 0x000A, "4bpp.p0.rd");
    do_read(2, 3, 0xABCD, 0x000D, "4bpp.p3.rd");
    do_read(3, 0, 0xABCD, 0x00AB, "8bpp.p0.rd");
    do_read(0, 0, 0x8000, 0x0001, "1bpp.p0.rd");
    do_read(1, 0, 0xE400, 0x0003, "2bpp.p0.rd");
    do_read(4, 0, 0x1234, 0x1234, "16bpp.rd");
    do_read(5, 0, 0xABCD, 0x00CD, "pass_lo8.rd");
    do_read(6, 0, 0xABCD, 0x00AB, "pass_hi8.rd");
    do_read(7, 0, 0x1234, 0x1234, "pass16.rd");

    // ===== writes (RMW) =====
    header("Write (RMW, 2 cache accesses via ack_i)");
    do_write(2, 0, 0xABCD, 0x0005, 0x5BCD, "4bpp.p0.wr");
    do_write(2, 3, 0xABCD, 0x0005, 0xABC5, "4bpp.p3.wr");
    do_write(3, 0, 0xABCD, 0x0012, 0x12CD, "8bpp.p0.wr");
    do_write(4, 0, 0xABCD, 0x1234, 0x1234, "16bpp.wr");
    do_write(5, 0, 0xABCD, 0x0012, 0xAB12, "pass_lo8.wr");
    do_write(6, 0, 0xABCD, 0x0012, 0x12CD, "pass_hi8.wr");

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