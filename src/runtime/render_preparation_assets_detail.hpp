#include "render_preparation_internal.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <algorithm>
#include <atomic>

namespace chronon3d::runtime::detail {
namespace {

struct ImageCounterSnapshot {
    uint64_t resolve_us{0};
    uint64_t decode_us{0};
    uint64_t convert_us{0};
    uint64_t decode_count{0};
    uint64_t cache_hits{0};
    uint64_t cache_misses{0};
};

ImageCounterSnapshot snapshot_image_counters(const RenderCounters& counters) {
    ImageCounterSnapshot s;
    s.resolve_us = counters.image_resolve_wall_us.load(std::memory_order_relaxed);
    s.decode_us = counters.image_decode_wall_us.load(std::memory_order_relaxed);
    s.convert_us = counters.image_convert_wall_us.load(std::memory_order_relaxed);
    s.decode_count = counters.image_decode_count.load(std::memory_order_relaxed);
    s.cache_hits = counters.image_cache_hits.load(std::memory_order_relaxed);
    s.cache_misses = counters.image_cache_misses.load(std::memory_order_relaxed);
    return s;
}

struct FontCounterSnapshot {
    uint64_t resolve_us{0};
    uint64_t cache_hits{0};
    uint64_t cache_misses{0};
};

FontCounterSnapshot snapshot_font_counters(const RenderCounters& counters) {
    FontCounterSnapshot s;
    s.resolve_us = counters.font_resolve_wall_us.load(std::memory_order_relaxed);
    s.cache_hits = counters.font_cache_hits.load(std::memory_order_relaxed);
    s.cache_misses = counters.font_cache_misses.load(std::memory_order_relaxed);
    return s;
}

} // namespace

RenderPreparationResult prepare_render_scene(
    SoftwareRenderer* renderer,
    const Scene& scene,
    const CompositionSpec& spec,
    const RenderPreparationOptions& options) {
    RenderPreparationResult result;
    if (renderer == nullptr) {
        result.preparation_error = PreparationError{
            .code = PreparationError::Code::InternalError,
            .message = "Render preparation requires a renderer",
            .phase = "setup",
        };
        return result;
    }

    profiling::ProfilingGuard profiling_scope(
        renderer->counters(), renderer->framebuffer_pool().get());

    const auto preflight_t0 = profiling::now();
    result.preflight = AssetPreflightResolver::check(
        scene, renderer->runtime().resolver(), options.preflight_mode,
        options.reference_frame);
    result.timings.asset_preflight_ms =
        profiling::duration_ms(preflight_t0, profiling::now());
    if (!result.preflight.ok()) {
        const auto issue = std::find_if(
            result.preflight.issues.begin(), result.preflight.issues.end(),
            [](const auto& candidate) {
                return candidate.severity == PreflightSeverity::Error;
            });
        const auto& error_issue = issue != result.preflight.issues.end()
            ? *issue : result.preflight.issues.front();
        result.preparation_error = PreparationError{
            .code = PreparationError::Code::PreflightFailed,
            .message = error_issue.message.empty()
                ? "asset preflight validation failed"
                : error_issue.message,
            .cause_code = error_issue.code,
            .path = error_issue.path,
            .owner = error_issue.layer_id,
            .phase = "preflight",
        };
        return result;
    }

    auto resource_options = options.resources;
    resource_options.mesh_cache = &renderer->runtime().mesh_cache();
    if (renderer->video_decoder() != nullptr) {
        resource_options.prepare_video_metadata = false;
    }
    const auto resolve_t0 = profiling::now();
    auto prepared = ResourcePreparation::prepare(
        scene.asset_manifest(), renderer->runtime().resolver(), resource_options);
    result.timings.asset_resolve_ms =
        profiling::duration_ms(resolve_t0, profiling::now());
    if (!prepared.has_value()) {
        result.preparation_error = std::move(prepared.error());
        return result;
    }
    result.prepared_assets = std::move(prepared.value());

    const auto font_t0 = profiling::now();
    const FontCounterSnapshot font_before = snapshot_font_counters(*renderer->counters());
    if (options.resources.prepare_fonts &&
        !scene.asset_manifest().filter(assets::AssetKind::Font).empty()) {
        const auto fonts = renderer->preflight_fonts(
            scene, renderer->runtime().resolver());
        result.timings.font_load_ms =
            profiling::duration_ms(font_t0, profiling::now());
        if (fonts.preflight_missing != 0) {
            result.preparation_error = PreparationError{
                .code = PreparationError::Code::CorruptedAsset,
                .message = "one or more fonts could not be loaded",
                .phase = "font",
            };
            return result;
        }
    }
    const FontCounterSnapshot font_after = snapshot_font_counters(*renderer->counters());
    result.timings.font_cache_hits = font_after.cache_hits - font_before.cache_hits;
    result.timings.font_cache_misses = font_after.cache_misses - font_before.cache_misses;
    result.timings.font_resolve_ms =
        static_cast<double>(font_after.resolve_us - font_before.resolve_us) / 1000.0;

    const auto decode_t0 = profiling::now();
    const ImageCounterSnapshot image_before = snapshot_image_counters(*renderer->counters());
    if (options.resources.prepare_images) {
        for (const auto& ref : scene.asset_manifest().filter(assets::AssetKind::Image)) {
            const auto image = renderer->runtime().image_cache().get_or_load(ref.path);
            if (!image || !image->valid()) {
                result.preparation_error = PreparationError{
                    .code = PreparationError::Code::CorruptedAsset,
                    .message = "image could not be decoded: " + ref.path,
                    .path = ref.path,
                    .owner = ref.owner,
                    .phase = "image",
                };
                return result;
            }
            if (auto it = result.prepared_assets->images.find(ref.owner);
                it != result.prepared_assets->images.end()) {
                it->second.width = image->width;
                it->second.height = image->height;
                it->second.content_digest = image->gpu_key.content_digest;
            }
        }
    }
    const ImageCounterSnapshot image_after = snapshot_image_counters(*renderer->counters());

    if (options.resources.prepare_images && renderer->runtime().backend_attached() &&
        renderer->runtime().backend().supports_native_surfaces()) {
        auto& cache = renderer->runtime().gpu_asset_cache();
        for (const auto& ref : scene.asset_manifest().filter(assets::AssetKind::Image)) {
            const auto image = renderer->runtime().image_cache().find(ref.path);
            if (!image || !image->valid() || image->gpu_rgba.empty()) {
                continue;
            }
            const auto& key = image->gpu_key;
            if (key.content_digest == assets::ContentDigest{}) {
                result.preparation_error = PreparationError{
                    .code = PreparationError::Code::CorruptedAsset,
                    .message = "static image has no content-addressed GPU key: " + ref.path,
                    .path = ref.path,
                    .owner = ref.owner,
                    .phase = "gpu-image",
                };
                return result;
            }
            const runtime::SurfaceDesc desc{
                key.width, key.height, key.format,
                runtime::ResourceUsage::Storage,
                runtime::LifetimeClass::JobPersistent,
                image->gpu_rgba.size() * sizeof(float)};
            const auto acquired = cache.acquire(key, desc, image->gpu_rgba);
            if (!acquired.ok()) {
                result.preparation_error = PreparationError{
                    .code = PreparationError::Code::CorruptedAsset,
                    .message = "static image GPU promotion failed: " + ref.path,
                    .path = ref.path,
                    .owner = ref.owner,
                    .phase = "gpu-image",
                };
                return result;
            }
        }
    }

    result.timings.asset_decode_ms =
        profiling::duration_ms(decode_t0, profiling::now());
    result.timings.image_resolve_ms =
        static_cast<double>(image_after.resolve_us - image_before.resolve_us) / 1000.0;
    result.timings.image_decode_ms =
        static_cast<double>(image_after.decode_us - image_before.decode_us) / 1000.0;
    result.timings.image_convert_ms =
        static_cast<double>(image_after.convert_us - image_before.convert_us) / 1000.0;
    result.timings.image_decode_count =
        image_after.decode_count - image_before.decode_count;
    result.timings.image_cache_hits = image_after.cache_hits - image_before.cache_hits;
    result.timings.image_cache_misses = image_after.cache_misses - image_before.cache_misses;

    const auto publish_t0 = profiling::now();
    renderer->runtime().publish_prepared_assets(*result.prepared_assets);
    result.timings.backend_prepare_ms =
        profiling::duration_ms(publish_t0, profiling::now());
    return result;
}

} // namespace chronon3d::runtime::detail
