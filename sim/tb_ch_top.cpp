#include "Vch_top.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

static Vch_top* dut;
static VerilatedVcdC* tfp = nullptr;
static uint64_t sim_time = 0;
static uint16_t sim_ram[512];

struct TestResult {
    std::string group;
    std::string name;
    bool passed;
    uint32_t got;
    uint32_t exp;
};

static std::vector<TestResult> results;
static std::string current_group = "Init";

// -------- tick driver --------
static void tick(int target_clk) {
    target_clk = target_clk ? 1 : 0;
    if (dut->clk == target_clk) {
        dut->clk = target_clk ^ 1; dut->eval();
        if (tfp) tfp->dump(sim_time);
        sim_time += 5;
    }
    dut->clk = target_clk; dut->eval();
    if (tfp) tfp->dump(sim_time);
    sim_time += 5;
}
static void cycle() { tick(0); tick(1); }

static void record(const char* name, uint32_t got, uint32_t exp) {
    bool ok = (got == exp);
    results.push_back({current_group, name, ok, got, exp});
    printf("  [%s] %-42s got=0x%04X  exp=0x%04X\n",
           ok ? " OK " : "FAIL", name, got, exp);
}

static void idle_inputs() {
    dut->preload_i      = 0;
    dut->preload_addr_i = 0;
    dut->ch_i           = 0;
    dut->reg_we_i       = 0;
    dut->reg_bank_i     = 0;
    dut->reg_grp_i      = 0;
    dut->wdata_i        = 0;
    dut->data_req_i     = 0;
    dut->data_we_i      = 0;
    dut->flush_req_i    = 0;
    dut->flush_sel_i    = 0;
    dut->wb_dat_i       = 0;
    dut->wb_ack_i       = 0;
}

static void init_ram() {
    for (int i = 0; i < 512; i++) sim_ram[i] = 0x1000 + i;
}

static void service_wishbone() {
    if (dut->wb_cyc_o && dut->wb_stb_o) {
        uint32_t word_addr = (dut->wb_adr_o >> 1) & 0x1FF;
        uint16_t sel16 = (dut->wb_sel_o & 1 ? 0x00FF : 0) |
                         (dut->wb_sel_o & 2 ? 0xFF00 : 0);
        if (dut->wb_we_o) {
            sim_ram[word_addr] =
                (sim_ram[word_addr] & ~sel16) | (dut->wb_dat_o & sel16);
        } else {
            dut->wb_dat_i = sim_ram[word_addr];
        }
        dut->wb_ack_i = 1;
    } else {
        dut->wb_ack_i = 0;
    }
}

// ================================================================
// Banked register write
//   bank 0 POS : 0=XI  1=YI  2=XF  3=YF
//   bank 1 STEP: 0=DXI 1=DYI 2=DXF 3=DYF
//   bank 2 CLIP: 0=XMIN 1=YMIN 2=XMAX 3=YMAX
//   bank 3 PEN : 0=B   1=KEY 2=ALU_CTL
//   bank 4 SURF: 0=MODE_PAGE 1=PITCH
// ================================================================
static void wr_reg(uint8_t ch, uint8_t bank, uint8_t grp, uint16_t data) {
    tick(0);
    dut->ch_i       = ch;
    dut->reg_we_i   = 1;
    dut->reg_bank_i = bank;
    dut->reg_grp_i  = grp;
    dut->wdata_i    = data;
    tick(1);
    tick(0);
    dut->reg_we_i   = 0;
    tick(1);
}

static void set_mode_page(uint8_t ch, uint8_t fmt, uint8_t page) {
    // MODE_PAGE: [7:0]=PAGE [10:8]=FMT [12]=CLIP [13]=SHRX [14]=SHRY
    uint16_t v = page | ((uint16_t)(fmt & 7) << 8);
    wr_reg(ch, 4, 0, v);
}

static void set_fmt_linear(uint8_t ch, uint8_t fmt) {
    // PITCH=0 -> linear; page 0; no clip; no shrink
    wr_reg(ch, 4, 1, 0x0000);   // PITCH = 0
    set_mode_page(ch, fmt, 0);
}

static void set_pos(uint8_t ch, uint16_t xi, uint16_t yi) {
    wr_reg(ch, 0, 0, xi);
    wr_reg(ch, 0, 1, yi);
    wr_reg(ch, 0, 2, 0);    // XF
    wr_reg(ch, 0, 3, 0);    // YF
}

static void set_step(uint8_t ch, int16_t dxi) {
    wr_reg(ch, 1, 0, (uint16_t)dxi);
    wr_reg(ch, 1, 1, 0);    // DYI
    wr_reg(ch, 1, 2, 0);    // DXF
    wr_reg(ch, 1, 3, 0);    // DYF
}

