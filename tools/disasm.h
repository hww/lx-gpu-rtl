// tools/disasm.h — дизассемблер LX-GPU (использует isa.h)
#pragma once
#include "isa.h"

static const char* disasm32(uint32_t w) {
    using namespace isa;
    static char buf[160];
    buf[0] = 0;

    uint32_t L     = dec_field(w, L_LO, L_HI);
    uint32_t J     = dec_field(w, J_LO, J_HI);
    uint32_t I     = dec_field(w, I_LO, I_HI);
    uint32_t COND  = dec_field(w, COND_LO, COND_HI);
    uint32_t WRITE = dec_field(w, WRITE_LO, WRITE_HI);
    uint32_t F     = dec_field(w, F_LO, F_HI);
    uint32_t DST   = dec_field(w, DST_LO, DST_HI);
    uint32_t OP    = dec_field(w, OP_LO, OP_HI);
    uint32_t SRC   = dec_field(w, SRC_LO, SRC_HI);
    uint32_t R     = dec_field(w, R_LO, R_HI);
    uint32_t SX    = dec_field(w, SX_LO, SX_HI);
    uint32_t IMM11 = dec_field(w, IMM11_LO, IMM11_HI);
    uint32_t ADDR  = dec_field(w, ADDR_LO, ADDR_HI);
    uint32_t IMM16 = dec_field(w, IMM16_LO, IMM16_HI);

    const char* lpre = (L ? "L " : "");
    const char* cnd  = cond_name(COND); // было COND_NAMES[COND]
    char csuf[8] = "";
    if (COND) snprintf(csuf, sizeof csuf, ".%s", cnd);

    // Безопасное получение имён
    auto reg = [](uint32_t idx) -> const char* {
        return reg_name(idx); // было REG_NAMES[idx]
    };

    // LDI16: J=0, I=1
    if (J == 0 && I == 1) {
        snprintf(buf, sizeof buf, "%sLDI%s %s, #0x%04X",
                lpre, csuf, reg(DST), IMM16);
        return buf;
    }

    // CTRL: J=1, I=0
    if (J == 1 && I == 0) {
        uint32_t opc = dec_field(w, CTRLOP_LO, CTRLOP_HI);
        const char* opn = ctrl_name(opc); // было CTRL_NAMES[opc]

        if (opc == CTRL_JMP || opc == CTRL_CALL) {
            if (R) {
                snprintf(buf, sizeof buf, "%s%s%s %s",
                         lpre, opn, csuf, reg(SRC));
            } else {
                snprintf(buf, sizeof buf, "%s%s%s 0x%03X",
                         lpre, opn, csuf, ADDR);
            }
            return buf;
        }
        if (opc == CTRL_HALT || opc == CTRL_IRQ) {
            snprintf(buf, sizeof buf, "%s%s%s 0x%03X",
                     lpre, opn, csuf, ADDR);
            return buf;
        }
        if (opc == CTRL_FLUSH) {
            // Показываем маску каналов в hex, например FLUSH 0x02 (канал 1)
            snprintf(buf, sizeof buf, "%s%sFLUSH 0x%X",
                     lpre, csuf, ADDR & 0xF);
            return buf;
        }
        snprintf(buf, sizeof buf, "%s%s%s", lpre, opn, csuf);
        return buf;
    }

    // REG-REG: J=0, I=0
    if (J == 0 && I == 0) {
        const char* opn = alu_name(OP); // было ALU_NAMES[OP]
        snprintf(buf, sizeof buf, "%s%s%s %s, %s%s%s",
                 lpre, opn, csuf,
                 reg(DST), reg(SRC),
                 WRITE ? "" : " !w",
                 F ? " !f" : "");
        return buf;
    }

    // REG-IMM11: J=1, I=1
    if (J == 1 && I == 1) {
        const char* opn = alu_name(OP); // было ALU_NAMES[OP]
        int32_t imm = (int32_t)IMM11;
        if (SX) imm |= ~0x7FF;
        snprintf(buf, sizeof buf, "%s%s%s %s, #%d%s%s",
                 lpre, opn, csuf,
                 reg(DST), imm,
                 WRITE ? "" : " !w",
                 F ? " !f" : "");
        return buf;
    }

    snprintf(buf, sizeof buf, "?? %08X", w);
    return buf;
}