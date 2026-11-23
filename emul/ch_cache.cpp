// === FILE: ch_cache.cpp ===
// Functional model of ch_cache.sv: one entry per channel, masked writes that
// never read memory, a dirty mask per nibble and write-back with a byte mask.
// An entry with a half-written byte and no fill is filled before its
// write-back (lazy fill). The posted write buffer of the RTL only changes
// timing, so it is not modelled.
#include "ch_cache.h"

namespace lx {

static uint16_t nib_bits(uint8_t n) {
    return static_cast<uint16_t>((n & 1 ? 0x000F : 0) | (n & 2 ? 0x00F0 : 0) |
                                 (n & 4 ? 0x0F00 : 0) | (n & 8 ? 0xF000 : 0));
}
static uint8_t nib_of(uint16_t m) {
    return static_cast<uint8_t>((m & 0x000F ? 1 : 0) | (m & 0x00F0 ? 2 : 0) |
                                (m & 0x0F00 ? 4 : 0) | (m & 0xF000 ? 8 : 0));
}

void ChCache::reset() {
    for (int i = 0; i < NUM_CH; ++i) {
        tag_[i]       = 0;
        data_[i]      = 0;
        page_[i]      = 0;
        pend_page_[i] = 0;
        valid_[i]     = false;
        dirty_[i]     = false;
        dmask_[i]     = 0;
        full_[i]      = false;
        pend_[i]      = false;
    }
    hits = misses = writebacks = 0;
}

// fill the entry with word_addr, keeping its dirty nibbles (merge fill)
void ChCache::fill(uint8_t ch, uint16_t word_addr) {
    uint32_t rd_addr = (static_cast<uint32_t>(page_[ch]) << 16)
                     | (static_cast<uint32_t>(word_addr) << 1);
    uint16_t keep = nib_bits(dmask_[ch]);
    data_[ch]  = static_cast<uint16_t>((mem_.read16(rd_addr) & ~keep) | (data_[ch] & keep));
    tag_[ch]   = word_addr;
    valid_[ch] = true;
    full_[ch]  = true;
    ++misses;
}

void ChCache::write_back(uint8_t ch) {
    if (!(valid_[ch] && dmask_[ch])) return;
    uint8_t n = dmask_[ch];
    if (!full_[ch] && (((n ^ (n >> 1)) & 1) || (((n >> 2) ^ (n >> 3)) & 1)))
        fill(ch, tag_[ch]);                        // a half-written byte
    uint32_t addr24 = (static_cast<uint32_t>(page_[ch]) << 16)
                    | (static_cast<uint32_t>(tag_[ch] & 0x7FFF) << 1);
    if (full_[ch]) {
        mem_.write16(addr24, data_[ch]);
    } else {
        if (dmask_[ch] & 0x3) mem_.write8(addr24,     static_cast<uint8_t>(data_[ch]));
        if (dmask_[ch] & 0xC) mem_.write8(addr24 + 1, static_cast<uint8_t>(data_[ch] >> 8));
    }
    ++writebacks;
    dmask_[ch] = 0;
    dirty_[ch] = false;
}

void ChCache::resolve_pending(uint8_t ch) {
    ch &= 3;
    if (!pend_[ch]) return;

    write_back(ch);
    page_[ch]  = pend_page_[ch];
    tag_[ch]   = 0;
    valid_[ch] = false;
    full_[ch]  = false;
    dmask_[ch] = 0;
    dirty_[ch] = false;
    pend_[ch]  = false;
}

void ChCache::write_page(uint8_t ch, uint8_t page) {
    ch &= 3;
    if (page != page_[ch]) {
        pend_[ch]      = true;
        pend_page_[ch] = page;
    } else {
        pend_[ch]      = false;     // back to the current page: A -> B -> A = A
    }
}

uint16_t ChCache::read_word(uint8_t ch, uint16_t word_addr) {
    ch &= 3;
    word_addr &= 0x7FFF;
    resolve_pending(ch);

    if (valid_[ch] && tag_[ch] == word_addr) {
        if (!full_[ch]) fill(ch, word_addr);       // partial entry: merge fill
        else ++hits;
        return data_[ch];
    }
    write_back(ch);
    dmask_[ch] = 0;
    fill(ch, word_addr);
    return data_[ch];
}

void ChCache::write_masked(uint8_t ch, uint16_t word_addr, uint16_t data, uint16_t mask) {
    ch &= 3;
    word_addr &= 0x7FFF;
    resolve_pending(ch);

    if (!(valid_[ch] && tag_[ch] == word_addr)) {
        write_back(ch);
        tag_[ch]   = word_addr;                    // allocate, no read
        valid_[ch] = true;
        full_[ch]  = false;
        dmask_[ch] = 0;
        ++misses;
    } else {
        ++hits;
    }
    data_[ch]   = static_cast<uint16_t>((data_[ch] & ~mask) | (data & mask));
    dmask_[ch] |= nib_of(mask);
    if (dmask_[ch] == 0xF) full_[ch] = true;
    dirty_[ch]  = true;
}

void ChCache::write_word(uint8_t ch, uint16_t word_addr, uint16_t data) {
    write_masked(ch, word_addr, data, 0xFFFF);
}

void ChCache::flush(uint8_t ch) {
    ch &= 3;
    resolve_pending(ch);
    write_back(ch);
    valid_[ch] = false;
    full_[ch]  = false;
    dmask_[ch] = 0;
    dirty_[ch] = false;
}

} // namespace lx
