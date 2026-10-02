#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
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

    // 방송 제목이나 게임 이름처럼 번역에 참고할 정보를 지시문에 넣는다. (UTF-8, 비우면 제거)
    // translate와 같은 스레드에서 호출해야 한다.
    void set_stream_info(const std::string& info);

    // UTF-8 문장을 번역해 UTF-8로 반환한다. 실패하면 빈 문자열을 반환한다.
    // chat에는 이 발화 직전에 올라온 채팅을 "이름: 내용" 형식으로 넘긴다. 번역의 맥락으로만 쓰인다.
    std::string translate(std::string_view text, std::span<const std::string> chat = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
