#include "Vcond_eval.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static Vcond_eval* dut;
static int checks = 0, errors = 0;

static void expect(int cond, int v, int c, int n, int z, int exp, const char* tag) {
    dut->cond_i  = cond;
    dut->flags_i = ((v&1)<<3)|((c&1)<<2)|((n&1)<<1)|(z&1);
    dut->eval();
    checks++;
    if ((int)dut->ok_o != exp) {
        printf("  FAIL %-6s cond=%2d V%dC%dN%dZ%d got %d exp %d\n",
               tag, cond, v, c, n, z, dut->ok_o, exp);
        errors++;
    } else {
        printf("  PASS %-6s cond=%2d V%dC%dN%dZ%d -> %d\n",
               tag, cond, v, c, n, z, exp);
    }
}

static void sweep_cond(int cond, const char* tag) {
    // все 16 комбинаций флагов; ожидание берём из независимой модели
    for (int f = 0; f < 16; f++) {
        int v = (f>>3)&1, c = (f>>2)&1, n = (f>>1)&1, z = f&1;
        int exp;
        switch (cond) {
            case 0:  exp = 1; break;
            case 1:  exp = z; break;
            case 2:  exp = !z; break;
            case 3:  exp = c; break;
            case 4:  exp = !c; break;
            case 5:  exp = n; break;
            case 6:  exp = !n; break;
            case 7:  exp = v; break;
            case 8:  exp = !v; break;
            case 9:  exp = c && !z; break;
            case 10: exp = !c || z; break;
            case 11: exp = (n == v); break;
            case 12: exp = (n != v); break;
            case 13: exp = !z && (n == v); break;
            case 14: exp = z || (n != v); break;
            default: exp = 0; break;   // 15 reserved
        }
        expect(cond, v, c, n, z, exp, tag);
    }
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vcond_eval;
    printf("=== cond_eval tests ===\n");
    for (int cond = 0; cond < 16; cond++) sweep_cond(cond, "sweep");
    printf("=== cond_eval tests done: %d checks, %d errors ===\n", checks, errors);
    delete dut;
    return errors ? 1 : 0;
}