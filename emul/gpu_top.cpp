// === FILE: gpu_top.cpp ===
#include "gpu_top.h"
#include <fstream>
#include <cstdio>
#include <cstring>

    namespace {
uint16_t pixel_alu(uint16_t a, uint16_t b, uint16_t d, uint16_t k,
                   uint16_t ctl, uint16_t mask,
                   bool shr_x, bool shr_y, bool par,
                   bool& rel_ok, bool& den_ok, bool& need_d)
{
    uint8_t op  = ctl & 0xF;
    uint8_t sx  = (ctl >> 4) & 3;
    uint8_t sy  = (ctl >> 6) & 3;
    uint8_t cs  = (ctl >> 8) & 3;
    uint8_t rel = (ctl >> 10) & 7;
    bool    den = (ctl >> 13) & 1;
    bool    dpar= (ctl >> 14) & 1;

    a &= mask; b &= mask; d &= mask; k &= mask;
    auto sel = [](uint8_t s, uint16_t A, uint16_t B, uint16_t D, uint16_t K) {
        switch (s) { case 0: return A; case 1: return B; case 2: return D; default: return K; }
    };
    uint16_t x_sel = sel(sx, a, b, d, k);
    uint16_t y_sel = sel(sy, a, b, d, k);
    uint16_t x = shr_x ? (x_sel >> 1) : x_sel;
    uint16_t y = shr_y ? (y_sel >> 1) : y_sel;

    uint16_t sum   = static_cast<uint16_t>(x + y);
    bool     over  = (static_cast<uint32_t>(x) + y) > mask;
    bool     under = x < y;

    uint16_t r_raw;
    switch (op) {
        case 0: r_raw = x; break;
        case 1: r_raw = x & y; break;
        case 2: r_raw = x | y; break;
        case 3: r_raw = x ^ y; break;
        case 4: r_raw = x & ~y; break;
        case 5: r_raw = sum; break;
        case 6: r_raw = over ? mask : sum; break;
        case 7: r_raw = under ? 0 : static_cast<uint16_t>(x - y); break;
        case 8: r_raw = static_cast<uint16_t>((x & b) | (y & ~b)); break;
        default: r_raw = x; break;
    }
    uint16_t r = r_raw & mask;

    uint16_t cp, cq;
    switch (cs) {
        case 0: cp = a; cq = k; break;
        case 1: cp = d; cq = k; break;
        case 2: cp = a; cq = d; break;
        default: cp = d; cq = b; break;
    }
    bool eq = (cp == cq);
    bool lt = (cp < cq);
    switch (rel) {
        case 0: rel_ok = true; break;
        case 1: rel_ok = eq; break;
        case 2: rel_ok = !eq; break;
        case 3: rel_ok = lt; break;
        case 4: rel_ok = !lt; break;
        case 5: rel_ok = !lt && !eq; break;
        case 6: rel_ok = lt || eq; break;
        default: rel_ok = false; break;
    }
    den_ok = !den || (par == dpar);
    need_d = (sx == 2) || (sy == 2)
           || ((rel != 0) && (rel != 7) && (cs != 0));
    return r;
}
} // namespace

