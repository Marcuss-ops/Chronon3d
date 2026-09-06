// ===========================================================================
// api/render_engine.cpp — RenderEngine private assembly and implementation
// boundaries. Prepared-job execution and public API methods live in dedicated
// implementation fragments included below, preserving one translation unit.
// ===========================================================================

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/runtime/render_pipeline.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include <chronon3d/runtime/frame/frame_queue.hpp>
#include <chronon3d/runtime/frame/frame_slot_pool.hpp>
#include <chronon3d/runtime/frame/gpu_completion_tracker.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/timeline/evaluated_composition_frame.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/runtime_adapter.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

#include <spdlog/spdlog.h>

#include <optional>
#include <array>
#include <stdexcept>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace chronon3d {

struct PreparedRenderJob::Impl {
    static constexpr std::size_t kPipelineDepth = 3;

    struct SlotPayload {
        FrameArena arena{4u * 1024u * 1024u, true};
        std::optional<EvaluatedCompositionFrame> evaluated{};
        std::shared_ptr<Framebuffer> rendered{};

        void reset() noexcept {
            evaluated.reset();
            rendered.reset();
            arena.reset();
        }
    };

    RenderEngine* engine{nullptr};
    std::shared_ptr<const CompiledComposition> compiled;
    Frame count{0};
    bool finished{false};
    runtime::ResourcePlan resource_plan;

    // Job-lifetime stable values. assets_root cannot change between frames of
    // one job (RenderEngine::set_assets_root() is an engine-level operation,
    // not a per-frame one), so materialize the mount-root string once per
    // job instead of rebuilding a std::string in every evaluate_frame().
    std::string assets_root;

    // Canonical fixed-frame execution authorities. PreparedRenderJob owns
    // orchestration only; slot storage/lifecycle, bounded handoff, and GPU
    // completion state remain in their single-responsibility components.
    runtime::GpuCompletionTracker gpu_completion{kPipelineDepth};
    runtime::FrameSlotPool slot_pool{kPipelineDepth, gpu_completion};
    runtime::FrameQueue evaluated_queue{kPipelineDepth};
    runtime::FrameQueue rendered_queue{kPipelineDepth};

    std::array<SlotPayload, kPipelineDepth> payloads{};
    std::uint64_t next_sequence{0};

    [[nodiscard]] SlotPayload& payload(const runtime::FrameExecutionSlot& slot) noexcept {
        return payloads[slot.slot_id];
    }

    [[nodiscard]] runtime::FrameExecutionSlot* acquire_for_evaluation() noexcept {
        return slot_pool.try_acquire(runtime::FrameSlotState::Evaluating);
    }

    [[nodiscard]] bool publish_evaluated(runtime::FrameExecutionSlot& slot) noexcept {
        if (!slot_pool.transition(
                slot,
                runtime::FrameSlotState::Evaluating,
                runtime::FrameSlotState::Evaluated)) {
            return false;
        }
        if (!evaluated_queue.try_push(slot.slot_id)) {
            slot_pool.set_state(slot, runtime::FrameSlotState::Evaluating);
            return false;
        }
        return true;
    }

    [[nodiscard]] runtime::FrameExecutionSlot* acquire_for_render() noexcept {
        runtime::FrameSlotId slot_id = 0;
        return evaluated_queue.try_pop(slot_id) ? &slot_pool.slot(slot_id) : nullptr;
    }

    [[nodiscard]] bool publish_rendered(runtime::FrameExecutionSlot& slot) noexcept {
        if (!slot_pool.transition(
                slot,
                runtime::FrameSlotState::Evaluated,
                runtime::FrameSlotState::Rendered)) {
            return false;
        }
        if (!rendered_queue.try_push(slot.slot_id)) {
            slot_pool.set_state(slot, runtime::FrameSlotState::Evaluated);
            return false;
        }
        return true;
    }

    [[nodiscard]] runtime::FrameExecutionSlot* acquire_for_encoding() noexcept {
        runtime::FrameSlotId slot_id = 0;
        return rendered_queue.try_pop(slot_id) ? &slot_pool.slot(slot_id) : nullptr;
    }

    [[nodiscard]] bool begin_encoding(runtime::FrameExecutionSlot& slot) noexcept {
        return slot_pool.transition(
            slot,
            runtime::FrameSlotState::Rendered,
            runtime::FrameSlotState::Encoding);
    }

