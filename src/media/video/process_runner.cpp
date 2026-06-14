#include "process_runner.hpp"

#if defined(_WIN32)

// ── Windows stubs ───────────────────────────────────────────────────────────
// CreateProcessW path not yet implemented.
// TODO(codex/ffmpeg-process-runner): implement CreateProcessW with stdin pipe.

#include <cstdio>

namespace chronon3d::media::video {

ProcessRunner::~ProcessRunner() noexcept {
    close_stdin();
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
            terminate();
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

bool ProcessRunner::launch(const std::string&, const std::vector<std::string>&) {
    std::fprintf(stderr, "[ProcessRunner] Windows CreateProcessW not yet implemented\n");
    return false;
}
bool ProcessRunner::write(const std::uint8_t*, std::size_t) { return false; }
void ProcessRunner::close_stdin() {}
int  ProcessRunner::wait() { return -1; }
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
#include <unistd.h>

// POSIX requires `environ` for posix_spawnp.
extern char** environ;

namespace chronon3d::media::video {

// ============================================================================
//  Construction / Destruction / Move
// ============================================================================

ProcessRunner::~ProcessRunner() noexcept {
    if (child_pid_ > 0) {
        terminate();
        int status;
        waitpid(child_pid_, &status, 0);  // blocks until child exits
        child_pid_ = -1;
    }
    close_stdin();
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
            terminate();
            int status;
            waitpid(child_pid_, &status, 0);
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

    // Build posix_spawn file actions: dup2(read-end → stdin), close write-end.
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[0], STDIN_FILENO);
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
//  wait() — collect child exit status
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

    int status;
    const pid_t result = waitpid(child_pid_, &status, 0);
    if (result < 0) {
        return -1;
    }

    child_pid_ = -1;

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return -WTERMSIG(status);
    }
    return -1;
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
    const pid_t result = waitpid(child_pid_, &status, WNOHANG);
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

} // namespace chronon3d::media::video

#endif  // _WIN32
