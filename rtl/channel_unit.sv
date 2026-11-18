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
    output logic        preload_busy_o,   // 1, пока идёт preload канала 0
    output logic        preload_done_o, 
    
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
    logic        pe_page_ovf;      // NEW
    logic        pe_idx_valid;     // NEW

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
    
    // =====================================================================
    // preload: два такта, чтобы IDX и STEP не конфликтовали за порт BRAM.
    // MODE и PAGE пишутся в FF (не в BRAM) — их можно писать вместе с IDX.
    // =====================================================================

    logic pl_start, pl_step;

    always_ff @(posedge clk) begin
        if (rst) pl_step <= 1'b0;
        else     pl_step <= pl_start;
        pl_start <= preload_i & ~rst;
    end

    assign preload_done_o = pl_step;
    assign preload_busy_o = pl_start | pl_step;

    // =====================================================================
    // 
    // =====================================================================    
    
    logic [1:0] ch_sel;
    assign ch_sel = (pl_start || pl_step) ? 2'd0 : ch_i;

    always_comb begin
        // по умолчанию — процессор
        pe_mode_we  = modepage_we_i;
        pe_mode_val = wdata_i[10:8];

        pe_idxh_we  = idxh_we_i;
        pe_idxh_val = wdata_i[2:0];

        pe_idx_we   = idx_we_i;
        pe_idx_val  = wdata_i;

        pe_step_we  = step_we_i;
        pe_step_val = wdata_i;

        cache_page_we  = modepage_we_i;
        cache_page_val = wdata_i[7:0];

        // такт 1 preload: MODE=4 (16bpp), PAGE, IDX = addr[15:1]
        // MODE и IDX одновременно — k уже = 0, сдвиг не нужен
        if (pl_start) begin
            pe_mode_we    = 1'b1;
            pe_mode_val   = 3'd4;
            cache_page_we = 1'b1;
            cache_page_val = preload_addr_i[23:16];
            pe_idx_we     = 1'b1;
            pe_idx_val    = {1'b0, preload_addr_i[15:1]};
        end

        // такт 2 preload: STEP = 16 (1 слово)
        if (pl_step) begin
            pe_step_we  = 1'b1;
            pe_step_val = 16'd16;
        end
    end


   logic ch_access;
    assign ch_access = data_req_i && pe_idx_valid;

    // --- Инстанс pixel_engine ---
    pixel_engine u_pe (
        .clk(clk), .rst(rst),
        .ch_i(ch_sel),
        .mode_we_i(pe_mode_we), .mode_i(pe_mode_val),
        .idxh_we_i(pe_idxh_we), .idxh_i(pe_idxh_val),
        .idx_we_i(pe_idx_we),   .idx_i(pe_idx_val),
        .step_we_i(pe_step_we), .step_i(pe_step_val),
        .access_i(data_req_i),
        .access_done_i(pe_access_done),
        .word_o(pe_word),
        .pos_o(pe_pos),
        .mode_o(pe_mode),
        .page_ovf_o(pe_page_ovf),
        .prefetch_o(pe_prefetch),
        .idx_valid_o(pe_idx_valid)
    );

    // --- Инстанс gpu_cache ---
    gpu_cache u_cache (
        .clk(clk), .rst(rst),
        .ch_sel_i(ch_sel),
        .ch_addr_i(pe_word),
        .ch_stb_i(ch_access),
        .ch_prefetch_i(pe_prefetch),
        .ch_we_i(data_we_i),
        .ch_dat_i(wdata_i),
        .ch_hit_o(cache_hit),
        .ch_dat_o(cache_rdata),
        .ack_o(cache_ack),
        .page_we_i(cache_page_we),
        .page_i(cache_page_val),
        .page_o(cache_page),
        .flush_en_i(flush_req_i),
        .flush_sel_i(ch_i),
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

    // --- Handshake для процессора ---
    assign pe_access_done = cache_ack;
    assign data_ack_o     = cache_ack;
    assign data_rdata_o   = cache_rdata;
    assign flush_ack_o    = cache_flush_done;
    assign status_o       = {2'b0, pe_page_ovf, cache_error};
    assign error_o        = cache_error | pe_page_ovf;


    
endmodule
