`timescale 1ns/1ps
`default_nettype none

// 4096 x 32 microcode ROM, true dual-port BRAM (DP16KD).
//
// Port B: instruction fetch. addr = PC, latched on ack.
// Port A: data access from ISA (ROML/ROMH/ROMA).
//         we=1 -> write at waddr
//         we=0 -> read  at waddr (1-cycle latency)
//
// ROMA is maintained by top (incremented on ROMH access).
module microcode_rom #(
    parameter int DEPTH     = 4096,
    parameter int ADDR_W    = 12,
    parameter int DATA_W    = 32,
    parameter     INIT_FILE = ""
) (
    input  logic              rst,
    input  logic              clk,

    // Port B: instruction fetch
    input  logic [ADDR_W-1:0] addr,
    output logic [DATA_W-1:0] data,
    input  logic              ack,

    // Port A: data access
    input  logic              we,
    input  logic [ADDR_W-1:0] waddr,
    input  logic [DATA_W-1:0] wdata,
    output logic [DATA_W-1:0] rdata_a,
    input  logic              re
);

    (* no_rw_check *)
    logic [DATA_W-1:0] mem [0:DEPTH-1];

    initial begin
        if (INIT_FILE != "") $readmemh(INIT_FILE, mem);
        else for (int i = 0; i < DEPTH; i++) mem[i] = '0;
    end

    // Port B: instruction fetch, latched on ack
    logic [DATA_W-1:0] data_r;
    always_ff @(posedge clk) begin
        if (rst)        data_r <= '0;
        else if (ack)   data_r <= mem[addr];
    end
    assign data = data_r;

    // Port A: read/write
    logic [DATA_W-1:0] rdata_a_r;
    always_ff @(posedge clk) begin
        if (rst)        rdata_a_r <= '0;
        else if (we)    mem[waddr] <= wdata;
        else if (re)    rdata_a_r <= mem[waddr];
    end
    assign rdata_a = rdata_a_r;

endmodule
