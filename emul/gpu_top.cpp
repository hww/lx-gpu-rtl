// === FILE: gpu_top.cpp ===
#include "gpu_top.h"
#include <fstream>
#include <cstdio>
#include <cstring>

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
    // Аппаратный preload из ch_top.sv:
    // Такт 1: CH0 MODE=0 (16bpp), PAGE=addr[23:16], IDX={1'b0, addr[15:1]}
    // Такт 2: CH0 STEP=1
    addr_gen_.write_mode(0, 0);
    cache_.write_page(0, static_cast<uint8_t>((preload_addr24 >> 16) & 0xFF));
    addr_gen_.write_idx(0, static_cast<uint16_t>((preload_addr24 >> 1) & 0x7FFF));
    addr_gen_.write_step(0, 1);

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
        case OP_DATA0: case OP_DATA1: case OP_DATA2: case OP_DATA3: {
            uint8_t ch = op & 3;
            if (addr_gen_.page_ovf(ch)) {
                halted_      = true;
                halt_code_   = 2;
                halted_by_err = true;
                return 0;
            }
            uint16_t w   = cache_.read_word(ch, addr_gen_.word(ch));
            uint16_t pix = ChBlender::unpack(w, addr_gen_.mode(ch), addr_gen_.frac(ch));
            addr_gen_.step_forward(ch);
            if (addr_gen_.page_ovf(ch)) {
                halted_      = true;
                halt_code_   = 2;
                halted_by_err = true;
            }
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
        case OP_DATA0: case OP_DATA1: case OP_DATA2: case OP_DATA3: {
            uint8_t ch = op & 3;
            if (addr_gen_.page_ovf(ch)) {
                halted_       = true;
                halt_code_    = 2;
                halted_by_err = true;
                return;
            }
            // masked write: no read-modify-write, as in ch_blender.sv/ch_cache.sv
            uint16_t msk = ChBlender::mask(addr_gen_.mode(ch), addr_gen_.frac(ch));
            if (msk)
                cache_.write_masked(ch, addr_gen_.word(ch),
                                    ChBlender::place(val, addr_gen_.mode(ch), addr_gen_.frac(ch)), msk);
            addr_gen_.step_forward(ch);
            if (addr_gen_.page_ovf(ch)) {
                halted_       = true;
                halt_code_    = 2;
                halted_by_err = true;
            }
            break;
        }
        case OP_IDX0: case OP_IDX1: case OP_IDX2: case OP_IDX3:
            addr_gen_.write_idx(op & 3, val);
            break;
        case OP_IDXH0: case OP_IDXH1: case OP_IDXH2: case OP_IDXH3:
            addr_gen_.write_idxh(op & 3, static_cast<uint8_t>(val & 7));
            break;
        case OP_STEP0: case OP_STEP1: case OP_STEP2: case OP_STEP3:
            addr_gen_.write_step(op & 3, val);
            break;
        case OP_MODEPAGE0: case OP_MODEPAGE1: case OP_MODEPAGE2: case OP_MODEPAGE3: {
            uint8_t ch = op & 3;
            addr_gen_.write_mode(ch, static_cast<uint8_t>((val >> 8) & 7));
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
    bool     is_rr    = (cls == 0);
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