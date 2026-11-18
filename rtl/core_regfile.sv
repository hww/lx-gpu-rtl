`timescale 1ns/1ps
`default_nettype none

// 32 x 16 register file, two EBR copies, synchronous read with write-through
// bypass. Reads happen only when rd_en_i = 1 (the pipeline advances); with
// rd_en_i = 0 the operand outputs hold their value.
module core_regfile (
    input  logic        clk,
    input  logic        rd_en_i,

    input  logic [4:0]  opa_addr_i,
    output logic [15:0] opa_data_o,
    input  logic [4:0]  opb_addr_i,
    output logic [15:0] opb_data_o,

    input  logic        wr_en_i,
    input  logic [4:0]  wr_addr_i,
    input  logic [15:0] wr_data_i
);

    (* ram_style = "block", no_rw_check *) logic [15:0] mem_a [0:31] /* verilator public_flat_rw */;
    (* ram_style = "block", no_rw_check *) logic [15:0] mem_b [0:31] /* verilator public_flat_rw */;

    logic [15:0] q_a, q_b, wr_data_r;
    logic        byp_a, byp_b;

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
        if (rd_en_i) begin
            q_a       <= mem_a[opa_addr_i];
            q_b       <= mem_b[opb_addr_i];
            byp_a     <= wr_en_i && (wr_addr_i == opa_addr_i);
            byp_b     <= wr_en_i && (wr_addr_i == opb_addr_i);
            wr_data_r <= wr_data_i;
        end
    end

    assign opa_data_o = byp_a ? wr_data_r : q_a;
    assign opb_data_o = byp_b ? wr_data_r : q_b;

endmodule