    void release_slot(runtime::FrameExecutionSlot& slot) noexcept {
        gpu_completion.recycle(slot.slot_id);
        slot.native_surface_ptr = 0;
        slot.gpu_ready_sync = 0;
        slot_pool.release(slot);
    }

    [[nodiscard]] bool release_encoded(runtime::FrameExecutionSlot& slot) noexcept {
        if (slot.state.load(std::memory_order_acquire) != runtime::FrameSlotState::Encoding) {
            return false;
        }
        release_slot(slot);
        return true;
    }

    [[nodiscard]] bool abort(runtime::FrameExecutionSlot& slot) noexcept {
        if (slot.state.load(std::memory_order_acquire) == runtime::FrameSlotState::Free) {
            return false;
        }
        release_slot(slot);
        return true;
    }

    [[nodiscard]] std::size_t rendered_depth() const noexcept {
        return rendered_queue.size();
    }

    void reset_slots() noexcept {
        for (auto& payload : payloads) payload.reset();
        evaluated_queue.clear();
        rendered_queue.clear();
        gpu_completion.reset();
        slot_pool.reset();
    }

    [[nodiscard]] bool can_split_evaluation() const noexcept;
    [[nodiscard]] EvaluatedCompositionFrame evaluate_frame(
        Frame frame, FrameArena& arena) const;
    [[nodiscard]] std::shared_ptr<Framebuffer> render_evaluated_frame(
        EvaluatedCompositionFrame& evaluated, Frame frame) const;

    // ── Persistent stage workers (data-oriented runtime profile) ─────────
    //
    // Three stage workers created once per job and reused across
    // render_frames() batches. A batch is one "session": the caller
    // publishes batch parameters under the pool mutex, wakes the workers,
    // and waits until all three stage-done flags are set. Workers park on
    // the session latch between batches instead of exiting, so short jobs
    // no longer pay thread create/join per batch (P1-6).
    //
    // Completion protocol: a worker sets its done flag as its LAST session
    // action, after its stage loop broke (no more work or failure). The
    // caller therefore waits for evaluation_done && render_done &&
    // encode_done before touching slot storage — the equivalent guarantee
    // the previous design got from thread::join().
    struct WorkerPool {
        Impl& self;

        std::mutex mutex;
        std::condition_variable cv;

        // Session lifecycle.
        bool session_active{false};
        bool shutdown{false};
        bool workers_started{false};

        // Per-session batch parameters. session_encoder borrows the caller's
        // callback for one session; render_frames() blocks until the session
        // completes, so the borrow always outlives its use.
        Frame batch_first{0};
        Frame batch_end{0};
        bool split_evaluation{false};
        const PreparedFrameEncoder* session_encoder{nullptr};

        // Stage-done flags. Reset by the caller at session start; set by the
        // owning worker when its stage has no more work for the session.
        bool evaluation_done{false};
        bool render_done{false};
        bool encode_done{false};

        // Per-session failure state and counters.
        bool failed{false};
        Frame failed_frame{-1};
        std::string error;
        std::size_t frames_encoded{0};
        std::size_t frames_rendered{0};
        std::size_t max_queue_depth{0};

        std::thread evaluation_thread;
        std::thread render_thread;
        std::thread encoder_thread;

        explicit WorkerPool(Impl& owner) : self(owner) {}
        ~WorkerPool() {
            {
                std::lock_guard lock(mutex);
                shutdown = true;
                session_active = false;
            }
            cv.notify_all();
            if (evaluation_thread.joinable()) evaluation_thread.join();
            if (render_thread.joinable()) render_thread.join();
            if (encoder_thread.joinable()) encoder_thread.join();
        }

        WorkerPool(const WorkerPool&) = delete;
        WorkerPool& operator=(const WorkerPool&) = delete;

        void fail(Frame frame, std::string message) {
            {
                std::lock_guard lock(mutex);
                failed = true;
                failed_frame = frame;
                if (error.empty()) error = std::move(message);
            }
            cv.notify_all();
        }

