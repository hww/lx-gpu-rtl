#include "ch_addr_gen.h"

namespace lx {

void ChAddrGen::reset() {
    for (int i = 0; i < NUM_CH; ++i) {
        xi_[i] = xf_[i] = yi_[i] = yf_[i] = 0;
        dxi_[i] = dxf_[i] = dyi_[i] = dyf_[i] = 0;
        xmin_[i] = ymin_[i] = xmax_[i] = ymax_[i] = 0;
        b_[i] = key_[i] = ctl_[i] = 0;
        pitch_[i] = 0;
        page_[i] = 0;
        fmt_[i] = 0;
        clip_[i] = shrx_[i] = shry_[i] = false;
    }
}

void ChAddrGen::write_banked(uint8_t bank, uint8_t group, uint8_t ch, uint16_t data) {
    ch &= 3;
    switch (bank) {
        case 0:
            switch (group & 3) {
                case 0: xi_[ch] = data; break;
                case 1: yi_[ch] = data; break;
                case 2: xf_[ch] = data; break;
                default: yf_[ch] = data; break;
            }
            break;
        case 1:
            switch (group & 3) {
                case 0: dxi_[ch] = data; break;
                case 1: dyi_[ch] = data; break;
                case 2: dxf_[ch] = data; break;
                default: dyf_[ch] = data; break;
            }
            break;
        case 2:
            switch (group & 3) {
                case 0: xmin_[ch] = data; break;
                case 1: ymin_[ch] = data; break;
                case 2: xmax_[ch] = data; break;
                default: ymax_[ch] = data; break;
            }
            break;
        case 3:
            switch (group & 3) {
                case 0: b_[ch]   = data; break;
                case 1: key_[ch] = data; break;
                case 2: ctl_[ch] = data; break;
                default: break;
            }
            break;
        case 4:
            switch (group & 3) {
                case 0:
                    fmt_[ch]  = (data >> 8) & 7;
                    page_[ch] = data & 0xFF;
                    clip_[ch] = (data >> 12) & 1;
                    shrx_[ch] = (data >> 13) & 1;
                    shry_[ch] = (data >> 14) & 1;
                    break;
                case 1: pitch_[ch] = data; break;
                default: break;
            }
            break;
        default:
            break;
    }
}

uint32_t ChAddrGen::pixel_index(uint8_t ch) const {
    ch &= 3;
    if (pitch_[ch] == 0) {
        return (static_cast<uint32_t>(yi_[ch] & 7) << 16) | xi_[ch];
    }
    int32_t prod = static_cast<int32_t>(static_cast<int16_t>(yi_[ch]))
                 * static_cast<int32_t>(pitch_[ch]);
    return static_cast<uint32_t>(prod + static_cast<int32_t>(static_cast<int16_t>(xi_[ch])));
}

uint16_t ChAddrGen::word(uint8_t ch) const {
    ch &= 3;
    FmtInfo f = get_fmt_info(fmt_[ch]);
    uint32_t sh_in = pixel_index(ch) << 4;
    int32_t  sh_out = static_cast<int32_t>(sh_in) >> f.k;
    return static_cast<uint16_t>((sh_out >> 4) & 0x7FFF);
}

uint8_t ChAddrGen::frac(uint8_t ch) const {
    ch &= 3;
    FmtInfo f = get_fmt_info(fmt_[ch]);
    uint32_t sh_in = pixel_index(ch) << 4;
    int32_t  sh_out = static_cast<int32_t>(sh_in) >> f.k;
    return static_cast<uint8_t>(sh_out & 0x0F);
}

bool ChAddrGen::inside(uint8_t ch) const {
    ch &= 3;
    if (!clip_[ch]) return true;
    int16_t x = static_cast<int16_t>(xi_[ch]);
    int16_t y = static_cast<int16_t>(yi_[ch]);
    return (x >= static_cast<int16_t>(xmin_[ch]) && x <= static_cast<int16_t>(xmax_[ch]) &&
            y >= static_cast<int16_t>(ymin_[ch]) && y <= static_cast<int16_t>(ymax_[ch]));
}

bool ChAddrGen::page_ovf(uint8_t ch) const {
    ch &= 3;
    FmtInfo f = get_fmt_info(fmt_[ch]);
    uint32_t sh_in = pixel_index(ch) << 4;
    int32_t  sh_out = static_cast<int32_t>(sh_in) >> f.k;
    return (static_cast<uint32_t>(sh_out) >> 19) != 0;
}

void ChAddrGen::step_forward(uint8_t ch) {
    ch &= 3;
    uint32_t x_sum = (static_cast<uint32_t>(xi_[ch]) << 16 | xf_[ch])
                   + (static_cast<uint32_t>(dxi_[ch]) << 16 | dxf_[ch]);
    uint32_t y_sum = (static_cast<uint32_t>(yi_[ch]) << 16 | yf_[ch])
                   + (static_cast<uint32_t>(dyi_[ch]) << 16 | dyf_[ch]);
    xi_[ch] = static_cast<uint16_t>(x_sum >> 16);
    xf_[ch] = static_cast<uint16_t>(x_sum & 0xFFFF);
    yi_[ch] = static_cast<uint16_t>(y_sum >> 16);
    yf_[ch] = static_cast<uint16_t>(y_sum & 0xFFFF);
}

void ChAddrGen::preload(uint8_t page, uint16_t word, uint16_t step_x) {
    fmt_[0]  = 0;
    page_[0] = page;
    xi_[0]   = word;
    yi_[0]   = 0;
    dxi_[0]  = step_x;
}

uint32_t ChAddrGen::raw_idx(uint8_t ch) const {
    return pixel_index(ch);
}

} // namespace lx