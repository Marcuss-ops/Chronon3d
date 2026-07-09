// SPDX-License-Identifier: MIT
// ════════════════════════════════════════════════════════════════════════════
// src/media/video/process_error_handler.cpp — FASE 5 process_runner sub-extract #2.
//
// Pure-function re-implementation of the error/exit-code-handling block
// formerly INLINE-DUPLICATED inside process_runner.cpp (reap_child / wait /
// wait_for / terminate / terminate_and_wait / is_running).  Stateless free
// functions in the `chronon3d::media::video::process_error_handler`
// sub-namespace operate on caller-supplied state references; the
// ProcessRunner class owns the actual fields and delegates to these
// helpers via 1-line callsites (see process_runner.cpp).
//
// Anti-duplication invariant (AGENTS.md Cat-5 ABI-stable):
//   * ZERO new public API surface (file lives in src/media/video/, NOT
//     in `include/chronon3d/`).
//   * ZERO new singleton/registry/cache/resolver/service-locator.
// ════════════════════════════════════════════════════════════════════════════

#include "process_error_handler.hpp"
#include "process_pipe_io.hpp"

#include <cerrno>
#include <chrono>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace chronon3d::media::video::process_error_handler {

// ============================================================================
//  reap_child() — waitpid with EINTR retry (private detail helper)
// ============================================================================

namespace detail {

int reap_child(int&         child_pid,
               int&         stderr_fd,
               std::string& stderr_buffer,
               std::size_t  stderr_max_bytes) {
    if (child_pid <= 0) {
        process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);
        return -1;
    }

    int status;
    pid_t result;
    do {
        result = ::waitpid(child_pid, &status, 0);
    } while (result < 0 && errno == EINTR);

    child_pid = -1;

    // Drain any remaining stderr after the child has fully exited.
    process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);

    // Close the stderr pipe read-end — no more data will arrive.
    if (stderr_fd >= 0) {
        ::close(stderr_fd);
        stderr_fd = -1;
    }

    if (result < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return -WTERMSIG(status);
    return -1;
}

} // namespace detail

// ============================================================================
//  wait() — block-wait for child exit
// ============================================================================

int wait(int&         child_pid,
         int&         cached_exit_status,
         bool&        exit_cached,
         int&         stdin_fd,
         int&         stderr_fd,
         std::string& stderr_buffer,
         std::size_t  stderr_max_bytes) {
    if (child_pid <= 0) {
        process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);
        return -1;
    }

    // If is_running() already cached the exit status, use it.
    if (exit_cached) {
        process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);
        const int rc = cached_exit_status;
        child_pid = -1;
        exit_cached = false;
        return rc;
    }

    // Ensure stdin is closed before waiting — the child may be blocked
    // reading from stdin.
    process_pipe_io::close_fd(stdin_fd);

    process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);

    return detail::reap_child(child_pid, stderr_fd, stderr_buffer, stderr_max_bytes);
}

// ============================================================================
//  wait_for() — block up to `timeout` for child to exit
// ============================================================================

