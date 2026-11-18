// === FILE: host_if.h ===
#pragma once
#include <cstdint>
#include "gpu_top.h"

namespace lx {

class HostIf {
public:
    explicit HostIf(GpuTop& gpu) : gpu_(gpu) { reset(); }

    void    reset();
    uint8_t read(uint8_t addr);
    void    write(uint8_t addr, uint8_t data);

    uint32_t gpu_addr() const {
        return (static_cast<uint32_t>(addr2_) << 16)
             | (static_cast<uint32_t>(addr1_) << 8)
             | static_cast<uint32_t>(addr0_);
    }

private:
    GpuTop& gpu_;
    uint8_t addr0_ = 0;
    uint8_t addr1_ = 0;
    uint8_t addr2_ = 0;
    uint8_t cmd_   = 0;
};

} // namespace lx