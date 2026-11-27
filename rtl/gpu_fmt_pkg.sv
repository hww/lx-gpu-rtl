typedef struct packed {
    logic [2:0]  k;         // log2(пикселей в 16-бит слове)
    logic [15:0] pix_mask;  // маска пикселя в младших битах
    logic [3:0]  s_mask;    // маска инверсной дробной части для сдвига
    logic        wr_en;     // разрешение записи в формат
} fmt_info_t;

// 0: 16 bpp (k=0)
// 1: 8 bpp  (k=1)
// 2: 4 bpp  (k=2)
// 3: 2 bpp  (k=3)
// 4: 1 bpp  (k=4)
// 5: LO8  (k=0) 8 бит в младшем байте слова, старший байт сохраняется при записи
// 6: HI8  (k=0) 8 бит в старшем байте слова, младший байт сохраняется при записи
// 7: резерв (чтение как 16 bpp, запись запрещена)
// pix_mask - это и ширина W операндов пиксельного ALU (v2): 16/8/4/2/1/8/8.
//
// Порядок пикселей (как видеоконтроллер Aleste, pixel_pipeline.sv): байты по
// возрастанию адреса (little-endian), внутри байта — со старших битов.
// 8 bpp: пиксель 0 = биты 7..0, пиксель 1 = 15..8; 4 bpp: 7..4, 3..0, 15..12,
// 11..8; 1 bpp: бит 7 .. бит 0, затем 15 .. 8. Сдвиг пикселя pos_shift().
function automatic fmt_info_t get_fmt_info(input logic [2:0] fmt);
    fmt_info_t res;
    begin
        case (fmt)
            3'd0:    res = {3'd0, 16'hFFFF, 4'b0000, 1'b1};
            3'd1:    res = {3'd1, 16'h00FF, 4'b1000, 1'b1};
            3'd2:    res = {3'd2, 16'h000F, 4'b1100, 1'b1};
            3'd3:    res = {3'd3, 16'h0003, 4'b1110, 1'b0};
            3'd4:    res = {3'd4, 16'h0001, 4'b1111, 1'b0};
            // k=0 => frac всегда 0, поэтому сдвиг задаёт только s_mask:
            // LO8: сдвиг 0, HI8: сдвиг 8 (см. pos_shift)
            default: res = {3'd0, 16'hFFFF, 4'b0000, 1'b0};
        endcase
        get_fmt_info = res;
    end
endfunction

// Сдвиг пикселя в 16-битном слове: старшие биты байта первыми, байты в
// порядке little-endian (для k = 0, 16 bpp, сдвиг всегда 0)
function automatic logic [3:0] pos_shift(input fmt_info_t f, input logic [3:0] frac);
    pos_shift = (~frac & f.s_mask) ^ {(f.k != 3'd0), 3'b000};
endfunction