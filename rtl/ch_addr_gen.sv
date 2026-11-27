`timescale 1ns/1ps
`default_nettype none

// ch_addr_gen (v2): per-channel state, position generator, clipping and the
// pixel index -> word / fraction conversion.
//
// All registers of the four channels are flip-flops (no BRAM): switching the
// channel costs nothing, which matters for loops that alternate between a
// source and a destination channel on every pixel. A single multiplier
// (YI x PITCH) and a single shifter serve the channel selected by `ach`.
//
// Banked register window (spec section 2). group = dst[3:2]:
//   bank 0 POS : 0 XI   1 YI   2 XF   3 YF
//   bank 1 STEP: 0 DXI  1 DYI  2 DXF  3 DYF
//   bank 2 CLIP: 0 XMIN 1 YMIN 2 XMAX 3 YMAX
//   bank 3 PEN : 0 B    1 KEY  2 ALU_CTL  3 -
//   bank 4 SURF: 0 MODE_PAGE   1 PITCH    2,3 -
//   banks 5..7 reserved: writes ignored.
// MODE_PAGE: [7:0] PAGE (kept by the cache), [10:8] FMT, [12] CLIP_ON,
//            [13] SHR_X, [14] SHR_Y.
//
// Pixel index:
//   PITCH != 0 : YI * PITCH + XI                      (2D mode)
//   PITCH == 0 : {YI[2:0], XI[15:0]}                  (linear mode, 19 bits;
//                the carry out of XI goes into YI[2:0], as the old IDX/IDXH)
// The index goes through ({idx, 4'b0} >>> k): word = [18:4], frac = [3:0].
// Anything non-zero above bit 18 (negative index included) is a page overflow.
//
// Timing: the address of every channel (word, frac, overflow, clip test,
// parity) is kept in registers. One shared address unit (multiplier, shifter,
// clip compares) recomputes the address of a channel whose position or
// surface changed, one channel per cycle. An access waits for rdy_o, so the
// multiplier is never in the path of a cache access. The address is ready
// one cycle after a register write or a step: a channel accessed on every
// instruction waits one cycle; two channels used alternately do not wait.
module ch_addr_gen (
    input  logic        clk,
    input  logic        rst,

    // channel of the instruction in X (garbage when it is not a channel op)
    input  logic [1:0]  ch_i,

    // banked register write
    input  logic        reg_we_i,
    input  logic [2:0]  bank_i,
    input  logic [1:0]  grp_i,
    input  logic [15:0] wdata_i,

    // host START preload of the channel given by ch_i (ch_top forces 0):
    // 16 bpp, linear mode, XI = pl_idx_i, DXI = 1, everything else cleared
    input  logic        pl_we_i,
    input  logic [15:0] pl_idx_i,

    // access handshake
    input  logic        req_i,      // a DATA access to ch_i is requested now
    input  logic        adv_i,      // it completes in this cycle: pos += step
    input  logic        rd_i,       // ... and it read the cache (arms prefetch)

    // channel the address / config outputs refer to (feeds the cache too)
    output logic [1:0]  ach_o,

    // address of the current position
    output logic [14:0] word_o,
    output logic [3:0]  frac_o,
    output logic        ovf_o,      // index outside the 64 KB page
    output logic        inside_o,   // inside the clip window (1 if CLIP_ON = 0)
    output logic        par_o,      // (XI ^ YI)[0]
    output logic        rdy_o,      // the outputs above are valid for ach_o

    // mode / pen of the channel
    output logic [2:0]  mode_o,     // FMT
    output logic        shr_x_o,
    output logic        shr_y_o,
    output logic [15:0] b_o,
    output logic [15:0] key_o,
    output logic [15:0] ctl_o,

    output logic        prefetch_o
);

    `include "rtl/gpu_fmt_pkg.sv"

    localparam int NUM_CH = 4;

    // ===== per-channel registers =====
    logic [15:0] xi_r   [0:NUM_CH-1];
    logic [15:0] xf_r   [0:NUM_CH-1];
    logic [15:0] yi_r   [0:NUM_CH-1];
    logic [15:0] yf_r   [0:NUM_CH-1];
    logic [15:0] dxi_r  [0:NUM_CH-1];
    logic [15:0] dxf_r  [0:NUM_CH-1];
    logic [15:0] dyi_r  [0:NUM_CH-1];
    logic [15:0] dyf_r  [0:NUM_CH-1];
    logic [15:0] xmin_r [0:NUM_CH-1];
    logic [15:0] ymin_r [0:NUM_CH-1];
    logic [15:0] xmax_r [0:NUM_CH-1];
    logic [15:0] ymax_r [0:NUM_CH-1];
    logic [15:0] b_r    [0:NUM_CH-1];
    logic [15:0] key_r  [0:NUM_CH-1];
    logic [15:0] ctl_r  [0:NUM_CH-1];
    logic [15:0] pitch_r[0:NUM_CH-1];
    logic [2:0]  fmt_r  [0:NUM_CH-1];
    logic        clip_r [0:NUM_CH-1];
    logic        shrx_r [0:NUM_CH-1];
    logic        shry_r [0:NUM_CH-1];

    // prefetch state
    logic [NUM_CH-1:0] pf_arm_r;
    logic [1:0]        last_ch_r;
    logic              prefetch_r;
    logic [1:0]        pf_ch_r;

    // ===== channel the outputs refer to =====
    // While an access or a register write is going on it is the instruction's
    // channel; otherwise the channel of the last access, so that the prefetch
    // keeps working while the processor executes other instructions.
    logic [1:0] ach;
    assign ach   = (req_i || reg_we_i || pl_we_i) ? ch_i : last_ch_r;
    assign ach_o = ach;

    // ===== selected channel (position advance, outputs) =====
    logic [15:0] xi_c, xf_c, yi_c, yf_c, dxi_c, dxf_c, dyi_c, dyf_c;
    logic [15:0] pitch_c;

    always_comb begin
        xi_c    = xi_r[ach];    xf_c   = xf_r[ach];
        yi_c    = yi_r[ach];    yf_c   = yf_r[ach];
        dxi_c   = dxi_r[ach];   dxf_c  = dxf_r[ach];
        dyi_c   = dyi_r[ach];   dyf_c  = dyf_r[ach];
        pitch_c = pitch_r[ach];
    end

    assign mode_o  = fmt_r[ach];
    assign shr_x_o = shrx_r[ach];
    assign shr_y_o = shry_r[ach];
    assign b_o     = b_r[ach];
    assign key_o   = key_r[ach];
    assign ctl_o   = ctl_r[ach];

    // ===== registered address of every channel =====
    logic [14:0]       aw_r   [0:NUM_CH-1];
    logic [3:0]        af_r   [0:NUM_CH-1];
    logic [NUM_CH-1:0] aovf_r, ains_r, apar_r, av_r;

    assign word_o   = aw_r[ach];
    assign frac_o   = af_r[ach];
    assign ovf_o    = aovf_r[ach];
    assign inside_o = ains_r[ach];
    assign par_o    = apar_r[ach];
    assign rdy_o    = av_r[ach];

    // ===== shared address unit: lowest channel whose address is stale =====
    // Selected from registers only, so the unit does not depend on the core.
    logic [1:0] uc;
    logic       u_go;
    always_comb begin
        u_go = !(&av_r);
        uc   = !av_r[0] ? 2'd0 : !av_r[1] ? 2'd1 : !av_r[2] ? 2'd2 : 2'd3;
    end

    logic [15:0] u_xi, u_yi, u_pitch;
    logic [15:0] u_xmin, u_ymin, u_xmax, u_ymax;
    logic [2:0]  u_fmt;
    logic        u_clip;
    always_comb begin
        u_xi    = xi_r[uc];     u_yi   = yi_r[uc];    u_pitch = pitch_r[uc];
        u_xmin  = xmin_r[uc];   u_ymin = ymin_r[uc];
        u_xmax  = xmax_r[uc];   u_ymax = ymax_r[uc];
        u_fmt   = fmt_r[uc];    u_clip = clip_r[uc];
    end

    logic               u_lin, u_inside;
    logic signed [39:0] u_prod;
    logic        [39:0] u_pidx, u_sh_in;
    logic signed [39:0] u_sh_out;
    fmt_info_t          u_fi;

    assign u_inside = !u_clip ||
                      (($signed(u_xi) >= $signed(u_xmin)) && ($signed(u_xi) <= $signed(u_xmax)) &&
                       ($signed(u_yi) >= $signed(u_ymin)) && ($signed(u_yi) <= $signed(u_ymax)));
    assign u_lin    = (u_pitch == 16'h0000);
    assign u_prod   = $signed(u_yi) * $signed({1'b0, u_pitch});
    assign u_pidx   = u_lin ? {21'b0, u_yi[2:0], u_xi}
                            : (u_prod + {{24{u_xi[15]}}, u_xi});
    assign u_sh_in  = u_pidx << 4;
    assign u_fi     = get_fmt_info(u_fmt);
    assign u_sh_out = $signed(u_sh_in) >>> u_fi.k;

    logic lin_c;
    assign lin_c = (pitch_c == 16'h0000);

    // ===== position advance =====
    logic [34:0] l_sum;
    logic [31:0] x_sum, y_sum;
    assign l_sum = {yi_c[2:0], xi_c, xf_c} + {{3{dxi_c[15]}}, dxi_c, dxf_c};
    assign x_sum = {xi_c, xf_c} + {dxi_c, dxf_c};
    assign y_sum = {yi_c, yf_c} + {dyi_c, dyf_c};

    // ===== write decode =====
    logic wr_pos, wr_step, wr_clip, wr_pen, wr_surf;
    assign wr_pos  = reg_we_i && (bank_i == 3'd0);
    assign wr_step = reg_we_i && (bank_i == 3'd1);
    assign wr_clip = reg_we_i && (bank_i == 3'd2);
    assign wr_pen  = reg_we_i && (bank_i == 3'd3);
    assign wr_surf = reg_we_i && (bank_i == 3'd4);

    // ===== prefetch condition =====
    // pf_arm_r is a one-shot set by the access that read the cache; the
    // prefetch fires as soon as the channel is idle again, no need to
    // compare words (last_word_r used to be updated in the same cycle the
    // arm was set, so the compare was always false).
    logic pf_cond;
    assign pf_cond = pf_arm_r[ach] && rdy_o && inside_o && !ovf_o &&
                     !req_i && !reg_we_i && !pl_we_i;

    assign prefetch_o = prefetch_r && (ach == pf_ch_r) && rdy_o && !req_i && !reg_we_i && !pl_we_i;

    // ===== address invalidation: position, clip or surface of a channel =====
    logic [NUM_CH-1:0] inval;
    always_comb begin
        inval = '0;
        if (adv_i)                                   inval[ach]  = 1'b1;
        if (reg_we_i && (bank_i == 3'd0 || bank_i == 3'd2 || bank_i == 3'd4))
                                                     inval[ch_i] = 1'b1;
        if (pl_we_i)                                 inval[ch_i] = 1'b1;
    end

    always_ff @(posedge clk) begin
        if (rst) begin
            av_r <= '0;
        end else begin
            if (u_go && !inval[uc]) begin
                aw_r[uc]   <= u_sh_out[18:4];
                af_r[uc]   <= u_sh_out[3:0];
                aovf_r[uc] <= |u_sh_out[39:19];
                ains_r[uc] <= u_inside;
                apar_r[uc] <= u_xi[0] ^ u_yi[0];
            end
            av_r <= (av_r | ((u_go && !inval[uc]) ? (4'b0001 << uc) : 4'b0000)) & ~inval;
        end
    end

    // ===== registers =====
    always_ff @(posedge clk) begin
        if (rst) begin
            for (int i = 0; i < NUM_CH; i++) begin
                xi_r[i]   <= 16'h0; xf_r[i]   <= 16'h0;
                yi_r[i]   <= 16'h0; yf_r[i]   <= 16'h0;
                dxi_r[i]  <= 16'h0; dxf_r[i]  <= 16'h0;
                dyi_r[i]  <= 16'h0; dyf_r[i]  <= 16'h0;
                xmin_r[i] <= 16'h0; ymin_r[i] <= 16'h0;
                xmax_r[i] <= 16'h0; ymax_r[i] <= 16'h0;
                b_r[i]    <= 16'h0; key_r[i]  <= 16'h0;
                ctl_r[i]  <= 16'h0; pitch_r[i]<= 16'h0;
                fmt_r[i]  <= 3'd0;  clip_r[i] <= 1'b0;
                shrx_r[i] <= 1'b0;  shry_r[i] <= 1'b0;
            end
            pf_arm_r   <= '0;
            last_ch_r  <= 2'd0;
            prefetch_r <= 1'b0;
            pf_ch_r    <= 2'd0;
        end else begin
            // ---- position advance after every access (register writes win) ----
            if (adv_i) begin
                if (lin_c) begin
                    xi_r[ach]       <= l_sum[31:16];
                    xf_r[ach]       <= l_sum[15:0];
                    yi_r[ach][2:0]  <= l_sum[34:32];
                end else begin
                    xi_r[ach] <= x_sum[31:16];
                    xf_r[ach] <= x_sum[15:0];
                    yi_r[ach] <= y_sum[31:16];
                    yf_r[ach] <= y_sum[15:0];
                end
                last_ch_r          <= ach;
                pf_arm_r[ach]      <= rd_i && inside_o && !ovf_o;
            end

            // ---- banked register writes ----
            if (wr_pos) begin
                case (grp_i)
                    2'd0: xi_r[ch_i] <= wdata_i;
                    2'd1: yi_r[ch_i] <= wdata_i;
                    2'd2: xf_r[ch_i] <= wdata_i;
                    default: yf_r[ch_i] <= wdata_i;
                endcase
            end
            if (wr_step) begin
                case (grp_i)
                    2'd0: dxi_r[ch_i] <= wdata_i;
                    2'd1: dyi_r[ch_i] <= wdata_i;
                    2'd2: dxf_r[ch_i] <= wdata_i;
                    default: dyf_r[ch_i] <= wdata_i;
                endcase
            end
            if (wr_clip) begin
                case (grp_i)
                    2'd0: xmin_r[ch_i] <= wdata_i;
                    2'd1: ymin_r[ch_i] <= wdata_i;
                    2'd2: xmax_r[ch_i] <= wdata_i;
                    default: ymax_r[ch_i] <= wdata_i;
                endcase
            end
            if (wr_pen) begin
                case (grp_i)
                    2'd0: b_r[ch_i]   <= wdata_i;
                    2'd1: key_r[ch_i] <= wdata_i;
                    2'd2: ctl_r[ch_i] <= wdata_i;
                    default: ;
                endcase
            end
            if (wr_surf) begin
                case (grp_i)
                    2'd0: begin
                        fmt_r[ch_i]  <= wdata_i[10:8];
                        clip_r[ch_i] <= wdata_i[12];
                        shrx_r[ch_i] <= wdata_i[13];
                        shry_r[ch_i] <= wdata_i[14];
                    end
                    2'd1: pitch_r[ch_i] <= wdata_i;
                    default: ;
                endcase
            end

            // ---- host START preload ----
            if (pl_we_i) begin
                xi_r[ch_i]   <= pl_idx_i;  xf_r[ch_i]  <= 16'h0;
                yi_r[ch_i]   <= 16'h0;     yf_r[ch_i]  <= 16'h0;
                dxi_r[ch_i]  <= 16'd1;     dxf_r[ch_i] <= 16'h0;
                dyi_r[ch_i]  <= 16'h0;     dyf_r[ch_i] <= 16'h0;
                ctl_r[ch_i]  <= 16'h0;     pitch_r[ch_i] <= 16'h0;
                fmt_r[ch_i]  <= 3'd0;      clip_r[ch_i]  <= 1'b0;
                shrx_r[ch_i] <= 1'b0;      shry_r[ch_i]  <= 1'b0;
            end

            // ---- prefetch ----
            // any change of position / surface of a channel disarms it
            if (reg_we_i || pl_we_i)
                pf_arm_r[ch_i] <= 1'b0;

            prefetch_r <= pf_cond;
            pf_ch_r    <= ach;
            if (pf_cond)
                pf_arm_r[ach] <= 1'b0;   // one-shot
        end
    end

endmodule
