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
    logic [11:0] error_code;
    logic        gpu_start, gpu_reset;
    logic [23:0] gpu_addr;
    logic        rom_we_ext;
    logic [11:0] rom_addr_ext;
    logic [31:0] rom_wdata_ext;

    assign gpu_start = 1'b0;
    assign gpu_reset = 1'b0;
    assign gpu_addr  = 24'h0;
    assign rom_we_ext    = 1'b0;
    assign rom_addr_ext  = 12'h0;
    assign rom_wdata_ext = 32'h0;
    
    top u_top (
        .clk(clk),
        .rst(rst),
        .halted(halted),
        .error(error),
        .error_code(error_code),
        .start(start),
        .gpu_start(gpu_start),
        .gpu_reset(gpu_reset),
        .gpu_addr(gpu_addr),
        `ifdef SIMULATION
        .rom_we_ext(rom_we_ext),
        .rom_addr_ext(rom_addr_ext),
        .rom_wdata_ext(rom_wdata_ext),
        `endif
        .wb_cyc(wb_cyc), .wb_stb(wb_stb), .wb_we(wb_we),
        .wb_adr(wb_adr), .wb_dat_o(wb_dat_o),
        .wb_dat_i(wb_dat_i), .wb_ack(wb_ack)
    );
endmodule