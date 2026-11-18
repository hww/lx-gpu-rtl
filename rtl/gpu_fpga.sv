`timescale 1ns/1ps
`default_nettype none

module gpu_fpga (
    input  logic        clk_25mhz,
    input  logic        rst_btn,

    // host Wishbone slave (wbs, 8-bit)
    input  logic        wbs_cyc_i,
    input  logic        wbs_stb_i,
    input  logic        wbs_we_i,
    input  logic [7:0]  wbs_adr_i,
    input  logic [7:0]  wbs_dat_i,
    output logic [7:0]  wbs_dat_o,
    output logic        wbs_ack_o,
    output logic        irq_o,          // GPU interrupt (IRQ instruction)

    // memory Wishbone master (wbm, 16-bit) -> SDRAM
    output logic        wbm_cyc_o,
    output logic        wbm_stb_o,
    output logic        wbm_we_o,
    output logic [23:0] wbm_adr_o,
    output logic [15:0] wbm_dat_o,
    input  logic [15:0] wbm_dat_i,
    input  logic        wbm_ack_i,

    output logic [7:0]  debug_leds_o
);

    // ===== clock / reset =====
    logic clk;
    logic rst;
    logic [1:0] rst_sync_r;

    assign clk = clk_25mhz;
    always_ff @(posedge clk) rst_sync_r <= {rst_sync_r[0], rst_btn};
    assign rst = rst_sync_r[1];

    // ===== GPU status =====
    logic        gpu_halted, gpu_error;
    logic [6:0] gpu_halt_code;

    // ===== host interface =====
    logic        gpu_start, gpu_reset;
    logic [23:0] gpu_addr;
    logic        gpu_irq_ack;
    logic [7:0]  gpu_irq_code;

    host_if u_host (
        .clk(clk), .rst(rst),
        .wb_cyc_i(wbs_cyc_i), .wb_stb_i(wbs_stb_i), .wb_we_i(wbs_we_i),
        .wb_adr_i(wbs_adr_i), .wb_dat_i(wbs_dat_i),
        .wb_dat_o(wbs_dat_o), .wb_ack_o(wbs_ack_o),
        .gpu_start_o(gpu_start), .gpu_reset_o(gpu_reset), .gpu_addr_o(gpu_addr),
        .gpu_halted_i(gpu_halted),
        .gpu_error_i(gpu_error),
        .gpu_error_code_i({5'd0, gpu_halt_code}),
        .gpu_irq_code_i(gpu_irq_code),
        .gpu_irq_ack_o(gpu_irq_ack)
    );

    // ===== GPU top =====
    logic        wbm_cyc_g, wbm_stb_g, wbm_we_g;
    logic [23:0] wbm_adr_g;
    logic [15:0] wbm_dat_o_g;
    logic        wbm_ack_g;

    gpu_top u_gpu (
        .clk(clk), .rst(rst),
        .halted_o(gpu_halted),
        .error_o(gpu_error),
        .halt_code_o(gpu_halt_code),
        .pc_o(),                      // not used on FPGA
        .start_i(1'b0),               // no auto-start; only GPU_START
        .gpu_start_i(gpu_start),
        .gpu_reset_i(gpu_reset),
        .gpu_addr_i(gpu_addr),
        .irq_ack_i(gpu_irq_ack),
        .irq_o(irq_o),
        .irq_code_o(gpu_irq_code),
        .wb_cyc_o(wbm_cyc_g), .wb_stb_o(wbm_stb_g), .wb_we_o(wbm_we_g),
        .wb_adr_o(wbm_adr_g), .wb_dat_o(wbm_dat_o_g),
        .wb_dat_i(wbm_dat_i), .wb_ack_i(wbm_ack_g)
    );

    // ===== WBM passthrough (placeholder for sdram_ctrl) =====
    assign wbm_cyc_o = wbm_cyc_g;
    assign wbm_stb_o = wbm_stb_g;
    assign wbm_we_o  = wbm_we_g;
    assign wbm_adr_o = wbm_adr_g;
    assign wbm_dat_o = wbm_dat_o_g;
    assign wbm_ack_g = wbm_ack_i;

    // ===== debug =====
    assign debug_leds_o = {5'h0, gpu_error, gpu_halted, gpu_start};

endmodule
