`timescale 1ns/1ps
`default_nettype none

module tb_top_wrapper (
    input  logic        clk,
    input  logic        rst,
    output logic        halted,
    output logic        error,
    input  logic        start,
    input  logic        rom_we,
    input  logic [11:0] rom_addr,
    input  logic [31:0] rom_wdata,
    output logic        wb_cyc,
    output logic        wb_stb,
    output logic        wb_we,
    output logic [23:0] wb_adr,
    output logic [15:0] wb_dat_o,
    input  logic [15:0] wb_dat_i,
    input  logic        wb_ack
);
    top u_top (
        .clk(clk), .rst(rst),
        .halted(halted), .error(error),
        .start(start),
        .rom_we(rom_we), .rom_addr(rom_addr), .rom_wdata(rom_wdata),
        .wb_cyc(wb_cyc), .wb_stb(wb_stb), .wb_we(wb_we),
        .wb_adr(wb_adr), .wb_dat_o(wb_dat_o),
        .wb_dat_i(wb_dat_i), .wb_ack(wb_ack)
    );
endmodule