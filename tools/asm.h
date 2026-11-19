// tools/asm.h — ассемблер LX-GPU (использует только tools/isa.h)
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

// ---------- ошибки ----------
// Сообщения печатаются только во втором проходе (asm_pass == 2);
// при ошибках asm_assemble_file возвращает пустой вектор.
inline int asm_pass   = 0;
inline int asm_lineno = 0;
inline int asm_errors = 0;

static inline void asm_error(const char* msg) {
    if (asm_pass != 2) return;
    fprintf(stderr, "asm: line %d: %s\n", asm_lineno, msg);
    asm_errors++;
}

// ---------- разбор одной строки ----------
// ok = 1, если строка — инструкция; ok = 0, если пустая/метка/ошибка
static inline uint32_t asm_line(const std::string& raw,
                                std::map<std::string,int>& labels,
                                int cur_pc, int* ok) {
    using namespace isa;
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

    // ================================================================
    // CTRL-класс (J=1, I=0)
    // ================================================================
    auto enc_ctrl = [&](uint32_t op, uint32_t addr_or_src, uint32_t r_bit) {
        return enc_field(L, L_LO, L_HI)
             | enc_field(1, J_LO, J_HI)
             | enc_field(0, I_LO, I_HI)
             | enc_field(cond, COND_LO, COND_HI)
             | enc_field(0, WRITE_LO, WRITE_HI)
             | enc_field(0, F_LO, F_HI)
             | enc_field(0, DST_LO, DST_HI)
             | enc_field(r_bit, R_LO, R_HI)
             | enc_field(op, CTRLOP_LO, CTRLOP_HI)
             | enc_field(addr_or_src, ADDR_LO, ADDR_HI);
    };

    if (mn == "JMP" || mn == "CALL") {
        if (args.size() != 1) return 0;
        // L на CALL запрещён: CALL кладёт в SHADOW адрес после тела цикла,
        // и после RET цикл не повторяется, а заканчивается. L допустим на
        // JMP/RET/HALT (например, L JMP.EQ exit — выход из цикла по условию).
        if (L && mn == "CALL")
            asm_error("L is not allowed on CALL (after RET the loop ends, it does "
                      "not repeat); put L on a data word after the CALL");
        uint32_t op = (mn == "JMP") ? isa::CTRL_JMP : isa::CTRL_CALL;
        int rr = reg_code(args[0]);
        if (rr >= 0) {
            // JMP/CALL по регистру: R=1, SRC в [11:6]
            uint32_t w = enc_field(L, L_LO, L_HI)
                       | enc_field(1, J_LO, J_HI)
                       | enc_field(0, I_LO, I_HI)
                       | enc_field(cond, COND_LO, COND_HI)
                       | enc_field(0, WRITE_LO, WRITE_HI)
                       | enc_field(0, F_LO, F_HI)
                       | enc_field(0, DST_LO, DST_HI)
                       | enc_field(1, R_LO, R_HI)
                       | enc_field(op, CTRLOP_LO, CTRLOP_HI)
                       | enc_field((uint32_t)rr, SRC_LO, SRC_HI);
            *ok = 1; return w;
        }
        uint32_t addr = labels.count(args[0])
                      ? (uint32_t)labels[args[0]]
                      : (uint32_t)asm_parse_int(args[0]);
        *ok = 1;
        return enc_ctrl(op, addr & 0xFFF, 0);
    }

    if (mn == "HALT" || mn == "IRQ" || mn == "FLUSH") {
        uint32_t op = (mn == "HALT")  ? isa::CTRL_HALT
                    : (mn == "IRQ")   ? isa::CTRL_IRQ
                                      : isa::CTRL_FLUSH;
        uint32_t code = 0;
        if (!args.empty()) {
            if (labels.count(args[0])) {
                code = (uint32_t)labels[args[0]];
            } else {
                char* endptr;
                long val = strtol(args[0].c_str(), &endptr, 0);
                if (*endptr != '\0') {
                    fprintf(stderr, "asm: invalid argument '%s' for %s (must be a number or label)\n", 
                            args[0].c_str(), mn.c_str());
                    return 0;
                }
                code = (uint32_t)val;
            }
        }
        *ok = 1;
        return enc_ctrl(op, code & 0xFFF, 0);
    }

    if (mn == "RET" || mn == "NOP") {
        uint32_t op = (mn == "RET") ? isa::CTRL_RET : isa::CTRL_NOP;
        *ok = 1;
        return enc_ctrl(op, 0, 0);
    }

    // ================================================================
    // LDI-класс (J=0, I=1): LDI dst, #imm16
    //   RTL принудительно ставит WRITE=1, F=0 для LDI
    // ================================================================
    if (mn == "LDI") {
        if (args.size() != 2) return 0;
        int dst = reg_code(args[0]);
        if (dst < 0 || args[1].empty() || args[1][0] != '#') return 0;
        uint32_t imm = (uint32_t)asm_parse_int(args[1].substr(1)) & 0xFFFF;
        uint32_t w = enc_field(L, L_LO, L_HI)
                   | enc_field(0, J_LO, J_HI)
                   | enc_field(1, I_LO, I_HI)
                   | enc_field(cond, COND_LO, COND_HI)
                   | enc_field(1, WRITE_LO, WRITE_HI)
                   | enc_field(0, F_LO, F_HI)
                   | enc_field((uint32_t)dst, DST_LO, DST_HI)
                   | enc_field(imm, IMM16_LO, IMM16_HI);
        *ok = 1; return w;
    }

    // ================================================================
    // ALU-класс: имя операции ищем в ALU_NAMES (единственный источник)
    // ================================================================
    int op_idx = alu_code(mn);
    if (op_idx < 0) return 0;

    uint32_t write = 1, f = 0;
    if (mn == "TST" || mn == "CMP") write = 0;
    if (mn == "AND" || mn == "BIC" || mn == "OR"  || mn == "ORN" ||
        mn == "XOR" || mn == "XNOR"|| mn == "ADD" || mn == "ADC" ||
        mn == "SUB" || mn == "SBC" || mn == "ROR" || mn == "SHR" ||
        mn == "SAR" || mn == "MUL" || mn == "TST" || mn == "CMP")
        f = 1;

    if (args.size() != 2) return 0;
    int dst = reg_code(args[0]);
    if (dst < 0) return 0;
    if (args[1].empty()) return 0;

    if (args[1][0] == '#') {
        // REG-IMM11 (J=1, I=1)
        long val = asm_parse_int(args[1].substr(1));
        if (val < -1024 || val > 1023) {
            fprintf(stderr, "asm: imm11 %ld out of range\n", val);
            return 0;
        }
        uint32_t sx  = (val < 0) ? 1u : 0u;
        uint32_t imm = (uint32_t)(val & 0x7FF);
        uint32_t w = enc_field(L, L_LO, L_HI)
                   | enc_field(1, J_LO, J_HI)
                   | enc_field(1, I_LO, I_HI)
                   | enc_field(cond, COND_LO, COND_HI)
                   | enc_field(write, WRITE_LO, WRITE_HI)
                   | enc_field(f, F_LO, F_HI)
                   | enc_field((uint32_t)dst, DST_LO, DST_HI)
                   | enc_field((uint32_t)op_idx, OP_LO, OP_HI)
                   | enc_field(sx, SX_LO, SX_HI)
                   | enc_field(imm, IMM11_LO, IMM11_HI);
        *ok = 1; return w;
    } else {
        // REG-REG (J=0, I=0)
        int src = reg_code(args[1]);
        if (src < 0) return 0;
        uint32_t w = enc_field(L, L_LO, L_HI)
                   | enc_field(0, J_LO, J_HI)
                   | enc_field(0, I_LO, I_HI)
                   | enc_field(cond, COND_LO, COND_HI)
                   | enc_field(write, WRITE_LO, WRITE_HI)
                   | enc_field(f, F_LO, F_HI)
                   | enc_field((uint32_t)dst, DST_LO, DST_HI)
                   | enc_field((uint32_t)op_idx, OP_LO, OP_HI)
                   | enc_field((uint32_t)src, SRC_LO, SRC_HI);
        *ok = 1; return w;
    }
}

