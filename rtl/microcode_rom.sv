`timescale 1ns/1ps
`default_nettype none

// 4096 x 32 microcode ROM, true dual-port BRAM.
// Port A: write (load microcode).
// Port B: read (PC -> instruction), 1-cycle latency.
module microcode_rom #(
    parameter int DEPTH     = 4096,
    parameter int ADDR_W    = 12,
    parameter int DATA_W    = 32,
    parameter     INIT_FILE = ""
) (
    input  logic              clk,
    input  logic [ADDR_W-1:0] addr,
    output logic [DATA_W-1:0] data,
    input  logic              we,
    input  logic [ADDR_W-1:0] waddr,
    input  logic [DATA_W-1:0] wdata
);

    (* no_rw_check *)
    logic [DATA_W-1:0] mem [0:DEPTH-1];

    initial begin
        if (INIT_FILE != "") $readmemh(INIT_FILE, mem);
        else for (int i = 0; i < DEPTH; i++) mem[i] = '0;
    end

    // Port A: write
    always_ff @(posedge clk) begin
        if (we) mem[waddr] <= wdata;
    end

    // Port B: read, 1-cycle latency
    // (было: addr_r <= addr; data_r <= mem[addr_r]; — 2 такта)
    // (стало: data_r <= mem[addr]; — 1 такт)
    logic [DATA_W-1:0] data_r;
    always_ff @(posedge clk) begin
        data_r <= mem[addr];
    end
    assign data = data_r;

endmodule