// === FILE: ch_blender.cpp ===
#include "ch_blender.h"

namespace lx {

uint16_t ChBlender::unpack(uint16_t word, uint8_t mode, uint8_t frac) {
    FmtInfo fmt = get_fmt_info(mode);
    uint8_t sh  = pos_shift(fmt, frac);
    return (word >> sh) & fmt.pix_mask;
}

uint16_t ChBlender::blend(uint16_t word, uint16_t pixel, uint8_t mode, uint8_t frac) {
    FmtInfo fmt = get_fmt_info(mode);
    if (!fmt.wr_en) {
        return word; // защита от записи в 1bpp / 2bpp / резерв
    }
    uint8_t  sh   = pos_shift(fmt, frac);
    uint16_t mask = static_cast<uint16_t>(fmt.pix_mask << sh);
    return (word & ~mask) | ((pixel << sh) & mask);
}

uint16_t ChBlender::mask(uint8_t mode, uint8_t frac) {
    FmtInfo fmt = get_fmt_info(mode);
    if (!fmt.wr_en) return 0;
    return static_cast<uint16_t>(fmt.pix_mask << pos_shift(fmt, frac));
}

uint16_t ChBlender::place(uint16_t pixel, uint8_t mode, uint8_t frac) {
    FmtInfo fmt = get_fmt_info(mode);
    uint8_t sh  = pos_shift(fmt, frac);
    return static_cast<uint16_t>((pixel << sh) & (fmt.pix_mask << sh));
}

} // namespace lx
