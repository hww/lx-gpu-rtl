#include "Vch_pixel_alu.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static Vch_pixel_alu* dut;
static int errors = 0;
static int checks = 0;

#define CLR_RST  "\033[0m"
#define CLR_GRN  "\033[32m"
#define CLR_RED  "\033[31m"
#define CLR_YEL  "\033[33m"
#define CLR_CYN  "\033[36m"
#define CLR_BLD  "\033[1m"

static void check(const char* name, uint32_t got, uint32_t exp, int bits) {
    checks++;
    if (got != exp) {
        printf(CLR_RED "    [FAIL] %-24s got=0x%0*X exp=0x%0*X" CLR_RST "\n",
               name, bits/4, got, bits/4, exp);
        errors++;
    }
}

static void check_bit(const char* name, int got, int exp) {
    checks++;
    if ((!!got) != (!!exp)) {
        printf(CLR_RED "    [FAIL] %-24s got=%d exp=%d" CLR_RST "\n",
               name, !!got, !!exp);
        errors++;
    }
}

static void header(const char* t) {
    printf("\n" CLR_BLD CLR_YEL "=== %s ===" CLR_RST "\n", t);
}

// One evaluation of the purely combinational ALU. Inputs are set, eval(),
// then the outputs are read. shr_x/shr_y/par are always given too.
struct Out {
    uint16_t r;
    int      rel_ok;
    int      den_ok;
    int      need_d;
};

static Out run(uint16_t a, uint16_t b, uint16_t d, uint16_t k,
               uint16_t ctl, uint16_t mask,
               int shr_x = 0, int shr_y = 0, int par = 0)
{
    dut->a_i     = a;
    dut->b_i     = b;
    dut->d_i     = d;
    dut->k_i     = k;
    dut->ctl_i   = ctl;
    dut->mask_i  = mask;
    dut->shr_x_i = shr_x;
    dut->shr_y_i = shr_y;
    dut->par_i   = par;
    dut->eval();
    Out o;
    o.r      = dut->r_o;
    o.rel_ok = dut->rel_ok_o;
    o.den_ok = dut->den_ok_o;
    o.need_d = dut->need_d_o;
    return o;
}

// ctl helpers: OP[3:0] SX[5:4] SY[7:6] CS[9:8] REL[12:10] DEN[13] DPAR[14]
static uint16_t ctl(uint8_t op, uint8_t sx, uint8_t sy,
                    uint8_t cs, uint8_t rel, int den, int dpar)
{
    return (uint16_t)op
         | ((uint16_t)(sx  & 3) << 4)
         | ((uint16_t)(sy  & 3) << 6)
         | ((uint16_t)(cs  & 3) << 8)
         | ((uint16_t)(rel & 7) << 10)
         | (den  ? (1u << 13) : 0)
         | (dpar ? (1u << 14) : 0);
}

#define MASK16 0xFFFFu
#define MASK8  0x00FFu
#define MASK4  0x000Fu
#define MASK2  0x0003u
#define MASK1  0x0001u

