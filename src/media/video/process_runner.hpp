#pragma once

// ---------------------------------------------------------------------------
// process_runner.hpp — Cross-platform subprocess runner (no shell).
//
// Replaces popen()/pclose() with posix_spawnp (Linux/POSIX).
// argv is passed directly to the child —
// no shell parsing, no injection risk.
//
// Lifecycle:  launch() → write()×N → close_stdin() → wait()
//             launch() → write()×N → terminate_and_wait() → wait()
//
// Timeout & escalation:
//   terminate_and_wait(5s) → SIGTERM → wait 5s → SIGKILL → reap
//   wait_for(2s)           → poll for exit up to 2s
//
// Thread-safety: NOT thread-safe.  Caller must serialise access.
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d::media::video {

class ProcessRunner {
public:
    ProcessRunner() noexcept = default;
    ~ProcessRunner() noexcept;

    ProcessRunner(const ProcessRunner&) = delete;
    ProcessRunner& operator=(const ProcessRunner&) = delete;
    ProcessRunner(ProcessRunner&&) noexcept;
    ProcessRunner& operator=(ProcessRunner&&) noexcept;

    // ── Lifecycle ───────────────────────────────────────────────────────

    /// Launch a child process.
    /// @param executable  Name or path of the executable (resolved via PATH).
    /// @param argv        Full argument vector; argv[0] should match
    ///                    `executable`.  Passed directly to the child.
    /// @return true on success.
    bool launch(const std::string& executable,
                const std::vector<std::string>& argv);

    /// Write bytes to the child's stdin.  Blocks until all bytes written
    /// or the pipe breaks.
    /// @return true if all bytes were written, false on pipe error.
    bool write(const std::uint8_t* data, std::size_t size);

    /// Write bytes to the child's stdin with a per-write deadline.
    /// Uses poll() with POLLOUT so the write is non-blocking; drains stderr
    /// inside the poll loop to prevent the child from blocking on stderr.
    /// @return true if all bytes were written before the deadline,
    ///         false on timeout or pipe error.
    bool write_for(const std::uint8_t* data, std::size_t size,
                   std::chrono::milliseconds timeout);

    /// Close the write end of the stdin pipe.  The child receives EOF on
    /// its next read.  Idempotent.
    void close_stdin();

    /// Wait for the child to exit and collect its exit code.
    /// Blocks until the child process terminates.
    /// Retries on EINTR.
    /// @return Exit code (0–255), or -1 on error.
    int wait();

    /// Wait for the child to exit, with a timeout.
    /// Polls non-blocking waitpid in a loop until the child exits or the
    /// timeout expires.
    /// @return Exit code (0–255), -1 on error, -2 on timeout.
    int wait_for(std::chrono::milliseconds timeout);

    /// Gracefully terminate the child with SIGTERM, wait up to
    /// `graceful_timeout`, then escalate to SIGKILL if still running.
    /// Closes stdin before signalling so the child can drain its input.
    /// Blocks until the child is fully reaped.
    /// @return Exit code (0–255), or -1 on error.
    int terminate_and_wait(std::chrono::milliseconds graceful_timeout);

    /// Send SIGTERM to the child. Does NOT wait — call wait() / wait_for() afterwards.
    void terminate();

    // ── Queries ─────────────────────────────────────────────────────────

    /// True while the child is running (after successful launch, before wait).
    /// Non-blocking.  Caches the exit status if the child has already exited,
    /// so a subsequent wait() will still work correctly.
    [[nodiscard]] bool is_running();

    /// Child PID (Linux/macOS) or process ID (Windows), or -1 if not launched.
    [[nodiscard]] int pid() const noexcept { return child_pid_; }

    // ── Diagnostics ─────────────────────────────────────────────────────

    /// Maximum bytes of stderr to buffer (prevents unbounded memory growth).
    static constexpr std::size_t kMaxStderrBytes = 1024 * 1024;  // 1 MiB

    /// Retrieve and clear the captured stderr output from the child process.
    /// Returns an empty string if no stderr was captured (or if capture was
    /// not enabled / the child did not write to stderr).
    /// Calling this method drains the internal buffer; subsequent calls
    /// return the new stderr data produced after the last call.
    [[nodiscard]] std::string consume_stderr() noexcept;

private:
    int  child_pid_{-1};     // POSIX PID or Windows process ID
    int  stdin_fd_{-1};      // write-end of the pipe to child's stdin
    int  stderr_fd_{-1};     // read-end of the child's stderr pipe
    std::string stderr_buffer_;  // accumulated stderr output
    int  cached_exit_status_{0};  // stored by is_running() if child exited
    bool exit_cached_{false};     // true when cached_exit_status_ is valid
    // NOTE: helpers (reap_child, drain_stderr) were eliminated as 1-line
    // trampolines per TICKET-P2-25-PROCESSRUNNER-TRAMPOLINES (DONE 2026-07-14).
    // The implementations now live as free functions in the anonymous namespace
    // of process_runner_posix.cpp, taking their state via reference parameters.
};

} // namespace chronon3d::media::video
