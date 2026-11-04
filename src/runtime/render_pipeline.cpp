// ===========================================================================
// runtime/render_pipeline.cpp
//
// RenderPipeline implementation — thin pass-through to the orchestrating
// graph free functions in `<chronon3d/render_graph/pipeline/render_pipeline.hpp>`.
// The facade exists for type safety + forward-compat with the TICKET-011
// final-stage split.
//
// All paths read the explicit per-job SoftwareExecutionContext. RuntimeContext
// remains the lifetime owner, but is no longer a stored orchestration
// dependency on this hot path.
// ===========================================================================

#include <chronon3d/runtime/render_pipeline.hpp>
#include "scene_topology_cache.hpp"
#include <chronon3d/runtime/job/job_session.hpp>
#include <chronon3d/runtime/job/temporal_sample_workspace.hpp>
#include <chronon3d/runtime/frame/frame_execution.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_execution_context.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include "../render_graph/pipeline/temporal/temporal_render_pipeline.hpp"
#include "../render_plan/render_plan_camera_adapter.hpp"
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>
#include <chronon3d/backends/software/scratch_buffer.hpp>
#include <chronon3d/animation/temporal/temporal_samples.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/compiler/scene_materializer.hpp>
#include <chronon3d/compiler/typed_program_sampler.hpp>
#include <chronon3d/motion/contract_apply.hpp>
#include <chronon3d/motion/element_contract.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>
#include <stdexcept>
#include <thread>
#include <utility>
#include <memory>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

