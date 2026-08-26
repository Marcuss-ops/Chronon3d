#pragma once

#include <chronon3d/core/dirty_tile_mask.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/runtime/dirty_history.hpp>

#include "scene_fingerprint.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <functional>

namespace chronon3d::graph::detail {

enum LayerDeltaChange : std::uint32_t {
    LayerAdded = 1u << 0,
    LayerRemoved = 1u << 1,
    LayerVisibility = 1u << 2,
    LayerGeometry = 1u << 3,
    LayerContent = 1u << 4,
    // Additive semantic bits.  The first five values above are retained for
    // compatibility with existing dirty-region consumers.
    LayerStructure = 1u << 5,
    LayerCamera = 1u << 6,
    LayerPosition = 1u << 7,
    LayerOpacity = 1u << 8,
    LayerText = 1u << 9,
    LayerColor = 1u << 10,
    LayerImage = 1u << 11,
    LayerEffects = 1u << 12,
    LayerVideoSource = 1u << 13,
};

struct LayerDelta {
    std::string instance_id;
    std::uint32_t change_mask{0};
    raster::BBox old_bounds{};
    raster::BBox new_bounds{};
};

/// Canonical input bundle for the previous frame (the "PreviousFrameState"
/// half of the delta compiler contract).  Every prev/current comparison in
/// the runtime must flow through FrameDeltaCompiler; no pipeline phase
/// computes its own diff of these fields.
struct FrameStateSnapshot {
    Frame frame{};

    // Per-layer bbox + semantic fingerprints.  May be EMPTY in the early
    // (pre-resolve) phase where only scene-level fingerprints are available.
    std::unordered_map<std::string, LayerBBoxState> layers;

    // Scene-level fingerprints (see scene_fingerprint.hpp).  Zero-invalid
    // when the producer has no history / hashing is not available.
    FrameFingerprints fingerprints{};
    bool fingerprints_valid{false};

    // Camera at the state's frame (by value; use camera_valid to gate).
    Camera2_5D camera{};
    bool camera_valid{false};

    // Scene properties consumed by the reuse gate.
    bool has_projected_surface{false};
    // Whether a previous framebuffer exists with the same width/height.
    bool has_previous_surface{false};
    // Whether the scene content is static across frame indices.
    bool scene_is_static{false};

    // True only when `layers` contains the authoritative current/previous
    // layer state. Early scene-fingerprint resolution runs before layer
    // resolution and must not infer dynamic-frame reuse from incomplete data.
    bool layer_state_complete{true};
};

/// Reuse analysis of PreviousFrameState vs CurrentFrameState.  This is the
/// SINGLE authority for "can we skip work" — the execution resolver and the
/// graph-cache coordinator consume these flags instead of re-comparing
/// fingerprints themselves.
struct FrameReuseEligibility {
    // Fast path #1: resolved-scene reuse (combined fingerprint identical +
    // camera unchanged + previous surface available + no projected layers
    // + sequential frame adjacency).
    bool resolved_scene_reuse{false};

    // Fast path #2: static-scene reuse (structure/static/active-at
    // fingerprints all unchanged + camera unchanged + eligible frame window).
    bool static_scene_reuse{false};

    // Graph topology unchanged — the compiled-graph cache may be refreshed
    // instead of rebuilt.
    bool structure_unchanged{false};

    // Camera identical between the two states (policy from
    // camera_change_policy.hpp, exposed here as the single sanctioned entry).
    bool camera_unchanged{true};

    // Stable snake_case token describing WHY reuse was rejected (first
    // failing gate). "" when at least one reuse path is available.
    std::string_view reason;
};

/// Result of the canonical current-vs-previous layer analysis.
struct FrameDeltaCompileOptions {
    // Conservative first-frame/projected-surface fallback. The compiler owns
    // the resulting bounds and tile mask so callers cannot diverge.
    bool force_full_frame{false};

    // Optional policy-selected damage replacement (for example the exposed
    // strip after a valid framebuffer scroll). It is normalized here and
    // applied consistently to both dirty_bounds and dirty_tiles.
    std::optional<raster::BBox> dirty_bounds_override;

    // Large damage is cheaper and safer as one full-frame execution. A value
    // <= 0 disables this normalization; the default is 50% of the canvas.
    double full_frame_threshold{0.5};

    // Optional per-layer spatial spread in pixels. The callback is evaluated
    // only for changed layers and lets predictable blur/glow damage expand
    // before bounds and tiles are emitted. No effect policy is duplicated here.
    std::function<double(std::string_view)> spatial_spread;
};

struct FrameDelta {
    Frame frame{};
    std::vector<LayerDelta> changes;
    std::optional<raster::BBox> dirty_bounds;
    bool full_frame_dirty{false};
    std::optional<raster::DirtyTileMask> dirty_tiles;

    // Reuse eligibility derived from the same prev/current states.
    FrameReuseEligibility reuse{};

    // Frame-level semantic flags.  These fields are intentionally additive
    // to the original bounds-only result and are derived solely from changes.
    bool scene_changed{false};
    bool camera_changed{false};
    bool structure_changed{false};
    bool geometry_changed{false};
    bool content_changed{false};
    bool visibility_changed{false};
    bool position_changed{false};
    bool opacity_changed{false};
    bool text_changed{false};
    bool color_changed{false};
    bool image_changed{false};
    bool effects_changed{false};
    bool video_source_changed{false};
};

/// Compiles layer state into one dirty-region decision for the frame.
///
/// This owns no history and performs no rendering.  The caller supplies the
/// canonical previous layer map from DirtyHistory, so exporters and backends
/// cannot grow independent delta/fingerprint logic.
class FrameDeltaCompiler final {
public:
    [[nodiscard]] static FrameDelta compile(
        Frame frame,
        const std::unordered_map<std::string, LayerBBoxState>& current,
        const std::unordered_map<std::string, LayerBBoxState>& previous,
        bool camera_changed,
        int width,
        int height,
        const raster::TileGrid* tile_grid = nullptr,
        const FrameDeltaCompileOptions& options = {});

    /// FULL canonical entry: PreviousFrameState + CurrentFrameState →
    /// FrameDelta (layer changes + dirty bounds/tiles + reuse eligibility
    /// + camera diff).  Every prev/current comparison in the runtime must
    /// route through this function (or `compile` for the layer-only
    /// variant) — no pipeline owns its own diff logic.
    [[nodiscard]] static FrameDelta compile_state(
        const FrameStateSnapshot& previous,
        const FrameStateSnapshot& current,
        int width,
        int height,
        const raster::TileGrid* tile_grid = nullptr,
        const FrameDeltaCompileOptions& options = {});

    /// Single sanctioned prev/current camera comparison.  Mirrors
    /// camera_change_policy::camera_changed() and is the only allowed
    /// entry point for pipeline camera diffs.
    [[nodiscard]] static bool camera_unchanged(
        const Camera2_5D& current,
        const Camera2_5D* previous,
        bool previous_valid);
};

} // namespace chronon3d::graph::detail
