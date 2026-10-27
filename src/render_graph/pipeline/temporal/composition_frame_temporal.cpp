#include "../composition_frame_internal.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/tracing/tracing_categories.hpp>
#include <chronon3d/animation/temporal/temporal_samples.hpp>
#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>
#include <chronon3d/runtime/job/job_session.hpp>
#include <chronon3d/backends/software/scratch_buffer.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/math/color.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>
#include <spdlog/spdlog.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <hwy/highway.h>

namespace chronon3d::graph::detail {

bool CompositionFrameState::render_temporal_accumulation() {
const int N = std::max(2, settings.motion_blur.samples);

chronon3d::temporal::TemporalSampleParams mb_params;
mb_params.shutter_angle_deg = settings.motion_blur.shutter_angle_deg;
mb_params.shutter_phase_deg = settings.motion_blur.shutter_phase_deg;
mb_params.pattern           = settings.motion_blur.pattern;
mb_params.filter            = settings.motion_blur.filter;
mb_params.jitter_seed       = settings.motion_blur.jitter_seed;

const chronon3d::temporal::TemporalSamplePlan sample_plan =
    chronon3d::temporal::make_temporal_sample_plan(
        mb_params, N, frame, spec.frame_rate,
        static_cast<std::size_t>(rw), static_cast<std::size_t>(rh),
        /*cache_version=*/0,
        chronon3d::temporal::TemporalBudgetResolver{
            settings.render_budget.max_temporal_pixels});
if (!sample_plan.valid()) {
    throw std::invalid_argument("motion-blur temporal sample plan is invalid");
}

// Every temporal sample owns all mutable frame domains.  In
// particular, do not reuse the main NodeCache, CompiledGraphCache,
// runtime::JobSession, or transform scratch: those domains belong to the
// integer-frame render and its history.  Sample contexts are kept
// alive for the complete accumulation window and are never published
// through commit_frame_state().
std::vector<std::unique_ptr<cache::NodeCache>> sample_value_caches;
std::vector<std::unique_ptr<CompiledGraphCache>> sample_topology_caches;
std::vector<std::unique_ptr<runtime::JobSession>> sample_sessions;
std::vector<std::unique_ptr<TransformScratchBuffer>> sample_scratches;
std::vector<std::shared_ptr<cache::FramebufferPool>> sample_framebuffer_pools;
sample_value_caches.reserve(sample_plan.contexts.size());
sample_topology_caches.reserve(sample_plan.contexts.size());
sample_sessions.reserve(sample_plan.contexts.size());
sample_scratches.reserve(sample_plan.contexts.size());
sample_framebuffer_pools.reserve(sample_plan.contexts.size());
const auto sample_pool_limit = frame_runtime
    ? (frame_runtime->config().cache().fb_pool_budget_bytes() > 0
        ? frame_runtime->config().cache().fb_pool_budget_bytes()
        : frame_runtime->config().cache().fb_pool_max_bytes())
    : cache::FramebufferPool::kDefaultBudgetBytes;
auto* owning_memory_tracker = sw_sidecar
    ? &sw_sidecar->session().memory_aggregate()
    : nullptr;
auto merge_sample_reports = [&](std::size_t completed_samples) {
    if (!owning_memory_tracker) return;
    for (std::size_t i = 0; i < completed_samples; ++i) {
        owning_memory_tracker->merge_report(
            sample_sessions[i]->memory_aggregate().snapshot());
    }
};
for (std::size_t i = 0; i < sample_plan.contexts.size(); ++i) {
    auto sample_cache = std::make_unique<cache::NodeCache>();
    if (frame_runtime) {
        sample_cache->set_capacity(
            frame_runtime->config().cache().node_cache_max_bytes());
    }
    sample_value_caches.push_back(std::move(sample_cache));
    sample_topology_caches.push_back(std::make_unique<CompiledGraphCache>());
    sample_sessions.push_back(std::make_unique<runtime::JobSession>());
    sample_scratches.push_back(std::make_unique<TransformScratchBuffer>());
    sample_framebuffer_pools.push_back(
        cache::FramebufferPool::create_shared(sample_pool_limit));
}

render_fb = sample_framebuffer_pools.front()->acquire(
    rw, rh, /*clear=*/true);

// Temporal samples are intermediate Vulkan outputs, not encoder
// frames. Give them one reusable GPU surface and defer CUDA/NVENC
// ownership handoff until the final weighted average is ready.
RenderSettings sample_settings = settings;
struct NativeSampleSurfaceRelease {
    RenderBackend* backend{};
    runtime::RuntimeContext* runtime_context{};
    runtime::RenderSurfaceHandle handle{
        runtime::kInvalidRenderSurfaceHandle};
    void release() noexcept {
        if (handle == runtime::kInvalidRenderSurfaceHandle) return;
        (void)backend->release_surface(handle);
        (void)runtime_context->surface_registry().release(handle);
        handle = runtime::kInvalidRenderSurfaceHandle;
    }
    ~NativeSampleSurfaceRelease() { release(); }
} native_sample_surface{&backend, frame_runtime};
struct DeferredNativeSampleBatch {
    RenderBackend* backend{};
    bool active{};
    void finish() noexcept {
        if (!active) return;
        backend->end_frame_batch();
        active = false;
    }
    ~DeferredNativeSampleBatch() { finish(); }
};
// A Vulkan raw/pixel-truth sink has no native encoder destination, but
// its temporal samples still need a GPU-resident target. Without this
// intermediate, isolated samples defer their frame-batch handoff while
// the compositor has no batch owner to close it; the terminal readback
// then races work that was never submitted. Use the same sample surface
// for native video and other native-surface consumers, and read back
// only the completed weighted accumulator for host sinks.
if (frame_runtime && frame_runtime->backend_attached() &&
    backend.supports_native_surfaces() && ssaa == 1.0f) {
    const auto desc = runtime::SurfaceDesc::make(
        static_cast<std::uint32_t>(rw),
        static_cast<std::uint32_t>(rh),
        runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::JobPersistent);
    native_sample_surface.handle =
        frame_runtime->surface_registry().create(desc);
    if (native_sample_surface.handle ==
        runtime::kInvalidRenderSurfaceHandle) {
        throw std::runtime_error(
            "failed to allocate temporal Vulkan sample output");
    }
    const auto created = backend.create_surface(
        native_sample_surface.handle, desc);
    if (!created.ok()) {
        throw std::runtime_error(
            "failed to create temporal Vulkan sample output: " +
            created.error().message);
    }
    sample_settings.native_video_encode_surface =
        native_sample_surface.handle;
    sample_settings.native_video_source_surface =
        runtime::kInvalidRenderSurfaceHandle;
    sample_settings.native_video_previous_surface =
        runtime::kInvalidRenderSurfaceHandle;
}

float actual_weight_sum = 0.0f;
float planned_weight_sum = 0.0f;
for (int s = 0; s < sample_plan.num_samples(); ++s) {
    planned_weight_sum += sample_plan[static_cast<std::size_t>(s)].weight;
}
if (!std::isfinite(planned_weight_sum) ||
    std::abs(planned_weight_sum) <= 1e-6f) {
    throw std::invalid_argument(
        "motion-blur temporal sample weights have an invalid sum");
}
bool native_temporal_accumulation = false;
runtime::RenderSurfaceHandle native_accumulator =
    runtime::kInvalidRenderSurfaceHandle;

const auto t_mb0 = profiling::now();
{
    CHRONON_TRACE_SCOPE("chronon.effect", "motion_blur_accumulation");
    for (int s = 0; s < sample_plan.num_samples(); ++s) {
        if (cancellation && cancellation->is_cancelled()) {
            merge_sample_reports(static_cast<std::size_t>(s));
            return false;
        }
        const auto& sample = sample_plan[static_cast<std::size_t>(s)];
        const float t = static_cast<float>(sample.time.frame - static_cast<double>(frame));
        const float w = sample.weight;
        actual_weight_sum += w;
        const FrameContext sub_ctx = make_frame_context({
            .global_time = sample.time,
            .duration = spec.duration,
            .width = spec.width,
            .height = spec.height,
            .assets_root = frame_assets_root,
            .font_engine = frame_runtime ? &frame_runtime->font_engine() : nullptr,
            .runtime = frame_runtime,
        });
        Scene sub = evaluate_scene(sub_ctx);
        if (s == 0) layer_count = static_cast<int>(sub.layers().size());
        if (native_sample_surface.handle !=
            runtime::kInvalidRenderSurfaceHandle) {
            const auto cleared = backend.initialize_transparent_surface(
                native_sample_surface.handle);
            if (!cleared.ok()) {
                throw std::runtime_error(
                    "failed to clear temporal Vulkan sample output: " +
                    cleared.error().message);
            }
        }
        const TemporalRenderContext sample_context{
            .sample_key = sample.cache_key,
            .sample_time = sample.time,
            .value_cache = sample_value_caches[static_cast<std::size_t>(s)].get(),
            .topology_cache = sample_topology_caches[static_cast<std::size_t>(s)].get(),
            .session = sample_sessions[static_cast<std::size_t>(s)].get(),
            .scratch = sample_scratches[static_cast<std::size_t>(s)].get(),
            .counters = nullptr,
            .framebuffer_pool = sample_framebuffer_pools[static_cast<std::size_t>(s)],
        };
        DeferredNativeSampleBatch deferred_batch{
            &backend,
            native_sample_surface.handle !=
                runtime::kInvalidRenderSurfaceHandle};
        auto sample_fb = call_graph(
            sub, frame, t, &sample_context, &sample_settings);
        if (!sample_fb) {
            merge_sample_reports(static_cast<std::size_t>(s + 1));
            return false;
        }
        if (cancellation && cancellation->is_cancelled()) {
            merge_sample_reports(static_cast<std::size_t>(s + 1));
            return false;
        }
        const Framebuffer& sub_fb = *sample_fb;
        if (native_sample_surface.handle !=
                runtime::kInvalidRenderSurfaceHandle &&
            sub_fb.surface_handle() != native_sample_surface.handle &&
            !backend.is_native_surface_valid(sub_fb.surface_handle())) {
            throw std::runtime_error(
                "isolated temporal graph returned neither its Vulkan sample output surface nor a valid native result surface");
        }

        // Strict Vulkan samples have no host pixel storage. Allocate
        // the accumulator lazily after the first scene has produced a
        // native source, then add normalized premultiplied RGBA
        // samples directly on the device. The legacy CPU path remains
        // for backends without native surfaces.
        const bool has_native_sample =
            sub_fb.surface_handle() !=
                runtime::kInvalidRenderSurfaceHandle &&
            backend.is_native_surface_valid(sub_fb.surface_handle());
        if (s == 0 && frame_runtime &&
            frame_runtime->backend_attached() &&
            backend.supports_native_surfaces() &&
            ssaa == 1.0f &&
            (has_native_sample || sub_fb.data())) {
            auto& surface_registry = frame_runtime->surface_registry();
            const auto desc = runtime::SurfaceDesc::make(
                static_cast<std::uint32_t>(rw),
                static_cast<std::uint32_t>(rh),
                runtime::PixelFormat::Rgba32Float,
                runtime::ResourceUsage::Storage,
                // This accumulator crosses several independent
                // Vulkan frame batches (one per temporal sample).
                // FrameTransient slots are retired between batches,
                // so keep its physical image job-persistent.
                runtime::LifetimeClass::JobPersistent);
            const auto accumulator = surface_registry.create(desc);
            if (accumulator != runtime::kInvalidRenderSurfaceHandle) {
                const auto created = backend.create_surface(accumulator, desc);
                const auto cleared = created.ok()
                    ? backend.initialize_transparent_surface(accumulator)
                    : created;
                if (cleared.ok()) {
                    native_accumulator = accumulator;
                    native_temporal_accumulation = true;
                    spdlog::info(
                        "[motion-blur] native weighted-add accumulator={} "
                        "first_sample={}",
                        accumulator, sub_fb.surface_handle());
                } else {
                    (void)backend.release_surface(accumulator);
                    (void)surface_registry.release(accumulator);
                    spdlog::warn(
                        "[motion-blur] native accumulator setup failed; "
                        "using host accumulation: {}",
                        cleared.error().message);
                }
            }
        }

        if (native_temporal_accumulation) {
            auto source_handle = sub_fb.surface_handle();
            auto& surface_registry = frame_runtime->surface_registry();
            bool release_sample_surface = false;
            if (source_handle == runtime::kInvalidRenderSurfaceHandle ||
                !backend.is_native_surface_valid(source_handle)) {
                if (!sub_fb.data()) {
                    throw std::runtime_error(
                        "Vulkan temporal sample has neither a GPU surface "
                        "nor pixels for upload");
                }
                const auto desc = runtime::SurfaceDesc::make(
                    static_cast<std::uint32_t>(rw),
                    static_cast<std::uint32_t>(rh),
                    runtime::PixelFormat::Rgba32Float,
                    runtime::ResourceUsage::Storage,
                    runtime::LifetimeClass::FrameTransient);
                source_handle = surface_registry.create(desc);
                if (source_handle == runtime::kInvalidRenderSurfaceHandle) {
                    throw std::runtime_error(
                        "failed to allocate a Vulkan temporal sample surface");
                }
                const auto created = backend.create_surface(source_handle, desc);
                if (!created.ok()) {
                    (void)surface_registry.release(source_handle);
                    throw std::runtime_error(
                        "failed to create Vulkan temporal sample surface: " +
                        created.error().message);
                }
                release_sample_surface = true;
                std::vector<float> packed;
                std::span<const float> rgba;
                if (sub_fb.stride() == rw) {
                    rgba = {reinterpret_cast<const float*>(sub_fb.data()),
                        static_cast<std::size_t>(rw) * rh * 4};
                } else {
                    packed.resize(static_cast<std::size_t>(rw) * rh * 4);
                    for (int y = 0; y < rh; ++y) {
                        std::memcpy(
                            packed.data() + static_cast<std::size_t>(y) * rw * 4,
                            sub_fb.pixels_row(y),
                            static_cast<std::size_t>(rw) * sizeof(Color));
                    }
                    rgba = packed;
                }
                runtime::UploadTicket upload_ticket{};
                const auto uploaded = backend.upload_surface_async(
                    source_handle, desc, rgba, upload_ticket);
                if (!uploaded.ok()) {
                    (void)backend.release_surface(source_handle);
                    (void)surface_registry.release(source_handle);
                    throw std::runtime_error(
                        "failed to upload Vulkan temporal sample: " +
                        uploaded.error().message);
                }
                const auto waited = backend.wait_upload(upload_ticket);
                if (!waited.ok()) {
                    (void)backend.release_surface(source_handle);
                    (void)surface_registry.release(source_handle);
                    throw std::runtime_error(
                        "failed waiting for Vulkan temporal sample upload: " +
                        waited.error().message);
                }
            } else if (sub_fb.is_cpu_authoritative()) {
                if (!sub_fb.data()) {
                    throw std::runtime_error(
                        "CPU-authoritative temporal sample has no host pixels to synchronize");
                }
                const auto source_desc = surface_registry.lookup(
                    source_handle);
                if (!source_desc) {
                    throw std::runtime_error(
                        "native temporal sample surface is missing from the registry");
                }
                const raster::BBox full_clip{0, 0, rw, rh};
                const RenderBackend::UploadRows rows{
                    .first_row = std::span<const Color>(
                        sub_fb.data(), static_cast<std::size_t>(rw)),
                    .stride = static_cast<std::size_t>(sub_fb.stride()),
                };
                runtime::UploadTicket upload_ticket{};
                const auto uploaded = backend.upload_surface_rows_async(
                    source_handle, source_desc->desc, full_clip, rows,
                    upload_ticket);
                if (!uploaded.ok()) {
                    throw std::runtime_error(
                        "failed to synchronize temporal sample to GPU: " +
                        uploaded.error().message);
                }
                const auto waited = backend.wait_upload(upload_ticket);
                if (!waited.ok()) {
                    throw std::runtime_error(
                        "failed waiting for temporal sample upload: " +
                        waited.error().message);
                }
            }
            const auto accumulated = backend.accumulate_surface_weighted(
                native_accumulator, source_handle,
                sample.weight / planned_weight_sum);
            if (!accumulated.ok()) {
                if (release_sample_surface) {
                    (void)backend.release_surface(source_handle);
                    (void)surface_registry.release(source_handle);
                }
                throw std::runtime_error(
                    "native temporal weighted-add failed (destination=" +
                    std::to_string(native_accumulator) +
                    ", source=" + std::to_string(source_handle) + "): " +
                    accumulated.error().message);
            }
            // Strict native temporal samples keep their Vulkan batch
            // open through the weighted add. End it only after the
            // accumulator has recorded its read of the sample output.
            deferred_batch.finish();
            if (!release_sample_surface &&
                source_handle != native_sample_surface.handle) {
                const auto source_record = surface_registry.lookup(
                    source_handle);
                if (source_record &&
                    source_record->desc.lifetime ==
                        runtime::LifetimeClass::FrameTransient) {
                    // The transient image has now been fenced by the
                    // batch submission; drop its registry lease. Its
                    // physical backing retires through the normal
                    // frame-batch cleanup path.
                    (void)surface_registry.release(source_handle);
                }
            }
            if (release_sample_surface) {
                (void)backend.release_surface(source_handle);
                (void)surface_registry.release(source_handle);
            }
            render_fb->mark_gpu_authoritative();
        } else {
            // Keep the CPU reference path for backends that do not
            // expose native surfaces. Never dereference a missing host
            // shadow on a strict GPU-only framebuffer.
            if (!sub_fb.data() || !render_fb->data()) {
                throw std::runtime_error(
                    "temporal accumulation has neither native surfaces nor host pixels");
            }

            tbb::parallel_for(tbb::blocked_range<int>(0, rh, 16),
                [&](const tbb::blocked_range<int>& range) {
                    using namespace hwy::HWY_NAMESPACE;
                    const ScalableTag<float> df;
                    const size_t lanes = Lanes(df);
                    const auto v_weight = Set(df, w);

                    for (int y = range.begin(); y < range.end(); ++y) {
                        const float* src = reinterpret_cast<const float*>(sub_fb.pixels_row(y));
                        float* dst = reinterpret_cast<float*>(render_fb->pixels_row(y));
                        const int total_floats = rw * 4;

                        int x = 0;
                        for (; x + static_cast<int>(lanes) <= total_floats; x += static_cast<int>(lanes)) {
                            auto acc = LoadU(df, dst + x);
                            auto vals = LoadU(df, src + x);
                            acc = MulAdd(vals, v_weight, acc);
                            StoreU(acc, df, dst + x);
                        }
                        for (; x < total_floats; ++x) {
                            dst[x] += src[x] * w;
                        }
                    }
                }
            );
        }
    }
}
native_sample_surface.release();
merge_sample_reports(sample_plan.contexts.size());
[[maybe_unused]] const double motion_blur_ms = profiling::duration_ms(t_mb0, profiling::now());

const float post_norm = (actual_weight_sum > 1e-6f)
    ? (1.0f / actual_weight_sum)
    : 1.0f;

if (!native_temporal_accumulation) {
    CHRONON_TRACE_SCOPE("chronon.effect", "motion_blur_normalize_in_place");
    tbb::parallel_for(tbb::blocked_range<int>(0, rh, 16),
        [&](const tbb::blocked_range<int>& range) {
            using namespace hwy::HWY_NAMESPACE;
            const ScalableTag<float> df;
            const size_t lanes = Lanes(df);
            const auto v_post = Set(df, post_norm);

            for (int y = range.begin(); y < range.end(); ++y) {
                float* row = reinterpret_cast<float*>(render_fb->pixels_row(y));
                const int total_floats = rw * 4;

                int x = 0;
                for (; x + static_cast<int>(lanes) <= total_floats; x += static_cast<int>(lanes)) {
                    auto v = LoadU(df, row + x);
                    v = Mul(v, v_post);
                    StoreU(v, df, row + x);
                }
                for (; x < total_floats; ++x) {
                    row[x] *= post_norm;
                }
            }
        }
    );
} else {
    const auto encode_surface = settings.native_video_encode_surface;
    spdlog::info(
        "[motion-blur] native weighted-add finalize accumulator={} "
        "settings_encode={} framebuffer_surface={} destination={}",
        native_accumulator, encode_surface,
        render_fb->surface_handle(), encode_surface);
    if (encode_surface != runtime::kInvalidRenderSurfaceHandle) {
        const auto copied = backend.copy_surface_to_video_encode(
            native_accumulator, encode_surface);
        if (!copied.ok()) {
            throw std::runtime_error(
                "native temporal accumulator could not be copied to "
                "the frame output surface: " + copied.error().message);
        }
        render_fb->set_surface_handle(
            encode_surface);
        auto& surface_registry = frame_runtime->surface_registry();
        (void)backend.release_surface(native_accumulator);
        (void)surface_registry.release(native_accumulator);
        native_accumulator = runtime::kInvalidRenderSurfaceHandle;
        render_fb->mark_gpu_authoritative();
    } else if (render_fb->data()) {
        // Non-native sinks need host pixels. Keep all sample
        // rendering and weighted accumulation on Vulkan, then do
        // one explicit readback and retire the per-frame image.
        std::vector<float> rgba(
            static_cast<std::size_t>(rw) * rh * 4);
        const auto downloaded = backend.download_surface(
            native_accumulator, rgba);
        if (!downloaded.ok()) {
            throw std::runtime_error(
                "native temporal accumulator readback failed: " +
                downloaded.error().message);
        }
        static_assert(sizeof(Color) == sizeof(float) * 4);
        for (int y = 0; y < rh; ++y) {
            std::memcpy(
                render_fb->pixels_row(y),
                rgba.data() + static_cast<std::size_t>(y) * rw * 4,
                static_cast<std::size_t>(rw) * sizeof(Color));
        }
        render_fb->mark_cpu_authoritative();
        render_fb->clear_surface_handle();
        auto& surface_registry = frame_runtime->surface_registry();
        (void)backend.release_surface(native_accumulator);
        (void)surface_registry.release(native_accumulator);
        native_accumulator = runtime::kInvalidRenderSurfaceHandle;
    } else {
        // Retained native consumers own the result handle beyond this
        // call; their existing surface lifecycle releases it.
        render_fb->set_surface_handle(native_accumulator);
        render_fb->mark_gpu_authoritative();
    }
}
return true;
}

}  // namespace chronon3d::graph::detail
