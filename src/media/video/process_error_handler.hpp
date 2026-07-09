// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/media/video/process_error_handler.hpp — FASE 5 process_runner sub-extract #2.
//
// Pure-function extraction of the error/exit-code-handling block formerly
// INLINE-DUPLICATED inside process_runner.cpp (reap_child / is_running /
// wait / wait_for / terminate / terminate_and_wait).  Stateless free
// functions in the `chronon3d::media::video::process_error_handler`
// sub-namespace operate on caller-supplied state references; the
// ProcessRunner class owns the actual fields and delegates to these
// helpers via 1-line callsites.
//
// What this module exposes:
//   * wait()                — block-wait for child exit.  Honours
//                             exit-cache; closes stdin first; then reaps.
//   * wait_for()            — bounded wait with poll-on-WNOHANG + iterative
//                             stderr drain.  Returns -2 on timeout.
//   * terminate()           — SIGTERM only (no reap).
//   * terminate_and_wait()  — SIGTERM → wait_for(graceful) → SIGKILL → reap.
//   * is_running()          — non-blocking check; caches exit status on
//                             first WIFEXITED/WIFSIGNALED detection.
//
// `detail::reap_child()` is a private helper called from wait() +
// terminate_and_wait(); kept out of the public surface.
//
// Anti-duplication invariant (AGENTS.md Cat-5 ABI-stable):
//   * ZERO new public API surface (file lives in src/media/video/, NOT
//     in `include/chronon3d/`).
//   * ZERO new singleton/registry/cache/resolver/service-locator.
//   * Functions are pure — no internal state, no IO ownership.  All knobs
//     (child_pid, cached_exit_status, exit_cached, stdin_fd, stderr_fd,
//     stderr_buffer, max_bytes, timeout) are explicit parameters.
//   * Internal calls into `process_pipe_io::drain_stderr(...)` and
//     `process_pipe_io::close_fd(...)` thread through `max_bytes` so the
//     ProcessRunner-owned stderr-buffer cap (kMaxStderrBytes) stays the
//     single source of truth.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chrono>
#include <cstddef>
#include <string>

namespace chronon3d::media::video::process_error_handler {

/// Block-wait for the child to exit.  Honours exit_cached (returns the
/// cached exit status immediately if is_running() already saw the child
/// exit).  Otherwise closes stdin first (so child can drain its input),
/// then drains stderr, then reaps with EINTR retry.
int wait(int&         child_pid,
         int&         cached_exit_status,
         bool&        exit_cached,
         int&         stdin_fd,
         int&         stderr_fd,
         std::string& stderr_buffer,
         std::size_t  stderr_max_bytes);

/// Block-wait up to `timeout`.  Returns -2 on timeout (child still
/// running); exit code (0-255) on normal exit; -1 on error.  Honours the
/// exit-cache.  Polls via WNOHANG every 10 ms against a steady_clock
/// deadline.
int wait_for(int&                child_pid,
             int&                cached_exit_status,
             bool&               exit_cached,
             int&                stdin_fd,
             int&                stderr_fd,
             std::string&        stderr_buffer,
             std::size_t         stderr_max_bytes,
             std::chrono::milliseconds timeout);

/// SIGTERM the child.  Does NOT wait — caller must follow with
/// wait() / wait_for().
void terminate(int child_pid);

/// SIGTERM the child, wait up to `graceful_timeout`, then escalate to
/// SIGKILL if still running.  Blocks until the child is fully reaped.
/// Honours the exit-cache.
int terminate_and_wait(int&                child_pid,
                       int&                cached_exit_status,
                       bool&               exit_cached,
                       int&                stdin_fd,
                       int&                stderr_fd,
                       std::string&        stderr_buffer,
                       std::size_t         stderr_max_bytes,
                       std::chrono::milliseconds graceful_timeout);

/// Non-blocking check.  Returns true if the child is still running.
/// On WIFEXITED / WIFSIGNALED detection, caches the exit status into
/// `cached_exit_status` and sets `exit_cached = true` so a subsequent
/// wait() returns immediately with the cached exit code.
///
/// Returns false if there is no child (`child_pid_ <= 0`) or the exit
/// status has already been cached.
bool is_running(int  child_pid,
                int& cached_exit_status,
                bool& exit_cached);

namespace detail {

/// Internal: reap the child with waitpid(2), retrying on EINTR.  Returns
/// exit code (0-255), negative on error.  On success, sets child_pid = -1.
/// Also drains stderr + closes the read-end fd as part of the cleanup.
int reap_child(int&         child_pid,
               int&         stderr_fd,
               std::string& stderr_buffer,
               std::size_t  stderr_max_bytes);

} // namespace detail

} // namespace chronon3d::media::video::process_error_handler
