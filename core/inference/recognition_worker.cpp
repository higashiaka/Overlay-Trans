#include "recognition_worker.h"

#include "audio/vad_segmenter.h"
#include "speech_recognizer.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace overlay_trans {

namespace {

using Clock = std::chrono::steady_clock;

// 인식을 기다리는 구간이 이보다 많이 쌓이면 submit이 기다린다.
constexpr size_t MAX_QUEUED_JOBS = 4;

enum class JobState {
    Early,      // 발화가 끝났는지 아직 모르는 채로 미리 시작한 구간
    Confirmed,  // 끝난 것이 확정된 구간
    Cancelled,  // 말이 다시 이어져 버리는 구간
};

struct Job {
    std::vector<float> samples;
    JobState state;
    std::stop_source stop;  // 버리는 구간의 인식을 도중에 멈춘다.
    Clock::time_point speech_ended_at;
    uint32_t silence_before_ms = 0;
};

}  // namespace

struct RecognitionWorker::Impl {
    SpeechRecognizer& recognizer;
    ResultCallback on_result;

    std::mutex mutex;
    std::condition_variable_any changed;
    // 맨 앞이 작업 스레드가 처리 중인 구간이다. Early 상태인 구간은 많아야 하나이고 항상 맨 뒤에 있다.
    std::deque<Job> jobs;

    // 다른 멤버가 모두 준비된 뒤에 시작하고 가장 먼저 끝나도록 마지막에 둔다.
    std::jthread thread;

    Impl(SpeechRecognizer& recognizer, ResultCallback on_result)
        : recognizer(recognizer), on_result(std::move(on_result)) {}

    void run(std::stop_token stop);
    void cancel_early();
};

void RecognitionWorker::Impl::run(std::stop_token stop) {
    std::unique_lock lock(mutex);
    while (true) {
        changed.wait(lock, stop, [&] { return !jobs.empty(); });
        if (stop.stop_requested()) {
            return;
        }

        // 맨 앞의 구간은 이 스레드만 꺼내므로, 잠금을 푼 동안에도 참조가 유효하다.
        Job& job = jobs.front();
        if (job.state != JobState::Cancelled) {
            const bool started_early = job.state == JobState::Early;
            lock.unlock();
            const auto started_at = Clock::now();
            std::string text = recognizer.transcribe(job.samples, job.stop.get_token());
            const auto recognition_time = Clock::now() - started_at;
            lock.lock();

            // 미리 시작한 구간이면 발화가 끝났는지 확정될 때까지 결과를 들고 기다린다.
            changed.wait(lock, stop, [&] { return job.state != JobState::Early; });
            if (stop.stop_requested()) {
                return;
            }

            if (job.state == JobState::Confirmed) {
                lock.unlock();
                on_result({job.samples, std::move(text), job.speech_ended_at, recognition_time, started_early,
                           job.silence_before_ms});
                lock.lock();
            }
        }

        jobs.pop_front();
        changed.notify_all();
    }
}

void RecognitionWorker::Impl::cancel_early() {
    if (!jobs.empty() && jobs.back().state == JobState::Early) {
        jobs.back().state = JobState::Cancelled;
        jobs.back().stop.request_stop();
        changed.notify_all();
    }
}

RecognitionWorker::RecognitionWorker(SpeechRecognizer& recognizer, ResultCallback on_result)
    : impl_(std::make_unique<Impl>(recognizer, std::move(on_result))) {
    impl_->thread = std::jthread([this](std::stop_token stop) { impl_->run(std::move(stop)); });
}

RecognitionWorker::~RecognitionWorker() {
    // 하던 인식을 끝까지 기다리지 않고 멈춘다.
    std::lock_guard lock(impl_->mutex);
    impl_->thread.request_stop();
    for (Job& job : impl_->jobs) {
        job.stop.request_stop();
    }
}

void RecognitionWorker::begin_early(std::span<const float> samples) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->jobs.size() >= MAX_QUEUED_JOBS) {
        return;
    }
    impl_->jobs.push_back({.samples = {samples.begin(), samples.end()}, .state = JobState::Early});
    impl_->changed.notify_all();
}

void RecognitionWorker::cancel_early() {
    std::lock_guard lock(impl_->mutex);
    impl_->cancel_early();
}

void RecognitionWorker::submit(const SpeechSegment& segment) {
    const std::span<const float> samples = segment.samples;
    const auto speech_ended_at = Clock::now() - std::chrono::milliseconds(segment.waited_ms);

    std::unique_lock lock(impl_->mutex);
    if (!impl_->jobs.empty() && impl_->jobs.back().state == JobState::Early) {
        Job& early = impl_->jobs.back();
        if (early.samples.size() == samples.size()) {
            early.speech_ended_at = speech_ended_at;
            early.silence_before_ms = segment.silence_before_ms;
            early.state = JobState::Confirmed;
            impl_->changed.notify_all();
            return;
        }
        // 미리 시작한 구간과 다른 구간으로 끝났다.
        impl_->cancel_early();
    }

    impl_->changed.wait(lock, [&] { return impl_->jobs.size() < MAX_QUEUED_JOBS; });
    impl_->jobs.push_back({.samples = {samples.begin(), samples.end()},
                           .state = JobState::Confirmed,
                           .speech_ended_at = speech_ended_at,
                           .silence_before_ms = segment.silence_before_ms});
    impl_->changed.notify_all();
}

void RecognitionWorker::wait_until_idle() {
    std::unique_lock lock(impl_->mutex);
    impl_->cancel_early();
    impl_->changed.wait(lock, [&] { return impl_->jobs.empty(); });
}

}  // namespace overlay_trans
