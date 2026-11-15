`timescale 1ns/1ps
`default_nettype none

module top_fpga (
    input  logic        clk_25mhz,
    input  logic        rst_btn,

    // host Wishbone slave (wbs, 8-bit)
    input  logic        wbs_cyc,
    input  logic        wbs_stb,
    input  logic        wbs_we,
    input  logic [7:0]  wbs_adr,
    input  logic [7:0]  wbs_dat_i,
    output logic [7:0]  wbs_dat_o,
    output logic        wbs_ack,

    // memory Wishbone master (wbm, 16-bit) -> SDRAM
    output logic        wbm_cyc,
    output logic        wbm_stb,
    output logic        wbm_we,
    output logic [23:0] wbm_adr,
    output logic [15:0] wbm_dat_o,
    input  logic [15:0] wbm_dat_i,
    input  logic        wbm_ack,

    output logic [7:0]  debug_leds
);

    logic clk = clk_25mhz;
    logic rst;
    logic [1:0] rst_sync;
    always_ff @(posedge clk) rst_sync <= {rst_sync[0], rst_btn};
    assign rst = rst_sync[1];

    // ===== host interface =====
    logic        gpu_start, gpu_reset;
    logic [23:0] gpu_addr;

    gpu_host u_host (
        .clk(clk), .rst(rst),
        .wb_cyc_i(wbs_cyc), .wb_stb_i(wbs_stb), .wb_we_i(wbs_we),
        .wb_adr_i(wbs_adr), .wb_dat_i(wbs_dat_i),
        .wb_dat_o(wbs_dat_o), .wb_ack_o(wbs_ack),
        .gpu_start(gpu_start), .gpu_reset(gpu_reset), .gpu_addr(gpu_addr),
        .gpu_halted(halted), .gpu_error(error), .gpu_error_code(error_code)
    );

    // ===== GPU top =====
    logic        halted, error;
    logic [11:0] error_code;

    logic        wbm_cyc_g, wbm_stb_g, wbm_we_g;
    logic [23:0] wbm_adr_g;
    logic [15:0] wbm_dat_o_g;
    logic        wbm_ack_g;

    top u_gpu (
        .clk(clk), .rst(rst),
        .halted(halted), .error(error),
        .start(1'b0),                 // no auto-start; only GPU_START
        .gpu_start(gpu_start),
        .gpu_reset(gpu_reset),
        .gpu_addr(gpu_addr),
        .wb_cyc(wbm_cyc_g), .wb_stb(wbm_stb_g), .wb_we(wbm_we_g),
        .wb_adr(wbm_adr_g), .wb_dat_o(wbm_dat_o_g),
        .wb_dat_i(wbm_dat_i), .wb_ack(wbm_ack_g)
    );

    // ===== WBM passthrough (placeholder for sdram_ctrl) =====
    assign wbm_cyc   = wbm_cyc_g;
    assign wbm_stb   = wbm_stb_g;
    assign wbm_we    = wbm_we_g;
    assign wbm_adr   = wbm_adr_g;
    assign wbm_dat_o = wbm_dat_o_g;
    assign wbm_ack_g = wbm_ack;

    // ===== debug =====
    assign debug_leds = {5'h0, error, halted, gpu_start};

endmodule
