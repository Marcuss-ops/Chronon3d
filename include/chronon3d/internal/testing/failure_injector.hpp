#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace chronon3d::testing {

enum class FailurePoint : std::size_t {
    SocketWrite = 0,
    SocketRead,
    VulkanBufferAllocation,
    VulkanImageAllocation,
    Count,
};

/// Deterministic, process-local failure registry used by fault-injection tests.
/// Production behaviour is unchanged until a test explicitly arms a point.
class FailureInjector final {
public:
    static void fail_next(FailurePoint point) noexcept {
        counters()[index(point)].store(1, std::memory_order_release);
    }

    static void fail_after(FailurePoint point, std::size_t successful_calls) noexcept {
        counters()[index(point)].store(-static_cast<long long>(successful_calls + 1),
                                        std::memory_order_release);
    }

    static bool should_fail(FailurePoint point) noexcept {
        auto& counter = counters()[index(point)];
        long long value = counter.load(std::memory_order_acquire);
        for (;;) {
            if (value == 0) return false;
            if (value == 1) {
                if (counter.compare_exchange_weak(value, 0, std::memory_order_acq_rel)) return true;
                continue;
            }
            // Negative values count successful calls remaining before failure.
            if (value == -1) {
                if (counter.compare_exchange_weak(value, 0, std::memory_order_acq_rel)) return true;
                continue;
            }
            if (counter.compare_exchange_weak(value, value + 1, std::memory_order_acq_rel)) return false;
        }
    }

    static void reset() noexcept {
        for (auto& counter : counters()) counter.store(0, std::memory_order_release);
    }

private:
    static constexpr std::size_t index(FailurePoint point) noexcept {
        return static_cast<std::size_t>(point);
    }
    static std::array<std::atomic<long long>, static_cast<std::size_t>(FailurePoint::Count)>& counters() noexcept {
        static std::array<std::atomic<long long>, static_cast<std::size_t>(FailurePoint::Count)> values{};
        return values;
    }
};

} // namespace chronon3d::testing
