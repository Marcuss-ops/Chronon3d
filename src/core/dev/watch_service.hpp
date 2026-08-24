// ---------------------------------------------------------------------------
// src/core/dev/watch_service.hpp — efsw-based file-system watcher
//
// Development-only infrastructure.  The watch service monitors a directory
// tree for file changes and invokes a callback after a debounce window.
//
// Rules (from AGENTS.md):
//   • Development mode ONLY — never wired into the production daemon.
//   • Uses only existing authority interfaces (RenderEngine::clear_caches,
//     command_render, etc.) — NO parallel caches.
//   • Debounce: waits for `debounce_ms` of quiescence before firing.
//
// Dependencies: efsw (vcpkg port)
// Gate: CHRONON3D_ENABLE_DEV_WATCH=ON
// ---------------------------------------------------------------------------
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::dev {

/// Callback signature: invoked after a file change is detected and the
/// debounce window has elapsed.  Receives the list of changed paths.
using WatchCallback = std::function<void(const std::vector<std::string>&)>;

/// Cross-platform file-system watcher for development tooling.
///
/// Usage:
///   WatchService watcher;
///   watcher.on_change([](auto& paths) {
///       // invalidate caches, recompile, re-render
///   });
///   watcher.add("/path/to/project/", /*recursive=*/true);
///   watcher.start();  // blocks until stop() is called from another thread
///
/// Thread-safe: callbacks are serialised on the watcher's internal thread.
/// start() blocks the calling thread.
class WatchService {
public:
    WatchService();
    ~WatchService();

    // Non-copyable, non-movable (owns efsw::FileWatcher).
    WatchService(const WatchService&) = delete;
    WatchService& operator=(const WatchService&) = delete;
    WatchService(WatchService&&) = delete;
    WatchService& operator=(WatchService&&) = delete;

    /// Add a directory to watch.  Must be called before start().
    /// `directory` must be an absolute or relative path.
    /// `recursive` controls whether subdirectories are watched.
    void add(std::string_view directory, bool recursive = true);

    /// Set the debounce window.  Default: 500ms.
    /// After a file change, the watcher waits this long for further changes
    /// before invoking the callback.
    void set_debounce(std::chrono::milliseconds ms) noexcept;

    /// Register the change callback.  Must be set before start().
    void on_change(WatchCallback callback);

    /// Start watching.  Blocks the calling thread until stop() is called
    /// (from a signal handler or another thread).  Must be called after
    /// add() and on_change().
    void start();

    /// Signal the watcher to stop.  Can be called from any thread.
    /// Causes start() to return.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace chronon3d::dev