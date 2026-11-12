#include "Vtb_top_wrapper.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <cstdio>

static Vtb_top_wrapper* dut;
static VerilatedVcdC* tfp;
static uint64_t t = 0;

static void tick() {
    dut->clk = 0; dut->eval(); if (tfp) tfp->dump(t++);
    dut->clk = 1; dut->eval(); if (tfp) tfp->dump(t++);
    dut->clk = 0; dut->eval(); if (tfp) tfp->dump(t++);
}

static void load_rom(uint16_t addr, uint32_t w) {
    dut->rom_we = 1;
    dut->rom_addr = addr;
    dut->rom_wdata = w;
    tick();
    dut->rom_we = 0;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    dut = new Vtb_top_wrapper;
    tfp = new VerilatedVcdC;
    dut->trace(tfp, 500);
    tfp->open("build/tb_top_wrapper.vcd");

       // 1. reset (full cycle)
    dut->rst = 1;
    dut->start = 0;
    dut->rom_we = 0; dut->mem0_we = 0;
    tick(); tick();

    // 2. release reset: halted=1, pc=0, processor idle
    dut->rst = 0;
    tick();

    // 3. load ROM while idle (pc frozen)
    load_rom(0, (0u<<31)|(3u<<29)|(18u<<24)|(1u<<23)|(0u<<19)|(0u<<18)|(0u<<13)|(0u<<12)|(0u<<0));
    load_rom(1, (0u<<31)|(3u<<29)|(17u<<24)|(1u<<23)|(0u<<19)|(0u<<18)|(0u<<13)|(0u<<12)|(2u<<0));
    load_rom(2, (0u<<31)|(3u<<29)|(16u<<24)|(1u<<23)|(0u<<19)|(0u<<18)|(0u<<13)|(0u<<12)|(1u<<0));
    load_rom(3, (0u<<31)|(3u<<29)|(13u<<24)|(1u<<23)|(0u<<19)|(0u<<18)|(0u<<13)|(0u<<12)|(0u<<0));
    load_rom(4, (0u<<31)|(3u<<29)|(14u<<24)|(1u<<23)|(0u<<19)|(0u<<18)|(0u<<13)|(0u<<12)|(0xAu<<0));
    load_rom(5, (0u<<31)|(0u<<29)|(25u<<24)|(1u<<23)|(0u<<19)|(0u<<18)|(0u<<13)|(0u<<12)|(32u<<7));
    load_rom(6, (0u<<31)|(2u<<29)|(0u<<24)|(0u<<23)|(0u<<19)|(0u<<18)|(5u<<13)|(0u<<12)|(0u<<0));

    // 4. preload stub mem[0]
    dut->mem0_we = 1; dut->mem0_data = 0xFFFF;
    tick();
    dut->mem0_we = 0;

    // 5. start (halted=0, pc=0)
    dut->start = 1;
    tick();
    dut->start = 0;

    // run
    for (int i = 0; i < 1500; i++) {
        tick();
        if (dut->halted) break;
        if (dut->error) { printf("FAIL top error\n"); tfp->close(); return 1; }
    }

    if (!dut->halted) { printf("FAIL: not halted\n"); tfp->close(); return 1; }
    if (dut->debug_mem0 != 0xAFFF) {
        printf("FAIL mem[0]=%04X, expected AFFF\n", dut->debug_mem0);
        tfp->close();
        return 1;
    }

    printf("top OK: mem[0]=%04X halted=1\n", dut->debug_mem0);
    printf("ALL PASS\n");
    tfp->close();
    delete dut;
    return 0;
}