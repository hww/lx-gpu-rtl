`timescale 1ns/1ps
`default_nettype none

// 16-bit ALU, purely combinational. Opcode is class-based (alu_op[4:3]):
//   00 logic : op[2:1] = MOV/AND/OR/XOR,  op[0] = invert B
//   01 arith : op[1]   = use carry flag,  op[0] = subtract
//              ADD=8  SUB=9  ADC=10  SBC=11  (op[2] reserved)
//   10 shift : op[2:1] = ROR/SHR/SAR, amount = b[3:0] (mod 16 for all)
//   11 mul   : signed 16x16 -> 32, result = low 16, mul_hi = high 16
//
// Write / no-write is decided by the WRITE bit in top (CMP = SUB with
// WRITE=0, TST = AND with WRITE=0). MHI register lives in top.
// Flags: Z, N always; C, V only for arithmetic (upd_cv = 1).
// C = carry out (= not borrow for SUB/SBC).
module alu (
    input  logic [4:0]  alu_op,
    input  logic [15:0] a,
    input  logic [15:0] b,
    input  logic        carry_in,
    output logic [15:0] result,
    output logic [15:0] mul_hi,
    output logic        flag_z,
    output logic        flag_n,
    output logic        flag_c,
    output logic        flag_v,
    output logic        upd_cv
);

    // ===== decode =====
    logic [1:0] op_class, op_fn;
    logic       is_logic, is_arith, is_shift, is_mul;
    logic       inv_b, sub, use_carry;

    assign op_class  = alu_op[4:3];
    assign op_fn     = alu_op[2:1];
    assign is_logic  = (op_class == 2'b00);
    assign is_arith  = (op_class == 2'b01);
    assign is_shift  = (op_class == 2'b10);
    assign is_mul    = (op_class == 2'b11);   // reserved codes act as MUL
    assign inv_b     = is_logic & alu_op[0];
    assign sub       = is_arith & alu_op[0];
    assign use_carry = is_arith & alu_op[1];
    assign upd_cv    = is_arith;

    // ===== operand B, inverted for ANDN/ORN/XNOR/MVN and SUB/SBC =====
    logic [15:0] b_x;
    assign b_x = b ^ {16{inv_b | sub}};

    // ===== one adder with carry-in as an extra low bit =====
    logic        cin;
    logic [17:0] sum_ext;
    logic [16:0] sum;
    assign cin     = use_carry ? carry_in : sub;
    assign sum_ext = {1'b0, a, 1'b1} + {1'b0, b_x, cin};
    assign sum     = sum_ext[17:1];           // sum[16] = carry out

    // ===== logic =====
    logic [15:0] logic_res;
    always_comb begin
        case (op_fn)
            2'b00:   logic_res = b_x;
            2'b01:   logic_res = a & b_x;
            2'b10:   logic_res = a | b_x;
            default: logic_res = a ^ b_x;
        endcase
    end

    // ===== one right shifter: ROR / SHR / SAR (amount mod 16) =====
    logic [3:0]  shamt;
    logic        is_ror, fill;
    logic [15:0] s0, s1, s2, s3;

    assign shamt  = b[3:0];
    assign is_ror = (op_fn == 2'b00);
    assign fill   = (op_fn == 2'b10) & a[15];   // SAR: sign, SHR: 0

    assign s0 = shamt[0] ? {is_ror ? a[0]    : fill,      a[15:1]}  : a;
    assign s1 = shamt[1] ? {is_ror ? s0[1:0] : {2{fill}}, s0[15:2]} : s0;
    assign s2 = shamt[2] ? {is_ror ? s1[3:0] : {4{fill}}, s1[15:4]} : s1;
    assign s3 = shamt[3] ? {is_ror ? s2[7:0] : {8{fill}}, s2[15:8]} : s2;

    // ===== signed 16x16 multiply =====
    logic signed [15:0] a_s, b_s;
    logic signed [31:0] mul_p;
    assign a_s    = a;
    assign b_s    = b;
    assign mul_p  = a_s * b_s;
    assign mul_hi = mul_p[31:16];

    // ===== result =====
    always_comb begin
        case (op_class)
            2'b00:   result = logic_res;
            2'b01:   result = sum[15:0];
            2'b10:   result = s3;
            default: result = mul_p[15:0];
        endcase
    end

    // ===== flags =====
    assign flag_z = (result == 16'h0);
    assign flag_n = result[15];
    assign flag_c = sum[16];
    assign flag_v = (a[15] == b_x[15]) && (sum[15] != a[15]);

endmodule
