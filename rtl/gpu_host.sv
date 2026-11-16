`timescale 1ns/1ps
`default_nettype none

// gpu_host: Wishbone slave (8-bit), host interface to GPU.
//
// Registers:
//   0  GPU_ADDR0   [7:0]     W
//   1  GPU_ADDR1   [15:8]    W
//   2  GPU_ADDR2   [23:16]   W
//   3  COMMAND     [1:0]     W   (0=NOP, 1=START, 2=RESET)
//   4  STATUS      [7:0]     R   (bit7=HALT, bits6:0=RESULT)
//
// gpu_start_o : 1-cycle pulse, gpu_addr_o latched
// gpu_reset_o : 1-cycle pulse
module gpu_host (
    input  logic        clk,
    input  logic        rst,

    // Wishbone slave (wbs)
    input  logic        wb_cyc_i,
    input  logic        wb_stb_i,
    input  logic        wb_we_i,
    input  logic [7:0]  wb_adr_i,
    input  logic [7:0]  wb_dat_i,
    output logic [7:0]  wb_dat_o,
    output logic        wb_ack_o,

    // GPU control
    output logic        gpu_start_o,
    output logic        gpu_reset_o,
    output logic [23:0] gpu_addr_o,

    // GPU status
    input  logic        gpu_halted_i,
    input  logic        gpu_error_i,
    input  logic [11:0] gpu_error_code_i
);

    logic [7:0]  addr0_r, addr1_r, addr2_r;
    logic [1:0]  cmd_r;
    logic        start_r, reset_r, ack_r;

    logic sel;
    assign sel = wb_cyc_i & wb_stb_i;

    // ===== Wishbone ack =====
    always_ff @(posedge clk) begin
        if (rst) ack_r <= 1'b0;
        else     ack_r <= sel & ~ack_r;
    end
    assign wb_ack_o = ack_r;

    // ===== register writes =====
    always_ff @(posedge clk) begin
        if (rst) begin
            addr0_r <= 8'h0;
            addr1_r <= 8'h0;
            addr2_r <= 8'h0;
            cmd_r   <= 2'd0;
            start_r <= 1'b0;
            reset_r <= 1'b0;
        end else begin
            start_r <= 1'b0;
            reset_r <= 1'b0;

            if (sel && wb_we_i && !ack_r) begin
                case (wb_adr_i)
                    8'd0: addr0_r <= wb_dat_i;
                    8'd1: addr1_r <= wb_dat_i;
                    8'd2: addr2_r <= wb_dat_i;
                    8'd3: begin
                        cmd_r <= wb_dat_i[1:0];
                        if (wb_dat_i[1:0] == 2'd1) start_r <= 1'b1;
                        if (wb_dat_i[1:0] == 2'd2) reset_r <= 1'b1;
                    end
                    default: ;
                endcase
            end
        end
    end

    assign gpu_addr_o  = {addr2_r, addr1_r, addr0_r};
    assign gpu_start_o = start_r;
    assign gpu_reset_o = reset_r;

    // ===== STATUS read =====
    always_comb begin
        wb_dat_o = 8'h00;
        case (wb_adr_i)
            8'd0: wb_dat_o = addr0_r;
            8'd1: wb_dat_o = addr1_r;
            8'd2: wb_dat_o = addr2_r;
            8'd3: wb_dat_o = {6'h0, cmd_r};
            8'd4: wb_dat_o = {gpu_halted_i,
                              gpu_error_i ? gpu_error_code_i[6:0] : 7'h0};
            default: wb_dat_o = 8'h00;
        endcase
    end

endmodule
