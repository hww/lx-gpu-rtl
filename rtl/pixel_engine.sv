`timescale 1ns/1ps
`default_nettype none

// gpu_cache: 4-entry cache (CH0..CH3), 3 EBR:
//   TAG  EBR: 15 бит, Dual (R/W + RO)
//   DATA EBR: 16 бит, True Dual Port
//   PAGE EBR:  8 бит, WO + RO
// valid, dirty, wb_state, wb_ch — триггеры.
//
// Ответственность: только кэширование (prefetch, writeback, hit/miss, hold addr).
// Не знает про пиксели, маски, блендинг.
//
// addr24 собирается внутри: addr24 = {page[ch], word_in, 1'b0}.
// page хранится здесь (в EBR), не в top.
//
// Writeback: при eviction (flush, set_page, miss с dirty) выставляем wb_req,
// ждём wb_ack. На wb_ack: dirty=0, valid=0 (для flush/set_page) или
// заменяем (для miss).
module gpu_cache (
    input  logic        clk,
    input  logic        rst,

    // ---- channel select ----
    input  logic [1:0]  ch,

    // ---- from pixel_engine ----
    input  logic [14:0] word_in,       // номер слова (15 бит)
    input  logic        access,        // 1 = доступ к данным
    input  logic        prefetch_req,  // 1 = prefetch (импульс)

    // ---- page write (set_page) ----
    input  logic        page_we,
    input  logic [7:0]  page_in,

    // ---- flush ----
    input  logic        flush_en,

    // ---- fill (on miss) ----
    input  logic        fill_en,
    input  logic [15:0] fill_data,

    // ---- write (on hit or after fill) ----
    input  logic        we,
    input  logic [15:0] wdata,

    // ---- read ----
    output logic        hit,
    output logic [15:0] rdata,

    // ---- writeback to memory ----
    output logic        wb_req,
    output logic [23:0] wb_addr,
    output logic [15:0] wb_data,
    input  logic        wb_ack,

    // ---- error (foreign page, microcode bug) ----
    output logic        error
);

    localparam int NCH = 4;

    // ===== EBR =====
    logic [14:0] tag_ebr  [0:NCH-1];   // 15 бит
    logic [15:0] data_ebr [0:NCH-1];   // 16 бит
    logic [7:0]  page_ebr [0:NCH-1];   // 8 бит

    // ===== триггеры =====
    logic        valid [0:NCH-1];
    logic        dirty [0:NCH-1];

    // ===== сборка addr24 =====
    logic [23:0] addr24;
    assign addr24 = {page_ebr[ch], word_in, 1'b0};

    // ===== hit / rdata =====
    logic [14:0] cur_tag;
    logic [7:0]  cur_page;
    assign cur_tag  = word_in;
    assign cur_page = page_ebr[ch];

    always_comb begin
        hit   = valid[ch] && (tag_ebr[ch] == cur_tag);
        rdata = data_ebr[ch];
    end

    // ===== writeback FSM =====
    typedef enum logic [1:0] { WB_IDLE, WB_REQ, WB_WAIT } wb_state_t;
    wb_state_t wb_state;

    logic [1:0]  wb_ch;
    logic        need_wb;
    logic [1:0]  need_ch;

    always_comb begin
        need_wb = 1'b0;
        need_ch = 2'd0;

        if (wb_state == WB_IDLE) begin
            if (flush_en) begin
                for (int i = 0; i < NCH; i++) begin
                    if (!need_wb && valid[i] && dirty[i]) begin
                        need_wb = 1'b1;
                        need_ch = i[1:0];
                    end
                end
            end else if (page_we && valid[ch] && dirty[ch]) begin
                need_wb = 1'b1;
                need_ch = ch;
            end else if (access && !hit && valid[ch] && dirty[ch]) begin
                need_wb = 1'b1;
                need_ch = ch;
            end
        end
    end

    assign wb_req  = (wb_state == WB_REQ);
    assign wb_addr = {page_ebr[wb_ch], tag_ebr[wb_ch], 1'b0};
    assign wb_data = data_ebr[wb_ch];

    // ===== EBR + FSM =====
    integer i;
    always_ff @(posedge clk) begin
        if (rst) begin
            for (i = 0; i < NCH; i++) begin
                tag_ebr[i]  <= 15'h0;
                data_ebr[i] <= 16'h0;
                page_ebr[i] <= 8'h0;
                valid[i]    <= 1'b0;
                dirty[i]    <= 1'b0;
            end
            wb_state <= WB_IDLE;
            wb_ch    <= 2'd0;
            error    <= 1'b0;
        end else begin
            // ---- PAGE write (set_page) ----
            // По вашему Q7: смена page ждёт writeback. Здесь page_we
            // выставляется только после wb_ack для старой страницы.
            if (page_we) begin
                page_ebr[ch] <= page_in;
                valid[ch]    <= 1'b0;
                dirty[ch]    <= 1'b0;
                tag_ebr[ch]  <= 15'h0;
            end

            // ---- flush ----
            if (flush_en && wb_state == WB_IDLE && !need_wb) begin
                for (i = 0; i < NCH; i++) begin
                    valid[i] <= 1'b0;
                    dirty[i] <= 1'b0;
                end
            end

            // ---- prefetch / fill ----
            if (prefetch_req && !hit) begin
                // prefetch = начать чтение из памяти (top делает)
            end
            if (fill_en) begin
                tag_ebr[ch]  <= cur_tag;
                data_ebr[ch] <= fill_data;
                valid[ch]    <= 1'b1;
            end

            // ---- write on hit ----
            if (we && hit) begin
                data_ebr[ch] <= wdata;
                dirty[ch]    <= 1'b1;
            end

            // ---- writeback FSM ----
            case (wb_state)
                WB_IDLE: begin
                    if (need_wb) begin
                        wb_ch    <= need_ch;
                        wb_state <= WB_REQ;
                    end
                end
                WB_REQ: begin
                    if (wb_ack) begin
                        dirty[wb_ch] <= 1'b0;
                        if (flush_en || page_we) begin
                            valid[wb_ch] <= 1'b0;
                        end
                        wb_state <= WB_IDLE;
                    end
                end
                default: wb_state <= WB_IDLE;
            endcase

            // ---- error: foreign page ----
            // (под SIMULATE_CACHE; здесь упрощённо)
        end
    end

endmodule