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

// ============================================================================
// StagePool — AUDIT F7
//
// The batch pipeline has exactly three fixed, equal-lifetime stages
// (evaluation, render, encode) that must run concurrently and be joined at one
// barrier before the batch result is read. That shape is a job barrier, not a
// work-stealing scheduler, so the pool is deliberately minimal:
//
//   - one thread per stage slot, created once per pipeline (lazily), parked on
//     a condition variable between batches;
//   - run() publishes the job array plus an epoch, wakes the workers, and waits
//     for every slot to report completion;
//   - a worker whose slot index is beyond the submitted job count still consumes
//     the epoch (so no worker spins) but contributes no completion.
//
// A stage loop normally owns its failure reporting (`fail()` records the error
// and flips the shared `failed` flag). A throw escaping a stage is retained by
// the pool and rethrown on the calling thread after the stage barrier, where it
// is converted into the same failed-job state instead of a false success.
// ============================================================================
class RenderPipeline::StagePool {
public:
    explicit StagePool(std::size_t worker_count) {
        m_workers.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) {
            m_workers.emplace_back([this, index] { worker(index); });
        }
    }

    ~StagePool() {
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
        }
        m_start_changed.notify_all();
        for (auto& worker_thread : m_workers) {
            if (worker_thread.joinable()) worker_thread.join();
        }
    }

    StagePool(const StagePool&) = delete;
    StagePool& operator=(const StagePool&) = delete;

    /// Run `job_count` jobs concurrently (one per pool slot) and block until all
    /// of them have returned. `jobs` must outlive this call.
    void run(std::size_t job_count, const std::function<void()>* jobs,
             const std::function<void(std::exception_ptr)>& on_failure) {
        if (job_count == 0) return;
        {
            std::lock_guard lock(m_mutex);
            m_jobs = jobs;
            m_job_count = job_count;
            m_completed = 0;
            m_on_failure = &on_failure;
            ++m_epoch;
        }
        m_start_changed.notify_all();
        {
            std::unique_lock lock(m_mutex);
            m_done_changed.wait(lock, [this] { return m_completed == m_job_count; });
            m_jobs = nullptr;
            m_job_count = 0;
            m_on_failure = nullptr;
        }
    }

private:
    void worker(std::size_t index) {
        std::uint64_t seen_epoch = 0;
        for (;;) {
            const std::function<void()>* job = nullptr;
            {
                std::unique_lock lock(m_mutex);
                m_start_changed.wait(lock, [this, seen_epoch] {
                    return m_stop || m_epoch != seen_epoch;
                });
                if (m_stop) return;
                seen_epoch = m_epoch;
                if (index < m_job_count) job = &m_jobs[index];
            }
            if (job) {
                try {
                    (*job)();
                } catch (...) {
                    const auto failure = std::current_exception();
                    const std::function<void(std::exception_ptr)>* failure_handler = nullptr;
                    {
                        std::lock_guard lock(m_mutex);
                        failure_handler = m_on_failure;
                    }
                    if (failure_handler) {
                        try {
                            (*failure_handler)(failure);
                        } catch (...) {
                            // The installed pipeline callback is noexcept in
                            // practice and contains its own failure fallback.
                        }
                    }
                }
            }
            std::lock_guard lock(m_mutex);
            if (job) {
                ++m_completed;
                if (m_completed == m_job_count) m_done_changed.notify_all();
            }
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_start_changed;
    std::condition_variable m_done_changed;
    std::vector<std::thread> m_workers;
    const std::function<void()>* m_jobs{nullptr};
    std::size_t m_job_count{0};
    std::size_t m_completed{0};
    std::uint64_t m_epoch{0};
    const std::function<void(std::exception_ptr)>* m_on_failure{nullptr};
    bool m_stop{false};
};

RenderPipeline::~RenderPipeline() = default;

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
    std::atomic<bool> escaped_stage_failure_reported{false};
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

    // AUDIT F7 — the three stage loops below are byte-for-byte the loops that
    // used to live in three per-call std::thread objects. They are now submitted
    // as jobs to the pipeline-owned StagePool; m_stage_pool->run() replaces the
    // join() barrier, so no stage ownership, wake-up or ordering semantics
    // change — only the thread lifecycle is amortized across batches.
    std::array<std::function<void()>, 3> stage_jobs{
    [&] {
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
    },

    [&] {
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
    },

    [&] {
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
    },
    };

    if (!m_stage_pool) {
        m_stage_pool = std::make_unique<StagePool>(stage_jobs.size());
    }
    const std::function<void(std::exception_ptr)> report_stage_failure =
        [&](std::exception_ptr failure) noexcept {
            if (escaped_stage_failure_reported.exchange(true, std::memory_order_acq_rel)) {
                failed.store(true, std::memory_order_release);
                evaluation_changed.notify_all();
                render_changed.notify_all();
                encoder_changed.notify_all();
                return;
            }
            try {
                std::rethrow_exception(failure);
            } catch (const std::exception& exception) {
                try {
                    fail(first, std::string{"RenderPipeline stage escaped its failure handler: "} +
                        exception.what());
                } catch (...) {
                    // Preserve the failure flag below if diagnostic allocation
                    // or session error reporting itself fails.
                }
            } catch (...) {
                try {
                    fail(first, "RenderPipeline stage escaped its failure handler with an unknown exception");
                } catch (...) {
                    // Preserve the failure flag below if session reporting fails.
                }
            }
            // Preserve the failed result and release every stage waiter even
            // if constructing/reporting the detailed error itself throws.
            failed.store(true, std::memory_order_release);
            evaluation_changed.notify_all();
            render_changed.notify_all();
            encoder_changed.notify_all();
        };
    m_stage_pool->run(stage_jobs.size(), stage_jobs.data(), report_stage_failure);
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


} // namespace chronon3d::runtime
