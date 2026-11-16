`timescale 1ns/1ps
`default_nettype none

module tb_top_wrapper (
    input  logic        clk,
    input  logic        rst,
    input  logic        start,
    input  logic        gpu_start,
    input  logic [23:0] gpu_addr,

    output logic        halted,
    output logic        error,
    output logic [11:0] error_code,
    output logic [11:0] pc,

    output logic        wb_cyc,
    output logic        wb_stb,
    output logic        wb_we,
    output logic [23:0] wb_adr,
    output logic [15:0] wb_dat_o,
    input  logic [15:0] wb_dat_i,
    input  logic        wb_ack
);
    logic gpu_reset;
    assign gpu_reset = 1'b0;

    top u_top (
        .clk(clk),
        .rst(rst),

        .start_i(start),
        .gpu_start_i(gpu_start),
        .gpu_reset_i(gpu_reset),
        .gpu_addr_i(gpu_addr),

        .halted_o(halted),
        .error_o(error),
        .error_code_o(error_code),
        .pc_o(pc),

        .wb_cyc_o(wb_cyc), .wb_stb_o(wb_stb), .wb_we_o(wb_we),
        .wb_adr_o(wb_adr), .wb_dat_o(wb_dat_o),
        .wb_dat_i(wb_dat_i), .wb_ack_i(wb_ack)
    );
endmodule