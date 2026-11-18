// === FILE: core_alu.cpp ===
#include "core_alu.h"

namespace lx {

AluOut CoreAlu::exec(uint8_t alu_op, uint16_t a, uint16_t b, bool carry_in) {
    uint8_t op_class  = (alu_op >> 3) & 3;
    uint8_t op_fn     = (alu_op >> 1) & 3;
    bool    is_logic  = (op_class == 0);
    bool    is_arith  = (op_class == 1);
    bool    inv_b     = is_logic && (alu_op & 1);
    bool    sub       = is_arith && (alu_op & 1);
    bool    use_carry = is_arith && ((alu_op >> 1) & 1);

    uint16_t b_x = (inv_b || sub) ? static_cast<uint16_t>(~b) : b;

    // Сложение с переносом (идентично sum_ext в core_alu.sv)
    uint32_t cin = use_carry ? (carry_in ? 1u : 0u) : (sub ? 1u : 0u);
    uint32_t sum = static_cast<uint32_t>(a) + static_cast<uint32_t>(b_x) + cin;

    // Логика
    uint16_t logic_res = 0;
    switch (op_fn) {
        case 0: logic_res = b_x;     break;
        case 1: logic_res = a & b_x; break;
        case 2: logic_res = a | b_x; break;
        case 3: logic_res = a ^ b_x; break;
    }

    // Сдвиг вправо (ROR / SHR / SAR на b[3:0])
    uint8_t  shamt = b & 0x0F;
    uint16_t shift_res = a;
    if (shamt != 0) {
        if (op_fn == 0) { // ROR
            shift_res = static_cast<uint16_t>((a >> shamt) | (a << (16 - shamt)));
        } else if (op_fn == 2) { // SAR
            shift_res = static_cast<uint16_t>(static_cast<int16_t>(a) >> shamt);
        } else { // SHR
            shift_res = static_cast<uint16_t>(a >> shamt);
        }
    }

    // Знаковое умножение 16x16 -> 32
    int32_t  mul_p  = static_cast<int32_t>(static_cast<int16_t>(a))
                    * static_cast<int32_t>(static_cast<int16_t>(b));
    uint16_t mul_lo = static_cast<uint16_t>(mul_p & 0xFFFF);
    uint16_t mul_hi = static_cast<uint16_t>((static_cast<uint32_t>(mul_p) >> 16) & 0xFFFF);

    uint16_t result = 0;
    switch (op_class) {
        case 0: result = logic_res;                   break;
        case 1: result = static_cast<uint16_t>(sum);  break;
        case 2: result = shift_res;                   break;
        case 3: result = mul_lo;                      break;
    }

    AluOut out{};
    out.result = result;
    out.mul_hi = mul_hi;
    out.z      = (result == 0);
    out.n      = (result >> 15) & 1;
    out.c      = (sum >> 16) & 1;
    out.v      = (((a ^ b_x) & 0x8000) == 0) && (((sum ^ a) & 0x8000) != 0);
    out.upd_cv = is_arith;
    return out;
}

bool CoreAlu::check_cond(uint8_t cond, uint8_t flags) {
    bool z = (flags >> 0) & 1;
    bool n = (flags >> 1) & 1;
    bool c = (flags >> 2) & 1;
    bool v = (flags >> 3) & 1;

    switch (cond & 0xF) {
        case COND_AL: return true;
        case COND_EQ: return z;
        case COND_NE: return !z;
        case COND_CS: return c;
        case COND_CC: return !c;
        case COND_MI: return n;
        case COND_PL: return !n;
        case COND_VS: return v;
        case COND_VC: return !v;
        case COND_HI: return c && !z;
        case COND_LS: return !c || z;
        case COND_GE: return n == v;
        case COND_LT: return n != v;
        case COND_GT: return !z && (n == v);
        case COND_LE: return z || (n != v);
        default:      return false;
    }
}

} // namespace lx