// sx/sy codes: 0=A 1=B 2=D 3=K
// cs  codes: 0=A?K 1=D?K 2=A?D 3=D?B
// rel codes: 0 always, 1 ==, 2 !=, 3 <, 4 >=, 5 >, 6 <=, 7 never

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vch_pixel_alu;

    printf(CLR_BLD "ch_pixel_alu testbench" CLR_RST "\n");

    // ============================================================
    header("OP: PASS / AND / OR / XOR / ANDN (A op B, mask 16)");
    {
        Out o;
        o = run(0x1234, 0x00F0, 0, 0, ctl(0,0,1,0,0,0,0), MASK16);
        check("PASS.a",   o.r, 0x1234, 16);
        o = run(0x1234, 0x00F0, 0, 0, ctl(1,0,1,0,0,0,0), MASK16);
        check("AND",      o.r, 0x0030, 16);
        o = run(0x1234, 0x00F0, 0, 0, ctl(2,0,1,0,0,0,0), MASK16);
        check("OR",       o.r, 0x12F4, 16);
        o = run(0x1234, 0x00F0, 0, 0, ctl(3,0,1,0,0,0,0), MASK16);
        check("XOR",      o.r, 0x12C4, 16);
        o = run(0x1234, 0x00F0, 0, 0, ctl(4,0,1,0,0,0,0), MASK16);
        check("ANDN",     o.r, 0x1204, 16);
    }

    // ============================================================
    header("OP: ADD / ADDS (saturate) / SUBS (clamp 0) / MSK");
    {
        Out o;
        // ADD mod 2^W: 0x00F0 + 0x0020 = 0x0110
        o = run(0x00F0, 0x0020, 0, 0, ctl(5,0,1,0,0,0,0), MASK16);
        check("ADD",      o.r, 0x0110, 16);
        // ADD wrap at 8 bpp: 0xF0 + 0x20 = 0x110 & 0xFF = 0x10
        o = run(0x00F0, 0x0020, 0, 0, ctl(5,0,1,0,0,0,0), MASK8);
        check("ADD.wrap8", o.r, 0x0010, 8);
        // ADDS: saturate to mask on overflow
        o = run(0x00F0, 0x0020, 0, 0, ctl(6,0,1,0,0,0,0), MASK8);
        check("ADDS.sat",  o.r, 0x00FF, 8);
        // ADDS no overflow
        o = run(0x0010, 0x0020, 0, 0, ctl(6,0,1,0,0,0,0), MASK8);
        check("ADDS.ok",   o.r, 0x0030, 8);
        // SUBS: 0x10 - 0x20 underflows -> 0
        o = run(0x0010, 0x0020, 0, 0, ctl(7,0,1,0,0,0,0), MASK8);
        check("SUBS.clamp", o.r, 0x0000, 8);
        // SUBS no underflow
        o = run(0x0030, 0x0020, 0, 0, ctl(7,0,1,0,0,0,0), MASK8);
        check("SUBS.ok",   o.r, 0x0010, 8);
        // MSK: (X & B) | (Y & ~B), X = SX, Y = SY
        // SX=A, SY=D, A=0x1234, D=0xABCD, B=0x00FF
        //  = (0x1234 & 0x00FF) | (0xABCD & 0xFF00) = 0x0034 | 0xAB00 = 0xAB34
        o = run(0x1234, 0x00FF, 0xABCD, 0, ctl(8,0,2,0,0,0,0), MASK16);
        check("MSK",      o.r, 0xAB34, 16);
    }

    // ============================================================
    header("SX/SY select: A B D K");
    {
        Out o;
        // PASS with SX = each source
        o = run(0x1111, 0x2222, 0x3333, 0x4444, ctl(0,0,0,0,0,0,0), MASK16);
        check("SX=A", o.r, 0x1111, 16);
        o = run(0x1111, 0x2222, 0x3333, 0x4444, ctl(0,1,0,0,0,0,0), MASK16);
        check("SX=B", o.r, 0x2222, 16);
        o = run(0x1111, 0x2222, 0x3333, 0x4444, ctl(0,2,0,0,0,0,0), MASK16);
        check("SX=D", o.r, 0x3333, 16);
        o = run(0x1111, 0x2222, 0x3333, 0x4444, ctl(0,3,0,0,0,0,0), MASK16);
        check("SX=K", o.r, 0x4444, 16);
    }

    // ============================================================
    header("SHR_X / SHR_Y (logical >> 1 on the selected source)");
    {
        Out o;
        o = run(0x1234, 0, 0, 0, ctl(0,0,0,0,0,0,0), MASK16, /*shr_x*/1, 0);
        check("SHR_X", o.r, 0x091A, 16);
        o = run(0, 0x00F0, 0, 0, ctl(0,1,1,0,0,0,0), MASK16, 0, /*shr_y*/1);
        // X = B (no shr_x) = 0x00F0, Y = B >> 1 = 0x0078 -> PASS returns X
        check("SHR_Y", o.r, 0x00F0, 16);
    }

    // ============================================================
    header("REL: all 8 conditions on (A, K)");
    {
        // cs=0 -> (A, K)
        const uint16_t A = 0x0010, K = 0x0020;
        Out o;
        o = run(A,0,0,K, ctl(0,0,0,0,0,0,0), MASK16);
        check_bit("AL",   o.rel_ok, 1);
        o = run(A,0,0,K, ctl(0,0,0,0,1,0,0), MASK16);
        check_bit("EQ",   o.rel_ok, 0);
        o = run(A,0,0,K, ctl(0,0,0,0,2,0,0), MASK16);
        check_bit("NE",   o.rel_ok, 1);
        o = run(A,0,0,K, ctl(0,0,0,0,3,0,0), MASK16);
        check_bit("LT",   o.rel_ok, 1);
        o = run(A,0,0,K, ctl(0,0,0,0,4,0,0), MASK16);
        check_bit("GE",   o.rel_ok, 0);
        o = run(A,0,0,K, ctl(0,0,0,0,5,0,0), MASK16);
        check_bit("GT",   o.rel_ok, 0);
        o = run(A,0,0,K, ctl(0,0,0,0,6,0,0), MASK16);
        check_bit("LE",   o.rel_ok, 1);
        o = run(A,0,0,K, ctl(0,0,0,0,7,0,0), MASK16);
        check_bit("NEVER", o.rel_ok, 0);
        // equality case
        o = run(K,0,0,K, ctl(0,0,0,0,1,0,0), MASK16);
        check_bit("EQ.hit", o.rel_ok, 1);
        o = run(K,0,0,K, ctl(0,0,0,0,5,0,0), MASK16);
        check_bit("GT.equal", o.rel_ok, 0);
        o = run(K,0,0,K, ctl(0,0,0,0,6,0,0), MASK16);
        check_bit("LE.equal", o.rel_ok, 1);
    }

    // ============================================================
    header("REL CS: 1=(D,K) 2=(A,D) 3=(D,B)");
    {
        Out o;
        // cs=1, D=5, K=7, LT -> 1
        o = run(0,0,5,7, ctl(0,0,0,1,3,0,0), MASK16);
        check_bit("CS1.D<K", o.rel_ok, 1);
        // cs=2, A=5, D=9, LT -> 1
        o = run(5,0,9,0, ctl(0,0,0,2,3,0,0), MASK16);
        check_bit("CS2.A<D", o.rel_ok, 1);
        // cs=3, D=9, B=5, LT -> 0
        o = run(0,5,9,0, ctl(0,0,0,3,3,0,0), MASK16);
        check_bit("CS3.D<B", o.rel_ok, 0);
    }

    // ============================================================
    header("DEN (screen door): ~DEN | (par == dpar)");
    {
        Out o;
        // DEN off -> always ok
        o = run(0,0,0,0, ctl(0,0,0,0,0,0,0), MASK16);
        check_bit("DEN.off", o.den_ok, 1);
        // DEN on, par==dpar -> ok
        o = run(0,0,0,0, ctl(0,0,0,0,0,1,0), MASK16, 0,0, /*par*/0);
        check_bit("DEN.match", o.den_ok, 1);
        // DEN on, par!=dpar -> no
        o = run(0,0,0,0, ctl(0,0,0,0,0,1,1), MASK16, 0,0, /*par*/0);
        check_bit("DEN.miss", o.den_ok, 0);
    }

    // ============================================================
    header("NEED_D");
    {
        Out o;
        // SX=D -> 1
        o = run(0,0,0,0, ctl(0,2,0,0,0,0,0), MASK16);
        check_bit("SX=D", o.need_d, 1);
        // SY=D -> 1
        o = run(0,0,0,0, ctl(0,0,2,0,0,0,0), MASK16);
        check_bit("SY=D", o.need_d, 1);
        // REL!=AL, CS!=0 -> 1
        o = run(0,0,0,0, ctl(0,0,0,1,1,0,0), MASK16);
        check_bit("REL+CS", o.need_d, 1);
        // REL=AL, CS!=0 -> 0 (no REL test, no D operand)
        o = run(0,0,0,0, ctl(0,0,0,1,0,0,0), MASK16);
        check_bit("AL+CS", o.need_d, 0);
        // REL=NEVER -> 0
        o = run(0,0,0,0, ctl(0,0,0,1,7,0,0), MASK16);
        check_bit("NEVER+CS", o.need_d, 0);
        // REL!=AL, CS=0 -> 0
        o = run(0,0,0,0, ctl(0,0,0,0,3,0,0), MASK16);
        check_bit("REL+CS0", o.need_d, 0);
        // nothing -> 0
        o = run(0,0,0,0, ctl(0,0,0,0,0,0,0), MASK16);
        check_bit("nothing", o.need_d, 0);
    }

    // ============================================================
    header("mask_i truncates inputs and output (W = 8/4/2/1)");
    {
        Out o;
        // AND at 4 bpp: A=0xFF, B=0xF3 -> r = 0xF3 & 0xF = 0x3
        o = run(0x00FF, 0x00F3, 0, 0, ctl(1,0,1,0,0,0,0), MASK4);
        check("AND.4bpp", o.r, 0x0003, 4);
        // OR at 2 bpp: A=0xFFFF, B=0 -> 0xFFFF & 3 = 3
        o = run(0xFFFF, 0x0000, 0, 0, ctl(2,0,1,0,0,0,0), MASK2);
        check("OR.2bpp",  o.r, 0x0003, 2);
        // ADD at 1 bpp: 1+1 = 0 (mod 2)
        o = run(0x0001, 0x0001, 0, 0, ctl(5,0,1,0,0,0,0), MASK1);
        check("ADD.1bpp", o.r, 0x0000, 1);
        // ADDS at 1 bpp: saturate to 1
        o = run(0x0001, 0x0001, 0, 0, ctl(6,0,1,0,0,0,0), MASK1);
        check("ADDS.1bpp", o.r, 0x0001, 1);
    }

    // ============================================================
    header("reserved OP / REL -> PASS / never");
    {
        Out o;
        // OP=9..15 -> PASS (x)
        o = run(0x1234, 0, 0, 0, ctl(9,0,0,0,0,0,0), MASK16);
        check("OP.9",  o.r, 0x1234, 16);
        o = run(0x1234, 0, 0, 0, ctl(15,0,0,0,0,0,0), MASK16);
        check("OP.15", o.r, 0x1234, 16);
    }

    // ============================================================
    printf("\n" CLR_BLD);
    if (errors == 0)
        printf(CLR_GRN "=== ALL %d CHECKS PASSED ===" CLR_RST "\n", checks);
    else
        printf(CLR_RED "=== %d/%d CHECKS FAILED ===" CLR_RST "\n", errors, checks);
    printf(CLR_RST);

    delete dut;
    return errors ? 1 : 0;
}