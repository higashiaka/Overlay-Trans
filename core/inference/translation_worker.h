#pragma once

#include "inference/recognition_worker.h"

#include <functional>
#include <memory>

namespace overlay_trans {

// 느린 외부 번역이 다음 발화의 음성 인식을 막지 않도록 인식 결과를 따로 처리한다.
class TranslationWorker {
public:
    using ResultCallback = std::function<void(const RecognizedSpeech&)>;
    explicit TranslationWorker(ResultCallback on_result);
    ~TranslationWorker();
    void set_batching(bool enabled);
    void submit(const RecognizedSpeech& speech);
    void wait_until_idle();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
