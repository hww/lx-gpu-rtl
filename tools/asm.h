// tools/asm.h — LX-GPU assembler (depends only on tools/isa.h).
//
// Source syntax (case-insensitive except for labels):
//   [label:] [L] MNEMONIC[.COND][.L] operands [!w] [!f] [!nf]   ; comment
//   MOV  Rd, Rs | #imm        reg-reg / reg-imm11 (imm: -1024..2047)
//   ADD/SUB/ADC/SBC/AND/BIC/OR/ORN/XOR/XNOR/MVN/ROR/SHR/SAR/MUL  (same forms)
//   CMP, TST                  pseudo: SUB / AND with !w and F=1
//   LDI  Rd, #imm16           (-32768..65535)
//   JMP  label|addr|Rn        CALL label|addr     RET   NOP
//   HALT [0..127]   IRQ [0..255]   FLUSH 0..3 | CH0..CH3
//   .org N   .word v[, v...]
// Modifiers: !w  do not write the result,  !f  force F=1,  !nf  force F=0.
// Default F: set for every ALU op except MOV/MVN (isa::alu_default_f).
// Numbers: decimal, 0x.., 0b..; an immediate may also be a label (its address).
//
// Errors are reported as "file:line: error: message" followed by the source
// line. On any error the assemble functions return an empty vector.
#pragma once
#include "isa.h"
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// ---------- string utils ----------
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

static inline std::string asm_upper(const std::string& s) { return isa::to_upper(s); }

static inline std::vector<std::string> asm_split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == sep) { out.push_back(cur); cur.clear(); }
        else cur += ch;
    }
    out.push_back(cur);
    return out;
}

// Strict number parser: [+-] (0x hex | 0b binary | decimal). A leading zero
// does NOT mean octal. Returns false on any stray character.
static inline bool asm_parse_num(const std::string& str, long long& out) {
    std::string t = asm_trim(str);
    size_t i = 0;
    bool neg = false;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) { neg = (t[i] == '-'); i++; }
    if (i >= t.size()) return false;
    int base = 10;
    if (t[i] == '0' && i + 1 < t.size() && (t[i + 1] == 'x' || t[i + 1] == 'X')) { base = 16; i += 2; }
    else if (t[i] == '0' && i + 1 < t.size() && (t[i + 1] == 'b' || t[i + 1] == 'B')) { base = 2; i += 2; }
    if (i >= t.size()) return false;
    unsigned long long v = 0;
    for (; i < t.size(); i++) {
        char ch = t[i];
        int d;
        if (ch >= '0' && ch <= '9')      d = ch - '0';
        else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
        else return false;
        if (d >= base) return false;
        if (v > (1ULL << 40)) return false;          // far beyond any field
        v = v * (unsigned)base + (unsigned)d;
    }
    out = neg ? -(long long)v : (long long)v;
    return true;
}

// Legacy helper: 0 on error.
static inline long asm_parse_int(const std::string& s) {
    long long v = 0;
    return asm_parse_num(s, v) ? (long)v : 0;
}

// ---------- diagnostics ----------
struct AsmCtx {
    const char*                     file  = "<asm>";
    FILE*                           err   = stderr;     // nullptr = silent
    const std::vector<std::string>* lines = nullptr;
    int                             cur_line = 0;
    int                             errors   = 0;
    std::map<std::string, int>      labels;
    std::map<std::string, int>      label_line;
};

static inline void asm_error(AsmCtx& cx, const char* fmt, ...) {
    cx.errors++;
    if (!cx.err) return;
    char msg[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fprintf(cx.err, "%s:%d: error: %s\n", cx.file, cx.cur_line, msg);
    if (cx.lines && cx.cur_line >= 1 && cx.cur_line <= (int)cx.lines->size()) {
        std::string s = asm_trim(asm_strip_comment((*cx.lines)[cx.cur_line - 1]));
        fprintf(cx.err, "    | %s\n", s.c_str());
    }
}

// ---------- labels ----------
static inline bool asm_is_ident(const std::string& s) {
    if (s.empty()) return false;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (char ch : s)
        if (!(isalnum((unsigned char)ch) || ch == '_')) return false;
    return true;
}

// Strips the comment and any leading "label:" prefixes. bad = a ':' was found
// that does not follow an identifier.
static inline std::string asm_peel_labels(const std::string& raw,
                                          std::vector<std::string>& labs, bool& bad) {
    std::string line = asm_trim(asm_strip_comment(raw));
    for (;;) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) break;
        std::string name = asm_trim(line.substr(0, colon));
        if (!asm_is_ident(name)) { bad = true; break; }
        labs.push_back(name);
        line = asm_trim(line.substr(colon + 1));
    }
    return line;
}

