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
}

ProcessRunner::ProcessRunner(ProcessRunner&& other) noexcept
    : child_pid_(other.child_pid_)
    , stdin_fd_(other.stdin_fd_)
    , cached_exit_status_(other.cached_exit_status_)
    , exit_cached_(other.exit_cached_)
{
    other.child_pid_ = -1;
    other.stdin_fd_  = -1;
    other.exit_cached_ = false;
}

ProcessRunner& ProcessRunner::operator=(ProcessRunner&& other) noexcept {
    if (this != &other) {
        if (child_pid_ > 0) {
            // terminate_and_wait → wait_for handles close_stdin internally.
            terminate_and_wait(std::chrono::seconds(5));
        }
        close_stdin();
        child_pid_         = other.child_pid_;
        stdin_fd_          = other.stdin_fd_;
        cached_exit_status_ = other.cached_exit_status_;
        exit_cached_       = other.exit_cached_;
        other.child_pid_   = -1;
        other.stdin_fd_    = -1;
        other.exit_cached_ = false;
    }
    return *this;
}

int ProcessRunner::reap_child() {
    if (child_pid_ <= 0) return -1;

    int status;
    pid_t result;
    do {
        result = ::waitpid(child_pid_, &status, 0);
    } while (result < 0 && errno == EINTR);

    child_pid_ = -1;

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
    if (child_pid_ > 0) {
        child_pid_ = -1;
    }
    return -1;
}
void ProcessRunner::terminate() {}
bool ProcessRunner::is_running() { return false; }

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
        // close_stdin internally before polling.
        terminate_and_wait(std::chrono::seconds(5));
    } else {
        close_stdin();
    }
}

ProcessRunner::ProcessRunner(ProcessRunner&& other) noexcept
    : child_pid_(other.child_pid_)
    , stdin_fd_(other.stdin_fd_)
    , cached_exit_status_(other.cached_exit_status_)
    , exit_cached_(other.exit_cached_)
{
    other.child_pid_   = -1;
    other.stdin_fd_    = -1;
    other.exit_cached_ = false;
}

ProcessRunner& ProcessRunner::operator=(ProcessRunner&& other) noexcept {
    if (this != &other) {
        if (child_pid_ > 0) {
            // terminate_and_wait → wait_for handles close_stdin internally.
            terminate_and_wait(std::chrono::seconds(5));
        }
        close_stdin();

        child_pid_         = other.child_pid_;
        stdin_fd_          = other.stdin_fd_;
        cached_exit_status_ = other.cached_exit_status_;
        exit_cached_       = other.exit_cached_;

        other.child_pid_   = -1;
        other.stdin_fd_    = -1;
        other.exit_cached_ = false;
    }
    return *this;
}

// ============================================================================
//  reap_child() — waitpid with EINTR retry
// ============================================================================

int ProcessRunner::reap_child() {
    if (child_pid_ <= 0) return -1;

    int status;
    pid_t result;
    do {
        result = ::waitpid(child_pid_, &status, 0);
    } while (result < 0 && errno == EINTR);

    child_pid_ = -1;

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

    // Create a pipe for the child's stdin.
    int pipe_fds[2];
    if (::pipe(pipe_fds) != 0) {
        return false;
    }

    // Build posix_spawn file actions:
    //   - dup2(read-end → stdin) so the child reads from the pipe
    //   - close both pipe_fds[0] and pipe_fds[1] in the child after dup2
    //     (pipe_fds[0] is the original read-end fd — no longer needed
    //      after dup2; pipe_fds[1] is the write-end — owned by the parent)
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);

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
    ::close(pipe_fds[0]);  // close the read end (parent doesn't need it)

    if (ret != 0) {
        ::close(pipe_fds[1]);
        return false;
    }

    child_pid_  = static_cast<int>(pid);
    stdin_fd_   = pipe_fds[1];
    exit_cached_ = false;

    // Enlarge the kernel pipe buffer to reduce backpressure.
    constexpr int kDesiredPipeSize = 2 * 1024 * 1024;  // 2 MiB
    (void)fcntl(stdin_fd_, F_SETPIPE_SZ, kDesiredPipeSize);

    return true;
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
        return -1;
    }

    // If is_running() already cached the exit status, use it.
    if (exit_cached_) {
        const int rc = cached_exit_status_;
        child_pid_ = -1;
        exit_cached_ = false;
        return rc;
    }

    // Ensure stdin is closed before waiting — the child may be blocked
    // reading from stdin.
    close_stdin();

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
    if (child_pid_ <= 0) return -1;

    // If is_running() already cached the exit status, use it.
    if (exit_cached_) {
        const int rc = cached_exit_status_;
        child_pid_ = -1;
        exit_cached_ = false;
        return rc;
    }

    // Ensure stdin is closed — the child may be blocked reading.
    close_stdin();

    // If timeout is zero or negative, just check once non-blocking.
    if (timeout.count() <= 0) {
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
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            if (WIFSIGNALED(status)) return -WTERMSIG(status);
            return -1;
        }
        if (result < 0) {
            if (errno == EINTR) continue;
            // Child already reaped or error.
            child_pid_ = -1;
            return -1;
        }
        // result == 0 → still running, sleep.
        if (std::chrono::steady_clock::now() >= deadline) {
            return -2;  // timeout
        }
        std::this_thread::sleep_for(kPollInterval);
    } while (true);
}

// ============================================================================
//  terminate_and_wait() — SIGTERM → wait → SIGKILL → reap
// ============================================================================

int ProcessRunner::terminate_and_wait(std::chrono::milliseconds graceful_timeout) {
    if (child_pid_ <= 0) return -1;

    // If the child already exited via is_running(), use cached status.
    if (exit_cached_) {
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
        return rc;
    }

    // Step 3: Timeout — escalate to SIGKILL.
    ::kill(child_pid_, SIGKILL);

    // Step 4: Must reap now — no timeout.
    return reap_child();
}

} // namespace chronon3d::media::video

#endif  // _WIN32
