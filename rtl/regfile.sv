`timescale 1ns/1ps
`default_nettype none

module regfile (
    input  logic        clk,
    input  logic        rst,
    input  logic [4:0]  ra,
    output logic [15:0] rdata_a,
    input  logic [4:0]  rb,
    output logic [15:0] rdata_b,
    input  logic        we,
    input  logic [4:0]  wa,
    input  logic [15:0] wdata
);
    logic [15:0] mem [0:31];

    initial begin
        for (int i = 0; i < 32; i++) mem[i] = '0;
    end

    always_ff @(posedge clk) begin
        if (we) mem[wa] <= wdata;
    end

    assign rdata_a = mem[ra];
    assign rdata_b = mem[rb];
endmodule
