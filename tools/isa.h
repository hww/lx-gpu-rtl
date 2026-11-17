// tools/isa.h — единый источник правды для ISA LX-GPU
#pragma once
#include <cstdint>
#include <string>

// ============================================================
// 1. СПИСКИ ОПЕРАЦИЙ (единственное место, где заданы коды)
// ============================================================
#define ISA_ALU_OPS(X) \
    X(MOV,   0)  X(MVN,   1)  X(AND,   2)  X(BIC,   3) \
    X(OR,    4)  X(ORN,   5)  X(XOR,   6)  X(XNOR,  7) \
    X(ADD,   8)  X(ADC,  10)  X(SUB,  13)  X(SBC,  15) \
    X(ROR,  16)  X(SHR,  18)  X(SAR,  20) \
    X(MUL,  24)

#define ISA_CTRL_OPS(X) \
    X(NOP,    0)  X(JMP,    1)  X(CALL,   2)  X(RET,    3) \
    X(HALT,   4)  X(IRQ,    5)  X(FLUSH,  6)

#define ISA_CONDS(X) \
    X(AL, 0)  X(EQ, 1)  X(NE, 2)  X(CS, 3) \
    X(CC, 4)  X(MI, 5)  X(PL, 6)  X(VS, 7) \
    X(VC, 8)  X(HI, 9)  X(LS,10)  X(GE,11) \
    X(LT,12)  X(GT,13)  X(LE,14)

#define ISA_REGS(X) \
    X(R0,0) X(R1,1) X(R2,2) X(R3,3) X(R4,4) X(R5,5) X(R6,6) X(R7,7) \
    X(R8,8) X(R9,9) X(R10,10) X(R11,11) X(R12,12) X(R13,13) X(R14,14) X(R15,15) \
    X(R16,16) X(R17,17) X(R18,18) X(R19,19) X(R20,20) X(R21,21) X(R22,22) X(R23,23) \
    X(R24,24) X(R25,25) X(R26,26) X(R27,27) X(R28,28) X(R29,29) X(R30,30) X(R31,31) \
    X(ZERO,32) X(FLAGS,33) X(BUSY,34) X(SHADOW,35) X(LOOP_CNT,36) X(LOOP_TGT,37) \
    X(ROML,38) X(ROMH,39) X(ROMA,40) X(MHI,41) X(RSV42,42) X(RSV43,43) \
    X(DATA0,44) X(DATA1,45) X(DATA2,46) X(DATA3,47) \
    X(IDX0,48) X(IDX1,49) X(IDX2,50) X(IDX3,51) \
    X(IDXH0,52) X(IDXH1,53) X(IDXH2,54) X(IDXH3,55) \
    X(STEP0,56) X(STEP1,57) X(STEP2,58) X(STEP3,59) \
    X(PAGE0,60) X(PAGE1,61) X(PAGE2,62) X(PAGE3,63)

