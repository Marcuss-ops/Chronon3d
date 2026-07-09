// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/media/video/process_pipe_io.cpp — see process_pipe_io.hpp for the
// module-level contract.  Body of each function is bit-identical to the
// prior inlined method body in process_runner.cpp with `this->member`
// → parameter rename; timeouts + errno handling + EPIPE semantics are
// preserved 1:1.
// ═══════════════════════════════════════════════════════════════════════════

#include "process_pipe_io.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <poll.h>
#include <unistd.h>

namespace chronon3d::media::video::process_pipe_io {

namespace {

constexpr std::size_t kStderrReadChunk = 4096;

} // namespace

void drain_stderr(int fd, std::string& buffer, std::size_t max_bytes) {
    if (fd < 0) return;

    char buf[kStderrReadChunk];
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            // Always append new data first, then trim from the front if the
            // buffer exceeds max_bytes.  This keeps the *last* max_bytes
            // of stderr, which is most useful for diagnosing failures.
            buffer.append(buf, static_cast<std::size_t>(n));
            if (buffer.size() > max_bytes) {
                buffer.erase(0, buffer.size() - max_bytes);
            }
        } else if (n == 0) {
            // EOF — pipe write-end closed by the child.
            break;
        } else {
            // n < 0
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // no more data right now
            }
            break;  // real error — stop draining
        }
    }
}

std::string consume_stderr(int fd, std::string& buffer, std::size_t max_bytes) {
    drain_stderr(fd, buffer, max_bytes);
    std::string result;
    result.swap(buffer);
    return result;
}

bool write_all(int fd, const std::uint8_t* data, std::size_t size) {
    if (fd < 0) {
        return false;
    }

    std::size_t written = 0;
    while (written < size) {
        const ssize_t n = ::write(
            fd,
            data + written,
            size - written);

        if (n < 0) {
            if (errno == EINTR) continue;
            // EPIPE = child closed its stdin (exited early, broken pipe).
            return false;
        }
        if (n == 0) {
            // Should not happen for a pipe write, but treat as failure.
            return false;
        }
        written += static_cast<std::size_t>(n);
    }

    return true;
}

bool write_for(int                stdin_fd,
               int                stderr_fd,
               std::string&       stderr_buffer,
               std::size_t         max_bytes,
               const std::uint8_t* data,
               std::size_t         size,
               std::chrono::milliseconds timeout) {
    if (stdin_fd < 0) {
        return false;
    }

    // If timeout is zero or negative, use a very large default instead of
    // falling back to write() — the stdin fd is O_NONBLOCK, so write()
    // would return EAGAIN on a full pipe instead of blocking.
    if (timeout.count() <= 0) {
        timeout = std::chrono::hours(24);
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::size_t written = 0;

    while (written < size) {
        const auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining_ms <= 0) {
            return false;  // deadline exceeded
        }

        // Poll stdin for POLLOUT and stderr for POLLIN simultaneously.
        struct pollfd fds[2];
        fds[0].fd      = stdin_fd;
        fds[0].events  = POLLOUT;
        fds[0].revents = 0;
        fds[1].fd      = stderr_fd;
        fds[1].events  = POLLIN;
        fds[1].revents = 0;

        const int nfds = (stderr_fd >= 0) ? 2 : 1;
        // Clamp remaining_ms to int range for poll().
        const int poll_timeout_ms =
            (remaining_ms > static_cast<decltype(remaining_ms)>(
                              std::numeric_limits<int>::max()))
                ? std::numeric_limits<int>::max()
                : static_cast<int>(remaining_ms);
        const int result = ::poll(fds, nfds, poll_timeout_ms);

        if (result < 0) {
            if (errno == EINTR) continue;
            return false;  // poll error
        }

        if (result == 0) {
            return false;  // timeout — no progress
        }

        // Drain stderr first (non-blocking inside drain_stderr).
        if (nfds > 1 && (fds[1].revents & (POLLIN | POLLERR | POLLHUP))) {
            drain_stderr(stderr_fd, stderr_buffer, max_bytes);
        }

        // Check if stdin is writable or broken.
        if (fds[0].revents & POLLOUT) {
            const ssize_t n = ::write(stdin_fd, data + written, size - written);
            if (n > 0) {
                written += static_cast<std::size_t>(n);
            } else if (n == 0) {
                return false;  // unexpected condition on pipe write
            } else {
                // n < 0
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;  // retry poll
                }
                if (errno == EINTR) continue;
                return false;  // EPIPE or other error
            }
        }

        // Check for pipe errors / hangup on stdin.
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            return false;
        }
    }

    return true;
}

void close_fd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

} // namespace chronon3d::media::video::process_pipe_io
