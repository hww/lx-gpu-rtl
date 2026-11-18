`timescale 1ns/1ps
`default_nettype none

module ch_top (
    input  logic        clk,
    input  logic        rst,

    // Сигнал старта от хоста
    input  logic        preload_i,
    input  logic [23:0] preload_addr_i,

    input  logic [1:0]  ch_i,
    
    // Регистры канала (от процессора)
    input  logic        idx_we_i,
    input  logic        idxh_we_i,
    input  logic        step_we_i,
    input  logic        modepage_we_i,
    input  logic [15:0] wdata_i,
    output logic        preload_busy_o,
    output logic        preload_done_o, 
    
    // Доступ к DATA (от процессора)
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
    input  logic [15:0] wb_dat_i,
    input  logic        wb_ack_i
);

    // --- Сигналы от pixel_engine ---
    logic [14:0] pe_word;
    logic [3:0]  pe_frac;
    logic [2:0]  pe_mode;
    logic        pe_prefetch;
    logic        pe_access_done;
    logic        pe_page_ovf;
    logic        pe_idx_valid;

    // --- Сигналы от pixel_blender ---
    logic [15:0] blend_dat;
    logic        blend_ack;
    logic        blend_rmw_req;
    logic        blend_rmw_we;
    logic [15:0] blend_rmw_dat;

    // --- Сигналы от gpu_cache ---
    logic [23:0] cache_addr24;
    logic        cache_hit;
    logic [15:0] cache_rdata;
    logic        cache_ack;
    logic [7:0]  cache_page;
    logic        cache_flush_done;

    // --- Мультиплексоры: что писать в pixel_engine и gpu_cache ---
    logic        pe_mode_we, pe_idxh_we, pe_idx_we, pe_step_we;
    logic [2:0]  pe_mode_val, pe_idxh_val;
    logic [15:0] pe_idx_val, pe_step_val;
    logic        cache_page_we;
    logic [7:0]  cache_page_val;

    // =====================================================================
    // preload: такт 1 -> MODE=0 (16bpp), PAGE, IDX; такт 2 -> STEP=1
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

    always_comb begin
        pe_mode_we     = modepage_we_i;
        pe_mode_val    = wdata_i[10:8];

        pe_idxh_we     = idxh_we_i;
        pe_idxh_val    = wdata_i[2:0];

        pe_idx_we      = idx_we_i;
        pe_idx_val     = wdata_i;

        pe_step_we     = step_we_i;
        pe_step_val    = wdata_i;

        cache_page_we  = modepage_we_i;
        cache_page_val = wdata_i[7:0];

        if (pl_start) begin
            pe_mode_we     = 1'b1;
            pe_mode_val    = 3'd0; // 16 bpp в новой кодировке (k=0)
            cache_page_we  = 1'b1;
            cache_page_val = preload_addr_i[23:16];
            pe_idx_we      = 1'b1;
            pe_idx_val     = {1'b0, preload_addr_i[15:1]};
        end

        if (pl_step) begin
            pe_step_we  = 1'b1;
            pe_step_val = 16'd1; // 1 пиксель (= 1 слово для 16 bpp)
        end
    end

    // =====================================================================
    // Арбитраж доступа к кэшу:
    // - Чтение (data_we_i == 0): прямой запрос в кэш
    // - Запись (data_we_i == 1): через RMW-автомат блендера
    // - !cache_ack гарантирует сброс строба на 1 такт между фазами RMW
    // =====================================================================
    logic        cache_stb;
    logic        cache_we;
    logic [15:0] cache_wdata;

    assign cache_stb = pe_idx_valid && !cache_ack &&
                   (data_we_i ? blend_rmw_req : data_req_i);



    assign cache_we    = data_we_i ? blend_rmw_we  : 1'b0;
    assign cache_wdata = blend_rmw_dat;


    // --- Инстанс pixel_engine ---
    ch_addr_gen u_pe (
        .clk(clk), .rst(rst),
        .ch_i(ch_sel),
        .mode_we_i(pe_mode_we), .mode_i(pe_mode_val),
        .idxh_we_i(pe_idxh_we), .idxh_i(pe_idxh_val),
        .idx_we_i(pe_idx_we),   .idx_i(pe_idx_val),
        .step_we_i(pe_step_we), .step_i(pe_step_val),
        .access_i(data_req_i && pe_idx_valid),
        .access_done_i(pe_access_done),
        .word_o(pe_word),
        .frac_o(pe_frac),
        .mode_o(pe_mode),
        .page_ovf_o(pe_page_ovf),
        .prefetch_o(pe_prefetch),
        .idx_valid_o(pe_idx_valid)
    );

    // --- Инстанс pixel_blender ---
    ch_blender u_blender (
        .clk(clk), .rst(rst),
        .cyc_i(data_req_i && pe_idx_valid),
        .stb_i(data_req_i && pe_idx_valid),
        .we_i(data_we_i),
        .mode_i(pe_mode),
        .frac_i(pe_frac),
        .dat_i(cache_rdata),
        .pixel_i(wdata_i),
        .dat_o(blend_dat),
        .ack_o(blend_ack),
        .ack_i(cache_ack),
        .rmw_req_o(blend_rmw_req),
        .rmw_we_o(blend_rmw_we),
        .rmw_dat_o(blend_rmw_dat)
    );

    // --- Инстанс gpu_cache ---
    ch_cache u_cache (
        .clk(clk), .rst(rst),
        .ch_sel_i(ch_sel),
        .ch_addr_i(pe_word),
        .ch_stb_i(cache_stb),
        .ch_prefetch_i(pe_prefetch  && pe_idx_valid),
        .ch_we_i(cache_we),
        .ch_dat_i(cache_wdata),
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
        .wb_ack_i(wb_ack_i),
        .wb_dat_i(wb_dat_i),
        .addr24_o(cache_addr24)
    );

    // --- Handshake для процессора и сдвига IDX (строго 1 импульс на запрос) ---

    assign pe_access_done = blend_ack;
    assign data_ack_o     = blend_ack;
    assign data_rdata_o   = blend_dat;
    assign flush_ack_o    = cache_flush_done;
    assign status_o       = {3'b0, pe_page_ovf};
    assign error_o        = pe_page_ovf;

endmodule
