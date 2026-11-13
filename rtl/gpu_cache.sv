`timescale 1ns/1ps
`default_nettype none

// gpu_cache: 4-entry cache (CH0..CH3), 3 EBR (TAG, DATA, PAGE).
// Wishbone master: wb_sel_o = 2'b11 (full word), no wb_tag.
// page_we: dumb (writes page, resets channel). Microcode calls flush first.
// flush_en + flush_ch: flush one channel (only where we write).
// access_done: 1-cycle ack to sequencer (stall until wb_ack + 1).
module gpu_cache (
    input  logic        clk,
    input  logic        rst,

    // ---- channel ----
    input  logic [1:0]  ch,

    // ---- from pixel_engine ----
    input  logic [14:0] word_in,
    input  logic        access,
    input  logic        prefetch_req,

    // ---- configuration ----
    input  logic        page_we,
    input  logic [7:0]  page_in,
    input  logic        flush_en,
    input  logic [1:0]  flush_ch,

    // ---- access ----
    input  logic        we,
    input  logic [15:0] wdata,
    output logic        hit,
    output logic [15:0] rdata,
    output logic        access_done,

    // ---- Wishbone master ----
    output logic        wb_cyc_o,
    output logic        wb_stb_o,
    output logic        wb_we_o,
    output logic [23:0] wb_adr_o,
    output logic [15:0] wb_dat_o,
    input  logic        wb_ack_i,
    input  logic [15:0] wb_dat_i,

    // ---- addr24 ----
    output logic [23:0] addr24,

    // ---- error ----
    output logic        error,

    output logic [7:0]  page_out,    // PAGE read
    output logic        flush_done,  // FLUSH завершён
);

    localparam int NCH = 4;

    // ===== EBR =====
    logic [14:0] tag_ebr  [0:NCH-1];
    logic [15:0] data_ebr [0:NCH-1];
    logic [7:0]  page_ebr [0:NCH-1];

    // ===== триггеры =====
    logic        valid [0:NCH-1];
    logic        dirty [0:NCH-1];

    // ===== addr24 =====
    logic [14:0] cur_word;
    logic [7:0]  cur_page;
    assign cur_word = word_in;
    assign cur_page = page_ebr[ch];
    assign addr24   = {cur_page, cur_word, 1'b0};
    assign page_out = page_ebr[ch];

    // ===== hit / rdata =====
    always_comb begin
        hit   = valid[ch] && (tag_ebr[ch] == cur_word);
        rdata = data_ebr[ch];
    end

    // ===== FSM =====
    typedef enum logic [2:0] {
        C_IDLE,
        C_FILL_REQ, C_FILL_WAIT,
        C_WB_REQ,   C_WB_WAIT
    } cache_state_t;
    cache_state_t cstate;

    logic [1:0]  op_ch;
    logic [14:0] op_word;
    logic [7:0]  op_page;
    logic        op_we;
    logic [15:0] op_wdata;
    logic        op_is_access;
    logic        op_is_wb;

    // ===== выбор следующей операции =====
    logic        start_fill, start_wb;
    logic [1:0]  start_ch;
    logic [14:0] start_word;
    logic [7:0]  start_page;
    logic        start_we;
    logic [15:0] start_wdata;
    logic        start_is_access;

    always_comb begin
        start_fill = 1'b0;
        start_wb   = 1'b0;
        start_ch   = 2'd0;
        start_word = 15'h0;
        start_page = 8'h0;
        start_we   = 1'b0;
        start_wdata = 16'h0;
        start_is_access = 1'b0;

        if (cstate == C_IDLE) begin
            // 1. Actual access miss (приоритет)
            if (access && !hit) begin
                start_fill = 1'b1;
                start_ch   = ch;
                start_word = cur_word;
                start_page = cur_page;
                start_we   = we;
                start_wdata = wdata;
                start_is_access = 1'b1;
            end
            // 2. Flush одного канала
            else if (flush_en && valid[flush_ch] && dirty[flush_ch]) begin
                start_wb   = 1'b1;
                start_ch   = flush_ch;
                start_word = tag_ebr[flush_ch];
                start_page = page_ebr[flush_ch];
            end
            // 3. Prefetch
            else if (prefetch_req && !hit) begin
                start_fill = 1'b1;
                start_ch   = ch;
                start_word = cur_word;
                start_page = cur_page;
                start_is_access = 1'b0;
            end
        end
    end

    // ===== Wishbone выходы =====
    always_comb begin
        wb_cyc_o = 1'b0;
        wb_stb_o = 1'b0;
        wb_we_o  = 1'b0;
        wb_adr_o = 24'h0;
        wb_dat_o = 16'h0;

        case (cstate)
            C_FILL_REQ, C_WB_REQ: begin
                wb_cyc_o = 1'b1;
                wb_stb_o = 1'b1;
                wb_we_o  = op_is_wb;
                wb_adr_o = {op_page, op_word, 1'b0};
                wb_dat_o = op_is_wb ? data_ebr[op_ch] : 16'h0;
            end
            default: ;
        endcase
    end

    // ===== access_done =====
    logic fill_done;
    assign fill_done = (cstate == C_FILL_WAIT) && wb_ack_i && op_is_access;
    assign access_done = access && (hit || fill_done);

    // ===== основная логика =====
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
            cstate <= C_IDLE;
            op_ch  <= 2'd0;
            op_word <= 15'h0;
            op_page <= 8'h0;
            op_we  <= 1'b0;
            op_wdata <= 16'h0;
            op_is_access <= 1'b0;
            op_is_wb <= 1'b0;
            error  <= 1'b0;
        end else begin
            // ---- page_we (тупой) ----
            if (page_we) begin
                page_ebr[ch] <= page_in;
                valid[ch]    <= 1'b0;
                dirty[ch]    <= 1'b0;
                tag_ebr[ch]  <= 15'h0;
            end

            // ---- write on hit (без FSM) ----
            if (we && access && hit) begin
                data_ebr[ch] <= wdata;
                dirty[ch]    <= 1'b1;
            end

            // ---- FSM ----
            case (cstate)
                C_IDLE: begin
                    if (start_fill) begin
                        op_ch   <= start_ch;
                        op_word <= start_word;
                        op_page <= start_page;
                        op_we   <= start_we;
                        op_wdata <= start_wdata;
                        op_is_access <= start_is_access;
                        op_is_wb <= 1'b0;
                        cstate  <= C_FILL_REQ;
                    end else if (start_wb) begin
                        op_ch   <= start_ch;
                        op_word <= start_word;
                        op_page <= start_page;
                        op_is_wb <= 1'b1;
                        op_is_access <= 1'b0;
                        cstate  <= C_WB_REQ;
                    end
                end

                C_FILL_REQ: begin
                    cstate <= C_FILL_WAIT;
                end

                C_FILL_WAIT: begin
                    if (wb_ack_i) begin
                        tag_ebr[op_ch]  <= op_word;
                        data_ebr[op_ch] <= wb_dat_i;
                        valid[op_ch]    <= 1'b1;
                        if (op_we) begin
                            data_ebr[op_ch] <= op_wdata;
                            dirty[op_ch]    <= 1'b1;
                        end
                        cstate <= C_IDLE;
                    end
                end

                C_WB_REQ: begin
                    cstate <= C_WB_WAIT;
                end

                C_WB_WAIT: begin
                    if (wb_ack_i) begin
                        dirty[op_ch] <= 1'b0;
                        if (flush_en && flush_ch == op_ch) valid[op_ch] <= 1'b0;
                        cstate <= C_IDLE;
                    end
                end
            endcase
        end
    end

    // flush_done
    logic flush_pending;
    always_ff @(posedge clk) begin
        if (rst) flush_pending <= 1'b0;
        else begin
            if (flush_en && !flush_pending) flush_pending <= 1'b1;
            if (cstate == C_WB_WAIT && wb_ack_i && flush_pending) flush_pending <= 1'b0;
        end
    end
    assign flush_done = flush_pending && (cstate == C_WB_WAIT) && wb_ack_i;

endmodule