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
    logic sub, is_arith, is_shift, is_logic, is_mul;
    assign sub      = (op == 5'd6) || (op == 5'd7);       // SUB, CMP
    assign is_arith = (op == 5'd5) || sub;                // ADD, SUB, CMP
    assign is_shift = (op >= 5'd9) && (op <= 5'd11);      // ROR, SHR, SAR
    assign is_logic = (op <= 5'd4) || (op == 5'd8);       // MOV..ANDN, TST
    assign is_mul   = (op == 5'd12);                      // MUL

    // ===== one adder: a + (b ^ {24{sub}}) + sub =====
    logic [23:0] b_x;
    logic [25:0] sum_ext;
    logic [24:0] sum;
    assign b_x     = b ^ {24{sub}};
    assign sum_ext = {1'b0, a, sub} + {1'b0, b_x, 1'b0};
    assign sum     = sum_ext[25:1];

    // ===== one right shifter: ROR / SHR / SAR =====
    logic        is_ror;
    logic [4:0]  sh;
    logic        fill;
    logic [23:0] s0, s1, s2, s3, s4;

    assign is_ror = (op == 5'd9);
    // ROR: mod 24 (b=24..31 -> b-24 = b[2:0]); SHR/SAR: b as is
    assign sh   = (is_ror && b[4] && b[3]) ? {2'b00, b[2:0]} : b[4:0];
    assign fill = (op == 5'd11) & a[23];   // SAR: sign, SHR: 0

    assign s0 = sh[0] ? {is_ror ? a[0]     : fill,       a[23:1]}   : a;
    assign s1 = sh[1] ? {is_ror ? s0[1:0]  : {2{fill}},  s0[23:2]}  : s0;
    assign s2 = sh[2] ? {is_ror ? s1[3:0]  : {4{fill}},  s1[23:4]}  : s1;
    assign s3 = sh[3] ? {is_ror ? s2[7:0]  : {8{fill}},  s2[23:8]}  : s2;
    assign s4 = sh[4] ? {is_ror ? s3[15:0] : {16{fill}}, s3[23:16]} : s3;

    // ===== logic ops =====
    logic [23:0] logic_res;
    always_comb begin
        case (op)
            5'd0: logic_res = b;          // MOV
            5'd1: logic_res = a & b;      // AND
            5'd2: logic_res = a | b;      // OR
            5'd3: logic_res = a ^ b;      // XOR
            5'd4: logic_res = a & ~b;     // ANDN
            5'd8: logic_res = a & b;      // TST
            default: logic_res = a;
        endcase
    end

    // ===== MUL: 18x18 -> 24 (one ECP5 DSP) =====
    logic signed [17:0] a18, b18;
    logic signed [35:0] mul_p;
    assign a18 = a[17:0];
    assign b18 = b[17:0];
    assign mul_p = a18 * b18;

    // ===== result mux =====
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
            res = s4;
        end else if (is_mul) begin
            res = mul_p[23:0];
        end else begin
            res = logic_res;
        end

        z = (res == 24'h0);
        n = res[23];
    end

endmodule