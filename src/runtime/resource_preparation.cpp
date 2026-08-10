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

#include <chrono>
#include <stdexcept>
#include <utility>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#endif

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

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
std::string ffmpeg_error(int error_code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(error_code, buffer, sizeof(buffer));
    return buffer;
}

Result<AVFormatContext*, PreparationError> open_media(
    const assets::InternalAssetRef& ref,
    const assets::AssetResolver& resolver,
    const char* phase) {
    if (ref.path.empty()) {
        return make_error(PreparationError::Code::UnresolvableAssetPath,
                          "empty asset path", &ref, phase);
    }
    const auto resolved = resolver.resolve(ref.path);
    if (!resolved.has_value()) {
        return make_error(PreparationError::Code::MissingAsset,
                          "asset not found: " + ref.path, &ref, phase);
    }

    AVFormatContext* context = nullptr;
    const std::string path = resolved->string();
    const int open_result = avformat_open_input(&context, path.c_str(), nullptr, nullptr);
    if (open_result < 0) {
        return make_error(PreparationError::Code::CorruptedAsset,
                          "media container could not be opened: " +
                              ffmpeg_error(open_result), &ref, phase);
    }
    const int stream_result = avformat_find_stream_info(context, nullptr);
    if (stream_result < 0) {
        avformat_close_input(&context);
        return make_error(PreparationError::Code::CorruptedAsset,
                          "media stream metadata could not be read: " +
                              ffmpeg_error(stream_result), &ref, phase);
    }
    return context;
}
#endif

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
                const auto [it, inserted] = prepared.images.emplace(ref.owner, PreparedImage{
                    .path   = ref.path,
                    .owner  = ref.owner,
                    .width  = 0,
                    .height = 0
                });
                if (inserted) ++prepared.diagnostics.images_decoded;
            }
        }

        // Phase 3 — video-metadata probe
        if (options.prepare_video_metadata) {
            for (const auto& ref : manifest.filter(assets::AssetKind::Video)) {
                const bool prepared_ok = run_phase(Policy::AppendWarning, options.failure_mode, ref, "video",
                          [&](const assets::InternalAssetRef& r) {
                              return probe_video_metadata(r, resolver);
                          }, &prepared.diagnostics, &prepared.diagnostics.warnings);
                if (!prepared_ok) continue;  // WarnAndSkip: missing asset — skip keyed map
                const auto [it, inserted] = prepared.video_metadata.emplace(ref.owner, PreparedVideoMetadata{
                    .path        = ref.path,
                    .owner       = ref.owner,
                    .width       = 0,
                    .height      = 0,
                    .fps         = 0.0f,
                    .frame_count = 0
                });
                if (inserted) ++prepared.diagnostics.video_metadata_probed;
            }
        }

        // Phase 4 — audio-index build
        if (options.prepare_audio_index) {
            for (const auto& ref : manifest.filter(assets::AssetKind::Audio)) {
                const bool prepared_ok = run_phase(Policy::AppendWarning, options.failure_mode, ref, "audio",
                          [&](const assets::InternalAssetRef& r) {
                              return build_audio_index(r, resolver);
                          }, &prepared.diagnostics, &prepared.diagnostics.warnings);
                if (!prepared_ok) continue;  // WarnAndSkip: missing asset — skip keyed map
                const auto [it, inserted] = prepared.audio_index.emplace(ref.owner, PreparedAudioIndex{
                    .path             = ref.path,
                    .owner            = ref.owner,
                    .duration_seconds = 0.0f,
                    .sample_rate      = 0
                });
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
    return PreparedImage{
        .path   = ref.path,
        .owner  = ref.owner,
        .width  = 0,
        .height = 0
    };
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 3 — video-metadata probe
// ═══════════════════════════════════════════════════════════════════════════

Result<PreparedVideoMetadata, PreparationError>
ResourcePreparation::probe_video_metadata(
    const assets::InternalAssetRef& ref,
    const assets::AssetResolver&    resolver
) {
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    auto opened = open_media(ref, resolver, "video");
    if (!opened.has_value()) return opened.error();

    AVFormatContext* context = opened.value();
    const int stream_index = av_find_best_stream(
        context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        avformat_close_input(&context);
        return make_error(PreparationError::Code::CorruptedAsset,
                          "media contains no video stream", &ref, "video");
    }

    AVStream* stream = context->streams[stream_index];
    const AVCodecParameters* codec = stream->codecpar;
    const AVRational rate = av_guess_frame_rate(context, stream, nullptr);
    const double fps = rate.den != 0
        ? av_q2d(rate)
        : 0.0;
    const std::int64_t frame_count = stream->nb_frames > 0
        ? stream->nb_frames
        : 0;
    PreparedVideoMetadata metadata{
        .path = ref.path,
        .owner = ref.owner,
        .width = codec->width,
        .height = codec->height,
        .fps = static_cast<float>(fps),
        .frame_count = frame_count,
    };
    avformat_close_input(&context);
    if (metadata.width <= 0 || metadata.height <= 0 || metadata.fps <= 0.0f) {
        return make_error(PreparationError::Code::CorruptedAsset,
                          "video stream has invalid metadata", &ref, "video");
    }
    return metadata;
#else
    if (ref.path.empty()) {
        return make_error(PreparationError::Code::UnresolvableAssetPath,
                          "empty asset path", &ref, "video");
    }
    if (!resolver.resolve(ref.path).has_value()) {
        return make_error(PreparationError::Code::MissingAsset,
                          "asset not found: " + ref.path, &ref, "video");
    }
    return make_error(
        PreparationError::Code::InternalError,
        "video preparation requires CHRONON3D_ENABLE_NATIVE_FFMPEG",
        &ref, "video");
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase 4 — audio-index build
// ═══════════════════════════════════════════════════════════════════════════

Result<PreparedAudioIndex, PreparationError>
ResourcePreparation::build_audio_index(
    const assets::InternalAssetRef& ref,
    const assets::AssetResolver&    resolver
) {
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    auto opened = open_media(ref, resolver, "audio");
    if (!opened.has_value()) return opened.error();

    AVFormatContext* context = opened.value();
    const int stream_index = av_find_best_stream(
        context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        avformat_close_input(&context);
        return make_error(PreparationError::Code::CorruptedAsset,
                          "media contains no audio stream", &ref, "audio");
    }

    const AVStream* stream = context->streams[stream_index];
    const AVCodecParameters* codec = stream->codecpar;
    double duration = 0.0;
    if (stream->duration != AV_NOPTS_VALUE) {
        duration = static_cast<double>(stream->duration) * av_q2d(stream->time_base);
    } else if (context->duration != AV_NOPTS_VALUE) {
        duration = static_cast<double>(context->duration) / AV_TIME_BASE;
    }
    PreparedAudioIndex metadata{
        .path = ref.path,
        .owner = ref.owner,
        .duration_seconds = static_cast<float>(duration),
        .sample_rate = codec->sample_rate,
    };
    avformat_close_input(&context);
    if (metadata.duration_seconds <= 0.0f || metadata.sample_rate <= 0) {
        return make_error(PreparationError::Code::CorruptedAsset,
                          "audio stream has invalid metadata", &ref, "audio");
    }
    return metadata;
#else
    if (ref.path.empty()) {
        return make_error(PreparationError::Code::UnresolvableAssetPath,
                          "empty asset path", &ref, "audio");
    }
    if (!resolver.resolve(ref.path).has_value()) {
        return make_error(PreparationError::Code::MissingAsset,
                          "asset not found: " + ref.path, &ref, "audio");
    }
    return make_error(
        PreparationError::Code::InternalError,
        "audio preparation requires CHRONON3D_ENABLE_NATIVE_FFMPEG",
        &ref, "audio");
#endif
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
