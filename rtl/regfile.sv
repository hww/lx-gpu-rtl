`timescale 1ns/1ps
`default_nettype none

// 32 x 16 register file, two read ports, one write port.
// Two EBR copies, both written, each read by its own operand port.
//
// Read is synchronous: opa_data / opb_data appear one cycle after
// opa_addr / opb_addr (decode stage D, data used in execute stage X).
// If the register being read is written in the same cycle, the new value
// is forwarded (write-through bypass), so back-to-back dependent
// instructions see correct data.
//
// The address inputs must stay stable while the pipeline is stalled.
module regfile (
    input  logic        clk,

    // read port A (first operand, destination register)
    input  logic [4:0]  opa_addr_i,
    output logic [15:0] opa_data_o,

    // read port B (second operand, source register)
    input  logic [4:0]  opb_addr_i,
    output logic [15:0] opb_data_o,

    // write port (result of the instruction in X)
    input  logic        wr_en_i,
    input  logic [4:0]  wr_addr_i,
    input  logic [15:0] wr_data_i
);

    (* ram_style = "block", no_rw_check *) logic [15:0] mem_a [0:31] /*verilator public_flat*/;
    (* ram_style = "block", no_rw_check *) logic [15:0] mem_b [0:31] /*verilator public_flat*/;

    logic [15:0] q_a, q_b;
    logic        byp_a, byp_b;
    logic [15:0] wr_data_r;

    initial begin
        for (int i = 0; i < 32; i++) begin
            mem_a[i] = '0;
            mem_b[i] = '0;
        end
        q_a = '0; q_b = '0; wr_data_r = '0;
        byp_a = 1'b0; byp_b = 1'b0;
    end

    always_ff @(posedge clk) begin
        if (wr_en_i) begin
            mem_a[wr_addr_i] <= wr_data_i;
            mem_b[wr_addr_i] <= wr_data_i;
        end
        q_a       <= mem_a[opa_addr_i];
        q_b       <= mem_b[opb_addr_i];
        byp_a     <= wr_en_i && (wr_addr_i == opa_addr_i);
        byp_b     <= wr_en_i && (wr_addr_i == opb_addr_i);
        wr_data_r <= wr_data_i;
    end

    assign opa_data_o = byp_a ? wr_data_r : q_a;
    assign opb_data_o = byp_b ? wr_data_r : q_b;

endmodule