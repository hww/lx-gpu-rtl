`timescale 1ns/1ps
`default_nettype none

// 32 x 24, 2 read ports, 1 write port. Two EBR copies, both written.
// Synchronous read (latency 1). Write-through bypass: если читается адрес,
// записываемый в этом же такте, на выход идёт новое значение.
module regfile (
    input  logic        clk,
    input  logic        rst,        // не используется: EBR не сбрасывается
    input  logic [4:0]  ra,
    output logic [15:0] rdata_a,
    input  logic [4:0]  rb,
    output logic [15:0] rdata_b,
    input  logic        we,
    input  logic [4:0]  wa,
    input  logic [15:0] wdata
);

    (* ram_style = "block", no_rw_check *) logic [15:0] mem_a [0:31];
    (* ram_style = "block", no_rw_check *) logic [15:0] mem_b [0:31];

    logic [15:0] q_a, q_b;
    logic        byp_a, byp_b;
    logic [15:0] wdata_r;

    initial begin
        for (int i = 0; i < 32; i++) begin
            mem_a[i] = '0;
            mem_b[i] = '0;
        end
    end

    always_ff @(posedge clk) begin
        if (we) begin
            mem_a[wa] <= wdata;
            mem_b[wa] <= wdata;
        end
        q_a     <= mem_a[ra];
        q_b     <= mem_b[rb];
        byp_a   <= we && (wa == ra);
        byp_b   <= we && (wa == rb);
        wdata_r <= wdata;
    end

    assign rdata_a = byp_a ? wdata_r : q_a;
    assign rdata_b = byp_b ? wdata_r : q_b;

endmodule