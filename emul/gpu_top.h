// === FILE: gpu_top.h ===
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include "memory.h"
#include "ch_addr_gen.h"
#include "ch_blender.h"
#include "ch_cache.h"
#include "core_alu.h"

namespace lx {

// Карта 6-битных операндов (0..63) из gpu_top.sv
enum Operand : uint8_t {
    OP_R0         = 0,  // 0..31: R0..R31
    OP_ZERO       = 32,
    OP_FLAGS      = 33,
    OP_BUSY       = 34,
    OP_SHADOW     = 35,
    OP_LOOP_CNT   = 36,
    OP_LOOP_TGT   = 37,
    OP_ROML       = 38,
    OP_ROMH       = 39,
    OP_ROMA       = 40,
    OP_MHI        = 41,
    OP_BANK       = 42,
    OP_RSV43      = 43,
    OP_DATA0      = 44, OP_DATA1 = 45, OP_DATA2 = 46, OP_DATA3 = 47,
    OP_W48        = 48, OP_W49 = 49, OP_W50 = 50, OP_W51 = 51,
    OP_W52        = 52, OP_W53 = 53, OP_W54 = 54, OP_W55 = 55,
    OP_W56        = 56, OP_W57 = 57, OP_W58 = 58, OP_W59 = 59,
    OP_W60        = 60, OP_W61 = 61, OP_W62 = 62, OP_W63 = 63
};

class GpuTop {
public:
    static constexpr size_t ROM_DEPTH = 4096;

    explicit GpuTop(Memory16& mem);

    void reset();
    void start(uint16_t entry = 0);           // start_i (без preload)
    void gpu_start(uint32_t preload_addr24);  // gpu_start_i (preload CH0 + run с PC=0)
    void gpu_reset();                         // gpu_reset_i (стоп и сброс)

    bool load_rom_hex(const std::string& path);
    void write_rom(uint16_t addr, uint32_t word);
    uint32_t read_rom(uint16_t addr) const { return rom_[addr & 0xFFF]; }

    int  step();                              // выполнить 1 инструкцию (возвращает 1)
    int  tick(int cycles);                    // выполнить до cycles инструкций
    bool run_until_halt(uint64_t limit = 1000000);

    // Статусы и выходы (аналог портов gpu_top.sv)
    bool     halted() const    { return halted_; }
    bool     error() const     { return halted_ && (halt_code_ != 0); }
    uint8_t  halt_code() const { return halt_code_; }
    bool     irq() const       { return irq_pending_; }
    uint8_t  irq_code() const  { return irq_code_; }
    void     ack_irq()         { irq_pending_ = false; }
    uint16_t pc() const        { return pc_; }

    // Доступ к внутренним регистрам для отладки и тестов
    uint16_t reg(uint8_t idx) const { return r_[idx & 31]; }
    uint8_t  flags() const          { return (v_<<3) | (c_<<2) | (n_<<1) | z_; }
    uint16_t mhi() const            { return mhi_; }
    uint16_t shadow() const         { return shadow_; }
    uint16_t loop_cnt() const       { return loop_cnt_; }
    uint16_t loop_tgt() const       { return loop_tgt_; }
    uint32_t ch_raw_idx(uint8_t ch) const { return addr_gen_.raw_idx(ch); }

    void set_trace(bool on) { trace_ = on; }

    uint16_t roma() const      { return roma_; }
    uint16_t roml() const      { return roml_; }
    bool     ch_error() const  { return any_ch_error(); }
    uint8_t  ch_status() const { return any_ch_error() ? 1 : 0; }

    ChCache& cache() { return cache_; }

private:
    void     clear_core_state();
    uint16_t read_special(uint8_t op, bool& halted_by_err);
    void     write_special(uint8_t op, uint16_t val, bool& halted_by_err);
    bool     any_ch_error() const;

    Memory16& mem_;
    ChAddrGen addr_gen_;
    ChCache   cache_;

    std::vector<uint32_t> rom_;

    // Состояние ядра
    bool     halted_      = true;
    uint8_t  halt_code_   = 0;
    bool     irq_pending_ = false;
    uint8_t  irq_code_    = 0;

    uint16_t pc_          = 0;
    uint16_t shadow_      = 0;
    uint16_t loop_cnt_    = 0;
    uint16_t loop_tgt_    = 0;

    uint16_t r_[32]       = {};
    bool     z_ = false, n_ = false, c_ = false, v_ = false;
    uint16_t mhi_         = 0;
    uint16_t roma_        = 0;
    uint16_t roml_        = 0;
    uint8_t  bank_        = 0;
    bool     trace_       = false;
};

} // namespace lx
