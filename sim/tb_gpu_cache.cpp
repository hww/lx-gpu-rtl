#include "Vgpu_cache.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

static Vgpu_cache* dut;
static int errors = 0;

#define MEM_WORDS 32768
static uint16_t mem[MEM_WORDS];

static void tick() {
    dut->wb_ack_i = 0;
    dut->wb_dat_i = 0;
    if (dut->wb_cyc_o && dut->wb_stb_o) {
        uint32_t a = (dut->wb_adr_o >> 1) & (MEM_WORDS - 1);
        if (dut->wb_we_o) mem[a] = dut->wb_dat_o;
        else              dut->wb_dat_i = mem[a];
        dut->wb_ack_i = 1;
    }
    dut->eval();
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

// access: сбросить *_done_r, выставить access, ждать access_done
static void do_access(uint8_t ch, uint16_t word, uint8_t we, uint16_t wdata,
                      uint16_t exp_rdata, const char* name) {
    dut->ch = ch;
    dut->word_in = word;
    dut->we = we;
    dut->wdata = wdata;

    // сброс access_done_r
    dut->access = 0;
    tick();

    // access
    dut->access = 1;
    int cycles = 0;
    while (!dut->access_done && cycles < 1000) { tick(); cycles++; }
    dut->access = 0;

    if (we == 0) check(name, dut->rdata, exp_rdata);
}

// flush: сбросить flush_done_r, выставить flush_en, ждать flush_done
static void do_flush(uint8_t ch) {
    dut->flush_ch = ch;

    // сброс flush_done_r
    dut->flush_en = 0;
    tick();

    // flush
    dut->flush_en = 1;
    int cycles = 0;
    while (!dut->flush_done && cycles < 1000) { tick(); cycles++; }
    dut->flush_en = 0;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vgpu_cache;

    memset(mem, 0, sizeof(mem));
    dut->rst = 1; tick(); tick(); dut->rst = 0;

    // Отключаем prefetch/flush/page_we/access
    dut->prefetch_req = 0;
    dut->flush_en = 0; dut->flush_ch = 0;
    dut->page_we = 0; dut->page_in = 0;
    dut->access = 0; dut->we = 0;

    printf("=== gpu_cache tests ===\n");

    // ---- 1. page_we (тупой) ----
    dut->ch = 0; dut->page_in = 0; dut->page_we = 1; tick(); dut->page_we = 0;
    check("page0", dut->page_out, 0);

    // ---- 2. Read miss: ch0, word 0x10, mem[0x10]=0xABCD ----
    mem[0x10] = 0xABCD;
    do_access(0, 0x10, 0, 0, 0xABCD, "read_miss");
    check_bit("read_miss.hit", dut->hit, 1);

    // ---- 3. Read hit: ch0, word 0x10 ----
    do_access(0, 0x10, 0, 0, 0xABCD, "read_hit");

    // ---- 4. Write miss: ch1, word 0x20, wdata=0x1234 ----
    dut->ch = 1; dut->page_in = 0; dut->page_we = 1; tick(); dut->page_we = 0;
    mem[0x20] = 0x5555;
    do_access(1, 0x20, 1, 0x1234, 0, "write_miss");
    check("write_hit.mem_still", mem[0x20], 0x5555);

    // ---- 5. Write hit: ch1, word 0x20 ----
    do_access(1, 0x20, 1, 0x5678, 0, "write_hit");
    check("write_hit.mem_still", mem[0x20], 0x5555); // dirty, не сброшено

    // ---- 6. Flush ch1 ----
    do_flush(1);
    check("flush.mem", mem[0x20], 0x5678);

    // ---- 7. Read after flush (miss) ----
    do_access(1, 0x20, 0, 0, 0x5678, "read_after_flush");

    // ---- 8. Multi-channel ----
    mem[0x30] = 0x1111; mem[0x40] = 0x2222;
    dut->ch = 2; dut->page_in = 0; dut->page_we = 1; tick(); dut->page_we = 0;
    do_access(2, 0x30, 0, 0, 0x1111, "ch2_read");
    dut->ch = 3; dut->page_in = 0; dut->page_we = 1; tick(); dut->page_we = 0;
    do_access(3, 0x40, 0, 0, 0x2222, "ch3_read");

    // ---- 9. page_we при dirty: flush, потом page_we ----
    mem[0x50] = 0x9999;
    do_access(0, 0x50, 1, 0x7777, 0, "ch0_write_dirty");
    do_flush(0);
    check("page_dirty.mem", mem[0x50], 0x7777);

    dut->ch = 0; dut->page_in = 1; dut->page_we = 1; tick(); dut->page_we = 0;
    check("page1", dut->page_out, 1);

    printf("=== gpu_cache tests done: %d errors ===\n", errors);
    delete dut;
    return errors ? 1 : 0;
}