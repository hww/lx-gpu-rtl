`timescale 1ns/1ps
`default_nettype none

// Program counter, SHADOW, LOOP.CNT/TGT, flags, next-PC selection.
// One instruction per cycle (model). Ports may stall the sequencer.
//
// Priority for next PC:
//   1. HALT (stops, PC not updated)
//   2. Taken JMP / CALL / RET / JMPR (L ignored, CNT not decremented)
//   3. L with CNT > 1 (PC = LOOP.TGT, CNT--)
//   4. PC + 1
//
// L is allowed on any class. A taken branch overrides L.
// CALL writes SHADOW = {flags[3:0], PC+1[11:0]}.
// RET uses SHADOW[11:0] only (flags not restored).
// JMPR uses operand REG[11:0].
// start resets PC, ERROR, error_code.
module sequencer (
    input  logic        clk,
    input  logic        rst,

    // instruction fields
    input  logic [1:0]  imm_class,   // {J, I}: 00=reg-reg, 01=LDI (unused), 10=ctrl, 11=reg-imm12
    input  logic [4:0]  reg_op,      // ALU op or CTRL op
    input  logic [3:0]  cond,
    input  logic        l,           // loop flag
    input  logic [11:0] imm12,       // ADDR / IMM12

    // current flags
    input  logic        z, n, c, v,

    // from ALU
    input  logic [11:0] jmpr_target, // operand[REG][11:0] for JMPR

    // outputs
    output logic [11:0] pc,
    output logic [11:0] pc_next,
    output logic [15:0] shadow,
    output logic [15:0] loop_cnt,    // CNT (16 bits)
    output logic [11:0] loop_tgt,    // TGT (12 bits)
    output logic        halted,
    output logic        error,
    output logic [11:0] error_code,
    output logic        irq_pending,
    output logic [11:0] irq_code,

    input  logic        start,
    input  logic        stall,

    // test-only: write LOOP.CNT / LOOP.TGT
    input  logic        loop_cnt_we,
    input  logic [15:0] loop_cnt_in,
    input  logic        loop_tgt_we,
    input  logic [11:0] loop_tgt_in
);

    // ===== condition decode =====
    function automatic logic cond_true(input logic [3:0] cd,
                                       input logic zz, nn, cc, vv);
        case (cd)
            4'd0:  cond_true = 1'b1;                       // AL
            4'd1:  cond_true = zz;                         // EQ
            4'd2:  cond_true = ~zz;                        // NE
            4'd3:  cond_true = cc;                         // CS
            4'd4:  cond_true = ~cc;                        // CC
            4'd5:  cond_true = nn;                         // MI
            4'd6:  cond_true = ~nn;                        // PL
            4'd7:  cond_true = vv;                         // VS
            4'd8:  cond_true = ~vv;                        // VC
            4'd9:  cond_true = cc & ~zz;                   // HI
            4'd10: cond_true = ~cc | zz;                   // LS
            4'd11: cond_true = (nn == vv);                 // GE
            4'd12: cond_true = (nn != vv);                 // LT
            4'd13: cond_true = ~zz & (nn == vv);           // GT
            4'd14: cond_true = zz | (nn != vv);            // LE
            default: cond_true = 1'b0;
        endcase
    endfunction

    // ===== CTRL op codes =====
    localparam logic [4:0] C_NOP  = 5'd0;
    localparam logic [4:0] C_JMP  = 5'd1;
    localparam logic [4:0] C_CALL = 5'd2;
    localparam logic [4:0] C_RET  = 5'd3;
    localparam logic [4:0] C_JMPR = 5'd4;
    localparam logic [4:0] C_HALT = 5'd5;
    localparam logic [4:0] C_IRQ  = 5'd6;

    logic is_ctrl;
    logic active;
    logic do_ctrl;
    logic taken_branch;
    logic [11:0] pc_next_c;

    assign is_ctrl = (imm_class == 2'b10);
    assign active  = cond_true(cond, z, n, c, v);
    assign do_ctrl = is_ctrl && active;

    // taken_branch: JMP/CALL/RET/JMPR, taken
    always_comb begin
        taken_branch = 1'b0;
        if (do_ctrl) begin
            case (reg_op)
                C_JMP, C_CALL, C_RET, C_JMPR: taken_branch = 1'b1;
                default: taken_branch = 1'b0;
            endcase
        end
    end

    // ===== combinational next-PC =====
    always_comb begin
        pc_next_c = pc + 12'd1;

        // 1. taken branch overrides everything (L ignored)
        if (taken_branch) begin
            case (reg_op)
                C_JMP, C_CALL: pc_next_c = imm12;
                C_RET:         pc_next_c = shadow[11:0];
                C_JMPR:        pc_next_c = jmpr_target;
                default:       pc_next_c = pc + 12'd1;
            endcase
        end
        // 2. L with CNT > 1 (only if no taken branch)
        else if (l && (loop_cnt > 16'd1)) begin
            pc_next_c = loop_tgt;
        end
        // 3. PC + 1 (default)
    end

    assign pc_next = pc_next_c;

    // ===== state =====
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
        end else if (start) begin
            pc          <= 12'h0;
            halted      <= 1'b0;
            error       <= 1'b0;
            error_code  <= 12'h0;
            irq_pending <= 1'b0;
            irq_code    <= 12'h0;
        end else if (halted || error || stall) begin
            // stay put
        end else begin
            pc <= pc_next_c;

            // LOOP.CNT / LOOP.TGT update (test-only, via ports)
            if (loop_cnt_we) loop_cnt <= loop_cnt_in;
            else if (l && !taken_branch && (loop_cnt > 16'd1))
                loop_cnt <= loop_cnt - 16'd1;
            if (loop_tgt_we) loop_tgt <= loop_tgt_in;

            // control side effects
            if (do_ctrl) begin
                case (reg_op)
                    C_CALL:
                            // ^^^ wait: SHADOW = {flags[3:0], PC+1[11:0]}
                            // flags = {v, c, n, z} — 4 bits
                            // so: shadow <= {v, c, n, z, pc + 12'd1};
                            C_CALL: shadow <= {v, c, n, z, pc + 12'd1};
                    C_HALT: begin
                        if (imm12 == 12'h0) halted <= 1'b1;
                        else begin
                            error      <= 1'b1;
                            error_code <= imm12;
                        end
                    end
                    C_IRQ: begin
                        irq_pending <= 1'b1;
                        irq_code    <= imm12;
                    end
                    default: ;
                endcase
            end
        end
    end

endmodule