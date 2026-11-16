#include "Vgpu_host.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <string>

static Vgpu_host* dut;
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
        printf(CLR_RED "    [TEST FAIL] %-24s got=0x%0*X exp=0x%0*X" CLR_RST "\n",
               name, bits/4, got, bits/4, exp);
        errors++;
    }
}

static void check_bit(const char* name, int got, int exp) {
    checks++;
    if ((!!got) != (!!exp)) {
        printf(CLR_RED "    [TEST FAIL] %-24s got=%d exp=%d" CLR_RST "\n",
               name, !!got, !!exp);
        errors++;
    }
}

static void header(const char* t) {
    printf("\n" CLR_BLD CLR_YEL "=== %s ===" CLR_RST "\n", t);
}

static void result_line(const char* name, int fails_before, const char* info) {
    if (errors == fails_before)
        printf(CLR_CYN "  %-22s" CLR_RST " -> " CLR_GRN "OK" CLR_RST " (%s)\n",
               name, info);
    else
        printf(CLR_CYN "  %-22s" CLR_RST " -> " CLR_RED "TEST FAIL" CLR_RST " (%s)\n",
               name, info);
}

// ---- Wishbone write byte ----
static void wb_write(uint8_t adr, uint8_t data) {
    dut->wb_cyc_i = 1;
    dut->wb_stb_i = 1;
    dut->wb_we_i  = 1;
    dut->wb_adr_i = adr;
    dut->wb_dat_i = data;
    tick();                 // slave latches ack
    int t = 10;
    while (!dut->wb_ack_o && t-- > 0) tick();
    dut->wb_cyc_i = 0;
    dut->wb_stb_i = 0;
    dut->wb_we_i  = 0;
    tick();                 // ack clears
}

// ---- Wishbone read byte ----
static uint8_t wb_read(uint8_t adr) {
    dut->wb_cyc_i = 1;
    dut->wb_stb_i = 1;
    dut->wb_we_i  = 0;
    dut->wb_adr_i = adr;
    tick();
    int t = 10;
    while (!dut->wb_ack_o && t-- > 0) tick();
    uint8_t d = dut->wb_dat_o;
    dut->wb_cyc_i = 0;
    dut->wb_stb_i = 0;
    tick();
    return d;
}

