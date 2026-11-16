`timescale 1ns/1ps
`default_nettype none

// 4096 x 32 microcode ROM on one true dual-port EBR set (DP16KD).
//
// Fetch port  (read only):  instruction fetch by the sequencer.
//   fetch_addr -> fetch_data, 1-cycle latency, held while fetch_en = 0.
// Access port (read/write): ISA access through ROML / ROMH / ROMA.
//   acc_we=1 : mem[acc_addr] <= acc_wdata
//   acc_re=1 : acc_rdata <= mem[acc_addr] (valid next cycle)
//   acc_we has priority over acc_re.
// ROMA (acc_addr) is kept and auto-incremented by top.
// Writing the word being fetched in the same cycle is undefined.
module microcode_rom #(
    parameter int DEPTH     = 4096,
    parameter int ADDR_W    = 12,
    parameter int DATA_W    = 32,
    parameter     INIT_FILE = ""
) (
    input  logic              clk,

    // fetch port
    input  logic              fetch_en_i,
    input  logic [ADDR_W-1:0] fetch_addr_i,
    output logic [DATA_W-1:0] fetch_data_o,

    // access port
    input  logic              acc_we_i,
    input  logic              acc_re_i,
    input  logic [ADDR_W-1:0] acc_addr_i,
    input  logic [DATA_W-1:0] acc_wdata_i,
    output logic [DATA_W-1:0] acc_rdata_o
);

    (* no_rw_check *)
    logic [DATA_W-1:0] mem [0:DEPTH-1] /*verilator public_flat*/;

    initial begin
        for (int i = 0; i < DEPTH; i++) mem[i] = '0;
    end

    always_ff @(posedge clk) begin
        if (fetch_en_i) fetch_data_o <= mem[fetch_addr_i];
    end

    always_ff @(posedge clk) begin
        if (acc_we_i)      mem[acc_addr_i] <= acc_wdata_i;
        else if (acc_re_i) acc_rdata_o   <= mem[acc_addr_i];
    end

endmodule