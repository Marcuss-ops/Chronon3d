#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <cstddef>
#include <thread>
#include <utility>
#include <vector>

namespace chronon3d {

/// Lightweight cancellation token for graceful shutdown.
/// - Check is_cancelled() at safe points (per-frame, between chunks).
/// - Signal handler calls cancel() to request stop.
/// - OnInterrupted callback runs after cancel for resource cleanup.
class CancellationToken {
public:
    using CallbackId = std::size_t;

    CancellationToken() = default;
    CancellationToken(const CancellationToken&) = delete;
    CancellationToken& operator=(const CancellationToken&) = delete;

    // A token is job-owned and is only moved while the owning job is
    // quiescent. Transfer its observable state under the source lock; the
    // mutex itself remains local to the destination object.
    CancellationToken(CancellationToken&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.m_callbacks_mutex);
        m_cancelled.store(other.m_cancelled.load(std::memory_order_acquire),
                          std::memory_order_release);
        m_next_callback_id = other.m_next_callback_id;
        m_on_cancel = std::move(other.m_on_cancel);
        other.m_cancelled.store(false, std::memory_order_release);
        other.m_on_cancel.clear();
    }

    CancellationToken& operator=(CancellationToken&& other) noexcept {
        if (this == &other) return *this;
        std::scoped_lock lock(m_callbacks_mutex, other.m_callbacks_mutex);
        m_cancelled.store(other.m_cancelled.load(std::memory_order_acquire),
                          std::memory_order_release);
        m_next_callback_id = other.m_next_callback_id;
        m_on_cancel = std::move(other.m_on_cancel);
        other.m_cancelled.store(false, std::memory_order_release);
        other.m_on_cancel.clear();
        return *this;
    }

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

/// F5.1 — SignalCancellationBridge: the single authority that turns POSIX
/// termination signals into a CancellationToken::cancel() executed on a
/// NORMAL thread.
///
/// The signal handler itself is async-signal-safe ONLY: it flips one
/// lock-free atomic flag and writes one byte to a self-pipe fd. No mutex,
/// no allocation, no spdlog, no callbacks in signal context — the previous
/// design called CancellationToken::cancel() directly from the handler,
/// which takes a mutex, copies a vector of std::function and runs arbitrary
/// callbacks (all undefined behavior in signal context; worst case deadlock
/// or corruption exactly when teardown must be clean).
///
/// cancel() runs exactly once per install: repeated signals only drain the
/// flag (re-running on_cancel callbacks was possible before and is unsafe).
class SignalCancellationBridge {
public:
    SignalCancellationBridge() = default;
    ~SignalCancellationBridge();  // restore() if still installed
    SignalCancellationBridge(const SignalCancellationBridge&) = delete;
    SignalCancellationBridge& operator=(const SignalCancellationBridge&) = delete;

    /// Install SIGINT/SIGTERM handlers bound to `token` and spawn the watcher
    /// thread that performs the real cancel(). Returns false if already
    /// installed (idempotent, no double watcher).
    bool install(CancellationToken& token);

    /// Non-blocking poll for event-loop callers: true exactly once per
    /// observed signal (the watcher thread already ran cancel()).
    [[nodiscard]] bool poll() noexcept;

    /// Restore default handlers, stop and join the watcher thread. Safe to
    /// call more than once.
    void restore() noexcept;

private:
    std::atomic<bool> m_installed{false};
    std::atomic<bool> m_stop_requested{false};
    std::atomic<bool> m_signal_seen{false};
    std::thread m_watcher;
};

/// Install global SIGINT/SIGTERM handler that cancels the given token
/// (delegates to the process-wide SignalCancellationBridge).
void install_signal_cancellation(CancellationToken& token);

/// Restore default signal handlers and stop the bridge watcher.
void restore_default_signal_handlers();

} // namespace chronon3d
