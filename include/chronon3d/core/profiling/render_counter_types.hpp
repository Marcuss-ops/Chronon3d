#pragma once

// ============================================================================
// render_counter_types.hpp — Counter structs, slot, and accessors
// ============================================================================
//
// Houses the concrete types built from the X-macros in
// render_counter_macros.hpp:
//   - DirtyFallbackSlot       (cache-line padded reason slot)
//   - RenderCountersRaw       (TLS, non-atomic accumulator)
//   - RenderCounters          (process-wide atomic store with anti-false-sharing)
//   - thread_local_counters() (TLS accessor; intentional header-only)
//   - render_counters_field_count()  (constexpr field count helper)
//   - kCounterNames           (constexpr name array for introspection)
//
// Split out of counters.hpp (FASE 21) so that consumers who only need the
// introspection helpers (e.g. render_counters_field_count / kCounterNames)
// can include this header without paying for the macro vocabulary.  The
// full entry point remains counters.hpp (single umbrella header).
// ============================================================================

#include <chronon3d/core/dirty_fallback_reason.hpp>
#include <chronon3d/core/profiling/render_counter_macros.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace chronon3d {

constexpr std::size_t kHardwareDestructiveInterferenceSize = 64;

// ---------------------------------------------------------------------------
// Raw (non-atomic) counters for thread-local accumulation.
//
// Each thread keeps its own RenderCountersRaw and increments the fields with
// plain `uint64_t` ops (no atomic RMW).  At the end of a frame, the per-thread
// raw is flushed into the global atomic RenderCounters via merge_tls().
//
// This is the key anti-false-sharing strategy: N threads doing 155 plain
// integer adds each is roughly N x 155 x ~1 ns = sub-microsecond.  The old
// approach (155 atomic fetch_adds, with cache-line bouncing between cores on
// every write) was the dominant cost in tight parallel regions.
//
// The fields are NOT individually aligned here because we never share them
// across threads: each thread has its own RenderCountersRaw.
// ---------------------------------------------------------------------------
struct RenderCountersRaw {
#define X(name) uint64_t name{0};
    CHRONON_RENDER_COUNTERS(X)
#undef X
    std::array<uint64_t, dirty_fallback_reason_count()> dirty_full_fallback_reasons{};
#define X(name) uint64_t name{0};
    CHRONON_RENDER_COUNTERS_SYSTEM(X)
    CHRONON_RENDER_COUNTERS_SETUP(X)
#undef X

    void reset() {
#define X(name) name = 0;
        CHRONON_RENDER_COUNTERS(X)
#undef X
        dirty_full_fallback_reasons.fill(0);
#define X(name) name = 0;
        CHRONON_RENDER_COUNTERS_SYSTEM(X)
        CHRONON_RENDER_COUNTERS_SETUP(X)
#undef X
    }
};

// Each slot occupies its own 64-byte cache line so that bumping one
// reason does not cause false sharing with another reason.
struct alignas(kHardwareDestructiveInterferenceSize) DirtyFallbackSlot {
    std::atomic<uint64_t> value{0};
};

// ---------------------------------------------------------------------------
// RenderCounters — the global, process-wide counter store.
//
// Every field is alignas(64) (one cache line) so that concurrent writers
// from different cores do not share a cache line.  This eliminates the
// "false sharing" pathology where 4 cores updating 4 different 8-byte
// atomics all ping-pong the same 64-byte line through L1 / L2 / L3.
//
// The fields are grouped (RENDER / SYSTEM / SETUP) so that a hot field
// (e.g. `pixels_touched`) does not share a cache line with a cold field
// (e.g. `system_ram_total_mb`).  The grouping is purely for locality —
// each individual field still gets its own 64-byte slot.
//
// For high-frequency counters in tight parallel loops, prefer the
// thread-local pattern: per-thread plain `++` on `thread_local_counters()`,
// then a single `merge_tls()` per thread at the end of the frame.
// ---------------------------------------------------------------------------
struct RenderCounters {
    // Hot counters — written by render hot loops on every frame.
#define X(name) alignas(kHardwareDestructiveInterferenceSize) std::atomic<uint64_t> name{0};
    CHRONON_RENDER_COUNTERS(X)
#undef X

    // The dirty-fallback reason array: each slot is individually cache-line
    // padded so bumping one reason does not invalidate the cache line of another.
    std::array<DirtyFallbackSlot, dirty_fallback_reason_count()>
        dirty_full_fallback_reasons{};

    // System / setup counters — written infrequently (sampler tick or
    // once per frame).  They are also given 64-byte slots so that the
    // "thundering herd" of render threads writing hot counters does not
    // bounce these cold lines through L1.
#define X(name) alignas(kHardwareDestructiveInterferenceSize) std::atomic<uint64_t> name{0};
    CHRONON_RENDER_COUNTERS_SYSTEM(X)
    CHRONON_RENDER_COUNTERS_SETUP(X)
#undef X

    // ── Non-resettable metadata counters ──────────────────────────────
    // These are set once by set_settings() and survive reset() calls.
    // They are NOT part of any X-macro so reset() and merge_tls() skip them.
    alignas(kHardwareDestructiveInterferenceSize) std::atomic<uint64_t> program_cache_capacity{0};
    // 1 = auto-tuning enabled, 0 = fixed.
    alignas(kHardwareDestructiveInterferenceSize) std::atomic<uint64_t> program_cache_tune{0};

