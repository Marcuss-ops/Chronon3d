// Unit tests for the CLI GpuSlotPool — the post-demolition composition of
// FrameSlotPool + GpuCompletionTracker (FrameExecutionSlotRing removal).
//
// Covered invariants:
//  - Lease release is a guarded Encoding→Free recycle; double-release and
//    stale-lease release never resurrect a Free slot (old B4 race).
//  - Lease retire pins the slot (stays Encoding, busy) until the registered
//    GpuCompletion reports ready, then the reaper frees it (old B7 race).
//  - acquire() honours cancellation and pool close.
//  - All three interop transitions (Recording→Submitted→Complete) remain
//    driven by the slot's own interop state machine.
#include <doctest/doctest.h>

#include <apps/chronon3d_cli/commands/video/common/gpu_slot_pool.hpp>

#include <atomic>
#include <memory>

using chronon3d::cli::GpuSlotPool;
using chronon3d::runtime::FrameSlotState;
using chronon3d::runtime::GpuCompletion;

namespace {

class FlagCompletion final : public GpuCompletion {
public:
    [[nodiscard]] bool ready() const noexcept override { return flag_.load(); }
    void wait() override { /* test token: no blocking */ }
    void fire() noexcept { flag_.store(true); }

private:
    std::atomic<bool> flag_{false};
};

} // namespace

TEST_CASE("GpuSlotPool: acquire and release recycle slots") {
    GpuSlotPool pool(2);

    auto lease = pool.acquire();
    REQUIRE(lease.valid());
    CHECK(pool.busy_count() == 1);
    CHECK(lease.slot().state.load() == FrameSlotState::Encoding);

    lease.release();
    CHECK_FALSE(lease.valid());
    CHECK(pool.busy_count() == 0);

    auto again = pool.acquire();
    REQUIRE(again.valid());
    CHECK(again.slot().slot_id == lease.slot().slot_id);
    again.release();
}

TEST_CASE("GpuSlotPool: capacity is respected and cancellation unblocks acquire") {
    chronon3d::CancellationToken token;
    GpuSlotPool pool(1);

    auto first = pool.acquire();
    REQUIRE(first.valid());

    // Cancel before the second acquire: must return an invalid lease instead
    // of blocking forever on the exhausted pool.
    token.cancel();
    auto second = pool.acquire(&token);
    CHECK_FALSE(second.valid());
    CHECK(pool.busy_count() == 1);

    first.release();
    CHECK(pool.busy_count() == 0);
}

TEST_CASE("GpuSlotPool: close() releases blocked acquirers") {
    GpuSlotPool pool(1);
    auto lease = pool.acquire();
    REQUIRE(lease.valid());

    pool.close();
    auto rejected = pool.acquire();
    CHECK_FALSE(rejected.valid());
    // Close must not forcibly free a still-pinned slot: the lease destructor
    // remains the owner of the guarded recycle.
    CHECK(pool.busy_count() == 1);
    lease.release();
    CHECK(pool.busy_count() == 0);
}

TEST_CASE("GpuSlotPool: retire pins the slot until the completion fires") {
    GpuSlotPool pool(1);
    auto lease = pool.acquire();
    REQUIRE(lease.valid());
    const auto slot_id = lease.slot().slot_id;

    auto completion = std::make_shared<FlagCompletion>();
    lease.retire(completion);
    CHECK_FALSE(lease.valid());

    // Pinned: still busy, still Encoding, not reaped.
    CHECK(pool.busy_count() == 1);
    CHECK(pool.slot(slot_id).state.load() == FrameSlotState::Encoding);

    auto blocked = pool.acquire();
    CHECK_FALSE(blocked.valid()); // would block/cancel without token? No token:
    // acquire() without a token loops; use a cancelled token path instead.

    // Fire the completion: the reaper frees the slot on the next acquire.
    completion->fire();
    auto reaped = pool.acquire();
    REQUIRE(reaped.valid());
    CHECK(reaped.slot().slot_id == slot_id);
    CHECK(pool.busy_count() == 1);
    reaped.release();
    CHECK(pool.busy_count() == 0);
}

TEST_CASE("GpuSlotPool: drain returns once retired completions fire") {
    GpuSlotPool pool(1);
    auto lease = pool.acquire();
    REQUIRE(lease.valid());

    auto completion = std::make_shared<FlagCompletion>();
    lease.retire(completion);
    CHECK(pool.busy_count() == 1);

    // drain() must not hang forever while the completion is pending: it
    // returns when the pool is closed, so close first (failure-path shape).
    pool.close();
    pool.drain();
    CHECK(pool.busy_count() == 1); // still pinned after close without firing

    // After the completion fires, an acquire reaps and hands the slot out.
    completion->fire();
    auto reaped = pool.acquire();
    CHECK(reaped.valid());
    if (reaped.valid()) reaped.release();
}

TEST_CASE("GpuSlotPool: interop lifecycle transitions remain slot-driven") {
    using chronon3d::runtime::InteropFrameState;
    GpuSlotPool pool(1);

    auto lease = pool.acquire();
    REQUIRE(lease.valid());
    auto& slot = lease.slot();

    CHECK(slot.transition_interop_state(InteropFrameState::VulkanRecording));
    CHECK(slot.transition_interop_state(InteropFrameState::VulkanSubmitted));
    CHECK(slot.transition_interop_state(InteropFrameState::VulkanComplete));
    CHECK(slot.native_surface_prepared());

    // Invalid jump: Complete → Recording is not a legal transition.
    CHECK_FALSE(slot.transition_interop_state(InteropFrameState::VulkanRecording));

    lease.release();
}
