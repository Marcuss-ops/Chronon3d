#include <doctest/doctest.h>

#include <chronon3d/runtime/frame/frame_queue.hpp>
#include <chronon3d/runtime/frame/frame_slot_pool.hpp>
#include <chronon3d/runtime/frame/gpu_completion_tracker.hpp>
#include <chronon3d/runtime/frame_execution_slot_ring.hpp>

#include <atomic>
#include <memory>
#include <stdexcept>

namespace chronon3d::runtime {
namespace {

class TestGpuCompletion final : public GpuCompletion {
public:
    explicit TestGpuCompletion(std::atomic<bool>& ready) noexcept
        : ready_(ready) {}

    [[nodiscard]] bool ready() const noexcept override {
        return ready_.load(std::memory_order_acquire);
    }

    void wait() override {
        ready_.store(true, std::memory_order_release);
    }

private:
    std::atomic<bool>& ready_;
};

TEST_CASE("frame slot pool owns fixed CPU lifecycle while GPU state lives in tracker") {
    GpuCompletionTracker tracker(3);
    FrameSlotPool pool(3, tracker);

    auto* first = pool.try_acquire(FrameSlotState::Evaluating);
    auto* second = pool.try_acquire(FrameSlotState::Evaluating);
    auto* third = pool.try_acquire(FrameSlotState::Evaluating);

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(third != nullptr);
    CHECK(first->slot_id != second->slot_id);
    CHECK(first->slot_id != third->slot_id);
    CHECK(second->slot_id != third->slot_id);
    CHECK(pool.try_acquire(FrameSlotState::Evaluating) == nullptr);

    CHECK(first->transition_interop_state(InteropFrameState::Allocated));
    CHECK(tracker.interop_state(first->slot_id) == InteropFrameState::Allocated);
    tracker.recycle(first->slot_id);
    CHECK(first->interop_state.load(std::memory_order_acquire) ==
          InteropFrameState::Recyclable);

    pool.release(*first);
    auto* reused = pool.try_acquire(FrameSlotState::GpuWriting);
    REQUIRE(reused != nullptr);
    CHECK(reused->slot_id == first->slot_id);
}

TEST_CASE("frame queue is bounded and transports slot ids only") {
    FrameQueue queue(2);
    CHECK(queue.try_push(FrameSlotId{7}));
    CHECK(queue.try_push(FrameSlotId{11}));
    CHECK_FALSE(queue.try_push(FrameSlotId{13}));
    CHECK(queue.size() == 2);

    FrameSlotId slot_id = 0;
    REQUIRE(queue.try_pop(slot_id));
    CHECK(slot_id == 7);
    REQUIRE(queue.try_pop(slot_id));
    CHECK(slot_id == 11);
    CHECK_FALSE(queue.try_pop(slot_id));
    CHECK(queue.size() == 0);
}

TEST_CASE("frame execution facade preserves evaluate render encode handoff") {
    FrameExecutionSlotRing ring(2);

    auto* evaluated = ring.acquire_for_evaluation();
    REQUIRE(evaluated != nullptr);
    const auto slot_id = evaluated->slot_id;
    CHECK(ring.publish_evaluated(*evaluated));

    auto* render = ring.acquire_for_render();
    REQUIRE(render != nullptr);
    CHECK(render->slot_id == slot_id);
    CHECK(ring.publish_rendered(*render));
    CHECK(ring.rendered_depth() == 1);

    auto* encode = ring.acquire_for_encoding();
    REQUIRE(encode != nullptr);
    CHECK(encode->slot_id == slot_id);
    CHECK(ring.begin_encoding(*encode));
    CHECK(ring.release_encoded(*encode));
    CHECK(ring.busy_count() == 0);
}

TEST_CASE("retired GPU completion is reaped before fixed slot reuse") {
    FrameExecutionSlotRing ring(1);
    auto lease = ring.acquire_lease();
    REQUIRE(lease.valid());
    const auto slot_id = lease.slot().slot_id;

    CHECK(lease.slot().transition_interop_state(InteropFrameState::Allocated));
    CHECK(lease.slot().transition_interop_state(InteropFrameState::VulkanRecording));
    CHECK(lease.slot().transition_interop_state(InteropFrameState::VulkanSubmitted));
    CHECK(lease.slot().transition_interop_state(InteropFrameState::VulkanComplete));
    CHECK(lease.slot().transition_interop_state(InteropFrameState::EncodeSubmitted));

    std::atomic<bool> ready{false};
    lease.retire(std::make_shared<TestGpuCompletion>(ready));
    CHECK(ring.busy_count() == 1);

    ready.store(true, std::memory_order_release);
    auto reused = ring.acquire_lease();
    REQUIRE(reused.valid());
    CHECK(reused.slot().slot_id == slot_id);
    CHECK(reused.slot().interop_state.load(std::memory_order_acquire) ==
          InteropFrameState::Recyclable);
    reused.release();
    CHECK(ring.busy_count() == 0);
}

TEST_CASE("frame execution capacity is fixed and fail closed") {
    CHECK_THROWS_AS(FrameExecutionSlotRing(0), std::invalid_argument);
    CHECK_THROWS_AS(
        FrameExecutionSlotRing(kMaxFrameExecutionSlots + 1),
        std::invalid_argument);
}

} // namespace
} // namespace chronon3d::runtime
