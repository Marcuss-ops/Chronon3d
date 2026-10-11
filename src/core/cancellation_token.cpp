#include <chronon3d/core/cancellation_token.hpp>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#include <spdlog/spdlog.h>

namespace chronon3d {

namespace {

// ── F5.1 async-signal-safe signal plane ──────────────────────────────────────
// Everything the handler touches is async-signal-safe: one atomic store and
// one write() to a pipe. No mutex, no heap, no logging, no callbacks.
//
// g_bridge_token is written by install()/restore() on the normal thread
// BEFORE the handler is armed / AFTER it is disarmed (signal() gives the
// required happens-before for a handler installed afterwards), and is only
// read inside the handler. This replaces the previous unsynchronized global
// raw pointer.

volatile std::sig_atomic_t g_cancel_requested = 0;
CancellationToken* g_bridge_token = nullptr;
int g_selfpipe_write_fd = -1;

int create_self_pipe(int fds[2]) noexcept {
#if defined(_WIN32)
    return ::_pipe(fds, 4096, _O_BINARY);
#else
    return ::pipe(fds);
#endif
}

std::ptrdiff_t read_self_pipe(int fd, char* data, std::size_t size) noexcept {
#if defined(_WIN32)
    return ::_read(fd, data, static_cast<unsigned int>(size));
#else
    return ::read(fd, data, size);
#endif
}

void write_self_pipe(int fd, const char* data, std::size_t size) noexcept {
#if defined(_WIN32)
    (void)::_write(fd, data, static_cast<unsigned int>(size));
#else
    (void)::write(fd, data, size);
#endif
}

void close_self_pipe(int fd) noexcept {
#if defined(_WIN32)
    (void)::_close(fd);
#else
    (void)::close(fd);
#endif
}

extern "C" void signal_handler(int) noexcept {
    g_cancel_requested = 1;  // sig_atomic_t store: async-signal-safe
    if (g_selfpipe_write_fd >= 0) {
        const char byte = 1;
        // A one-byte write wakes the watcher on both POSIX and Windows.
        // EAGAIN (pipe full) is fine: the flag above already latched.
        write_self_pipe(g_selfpipe_write_fd, &byte, 1);
    }
}

} // anonymous namespace

bool SignalCancellationBridge::install(CancellationToken& token) {
    bool expected = false;
    if (!m_installed.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        return false;  // already installed — idempotent, no second watcher
    }

    int fds[2];
    if (create_self_pipe(fds) != 0) {
        m_installed.store(false, std::memory_order_release);
        spdlog::error("[cancellation] SignalCancellationBridge: pipe() failed");
        return false;
    }
    g_selfpipe_write_fd = fds[1];
    g_bridge_token = &token;  // published before handlers are armed

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    int read_fd = fds[0];
    m_watcher = std::thread([this, read_fd, &token]() {
        char drain[64];
        while (!m_stop_requested.load(std::memory_order_acquire)) {
            // Blocking read wakes on either a signal byte or an EOF byte
            // written by restore() — no polling, no races on join.
            const auto n = read_self_pipe(read_fd, drain, sizeof(drain));
            if (n <= 0) {
                if (m_stop_requested.load(std::memory_order_acquire)) break;
                continue;
            }
            if (!m_signal_seen.exchange(true, std::memory_order_acq_rel)) {
                // First observed signal: run the real cancellation on this
                // NORMAL thread. Repeated signals only drain the pipe and
                // leave m_signal_seen set — cancel() runs exactly once.
                // cancel() itself is not noexcept (callback vector copy),
                // so a pathological allocation failure here degrades to a
                // logged error instead of terminating the watcher.
                try {
                    token.cancel();
                } catch (const std::exception& e) {
                    spdlog::error("[cancellation] cancel() failed: {}", e.what());
                } catch (...) {
                    spdlog::error("[cancellation] cancel() failed: unknown");
                }
            }
        }
        close_self_pipe(read_fd);
    });
    return true;
}

bool SignalCancellationBridge::poll() noexcept {
    return m_signal_seen.exchange(false, std::memory_order_acq_rel);
}

void SignalCancellationBridge::restore() noexcept {
    if (!m_installed.exchange(false, std::memory_order_acq_rel)) {
        return;  // never installed / already restored
    }
    m_stop_requested.store(true, std::memory_order_release);
    // Wake the watcher with a sentinel byte (this runs on a normal thread).
    if (g_selfpipe_write_fd >= 0) {
        const char byte = 0;
        write_self_pipe(g_selfpipe_write_fd, &byte, 1);
    }
    if (m_watcher.joinable()) {
        m_watcher.join();
    }
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
    g_bridge_token = nullptr;  // handler is disarmed before the pointer drops
    if (g_selfpipe_write_fd >= 0) {
        close_self_pipe(g_selfpipe_write_fd);
        g_selfpipe_write_fd = -1;
    }
}

SignalCancellationBridge::~SignalCancellationBridge() {
    restore();
}

namespace {

// Process-wide bridge for the free-function API. Leak-parked singleton:
// restore() is the explicit teardown path and must stay callable after
// arbitrary exit orders; a static destructor joining a watcher thread at
// process exit adds a teardown hazard for zero benefit.
SignalCancellationBridge& global_bridge() {
    static SignalCancellationBridge* bridge = new SignalCancellationBridge();
    return *bridge;
}

} // anonymous namespace

void install_signal_cancellation(CancellationToken& token) {
    (void)global_bridge().install(token);
}

void restore_default_signal_handlers() {
    global_bridge().restore();
}

} // namespace chronon3d
