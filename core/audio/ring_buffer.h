#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace overlay_trans {

// 생산자 스레드 하나와 소비자 스레드 하나가 함께 쓰는 lock-free 링 버퍼.
// 오버플로우 정책: 공간이 부족하면 해당 write 호출의 샘플을 통째로 버린다.
// 블록 단위로 버리므로 인터리브된 스테레오 프레임의 채널 정렬이 깨지지 않는다.
class RingBuffer {
public:
    // capacity는 2의 거듭제곱으로 올림된다.
    explicit RingBuffer(size_t capacity);

    // 생산자 스레드에서 호출한다. 공간이 부족해 버렸으면 false를 반환한다.
    bool write(const float* samples, size_t count);

    // 소비자 스레드에서 호출한다. 실제로 읽은 샘플 수를 반환한다.
    size_t read(float* out, size_t count);

    size_t available() const;
    uint64_t dropped_samples() const;

private:
    std::vector<float> buffer_;
    size_t mask_;
    std::atomic<size_t> write_pos_{0};
    std::atomic<size_t> read_pos_{0};
    std::atomic<uint64_t> dropped_samples_{0};
};

}  // namespace overlay_trans
