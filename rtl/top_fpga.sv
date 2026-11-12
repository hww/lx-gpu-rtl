`timescale 1ns/1ps
module top_fpga (
    // clocks
    input  logic        clk_25mhz,
    input  logic        rst_btn,     // active high
    // SDRAM
    output logic [12:0] sdram_a,
    output logic [1:0]  sdram_ba,
    output logic        sdram_cke,
    output logic        sdram_cs_n,
    output logic        sdram_ras_n,
    output logic        sdram_cas_n,
    output logic        sdram_we_n,
    output logic [1:0]  sdram_dm,
    output logic        sdram_clk,
    inout  wire  [15:0] sdram_dq,
    // debug
    output logic [7:0]  debug_leds
);

    // ===== reset =====
    logic rst;
    logic clk;
    assign clk = clk_25mhz;
    // simple 2-FF reset synchronizer
    logic [1:0] rst_sync;
    always_ff @(posedge clk) begin
        rst_sync <= {rst_sync[0], rst_btn};
    end
    assign rst = rst_sync[1];

    // ===== GPU top =====
    logic        gpu_req, gpu_we, gpu_ack0, gpu_ack1;
    logic [23:0] gpu_addr;
    logic [15:0] gpu_wdata, gpu_rdata;
    logic [1:0]  gpu_sel;
    logic        halted, error;
    logic        start;
    logic        rom_we;
    logic [11:0] rom_addr;
    logic [31:0] rom_wdata;

    // start: simple - start once reset released, after a short delay
    logic [7:0] start_cnt;
    always_ff @(posedge clk) begin
        if (rst) begin
            start_cnt <= 8'd0;
            start <= 1'b0;
        end else if (start_cnt != 8'hFF) begin
            start_cnt <= start_cnt + 8'd1;
            if (start_cnt == 8'd200) start <= 1'b1;
        end else begin
            start <= 1'b0;
        end
    end

    // ROM load: disable (microcode.hex in BRAM)
    assign rom_we    = 1'b0;
    assign rom_addr  = 12'h0;
    assign rom_wdata = 32'h0;

    top u_gpu (
        .clk(clk), .rst(rst),
        .halted(halted), .error(error),
        .gpu_req(gpu_req), .gpu_we(gpu_we),
        .gpu_addr(gpu_addr), .gpu_wdata(gpu_wdata), .gpu_sel(gpu_sel),
        .gpu_ack0(gpu_ack0), .gpu_ack1(gpu_ack1), .gpu_rdata(gpu_rdata),
        .rom_we(rom_we), .rom_addr(rom_addr), .rom_wdata(rom_wdata),
        .start(start)
    );

    // ===== memory_arbiter =====
    logic        wb_cyc, wb_stb, wb_ack, wb_we;
    logic [23:0] wb_adr;
    logic [7:0]  wb_dat_o, wb_dat_i;
    logic [2:0]  wb_tag;
    logic        wb_grant;

    logic [23:0] video_addr;
    logic        video_req, video_ack0, video_ack1;
    logic [15:0] video_data;

    logic [23:0] sdram_addr;
    logic [15:0] sdram_data_o, sdram_data_i;
    logic        sdram_we, sdram_req, sdram_ack0, sdram_ack1;
    logic [1:0]  sdram_sel;

    assign wb_cyc = 1'b0;
    assign wb_stb = 1'b0;
    assign wb_we  = 1'b0;
    assign wb_adr = 24'h0;
    assign wb_dat_i = 8'h0;
    assign wb_tag = 3'b0;
    assign video_req = 1'b0;
    assign video_addr = 24'h0;

    memory_arbiter u_arb (
        .clk_i(clk), .clke_i(1'b1), .rst(rst),
        .wb_cyc_i(wb_cyc), .wb_stb_i(wb_stb), .wb_ack_o(wb_ack),
        .wb_we_i(wb_we), .wb_adr_i(wb_adr), .wb_dat_i(wb_dat_i),
        .wb_dat_o(wb_dat_o), .wb_tag_i(wb_tag), .wb_grant_o(wb_grant),
        .video_addr_i(video_addr), .video_req_i(video_req),
        .video_data_o(video_data),
        .video_ack0_o(video_ack0), .video_ack1_o(video_ack1),
        .gpu_addr_i(gpu_addr), .gpu_req_i(gpu_req),
        .gpu_we_i(gpu_we), .gpu_dat_i(gpu_wdata),
        .gpu_dat_o(gpu_rdata),
        .gpu_ack0_o(gpu_ack0), .gpu_ack1_o(gpu_ack1),
        .gpu_sel_i(gpu_sel),
        .sdram_addr_o(sdram_addr), .sdram_data_o(sdram_data_o),
        .sdram_data_i(sdram_data_i), .sdram_we_o(sdram_we),
        .sdram_req_o(sdram_req),
        .sdram_ack0_i(sdram_ack0), .sdram_ack1_i(sdram_ack1),
        .sdram_sel_o(sdram_sel),
        .debug_state_o(), .debug_wb_active_o(),
        .debug_gpu_active_o(), .debug_video_active_o()
    );

    // ===== sdram_ctrl (placeholder — use your existing sdram_ctrl_wb) =====
    // ...
    // For now, tie off (GPU will hang on first memory access).
    assign sdram_ack0 = 1'b0;
    assign sdram_ack1 = 1'b0;
    assign sdram_data_i = 16'h0;
    assign sdram_a  = 13'h0;
    assign sdram_ba = 2'b0;
    assign sdram_cke = 1'b0;
    assign sdram_cs_n = 1'b1;
    assign sdram_ras_n = 1'b1;
    assign sdram_cas_n = 1'b1;
    assign sdram_we_n = 1'b1;
    assign sdram_dm = 2'b11;
    assign sdram_clk = 1'b0;

    // ===== debug =====
    assign debug_leds = {6'h0, error, halted};

endmodule