namespace isa {

// ============================================================
// 2. ГЕНЕРАЦИЯ ENUM (работает везде)
// ============================================================
enum AluOp : uint8_t {
#define X(name, val) ALU_##name = val,
    ISA_ALU_OPS(X)
#undef X
};

enum CtrlOp : uint8_t {
#define X(name, val) CTRL_##name = val,
    ISA_CTRL_OPS(X)
#undef X
};

// ============================================================
// 3. ФУНКЦИИ ДЛЯ ПОЛУЧЕНИЯ ИМЁН (вместо массивов с пропусками)
// ============================================================
inline const char* alu_name(int code) {
    switch (code) {
#define X(name, val) case val: return #name;
        ISA_ALU_OPS(X)
#undef X
        default: return "???";
    }
}

inline const char* ctrl_name(int code) {
    switch (code) {
#define X(name, val) case val: return #name;
        ISA_CTRL_OPS(X)
#undef X
        default: return "???";
    }
}

inline const char* cond_name(int code) {
    switch (code) {
#define X(name, val) case val: return #name;
        ISA_CONDS(X)
#undef X
        default: return "??";
    }
}

inline const char* reg_name(int code) {
    switch (code) {
#define X(name, val) case val: return #name;
        ISA_REGS(X)
#undef X
        default: return "R??";
    }
}

// ============================================================
// 4. УТИЛИТЫ ДЛЯ ПАРСИНГА (ищут код по имени)
// ============================================================
inline int alu_code(const std::string& s) {
    if (s.empty()) return -1;
#define X(name, val) if (s == #name) return val;
    ISA_ALU_OPS(X)
#undef X
    return -1;
}

inline int cond_code(const std::string& s) {
    if (s.empty()) return -1;
#define X(name, val) if (s == #name) return val;
    ISA_CONDS(X)
#undef X
    return -1;
}

inline int reg_code(const std::string& s) {
    if (s.empty()) return -1;
#define X(name, val) if (s == #name) return val;
    ISA_REGS(X)
#undef X
    return -1;
}

// ============================================================
// 5. ПОЛЯ И УТИЛИТЫ БИТОВЫХ ОПЕРАЦИЙ
// ============================================================
constexpr int L_LO      = 31, L_HI      = 31;
constexpr int J_LO      = 30, J_HI      = 30;
constexpr int I_LO      = 29, I_HI      = 29;
constexpr int COND_LO   = 25, COND_HI   = 28;
constexpr int WRITE_LO  = 24, WRITE_HI  = 24;
constexpr int F_LO      = 23, F_HI      = 23;
constexpr int DST_LO    = 17, DST_HI    = 22;
constexpr int OP_LO     = 12, OP_HI     = 16;
constexpr int SRC_LO    = 6,  SRC_HI    = 11;
constexpr int SX_LO     = 11, SX_HI     = 11;
constexpr int IMM11_LO  = 0,  IMM11_HI  = 10;
constexpr int IMM16_LO  = 0,  IMM16_HI  = 15;
constexpr int R_LO      = 16, R_HI      = 16;
constexpr int CTRLOP_LO = 12, CTRLOP_HI = 15;
constexpr int ADDR_LO   = 0,  ADDR_HI   = 11;

constexpr uint32_t CLASS_RR   = 0b00;
constexpr uint32_t CLASS_LDI  = 0b01;
constexpr uint32_t CLASS_CTRL = 0b10;
constexpr uint32_t CLASS_IMM  = 0b11;

inline uint32_t enc_field(uint32_t val, int lo, int hi) {
    int n = hi - lo + 1;
    uint32_t mask = (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1);
    return (val & mask) << lo;
}
inline uint32_t dec_field(uint32_t w, int lo, int hi) {
    int n = hi - lo + 1;
    uint32_t mask = (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1);
    return (w >> lo) & mask;
}

} // namespace isa

// ============================================================
// 6. COMPILE-TIME ПРОВЕРКИ
// ============================================================
static_assert(isa::L_LO == 31 && isa::J_LO == 30 && isa::I_LO == 29, "ISA drift: L/J/I");
static_assert(isa::COND_LO == 25 && isa::COND_HI == 28, "ISA drift: COND");
static_assert(isa::DST_LO == 17 && isa::DST_HI == 22, "ISA drift: DST");
static_assert(isa::OP_LO == 12 && isa::OP_HI == 16, "ISA drift: ALU_OP");
static_assert(isa::SRC_LO == 6 && isa::SRC_HI == 11, "ISA drift: SRC");
static_assert(isa::IMM11_LO == 0 && isa::IMM11_HI == 10, "ISA drift: IMM11");
static_assert(isa::IMM16_LO == 0 && isa::IMM16_HI == 15, "ISA drift: IMM16");
static_assert(isa::CTRLOP_LO == 12 && isa::CTRLOP_HI == 15, "ISA drift: CTRL_OP");
static_assert(isa::ADDR_LO == 0 && isa::ADDR_HI == 11, "ISA drift: ADDR");
static_assert(isa::ALU_SUB == 13, "isa.h: ALU_SUB must be 13 per RTL");
static_assert(isa::ALU_ADC == 10, "isa.h: ALU_ADC must be 10 per RTL");
static_assert(isa::ALU_SBC == 15, "isa.h: ALU_SBC must be 15 per RTL");