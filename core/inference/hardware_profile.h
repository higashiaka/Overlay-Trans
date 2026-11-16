#pragma once

#include <cstddef>

namespace overlay_trans {

struct HardwareProfile {
    bool gpu = false;
    size_t free_vram_mb = 0;
    size_t total_vram_mb = 0;
};

HardwareProfile probe_hardware();

}  // namespace overlay_trans