// number or label -> value
static inline bool asm_eval(AsmCtx& cx, const std::string& tok, long long& v) {
    std::string t = asm_trim(tok);
    if (t.empty()) { asm_error(cx, "missing value"); return false; }
    if (asm_parse_num(t, v)) return true;
    if (asm_is_ident(t)) {
        auto it = cx.labels.find(t);
        if (it == cx.labels.end()) { asm_error(cx, "undefined label '%s'", t.c_str()); return false; }
        v = it->second;
        return true;
    }
    asm_error(cx, "bad number '%s'", t.c_str());
    return false;
}

// ---------- one instruction ----------
static inline bool asm_insn(AsmCtx& cx, const std::string& body_in, uint32_t& out) {
    using namespace isa;
    Insn c;
    std::string body = body_in;

    // "L" prefix = loop bit
    {
        size_t sp = body.find_first_of(" \t");
        if (sp != std::string::npos) {
            std::string first = body.substr(0, sp);
            if (first == "L" || first == "l") { c.l = 1; body = asm_trim(body.substr(sp + 1)); }
        }
    }

    // MNEMONIC[.COND][.L]  operands
    size_t sp = body.find_first_of(" \t");
    std::string head = (sp == std::string::npos) ? body : body.substr(0, sp);
    std::string rest = (sp == std::string::npos) ? "" : asm_trim(body.substr(sp + 1));

    std::vector<std::string> parts = asm_split(head, '.');
    std::string mn = asm_upper(parts[0]);
    if (mn.empty()) { asm_error(cx, "missing mnemonic"); return false; }

    bool have_cond = false, ok = true;
    for (size_t k = 1; k < parts.size(); k++) {
        std::string s = asm_upper(parts[k]);
        if (s == "L") {
            if (c.l) { asm_error(cx, "loop bit given twice"); ok = false; }
            c.l = 1;
        } else {
            int cc = cond_code(s);
            if (cc < 0)          { asm_error(cx, "unknown condition '%s'", parts[k].c_str()); ok = false; }
            else if (have_cond)  { asm_error(cx, "condition given twice"); ok = false; }
            else                 { c.cond = (uint32_t)cc; have_cond = true; }
        }
    }
    if (!ok) return false;

    // operands and trailing modifiers
    std::vector<std::string> args, mods;
    if (!rest.empty()) {
        args = asm_split(rest, ',');
        for (auto& a : args) a = asm_trim(a);
        std::string& last = args.back();
        size_t ws = last.find_first_of(" \t");
        if (ws != std::string::npos) {
            std::string tail = asm_trim(last.substr(ws + 1));
            last = last.substr(0, ws);
            size_t p = 0;
            while (p != std::string::npos && p < tail.size()) {
                size_t q = tail.find_first_of(" \t", p);
                if (q == std::string::npos) q = tail.size();
                mods.push_back(asm_upper(tail.substr(p, q - p)));
                p = tail.find_first_not_of(" \t", q);
            }
        }
        for (auto& a : args) {
            if (a.empty()) { asm_error(cx, "empty operand"); return false; }
            if (a.find_first_of(" \t") != std::string::npos) {
                asm_error(cx, "unexpected text in operand '%s'", a.c_str());
                return false;
            }
        }
    }

    auto need = [&](size_t n) -> bool {
        if (args.size() != n) {
            asm_error(cx, "%s expects %zu operand(s), got %zu", mn.c_str(), n, args.size());
            return false;
        }
        return true;
    };
    auto no_mods = [&]() -> bool {
        if (!mods.empty()) { asm_error(cx, "modifiers are not allowed for %s", mn.c_str()); return false; }
        return true;
    };
    auto reg = [&](const std::string& tok, uint32_t& code) -> bool {
        int r = reg_code(asm_upper(tok));
        if (r < 0) { asm_error(cx, "unknown register '%s'", tok.c_str()); return false; }
        code = (uint32_t)r;
        return true;
    };

    if (mn == "NOP" || mn == "RET") {
        if (!need(0) || !no_mods()) return false;
        c.cls = CLASS_CTRL;
        c.op  = (mn == "NOP") ? (uint32_t)CTRL_NOP : (uint32_t)CTRL_RET;

    } else if (mn == "JMP" || mn == "CALL") {
        if (!need(1) || !no_mods()) return false;
        c.cls = CLASS_CTRL;
        c.op  = (mn == "JMP") ? (uint32_t)CTRL_JMP : (uint32_t)CTRL_CALL;
        int rr = reg_code(asm_upper(args[0]));
        if (rr >= 0) {
            c.r = 1; c.src = (uint32_t)rr;
        } else {
            long long v;
            if (!asm_eval(cx, args[0], v)) return false;
            if (v < 0 || v >= ROM_WORDS) {
                asm_error(cx, "jump target %lld is outside the ROM (0..%d)", v, ROM_WORDS - 1);
                return false;
            }
            c.addr = (uint32_t)v;
        }

    } else if (mn == "HALT" || mn == "IRQ") {
        if (args.size() > 1 || !no_mods()) { asm_error(cx, "%s takes at most one number", mn.c_str()); return false; }
        long long v = 0;
        if (args.size() == 1 && !asm_parse_num(args[0], v)) {
            asm_error(cx, "%s expects a number, got '%s'", mn.c_str(), args[0].c_str());
            return false;
        }
        if (v < 0) { asm_error(cx, "%s code must not be negative", mn.c_str()); return false; }
        c.cls  = CLASS_CTRL;
        c.op   = (mn == "HALT") ? (uint32_t)CTRL_HALT : (uint32_t)CTRL_IRQ;
        c.addr = (uint32_t)(v > 0xFFFFF ? 0xFFFFF : v);    // validate() reports the range

    } else if (mn == "FLUSH") {
        if (!need(1) || !no_mods()) return false;
        std::string u = asm_upper(args[0]);
        long long v;
        bool good = (u.size() > 2 && u[0] == 'C' && u[1] == 'H') ? asm_parse_num(u.substr(2), v)
                                                                  : asm_parse_num(u, v);
        if (!good || v < 0) {
            asm_error(cx, "FLUSH expects a channel number 0..%d or CH0..CH%d, got '%s'",
                      NUM_CHANNELS - 1, NUM_CHANNELS - 1, args[0].c_str());
            return false;
        }
        c.cls  = CLASS_CTRL;
        c.op   = CTRL_FLUSH;
        c.addr = (uint32_t)(v > 0xFFFFF ? 0xFFFFF : v);

    } else if (mn == "LDI") {
        if (!need(2) || !no_mods()) return false;
        uint32_t dst;
        if (!reg(args[0], dst)) return false;
        if (args[1][0] != '#') { asm_error(cx, "LDI needs an immediate: LDI Rn, #value"); return false; }
        long long v;
        if (!asm_eval(cx, args[1].substr(1), v)) return false;
        if (v < IMM16_MIN || v > IMM16_MAX) {
            asm_error(cx, "constant %lld does not fit 16 bits (%d..%d)", v, IMM16_MIN, IMM16_MAX);
            return false;
        }
        c.cls = CLASS_LDI; c.write = 1; c.f = 0; c.dst = dst;
        c.imm16 = (uint32_t)(v & 0xFFFF);

    } else {
        // ALU, including the CMP / TST pseudo-ops
        bool is_cmp = (mn == "CMP"), is_tst = (mn == "TST");
        int op = is_cmp ? (int)ALU_SUB : is_tst ? (int)ALU_AND : alu_code(mn);
        if (op < 0) { asm_error(cx, "unknown mnemonic '%s'", mn.c_str()); return false; }
        if (!need(2)) return false;
        uint32_t dst;
        if (!reg(args[0], dst)) return false;
        c.op    = (uint32_t)op;
        c.dst   = dst;
        c.write = (is_cmp || is_tst) ? 0 : 1;
        c.f     = (is_cmp || is_tst || alu_default_f(op)) ? 1 : 0;

        bool f_set = false, nf_set = false;
        for (auto& m : mods) {
            if      (m == "!W")  c.write = 0;
            else if (m == "!F")  f_set = true;
            else if (m == "!NF") nf_set = true;
            else {
                asm_error(cx, "unknown modifier '%s' (expected !w, !f or !nf)", m.c_str());
                return false;
            }
        }
        if (f_set && nf_set) { asm_error(cx, "!f and !nf together"); return false; }
        if (f_set) c.f = 1;
        if (nf_set) {
            if (is_cmp || is_tst) { asm_error(cx, "%s always updates the flags", mn.c_str()); return false; }
            c.f = 0;
        }

        if (args[1][0] == '#') {
            long long v;
            if (!asm_eval(cx, args[1].substr(1), v)) return false;
            if (v < IMM11_MIN || v > IMM11_MAX) {
                asm_error(cx, "constant %lld does not fit the 11-bit immediate (%d..%d); use LDI for 16-bit constants",
                          v, IMM11_MIN, IMM11_MAX);
                return false;
            }
            c.cls = CLASS_IMM;
            if (v < 0) { c.sx = 1; c.imm11 = (uint32_t)(v & 0x7FF); }
            else       { c.sx = 0; c.imm11 = (uint32_t)v; }
        } else {
            uint32_t src;
            if (!reg(args[1], src)) return false;
            c.cls = CLASS_RR; c.src = src;
        }
    }

    std::string why;
    if (!validate(c, &why)) { asm_error(cx, "%s", why.c_str()); return false; }
    out = encode(c);
    return true;
}

