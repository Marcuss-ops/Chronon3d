#pragma once

#include <atomic>
#include <functional>
#include <vector>

namespace chronon3d {

/// Lightweight cancellation token for graceful shutdown.
/// - Check is_cancelled() at safe points (per-frame, between chunks).
/// - Signal handler calls cancel() to request stop.
/// - OnInterrupted callback runs after cancel for resource cleanup.
class CancellationToken {
public:
    void cancel() noexcept {
        m_cancelled.store(true, std::memory_order_seq_cst);
        // Fire cleanup callbacks
        for (auto& cb : m_on_cancel) {
            if (cb) cb();
        }
    }

    [[nodiscard]] bool is_cancelled() const noexcept {
        return m_cancelled.load(std::memory_order_acquire);
    }

    /// Register a cleanup callback (e.g. close pipe, flush telemetry).
    /// Must be called before rendering starts — not thread-safe after.
    void on_cancel(std::function<void()> cb) {
        m_on_cancel.push_back(std::move(cb));
    }

    void reset() noexcept {
        m_cancelled.store(false, std::memory_order_relaxed);
        m_on_cancel.clear();
    }

private:
    std::atomic<bool> m_cancelled{false};
    std::vector<std::function<void()>> m_on_cancel;
};

/// Install global SIGINT/SIGTERM handler that cancels the given token.
void install_signal_cancellation(CancellationToken& token);

/// Restore default signal handlers.
void restore_default_signal_handlers();

} // namespace chronon3d
