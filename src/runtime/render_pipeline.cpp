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
#include <chronon3d/runtime/job/job_session.hpp>
#include <chronon3d/runtime/frame/frame_execution.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_execution_context.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include "../render_graph/pipeline/temporal_render_pipeline.hpp"
#include <chronon3d/render_graph/render_backend.hpp>
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
#include <array>
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <vector>
#include <stdexcept>
#include <thread>
#include <utility>
#include <memory>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

namespace chronon3d::runtime {

// 06 R3b — SoftwareRenderer passed in by pointer (a single canonical type
// the RenderPipeline FACADE stores and forwards to graph free functions).
RenderPipeline::RenderPipeline(
    SoftwareRenderer* renderer, ::chronon3d::SoftwareExecutionContext& context) noexcept
    : m_renderer(renderer), m_context(context) {}

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

std::shared_ptr<Framebuffer> RenderPipeline::render_scene(
    const Scene& scene, const Camera& camera, i32 width, i32 height,
    float fps)
{
    // Dispatch through the already-resolved backend and dependency views;
    // RuntimeContext lookup is intentionally absent from this hot path.
    return chronon3d::graph::render_scene_via_graph(
        *m_context.backend,
        *m_context.scratch.node_cache,
        scene, camera, width, height,
        /*frame=*/0, /*frame_time=*/0.0f,
        *m_context.settings,
        m_context.registry.compositions,
        m_context.video_decoder,
        fps, "scene", m_renderer
    );
}

std::shared_ptr<Framebuffer> RenderPipeline::render_scene(
    const Scene& scene, const std::optional<Camera2_5D>& camera,
    i32 width, i32 height,
    float fps)
{
    // 2.5D variant: clone the scene, stamp the optional camera
    // onto it, then route through the standard Scene path.
    Scene effective = scene.clone();
    if (camera.has_value()) {
        effective.set_camera_2_5d(*camera);
    }
    Camera default_camera;
    return chronon3d::graph::render_scene_via_graph(
        *m_context.backend,
        *m_context.scratch.node_cache,
        effective, default_camera, width, height,
        /*frame=*/0, /*frame_time=*/0.0f,
        *m_context.settings,
        m_context.registry.compositions,
        m_context.video_decoder,
        fps, "scene", m_renderer
    );
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
    std::size_t element_state_count) {
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
    if (execution) {
        compiler::TypedProgramSampler{}.sample_into(
            program, frame, execution->parameters());
        (void)motion::apply_layer_states(layer_states, layer_state_count,
                                          execution->parameters());
        (void)motion::apply_element_states(element_states, element_state_count,
                                            execution->parameters());
        frame_parameters = &execution->parameters();
    } else {
        compiler::TypedProgramSampler{}.sample_into(
            program, frame, compatibility_parameters);
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
    const auto key = program.fingerprint();
    const auto topology = m_topologies.compute_if_absent(
        key, [&]() -> std::pair<std::shared_ptr<const Scene>, std::size_t> {
            return {std::make_shared<const Scene>(
                        compiler::SceneMaterializer{}.materialize_scene(program)),
                    1};
        });

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

        std::vector<std::unique_ptr<cache::NodeCache>> value_caches;
        std::vector<std::unique_ptr<graph::CompiledGraphCache>> topology_caches;
        std::vector<std::unique_ptr<JobSession>> sample_sessions;
        std::vector<std::unique_ptr<TransformScratchBuffer>> scratches;
        std::vector<std::shared_ptr<cache::FramebufferPool>> framebuffer_pools;
        const auto count = samples.contexts.size();
        value_caches.reserve(count);
        topology_caches.reserve(count);
        sample_sessions.reserve(count);
        scratches.reserve(count);
        framebuffer_pools.reserve(count);
        const auto pool_limit = m_renderer->framebuffer_pool()->stats().budget_bytes;
        for (std::size_t i = 0; i < count; ++i) {
            value_caches.push_back(std::make_unique<cache::NodeCache>());
            topology_caches.push_back(std::make_unique<graph::CompiledGraphCache>());
            sample_sessions.push_back(std::make_unique<JobSession>());
            scratches.push_back(std::make_unique<TransformScratchBuffer>());
            framebuffer_pools.push_back(
                cache::FramebufferPool::create_shared(pool_limit));
        }

        auto accumulated = framebuffer_pools.front()->acquire(
            width, height, /*clear=*/true);
        float weight_sum = 0.0f;
        compiler::TypedProgramSampler sampler;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& sample = samples[i];
            compiler::FrameParameterBlock parameters;
            sampler.sample_into(program, sample.time.frame, parameters);
            (void)motion::apply_layer_states(layer_states, layer_state_count, parameters);
            (void)motion::apply_element_states(element_states, element_state_count, parameters);
            const float frame_offset = static_cast<float>(
                sample.time.frame - static_cast<double>(frame.integral()));
            const graph::TemporalRenderContext temporal_context{
                .sample_key = sample.cache_key,
                .sample_time = sample.time,
                .value_cache = value_caches[i].get(),
                .topology_cache = topology_caches[i].get(),
                .session = sample_sessions[i].get(),
                .scratch = scratches[i].get(),
                .counters = nullptr,
                .framebuffer_pool = framebuffer_pools[i],
            };
            const auto rendered = graph::render_scene_via_graph_temporal(
                *m_context.backend, *m_context.scratch.node_cache, *topology,
                Camera{}, width, height, frame, frame_offset, settings,
                m_context.registry.compositions, m_context.video_decoder,
                static_cast<float>(canvas_fps.fps()), metadata.job_id, m_renderer,
                &temporal_context, nullptr, &parameters, key);
            if (!rendered)
                throw std::runtime_error(
                    "Render Plan motion-blur temporal sample produced no framebuffer");
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
        return accumulated;
    }

    return chronon3d::graph::render_scene_via_graph(
        *m_context.backend, *m_context.scratch.node_cache, *topology, Camera{},
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

std::string RenderPipeline::debug_graph(
    const Scene& scene, const Camera& camera, i32 width, i32 height,
    float fps, Frame frame, f32 frame_time)
{
    return chronon3d::graph::debug_scene_graph(
        *m_context.backend,
        *m_context.scratch.node_cache,
        scene, camera, width, height,
        frame, frame_time,
        *m_context.settings,
        m_context.registry.compositions,
        m_context.video_decoder,
        fps
    );
}

} // namespace chronon3d::runtime
