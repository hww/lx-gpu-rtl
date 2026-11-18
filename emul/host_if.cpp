// === FILE: host_if.cpp ===
#include "host_if.h"

namespace lx {

void HostIf::reset() {
    addr0_ = 0;
    addr1_ = 0;
    addr2_ = 0;
    cmd_   = 0;
}

uint8_t HostIf::read(uint8_t addr) {
    switch (addr) {
        case 0: return addr0_;
        case 1: return addr1_;
        case 2: return addr2_;
        case 3: return cmd_ & 0x03;
        case 4: {
            uint8_t h   = gpu_.halted() ? 0x80 : 0x00;
            uint8_t err = gpu_.error() ? (gpu_.halt_code() & 0x7F) : 0x00;
            return h | err;
        }
        case 5: {                       // IRQ_CODE: reading acknowledges
            uint8_t code = gpu_.irq_code();
            gpu_.ack_irq();
            return code;
        }
        default: return 0x00;
    }
}

void HostIf::write(uint8_t addr, uint8_t data) {
    switch (addr) {
        case 0: addr0_ = data; break;
        case 1: addr1_ = data; break;
        case 2: addr2_ = data; break;
        case 3:
            cmd_ = data & 0x03;
            if (cmd_ == 1) gpu_.gpu_start(gpu_addr());
            if (cmd_ == 2) gpu_.gpu_reset();
            break;
        default:
            break;
    }
}

} // namespace lx