int wait_for(int&                child_pid,
             int&                cached_exit_status,
             bool&               exit_cached,
             int&                stdin_fd,
             int&                stderr_fd,
             std::string&        stderr_buffer,
             std::size_t         stderr_max_bytes,
             std::chrono::milliseconds timeout) {
    if (child_pid <= 0) {
        process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);
        return -1;
    }

    // If is_running() already cached the exit status, use it.
    if (exit_cached) {
        process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);
        const int rc = cached_exit_status;
        child_pid = -1;
        exit_cached = false;
        return rc;
    }

    // Ensure stdin is closed — the child may be blocked reading.
    process_pipe_io::close_fd(stdin_fd);

    // If timeout is zero or negative, just check once non-blocking.
    if (timeout.count() <= 0) {
        process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);
        int status;
        const pid_t result = ::waitpid(child_pid, &status, WNOHANG);
        if (result == 0) return -2;  // still running → timeout
        if (result < 0 && errno == EINTR) return -2;
        if (result < 0) return -1;

        child_pid = -1;
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return -WTERMSIG(status);
        return -1;
    }

    // Poll with WNOHANG, sleeping 10ms between polls.
    // This is simpler and more portable than waitid(P_PID, ..., WEXITED | WNOWAIT)
    // with a timer, and the granularity is fine for subprocess management.
    constexpr auto kPollInterval = std::chrono::milliseconds(10);
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    int status;
    pid_t result;
    do {
        // Drain stderr every iteration to prevent the child from blocking
        // on a full stderr pipe while we wait for it to exit.
        process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);

        result = ::waitpid(child_pid, &status, WNOHANG);
        if (result > 0) {
            // Child has exited.
            child_pid = -1;
            process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            if (WIFSIGNALED(status)) return -WTERMSIG(status);
            return -1;
        }
        if (result < 0) {
            if (errno == EINTR) continue;
            // Child already reaped or error.
            child_pid = -1;
            process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);
            return -1;
        }
        // result == 0 → still running, sleep.
        if (std::chrono::steady_clock::now() >= deadline) {
            process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);
            return -2;  // timeout
        }
        std::this_thread::sleep_for(kPollInterval);
    } while (true);
}

// ============================================================================
//  terminate() — SIGTERM the child
// ============================================================================

void terminate(int child_pid) {
    if (child_pid > 0) {
        ::kill(child_pid, SIGTERM);
    }
}

// ============================================================================
//  terminate_and_wait() — SIGTERM → wait → SIGKILL → reap
// ============================================================================

int terminate_and_wait(int&                child_pid,
                       int&                cached_exit_status,
                       bool&               exit_cached,
                       int&                stdin_fd,
                       int&                stderr_fd,
                       std::string&        stderr_buffer,
                       std::size_t         stderr_max_bytes,
                       std::chrono::milliseconds graceful_timeout) {
    if (child_pid <= 0) {
        process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);
        return -1;
    }

    // If the child already exited via is_running(), use cached status.
    if (exit_cached) {
        process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);
        const int rc = cached_exit_status;
        child_pid = -1;
        exit_cached = false;
        return rc;
    }

    // Step 1: SIGTERM.
    ::kill(child_pid, SIGTERM);

    // Step 2: Wait for graceful shutdown with timeout.
    int rc = process_error_handler::wait_for(child_pid, cached_exit_status, exit_cached,
                                             stdin_fd, stderr_fd, stderr_buffer,
                                             stderr_max_bytes, graceful_timeout);
    if (rc != -2) {
        // Child exited or error (not a timeout).
        process_pipe_io::drain_stderr(stderr_fd, stderr_buffer, stderr_max_bytes);
        return rc;
    }

    // Step 3: Timeout — escalate to SIGKILL.
    ::kill(child_pid, SIGKILL);

    // Step 4: Must reap now — no timeout.
    return detail::reap_child(child_pid, stderr_fd, stderr_buffer, stderr_max_bytes);
}

// ============================================================================
//  is_running() — non-blocking check; caches exit status for wait()
// ============================================================================

bool is_running(int   child_pid,
                int&  cached_exit_status,
                bool& exit_cached) {
    if (child_pid <= 0) return false;

    // If we already cached the exit, the child is not running.
    if (exit_cached) return false;

    int status;
    const pid_t result = ::waitpid(child_pid, &status, WNOHANG);
    if (result == 0) return true;   // still running

    // Child has exited — cache the exit status so wait() can retrieve it.
    if (result > 0) {
        if (WIFEXITED(status)) {
            cached_exit_status = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            cached_exit_status = -WTERMSIG(status);
        } else {
            cached_exit_status = -1;
        }
        exit_cached = true;
    }
    // result < 0: child already reaped or error — not running.
    return false;
}

} // namespace chronon3d::media::video::process_error_handler
