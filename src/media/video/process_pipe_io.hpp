// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/media/video/process_pipe_io.hpp — FASE 5 process_runner sub-extract #1.
//
// Pure-function extraction of the pipe-read/pipe-write block formerly
// INLINE-DUPLICATED inside process_runner.cpp (drain_stderr / consume_stderr /
// write / write_for / close_stdin).  Stateless free functions in the
// `chronon3d::media::video::process_pipe_io` sub-namespace operate on
// caller-supplied file descriptors + a stderr buffer reference; the
// ProcessRunner class owns the state and delegates to these helpers via
// 1-line callsites.
//
// What this module exposes:
//   * drain_stderr()    — non-blocking stderr reader with dd-style "keep
//                          last max_bytes" trim after each read.
//   * consume_stderr()  — drain + swap-and-return wrapper.  Idempotent
//                          w.r.t. caller-stderr_buffer (cleared on return).
//   * write_all()       — raw fd write with full-buffer + EINTR retry loop.
//                          Matches the legacy `ProcessRunner::write` semantics.
//   * write_for()       — poll(POLLOUT + POLLIN) write with deadline + a
//                          concurrent stderr drain to prevent the child
//                          from blocking on a full stderr pipe while we wait.
//                          Closure-walking deadline calculated in steady_clock.
//   * close_fd()        — ::close + sentinel reset on the fd reference.
//
// Anti-duplication invariant (AGENTS.md Cat-5 ABI-stable):
//   * ZERO new public API surface (lives in src/media/video/, NOT in
//     `include/chronon3d/`).
//   * ZERO new singleton/registry/cache/resolver/service-locator.
//   * Functions are pure — no internal state, no class methods, no IO
//     ownership.  All knobs (fd, buffer, max_bytes, timeout) are explicit
//     parameters; the ProcessRunner class owns the actual state.
//   * Body of each function is bit-identical to the prior inlined method
//     body in process_runner.cpp with `this->member` → parameter rename.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace chronon3d::media::video::process_pipe_io {

/// Non-blocking drain of FD `fd` into `buffer`.  After each successful
/// read, trim from the front so the buffer never exceeds `max_bytes` —
/// keeping the LAST max_bytes is what makes late-append diagnostics
/// (e.g. error-at-shutdown mode) useful.
///
/// Returns early on EOF (n == 0), EAGAIN/EWOULDBLOCK (no data right now),
/// or any other read error.  EINTR retries inline.
void drain_stderr(int fd, std::string& buffer, std::size_t max_bytes);

/// Drain then atomically swap+return the accumulated stderr text, clearing
/// the caller's buffer.  Subsequent calls see only stderr produced AFTER
/// the previous consume_stderr().
std::string consume_stderr(int fd, std::string& buffer, std::size_t max_bytes);

/// Write all `size` bytes from `data` to FD `fd`.  Retries on EINTR.
/// Treats EPIPE (and any other write error) as a hard failure.  Returns
/// false if the fd is invalid (< 0).
bool write_all(int fd, const std::uint8_t* data, std::size_t size);

/// Poll-based bounded write with concurrent stderr drain.
///
/// Polls `stdin_fd` for POLLOUT and `stderr_fd` for POLLIN simultaneously;
/// when stderr is readable, drains it via `drain_stderr(stderr_fd, …)` to
/// prevent the child from blocking on a full stderr pipe.  Honours the
/// deadline via `std::chrono::steady_clock`; clamps remaining_ms to
/// `int` range for ::poll(2).  Polls with O_NONBLOCK fds (set up by the
/// launcher).  Returns false on timeout, EPIPE, or POLLERR/POLLHUP/POLLNVAL.
bool write_for(int                stdin_fd,
               int                stderr_fd,
               std::string&       stderr_buffer,
               std::size_t         max_bytes,
               const std::uint8_t* data,
               std::size_t         size,
               std::chrono::milliseconds timeout);

/// ::close(2) wrapper that resets the caller's fd reference to -1 after
/// success.  Idempotent: calling on an already-negative fd is a no-op.
void close_fd(int& fd);

} // namespace chronon3d::media::video::process_pipe_io
