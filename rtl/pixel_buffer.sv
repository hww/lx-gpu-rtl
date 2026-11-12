`timescale 1ns/1ps
// 4-entry pixel buffer: SRC, DST, STM, RSV.
// Each entry: tag (16-bit word address), data (16-bit), page (8-bit),
// valid, dirty.
//
// Writeback: when an entry is dirty and needs to be evicted (flush, set_page,
// or miss in the same channel), the buffer raises wb_req with wb_addr/wb_data
// and waits for wb_ack. On wb_ack, dirty is cleared and the entry is
// invalidated (for flush/set_page) or replaced (for miss).
module pixel_buffer (
    input  logic        clk,
    input  logic        rst,

    // channel select (0..3)
    input  logic [1:0]  ch,

    // lookup
    input  logic [23:0] addr24,
    output logic        hit,
    output logic [15:0] rdata,

    // fill (on miss)
    input  logic        fill_en,
    input  logic [15:0] fill_data,

    // write (on hit or after fill)
    input  logic        we,
    input  logic [15:0] wdata,

    // set_page (writeback + reset one channel)
    input  logic        set_page_en,
    input  logic [7:0]  set_page_val,

    // flush (writeback + invalidate all)
    input  logic        flush_en,

    // writeback handshake (to top / memory arbiter)
    output logic        wb_req,
    output logic [23:0] wb_addr,
    output logic [15:0] wb_data,
    input  logic        wb_ack,

    // error (SIMULATE_CACHE: foreign page)
    output logic        error
);

    logic [15:0] tag   [0:3];
    logic [15:0] data  [0:3];
    logic [7:0]  page  [0:3];
    logic        valid [0:3];
    logic        dirty [0:3];

    logic [15:0] cur_tag;
    logic [7:0]  cur_page;

    assign cur_tag  = addr24[15:0];
    assign cur_page = addr24[23:16];

    // ===== writeback FSM =====
    typedef enum logic [1:0] { WB_IDLE, WB_REQ, WB_WAIT } wb_state_t;
    wb_state_t wb_state;

    logic [1:0]  wb_ch;       // channel being written back
    logic        wb_pending;  // writeback in progress

    // ===== hit / rdata =====
    always_comb begin
        hit   = valid[ch] && (tag[ch] == cur_tag) && (page[ch] == cur_page);
        rdata = data[ch];
    end

    // ===== wb_req generation =====
    // A writeback is requested when:
    //  - wb_state == WB_REQ (we have a dirty entry to flush)
    //  - flush_en, set_page_en, or a miss in the same channel with dirty
    // The priority is: flush/set_page first, then miss-eviction.
    logic need_wb;
    logic [1:0] need_ch;

    always_comb begin
        need_wb = 1'b0;
        need_ch = 2'd0;

        if (wb_state == WB_IDLE) begin
            if (flush_en) begin
                // pick the first dirty entry
                for (int i = 0; i < 4; i++) begin
                    if (!need_wb && valid[i] && dirty[i]) begin
                        need_wb = 1'b1;
                        need_ch = i[1:0];
                    end
                end
            end else if (set_page_en && valid[ch] && dirty[ch]) begin
                need_wb = 1'b1;
                need_ch = ch;
            end else if (we && !hit && valid[ch] && dirty[ch]) begin
                need_wb = 1'b1;
                need_ch = ch;
            end
        end
    end

    assign wb_req  = (wb_state == WB_REQ);
    assign wb_addr = {page[wb_ch], tag[wb_ch]} << 1;   // byte address
    assign wb_data = data[wb_ch];

    // ===== FSM + entry update =====
    integer i;
    always_ff @(posedge clk) begin
        if (rst) begin
            for (i = 0; i < 4; i++) begin
                tag[i]   <= 16'h0;
                data[i]  <= 16'h0;
                page[i]  <= 8'h0;
                valid[i] <= 1'b0;
                dirty[i] <= 1'b0;
            end
            wb_state <= WB_IDLE;
            wb_ch    <= 2'd0;
            error    <= 1'b0;
        end else begin
            case (wb_state)
                WB_IDLE: begin
                    if (need_wb) begin
                        wb_ch    <= need_ch;
                        wb_state <= WB_REQ;
                    end else begin
                        // normal operation (no writeback pending)
                        if (flush_en) begin
                            for (i = 0; i < 4; i++) begin
                                valid[i] <= 1'b0;
                                dirty[i] <= 1'b0;
                            end
                        end else if (set_page_en) begin
                            tag[ch]   <= 16'h0;
                            data[ch]  <= 16'h0;
                            page[ch]  <= set_page_val;
                            valid[ch] <= 1'b0;
                            dirty[ch] <= 1'b0;
                        end else if (fill_en) begin
                            tag[ch]   <= cur_tag;
                            data[ch]  <= fill_data;
                            page[ch]  <= cur_page;
                            valid[ch] <= 1'b1;
                        end else if (we && valid[ch] && (tag[ch] == cur_tag) && (page[ch] == cur_page)) begin
                            data[ch]  <= wdata;
                            dirty[ch] <= 1'b1;
                        end
                    end
                end

                WB_REQ: begin
                    // wait for wb_ack from top
                    if (wb_ack) begin
                        // clear dirty on the flushed entry
                        dirty[wb_ch] <= 1'b0;
                        // if this writeback was for flush/set_page, invalidate
                        if (flush_en || set_page_en) begin
                            valid[wb_ch] <= 1'b0;
                        end
                        wb_state <= WB_IDLE;
                    end
                end

                default: wb_state <= WB_IDLE;
            endcase
        end
    end

endmodule