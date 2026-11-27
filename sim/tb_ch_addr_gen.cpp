#include "Vch_addr_gen.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static Vch_addr_gen* dut;
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

// ---------- helpers for banked window writes ----------
//  bank 0 POS : grp 0=XI 1=YI 2=XF 3=YF
//  bank 1 STEP: grp 0=DXI 1=DYI 2=DXF 3=DYF
//  bank 2 CLIP: grp 0=XMIN 1=YMIN 2=XMAX 3=YMAX
//  bank 3 PEN : grp 0=B 1=KEY 2=ALU_CTL
//  bank 4 SURF: grp 0=MODE_PAGE 1=PITCH
static void wr(uint8_t ch, uint8_t bank, uint8_t grp, uint16_t data) {
    dut->ch_i    = ch;
    dut->bank_i  = bank;
    dut->grp_i   = grp;
    dut->wdata_i = data;
    dut->reg_we_i = 1;
    dut->req_i = 0; dut->adv_i = 0; dut->rd_i = 0; dut->pl_we_i = 0;
    tick();
    dut->reg_we_i = 0;
}

static void write_mode_page(uint8_t ch, uint8_t page, uint8_t fmt,
                            int clip, int shrx, int shry) {
    uint16_t v = (uint16_t)page
               | ((uint16_t)(fmt & 7) << 8)
               | (clip ? (1u << 12) : 0)
               | (shrx ? (1u << 13) : 0)
               | (shry ? (1u << 14) : 0);
    wr(ch, 4, 0, v);
}


// Latch last_ch_r = ch without moving the position: one dummy access with
// all STEP registers cleared. Call it before writing XI/YI in a non-zero
// channel, so the combinational outputs refer to that channel.
static void select_ch(uint8_t ch) {
    wr(ch, 1, 0, 0);
    wr(ch, 1, 1, 0);
    wr(ch, 1, 2, 0);
    wr(ch, 1, 3, 0);
    dut->ch_i  = ch;
    dut->req_i = 1;
    dut->adv_i = 1;     // <-- одновременно
    dut->rd_i  = 0;
    dut->reg_we_i = 0; dut->pl_we_i = 0;
    tick();
    dut->req_i = 0; dut->adv_i = 0;
    tick();
}


// linear mode: PITCH=0, XI = idx[15:0], YI[2:0] = idx[18:16]
static void set_linear_idx(uint8_t ch, uint32_t idx19) {
    wr(ch, 4, 1, 0x0000);                 // PITCH = 0 -> linear
    wr(ch, 0, 0, (uint16_t)(idx19 & 0xFFFF));      // XI
    wr(ch, 0, 1, (uint16_t)((idx19 >> 16) & 7));   // YI[2:0]
    wr(ch, 0, 2, 0);                      // XF
    wr(ch, 0, 3, 0);                      // YF
}

// one request that completes in this cycle: adv_i = 1, rd_i = 0/1
static void do_access(uint8_t ch, int we, int rd) {
    dut->ch_i = ch;
    dut->req_i = 1;
    dut->adv_i = 0;
    dut->rd_i  = 0;
    tick();
    // completion: adv (and rd if it was a cache read)
    dut->req_i = 0;
    dut->adv_i = 1;
    dut->rd_i  = rd;
    tick();
    dut->adv_i = 0;
    dut->rd_i  = 0;
}

static void header(const char* t) {
    printf("\n" CLR_BLD CLR_YEL "=== %s ===" CLR_RST "\n", t);
}

