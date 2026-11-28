#pragma once

#include <chrono>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <string>
#include <vector>
#include <utility>

namespace overlay_trans {

// HTTP producers and the VAD consumer share a bounded queue, never the SPSC loopback ring.
// A lease prevents a closed/crashed browser from permanently disabling system capture.
class BrowserAudioInput {
public:
    struct Chunk {
        std::vector<float> samples;
        std::string session;
        int64_t ended_at_ms = 0;
    };
    void set_ready(bool ready) {
        std::lock_guard lock(mutex_);
        ready_ = ready;
        if (!ready) clear();
    }
    bool start(const std::string& session) {
        std::lock_guard lock(mutex_);
        expire();
        if (!ready_ || !session_.empty() || session.empty() || session.size() > 64) return false;
        session_ = session;
        active_.store(true, std::memory_order_release);
        renewed_ = Clock::now();
        return true;
    }
    bool push(const std::string& session, int64_t sequence, int64_t ended_at_ms, std::span<const float> samples) {
        std::lock_guard lock(mutex_);
        expire();
        if (session_.empty() || session != session_ || sequence != next_sequence_ || samples.empty() ||
            samples.size() > 4096 || queued_samples_ + samples.size() > 32000 ||
            (last_ended_at_ms_ && ended_at_ms <= last_ended_at_ms_)) return false;
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (ended_at_ms > now_ms + 1000 || ended_at_ms < now_ms - 5000) return false;
        for (float sample : samples) if (!std::isfinite(sample) || std::abs(sample) > 1.01f) return false;
        queue_.push_back({{samples.begin(), samples.end()}, session, ended_at_ms});
        queued_samples_ += samples.size();
        ++next_sequence_;
        last_ended_at_ms_ = ended_at_ms;
        renewed_ = Clock::now();
        return true;
    }
    void stop(const std::string& session) {
        std::lock_guard lock(mutex_);
        if (session == session_) clear();
    }
    std::string session() {
        std::lock_guard lock(mutex_);
        expire();
        return session_;
    }
    // The WASAPI real-time callback must never acquire the HTTP queue mutex.
    bool active() const { return active_.load(std::memory_order_acquire); }
    bool pop(Chunk& chunk) {
        std::lock_guard lock(mutex_);
        expire();
        if (queue_.empty()) return false;
        chunk = std::move(queue_.front());
        queue_.pop_front();
        queued_samples_ -= chunk.samples.size();
        return true;
    }
private:
    using Clock = std::chrono::steady_clock;
    std::mutex mutex_;
    bool ready_ = false;
    std::atomic<bool> active_{false};
    std::string session_;
    Clock::time_point renewed_;
    std::deque<Chunk> queue_;
    size_t queued_samples_ = 0;
    int64_t next_sequence_ = 0;
    int64_t last_ended_at_ms_ = 0;
    void clear() {
        active_.store(false, std::memory_order_release);
        session_.clear();
        queue_.clear();
        queued_samples_ = 0;
        next_sequence_ = 0;
        last_ended_at_ms_ = 0;
    }
    void expire() {
        if (!session_.empty() && Clock::now() - renewed_ > std::chrono::seconds(5)) clear();
    }
};

}  // namespace overlay_trans
