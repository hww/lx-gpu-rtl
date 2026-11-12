`timescale 1ns/1ps
// Wrapper: exposes pixel_buffer internals for C++ testbench.
module tb_pixel_buffer_wrapper (
    input  logic clk,
    input  logic rst,
    input  logic [1:0]  ch,
    input  logic [23:0] addr24,
    output logic        hit,
    output logic [15:0] rdata,
    input  logic        fill_en,
    input  logic [15:0] fill_data,
    input  logic        we,
    input  logic [15:0] wdata,
    input  logic        set_page_en,
    input  logic [7:0]  set_page_val,
    input  logic        flush_en,
    output logic        wb_req,
    output logic [23:0] wb_addr,
    output logic [15:0] wb_data,
    input  logic        wb_ack,
    output logic        error,
    // debug: expose internals
    output logic        dirty1,
    output logic [15:0] data1
);
    pixel_buffer dut (
        .clk(clk), .rst(rst),
        .ch(ch), .addr24(addr24),
        .hit(hit), .rdata(rdata),
        .fill_en(fill_en), .fill_data(fill_data),
        .we(we), .wdata(wdata),
        .set_page_en(set_page_en), .set_page_val(set_page_val),
        .flush_en(flush_en),
        .wb_req(wb_req), .wb_addr(wb_addr), .wb_data(wb_data),
        .wb_ack(wb_ack),
        .error(error)
    );
    assign dirty1 = dut.dirty[1];
    assign data1  = dut.data[1];
endmodule