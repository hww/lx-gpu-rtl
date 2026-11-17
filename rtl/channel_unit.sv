`timescale 1ns/1ps
`default_nettype none

module channel_unit (
    input  logic        clk,
    input  logic        rst,

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
    logic        pe_access_done; // Импульс: данные готовы, IDX++

    // --- Сигналы от gpu_cache ---
    logic [23:0] cache_addr24;
    logic        cache_hit;
    logic [15:0] cache_rdata;
    logic        cache_ack;
    logic [7:0]  cache_page;
    logic        cache_flush_done;
    logic        cache_error;

    // --- Запись регистров каналов ---
    // Распаковываем 16-битные данные от процессора в нужные форматы для pixel_engine
    // modepage_we_i: пишем в MODE (младшие 3 бита)
    // idx_we_i: пишем IDX
    // idxh_we_i: пишем IDXH (старшие 3 бита адреса)
    // step_we_i: пишем STEP
    logic mode_we, idxh_we, idx_we, step_we;
    logic [2:0] mode_val;
    logic [2:0] idxh_val;

    assign mode_we  = modepage_we_i;
    assign mode_val = wdata_i[2:0];
    
    assign idxh_we  = idxh_we_i;
    assign idxh_val = wdata_i[2:0];

    assign idx_we   = idx_we_i;
    assign step_we  = step_we_i;

    // Выбор канала: при записи берем из dst, при чтении из src (уже передано в ch_i из top.sv)
    
    pixel_engine u_pe (
        .clk(clk), .rst(rst),
        .ch_i(ch_i),
        .mode_we_i(mode_we), .mode_i(mode_val),
        .idxh_we_i(idxh_we), .idxh_i(idxh_val),
        .idx_we_i(idx_we), .idx_i(wdata_i),
        .step_we_i(step_we), .step_i(wdata_i),
        .access_i(data_req_i),
        .access_done_i(pe_access_done),
        .word_o(pe_word),
        .pos_o(pe_pos),
        .mode_o(pe_mode),
        .prefetch_o(pe_prefetch)
    );

    gpu_cache u_cache (
        .clk(clk), .rst(rst),
        .ch_i(ch_i),
        .word_i(pe_word),
        .access_i(data_req_i),
        .prefetch_i(pe_prefetch),
        .we_i(data_we_i),
        .wdata_i(wdata_i),         // Для записи в память
        .hit_o(cache_hit),
        .rdata_o(cache_rdata),
        .ack_o(cache_ack),
        .page_we_i(1'b0),          // TODO: PAGE регистр (если есть)
        .page_i(8'h0),
        .page_o(cache_page),
        .flush_en_i(flush_req_i),  // Упрощенно: flush по маске
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
    // Когда gpu_cache выдает ack (данные прочитаны/записаны), 
    // мы должны сообщить pixel_engine, чтобы он сдвинул IDX.
    assign pe_access_done = cache_ack;

    // data_ack_o для процессора: это тот же сигнал, что и cache_ack
    assign data_ack_o = cache_ack;
    
    // data_rdata_o: если чтение, отдаем данные из кэша, иначе 0
    assign data_rdata_o = cache_rdata;

    // flush_ack_o: ждем, пока кэш не закончит writeback
    assign flush_ack_o = cache_flush_done;

    assign status_o = {3'b0, cache_error};
    assign error_o = cache_error;

    // Предзагрузка (из gpu_start): устанавливаем начальный адрес
    // В bootloader'е это выставляется через PAGE и IDX, но preload_i
    // может сразу задать PAGE и IDX (нужно для gpu_start).
    // В нашем случае bootloader сам ставит PAGE и IDX, так что preload_i
    // можно пока игнорировать или использовать для сброса PAGE0.
    // (Пока оставим закомментированным, т.к. bootloader настраивает всё сам).
    /*
    always_ff @(posedge clk) begin
        if (preload_i) begin
            // Здесь должен быть сброс PAGE и IDX для канала 0
        end
    end
    */

endmodule