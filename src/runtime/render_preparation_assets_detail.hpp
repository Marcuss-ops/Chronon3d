#include "render_preparation_internal.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/asset_prepare_policy.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <algorithm>
#include <atomic>
#include <span>
#include <unordered_map>

namespace chronon3d::runtime::detail {
namespace {

struct ImageCounterSnapshot {
    uint64_t resolve_us{0};
    uint64_t decode_us{0};
    uint64_t convert_us{0};
    uint64_t decode_count{0};
    uint64_t post_decode_downscale_count{0};
    uint64_t cache_hits{0};
    uint64_t cache_misses{0};
};

ImageCounterSnapshot snapshot_image_counters(const RenderCounters& counters) {
    ImageCounterSnapshot s;
    s.resolve_us = counters.image_resolve_wall_us.load(std::memory_order_relaxed);
    s.decode_us = counters.image_decode_wall_us.load(std::memory_order_relaxed);
    s.convert_us = counters.image_convert_wall_us.load(std::memory_order_relaxed);
    s.decode_count = counters.image_decode_count.load(std::memory_order_relaxed);
    s.post_decode_downscale_count = counters.image_post_decode_downscale_count.load(std::memory_order_relaxed);
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
        result.preparation_error = make_preparation_error(
            PreparationError::Code::InternalError,
            "Render preparation requires a renderer", {}, {}, {}, "setup");
        return result;
    }

    profiling::ProfilingGuard profiling_scope(
        renderer->counters(), renderer->framebuffer_pool().get());

    const auto preflight_t0 = profiling::now();
    result.preflight = AssetPreflightResolver::check(
        scene, renderer->execution_context().runtime->resolver(), options.preflight_mode,
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
        result.preparation_error = make_preparation_error(
            PreparationError::Code::PreflightFailed,
            error_issue.message.empty()
                ? "asset preflight validation failed"
                : error_issue.message,
            error_issue.code, error_issue.path, error_issue.layer_id, "preflight");
        return result;
    }

