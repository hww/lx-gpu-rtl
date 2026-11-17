// sim/tb_channel.sv
`timescale 1ns/1ps
`default_nettype none

module tb_channel (
    input  logic        clk,
    input  logic        rst,

    // pixel_engine control
    input  logic [1:0]  ch_i,
    input  logic        mode_we_i,
    input  logic [2:0]  mode_i,
    input  logic        idxh_we_i,
    input  logic [2:0]  idxh_i,
    input  logic        idx_we_i,
    input  logic [15:0] idx_i,
    input  logic        step_we_i,
    input  logic [15:0] step_i,
    input  logic        pe_access_i,
    input  logic        pe_access_done_i,

    // gpu_cache control
    input  logic        gc_access_i,
    input  logic        gc_we_i,
    input  logic [15:0] gc_wdata_i,
    input  logic        gc_flush_en_i,
    input  logic [1:0]  gc_flush_ch_i,
    input  logic        gc_page_we_i,
    input  logic [7:0]  gc_page_i,

    // Wishbone
    output logic        wb_cyc_o,
    output logic        wb_stb_o,
    output logic        wb_we_o,
    output logic [23:0] wb_adr_o,
    output logic [15:0] wb_dat_o,
    input  logic        wb_ack_i,
    input  logic [15:0] wb_dat_i,

    // blender control (Wishbone slave)
    input  logic        bl_cyc_i,
    input  logic        bl_stb_i,
    input  logic        bl_we_i,
    input  logic [2:0]  bl_adr_i,
    input  logic [15:0] bl_pixel_i,
    input  logic        bl_ack_i,
    output logic [15:0] bl_dat_o,
    output logic        bl_ack_o,
    output logic        bl_rmw_req_o,
    output logic        bl_rmw_we_o,
    output logic [15:0] bl_rmw_dat_o,

    // page register (for pe_set_page)
    input  logic        page_we_i,
    input  logic [7:0]  page_i,

    // debug outputs
    output logic [14:0] pe_word_o,
    output logic [3:0]  pe_pos_o,
    output logic [2:0]  pe_mode_o,
    output logic        gc_access_done_o
);

    // pixel_engine
    pixel_engine u_pe (
        .clk(clk), .rst(rst),
        .ch_i(ch_i),
        .mode_we_i(mode_we_i), .mode_i(mode_i),
        .idxh_we_i(idxh_we_i), .idxh_i(idxh_i),
        .idx_we_i(idx_we_i), .idx_i(idx_i),
        .step_we_i(step_we_i), .step_i(step_i),
        .access_i(pe_access_i),
        .access_done_i(pe_access_done_i),
        .word_o(pe_word_o),
        .pos_o(pe_pos_o),
        .mode_o(pe_mode_o),
        .prefetch_o()
    );

    // gpu_cache
    logic [15:0] gc_rdata;
    logic        gc_hit, gc_flush_done, gc_error;
    logic [7:0]  gc_page_o;
    logic [23:0] gc_addr24;

    gpu_cache u_gc (
        .clk(clk), .rst(rst),
        .ch_i(ch_i),
        .word_i(pe_word_o),
        .access_i(gc_access_i),
        .prefetch_i(1'b0),
        .we_i(gc_we_i),
        .wdata_i(gc_wdata_i),
        .hit_o(gc_hit),
        .rdata_o(gc_rdata),
        .ack_o(gc_access_done_o),
        .page_we_i(gc_page_we_i),
        .page_i(gc_page_i),
        .page_o(gc_page_o),
        .flush_en_i(gc_flush_en_i),
        .flush_ch_i(gc_flush_ch_i),
        .flush_done_o(gc_flush_done),
        .wb_cyc_o(wb_cyc_o),
        .wb_stb_o(wb_stb_o),
        .wb_we_o(wb_we_o),
        .wb_adr_o(wb_adr_o),
        .wb_dat_o(wb_dat_o),
        .wb_ack_i(wb_ack_i),
        .wb_dat_i(wb_dat_i),
        .addr24_o(gc_addr24),
        .error_o(gc_error)
    );

    // pixel_blender
    pixel_blender u_bl (
        .clk(clk), .rst(rst),
        .cyc_i(bl_cyc_i), .stb_i(bl_stb_i), .we_i(bl_we_i),
        .adr_i(bl_adr_i),
        .mode_i(pe_mode_o),
        .pos_i(pe_pos_o),
        .dat_i(gc_rdata),
        .pixel_i(bl_pixel_i),
        .dat_o(bl_dat_o),
        .ack_o(bl_ack_o),
        .ack_i(gc_access_done_o),
        .rmw_req_o(bl_rmw_req_o),
        .rmw_we_o(bl_rmw_we_o),
        .rmw_dat_o(bl_rmw_dat_o)
    );

endmodule