namespace chronon3d::runtime {

const char* frame_scheduling_class_name(
    FrameSchedulingClass scheduling_class) noexcept {
    switch (scheduling_class) {
        case FrameSchedulingClass::Backpressure:
            return "Backpressure";
        case FrameSchedulingClass::InvariantViolation:
            return "InvariantViolation";
        case FrameSchedulingClass::JobFailure:
            return "JobFailure";
    }
    return "Unknown";
}

const char* frame_scheduling_code_name(FrameSchedulingCode code) noexcept {
    switch (code) {
        case FrameSchedulingCode::NoFreeFrameSlot:
            return "NoFreeFrameSlot";
        case FrameSchedulingCode::PublishEvaluatedFailed:
            return "PublishEvaluatedFailed";
        case FrameSchedulingCode::AcquireRenderSlotFailed:
            return "AcquireRenderSlotFailed";
        case FrameSchedulingCode::PublishRenderedFailed:
            return "PublishRenderedFailed";
        case FrameSchedulingCode::ReleaseEncodedSlotFailed:
            return "ReleaseEncodedSlotFailed";
        case FrameSchedulingCode::ExecutorFailed:
            return "ExecutorFailed";
    }
    return "Unknown";
}

std::shared_ptr<Framebuffer> RenderPipeline::render_compiled_composition(
    const CompiledComposition& compiled, Frame frame, FrameExecution* execution)
{
    return chronon3d::graph::render_compiled_composition_frame(
        *m_context.backend,
        *m_context.scratch.node_cache,
        *m_context.settings,
        m_context.registry.compositions,
        m_context.video_decoder,
        compiled,
        frame,
        m_renderer /* R3 sidecar */, execution
    );
}

std::shared_ptr<Framebuffer> RenderPipeline::render_program(
    const compiler::CompiledProgram& program, Frame frame,
    FrameExecution* execution, const chronon_layer_state* layer_states,
    std::size_t layer_state_count, const chronon_element_state* element_states,
    std::size_t element_state_count, const CancellationToken* cancellation) {
    if (cancellation && cancellation->is_cancelled()) return {};
    const auto& metadata = program.metadata();
    const FrameRate canvas_fps{metadata.fps_numerator, metadata.fps_denominator};
    // TypedProgramSampler is the only frame-varying producer on this route.
    // The scene below is immutable graph topology; activity, transforms and
    // typed payloads come from the leased FrameExecution block.  Reusing the
    // slot-owned vector keeps the steady-state frame path allocation-free.
    compiler::FrameParameterBlock compatibility_parameters;
    const compiler::FrameParameterBlock* frame_parameters = nullptr;
    // The ADR-032 overlay lands here, on the sampled block itself, so a
    // contract frame cannot reach one consumer of `frame_parameters` and miss
    // another. A record set that fails the ABI discipline has already been
    // refused by the caller (validate_layer_states); if one slips through, the
    // accepted records still apply and the frame stays coherent.
    const auto sample_program = [&](compiler::FrameParameterBlock& parameters) {
        const auto sample_start = std::chrono::steady_clock::now();
        compiler::TypedProgramSampler{}.sample_into(program, frame, parameters);
        const auto sample_end = std::chrono::steady_clock::now();
        if (m_context.counters) {
            const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                sample_end - sample_start).count();
            m_context.counters->program_sample_wall_us.fetch_add(
                static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed_us)),
                std::memory_order_relaxed);
        }
    };
    if (execution) {
        sample_program(execution->parameters());
        (void)motion::apply_layer_states(layer_states, layer_state_count,
                                          execution->parameters());
        (void)motion::apply_element_states(element_states, element_state_count,
                                            execution->parameters());
        frame_parameters = &execution->parameters();
    } else {
        sample_program(compatibility_parameters);
        (void)motion::apply_layer_states(layer_states, layer_state_count,
                                          compatibility_parameters);
        (void)motion::apply_element_states(element_states, element_state_count,
                                            compatibility_parameters);
        frame_parameters = &compatibility_parameters;
    }
    // AUDIT F11 — the canonical LruCache owns lookup, LRU eviction and request
    // coalescing. materialize_scene (Scene::clone + topology build) is heavy and
    // runs outside the shard lock: concurrent jobs asking for the same
    // fingerprint share one materialization instead of racing to build it twice,
    // and a bounded Count-mode cache evicts the least recently *used* entry
    // rather than whichever was inserted first.
    const auto topology_key = program.fingerprint_data();
    const auto key = program.fingerprint();
    bool materialized_here = false;
    const auto materialize = [&]()
        -> std::pair<std::shared_ptr<const Scene>, std::size_t> {
        materialized_here = true;
        const auto materialize_start = std::chrono::steady_clock::now();
        // Text layers need the runtime font engine to shape their glyphs;
        // without it a compiled text layer materializes with no nodes and
        // the graph renders the background alone.
        auto* const font_engine = m_context.runtime
            ? &m_context.runtime->font_engine() : nullptr;
        try {
            auto scene = std::make_shared<const Scene>(
                compiler::SceneMaterializer{}.materialize_scene(
                    program, std::pmr::get_default_resource(), font_engine));
            if (m_context.counters) {
                const auto materialize_end = std::chrono::steady_clock::now();
                const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    materialize_end - materialize_start).count();
                m_context.counters->scene_materialize_wall_us.fetch_add(
                    static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed_us)),
                    std::memory_order_relaxed);
                m_context.counters->scene_materialize_count.fetch_add(
                    1, std::memory_order_relaxed);
            }
            return {std::move(scene), 1};
        } catch (...) {
            if (m_context.counters) {
                const auto materialize_end = std::chrono::steady_clock::now();
                const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    materialize_end - materialize_start).count();
                m_context.counters->scene_materialize_wall_us.fetch_add(
                    static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed_us)),
                    std::memory_order_relaxed);
            }
            throw;
        }
    };
    // Prefer the renderer-owned cache: probe, warmup and the export loop build
    // different RenderPipeline objects but share this execution context, so
    // one program fingerprint is materialized once instead of once per
    // pipeline. The per-pipeline cache stays as the fallback for contexts
    // that carry no shared cache (unit fixtures).
    std::shared_ptr<const Scene> topology;
    if (m_context.topology_cache) {
        topology = m_context.topology_cache->cache().compute_if_absent(
            topology_key, materialize);
    } else {
        topology = m_topologies.compute_if_absent(topology_key, materialize);
    }
    if (!materialized_here && m_context.counters) {
        m_context.counters->scene_materialize_cache_hit_count.fetch_add(
            1, std::memory_order_relaxed);
    }

    // The cached topology is immutable. Only animated-camera programs need a
    // per-render scene copy so all graph consumers (including dirty/reuse
    // policy) observe the same resolved camera without mutating the cache.
    std::optional<Scene> camera_scene;
    if (!metadata.camera_samples.empty()) camera_scene.emplace(topology->clone());
    const auto scene_at = [&](double sample_frame) -> const Scene& {
        if (!camera_scene) return *topology;
        camera_scene->set_camera_2_5d(render_plan::detail::sample_resolved_camera(
            metadata.camera_samples, sample_frame));
        return *camera_scene;
    };

    const auto& settings = *m_context.settings;
    if (settings.motion_blur.mode == MotionBlurMode::TemporalAccumulation &&
        settings.motion_blur.samples > 1 &&
        settings.motion_blur.shutter_angle_deg > 0.0f) {
        const auto width = static_cast<i32>(metadata.width);
        const auto height = static_cast<i32>(metadata.height);
        temporal::TemporalSampleParams shutter;
        shutter.shutter_angle_deg = settings.motion_blur.shutter_angle_deg;
        shutter.shutter_phase_deg = settings.motion_blur.shutter_phase_deg;
        shutter.pattern = settings.motion_blur.pattern;
        shutter.filter = settings.motion_blur.filter;
        shutter.jitter_seed = settings.motion_blur.jitter_seed;
        const auto samples = temporal::make_temporal_sample_plan(
            shutter, settings.motion_blur.samples, frame, canvas_fps,
            static_cast<std::size_t>(width), static_cast<std::size_t>(height),
            key, temporal::TemporalBudgetResolver{
                settings.render_budget.max_temporal_pixels});
        if (!samples.valid())
            throw std::invalid_argument(
                "Render Plan motion-blur temporal sample plan is invalid");

        // Render settings can differ from the plan used to bake resolved Motion
        // values. Do not interpolate an exact bake at new shutter instants.
        const bool has_resolved_layers = std::any_of(
            program.layers().begin(), program.layers().end(),
            [](const auto& layer) { return !layer.transform_samples.empty(); });
        if (has_resolved_layers) {
            for (const auto& sample : samples.contexts) {
                const double time = sample.time.frame;
                if (!metadata.camera_samples.empty()) {
                    const auto camera = std::lower_bound(
                        metadata.camera_samples.begin(), metadata.camera_samples.end(), time,
                        [](const auto& entry, double value) { return entry.first < value; });
                    if (camera == metadata.camera_samples.end() || camera->first != time)
                        throw std::invalid_argument(
                            "Render Plan resolved camera is missing an actual shutter sample");
                }
                for (const auto& layer : program.layers()) {
                    if (layer.transform_samples.empty()) continue;
                    const auto transform = std::lower_bound(
                        layer.transform_samples.begin(), layer.transform_samples.end(), time,
                        [](const auto& entry, double value) { return entry.frame < value; });
                    if (transform == layer.transform_samples.end() || transform->frame != time)
                        throw std::invalid_argument(
                            "Render Plan resolved layer '" + layer.name +
                            "' is missing an actual shutter sample");
                }
            }
        }

        const auto count = samples.contexts.size();
        const auto pool_limit = m_renderer->framebuffer_pool()->stats().budget_bytes;
        TemporalSampleWorkspace compatibility_workspace;
        auto& workspace = execution
            ? execution->job().temporal_sample_workspace(
                  execution->slot()->slot_id)
            : compatibility_workspace;
        workspace.ensure(count, pool_limit);
        workspace.begin_frame();

        auto accumulated = workspace.framebuffer_pool(0)->acquire(
            width, height, /*clear=*/true);
        // The typed pipeline owns this sub-frame accumulation: each sample is
        // a SHARP render at its own sub-frame time (the FrameParameterBlock is
        // re-sampled per sample below). Render the samples with motion blur
        // disabled so the composition layer cannot re-enter its own temporal
        // accumulator, which knows nothing about typed frame parameters and
        // would render every sub-scene empty.
        RenderSettings sample_settings = settings;
        sample_settings.motion_blur.mode = MotionBlurMode::Off;
        sample_settings.motion_blur.samples = 1;
        struct NativeTemporalSurfaces {
            graph::RenderBackend* backend{};
            runtime::RenderSurfaceRegistry* registry{};
            runtime::RenderSurfaceHandle sample{runtime::kInvalidRenderSurfaceHandle};
            runtime::RenderSurfaceHandle accumulator{runtime::kInvalidRenderSurfaceHandle};
            void release(runtime::RenderSurfaceHandle& handle) noexcept {
                if (handle == runtime::kInvalidRenderSurfaceHandle) return;
                if (backend) (void)backend->release_surface(handle);
                if (registry) (void)registry->release(handle);
                handle = runtime::kInvalidRenderSurfaceHandle;
            }
            ~NativeTemporalSurfaces() {
                release(accumulator);
                release(sample);
            }
        } native_surfaces{m_context.backend,
                          m_context.runtime ? &m_context.runtime->surface_registry() : nullptr};
        // An isolated native sample defers its graph batch to its temporal
        // parent, which must submit the batch after the weighted read. Keep
        // that ownership balanced on errors as well as on the success path.
        struct DeferredNativeSampleBatch {
            graph::RenderBackend* backend{};
            bool active{};
            void finish() noexcept {
                if (!active) return;
                backend->end_frame_batch();
                active = false;
            }
            ~DeferredNativeSampleBatch() { finish(); }
        };
        const bool strict_native = settings.require_native_gpu;
        if (strict_native) {
            if (!native_surfaces.backend || !native_surfaces.registry ||
                !native_surfaces.backend->supports_native_surfaces()) {
                throw std::runtime_error(
                    "strict-native typed shutter accumulation requires an attached native backend and surface registry");
            }
            const auto desc = runtime::SurfaceDesc::make(
                static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height),
                runtime::PixelFormat::Rgba32Float,
                runtime::ResourceUsage::Storage,
                runtime::LifetimeClass::PipelineSlot);
            native_surfaces.sample = native_surfaces.registry->create(desc);
            native_surfaces.accumulator = native_surfaces.registry->create(desc);
            if (native_surfaces.sample == runtime::kInvalidRenderSurfaceHandle ||
                native_surfaces.accumulator == runtime::kInvalidRenderSurfaceHandle) {
                throw std::runtime_error(
                    "failed to allocate strict-native typed shutter surfaces");
            }
            const auto sample_created = native_surfaces.backend->create_surface(
                native_surfaces.sample, desc);
            const auto accumulator_created = native_surfaces.backend->create_surface(
                native_surfaces.accumulator, desc);
            if (!sample_created.ok() || !accumulator_created.ok()) {
                throw std::runtime_error(
                    "failed to create strict-native typed shutter surfaces");
            }
            const auto cleared = native_surfaces.backend->initialize_transparent_surface(
                native_surfaces.accumulator);
            if (!cleared.ok()) {
                throw std::runtime_error(
                    "failed to clear strict-native typed shutter accumulator: " +
                    cleared.error().message);
            }
            sample_settings.native_video_encode_surface = native_surfaces.sample;
            sample_settings.native_video_source_surface = runtime::kInvalidRenderSurfaceHandle;
            sample_settings.native_video_previous_surface = runtime::kInvalidRenderSurfaceHandle;
        }
        float weight_sum = 0.0f;
        float planned_weight_sum = 0.0f;
        for (const auto& sample : samples.contexts) planned_weight_sum += sample.weight;
        if (!std::isfinite(planned_weight_sum) ||
            std::abs(planned_weight_sum) <= 1e-6f) {
            throw std::invalid_argument(
                "Render Plan temporal shutter weights have an invalid sum");
        }
        compiler::TypedProgramSampler sampler;
        for (std::size_t i = 0; i < count; ++i) {
            if (cancellation && cancellation->is_cancelled()) return {};
            const auto& sample = samples[i];
            if (strict_native) {
                const auto cleared = native_surfaces.backend->initialize_transparent_surface(
                    native_surfaces.sample);
                if (!cleared.ok()) {
                    throw std::runtime_error(
                        "failed to clear strict-native typed shutter sample: " +
                        cleared.error().message);
                }
            }
            compiler::FrameParameterBlock parameters;
            sampler.sample_into(program, sample.time.frame, parameters);
            (void)motion::apply_layer_states(layer_states, layer_state_count, parameters);
            (void)motion::apply_element_states(element_states, element_state_count, parameters);
            const float frame_offset = static_cast<float>(
                sample.time.frame - static_cast<double>(frame.integral()));
            const graph::TemporalRenderContext temporal_context{
                .sample_key = sample.cache_key,
                .sample_time = sample.time,
                .value_cache = &workspace.value_cache(i),
                .topology_cache = &workspace.topology_cache(i),
                .session = &workspace.session(i),
                .scratch = &workspace.transform_scratch(i),
                .counters = nullptr,
                .framebuffer_pool = workspace.framebuffer_pool(i),
            };
            DeferredNativeSampleBatch deferred_batch{m_context.backend, strict_native};
            const auto rendered = graph::render_scene_via_graph_temporal(
                *m_context.backend, *m_context.scratch.node_cache, scene_at(sample.time.frame),
                Camera{}, width, height, frame, frame_offset, sample_settings,
                m_context.registry.compositions, m_context.video_decoder,
                static_cast<float>(canvas_fps.fps()), metadata.job_id, m_renderer,
                &temporal_context, nullptr, &parameters, key);
            if (!rendered)
                throw std::runtime_error(
                    "Render Plan motion-blur temporal sample produced no framebuffer");
            if (strict_native) {
                const auto source = rendered->surface_handle();
                if (source == runtime::kInvalidRenderSurfaceHandle ||
                    !native_surfaces.backend->is_native_surface_valid(source)) {
                    throw std::runtime_error(
                        "strict-native typed shutter sample has no valid GPU surface");
                }
                const auto accumulated_gpu = native_surfaces.backend->accumulate_surface_weighted(
                    native_surfaces.accumulator, source,
                    sample.weight / planned_weight_sum);
                if (!accumulated_gpu.ok()) {
                    throw std::runtime_error(
                        "strict-native typed shutter weighted-add failed: " +
                        accumulated_gpu.error().message);
                }
                deferred_batch.finish();
                continue;
            }
            // The loop below is a CPU pixel accumulator. A strict-native graph
            // can return a GPU-authoritative framebuffer with no host storage;
            // reject that capability mismatch before pixels_row() reaches a
            // worker thread and turns it into a process-level SIGSEGV.
            if (!rendered->data() || !accumulated->data())
                throw std::runtime_error(
                    "Render Plan temporal motion-blur CPU accumulation requires "
                    "host framebuffer pixels; strict GPU-native samples require "
                    "a native weighted-add accumulator");
            const float weight = sample.weight;
            weight_sum += weight;
            tbb::parallel_for(tbb::blocked_range<i32>(0, height, 16),
                [&](const tbb::blocked_range<i32>& rows) {
                    for (i32 y = rows.begin(); y < rows.end(); ++y) {
                        const Color* source = rendered->pixels_row(y);
                        Color* destination = accumulated->pixels_row(y);
                        for (i32 x = 0; x < width; ++x) {
                            destination[x].r += source[x].r * weight;
                            destination[x].g += source[x].g * weight;
                            destination[x].b += source[x].b * weight;
                            destination[x].a += source[x].a * weight;
                        }
                    }
                });
        }
        if (weight_sum > 1e-6f && std::abs(weight_sum - 1.0f) > 1e-6f) {
            const float inverse_weight = 1.0f / weight_sum;
            tbb::parallel_for(tbb::blocked_range<i32>(0, height, 16),
                [&](const tbb::blocked_range<i32>& rows) {
                    for (i32 y = rows.begin(); y < rows.end(); ++y) {
                        Color* destination = accumulated->pixels_row(y);
                        for (i32 x = 0; x < width; ++x) {
                            destination[x].r *= inverse_weight;
                            destination[x].g *= inverse_weight;
                            destination[x].b *= inverse_weight;
                            destination[x].a *= inverse_weight;
                        }
                    }
                });
        }
        if (strict_native) {
            const auto encode_surface = settings.native_video_encode_surface;
            if (encode_surface != runtime::kInvalidRenderSurfaceHandle) {
                const auto copied = native_surfaces.backend->copy_surface_to_video_encode(
                    native_surfaces.accumulator, encode_surface);
                if (!copied.ok()) {
                    throw std::runtime_error(
                        "strict-native typed shutter could not publish the accumulated output: " +
                        copied.error().message);
                }
                accumulated->set_surface_handle(encode_surface);
                accumulated->mark_gpu_authoritative();
            } else {
                std::vector<float> rgba(static_cast<std::size_t>(width) * height * 4);
                const auto downloaded = native_surfaces.backend->download_surface(
                    native_surfaces.accumulator, rgba);
                if (!downloaded.ok()) {
                    throw std::runtime_error(
                        "strict-native typed shutter final readback failed: " +
                        downloaded.error().message);
                }
                static_assert(sizeof(Color) == sizeof(float) * 4);
                for (i32 y = 0; y < height; ++y) {
                    std::memcpy(accumulated->pixels_row(y),
                        rgba.data() + static_cast<std::size_t>(y) * width * 4,
                        static_cast<std::size_t>(width) * sizeof(Color));
                }
                accumulated->mark_cpu_authoritative();
            }
            return accumulated;
        }
        return accumulated;
    }

    return chronon3d::graph::render_scene_via_graph(
        *m_context.backend, *m_context.scratch.node_cache,
        scene_at(static_cast<double>(frame.integral())), Camera{},
        static_cast<i32>(metadata.width), static_cast<i32>(metadata.height), frame, 0.0f,
        *m_context.settings, m_context.registry.compositions,
        m_context.video_decoder, static_cast<float>(canvas_fps.fps()),
        metadata.job_id, m_renderer, execution, frame_parameters,
        program.fingerprint());
}

