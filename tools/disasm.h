// tools/disasm.h — LX-GPU disassembler (depends only on tools/isa.h).
//
// Round-trip guarantee: for EVERY 32-bit word w,
//     asm(disasm32(w)) == w
// A word is printed as text only if
//   * it passes isa::validate()  (the assembler would accept the text), and
//   * re-encoding the printed fields gives w again (no reserved/unused bits
//     are set, no alias opcode, no non-canonical LDI/CTRL padding).
// Every other word is printed as ".word 0x........".
//
// Output syntax is the one documented in asm.h:
//   [L ]MNEMONIC[.COND] operands [!w] [!f|!nf]
// Modifiers appear only where they differ from the assembler defaults;
// SUB/AND with !w and F=1 are printed as CMP / TST.
#pragma once
#include "isa.h"
#include <cstdio>
#include <string>

static inline const char* disasm32(uint32_t w) {
    using namespace isa;
    static char buf[160];

    Insn c = decode(w);

    // Reduce to what the text can express; encode() below detects any
    // difference from the original word.
    if (c.cls == CLASS_LDI) { c.write = 1; c.f = 0; }
    if (c.cls == CLASS_CTRL) {
        c.write = 0; c.f = 0; c.dst = 0;
        if (c.r) c.addr = 0; else c.src = 0;
    }

    std::string why;
    if (!validate(c, &why) || encode(c) != w) {
        snprintf(buf, sizeof buf, ".word 0x%08X", w);
        return buf;
    }

    std::string s;
    if (c.l) s += "L ";
    const std::string cnd = c.cond ? std::string(".") + cond_name((int)c.cond) : std::string();

    switch (c.cls) {
    case CLASS_LDI: {
        char t[48];
        snprintf(t, sizeof t, "LDI%s %s, #0x%04X", cnd.c_str(), reg_name((int)c.dst), c.imm16);
        s += t;
        break;
    }
    case CLASS_CTRL: {
        const char* opn = ctrl_name((int)c.op);
        char t[64];
        if (c.op == CTRL_JMP || c.op == CTRL_CALL) {
            if (c.r) snprintf(t, sizeof t, "%s%s %s", opn, cnd.c_str(), reg_name((int)c.src));
            else     snprintf(t, sizeof t, "%s%s 0x%03X", opn, cnd.c_str(), c.addr);
        } else if (c.op == CTRL_HALT || c.op == CTRL_IRQ || c.op == CTRL_FLUSH) {
            snprintf(t, sizeof t, "%s%s %u", opn, cnd.c_str(), c.addr);
        } else {
            snprintf(t, sizeof t, "%s%s", opn, cnd.c_str());
        }
        s += t;
        break;
    }
    default: {   // CLASS_RR / CLASS_IMM
        const char* mn = alu_name((int)c.op);
        bool pseudo = false;
        if (!c.write && c.f && c.op == ALU_SUB) { mn = "CMP"; pseudo = true; }
        else if (!c.write && c.f && c.op == ALU_AND) { mn = "TST"; pseudo = true; }

        s += mn + cnd + " " + reg_name((int)c.dst) + ", ";
        if (c.cls == CLASS_RR) s += reg_name((int)c.src);
        else                   s += "#" + std::to_string(imm11_value(c.sx, c.imm11));

        if (!pseudo) {
            if (!c.write) s += " !w";
            if ((c.f != 0) != alu_default_f((int)c.op)) s += c.f ? " !f" : " !nf";
        }
        break;
    }
    }

    snprintf(buf, sizeof buf, "%s", s.c_str());
    return buf;
}
