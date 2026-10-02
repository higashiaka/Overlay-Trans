#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace overlay_trans {

// 디버깅용 16비트 PCM WAV 파일 기록기.
class WavWriter {
public:
    WavWriter() = default;
    ~WavWriter();

    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    bool open(const std::filesystem::path& path, uint32_t sample_rate, uint16_t channels);

    // samples는 인터리브된 float32 샘플이며 int16으로 변환해 기록한다.
    void write(const float* samples, size_t count);

    // 헤더의 크기 필드를 채우고 파일을 닫는다.
    void close();

    bool is_open() const;

private:
    std::ofstream file_;
    uint32_t data_bytes_ = 0;
};

}  // namespace overlay_trans
