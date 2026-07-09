#include "process_runner.hpp"
#include "process_pipe_io.hpp"
#include "process_error_handler.hpp"

// ════════════════════════════════════════════════════════════════════════════
// process_runner.cpp — ProcessOrchestration owner (FASE 5 step 3 polish).
//
//  This file owns the I/O-orchestration layer of the FASE 5 media_video
//  decomposition:
//
//    * construction  — ~ProcessRunner() (graceful shutdown).
//    * launch()      — posix_spawnp + pipe + non-blocking setup (the only
//                      fully-orchestrating method this class owns itself).
//    * move/dtor ops — move ctor + move assignment.
//
//  Every other public method is a 1-line ABI-stability trampoline that
//  delegates to one of two stateless sub-namespace helpers:
//
//    chronon3d::media::video::process_pipe_io
//      drain_stderr / consume_stderr / write_all / write_for / close_fd
//
//    chronon3d::media::video::process_error_handler
//      wait / wait_for / terminate / terminate_and_wait / is_running
//      detail::reap_child
//
//  The two sub-namespace modules live in src/media/video/ as siblings to
//  this file; their bodies are bit-identical to the pre-extraction
//  ProcessRunner::{method} bodies with `this->member` renamed to explicit
//  parameter `member`.  Anti-duplication invariants per AGENTS.md Cat-5
//  ABI-stable preserved.
// ════════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
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

// ── reap_child() (ABI-trampoline → process_error_handler::detail::reap_child).
//
//   Private member — no longer called from inside ProcessRunner.  Kept as a
//   private member so pre-extraction SO/DLL consumers that linked against
//   runner.reap_child() still resolve.
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

// ── drain_stderr() (ABI-trampoline → process_pipe_io::drain_stderr).
//
//   No longer called from inside ProcessRunner — the process_error_handler
//   callers reach process_pipe_io::drain_stderr directly.  Member symbol
//   kept for ABI-stability with pre-extraction SO/DLL consumers.
void ProcessRunner::drain_stderr() {
    process_pipe_io::drain_stderr(stderr_fd_, stderr_buffer_, kMaxStderrBytes);
}

// ── consume_stderr() (ABI-trampoline → process_pipe_io::consume_stderr).
std::string ProcessRunner::consume_stderr() noexcept {
    return process_pipe_io::consume_stderr(stderr_fd_, stderr_buffer_, kMaxStderrBytes);
}

// ── write() (ABI-trampoline → process_pipe_io::write_all).
bool ProcessRunner::write(const std::uint8_t* data, std::size_t size) {
    return process_pipe_io::write_all(stdin_fd_, data, size);
}

// ── write_for() (ABI-trampoline → process_pipe_io::write_for).
bool ProcessRunner::write_for(const std::uint8_t* data, std::size_t size,
                               std::chrono::milliseconds timeout) {
    return process_pipe_io::write_for(stdin_fd_, stderr_fd_, stderr_buffer_,
                                      kMaxStderrBytes, data, size, timeout);
}

// ── close_stdin() (ABI-trampoline → process_pipe_io::close_fd).
void ProcessRunner::close_stdin() {
    process_pipe_io::close_fd(stdin_fd_);
}

// ── wait() (ABI-trampoline → process_error_handler::wait).
int ProcessRunner::wait() {
    return process_error_handler::wait(child_pid_, cached_exit_status_, exit_cached_, stdin_fd_, stderr_fd_, stderr_buffer_, kMaxStderrBytes);
}


// ── terminate() (ABI-trampoline → process_error_handler::terminate).
void ProcessRunner::terminate() {
    process_error_handler::terminate(child_pid_);
}


// ── is_running() (ABI-trampoline → process_error_handler::is_running).
bool ProcessRunner::is_running() {
    return process_error_handler::is_running(child_pid_, cached_exit_status_, exit_cached_);
}


// ── wait_for() (ABI-trampoline → process_error_handler::wait_for).
int ProcessRunner::wait_for(std::chrono::milliseconds timeout) {
    return process_error_handler::wait_for(child_pid_, cached_exit_status_, exit_cached_, stdin_fd_, stderr_fd_, stderr_buffer_, kMaxStderrBytes, timeout);
}


// ── terminate_and_wait() (ABI-trampoline → process_error_handler::terminate_and_wait).
int ProcessRunner::terminate_and_wait(std::chrono::milliseconds graceful_timeout) {
    return process_error_handler::terminate_and_wait(child_pid_, cached_exit_status_, exit_cached_, stdin_fd_, stderr_fd_, stderr_buffer_, kMaxStderrBytes, graceful_timeout);
}


} // namespace chronon3d::media::video
