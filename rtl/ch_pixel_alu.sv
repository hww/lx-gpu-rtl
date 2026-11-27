`timescale 1ns/1ps
`default_nettype none

// ch_pixel_alu: pixel ALU / combiner of channels v2 (spec section 5).
//
// Purely combinational, one stage, no multiplier. All operands are unsigned
// W-bit values; W is given by mask_i (fmt.pix_mask: 16/8/4/2/1). The datapath
// is 16 bits wide, bits above W are forced to 0 on entry and on the result.
//
//   A  pixel written by the processor      B  pen constant (PEN bank, group 0)
//   D  destination pixel read from memory  K  key / reference (PEN group 1)
//
// ALU_CTL: [3:0] OP  [5:4] SX  [7:6] SY  [9:8] CS  [12:10] REL
//          [13] DEN  [14] DPAR  [15] reserved
//
// need_d_o tells the channel whether D has to be read from memory before the
// result and the write condition are valid. While need_d_o = 0, d_i is not
// used by r_o / rel_ok_o and may be anything.
module ch_pixel_alu (
    input  logic [15:0] a_i,
    input  logic [15:0] b_i,
    input  logic [15:0] d_i,
    input  logic [15:0] k_i,
    input  logic [15:0] ctl_i,
    input  logic [15:0] mask_i,     // W-bit mask
    input  logic        shr_x_i,    // MODE.SHR_X
    input  logic        shr_y_i,    // MODE.SHR_Y
    input  logic        par_i,      // (XI ^ YI)[0], pre-advance

    output logic [15:0] r_o,        // ALU result (already masked to W)
    output logic        rel_ok_o,   // REL condition on the CS pair
    output logic        den_ok_o,   // screen-door gate
    output logic        need_d_o    // D must be read first
);

    // ===== field decode =====
    logic [3:0] op;
    logic [1:0] sx, sy, cs;
    logic [2:0] rel;
    logic       den, dpar;

    assign op   = ctl_i[3:0];
    assign sx   = ctl_i[5:4];
    assign sy   = ctl_i[7:6];
    assign cs   = ctl_i[9:8];
    assign rel  = ctl_i[12:10];
    assign den  = ctl_i[13];
    assign dpar = ctl_i[14];

    // ===== operands, forced to W bits =====
    logic [15:0] a, b, d, k;
    assign a = a_i & mask_i;
    assign b = b_i & mask_i;
    assign d = d_i & mask_i;
    assign k = k_i & mask_i;

    // ===== X / Y selection and SHR (logical >> 1, after selection) =====
    logic [15:0] x_sel, y_sel, x, y;
    always_comb begin
        case (sx)
            2'd0:    x_sel = a;
            2'd1:    x_sel = b;
            2'd2:    x_sel = d;
            default: x_sel = k;
        endcase
        case (sy)
            2'd0:    y_sel = a;
            2'd1:    y_sel = b;
            2'd2:    y_sel = d;
            default: y_sel = k;
        endcase
    end
    assign x = shr_x_i ? {1'b0, x_sel[15:1]} : x_sel;
    assign y = shr_y_i ? {1'b0, y_sel[15:1]} : y_sel;

    // ===== one adder / one subtractor =====
    // Saturation: the carry is bit W of the sum (any sum bit above the mask),
    // SUBS uses the borrow. Valid for every W, no special sub-byte case.
    logic [16:0] sum, dif;
    logic        over, under;
    assign sum   = {1'b0, x} + {1'b0, y};
    assign dif   = {1'b0, x} - {1'b0, y};
    assign over  = |(sum & ~{1'b0, mask_i});
    assign under = dif[16];

    // ===== operation =====
    logic [15:0] r_raw;
    always_comb begin
        case (op)
            4'd0:    r_raw = x;                              // PASS
            4'd1:    r_raw = x & y;                          // AND
            4'd2:    r_raw = x | y;                          // OR
            4'd3:    r_raw = x ^ y;                          // XOR
            4'd4:    r_raw = x & ~y;                         // ANDN
            4'd5:    r_raw = sum[15:0];                      // ADD (mod 2^W)
            4'd6:    r_raw = over  ? mask_i : sum[15:0];     // ADDS
            4'd7:    r_raw = under ? 16'h0000 : dif[15:0];   // SUBS
            4'd8:    r_raw = (x & b) | (y & ~b);             // MSK (B not shifted)
            default: r_raw = x;                              // reserved -> PASS
        endcase
    end
    assign r_o = r_raw & mask_i;

    // ===== write condition (unsigned, on the unshifted operands) =====
    logic [15:0] cp, cq;
    always_comb begin
        case (cs)
            2'd0:    begin cp = a; cq = k; end   // A ? K
            2'd1:    begin cp = d; cq = k; end   // D ? K
            2'd2:    begin cp = a; cq = d; end   // A ? D
            default: begin cp = d; cq = b; end   // D ? B
        endcase
    end

    logic eq, lt;
    assign eq = (cp == cq);
    assign lt = (cp <  cq);

    always_comb begin
        case (rel)
            3'd0:    rel_ok_o = 1'b1;            // always
            3'd1:    rel_ok_o = eq;              // ==
            3'd2:    rel_ok_o = ~eq;             // !=
            3'd3:    rel_ok_o = lt;              // <
            3'd4:    rel_ok_o = ~lt;             // >=
            3'd5:    rel_ok_o = ~lt & ~eq;       // >
            3'd6:    rel_ok_o = lt | eq;         // <=
            default: rel_ok_o = 1'b0;            // never
        endcase
    end

    // ===== screen-door gate =====
    assign den_ok_o = ~den | (par_i == dpar);

    // ===== NEED_D =====
    // (SX == D) | (SY == D) | (REL in {==, !=, <, >=, >, <=} & CS != 0)
    assign need_d_o = (sx == 2'd2) | (sy == 2'd2)
                    | ((rel != 3'd0) & (rel != 3'd7) & (cs != 2'd0));

endmodule
