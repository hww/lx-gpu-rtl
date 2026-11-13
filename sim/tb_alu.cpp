#include "Valu.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <string>

static Valu* dut;
static int errors = 0;

static void tick() {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

static void check(const char* name, uint16_t got, uint16_t exp) {
    if (got != exp) {
        printf("FAIL %s: got %04X exp %04X\n", name, got, exp);
        errors++;
    }
}

static void check_bit(const char* name, int got, int exp) {
    if (got != exp) {
        printf("FAIL %s: got %d exp %d\n", name, got, exp);
        errors++;
    }
}

static void do_op(uint8_t op, uint16_t a, uint16_t b, uint8_t c_in,
                  uint16_t exp_res, int exp_z, int exp_n, int exp_c, int exp_v,
                  const char* name) {
    dut->op = op;
    dut->a = a;
    dut->b = b;
    dut->c_in = c_in;
    dut->eval();
    check(name, dut->res, exp_res);
    check_bit((std::string(name) + ".z").c_str(), dut->z, exp_z);
    check_bit((std::string(name) + ".n").c_str(), dut->n, exp_n);
    check_bit((std::string(name) + ".c").c_str(), dut->c, exp_c);
    check_bit((std::string(name) + ".v").c_str(), dut->v, exp_v);
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Valu;

    dut->rst = 1; tick(); tick();
    dut->rst = 0;

    printf("=== ALU tests ===\n");

    // MOV (logic, fn=00, inv=0): res = b
    do_op(0b00'00'0, 0x1234, 0x5678, 0, 0x5678, 0, 0, 0, 0, "MOV");

    // MVN (logic, fn=00, inv=1): res = ~b
    do_op(0b00'00'1, 0x1234, 0x0F0F, 0, 0xF0F0, 0, 1, 0, 0, "MVN");

    // AND
    do_op(0b00'01'0, 0xFF00, 0x0FF0, 0, 0x0F00, 0, 0, 0, 0, "AND");

    // BIC (a & ~b)
    do_op(0b00'01'1, 0xFF00, 0x0FF0, 0, 0xF000, 0, 1, 0, 0, "BIC");

    // OR
    do_op(0b00'10'0, 0xFF00, 0x0FF0, 0, 0xFFF0, 0, 1, 0, 0, "OR");

    // XOR
    do_op(0b00'11'0, 0xFF00, 0x0FF0, 0, 0xF0F0, 0, 1, 0, 0, "XOR");

    // ADD
    do_op(0b01'00'0, 0x1000, 0x2000, 0, 0x3000, 0, 0, 0, 0, "ADD");

    // ADD carry: 0xFFFF + 1 = 0x0000, C=1
    do_op(0b01'00'0, 0xFFFF, 0x0001, 0, 0x0000, 1, 0, 1, 0, "ADD_C");

    // ADD overflow: 0x7FFF + 1 = 0x8000, N=1, V=1
    do_op(0b01'00'0, 0x7FFF, 0x0001, 0, 0x8000, 0, 1, 0, 1, "ADD_V");

    // ADC
    do_op(0b01'01'0, 0x1000, 0x2000, 1, 0x3001, 0, 0, 0, 0, "ADC");

    // SUB: 0x3000 - 0x1000 = 0x2000, C=1 (not borrow)
    do_op(0b01'10'1, 0x3000, 0x1000, 0, 0x2000, 0, 0, 1, 0, "SUB");

    // SUB borrow: 0x1000 - 0x2000 = 0xF000, C=0
    do_op(0b01'10'1, 0x1000, 0x2000, 0, 0xF000, 0, 1, 0, 0, "SUB_B");

    // SBC
    do_op(0b01'11'1, 0x3000, 0x1000, 1, 0x2000, 0, 0, 1, 0, "SBC");

    // SBC borrow: 0x3000 - 0x1000 - 1 = 0x1FFF
    do_op(0b01'11'1, 0x3000, 0x1000, 0, 0x1FFF, 0, 0, 1, 0, "SBC_B");

    // ROR 4
    do_op(0b10'00'0, 0x1234, 0x0004, 0, 0x4123, 0, 0, 0, 0, "ROR4");

    // SHR 4
    do_op(0b10'01'0, 0x1234, 0x0004, 0, 0x0123, 0, 0, 0, 0, "SHR4");

    // SAR 4: 0x8000 -> 0xF800
    do_op(0b10'10'0, 0x8000, 0x0004, 0, 0xF800, 0, 1, 0, 0, "SAR4");

    // MUL: 1000*1000 = 1,000,000 = 0xF4240, low 16 = 0x4240
    do_op(0b11'00'0, 1000, 1000, 0, 0x4240, 0, 0, 0, 0, "MUL");
    tick();  // mhi регистрируется
    check("MUL_MHI", dut->mhi, 0x000F);

    // TST
    do_op(0b11'10'0, 0xFF00, 0x0FF0, 0, 0xFF00, 0, 0, 0, 0, "TST");
    check_bit("TST.nowrite", dut->nowrite, 1);

    // CMP
    do_op(0b11'11'0, 0x3000, 0x1000, 0, 0x3000, 0, 0, 1, 0, "CMP");
    check_bit("CMP.nowrite", dut->nowrite, 1);

    printf("=== ALU tests done: %d errors ===\n", errors);
    delete dut;
    return errors ? 1 : 0;
}