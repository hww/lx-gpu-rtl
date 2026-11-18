#include "Vcore_alu.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

static Vcore_alu* dut;
static int errors = 0;
static int checks = 0;

// ANSI colors
#define CLR_RST  "\033[0m"
#define CLR_GRN  "\033[32m"
#define CLR_RED  "\033[31m"
#define CLR_YEL  "\033[33m"
#define CLR_CYN  "\033[36m"
#define CLR_BLD  "\033[1m"

static void check(const char* name, uint32_t got, uint32_t exp, int bits) {
    checks++;
    if (got != exp) {
        printf(CLR_RED "  [FAIL] %-24s got=0x%0*X exp=0x%0*X" CLR_RST "\n",
               name, bits/4, got, bits/4, exp);
        errors++;
    }
}

static void check_bool(const char* name, int got, int exp) {
    checks++;
    if ((!!got) != (!!exp)) {
        printf(CLR_RED "  [FAIL] %-24s got=%d exp=%d" CLR_RST "\n",
               name, !!got, !!exp);
        errors++;
    }
}

static void do_op(uint8_t op, uint16_t a, uint16_t b, uint8_t c_in,
                  uint16_t exp_res, int exp_z, int exp_n, int exp_c, int exp_v,
                  const char* name) {
    dut->alu_op = op;
    dut->a = a;
    dut->b = b;
    dut->carry_in = c_in;
    dut->eval();

    printf(CLR_CYN "  %-8s" CLR_RST " op=%02X a=%04X b=%04X cin=%d -> ",
           name, op, a, b, c_in);
    int fails_before = errors;

    check("result", dut->result, exp_res, 16);
    check_bool("flag_z", dut->flag_z, exp_z);
    check_bool("flag_n", dut->flag_n, exp_n);
    if (exp_c >= 0) check_bool("flag_c", dut->flag_c, exp_c);   // -1 = don't care
    if (exp_v >= 0) check_bool("flag_v", dut->flag_v, exp_v);

    if (errors == fails_before)
        printf(CLR_GRN "OK" CLR_RST " (res=%04X z=%d n=%d c=%d v=%d)\n",
               dut->result, dut->flag_z, dut->flag_n, dut->flag_c, dut->flag_v);
    else
        printf("\n");
}

static void header(const char* title) {
    printf("\n" CLR_BLD CLR_YEL "=== %s ===" CLR_RST "\n", title);
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vcore_alu;

    printf(CLR_BLD "CORE_ALU testbench" CLR_RST " (combinational, no clk/rst)\n");

    // ===== logic =====
    header("Logic");
    do_op(0b00'00'0, 0x1234, 0x5678, 0, 0x5678, 0, 0, -1, -1, "MOV");
    do_op(0b00'00'1, 0x1234, 0x0F0F, 0, 0xF0F0, 0, 1, -1, -1, "MVN");
    do_op(0b00'01'0, 0xFF00, 0x0FF0, 0, 0x0F00, 0, 0, -1, -1, "AND");
    do_op(0b00'01'1, 0xFF00, 0x0FF0, 0, 0xF000, 0, 1, -1, -1, "BIC");
    do_op(0b00'10'0, 0xFF00, 0x0FF0, 0, 0xFFF0, 0, 1, -1, -1, "OR");
    do_op(0b00'10'1, 0xFF00, 0x0FF0, 0, 0xFF0F, 0, 1, -1, -1, "ORN");
    do_op(0b00'11'0, 0xFF00, 0x0FF0, 0, 0xF0F0, 0, 1, -1, -1, "XOR");
    do_op(0b00'11'1, 0xFF00, 0x0FF0, 0, 0x0F0F, 0, 0, -1, -1, "XNOR");

    // ===== arithmetic =====
    header("Arithmetic");
    do_op(0b01'00'0, 0x1000, 0x2000, 0, 0x3000, 0, 0, 0, 0, "ADD");
    do_op(0b01'00'0, 0xFFFF, 0x0001, 0, 0x0000, 1, 0, 1, 0, "ADD_C");
    do_op(0b01'00'0, 0x7FFF, 0x0001, 0, 0x8000, 0, 1, 0, 1, "ADD_V");
    do_op(0b01'01'0, 0x1000, 0x2000, 1, 0x3001, 0, 0, 0, 0, "ADC");
    do_op(0b01'10'1, 0x3000, 0x1000, 0, 0x2000, 0, 0, 1, 0, "SUB");
    do_op(0b01'10'1, 0x1000, 0x2000, 0, 0xF000, 0, 1, 0, 0, "SUB_B");
    do_op(0b01'11'1, 0x3000, 0x1000, 1, 0x2000, 0, 0, 1, 0, "SBC");
    do_op(0b01'11'1, 0x3000, 0x1000, 0, 0x1FFF, 0, 0, 1, 0, "SBC_B");

    // ===== shifts =====
    header("Shifts");
    do_op(0b10'00'0, 0x1234, 0x0004, 0, 0x4123, 0, 0, -1, -1, "ROR4");
    do_op(0b10'01'0, 0x1234, 0x0004, 0, 0x0123, 0, 0, -1, -1, "SHR4");
    do_op(0b10'10'0, 0x8000, 0x0004, 0, 0xF800, 0, 1, -1, -1, "SAR4");

    // ===== MUL =====
    header("MUL");
    do_op(0b11'00'0, 1000, 1000, 0, 0x4240, 0, 0, -1, -1, "MUL");
    check("mul_hi", dut->mul_hi, 0x000F, 16);

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