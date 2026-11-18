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
// 5: LO8    (k=0, младший байт, frac=0 -> sh=0)
// 6: HI8    (k=0, старший байт, frac=0 -> sh=8)
// 7: резерв
function automatic fmt_info_t get_fmt_info(input logic [2:0] fmt);
    fmt_info_t res;
    begin
        case (fmt)
            3'd0:    res = {3'd0, 16'hFFFF, 4'b0000, 1'b1};
            3'd1:    res = {3'd1, 16'h00FF, 4'b1000, 1'b1};
            3'd2:    res = {3'd2, 16'h000F, 4'b1100, 1'b1};
            3'd3:    res = {3'd3, 16'h0003, 4'b1110, 1'b0};
            3'd4:    res = {3'd4, 16'h0001, 4'b1111, 1'b0};
            3'd5:    res = {3'd0, 16'h00FF, 4'b0000, 1'b1};
            3'd6:    res = {3'd0, 16'h00FF, 4'b1000, 1'b1};
            default: res = {3'd0, 16'hFFFF, 4'b0000, 1'b0};
        endcase
        get_fmt_info = res;
    end
endfunction
