// === FILE: ch_blender.h ===
#pragma once
#include <cstdint>
#include "gpu_fmt.h"

namespace lx {

class ChBlender {
public:
    static uint16_t unpack(uint16_t word, uint8_t mode, uint8_t frac);
    static uint16_t blend(uint16_t word, uint16_t pixel, uint8_t mode, uint8_t frac);
    // bits of the word a pixel write replaces (0 if the format is write-protected)
    static uint16_t mask(uint8_t mode, uint8_t frac);
    // the pixel shifted into its position
    static uint16_t place(uint16_t pixel, uint8_t mode, uint8_t frac);
};

} // namespace lx