FrameRenderResult RenderPipeline::render(
    JobSession& session, Frame frame) {
    session.begin_job();
    const auto* program = session.program_ptr().get();
    if (!program) {
        throw std::runtime_error(
            "RenderPipeline::render requires a CompiledProgram");
    }

    auto* slot = session.acquire_for_evaluation();
    if (!slot) {
        return FrameSchedulingCode::NoFreeFrameSlot;
    }
    slot->frame_index = frame.integral();
    auto& payload = session.payload(*slot);
    payload.reset();

    const FrameRate frame_rate{
        program->metadata().fps_numerator,
        program->metadata().fps_denominator};
    const auto frame_time = SampleTime::from_frame_int(frame, frame_rate);
    FrameExecution execution(session, frame, frame_time, *slot, payload);

    // Expected scheduling states are returned, not thrown (audit N5).
    if (!session.publish_evaluated(*slot)) {
        (void)session.abort(*slot);
        return FrameSchedulingCode::PublishEvaluatedFailed;
    }
    auto* render_slot = session.acquire_for_render();
    if (!render_slot) {
        (void)session.abort(*slot);
        return FrameSchedulingCode::AcquireRenderSlotFailed;
    }
    // Outcomes inside the render try-block keep the former exception path's
    // cleanup: close the frame execution, then abort the render slot.
    auto fail_with = [&](FrameSchedulingCode code) -> FrameRenderResult {
        execution.fail();
        if (render_slot->state.load(std::memory_order_acquire) !=
                FrameSlotState::Free) {
            (void)session.abort(*render_slot);
        }
        return code;
    };

    try {
        auto output = render_program(*program, frame, &execution);
        if (!output) {
            if (const auto frame_error = session.last_frame_error()) {
                session.fail_job(frame_error->canonical_error(
                    frame.integral(), session.job_id()));
            } else {
                session.fail_job(chronon::make_error(
                    chronon::ErrorKind::InvariantViolation, "render_pipeline",
                    "MissingFrameOutput", "executor returned no framebuffer",
                    "render_pipeline", {}, {}, frame.integral(), session.job_id()));
            }
            return fail_with(FrameSchedulingCode::ExecutorFailed);
        }
        if (!session.publish_rendered(*render_slot)) {
            return fail_with(FrameSchedulingCode::PublishRenderedFailed);
        }
        auto* encode_slot = session.acquire_for_encoding();
        if (!encode_slot || encode_slot != render_slot ||
            !session.begin_encoding(*encode_slot) ||
            !session.release_encoded(*encode_slot)) {
            if (encode_slot && encode_slot->state.load(std::memory_order_acquire) !=
                    FrameSlotState::Free) {
                (void)session.abort(*encode_slot);
            }
            return fail_with(FrameSchedulingCode::ReleaseEncodedSlotFailed);
        }
        session.complete_job();
        return output;
    } catch (...) {
        if (session.state() != JobState::Failed) {
            session.fail_job(session.cancellation().is_cancelled()
                ? chronon::make_error(
                    chronon::ErrorKind::Cancelled, "render_pipeline",
                    "Cancelled", "render pipeline cancelled by caller",
                    "render_pipeline", {}, {}, frame.integral(), session.job_id())
                : chronon::make_error(
                    chronon::ErrorKind::FrameFailed, "render_pipeline",
                    "PipelineException", "render pipeline aborted the frame",
                    "render_pipeline", {}, {}, frame.integral(), session.job_id()));
        }
        execution.fail();
        if (render_slot && render_slot->state.load(std::memory_order_acquire) !=
                FrameSlotState::Free) {
            (void)session.abort(*render_slot);
        }
        throw;
    }
}

