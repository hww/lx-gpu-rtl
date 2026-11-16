// tools/disasm.h — дизассемблер LX-GPU
#pragma once
#include "isa.h"

static const char* disasm32(uint32_t w) {
    static char buf[160];
    buf[0] = 0;

    uint32_t L     = dec_field(w, 31, 31);
    uint32_t J     = dec_field(w, 30, 30);
    uint32_t I     = dec_field(w, 29, 29);
    uint32_t COND  = dec_field(w, 28, 25);
    uint32_t WRITE = dec_field(w, 24, 24);
    uint32_t F     = dec_field(w, 23, 23);
    uint32_t DST   = dec_field(w, 22, 17);
    uint32_t OP    = dec_field(w, 16, 12);
    uint32_t SRC   = dec_field(w, 11, 6);
    uint32_t R     = dec_field(w, 16, 16);
    uint32_t SX    = dec_field(w, 11, 11);
    uint32_t IMM11 = dec_field(w, 10, 0);
    uint32_t ADDR  = dec_field(w, 11, 0);
    uint32_t IMM16 = dec_field(w, 15, 0);

    const char* lpre = (L ? "L " : "");
    const char* cnd  = (COND ? COND_NAMES[COND] : "");
    char csuffix[8] = "";
    if (COND) snprintf(csuffix, sizeof csuffix, ".%s", cnd);

    // LDI16: J=0, I=1
    if (J == 0 && I == 1) {
        snprintf(buf, sizeof buf, "%sLDI%s %s, #0x%04X",
                 lpre, csuffix, REG_NAMES[DST], IMM16);
        return buf;
    }

    // CTRL: J=1, I=0
    if (J == 1 && I == 0) {
        uint32_t opc = dec_field(w, 15, 12);
        const char* opn = CTRL_NAMES[opc];

        if (opc == 1 /*JMP*/ || opc == 2 /*CALL*/) {
            if (R) {
                snprintf(buf, sizeof buf, "%s%s%s %s",
                         lpre, opn, csuffix, REG_NAMES[SRC]);
            } else {
                snprintf(buf, sizeof buf, "%s%s%s 0x%03X",
                         lpre, opn, csuffix, ADDR);
            }
            return buf;
        }
        if (opc == 4 /*HALT*/ || opc == 5 /*IRQ*/) {
            snprintf(buf, sizeof buf, "%s%s%s 0x%03X",
                     lpre, opn, csuffix, ADDR);
            return buf;
        }
        if (opc == 6 /*FLUSH*/) {
            snprintf(buf, sizeof buf, "%s%sFLUSH CH%u",
                     lpre, csuffix, ADDR & 3);
            return buf;
        }
        snprintf(buf, sizeof buf, "%s%s%s",
                 lpre, opn, csuffix);
        return buf;
    }

    // REG-REG: J=0, I=0
    if (J == 0 && I == 0) {
        const char* opn = ALU_NAMES[OP & 0x1F];
        snprintf(buf, sizeof buf, "%s%s%s %s, %s%s%s",
                 lpre, opn, csuffix,
                 REG_NAMES[DST], REG_NAMES[SRC],
                 WRITE ? "" : " !w",
                 F ? " !f" : "");
        return buf;
    }

    // REG-IMM11: J=1, I=1
    if (J == 1 && I == 1) {
        const char* opn = ALU_NAMES[OP & 0x1F];
        int32_t imm = (int32_t)IMM11;
        if (SX) imm |= ~0x7FF;
        snprintf(buf, sizeof buf, "%s%s%s %s, #%d%s%s",
                 lpre, opn, csuffix,
                 REG_NAMES[DST], imm,
                 WRITE ? "" : " !w",
                 F ? " !f" : "");
        return buf;
    }

    snprintf(buf, sizeof buf, "?? %08X", w);
    return buf;
}