`timescale 1ns/1ps
`default_nettype none

// sequencer: PC, SHADOW, LOOP, halt/error/IRQ state and next-PC selection.
//
// PC is a plain register that drives the ROM fetch address directly. The ROM
// is synchronous, so the instruction for PC = A arrives when PC = A + 1.
// Consequences:
//   * SHADOW on CALL = pc_r, which already points past the CALL.
//   * Branch delay slot: on a taken JMP/CALL/RET or an L-iteration, pc_r is
//     loaded with the target at the end of cycle T, but the ROM still outputs
//     the old "next" word at T+1. skip_o = 1 tells top to ignore that word
//     (no register, flag, port or sequencer side effects). Cost: one bubble
//     per taken branch and per loop iteration.
//
// Priority inside one instruction: taken branch > loop return > pc + 1.
// L is evaluated even when COND is false (it belongs to the word, not to
// the condition).
//
// The *_we_i strobes are NOT gated here: top must raise them only for an
// instruction that commits (insn_valid_i, not skipped, COND true).
module sequencer (
    input  logic        clk,
    input  logic        rst,

    // instruction fields
    input  logic [1:0]  insn_class_i,   // {J, I}; control class = 2'b10
    input  logic [3:0]  ctrl_op_i,
    input  logic [3:0]  cond_i,
    input  logic        loop_i,         // L bit
    input  logic [11:0] addr_i,         // ADDR: target / HALT code / IRQ code
    input  logic        use_reg_i,      // R bit: JMP/CALL target from reg_target_i
    input  logic [11:0] reg_target_i,

    // current flags
    input  logic        flag_z_i,
    input  logic        flag_n_i,
    input  logic        flag_c_i,
    input  logic        flag_v_i,

    // pipeline handshake
    input  logic        insn_valid_i,   // ROM output is a valid word this cycle
    output logic        skip_o,         // that word must be ignored (branch bubble)

    // host commands
    input  logic        start_i,        // reset state and run from PC = 0
    input  logic        reset_halt_i,   // reset state and stay halted

    // state
    output logic [11:0] pc_o,
    output logic [15:0] shadow_o,
    output logic [15:0] loop_cnt_o,
    output logic [11:0] loop_tgt_o,
    output logic        halted_o,
    output logic        error_o,
    output logic [11:0] error_code_o,
    output logic        irq_pending_o,
    output logic [7:0]  irq_code_o,

    // register writes from the operand map (already gated by top)
    input  logic        loop_cnt_we_i,
    input  logic [15:0] loop_cnt_i,
    input  logic        loop_tgt_we_i,
    input  logic [11:0] loop_tgt_i,
    input  logic        shadow_we_i,
    input  logic [15:0] shadow_i,

    output logic        commit_o,       // word commits (not skipped, running)
    input  logic        irq_ack_i       // clear irq_pending (read of IRQ_CODE)
);

    // control operations (FLUSH, NOP and reserved codes are decoded in top)
    localparam logic [3:0] CTRL_NOP   = 4'd0;
    localparam logic [3:0] CTRL_JMP   = 4'd1;
    localparam logic [3:0] CTRL_CALL  = 4'd2;
    localparam logic [3:0] CTRL_RET   = 4'd3;
    localparam logic [3:0] CTRL_HALT  = 4'd4;
    localparam logic [3:0] CTRL_IRQ   = 4'd5;
    localparam logic [3:0] CTRL_FLUSH = 4'd6;
    localparam logic [3:0] CTRL_RSV   = 4'd7;

    function automatic logic eval_cond(input logic [3:0] cd,
                                       input logic zz, nn, cc, vv);
        case (cd)
            4'd0:    eval_cond = 1'b1;               // AL
            4'd1:    eval_cond = zz;                 // EQ
            4'd2:    eval_cond = ~zz;                // NE
            4'd3:    eval_cond = cc;                 // CS
            4'd4:    eval_cond = ~cc;                // CC
            4'd5:    eval_cond = nn;                 // MI
            4'd6:    eval_cond = ~nn;                // PL
            4'd7:    eval_cond = vv;                 // VS
            4'd8:    eval_cond = ~vv;                // VC
            4'd9:    eval_cond = cc & ~zz;           // HI
            4'd10:   eval_cond = ~cc | zz;           // LS
            4'd11:   eval_cond = (nn == vv);         // GE
            4'd12:   eval_cond = (nn != vv);         // LT
            4'd13:   eval_cond = ~zz & (nn == vv);   // GT
            4'd14:   eval_cond = zz | (nn != vv);    // LE
            default: eval_cond = 1'b0;               // reserved: never
        endcase
    endfunction

    // ===== state =====
    logic [11:0] pc_r;
    logic [15:0] shadow_r;
    logic [15:0] loop_cnt_r;
    logic [11:0] loop_tgt_r;
    logic        halted_r;
    logic        error_r;
    logic [11:0] error_code_r;
    logic        irq_pending_r;
    logic [7:0]  irq_code_r;
    logic        was_branch_r;      // previous word was a taken branch / loop return

    assign pc_o          = pc_r;
    assign shadow_o      = shadow_r;
    assign loop_cnt_o    = loop_cnt_r;
    assign loop_tgt_o    = loop_tgt_r;
    assign halted_o      = halted_r;
    assign error_o       = error_r;
    assign error_code_o  = error_code_r;
    assign irq_pending_o = irq_pending_r;
    assign irq_code_o    = irq_code_r;
    assign skip_o        = was_branch_r;
    assign commit_o      = commit_c;

    // ===== decode =====
    logic        is_ctrl_c;     // control-class word
    logic        cond_ok_c;     // COND is true
    logic        do_ctrl_c;     // control word that takes effect
    logic        reg_jump_c;    // JMP/CALL through a register
    logic        taken_c;       // JMP / CALL / RET taken
    logic        loop_taken_c;  // L bit and count > 1 and no taken branch
    logic        run_c;         // a valid word while the core is running
    logic        commit_c;      // run_c and not a skipped word
    logic [11:0] target_c;
    logic [11:0] pc_in_c;

    assign is_ctrl_c  = (insn_class_i == 2'b10);
    assign cond_ok_c  = eval_cond(cond_i, flag_z_i, flag_n_i, flag_c_i, flag_v_i);
    assign do_ctrl_c  = is_ctrl_c && cond_ok_c;
    assign reg_jump_c = use_reg_i && do_ctrl_c &&
                        (ctrl_op_i == CTRL_JMP || ctrl_op_i == CTRL_CALL);
    assign target_c   = reg_jump_c ? reg_target_i : addr_i;

    always_comb begin
        taken_c = 1'b0;
        if (do_ctrl_c) begin
            case (ctrl_op_i)
                CTRL_JMP, CTRL_CALL, CTRL_RET: taken_c = 1'b1;
                default:                       taken_c = 1'b0;
            endcase
        end
    end

    assign loop_taken_c = loop_i && (loop_cnt_r > 16'd1) && !taken_c;
    assign run_c        = insn_valid_i && !halted_r && !error_r;
    assign commit_c     = run_c && !was_branch_r;

    // ===== next PC =====
    always_comb begin
        if (start_i)
            pc_in_c = 12'h0;
        else if (!commit_c)
            pc_in_c = pc_r;
        else if (taken_c)
            pc_in_c = (ctrl_op_i == CTRL_RET) ? shadow_r[11:0] : target_c;
        else if (loop_taken_c)
            pc_in_c = loop_tgt_r;
        else
            pc_in_c = pc_r + 12'd1;
    end

    // ===== branch bubble =====
    always_ff @(posedge clk) begin
        if (rst || start_i)
            was_branch_r <= 1'b0;
        else if (run_c) begin
            if (was_branch_r)                   was_branch_r <= 1'b0;
            else if (taken_c || loop_taken_c)   was_branch_r <= 1'b1;
        end
    end

    // ===== state update =====
    always_ff @(posedge clk) begin
        if (rst) begin
            pc_r          <= 12'h0;
            shadow_r      <= 16'h0;
            loop_cnt_r    <= 16'h0;
            loop_tgt_r    <= 12'h0;
            halted_r      <= 1'b1;
            error_r       <= 1'b0;
            error_code_r  <= 12'h0;
            irq_pending_r <= 1'b0;
            irq_code_r    <= 8'h0;
        end else if (reset_halt_i) begin
            pc_r          <= 12'h0;
            shadow_r      <= 16'h0;
            loop_cnt_r    <= 16'h0;
            loop_tgt_r    <= 12'h0;
            halted_r      <= 1'b1;
            error_r       <= 1'b0;
            error_code_r  <= 12'h0;
            irq_pending_r <= 1'b0;
            irq_code_r    <= 8'h0;
            was_branch_r  <= 1'b0;
        end else if (start_i) begin
            pc_r          <= 12'h0;
            halted_r      <= 1'b0;
            error_r       <= 1'b0;
            error_code_r  <= 12'h0;
            irq_pending_r <= 1'b0;
            irq_code_r    <= 8'h0;
            was_branch_r  <= 1'b0;            
        end else begin
            pc_r <= pc_in_c;

            // IRQ clear on ack (host read IRQ_CODE)
            if (irq_ack_i) irq_pending_r <= 1'b0;

            // LOOP count: explicit write wins over the automatic decrement
            if (loop_cnt_we_i)
                loop_cnt_r <= loop_cnt_i;
            else if (commit_c && loop_taken_c)
                loop_cnt_r <= loop_cnt_r - 16'd1;

            if (loop_tgt_we_i)
                loop_tgt_r <= loop_tgt_i;

            // SHADOW: CALL wins over an explicit write
            if (commit_c && do_ctrl_c && ctrl_op_i == CTRL_CALL)
                shadow_r <= {flag_v_i, flag_c_i, flag_n_i, flag_z_i, pc_r};
            else if (shadow_we_i)
                shadow_r <= shadow_i;

            // HALT / IRQ
            if (commit_c && do_ctrl_c) begin
                case (ctrl_op_i)
                    CTRL_HALT: begin
                        if (addr_i == 12'h0) halted_r <= 1'b1;
                        else begin
                            error_r      <= 1'b1;
                            error_code_r <= addr_i;
                        end
                    end
                    CTRL_IRQ: begin
                        irq_pending_r <= 1'b1;
                        irq_code_r    <= addr_i[7:0];
                    end
                    default: ;
                endcase
            end
        end
    end

endmodule
