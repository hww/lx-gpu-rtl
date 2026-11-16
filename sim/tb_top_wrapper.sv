`timescale 1ns/1ps
`default_nettype none

// Testbench wrapper for `top`. All ports use the *_i / *_o convention.
module tb_top_wrapper (
    // clock / reset
    input  logic        clk_i,
    input  logic        rst_i,

    // host commands
    input  logic        start_i,
    input  logic        gpu_start_i,
    input  logic [23:0] gpu_addr_i,

    // status
    output logic        halted_o,
    output logic        error_o,
    output logic [11:0] error_code_o,
    output logic [11:0] pc_o,

    // Wishbone master
    output logic        wb_cyc_o,
    output logic        wb_stb_o,
    output logic        wb_we_o,
    output logic [23:0] wb_adr_o,
    output logic [15:0] wb_dat_o,
    input  logic [15:0] wb_dat_i,
    input  logic        wb_ack_i
);

    logic gpu_reset_i;
    assign gpu_reset_i = 1'b0;

    top u_top (
        .clk(clk_i),
        .rst(rst_i),

        .start_i(start_i),
        .gpu_start_i(gpu_start_i),
        .gpu_reset_i(gpu_reset_i),
        .gpu_addr_i(gpu_addr_i),

        .halted_o(halted_o),
        .error_o(error_o),
        .error_code_o(error_code_o),
        .pc_o(pc_o),

        .wb_cyc_o(wb_cyc_o), .wb_stb_o(wb_stb_o), .wb_we_o(wb_we_o),
        .wb_adr_o(wb_adr_o), .wb_dat_o(wb_dat_o),
        .wb_dat_i(wb_dat_i), .wb_ack_i(wb_ack_i)
    );

endmodule
