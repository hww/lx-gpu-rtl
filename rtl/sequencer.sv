`timescale 1ns/1ps
`default_nettype none

// PC is a plain register: combinational next-value on input, address on output.
// ROM (synchronous) presents the instruction one cycle later.
// No pc_next signal is exposed; there is nothing to route to ROM.
//
// Branch delay slot (was_branch):
//   On a taken JMP/CALL/RET or L-with-CNT>1, PC is loaded with target at the
//   end of cycle T, while ROM at T+1 still outputs the old "next" word.
//   was_branch=1 tells top to ignore that word for one cycle.
//
// SHADOW on CALL = PC at the CALL cycle (PC already points past CALL).
module sequencer (
    input  logic        clk,
    input  logic        rst,

    input  logic [1:0]  imm_class,
    input  logic [4:0]  reg_op,
    input  logic [3:0]  cond,
    input  logic        l,
    input  logic [11:0] imm12,
    input  logic        r_is_reg,

    input  logic        z, n, c, v,
    input  logic [11:0] jmpr_target,

    output logic [11:0] pc,
    output logic [15:0] shadow,
    output logic [15:0] loop_cnt,
    output logic [11:0] loop_tgt,
    output logic        halted,
    output logic        error,
    output logic [11:0] error_code,
    output logic        irq_pending,
    output logic [11:0] irq_code,
    output logic        skip_insn,

    input  logic        start,
    input  logic        reset_halt,    
    input  logic        ack,

    input  logic        loop_cnt_we,
    input  logic [15:0] loop_cnt_in,
    input  logic        loop_tgt_we,
    input  logic [11:0] loop_tgt_in,
    input  logic        shadow_we,
    input  logic [15:0] shadow_in
);

    function automatic logic cond_true(input logic [3:0] cd,
                                       input logic zz, nn, cc, vv);
        case (cd)
            4'd0:  cond_true = 1'b1;
            4'd1:  cond_true = zz;
            4'd2:  cond_true = ~zz;
            4'd3:  cond_true = cc;
            4'd4:  cond_true = ~cc;
            4'd5:  cond_true = nn;
            4'd6:  cond_true = ~nn;
            4'd7:  cond_true = vv;
            4'd8:  cond_true = ~vv;
            4'd9:  cond_true = cc & ~zz;
            4'd10: cond_true = ~cc | zz;
            4'd11: cond_true = (nn == vv);
            4'd12: cond_true = (nn != vv);
            4'd13: cond_true = ~zz & (nn == vv);
            4'd14: cond_true = zz | (nn != vv);
            default: cond_true = 1'b0;
        endcase
    endfunction

    `define OP_NOP   5'd0
    `define OP_JMP   5'd1
    `define OP_CALL  5'd2
    `define OP_RET   5'd3
    `define OP_HALT  5'd4
    `define OP_IRQ   5'd5
    `define OP_FLUSH 5'd6
    `define OP_RSV   5'd7

    logic is_ctrl, active, do_ctrl, taken_branch, use_reg, loop_taken;
    logic [11:0] branch_target, pc_in;
    logic was_branch;

    assign is_ctrl = (imm_class == 2'b10);
    assign active  = cond_true(cond, z, n, c, v);
    assign do_ctrl = is_ctrl && active;
    assign use_reg = r_is_reg && do_ctrl &&
                     (reg_op == `OP_JMP || reg_op == `OP_CALL);
    assign branch_target = use_reg ? jmpr_target : imm12;

    always_comb begin
        taken_branch = 1'b0;
        if (do_ctrl) begin
            case (reg_op)
                `OP_JMP, `OP_CALL, `OP_RET: taken_branch = 1'b1;
                default: taken_branch = 1'b0;
            endcase
        end
    end

    assign loop_taken = l && (loop_cnt > 16'd1) && !taken_branch;
    assign skip_insn  = was_branch;

    // combinational PC input
    always_comb begin
        if (start)
            pc_in = 12'h0;
        else if (rst || halted || error || !ack || skip_insn)
            pc_in = pc;
        else if (taken_branch)
            pc_in = (reg_op == `OP_RET) ? shadow[11:0] : branch_target;
        else if (loop_taken)
            pc_in = loop_tgt;
        else
            pc_in = pc + 12'd1;
    end

    always_ff @(posedge clk) begin
        if (rst || start)
            was_branch <= 1'b0;
        else if (ack && !halted && !error) begin
            if (skip_insn)                       was_branch <= 1'b0;
            else if (taken_branch || loop_taken) was_branch <= 1'b1;
        end
    end

    always_ff @(posedge clk) begin
        if (rst) begin
            pc          <= 12'h0;
            shadow      <= 16'h0;
            loop_cnt    <= 16'h0;
            loop_tgt    <= 12'h0;
            halted      <= 1'b1;
            error       <= 1'b0;
            error_code  <= 12'h0;
            irq_pending <= 1'b0;
            irq_code    <= 12'h0;
        end else if (reset_halt) begin
            pc          <= 12'h0;
            halted      <= 1'b1;
            error       <= 1'b0;
            error_code  <= 12'h0;
            irq_pending <= 1'b0;
            irq_code    <= 12'h0;            
        end else if (start) begin
            pc          <= 12'h0;
            halted      <= 1'b0;
            error       <= 1'b0;
            error_code  <= 12'h0;
            irq_pending <= 1'b0;
            irq_code    <= 12'h0;
        end else begin
            pc <= pc_in;

            if (loop_cnt_we) loop_cnt <= loop_cnt_in;
            else if (ack && !halted && !error && !skip_insn && loop_taken)
                loop_cnt <= loop_cnt - 16'd1;

            if (loop_tgt_we) loop_tgt <= loop_tgt_in;

            if (ack && !halted && !error && !skip_insn && do_ctrl &&
                reg_op == `OP_CALL)
                shadow <= {v, c, n, z, pc};
            else if (shadow_we)
                shadow <= shadow_in;

            if (ack && !halted && !error && !skip_insn && do_ctrl) begin
                case (reg_op)
                    `OP_HALT: begin
                        if (imm12 == 12'h0) halted <= 1'b1;
                        else begin
                            error      <= 1'b1;
                            error_code <= imm12;
                        end
                    end
                    `OP_IRQ: begin
                        irq_pending <= 1'b1;
                        irq_code    <= imm12;
                    end
                    default: ;
                endcase
            end
        end
    end

endmodule