FrameRenderResult RenderPipeline::render_compatibility(
    JobSession& session, const CompiledComposition& compiled, Frame frame) {
    session.begin_job();
    auto* slot = session.acquire_for_evaluation();
    if (!slot) {
        return FrameSchedulingCode::NoFreeFrameSlot;
    }
    slot->frame_index = frame.integral();
    auto& payload = session.payload(*slot);
    payload.reset();
    const auto frame_time = SampleTime::from_frame_int(
        frame, compiled.composition ? compiled.composition->frame_rate() : FrameRate{});
    FrameExecution execution(session, frame, frame_time, *slot, payload);

    // Same contract as render(): expected scheduling states are returned.
    if (!session.publish_evaluated(*slot)) {
        (void)session.abort(*slot);
        return FrameSchedulingCode::PublishEvaluatedFailed;
    }
    auto* render_slot = session.acquire_for_render();
    if (!render_slot) {
        (void)session.abort(*slot);
        return FrameSchedulingCode::AcquireRenderSlotFailed;
    }
    auto fail_with = [&](FrameSchedulingCode code) -> FrameRenderResult {
        execution.fail();
        if (render_slot->state.load(std::memory_order_acquire) !=
                FrameSlotState::Free) {
            (void)session.abort(*render_slot);
        }
        return code;
    };
    try {
        auto output = render_compiled_composition(compiled, frame, &execution);
        if (!output) {
            if (const auto frame_error = session.last_frame_error()) {
                session.fail_job(frame_error->canonical_error(
                    frame.integral(), session.job_id()));
            } else {
                session.fail_job(chronon::make_error(
                    chronon::ErrorKind::InvariantViolation, "render_pipeline",
                    "MissingFrameOutput", "executor returned no framebuffer",
                    "render_pipeline", {}, {}, frame.integral(), session.job_id()));
            }
            return fail_with(FrameSchedulingCode::ExecutorFailed);
        }
        if (!session.publish_rendered(*render_slot)) {
            return fail_with(FrameSchedulingCode::PublishRenderedFailed);
        }
        auto* encode_slot = session.acquire_for_encoding();
        if (!encode_slot || encode_slot != render_slot ||
            !session.begin_encoding(*encode_slot) ||
            !session.release_encoded(*encode_slot)) {
            if (encode_slot && encode_slot->state.load(std::memory_order_acquire) !=
                    FrameSlotState::Free) {
                (void)session.abort(*encode_slot);
            }
            return fail_with(FrameSchedulingCode::ReleaseEncodedSlotFailed);
        }
        session.complete_job();
        return output;
    } catch (...) {
        if (session.state() != JobState::Failed) {
            session.fail_job(session.cancellation().is_cancelled()
                ? chronon::make_error(
                    chronon::ErrorKind::Cancelled, "render_pipeline",
                    "Cancelled", "render pipeline cancelled by caller",
                    "render_pipeline", {}, {}, frame.integral(), session.job_id())
                : chronon::make_error(
                    chronon::ErrorKind::FrameFailed, "render_pipeline",
                    "PipelineException", "render pipeline aborted the frame",
                    "render_pipeline", {}, {}, frame.integral(), session.job_id()));
        }
        execution.fail();
        if (render_slot && render_slot->state.load(std::memory_order_acquire) !=
                FrameSlotState::Free) {
            (void)session.abort(*render_slot);
        }
        throw;
    }
}

} // namespace chronon3d::runtime
