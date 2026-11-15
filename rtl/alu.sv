`timescale 1ns/1ps
`default_nettype none

// 16-bit ALU with class-based opcode (op[4:3]).
//   00 = logic:    op[2:1] = MOV/AND/OR/XOR, op[0] = inv
//   01 = arith:    op[2:1] = ADD/ADC/SUB/SBC, op[0] = sub
//   10 = shift:    op[2:1] = ROR/SHR/SAR
//   11 = other:    op[2:1] = MUL/RSV/TST/CMP
//
// ALU — чистая комбинационная машина. Решение "писать / не писать"
// принимается в top.sv по биту write инструкции.
//
// Flags: Z N C V. C = carry out (not borrow for SUB/SBC/CMP).
// MUL: signed 16x16 -> 32, low 16 to res, high 16 to mhi (registered).
module alu (
    input  logic        clk,
    input  logic        rst,
    input  logic [4:0]  op,
    input  logic [15:0] a,
    input  logic [15:0] b,
    input  logic        c_in,        // для ADC/SBC
    output logic [15:0] res,
    output logic [15:0] mhi,         // старшие 16 бит последнего MUL
    output logic        z,
    output logic        n,
    output logic        c,
    output logic        v
);

    // ===== decode =====
    logic [1:0] cls;
    logic [1:0] fn;
    logic       inv;
    logic       sub;
    logic       cin_sel;

    assign cls     = op[4:3];
    assign fn      = op[2:1];
    assign inv     = (cls == 2'b00) ? op[0] : 1'b0;
    assign sub     = (cls == 2'b01) ? op[0] : (cls == 2'b11 && fn == 2'b11) ? 1'b1 : 1'b0;
    assign cin_sel = (cls == 2'b01) ? op[1] : 1'b0;

    // ===== operand B =====
    logic [15:0] b_x;
    assign b_x = b ^ {16{inv | sub}};

    // ===== сумматор =====
    logic        cin;
    logic [16:0] sum;
    assign cin = cin_sel ? c_in : (inv | sub);
    assign sum = 17'({1'b0, a} + {1'b0, b_x} + {17'd0, cin});

    // ===== логика =====
    logic [15:0] logic_res;
    always_comb begin
        case (fn)
            2'b00: logic_res = b_x;
            2'b01: logic_res = a & b_x;
            2'b10: logic_res = a | b_x;
            2'b11: logic_res = a ^ b_x;
        endcase
    end

    // ===== сдвигатель (mod 16) =====
    logic [3:0]  sh;
    logic        fill;
    logic [15:0] s0, s1, s2, s3;
    logic        is_ror;

    assign sh     = b[3:0];
    assign is_ror = (fn == 2'b00);
    assign fill   = (fn == 2'b10) & a[15];

    assign s0 = sh[0] ? {is_ror ? a[0] : fill, a[15:1]} : a;
    assign s1 = sh[1] ? {is_ror ? s0[1:0] : {2{fill}}, s0[15:2]} : s0;
    assign s2 = sh[2] ? {is_ror ? s1[3:0] : {4{fill}}, s1[15:4]} : s1;
    assign s3 = sh[3] ? {is_ror ? s2[7:0] : {8{fill}}, s2[15:8]} : s2;

    // ===== MUL (signed 16x16 -> 32) =====
    logic signed [15:0] a_s, b_s;
    logic signed [31:0] mul_p;
    assign a_s = a;
    assign b_s = b;
    assign mul_p = a_s * b_s;

    logic [15:0] mul_lo, mul_hi;
    assign mul_lo = mul_p[15:0];
    assign mul_hi = mul_p[31:16];

    always_ff @(posedge clk) begin
        if (rst) mhi <= 16'h0;
        else if (cls == 2'b11 && fn == 2'b00) mhi <= mul_hi;
    end

    // ===== выбор результата =====
    always_comb begin
        res = a;
        case (cls)
            2'b00: res = logic_res;
            2'b01: res = sum[15:0];
            2'b10: res = s3;
            2'b11: res = (fn == 2'b00) ? mul_lo : a;
        endcase
    end

    // ===== флаги =====
    logic [15:0] flag_res;
    assign flag_res = (cls == 2'b11 && fn == 2'b10) ? (a & b_x) :   // TST
                      (cls == 2'b11 && fn == 2'b11) ? sum[15:0] :  // CMP
                      res;

    assign z = (flag_res == 16'h0);
    assign n = flag_res[15];
    assign c = (cls == 2'b01) ? sum[16] : (cls == 2'b11 && fn == 2'b11) ? sum[16] : 1'b0;
    assign v = (cls == 2'b01) ?
               ((a[15] == b_x[15]) && (sum[15] != a[15])) : 1'b0;

endmodule
