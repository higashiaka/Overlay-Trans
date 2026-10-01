#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace overlay_trans {

struct SttConfig {
    // "ja", "ko", "en" 같은 언어 코드. "auto"면 발화마다 언어를 감지한다.
    std::string language = "auto";
};

// whisper.cpp로 발화 구간을 텍스트로 변환한다.
class SpeechRecognizer {
public:
    SpeechRecognizer();
    ~SpeechRecognizer();

    SpeechRecognizer(const SpeechRecognizer&) = delete;
    SpeechRecognizer& operator=(const SpeechRecognizer&) = delete;

    bool init(const std::filesystem::path& model_path, const SttConfig& config);

    // 16kHz 모노 샘플을 UTF-8 텍스트로 변환한다. 실패하면 빈 문자열을 반환한다.
    std::string transcribe(std::span<const float> samples);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