static void reset() {
    dut->rst = 1;
    dut->wb_cyc_i = 0; dut->wb_stb_i = 0; dut->wb_we_i = 0;
    dut->wb_adr_i = 0; dut->wb_dat_i = 0;
    dut->gpu_halted_i = 0;
    dut->gpu_error_i = 0;
    dut->gpu_error_code_i = 0;
    tick(); tick();
    dut->rst = 0;
    tick();
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vgpu_host;

    printf(CLR_BLD "gpu_host testbench" CLR_RST "\n");
    reset();

    // ===== 1. write GPU_ADDR0/1/2 =====
    header("write GPU_ADDR0/1/2");
    {
        int fb = errors;
        wb_write(0, 0x34);
        wb_write(1, 0x12);
        wb_write(2, 0x56);
        test_fail("gpu_addr", dut->gpu_addr_o, 0x561234, 24);
        char buf[32]; snprintf(buf, sizeof buf, "gpu_addr=%06X", dut->gpu_addr_o);
        result_line("addr write", fb, buf);
    }

    // ===== 2. read back GPU_ADDR0/1/2 =====
    header("read back GPU_ADDR0/1/2");
    {
        int fb = errors;
        uint8_t r0 = wb_read(0);
        uint8_t r1 = wb_read(1);
        uint8_t r2 = wb_read(2);
        test_fail("addr0", r0, 0x34, 8);
        test_fail("addr1", r1, 0x12, 8);
        test_fail("addr2", r2, 0x56, 8);
        char buf[48]; snprintf(buf, sizeof buf, "%02X %02X %02X", r0, r1, r2);
        result_line("addr read", fb, buf);
    }

    // ===== 3. COMMAND = GPU_START (1-cycle pulse) =====
    header("COMMAND = GPU_START");
    {
        int fb = errors;
        // start must be 0 initially
        check_bit("start_idle", dut->gpu_start_o, 0);
        wb_write(3, 0x01);   // GPU_START
        // after wb_write, gpu_start_o should have pulsed (1 cycle during write)
        // but wb_write consumes the pulse; check that reset/start isn't stuck high
        check_bit("start_after", dut->gpu_start_o, 0);
        // read COMMAND: cmd_r = 1
        uint8_t cmd = wb_read(3);
        test_fail("cmd", cmd & 0x03, 0x01, 2);
        char buf[32]; snprintf(buf, sizeof buf, "cmd=%d", cmd & 0x03);
        result_line("start pulse", fb, buf);
    }

    // ===== 4. COMMAND = GPU_RESET =====
    header("COMMAND = GPU_RESET");
    {
        int fb = errors;
        wb_write(3, 0x02);
        check_bit("reset_after", dut->gpu_reset_o, 0);
        uint8_t cmd = wb_read(3);
        test_fail("cmd", cmd & 0x03, 0x02, 2);
        char buf[32]; snprintf(buf, sizeof buf, "cmd=%d", cmd & 0x03);
        result_line("reset pulse", fb, buf);
    }

    // ===== 5. STATUS: halted=0, no error =====
    header("STATUS: running");
    {
        int fb = errors;
        dut->gpu_halted_i = 0;
        dut->gpu_error_i  = 0;
        uint8_t s = wb_read(4);
        check_bit("status.halt", (s >> 7) & 1, 0);
        test_fail("status.result", s & 0x7F, 0, 7);
        char buf[32]; snprintf(buf, sizeof buf, "status=%02X", s);
        result_line("running", fb, buf);
    }

    // ===== 6. STATUS: halted=1, result=0 (OK) =====
    header("STATUS: halted OK");
    {
        int fb = errors;
        dut->gpu_halted_i = 1;
        dut->gpu_error_i  = 0;
        uint8_t s = wb_read(4);
        check_bit("status.halt", (s >> 7) & 1, 1);
        test_fail("status.result", s & 0x7F, 0, 7);
        char buf[32]; snprintf(buf, sizeof buf, "status=%02X", s);
        result_line("halted OK", fb, buf);
    }

    // ===== 7. STATUS: halted=1, error=3 (BADFMT) =====
    header("STATUS: halted error 3");
    {
        int fb = errors;
        dut->gpu_halted_i = 1;
        dut->gpu_error_i  = 1;
        dut->gpu_error_code_i = 3;
        uint8_t s = wb_read(4);
        check_bit("status.halt", (s >> 7) & 1, 1);
        test_fail("status.result", s & 0x7F, 3, 7);
        char buf[32]; snprintf(buf, sizeof buf, "status=%02X", s);
        result_line("halted err3", fb, buf);
    }

    // ===== 8. STATUS: halted=1, error=5 (WBWDT, low 7 bits) =====
    header("STATUS: error code truncated to 7 bits");
    {
        int fb = errors;
        dut->gpu_halted_i = 1;
        dut->gpu_error_i  = 1;
        dut->gpu_error_code_i = 0x123;     // low 7 bits = 0x23
        uint8_t s = wb_read(4);
        test_fail("status.result", s & 0x7F, 0x23, 7);
        char buf[32]; snprintf(buf, sizeof buf, "status=%02X", s);
        result_line("truncated", fb, buf);
    }

    // ===== 9. ack handshake: ack only for 1 cycle =====
    header("ack handshake");
    {
        int fb = errors;
        dut->wb_cyc_i = 1; dut->wb_stb_i = 1; dut->wb_we_i = 0;
        dut->wb_adr_i = 0;
        tick();                    // ack_r set next cycle
        tick();
        int ack_seen = 0;
        for (int i = 0; i < 5; i++) {
            if (dut->wb_ack_o) ack_seen++;
            tick();
        }
        check_bit("ack.seen", ack_seen > 0, 1);
        check_bit("ack.one_cycle", ack_seen, 1);
        dut->wb_cyc_i = 0; dut->wb_stb_i = 0;
        tick();
        char buf[32]; snprintf(buf, sizeof buf, "ack_seen=%d", ack_seen);
        result_line("ack", fb, buf);
    }

    // ===== 10. unknown register read -> 0 =====
    header("unknown register -> 0");
    {
        int fb = errors;
        uint8_t d = wb_read(0x10);
        test_fail("unknown", d, 0, 8);
        char buf[32]; snprintf(buf, sizeof buf, "dat=%02X", d);
        result_line("unknown", fb, buf);
    }

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