        void ensure_started() {
            std::lock_guard lock(mutex);
            if (workers_started) return;
            workers_started = true;
            evaluation_thread = std::thread([this] { evaluation_loop(); });
            render_thread = std::thread([this] { render_loop(); });
            encoder_thread = std::thread([this] { encoder_loop(); });
        }

        void evaluation_loop() {
            for (;;) {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] {
                    return shutdown || (session_active && !evaluation_done);
                });
                if (shutdown) return;
                if (!session_active) continue; // spurious/parked wakeup
                lock.unlock();

                // Production loop: one slot per frame in the batch window.
                for (Frame frame = batch_first; frame < batch_end; ++frame) {
                    runtime::FrameExecutionSlot* slot = nullptr;
                    {
                        std::unique_lock wait_lock(mutex);
                        cv.wait(wait_lock, [&] {
                            return failed || shutdown ||
                                (slot = self.acquire_for_evaluation()) != nullptr;
                        });
                        if (failed || shutdown) break;
                    }
                    slot->frame_index = frame.integral();
                    auto& payload = self.payload(*slot);
                    payload.reset();
                    try {
                        if (split_evaluation) {
                            payload.evaluated.emplace(
                                self.evaluate_frame(frame, payload.arena));
                        }
                        if (!self.publish_evaluated(*slot)) {
                            throw std::runtime_error(
                                "PreparedRenderJob failed to publish evaluated frame");
                        }
                    } catch (const std::exception& exception) {
                        (void)self.abort(*slot);
                        fail(frame, std::string{"PreparedRenderJob evaluation failed: "} +
                                        exception.what());
                        break;
                    } catch (...) {
                        (void)self.abort(*slot);
                        fail(frame,
                             "PreparedRenderJob evaluation failed with an unknown exception");
                        break;
                    }
                    cv.notify_all();
                }

                lock.lock();
                evaluation_done = true;
                cv.notify_all();
            }
        }

        void render_loop() {
            for (;;) {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] {
                    return shutdown || (session_active && !render_done);
                });
                if (shutdown) return;
                if (!session_active) continue; // spurious/parked wakeup
                lock.unlock();

                for (;;) {
                    runtime::FrameExecutionSlot* slot = nullptr;
                    {
                        std::unique_lock wait_lock(mutex);
                        cv.wait(wait_lock, [&] {
                            if (slot) return true;
                            if (shutdown) return true;
                            if ((slot = self.acquire_for_render()) != nullptr) {
                                return true;
                            }
                            return failed || evaluation_done;
                        });
                        if (!slot || shutdown) break; // stage drained / failure / shutdown
                    }
                    auto& payload = self.payload(*slot);
                    const Frame slot_frame{static_cast<std::int64_t>(slot->frame_index)};
                    try {
                        std::shared_ptr<Framebuffer> output;
                        if (split_evaluation) {
                            output = self.render_evaluated_frame(
                                *payload.evaluated, slot_frame);
                            payload.evaluated.reset();
                        } else {
                            // Temporal/SSAA paths retain their canonical complete
                            // compositor boundary, but still participate in the
                            // bounded render→encode stages.
                            output = self.engine->render_compiled(
                                *self.compiled, slot_frame);
                        }
                        payload.rendered = std::move(output);
                        ++frames_rendered;
                        if (!self.publish_rendered(*slot)) {
                            throw std::runtime_error(
                                "PreparedRenderJob failed to publish rendered frame");
                        }
                        const auto depth = self.rendered_depth();
                        if (depth > max_queue_depth) max_queue_depth = depth;
                    } catch (const std::exception& exception) {
                        (void)self.abort(*slot);
                        fail(slot_frame, std::string{"PreparedRenderJob render failed: "} +
                                            exception.what());
                        break;
                    } catch (...) {
                        (void)self.abort(*slot);
                        fail(slot_frame,
                             "PreparedRenderJob render failed with an unknown exception");
                        break;
                    }
                    cv.notify_all();
                }

                lock.lock();
                render_done = true;
                cv.notify_all();
            }
        }

        void encoder_loop() {
            for (;;) {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] {
                    return shutdown || (session_active && !encode_done);
                });
                if (shutdown) return;
                if (!session_active) continue; // spurious/parked wakeup
                const PreparedFrameEncoder* encoder = session_encoder;
                lock.unlock();

                for (;;) {
                    runtime::FrameExecutionSlot* slot = nullptr;
                    {
                        std::unique_lock wait_lock(mutex);
                        cv.wait(wait_lock, [&] {
                            if (slot) return true;
                            if (shutdown) return true;
                            if ((slot = self.acquire_for_encoding()) != nullptr) {
                                return true;
                            }
                            return failed || render_done;
                        });
                        if (!slot || shutdown) break; // stage drained / failure / shutdown
                    }
                    auto& payload = self.payload(*slot);
                    const Frame slot_frame{static_cast<std::int64_t>(slot->frame_index)};
                    try {
                        if (!self.begin_encoding(*slot)) {
                            throw std::runtime_error(
                                "PreparedRenderJob failed to begin encoding frame");
                        }
                        if (!encoder || !payload.rendered ||
                            !(*encoder)(slot_frame, *payload.rendered)) {
                            (void)self.abort(*slot);
                            fail(slot_frame, "PreparedRenderJob encoder rejected frame");
                            break;
                        }
                    } catch (const std::exception& exception) {
                        (void)self.abort(*slot);
                        fail(slot_frame, std::string{"PreparedRenderJob encoder threw: "} +
                                            exception.what());
                        break;
                    } catch (...) {
                        (void)self.abort(*slot);
                        fail(slot_frame,
                             "PreparedRenderJob encoder threw an unknown exception");
                        break;
                    }
                    payload.rendered.reset();
                    (void)self.release_encoded(*slot);
                    ++frames_encoded;
                    cv.notify_all();
                }

                lock.lock();
                encode_done = true;
                cv.notify_all();
            }
        }
    };

    WorkerPool workers{*this};
};

