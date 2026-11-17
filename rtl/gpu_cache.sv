`timescale 1ns/1ps
`default_nettype none

// gpu_cache: one-word cache entry per channel (CH0..CH3), write-back.
// Wishbone master; ack_o and flush_done_o are one-cycle strobes.
//
// A miss on a dirty entry first writes the old word back (old tag, old page),
// then fills. A page write is deferred until the old entry is written back.
// FLUSH is only needed to push out the last dirty words at the end of a task.
//
// Contract: access_i must drop in the cycle ack_o is high; flush_ch_i must
// stay stable until flush_done_o.
module gpu_cache (
    input  logic        clk,
    input  logic        rst,

    input  logic [1:0]  ch_i,

    // access / prefetch request (word address inside the page)
    input  logic [14:0] word_i,
    input  logic        access_i,
    input  logic        prefetch_i,
    input  logic        we_i,
    input  logic [15:0] wdata_i,
    output logic        hit_o,
    output logic [15:0] rdata_o,
    output logic        ack_o,

    // channel page register
    input  logic        page_we_i,
    input  logic [7:0]  page_i,
    output logic [7:0]  page_o,

    // flush one channel
    input  logic        flush_en_i,
    input  logic [1:0]  flush_ch_i,
    output logic        flush_done_o,

    // Wishbone master
    output logic        wb_cyc_o,
    output logic        wb_stb_o,
    output logic        wb_we_o,
    output logic [23:0] wb_adr_o,
    output logic [15:0] wb_dat_o,
    input  logic        wb_ack_i,
    input  logic [15:0] wb_dat_i,

    output logic [23:0] addr24_o,
    output logic        error_o
);

    localparam int NUM_CH = 4;

    logic [14:0] tag_mem     [0:NUM_CH-1];
    logic [15:0] data_mem    [0:NUM_CH-1];
    logic [7:0]  page_mem    [0:NUM_CH-1];
    logic        valid_r     [0:NUM_CH-1];
    logic        dirty_r     [0:NUM_CH-1];
    logic        pend_r      [0:NUM_CH-1];   // page change waiting
    logic [7:0]  pend_page_r [0:NUM_CH-1];

    typedef enum logic [1:0] { ST_IDLE, ST_FILL, ST_WRITEBACK } state_t;
    state_t state;

    logic [1:0]  op_ch;
    logic [14:0] op_word;
    logic [7:0]  op_page;
    logic        op_flush_r;     // the writeback in flight is a FLUSH

    // ===== request view =====
    logic hit, busy_same;
    assign busy_same = (state != ST_IDLE) && (op_ch == ch_i);
    assign hit       = valid_r[ch_i] && (tag_mem[ch_i] == word_i)
                       && !pend_r[ch_i] && !busy_same;

    assign hit_o    = hit;
    assign rdata_o  = data_mem[ch_i];
    assign page_o   = page_mem[ch_i];
    assign addr24_o = {page_mem[ch_i], word_i, 1'b0};
    assign error_o  = 1'b0;      // page overflow is detected in pixel_engine

    // ===== flush tracking =====
    logic flush_req_r, flush_done_r;
    logic flush_noop_c, flush_done_c;

    assign flush_noop_c = (state == ST_IDLE) && flush_req_r
                       && !(valid_r[flush_ch_i] && dirty_r[flush_ch_i]);
    assign flush_done_c = ((state == ST_WRITEBACK) && wb_ack_i && op_flush_r)
                       || flush_noop_c;

    always_ff @(posedge clk) begin
        if (rst) begin
            flush_req_r  <= 1'b0;
            flush_done_r <= 1'b0;
        end else begin
            if (flush_done_r) flush_req_r <= 1'b0;
            if (flush_en_i)   flush_req_r <= 1'b1;     // a new request wins
            flush_done_r <= flush_done_c && !flush_done_r;
        end
    end
    assign flush_done_o = flush_done_r;

    // ===== what to do next (only in ST_IDLE) =====
    logic       pend_any;
    logic [1:0] pend_ch;
    always_comb begin
        pend_any = 1'b0;
        pend_ch  = 2'd0;
        for (int i = NUM_CH-1; i >= 0; i--)
            if (pend_r[i]) begin pend_any = 1'b1; pend_ch = i[1:0]; end
    end

    logic       issue_apply, issue_wb, issue_fill, issue_flush;
    logic [1:0] issue_ch;
    always_comb begin
        issue_apply = 1'b0;
        issue_wb    = 1'b0;
        issue_fill  = 1'b0;
        issue_flush = 1'b0;
        issue_ch    = 2'd0;

        if (state == ST_IDLE) begin
            if (pend_any) begin
                issue_ch = pend_ch;
                if (valid_r[pend_ch] && dirty_r[pend_ch]) issue_wb    = 1'b1;
                else                                      issue_apply = 1'b1;
            end else if (flush_req_r && valid_r[flush_ch_i] && dirty_r[flush_ch_i]) begin
                issue_wb    = 1'b1;
                issue_flush = 1'b1;
                issue_ch    = flush_ch_i;
            end else if ((access_i || prefetch_i) && !hit) begin
                issue_ch = ch_i;
                if (valid_r[ch_i] && dirty_r[ch_i]) issue_wb   = 1'b1;   // evict first
                else                                issue_fill = 1'b1;
            end
        end
    end

    // ===== Wishbone =====
    always_comb begin
        wb_cyc_o = 1'b0;
        wb_stb_o = 1'b0;
        wb_we_o  = 1'b0;
        wb_adr_o = 24'h0;
        wb_dat_o = 16'h0;
        case (state)
            ST_FILL: begin
                wb_cyc_o = 1'b1;
                wb_stb_o = 1'b1;
                wb_adr_o = {op_page, op_word, 1'b0};
            end
            ST_WRITEBACK: begin
                wb_cyc_o = 1'b1;
                wb_stb_o = 1'b1;
                wb_we_o  = 1'b1;
                wb_adr_o = {op_page, op_word, 1'b0};
                wb_dat_o = data_mem[op_ch];
            end
            default: ;
        endcase
    end

    // ===== ack_o: one-cycle strobe =====
    logic ack_r;
    always_ff @(posedge clk) begin
        if (rst) ack_r <= 1'b0;
        else     ack_r <= access_i && hit && !ack_r;
    end
    assign ack_o = ack_r;

    // ===== state, entries and flags =====
    always_ff @(posedge clk) begin
        if (rst) begin
            for (int i = 0; i < NUM_CH; i++) begin
                tag_mem[i]     <= 15'h0;
                data_mem[i]    <= 16'h0;
                page_mem[i]    <= 8'h0;
                valid_r[i]     <= 1'b0;
                dirty_r[i]     <= 1'b0;
                pend_r[i]      <= 1'b0;
                pend_page_r[i] <= 8'h0;
            end
            state      <= ST_IDLE;
            op_ch      <= 2'd0;
            op_word    <= 15'h0;
            op_page    <= 8'h0;
            op_flush_r <= 1'b0;
        end else begin
            // write hit
            if (we_i && access_i && hit && !ack_r) begin
                data_mem[ch_i] <= wdata_i;
                dirty_r[ch_i]  <= 1'b1;
            end

            // flush of an invalid or clean channel: just invalidate
            if (flush_noop_c) begin
                valid_r[flush_ch_i] <= 1'b0;
                dirty_r[flush_ch_i] <= 1'b0;
            end

            // apply a pending page change (entry is clean or invalid)
            if (issue_apply) begin
                page_mem[issue_ch] <= pend_page_r[issue_ch];
                tag_mem[issue_ch]  <= 15'h0;
                valid_r[issue_ch]  <= 1'b0;
                dirty_r[issue_ch]  <= 1'b0;
                pend_r[issue_ch]   <= 1'b0;
            end

            // page register write: remember it (after the apply above, so a
            // new request in the same cycle is not lost)
            if (page_we_i) begin
                pend_r[ch_i]      <= 1'b1;
                pend_page_r[ch_i] <= page_i;
            end

            case (state)
                ST_IDLE: begin
                    if (issue_fill) begin
                        op_ch      <= issue_ch;
                        op_word    <= word_i;
                        op_page    <= page_mem[ch_i];
                        op_flush_r <= 1'b0;
                        state      <= ST_FILL;
                    end else if (issue_wb) begin
                        op_ch      <= issue_ch;
                        op_word    <= tag_mem[issue_ch];     // OLD word
                        op_page    <= page_mem[issue_ch];    // OLD page
                        op_flush_r <= issue_flush;
                        state      <= ST_WRITEBACK;
                    end
                end

                ST_FILL: begin
                    if (wb_ack_i) begin
                        tag_mem[op_ch]  <= op_word;
                        data_mem[op_ch] <= wb_dat_i;
                        valid_r[op_ch]  <= 1'b1;
                        dirty_r[op_ch]  <= 1'b0;
                        state           <= ST_IDLE;
                    end
                end

                ST_WRITEBACK: begin
                    if (wb_ack_i) begin
                        dirty_r[op_ch] <= 1'b0;
                        if (op_flush_r) valid_r[op_ch] <= 1'b0;
                        state <= ST_IDLE;
                    end
                end

                default: state <= ST_IDLE;
            endcase
        end
    end

endmodule