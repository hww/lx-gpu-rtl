#include "Vfetch_unit.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static Vfetch_unit* dut;
static int checks = 0;
static int errors = 0;

static void tick() {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
    dut->clk = 0; dut->eval();
}

// установить входы и дождаться комбинационной стабилизации, без позеджа
static void eval_only() { dut->eval(); }

static void check(const char* name, uint32_t got, uint32_t exp) {
    checks++;
    if (got != exp) {
        printf("  FAIL %-28s got 0x%03X exp 0x%03X\n", name, got, exp);
        errors++;
    } else {
        printf("  PASS %-28s = 0x%03X\n", name, got);
    }
}

static void check_bit(const char* name, int got, int exp) {
    checks++;
    if (got != exp) {
        printf("  FAIL %-28s got %d exp %d\n", name, got, exp);
        errors++;
    } else {
        printf("  PASS %-28s = %d\n", name, got);
    }
}

// Собрать 32-битное слово инструкции
static uint32_t make_ir(uint8_t is_ctrl, uint8_t l, uint8_t cond,
                        uint8_t reg, uint8_t op, uint16_t addr) {
    uint32_t w = 0;
    w |= (uint32_t)(l & 1)      << 31;
    if (is_ctrl)
        w |= (uint32_t)0b10     << 29;   // ir[30:29] == 2'b10
    w |= (uint32_t)(cond & 0xF) << 25;
    w |= (uint32_t)(reg & 1)    << 16;
    w |= (uint32_t)(op  & 0xF)  << 12;
    w |= (uint32_t)(addr & 0xFFF);
    return w;
}

// Подать одно слово и дать ему продвинуться (adv_i=1, без hazard)
static void do_insn(uint8_t is_ctrl, uint8_t l, uint8_t cond,
                    uint8_t reg, uint8_t op, uint16_t addr,
                    uint8_t z, uint8_t n, uint8_t c, uint8_t v) {
    dut->ir_i       = make_ir(is_ctrl, l, cond, reg, op, addr);
    dut->flags_i    = ((v & 1) << 3) | ((c & 1) << 2) |
                      ((n & 1) << 1) |  (z & 1);
    dut->adv_i      = 1;
    dut->x_jump_i   = 0;
    dut->x_hazard_i = 0;
    dut->csr_we_i   = 0;
    dut->clear_i    = 0;
    tick();
}