// ---------- сборка массива строк в массив слов (2 прохода) ----------
static inline std::vector<uint32_t> asm_assemble(const std::vector<std::string>& lines) {
    std::map<std::string,int> labels;
    std::vector<uint32_t> words;
    asm_errors = 0;

    // ---- проход 1: метки + .org ----
    asm_pass = 1;
    int pc = 0;
    for (auto& raw : lines) {
        std::string line = asm_trim(asm_strip_comment(raw));
        if (line.empty()) continue;

        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string label = asm_trim(line.substr(0, colon));
            labels[label] = pc;
            line = asm_trim(line.substr(colon + 1));
            if (line.empty()) continue;
        }

        if (line.rfind(".org", 0) == 0 || line.rfind(".ORG", 0) == 0) {
            pc = (int)asm_parse_int(asm_trim(line.substr(4)));
            continue;
        }

        if (line.rfind(".word", 0) == 0 || line.rfind(".WORD", 0) == 0) {
            std::string arg = asm_trim(line.substr(5));
            std::string cur;
            for (char c : arg) {
                if (c == ',') { pc++; cur.clear(); }
                else cur += c;
            }
            if (!cur.empty()) pc++;
            continue;
        }

        int ok = 0;
        asm_line(line, labels, pc, &ok);
        if (ok) pc++;
    }

    // ---- проход 2: код + .org ----
    asm_pass = 2;
    asm_lineno = 0;
    for (auto& raw : lines) {
        asm_lineno++;
        std::string line = asm_trim(asm_strip_comment(raw));
        if (line.empty()) continue;

        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            line = asm_trim(line.substr(colon + 1));
            if (line.empty()) continue;
        }

        if (line.rfind(".org", 0) == 0 || line.rfind(".ORG", 0) == 0) {
            int target = (int)asm_parse_int(asm_trim(line.substr(4)));
            while (words.size() < (size_t)target) words.push_back(0);
            continue;
        }

        if (line.rfind(".word", 0) == 0 || line.rfind(".WORD", 0) == 0) {
            std::string arg = asm_trim(line.substr(5));
            std::string cur;
            for (char c : arg) {
                if (c == ',') {
                    words.push_back((uint32_t)asm_parse_int(cur));
                    cur.clear();
                } else cur += c;
            }
            if (!cur.empty()) words.push_back((uint32_t)asm_parse_int(cur));
            continue;
        }

        int ok = 0;
        uint32_t w = asm_line(line, labels, (int)words.size(), &ok);
        if (ok) words.push_back(w);
    }

    return words;
}

// ---------- .asm файл -> слова ----------
static inline std::vector<uint32_t> asm_assemble_file(const char* path, uint16_t* out_org = nullptr) {
    std::vector<std::string> lines;
    FILE* fp = fopen(path, "r");
    if (!fp) { fprintf(stderr, "asm: cannot open %s\n", path); return {}; }
    char buf[512];
    while (fgets(buf, sizeof buf, fp)) lines.push_back(buf);
    fclose(fp);

    int initial_org = -1;
    for (auto& raw : lines) {
        std::string line = asm_trim(asm_strip_comment(raw));
        if (line.empty()) continue;
        if (line.rfind(".org", 0) == 0 || line.rfind(".ORG", 0) == 0)
            initial_org = (int)asm_parse_int(asm_trim(line.substr(4)));
        break;
    }

    auto words = asm_assemble(lines);
    asm_pass = 0;
    if (asm_errors) {
        fprintf(stderr, "asm: %d error(s) in %s\n", asm_errors, path);
        return {};
    }

    if (initial_org > 0 && (size_t)initial_org <= words.size()) {
        if (out_org) *out_org = (uint16_t)initial_org;
        words.erase(words.begin(), words.begin() + initial_org);
    }
    return words;
}