#include "Vtb_pixel_buffer_wrapper.h"
#include "verilated.h"
#include <cstdio>

static Vtb_pixel_buffer_wrapper* dut;

static void tick() {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
    dut->clk = 0; dut->eval();
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vtb_pixel_buffer_wrapper;

    dut->rst = 1;
    tick(); tick();
    dut->rst = 0;

    // fill page 0, tag 0x100, word 0xFFFF
    dut->ch = 1;
    dut->addr24 = 0x000100;
    dut->fill_en = 1; dut->fill_data = 0xFFFF;
    tick();
    dut->fill_en = 0;
    tick();
    if (!dut->hit) { printf("FAIL fill: not hit\n"); return 1; }
    if (dut->rdata != 0xFFFF) { printf("FAIL fill: rdata=%04X\n", dut->rdata); return 1; }
    printf("fill OK: hit=1 rdata=%04X\n", dut->rdata);

    // write we=1, wdata=0xAFFF
    dut->we = 1; dut->wdata = 0xAFFF;
    tick();
    dut->we = 0;
    tick();
    if (dut->dirty1 != 1) { printf("FAIL write: dirty1=%d\n", dut->dirty1); return 1; }
    if (dut->data1 != 0xAFFF) { printf("FAIL write: data1=%04X\n", dut->data1); return 1; }
    printf("write OK: dirty1=1 data1=%04X\n", dut->data1);

    // flush
    dut->flush_en = 1;
    tick(); tick();
    dut->flush_en = 0;
    tick();
    if (!dut->wb_req) { printf("FAIL flush: no wb_req\n"); return 1; }
    if (dut->wb_addr != 0x200) { printf("FAIL flush: wb_addr=%06X\n", dut->wb_addr); return 1; }
    if (dut->wb_data != 0xAFFF) { printf("FAIL flush: wb_data=%04X\n", dut->wb_data); return 1; }
    printf("flush OK: wb_req=1 wb_addr=%06X wb_data=%04X\n", dut->wb_addr, dut->wb_data);

    // ack
    dut->wb_ack = 1;
    tick(); tick();
    dut->wb_ack = 0;
    tick();
    if (dut->wb_req) { printf("FAIL ack: wb_req still high\n"); return 1; }
    printf("ack OK: wb_req=0\n");

    printf("ALL PASS\n");
    delete dut;
    return 0;
}