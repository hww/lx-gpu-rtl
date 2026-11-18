// === FILE: ch_cache.cpp ===
#include "ch_cache.h"

namespace lx {

void ChCache::reset() {
    for (int i = 0; i < NUM_CH; ++i) {
        tag_[i]       = 0;
        data_[i]      = 0;
        page_[i]      = 0;
        pend_page_[i] = 0;
        valid_[i]     = false;
        dirty_[i]     = false;
        pend_[i]      = false;
    }
    hits = misses = writebacks = 0;
}

void ChCache::resolve_pending(uint8_t ch) {
    ch &= 3;
    if (!pend_[ch]) return;

    if (valid_[ch] && dirty_[ch]) {
        uint32_t addr24 = (static_cast<uint32_t>(page_[ch]) << 16)
                        | (static_cast<uint32_t>(tag_[ch] & 0x7FFF) << 1);
        mem_.write16(addr24, data_[ch]);
        ++writebacks;
    }
    page_[ch]  = pend_page_[ch];
    tag_[ch]   = 0;
    valid_[ch] = false;
    dirty_[ch] = false;
    pend_[ch]  = false;
}

void ChCache::write_page(uint8_t ch, uint8_t page) {
    ch &= 3;
    if (page != page_[ch]) {
        pend_[ch]      = true;
        pend_page_[ch] = page;
    }
}

uint16_t ChCache::read_word(uint8_t ch, uint16_t word_addr) {
    ch &= 3;
    word_addr &= 0x7FFF;
    resolve_pending(ch);

    if (valid_[ch] && tag_[ch] == word_addr) {
        ++hits;
        return data_[ch];
    }

    if (valid_[ch] && dirty_[ch]) {
        uint32_t wb_addr = (static_cast<uint32_t>(page_[ch]) << 16)
                         | (static_cast<uint32_t>(tag_[ch] & 0x7FFF) << 1);
        mem_.write16(wb_addr, data_[ch]);
        ++writebacks;
    }

    uint32_t rd_addr = (static_cast<uint32_t>(page_[ch]) << 16)
                     | (static_cast<uint32_t>(word_addr) << 1);
    data_[ch]  = mem_.read16(rd_addr);
    tag_[ch]   = word_addr;
    valid_[ch] = true;
    dirty_[ch] = false;
    ++misses;
    return data_[ch];
}

void ChCache::write_word(uint8_t ch, uint16_t word_addr, uint16_t data) {
    ch &= 3;
    word_addr &= 0x7FFF;
    resolve_pending(ch);

    if (!(valid_[ch] && tag_[ch] == word_addr)) {
        if (valid_[ch] && dirty_[ch]) {
            uint32_t wb_addr = (static_cast<uint32_t>(page_[ch]) << 16)
                             | (static_cast<uint32_t>(tag_[ch] & 0x7FFF) << 1);
            mem_.write16(wb_addr, data_[ch]);
            ++writebacks;
        }
        uint32_t rd_addr = (static_cast<uint32_t>(page_[ch]) << 16)
                         | (static_cast<uint32_t>(word_addr) << 1);
        data_[ch]  = mem_.read16(rd_addr);
        tag_[ch]   = word_addr;
        valid_[ch] = true;
        ++misses;
    } else {
        ++hits;
    }

    data_[ch]  = data;
    dirty_[ch] = true;
}

void ChCache::flush(uint8_t ch) {
    ch &= 3;
    resolve_pending(ch);

    if (valid_[ch] && dirty_[ch]) {
        uint32_t wb_addr = (static_cast<uint32_t>(page_[ch]) << 16)
                         | (static_cast<uint32_t>(tag_[ch] & 0x7FFF) << 1);
        mem_.write16(wb_addr, data_[ch]);
        ++writebacks;
    }
    valid_[ch] = false;
    dirty_[ch] = false;
}

} // namespace lx
