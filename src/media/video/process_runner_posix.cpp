// ── ProcessRunner POSIX implementation ─────────────────────────────────────
// This TU contains the POSIX-specific method bodies for ProcessRunner.
// The cross-platform dtor/move operations live in process_runner.cpp.

#include "process_runner.hpp"

#include <cerrno>
#include <chrono>
#include <limits>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

// POSIX requires `environ` for posix_spawnp.
extern char** environ;

namespace chronon3d::media::video {

namespace {

constexpr std::size_t kStderrReadChunk = 4096;

void close_fd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

// TICKET-P2-25-PROCESSRUNNER-TRAMPOLINES (2026-07-14):
// 1-line member-method trampolines (reap_child + drain_stderr) eliminated;
// the implementations now live as free functions in this anonymous namespace.
// State is passed via reference parameters; the helpers do not touch any
// ProcessRunner member state directly (the class is NOT a public SDK symbol
// — see src/media/video/, no include/chronon3d/ entry — so removing the
// member-method ABI shape is permitted by the cat-2 freeze source-removal rule).
void drain_stderr(int& stderr_fd, std::string& stderr_buffer) {
    if (stderr_fd < 0) return;

    char buf[kStderrReadChunk];
    for (;;) {
        const ssize_t n = ::read(stderr_fd, buf, sizeof(buf));
        if (n > 0) {
            stderr_buffer.append(buf, static_cast<std::size_t>(n));
            if (stderr_buffer.size() > ProcessRunner::kMaxStderrBytes) {
                stderr_buffer.erase(0, stderr_buffer.size() - ProcessRunner::kMaxStderrBytes);
            }
        } else if (n == 0) {
            break;
        } else {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            break;
        }
    }
}

int reap_child(int& child_pid, int& stderr_fd, std::string& stderr_buffer) {
    if (child_pid <= 0) {
        drain_stderr(stderr_fd, stderr_buffer);
        return -1;
    }

    int status;
    pid_t result;
    do {
        result = ::waitpid(child_pid, &status, 0);
    } while (result < 0 && errno == EINTR);

    child_pid = -1;

    drain_stderr(stderr_fd, stderr_buffer);

    if (stderr_fd >= 0) {
        ::close(stderr_fd);
        stderr_fd = -1;
    }

    if (result < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return -WTERMSIG(status);
    return -1;
}

} // namespace

// ── launch ──────────────────────────────────────────────────────────────────

bool ProcessRunner::launch(const std::string& executable,
                           const std::vector<std::string>& argv) {
    if (child_pid_ > 0) {
        return false;  // already running
    }

    // Ignore SIGPIPE so ::write() returns EPIPE instead of killing
    // the process when the child closes its stdin (e.g. ffmpeg exits early).
    (void)signal(SIGPIPE, SIG_IGN);

    int pipe_fds[2];      // stdin  pipe: read-end→child, write-end→parent
    int stderr_pipe[2];   // stderr pipe: write-end→child, read-end→parent

    if (::pipe(pipe_fds) != 0) {
        return false;
    }
    if (::pipe(stderr_pipe) != 0) {
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        return false;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    // stdin
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);

    // stderr
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);

    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& arg : argv) {
        cargv.push_back(const_cast<char*>(arg.c_str()));
    }
    cargv.push_back(nullptr);

    pid_t pid;
    const int ret = posix_spawnp(
        &pid,
        executable.c_str(),
        &actions,
        nullptr,
        cargv.data(),
        environ);

    posix_spawn_file_actions_destroy(&actions);
    ::close(pipe_fds[0]);
    ::close(stderr_pipe[1]);

    if (ret != 0) {
        ::close(pipe_fds[1]);
        ::close(stderr_pipe[0]);
        return false;
    }

    child_pid_   = static_cast<int>(pid);
    stdin_fd_    = pipe_fds[1];
    stderr_fd_   = stderr_pipe[0];
    exit_cached_ = false;
    stderr_buffer_.clear();

    const int flags = ::fcntl(stderr_fd_, F_GETFL);
    if (flags >= 0) {
        ::fcntl(stderr_fd_, F_SETFL, flags | O_NONBLOCK);
    }

    const int stdin_fl = ::fcntl(stdin_fd_, F_GETFL);
    if (stdin_fl >= 0) {
        ::fcntl(stdin_fd_, F_SETFL, stdin_fl | O_NONBLOCK);
    }

    constexpr int kDesiredPipeSize = 2 * 1024 * 1024;  // 2 MiB
    (void)fcntl(stdin_fd_, F_SETPIPE_SZ, kDesiredPipeSize);

    return true;
}

// ── write ───────────────────────────────────────────────────────────────────

