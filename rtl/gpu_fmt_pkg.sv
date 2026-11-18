`ifndef GPU_FMT_PKG_SV
`define GPU_FMT_PKG_SV


`timescale 1ns/1ps
`default_nettype none

package gpu_fmt_pkg;

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
        case (fmt)
            3'd0:    return '{k: 3'd0, pix_mask: 16'hFFFF, s_mask: 4'b0000, wr_en: 1'b1};
            3'd1:    return '{k: 3'd1, pix_mask: 16'h00FF, s_mask: 4'b1000, wr_en: 1'b1};
            3'd2:    return '{k: 3'd2, pix_mask: 16'h000F, s_mask: 4'b1100, wr_en: 1'b1};
            3'd3:    return '{k: 3'd3, pix_mask: 16'h0003, s_mask: 4'b1110, wr_en: 1'b0};
            3'd4:    return '{k: 3'd4, pix_mask: 16'h0001, s_mask: 4'b1111, wr_en: 1'b0};
            3'd5:    return '{k: 3'd0, pix_mask: 16'h00FF, s_mask: 4'b0000, wr_en: 1'b1};
            3'd6:    return '{k: 3'd0, pix_mask: 16'h00FF, s_mask: 4'b1000, wr_en: 1'b1};
            default: return '{k: 3'd0, pix_mask: 16'hFFFF, s_mask: 4'b0000, wr_en: 1'b0};
        endcase
    endfunction

endpackage
`endif