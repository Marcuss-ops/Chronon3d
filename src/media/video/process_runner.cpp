#include "process_runner.hpp"
#include "process_pipe_io.hpp"

// ── Linux / POSIX subprocess runner using posix_spawnp ───────────────────

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include "process_error_handler.hpp"

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

// Process-error-handler extract (FASE 5 step 2): body of ProcessRunner::reap_child() moved verbatim into process_error_handler::detail::reap_child() with `this->member_` -> explicit parameter rename.  Kept here as a 1-line trampoline for ABI-safety (binary consumers that linked against the pre-extraction symbols still resolve).
int ProcessRunner::reap_child() {
    return process_error_handler::detail::reap_child(child_pid_, stderr_fd_, stderr_buffer_, kMaxStderrBytes);
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

    // Set stdin write-end to non-blocking so that write_for() can honour
    // the deadline via poll() without blocking indefinitely on a full pipe.
    const int stdin_fl = ::fcntl(stdin_fd_, F_GETFL);
    if (stdin_fl >= 0) {
        ::fcntl(stdin_fd_, F_SETFL, stdin_fl | O_NONBLOCK);
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
    // Pipe-IO extract (FASE 5 step 1): body moved verbatim into
    // process_pipe_io::drain_stderr with `this->member` → parameter rename.
    process_pipe_io::drain_stderr(stderr_fd_, stderr_buffer_, kMaxStderrBytes);
}

// ============================================================================
//  consume_stderr() — return captured stderr and clear the buffer
// ============================================================================

std::string ProcessRunner::consume_stderr() noexcept {
    // Pipe-IO extract (FASE 5 step 1): body moved verbatim; helper internally
    // calls drain_stderr and then swap-and-returns in one step.
    return process_pipe_io::consume_stderr(stderr_fd_, stderr_buffer_, kMaxStderrBytes);
}

// ============================================================================
//  write() — raw fd write with EINTR retry
// ============================================================================

bool ProcessRunner::write(const std::uint8_t* data, std::size_t size) {
    // Pipe-IO extract (FASE 5 step 1): body moved verbatim into
    // process_pipe_io::write_all with `stdin_fd_` → `fd` parameter rename.
    return process_pipe_io::write_all(stdin_fd_, data, size);
}

// ============================================================================
//  write_for() — poll-based write with deadline + stderr drain
// ============================================================================

bool ProcessRunner::write_for(const std::uint8_t* data, std::size_t size,
                               std::chrono::milliseconds timeout) {
    // Pipe-IO extract (FASE 5 step 1): body moved verbatim into
    // process_pipe_io::write_for with all member references → explicit
    // parameters (stdin_fd_, stderr_fd_, stderr_buffer_, kMaxStderrBytes).
    return process_pipe_io::write_for(stdin_fd_, stderr_fd_, stderr_buffer_,
                                      kMaxStderrBytes, data, size, timeout);
}

// ============================================================================
//  close_stdin() — signal EOF to the child
// ============================================================================

void ProcessRunner::close_stdin() {
    // Pipe-IO extract (FASE 5 step 1): body moved verbatim into
    // process_pipe_io::close_fd; the sentinel-reset semantics are preserved.
    process_pipe_io::close_fd(stdin_fd_);
}

// ============================================================================
//  wait() — collect child exit status (with EINTR retry)
// ============================================================================

// Process-error-handler extract (FASE 5 step 2): body of ProcessRunner::wait() moved verbatim into process_error_handler::wait() with `this->member_` -> explicit parameter rename.  Kept here as a 1-line trampoline for ABI-safety (binary consumers that linked against the pre-extraction symbols still resolve).
int ProcessRunner::wait() {
    return process_error_handler::wait(child_pid_, cached_exit_status_, exit_cached_, stdin_fd_, stderr_fd_, stderr_buffer_, kMaxStderrBytes);
}


// ============================================================================
//  terminate() — SIGTERM the child
// ============================================================================

// Process-error-handler extract (FASE 5 step 2): body of ProcessRunner::terminate() moved verbatim into process_error_handler::terminate() with `this->member_` -> explicit parameter rename.  Kept here as a 1-line trampoline for ABI-safety (binary consumers that linked against the pre-extraction symbols still resolve).
void ProcessRunner::terminate() {
    process_error_handler::terminate(child_pid_);
}


// ============================================================================
//  is_running() — non-blocking check; caches exit status for wait()
// ============================================================================

// Process-error-handler extract (FASE 5 step 2): body of ProcessRunner::is_running() moved verbatim into process_error_handler::is_running() with `this->member_` -> explicit parameter rename.  Kept here as a 1-line trampoline for ABI-safety (binary consumers that linked against the pre-extraction symbols still resolve).
bool ProcessRunner::is_running() {
    return process_error_handler::is_running(child_pid_, cached_exit_status_, exit_cached_);
}


// ============================================================================
//  wait_for() — block up to `timeout` for child to exit
// ============================================================================

// Process-error-handler extract (FASE 5 step 2): body of ProcessRunner::wait_for() moved verbatim into process_error_handler::wait_for() with `this->member_` -> explicit parameter rename.  Kept here as a 1-line trampoline for ABI-safety (binary consumers that linked against the pre-extraction symbols still resolve).
int ProcessRunner::wait_for(std::chrono::milliseconds timeout) {
    return process_error_handler::wait_for(child_pid_, cached_exit_status_, exit_cached_, stdin_fd_, stderr_fd_, stderr_buffer_, kMaxStderrBytes, timeout);
}


// ============================================================================
//  terminate_and_wait() — SIGTERM → wait → SIGKILL → reap
// ============================================================================

// Process-error-handler extract (FASE 5 step 2): body of ProcessRunner::terminate_and_wait() moved verbatim into process_error_handler::terminate_and_wait() with `this->member_` -> explicit parameter rename.  Kept here as a 1-line trampoline for ABI-safety (binary consumers that linked against the pre-extraction symbols still resolve).
int ProcessRunner::terminate_and_wait(std::chrono::milliseconds graceful_timeout) {
    return process_error_handler::terminate_and_wait(child_pid_, cached_exit_status_, exit_cached_, stdin_fd_, stderr_fd_, stderr_buffer_, kMaxStderrBytes, graceful_timeout);
}


} // namespace chronon3d::media::video