bool ProcessRunner::write(const std::uint8_t* data, std::size_t size) {
    if (stdin_fd_ < 0) {
        return false;
    }

    std::size_t written = 0;
    while (written < size) {
        const ssize_t n = ::write(stdin_fd_, data + written, size - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) {
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    return true;
}

// ── write_for ───────────────────────────────────────────────────────────────

bool ProcessRunner::write_for(const std::uint8_t* data, std::size_t size,
                              std::chrono::milliseconds timeout) {
    if (stdin_fd_ < 0) {
        return false;
    }

    if (timeout.count() <= 0) {
        timeout = std::chrono::hours(24);
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::size_t written = 0;

    while (written < size) {
        const auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining_ms <= 0) {
            return false;
        }

        struct pollfd fds[2];
        fds[0].fd      = stdin_fd_;
        fds[0].events  = POLLOUT;
        fds[0].revents = 0;
        fds[1].fd      = stderr_fd_;
        fds[1].events  = POLLIN;
        fds[1].revents = 0;

        const int nfds = (stderr_fd_ >= 0) ? 2 : 1;
        const int poll_timeout_ms =
            (remaining_ms > static_cast<decltype(remaining_ms)>(std::numeric_limits<int>::max()))
                ? std::numeric_limits<int>::max()
                : static_cast<int>(remaining_ms);

        const int result = ::poll(fds, nfds, poll_timeout_ms);
        if (result < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (result == 0) {
            return false;
        }

        if (nfds > 1 && (fds[1].revents & (POLLIN | POLLERR | POLLHUP))) {
            drain_stderr(stderr_fd_, stderr_buffer_);
        }

        if (fds[0].revents & POLLOUT) {
            const ssize_t n = ::write(stdin_fd_, data + written, size - written);
            if (n > 0) {
                written += static_cast<std::size_t>(n);
            } else if (n == 0) {
                return false;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                if (errno == EINTR) continue;
                return false;
            }
        }

        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            return false;
        }
    }

    return true;
}

// ── close_stdin ─────────────────────────────────────────────────────────────

void ProcessRunner::close_stdin() {
    close_fd(stdin_fd_);
}

// ── wait ────────────────────────────────────────────────────────────────────

int ProcessRunner::wait() {
    if (child_pid_ <= 0) {
        drain_stderr(stderr_fd_, stderr_buffer_);
        return -1;
    }

    if (exit_cached_) {
        drain_stderr(stderr_fd_, stderr_buffer_);
        const int rc = cached_exit_status_;
        child_pid_ = -1;
        exit_cached_ = false;
        return rc;
    }

    close_fd(stdin_fd_);
    drain_stderr(stderr_fd_, stderr_buffer_);
    return reap_child(child_pid_, stderr_fd_, stderr_buffer_);
}

// ── wait_for ─────────────────────────────────────────────────────────────────

int ProcessRunner::wait_for(std::chrono::milliseconds timeout) {
    if (child_pid_ <= 0) {
        drain_stderr(stderr_fd_, stderr_buffer_);
        return -1;
    }

    if (exit_cached_) {
        drain_stderr(stderr_fd_, stderr_buffer_);
        const int rc = cached_exit_status_;
        child_pid_ = -1;
        exit_cached_ = false;
        return rc;
    }

    close_fd(stdin_fd_);

    if (timeout.count() <= 0) {
        drain_stderr(stderr_fd_, stderr_buffer_);
        int status;
        const pid_t result = ::waitpid(child_pid_, &status, WNOHANG);
        if (result == 0) return -2;
        if (result < 0 && errno == EINTR) return -2;
        if (result < 0) return -1;

        child_pid_ = -1;
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return -WTERMSIG(status);
        return -1;
    }

    constexpr auto kPollInterval = std::chrono::milliseconds(10);
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    int status;
    pid_t result;
    do {
        drain_stderr(stderr_fd_, stderr_buffer_);
        result = ::waitpid(child_pid_, &status, WNOHANG);
        if (result > 0) {
            child_pid_ = -1;
            drain_stderr(stderr_fd_, stderr_buffer_);
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            if (WIFSIGNALED(status)) return -WTERMSIG(status);
            return -1;
        }
        if (result < 0) {
            if (errno == EINTR) continue;
            child_pid_ = -1;
            drain_stderr(stderr_fd_, stderr_buffer_);
            return -1;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            drain_stderr(stderr_fd_, stderr_buffer_);
            return -2;
        }
        std::this_thread::sleep_for(kPollInterval);
    } while (true);
}

// ── terminate ───────────────────────────────────────────────────────────────

void ProcessRunner::terminate() {
    if (child_pid_ > 0) {
        ::kill(child_pid_, SIGTERM);
    }
}

// ── terminate_and_wait ───────────────────────────────────────────────────────

int ProcessRunner::terminate_and_wait(std::chrono::milliseconds graceful_timeout) {
    if (child_pid_ <= 0) {
        drain_stderr(stderr_fd_, stderr_buffer_);
        return -1;
    }

    if (exit_cached_) {
        drain_stderr(stderr_fd_, stderr_buffer_);
        const int rc = cached_exit_status_;
        child_pid_ = -1;
        exit_cached_ = false;
        return rc;
    }

    ::kill(child_pid_, SIGTERM);

    int rc = wait_for(graceful_timeout);
    if (rc != -2) {
        drain_stderr(stderr_fd_, stderr_buffer_);
        return rc;
    }

    ::kill(child_pid_, SIGKILL);
    return reap_child(child_pid_, stderr_fd_, stderr_buffer_);
}

// ── is_running ───────────────────────────────────────────────────────────────

bool ProcessRunner::is_running() {
    if (child_pid_ <= 0) return false;
    if (exit_cached_) return false;

    int status;
    const pid_t result = ::waitpid(child_pid_, &status, WNOHANG);
    if (result == 0) return true;

    if (result > 0) {
        if (WIFEXITED(status)) {
            cached_exit_status_ = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            cached_exit_status_ = -WTERMSIG(status);
        } else {
            cached_exit_status_ = -1;
        }
        exit_cached_ = true;
    }
    return false;
}

// ── consume_stderr ──────────────────────────────────────────────────────────

std::string ProcessRunner::consume_stderr() noexcept {
    drain_stderr(stderr_fd_, stderr_buffer_);
    std::string result;
    result.swap(stderr_buffer_);
    return result;
}

// ── drain_stderr / reap_child ────────────────────────────────────────────────
// Eliminated as 1-line member-method trampolines per
// TICKET-P2-25-PROCESSRUNNER-TRAMPOLINES (DONE 2026-07-14).
// The implementations now live as free functions in the anonymous namespace
// at the top of this TU, taking their state via reference parameters.

} // namespace chronon3d::media::video