    RenderCounters() = default;

private:
    template <typename T>
    void copy_all_fields_from(T& other) {
#define X(name) name.store(other.name.load(std::memory_order_relaxed), std::memory_order_relaxed);
        CHRONON_RENDER_COUNTERS(X)
#undef X
        for (std::size_t i = 0; i < dirty_fallback_reason_count(); ++i) {
            dirty_full_fallback_reasons[i].value.store(
                other.dirty_full_fallback_reasons[i].value.load(std::memory_order_relaxed),
                std::memory_order_relaxed
            );
        }
#define X(name) name.store(other.name.load(std::memory_order_relaxed), std::memory_order_relaxed);
        CHRONON_RENDER_COUNTERS_SYSTEM(X)
        CHRONON_RENDER_COUNTERS_SETUP(X)
#undef X
        // Non-resettable metadata counters (outside X-macros):
        program_cache_capacity.store(other.program_cache_capacity.load(std::memory_order_relaxed), std::memory_order_relaxed);
        program_cache_tune.store(other.program_cache_tune.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

public:
    RenderCounters(RenderCounters&& other) noexcept {
        copy_all_fields_from(other);
    }

    RenderCounters& operator=(RenderCounters&& other) noexcept {
        if (this != &other) {
            copy_all_fields_from(other);
        }
        return *this;
    }

    RenderCounters(const RenderCounters& other) {
        copy_all_fields_from(other);
    }

    RenderCounters& operator=(const RenderCounters& other) {
        if (this != &other) {
            copy_all_fields_from(other);
        }
        return *this;
    }

    void reset() {
#define X(name) name.store(0, std::memory_order_relaxed);
        CHRONON_RENDER_COUNTERS(X)
#undef X
#define X(name) name.store(0, std::memory_order_relaxed);
        CHRONON_RENDER_COUNTERS_SYSTEM(X)
        CHRONON_RENDER_COUNTERS_SETUP(X)
#undef X
        for (auto& reason : dirty_full_fallback_reasons) {
            reason.value.store(0, std::memory_order_relaxed);
        }
    }

    void increment_dirty_full_fallback_reason(DirtyFallbackReason reason) {
        const auto index = static_cast<std::size_t>(reason);
        if (index < dirty_fallback_reason_count()) {
            dirty_full_fallback_reasons[index].value.fetch_add(1, std::memory_order_relaxed);
        }
    }

    /// Merge thread-local (non-atomic) raw counters into this atomic instance.
    /// Use at the end of each frame: threads accumulate into thread-local
    /// RenderCountersRaw, then call this once to flush into the global atomics.
    /// This replaces N atomic fetch_add calls per counter with a single pass.
    ///
    /// This function is the "anti-false-sharing" producer: it is the only
    /// place where thread-local writes cross into the shared atomic domain.
    /// The number of cross-domain transactions is O(num_threads) per frame,
    /// not O(num_threads * num_counters).
    void merge_tls(const RenderCountersRaw& tls) {
#define X(name) name.fetch_add(tls.name, std::memory_order_relaxed);
        CHRONON_RENDER_COUNTERS(X)
#undef X
        for (std::size_t i = 0; i < dirty_fallback_reason_count(); ++i) {
            dirty_full_fallback_reasons[i].value.fetch_add(
                tls.dirty_full_fallback_reasons[i], std::memory_order_relaxed);
        }
#define X(name) name.fetch_add(tls.name, std::memory_order_relaxed);
        CHRONON_RENDER_COUNTERS_SYSTEM(X)
        CHRONON_RENDER_COUNTERS_SETUP(X)
#undef X
    }
};

// ---------------------------------------------------------------------------
// Thread-local counter accessor.
//
// Each thread sees its own RenderCountersRaw instance, lazily initialised
// on first use.  Workers should increment fields on this local with plain
// `++` (no atomic op, no cache-line bouncing), then call merge_tls() once
// per frame on the global RenderCounters.
//
// Usage:
//   // In the render hot path:
//   auto& c = thread_local_counters();
//   c.pixels_touched += 1;
//   c.composite_pixels += w * h;
//
//   // At end of frame (once per thread, on the worker itself):
//   global_counters.merge_tls(c);
//   c.reset();   // optional — if you want to keep the same thread-local
//                // across frames, reset after the merge.  Otherwise the
//                // counters will accumulate across frames and the merge
//                // will keep growing — fine for a single render, not for
//                // continuous runs.
//
// This accessor is intentionally in this header (not a separate .cpp) so
// the compiler can fully inline `thread_local_counters()` at every call
// site: a single TLS load + a single pointer return.
// ---------------------------------------------------------------------------
inline RenderCountersRaw& thread_local_counters() {
    thread_local RenderCountersRaw tls;
    return tls;
}

/// Number of fields in RenderCounters / RenderCountersRaw.
/// Used by tests / benchmarks to compute throughput.
///
/// Implementation: RenderCountersRaw is a POD struct whose only members are
/// `uint64_t` (X-macro generated) plus the `dirty_full_fallback_reasons`
/// array of `uint64_t`.  `sizeof(RenderCountersRaw) / sizeof(uint64_t)`
/// therefore gives the exact total number of uint64_t fields including the
/// array slots, which is what the tests / benchmarks want.
constexpr std::size_t render_counters_field_count() {
    return sizeof(RenderCountersRaw) / sizeof(uint64_t);
}

/// Constexpr array of counter field names for logging, introspection, and
/// debug output.  Affiancato (non sostitutivo) alla X-macro per non rompere
/// l'allineamento anti false-sharing dei campi struct.
/// Uses CTAD (C++17) to let the compiler count elements automatically,
/// avoiding size mismatches with dirty_fallback_reasons array slots.
constexpr std::array kCounterNames = {
#define X(name) std::string_view{#name},
    CHRONON_RENDER_COUNTERS(X)
    CHRONON_RENDER_COUNTERS_SYSTEM(X)
    CHRONON_RENDER_COUNTERS_SETUP(X)
#undef X
};

} // namespace chronon3d
