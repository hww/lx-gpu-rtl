`timescale 1ns/1ps
`default_nettype none

// core_cond: ARM-style condition test. flags_i = {V, C, N, Z}.
module core_cond (
    input  logic [3:0] cond_i,
    input  logic [3:0] flags_i,
    output logic       ok_o
);
    logic z, n, c, v;
    assign {v, c, n, z} = flags_i;

    always_comb begin
        case (cond_i)
            4'd0:    ok_o = 1'b1;               // AL
            4'd1:    ok_o = z;                  // EQ
            4'd2:    ok_o = ~z;                 // NE
            4'd3:    ok_o = c;                  // CS
            4'd4:    ok_o = ~c;                 // CC
            4'd5:    ok_o = n;                  // MI
            4'd6:    ok_o = ~n;                 // PL
            4'd7:    ok_o = v;                  // VS
            4'd8:    ok_o = ~v;                 // VC
            4'd9:    ok_o = c & ~z;             // HI
            4'd10:   ok_o = ~c | z;             // LS
            4'd11:   ok_o = (n == v);           // GE
            4'd12:   ok_o = (n != v);           // LT
            4'd13:   ok_o = ~z & (n == v);      // GT
            4'd14:   ok_o = z | (n != v);       // LE
            default: ok_o = 1'b0;               // reserved: never
        endcase
    end
endmodule
