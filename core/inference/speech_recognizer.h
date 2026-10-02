#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>
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
    // whisper는 발화가 짧아도 항상 30초 분량(1500칸, 한 칸에 20ms)을 계산한다. 이 값만큼만 계산하게 줄이면
    // 인식이 그만큼 빨라진다. 발화가 이보다 길면 그 발화에 한해 필요한 만큼 늘린다. 0이면 줄이지 않는다.
    int audio_context = 0;
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
    // stop으로 중단을 요청하면 하던 계산 단계까지만 마치고 빈 문자열을 반환한다.
    std::string transcribe(std::span<const float> samples, std::stop_token stop = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
