#include "process_runner.hpp"

#if defined(_WIN32)

// ── Windows stubs ───────────────────────────────────────────────────────────
// CreateProcessW path not yet implemented.
// TODO(codex/ffmpeg-process-runner): implement CreateProcessW with stdin pipe.

#include <cstdio>

namespace chronon3d::media::video {

ProcessRunner::~ProcessRunner() noexcept {
    if (child_pid_ > 0) {
        // Graceful shutdown: close stdin, SIGTERM, wait 5s, SIGKILL, reap.
        // terminate_and_wait → wait_for handles close_stdin internally.
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
    other.child_pid_ = -1;
    other.stdin_fd_  = -1;
    other.stderr_fd_ = -1;
    other.exit_cached_ = false;
}

ProcessRunner& ProcessRunner::operator=(ProcessRunner&& other) noexcept {
    if (this != &other) {
        if (child_pid_ > 0) {
            // terminate_and_wait → wait_for handles close_stdin internally.
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

int ProcessRunner::reap_child() {
    if (child_pid_ <= 0) {
        drain_stderr();
        return -1;
    }

    int status;
    pid_t result;
    do {
        result = ::waitpid(child_pid_, &status, 0);
    } while (result < 0 && errno == EINTR);

    child_pid_ = -1;

    drain_stderr();

    if (stderr_fd_ >= 0) {
        ::close(stderr_fd_);
        stderr_fd_ = -1;
    }

    if (result < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return -WTERMSIG(status);
    return -1;
}

bool ProcessRunner::launch(const std::string&, const std::vector<std::string>&) {
    std::fprintf(stderr, "[ProcessRunner] Windows CreateProcessW not yet implemented\n");
    return false;
}
bool ProcessRunner::write(const std::uint8_t*, std::size_t) { return false; }
void ProcessRunner::close_stdin() {}
int  ProcessRunner::wait() { return -1; }
int  ProcessRunner::wait_for(std::chrono::milliseconds) { return -1; }
int  ProcessRunner::terminate_and_wait(std::chrono::milliseconds) {
    // On Windows stubs, just reap what we can.
    drain_stderr();
    if (child_pid_ > 0) {
        child_pid_ = -1;
    }
    return -1;
}
void ProcessRunner::terminate() {}
bool ProcessRunner::is_running() { return false; }
void ProcessRunner::drain_stderr() {
    // No-op on Windows stubs.
}
std::string ProcessRunner::consume_stderr() noexcept {
    drain_stderr();
    std::string result;
    result.swap(stderr_buffer_);
    return result;
}

} // namespace chronon3d::media::video

#else  // ── Linux / POSIX ────────────────────────────────────────────────────

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

// POSIX requires `environ` for posix_spawnp.
extern char** environ;

namespace chronon3d::media::video {

// ============================================================================
//  Construction / Destruction / Move
// ============================================================================

ProcessRunner::~ProcessRunner() noexcept {
    if (child_pid_ > 0) {
        // Graceful shutdown: terminate_and_wait → wait_for handles
        // close_stdin internally before polling. drain_stderr is called
        // inside terminate_and_wait before we return.
        terminate_and_wait(std::chrono::seconds(5));
    } else {
        close_stdin();
    }

    // Close the stderr pipe read-end if the child exited without us
    // having cleaned up (e.g. after a move).
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
            // terminate_and_wait → wait_for handles close_stdin internally.
            terminate_and_wait(std::chrono::seconds(5));
        }
        close_stdin();

        // Close our old stderr fd before taking ownership of other's.
        if (stderr_fd_ >= 0) {
            ::close(stderr_fd_);
        }

        child_pid_         = other.child_pid_;
        stdin_fd_          = other.stdin_fd_;
        stderr_fd_         = other.stderr_fd_;
        stderr_buffer_     = std::move(other.stderr_buffer_);
        cached_exit_status_ = other.cached_exit_status_;
        exit_cached_       = other.exit_cached_;

        other.child_pid_   = -1;
        other.stdin_fd_    = -1;
        other.stderr_fd_   = -1;
        other.exit_cached_ = false;
    }
    return *this;
}

// ============================================================================
//  reap_child() — waitpid with EINTR retry
// ============================================================================

int ProcessRunner::reap_child() {
    if (child_pid_ <= 0) {
        drain_stderr();
        return -1;
    }

    int status;
    pid_t result;
    do {
        result = ::waitpid(child_pid_, &status, 0);
    } while (result < 0 && errno == EINTR);

    child_pid_ = -1;

    // Drain any remaining stderr after the child has fully exited.
    drain_stderr();

    // Close the stderr pipe read-end — no more data will arrive.
    if (stderr_fd_ >= 0) {
        ::close(stderr_fd_);
        stderr_fd_ = -1;
    }

    if (result < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return -WTERMSIG(status);
    return -1;
}

// ============================================================================
//  launch() — posix_spawnp with stdin pipe
// ============================================================================

bool ProcessRunner::launch(const std::string& executable,
                            const std::vector<std::string>& argv) {
    if (child_pid_ > 0) {
        return false;  // already running
    }

    // Ignore SIGPIPE so ::write() returns EPIPE instead of killing
    // the process when the child closes its stdin (e.g. ffmpeg exits early).
    // This is process-wide, which is the desired behaviour for a video pipeline.
    (void)signal(SIGPIPE, SIG_IGN);

    // Create pipes for stdin and stderr.
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

    // Build posix_spawn file actions:
    //   - dup2(read-end → stdin) so the child reads from the pipe
    //   - dup2(write-end → stderr) so the child writes stderr to the pipe
    //   - close all pipe ends in the child after dup2
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

    // Build C-style argv array (null-terminated).
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
        nullptr,       // no custom attributes
        cargv.data(),
        environ);

    posix_spawn_file_actions_destroy(&actions);
    ::close(pipe_fds[0]);      // close stdin read-end  (parent doesn't need it)
    ::close(stderr_pipe[1]);   // close stderr write-end (child owns the dup'd fd)

    if (ret != 0) {
        ::close(pipe_fds[1]);
        ::close(stderr_pipe[0]);
        return false;
    }

    child_pid_  = static_cast<int>(pid);
    stdin_fd_   = pipe_fds[1];
    stderr_fd_  = stderr_pipe[0];
    exit_cached_ = false;
    stderr_buffer_.clear();

    // Set the stderr read-end to non-blocking so drain_stderr() never blocks.
    const int flags = ::fcntl(stderr_fd_, F_GETFL);
    if (flags >= 0) {
        ::fcntl(stderr_fd_, F_SETFL, flags | O_NONBLOCK);
    }

    // Enlarge the kernel pipe buffer to reduce backpressure.
    constexpr int kDesiredPipeSize = 2 * 1024 * 1024;  // 2 MiB
    (void)fcntl(stdin_fd_, F_SETPIPE_SZ, kDesiredPipeSize);

    return true;
}

// ============================================================================
//  drain_stderr() — collect child stderr into buffer (non-blocking)
// ============================================================================

void ProcessRunner::drain_stderr() {
    if (stderr_fd_ < 0) return;

    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(stderr_fd_, buf, sizeof(buf));
        if (n > 0) {
            stderr_buffer_.append(buf, static_cast<std::size_t>(n));
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

// ============================================================================
//  consume_stderr() — return captured stderr and clear the buffer
// ============================================================================

std::string ProcessRunner::consume_stderr() noexcept {
    drain_stderr();
    std::string result;
    result.swap(stderr_buffer_);
    return result;
}

// ============================================================================
//  write() — raw fd write with EINTR retry
// ============================================================================

bool ProcessRunner::write(const std::uint8_t* data, std::size_t size) {
    if (stdin_fd_ < 0) {
        return false;
    }

    std::size_t written = 0;
    while (written < size) {
        const ssize_t n = ::write(
            stdin_fd_,
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

// ============================================================================
//  close_stdin() — signal EOF to the child
// ============================================================================

void ProcessRunner::close_stdin() {
    if (stdin_fd_ >= 0) {
        ::close(stdin_fd_);
        stdin_fd_ = -1;
    }
}

// ============================================================================
//  wait() — collect child exit status (with EINTR retry)
// ============================================================================

int ProcessRunner::wait() {
    if (child_pid_ <= 0) {
        drain_stderr();
        return -1;
    }

    // If is_running() already cached the exit status, use it.
    if (exit_cached_) {
        drain_stderr();
        const int rc = cached_exit_status_;
        child_pid_ = -1;
        exit_cached_ = false;
        return rc;
    }

    // Ensure stdin is closed before waiting — the child may be blocked
    // reading from stdin.
    close_stdin();

    drain_stderr();

    return reap_child();
}

// ============================================================================
//  terminate() — SIGTERM the child
// ============================================================================

void ProcessRunner::terminate() {
    if (child_pid_ > 0) {
        ::kill(child_pid_, SIGTERM);
    }
}

// ============================================================================
//  is_running() — non-blocking check; caches exit status for wait()
// ============================================================================

bool ProcessRunner::is_running() {
    if (child_pid_ <= 0) return false;

    // If we already cached the exit, the child is not running.
    if (exit_cached_) return false;

    int status;
    const pid_t result = ::waitpid(child_pid_, &status, WNOHANG);
    if (result == 0) return true;   // still running

    // Child has exited — cache the exit status so wait() can retrieve it.
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
    // result < 0: child already reaped or error — not running.
    return false;
}

// ============================================================================
//  wait_for() — block up to `timeout` for child to exit
// ============================================================================

int ProcessRunner::wait_for(std::chrono::milliseconds timeout) {
    if (child_pid_ <= 0) {
        drain_stderr();
        return -1;
    }

    // If is_running() already cached the exit status, use it.
    if (exit_cached_) {
        drain_stderr();
        const int rc = cached_exit_status_;
        child_pid_ = -1;
        exit_cached_ = false;
        return rc;
    }

    // Ensure stdin is closed — the child may be blocked reading.
    close_stdin();

    // If timeout is zero or negative, just check once non-blocking.
    if (timeout.count() <= 0) {
        drain_stderr();
        int status;
        const pid_t result = ::waitpid(child_pid_, &status, WNOHANG);
        if (result == 0) return -2;  // still running → timeout
        if (result < 0 && errno == EINTR) return -2;
        if (result < 0) return -1;

        child_pid_ = -1;
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
        result = ::waitpid(child_pid_, &status, WNOHANG);
        if (result > 0) {
            // Child has exited.
            child_pid_ = -1;
            drain_stderr();
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            if (WIFSIGNALED(status)) return -WTERMSIG(status);
            return -1;
        }
        if (result < 0) {
            if (errno == EINTR) continue;
            // Child already reaped or error.
            child_pid_ = -1;
            drain_stderr();
            return -1;
        }
        // result == 0 → still running, sleep.
        if (std::chrono::steady_clock::now() >= deadline) {
            drain_stderr();
            return -2;  // timeout
        }
        std::this_thread::sleep_for(kPollInterval);
    } while (true);
}

// ============================================================================
//  terminate_and_wait() — SIGTERM → wait → SIGKILL → reap
// ============================================================================

int ProcessRunner::terminate_and_wait(std::chrono::milliseconds graceful_timeout) {
    if (child_pid_ <= 0) {
        drain_stderr();
        return -1;
    }

    // If the child already exited via is_running(), use cached status.
    if (exit_cached_) {
        drain_stderr();
        const int rc = cached_exit_status_;
        child_pid_ = -1;
        exit_cached_ = false;
        return rc;
    }

    // Step 1: SIGTERM.
    ::kill(child_pid_, SIGTERM);

    // Step 2: Wait for graceful shutdown with timeout.
    int rc = wait_for(graceful_timeout);
    if (rc != -2) {
        // Child exited or error (not a timeout).
        drain_stderr();
        return rc;
    }

    // Step 3: Timeout — escalate to SIGKILL.
    ::kill(child_pid_, SIGKILL);

    // Step 4: Must reap now — no timeout.
    return reap_child();
}

} // namespace chronon3d::media::video

#endif  // _WIN32
