#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace chronon3d::cache::detail {

inline std::uint64_t pool_lock_wait_us(
    std::chrono::steady_clock::time_point started) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
}

class PoolLockHoldMeter {
public:
    explicit PoolLockHoldMeter(
        std::atomic<std::uint64_t>& sink, bool enabled = true) noexcept
        : sink_(sink), enabled_(enabled) {
        if (enabled_) started_ = std::chrono::steady_clock::now();
    }

    ~PoolLockHoldMeter() {
        if (!enabled_) return;
        sink_.fetch_add(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started_).count()),
            std::memory_order_relaxed);
    }

    PoolLockHoldMeter(const PoolLockHoldMeter&) = delete;
    PoolLockHoldMeter& operator=(const PoolLockHoldMeter&) = delete;

private:
    std::atomic<std::uint64_t>& sink_;
    bool enabled_{true};
    std::chrono::steady_clock::time_point started_{};
};

} // namespace chronon3d::cache::detail
