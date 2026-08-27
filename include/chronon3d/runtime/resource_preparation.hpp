#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// runtime/resource_preparation.hpp
//
// TICKET-ASSET-PREP-BARRIER — Explicit asset-preparation barrier
//
// Replaces the implicit per-frame loading pattern (Remotion-like
// `delayRender()` in the frame loop) with an explicit 3-stage pipeline:
//
//   1. plan_render(request)        ─→ RenderPlan
//   2. prepare(plan, options)      ─→ PreparedAssets  ← THE BARRIER
//   3. render/encode               ─→ RenderOutput
//
// The barrier:
//   * is fail-loud by default (missing required asset → structured
//     `PreparationError` returned BEFORE any encoder work);
//   * aggregates the resource phases (font-load / image-decode /
//     video-metadata / audio-index / mesh-preparation / layout-preparation)
//     into a single snapshot;
//   * produces an immutable `PreparedAssets` readiness snapshot; concrete
//     runtime caches remain owned by RenderRuntime and are populated by the
//     render-preparation orchestrator;
//   * is deterministic: no PRNG, no time-based decisions, no thread-local
//     state — all I/O is bounded by (manifest, resolver, options) and the
//     selected canonical media backend.
//
// Failure semantics:
//   * `PreparationOptions::failure_mode == FailLoud` (default):
//     the FIRST missing/unresolvable asset aborts preparation with
//     `PreparationError{ .code = Code::MissingAsset, ... }`.
//     Tests MUST lock this default behavior.
//   * `FailureMode::WarnAndSkip`: missing assets emit a
//     `ResourceDiagnostics` warning + are skipped; downstream layers
//     using those assets fail at execute time. Reserved for non-strict
//     renderers (CLI preview mode).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/mesh_loader.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/result.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::runtime {

// ═══════════════════════════════════════════════════════════════════════════
// PreparationOptions — per-call configuration for the barrier
// ═══════════════════════════════════════════════════════════════════════════

struct PreparationOptions {
    enum class FailureMode {
        FailLoud,    // First missing/unresolvable asset → PreparationError
        WarnAndSkip, // Missing asset → diagnostics.warning; skip that resource
    };

    FailureMode                    failure_mode{FailureMode::FailLoud};
    bool                           prepare_fonts{true};
    bool                           prepare_images{true};
    bool                           prepare_video_metadata{true};
    bool                           prepare_audio_index{true};
    bool                           prepare_layouts{true};
    bool                           prepare_meshes{true};
    /// Optional runtime-owned cache. Null keeps preparation functional but
    /// disables cross-call mesh reuse (tests and one-shot callers).
    assets::MeshPreparationCache*  mesh_cache{nullptr};
    std::chrono::milliseconds      phase_timeout{0};   // 0 = no timeout
};

// ═══════════════════════════════════════════════════════════════════════════
// PreparationError — structured error returned by prepare()
// ═══════════════════════════════════════════════════════════════════════════

struct PreparationError {
    enum class Code {
        MissingAsset = 0,          // required asset not found by resolver
        UnresolvableAssetPath = 1, // path is malformed / empty
        CorruptedAsset = 2,        // asset exists but failed to decode/probe
        ResolverFailure = 3,       // resolver returned an error
        LayoutPreparationFailed = 4, // layout pipeline returned Error
        InternalError = 5,         // preserve the existing stable ordinal
        PreflightFailed = 6,       // asset preflight rejected the composition
    };

    Code        code{Code::InternalError};
    std::string message;
    std::string cause_code; // stable source code from the failing phase, if known
    std::string path;     // failing asset path (if known)
    std::string owner;    // manifest owner-key of failing asset (if known)
    std::string phase;    // "font" / "image" / "video" / "audio" / "layout"
};

// ═══════════════════════════════════════════════════════════════════════════
// Phase result types — per-resource snapshots
// ═══════════════════════════════════════════════════════════════════════════

struct PreparedFont {
    std::string          path;
    std::string          owner;
};

struct PreparedImage {
    std::string          path;
    std::string          owner;
    std::int32_t         width{0};
    std::int32_t         height{0};
    assets::ContentDigest content_digest{};
};