// ---------- whole program ----------
struct AsmResult {
    std::vector<uint32_t> words;     // starts at address 0 (padded by .org)
    int                   first_org = -1;   // value of a leading .org, -1 if none
    bool                  ok = false;
};

struct AsmLine {
    enum Kind { INSN, WORD, ORG } kind = INSN;
    int         no = 0;
    int         pc = 0;
    long long   org = 0;
    std::string body;
};

// err = nullptr silences the diagnostics (used by tests).
static inline bool asm_assemble_ex(const std::vector<std::string>& lines, const char* name,
                                   AsmResult& res, FILE* err = stderr) {
    AsmCtx cx;
    cx.file  = name;
    cx.err   = err;
    cx.lines = &lines;
    res = AsmResult();

    std::vector<AsmLine> prog;
    int pc = 0;
    bool rom_reported = false;

    // ---- pass 1: labels, directives, addresses ----
    for (size_t n = 0; n < lines.size(); n++) {
        cx.cur_line = (int)n + 1;
        std::vector<std::string> labs;
        bool bad = false;
        std::string body = asm_peel_labels(lines[n], labs, bad);
        if (bad) asm_error(cx, "':' must follow a label name");

        for (auto& l : labs) {
            if (isa::reg_code(asm_upper(l)) >= 0 || isa::alu_code(asm_upper(l)) >= 0) {
                asm_error(cx, "label '%s' clashes with a register or mnemonic", l.c_str());
            } else if (cx.labels.count(l)) {
                asm_error(cx, "label '%s' already defined at line %d", l.c_str(), cx.label_line[l]);
            } else {
                cx.labels[l]     = pc;
                cx.label_line[l] = cx.cur_line;
            }
        }
        if (bad || body.empty()) continue;

        AsmLine L;
        L.no = cx.cur_line; L.pc = pc; L.body = body;

        if (body[0] == '.') {
            size_t sp = body.find_first_of(" \t");
            std::string dir = asm_upper(sp == std::string::npos ? body : body.substr(0, sp));
            std::string arg = sp == std::string::npos ? "" : asm_trim(body.substr(sp + 1));
            if (dir == ".ORG") {
                long long v;
                if (!asm_parse_num(arg, v) || v < 0 || v >= isa::ROM_WORDS)
                    asm_error(cx, ".org expects a number 0..%d", isa::ROM_WORDS - 1);
                else if (v < pc)
                    asm_error(cx, "backwards .org 0x%llX (current address is 0x%X)", v, pc);
                else { L.kind = AsmLine::ORG; L.org = v; pc = (int)v; prog.push_back(L); }
            } else if (dir == ".WORD") {
                if (arg.empty()) asm_error(cx, ".word needs at least one value");
                else { L.kind = AsmLine::WORD; pc += (int)asm_split(arg, ',').size(); prog.push_back(L); }
            } else {
                asm_error(cx, "unknown directive '%s'", dir.c_str());
            }
        } else {
            L.kind = AsmLine::INSN;
            pc += 1;
            prog.push_back(L);
        }
        if (pc > isa::ROM_WORDS && !rom_reported) {
            asm_error(cx, "program does not fit the %d-word ROM", isa::ROM_WORDS);
            rom_reported = true;
        }
    }

    // ---- pass 2: encode ----
    bool seen_content = false;
    for (auto& L : prog) {
        cx.cur_line = L.no;
        switch (L.kind) {
        case AsmLine::ORG:
            if (!seen_content) res.first_org = (int)L.org;
            seen_content = true;
            while (res.words.size() < (size_t)L.org) res.words.push_back(0);
            break;
        case AsmLine::WORD: {
            seen_content = true;
            size_t sp = L.body.find_first_of(" \t");
            for (auto& item : asm_split(asm_trim(L.body.substr(sp)), ',')) {
                long long v;
                if (asm_trim(item).empty()) { asm_error(cx, "empty value in .word"); res.words.push_back(0); continue; }
                if (!asm_eval(cx, item, v)) { res.words.push_back(0); continue; }
                if (v < -2147483648LL || v > 4294967295LL) {
                    asm_error(cx, ".word value %lld does not fit 32 bits", v);
                    res.words.push_back(0);
                    continue;
                }
                res.words.push_back((uint32_t)(v & 0xFFFFFFFFLL));
            }
            break;
        }
        case AsmLine::INSN: {
            seen_content = true;
            uint32_t w = 0;
            if (!asm_insn(cx, L.body, w)) w = 0;
            res.words.push_back(w);
            break;
        }
        }
    }

    res.ok = (cx.errors == 0);
    if (!res.ok && err) fprintf(err, "%s: %d error(s)\n", name, cx.errors);
    return res.ok;
}

