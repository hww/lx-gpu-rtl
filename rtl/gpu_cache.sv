`timescale 1ns/1ps
`default_nettype none

// gpu_cache: one-word cache entry per channel (CH0..CH3).
// Wishbone master. ack_o / flush_done_o are one-cycle strobes.
//
// FSM: ST_IDLE -> ST_FILL      (wb_cyc_o held until wb_ack_i) -> ST_IDLE
//      ST_IDLE -> ST_WRITEBACK (wb_cyc_o held until wb_ack_i) -> ST_IDLE
module gpu_cache (
    input  logic        clk,
    input  logic        rst,

    // channel select (applies to access, prefetch, page and result outputs)
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

    // channel page register (write also invalidates the entry)
    input  logic        page_we_i,
    input  logic [7:0]  page_i,
    output logic [7:0]  page_o,

    // flush one channel; flush_ch_i must stay stable until flush_done_o
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

    // debug / status
    output logic [23:0] addr24_o,
    output logic        error_o
);

    localparam int NUM_CH = 4;

    logic [14:0] tag_mem  [0:NUM_CH-1];
    logic [15:0] data_mem [0:NUM_CH-1];
    logic [7:0]  page_mem [0:NUM_CH-1];
    logic        valid_r  [0:NUM_CH-1];
    logic        dirty_r  [0:NUM_CH-1];

    // ===== request view of the selected channel =====
    logic [14:0] req_word;
    logic [7:0]  req_page;
    logic        hit;

    assign req_word = word_i;
    assign req_page = page_mem[ch_i];
    assign addr24_o = {req_page, req_word, 1'b0};
    assign page_o   = req_page;

    assign hit      = valid_r[ch_i] && (tag_mem[ch_i] == req_word);
    assign hit_o    = hit;
    assign rdata_o  = data_mem[ch_i];

    // ===== FSM state and the operation in flight =====
    typedef enum logic [1:0] { ST_IDLE, ST_FILL, ST_WRITEBACK } state_t;
    state_t state;

    logic [1:0]  op_ch;
    logic [14:0] op_word;
    logic [7:0]  op_page;
    logic        op_we;
    logic [15:0] op_wdata;
    logic        op_is_access;

    // ===== flush request tracking =====
    logic flush_req_r;       // flush requested, not finished yet
    logic flush_active_r;    // its writeback has been issued
    logic flush_done_r;
    logic access_done_r;

    // ===== choice of the next operation (only in ST_IDLE) =====
    logic        issue_fill, issue_wb;
    logic [1:0]  issue_ch;
    logic [14:0] issue_word;
    logic [7:0]  issue_page;
    logic        issue_we;
    logic [15:0] issue_wdata;
    logic        issue_is_access;

    always_comb begin
        issue_fill      = 1'b0;
        issue_wb        = 1'b0;
        issue_ch        = 2'd0;
        issue_word      = 15'h0;
        issue_page      = 8'h0;
        issue_we        = 1'b0;
        issue_wdata     = 16'h0;
        issue_is_access = 1'b0;

        if (state == ST_IDLE) begin
            if (access_i && !hit) begin
                issue_fill      = 1'b1;
                issue_ch        = ch_i;
                issue_word      = req_word;
                issue_page      = req_page;
                issue_we        = we_i;
                issue_wdata     = wdata_i;
                issue_is_access = 1'b1;
            end else if (flush_req_r && valid_r[flush_ch_i] && dirty_r[flush_ch_i]) begin
                issue_wb        = 1'b1;
                issue_ch        = flush_ch_i;
                issue_word      = tag_mem[flush_ch_i];
                issue_page      = page_mem[flush_ch_i];
            end else if (prefetch_i && !hit) begin
                issue_fill      = 1'b1;
                issue_ch        = ch_i;
                issue_word      = req_word;
                issue_page      = req_page;
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
    logic fill_done_c;
    logic access_finish_c;

    assign fill_done_c     = (state == ST_FILL) && wb_ack_i && op_is_access && (ch_i == op_ch);
    assign access_finish_c = access_i && (hit || fill_done_c) && !access_done_r;

    always_ff @(posedge clk) begin
        if (rst) access_done_r <= 1'b0;
        else     access_done_r <= access_finish_c;
    end
    assign ack_o = access_done_r;

    // ===== flush_done_o: one-cycle strobe =====
    // flush of an invalid or clean channel needs no bus cycle (no-op flush)
    logic flush_noop_c;
    logic flush_done_c;

    assign flush_noop_c = (state == ST_IDLE) && flush_req_r
                       && !(valid_r[flush_ch_i] && dirty_r[flush_ch_i]);
    assign flush_done_c = ((state == ST_WRITEBACK) && wb_ack_i && flush_active_r && (op_ch == flush_ch_i))
                       || flush_noop_c;

    always_ff @(posedge clk) begin
        if (rst) begin
            flush_req_r    <= 1'b0;
            flush_active_r <= 1'b0;
            flush_done_r   <= 1'b0;
        end else begin
            if (flush_en_i)    flush_req_r <= 1'b1;
            if (flush_done_r)  flush_req_r <= 1'b0;

            if (flush_req_r && !flush_active_r && state == ST_IDLE) flush_active_r <= 1'b1;
            if (flush_done_r)  flush_active_r <= 1'b0;

            flush_done_r <= flush_done_c && !flush_done_r;
        end
    end
    assign flush_done_o = flush_done_r;

    // ===== state, entries and flags =====
    always_ff @(posedge clk) begin
        if (rst) begin
            for (int i = 0; i < NUM_CH; i++) begin
                tag_mem[i]  <= 15'h0;
                data_mem[i] <= 16'h0;
                page_mem[i] <= 8'h0;
                valid_r[i]  <= 1'b0;
                dirty_r[i]  <= 1'b0;
            end
            state        <= ST_IDLE;
            op_ch        <= 2'd0;
            op_word      <= 15'h0;
            op_page      <= 8'h0;
            op_we        <= 1'b0;
            op_wdata     <= 16'h0;
            op_is_access <= 1'b0;
            error_o      <= 1'b0;
        end else begin
            // page register write: invalidate the entry
            if (page_we_i) begin
                page_mem[ch_i] <= page_i;
                valid_r[ch_i]  <= 1'b0;
                dirty_r[ch_i]  <= 1'b0;
                tag_mem[ch_i]  <= 15'h0;
            end

            // write on hit
            if (we_i && access_i && hit) begin
                data_mem[ch_i] <= wdata_i;
                dirty_r[ch_i]  <= 1'b1;
            end

            // flush of an invalid or clean channel: just invalidate
            if (flush_noop_c) begin
                valid_r[flush_ch_i] <= 1'b0;
                dirty_r[flush_ch_i] <= 1'b0;
            end

            case (state)
                ST_IDLE: begin
                    if (issue_fill) begin
                        op_ch        <= issue_ch;
                        op_word      <= issue_word;
                        op_page      <= issue_page;
                        op_we        <= issue_we;
                        op_wdata     <= issue_wdata;
                        op_is_access <= issue_is_access;
                        state        <= ST_FILL;
                    end else if (issue_wb) begin
                        op_ch        <= issue_ch;
                        op_word      <= issue_word;
                        op_page      <= issue_page;
                        op_is_access <= 1'b0;
                        state        <= ST_WRITEBACK;
                    end
                end

                ST_FILL: begin
                    if (wb_ack_i) begin
                        tag_mem[op_ch]  <= op_word;
                        data_mem[op_ch] <= wb_dat_i;
                        valid_r[op_ch]  <= 1'b1;
                        if (op_we) begin
                            data_mem[op_ch] <= op_wdata;
                            dirty_r[op_ch]  <= 1'b1;
                        end
                        state <= ST_IDLE;
                    end
                end

                ST_WRITEBACK: begin
                    if (wb_ack_i) begin
                        dirty_r[op_ch] <= 1'b0;
                        if (flush_en_i && flush_ch_i == op_ch) valid_r[op_ch] <= 1'b0;
                        state <= ST_IDLE;
                    end
                end

                default: state <= ST_IDLE;
            endcase
        end
    end

endmodule
