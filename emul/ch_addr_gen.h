// === FILE: ch_addr_gen.h ===
#pragma once
#include <cstdint>
#include "gpu_fmt.h"

namespace lx {

class ChAddrGen {
public:
    static constexpr int NUM_CH = 4;

    ChAddrGen() { reset(); }

    void reset();

    void write_mode(uint8_t ch, uint8_t mode);
    void write_idxh(uint8_t ch, uint8_t idxh);
    void write_idx(uint8_t ch, uint16_t idx);
    void write_step(uint8_t ch, uint16_t step);

    void step_forward(uint8_t ch);

    uint16_t word(uint8_t ch) const     { return (idx_[ch & 3] >> 4) & 0x7FFF; }
    uint8_t  frac(uint8_t ch) const     { return idx_[ch & 3] & 0x0F; }
    uint8_t  mode(uint8_t ch) const     { return mode_[ch & 3]; }
    bool     page_ovf(uint8_t ch) const { return (idx_[ch & 3] >> 19) & 1; }
    uint32_t raw_idx(uint8_t ch) const  { return idx_[ch & 3]; }

private:
    uint8_t  mode_[NUM_CH];
    uint8_t  idxh_[NUM_CH];
    uint32_t idx_[NUM_CH];  // 20 бит: [19]=ovf, [18:4]=word, [3:0]=frac
    uint32_t step_[NUM_CH]; // 20 бит (в доп. коде)
};

} // namespace lx
