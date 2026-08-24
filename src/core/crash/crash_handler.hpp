// ---------------------------------------------------------------------------
// src/core/crash/crash_handler.hpp — Crash handler public API
// ---------------------------------------------------------------------------
#pragma once

#include <chronon3d/core/config.hpp>

#include <cstdint>

namespace chronon3d::crash {

/// Lightweight crash context — set before work, cleared after.
/// All fields are plain C-strings so the signal handler can read them
/// without touching C++ containers (thread_local pointer, read-only).
struct CrashContext {
    const char* thread_name{nullptr};    // e.g. "render-worker-3"
    const char* job_id{nullptr};         // e.g. "18482"
    const char* composition_id{nullptr}; // e.g. "intro-93"
    const char* backend{nullptr};        // e.g. "vulkan"
    std::uint32_t frame{0};              // e.g. 414
};

/// Set the crash context for the current thread.
/// Thread-safe: uses thread_local storage.  Pass nullptr to clear.
void set_crash_context(const CrashContext* ctx) noexcept;

/// One-time install of fatal signal handlers (SIGSEGV, SIGABRT, SIGFPE,
/// SIGBUS, SIGILL).  Safe to call multiple times — subsequent calls are no-ops.
///
/// On crash, the handler writes a structured "CHRONON FATAL" report to stderr
/// including the current CrashContext (if set), git SHA, and a resolved
/// stacktrace via cpptrace.  The process is then terminated.
///
/// The handler is intentionally minimal inside the signal path:
///   - write() for output (async-signal-safe)
///   - cpptrace::generate_raw_trace() (signal-safe address capture)
///   - Resolve + print after capture (allocates but we're about to die)
///   - _exit(1) — no atexit, no destructors
void install() noexcept;

} // namespace chronon3d::crash