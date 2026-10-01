#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace overlay_trans {

// 오디오 파일(WAV, MP3, FLAC)을 지정한 샘플레이트와 채널 수의 float32로 읽는다.
class AudioFileReader {
public:
    AudioFileReader();
    ~AudioFileReader();

    AudioFileReader(const AudioFileReader&) = delete;
    AudioFileReader& operator=(const AudioFileReader&) = delete;

    bool open(const std::filesystem::path& path, uint32_t sample_rate, uint32_t channels);

    // 인터리브된 프레임을 읽어 실제로 읽은 프레임 수를 반환한다. 파일 끝이면 0을 반환한다.
    size_t read(float* frames, size_t frame_count);

    const std::string& last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
