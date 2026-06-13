#pragma once

// ── Effect Execution Traits ─────────────────────────────────────────────────
//
// Trait enums and structs that describe how an effect behaves at runtime:
//   - EffectDomain   : what kind of data the effect operates on
//   - AlphaPolicy    : how the effect handles the alpha channel
//   - DirtyPolicy    : how the effect expands the dirty (changed) region
//   - BoundsPolicy   : how the effect expands the bounding box
//
// These are STATIC traits — they are the same for all instances of a given
// effect type regardless of parameter values.
//
// For parameter-dependent spatial properties (e.g., blur radius, offset
// distance), use EffectFootprint which is computed at runtime.

#include <cstdint>

namespace chronon3d::effects {

// ── EffectDomain ────────────────────────────────────────────────────────────
// Broad categorisation of the computational domain.

enum class EffectDomain : uint8_t {
    Color,       // Per-pixel colour operation (independent of neighbours)
    Spatial,     // Reads neighbouring pixels (blur, offset, etc.)
    Generate,    // Generates new content (noise, fill solid, etc.)
    Composite,   // Combines multiple inputs or buffers
};

// ── AlphaPolicy ─────────────────────────────────────────────────────────────
// How the effect treats the alpha channel.

enum class AlphaPolicy : uint8_t {
    Preserve,    // Alpha is guaranteed unchanged
    MayModify,   // Alpha may be modified by the effect
    Generate,    // The effect produces its own alpha (e.g. stroke)
};

// ── DirtyPolicy ─────────────────────────────────────────────────────────────
// How the effect's dirty (changed) region relates to the input dirty region.
// Used for partial / dirty-rectangle rendering decisions.

enum class DirtyPolicy : uint8_t {
    Preserve,    // Dirty region unchanged
    Expand,      // Dirty region expands by a parameter-dependent margin
    Translate,   // Dirty region shifts by a parameter-dependent offset
    FullFrame,   // Always dirties the entire framebuffer
};

// ── BoundsPolicy ────────────────────────────────────────────────────────────
// How the effect's output bounding box relates to the input bounding box.

enum class BoundsPolicy : uint8_t {
    Preserve,    // Bounding box unchanged
    Expand,      // Bounding box expands by a parameter-dependent margin
    Translate,   // Bounding box shifts by a parameter-dependent offset
    FullFrame,   // Output always fills the entire framebuffer
};

// ── EffectExecutionTraits ───────────────────────────────────────────────────
// Static traits carried by each effect type (same for all instances).

struct EffectExecutionTraits {
    EffectDomain  domain{EffectDomain::Spatial};
    AlphaPolicy   alpha_policy{AlphaPolicy::MayModify};
    DirtyPolicy   dirty_policy{DirtyPolicy::FullFrame};
    BoundsPolicy  bounds_policy{BoundsPolicy::FullFrame};

    bool in_place{false};            // Can safely operate in-place on the source buffer
    bool temporal{false};            // Varies per frame (e.g. animated)
    bool deterministic{true};        // Same input → same output every time
    bool fusible_color{false};       // Can be fused into a color pipeline
    bool needs_source_copy{true};    // Requires a separate source snapshot
};

// ── EffectFootprint ─────────────────────────────────────────────────────────
// Parameter-dependent spatial information, computed at runtime per-instance.

struct EffectFootprint {
    int  left{0};
    int  top{0};
    int  right{0};
    int  bottom{0};
    bool full_frame{false};
};

// ── Helper: resolve footprint from type + params ────────────────────────────
// Default implementation returns a zero margin.  Effect-specific overloads are
// expected in the respective implementation files.

[[nodiscard]] inline EffectFootprint resolve_effect_footprint(
    EffectType /*type*/) {
    return EffectFootprint{};
}

} // namespace chronon3d::effects
