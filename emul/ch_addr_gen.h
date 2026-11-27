#pragma once
#include <cstdint>
#include "gpu_fmt.h"

namespace lx {

class ChAddrGen {
public:
    static constexpr int NUM_CH = 4;

    ChAddrGen() { reset(); }
    void reset();

    void write_banked(uint8_t bank, uint8_t group, uint8_t ch, uint16_t data);

    uint16_t word(uint8_t ch) const;
    uint8_t  frac(uint8_t ch) const;
    uint8_t  mode(uint8_t ch) const    { return fmt_[ch & 3]; }
    bool     clip_on(uint8_t ch) const { return clip_[ch & 3]; }
    bool     shr_x(uint8_t ch) const   { return shrx_[ch & 3]; }
    bool     shr_y(uint8_t ch) const   { return shry_[ch & 3]; }
    uint16_t b(uint8_t ch) const       { return b_[ch & 3]; }
    uint16_t key(uint8_t ch) const     { return key_[ch & 3]; }
    uint16_t ctl(uint8_t ch) const     { return ctl_[ch & 3]; }
    bool     inside(uint8_t ch) const;
    bool     page_ovf(uint8_t ch) const;
    bool     par(uint8_t ch) const     { return (xi_[ch&3] ^ yi_[ch&3]) & 1; }

    void step_forward(uint8_t ch);
    void preload(uint8_t page, uint16_t word, uint16_t step_x);

    uint32_t raw_idx(uint8_t ch) const;

private:
    uint32_t pixel_index(uint8_t ch) const;

    uint16_t xi_[NUM_CH], xf_[NUM_CH], yi_[NUM_CH], yf_[NUM_CH];
    uint16_t dxi_[NUM_CH], dxf_[NUM_CH], dyi_[NUM_CH], dyf_[NUM_CH];
    uint16_t xmin_[NUM_CH], ymin_[NUM_CH], xmax_[NUM_CH], ymax_[NUM_CH];
    uint16_t b_[NUM_CH], key_[NUM_CH], ctl_[NUM_CH];
    uint16_t pitch_[NUM_CH];
    uint8_t  page_[NUM_CH];
    uint8_t  fmt_[NUM_CH];
    bool     clip_[NUM_CH], shrx_[NUM_CH], shry_[NUM_CH];
};

} // namespace lx