#include "audio_file_reader.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace overlay_trans {

namespace {

using Microsoft::WRL::ComPtr;

constexpr DWORD AUDIO_STREAM = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
constexpr DWORD ALL_STREAMS = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
constexpr UINT32 BITS_PER_SAMPLE = 32;

std::string describe(const char* step, HRESULT result) {
    char message[96];
    std::snprintf(message, sizeof(message), "%s failed (0x%08lX)", step, static_cast<unsigned long>(result));
    return message;
}

}  // namespace

struct AudioFileReader::Impl {
    ComPtr<IMFSourceReader> reader;
    std::vector<float> decoded;  // 디코딩했지만 아직 넘기지 않은 샘플
    size_t decoded_offset = 0;
    uint32_t channels = 0;
    bool media_foundation_started = false;
    bool end_of_stream = false;
    std::string last_error;

    bool fail(const char* step, HRESULT result) {
        last_error = describe(step, result);
        return false;
    }

    // 다음 디코딩 블록을 decoded에 채운다. 더 읽을 것이 없으면 false를 반환한다.
    bool decode_next_block();
};

bool AudioFileReader::Impl::decode_next_block() {
    decoded.clear();
    decoded_offset = 0;

    while (!end_of_stream && decoded.empty()) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample))) {
            end_of_stream = true;
            break;
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            end_of_stream = true;
        }
        if (sample == nullptr) {
            continue;
        }

        ComPtr<IMFMediaBuffer> buffer;
        BYTE* data = nullptr;
        DWORD byte_count = 0;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || FAILED(buffer->Lock(&data, nullptr, &byte_count))) {
            end_of_stream = true;
            break;
        }
        const float* samples = reinterpret_cast<const float*>(data);
        decoded.assign(samples, samples + byte_count / sizeof(float));
        buffer->Unlock();
    }

    return !decoded.empty();
}

AudioFileReader::AudioFileReader() : impl_(std::make_unique<Impl>()) {}

AudioFileReader::~AudioFileReader() {
    impl_->reader.Reset();
    if (impl_->media_foundation_started) {
        MFShutdown();
    }
}

bool AudioFileReader::open(const std::filesystem::path& path, uint32_t sample_rate, uint32_t channels) {
    if (!impl_->media_foundation_started) {
        // 이미 다른 모드로 초기화된 스레드여도 Media Foundation은 동작하므로 결과는 확인하지 않는다.
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const HRESULT started = MFStartup(MF_VERSION);
        if (FAILED(started)) {
            return impl_->fail("MFStartup", started);
        }
        impl_->media_foundation_started = true;
    }

    impl_->reader.Reset();
    impl_->decoded.clear();
    impl_->decoded_offset = 0;
    impl_->end_of_stream = false;
    impl_->channels = channels;

    HRESULT result = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &impl_->reader);
    if (FAILED(result)) {
        return impl_->fail("Opening the file", result);
    }

    // 영상 파일이어도 오디오 트랙만 읽는다.
    impl_->reader->SetStreamSelection(ALL_STREAMS, FALSE);
    result = impl_->reader->SetStreamSelection(AUDIO_STREAM, TRUE);
    if (FAILED(result)) {
        return impl_->fail("Selecting the audio stream", result);
    }

    // 파일 포맷과 다르면 Media Foundation이 디코딩하면서 채널/샘플레이트를 변환해 전달한다.
    const UINT32 block_align = channels * BITS_PER_SAMPLE / 8;
    ComPtr<IMFMediaType> output_type;
    result = MFCreateMediaType(&output_type);
    if (FAILED(result)) {
        return impl_->fail("MFCreateMediaType", result);
    }
    output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    output_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    output_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    output_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
    output_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, BITS_PER_SAMPLE);
    output_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, block_align);
    output_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sample_rate * block_align);

    result = impl_->reader->SetCurrentMediaType(AUDIO_STREAM, nullptr, output_type.Get());
    if (FAILED(result)) {
        return impl_->fail("Setting the output format", result);
    }
    return true;
}

size_t AudioFileReader::read(float* frames, size_t frame_count) {
    const size_t wanted = frame_count * impl_->channels;
    size_t copied = 0;

    while (copied < wanted) {
        if (impl_->decoded_offset == impl_->decoded.size() && !impl_->decode_next_block()) {
            break;
        }
        const size_t count = std::min(wanted - copied, impl_->decoded.size() - impl_->decoded_offset);
        std::copy_n(impl_->decoded.data() + impl_->decoded_offset, count, frames + copied);
        impl_->decoded_offset += count;
        copied += count;
    }

    return copied / impl_->channels;
}

const std::string& AudioFileReader::last_error() const {
    return impl_->last_error;
}

}  // namespace overlay_trans
