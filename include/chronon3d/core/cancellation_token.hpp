#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <cstddef>
#include <vector>

namespace chronon3d {

/// Lightweight cancellation token for graceful shutdown.
/// - Check is_cancelled() at safe points (per-frame, between chunks).
/// - Signal handler calls cancel() to request stop.
/// - OnInterrupted callback runs after cancel for resource cleanup.
class CancellationToken {
public:
    using CallbackId = std::size_t;

    void cancel() noexcept {
        m_cancelled.store(true, std::memory_order_seq_cst);
        std::vector<std::function<void()>> callbacks;
        {
            std::lock_guard<std::mutex> lock(m_callbacks_mutex);
            callbacks.reserve(m_on_cancel.size());
            for (const auto& entry : m_on_cancel) callbacks.push_back(entry.second);
        }
        for (auto& cb : callbacks) {
            if (cb) cb();
        }
    }

    [[nodiscard]] bool is_cancelled() const noexcept {
        return m_cancelled.load(std::memory_order_acquire);
    }

    /// Register a callback and return a handle that can be removed safely.
    /// If cancellation already happened, the callback runs synchronously.
    CallbackId on_cancel(std::function<void()> cb) const {
        if (!cb) return 0;
        if (is_cancelled()) {
            cb();
            return 0;
        }
        std::lock_guard<std::mutex> lock(m_callbacks_mutex);
        if (m_cancelled.load(std::memory_order_acquire)) {
            cb();
            return 0;
        }
        const auto id = m_next_callback_id++;
        m_on_cancel.emplace_back(id, std::move(cb));
        return id;
    }

    void remove_on_cancel(CallbackId id) const noexcept {
        if (id == 0) return;
        std::lock_guard<std::mutex> lock(m_callbacks_mutex);
        for (auto it = m_on_cancel.begin(); it != m_on_cancel.end(); ++it) {
            if (it->first == id) {
                m_on_cancel.erase(it);
                return;
            }
        }
    }

    void reset() noexcept {
        m_cancelled.store(false, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(m_callbacks_mutex);
        m_on_cancel.clear();
    }

private:
    std::atomic<bool> m_cancelled{false};
    mutable std::mutex m_callbacks_mutex;
    mutable CallbackId m_next_callback_id{1};
    mutable std::vector<std::pair<CallbackId, std::function<void()>>> m_on_cancel;
};

/// Install global SIGINT/SIGTERM handler that cancels the given token.
void install_signal_cancellation(CancellationToken& token);

/// Restore default signal handlers.
void restore_default_signal_handlers();

} // namespace chronon3d
