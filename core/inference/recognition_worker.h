#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace overlay_trans {

class SpeechRecognizer;
struct SpeechSegment;

// 발화 구간 하나를 인식한 결과.
struct RecognizedSpeech {
    std::span<const float> samples;
    std::string text;  // 인식에 실패했으면 비어 있다.
    std::chrono::steady_clock::time_point speech_ended_at;  // 구간의 마지막 샘플이 들어온 시각
    std::chrono::steady_clock::duration recognition_time;
    bool started_early = false;  // 발화가 끝났다고 확정되기 전에 인식을 시작했는지
    uint32_t silence_before_ms = 0;  // 직전 구간과의 사이에 있던 무음의 길이 (SpeechSegment 참고)
};

// 발화 구간을 별도 스레드에서 차례대로 인식한다.
//
// 발화가 끝났는지는 무음이 한동안 이어진 뒤에야 확정되는데, 그때까지 기다렸다가 인식을 시작하면 그만큼 자막이 늦어진다.
// 그래서 말이 잠깐 멈춘 시점에 인식을 미리 시작해 두고(begin_early), 발화가 그대로 끝나면 그 결과를 쓰고(submit),
// 말이 다시 이어지면 버린다(cancel_early).
class RecognitionWorker {
public:
    // 작업 스레드에서 호출된다. 반환할 때까지 다음 구간의 결과는 나오지 않는다.
    using ResultCallback = std::function<void(const RecognizedSpeech& speech)>;

    RecognitionWorker(SpeechRecognizer& recognizer, ResultCallback on_result);
    ~RecognitionWorker();

    RecognitionWorker(const RecognitionWorker&) = delete;
    RecognitionWorker& operator=(const RecognitionWorker&) = delete;

    // 아래 세 함수는 한 스레드에서만 호출해야 한다.

    // 끝났을 가능성이 높은 구간의 인식을 미리 시작한다. 밀린 구간이 많으면 아무것도 하지 않는다.
    void begin_early(std::span<const float> samples);

    // 미리 시작한 인식을 버린다.
    void cancel_early();

    // 끝난 것이 확정된 구간을 넘긴다. 같은 구간의 인식을 미리 시작해 두었으면 그 결과를 쓴다.
    // 밀린 구간이 많으면 자리가 날 때까지 기다린다.
    void submit(const SpeechSegment& segment);

    // 넘긴 구간이 모두 처리될 때까지 기다린다.
    void wait_until_idle();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace overlay_trans
