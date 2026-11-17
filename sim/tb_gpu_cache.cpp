#include "Vgpu_cache.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

static Vgpu_cache* dut;
static int errors = 0;
static int checks = 0;

#define CLR_RST  "\033[0m"
#define CLR_GRN  "\033[32m"
#define CLR_RED  "\033[31m"
#define CLR_YEL  "\033[33m"
#define CLR_CYN  "\033[36m"
#define CLR_BLD  "\033[1m"

// fake Wishbone memory: 64K words (128 KB), enough for page 0x00..0x01
#define WB_MEM_WORDS  (64 * 1024)
static uint16_t wb_mem[WB_MEM_WORDS];

static void wb_reset_mem() {
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

// TEST FAIL (not cache miss)
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

static void reset() {
    dut->rst = 1;
    dut->ch_i = 0; dut->word_i = 0;
    dut->access_i = 0; dut->prefetch_i = 0;
    dut->we_i = 0; dut->wdata_i = 0;
    dut->page_we_i = 0; dut->page_i = 0;
    dut->flush_en_i = 0; dut->flush_ch_i = 0;
    dut->wb_ack_i = 0; dut->wb_dat_i = 0;
    tick(); tick();
    dut->rst = 0;
    tick();
}

static void set_page(uint8_t ch, uint8_t page) {
    dut->ch_i = ch;
    dut->page_we_i = 1;
    dut->page_i = page;
    tick();
    dut->page_we_i = 0;
}

// read or write; wait for access_done; *hit_out captured before fill
static uint16_t do_access(uint8_t ch, uint16_t word, int we, uint16_t wdata,
                          int* hit_out) {
    dut->ch_i = ch;
    dut->word_i = word;
    dut->we_i = we;
    dut->wdata_i = wdata;
    dut->access_i = 1;
    tick();
    if (hit_out) *hit_out = dut->hit_o;

    int timeout = 100;
    while (!dut->ack_o && timeout-- > 0) tick();

    uint16_t r = dut->rdata_o;
    dut->access_i = 0;
    dut->we_i = 0;
    tick();
    return r;
}

static void do_flush(uint8_t ch) {
    dut->flush_en_i = 1;
    dut->flush_ch_i = ch;
    tick();
    dut->flush_en_i = 0;
    int timeout = 200;
    while (!dut->flush_done_o && timeout-- > 0) tick();
    tick();
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vgpu_cache;

    printf(CLR_BLD "gpu_cache testbench" CLR_RST "\n");
    wb_reset_mem();
    reset();

    // ===== 1. read miss -> fill =====
    header("read miss -> fill");
    {
        int fb = errors;
        set_page(0, 0x00);
        wb_mem[(0x00000 >> 1)] = 0x1234;
        int hit = -1;
        uint16_t r = do_access(0, 0x0000, 0, 0, &hit);
        check_bit("miss.hit", hit, 0);
        test_fail("miss.rdata", r, 0x1234, 16);
        char buf[32]; snprintf(buf, sizeof buf, "rdata=%04X", r);
        result_line("read miss", fb, buf);
    }

    // ===== 2. read hit =====
    header("read hit");
    {
        int fb = errors;
        int hit = -1;
        uint16_t r = do_access(0, 0x0000, 0, 0, &hit);
        check_bit("hit.hit", hit, 1);
        test_fail("hit.rdata", r, 0x1234, 16);
        char buf[32]; snprintf(buf, sizeof buf, "rdata=%04X", r);
        result_line("read hit", fb, buf);
    }

    // ===== 3. different word -> miss =====
    header("word change -> miss");
    {
        int fb = errors;
        wb_mem[(0x00002 >> 1)] = 0xBEEF;
        int hit = -1;
        uint16_t r = do_access(0, 0x0001, 0, 0, &hit);
        check_bit("miss2.hit", hit, 0);
        test_fail("miss2.rdata", r, 0xBEEF, 16);
        char buf[32]; snprintf(buf, sizeof buf, "rdata=%04X", r);
        result_line("word change", fb, buf);
    }

    // ===== 4. write miss -> fill + write (dirty), then flush -> WB =====
    header("write miss -> fill + dirty, flush -> WB");
    {
        int fb = errors;
        dut->ch_i = 0; dut->word_i = 0x0010;
        dut->access_i = 1; dut->we_i = 1; dut->wdata_i = 0xABCD;
        tick();
        int to = 100;
        while (!dut->ack_o && to-- > 0) tick();
        dut->access_i = 0; dut->we_i = 0;
        tick();
        // not yet in WB (still in cache, dirty)
        test_fail("wmiss.wb_before", wb_mem[(0x00020 >> 1)], 0x0000, 16);
        // flush -> WB
        do_flush(0);
        test_fail("wmiss.wb_after", wb_mem[(0x00020 >> 1)], 0xABCD, 16);
        char buf[48]; snprintf(buf, sizeof buf, "wb_before=0000 wb_after=%04X",
                               wb_mem[(0x00020 >> 1)]);
        result_line("write miss", fb, buf);
    }

    // ===== 5. write hit -> dirty =====
    header("write hit -> dirty");
    {
        int fb = errors;
        // fill word 0x20 in cache with 0x0000 first (from WB)
        do_access(0, 0x0010, 0, 0, nullptr);
        // write
        dut->ch_i = 0; dut->word_i = 0x0010;
        dut->access_i = 1; dut->we_i = 1; dut->wdata_i = 0x1111;
        tick();
        int to = 100;
        while (!dut->ack_o && to-- > 0) tick();
        dut->access_i = 0; dut->we_i = 0;
        tick();
        int hit = -1;
        uint16_t r = do_access(0, 0x0010, 0, 0, &hit);
        check_bit("whit.hit", hit, 1);
        test_fail("whit.rdata", r, 0x1111, 16);
        char buf[32]; snprintf(buf, sizeof buf, "rdata=%04X", r);
        result_line("write hit", fb, buf);
    }

    // ===== 6. flush dirty -> writeback =====
    header("flush dirty -> writeback");
    {
        int fb = errors;
        do_flush(0);
        test_fail("flush.wb", wb_mem[(0x00020 >> 1)], 0x1111, 16);
        char buf[32]; snprintf(buf, sizeof buf, "wb=%04X", wb_mem[(0x00020 >> 1)]);
        result_line("flush dirty", fb, buf);
    }

    // ===== 7. flush clean -> no WB =====
    header("flush clean -> no WB");
    {
        int fb = errors;
        wb_mem[(0x10000 >> 1) + 0] = 0x5555;
        set_page(1, 0x01);
        do_access(1, 0x0000, 0, 0, nullptr);
        // change mem: if WB happens, it'll overwrite with old value
        wb_mem[(0x10000 >> 1) + 0] = 0xDEAD;
        do_flush(1);
        test_fail("flush_clean.wb", wb_mem[(0x10000 >> 1) + 0], 0xDEAD, 16);
        char buf[32]; snprintf(buf, sizeof buf, "wb=%04X (unchanged)",
                               wb_mem[(0x10000 >> 1) + 0]);
        result_line("flush clean", fb, buf);
    }

    // ===== 8. page change invalidates =====
    header("page change invalidates");
    {
        int fb = errors;
        set_page(2, 0x00);
        // addr24 for word=0x0001, page=0x00 is 0x000002 -> wb_mem[1]
        wb_mem[(0x00002 >> 1)] = 0xAAAA;
        int hit = -1;
        uint16_t r0 = do_access(2, 0x0001, 0, 0, &hit);
        check_bit("pg0.hit", hit, 0);
        test_fail("pg0.rdata", r0, 0xAAAA, 16);

        set_page(2, 0x01);   // page 0x01 -> addr 0x10000..0x1FFFF
        wb_mem[((0x01 << 16) | 0x0002) >> 1] = 0xBBBB;
        uint16_t r1 = do_access(2, 0x0001, 0, 0, &hit);
        check_bit("pg1.hit", hit, 0);
        test_fail("pg1.rdata", r1, 0xBBBB, 16);
        char buf[48]; snprintf(buf, sizeof buf, "pg0=%04X pg1=%04X", r0, r1);
        result_line("page change", fb, buf);
    }

    // ===== 9. per-channel independence =====
    header("per-channel independence");
    {
        int fb = errors;
        wb_mem[(0x00008 >> 1)] = 0xCAFE;
        wb_mem[(0x0000A >> 1)] = 0xF00D;
        set_page(0, 0x00);
        set_page(3, 0x00);
        uint16_t r0 = do_access(0, 0x0004, 0, 0, nullptr);
        uint16_t r3 = do_access(3, 0x0005, 0, 0, nullptr);
        test_fail("ch0.rdata", r0, 0xCAFE, 16);
        test_fail("ch3.rdata", r3, 0xF00D, 16);
        int hit = -1;
        do_access(0, 0x0004, 0, 0, &hit); check_bit("ch0.hit", hit, 1);
        do_access(3, 0x0005, 0, 0, &hit); check_bit("ch3.hit", hit, 1);
        char buf[48]; snprintf(buf, sizeof buf, "ch0=%04X ch3=%04X", r0, r3);
        result_line("per-channel", fb, buf);
    }

    // ===== 10. addr24 output =====
    header("addr24 output");
    {
        int fb = errors;
        set_page(0, 0x00);
        dut->ch_i = 0; dut->word_i = 0x0123;
        dut->eval();
        test_fail("addr24", dut->addr24_o, (0x00 << 16) | (0x0123 << 1), 24);
        char buf[32]; snprintf(buf, sizeof buf, "addr24=%06X", dut->addr24_o);
        result_line("addr24", fb, buf);
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