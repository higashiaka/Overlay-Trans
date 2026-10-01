#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace overlay_trans {

struct TranslatorConfig {
    std::string target_language = "Korean";
    // GPU 백엔드(Vulkan)를 포함해 빌드한 경우에만 효과가 있다.
    bool use_gpu = true;
    int gpu_device = 0;
    uint32_t context_size = 2048;
    int max_output_tokens = 128;
};

// llama.cpp로 인식된 문장을 번역한다.
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
