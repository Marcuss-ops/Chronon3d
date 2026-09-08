// ===========================================================================
// runtime/render_pipeline.cpp
//
// RenderPipeline implementation — thin pass-through to the orchestrating
// graph free functions in `<chronon3d/render_graph/pipeline/render_pipeline.hpp>`.
// The facade exists for type safety + forward-compat with the TICKET-011
// final-stage split.
//
// All paths read the shared typed accessors on `m_runtime` (P1-15
// canonical surface: `runtime.node_cache()`, `runtime.backend()`, etc.)
// and per-instance state from `m_renderer` (SoftwareRenderer counts as
// the RenderBackend for graph dispatch).
// ===========================================================================

#include <chronon3d/runtime/render_pipeline.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/job/job_session.hpp>
#include <chronon3d/runtime/frame/frame_execution.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/compiler/program_sampler.hpp>
#include <chronon3d/compiler/typed_program_sampler.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace chronon3d::runtime {

// 06 R3b — SoftwareRenderer passed in by pointer (a single canonical type
// the RenderPipeline FACADE stores and forwards to graph free functions).
RenderPipeline::RenderPipeline(SoftwareRenderer* renderer, RuntimeContext& runtime) noexcept
    : m_renderer(renderer), m_runtime(runtime) {}

std::shared_ptr<Framebuffer> RenderPipeline::render_scene(
    const Scene& scene, const Camera& camera, i32 width, i32 height,
    float fps)
{
    // Pull shared caches/catalogs from the runtime; per-instance
    // state from the renderer (settings, registry, video_decoder).
    // The pipeline dispatches to the graph via the runtime's
    // attached backend (m_runtime.backend()), bypassing
    // SoftwareRenderer (06 R3b single-identity boundary).
    return chronon3d::graph::render_scene_via_graph(
        m_runtime.backend(),
        m_runtime.node_cache(),
        scene, camera, width, height,
        /*frame=*/0, /*frame_time=*/0.0f,
        m_renderer->render_settings(),
        m_renderer->composition_registry(),
        m_renderer->video_decoder(),
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
        m_runtime.backend(),
        m_runtime.node_cache(),
        effective, default_camera, width, height,
        /*frame=*/0, /*frame_time=*/0.0f,
        m_renderer->render_settings(),
        m_renderer->composition_registry(),
        m_renderer->video_decoder(),
        fps, "scene", m_renderer
    );
}

