// === FILE: core_alu.h ===
#pragma once
#include <cstdint>

namespace lx {

// Коды операций ALU (5 бит: [4:3]=класс, [2:1]=функция, [0]=модификатор)
enum AluOp : uint8_t {
    // 00: Logic
    ALU_MOV  = 0,  // 00_00_0: res = B
    ALU_MVN  = 1,  // 00_00_1: res = ~B
    ALU_AND  = 2,  // 00_01_0: res = A & B
    ALU_ANDN = 3,  // 00_01_1: res = A & ~B
    ALU_OR   = 4,  // 00_10_0: res = A | B
    ALU_ORN  = 5,  // 00_10_1: res = A | ~B
    ALU_XOR  = 6,  // 00_11_0: res = A ^ B
    ALU_XNOR = 7,  // 00_11_1: res = A ^ ~B

    // 01: Arith
    ALU_ADD  = 8,  // 01_00_0: A + B
    ALU_SUB  = 9,  // 01_00_1: A - B
    ALU_ADC  = 10, // 01_01_0: A + B + C
    ALU_SBC  = 11, // 01_01_1: A - B - !C

    // 10: Shift
    ALU_ROR  = 16, // 10_00_0: ROR(A, B[3:0])
    ALU_SHR  = 18, // 10_01_0: SHR(A, B[3:0])
    ALU_SAR  = 20, // 10_10_0: SAR(A, B[3:0])

    // 11: Mul
    ALU_MUL  = 24  // 11_00_0: signed 16x16 -> {mul_hi, result}
};

// Условия исполнения (4 бита: [28:25])
enum Cond : uint8_t {
    COND_AL = 0,  COND_EQ = 1,  COND_NE = 2,  COND_CS = 3,
    COND_CC = 4,  COND_MI = 5,  COND_PL = 6,  COND_VS = 7,
    COND_VC = 8,  COND_HI = 9,  COND_LS = 10, COND_GE = 11,
    COND_LT = 12, COND_GT = 13, COND_LE = 14, COND_NV = 15
};

// Управляющие инструкции (cls == 2'b10, ctrl_op = [15:12])
enum CtrlOp : uint8_t {
    CTRL_NOP   = 0,
    CTRL_JMP   = 1,
    CTRL_CALL  = 2,
    CTRL_RET   = 3,
    CTRL_HALT  = 4,
    CTRL_IRQ   = 5,
    CTRL_FLUSH = 6
};

struct AluOut {
    uint16_t result;
    uint16_t mul_hi;
    bool     z;
    bool     n;
    bool     c;
    bool     v;
    bool     upd_cv;
};

class CoreAlu {
public:
    static AluOut exec(uint8_t alu_op, uint16_t a, uint16_t b, bool carry_in);
    static bool   check_cond(uint8_t cond, uint8_t flags); // flags = {V, C, N, Z}
};

} // namespace lx