    auto resource_options = options.resources;
    resource_options.mesh_cache = &renderer->execution_context().runtime->mesh_cache();
    if (renderer->video_decoder() != nullptr) {
        resource_options.prepare_video_metadata = false;
    }
    // AUDIT-2026-09-09 N-12 — the image manifest slice is needed three more
    // times below (decode, GPU promotion, metrics snapshot). Filter once and
    // share the refs instead of rebuilding vector<InternalAssetRef> copies
    // per phase at the barrier.
    const auto image_refs = scene.asset_manifest().filter(assets::AssetKind::Image);
    const auto image_refs_view = std::span<const assets::InternalAssetRef>(image_refs);
    const auto resolve_t0 = profiling::now();
    auto prepared = ResourcePreparation::prepare(
        scene.asset_manifest(), renderer->execution_context().runtime->resolver(), resource_options);
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
            scene, renderer->execution_context().runtime->resolver());
        result.timings.font_load_ms =
            profiling::duration_ms(font_t0, profiling::now());
        if (fonts.preflight_missing != 0) {
            result.preparation_error = make_preparation_error(
                PreparationError::Code::CorruptedAsset,
                "one or more fonts could not be loaded", {}, {}, {}, "font");
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
    std::uint64_t image_decode_batch_count = 0;
    std::uint64_t image_decode_peak_bytes = 0;
    if (options.resources.prepare_images) {
        auto& image_cache = renderer->execution_context().runtime->image_cache();
        struct ImageDecodeTask {
            std::string path;
            ImageDecodeOptions options;
        };
        std::vector<ImageDecodeTask> decode_tasks;
        std::vector<std::vector<std::size_t>> task_references;
        std::unordered_map<ImageAssetKey, std::size_t, ImageAssetKeyHash> task_by_key;
        task_by_key.reserve(image_refs_view.size());

        for (std::size_t index = 0; index < image_refs_view.size(); ++index) {
            const auto& ref = image_refs_view[index];
            ImageDecodeOptions decode_options{};
            decode_options.max_dimension = ref.max_dimension;
            decode_options.color_space = ref.color_space;
            const auto key = image_cache.canonical_key_for(ref.path, decode_options);
            if (!key) {
                task_references.push_back({index});
                decode_tasks.push_back({ref.path, std::move(decode_options)});
                continue;
            }
            const auto [it, inserted] = task_by_key.try_emplace(
                *key, decode_tasks.size());
            if (inserted) {
                decode_tasks.push_back({ref.path, std::move(decode_options)});
                task_references.emplace_back();
            }
            task_references[it->second].push_back(index);
        }

        const bool promote_to_gpu =
            renderer->execution_context().runtime->backend_attached() &&
            renderer->execution_context().runtime->backend().supports_native_surfaces();
        auto& gpu_cache = renderer->execution_context().runtime->gpu_asset_cache();

        // Decode one unique asset, update all of its manifest owners, and
        // promote it before loading the next. ImageCache may evict the entry
        // immediately (or reject it when larger than one shard); this local
        // shared reference keeps its decoded pixels alive exactly until the
        // GPU upload finishes, without retaining the whole job's working set.
        for (std::size_t task = 0; task < decode_tasks.size(); ++task) {
            profiling::ProfilingGuard worker_profile(
                renderer->counters(), renderer->framebuffer_pool().get());
            const auto image = image_cache.get_or_load(
                decode_tasks[task].path, decode_tasks[task].options);
            const auto& references = task_references[task];
            const auto& first_ref = image_refs_view[references.front()];
            if (!image || !image->valid()) {
                result.preparation_error = make_preparation_error(
                    PreparationError::Code::CorruptedAsset,
                    "image could not be decoded: " + first_ref.path,
                    {}, first_ref.path, first_ref.owner, "image");
                return result;
            }
            ++image_decode_batch_count;
            image_decode_peak_bytes = std::max(
                image_decode_peak_bytes,
                static_cast<std::uint64_t>(image_cache_detail::cached_image_bytes(*image)));
            for (const auto reference : references) {
                const auto& ref = image_refs_view[reference];
                if (auto it = result.prepared_assets->images.find(ref.owner);
                    it != result.prepared_assets->images.end()) {
                    it->second.width = image->width;
                    it->second.height = image->height;
                    it->second.content_digest = image->gpu_key.content_digest;
                }
            }
            if (promote_to_gpu) {
                const auto pixels = image->gpu_rgba_span();
                const auto& key = image->gpu_key;
                if (pixels.empty() || key.content_digest == assets::ContentDigest{}) {
                    result.preparation_error = make_preparation_error(
                        PreparationError::Code::CorruptedAsset,
                        "static image has no content-addressed GPU payload: " +
                            first_ref.path,
                        {}, first_ref.path, first_ref.owner, "gpu-image");
                    return result;
                }
                const runtime::SurfaceDesc desc{
                    key.width, key.height, key.format,
                    runtime::ResourceUsage::Storage,
                    runtime::LifetimeClass::JobPersistent,
                    pixels.size_bytes()};
                const auto acquired = gpu_cache.acquire(key, desc, pixels);
                if (!acquired.ok()) {
                    result.preparation_error = make_preparation_error(
                        PreparationError::Code::CorruptedAsset,
                        "static image GPU promotion failed: " + first_ref.path +
                            " (" + acquired.error + ")",
                        {}, first_ref.path, first_ref.owner, "gpu-image");
                    return result;
                }
            }
        }
    }
    const ImageCounterSnapshot image_after = snapshot_image_counters(*renderer->counters());

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
    if (options.resources.prepare_images) {
        result.timings.image_decode_batch_count = image_decode_batch_count;
        result.timings.image_decode_peak_bytes = image_decode_peak_bytes;
    }
    result.timings.image_post_decode_downscale_count =
        image_after.post_decode_downscale_count - image_before.post_decode_downscale_count;
    result.timings.image_cache_hits = image_after.cache_hits - image_before.cache_hits;
    result.timings.image_cache_misses = image_after.cache_misses - image_before.cache_misses;

    const auto publish_t0 = profiling::now();
    renderer->execution_context().runtime->publish_prepared_assets(*result.prepared_assets);
    result.timings.backend_prepare_ms =
        profiling::duration_ms(publish_t0, profiling::now());
    return result;
}

} // namespace chronon3d::runtime::detail
