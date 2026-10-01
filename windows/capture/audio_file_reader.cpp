#include "audio_file_reader.h"

#include <miniaudio.h>

namespace overlay_trans {

struct AudioFileReader::Impl {
    ma_decoder decoder{};
    bool opened = false;
    std::string last_error;
};

AudioFileReader::AudioFileReader() : impl_(std::make_unique<Impl>()) {}

AudioFileReader::~AudioFileReader() {
    if (impl_->opened) {
        ma_decoder_uninit(&impl_->decoder);
    }
}

bool AudioFileReader::open(const std::filesystem::path& path, uint32_t sample_rate, uint32_t channels) {
    if (impl_->opened) {
        ma_decoder_uninit(&impl_->decoder);
        impl_->opened = false;
    }

    // 파일 포맷과 다르면 miniaudio가 채널/샘플레이트를 변환해 전달한다.
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, channels, sample_rate);
    // 한글/일본어 파일 이름을 열 수 있도록 유니코드 경로를 그대로 넘긴다.
    const ma_result result = ma_decoder_init_file_w(path.c_str(), &config, &impl_->decoder);
    if (result != MA_SUCCESS) {
        impl_->last_error = ma_result_description(result);
        return false;
    }

    impl_->opened = true;
    return true;
}

size_t AudioFileReader::read(float* frames, size_t frame_count) {
    ma_uint64 frames_read = 0;
    ma_decoder_read_pcm_frames(&impl_->decoder, frames, frame_count, &frames_read);
    return static_cast<size_t>(frames_read);
}

const std::string& AudioFileReader::last_error() const {
    return impl_->last_error;
}

}  // namespace overlay_trans
