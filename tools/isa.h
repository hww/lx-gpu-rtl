// tools/isa.h — общие определения ISA (используется asm.h и disasm.h)
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <string>
#include <vector>
#include <map>

// ---------- имена регистров (6-битный код) ----------
static const char* REG_NAMES[64] = {
    "R0","R1","R2","R3","R4","R5","R6","R7",
    "R8","R9","R10","R11","R12","R13","R14","R15",
    "R16","R17","R18","R19","R20","R21","R22","R23",
    "R24","R25","R26","R27","R28","R29","R30","R31",
    "ZERO","FLAGS","BUSY","SHADOW","LOOP.CNT","LOOP.TGT",
    "ROML","ROMH","ROMA","MHI",
    "RSV42","RSV43",
    "DATA0","DATA1","DATA2","DATA3",
    "IDX0","IDX1","IDX2","IDX3",
    "IDXH0","IDXH1","IDXH2","IDXH3",
    "STEP0","STEP1","STEP2","STEP3",
    "PAGE0","PAGE1","PAGE2","PAGE3",
};

// ---------- имена ALU (5-битный op) ----------
static const char* ALU_NAMES[32] = {
    /* 00000 */ "MOV",   /* 00001 */ "MVN",
    /* 00010 */ "AND",   /* 00011 */ "BIC",
    /* 00100 */ "OR",    /* 00101 */ "ORN",
    /* 00110 */ "XOR",   /* 00111 */ "XNOR",
    /* 01000 */ "ADD",   /* 01001 */ "ADC",
    /* 01010 */ "SUB",   /* 01011 */ "SBC",
    /* 01100 */ "ROR",   /* 01101 */ "SHR",
    /* 01110 */ "SAR",   /* 01111 */ "???",
    /* 10000 */ "???",   /* 10001 */ "???",
    /* 10010 */ "???",   /* 10011 */ "???",
    /* 10100 */ "???",   /* 10101 */ "???",
    /* 10110 */ "???",   /* 10111 */ "???",
    /* 11000 */ "MUL",   /* 11001 */ "???",
    /* 11010 */ "TST",   /* 11011 */ "???",
    /* 11100 */ "???",   /* 11101 */ "???",
    /* 11110 */ "???",   /* 11111 */ "???",
};

// ---------- имена COND ----------
static const char* COND_NAMES[16] = {
    "AL","EQ","NE","CS","CC","MI","PL","VS",
    "VC","HI","LS","GE","LT","GT","LE","??"
};

// ---------- имена CTRL ----------
static const char* CTRL_NAMES[16] = {
    "NOP","JMP","CALL","RET","HALT","IRQ","FLUSH","RSV7",
    "RSV8","RSV9","RSV10","RSV11","RSV12","RSV13","RSV14","RSV15"
};

// ---------- кодирование/декодирование полей ----------
static inline uint32_t enc_field(uint32_t val, int lo, int hi) {
    uint32_t mask = (hi - lo + 1 >= 32) ? 0xFFFFFFFFu : ((1u << (hi - lo + 1)) - 1);
    return (val & mask) << lo;
}
static inline uint32_t dec_field(uint32_t w, int lo, int hi) {
    uint32_t mask = (hi - lo + 1 >= 32) ? 0xFFFFFFFFu : ((1u << (hi - lo + 1)) - 1);
    return (w >> lo) & mask;
}

// ---------- утилиты ----------
static inline int reg_code(const std::string& s) {
    for (int i = 0; i < 64; i++)
        if (s == REG_NAMES[i]) return i;
    return -1;
}
static inline int cond_code(const std::string& s) {
    for (int i = 0; i < 15; i++)
        if (s == COND_NAMES[i]) return i;
    return -1;
}
static inline int alu_code(const std::string& s) {
    for (int i = 0; i < 32; i++)
        if (s == ALU_NAMES[i]) return i;
    return -1;
}