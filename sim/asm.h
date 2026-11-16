// tools/asm.h — ассемблер LX-GPU
#pragma once
#include "isa.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <map>

// ---------- утилиты ----------
static inline std::string asm_trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static inline std::string asm_strip_comment(const std::string& s) {
    size_t p = s.find(';');
    return (p == std::string::npos) ? s : s.substr(0, p);
}

static inline long asm_parse_int(const std::string& s) {
    std::string t = asm_trim(s);
    bool neg = !t.empty() && t[0] == '-';
    if (neg) t = t.substr(1);
    long v = strtol(t.c_str(), nullptr, 0);
    return neg ? -v : v;
}

// ---------- разбор одной строки ----------
// ok = 1, если строка — инструкция; ok = 0, если пустая/метка/ошибка
static inline uint32_t asm_line(const std::string& raw,
                                std::map<std::string,int>& labels,
                                int cur_pc, int* ok) {
    *ok = 0;
    std::string line = asm_trim(asm_strip_comment(raw));
    if (line.empty()) return 0;

    // метка
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
        std::string label = asm_trim(line.substr(0, colon));
        labels[label] = cur_pc;
        line = asm_trim(line.substr(colon + 1));
        if (line.empty()) return 0;
    }

    // L-префикс
    uint32_t L = 0;
    if (line.size() > 1 && (line[0] == 'L' || line[0] == 'l') && isspace((unsigned char)line[1])) {
        L = 1;
        line = asm_trim(line.substr(1));
    }

    // mnemonic[.cond]
    std::string mn, rest;
    size_t sp = line.find_first_of(" \t");
    if (sp == std::string::npos) { mn = line; rest = ""; }
    else { mn = line.substr(0, sp); rest = asm_trim(line.substr(sp + 1)); }

    uint32_t cond = 0;
    size_t dot = mn.find('.');
    if (dot != std::string::npos) {
        std::string cnd = mn.substr(dot + 1);
        mn = mn.substr(0, dot);
        int cc = cond_code(cnd);
        if (cc < 0) { fprintf(stderr, "asm: unknown cond %s\n", cnd.c_str()); return 0; }
        cond = (uint32_t)cc;
    }

    // аргументы
    std::vector<std::string> args;
    if (!rest.empty()) {
        std::string cur;
        for (char c : rest) {
            if (c == ',') { args.push_back(asm_trim(cur)); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) args.push_back(asm_trim(cur));
    }

    // ---- .word ----
    if (mn == ".word" && args.size() == 1) {
        *ok = 1;
        return (uint32_t)asm_parse_int(args[0]);
    }

    // ---- CTRL ----
    if (mn == "JMP" || mn == "CALL") {
        if (args.size() != 1) return 0;
        int rr = reg_code(args[0]);
        uint32_t op = (mn == "JMP") ? 1 : 2;
        if (rr >= 0) {
            uint32_t w = enc_field(L,31,31) | enc_field(1,30,30) | enc_field(0,29,29)
                       | enc_field(cond,28,25) | enc_field(0,24,24) | enc_field(0,23,23)
                       | enc_field(0,22,17) | enc_field(1,16,16)
                       | enc_field(op,15,12) | enc_field((uint32_t)rr,11,6);
            *ok = 1; return w;
        }
        uint32_t addr;
        if (labels.count(args[0])) addr = (uint32_t)labels[args[0]];
        else addr = (uint32_t)asm_parse_int(args[0]);
        uint32_t w = enc_field(L,31,31) | enc_field(1,30,30) | enc_field(0,29,29)
                   | enc_field(cond,28,25) | enc_field(0,24,24) | enc_field(0,23,23)
                   | enc_field(0,22,17) | enc_field(0,16,16)
                   | enc_field(op,15,12) | enc_field(addr,11,0);
        *ok = 1; return w;
    }
    if (mn == "HALT" || mn == "IRQ" || mn == "FLUSH") {
        uint32_t op = (mn == "HALT") ? 4 : (mn == "IRQ") ? 5 : 6;
        uint32_t code = 0;
        if (!args.empty()) {
            if (labels.count(args[0])) code = (uint32_t)labels[args[0]];
            else code = (uint32_t)asm_parse_int(args[0]);
        }
        uint32_t w = enc_field(L,31,31) | enc_field(1,30,30) | enc_field(0,29,29)
                   | enc_field(cond,28,25) | enc_field(0,24,24) | enc_field(0,23,23)
                   | enc_field(0,22,17) | enc_field(0,16,16)
                   | enc_field(op,15,12) | enc_field(code,11,0);
        *ok = 1; return w;
    }
    if (mn == "RET" || mn == "NOP") {
        uint32_t op = (mn == "RET") ? 3 : 0;
        uint32_t w = enc_field(L,31,31) | enc_field(1,30,30) | enc_field(0,29,29)
                   | enc_field(cond,28,25) | enc_field(0,24,24) | enc_field(0,23,23)
                   | enc_field(0,22,17) | enc_field(0,16,16)
                   | enc_field(op,15,12);
        *ok = 1; return w;
    }

    // ---- LDI ----
    if (mn == "LDI") {
        if (args.size() != 2) return 0;
        int dst = reg_code(args[0]);
        if (dst < 0 || args[1].empty() || args[1][0] != '#') return 0;
        uint32_t imm = (uint32_t)asm_parse_int(args[1].substr(1));
        uint32_t w = enc_field(L,31,31) | enc_field(0,30,30) | enc_field(1,29,29)
                   | enc_field(cond,28,25) | enc_field(1,24,24) | enc_field(0,23,23)
                   | enc_field((uint32_t)dst,22,17)
                   | enc_field(imm,15,0);
        *ok = 1; return w;
    }

    // ---- ALU ----
    uint32_t op = 0;
    uint32_t write = 1, f = 0;
    if (mn == "MOV")       { op = 0x00; f = 0; }
    else if (mn == "MVN")  { op = 0x01; f = 0; }
    else if (mn == "AND")  { op = 0x02; f = 1; }
    else if (mn == "BIC")  { op = 0x03; f = 1; }
    else if (mn == "OR")   { op = 0x04; f = 1; }
    else if (mn == "ORN")  { op = 0x05; f = 1; }
    else if (mn == "XOR")  { op = 0x06; f = 1; }
    else if (mn == "XNOR") { op = 0x07; f = 1; }
    else if (mn == "ADD")  { op = 0x08; f = 1; }
    else if (mn == "ADC")  { op = 0x09; f = 1; }
    else if (mn == "SUB")  { op = 0x0A; f = 1; }
    else if (mn == "SBC")  { op = 0x0B; f = 1; }
    else if (mn == "ROR")  { op = 0x0C; f = 1; }
    else if (mn == "SHR")  { op = 0x0D; f = 1; }
    else if (mn == "SAR")  { op = 0x0E; f = 1; }
    else if (mn == "MUL")  { op = 0x18; f = 1; }
    else if (mn == "TST")  { op = 0x1A; f = 1; write = 0; }
    else if (mn == "CMP")  { op = 0x1B; f = 1; write = 0; }
    else return 0;

    if (args.size() != 2) return 0;
    int dst = reg_code(args[0]);
    if (dst < 0) return 0;

    if (args[1].empty()) return 0;
    if (args[1][0] == '#') {
        long val = asm_parse_int(args[1].substr(1));
        if (val < -1024 || val > 1023) {
            fprintf(stderr, "asm: imm11 %ld out of range\n", val);
            return 0;
        }
        uint32_t sx = (val < 0) ? 1 : 0;
        uint32_t imm = (uint32_t)(val & 0x7FF);
        uint32_t w = enc_field(L,31,31) | enc_field(1,30,30) | enc_field(1,29,29)
                   | enc_field(cond,28,25) | enc_field(write,24,24) | enc_field(f,23,23)
                   | enc_field((uint32_t)dst,22,17) | enc_field(op,16,12)
                   | enc_field(sx,11,11) | enc_field(imm,10,0);
        *ok = 1; return w;
    } else {
        int src = reg_code(args[1]);
        if (src < 0) return 0;
        uint32_t w = enc_field(L,31,31) | enc_field(0,30,30) | enc_field(0,29,29)
                   | enc_field(cond,28,25) | enc_field(write,24,24) | enc_field(f,23,23)
                   | enc_field((uint32_t)dst,22,17) | enc_field(op,16,12)
                   | enc_field((uint32_t)src,11,6);
        *ok = 1; return w;
    }
}

// ---------- сборка массива строк в массив слов (2 прохода: метки, затем инструкции) ----------
static inline std::vector<uint32_t> asm_assemble(const std::vector<std::string>& lines) {
    std::map<std::string,int> labels;
    // проход 1: метки
    int pc = 0;
    for (auto& l : lines) {
        int ok = 0;
        asm_line(l, labels, pc, &ok);
        if (ok) pc++;
    }
    // проход 2: инструкции
    std::vector<uint32_t> words;
    for (auto& l : lines) {
        int ok = 0;
        uint32_t w = asm_line(l, labels, (int)words.size(), &ok);
        if (ok) words.push_back(w);
    }
    return words;
}

// ---------- вспомогательное: собрать .asm файл в слова ----------
static inline std::vector<uint32_t> asm_assemble_file(const char* path) {
    std::vector<std::string> lines;
    FILE* fp = fopen(path, "r");
    if (!fp) { fprintf(stderr, "asm: cannot open %s\n", path); return {}; }
    char buf[512];
    while (fgets(buf, sizeof buf, fp)) lines.push_back(buf);
    fclose(fp);
    return asm_assemble(lines);
}