`timescale 1ns/1ps
`default_nettype none

module pixel_engine (
    input  logic        clk,
    input  logic        rst,

    input  logic [1:0]  ch,

    input  logic        mode_we,
    input  logic [2:0]  mode_in,
    input  logic        idxh_we,
    input  logic [2:0]  idxh_in,
    input  logic        idx_we,
    input  logic [15:0] idx_in,
    input  logic        step_we,
    input  logic [15:0] step_in,

    input  logic        access,
    input  logic        access_done,   // импульс: один такт, когда данные готовы
    output logic [14:0] word_out,
    output logic [3:0]  pos,
    output logic [2:0]  mode,
    output logic        prefetch_req
);

    localparam int NCH = 4;

    // EBR: 8 записей × 19 бит (IDX/STEP, 4 канала × 2)
    logic [18:0] idx_step_mem [0:7];
    logic [18:0] idx_stored, step_stored;

    logic [2:0]  mode_r [0:NCH-1];
    logic [2:0]  idxh_r [0:NCH-1];

    function automatic logic [2:0] k_of_fmt(input logic [2:0] fmt);
        case (fmt)
            3'd0: k_of_fmt = 3'd4;  // 1 bpp
            3'd1: k_of_fmt = 3'd3;  // 2 bpp
            3'd2: k_of_fmt = 3'd2;  // 4 bpp
            3'd3: k_of_fmt = 3'd1;  // 8 bpp
            3'd4: k_of_fmt = 3'd0;  // 16 bpp
            default: k_of_fmt = 3'd0; // PASS_*
        endcase
    endfunction

    logic [2:0]  k;
    logic [3:0]  shift_left;
    assign k          = k_of_fmt(mode_r[ch]);
    assign shift_left = 4 - k;

    assign idx_stored  = idx_step_mem[{ch, 1'b0}];
    assign step_stored = idx_step_mem[{ch, 1'b1}];

    // word_out = целая часть (15 бит), pos = дробная часть >> shift_left
    assign word_out = idx_stored[18:4];
    assign pos      = idx_stored[3:0] >> shift_left;
    assign mode     = mode_r[ch];

    // Запись IDX: value = {idxh_r[ch], idx_in} = 19 целых.
    // stored = (value << 4) >> k, обрезаем до 19 бит.
    logic [22:0] idx_shifted;
    assign idx_shifted   = {idxh_r[ch], idx_in, 4'b0} >> k;
    logic [18:0] idx_write_val;
    assign idx_write_val = idx_shifted[18:0];

    // Запись STEP: 12.4 (16 бит знаковый) -> 15.4 (19 бит), расширение знаком.
    logic [18:0] step_write_val;
    assign step_write_val = {{3{step_in[15]}}, step_in} >> k;

    // EBR: запись при IDX/STEP или автоинкремент
    logic [18:0] idx_next;
    assign idx_next = idx_stored + step_stored;

    integer j;
    always_ff @(posedge clk) begin
        if (rst) begin
            for (j = 0; j < 8; j++) idx_step_mem[j] <= 19'h0;
        end else begin
            if (idx_we) begin
                $display("IDX write: ch=%d mode=%d k=%d idx_in=%h write_val=%h",
                         ch, mode_r[ch], k, idx_in, idx_write_val);
                idx_step_mem[{ch, 1'b0}] <= idx_write_val;
            end else if (access_done) begin
                idx_step_mem[{ch, 1'b0}] <= idx_next;
            end
            if (step_we) idx_step_mem[{ch, 1'b1}] <= step_write_val;
        end
    end

    // MODE регистры
    integer i;
    always_ff @(posedge clk) begin
        if (rst) begin
            for (i = 0; i < NCH; i++) mode_r[i] <= 3'd0;
        end else if (mode_we) begin
            mode_r[ch] <= mode_in;
        end
    end

    // IDXH регистры (на канал, обнуляется после IDX)
    always_ff @(posedge clk) begin
        if (rst) begin
            for (i = 0; i < NCH; i++) idxh_r[i] <= 3'h0;
        end else begin
            for (i = 0; i < NCH; i++) begin
                if (idxh_we && ch == i[1:0])    idxh_r[i] <= idxh_in;
                else if (idx_we && ch == i[1:0]) idxh_r[i] <= 3'h0;
            end
        end
    end

    // prefetch
    logic [14:0] word_prev;
    always_ff @(posedge clk) begin
        if (rst) word_prev <= 15'h0;
        else     word_prev <= word_out;
    end

    logic idx_we_d;
    always_ff @(posedge clk) begin
        if (rst) idx_we_d <= 1'b0;
        else     idx_we_d <= idx_we;
    end
    assign prefetch_req = idx_we_d || (access && (word_out != word_prev));

endmodule
