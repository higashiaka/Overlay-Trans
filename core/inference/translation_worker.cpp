#include "translation_worker.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace overlay_trans {

namespace {
constexpr size_t MAX_PENDING_RESULTS = 16;
constexpr size_t MAX_BATCH_SEGMENTS = 3;
constexpr size_t MAX_BATCH_TEXT_BYTES = 1200;

struct PendingSpeech {
    std::vector<float> samples;
    RecognizedSpeech speech;
};
}

struct TranslationWorker::Impl {
    ResultCallback on_result;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<PendingSpeech> pending;
    bool active = false;
    bool stopping = false;
    bool batching = false;
    std::thread thread;

    explicit Impl(ResultCallback callback) : on_result(std::move(callback)) {}

    void run() {
        std::unique_lock lock(mutex);
        while (true) {
            changed.wait(lock, [&] { return stopping || !pending.empty(); });
            if (stopping) return;
            auto next = std::move(pending.front());
            pending.pop_front();
            // 추가 발화를 기다리지 않는다. 앞 번역을 기다리며 이미 쌓인 결과만 묶는다.
            while (batching && !pending.empty() && next.speech.batched_segments < MAX_BATCH_SEGMENTS &&
                   next.speech.text.size() + pending.front().speech.text.size() + 1 <= MAX_BATCH_TEXT_BYTES) {
                auto& following = pending.front();
                if (!following.speech.text.empty()) {
                    if (!next.speech.text.empty()) next.speech.text += '\n';
                    next.speech.text += following.speech.text;
                }
                next.samples.insert(next.samples.end(), following.samples.begin(), following.samples.end());
                next.speech.recognition_time += following.speech.recognition_time;
                next.speech.batched_segments += following.speech.batched_segments;
                pending.pop_front();
            }
            next.speech.samples = next.samples;
            active = true;
            changed.notify_all();
            lock.unlock();
            on_result(next.speech);
            lock.lock();
            active = false;
            changed.notify_all();
        }
    }
};

TranslationWorker::TranslationWorker(ResultCallback callback) : impl_(std::make_unique<Impl>(std::move(callback))) {
    impl_->thread = std::thread([this] { impl_->run(); });
}

TranslationWorker::~TranslationWorker() {
    stop();
    if (impl_->thread.joinable()) impl_->thread.join();
}

void TranslationWorker::set_batching(bool enabled) {
    std::lock_guard lock(impl_->mutex);
    impl_->batching = enabled;
}

void TranslationWorker::submit(const RecognizedSpeech& speech) {
    std::unique_lock lock(impl_->mutex);
    impl_->changed.wait(lock, [&] { return impl_->stopping || impl_->pending.size() < MAX_PENDING_RESULTS; });
    if (impl_->stopping) return;
    PendingSpeech next{{speech.samples.begin(), speech.samples.end()}, speech};
    next.speech.samples = {};
    impl_->pending.push_back(std::move(next));
    impl_->changed.notify_all();
}

void TranslationWorker::wait_until_idle() {
    std::unique_lock lock(impl_->mutex);
    impl_->changed.wait(lock, [&] { return impl_->pending.empty() && !impl_->active; });
}

void TranslationWorker::stop() {
    std::lock_guard lock(impl_->mutex);
    impl_->stopping = true;
    impl_->pending.clear();
    impl_->changed.notify_all();
}

}  // namespace overlay_trans
