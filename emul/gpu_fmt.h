// === FILE: gpu_fmt.h ===
#pragma once
#include <cstdint>

namespace lx {

enum PixFmt : uint8_t {
    FMT_16BPP    = 0, // k=0
    FMT_8BPP     = 1, // k=1
    FMT_4BPP     = 2, // k=2
    FMT_2BPP     = 3, // k=3 (read-only)
    FMT_1BPP     = 4, // k=4 (read-only)
    FMT_PASS_LO8 = 5, // k=0, младший байт (sh=0)
    FMT_PASS_HI8 = 6, // k=0, старший байт (sh=8)
    FMT_RSV      = 7  // резерв (read-only)
};

struct FmtInfo {
    uint8_t  k;
    uint16_t pix_mask;
    uint8_t  s_mask;
    bool     wr_en;
};

inline FmtInfo get_fmt_info(uint8_t fmt) {
    switch (fmt & 7) {
        case 0:  return {0, 0xFFFF, 0x0, true};  // 16 bpp
        case 1:  return {1, 0x00FF, 0x8, true};  // 8 bpp
        case 2:  return {2, 0x000F, 0xC, true};  // 4 bpp
        case 3:  return {3, 0x0003, 0xE, false}; // 2 bpp
        case 4:  return {4, 0x0001, 0xF, false}; // 1 bpp
        case 5:  return {0, 0x00FF, 0x0, true};  // LO8
        case 6:  return {0, 0x00FF, 0x8, true};  // HI8
        default: return {0, 0xFFFF, 0x0, false}; // резерв
    }
}

} // namespace lx