namespace lx {

GpuTop::GpuTop(Memory16& mem)
    : mem_(mem), cache_(mem), rom_(ROM_DEPTH, 0) {
    reset();
}

void GpuTop::clear_core_state() {
    pc_          = 0;
    shadow_      = 0;
    loop_cnt_    = 0;
    loop_tgt_    = 0;
    z_ = n_ = c_ = v_ = false;
    mhi_         = 0;
    roma_        = 0;
    roml_        = 0;
    bank_        = 0;
}

void GpuTop::reset() {
    std::memset(r_, 0, sizeof(r_));
    addr_gen_.reset();
    cache_.reset();
    clear_core_state();
    halted_      = true;
    halt_code_   = 0;
    irq_pending_ = false;
    irq_code_    = 0;
}

void GpuTop::gpu_reset() {
    clear_core_state();
    halted_      = true;
    halt_code_   = 0;
    irq_pending_ = false;
}

void GpuTop::start(uint16_t entry) {
    clear_core_state();
    pc_          = entry & 0xFFF;
    halted_      = false;
    halt_code_   = 0;
    irq_pending_ = false;
}

void GpuTop::gpu_start(uint32_t preload_addr24) {
    // v2 preload: CH0 = 16bpp, linear, XI = word, DXI = 1
    addr_gen_.write_banked(4, 0, 0, 0x0000);                     // MODE_PAGE = 16bpp, page 0
    cache_.write_page(0, static_cast<uint8_t>((preload_addr24 >> 16) & 0xFF));
    addr_gen_.write_banked(0, 0, 0, static_cast<uint16_t>((preload_addr24 >> 1) & 0x7FFF));  // XI
    addr_gen_.write_banked(1, 0, 0, 1);                          // DXI = 1
    // the rest is cleared as in the RTL (ch_addr_gen, pl_we_i): YI, XF, YF,
    // DYI, DXF, DYF, ALU_CTL, PITCH. A YI left by the previous program would
    // move the bootloader's reads into another 64 KB window.
    addr_gen_.write_banked(0, 1, 0, 0);                          // YI
    addr_gen_.write_banked(0, 2, 0, 0);                          // XF
    addr_gen_.write_banked(0, 3, 0, 0);                          // YF
    addr_gen_.write_banked(1, 1, 0, 0);                          // DYI
    addr_gen_.write_banked(1, 2, 0, 0);                          // DXF
    addr_gen_.write_banked(1, 3, 0, 0);                          // DYF
    addr_gen_.write_banked(3, 2, 0, 0);                          // ALU_CTL
    addr_gen_.write_banked(4, 1, 0, 0);                          // PITCH

    clear_core_state();
    halted_      = false;
    halt_code_   = 0;
    irq_pending_ = false;
}

bool GpuTop::load_rom_hex(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line;
    size_t addr = 0;
    while (std::getline(f, line) && addr < ROM_DEPTH) {
        if (line.empty() || line[0] == '/' || line[0] == '#') continue;
        if (line[0] == '@') {
            addr = std::stoul(line.substr(1), nullptr, 16);
            continue;
        }
        rom_[addr++] = static_cast<uint32_t>(std::stoul(line, nullptr, 16));
    }
    return true;
}

void GpuTop::write_rom(uint16_t addr, uint32_t word) {
    rom_[addr & 0xFFF] = word;
}

bool GpuTop::any_ch_error() const {
    for (uint8_t ch = 0; ch < 4; ++ch) {
        if (addr_gen_.page_ovf(ch)) return true;
    }
    return false;
}

uint16_t GpuTop::read_special(uint8_t op, bool& halted_by_err) {
    if (op < 32) return r_[op];
    switch (op) {
        case OP_ZERO:     return 0;
        case OP_FLAGS:    return flags();
        case OP_BUSY:     return any_ch_error() ? 1 : 0; // {3'b0, pe_page_ovf}
        case OP_SHADOW:   return shadow_;
        case OP_LOOP_CNT: return loop_cnt_;
        case OP_LOOP_TGT: return loop_tgt_ & 0xFFF;
        case OP_ROML:     return static_cast<uint16_t>(rom_[roma_ & 0xFFF] & 0xFFFF);
        case OP_ROMH: {
            uint16_t hi = static_cast<uint16_t>((rom_[roma_ & 0xFFF] >> 16) & 0xFFFF);
            roma_ = (roma_ + 1) & 0xFFF;
            return hi;
        }
        case OP_MHI:      return mhi_;
        case OP_BANK:     return bank_;
        case OP_DATA0: case OP_DATA1: case OP_DATA2: case OP_DATA3: {
            uint8_t ch = op & 3;
            if (!addr_gen_.inside(ch) || addr_gen_.page_ovf(ch)) {
                if (addr_gen_.inside(ch) && addr_gen_.page_ovf(ch)) {
                    halted_       = true;
                    halt_code_    = 2;
                    halted_by_err = true;
                }
                addr_gen_.step_forward(ch);
                return addr_gen_.key(ch);
            }
            uint16_t w   = cache_.read_word(ch, addr_gen_.word(ch));
            uint16_t pix = ChBlender::unpack(w, addr_gen_.mode(ch), addr_gen_.frac(ch));
            addr_gen_.step_forward(ch);
            return pix;
        }
        default:
            return 0; // W-only или зарезервированные операнды читаются как 0
    }
}

void GpuTop::write_special(uint8_t op, uint16_t val, bool& halted_by_err) {
    if (op < 32) {
        r_[op] = val;
        return;
    }
    switch (op) {
        case OP_ZERO:
            break;
        case OP_FLAGS:
            z_ = (val >> 0) & 1;
            n_ = (val >> 1) & 1;
            c_ = (val >> 2) & 1;
            v_ = (val >> 3) & 1;
            break;
        case OP_SHADOW:
            shadow_ = val;
            break;
        case OP_LOOP_CNT:
            loop_cnt_ = val;
            break;
        case OP_LOOP_TGT:
            loop_tgt_ = val & 0xFFF;
            break;
        case OP_ROML:
            roml_ = val;
            break;
        case OP_ROMH:
            if ((roma_ & 0xFFF) < 32) {
                // Защита первых 32 слов ROM (halt_code = 1)
                halted_       = true;
                halt_code_    = 1;
                halted_by_err = true;
            } else {
                rom_[roma_ & 0xFFF] = (static_cast<uint32_t>(val) << 16) | roml_;
                roma_ = (roma_ + 1) & 0xFFF;
            }
            break;
        case OP_ROMA:
            roma_ = val & 0xFFF;
            break;
        case OP_BANK:
            bank_ = val & 7;    // takes effect NEXT instruction
            break;            
        case OP_DATA0: case OP_DATA1: case OP_DATA2: case OP_DATA3: {
            uint8_t ch = op & 3;
            if (!addr_gen_.inside(ch) || addr_gen_.page_ovf(ch)) {
                if (addr_gen_.inside(ch) && addr_gen_.page_ovf(ch)) {
                    halted_       = true;
                    halt_code_    = 2;
                    halted_by_err = true;
                }
                addr_gen_.step_forward(ch);
                return;
            }
            uint16_t msk = ChBlender::mask(addr_gen_.mode(ch), addr_gen_.frac(ch));
            if (msk) {
                FmtInfo fi = get_fmt_info(addr_gen_.mode(ch));
                bool rel_ok = true, den_ok = true, need_d = false;
                pixel_alu(val, addr_gen_.b(ch), 0, addr_gen_.key(ch), addr_gen_.ctl(ch),
                          fi.pix_mask, addr_gen_.shr_x(ch), addr_gen_.shr_y(ch),
                          addr_gen_.par(ch), rel_ok, den_ok, need_d);
                uint16_t d = 0;
                if (need_d) {
                    uint16_t w = cache_.read_word(ch, addr_gen_.word(ch));
                    d = ChBlender::unpack(w, addr_gen_.mode(ch), addr_gen_.frac(ch));
                }
                uint16_t r = pixel_alu(val, addr_gen_.b(ch), d, addr_gen_.key(ch),
                                       addr_gen_.ctl(ch), fi.pix_mask,
                                       addr_gen_.shr_x(ch), addr_gen_.shr_y(ch),
                                       addr_gen_.par(ch), rel_ok, den_ok, need_d);
                if (rel_ok && den_ok)
                    cache_.write_masked(ch, addr_gen_.word(ch),
                                        ChBlender::place(r, addr_gen_.mode(ch), addr_gen_.frac(ch)), msk);
            }
            addr_gen_.step_forward(ch);
            break;
        }
        case OP_W48: case OP_W49: case OP_W50: case OP_W51:
        case OP_W52: case OP_W53: case OP_W54: case OP_W55:
        case OP_W56: case OP_W57: case OP_W58: case OP_W59:
        case OP_W60: case OP_W61: case OP_W62: case OP_W63: {
            uint8_t grp = (op - 48) >> 2;
            uint8_t ch  = op & 3;
            addr_gen_.write_banked(bank_, grp, ch, val);
            // SURF.MODE_PAGE also carries the page for the cache, as in the
            // RTL (ch_top: cache_page_we on bank 4, group 0)
            if (bank_ == 4 && grp == 0)
                cache_.write_page(ch, static_cast<uint8_t>(val & 0xFF));
            break;
        }
        default:
            break;
    }
}

int GpuTop::step() {
    if (halted_) return 0;

    uint16_t cur_pc = pc_ & 0xFFF;
    uint32_t ins    = rom_[cur_pc];
    uint16_t next_pc = (cur_pc + 1) & 0xFFF;

    // Декодирование полей слова инструкции (gpu_top.sv)
    bool     loop_bit = (ins >> 31) & 1;
    uint8_t  cls      = (ins >> 29) & 3;
    //bool     is_rr    = (cls == 0);
    bool     is_ldi   = (cls == 1);
    bool     is_ctrl  = (cls == 2);
    bool     is_imm   = (cls == 3);

    uint8_t  cond     = (ins >> 25) & 0xF;
    bool     write_en = is_ldi || ((ins >> 24) & 1);
    bool     set_f    = !is_ldi && ((ins >> 23) & 1);
    uint8_t  dst      = (ins >> 17) & 0x3F;
    uint8_t  alu_op   = is_ldi ? 0 : ((ins >> 12) & 0x1F);
    bool     reg_jmp  = (ins >> 16) & 1;
    uint8_t  ctrl_op  = (ins >> 12) & 0xF;
    uint8_t  src      = (ins >> 6) & 0x3F;
    bool     sx       = (ins >> 11) & 1;
    uint16_t imm11    = ins & 0x7FF;
    uint16_t addr12   = ins & 0xFFF;
    uint16_t imm16    = ins & 0xFFFF;

    if (trace_) {
        std::printf("PC=%03X INS=%08X cls=%u cond=%u dst=%u src=%u flags=%X\n",
                    cur_pc, ins, cls, cond, dst, src, flags());
    }

    // Проверка ошибки канала (pe_page_ovf -> halt_code = 2)
    if (any_ch_error()) {
        halted_    = true;
        halt_code_ = 2;
        return 1;
    }

    bool cond_ok       = CoreAlu::check_cond(cond, flags());
    bool branch_taken  = false;
    bool halted_by_err = false;

    if (cond_ok) {
        if (is_ctrl) {
            switch (ctrl_op) {
                case CTRL_NOP:
                    break;
                case CTRL_JMP:
                    if (reg_jmp) {
                        uint16_t target = read_special(src, halted_by_err);
                        next_pc = target & 0xFFF;
                    } else {
                        next_pc = addr12;
                    }
                    branch_taken = true;
                    break;
                case CTRL_CALL:
                    if (!reg_jmp) {
                        // В core_fetch.sv shadow_r <= {flags_i, pc_r}, где pc_r = cur_pc + 1
                        shadow_      = (static_cast<uint16_t>(flags()) << 12) | ((cur_pc + 1) & 0xFFF);
                        next_pc      = addr12;
                        branch_taken = true;
                    }
                    break;
                case CTRL_RET:
                    if (!reg_jmp) {
                        next_pc      = shadow_ & 0xFFF;
                        branch_taken = true;
                    }
                    break;
                case CTRL_HALT:
                    halted_    = true;
                    halt_code_ = addr12 & 0x7F;
                    return 1;
                case CTRL_IRQ:
                    irq_pending_ = true;
                    irq_code_    = addr12 & 0xFF;
                    break;
                case CTRL_FLUSH:
                    cache_.flush(addr12 & 3);
                    break;
                default:
                    break;
            }
        } else {
            // Арифметико-логическая инструкция (RR / LDI / IMM)
            uint16_t op_a = r_[dst & 0x1F]; // в RTL opa_addr = dst[4:0]
            uint16_t op_b = 0;

            if (is_imm) {
                op_b = (sx && (imm11 & 0x400)) ? static_cast<uint16_t>(0xF800 | imm11)
                                               : imm11;
            } else if (is_ldi) {
                op_b = imm16;
            } else { // is_rr
                op_b = read_special(src, halted_by_err);
            }

if (!halted_by_err) {
AluOut alu = CoreAlu::exec(alu_op, op_a, op_b, c_);
bool is_mov = (alu_op == 0);
// Запись результата
if (write_en) {
if (!((dst >> 5) & 1)) {
r_[dst & 0x1F] = alu.result;
} else if (is_mov) {
// Специальный регистр-приёмник разрешён только для MOV
write_special(dst, alu.result, halted_by_err);
}
}
// Запись MHI при умножении (alu_op[4:3] == 2'b11)
if (((alu_op >> 3) & 3) == 3) {
mhi_ = alu.mul_hi;
}
// Обновление флагов (если не было прямой записи в FLAGS)
bool wrote_flags_direct = write_en && is_mov && (dst == OP_FLAGS);
if (set_f && !wrote_flags_direct) {
z_ = alu.z;
n_ = alu.n;
if (alu.upd_cv) {
c_ = alu.c;
v_ = alu.v;
}
}
}
}
}
if (halted_by_err || halted_) {
return 1;
}
// Аппаратный цикл L (выполняется даже если COND ложно, но не при сработавшем ветвлении)
if (loop_bit && (loop_cnt_ > 1) && !branch_taken) {
next_pc = loop_tgt_ & 0xFFF;
loop_cnt_--;
}
pc_ = next_pc & 0xFFF;
return 1;
}
int GpuTop::tick(int cycles) {
int executed = 0;
while (executed < cycles && !halted_) {
executed += step();
}
return executed;
}
bool GpuTop::run_until_halt(uint64_t limit) {
while (!halted_ && limit-- > 0) {
step();
}
return halted_;
}
} // namespace lx