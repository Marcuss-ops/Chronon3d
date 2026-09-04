#include <doctest/doctest.h>

#include <chronon3d/runtime/frame/frame_queue.hpp>
#include <chronon3d/runtime/frame/frame_slot_pool.hpp>
#include <chronon3d/runtime/frame/gpu_completion_tracker.hpp>

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

TEST_CASE("frame components preserve evaluate render encode handoff without facade") {
    GpuCompletionTracker tracker(2);
    FrameSlotPool pool(2, tracker);
    FrameQueue evaluated_queue(2);
    FrameQueue rendered_queue(2);

    auto* evaluated = pool.try_acquire(FrameSlotState::Evaluating);
    REQUIRE(evaluated != nullptr);
    const auto slot_id = evaluated->slot_id;
    CHECK(pool.transition(
        *evaluated, FrameSlotState::Evaluating, FrameSlotState::Evaluated));
    CHECK(evaluated_queue.try_push(slot_id));

    FrameSlotId render_id = 0;
    REQUIRE(evaluated_queue.try_pop(render_id));
    CHECK(render_id == slot_id);
    auto& render = pool.slot(render_id);
    CHECK(pool.transition(
        render, FrameSlotState::Evaluated, FrameSlotState::Rendered));
    CHECK(rendered_queue.try_push(render_id));
    CHECK(rendered_queue.size() == 1);

    FrameSlotId encode_id = 0;
    REQUIRE(rendered_queue.try_pop(encode_id));
    CHECK(encode_id == slot_id);
    auto& encode = pool.slot(encode_id);
    CHECK(pool.transition(
        encode, FrameSlotState::Rendered, FrameSlotState::Encoding));
    pool.release(encode);
    CHECK(pool.busy_count() == 0);
}

TEST_CASE("GPU completion tracker gates fixed slot recycle explicitly") {
    GpuCompletionTracker tracker(1);
    FrameSlotPool pool(1, tracker);
    auto* slot = pool.try_acquire(FrameSlotState::GpuWriting);
    REQUIRE(slot != nullptr);
    const auto slot_id = slot->slot_id;

    CHECK(slot->transition_interop_state(InteropFrameState::Allocated));
    CHECK(slot->transition_interop_state(InteropFrameState::VulkanRecording));
    CHECK(slot->transition_interop_state(InteropFrameState::VulkanSubmitted));
    CHECK(slot->transition_interop_state(InteropFrameState::VulkanComplete));
    CHECK(slot->transition_interop_state(InteropFrameState::EncodeSubmitted));

    std::atomic<bool> ready{false};
    tracker.retire(slot_id, std::make_shared<TestGpuCompletion>(ready));
    CHECK(tracker.has_completion(slot_id));
    CHECK_FALSE(tracker.completion_ready(slot_id));
    CHECK(pool.busy_count() == 1);

    ready.store(true, std::memory_order_release);
    CHECK(tracker.completion_ready(slot_id));
    tracker.recycle(slot_id);
    pool.release(*slot);

    auto* reused = pool.try_acquire(FrameSlotState::GpuWriting);
    REQUIRE(reused != nullptr);
    CHECK(reused->slot_id == slot_id);
    CHECK(reused->interop_state.load(std::memory_order_acquire) ==
          InteropFrameState::Recyclable);
    pool.release(*reused);
    CHECK(pool.busy_count() == 0);
}

TEST_CASE("frame component capacities are fixed and fail closed") {
    CHECK_THROWS_AS(GpuCompletionTracker(0), std::invalid_argument);
    CHECK_THROWS_AS(
        GpuCompletionTracker(kMaxFrameExecutionSlots + 1),
        std::invalid_argument);

    GpuCompletionTracker tracker(1);
    CHECK_THROWS_AS(FrameSlotPool(0, tracker), std::invalid_argument);
    CHECK_THROWS_AS(FrameSlotPool(2, tracker), std::invalid_argument);
}

} // namespace
} // namespace chronon3d::runtime