PreparedRenderJob::PreparedRenderJob(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl)) {}

PreparedRenderJob::~PreparedRenderJob() = default;
PreparedRenderJob::PreparedRenderJob(PreparedRenderJob&&) noexcept = default;
PreparedRenderJob& PreparedRenderJob::operator=(PreparedRenderJob&&) noexcept = default;

Frame PreparedRenderJob::frame_count() const noexcept {
    return m_impl ? m_impl->count : Frame{0};
}

const runtime::ResourcePlan& PreparedRenderJob::resource_plan() const noexcept {
    static const runtime::ResourcePlan empty;
    return m_impl ? m_impl->resource_plan : empty;
}

void PreparedRenderJob::finish() noexcept {
    if (!m_impl) return;
    m_impl->finished = true;
    m_impl->compiled.reset();
}

struct RenderEngine::Impl {
    Config                                    m_config;
    AssetRegistry                             m_assets;
    registry::ShapeRegistry                   m_shape_registry{
        registry::make_default_shape_registry()};
    std::unique_ptr<runtime::RenderRuntime>    m_runtime;
    std::unique_ptr<SoftwareRenderer>          m_renderer;
    std::optional<runtime::RenderPipeline>     m_pipeline;

    explicit Impl(Config config, std::optional<std::filesystem::path> assets_root = std::nullopt)
        : m_config(std::move(config))
    {
        auto runtime_result = runtime::RenderRuntime::create(
            runtime::RuntimeConfig{.config = m_config, .assets_root = assets_root});
        if (!runtime_result) {
            throw std::runtime_error(
                "RenderEngine::Impl: runtime assembly failed: " +
                runtime_result.error().message);
        }
        m_runtime = std::move(runtime_result).value();
        m_renderer = std::make_unique<SoftwareRenderer>(*m_runtime, m_config);
        m_renderer->set_image_backend(std::make_shared<image::StbImageBackend>());

        chronon3d::backends::software::attach_software_backend(
            m_renderer.get(), m_config.backend_preference());
        m_pipeline.emplace(m_renderer.get(), *m_runtime);

        if (assets_root.has_value()) {
            set_assets_root(*assets_root);
            spdlog::debug("RenderEngine::Impl: constructed with assets_root={}",
                          assets_root->string());
        } else {
            spdlog::debug("RenderEngine::Impl: constructed; runtime backend attached");
        }
    }

    ~Impl() = default;

    void set_assets_root(const std::filesystem::path& root) {
        m_runtime->resolver().mount(root);
    }
};

#include "render_engine_prepared_job_detail.hpp"
#include "render_engine_api_detail.hpp"

} // namespace chronon3d
