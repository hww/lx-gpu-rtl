`timescale 1ns/1ps
module alu (
    input  logic [4:0]  op,
    input  logic [23:0] a,
    input  logic [23:0] b,
    output logic [23:0] res,
    output logic        z,
    output logic        n,
    output logic        c,
    output logic        v
);

    // ===== op decode =====
    // 0 MOV, 1 AND, 2 OR, 3 XOR, 4 ANDN, 5 ADD, 6 SUB, 7 CMP, 8 TST,
    // 9 ROR, 10 SHR, 11 SAR, 12 MUL
    logic        sub;       // 1 for SUB/CMP
    logic        is_arith;  // ADD/SUB/CMP
    logic        is_shift;  // ROR/SHR/SAR
    logic        is_logic;  // MOV/AND/OR/XOR/ANDN/TST
    logic        is_mul;

    assign sub      = (op == 5'd6) || (op == 5'd7);
    assign is_arith = (op == 5'd5) || sub;
    assign is_shift = (op >= 5'd9) && (op <= 5'd11);
    assign is_logic = (op <= 5'd4) || (op == 5'd8);
    assign is_mul   = (op == 5'd12);

    // ===== one adder: a + (b ^ {24{sub}}) + sub =====
    logic [24:0] sum;
    logic [23:0] b_x;
    assign b_x = b ^ {24{sub}};
    assign sum = {1'b0, a} + {1'b0, b_x} + {24'h0, sub};

    // ===== one ROR cascade (24-bit) =====
    logic [4:0]  sh;        // shift amount (0..23)
    logic [23:0] ror;
    logic [23:0] stage1, stage2, stage3, stage4, stage5;

    assign sh = (b[4:0] >= 5'd24) ? (b[4:0] - 5'd24) : b[4:0];  // ROR: mod 24

    // stage 1: shift by sh[0] (0 or 1)
    assign stage1 = sh[0] ? {a[0], a[23:1]} : a;
    // stage 2: shift by sh[1] ? 2 : 0
    assign stage2 = sh[1] ? {stage1[1:0], stage1[23:2]} : stage1;
    // stage 3: shift by sh[2] ? 4 : 0
    assign stage3 = sh[2] ? {stage2[3:0], stage2[23:4]} : stage2;
    // stage 4: shift by sh[3] ? 8 : 0
    assign stage4 = sh[3] ? {stage3[7:0], stage3[23:8]} : stage3;
    // stage 5: shift by sh[4] ? 16 : 0 (only 0..15 needed for 24-bit, so sh[4]=1 -> sh=16..23 -> use (sh-16)=0..7)
    // simpler: for 24-bit, sh max 23 -> sh[4] = 1 only when sh >= 16
    assign stage5 = sh[4] ? {stage4[15:0], stage4[23:16]} : stage4;
    assign ror = stage5;

    // ===== SHR / SAR via mask =====
    logic [23:0] shr, sar;
    logic [23:0] mask;    // 1 for bits that survive
    logic [23:0] sign_fill;

    // mask: bits [23:sh] = 1, bits [sh-1:0] = 0
    // mask = (24'hFFFFFF >> sh)  but sh=0 -> all 1
    assign mask = (b[4:0] >= 5'd24) ? 24'h0 : (24'hFFFFFF >> sh);  // SHR/SAR: b>=24 -> 0
    // SHR: a >> sh, but we don't have a >> sh directly. Use ROR: ror = (a >> sh) | (a << (24-sh))
    // SHR result = ror & mask  (surviving low bits = a[23:sh] shifted to [23-sh:0])
    // wait: ROR right by sh gives: bits [23:sh] of a in [23-sh:0], bits [sh-1:0] of a in [23:24-sh]
    // SHR = (ror & mask) gives (a >> sh) in low bits, zeros in high bits. Correct.
    assign shr = ror & mask;

    // SAR: sign-fill high bits with a[23]
    assign sign_fill = {24{a[23]}};
    // SAR = (ror & mask) | (sign_fill & ~mask)  -> high bits = sign, low = a >> sh
    assign sar = (ror & mask) | (sign_fill & ~mask);

    // ===== logic ops (per-bit LUT) =====
    // op[2:0] = fn: 0 MOV, 1 AND, 2 OR, 3 XOR, 4 ANDN, 8 TST (AND)
    // per-bit truth table:
    //   0 (MOV):  b
    //   1 (AND):  a & b
    //   2 (OR):   a | b
    //   3 (XOR):  a ^ b
    //   4 (ANDN): a & ~b
    //   8 (TST):  a & b
    logic [2:0] fn;
    logic [23:0] logic_res;

    assign fn = op[2:0];  // 0..7 covers MOV/AND/OR/XOR/ANDN; TST=8 -> fn=0, handled below
    // per-bit LUT using AND/OR/XOR/ANDN + MOV
    logic [23:0] and_r, or_r, xor_r, andn_r;
    assign and_r  = a & b;
    assign or_r   = a | b;
    assign xor_r  = a ^ b;
    assign andn_r = a & ~b;

    always_comb begin
        case (op)
            5'd0: logic_res = b;         // MOV
            5'd1: logic_res = and_r;     // AND
            5'd2: logic_res = or_r;      // OR
            5'd3: logic_res = xor_r;     // XOR
            5'd4: logic_res = andn_r;    // ANDN
            5'd8: logic_res = and_r;     // TST
            default: logic_res = a;
        endcase
    end

    // ===== MUL: 18x18 -> 24 (one ECP5 DSP) =====
    logic signed [17:0] a18, b18;
    logic signed [35:0] mul_p;
    assign a18 = a[17:0];
    assign b18 = b[17:0];
    assign mul_p = a18 * b18;
    logic [23:0] mul_res;
    assign mul_res = mul_p[23:0];

    // ===== final result mux =====
    always_comb begin
        res = a;
        c   = 1'b0;
        v   = 1'b0;

        if (is_arith) begin
            res = sum[23:0];
            c   = sub ? !sum[24] : sum[24];
            v   = sub ? ((a[23] != b[23]) && (res[23] != a[23]))
                      : ((a[23] == b[23]) && (res[23] != a[23]));
        end else if (is_shift) begin
            case (op)
                5'd9:  res = ror;
                5'd10: res = shr;
                5'd11: res = sar;
            endcase
        end else if (is_mul) begin
            res = mul_res;
        end else begin
            res = logic_res;
        end

        z = (res == 24'h0);
        n = res[23];
    end

endmodule