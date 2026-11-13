`timescale 1ns/1ps
`default_nettype none

// 32 x 24 register file, 2 read ports, 1 write port.
// Two copies, both written, each read separately.
module regfile (
    input  logic        clk,
    input  logic        rst,
    input  logic [4:0]  ra,
    output logic [23:0] rdata_a,
    input  logic [4:0]  rb,
    output logic [23:0] rdata_b,
    input  logic        we,
    input  logic [4:0]  wa,
    input  logic [23:0] wdata
);

    logic [23:0] mem_a [0:31];
    logic [23:0] mem_b [0:31];

    integer i;
    always_ff @(posedge clk) begin
        if (rst) begin
            for (i = 0; i < 32; i++) begin
                mem_a[i] <= 24'h0;
                mem_b[i] <= 24'h0;
            end
        end else if (we) begin
            mem_a[wa] <= wdata;
            mem_b[wa] <= wdata;
        end
    end

    assign rdata_a = mem_a[ra];
    assign rdata_b = mem_b[rb];

endmodule