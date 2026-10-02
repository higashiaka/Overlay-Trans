#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace overlay_trans {

struct TranslatorConfig {
    std::string target_language = "Korean";
    // GPU에 올릴 층 수. -1이면 전부, 0이면 CPU만 쓴다. VRAM이 모자라면 일부만 올리는 편이 빠르다.
    // GPU 백엔드(Vulkan)를 포함해 빌드한 경우에만 효과가 있다.
    int gpu_layers = -1;
    int gpu_device = 0;
    uint32_t context_size = 2048;
    int max_output_tokens = 128;
    // 모델의 메모리가 차서 다시 시작할 때 맥락으로 남겨 둘 이전 문장 수.
    size_t context_lines = 6;
    // 고정해서 쓸 번역 표기. 한 줄에 하나씩 "원문 = 번역" 형식으로 적는다. (UTF-8)
    std::string glossary;
};

// llama.cpp로 인식된 문장을 번역한다. 앞서 번역한 문장들을 맥락으로 함께 사용한다.
class Translator {
public:
    Translator();
    ~Translator();

    Translator(const Translator&) = delete;
    Translator& operator=(const Translator&) = delete;

    bool init(const std::filesystem::path& model_path, const TranslatorConfig& config);

    // UTF-8 문장을 번역해 UTF-8로 반환한다. 실패하면 빈 문자열을 반환한다.
    std::string translate(std::string_view text);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
