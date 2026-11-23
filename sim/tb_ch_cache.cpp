#include "Vch_cache.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <cstring>

static Vch_cache* dut;
static int errors = 0;
static int checks = 0;

#define CLR_RST  "\033[0m"
#define CLR_GRN  "\033[32m"
#define CLR_RED  "\033[31m"
#define CLR_YEL  "\033[33m"
#define CLR_CYN  "\033[36m"
#define CLR_BLD  "\033[1m"

// fake Wishbone memory: 64K words (128 KB), enough for page 0x00..0x01
#define WB_MEM_WORDS  (512 * 1024)   // pages 00..07
static uint16_t wb_mem[WB_MEM_WORDS];

static void wb_reset_mem() {
    for (int i = 0; i < WB_MEM_WORDS; i++) wb_mem[i] = 0;
}

static int wb_rd_cnt = 0, wb_wr_cnt = 0, wb_last_sel = 0;

static void tick() {
    dut->wb_ack_i = 0;
    dut->wb_dat_i = 0;
    if (dut->wb_cyc_o && dut->wb_stb_o) {
        uint32_t a = (dut->wb_adr_o >> 1) & (WB_MEM_WORDS - 1);
        if (dut->wb_we_o) { wb_wr_cnt++; wb_last_sel = dut->wb_sel_o; } else wb_rd_cnt++;
        if (dut->wb_we_o) {
            wb_mem[a] = (wb_mem[a] & ~((dut->wb_sel_o & 1 ? 0x00FF : 0) | (dut->wb_sel_o & 2 ? 0xFF00 : 0))) | (dut->wb_dat_o & ((dut->wb_sel_o & 1 ? 0x00FF : 0) | (dut->wb_sel_o & 2 ? 0xFF00 : 0)));
        } else {
            dut->wb_dat_i = wb_mem[a];
        }
        dut->wb_ack_i = 1;
    }
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

static void reset() {
    dut->rst = 1;
    dut->ch_sel_i = 0; dut->ch_addr_i = 0;
    dut->ch_stb_i = 0; dut->ch_prefetch_i = 0;
    dut->ch_we_i = 0;  dut->ch_dat_i = 0; dut->ch_msk_i = 0;
    dut->page_we_i = 0; dut->page_i = 0;
    dut->flush_en_i = 0; dut->flush_sel_i = 0;
    dut->wb_ack_i = 0; dut->wb_dat_i = 0;
    tick(); tick();
    dut->rst = 0;
    tick();
}

static void set_page(uint8_t ch, uint8_t page) {
    dut->ch_sel_i  = ch;
    dut->page_we_i = 1;
    dut->page_i    = page;
    tick();
    dut->page_we_i = 0;
}

// read or write; wait for ack_o; *hit_out captured before fill
static uint16_t do_access(uint8_t ch, uint16_t word, int we, uint16_t wdata,
                          int* hit_out, uint16_t msk = 0xFFFF) {
    dut->ch_msk_i  = msk;
    dut->ch_sel_i  = ch;
    dut->ch_addr_i = word;
    dut->ch_we_i   = we;
    dut->ch_dat_i  = wdata;
    dut->ch_stb_i  = 1;
    tick();
    if (hit_out) *hit_out = dut->ch_hit_o;

    int timeout = 100;
    while (!dut->ack_o && timeout-- > 0) tick();

    uint16_t r = dut->ch_dat_o;
    dut->ch_stb_i = 0;
    dut->ch_we_i  = 0;
    tick();
    return r;
}

static void do_flush(uint8_t ch) {
    dut->flush_en_i  = 1;
    dut->flush_sel_i = ch;
    tick();
    dut->flush_en_i = 0;
    int timeout = 200;
    while (!dut->flush_ack_o && timeout-- > 0) tick();
    tick();
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vch_cache;

    printf(CLR_BLD "ch_cache testbench (Wishbone-style ch_* ports)" CLR_RST "\n");
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
    header("write miss -> allocate + dirty, flush -> WB");
    {
        int fb = errors;
        dut->ch_sel_i  = 0;
        dut->ch_addr_i = 0x0010;
        dut->ch_stb_i  = 1;
        dut->ch_we_i   = 1;
        dut->ch_dat_i  = 0xABCD;
        dut->ch_msk_i  = 0xFFFF;
        tick();
        int to = 100;
        while (!dut->ack_o && to-- > 0) tick();
        dut->ch_stb_i = 0;
        dut->ch_we_i  = 0;
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
        // fill word 0x10 (byte addr 0x20) in cache first
        do_access(0, 0x0010, 0, 0, nullptr);
        // write hit
        dut->ch_sel_i  = 0;
        dut->ch_addr_i = 0x0010;
        dut->ch_stb_i  = 1;
        dut->ch_we_i   = 1;
        dut->ch_dat_i  = 0x1111;
        dut->ch_msk_i  = 0xFFFF;
        tick();
        int to = 100;
        while (!dut->ack_o && to-- > 0) tick();
        dut->ch_stb_i = 0;
        dut->ch_we_i  = 0;
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
        wb_mem[(0x00002 >> 1)] = 0xAAAA;
        int hit = -1;
        uint16_t r0 = do_access(2, 0x0001, 0, 0, &hit);
        check_bit("pg0.hit", hit, 0);
        test_fail("pg0.rdata", r0, 0xAAAA, 16);

        set_page(2, 0x01);
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
        wb_mem[(0x00040 >> 1)] = 0x3030;
        wb_mem[(0x00042 >> 1)] = 0x3131;
        set_page(2, 0x00);
        set_page(3, 0x00);

        do_access(2, 0x0020, 0, 0, nullptr);
        do_access(3, 0x0021, 0, 0, nullptr);

        int h2 = -1, h3 = -1;
        uint16_t r2 = do_access(2, 0x0020, 0, 0, &h2);
        uint16_t r3 = do_access(3, 0x0021, 0, 0, &h3);

        check_bit("ch2.hit", h2, 1);
        check_bit("ch3.hit", h3, 1);
        test_fail("ch2.rdata", r2, 0x3030, 16);
        test_fail("ch3.rdata", r3, 0x3131, 16);

        char buf[48]; snprintf(buf, sizeof buf, "ch2=%04X ch3=%04X", r2, r3);
        result_line("per-channel", fb, buf);
    }


    // ===== 10. write miss reads nothing (16 bpp) =====
    header("write miss: no read, flush writes the word");
    {
        int fb = errors;
        set_page(0, 0x02);
        wb_mem[(0x20100 >> 1)] = 0xDEAD;
        wb_rd_cnt = wb_wr_cnt = 0;
        do_access(0, 0x0080, 1, 0x4321, nullptr);
        do_flush(0);
        test_fail("nr.reads", wb_rd_cnt, 0, 8);
        test_fail("nr.writes", wb_wr_cnt, 1, 8);
        test_fail("nr.sel", wb_last_sel, 3, 8);
        test_fail("nr.mem", wb_mem[(0x20100 >> 1)], 0x4321, 16);
        result_line("16bpp no read", fb, "0 reads, 1 write");
    }

    // ===== 11. 8 bpp byte write: DQM, no read =====
    header("8 bpp byte: byte mask, no read");
    {
        int fb = errors;
        wb_mem[(0x20102 >> 1)] = 0xABCD;
        wb_rd_cnt = wb_wr_cnt = 0;
        do_access(0, 0x0081, 1, 0x0012, nullptr, 0x00FF);
        do_flush(0);
        test_fail("b8.reads", wb_rd_cnt, 0, 8);
        test_fail("b8.sel", wb_last_sel, 1, 8);
        test_fail("b8.mem", wb_mem[(0x20102 >> 1)], 0xAB12, 16);
        char buf[48]; snprintf(buf, sizeof buf, "mem=%04X sel=%d", wb_mem[(0x20102 >> 1)], wb_last_sel);
        result_line("8bpp byte", fb, buf);
    }

    // ===== 12. 8 bpp two bytes -> full word =====
    header("8 bpp two bytes: full word");
    {
        int fb = errors;
        wb_mem[(0x20104 >> 1)] = 0xABCD;
        wb_rd_cnt = wb_wr_cnt = 0;
        do_access(0, 0x0082, 1, 0x0034, nullptr, 0x00FF);
        do_access(0, 0x0082, 1, 0x5600, nullptr, 0xFF00);
        int hit = -1;
        uint16_t r = do_access(0, 0x0082, 0, 0, &hit);
        check_bit("b88.rdhit", hit, 1);
        test_fail("b88.rd", r, 0x5634, 16);
        do_flush(0);
        test_fail("b88.reads", wb_rd_cnt, 0, 8);
        test_fail("b88.sel", wb_last_sel, 3, 8);
        test_fail("b88.mem", wb_mem[(0x20104 >> 1)], 0x5634, 16);
        result_line("8bpp full", fb, "0 reads");
    }

    // ===== 13. 4 bpp half byte -> lazy fill at write-back =====
    header("4 bpp nibble: lazy fill before write-back");
    {
        int fb = errors;
        wb_mem[(0x20106 >> 1)] = 0xABCD;
        wb_rd_cnt = wb_wr_cnt = 0;
        do_access(0, 0x0083, 1, 0x0050, nullptr, 0x00F0);
        do_flush(0);
        test_fail("n4.reads", wb_rd_cnt, 1, 8);
        test_fail("n4.mem", wb_mem[(0x20106 >> 1)], 0xAB5D, 16);
        char buf[48]; snprintf(buf, sizeof buf, "mem=%04X", wb_mem[(0x20106 >> 1)]);
        result_line("4bpp nibble", fb, buf);
    }

    // ===== 14. 4 bpp whole byte: no read =====
    header("4 bpp two nibbles of a byte: no read");
    {
        int fb = errors;
        wb_mem[(0x20108 >> 1)] = 0xABCD;
        wb_rd_cnt = wb_wr_cnt = 0;
        do_access(0, 0x0084, 1, 0x0070, nullptr, 0x00F0);
        do_access(0, 0x0084, 1, 0x0008, nullptr, 0x000F);
        do_flush(0);
        test_fail("n44.reads", wb_rd_cnt, 0, 8);
        test_fail("n44.sel", wb_last_sel, 1, 8);
        test_fail("n44.mem", wb_mem[(0x20108 >> 1)], 0xAB78, 16);
        result_line("4bpp byte", fb, "0 reads");
    }

    // ===== 15. read of a partly written word: merge fill =====
    header("read of a partial entry: merge fill");
    {
        int fb = errors;
        wb_mem[(0x2010A >> 1)] = 0x1122;
        do_access(0, 0x0085, 1, 0x3400, nullptr, 0xFF00);
        uint16_t r = do_access(0, 0x0085, 0, 0, nullptr);
        test_fail("mf.rd", r, 0x3422, 16);
        do_flush(0);
        test_fail("mf.mem", wb_mem[(0x2010A >> 1)], 0x3422, 16);
        char buf[48]; snprintf(buf, sizeof buf, "rd=%04X", r);
        result_line("merge fill", fb, buf);
    }

    // ===== 16. posted write-back, then read it from another channel =====
    header("posted write-back, read-after-write from another channel");
    {
        int fb = errors;
        set_page(1, 0x02);
        wb_mem[(0x20200 >> 1)] = 0x0000;
        wb_mem[(0x20202 >> 1)] = 0x0000;
        do_access(0, 0x0100, 1, 0xCAFE, nullptr);     // allocate, dirty
        do_access(0, 0x0101, 1, 0xF00D, nullptr);     // miss on dirty: post 0x100
        uint16_t r = do_access(1, 0x0100, 0, 0, nullptr);
        test_fail("post.rd_other", r, 0xCAFE, 16);
        do_flush(0);
        test_fail("post.mem0", wb_mem[(0x20200 >> 1)], 0xCAFE, 16);
        test_fail("post.mem1", wb_mem[(0x20202 >> 1)], 0xF00D, 16);
        result_line("posted write", fb, "ordered");
    }

    // ===== 17. page A -> B -> A with no access keeps A =====
    header("page A -> B -> A without access");
    {
        int fb = errors;
        wb_mem[(0x30000 >> 1)] = 0x0A0A;
        wb_mem[(0x40000 >> 1)] = 0x0B0B;
        set_page(3, 0x03);
        do_access(3, 0x0000, 0, 0, nullptr);
        set_page(3, 0x04);
        set_page(3, 0x03);
        dut->ch_sel_i = 3;
        tick();
        test_fail("aba.page", dut->page_o, 0x03, 8);
        uint16_t r = do_access(3, 0x0000, 0, 0, nullptr);
        test_fail("aba.rd", r, 0x0A0A, 16);
        result_line("A->B->A", fb, "stays on A");
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
