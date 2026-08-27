// ═══════════════════════════════════════════════════════════════════════════
// src/runtime/resource_preparation.cpp — TICKET-ASSET-PREP-BARRIER
//
// Resource preparation barrier:
//   1. font-load              (resolve and validate path)
//   2. image-decode           (resolve and validate path; runtime cache is
//                              populated by render_preparation)
//   3. video-metadata probe   (libavformat when native FFmpeg is enabled)
//   4. audio-index build      (libavformat when native FFmpeg is enabled)
//   5. layout-preparation     (resolve path → layout fingerprint stub)
//
// All phases share a single fail-loud policy: the FIRST missing or
// unresolvable asset aborts preparation with a structured
// `PreparationError`. Optional `FailureMode::WarnAndSkip` collects
// diagnostics instead.
//
// The manifest orchestration is deterministic. Resource phases may perform
// bounded, synchronous reads through the supplied resolver/backend; they do
// not retain state or load anything from the frame loop.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/runtime/resource_preparation.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace chronon3d::runtime {

namespace {

// ── Internal helper — emits a structured error keyed by phase ───────
PreparationError make_error(PreparationError::Code    code,
                             std::string                message,
                             const assets::InternalAssetRef* ref,
                             const char*                 phase) {
    PreparationError e;
    e.code    = code;
    e.message = std::move(message);
    e.phase   = phase ? phase : "";
    if (ref) {
        e.path  = ref->path;
        e.owner = ref->owner;
    }
    return e;
}

// ── Internal helper — generic per-failure policy dispatch ───────────
enum class Policy { EmitError, AppendWarning };

template <typename RefT, typename PhaseFn>
bool run_phase(Policy                                policy,
               PreparationOptions::FailureMode      mode,
               const RefT&                          ref,
               const char*                          phase_name,
               PhaseFn&&                            fn,
               ResourceDiagnostics*                /*diags*/,
               std::vector<ResourceDiagnostics::Warning>* warnings_out) {
    auto result = fn(ref);
    if (result.has_value()) return true;
    if (policy == Policy::EmitError
        || mode == PreparationOptions::FailureMode::FailLoud) {
        throw result.error();   // bubble up to main loop, fail-loud
    }
    if (warnings_out) {
        warnings_out->push_back({
            result.error().code,
            result.error().message,
            phase_name
        });
    }
    return false;
}

// Value-preserving variant for phases whose result is part of the prepared
// SSOT.  The boolean helper is intentionally kept for validation-only phases;
// metadata phases must not discard the successful payload and recreate it
// with placeholder values.
template <typename RefT, typename PhaseFn>
auto run_phase_value(Policy                                policy,
                     PreparationOptions::FailureMode      mode,
                     const RefT&                          ref,
                     const char*                          phase_name,
                     PhaseFn&&                            fn,
                     ResourceDiagnostics*                 /*diags*/,
                     std::vector<ResourceDiagnostics::Warning>* warnings_out)
    -> std::optional<std::decay_t<decltype(fn(ref).value())>> {
    auto result = fn(ref);
    if (result.has_value()) return std::move(result.value());
    if (policy == Policy::EmitError
        || mode == PreparationOptions::FailureMode::FailLoud) {
        throw result.error();
    }
    if (warnings_out) {
        warnings_out->push_back({
            result.error().code,
            result.error().message,
            phase_name
        });
    }
    return std::nullopt;
}

// Template variant for frames in the layout phase.
template <typename RefT, typename PhaseFn>
bool run_phase_with_frame(Policy                                policy,
                           PreparationOptions::FailureMode      mode,
                           const RefT&                          ref,
                           const char*                          phase_name,
                           chronon3d::Frame                     frame,
                           PhaseFn&&                            fn,
                           ResourceDiagnostics*                /*diags*/,
                           std::vector<ResourceDiagnostics::Warning>* warnings_out) {
    auto result = fn(ref, frame);
    if (result.has_value()) return true;
    if (policy == Policy::EmitError
        || mode == PreparationOptions::FailureMode::FailLoud) {
        throw result.error();
    }
    if (warnings_out) {
        warnings_out->push_back({
            result.error().code,
            result.error().message,
            phase_name
        });
    }
    return false;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Aggregator: 5 phases, fail-loud by default
// ═══════════════════════════════════════════════════════════════════════════

Result<PreparedAssets, PreparationError>
ResourcePreparation::prepare(
    const assets::AssetManifest&    manifest,
    const assets::AssetResolver&    resolver,
    const PreparationOptions&       options
) {
    PreparedAssets prepared;
    const auto t_start = std::chrono::steady_clock::now();

    try {
        // Phase 1 — font-load
        if (options.prepare_fonts) {
            for (const auto& ref : manifest.filter(assets::AssetKind::Font)) {
                const bool prepared_ok = run_phase(Policy::AppendWarning, options.failure_mode, ref, "font",
                          [&](const assets::InternalAssetRef& r) {
                              return load_font(r, resolver);
                          }, &prepared.diagnostics, &prepared.diagnostics.warnings);
                if (!prepared_ok) continue;  // WarnAndSkip: missing asset — skip keyed map
                const auto [it, inserted] = prepared.fonts.emplace(ref.owner, PreparedFont{
                    .path  = ref.path,
                    .owner = ref.owner
                });
                if (inserted) ++prepared.diagnostics.fonts_loaded;
            }
        }

        // Phase 2 — image-decode
        if (options.prepare_images) {
            for (const auto& ref : manifest.filter(assets::AssetKind::Image)) {
                const bool prepared_ok = run_phase(Policy::AppendWarning, options.failure_mode, ref, "image",
                          [&](const assets::InternalAssetRef& r) {
                              return decode_image(r, resolver);
                          }, &prepared.diagnostics, &prepared.diagnostics.warnings);
                if (!prepared_ok) continue;  // WarnAndSkip: missing asset — skip keyed map
                const auto resolved = resolver.resolve(ref.path);
                if (!resolved) continue;
                const auto digest = assets::sha256_file(*resolved);
                if (!digest) {
                    if (options.failure_mode == PreparationOptions::FailureMode::FailLoud) {
                        throw PreparationError{
                            .code = PreparationError::Code::CorruptedAsset,
                            .message = "image content digest failed: " + ref.path,
                            .path = ref.path,
                            .owner = ref.owner,
                            .phase = "image",
                        };
                    }
                    continue;
                }
                const auto [it, inserted] = prepared.images.emplace(ref.owner, PreparedImage{
                    .path   = ref.path,
                    .owner  = ref.owner,
                    .width  = 0,
                    .height = 0,
                    .content_digest = *digest
                });
                if (inserted) ++prepared.diagnostics.images_decoded;
            }
        }

        // Phase 3 — video-metadata probe
        if (options.prepare_video_metadata) {
            for (const auto& ref : manifest.filter(assets::AssetKind::Video)) {
                auto metadata = run_phase_value(Policy::AppendWarning, options.failure_mode,
                          ref, "video",
                          [&](const assets::InternalAssetRef& r) {
                              return probe_video_metadata(r, resolver);
                          }, &prepared.diagnostics, &prepared.diagnostics.warnings);
                if (!metadata) continue;  // WarnAndSkip: missing asset — skip keyed map
                const auto [it, inserted] = prepared.video_metadata.emplace(
                    ref.owner, std::move(*metadata));
                if (inserted) ++prepared.diagnostics.video_metadata_probed;
            }
        }

        // Phase 4 — audio-index build
        if (options.prepare_audio_index) {
            for (const auto& ref : manifest.filter(assets::AssetKind::Audio)) {
                auto metadata = run_phase_value(Policy::AppendWarning, options.failure_mode,
                          ref, "audio",
                          [&](const assets::InternalAssetRef& r) {
                              return build_audio_index(r, resolver);
                          }, &prepared.diagnostics, &prepared.diagnostics.warnings);
                if (!metadata) continue;  // WarnAndSkip: missing asset — skip keyed map
                const auto [it, inserted] = prepared.audio_index.emplace(
                    ref.owner, std::move(*metadata));
                if (inserted) ++prepared.diagnostics.audio_indexes_built;
            }
        }

        // Mesh preparation — GLB import is the explicit runtime boundary.
        if (options.prepare_meshes) {
            for (const auto& ref : manifest.filter(assets::AssetKind::Mesh)) {
                auto mesh = prepare_mesh(ref, resolver, options.mesh_cache);
                if (!mesh.has_value()) {
                    const bool optional_missing =
                        !ref.required && mesh.error().code == PreparationError::Code::MissingAsset;
                    if (!optional_missing &&
                        options.failure_mode == PreparationOptions::FailureMode::FailLoud) {
                        throw mesh.error();
                    }
                    prepared.diagnostics.warnings.push_back({
                        mesh.error().code, mesh.error().message, "mesh"});
                    continue;
                }
                const auto [it, inserted] = prepared.meshes.emplace(ref.owner, std::move(mesh.value()));
                if (inserted) ++prepared.diagnostics.meshes_prepared;
            }
        }

        // Phase 5 — layout-preparation (per-frame; pre-compile fingerprint)
        if (options.prepare_layouts) {
            // Layout prep is keyed by image-kind assets for now; the actual
            // Layout preparation is a deterministic manifest phase. Concrete
            // runtime layout caches, when present, remain owned by the
            // renderer runtime and are not duplicated here.
            for (const auto& ref : manifest.filter(assets::AssetKind::Image)) {
                const bool prepared_ok = run_phase_with_frame(Policy::AppendWarning,
                                     options.failure_mode,
                                     ref, "layout", chronon3d::Frame{0},
                                     [&](const assets::InternalAssetRef& r,
                                         chronon3d::Frame f) {
                                         return prepare_layout(r, resolver, f);
                                     },
                                     &prepared.diagnostics,
                                     &prepared.diagnostics.warnings);
                if (!prepared_ok) continue;  // WarnAndSkip: missing asset — skip keyed map
                const auto [it, inserted] = prepared.layouts.emplace(ref.owner, PreparedLayout{
                    .owner  = ref.owner
                });
                if (inserted) ++prepared.diagnostics.layouts_prepared;
            }
        }

        prepared.diagnostics.elapsed = std::chrono::steady_clock::now() - t_start;
        return Result<PreparedAssets, PreparationError>(std::move(prepared));
    } catch (PreparationError& err) {
        return Result<PreparedAssets, PreparationError>(std::move(err));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 1 — font-load
// ═══════════════════════════════════════════════════════════════════════════

Result<PreparedFont, PreparationError>
ResourcePreparation::load_font(
    const assets::InternalAssetRef& ref,
    const assets::AssetResolver&    resolver
) {
    if (ref.path.empty()) {
        return PreparationError{
            .code    = PreparationError::Code::UnresolvableAssetPath,
            .message = std::string("empty asset path"),
            .path    = ref.path,
            .owner   = ref.owner,
            .phase   = "font"
        };
    }
    auto resolved = resolver.resolve(ref.path);
    if (!resolved.has_value()) {
        return PreparationError{
            .code    = PreparationError::Code::MissingAsset,
            .message = std::string("asset not found: ") + ref.path,
            .path    = ref.path,
            .owner   = ref.owner,
            .phase   = "font"
        };
    }
    return PreparedFont{
        .path   = ref.path,
        .owner  = ref.owner
    };
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 2 — image-decode
// ═══════════════════════════════════════════════════════════════════════════

Result<PreparedImage, PreparationError>
ResourcePreparation::decode_image(
    const assets::InternalAssetRef& ref,
    const assets::AssetResolver&    resolver
) {
    if (ref.path.empty()) {
        return PreparationError{
            .code    = PreparationError::Code::UnresolvableAssetPath,
            .message = std::string("empty asset path"),
            .path    = ref.path,
            .owner   = ref.owner,
            .phase   = "image"
        };
    }
    auto resolved = resolver.resolve(ref.path);
    if (!resolved.has_value()) {
        return PreparationError{
            .code    = PreparationError::Code::MissingAsset,
            .message = std::string("asset not found: ") + ref.path,
            .path    = ref.path,
            .owner   = ref.owner,
            .phase   = "image"
        };
    }
    const auto digest = assets::sha256_file(*resolved);
    if (!digest) {
        return PreparationError{
            .code = PreparationError::Code::CorruptedAsset,
            .message = std::string("image content digest failed: ") + ref.path,
            .path = ref.path,
            .owner = ref.owner,
            .phase = "image"
        };
    }
    return PreparedImage{
        .path   = ref.path,
        .owner  = ref.owner,
        .width  = 0,
        .height = 0,
        .content_digest = *digest
    };
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 5 — layout-preparation (per-frame fingerprint)
// ═══════════════════════════════════════════════════════════════════════════

Result<PreparedMesh, PreparationError>
ResourcePreparation::prepare_mesh(
    const assets::InternalAssetRef& ref,
    const assets::AssetResolver& resolver,
    assets::MeshPreparationCache* cache
) {
    const auto loaded = assets::MeshLoader::load(ref, resolver, cache);
    if (!loaded.has_value()) {
        PreparationError::Code code = PreparationError::Code::CorruptedAsset;
        switch (loaded.error().code) {
            case assets::MeshLoadErrorCode::MissingAsset:
                code = PreparationError::Code::MissingAsset;
                break;
            case assets::MeshLoadErrorCode::InvalidReference:
                code = PreparationError::Code::UnresolvableAssetPath;
                break;
            case assets::MeshLoadErrorCode::UnsupportedGlb:
                code = PreparationError::Code::PreflightFailed;
                break;
            case assets::MeshLoadErrorCode::ReadFailed:
            case assets::MeshLoadErrorCode::InvalidGlb:
            case assets::MeshLoadErrorCode::InvalidGeometry:
                code = PreparationError::Code::CorruptedAsset;
                break;
        }
        const char* cause_code = "MESH_LOAD_ERROR";
        switch (loaded.error().code) {
            case assets::MeshLoadErrorCode::MissingAsset: cause_code = "MISSING_ASSET"; break;
            case assets::MeshLoadErrorCode::ReadFailed: cause_code = "READ_FAILED"; break;
            case assets::MeshLoadErrorCode::InvalidGlb: cause_code = "INVALID_GLB"; break;
            case assets::MeshLoadErrorCode::UnsupportedGlb: cause_code = "UNSUPPORTED_GLB"; break;
            case assets::MeshLoadErrorCode::InvalidGeometry: cause_code = "INVALID_GEOMETRY"; break;
            case assets::MeshLoadErrorCode::InvalidReference: cause_code = "INVALID_REFERENCE"; break;
        }
        PreparationError error{
            .code = code,
            .message = "mesh preparation failed: " + loaded.error().message,
            .cause_code = cause_code,
            .path = ref.path,
            .owner = ref.owner,
            .phase = "mesh",
        };
        return error;
    }
    return PreparedMesh{
        .path = ref.path,
        .owner = ref.owner,
        .source = loaded.value(),
    };
}

Result<PreparedLayout, PreparationError>
ResourcePreparation::prepare_layout(
    const assets::InternalAssetRef& ref,
    const assets::AssetResolver&    resolver,
    chronon3d::Frame                /*frame*/
) {
    if (ref.path.empty()) {
        return PreparationError{
            .code    = PreparationError::Code::UnresolvableAssetPath,
            .message = std::string("empty asset path"),
            .path    = ref.path,
            .owner   = ref.owner,
            .phase   = "layout"
        };
    }
    auto resolved = resolver.resolve(ref.path);
    if (!resolved.has_value()) {
        return PreparationError{
            .code    = PreparationError::Code::MissingAsset,
            .message = std::string("layout asset not found: ") + ref.path,
            .path    = ref.path,
            .owner   = ref.owner,
            .phase   = "layout"
        };
    }
    return PreparedLayout{
        .owner  = ref.owner
    };
}

} // namespace chronon3d::runtime
