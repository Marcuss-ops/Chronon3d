// ── ProcessRunner cross-platform shell ───────────────────────────────────────
// POSIX-specific method bodies live in process_runner_posix.cpp.

#include "process_runner.hpp"

#include <unistd.h>

namespace chronon3d::media::video {

ProcessRunner::~ProcessRunner() noexcept {
    if (child_pid_ > 0) {
        terminate_and_wait(std::chrono::seconds(5));
    } else {
        close_stdin();
    }

    if (stderr_fd_ >= 0) {
        ::close(stderr_fd_);
        stderr_fd_ = -1;
    }
}

ProcessRunner::ProcessRunner(ProcessRunner&& other) noexcept
    : child_pid_(other.child_pid_)
    , stdin_fd_(other.stdin_fd_)
    , stderr_fd_(other.stderr_fd_)
    , stderr_buffer_(std::move(other.stderr_buffer_))
    , cached_exit_status_(other.cached_exit_status_)
    , exit_cached_(other.exit_cached_)
{
    other.child_pid_   = -1;
    other.stdin_fd_    = -1;
    other.stderr_fd_   = -1;
    other.exit_cached_ = false;
}

ProcessRunner& ProcessRunner::operator=(ProcessRunner&& other) noexcept {
    if (this != &other) {
        if (child_pid_ > 0) {
            terminate_and_wait(std::chrono::seconds(5));
        }
        close_stdin();

        if (stderr_fd_ >= 0) {
            ::close(stderr_fd_);
        }

        child_pid_          = other.child_pid_;
        stdin_fd_           = other.stdin_fd_;
        stderr_fd_          = other.stderr_fd_;
        stderr_buffer_      = std::move(other.stderr_buffer_);
        cached_exit_status_ = other.cached_exit_status_;
        exit_cached_        = other.exit_cached_;

        other.child_pid_   = -1;
        other.stdin_fd_    = -1;
        other.stderr_fd_   = -1;
        other.exit_cached_ = false;
    }
    return *this;
}

} // namespace chronon3d::media::video
