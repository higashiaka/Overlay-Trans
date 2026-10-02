#include "wav_writer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace overlay_trans {

namespace {

// WAV는 리틀 엔디언 포맷이라 필드를 메모리 표현 그대로 기록한다.
static_assert(std::endian::native == std::endian::little);

constexpr uint16_t PCM_FORMAT_TAG = 1;
constexpr uint16_t BITS_PER_SAMPLE = 16;
constexpr uint32_t FMT_CHUNK_SIZE = 16;
constexpr uint32_t HEADER_SIZE = 44;
constexpr std::streamoff RIFF_SIZE_OFFSET = 4;
constexpr std::streamoff DATA_SIZE_OFFSET = 40;

template <typename T>
void put(std::ofstream& file, T value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

}  // namespace

WavWriter::~WavWriter() {
    close();
}

bool WavWriter::open(const std::filesystem::path& path, uint32_t sample_rate, uint16_t channels) {
    close();

    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_) {
        return false;
    }

    const auto block_align = static_cast<uint16_t>(channels * BITS_PER_SAMPLE / 8);

    // 크기 필드는 close()에서 채운다.
    file_.write("RIFF", 4);
    put<uint32_t>(file_, 0);
    file_.write("WAVE", 4);

    file_.write("fmt ", 4);
    put<uint32_t>(file_, FMT_CHUNK_SIZE);
    put<uint16_t>(file_, PCM_FORMAT_TAG);
    put<uint16_t>(file_, channels);
    put<uint32_t>(file_, sample_rate);
    put<uint32_t>(file_, sample_rate * block_align);
    put<uint16_t>(file_, block_align);
    put<uint16_t>(file_, BITS_PER_SAMPLE);

    file_.write("data", 4);
    put<uint32_t>(file_, 0);

    data_bytes_ = 0;
    return file_.good();
}

void WavWriter::write(const float* samples, size_t count) {
    std::array<int16_t, 1024> pcm;
    while (count > 0) {
        const size_t batch = std::min(count, pcm.size());
        for (size_t i = 0; i < batch; ++i) {
            const float clamped = std::clamp(samples[i], -1.0f, 1.0f);
            pcm[i] = static_cast<int16_t>(std::lround(clamped * 32767.0f));
        }

        const size_t bytes = batch * sizeof(int16_t);
        file_.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(bytes));
        data_bytes_ += static_cast<uint32_t>(bytes);

        samples += batch;
        count -= batch;
    }
}

void WavWriter::close() {
    if (!file_.is_open()) {
        return;
    }

    file_.seekp(RIFF_SIZE_OFFSET);
    put<uint32_t>(file_, HEADER_SIZE - 8 + data_bytes_);
    file_.seekp(DATA_SIZE_OFFSET);
    put<uint32_t>(file_, data_bytes_);
    file_.close();
}

bool WavWriter::is_open() const {
    return file_.is_open();
}

}  // namespace overlay_trans
