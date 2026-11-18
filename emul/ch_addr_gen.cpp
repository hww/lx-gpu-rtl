// === FILE: ch_addr_gen.cpp ===
#include "ch_addr_gen.h"

namespace lx {

void ChAddrGen::reset() {
    for (int i = 0; i < NUM_CH; ++i) {
        mode_[i] = 0;
        idxh_[i] = 0;
        idx_[i]  = 0;
        step_[i] = 0;
    }
}

void ChAddrGen::write_mode(uint8_t ch, uint8_t mode) {
    ch &= 3;
    mode_[ch] = mode & 7;
}

void ChAddrGen::write_idxh(uint8_t ch, uint8_t idxh) {
    ch &= 3;
    idxh_[ch] = idxh & 7;
}

void ChAddrGen::write_idx(uint8_t ch, uint16_t idx) {
    ch &= 3;
    uint8_t k = get_fmt_info(mode_[ch]).k;
    // Формируем 23-битное знаковое число {idxh[2:0], idx[15:0], 4'b0}
    uint32_t raw23 = (static_cast<uint32_t>(idxh_[ch] & 7) << 20)
                   | (static_cast<uint32_t>(idx) << 4);
    int32_t signed23 = static_cast<int32_t>(raw23 << 9) >> 9;
    idx_[ch]  = static_cast<uint32_t>(signed23 >> k) & 0xFFFFF;
    idxh_[ch] = 0; // в RTL idxh_r сбрасывается после записи idx
}

void ChAddrGen::write_step(uint8_t ch, uint16_t step) {
    ch &= 3;
    uint8_t k = get_fmt_info(mode_[ch]).k;
    // Формируем 23-битное знаковое число {{3{step[15]}}, step[15:0], 4'b0}
    int32_t signed23 = static_cast<int32_t>(static_cast<int16_t>(step)) << 4;
    step_[ch] = static_cast<uint32_t>(signed23 >> k) & 0xFFFFF;
}

void ChAddrGen::step_forward(uint8_t ch) {
    ch &= 3;
    idx_[ch] = (idx_[ch] + step_[ch]) & 0xFFFFF;
}

} // namespace lx
