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
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/compiler/program_sampler.hpp>
#include <chronon3d/compiler/typed_program_sampler.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace chronon3d::runtime {

// 06 R3b — SoftwareRenderer passed in by pointer (a single canonical type
// the RenderPipeline FACADE stores and forwards to graph free functions).
RenderPipeline::RenderPipeline(
    SoftwareRenderer* renderer, ::chronon3d::SoftwareExecutionContext& context) noexcept
    : m_renderer(renderer), m_context(context) {}

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
    // onto it, then route through the standard Scene path.  Mirrors
    // `SoftwareRenderer::render_scene(scene, optional<Camera2_5D>, …)`
    // semantics exactly.
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
    FrameExecution* execution) {
    const auto& metadata = program.metadata();
    const FrameRate canvas_fps{metadata.fps_numerator, metadata.fps_denominator};
    // TypedProgramSampler is the only frame-varying producer on this route.
    // The scene below is immutable graph topology; activity, transforms and
    // typed payloads come from the leased FrameExecution block.  Reusing the
    // slot-owned vector keeps the steady-state frame path allocation-free.
    compiler::FrameParameterBlock compatibility_parameters;
    const compiler::FrameParameterBlock* frame_parameters = nullptr;
    if (execution) {
        compiler::TypedProgramSampler{}.sample_into(
            program, frame, execution->parameters());
        frame_parameters = &execution->parameters();
    } else {
        compiler::TypedProgramSampler{}.sample_into(
            program, frame, compatibility_parameters);
        frame_parameters = &compatibility_parameters;
    }
    // FIXING.md S-04 — double-checked locking: allocate outside lock.
    // materialize_scene (Scene::clone + topology build) is heavy and must
    // not be held under m_topology_mutex when multiple jobs race.
    std::shared_ptr<const Scene> topology;
    const auto key = program.fingerprint();
    {
        std::shared_lock lock(m_topology_mutex);
        const auto found = m_topologies.find(key);
        if (found != m_topologies.end()) {
            topology = found->second;
        }
    }
    if (!topology) {
        auto fresh = std::make_shared<const Scene>(
            compiler::ProgramSampler{}.materialize_scene(program));
        std::unique_lock lock(m_topology_mutex);
        const auto found = m_topologies.find(key);
        if (found != m_topologies.end()) {
            topology = found->second;
        } else {
            if (m_topologies.size() >= 32) {
                m_topologies.erase(m_topologies.begin());
            }
            topology = fresh;
            m_topologies.emplace(key, topology);
        }
    }
    return chronon3d::graph::render_scene_via_graph(
        *m_context.backend, *m_context.scratch.node_cache, *topology, Camera{},
        static_cast<i32>(metadata.width), static_cast<i32>(metadata.height), frame, 0.0f,
        *m_context.settings, m_context.registry.compositions,
        m_context.video_decoder, static_cast<float>(canvas_fps.fps()),
        metadata.job_id, m_renderer, execution, frame_parameters,
        program.fingerprint());
}