// ================================================================
// Access through ch_top
// ================================================================
static uint16_t read_pixel(uint8_t ch) {
    tick(0);
    dut->ch_i       = ch;
    dut->data_req_i = 1;
    dut->data_we_i  = 0;

    int timeout = 100;
    while (timeout--) {
        tick(0);
        service_wishbone();
        bool ack_now = dut->data_ack_o;
        uint16_t val = dut->data_rdata_o;
        tick(1);
        if (ack_now) {
            tick(0);
            dut->data_req_i = 0;
            dut->wb_ack_i   = 0;
            tick(1);
            return val;
        }
    }
    tick(0); dut->data_req_i = 0; dut->wb_ack_i = 0; tick(1);
    printf("  [WARN] Timeout READ ch=%d\n", ch);
    return 0;
}

static void write_pixel(uint8_t ch, uint16_t pixel) {
    tick(0);
    dut->ch_i       = ch;
    dut->data_req_i = 1;
    dut->data_we_i  = 1;
    dut->wdata_i    = pixel;

    int timeout = 100;
    while (timeout--) {
        tick(0);
        service_wishbone();
        bool ack_now = dut->data_ack_o;
        tick(1);
        if (ack_now) {
            tick(0);
            dut->data_req_i = 0;
            dut->data_we_i  = 0;
            dut->wb_ack_i   = 0;
            tick(1);
            return;
        }
    }
    tick(0);
    dut->data_req_i = 0; dut->data_we_i = 0; dut->wb_ack_i = 0;
    tick(1);
    printf("  [WARN] Timeout WRITE ch=%d\n", ch);
}

static void flush_ch(uint8_t ch) {
    dut->ch_i        = ch;
    dut->flush_sel_i = ch;
    dut->flush_req_i = 1;

    int timeout = 100;
    while (timeout--) {
        tick(0);
        service_wishbone();
        bool ack_now = dut->flush_ack_o;
        tick(1);
        if (ack_now) {
            tick(0);
            dut->flush_req_i = 0;
            dut->wb_ack_i    = 0;
            tick(1);
            return;
        }
    }
    tick(0); dut->flush_req_i = 0; dut->wb_ack_i = 0; tick(1);
    printf("  [WARN] Timeout FLUSH ch=%d\n", ch);
}

