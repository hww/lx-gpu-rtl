`timescale 1ns/1ps
`default_nettype none

module channel_unit (
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

    // Доступ к DATA (от процессора)
    input  logic        data_req_i,
    input  logic        data_we_i,
    output logic        data_ack_o,
    output logic [15:0] data_rdata_o,

    input  logic        flush_req_i,
    input  logic [3:0]  flush_mask_i,
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
    logic [3:0]  pe_pos;
    logic [2:0]  pe_mode;
    logic        pe_prefetch;
    logic        pe_access_done;

    // --- Сигналы от gpu_cache ---
    logic [23:0] cache_addr24;
    logic        cache_hit;
    logic [15:0] cache_rdata;
    logic        cache_ack;
    logic [7:0]  cache_page;
    logic        cache_flush_done;
    logic        cache_error;

    // --- Мультиплексоры: что писать в pixel_engine и gpu_cache ---
    logic        pe_mode_we, pe_idxh_we, pe_idx_we, pe_step_we;
    logic [2:0]  pe_mode_val, pe_idxh_val;
    logic [15:0] pe_idx_val, pe_step_val;
    logic        cache_page_we;
    logic [7:0]  cache_page_val;

    always_comb begin
        // По умолчанию берем данные от процессора
        // По умолчанию берем данные от процессора
        pe_mode_we  = modepage_we_i;
        pe_mode_val = wdata_i[10:8];  // FMT (MODE) - биты 10..8
        
        pe_idxh_we  = idxh_we_i;
        pe_idxh_val = wdata_i[2:0];
        
        pe_idx_we   = idx_we_i;
        pe_idx_val  = wdata_i;
        
        pe_step_we  = step_we_i;
        pe_step_val = wdata_i;
        
        // PAGE - это биты 7..0
        cache_page_we  = modepage_we_i; 
        cache_page_val = wdata_i[7:0];

        // Если пришел старт от хоста — принудительно настраиваем канал 0
        if (preload_i) begin
            pe_mode_we  = 1'b1;
            pe_mode_val = 3'd4;     // FMT = 4 (16bpp)
            
            pe_step_we  = 1'b1;
            pe_step_val = 16'd16;   // STEP = 1 слово
            
            pe_idx_we   = 1'b1;
            pe_idx_val  = {1'b0, preload_addr_i[15:1]}; // Адрес в словах
            
            cache_page_we  = 1'b1;
            cache_page_val = preload_addr_i[23:16]; // PAGE
        end
    end

    // --- Инстанс pixel_engine ---
    pixel_engine u_pe (
        .clk(clk), .rst(rst),
        .ch_i(ch_i),
        .mode_we_i(pe_mode_we), .mode_i(pe_mode_val),
        .idxh_we_i(pe_idxh_we), .idxh_i(pe_idxh_val),
        .idx_we_i(pe_idx_we),   .idx_i(pe_idx_val),
        .step_we_i(pe_step_we), .step_i(pe_step_val),
        .access_i(data_req_i),
        .access_done_i(pe_access_done),
        .word_o(pe_word),
        .pos_o(pe_pos),
        .mode_o(pe_mode),
        .prefetch_o(pe_prefetch)
    );

    // --- Инстанс gpu_cache ---
    gpu_cache u_cache (
        .clk(clk), .rst(rst),
        .ch_i(ch_i),
        .word_i(pe_word),
        .access_i(data_req_i),
        .prefetch_i(pe_prefetch),
        .we_i(data_we_i),
        .wdata_i(wdata_i),
        .hit_o(cache_hit),
        .rdata_o(cache_rdata),
        .ack_o(cache_ack),
        .page_we_i(cache_page_we),
        .page_i(cache_page_val),
        .page_o(cache_page),
        .flush_en_i(flush_req_i),
        .flush_ch_i(ch_i),
        .flush_done_o(cache_flush_done),
        .wb_cyc_o(wb_cyc_o),
        .wb_stb_o(wb_stb_o),
        .wb_we_o(wb_we_o),
        .wb_adr_o(wb_adr_o),
        .wb_dat_o(wb_dat_o),
        .wb_ack_i(wb_ack_i),
        .wb_dat_i(wb_dat_i),
        .addr24_o(cache_addr24),
        .error_o(cache_error)
    );

    // --- Handshake для процессора ---
    assign pe_access_done = cache_ack;
    assign data_ack_o     = cache_ack;
    assign data_rdata_o   = cache_rdata;
    assign flush_ack_o    = cache_flush_done;
    assign status_o       = {3'b0, cache_error};
    assign error_o        = cache_error;

endmodule
