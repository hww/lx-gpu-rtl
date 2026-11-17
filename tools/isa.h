// tools/isa.h — единый источник правды для ISA LX-GPU
#pragma once
#include <cstdint>
#include <string>

namespace isa {

// ===== раскладка полей (единственное место с номерами бит) =====
constexpr int L_LO      = 31, L_HI      = 31;
constexpr int J_LO      = 30, J_HI      = 30;
constexpr int I_LO      = 29, I_HI      = 29;
constexpr int COND_LO   = 25, COND_HI   = 28;   // ← было 28, 25
constexpr int WRITE_LO  = 24, WRITE_HI  = 24;
constexpr int F_LO      = 23, F_HI      = 23;
constexpr int DST_LO    = 17, DST_HI    = 22;   // ← было 22, 17
constexpr int OP_LO     = 12, OP_HI     = 16;   // ← было 16, 12
constexpr int SRC_LO    = 6,  SRC_HI    = 11;   // ← было 11, 6
constexpr int SX_LO     = 11, SX_HI     = 11;
constexpr int IMM11_LO  = 0,  IMM11_HI  = 10;   // ← было 10, 0
constexpr int IMM16_LO  = 0,  IMM16_HI  = 15;   // ← было 15, 0
constexpr int R_LO      = 16, R_HI      = 16;
constexpr int CTRLOP_LO = 12, CTRLOP_HI = 15;   // ← было 15, 12
constexpr int ADDR_LO   = 0,  ADDR_HI   = 11;   // ← было 11, 0

// классы (J:I)
constexpr uint32_t CLASS_RR   = 0b00;
constexpr uint32_t CLASS_LDI  = 0b01;
constexpr uint32_t CLASS_CTRL = 0b10;
constexpr uint32_t CLASS_IMM  = 0b11;

// ===== коды ALU =====
enum AluOp : uint8_t {
    ALU_MOV=0, ALU_MVN=1, ALU_AND=2, ALU_BIC=3,
    ALU_OR=4,  ALU_ORN=5, ALU_XOR=6, ALU_XNOR=7,
    ALU_ADD=8, ALU_ADC=9, ALU_SUB=10, ALU_SBC=11,
    ALU_ROR=12, ALU_SHR=13, ALU_SAR=14,
    ALU_MUL=24, ALU_TST=26, ALU_CMP=27,
};

// ===== коды CTRL =====
enum CtrlOp : uint8_t {
    CTRL_NOP=0, CTRL_JMP=1, CTRL_CALL=2, CTRL_RET=3,
    CTRL_HALT=4, CTRL_IRQ=5, CTRL_FLUSH=6,
};

// ===== имена (для asm и disasm) =====
static const char* const REG_NAMES[64] = {
    "R0","R1","R2","R3","R4","R5","R6","R7",
    "R8","R9","R10","R11","R12","R13","R14","R15",
    "R16","R17","R18","R19","R20","R21","R22","R23",
    "R24","R25","R26","R27","R28","R29","R30","R31",
    "ZERO","FLAGS","BUSY","SHADOW","LOOP.CNT","LOOP.TGT",
    "ROML","ROMH","ROMA","MHI","RSV42","RSV43",
    "DATA0","DATA1","DATA2","DATA3",
    "IDX0","IDX1","IDX2","IDX3",
    "IDXH0","IDXH1","IDXH2","IDXH3",
    "STEP0","STEP1","STEP2","STEP3",
    "PAGE0","PAGE1","PAGE2","PAGE3",
};

static const char* const ALU_NAMES[32] = {
    "MOV","MVN","AND","BIC","OR","ORN","XOR","XNOR",
    "ADD","ADC","SUB","SBC","ROR","SHR","SAR","???",
    "???","???","???","???","???","???","???","???",
    "MUL","???","TST","CMP","???","???","???","???"
};

static const char* const COND_NAMES[16] = {
    "AL","EQ","NE","CS","CC","MI","PL","VS",
    "VC","HI","LS","GE","LT","GT","LE","??"
};

static const char* const CTRL_NAMES[16] = {
    "NOP","JMP","CALL","RET","HALT","IRQ","FLUSH","RSV7",
    "RSV8","RSV9","RSV10","RSV11","RSV12","RSV13","RSV14","RSV15"
};

// ===== утилиты =====
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

inline int reg_code(const std::string& s) {
    for (int i = 0; i < 64; i++) if (s == REG_NAMES[i]) return i;
    return -1;
}
inline int cond_code(const std::string& s) {
    for (int i = 0; i < 15; i++) if (s == COND_NAMES[i]) return i;
    return -1;
}
inline int alu_code(const std::string& s) {
    for (int i = 0; i < 32; i++) if (s == ALU_NAMES[i] && s != "???") return i;
    return -1;
}

} // namespace isa

// ===== compile-time проверка: раскладка соответствует RTL =====
// Если поменяешь rtl/top.sv и забудешь isa.h — упадёт компиляция.
static_assert(isa::L_LO == 31 && isa::J_LO == 30 && isa::I_LO == 29,
              "ISA drift: L/J/I bits, check rtl/top.sv");
static_assert(isa::COND_LO == 25 && isa::COND_HI == 28, "ISA drift: COND");
static_assert(isa::WRITE_LO == 24 && isa::F_LO == 23, "ISA drift: WRITE/F");
static_assert(isa::DST_LO == 17 && isa::DST_HI == 22, "ISA drift: DST");
static_assert(isa::OP_LO == 12 && isa::OP_HI == 16, "ISA drift: ALU_OP");
static_assert(isa::SRC_LO == 6 && isa::SRC_HI == 11, "ISA drift: SRC");
static_assert(isa::IMM11_LO == 0 && isa::IMM11_HI == 10, "ISA drift: IMM11");
static_assert(isa::IMM16_LO == 0 && isa::IMM16_HI == 15, "ISA drift: IMM16");
static_assert(isa::CTRLOP_LO == 12 && isa::CTRLOP_HI == 15, "ISA drift: CTRL_OP");
static_assert(isa::ADDR_LO == 0 && isa::ADDR_HI == 11, "ISA drift: ADDR");