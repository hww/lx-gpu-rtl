`timescale 1ns/1ps
module tb_top_wrapper (
    input  logic clk,
    input  logic rst,
    output logic halted,
    output logic error,
    output logic [15:0] debug_mem0,
    // debug: ROM load
    input  logic        rom_we,
    input  logic [11:0] rom_addr,
    input  logic [31:0] rom_wdata,
    // debug: stub mem[0] load
    input  logic        mem0_we,
    input  logic [15:0] mem0_data,
    input  logic start
);
    logic        gpu_req, gpu_we;
    logic [23:0] gpu_addr;
    logic [15:0] gpu_wdata;
    logic [1:0]  gpu_sel;
    logic        gpu_ack0, gpu_ack1;
    logic [15:0] gpu_rdata;
    
    top u_top (
        .clk(clk), .rst(rst), .start(start),
        .halted(halted), .error(error),
        .gpu_req(gpu_req), .gpu_we(gpu_we), .gpu_addr(gpu_addr),
        .gpu_wdata(gpu_wdata), .gpu_sel(gpu_sel),
        .gpu_ack0(gpu_ack0), .gpu_ack1(gpu_ack1), .gpu_rdata(gpu_rdata),
        .rom_we(rom_we), .rom_addr(rom_addr), .rom_wdata(rom_wdata)
    );

    memory_arbiter_stub #(.MEM_WORDS(32768)) u_arb (
        .clk(clk), .rst(rst),
        .gpu_addr_i(gpu_addr), .gpu_req_i(gpu_req),
        .gpu_we_i(gpu_we), .gpu_dat_i(gpu_wdata),
        .gpu_dat_o(gpu_rdata),
        .gpu_ack0_o(gpu_ack0), .gpu_ack1_o(gpu_ack1),
        .gpu_sel_i(gpu_sel)
    );

    assign debug_mem0 = u_arb.debug_mem0;

    always_ff @(posedge clk) begin
        if (mem0_we)
            u_arb.mem[0] <= mem0_data;
    end

        // debug: watch pixel buffer on FSM_WRITE
    always @(posedge clk) begin
        if (u_top.fsm == 2'd3) begin
            $display("WRAP FSM_WRITE: pb_ch=%d pb_addr=%h pb_we=%b pb_wdata=%h pb_hit=%b pb_rdata=%h valid1=%b tag1=%h dirty1=%b",
                     u_top.pb_ch, u_top.pb_addr, u_top.pb_we, u_top.pb_wdata,
                     u_top.pb_hit, u_top.pb_rdata,
                     u_top.pb.valid[1], u_top.pb.tag[1], u_top.pb.dirty[1]);
        end
    end
endmodule