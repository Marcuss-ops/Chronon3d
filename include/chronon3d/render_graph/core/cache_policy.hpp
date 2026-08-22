#pragma once

#include <cstdint>
#include <string_view>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>

namespace chronon3d::graph {

enum class TemporalClass : std::uint8_t {
    // ── Legacy values (cache-key generation) ──────────────────────────
    Pure,          // static: output never changes
    TimeDependent, // generic per-frame dependency
    Stateful,      // state-dependent across frames (content version)

    // ── Fase B — granular compile-time classification ─────────────────
    // These five values are the canonical output of the compiler's
    // temporal analysis pass.  Only Static nodes are eligible for
    // maximal static island baking.  The legacy values are preserved
    // for backward compatibility with the cache-key layer.
    Static,            // never changes (≡ Pure at cache level)
    TransformDynamic,  // only spatial transform changes per frame
    ParameterDynamic,  // per-frame parameters (opacity, effects) change
    ContentDynamic,    // content changes per frame (text, source)
    ExternalDynamic,   // external frame dependency (video decoder, NVDEC)
};

/// Returns true when the class is static (no frame dependency).
[[nodiscard]] constexpr bool is_static(TemporalClass tc) noexcept {
    return tc == TemporalClass::Pure || tc == TemporalClass::Static;
}

/// Returns true when the class is any kind of dynamic.
[[nodiscard]] constexpr bool is_dynamic(TemporalClass tc) noexcept {
    return tc != TemporalClass::Pure && tc != TemporalClass::Static;
}

/// Human-readable label for diagnostic/gate output.
[[nodiscard]] inline const char* to_string(TemporalClass tc) noexcept {
    switch (tc) {
        case TemporalClass::Pure:              return "Pure";
        case TemporalClass::TimeDependent:     return "TimeDependent";
        case TemporalClass::Stateful:          return "Stateful";
        case TemporalClass::Static:            return "Static";
        case TemporalClass::TransformDynamic:  return "TransformDynamic";
        case TemporalClass::ParameterDynamic:  return "ParameterDynamic";
        case TemporalClass::ContentDynamic:    return "ContentDynamic";
        case TemporalClass::ExternalDynamic:   return "ExternalDynamic";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// CacheMode — single canonical axis for the cache contract
// ---------------------------------------------------------------------------
//
// fields. Every node now describes its cache behaviour *exclusively* via this
// enum, exposed through `RenderNodeCachePolicy::mode`. The combinations below
// are exhaustive: by collapsing onto one enum, callers cannot construct
// contradictory states (e.g. "disabled but reusable across frames") that the
// old 4-flag struct permitted.
// ---------------------------------------------------------------------------
enum class CacheMode : std::uint8_t {
    /// Caching is disabled — node re-executes for every consumer call.
    /// `reusable_across_frames` and `persistent` are both false and undefined.
    Disabled,

    /// Output changes per frame; cache key must include the frame number.
    /// Multiple consumers within the same render frame may still dedupe.
    FrameVariant,

    /// Output is invariant across frames; entries live in memory only.
    /// Survives across frames within a single composition run.
    FrameInvariantMemory,
};

// ---------------------------------------------------------------------------
// CacheInvalidation — orthogonal axis for invalidation triggers
// ---------------------------------------------------------------------------
enum class CacheInvalidation {
    Always,              ///< re-execute unconditionally (used with Disabled)
    WhenParamsChange,    ///< re-execute when node parameters change
    WhenInputsChange,    ///< re-execute when any input node produces a new output
};

// ---------------------------------------------------------------------------
// RenderNodeCachePolicy — immutable single-axis descriptor
// ---------------------------------------------------------------------------
struct RenderNodeCachePolicy {
    CacheMode mode{CacheMode::FrameVariant};
    CacheInvalidation invalidation{CacheInvalidation::WhenInputsChange};
    std::string_view reason{"default"};
    TemporalClass temporal_class{TemporalClass::Pure};

    [[nodiscard]] constexpr bool enabled() const noexcept {
        return mode != CacheMode::Disabled;
    }

    [[nodiscard]] constexpr bool frame_dependent() const noexcept {
        return mode == CacheMode::FrameVariant;
    }

    [[nodiscard]] constexpr bool reusable_across_frames() const noexcept {
        return mode == CacheMode::FrameInvariantMemory;
    }
};

/// Build the temporal part of a cache key without coupling reuse to the
/// output frame number.  A video node can pass its sampled source time here,
/// so two output frames sampling the same source frame share the key.
[[nodiscard]] inline TemporalSampleKey temporal_key_for(
    TemporalClass temporal_class,
    const SampleTime& sampled_time,
    EvaluationVersion content_version = 0) noexcept {
    if (temporal_class == TemporalClass::Pure) {
        return TemporalSampleKey{Frame{0}, 0, content_version};
    }
    auto key = make_temporal_key(
        sampled_time,
        temporal_class == TemporalClass::Stateful ? content_version : 0);
    return key;
}

/// Canonical frame component for a node cache key.
[[nodiscard]] constexpr Frame cache_frame_for_policy(
    const RenderNodeCachePolicy& policy,
    Frame evaluated_frame,
    Frame explicit_frame = Frame{-1}) noexcept {
    if (!policy.frame_dependent()) return Frame{0};
    return explicit_frame.integral() >= 0 ? explicit_frame : evaluated_frame;
}

// ---------------------------------------------------------------------------
// Canonical factory helpers (constexpr noexcept, string_view reason)
// ---------------------------------------------------------------------------
constexpr RenderNodeCachePolicy no_cache(std::string_view reason) noexcept {
    return RenderNodeCachePolicy{
        .mode = CacheMode::Disabled,
        .invalidation = CacheInvalidation::Always,
        .reason = reason,
        .temporal_class = TemporalClass::Pure,
    };
}

constexpr RenderNodeCachePolicy frame_variant_cache(std::string_view reason) noexcept {
    return RenderNodeCachePolicy{
        .mode = CacheMode::FrameVariant,
        .invalidation = CacheInvalidation::WhenInputsChange,
        .reason = reason,
        .temporal_class = TemporalClass::TimeDependent,
    };
}

constexpr RenderNodeCachePolicy static_memory_cache(std::string_view reason) noexcept {
    return RenderNodeCachePolicy{
        .mode = CacheMode::FrameInvariantMemory,
        .invalidation = CacheInvalidation::WhenParamsChange,
        .reason = reason,
        .temporal_class = TemporalClass::Pure,
    };
}

} // namespace chronon3d::graph
