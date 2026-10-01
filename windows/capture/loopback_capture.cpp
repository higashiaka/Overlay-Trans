#include "loopback_capture.h"

#include <miniaudio.h>

#include <utility>

namespace overlay_trans {

struct LoopbackCapture::Impl {
    ma_device device{};
    bool initialized = false;
    FrameCallback callback;
    std::string last_error;

    static void on_data(ma_device* device, void*, const void* input, ma_uint32 frame_count) {
        auto* self = static_cast<Impl*>(device->pUserData);
        self->callback(static_cast<const float*>(input), frame_count);
    }
};

LoopbackCapture::LoopbackCapture() : impl_(std::make_unique<Impl>()) {}

LoopbackCapture::~LoopbackCapture() {
    stop();
}

bool LoopbackCapture::start(const CaptureConfig& config, FrameCallback callback) {
    stop();

    if (!callback) {
        impl_->last_error = "callback is empty";
        return false;
    }
    impl_->callback = std::move(callback);

    // 장치 포맷과 다르면 miniaudio가 채널/샘플레이트를 변환해 전달한다.
    ma_device_config device_config = ma_device_config_init(ma_device_type_loopback);
    device_config.capture.format = ma_format_f32;
    device_config.capture.channels = config.channels;
    device_config.sampleRate = config.sample_rate;
    device_config.dataCallback = &Impl::on_data;
    device_config.pUserData = impl_.get();

    const ma_backend backends[] = {ma_backend_wasapi};
    ma_result result = ma_device_init_ex(backends, 1, nullptr, &device_config, &impl_->device);
    if (result != MA_SUCCESS) {
        impl_->last_error = ma_result_description(result);
        return false;
    }
    impl_->initialized = true;

    result = ma_device_start(&impl_->device);
    if (result != MA_SUCCESS) {
        impl_->last_error = ma_result_description(result);
        stop();
        return false;
    }
    return true;
}

void LoopbackCapture::stop() {
    if (!impl_->initialized) {
        return;
    }
    ma_device_uninit(&impl_->device);
    impl_->initialized = false;
}

const std::string& LoopbackCapture::last_error() const {
    return impl_->last_error;
}

}  // namespace overlay_trans