std::shared_ptr<Framebuffer> RenderPipeline::render(
    JobSession& session, Frame frame) {
    session.begin_job();
    const auto* program = session.program_ptr().get();
    if (!program) {
        throw std::runtime_error(
            "RenderPipeline::render requires a CompiledProgram");
    }

    auto* slot = session.acquire_for_evaluation();
    if (!slot) {
        throw std::runtime_error("RenderPipeline has no free frame slot");
    }
    slot->frame_index = frame.integral();
    auto& payload = session.payload(*slot);
    payload.reset();

    const FrameRate frame_rate{
        program->metadata().fps_numerator,
        program->metadata().fps_denominator};
    const auto frame_time = SampleTime::from_frame_int(frame, frame_rate);
    FrameExecution execution(session, frame, frame_time, *slot, payload);

    if (!session.publish_evaluated(*slot)) {
        (void)session.abort(*slot);
        throw std::runtime_error("RenderPipeline failed to publish evaluated frame");
    }
    auto* render_slot = session.acquire_for_render();
    if (!render_slot) {
        (void)session.abort(*slot);
        throw std::runtime_error("RenderPipeline failed to acquire render slot");
    }

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
            throw std::runtime_error("RenderPipeline executor failed the frame");
        }
        if (!session.publish_rendered(*render_slot)) {
            (void)session.abort(*render_slot);
            throw std::runtime_error("RenderPipeline failed to publish rendered frame");
        }
        auto* encode_slot = session.acquire_for_encoding();
        if (!encode_slot || encode_slot != render_slot ||
            !session.begin_encoding(*encode_slot) ||
            !session.release_encoded(*encode_slot)) {
            if (encode_slot && encode_slot->state.load(std::memory_order_acquire) !=
                    FrameSlotState::Free) {
                (void)session.abort(*encode_slot);
            }
            throw std::runtime_error("RenderPipeline failed to release encoded frame slot");
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

std::shared_ptr<Framebuffer> RenderPipeline::render_compatibility(
    JobSession& session, const CompiledComposition& compiled, Frame frame) {
    session.begin_job();
    auto* slot = session.acquire_for_evaluation();
    if (!slot) {
        throw std::runtime_error("RenderPipeline has no free frame slot");
    }
    slot->frame_index = frame.integral();
    auto& payload = session.payload(*slot);
    payload.reset();
    const auto frame_time = SampleTime::from_frame_int(
        frame, compiled.composition ? compiled.composition->frame_rate() : FrameRate{});
    FrameExecution execution(session, frame, frame_time, *slot, payload);

    if (!session.publish_evaluated(*slot)) {
        (void)session.abort(*slot);
        throw std::runtime_error("RenderPipeline failed to publish evaluated frame");
    }
    auto* render_slot = session.acquire_for_render();
    if (!render_slot) {
        (void)session.abort(*slot);
        throw std::runtime_error("RenderPipeline failed to acquire render slot");
    }
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
            throw std::runtime_error("RenderPipeline executor failed the frame");
        }
        if (!session.publish_rendered(*render_slot)) {
            (void)session.abort(*render_slot);
            throw std::runtime_error("RenderPipeline failed to publish rendered frame");
        }
        auto* encode_slot = session.acquire_for_encoding();
        if (!encode_slot || encode_slot != render_slot ||
            !session.begin_encoding(*encode_slot) ||
            !session.release_encoded(*encode_slot)) {
            if (encode_slot && encode_slot->state.load(std::memory_order_acquire) !=
                    FrameSlotState::Free) {
                (void)session.abort(*encode_slot);
            }
            throw std::runtime_error("RenderPipeline failed to release encoded frame slot");
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

RenderBatchResult RenderPipeline::render_compatibility_frames(
    JobSession& session, const CompiledComposition& compiled,
    Frame first, Frame count, const FrameEncoder& encoder) {
    if (!encoder) {
        throw std::invalid_argument("RenderPipeline encoder callback is empty");
    }
    if (count.integral() < 0) {
        throw std::invalid_argument("RenderPipeline frame count must not be negative");
    }

    session.begin_job();

    // The queues and slot states are already concurrent. Keep one wait
    // mutex/CV per stage so a wake-up or short queue handoff in one stage does
    // not convoy evaluation, render and encode behind the same mutex.
    std::mutex evaluation_wait_mutex;
    std::mutex render_wait_mutex;
    std::mutex encoder_wait_mutex;
    std::mutex error_mutex;
    std::condition_variable evaluation_changed;
    std::condition_variable render_changed;
    std::condition_variable encoder_changed;
    std::atomic<bool> evaluation_done{false};
    std::atomic<bool> render_done{false};
    std::atomic<bool> failed{false};
    Frame failed_frame{-1};
    std::string error;
    std::size_t frames_encoded = 0;
    std::size_t frames_rendered = 0;
    std::size_t max_queue_depth = 0;

    auto fail = [&](Frame frame, std::string message) {
        session.fail_job(chronon::make_error(
            session.cancellation().is_cancelled()
                ? chronon::ErrorKind::Cancelled
                : chronon::ErrorKind::JobFailed,
            "render_pipeline",
            session.cancellation().is_cancelled() ? "Cancelled" : "BatchFailed",
            message, "render_pipeline", {}, {}, frame.integral(), session.job_id()));
        {
            std::lock_guard lock(error_mutex);
            failed.store(true, std::memory_order_release);
            failed_frame = frame;
            if (error.empty()) error = std::move(message);
        }
        evaluation_changed.notify_all();
        render_changed.notify_all();
        encoder_changed.notify_all();
    };

    const Frame end = first + count;

    std::thread evaluation_thread([&] {
        for (Frame frame = first; frame < end; ++frame) {
            FrameExecutionSlot* slot = nullptr;
            {
                std::unique_lock lock(evaluation_wait_mutex);
                evaluation_changed.wait(lock, [&] {
                    return failed.load(std::memory_order_acquire) ||
                        (slot = session.acquire_for_evaluation()) != nullptr;
                });
                if (failed.load(std::memory_order_acquire)) break;
            }
            slot->frame_index = frame.integral();
            auto& payload = session.payload(*slot);
            payload.reset();
            try {
                if (!session.publish_evaluated(*slot)) {
                    throw std::runtime_error(
                        "RenderPipeline failed to publish evaluated frame");
                }
            } catch (const std::exception& exception) {
                (void)session.abort(*slot);
                fail(frame, std::string{"RenderPipeline evaluation failed: "} +
                    exception.what());
                break;
            } catch (...) {
                (void)session.abort(*slot);
                fail(frame, "RenderPipeline evaluation failed with an unknown exception");
                break;
            }
            render_changed.notify_one();
        }
        evaluation_done.store(true, std::memory_order_release);
        render_changed.notify_all();
    });

    std::thread render_thread([&] {
        for (;;) {
            FrameExecutionSlot* slot = nullptr;
            {
                std::unique_lock lock(render_wait_mutex);
                render_changed.wait(lock, [&] {
                    if (slot) return true;
                    if ((slot = session.acquire_for_render()) != nullptr) return true;
                    return failed.load(std::memory_order_acquire) ||
                        evaluation_done.load(std::memory_order_acquire);
                });
                if (!slot) {
                    if (failed.load(std::memory_order_acquire) ||
                        evaluation_done.load(std::memory_order_acquire)) {
                        lock.unlock();
                        render_done.store(true, std::memory_order_release);
                        encoder_changed.notify_all();
                        return;
                    }
                    continue;
                }
            }
            auto& payload = session.payload(*slot);
            const Frame slot_frame{static_cast<std::int64_t>(slot->frame_index)};
            const auto frame_time = SampleTime::from_frame_int(
                slot_frame,
                compiled.composition ? compiled.composition->frame_rate() : FrameRate{});
            FrameExecution frame_execution(
                session, slot_frame, frame_time, *slot, payload);
            try {
                payload.rendered = render_compiled_composition(
                    compiled, slot_frame, &frame_execution);
                if (!payload.rendered) {
                    throw std::runtime_error(
                        "RenderPipeline executor failed the frame");
                }
                ++frames_rendered;
                if (!session.publish_rendered(*slot)) {
                    throw std::runtime_error(
                        "RenderPipeline failed to publish rendered frame");
                }
                if (session.rendered_depth() > max_queue_depth) {
                    max_queue_depth = session.rendered_depth();
                }
            } catch (const std::exception& exception) {
                frame_execution.fail();
                (void)session.abort(*slot);
                fail(slot_frame, std::string{"RenderPipeline render failed: "} +
                    exception.what());
                return;
            } catch (...) {
                frame_execution.fail();
                (void)session.abort(*slot);
                fail(slot_frame, "RenderPipeline render failed with an unknown exception");
                return;
            }
            encoder_changed.notify_one();
        }
    });

    std::thread encoder_thread([&] {
        for (;;) {
            FrameExecutionSlot* slot = nullptr;
            {
                std::unique_lock lock(encoder_wait_mutex);
                encoder_changed.wait(lock, [&] {
                    if (slot) return true;
                    if ((slot = session.acquire_for_encoding()) != nullptr) return true;
                    return failed.load(std::memory_order_acquire) ||
                        render_done.load(std::memory_order_acquire);
                });
                if (!slot) {
                    if (failed.load(std::memory_order_acquire) ||
                        render_done.load(std::memory_order_acquire)) return;
                    continue;
                }
            }
            auto& payload = session.payload(*slot);
            const Frame slot_frame{static_cast<std::int64_t>(slot->frame_index)};
            try {
                if (!session.begin_encoding(*slot)) {
                    throw std::runtime_error(
                        "RenderPipeline failed to begin encoding frame");
                }
                if (!payload.rendered || !encoder(slot_frame, *payload.rendered)) {
                    (void)session.abort(*slot);
                    fail(slot_frame, "RenderPipeline encoder rejected frame");
                    return;
                }
            } catch (const std::exception& exception) {
                (void)session.abort(*slot);
                fail(slot_frame, std::string{"RenderPipeline encoder threw: "} +
                    exception.what());
                return;
            } catch (...) {
                (void)session.abort(*slot);
                fail(slot_frame, "RenderPipeline encoder threw an unknown exception");
                return;
            }
            payload.rendered.reset();
            (void)session.release_encoded(*slot);
            ++frames_encoded;
            evaluation_changed.notify_one();
        }
    });

    evaluation_thread.join();
    render_thread.join();
    encoder_thread.join();
    session.reset_slots();

    RenderBatchResult result;
    result.frames_rendered = frames_rendered;
    result.frames_encoded = frames_encoded;
    result.max_queue_depth = max_queue_depth;
    {
        std::lock_guard lock(error_mutex);
        result.ok = !failed.load(std::memory_order_acquire);
        result.failed_frame = failed_frame;
        result.error = error;
    }
    if (result.ok) session.complete_job();
    return result;
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