struct PreparedVideoMetadata {
    std::string   path;
    std::string   owner;
    std::int32_t  width{0};
    std::int32_t  height{0};
    float         fps{0.0f};
    std::int64_t  frame_count{0};
};

struct PreparedAudioIndex {
    std::string   path;
    std::string   owner;
    float         duration_seconds{0.0f};
    std::int32_t  sample_rate{0};
};

struct PreparedLayout {
    std::string          owner;
};

struct PreparedMesh {
    std::string                         path;
    std::string                         owner;
    assets::PreparedMeshSourceRef       source;
};

// ═══════════════════════════════════════════════════════════════════════════
// ResourceDiagnostics — observability surface emitted by prepare()
// ═══════════════════════════════════════════════════════════════════════════

struct ResourceDiagnostics {
    struct Warning {
        PreparationError::Code   code;
        std::string               message;
        std::string               phase;
    };

    std::vector<Warning>                warnings;
    std::size_t                         fonts_loaded{0};
    std::size_t                         images_decoded{0};
    std::size_t                         video_metadata_probed{0};
    std::size_t                         audio_indexes_built{0};
    std::size_t                         layouts_prepared{0};
    std::size_t                         meshes_prepared{0};
    std::chrono::steady_clock::duration elapsed{};
};

// ═══════════════════════════════════════════════════════════════════════════
// PreparedAssets — immutable readiness snapshot consumed by render setup.
// ═══════════════════════════════════════════════════════════════════════════

struct PreparedAssets {
    std::unordered_map<std::string, PreparedFont>             fonts;       // keyed by owner
    std::unordered_map<std::string, PreparedImage>            images;
    std::unordered_map<std::string, PreparedVideoMetadata>    video_metadata;
    std::unordered_map<std::string, PreparedAudioIndex>       audio_index;
    std::unordered_map<std::string, PreparedLayout>           layouts;
    std::unordered_map<std::string, PreparedMesh>             meshes;
    ResourceDiagnostics                                       diagnostics;

    [[nodiscard]] bool empty() const noexcept {
        return fonts.empty() && images.empty() && video_metadata.empty()
            && audio_index.empty() && layouts.empty() && meshes.empty();
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// ResourcePreparation — the barrier.
//
// Stateless helper; callers instantiate locally or call the static methods
// directly. No global state, no thread-local cache, no PRNG. All 5 phases
// are pure functions of (manifest, resolver, options).
// ═══════════════════════════════════════════════════════════════════════════

class ResourcePreparation {
public:
    /// Primary entry point: aggregate 5 phases into a single
    /// `PreparedAssets` snapshot. Fail-loud by default per TICKET-ASSET-
    /// PREP-BARRIER contract; tests lock this behavior.
    [[nodiscard]] static Result<PreparedAssets, PreparationError>
    prepare(
        const assets::AssetManifest&    manifest,
        const assets::AssetResolver&    resolver,
        const PreparationOptions&       options = {}
    );

    // Per-phase overloads (granular; rarely needed but exposed for
    // adapter integration on apps that want to short-circuit one phase).
    [[nodiscard]] static Result<PreparedFont, PreparationError>
    load_font(
        const assets::InternalAssetRef& ref,
        const assets::AssetResolver&    resolver
    );

    [[nodiscard]] static Result<PreparedImage, PreparationError>
    decode_image(
        const assets::InternalAssetRef& ref,
        const assets::AssetResolver&    resolver
    );

    [[nodiscard]] static Result<PreparedVideoMetadata, PreparationError>
    probe_video_metadata(
        const assets::InternalAssetRef& ref,
        const assets::AssetResolver&    resolver
    );

    [[nodiscard]] static Result<PreparedAudioIndex, PreparationError>
    build_audio_index(
        const assets::InternalAssetRef& ref,
        const assets::AssetResolver&    resolver
    );

    [[nodiscard]] static Result<PreparedLayout, PreparationError>
    prepare_layout(
        const assets::InternalAssetRef& ref,
        const assets::AssetResolver&    resolver,
        chronon3d::Frame                frame
    );

    [[nodiscard]] static Result<PreparedMesh, PreparationError>
    prepare_mesh(
        const assets::InternalAssetRef& ref,
        const assets::AssetResolver&    resolver,
        assets::MeshPreparationCache*   cache = nullptr
    );
};

} // namespace chronon3d::runtime
