`timescale 1ns/1ps
`default_nettype none

// gpu_cache: one-word cache entry per channel (CH0..CH3), write-back, plus a
// one-word posted write buffer shared by all channels.
// Wishbone master; ack_o and flush_ack_o are one-cycle strobes.
//
// Writes come with a bit mask (ch_msk_i) and never read memory first:
//   - a write miss allocates the entry at once and merges the pixel into it;
//   - each entry keeps a dirty mask per nibble (dmask) and a "full" flag
//     (the whole word is valid: filled from memory or completely written);
//   - a read needs a full entry. A read of a partial entry fills it and
//     merges the dirty nibbles over the word read (merge fill).
// Write-back of a dirty entry goes to the posted write buffer and the cache
// carries on at once; the buffer drains to memory on its own. Bytes that were
// not written are masked off with wb_sel_o (SDRAM DQM), so 8 and 16 bpp
// never read. Only an entry with a half-written byte (one 4 bpp pixel of a
// pair) and no fill is filled before it is written back (lazy fill).
// Reads from memory wait until the write buffer is empty, so a read always
// sees the latest posted write.
//
// A page write is deferred until the old entry is written back. Writing the
// page the channel already has cancels a pending change (A -> B -> A = A).
// FLUSH writes the entry back without the buffer, waits for the buffer to
// drain, and only then acks: after FLUSH everything is in memory.
//
// Contract: ch_stb_i must drop in the cycle ack_o is high; flush_sel_i must
// stay stable until flush_ack_o.
module ch_cache (
    input  logic        clk,
    input  logic        rst,

    input  logic [1:0]  ch_sel_i,

    // access / prefetch request (word address inside the page)
    input  logic [14:0] ch_addr_i,
    input  logic        ch_stb_i,
    input  logic        ch_prefetch_i,
    input  logic        ch_we_i,
    input  logic [15:0] ch_dat_i,
    input  logic [15:0] ch_msk_i,     // write: bits of the word to replace
    output logic        ch_hit_o,
    output logic [15:0] ch_dat_o,
    output logic        ack_o,

    // channel page register
    input  logic        page_we_i,
    input  logic [7:0]  page_i,
    output logic [7:0]  page_o,

    // flush one channel
    input  logic        flush_en_i,
    input  logic [1:0]  flush_sel_i,
    output logic        flush_ack_o,

    // Wishbone master
    output logic        wb_cyc_o,
    output logic        wb_stb_o,
    output logic        wb_we_o,
    output logic [23:0] wb_adr_o,
    output logic [15:0] wb_dat_o,
    output logic [1:0]  wb_sel_o,     // byte enables of a write ([0] = bits 7..0)
    input  logic        wb_ack_i,
    input  logic [15:0] wb_dat_i,

    output logic [23:0] addr24_o
);

    localparam int NUM_CH = 4;

    logic [14:0]       tag_mem     [0:NUM_CH-1];
    logic [15:0]       data_mem    [0:NUM_CH-1];
    logic [7:0]        page_mem    [0:NUM_CH-1];
    logic [7:0]        pend_page_r [0:NUM_CH-1];
    logic [3:0]        dmask_r     [0:NUM_CH-1];  // dirty nibbles

    logic [NUM_CH-1:0] valid_r;
    logic [NUM_CH-1:0] full_r;
    logic [NUM_CH-1:0] pend_r;

    typedef enum logic [1:0] { ST_IDLE, ST_FILL, ST_WRITEBACK } state_t;
    state_t state;

    logic [1:0]  op_ch;
    logic [14:0] op_word;
    logic [7:0]  op_page;
    logic [1:0]  op_sel;
    logic        op_flush_r;     // the writeback in flight is a FLUSH

    // posted write buffer
    logic        wbuf_v;
    logic [7:0]  wbuf_page;
    logic [14:0] wbuf_word;
    logic [15:0] wbuf_dat;
    logic [1:0]  wbuf_sel;

    // ===== helpers =====
    function automatic logic [3:0] nib_of(input logic [15:0] m);
        nib_of = {|m[15:12], |m[11:8], |m[7:4], |m[3:0]};
    endfunction
    function automatic logic [15:0] bits_of(input logic [3:0] n);
        bits_of = {{4{n[3]}}, {4{n[2]}}, {4{n[1]}}, {4{n[0]}}};
    endfunction

    logic [NUM_CH-1:0] dirty_c, mneed_c;
    logic [1:0]        sel_c [0:NUM_CH-1];
    always_comb begin
        for (int i = 0; i < NUM_CH; i++) begin
            dirty_c[i] = valid_r[i] && (dmask_r[i] != 4'h0);
            // a half-written byte with no fill underneath: fill before write-back
            mneed_c[i] = !full_r[i] && ((dmask_r[i][0] ^ dmask_r[i][1]) ||
                                        (dmask_r[i][2] ^ dmask_r[i][3]));
            sel_c[i]   = full_r[i] ? 2'b11
                                   : {dmask_r[i][3] | dmask_r[i][2], dmask_r[i][1] | dmask_r[i][0]};
        end
    end

    // ===== request view =====
    logic tagm, hit, busy_same;
    assign busy_same = (state != ST_IDLE) && (op_ch == ch_sel_i);
    assign tagm      = valid_r[ch_sel_i] && (tag_mem[ch_sel_i] == ch_addr_i);
    assign hit       = tagm && !pend_r[ch_sel_i] && !busy_same
                       && (ch_we_i || full_r[ch_sel_i]);

    assign ch_hit_o = hit;
    assign ch_dat_o = data_mem[ch_sel_i];
    assign page_o   = page_mem[ch_sel_i];
    assign addr24_o = {page_mem[ch_sel_i], ch_addr_i, 1'b0};

    // ===== flush tracking =====
    logic flush_act, flush_req_r, flush_done_r;
    logic flush_noop_c, flush_done_c;

    assign flush_act    = flush_req_r || (flush_en_i && !flush_done_r);
    assign flush_noop_c = (state == ST_IDLE) && !wbuf_v && flush_act && !pend_r[flush_sel_i]
                       && !dirty_c[flush_sel_i];
    assign flush_done_c = ((state == ST_WRITEBACK) && wb_ack_i && op_flush_r && !wbuf_v)
                       || flush_noop_c;

    always_ff @(posedge clk) begin
        if (rst) begin
            flush_req_r  <= 1'b0;
            flush_done_r <= 1'b0;
        end else begin
            if (flush_done_c)
                flush_req_r <= 1'b0;
            else if (flush_en_i && !flush_done_r)
                flush_req_r <= 1'b1;

            flush_done_r <= flush_done_c && !flush_done_r;
        end
    end

    assign flush_ack_o = flush_done_r;

    // ===== what to do next (only in ST_IDLE) =====
    logic       pend_any;
    logic [1:0] pend_ch;
    always_comb begin
        pend_any = 1'b0;
        pend_ch  = 2'd0;
        for (int i = NUM_CH-1; i >= 0; i--)
            if (pend_r[i]) begin pend_any = 1'b1; pend_ch = i[1:0]; end
    end

    logic       issue_apply, issue_post, issue_wb, issue_fill, issue_mfill, issue_alloc;
    logic       issue_flush;
    logic [1:0] issue_ch;
    always_comb begin
        issue_apply = 1'b0;
        issue_post  = 1'b0;   // dirty entry -> write buffer
        issue_wb    = 1'b0;   // dirty entry -> memory, blocking (FLUSH)
        issue_fill  = 1'b0;   // fill the requested word (merging dirty nibbles)
        issue_mfill = 1'b0;   // fill the entry's own word before its write-back
        issue_alloc = 1'b0;   // write miss: take the entry without reading
        issue_flush = 1'b0;
        issue_ch    = 2'd0;

        if (state == ST_IDLE) begin
            if (pend_any) begin
                issue_ch = pend_ch;
                if (!dirty_c[pend_ch])      issue_apply = 1'b1;
                else if (!wbuf_v) begin
                    if (mneed_c[pend_ch])   issue_mfill = 1'b1;
                    else                    issue_post  = 1'b1;
                end
            end else if (flush_act && dirty_c[flush_sel_i]) begin
                issue_ch = flush_sel_i;
                if (!wbuf_v) begin
                    if (mneed_c[flush_sel_i]) issue_mfill = 1'b1;
                    else begin issue_wb = 1'b1; issue_flush = 1'b1; end
                end
            end else if ((ch_stb_i || ch_prefetch_i) && !hit) begin
                issue_ch = ch_sel_i;
                if (tagm) begin
                    // read of a partial entry: fill it, keep the dirty nibbles
                    if (!wbuf_v) issue_fill = 1'b1;
                end else if (dirty_c[ch_sel_i]) begin
                    if (!wbuf_v) begin
                        if (mneed_c[ch_sel_i]) issue_mfill = 1'b1;
                        else begin
                            issue_post = 1'b1;
                            // a write takes the entry in the same cycle
                            if (ch_stb_i && ch_we_i) issue_alloc = 1'b1;
                        end
                    end
                end else if (ch_stb_i && ch_we_i) begin
                    issue_alloc = 1'b1;
                end else if (!wbuf_v) begin
                    issue_fill = 1'b1;
                end
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
        wb_sel_o = 2'b11;
        if (wbuf_v) begin
            wb_cyc_o = 1'b1;
            wb_stb_o = 1'b1;
            wb_we_o  = 1'b1;
            wb_adr_o = {wbuf_page, wbuf_word, 1'b0};
            wb_dat_o = wbuf_dat;
            wb_sel_o = wbuf_sel;
        end else begin
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
                    wb_sel_o = op_sel;
                end
                default: ;
            endcase
        end
    end

    // ===== ack_o: one-cycle strobe =====
    logic ack_r;
    always_ff @(posedge clk) begin
        if (rst) ack_r <= 1'b0;
        else     ack_r <= ch_stb_i && (hit || issue_alloc) && !ack_r;
    end
    assign ack_o = ack_r;

    // ===== state, entries and flags =====
    always_ff @(posedge clk) begin
        if (rst) begin
            for (int i = 0; i < NUM_CH; i++) begin
                tag_mem[i]     <= 15'h0;
                data_mem[i]    <= 16'h0;
                page_mem[i]    <= 8'h0;
                dmask_r[i]     <= 4'h0;
                valid_r[i]     <= 1'b0;
                full_r[i]      <= 1'b0;
                pend_r[i]      <= 1'b0;
                pend_page_r[i] <= 8'h0;
            end
            state      <= ST_IDLE;
            op_ch      <= 2'd0;
            op_word    <= 15'h0;
            op_page    <= 8'h0;
            op_sel     <= 2'b11;
            op_flush_r <= 1'b0;
            wbuf_v     <= 1'b0;
            wbuf_page  <= 8'h0;
            wbuf_word  <= 15'h0;
            wbuf_dat   <= 16'h0;
            wbuf_sel   <= 2'b11;
        end else begin
            // the write buffer drains on its own
            if (wbuf_v && wb_ack_i) wbuf_v <= 1'b0;

            // write hit: merge the pixel
            if (ch_we_i && ch_stb_i && hit) begin
                data_mem[ch_sel_i] <= (data_mem[ch_sel_i] & ~ch_msk_i) | (ch_dat_i & ch_msk_i);
                dmask_r[ch_sel_i]  <= dmask_r[ch_sel_i] | nib_of(ch_msk_i);
                if ((dmask_r[ch_sel_i] | nib_of(ch_msk_i)) == 4'hF) full_r[ch_sel_i] <= 1'b1;
            end

            // flush of an invalid or clean channel: just invalidate
            if (flush_noop_c) begin
                valid_r[flush_sel_i] <= 1'b0;
                full_r[flush_sel_i]  <= 1'b0;
                dmask_r[flush_sel_i] <= 4'h0;
            end

            // apply a pending page change (entry is clean or invalid)
            if (issue_apply) begin
                page_mem[issue_ch] <= pend_page_r[issue_ch];
                tag_mem[issue_ch]  <= 15'h0;
                valid_r[issue_ch]  <= 1'b0;
                full_r[issue_ch]   <= 1'b0;
                dmask_r[issue_ch]  <= 4'h0;
                pend_r[issue_ch]   <= 1'b0;
            end

            // dirty entry -> posted write buffer (the entry stays valid)
            if (issue_post) begin
                wbuf_v             <= 1'b1;
                wbuf_page          <= page_mem[issue_ch];
                wbuf_word          <= tag_mem[issue_ch];
                wbuf_dat           <= data_mem[issue_ch];
                wbuf_sel           <= sel_c[issue_ch];
                dmask_r[issue_ch]  <= 4'h0;
            end

            // write miss: take the entry and merge the pixel, no read
            if (issue_alloc) begin
                tag_mem[ch_sel_i]  <= ch_addr_i;
                valid_r[ch_sel_i]  <= 1'b1;
                data_mem[ch_sel_i] <= ch_dat_i & ch_msk_i;
                dmask_r[ch_sel_i]  <= nib_of(ch_msk_i);
                full_r[ch_sel_i]   <= (nib_of(ch_msk_i) == 4'hF);
            end

            // page register write. The target is the page the channel will
            // have once a pending change is applied.
            if (page_we_i) begin
                if (issue_apply && issue_ch == ch_sel_i) begin
                    if (page_i != pend_page_r[ch_sel_i]) begin
                        pend_r[ch_sel_i]      <= 1'b1;
                        pend_page_r[ch_sel_i] <= page_i;
                    end
                end else if (page_i != page_mem[ch_sel_i]) begin
                    pend_r[ch_sel_i]      <= 1'b1;
                    pend_page_r[ch_sel_i] <= page_i;
                end else begin
                    pend_r[ch_sel_i]      <= 1'b0;   // back to the current page
                end
            end

            case (state)
                ST_IDLE: begin
                    if (issue_fill) begin
                        op_ch      <= issue_ch;
                        op_word    <= ch_addr_i;
                        op_page    <= page_mem[ch_sel_i];
                        op_flush_r <= 1'b0;
                        state      <= ST_FILL;
                    end else if (issue_mfill) begin
                        op_ch      <= issue_ch;
                        op_word    <= tag_mem[issue_ch];     // the entry's own word
                        op_page    <= page_mem[issue_ch];
                        op_flush_r <= 1'b0;
                        state      <= ST_FILL;
                    end else if (issue_wb) begin
                        op_ch      <= issue_ch;
                        op_word    <= tag_mem[issue_ch];
                        op_page    <= page_mem[issue_ch];
                        op_sel     <= sel_c[issue_ch];
                        op_flush_r <= issue_flush;
                        state      <= ST_WRITEBACK;
                    end
                end

                ST_FILL: begin
                    if (wb_ack_i && !wbuf_v) begin
                        tag_mem[op_ch]  <= op_word;
                        data_mem[op_ch] <= (wb_dat_i & ~bits_of(dmask_r[op_ch]))
                                         | (data_mem[op_ch] & bits_of(dmask_r[op_ch]));
                        valid_r[op_ch]  <= 1'b1;
                        full_r[op_ch]   <= 1'b1;
                        state           <= ST_IDLE;
                    end
                end

                ST_WRITEBACK: begin
                    if (wb_ack_i && !wbuf_v) begin
                        dmask_r[op_ch] <= 4'h0;
                        if (op_flush_r) begin
                            valid_r[op_ch] <= 1'b0;
                            full_r[op_ch]  <= 1'b0;
                        end
                        state <= ST_IDLE;
                    end
                end

                default: state <= ST_IDLE;
            endcase
        end
    end

endmodule
