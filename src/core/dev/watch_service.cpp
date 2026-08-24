// ---------------------------------------------------------------------------
// src/core/dev/watch_service.cpp — efsw integration
//
// Wraps efsw's FileWatcher + FileWatchListener in a simple debounced callback
// service for the CLI dev --watch mode.
//
// The debounce algorithm:
//   Each file event resets a timer.  When the timer expires (no events for
//   `debounce_ms`), the accumulated set of changed paths is flushed to the
//   callback.  This avoids re-rendering on every individual write during a
//   multi-file save operation.
//
// Thread model:
//   efsw runs its own internal watch thread.  Our listener's handleFileAction
//   is called from that thread.  We accumulate paths and kick a debounce
//   timer on a separate thread.  start() blocks on a condition variable until
//   stop() is signalled.
// ---------------------------------------------------------------------------

#include "watch_service.hpp"

#include <efsw/efsw.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <string>

namespace chronon3d::dev {
namespace {

// ── Listener (efsw callback → internal debounce queue) ──────────────────

class WatchListener : public efsw::FileWatchListener {
public:
    void handleFileAction(
        efsw::WatchID /*watchid*/,
        const std::string& dir,
        const std::string& filename,
        efsw::Action action,
        std::string /*oldFilename*/) override {

        // Ignore directories and ephemeral files.
        if (filename.empty()) return;
        if (action == efsw::Actions::Delete) {
            // Deletes still count as a change for cache invalidation.
        }

        std::lock_guard lock(m_mutex);
        m_changed_paths.insert(dir + filename);
        m_has_events.store(true, std::memory_order_release);
        m_cv.notify_one();
    }

    // Resets the "pending event" flag but KEEPS the accumulated paths, so a
    // debounce window can be measured from this point onward.
    void clear_pending() {
        std::lock_guard lock(m_mutex);
        m_has_events.store(false, std::memory_order_release);
    }

    // Returns all accumulated paths and resets the pending-event flag.
    std::vector<std::string> drain() {
        std::lock_guard lock(m_mutex);
        std::vector<std::string> result(m_changed_paths.begin(),
                                         m_changed_paths.end());
        m_changed_paths.clear();
        m_has_events.store(false, std::memory_order_release);
        return result;
    }

    // Wait for a pending event or timeout.
    template <typename Duration>
    bool wait_for(Duration d) {
        std::unique_lock lock(m_mutex);
        return m_cv.wait_for(lock, d, [this] {
            return m_has_events.load(std::memory_order_acquire);
        });
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::set<std::string> m_changed_paths;
    std::atomic<bool> m_has_events{false};
};

}  // anonymous namespace

// ── WatchService::Impl ────────────────────────────────────────────────────

struct WatchService::Impl {
    efsw::FileWatcher watcher;
    WatchListener listener;
    WatchCallback callback;
    std::chrono::milliseconds debounce{500};

    // Atomic so stop() is safe to call from a signal handler (async-signal-safe
    // store on mainstream platforms) and from any thread.
    std::atomic<bool> stop_requested{false};
};

// ── Public API ────────────────────────────────────────────────────────────

WatchService::WatchService() : m_impl(std::make_unique<Impl>()) {}

WatchService::~WatchService() {
    stop();
}

void WatchService::add(std::string_view directory, bool recursive) {
    std::string dir(directory);
    // efsw requires trailing slash for directory watches.
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        dir += '/';
    }
    m_impl->watcher.addWatch(dir, &m_impl->listener, recursive);
}

void WatchService::set_debounce(std::chrono::milliseconds ms) noexcept {
    m_impl->debounce = ms;
}

void WatchService::on_change(WatchCallback callback) {
    m_impl->callback = std::move(callback);
}

void WatchService::start() {
    if (!m_impl->callback) return;

    m_impl->stop_requested.store(false, std::memory_order_release);

    // Kick off efsw's internal watch thread.
    m_impl->watcher.watch();

    // Debounce loop on the calling thread.
    auto& L = m_impl->listener;
    const auto debounce = m_impl->debounce;

    while (!m_impl->stop_requested.load(std::memory_order_acquire)) {
        // Wait for the first event (or stop).
        bool got_event = L.wait_for(std::chrono::milliseconds(250));
        if (m_impl->stop_requested.load(std::memory_order_acquire)) break;
        if (!got_event) continue;

        // Debounce: wait for a quiet period of `debounce` ms.  Each new event
        // resets the quiet timer by clearing the pending flag and waiting
        // again.
        L.clear_pending();
        while (L.wait_for(debounce)) {
            if (m_impl->stop_requested.load(std::memory_order_acquire)) break;
            L.clear_pending();
        }
        if (m_impl->stop_requested.load(std::memory_order_acquire)) break;

        // Drain accumulated paths and fire callback.
        auto paths = L.drain();
        if (!paths.empty() && m_impl->callback) {
            m_impl->callback(paths);
        }
    }

}

void WatchService::stop() {
    // Signal-safe: only an atomic store.  The start() loop observes the flag
    // within its 250 ms poll window and returns; the efsw thread is then
    // stopped when Impl (and its efsw::FileWatcher) is destroyed.
    m_impl->stop_requested.store(true, std::memory_order_release);
}

}  // namespace chronon3d::dev