// ---------- public API (unchanged signatures) ----------
// Lines -> words. Empty vector on error. Leading .org padding is kept.
static inline std::vector<uint32_t> asm_assemble(const std::vector<std::string>& lines,
                                                 const char* name = "<asm>") {
    AsmResult r;
    if (!asm_assemble_ex(lines, name, r)) return {};
    return r.words;
}

// One line -> one word (kept for compatibility). labels is read and updated.
static inline uint32_t asm_line(const std::string& raw, std::map<std::string, int>& labels,
                                int cur_pc, int* ok) {
    *ok = 0;
    AsmCtx cx;
    cx.labels = labels;
    std::vector<std::string> labs;
    bool bad = false;
    std::string body = asm_peel_labels(raw, labs, bad);
    for (auto& l : labs) { labels[l] = cur_pc; cx.labels[l] = cur_pc; }
    if (bad || body.empty() || body[0] == '.') return 0;
    uint32_t w = 0;
    cx.lines = nullptr;
    cx.cur_line = 1;
    if (!asm_insn(cx, body, w)) return 0;
    *ok = 1;
    return w;
}

// .asm file -> words. A leading ".org N" is stripped: the returned words
// start at address N and *out_org receives N (also for N = 0).
static inline std::vector<uint32_t> asm_assemble_file(const char* path, uint16_t* out_org = nullptr) {
    std::ifstream in(path);
    if (!in) { fprintf(stderr, "asm: cannot open %s\n", path); return {}; }
    std::vector<std::string> lines;
    std::string s;
    while (std::getline(in, s)) lines.push_back(s);

    AsmResult r;
    if (!asm_assemble_ex(lines, path, r)) return {};

    if (r.first_org >= 0) {
        if ((size_t)r.first_org >= r.words.size()) {
            fprintf(stderr, "%s: error: no code after .org 0x%X\n", path, r.first_org);
            return {};
        }
        if (out_org) *out_org = (uint16_t)r.first_org;
        r.words.erase(r.words.begin(), r.words.begin() + r.first_org);
    }
    return r.words;
}
