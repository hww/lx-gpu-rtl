// tools/isa.h — single source of truth for the LX-GPU ISA.
//
// Everything the assembler, the disassembler, the tests and the doc
// generators have to agree on lives here:
//   * opcode / condition / operand tables (X-macros)
//   * bit-field positions of the 32-bit instruction word
//   * operand capabilities (readable / writable / device access)
//   * Insn: a decoded instruction, with encode() and decode()
//   * validate(): the architectural rules that the RTL relies on.
//     The assembler rejects what validate() rejects, and the disassembler
//     prints `.word` for every word validate() rejects, so the two can
//     never drift apart.
//
// The machine-code format is unchanged with respect to the previous isa.h.
// v2 change: operands 42 and 48..63 changed meaning. 42 is now BANK
// (a global 3-bit channel-window selector, readable and writable). 48..63
// are the 16 banked-window slots, addressed as
//     operand = 48 + (group << 2) + channel
// where group and channel are taken from the low 4 bits of the operand
// code and the bank comes from the BANK register at run time. The legacy
// aliases IDXn/IDXHn/STEPn/PAGEn (v1) are gone; programs must use
// W48..W63 together with an explicit MOV BANK, #bank. The machine-code
// bit layout of the 32-bit word is unchanged.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>

// ============================================================
// 1. TABLES (the only place where codes are defined)
// ============================================================
// Arithmetic class (codes 8..15): the RTL looks only at alu_op[1:0]
// (bit 0 = subtract, bit 1 = use carry flag) and ignores alu_op[2], so
// 9/13 are both SUB, 11/15 both SBC, 8/12 both ADD, 10/14 both ADC.
// The values below are the ones already present in existing images
// (e.g. rtl/bootloader.hex uses SUB = 13) and are kept as canonical.
// Shift class (16..23): alu_op[2:1] = ROR/SHR/SAR. Class 11 is MUL.
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

// v2 operand map (see gpu_top.sv):
//   0..31 R0..R31   32 ZERO   33 FLAGS   34 BUSY   35 SHADOW   36 LOOP_CNT
//   37 LOOP_TGT   38 ROML   39 ROMH   40 ROMA(W)   41 MHI   42 BANK   43 reserved
//   44..47 DATA[0..3]
//   48..63 banked window, operand = 48 + (group << 2) + channel,
//          bank = BANK (global, 0..4):
//            bank 0 POS : 0 XI 1 YI 2 XF 3 YF
//            bank 1 STEP: 0 DXI 1 DYI 2 DXF 3 DYF
//            bank 2 CLIP: 0 XMIN 1 YMIN 2 XMAX 3 YMAX
//            bank 3 PEN : 0 B 1 KEY 2 ALU_CTL 3 -
//            bank 4 SURF: 0 MODE_PAGE 1 PITCH 2,3 -
//          banks 5..7 reserved (writes ignored).
// The W<n> names (W48..W63) are the raw operand codes. The named aliases
// (XI0, DXI0, MODE_PAGE0, ..., see ISA_BANKED) are *convenience* only: the
// program still needs an explicit MOV BANK, #bank before writing them,
// because bank is a runtime register, not part of the operand code.
#define ISA_REGS(X) \
    X(R0,0) X(R1,1) X(R2,2) X(R3,3) X(R4,4) X(R5,5) X(R6,6) X(R7,7) \
    X(R8,8) X(R9,9) X(R10,10) X(R11,11) X(R12,12) X(R13,13) X(R14,14) X(R15,15) \
    X(R16,16) X(R17,17) X(R18,18) X(R19,19) X(R20,20) X(R21,21) X(R22,22) X(R23,23) \
    X(R24,24) X(R25,25) X(R26,26) X(R27,27) X(R28,28) X(R29,29) X(R30,30) X(R31,31) \
    X(ZERO,32) X(FLAGS,33) X(BUSY,34) X(SHADOW,35) X(LOOP_CNT,36) X(LOOP_TGT,37) \
    X(ROML,38) X(ROMH,39) X(ROMA,40) X(MHI,41) X(BANK,42) X(RSV43,43) \
    X(DATA0,44) X(DATA1,45) X(DATA2,46) X(DATA3,47) \
    X(W48,48) X(W49,49) X(W50,50) X(W51,51) \
    X(W52,52) X(W53,53) X(W54,54) X(W55,55) \
    X(W56,56) X(W57,57) X(W58,58) X(W59,59) \
    X(W60,60) X(W61,61) X(W62,62) X(W63,63)

