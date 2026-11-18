`timescale 1ns/1ps
`default_nettype none

// core_fetch: PC, hardware loop (L bit), SHADOW and PC redirection.
//
// Invariant: while kill_r = 0 the IR holds mem[pc_o - 1]. Any redirect breaks
// it, so kill_r = 1 marks the IR as a bubble until the next advance.
//   * D-stage redirect (JMP/CALL/RET by address, L-loop return): 1 bubble.
//   * X-stage redirect (JMP by register): 2 bubbles (the D word is dropped by
//     top, the IR word by kill_r).
//
// Priority in D: taken branch > loop return > pc + 1.
// L is evaluated even when COND is false.
//
// hold_o: D must wait one cycle because X is about to change state that this
// D word reads (flags, SHADOW, LOOP). Costs nothing otherwise.
module core_fetch (
    input  logic        clk,
    input  logic        rst,
    input  logic        clear_i,        // start / host reset: pc = 0, state cleared

    input  logic        adv_i,          // the D word advances this cycle
    input  logic [31:0] ir_i,           // D word (ROM fetch register)
    input  logic [3:0]  flags_i,        // {V, C, N, Z}
    input  logic [2:0]  x_hazard_i,     // X writes {LOOP, SHADOW, flags}

    input  logic        x_jump_i,       // X redirects the PC (JMP by register)
    input  logic [11:0] x_target_i,

    input  logic        csr_we_i,       // X writes a CSR
    input  logic [1:0]  csr_sel_i,      // 0 SHADOW, 1 LOOP_CNT, 2 LOOP_TGT
    input  logic [15:0] csr_wdata_i,

    output logic [11:0] pc_o,           // ROM fetch address
    output logic        d_valid_o,      // ir_i is a real instruction
    output logic        hold_o,
    output logic [15:0] shadow_o,
    output logic [15:0] loop_cnt_o,
    output logic [11:0] loop_tgt_o
);

    localparam logic [3:0] CTRL_JMP  = 4'd1;
    localparam logic [3:0] CTRL_CALL = 4'd2;
    localparam logic [3:0] CTRL_RET  = 4'd3;

    logic [11:0] pc_r;
    logic        kill_r;
    logic [15:0] shadow_r;
    logic [15:0] loop_cnt_r;
    logic [11:0] loop_tgt_r;

    assign pc_o       = pc_r;
    assign d_valid_o  = ~kill_r;
    assign shadow_o   = shadow_r;
    assign loop_cnt_o = loop_cnt_r;
    assign loop_tgt_o = loop_tgt_r;

    // ===== D word =====
    logic        d_loop, d_is_ctrl, d_reg;
    logic [3:0]  d_cond, d_op;
    logic [11:0] d_addr;
    logic        d_jmp, d_call, d_ret, d_branch;
    logic        d_cond_ok, d_taken, d_loop_taken;
    logic [11:0] d_target;

    assign d_loop    = ir_i[31];
    assign d_is_ctrl = (ir_i[30:29] == 2'b10);
    assign d_cond    = ir_i[28:25];
    assign d_reg     = ir_i[16];
    assign d_op      = ir_i[15:12];
    assign d_addr    = ir_i[11:0];

    assign d_jmp    = d_is_ctrl && !d_reg && (d_op == CTRL_JMP);
    assign d_call   = d_is_ctrl && !d_reg && (d_op == CTRL_CALL);
    assign d_ret    = d_is_ctrl && !d_reg && (d_op == CTRL_RET);
    assign d_branch = d_jmp || d_call || d_ret;

    core_cond u_cond (.cond_i(d_cond), .flags_i(flags_i), .ok_o(d_cond_ok));

    assign d_taken      = d_valid_o && d_branch && d_cond_ok;
    assign d_loop_taken = d_valid_o && d_loop && (loop_cnt_r > 16'd1) && !d_taken;
    assign d_target     = d_ret ? shadow_r[11:0] : d_addr;

    // ===== hazard against the instruction in X =====
    logic d_needs_flags, d_needs_shadow;
    assign d_needs_flags  = d_branch && ((d_cond != 4'd0) || d_call);
    assign d_needs_shadow = d_call || d_ret;

    assign hold_o = d_valid_o &&
                    ((d_needs_flags  && x_hazard_i[0]) ||
                     (d_needs_shadow && x_hazard_i[1]) ||
                     (d_loop         && x_hazard_i[2]));

    // ===== state =====
    always_ff @(posedge clk) begin
        if (rst || clear_i) begin
            pc_r       <= 12'h0;
            kill_r     <= 1'b1;
            shadow_r   <= 16'h0;
            loop_cnt_r <= 16'h0;
            loop_tgt_r <= 12'h0;
        end else begin
            if (csr_we_i) begin
                case (csr_sel_i)
                    2'd0:    shadow_r   <= csr_wdata_i;
                    2'd1:    loop_cnt_r <= csr_wdata_i;
                    default: loop_tgt_r <= csr_wdata_i[11:0];
                endcase
            end

            if (x_jump_i) begin
                pc_r   <= x_target_i;
                kill_r <= 1'b1;
            end else if (adv_i) begin
                if (d_taken) begin
                    pc_r   <= d_target;
                    kill_r <= 1'b1;
                    if (d_call) shadow_r <= {flags_i, pc_r};   // pc_r = address after CALL
                end else if (d_loop_taken) begin
                    pc_r       <= loop_tgt_r;
                    kill_r     <= 1'b1;
                    loop_cnt_r <= loop_cnt_r - 16'd1;
                end else begin
                    pc_r   <= pc_r + 12'd1;
                    kill_r <= 1'b0;
                end
            end
        end
    end

endmodule