static void section(const char* name) { printf("\n[%s]\n", name); }

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vfetch_unit;

    // ---- сброс ----
    dut->rst         = 1;
    dut->clear_i     = 0;
    dut->adv_i       = 0;
    dut->ir_i        = 0;
    dut->flags_i     = 0;
    dut->x_hazard_i  = 0;
    dut->x_jump_i    = 0;
    dut->x_target_i  = 0;
    dut->csr_we_i    = 0;
    dut->csr_sel_i   = 0;
    dut->csr_wdata_i = 0;
    tick(); tick();
    dut->rst = 0;

    printf("=== fetch_unit tests ===\n");

    section("clear_i");
    dut->clear_i = 1; tick();
    dut->clear_i = 0;
    check("clear.pc",              dut->pc_o,     0);
    check_bit("clear.kill",        dut->d_valid_o, 0);

    section("advance / kill release");
    do_insn(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    check("adv1.pc",               dut->pc_o,     1);
    check_bit("adv1.d_valid",      dut->d_valid_o, 1);

    section("JMP taken");
    do_insn(1, 0, 0, 0, 1, 0x100, 0, 0, 0, 0);
    check("JMP.pc",                dut->pc_o,     0x100);
    check_bit("JMP.kill",          dut->d_valid_o, 0);
    do_insn(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    check("JMP.post.pc",           dut->pc_o,     0x101);
    check_bit("JMP.post.d_valid",  dut->d_valid_o, 1);

    section("JMP not taken (cond=EQ, z=0)");
    do_insn(1, 0, 1, 0, 1, 0x100, 0, 0, 0, 0);
    check("JMP_nt.pc",             dut->pc_o,     0x102);
    check_bit("JMP_nt.d_valid",    dut->d_valid_o, 1);

    section("CALL");
    do_insn(1, 0, 0, 0, 2, 0x200, 0, 1, 0, 1);   // flags V=1 N=1
    check("CALL.pc",               dut->pc_o,     0x200);
    check("CALL.shadow",           dut->shadow_o, 0xA102);
    check_bit("CALL.kill",         dut->d_valid_o, 0);

    section("RET");
    do_insn(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);        // 0x200 -> 0x201, kill снят
    check("RET.pre.pc",            dut->pc_o,     0x201);
    do_insn(1, 0, 0, 0, 3, 0, 0, 0, 0, 0);
    check("RET.pc",                dut->pc_o,     0x102);
    check_bit("RET.kill",          dut->d_valid_o, 0);

    section("JMPR (X-stage redirect)");
    do_insn(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);        // снять kill, 0x102 -> 0x103
    check("JMPR.pre.pc",           dut->pc_o,     0x103);
    dut->ir_i       = make_ir(1, 0, 0, 1, 1, 0);
    dut->adv_i      = 1;
    dut->x_jump_i   = 1;
    dut->x_target_i = 0x300;
    dut->flags_i    = 0;
    dut->x_hazard_i = 0;
    dut->csr_we_i   = 0;
    tick();
    dut->x_jump_i = 0;
    check("JMPR.pc",               dut->pc_o,     0x300);
    check_bit("JMPR.kill",         dut->d_valid_o, 0);

    section("CSR write");
    dut->adv_i = 1;
    dut->csr_we_i = 1; dut->csr_sel_i = 1; dut->csr_wdata_i = 3;
    tick();
    dut->csr_we_i = 1; dut->csr_sel_i = 2; dut->csr_wdata_i = 0x400;
    tick();
    dut->csr_we_i = 0;
    check("CSR.loop_cnt",          dut->loop_cnt_o, 3);
    check("CSR.loop_tgt",          dut->loop_tgt_o, 0x400);

    section("LOOP taken (CNT>1)");
    do_insn(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);        // снять kill
    check_bit("LOOP.pre.d_valid",  dut->d_valid_o, 1);
    do_insn(0, 1, 0, 0, 0, 0, 0, 0, 0, 0);
    check("LOOP1.pc",              dut->pc_o,     0x400);
    check("LOOP1.cnt",             dut->loop_cnt_o, 2);
    check_bit("LOOP1.kill",        dut->d_valid_o, 0);
    do_insn(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    check_bit("LOOP1.post.d_valid",dut->d_valid_o, 1);
    do_insn(0, 1, 0, 0, 0, 0, 0, 0, 0, 0);
    check("LOOP2.pc",              dut->pc_o,     0x400);
    check("LOOP2.cnt",             dut->loop_cnt_o, 1);
    do_insn(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    check_bit("LOOP2.post.d_valid",dut->d_valid_o, 1);

    section("LOOP not taken (CNT==1)");
    uint16_t pc_before = dut->pc_o;
    do_insn(0, 1, 0, 0, 0, 0, 0, 0, 0, 0);
    check("LOOP3.pc",              dut->pc_o,     pc_before + 1);
    check("LOOP3.cnt",             dut->loop_cnt_o, 1);
    check_bit("LOOP3.d_valid",     dut->d_valid_o, 1);

    section("LOOP + taken JMP (JMP wins, CNT unchanged)");
    dut->adv_i = 1;
    dut->csr_we_i = 1; dut->csr_sel_i = 1; dut->csr_wdata_i = 5;
    tick(); dut->csr_we_i = 0;
    do_insn(1, 1, 0, 0, 1, 0x500, 0, 0, 0, 0);
    check("LOOP_JMP.pc",           dut->pc_o,     0x500);
    check("LOOP_JMP.cnt",          dut->loop_cnt_o, 5);
    check_bit("LOOP_JMP.kill",     dut->d_valid_o, 0);

    section("STALL (adv_i=0)");
    do_insn(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    uint16_t pc_stall = dut->pc_o;
    dut->ir_i       = make_ir(1, 0, 0, 0, 1, 0x777); // даже JMP не должен сработать
    dut->flags_i    = 0;
    dut->adv_i      = 0;
    dut->x_jump_i   = 0;
    dut->x_hazard_i = 0;
    dut->csr_we_i   = 0;
    tick();
    check("STALL.pc",              dut->pc_o,     pc_stall);
    check_bit("STALL.d_valid",     dut->d_valid_o, 1);

    section("HOLD: CALL needs flags+shadow, X writes both");
    do_insn(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);        // снять kill
    uint16_t pc_hold = dut->pc_o;
    dut->ir_i       = make_ir(1, 0, 0, 0, 2, 0x600); // CALL
    dut->flags_i    = 0;
    dut->x_hazard_i = 0b011;                          // X пишет flags и shadow
    dut->adv_i      = 1;
    dut->x_jump_i   = 0;
    dut->csr_we_i   = 0;
    eval_only();
    check_bit("HOLD.hold_o",       dut->hold_o,   1);
    tick();
    check("HOLD.pc",               dut->pc_o,     0x600);
    check_bit("HOLD.d_valid",      dut->d_valid_o, 0);

    section("HOLD: L needs LOOP, X writes LOOP");
    do_insn(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);        // снять kill
    dut->ir_i       = make_ir(0, 1, 0, 0, 0, 0);   // L=1, не ctrl
    dut->x_hazard_i = 0b100;                        // X пишет LOOP
    dut->flags_i    = 0;
    dut->adv_i      = 1;
    dut->x_jump_i   = 0;
    dut->csr_we_i   = 0;
    eval_only();
    check_bit("HOLD.loop_hold_o",  dut->hold_o,   1);
    tick();

    section("HOLD: no hazard -> hold_o=0");
    do_insn(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    dut->ir_i       = make_ir(1, 0, 0, 0, 2, 0x600); // CALL
    dut->flags_i    = 0;
    dut->x_hazard_i = 0;
    dut->adv_i      = 0;
    dut->x_jump_i   = 0;
    dut->csr_we_i   = 0;
    eval_only();
    check_bit("HOLD.none",         dut->hold_o,   0);
    tick();

    printf("\n=== fetch_unit tests done: %d checks, %d errors ===\n",
           checks, errors);
    delete dut;
    return errors ? 1 : 0;
}