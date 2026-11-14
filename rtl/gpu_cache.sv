`timescale 1ns/1ps
`default_nettype none

// gpu_cache: 4-entry cache (CH0..CH3), 3 EBR (TAG, DATA, PAGE).
// Wishbone master. page_we — тупой. access_done / flush_done — регистры (1 такт).
// FSM: C_IDLE -> C_FILL_REQ (wb_cyc_o держится до wb_ack_i) -> C_IDLE
//      C_IDLE -> C_WB_REQ   (wb_cyc_o держится до wb_ack_i) -> C_IDLE
module gpu_cache (
    input  logic        clk,
    input  logic        rst,

    input  logic [1:0]  ch,

    input  logic [14:0] word_in,
    input  logic        access,
    input  logic        prefetch_req,

    input  logic        page_we,
    input  logic [7:0]  page_in,
    output logic [7:0]  page_out,
    input  logic        flush_en,
    input  logic [1:0]  flush_ch,
    output logic        flush_done,

    input  logic        we,
    input  logic [15:0] wdata,
    output logic        hit,
    output logic [15:0] rdata,
    output logic        access_done,

    output logic        wb_cyc_o,
    output logic        wb_stb_o,
    output logic        wb_we_o,
    output logic [23:0] wb_adr_o,
    output logic [15:0] wb_dat_o,
    input  logic        wb_ack_i,
    input  logic [15:0] wb_dat_i,

    output logic [23:0] addr24,
    output logic        error
);

    localparam int NCH = 4;

    logic [14:0] tag_ebr  [0:NCH-1];
    logic [15:0] data_ebr [0:NCH-1];
    logic [7:0]  page_ebr [0:NCH-1];
    logic        valid [0:NCH-1];
    logic        dirty [0:NCH-1];

    logic [14:0] cur_word;
    logic [7:0]  cur_page;
    assign cur_word = word_in;
    assign cur_page = page_ebr[ch];
    assign addr24   = {cur_page, cur_word, 1'b0};
    assign page_out = page_ebr[ch];

    always_comb begin
        hit   = valid[ch] && (tag_ebr[ch] == cur_word);
        rdata = data_ebr[ch];
    end

    // ===== FSM =====
    typedef enum logic [1:0] { C_IDLE, C_FILL_REQ, C_WB_REQ } cache_state_t;
    cache_state_t cstate;

    logic [1:0]  op_ch;
    logic [14:0] op_word;
    logic [7:0]  op_page;
    logic        op_we;
    logic [15:0] op_wdata;
    logic        op_is_access;
    logic        op_is_wb;

    // ===== выбор операции =====
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
            if (access && !hit) begin
                start_fill = 1'b1;
                start_ch   = ch;
                start_word = cur_word;
                start_page = cur_page;
                start_we   = we;
                start_wdata = wdata;
                start_is_access = 1'b1;
            end else if (flush_req && valid[flush_ch] && dirty[flush_ch]) begin
                start_wb   = 1'b1;
                start_ch   = flush_ch;
                start_word = tag_ebr[flush_ch];
                start_page = page_ebr[flush_ch];
            end else if (prefetch_req && !hit) begin
                start_fill = 1'b1;
                start_ch   = ch;
                start_word = cur_word;
                start_page = cur_page;
                start_is_access = 1'b0;
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

        case (cstate)
            C_FILL_REQ: begin
                wb_cyc_o = 1'b1;
                wb_stb_o = 1'b1;
                wb_we_o  = 1'b0;
                wb_adr_o = {op_page, op_word, 1'b0};
            end
            C_WB_REQ: begin
                wb_cyc_o = 1'b1;
                wb_stb_o = 1'b1;
                wb_we_o  = 1'b1;
                wb_adr_o = {op_page, op_word, 1'b0};
                wb_dat_o = data_ebr[op_ch];
            end
            default: ;
        endcase
    end

    // ===== access_done / flush_done (регистры) =====
    // ===== access_done / flush_done (регистры) =====
    logic fill_done_c;
    assign fill_done_c = (cstate == C_FILL_REQ) && wb_ack_i && op_is_access && (ch == op_ch);

    // flush-noop: запрошен flush канала, который невалиден или чист.
    // Никакого WB не нужно — завершаем flush "пустым" тактом.
    logic flush_noop_c;
    assign flush_noop_c = (cstate == C_IDLE) && flush_req
                       && !(valid[flush_ch] && dirty[flush_ch]);

    // access_done — однотактовый строп (монолитный ack от кэша).
    logic access_done_r;
    logic access_finish;
    assign access_finish = access && (hit || fill_done_c) && !access_done_r;

    always_ff @(posedge clk) begin
        if (rst) access_done_r <= 1'b0;
        else     access_done_r <= access_finish;
    end
    assign access_done = access_done_r;

    logic flush_req;
    always_ff @(posedge clk) begin
        if (rst) flush_req <= 1'b0;
        else begin
            if (flush_en) flush_req <= 1'b1;
            if (flush_done) flush_req <= 1'b0;
        end
    end

    logic flush_pending;
    always_ff @(posedge clk) begin
        if (rst) flush_pending <= 1'b0;
        else begin
            if (flush_req && !flush_pending && cstate == C_IDLE) flush_pending <= 1'b1;
            if (flush_done) flush_pending <= 1'b0;
        end
    end

    logic flush_done_c;
    assign flush_done_c = ((cstate == C_WB_REQ) && wb_ack_i && flush_pending && (op_ch == flush_ch))
                       || flush_noop_c;

    logic flush_done_r;
    always_ff @(posedge clk) begin
        if (rst) flush_done_r <= 1'b0;
        else     flush_done_r <= flush_done_c && !flush_done_r;  // однотактовый строп
    end
    assign flush_done = flush_done_r;

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
            cstate  <= C_IDLE;
            op_ch   <= 2'd0;
            op_word <= 15'h0;
            op_page <= 8'h0;
            op_we   <= 1'b0;
            op_wdata <= 16'h0;
            op_is_access <= 1'b0;
            op_is_wb <= 1'b0;
            error   <= 1'b0;
        end else begin
            // page_we (тупой)
            if (page_we) begin
                page_ebr[ch] <= page_in;
                valid[ch]    <= 1'b0;
                dirty[ch]    <= 1'b0;
                tag_ebr[ch]  <= 15'h0;
            end

            // write on hit
            if (we && access && hit) begin
                data_ebr[ch] <= wdata;
                dirty[ch]    <= 1'b1;
            end

            // flush-noop: инвалидируем канал без WB.
            // flush_req/flush_pending снимутся автоматически на следующем такте,
            // т.к. flush_done_c = 1 → flush_done_r = 1 → в блоках flush_req/flush_pending
            // стоит "if (flush_done) ... <= 0".
            if (flush_noop_c) begin
                valid[flush_ch] <= 1'b0;
                dirty[flush_ch] <= 1'b0;
            end

            // FSM
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
                    if (wb_ack_i) begin
                        dirty[op_ch] <= 1'b0;
                        if (flush_en && flush_ch == op_ch) valid[op_ch] <= 1'b0;
                        cstate <= C_IDLE;
                    end
                end
            endcase
        end
    end

endmodule