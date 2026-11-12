`timescale 1ns/1ps
`default_nettype none

// 4096 x 32 microcode ROM.
// Read port: 1-cycle latency (registered output) -> BRAM inference.
// Write port: simulation only (loaded from C++ testbench or $readmemh).
module microcode_rom #(
    parameter int DEPTH     = 4096,
    parameter int ADDR_W    = 12,
    parameter int DATA_W    = 32,
    parameter     INIT_FILE = ""
) (
    input  logic              clk,

    // read port (PC -> instruction)
    input  logic [ADDR_W-1:0] addr,
    output logic [DATA_W-1:0] data,

    // write port (simulation only)
    input  logic              we,
    input  logic [ADDR_W-1:0] waddr,
    input  logic [DATA_W-1:0] wdata
);

    (* no_rw_check *)
    logic [DATA_W-1:0] mem [0:DEPTH-1];

`ifdef SIMULATION
    initial begin
        if (INIT_FILE != "") $readmemh(INIT_FILE, mem);
        else                  for (int i = 0; i < DEPTH; i++) mem[i] = '0;
    end

    always_ff @(posedge clk) begin
        if (we) mem[waddr] <= wdata;
    end
`else
    initial begin
        if (INIT_FILE != "") $readmemh(INIT_FILE, mem);
        else for (int i = 0; i < DEPTH; i++) mem[i] = '0;
    end
`endif

    // registered read (1-cycle latency) -> BRAM
    logic [ADDR_W-1:0] addr_r;
    logic [DATA_W-1:0] data_r;

    always_ff @(posedge clk) begin
        addr_r <= addr;
        data_r <= mem[addr_r];
    end

    assign data = data_r;

endmodule