// common check for linear-mode index -> word / frac
static void expect_lin(const char* name, uint8_t ch, uint8_t fmt,
                       uint32_t idx19,
                       uint16_t exp_word, uint8_t exp_frac) {
    write_mode_page(ch, 0, fmt, 0, 0, 0);
    set_linear_idx(ch, idx19);
    // let combinational settle
    dut->eval();
    int fails_before = errors;
    check("word_o", dut->word_o, exp_word, 15);
    check("frac_o", dut->frac_o, exp_frac, 4);
    check("mode_o", dut->mode_o, fmt, 3);
    check_bit("ovf_o",  dut->ovf_o,  0);
    check_bit("inside_o (clip off)", dut->inside_o, 1);

    printf(CLR_CYN "  %-22s" CLR_RST " ch=%d fmt=%d idx=%05X -> ",
           name, ch, fmt, idx19);
    if (errors == fails_before)
        printf(CLR_GRN "OK" CLR_RST " (word=%04X frac=0x%X)\n",
               dut->word_o, dut->frac_o);
    else
        printf("\n");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vch_addr_gen;

    printf(CLR_BLD "ch_addr_gen v2 testbench" CLR_RST "\n");

    // reset + idle all inputs
    dut->rst = 1;
    dut->ch_i = 0;
    dut->reg_we_i = 0; dut->bank_i = 0; dut->grp_i = 0; dut->wdata_i = 0;
    dut->pl_we_i = 0; dut->pl_idx_i = 0;
    dut->req_i = 0; dut->adv_i = 0; dut->rd_i = 0;
    tick(); tick();
    dut->rst = 0;
    tick();

    // ===== index -> word/frac, linear mode, no page overflow =====
    header("linear mode: idx -> word/frac (k = fmt)");

    // 16 bpp (fmt=0, k=0): idx 0x1234 -> word=0x1234, frac=0x0
    expect_lin("16bpp.p", 0, 0, 0x1234, 0x1234, 0x0);

    // 8 bpp (fmt=1, k=1): idx 0x1234 -> word=0x091A, frac=0x0
    expect_lin("8bpp.p0", 0, 1, 0x1234, 0x091A, 0x0);
    expect_lin("8bpp.p1", 0, 1, 0x1235, 0x091A, 0x8);

    // 4 bpp (fmt=2, k=2): idx 0x1234..0x1237 -> word=0x048D, frac=0,4,8,C
    expect_lin("4bpp.p0", 0, 2, 0x1234, 0x048D, 0x0);
    expect_lin("4bpp.p1", 0, 2, 0x1235, 0x048D, 0x4);
    expect_lin("4bpp.p2", 0, 2, 0x1236, 0x048D, 0x8);
    expect_lin("4bpp.p3", 0, 2, 0x1237, 0x048D, 0xC);

    // 2 bpp (fmt=3, k=3): idx 0x1234 -> word=0x0246, frac=0x8
    expect_lin("2bpp.p4", 0, 3, 0x1234, 0x0246, 0x8);
    expect_lin("2bpp.p7", 0, 3, 0x123F, 0x0247, 0xE);

    // 1 bpp (fmt=4, k=4): idx 0x1234 -> word=0x0123, frac=0x4
    expect_lin("1bpp.p4",  0, 4, 0x1234, 0x0123, 0x4);
    expect_lin("1bpp.p15", 0, 4, 0x123F, 0x0123, 0xF);


    // ===== linear mode: 19-bit index (idx above 16 bits comes from YI[2:0]) =====
    header("linear mode: 19-bit index");
    // idx = 0x1_1234: word = 0x1123, frac=0x4 (1bpp)
    expect_lin("idx19.1bpp", 0, 4, 0x11234, 0x1123, 0x4);

    // ===== page overflow (index above bit 18) =====
    header("page overflow");
    {
        write_mode_page(0, 0, 0, 0, 0, 0);      // 16 bpp, no clip
        // linear, XI=0xFFFF, YI=7 -> idx = 0x7FFFF, 16bpp: pidx<<4 = 0x7FFFF0
        // >>>0 -> 0x7FFFF0; bits [39:19] = 0xF -> ovf=1
        wr(0, 4, 1, 0x0000);            // PITCH=0
        wr(0, 0, 0, 0xFFFF);            // XI
        wr(0, 0, 1, 7);                 // YI = 7
        dut->eval();
        int f = errors;
        check_bit("ovf_o", dut->ovf_o, 1);
        printf(CLR_CYN "  %-22s" CLR_RST " -> ", "linear ovf");
        if (errors == f) printf(CLR_GRN "OK" CLR_RST "\n"); else printf("\n");
    }

    // ===== 2D mode: PITCH != 0 -> idx = YI*PITCH + XI =====
    header("2D mode: PITCH != 0 (idx = YI*PITCH + XI)");
    {
        select_ch(1);                   // latch last_ch_r = 1
        write_mode_page(1, 0, 0, 0, 0, 0);      // 16bpp on ch1
        wr(1, 4, 1, 320);               // PITCH = 320
        wr(1, 0, 0, 10);                // XI = 10
        wr(1, 0, 1, 2);                 // YI = 2
        wr(1, 0, 2, 0); wr(1, 0, 3, 0);
        dut->eval();
        int f = errors;
        // idx = 2*320 + 10 = 650 = 0x028A; k=0 -> word=0x028A, frac=0
        check("2D.word", dut->word_o, 0x028A, 15);
        check("2D.frac", dut->frac_o, 0, 4);
        printf(CLR_CYN "  %-22s" CLR_RST " idx=2*320+10 -> ", "2D");
        if (errors == f) printf(CLR_GRN "OK" CLR_RST " (word=028A)\n"); else printf("\n");
    }

    // ===== clip window (signed, inclusive) =====
    header("clip window");
    {
        select_ch(2);                   // latch last_ch_r = 2
        write_mode_page(2, 0, 0, /*clip*/1, 0, 0);
        wr(2, 2, 0, 0xFFFF);            // XMIN = -1
        wr(2, 2, 1, 0xFFFF);            // YMIN = -1
        wr(2, 2, 2, 5);                 // XMAX = 5
        wr(2, 2, 3, 5);                 // YMAX = 5
        wr(2, 0, 0, 3); wr(2, 0, 1, 3);
        wr(2, 0, 2, 0); wr(2, 0, 3, 0);
        dut->eval();
        int f = errors;
        check_bit("inside (3,3)", dut->inside_o, 1);
        wr(2, 0, 0, 6);                 // XI = 6 -> outside
        dut->eval();
        check_bit("outside (6,3)", dut->inside_o, 0);
        wr(2, 0, 0, 5);                 // XI = 5 -> inside (inclusive)
        dut->eval();
        check_bit("inside (5,3)", dut->inside_o, 1);
        printf(CLR_CYN "  %-22s" CLR_RST " -> ", "clip");
        if (errors == f) printf(CLR_GRN "OK" CLR_RST "\n"); else printf("\n");
    }

    // ===== advance (adv_i): pos += STEP in linear mode =====
    header("advance (linear mode)");
    {
        // 4 bpp: idx=0x1234, DXI=1, XF=0 -> after adv idx=0x1235, frac=4
        select_ch(3);
        write_mode_page(3, 0, 2, 0, 0, 0);
        wr(3, 4, 1, 0x0000);            // PITCH=0 (linear)
        wr(3, 0, 0, 0x1234);            // XI
        wr(3, 0, 1, 0);                 // YI
        wr(3, 0, 2, 0); wr(3, 0, 3, 0);
        wr(3, 1, 0, 1);                 // DXI=1
        wr(3, 1, 1, 0); wr(3, 1, 2, 0); wr(3, 1, 3, 0);
        // make ach = ch3 by the last reg_we cycle
        do_access(3, /*we*/1, /*rd*/0);
        dut->eval();
        int f = errors;
        check("adv4.word", dut->word_o, 0x048D, 15);
        check("adv4.frac", dut->frac_o, 0x4, 4);
        printf(CLR_CYN "  %-22s" CLR_RST " 4bpp idx=1234 DXI=1 -> ", "adv4");
        if (errors == f) printf(CLR_GRN "OK" CLR_RST " (word=048D frac=4)\n"); else printf("\n");
    }
    {
        // 16 bpp: idx=0x1234, DXI=8 -> next word 0x123C
        select_ch(3);
        write_mode_page(3, 0, 0, 0, 0, 0);
        wr(3, 4, 1, 0x0000);
        wr(3, 0, 0, 0x1234); wr(3, 0, 1, 0);
        wr(3, 0, 2, 0); wr(3, 0, 3, 0);
        wr(3, 1, 0, 8); wr(3, 1, 1, 0); wr(3, 1, 2, 0); wr(3, 1, 3, 0);
        do_access(3, 1, 0);
        dut->eval();
        int f = errors;
        check("adv16.word", dut->word_o, 0x123C, 15);
        check("adv16.frac", dut->frac_o, 0x0, 4);
        printf(CLR_CYN "  %-22s" CLR_RST " 16bpp idx=1234 DXI=8 -> ", "adv16");
        if (errors == f) printf(CLR_GRN "OK" CLR_RST " (word=123C)\n"); else printf("\n");
    }
    {
        // 8 bpp: idx=0x1234, DXI=-1 -> idx=0x1233, word=0x0919, frac=8
        select_ch(3);        
        write_mode_page(3, 0, 1, 0, 0, 0);
        wr(3, 4, 1, 0x0000);
        wr(3, 0, 0, 0x1234); wr(3, 0, 1, 0);
        wr(3, 0, 2, 0); wr(3, 0, 3, 0);
        wr(3, 1, 0, 0xFFFF);            // DXI = -1
        wr(3, 1, 1, 0); wr(3, 1, 2, 0); wr(3, 1, 3, 0);
        do_access(3, 1, 0);
        dut->eval();
        int f = errors;
        check("advneg.word", dut->word_o, 0x0919, 15);
        check("advneg.frac", dut->frac_o, 0x8, 4);
        printf(CLR_CYN "  %-22s" CLR_RST " 8bpp idx=1234 DXI=-1 -> ", "advneg");
        if (errors == f) printf(CLR_GRN "OK" CLR_RST " (word=0919 frac=8)\n"); else printf("\n");
    }

    // ===== host START preload (pl_we_i) =====
    header("host START preload");
    {
        dut->ch_i = 0;
        dut->pl_we_i = 1;
        dut->pl_idx_i = 0x0040;
        dut->reg_we_i = 0; dut->req_i = 0; dut->adv_i = 0; dut->rd_i = 0;
        tick();
        dut->pl_we_i = 0;
        select_ch(0);
        dut->eval();
        int f = errors;
        // preload: 16bpp, linear, XI=pl_idx, DXI=1 -> word = pl_idx
        check("pl.word", dut->word_o, 0x0040, 15);
        check("pl.frac", dut->frac_o, 0x0, 4);
        check("pl.mode", dut->mode_o, 0, 3);
        printf(CLR_CYN "  %-22s" CLR_RST " -> ", "preload");
        if (errors == f) printf(CLR_GRN "OK" CLR_RST "\n"); else printf("\n");
    }

    // ===== prefetch (level, while ach == last_ch and no req) =====
    header("prefetch");
    {        
        select_ch(0);
        write_mode_page(0, 0, 0, 0, 0, 0);
        wr(0, 4, 1, 0x0000);
        wr(0, 0, 0, 0x0100); wr(0, 0, 1, 0);
        wr(0, 0, 2, 0); wr(0, 0, 3, 0);
        wr(0, 1, 0, 1); wr(0, 1, 1, 0); wr(0, 1, 2, 0); wr(0, 1, 3, 0);
        int f = errors;
        // a cache read arms pf (rd_i=1)
        do_access(0, /*we*/0, /*rd*/1);
        tick();                       // pf_cond settles -> prefetch_r <= 1
        dut->eval();
        check_bit("prefetch.pulse", dut->prefetch_o, 1);
        tick();                       // one-shot -> prefetch_r <= 0
        dut->eval();
        check_bit("prefetch.after", dut->prefetch_o, 0);
        printf(CLR_CYN "  %-22s" CLR_RST " -> ", "prefetch");
        if (errors == f) printf(CLR_GRN "OK" CLR_RST "\n"); else printf("\n");
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