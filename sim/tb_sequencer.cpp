#include "Vsequencer.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <string>

static Vsequencer* dut;
static int errors = 0;

static void tick() {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
    dut->clk = 0; dut->eval();
}

static void check(const char* name, uint16_t got, uint16_t exp) {
    if (got != exp) {
        printf("FAIL %s: got %04X exp %04X\n", name, got, exp);
        errors++;
    }
}

static void check_bit(const char* name, int got, int exp) {
    if (got != exp) {
        printf("FAIL %s: got %d exp %d\n", name, got, exp);
        errors++;
    }
}

// do one instruction: set inputs, tick
static void do_insn(uint8_t imm_class, uint8_t reg_op, uint8_t cond, uint8_t l,
                    uint16_t imm12, uint8_t z, uint8_t n, uint8_t c, uint8_t v,
                    uint16_t jmpr_target) {
    dut->imm_class  = imm_class;
    dut->reg_op     = reg_op;
    dut->cond       = cond;
    dut->l          = l;
    dut->imm12      = imm12;
    dut->z          = z;
    dut->n          = n;
    dut->c          = c;
    dut->v          = v;
    dut->jmpr_target = jmpr_target;
    dut->start      = 0;
    dut->stall      = 0;
    dut->loop_cnt_we = 0;
    dut->loop_tgt_we = 0;
    tick();
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vsequencer;

    // reset
    dut->rst = 1; tick(); tick();
    dut->rst = 0;

    // start: PC=0, halted=0
    dut->start = 1; tick();
    dut->start = 0;
    check_bit("start.halted", dut->halted, 0);
    check("start.pc", dut->pc, 0);

    printf("=== sequencer tests ===\n");

    // JMP: PC -> imm12
    do_insn(0b10, 1, 0, 0, 0x100, 0, 0, 0, 0, 0);
    check("JMP.pc", dut->pc, 0x100);

    // JMP not taken (cond=EQ, z=0): PC -> PC+1
    do_insn(0b10, 1, 1, 0, 0x100, 0, 0, 0, 0, 0);
    check("JMP_not_taken.pc", dut->pc, 0x101);

    // CALL: SHADOW = {flags, PC+1}, PC -> imm12
    // flags: v=1, c=0, n=1, z=0 -> 1010
    do_insn(0b10, 2, 0, 0, 0x200, 0, 1, 0, 1, 0);
    check("CALL.pc", dut->pc, 0x200);
    // shadow = {v, c, n, z, pc+1} = {1, 0, 1, 0, 0x102} = 0xA102
    check("CALL.shadow", dut->shadow, 0xA102);

    // RET: PC -> SHADOW[11:0]
    do_insn(0b10, 3, 0, 0, 0, 0, 0, 0, 0, 0);
    check("RET.pc", dut->pc, 0x102);

    // JMPR: PC -> jmpr_target
    do_insn(0b10, 4, 0, 0, 0, 0, 0, 0, 0, 0x300);
    check("JMPR.pc", dut->pc, 0x300);

    // HALT 0: halted=1
    do_insn(0b10, 5, 0, 0, 0, 0, 0, 0, 0, 0);
    check_bit("HALT0.halted", dut->halted, 1);

    // restart
    dut->start = 1; tick(); dut->start = 0;
    check_bit("restart.halted", dut->halted, 0);
    check("restart.pc", dut->pc, 0);

    // HALT X (X=0x55): error=1, error_code=0x55
    do_insn(0b10, 5, 0, 0, 0x55, 0, 0, 0, 0, 0);
    check_bit("HALT55.error", dut->error, 1);
    check("HALT55.error_code", dut->error_code, 0x55);

    // restart
    dut->start = 1; tick(); dut->start = 0;
    check_bit("restart2.halted", dut->halted, 0);

    // IRQ: irq_pending=1, irq_code=0x10
    do_insn(0b10, 6, 0, 0, 0x10, 0, 0, 0, 0, 0);
    check_bit("IRQ.irq_pending", dut->irq_pending, 1);
    check("IRQ.irq_code", dut->irq_code, 0x10);

    // ===== LOOP tests =====
    // Записываем CNT=3, TGT=0x400 через порты
    dut->loop_cnt_we = 1; dut->loop_cnt_in = 3;
    dut->loop_tgt_we = 1; dut->loop_tgt_in = 0x400;
    dut->start = 0; dut->stall = 0;
    tick();
    dut->loop_cnt_we = 0; dut->loop_tgt_we = 0;
    check("LOOP.cnt_init", dut->loop_cnt, 3);
    check("LOOP.tgt_init", dut->loop_tgt, 0x400);

    // L=1, CNT=3 > 1: PC -> LOOP.TGT (0x400), CNT--
    do_insn(0b00, 0, 0, 1, 0, 0, 0, 0, 0, 0);
    check("LOOP1.pc", dut->pc, 0x400);
    check("LOOP1.cnt", dut->loop_cnt, 2);

    // L=1, CNT=2 > 1: PC -> LOOP.TGT (0x400), CNT--
    do_insn(0b00, 0, 0, 1, 0, 0, 0, 0, 0, 0);
    check("LOOP2.pc", dut->pc, 0x400);
    check("LOOP2.cnt", dut->loop_cnt, 1);

    // L=1, CNT=1: PC -> PC+1 (0x401), CNT не меняется
    do_insn(0b00, 0, 0, 1, 0, 0, 0, 0, 0, 0);
    check("LOOP3.pc", dut->pc, 0x401);
    check("LOOP3.cnt", dut->loop_cnt, 1);

    // L=1, CNT=5, взятый JMP: PC -> JMP target, CNT не меняется
    dut->loop_cnt_we = 1; dut->loop_cnt_in = 5;
    dut->start = 0; dut->stall = 0;
    tick();
    dut->loop_cnt_we = 0;
    do_insn(0b10, 1, 0, 1, 0x500, 0, 0, 0, 0, 0);  // CTRL JMP, L=1
    check("LOOP_JMP.pc", dut->pc, 0x500);
    check("LOOP_JMP.cnt", dut->loop_cnt, 5);

    // ===== STALL test =====
    uint16_t pc_before = dut->pc;
    dut->imm_class = 0b00; dut->reg_op = 0; dut->cond = 0; dut->l = 0;
    dut->imm12 = 0; dut->z = 0; dut->n = 0; dut->c = 0; dut->v = 0;
    dut->jmpr_target = 0; dut->start = 0; dut->stall = 1;
    dut->loop_cnt_we = 0; dut->loop_tgt_we = 0;
    tick();
    dut->stall = 0;
    check("STALL.pc", dut->pc, pc_before);

    printf("=== sequencer tests done: %d errors ===\n", errors);
    delete dut;
    return errors ? 1 : 0;
}