std::shared_ptr<Framebuffer> RenderPipeline::render_compiled_composition(
    const CompiledComposition& compiled, Frame frame, FrameExecution* execution)
{
    return chronon3d::graph::render_compiled_composition_frame(
        m_runtime.backend(),
        m_runtime.node_cache(),
        m_renderer->render_settings(),
        m_renderer->composition_registry(),
        m_renderer->video_decoder(),
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
    // typed payloads come from the borrowed FrameParameterBlock.
    const auto frame_parameters = compiler::TypedProgramSampler{}.sample(program, frame);
    std::shared_ptr<const Scene> topology;
    {
        std::lock_guard lock(m_topology_mutex);
        const auto key = program.fingerprint();
        const auto found = m_topologies.find(key);
        if (found != m_topologies.end()) {
            topology = found->second;
        } else {
            // The topology is materialized once per immutable program. Use the
            // default resource: a frame arena must never back cached state.
            topology = std::make_shared<const Scene>(
                compiler::ProgramSampler{}.materialize_scene(program));
            if (m_topologies.size() >= 32) {
                m_topologies.erase(m_topologies.begin());
            }
            m_topologies.emplace(key, topology);
        }
    }
    return chronon3d::graph::render_scene_via_graph(
        m_runtime.backend(), m_runtime.node_cache(), *topology, Camera{},
        static_cast<i32>(metadata.width), static_cast<i32>(metadata.height), frame, 0.0f,
        m_renderer->render_settings(), m_renderer->composition_registry(),
        m_renderer->video_decoder(), static_cast<float>(canvas_fps.fps()),
        metadata.job_id, m_renderer, execution, &frame_parameters,
        program.fingerprint());
}

std::shared_ptr<Framebuffer> RenderPipeline::render(
    JobSession& session, Frame frame) {
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
        return output;
    } catch (...) {
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
        return output;
    } catch (...) {
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

    std::mutex wait_mutex;
    std::condition_variable stage_changed;
    bool evaluation_done = false;
    bool render_done = false;
    bool failed = false;
    Frame failed_frame{-1};
    std::string error;
    std::size_t frames_encoded = 0;
    std::size_t frames_rendered = 0;
    std::size_t max_queue_depth = 0;

    auto fail = [&](Frame frame, std::string message) {
        {
            std::lock_guard lock(wait_mutex);
            failed = true;
            failed_frame = frame;
            if (error.empty()) error = std::move(message);
        }
        stage_changed.notify_all();
    };

    const Frame end = first + count;

    std::thread evaluation_thread([&] {
        for (Frame frame = first; frame < end; ++frame) {
            FrameExecutionSlot* slot = nullptr;
            {
                std::unique_lock lock(wait_mutex);
                stage_changed.wait(lock, [&] {
                    return failed ||
                        (slot = session.acquire_for_evaluation()) != nullptr;
                });
                if (failed) break;
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
            stage_changed.notify_all();
        }
        {
            std::lock_guard lock(wait_mutex);
            evaluation_done = true;
        }
        stage_changed.notify_all();
    });

    std::thread render_thread([&] {
        for (;;) {
            FrameExecutionSlot* slot = nullptr;
            {
                std::unique_lock lock(wait_mutex);
                stage_changed.wait(lock, [&] {
                    if (slot) return true;
                    if ((slot = session.acquire_for_render()) != nullptr) return true;
                    return failed || evaluation_done;
                });
                if (!slot) {
                    if (failed || evaluation_done) {
                        lock.unlock();
                        {
                            std::lock_guard done_lock(wait_mutex);
                            render_done = true;
                        }
                        stage_changed.notify_all();
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
                ++frames_rendered;
                if (!session.publish_rendered(*slot)) {
                    throw std::runtime_error(
                        "RenderPipeline failed to publish rendered frame");
                }
                if (session.rendered_depth() > max_queue_depth) {
                    max_queue_depth = session.rendered_depth();
                }
            } catch (const std::exception& exception) {
                (void)session.abort(*slot);
                fail(slot_frame, std::string{"RenderPipeline render failed: "} +
                    exception.what());
                return;
            } catch (...) {
                (void)session.abort(*slot);
                fail(slot_frame, "RenderPipeline render failed with an unknown exception");
                return;
            }
            stage_changed.notify_all();
        }
    });

    std::thread encoder_thread([&] {
        for (;;) {
            FrameExecutionSlot* slot = nullptr;
            {
                std::unique_lock lock(wait_mutex);
                stage_changed.wait(lock, [&] {
                    if (slot) return true;
                    if ((slot = session.acquire_for_encoding()) != nullptr) return true;
                    return failed || render_done;
                });
                if (!slot) {
                    if (failed || render_done) return;
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
            stage_changed.notify_all();
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
        std::lock_guard lock(wait_mutex);
        result.ok = !failed;
        result.failed_frame = failed_frame;
        result.error = error;
    }
    return result;
}

std::string RenderPipeline::debug_graph(
    const Scene& scene, const Camera& camera, i32 width, i32 height,
    float fps, Frame frame, f32 frame_time)
{
    return chronon3d::graph::debug_scene_graph(
        m_runtime.backend(),
        m_runtime.node_cache(),
        scene, camera, width, height,
        frame, frame_time,
        m_renderer->render_settings(),
        m_renderer->composition_registry(),
        m_renderer->video_decoder(),
        fps
    );
}

} // namespace chronon3d::runtime
