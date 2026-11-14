`timescale 1ns/1ps
`default_nettype none

module top_fpga (
    input  logic        clk_25mhz,
    input  logic        rst_btn,

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

    output logic [7:0]  debug_leds
);

    // ===== reset / clock =====
    logic clk = clk_25mhz;
    logic rst;
    logic [1:0] rst_sync;
    always_ff @(posedge clk) rst_sync <= {rst_sync[0], rst_btn};
    assign rst = rst_sync[1];

    // ===== start pulse =====
    logic start;
    logic [7:0] start_cnt;
    always_ff @(posedge clk) begin
        if (rst) begin
            start_cnt <= 8'd0;
            start     <= 1'b0;
        end else if (start_cnt != 8'hFF) begin
            start_cnt <= start_cnt + 8'd1;
            if (start_cnt == 8'd200) start <= 1'b1;
        end else begin
            start <= 1'b0;
        end
    end

    // ===== GPU top =====
    logic        halted, error;
    logic        rom_we;
    logic [11:0] rom_addr;
    logic [31:0] rom_wdata;

    logic        wb_cyc, wb_stb, wb_we, wb_ack;
    logic [23:0] wb_adr;
    logic [15:0] wb_dat_o, wb_dat_i;

    assign rom_we    = 1'b0;
    assign rom_addr  = 12'h0;
    assign rom_wdata = 32'h0;

    // Заглушка шины: любой запрос подтверждается сразу с нулевыми данными.
    // (GPU работает, но DATA[] возвращает 0; убрать, когда появится
    //  настоящий sdram_ctrl / memory_arbiter.)
    assign wb_ack   = wb_cyc & wb_stb;
    assign wb_dat_i = 16'h0000;

    top u_gpu (
        .clk(clk), .rst(rst),
        .halted(halted), .error(error),
        .start(start),
        .rom_we(rom_we), .rom_addr(rom_addr), .rom_wdata(rom_wdata),
        .wb_cyc(wb_cyc), .wb_stb(wb_stb), .wb_we(wb_we),
        .wb_adr(wb_adr), .wb_dat_o(wb_dat_o),
        .wb_dat_i(wb_dat_i), .wb_ack(wb_ack)
    );

    // ===== SDRAM пины заглушены (контроллера пока нет) =====
    assign sdram_a     = 13'h0;
    assign sdram_ba    = 2'b0;
    assign sdram_cke   = 1'b0;
    assign sdram_cs_n  = 1'b1;
    assign sdram_ras_n = 1'b1;
    assign sdram_cas_n = 1'b1;
    assign sdram_we_n  = 1'b1;
    assign sdram_dm    = 2'b11;
    assign sdram_clk   = 1'b0;
    assign sdram_dq    = 16'bz;

    // ===== debug =====
    assign debug_leds = {6'h0, error, halted};

endmodule