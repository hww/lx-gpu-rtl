`timescale 1ns/1ps
// Stub of memory_arbiter's GPU port for testbenching top.sv without SDRAM.
// Responds to gpu_req with ack0 (first word) then ack1 (second word).
// Reads/writes a 16-bit memory array indexed by gpu_addr>>1.
module memory_arbiter_stub #(
    parameter int MEM_WORDS = 32768
) (
    input  logic        clk,
    input  logic        rst,

    // GPU port (same as memory_arbiter)
    input  logic [23:0] gpu_addr_i,
    input  logic        gpu_req_i,
    input  logic        gpu_we_i,
    input  logic [15:0] gpu_dat_i,
    output logic [15:0] gpu_dat_o,
    output logic        gpu_ack0_o,
    output logic        gpu_ack1_o,
    input  logic [1:0]  gpu_sel_i,
    input  logic        debug_mem0_we,
    input  logic [15:0] debug_mem0_data,
    output logic [15:0] debug_mem0
);

    logic [15:0] mem [0:MEM_WORDS-1];

    typedef enum logic [1:0] { S_IDLE, S_ACK0, S_ACK1 } state_t;
    state_t state;

    logic [23:0] saved_addr;
    logic        saved_we;
    logic [15:0] saved_wdata;

    // test can write to mem directly: dut.u_arb.mem[word_addr] = 16'hXXXX;

    always_ff @(posedge clk) begin
        if (rst) begin
            state       <= S_IDLE;
            saved_addr  <= 24'h0;
            saved_we    <= 1'b0;
            saved_wdata <= 16'h0;
        end else begin
            if (debug_mem0_we) mem[0] <= debug_mem0_data;
            case (state)
                S_IDLE: begin
                    if (gpu_req_i) begin
                        saved_addr  <= gpu_addr_i;
                        saved_we    <= gpu_we_i;
                        saved_wdata <= gpu_dat_i;
                        state       <= S_ACK0;
                    end
                end
                S_ACK0: begin
                    if (saved_we) begin
                        mem[saved_addr[16:1]] <= saved_wdata;
                    end
                    state <= S_ACK1;
                end
                S_ACK1: begin
                    state <= S_IDLE;
                end
                default: state <= S_IDLE;
            endcase
        end
    end

    assign gpu_ack0_o = (state == S_ACK0);
    assign gpu_ack1_o = (state == S_ACK1);
    assign gpu_dat_o  = mem[saved_addr >> 1];
    assign debug_mem0 = mem[0];

endmodule