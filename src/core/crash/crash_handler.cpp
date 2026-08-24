// ---------------------------------------------------------------------------
// src/core/crash/crash_handler.cpp — cpptrace-based fatal crash handler
//
// Installs signal handlers for SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL.
// On crash, writes a structured report to stderr with:
//   - CHRONON FATAL header
//   - Signal name + crash context (thread, job, composition, backend, frame)
//   - Git commit SHA (compile-time)
//   - Resolved stacktrace via cpptrace
//
// Signal-handler safety rules:
//   1. write() only for output — no fprintf, no spdlog, no iostream.
//   2. cpptrace::generate_raw_trace() is signal-safe (captures addresses only).
//   3. Resolve + format happens after capture — allocates but we're dying.
//   4. _exit(1) — no atexit hooks, no C++ destructors, no std::exit.
//   5. Thread-local CrashContext is read-only from the handler perspective.
// ---------------------------------------------------------------------------

#include "crash_handler.hpp"

#include <cpptrace/cpptrace.hpp>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

// Compile-time git SHA (injected via CMake target_compile_definitions).
#ifndef CHRONON3D_GIT_COMMIT
#define CHRONON3D_GIT_COMMIT "unknown"
#endif

namespace chronon3d::crash {
namespace {

// ── Thread-local crash context ──────────────────────────────────────────

thread_local const CrashContext* tls_crash_ctx = nullptr;

// ── Signal-name lookup (no allocations) ─────────────────────────────────

const char* signal_name(int sig) noexcept {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGFPE:  return "SIGFPE";
        case SIGBUS:  return "SIGBUS";
        case SIGILL:  return "SIGILL";
        case SIGTERM: return "SIGTERM";
        case SIGINT:  return "SIGINT";
        default:      return "UNKNOWN";
    }
}

// ── Async-signal-safe write helpers ──────────────────────────────────────

void safe_write_str(const char* s) noexcept {
    if (!s) return;
    ::write(STDERR_FILENO, s, std::strlen(s));
}

void safe_write_str_n(const char* s, std::size_t n) noexcept {
    if (!s) return;
    ::write(STDERR_FILENO, s, n);
}

void safe_write_uint32(std::uint32_t n) noexcept {
    if (n == 0) {
        safe_write_str("0");
        return;
    }
    char buf[12];
    int pos = 0;
    while (n > 0) {
        buf[pos++] = static_cast<char>('0' + (n % 10));
        n /= 10;
    }
    for (int i = 0; i < pos / 2; ++i) {
        char tmp = buf[i];
        buf[i] = buf[pos - 1 - i];
        buf[pos - 1 - i] = tmp;
    }
    buf[pos] = '\0';
    safe_write_str(buf);
}

// ── Crash report writer ─────────────────────────────────────────────────

// Single-install guard.
std::atomic<bool> s_installed{false};

void write_crash_report(int sig, const CrashContext* ctx) noexcept {
    safe_write_str("\n"
                   "══════════════════════════════════════════════════════════\n"
                   "  CHRONON FATAL\n"
                   "══════════════════════════════════════════════════════════\n"
                   "  signal: ");
    safe_write_str(signal_name(sig));
    safe_write_str("\n");

    if (ctx) {
        if (ctx->thread_name) {
            safe_write_str("  thread: ");
            safe_write_str(ctx->thread_name);
            safe_write_str("\n");
        }
        if (ctx->job_id) {
            safe_write_str("  job_id: ");
            safe_write_str(ctx->job_id);
            safe_write_str("\n");
        }
        if (ctx->composition_id) {
            safe_write_str("  composition_id: ");
            safe_write_str(ctx->composition_id);
            safe_write_str("\n");
        }
        if (ctx->backend) {
            safe_write_str("  backend: ");
            safe_write_str(ctx->backend);
            safe_write_str("\n");
        }
        safe_write_str("  frame: ");
        safe_write_uint32(ctx->frame);
        safe_write_str("\n");
    }

    safe_write_str("  git_sha: " CHRONON3D_GIT_COMMIT "\n");

    safe_write_str("──────────────────────────────────────────────────────────\n"
                   "  Stack trace:\n\n");

    // Capture raw addresses (signal-safe), then resolve (allocs OK, dying).
    auto raw_trace = cpptrace::generate_raw_trace(/*skip=*/2);
    const auto trace = raw_trace.resolve();
    std::string formatted = trace.to_string(/*color=*/false);
    safe_write_str_n(formatted.data(), formatted.size());

    safe_write_str("\n══════════════════════════════════════════════════════════\n"
                   "  Process terminated.\n"
                   "══════════════════════════════════════════════════════════\n\n");

    _exit(1);
}

extern "C" void crash_signal_handler(int sig) noexcept {
    const auto* ctx = tls_crash_ctx;
    write_crash_report(sig, ctx);
}

} // anonymous namespace

// ── Public API ───────────────────────────────────────────────────────────

void set_crash_context(const CrashContext* ctx) noexcept {
    tls_crash_ctx = ctx;
}

void install() noexcept {
    bool expected = false;
    if (!s_installed.compare_exchange_strong(expected, true)) {
        return;
    }

    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = crash_signal_handler;
    sa.sa_flags   = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
}

} // namespace chronon3d::crash