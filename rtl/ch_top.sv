`timescale 1ns/1ps
`default_nettype none

// ch_top (v2): channel unit = position generator + pixel ALU + packer + cache.
//
// A DATA access is the only event that starts a pixel operation.
//
//   read  DATA : clipped / out of page -> KEY, no cache access;
//                else one cache read, the pixel is unpacked by ch_blender.
//   write DATA : A = wdata. Skipped (no cache access) when clipped, out of
//                page, the format is write-protected, or the screen-door /
//                REL gate says no. Otherwise
//                  NEED_D = 0 : one masked cache write of R = OP(X, Y);
//                  NEED_D = 1 : PH_1 reads D, PH_2 evaluates the ALU and REL
//                               on D and writes R (or not).
//   After every access (committed or not) the position advances by STEP.
//
// Timing (54 MHz on the MiSTer): the access waits until ch_addr_gen has the
// channel's address in registers (rdy), and a write first latches A = wdata
// (PH_A) so that neither the core ALU nor the YI*PITCH multiplier is in the
// path of the cache write. A write costs one cycle more than a read.
//
// The cache merges a write into its entry with a bit mask and never has to
// read memory first, so a partial-word write needs no read-modify-write here.
//
// Handshake: data_ack_o is combinational, high in the cycle the operation
// completes (cache ack, or at once for a skipped access). The core holds the
// request until then. data_rdata_o does not depend on wdata_i.
module ch_top (
    input  logic        clk,
    input  logic        rst,

    // host START: preload channel 0 (16 bpp, linear, XI = word, DXI = 1)
    input  logic        preload_i,
    input  logic [23:0] preload_addr_i,

    input  logic [1:0]  ch_i,

    // banked register write (from the core)
    input  logic        reg_we_i,
    input  logic [2:0]  reg_bank_i,
    input  logic [1:0]  reg_grp_i,
    input  logic [15:0] wdata_i,
    output logic        preload_busy_o,
    output logic        preload_done_o,

    // DATA access (from the core)
    input  logic        data_req_i,
    input  logic        data_we_i,
    output logic        data_ack_o,
    output logic [15:0] data_rdata_o,

    input  logic        flush_req_i,
    input  logic [1:0]  flush_sel_i,
    output logic        flush_ack_o,

    output logic [3:0]  status_o,
    output logic        error_o,

    // Wishbone Master
    output logic        wb_cyc_o,
    output logic        wb_stb_o,
    output logic        wb_we_o,
    output logic [23:0] wb_adr_o,
    output logic [15:0] wb_dat_o,
    output logic [1:0]  wb_sel_o,
    input  logic [15:0] wb_dat_i,
    input  logic        wb_ack_i
);

    `include "rtl/gpu_fmt_pkg.sv"

    // --- from ch_addr_gen ---
    logic [1:0]  ag_ach;
    logic [14:0] ag_word;
    logic [3:0]  ag_frac;
    logic        ag_ovf, ag_inside, ag_par, ag_rdy;
    logic [2:0]  ag_mode;
    logic        ag_shr_x, ag_shr_y;
    logic [15:0] ag_b, ag_key, ag_ctl;
    logic        ag_prefetch;

    // --- from ch_blender ---
    logic [15:0] blend_dat;
    logic        blend_ack;
    logic        blend_rmw_req;
    logic        blend_rmw_we;
    logic [15:0] blend_rmw_dat;
    logic [15:0] blend_rmw_msk;

    // --- from ch_cache ---
    logic [23:0] cache_addr24;
    logic        cache_hit;
    logic [15:0] cache_rdata;
    logic        cache_ack;
    logic [7:0]  cache_page;
    logic        cache_flush_done;

    // =====================================================================
    // preload: cycle 1 -> channel 0 = 16 bpp, linear, page, XI, DXI = 1;
    // cycle 2 only delays preload_done (same timing as before)
    // =====================================================================
    logic pl_start, pl_step;

    always_ff @(posedge clk) begin
        if (rst) pl_step <= 1'b0;
        else     pl_step <= pl_start;
        pl_start <= preload_i & ~rst;
    end

    assign preload_done_o = pl_step;
    assign preload_busy_o = pl_start | pl_step;

    logic [1:0] ch_sel;
    assign ch_sel = (pl_start || pl_step) ? 2'd0 : ch_i;

    // MODE_PAGE write (bank 4, group 0) also carries the page for the cache
    logic        cache_page_we;
    logic [7:0]  cache_page_val;
    always_comb begin
        cache_page_we  = reg_we_i && (reg_bank_i == 3'd4) && (reg_grp_i == 2'd0);
        cache_page_val = wdata_i[7:0];
        if (pl_start) begin
            cache_page_we  = 1'b1;
            cache_page_val = preload_addr_i[23:16];
        end
    end

    // =====================================================================
    // Position generator, clip, address, per-channel registers
    // =====================================================================
    logic ag_adv, ag_rd;

    ch_addr_gen u_pe (
        .clk(clk), .rst(rst),
        .ch_i(ch_sel),
        .reg_we_i(reg_we_i && !pl_start),
        .bank_i(reg_bank_i), .grp_i(reg_grp_i), .wdata_i(wdata_i),
        .pl_we_i(pl_start), .pl_idx_i({1'b0, preload_addr_i[15:1]}),
        .req_i(data_req_i), .adv_i(ag_adv), .rd_i(ag_rd),
        .ach_o(ag_ach),
        .word_o(ag_word), .frac_o(ag_frac),
        .ovf_o(ag_ovf), .inside_o(ag_inside), .par_o(ag_par), .rdy_o(ag_rdy),
        .mode_o(ag_mode), .shr_x_o(ag_shr_x), .shr_y_o(ag_shr_y),
        .b_o(ag_b), .key_o(ag_key), .ctl_o(ag_ctl),
        .prefetch_o(ag_prefetch)
    );

    fmt_info_t fi;
    assign fi = get_fmt_info(ag_mode);

    // =====================================================================
    // Pixel ALU. D is the pixel read in PH_1 (0 while it is not needed).
    // =====================================================================
    // Phase of a DATA access:
    //   PH_A : write only, latch A = wdata (no cache access)
    //   PH_1 : the access (a read, a write, or the read of D)
    //   PH_2 : write of R once D is known
    localparam logic [1:0] PH_A = 2'd0, PH_1 = 2'd1, PH_2 = 2'd2;
    logic [1:0]  ph_r;
    logic [15:0] a_r;
    logic [15:0] d_r;
    logic [15:0] alu_r;
    logic        alu_rel_ok, alu_den_ok, alu_need_d;

    ch_pixel_alu u_alu (
        .a_i(a_r), .b_i(ag_b), .d_i((ph_r == PH_2) ? d_r : 16'h0), .k_i(ag_key),
        .ctl_i(ag_ctl), .mask_i(fi.pix_mask),
        .shr_x_i(ag_shr_x), .shr_y_i(ag_shr_y), .par_i(ag_par),
        .r_o(alu_r),
        .rel_ok_o(alu_rel_ok), .den_ok_o(alu_den_ok), .need_d_o(alu_need_d)
    );

    // =====================================================================
    // Access sequencing
    // =====================================================================
    logic skip_c;                       // no memory access for this position
    assign skip_c = !ag_inside || ag_ovf;

    logic acc_active;                   // a cache operation is wanted now
    logic ph_we;                        // ... and it is a write
    logic latch_a, latch_d, ack_c, did_rd;
    logic [1:0] ph_nxt;

    always_comb begin
        acc_active = 1'b0;
        ph_we      = 1'b0;
        ph_nxt     = ph_r;
        latch_a    = 1'b0;
        latch_d    = 1'b0;
        ack_c      = 1'b0;
        did_rd     = 1'b0;

        if (data_req_i) begin
            if (!data_we_i) begin
                // ---------------- read: wait for the address ----------------
                if (ag_rdy) begin
                    if (skip_c) begin
                        ack_c = 1'b1;                        // reads KEY
                    end else begin
                        acc_active = 1'b1;
                        ack_c      = cache_ack;
                        did_rd     = 1'b1;
                    end
                end
            end else if (ph_r == PH_A) begin
                // ---------------- PH_A: latch A ----------------
                latch_a = 1'b1;
                ph_nxt  = PH_1;
            end else if (ph_r == PH_1) begin
                // ---------------- PH_1 ----------------
                if (ag_rdy) begin
                    if (skip_c || !fi.wr_en || !alu_den_ok) begin
                        ack_c = 1'b1;                        // skipped write
                    end else if (!alu_need_d) begin
                        if (!alu_rel_ok) begin
                            ack_c = 1'b1;                    // REL says no
                        end else begin
                            acc_active = 1'b1;
                            ph_we      = 1'b1;
                            ack_c      = cache_ack;
                        end
                    end else begin
                        acc_active = 1'b1;                   // read D first
                        if (cache_ack) begin
                            latch_d = 1'b1;
                            ph_nxt  = PH_2;
                        end
                    end
                end
            end else begin
                // ---------------- PH_2: D is known ----------------
                did_rd = 1'b1;
                if (!alu_rel_ok) begin
                    ack_c = 1'b1;                            // REL on D says no
                end else begin
                    acc_active = 1'b1;
                    ph_we      = 1'b1;
                    ack_c      = cache_ack;
                end
            end
            if (ack_c) ph_nxt = PH_A;
        end else begin
            ph_nxt = PH_A;                                   // no access
        end
    end

    always_ff @(posedge clk) begin
        if (rst) begin
            ph_r <= PH_A;
            a_r  <= 16'h0;
            d_r  <= 16'h0;
        end else begin
            ph_r <= ph_nxt;
            if (latch_a) a_r <= wdata_i;
            if (latch_d) d_r <= blend_dat;
        end
    end

    // cache request: the strobe drops in the cycle of the ack
    logic        cache_stb, cache_we;
    assign cache_stb = acc_active && !cache_ack;
    assign cache_we  = ph_we;

    assign ag_adv = ack_c;
    assign ag_rd  = did_rd;

    // =====================================================================
    // Blender and cache
    // =====================================================================
    ch_blender u_blender (
        .clk(clk), .rst(rst),
        .cyc_i(acc_active),
        .stb_i(acc_active),
        .we_i(ph_we),
        .mode_i(ag_mode),
        .frac_i(ag_frac),
        .dat_i(cache_rdata),
        .pixel_i(alu_r),
        .dat_o(blend_dat),
        .ack_o(blend_ack),
        .ack_i(cache_ack),
        .rmw_req_o(blend_rmw_req),
        .rmw_we_o(blend_rmw_we),
        .rmw_dat_o(blend_rmw_dat),
        .rmw_msk_o(blend_rmw_msk)
    );

    ch_cache u_cache (
        .clk(clk), .rst(rst),
        .ch_sel_i(ag_ach),
        .ch_addr_i(ag_word),
        .ch_stb_i(cache_stb),
        .ch_prefetch_i(ag_prefetch),
        .ch_we_i(cache_we),
        .ch_dat_i(blend_rmw_dat),
        .ch_msk_i(blend_rmw_msk),
        .ch_hit_o(cache_hit),
        .ch_dat_o(cache_rdata),
        .ack_o(cache_ack),
        .page_we_i(cache_page_we),
        .page_i(cache_page_val),
        .page_o(cache_page),
        .flush_en_i(flush_req_i),
        .flush_sel_i(flush_sel_i),
        .flush_ack_o(cache_flush_done),
        .wb_cyc_o(wb_cyc_o),
        .wb_stb_o(wb_stb_o),
        .wb_we_o(wb_we_o),
        .wb_adr_o(wb_adr_o),
        .wb_dat_o(wb_dat_o),
        .wb_sel_o(wb_sel_o),
        .wb_ack_i(wb_ack_i),
        .wb_dat_i(wb_dat_i),
        .addr24_o(cache_addr24)
    );

    // =====================================================================
    // Outputs
    // =====================================================================
    // Read data: KEY for a read outside the clip window (or the page), else
    // the unpacked pixel. Selected by registers only (no path from wdata_i).
    assign data_ack_o   = ack_c;
    assign data_rdata_o = skip_c ? (ag_key & fi.pix_mask) : blend_dat;
    assign flush_ack_o  = cache_flush_done;

    // Page overflow: only for an access that is not clipped away
    logic ovf_hit, ovf_r;
    assign ovf_hit = ack_c && ag_inside && ag_ovf;

    always_ff @(posedge clk) begin
        if (rst || pl_start) ovf_r <= 1'b0;
        else if (ovf_hit)    ovf_r <= 1'b1;
    end

    assign status_o = {3'b0, ovf_r};
    assign error_o  = ovf_hit;

endmodule
