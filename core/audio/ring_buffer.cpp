#include "ring_buffer.h"

#include <algorithm>
#include <bit>

namespace overlay_trans {

RingBuffer::RingBuffer(size_t capacity) : buffer_(std::bit_ceil(capacity)), mask_(buffer_.size() - 1) {}

bool RingBuffer::write(const float* samples, size_t count) {
    // 위치 값은 계속 증가시키고 접근할 때만 mask를 적용한다.
    const size_t write_pos = write_pos_.load(std::memory_order_relaxed);
    const size_t read_pos = read_pos_.load(std::memory_order_acquire);
    const size_t free_space = buffer_.size() - (write_pos - read_pos);
    if (count > free_space) {
        dropped_samples_.fetch_add(count, std::memory_order_relaxed);
        return false;
    }

    const size_t offset = write_pos & mask_;
    const size_t first = std::min(count, buffer_.size() - offset);
    std::copy_n(samples, first, buffer_.data() + offset);
    std::copy_n(samples + first, count - first, buffer_.data());

    write_pos_.store(write_pos + count, std::memory_order_release);
    return true;
}

size_t RingBuffer::read(float* out, size_t count) {
    const size_t read_pos = read_pos_.load(std::memory_order_relaxed);
    const size_t write_pos = write_pos_.load(std::memory_order_acquire);
    count = std::min(count, write_pos - read_pos);

    const size_t offset = read_pos & mask_;
    const size_t first = std::min(count, buffer_.size() - offset);
    std::copy_n(buffer_.data() + offset, first, out);
    std::copy_n(buffer_.data(), count - first, out + first);

    read_pos_.store(read_pos + count, std::memory_order_release);
    return count;
}

size_t RingBuffer::available() const {
    return write_pos_.load(std::memory_order_acquire) - read_pos_.load(std::memory_order_acquire);
}

uint64_t RingBuffer::dropped_samples() const {
    return dropped_samples_.load(std::memory_order_relaxed);
}

}  // namespace overlay_trans
