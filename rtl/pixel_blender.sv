`timescale 1ns/1ps
`default_nettype none

// pixel_blender: упаковка/распаковка пикселя + блендинг в 16-битное слово.
// Отдельный модуль от pixel_engine (по Q8).
//
// Чтение (read=1): word_in -> pixel_out (распакованный пиксель).
// Запись (read=0): word_in + pixel_in -> word_out (сблендённое слово).
//
// MODE:
//   0 = 1 bpp  (16 pixels/word) — только чтение
//   1 = 2 bpp  (8 pixels/word)  — только чтение (reserved)
//   2 = 4 bpp  (4 pixels/word)  — чтение и запись
//   3 = 8 bpp  (2 pixels/word)  — чтение и запись
//   4 = 16 bpp (1 pixel/word)   — чтение и запись (pass-through)
//   5 = PASS_LO8                 — низкий байт
//   6 = PASS_HI8                 — высокий байт
//   7 = PASS16                   — всё слово
module pixel_blender (
    input  logic        read,         // 1 = read (unpack), 0 = write (pack+blend)
    input  logic [2:0]  mode,
    input  logic [3:0]  pos,
    input  logic [15:0] word_in,
    input  logic [15:0] pixel_in,
    output logic [15:0] pixel_out,
    output logic [15:0] word_out
);

    logic [4:0]  shift;
    logic [15:0] mask;
    logic        is_16bit;
    logic        is_pass;

    always_comb begin
        shift  = 5'd0;
        mask   = 16'hFFFF;
        is_16bit = 1'b0;
        is_pass  = 1'b0;

        case (mode)
            3'd0: begin  // 1 bpp
                shift = 5'd15 - {1'b0, pos};
                mask  = 16'h0001 << shift;
            end
            3'd1: begin  // 2 bpp
                shift = ({1'b0, 3'd7} - {1'b0, pos[2:0]}) * 2;
                mask  = 16'h0003 << shift;
            end
            3'd2: begin  // 4 bpp
                shift = ({1'b0, 2'd3} - {1'b0, pos[1:0]}) * 4;
                mask  = 16'h000F << shift;
            end
            3'd3: begin  // 8 bpp
                shift = ({1'b0, 1'b1} - {1'b0, pos[0]}) * 8;
                mask  = 16'h00FF << shift;
            end
            3'd4: begin  // 16 bpp
                shift = 5'd0;
                mask  = 16'hFFFF;
                is_16bit = 1'b1;
            end
            3'd5: begin  // PASS_LO8
                shift = 5'd0;
                mask  = 16'h00FF;
                is_pass = 1'b1;
            end
            3'd6: begin  // PASS_HI8
                shift = 5'd8;
                mask  = 16'hFF00;
                is_pass = 1'b1;
            end
            3'd7: begin  // PASS16
                shift = 5'd0;
                mask  = 16'hFFFF;
                is_pass = 1'b1;
                is_16bit = 1'b1;
            end
        endcase
    end

    // ===== чтение: распаковка =====
    always_comb begin
        if (is_16bit)
            pixel_out = word_in;
        else if (is_pass)
            pixel_out = {8'h0, (word_in & mask) >> shift};
        else
            pixel_out = {12'h0, (word_in & mask) >> shift};
    end

    // ===== запись: блендинг =====
    logic write_allowed;
    always_comb begin
        write_allowed = 1'b1;
        if (mode == 3'd0 || mode == 3'd1) write_allowed = 1'b0;
    end

    always_comb begin
        if (!write_allowed)
            word_out = word_in;
        else if (is_16bit)
            word_out = pixel_in;
        else
            word_out = (word_in & ~mask) | ((pixel_in << shift) & mask);
    end

endmodule