#include "Vchannel_unit.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static Vchannel_unit* dut;
static int checks = 0, errors = 0;

static void tick() {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
    dut->clk = 0; dut->eval();
}

static void eval_only() { dut->eval(); }

static void check(const char* name, uint32_t got, uint32_t exp) {
    checks++;
    if (got != exp) {
        printf("  FAIL  %-30s got 0x%X exp 0x%X\n", name, got, exp);
        errors++;
    } else {
        printf("  PASS  %-30s = 0x%X\n", name, got);
    }
}

static void check_bit(const char* name, int got, int exp) {
    checks++;
    if (!!got != !!exp) {
        printf("  FAIL  %-30s got %d exp %d\n", name, !!got, !!exp);
        errors++;
    } else {
        printf("  PASS  %-30s = %d\n", name, !!got);
    }
}

static void section(const char* n) { printf("\n[%s]\n", n); }

static void idle_inputs() {
    dut->preload_i      = 0;
    dut->preload_addr_i = 0;
    dut->ch_i           = 0;
    dut->idx_we_i       = 0;
    dut->idxh_we_i      = 0;
    dut->step_we_i      = 0;
    dut->modepage_we_i  = 0;
    dut->wdata_i        = 0;
    dut->data_req_i     = 0;
    dut->data_we_i      = 0;
    dut->flush_req_i    = 0;
    dut->flush_mask_i   = 0;
    dut->wb_dat_i       = 0;
    dut->wb_ack_i       = 0;
}

static uint16_t sim_ram[256];
static void init_ram() {
    for (int i = 0; i < 256; i++) sim_ram[i] = 0x1000 + i;
}

static void tick_wb() {
    int timeout = 100;
    while (timeout--) {
        dut->clk = 0; dut->eval();
        if (dut->wb_cyc_o && dut->wb_stb_o) {
            uint32_t addr = (dut->wb_adr_o >> 1) & 0xFF;
            if (dut->wb_we_o) sim_ram[addr] = dut->wb_dat_o;
            else dut->wb_dat_i = sim_ram[addr];
            dut->wb_ack_i = 1;
        } else {
            dut->wb_ack_i = 0;
        }
        
        dut->clk = 1; dut->eval();
        dut->clk = 0; dut->eval();
        
        if (dut->data_ack_o || dut->flush_ack_o) {
            dut->wb_ack_i = 0;
            dut->clk = 1; dut->eval();
            dut->clk = 0; dut->eval();
            return;
        }
    }
    dut->wb_ack_i = 0;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vchannel_unit;

    idle_inputs();
    dut->rst = 1; tick(); tick();
    dut->rst = 0;
    idle_inputs();
    init_ram();

    printf("\n=== channel_unit (full) tests ===\n");

    section("1. after reset");
    check_bit("rst.data_ack",  dut->data_ack_o,  0);
    check_bit("rst.flush_ack", dut->flush_ack_o, 0);
    check_bit("rst.error",     dut->error_o,     0);
    check_bit("rst.wb_cyc",    dut->wb_cyc_o,    0);

    section("2. write IDX=1, read DATA (RAM[1])");
    dut->modepage_we_i = 1; dut->wdata_i = 4; dut->ch_i = 0;
    tick(); dut->modepage_we_i = 0;

    dut->idx_we_i = 1; dut->wdata_i = 1; dut->ch_i = 0;
    tick(); dut->idx_we_i = 0;

    dut->data_req_i = 1; dut->data_we_i = 0;
    eval_only();
    check_bit("wb_cyc assert", dut->wb_cyc_o, 1);

    tick_wb();
    check("data_rdata", dut->data_rdata_o, sim_ram[1]);

    dut->data_req_i = 0;
    tick_wb();
    check_bit("data_ack deassert", dut->data_ack_o, 0);
    check_bit("wb_cyc deassert", dut->wb_cyc_o, 0);

    section("3. auto-increment of IDX");
    dut->modepage_we_i = 1; dut->wdata_i = 4; dut->ch_i = 0;
    tick(); dut->modepage_we_i = 0;
    dut->step_we_i = 1; dut->wdata_i = 1; dut->ch_i = 0;
    tick(); dut->step_we_i = 0;

    dut->idx_we_i = 1; dut->wdata_i = 1; dut->ch_i = 0;
    tick(); dut->idx_we_i = 0;

    dut->data_req_i = 1; dut->data_we_i = 0;
    tick_wb(); 
    check("read IDX=1 (RAM[1])", dut->data_rdata_o, sim_ram[1]);
    dut->data_req_i = 0;
    tick_wb();

    dut->idx_we_i = 1; dut->wdata_i = 0; dut->ch_i = 0;
    tick(); dut->idx_we_i = 0;

    dut->data_req_i = 1; dut->data_we_i = 0;
    tick_wb();
    check("read IDX=0 (RAM[0])", dut->data_rdata_o, sim_ram[0]);
    dut->data_req_i = 0;
    tick_wb();

    section("4. write DATA + flush (write-back cache)");
    dut->idx_we_i = 1; dut->wdata_i = 5; dut->ch_i = 0;
    tick(); dut->idx_we_i = 0;

    dut->data_req_i = 1; dut->data_we_i = 1; dut->wdata_i = 0xABCD;
    tick_wb(); 
    dut->data_req_i = 0;
    tick_wb();

    dut->flush_req_i = 1;
    dut->flush_mask_i = 1;
    tick_wb(); 
    dut->flush_req_i = 0;
    tick_wb();

    check("RAM[5] written after flush", sim_ram[5], 0xABCD);

    printf("\n========================================\n");
    if (errors == 0) {
        printf("  ALL %d CHECKS PASSED\n", checks);
    } else {
        printf("  %d CHECK(S) FAILED out of %d\n", errors, checks);
    }
    printf("========================================\n\n");

    delete dut;
    return errors ? 1 : 0;
}