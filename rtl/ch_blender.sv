`timescale 1ns/1ps
`default_nettype none

// ch_blender: pixel <-> 16-bit word for one channel access.
//
// Read:  the cache word comes in on dat_i and the pixel is unpacked from it
//        (combinational, ack_o = ack_i).
// Write: no read-modify-write here. The pixel is shifted into place and sent
//        to the cache with a bit mask (rmw_dat_o / rmw_msk_o) in a single
//        access; the cache merges it into the line and keeps a per-nibble
//        dirty mask, so a write never has to read memory first.
//        1 bpp, 2 bpp and the reserved formats are write-protected: the
//        access is acknowledged without touching the cache.
module ch_blender (
    input  logic        clk,
    input  logic        rst,

    // Wishbone slave (from the channel)
    input  logic        cyc_i,
    input  logic        stb_i,
    input  logic        we_i,        // 0 = read, 1 = write
    input  logic [2:0]  mode_i,      // from ch_addr_gen
    input  logic [3:0]  frac_i,      // from ch_addr_gen (reg_idx[3:0])
    input  logic [15:0] dat_i,       // cache word (cache_rdata)
    input  logic [15:0] pixel_i,     // pixel to write
    output logic [15:0] dat_o,       // unpacked pixel (reads only)
    output logic        ack_o,       // one-cycle completion strobe

    // to the cache
    input  logic        ack_i,       // cache_ack
    output logic        rmw_req_o,   // cache request for a write
    output logic        rmw_we_o,    // 1 = write
    output logic [15:0] rmw_dat_o,   // pixel shifted into its position
    output logic [15:0] rmw_msk_o    // bits of the word it replaces
);

    `include "rtl/gpu_fmt_pkg.sv"

    fmt_info_t   fmt_c;
    logic [3:0]  sh_c;
    logic [15:0] pixel_c, msk_c;

    assign fmt_c   = get_fmt_info(mode_i);
    assign sh_c    = pos_shift(fmt_c, frac_i);
    assign pixel_c = (dat_i >> sh_c) & fmt_c.pix_mask;
    assign msk_c   = fmt_c.pix_mask << sh_c;

    logic wr;
    assign wr = cyc_i && stb_i && we_i;

    // write-protected format: one ack strobe, no cache access
    logic prot_ack_r;
    always_ff @(posedge clk) begin
        if (rst) prot_ack_r <= 1'b0;
        else     prot_ack_r <= wr && !fmt_c.wr_en && !prot_ack_r;
    end

    assign rmw_req_o = wr && fmt_c.wr_en;
    assign rmw_we_o  = wr;
    assign rmw_dat_o = (pixel_i << sh_c) & msk_c;
    assign rmw_msk_o = fmt_c.wr_en ? msk_c : 16'h0000;

    assign ack_o = wr ? (fmt_c.wr_en ? ack_i : prot_ack_r) : ack_i;
    // reads only: on a write the core does not use dat_o, and it must not
    // depend on pixel_i (ALU result -> blender -> operand mux would be a
    // combinational loop)
    assign dat_o = pixel_c;

endmodule
