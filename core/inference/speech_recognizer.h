#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace overlay_trans {

struct SttConfig {
    // "ja", "ko", "en" 같은 언어 코드. "auto"면 발화마다 언어를 감지한다.
    std::string language = "auto";
    // 자주 나오는 이름과 용어를 적어 두면 그 표기로 인식될 확률이 올라간다. (UTF-8, 비워 두면 사용하지 않는다.)
    std::string vocabulary_hint;
    // GPU 백엔드(Vulkan)를 포함해 빌드한 경우에만 효과가 있다.
    bool use_gpu = true;
    // 외장 GPU가 여러 개일 때 몇 번째를 쓸지. 내장 GPU는 세지 않는다.
    int gpu_device = 0;
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