// ================================================================
// Main
// ================================================================
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    dut = new Vch_top;
    tfp = new VerilatedVcdC;
    dut->trace(tfp, 99);
    tfp->open("build/sim_channel/wave.vcd");

    dut->clk = 1;
    idle_inputs();
    dut->rst = 1;
    cycle(); cycle();
    tick(0); dut->rst = 0; tick(1);
    init_ram();

    printf("\n====================================================================\n");
    printf("  CHANNEL UNIT + BLENDER + CACHE (v2) VERIFICATION\n");
    printf("====================================================================\n");

    // ----------------------------------------------------------------
    current_group = "1. Reset & Preload";
    printf("\n[%s]\n", current_group.c_str());
    record("Reset: data_ack_o == 0",  dut->data_ack_o,  0);
    record("Reset: flush_ack_o == 0", dut->flush_ack_o, 0);
    record("Reset: error_o == 0",     dut->error_o,     0);

    // Preload sets CH0: 16bpp, linear, XI = preload_addr[15:1], page = addr[23:16]
    sim_ram[16] = 0xCAFE;
    tick(0);
    dut->preload_i      = 1;
    dut->preload_addr_i = 0x000020;   // word 16
    tick(1);
    tick(0);
    dut->preload_i = 0;
    tick(1);
    cycle();
    record("Preload: CH0 read word 16", read_pixel(0), 0xCAFE);

    // ----------------------------------------------------------------
    current_group = "2. 16bpp (FMT=0) & Auto-Increment";
    printf("\n[%s]\n", current_group.c_str());
    sim_ram[10] = 0x1111;
    sim_ram[11] = 0x2222;
    set_fmt_linear(0, 0);
    set_step(0, 1);
    set_pos(0, 10, 0);

    record("16bpp read word 10", read_pixel(0), 0x1111);
    record("16bpp read word 11 (auto-inc)", read_pixel(0), 0x2222);

    set_pos(0, 12, 0);
    write_pixel(0, 0xDEAD);
    flush_ch(0);
    record("16bpp write + flush -> RAM 12", sim_ram[12], 0xDEAD);

    // ----------------------------------------------------------------
    current_group = "3. 8bpp (FMT=1) Unpack & RMW Blend";
    printf("\n[%s]\n", current_group.c_str());
    sim_ram[20] = 0xAABB;
    set_fmt_linear(1, 1);
    set_step(1, 1);
    set_pos(1, 40, 0);

    record("8bpp read pix 40 (LO byte)", read_pixel(1), 0x00BB);
    record("8bpp read pix 41 (HI byte)", read_pixel(1), 0x00AA);

    set_pos(1, 41, 0);
    write_pixel(1, 0x0055);
    flush_ch(1);
    record("8bpp RMW write pix 41 -> RAM 20", sim_ram[20], 0x55BB);

    // ----------------------------------------------------------------
    current_group = "4. 4bpp (FMT=2) Nibble RMW Blend";
    printf("\n[%s]\n", current_group.c_str());
    sim_ram[30] = 0x3412;
    set_fmt_linear(2, 2);
    set_step(2, 1);
    set_pos(2, 120, 0);

    record("4bpp read nibble 0", read_pixel(2), 0x1);
    record("4bpp read nibble 1", read_pixel(2), 0x2);
    record("4bpp read nibble 2", read_pixel(2), 0x3);
    record("4bpp read nibble 3", read_pixel(2), 0x4);

    set_pos(2, 122, 0);
    write_pixel(2, 0xF);
    flush_ch(2);
    record("4bpp RMW nibble 2 -> RAM 30", sim_ram[30], 0xF412);

    // ----------------------------------------------------------------
    current_group = "5. 1bpp/2bpp Read & Write-Protect";
    printf("\n[%s]\n", current_group.c_str());
    sim_ram[40] = 0x0180;
    set_fmt_linear(3, 4);
    set_step(3, 1);
    set_pos(3, 640, 0);
    record("1bpp read bit 7 (pix 640)", read_pixel(3), 1);
    record("1bpp read bit 6 (pix 641)", read_pixel(3), 0);

    set_pos(3, 641, 0);
    write_pixel(3, 1);
    flush_ch(3);
    record("1bpp write-protect -> RAM 40 intact", sim_ram[40], 0x0180);

    sim_ram[41] = 0x00E4;
    set_fmt_linear(3, 3);
    set_step(3, 1);
    set_pos(3, 41 * 8, 0);
    record("2bpp read crumb 0", read_pixel(3), 3);
    record("2bpp read crumb 1", read_pixel(3), 2);

    set_pos(3, 41 * 8, 0);
    write_pixel(3, 0);
    flush_ch(3);
    record("2bpp write-protect -> RAM 41 intact", sim_ram[41], 0x00E4);

    // ----------------------------------------------------------------
    current_group = "6. 8bpp STEP=2 (one byte per word)";
    printf("\n[%s]\n", current_group.c_str());
    sim_ram[50] = 0x1122;
    sim_ram[51] = 0x3344;
    set_fmt_linear(0, 1);
    set_step(0, 2);
    set_pos(0, 100, 0);
    write_pixel(0, 0x0099);
    write_pixel(0, 0x0088);
    flush_ch(0);
    record("8bpp STEP 2 low bytes -> RAM 50", sim_ram[50], 0x1199);
    record("8bpp STEP 2 low bytes -> RAM 51", sim_ram[51], 0x3388);

    set_pos(0, 101, 0);
    write_pixel(0, 0x0077);
    write_pixel(0, 0x0066);
    flush_ch(0);
    record("8bpp STEP 2 high bytes -> RAM 50", sim_ram[50], 0x7799);
    record("8bpp STEP 2 high bytes -> RAM 51", sim_ram[51], 0x6688);

    // ----------------------------------------------------------------
    current_group = "7. Negative Step (STEP = -1)";
    printf("\n[%s]\n", current_group.c_str());
    sim_ram[60] = 0xABCD;

    set_fmt_linear(1, 1);
    set_step(1, -1);
    set_pos(1, 121, 0);
    record("Neg step read pix 121", read_pixel(1), 0x00AB);
    record("Neg step read pix 120", read_pixel(1), 0x00CD);

    // ----------------------------------------------------------------
    current_group = "8. Multi-Channel Copy (CH0 -> CH1)";
    printf("\n[%s]\n", current_group.c_str());
    sim_ram[70] = 0x1234;
    sim_ram[80] = 0xFFFF;
    set_fmt_linear(0, 1);
    set_step(0, 1);
    set_pos(0, 140, 0);
    set_fmt_linear(1, 1);
    set_step(1, 1);
    set_pos(1, 160, 0);
    for (int i = 0; i < 2; i++) {
        uint16_t px = read_pixel(0);
        write_pixel(1, px);
    }
    flush_ch(1);
    record("Multi-ch copy 2x 8bpp -> RAM 80", sim_ram[80], 0x1234);

    // ----------------------------------------------------------------
    int passed = 0, failed = 0;
    for (const auto& r : results) if (r.passed) passed++; else failed++;
    printf("\n====================================================================\n");
    if (failed == 0) {
        printf("  RESULT: [ PASS ]  All %d / %d checks succeeded!\n",
               passed, (int)results.size());
    } else {
        printf("  RESULT: [ FAIL ]  %d failed, %d passed (total %d)\n",
               failed, passed, (int)results.size());
        printf("--------------------------------------------------------------------\n");
        for (const auto& r : results) {
            if (!r.passed) {
                printf("   * [%s] %s: got 0x%04X, expected 0x%04X\n",
                       r.group.c_str(), r.name.c_str(), r.got, r.exp);
            }
        }
    }
    printf("====================================================================\n\n");
    if (tfp) { tfp->close(); delete tfp; }
    delete dut;
    return failed ? 1 : 0;
}