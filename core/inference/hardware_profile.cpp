#include "hardware_profile.h"

#include <ggml-backend.h>

namespace overlay_trans {

HardwareProfile probe_hardware() {
    HardwareProfile best;
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        const ggml_backend_dev_t device = ggml_backend_dev_get(i);
        const auto type = ggml_backend_dev_type(device);
        if (type != GGML_BACKEND_DEVICE_TYPE_GPU && type != GGML_BACKEND_DEVICE_TYPE_IGPU) continue;

        size_t free_bytes = 0;
        size_t total_bytes = 0;
        ggml_backend_dev_memory(device, &free_bytes, &total_bytes);
        const size_t free_mb = free_bytes / (1024 * 1024);
        if (best.gpu && free_mb <= best.free_vram_mb) continue;
        best = {.gpu = true, .free_vram_mb = free_mb, .total_vram_mb = total_bytes / (1024 * 1024)};
    }
    return best;
}

}  // namespace overlay_trans
