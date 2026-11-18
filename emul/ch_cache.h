// === FILE: ch_cache.h ===
#pragma once
#include <cstdint>
#include "memory.h"

namespace lx {

class ChCache {
public:
    static constexpr int NUM_CH = 4;

    explicit ChCache(Memory16& mem) : mem_(mem) { reset(); }

    void reset();

    void     write_page(uint8_t ch, uint8_t page);
    uint8_t  page(uint8_t ch) const { return page_[ch & 3]; }

    uint16_t read_word(uint8_t ch, uint16_t word_addr);
    void     write_word(uint8_t ch, uint16_t word_addr, uint16_t data);
    void     flush(uint8_t ch);

    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t writebacks = 0;

private:
    void resolve_pending(uint8_t ch);

    Memory16& mem_;
    uint16_t  tag_[NUM_CH];
    uint16_t  data_[NUM_CH];
    uint8_t   page_[NUM_CH];
    uint8_t   pend_page_[NUM_CH];
    bool      valid_[NUM_CH];
    bool      dirty_[NUM_CH];
    bool      pend_[NUM_CH];
};

} // namespace lx
