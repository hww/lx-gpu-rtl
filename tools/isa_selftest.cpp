// tools/isa_selftest.cpp — self-test for isa.h / asm.h / disasm.h
//   g++ -std=c++17 -Wall -Wextra -O2 -Itools tools/isa_selftest.cpp -o build/isa_selftest
//   ./build/isa_selftest
#include "isa.h"
#include "asm.h"
#include "disasm.h"
#include <cstdio>
#include <random>

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

// assemble silently; true and *w on success
static bool asm1(const std::string& line, uint32_t* w = nullptr) {
    AsmResult r;
    if (!asm_assemble_ex({line}, "<t>", r, nullptr)) return false;
    if (r.words.size() != 1) return false;
    if (w) *w = r.words[0];
    return true;
}
static bool asm_prog(const std::vector<std::string>& l, AsmResult& r) {
    return asm_assemble_ex(l, "<t>", r, nullptr);
}

int main() {
    // ---- 1. golden words (taken from the existing rtl/bootloader.hex) ----
    struct G { const char* text; uint32_t w; };
    const G golden[] = {
        {"MOV R0, DATA0",  0x01000B00}, {"MOV R1, DATA0", 0x01020B00},
        {"MOV ROMA, R1",   0x01500040}, {"MOV R2, DATA0", 0x01040B00},
        {"MOV ROML, R2",   0x014C0080}, {"MOV R3, DATA0", 0x01060B00},
        {"MOV ROMH, R3",   0x014E00C0}, {"SUB R0, #2",    0x6180D002},
        {"JMP.NE 3",       0x44001003}, {"JMP R1",        0x40011040},
        {"mov r0, data0",  0x01000B00},                       // case-insensitive
        {"L SUB R0, #2",   0xE180D002}, {"SUB.L R0, #2",  0xE180D002},
        {"NOP",            0x40000000}, {"MOV R0, R0 !w", 0x00000000},
    };
    for (auto& g : golden) {
        uint32_t w = 0;
        CHECK(asm1(g.text, &w) && w == g.w, "golden '%s': got %08X want %08X", g.text, w, g.w);
    }

    // ---- 2. things that must be rejected ----
    const char* bad[] = {
        "ADD XI0, #1", "MVN XI0, R1", "MOV BUSY, R1", "MOV MHI, R1", "MOV RSV42, R1",
        "MOV R1, XI0", "MOV R1, ROMA", "MOV R1, RSV43",
        "MOV DATA0, DATA1", "MOV DATA0, ROML", "MOV ROMH, ROML", "MOV ROMH, ROMH",
        "MOV R1, #2048", "MOV R1, #-1025", "LDI R1, #65536", "LDI R1, #-32769",
        "MOV BANK, #8", "MOV ROMA, #4096", "MOV FLAGS, #16", "MOV MODE_PAGE1, #2048",
        "MOV IDX0, R1", "MOV IDXH0, #0", "MOV STEP0, #1", "MOV PAGE0, R1", "MOV XI4, R1", "MOV DXI, R1",
        "LDI ROMA, #4096", "LDI LOOP_TGT, #5000",
        "JMP nowhere", "CALL R5", "JMP DATA0", "JMP ROML", "JMP 4096", "JMP -1", "JMP XI0",
        "HALT 128", "IRQ 256", "HALT -1", "HALT foo", "FLUSH 4", "FLUSH", "FLUSH CH4",
        "MOV R1,", "MOV R1, R2, R3", "MOV R1", "FOO R1, R2", "MOV.XX R1, R2", "MOV.EQ.NE R1, R2",
        "RET R1", "NOP 1", "CMP R1, R2 !nf", "MOV R1, R2 !f !nf", "MOV R1, R2 bogus",
        "MOV R1 R2", "MOV Q1, R2", "MOV R1, #", "MOV R1, #abc", "LDI R1, 5", "LDI R1, #1 !w",
        "ADD ZERO, R1", "CMP BUSY, R1",
        "L CALL 5", "CALL.L 5", "L CALL.EQ 5",
        ".org 5000", ".word", ".bogus 1", "1abc: NOP", "R1: NOP", "MOV: NOP",
    };
    for (auto s : bad) CHECK(!asm1(s), "must be rejected: '%s'", s);

    // ---- 3. things that must be accepted ----
    const char* good[] = {
        "MOV ZERO, DATA0", "MOV R1, #2047", "MOV R1, #-1024", "MOV R1, #0x7FF",
        "MOV YI0, #7", "MOV MODE_PAGE0, #0x7FF", "MOV ROMA, #2047", "MOV FLAGS, #15",
        "LDI ROMA, #4095", "LDI R1, #-1", "LDI R1, #65535", "LDI.NE R1, #0b101",
        "MOV ROMH, DATA2", "MOV ROML, DATA2", "MOV DATA3, R1",
        "MOV R1, ROML", "MOV DATA1, R2", "MOV SHADOW, SHADOW", "JMP SHADOW", "JMP R31", "JMP.EQ 4095",
        "HALT", "HALT 127", "IRQ 255", "IRQ", "FLUSH 3", "FLUSH CH0", "flush ch2",
        "CMP R1, R2", "CMP R1, #5", "TST R1, R2", "CALL.PL 0x10", "L ADD.NE R1, #1 !nf",
        "MVN R1, R2", "MUL R1, R2", "SAR R1, #3", "ADD R1, R2 !w", "MOV R1, R2 !f",
        "MOV XI3, R1", "MOV DXI0, #-1", "MOV MODE_PAGE2, R5", "MOV BANK, #4", "MOV R1, BANK",
        "MOV ALU_CTL1, R2", "MOV KEY3, #0", "LDI PITCH0, #320", "MOV W48, R1", "MOV YMAX2, R7",
        "L JMP.EQ 5", "L JMP R1", "L RET", "L HALT", "JMP.L 5",
    };
    for (auto s : good)
        CHECK(asm1(s), "must be accepted: '%s'", s);

    // ---- 4. labels, .org, duplicates ----
    {
        AsmResult r;
        CHECK(asm_prog({"start: NOP", "loop: SUB R0, #1", "JMP.NE loop", "JMP start", "LDI R1, #loop"}, r), "labels program");
        if (r.words.size() == 5) {
            CHECK((r.words[2] & 0xFFF) == 1, "JMP.NE loop -> 1, got %u", r.words[2] & 0xFFF);
            CHECK((r.words[3] & 0xFFF) == 0, "JMP start -> 0");
            CHECK((r.words[4] & 0xFFFF) == 1, "LDI R1,#loop -> 1");
        } else CHECK(false, "label program size %zu", r.words.size());

        CHECK(!asm_prog({"a: NOP", "a: NOP"}, r), "duplicate label");
        CHECK(!asm_prog({"JMP later", "NOP"}, r), "undefined label");
        CHECK(asm_prog({"JMP later", "later: NOP"}, r), "forward label");
        CHECK(!asm_prog({".org 8", "NOP", ".org 4", "NOP"}, r), "backwards .org");
        CHECK(asm_prog({".org 4", "x: NOP", "JMP x"}, r) && r.first_org == 4 &&
              r.words.size() == 6 && (r.words[5] & 0xFFF) == 4, ".org label address");
        CHECK(asm_prog({"NOP", ".org 3", "NOP"}, r) && r.first_org == -1 && r.words.size() == 4, ".org padding");
        CHECK(asm_prog({".word 1, 0x20, -1", "NOP"}, r) && r.words.size() == 4 && r.words[2] == 0xFFFFFFFFu, ".word");
        CHECK(!asm_prog({".word 4294967296"}, r), ".word overflow");
        CHECK(asm_prog({"  ; only a comment", ""}, r) && r.words.empty(), "empty program");

        // JMP with the loop bit and a condition, both syntaxes, must agree
        uint32_t a = 0, b = 0, c = 0;
        CHECK(asm1("L JMP.NE 5", &a) && asm1("JMP.NE.L 5", &b) && asm1("jmp.l.ne 5", &c) && a == b && b == c,
              "L prefix and .L suffix agree");
    }

    // ---- 5. round trip: asm(disasm(w)) == w for every word ----
    std::mt19937 rng(12345);
    long long n_text = 0, n_word = 0, n_bad = 0;
    auto roundtrip = [&](uint32_t w) {
        std::string t = disasm32(w);
        uint32_t back = 0;
        if (!asm1(t, &back) || back != w) {
            if (n_bad++ < 10) printf("ROUNDTRIP FAIL: %08X -> '%s' -> %08X\n", w, t.c_str(), back);
        }
        if (t.rfind(".word", 0) == 0) n_word++; else n_text++;
    };

    // 5a. fully random words
    for (int i = 0; i < 400000; i++) roundtrip(rng());

    static const uint32_t kOps[16] = {0,1,2,3,4,5,6,7,8,10,13,15,16,18,20,24};
    // 5b. random fields (hits valid instructions much more often)
    for (int i = 0; i < 1500000; i++) {
        isa::Insn c;
        c.cls   = rng() & 3;
        c.l     = rng() & 1;
        c.cond  = (rng() % 8 == 0) ? (rng() & 15) : (rng() % 15);
        c.write = rng() & 1;
        c.f     = rng() & 1;
        c.dst   = (rng() & 1) ? (rng() % 32) : (rng() & 63);
        c.op    = (c.cls == isa::CLASS_CTRL) ? (rng() % 9)
                : (rng() & 1) ? kOps[rng() % 16]
                              : (rng() & 31);
        c.src   = (rng() & 1) ? (rng() % 32) : (rng() & 63);
        c.sx    = rng() & 1;
        c.imm11 = (rng() & 1) ? (rng() & 0x7FF) : (rng() % 16);
        c.imm16 = (rng() & 1) ? (rng() & 0xFFFF) : (rng() % 16);
        c.r     = (rng() % 4 == 0);
        c.addr  = (rng() & 1) ? (rng() & 0xFFF) : (rng() % 8);
        roundtrip(isa::encode(c));
    }
    printf("round trip: %lld as text, %lld as .word, %lld failures\n", n_text, n_word, n_bad);
    CHECK(n_bad == 0, "round trip failures");
    CHECK(n_text > 100000, "too few words reached the text path (%lld)", n_text);

    // ---- 6. every printed word survives a second disassembly ----
    for (uint32_t w : {0x01000B00u, 0x6180D002u, 0x44001003u, 0x40011040u, 0xE180D002u, 0x0u})
        printf("  %08X  %s\n", w, disasm32(w));

    printf(g_fail ? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