namespace isa {

// ============================================================
// 2. ENUMS
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

// Operand codes (6 bits): OPR_R0.., OPR_ZERO, OPR_FLAGS, OPR_DATA0, ...
enum Opr : uint8_t {
#define X(name, val) OPR_##name = val,
    ISA_REGS(X)
#undef X
};

// ============================================================
// 3. NAME <-> CODE (callers pass upper-case names to *_code)
// ============================================================
inline std::string to_upper(std::string s) {
    for (auto& ch : s) ch = (char)std::toupper((unsigned char)ch);
    return s;
}

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

inline int alu_code(const std::string& s) {
    if (s.empty()) return -1;
#define X(name, val) if (s == #name) return val;
    ISA_ALU_OPS(X)
#undef X
    return -1;
}

inline int ctrl_code(const std::string& s) {
    if (s.empty()) return -1;
#define X(name, val) if (s == #name) return val;
    ISA_CTRL_OPS(X)
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

// Named aliases of the banked window (assembler input only; the
// disassembler prints W48..W63): <name><channel 0..3>, code = 48 +
// (group << 2) + channel. The bank is NOT part of the code: the program must
// select it with MOV BANK, #bank before the write.
#define ISA_BANKED(X) \
    X(XI, 0, 0)   X(YI, 0, 1)   X(XF, 0, 2)   X(YF, 0, 3) \
    X(DXI, 1, 0)  X(DYI, 1, 1)  X(DXF, 1, 2)  X(DYF, 1, 3) \
    X(XMIN, 2, 0) X(YMIN, 2, 1) X(XMAX, 2, 2) X(YMAX, 2, 3) \
    X(B, 3, 0)    X(KEY, 3, 1)  X(ALU_CTL, 3, 2) \
    X(MODE_PAGE, 4, 0) X(PITCH, 4, 1)

// Bank of a named alias (-1 if s is not one), for tools that check it.
inline int banked_alias(const std::string& s, int* bank = nullptr) {
    if (s.size() < 2) return -1;
    char ch = s.back();
    if (ch < '0' || ch > '3') return -1;
    std::string base = s.substr(0, s.size() - 1);
#define X(name, bk, grp) \
    if (base == #name) { if (bank) *bank = bk; return 48 + ((grp) << 2) + (ch - '0'); }
    ISA_BANKED(X)
#undef X
    return -1;
}

inline int reg_code(const std::string& s) {
    if (s.empty()) return -1;
#define X(name, val) if (s == #name) return val;
    ISA_REGS(X)
#undef X
    return banked_alias(s);
}

inline bool alu_valid(int code)  { return alu_name(code)[0]  != '?'; }
inline bool ctrl_valid(int code) { return ctrl_name(code)[0] != '?'; }

// ============================================================
// 4. FIELDS
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

constexpr uint32_t CLASS_RR   = 0b00;   // {J, I}
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

// ============================================================
// 5. ARCHITECTURAL LIMITS
// ============================================================
constexpr int      ROM_WORDS     = 4096;   // microcode ROM depth, PC is 12 bits
constexpr int      NUM_CHANNELS  = 4;
constexpr uint32_t HALT_CODE_MAX = 127;    // status register keeps 7 bits
constexpr uint32_t IRQ_CODE_MAX  = 255;    // IRQ_CODE port is 8 bits
constexpr int      IMM11_MIN     = -1024;  // SX=1: sign-extended
constexpr int      IMM11_MAX     = 2047;   // SX=0: zero-extended
constexpr int      IMM16_MIN     = -32768;
constexpr int      IMM16_MAX     = 65535;
// RTL: only JMP by register is resolved in the X stage; CALL by register
// does not exist. Flip this together with the RTL if it is ever added.
constexpr bool     CALL_BY_REG   = false;

// ============================================================
// 6. OPERAND CAPABILITIES
// ============================================================
// RDEV: a read is a bus transaction that stalls the pipeline until acked
//       (DATAn, ROML, ROMH).
// WDEV: a write is such a transaction (DATAn).
enum : uint8_t { CAP_R = 1, CAP_W = 2, CAP_RDEV = 4, CAP_WDEV = 8 };

inline uint8_t operand_caps(int c) {
    if (c < 0 || c > 63) return 0;
    if (c < 32)          return CAP_R | CAP_W;
    switch (c) {
        case OPR_ZERO:                          // write is ignored
        case OPR_FLAGS: case OPR_SHADOW:
        case OPR_LOOP_CNT: case OPR_LOOP_TGT:
            return CAP_R | CAP_W;
        case OPR_BUSY: case OPR_MHI:
            return CAP_R;
        case OPR_ROML: case OPR_ROMH:
            return CAP_R | CAP_W | CAP_RDEV;
        case OPR_ROMA:
            return CAP_W;
        case OPR_BANK:
            return CAP_R | CAP_W;               // BANK is a normal register
        case OPR_RSV43:
            return 0;
        default: break;
    }
    if (c >= OPR_DATA0 && c < OPR_DATA0 + NUM_CHANNELS)
        return CAP_R | CAP_W | CAP_RDEV | CAP_WDEV;
    return CAP_W;                               // W48..W63: write only
}

// Largest constant a MOV/LDI may store into a special operand without the
// RTL silently truncating it.
inline uint32_t operand_imm_max(int c) {
    if (c == OPR_FLAGS)                   return 0xF;    // 4 flag bits
    if (c == OPR_LOOP_TGT || c == OPR_ROMA) return ROM_WORDS - 1;
    if (c == OPR_BANK)                    return 0x7;    // 3-bit bank select
    return 0xFFFF;                                       // W48..W63: full 16 bits
}

// Assembler convention (kept from the previous version): every ALU op except
// MOV/MVN updates the flags unless told otherwise (!nf).
inline bool alu_default_f(int op) { return op != ALU_MOV && op != ALU_MVN; }

// ============================================================
// 7. DECODED INSTRUCTION
// ============================================================
struct Insn {
    uint32_t cls   = CLASS_RR;
    uint32_t l     = 0;
    uint32_t cond  = 0;
    uint32_t write = 0;
    uint32_t f     = 0;
    uint32_t dst   = 0;
    uint32_t op    = 0;     // ALU_OP (RR / IMM) or CTRL_OP (CTRL)
    uint32_t src   = 0;     // RR: SRC;  CTRL with R=1: register operand
    uint32_t sx    = 0;
    uint32_t imm11 = 0;
    uint32_t imm16 = 0;
    uint32_t r     = 0;     // CTRL: JMP by register
    uint32_t addr  = 0;     // CTRL with R=0: ADDR / HALT code / IRQ code / channel
};

inline uint32_t encode(const Insn& i) {
    uint32_t w = enc_field(i.l, L_LO, L_HI)
               | enc_field(i.cls >> 1, J_LO, J_HI)
               | enc_field(i.cls & 1u, I_LO, I_HI)
               | enc_field(i.cond, COND_LO, COND_HI)
               | enc_field(i.write, WRITE_LO, WRITE_HI)
               | enc_field(i.f, F_LO, F_HI)
               | enc_field(i.dst, DST_LO, DST_HI);
    switch (i.cls) {
        case CLASS_RR:
            w |= enc_field(i.op, OP_LO, OP_HI) | enc_field(i.src, SRC_LO, SRC_HI);
            break;
        case CLASS_IMM:
            w |= enc_field(i.op, OP_LO, OP_HI) | enc_field(i.sx, SX_LO, SX_HI)
               | enc_field(i.imm11, IMM11_LO, IMM11_HI);
            break;
        case CLASS_LDI:
            w |= enc_field(i.imm16, IMM16_LO, IMM16_HI);
            break;
        default:  // CLASS_CTRL
            w |= enc_field(i.r, R_LO, R_HI) | enc_field(i.op, CTRLOP_LO, CTRLOP_HI);
            w |= i.r ? enc_field(i.src, SRC_LO, SRC_HI)
                     : enc_field(i.addr, ADDR_LO, ADDR_HI);
            break;
    }
    return w;
}

// Raw field extraction; bits the format does not carry (RSV) are dropped.
inline Insn decode(uint32_t w) {
    Insn c;
    c.l     = dec_field(w, L_LO, L_HI);
    c.cls   = (dec_field(w, J_LO, J_HI) << 1) | dec_field(w, I_LO, I_HI);
    c.cond  = dec_field(w, COND_LO, COND_HI);
    c.write = dec_field(w, WRITE_LO, WRITE_HI);
    c.f     = dec_field(w, F_LO, F_HI);
    c.dst   = dec_field(w, DST_LO, DST_HI);
    switch (c.cls) {
        case CLASS_RR:
            c.op  = dec_field(w, OP_LO, OP_HI);
            c.src = dec_field(w, SRC_LO, SRC_HI);
            break;
        case CLASS_IMM:
            c.op    = dec_field(w, OP_LO, OP_HI);
            c.sx    = dec_field(w, SX_LO, SX_HI);
            c.imm11 = dec_field(w, IMM11_LO, IMM11_HI);
            break;
        case CLASS_LDI:
            c.imm16 = dec_field(w, IMM16_LO, IMM16_HI);
            break;
        default:
            c.r    = dec_field(w, R_LO, R_HI);
            c.op   = dec_field(w, CTRLOP_LO, CTRLOP_HI);
            c.src  = dec_field(w, SRC_LO, SRC_HI);
            c.addr = dec_field(w, ADDR_LO, ADDR_HI);
            break;
    }
    return c;
}

// Value an IMM11 operand has after the RTL's extension (sign-extends only if
// SX = 1 and bit 10 is set).
inline int imm11_value(uint32_t sx, uint32_t imm11) {
    return (sx && (imm11 & 0x400u)) ? (int)imm11 - 2048 : (int)imm11;
}

// ============================================================
// 8. ARCHITECTURAL RULES
// ============================================================
// Returns true if the instruction is something the assembler accepts and the
// RTL executes as written. Otherwise fills *why (if given).
// Rules (see top.sv):
//   * special operands can be written only by MOV / LDI (operand A of every
//     other op is read from the register file, so a special destination
//     would read garbage);
//   * read-only operands (BUSY, MHI) are not writable, write-only operands
//     (ROMA, IDX*, IDXH*, STEP*, PAGE*) are not readable, 42/43 are reserved;
//   * one device transaction per instruction;
//   * constants stored into special operands must fit the register;
//   * JMP by register takes a readable, non-device operand; no CALL by reg;
//   * control-word argument ranges (HALT, IRQ, FLUSH);
//   * no L bit on CALL (the loop would end after RET, not repeat).
inline bool validate(const Insn& c, std::string* why = nullptr) {
    auto fail = [&](const std::string& m) { if (why) *why = m; return false; };
    auto nm   = [](uint32_t code) { return std::string(reg_name((int)code)); };
    auto hex  = [](uint32_t v) {
        char b[16]; snprintf(b, sizeof b, "0x%X", v); return std::string(b);
    };

    if (c.cond > 14) return fail("reserved condition code " + std::to_string(c.cond));

    switch (c.cls) {
    case CLASS_RR:
    case CLASS_IMM: {
        if (!alu_valid((int)c.op))
            return fail("reserved ALU opcode " + std::to_string(c.op));
        uint8_t dcap = operand_caps((int)c.dst);
        if (!(dcap & CAP_W))
            return fail("operand " + nm(c.dst) + " is not writable");
        if (c.dst >= 32 && c.op != ALU_MOV)
            return fail("special operand " + nm(c.dst) +
                        " can be written only by MOV or LDI");
        if (c.cls == CLASS_RR) {
            uint8_t scap = operand_caps((int)c.src);
            if (!(scap & CAP_R))
                return fail("operand " + nm(c.src) + " is not readable");
            if ((scap & CAP_RDEV) && (dcap & CAP_WDEV))
                return fail("source " + nm(c.src) + " and destination " + nm(c.dst) +
                            " are both bus accesses; use a register in between");
            if ((c.src == OPR_ROML || c.src == OPR_ROMH) && c.dst == OPR_ROMH)
                return fail("ROM read and ROMH write in one instruction");
        } else {
            if (c.sx && !(c.imm11 & 0x400u))
                return fail("SX set but IMM11 bit 10 clear (not a form the assembler produces)");
            if (c.dst >= 32) {
                uint32_t v = (uint32_t)imm11_value(c.sx, c.imm11) & 0xFFFFu;
                if (v > operand_imm_max((int)c.dst))
                    return fail("constant " + hex(v) + " does not fit " + nm(c.dst) +
                                " (max " + hex(operand_imm_max((int)c.dst)) + ")");
            }
        }
        // A banked-window operand (W48..W63) is only meaningful when BANK
        // has been set to a defined value (0..4) by a previous instruction.
        // The assembler cannot see the bank at run time, so it warns but
        // does not reject — the program author is responsible for pairing
        // MOV BANK, #n with the matching banked write. (The RTL ignores
        // writes to banks 5..7.)
        if (c.dst >= OPR_W48 && c.dst <= OPR_W63) {
            // no static check possible; the address decode is done by
            // ch_addr_gen at run time based on the current bank register
        }
        return true;
    }
    case CLASS_LDI: {
        uint8_t dcap = operand_caps((int)c.dst);
        if (!(dcap & CAP_W))
            return fail("operand " + nm(c.dst) + " is not writable");
        if (c.dst >= 32 && c.imm16 > operand_imm_max((int)c.dst))
            return fail("constant " + hex(c.imm16) + " does not fit " + nm(c.dst) +
                        " (max " + hex(operand_imm_max((int)c.dst)) + ")");
        return true;
    }
    default: {   // CLASS_CTRL
        if (!ctrl_valid((int)c.op))
            return fail("reserved control opcode " + std::to_string(c.op));
        // CALL stores the address after the loop body in SHADOW, so after RET
        // the loop ends instead of repeating. L stays legal on JMP, RET, HALT.
        if (c.l && c.op == CTRL_CALL)
            return fail("L is not allowed on CALL (after RET the loop ends, it does "
                        "not repeat); put L on a data word after the CALL");
        if (c.r) {
            if (c.op == CTRL_CALL && !CALL_BY_REG)
                return fail("CALL by register is not supported (only JMP Rn)");
            if (c.op != CTRL_JMP && c.op != CTRL_CALL)
                return fail(std::string(ctrl_name((int)c.op)) + " cannot use a register operand");
            uint8_t scap = operand_caps((int)c.src);
            if (!(scap & CAP_R))
                return fail("jump operand " + nm(c.src) + " is not readable");
            if (scap & CAP_RDEV)
                return fail("jump operand " + nm(c.src) +
                            " is a bus access; copy it to a register first");
            return true;
        }
        switch (c.op) {
            case CTRL_NOP: case CTRL_RET:
                if (c.addr != 0) return fail(std::string(ctrl_name((int)c.op)) + " takes no argument");
                break;
            case CTRL_HALT:
                if (c.addr > HALT_CODE_MAX)
                    return fail("HALT code " + std::to_string(c.addr) + " exceeds " +
                                std::to_string(HALT_CODE_MAX));
                break;
            case CTRL_IRQ:
                if (c.addr > IRQ_CODE_MAX)
                    return fail("IRQ code " + std::to_string(c.addr) + " exceeds " +
                                std::to_string(IRQ_CODE_MAX));
                break;
            case CTRL_FLUSH:
                if (c.addr >= (uint32_t)NUM_CHANNELS)
                    return fail("FLUSH channel " + std::to_string(c.addr) + " is not 0.." +
                                std::to_string(NUM_CHANNELS - 1));
                break;
            default: break;   // JMP / CALL: 12-bit target, always in range
        }
        return true;
    }
    }
}

} // namespace isa

// ============================================================
// 9. COMPILE-TIME CHECKS
// ============================================================
static_assert(isa::L_LO == 31 && isa::J_LO == 30 && isa::I_LO == 29, "ISA drift: L/J/I");
static_assert(isa::COND_LO == 25 && isa::COND_HI == 28, "ISA drift: COND");
static_assert(isa::WRITE_LO == 24 && isa::F_LO == 23, "ISA drift: WRITE/F");
static_assert(isa::DST_LO == 17 && isa::DST_HI == 22, "ISA drift: DST");
static_assert(isa::OP_LO == 12 && isa::OP_HI == 16, "ISA drift: ALU_OP");
static_assert(isa::SRC_LO == 6 && isa::SRC_HI == 11, "ISA drift: SRC");
static_assert(isa::IMM11_LO == 0 && isa::IMM11_HI == 10, "ISA drift: IMM11");
static_assert(isa::IMM16_LO == 0 && isa::IMM16_HI == 15, "ISA drift: IMM16");
static_assert(isa::CTRLOP_LO == 12 && isa::CTRLOP_HI == 15, "ISA drift: CTRL_OP");
static_assert(isa::ADDR_LO == 0 && isa::ADDR_HI == 11, "ISA drift: ADDR");
static_assert(isa::ALU_SUB == 13, "isa.h: SUB = 13 is used by existing images (9 is the RTL alias)");
static_assert(isa::ALU_ADC == 10, "isa.h: ADC must be 10");
static_assert(isa::ALU_SBC == 15, "isa.h: SBC = 15 is used by existing images (11 is the RTL alias)");
