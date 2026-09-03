// tests/runtime/test_resource_state_tracker.cpp
// ═══════════════════════════════════════════════════════════════════════════
// New canonical resource-synchronization primitives (sync2-era contract):
//
//   * ResourceRange overlap semantics (whole / image subresource / buffer)
//   * ResourceStateResolver intent → state mapping
//   * ResourceStateTracker hazard rules (RAW/WAR/WAW, read→read accumulation,
//     layout change, queue ownership transfer, discard, alias boundary,
//     non-overlapping subresource elision)
//   * PARALLEL comparison vs the legacy BarrierPlan: the same pass scenarios
//     run through GpuCommandPlanner (old) and ResourceStateTracker (new) and
//     must produce the same (pass, resource, before, after) transition set.
//
// The legacy BarrierTransition stays untouched; this suite is the equivalence
// lock for the migration. See docs/tickets/TICKET-RESOURCE-STATE-V1.md.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/runtime/resource_transition.hpp>

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <utility>
#include <vector>

using namespace chronon3d::runtime;

namespace {

ResourceDesc color_desc() {
    return ResourceDesc::make(
        64, 64, PixelFormat::Rgba32Float,
        ResourceUsage::Generic, LifetimeClass::FrameTransient);
}

ResourceUse make_use(ResourceId id, ResourceRange range = whole_range()) {
    ResourceUse use;
    use.resource = id;
    use.intent = UsageIntent::StorageWrite;  // arbitrary; callers pass states explicitly
    use.range = std::move(range);
    return use;
}

// ── Parallel comparison helpers ─────────────────────────────────────────────

struct OldKey {
    std::size_t pass{0};
    std::uint32_t surface{0};
    ResourceState before{};
    ResourceState after{};
};

struct NewKey {
    std::size_t pass{0};
    std::uint32_t resource{0};
    ResourceState before{};
    ResourceState after{};
};

bool operator<(const OldKey& a, const OldKey& b) noexcept {
    return std::tie(a.pass, a.surface) < std::tie(b.pass, b.surface);
}
bool operator<(const NewKey& a, const NewKey& b) noexcept {
    return std::tie(a.pass, a.resource) < std::tie(b.pass, b.resource);
}
bool operator==(const OldKey& a, const NewKey& b) noexcept {
    return a.pass == b.pass && a.surface == b.resource &&
           a.before == b.before && a.after == b.after;
}

/// Replicate the legacy planner's desired-state derivation for one pass
/// (destination = write, other references = read, in-place = read+write)
/// and feed the SAME states into the new tracker.
std::vector<std::pair<ResourceUse, ResourceState>> uses_for_pass(
    const GpuPass& pass) {
    struct Desired {
        std::uint32_t id{0};
        ResourceState state{};
    };
    std::vector<Desired> desired;
    const auto refs = detail::referenced_handles(pass);
    const auto dest = detail::destination_handle(pass);
    bool skipped_destination_occurrence = false;
    for (const auto handle : refs) {
        if (handle == kInvalidRenderSurfaceHandle) continue;
        if (!skipped_destination_occurrence && handle == dest) {
            skipped_destination_occurrence = true;
            continue;
        }
        desired.push_back({static_cast<std::uint32_t>(handle),
                           ResourceState::compute_read()});
    }
    if (dest != kInvalidRenderSurfaceHandle) {
        const auto dest_id = static_cast<std::uint32_t>(dest);
        bool merged_in_place = false;
        for (auto& d : desired) {
            if (d.id == dest_id) {
                d.state = ResourceState::compute_read_write();
                merged_in_place = true;
                break;
            }
        }
        if (!merged_in_place) {
            desired.push_back({dest_id, ResourceState::compute_write()});
        }
    }

    std::vector<std::pair<ResourceUse, ResourceState>> out;
    out.reserve(desired.size());
    for (const auto& d : desired) {
        out.push_back({make_use(d.id), d.state});
    }
    return out;
}

/// Run one scenario through BOTH systems and assert transition equivalence.
void assert_parallel_equivalence(
    const std::vector<CompositePass>& scenario) {
    // ── legacy system ────────────────────────────────────────────────
    GpuCommandPlanner planner;
    std::vector<RenderSurfaceHandle> handles;
    for (const auto& pass : scenario) {
        for (const auto h : {pass.destination, pass.source}) {
            if (h != kInvalidRenderSurfaceHandle &&
                std::find(handles.begin(), handles.end(), h) == handles.end()) {
                handles.push_back(h);
            }
        }
    }
    for (const auto h : handles) planner.declare_surface(h, color_desc());
    for (const auto& pass : scenario) planner.composite(pass);
    const auto plan = planner.build();

    // ── new system ────────────────────────────────────────────────────
    ResourceStateTracker tracker;
    for (std::size_t i = 0; i < plan.passes.size(); ++i) {
        const auto uses = uses_for_pass(plan.passes.passes[i]);
        for (const auto& [use, state] : uses) tracker.apply_use(i, use, state);
    }

    // ── compare ───────────────────────────────────────────────────────
    std::vector<OldKey> old_keys;
    old_keys.reserve(plan.barriers.transitions.size());
    for (const auto& t : plan.barriers.transitions) {
        old_keys.push_back({t.pass_index,
                            static_cast<std::uint32_t>(t.surface),
                            t.before, t.after});
    }
    std::vector<NewKey> new_keys;
    new_keys.reserve(tracker.transition_count());
    for (const auto& t : tracker.transitions()) {
        new_keys.push_back({t.consumer_pass, t.resource, t.before, t.after});
    }
    std::sort(old_keys.begin(), old_keys.end());
    std::sort(new_keys.begin(), new_keys.end());

    REQUIRE(old_keys.size() == new_keys.size());
    for (std::size_t i = 0; i < old_keys.size(); ++i) {
        CHECK(old_keys[i] == new_keys[i]);
    }
}

} // namespace

// ── ResourceRange semantics ─────────────────────────────────────────────────

TEST_CASE("ResourceRange whole overlaps everything") {
    const auto whole = whole_range();
    CHECK(ranges_overlap(whole, image_range(ResourceAspect::Color)));
    CHECK(ranges_overlap(whole, buffer_range(0, 1024)));
    CHECK(ranges_overlap(image_range(ResourceAspect::Color), whole));
    CHECK(ranges_overlap(whole, whole));
}

TEST_CASE("ResourceRange image subresource overlap is mip/layer/aspect aware") {
    const auto mip0 = image_range(ResourceAspect::Color, 0, 1, 0, 1);
    const auto mip1 = image_range(ResourceAspect::Color, 1, 1, 0, 1);
    CHECK_FALSE(ranges_overlap(mip0, mip1));

    const auto mips0_1 = image_range(ResourceAspect::Color, 0, 2, 0, 1);
    CHECK(ranges_overlap(mips0_1, mip1));
    CHECK(ranges_overlap(mip1, mips0_1));

    const auto layer1 = image_range(ResourceAspect::Color, 0, 1, 1, 1);
    CHECK_FALSE(ranges_overlap(mip0, layer1));

    const auto depth = image_range(ResourceAspect::Depth, 0, 1, 0, 1);
    CHECK_FALSE(ranges_overlap(mip0, depth));

    const auto plane0 = image_range(ResourceAspect::Plane0, 0, 1, 0, 1);
    const auto plane1 = image_range(ResourceAspect::Plane1, 0, 1, 0, 1);
    CHECK_FALSE(ranges_overlap(plane0, plane1));

    // Same mip/layer, overlapping aspect set.
    const auto color_depth = image_range(
        ResourceAspect::Color | ResourceAspect::Depth, 0, 1, 0, 1);
    CHECK(ranges_overlap(mip0, color_depth));
}

TEST_CASE("ResourceRange buffer overlap is byte aware") {
    CHECK(ranges_overlap(buffer_range(0, 1024), buffer_range(512, 1024)));
    CHECK_FALSE(ranges_overlap(buffer_range(0, 1024), buffer_range(2048, 1024)));
    CHECK(ranges_overlap(buffer_range(0, kWholeBufferSize), buffer_range(4096, 128)));
    CHECK(ranges_overlap(buffer_range(4096, 128), buffer_range(0, kWholeBufferSize)));
}

TEST_CASE("ResourceRange image never overlaps buffer") {
    CHECK_FALSE(ranges_overlap(
        image_range(ResourceAspect::Color),
        buffer_range(0, 1024)));
    CHECK_FALSE(ranges_overlap(
        buffer_range(0, 1024),
        image_range(ResourceAspect::Plane0)));
}

TEST_CASE("SubresourceRange overlaps() covers plane and span rules") {
    const SubresourceRange full{ResourceAspect::Plane0 | ResourceAspect::Plane1,
                                0, 1, 0, 1};
    CHECK(full.overlaps(SubresourceRange{ResourceAspect::Plane1, 0, 1, 0, 1}));
    CHECK_FALSE(full.overlaps(SubresourceRange{ResourceAspect::Color, 0, 1, 0, 1}));
}

// ── ResourceStateResolver ──────────────────────────────────────────────────

TEST_CASE("ResourceStateResolver maps sampled reads to shader-read-only") {
    ResourceStateResolver resolver;
    const auto state = resolver.resolve(UsageIntent::SampledRead, ResourceKind::Color);
    CHECK(state.stages == (PipelineStage::FragmentShader | PipelineStage::ComputeShader));
    CHECK(state.access == AccessMask::ShaderRead);
    CHECK(state.layout == ResourceLayout::ShaderReadOnly);
    CHECK(state.queue == QueueClass::GraphicsCompute);
    CHECK(state.reads());
    CHECK_FALSE(state.writes());
}

TEST_CASE("ResourceStateResolver maps storage write to compute general") {
    ResourceStateResolver resolver;
    const auto state = resolver.resolve(UsageIntent::StorageWrite, ResourceKind::Bytes);
    CHECK(state.stages == PipelineStage::ComputeShader);
    CHECK(state.access == AccessMask::ShaderWrite);
    CHECK(state.layout == ResourceLayout::General);
    CHECK(state.queue == QueueClass::Compute);
    CHECK_FALSE(state.reads());
    CHECK(state.writes());
}

TEST_CASE("ResourceStateResolver maps color attachment writes") {
    ResourceStateResolver resolver;
    const auto state = resolver.resolve(
        UsageIntent::ColorAttachmentWrite, ResourceKind::Color);
    CHECK(state.stages == PipelineStage::ColorOutput);
    CHECK(state.access == AccessMask::ColorWrite);
    CHECK(state.layout == ResourceLayout::ColorAttachment);
    CHECK(state.queue == QueueClass::GraphicsCompute);
}

TEST_CASE("ResourceStateResolver maps transfer + video + host intents") {
    ResourceStateResolver resolver;
    const auto transfer_src = resolver.resolve(UsageIntent::TransferSrc, ResourceKind::Color);
    CHECK(transfer_src.stages == PipelineStage::Transfer);
    CHECK(transfer_src.access == AccessMask::TransferRead);
    CHECK(transfer_src.layout == ResourceLayout::TransferSource);
    CHECK(transfer_src.queue == QueueClass::Transfer);

    const auto decode_dst = resolver.resolve(UsageIntent::VideoDecodeDst, ResourceKind::Yuv);
    CHECK(decode_dst.stages == PipelineStage::VideoDecode);
    CHECK(decode_dst.access == AccessMask::VideoDecodeWrite);
    CHECK(decode_dst.layout == ResourceLayout::VideoDecodeDst);
    CHECK(decode_dst.queue == QueueClass::Decode);

    const auto encode_src = resolver.resolve(UsageIntent::VideoEncodeSrc, ResourceKind::Yuv);
    CHECK(encode_src.stages == PipelineStage::VideoEncode);
    CHECK(encode_src.access == AccessMask::VideoEncodeRead);
    CHECK(encode_src.layout == ResourceLayout::VideoEncodeSrc);
    CHECK(encode_src.queue == QueueClass::Encode);

    const auto host_read = resolver.resolve(UsageIntent::HostRead, ResourceKind::Bytes);
    CHECK(host_read.stages == PipelineStage::Host);
    CHECK(host_read.access == AccessMask::HostRead);
    CHECK(host_read.layout == ResourceLayout::General);
    CHECK(host_read.queue == QueueClass::External);
}

// ── ResourceStateTracker hazards ───────────────────────────────────────────

TEST_CASE("Tracker emits first-write and RAW transitions") {
    ResourceStateTracker tracker;
    ResourceUse r1 = make_use(1);
    ResourceUse r2 = make_use(2);

    // Pass 0: write 2 from 1 (1 read first-use elided, 2 first-write emitted).
    const auto first_write = tracker.apply_use(0, r1, ResourceState::compute_read());
    CHECK(first_write.action == TransitionAction::NoBarrier);
    const auto w2 = tracker.apply_use(0, r2, ResourceState::compute_write());
    REQUIRE(w2.action == TransitionAction::EmitTransition);
    CHECK(w2.transition->before.undefined());
    CHECK(w2.transition->after == ResourceState::compute_write());
    CHECK_FALSE(w2.transition->queue_ownership_transfer);
    CHECK_FALSE(w2.transition->alias_boundary);

    // Pass 1: read 2 → RAW.
    const auto raw = tracker.apply_use(1, r2, ResourceState::compute_read());
    REQUIRE(raw.action == TransitionAction::EmitTransition);
    CHECK(raw.transition->before.writes());
    CHECK(raw.transition->after.reads());
    CHECK(raw.transition->producer_pass == 0);
    CHECK(raw.transition->consumer_pass == 1);
}

TEST_CASE("Tracker elides read to read barriers and accumulates stages") {
    ResourceStateTracker tracker;
    ResourceUse r = make_use(1);

    const ResourceState frag_read{
        PipelineStage::FragmentShader, AccessMask::ShaderRead,
        ResourceLayout::ShaderReadOnly, QueueClass::GraphicsCompute, {}};
    const ResourceState compute_read{
        PipelineStage::ComputeShader, AccessMask::ShaderRead,
        ResourceLayout::ShaderReadOnly, QueueClass::GraphicsCompute, {}};

    CHECK(tracker.apply_use(0, r, frag_read).action == TransitionAction::NoBarrier);
    CHECK(tracker.apply_use(1, r, compute_read).action == TransitionAction::NoBarrier);

    // A later writer must synchronize with BOTH readers.
    const auto war = tracker.apply_use(2, r, ResourceState::compute_write());
    REQUIRE(war.action == TransitionAction::EmitTransition);
    CHECK(war.transition->before.stages ==
          (PipelineStage::FragmentShader | PipelineStage::ComputeShader));
    CHECK(war.transition->before.access == AccessMask::ShaderRead);
}

TEST_CASE("Tracker resolves WAR and WAW hazards") {
    ResourceUse a = make_use(1);
    ResourceUse b = make_use(2);

    SUBCASE("WAR") {
        ResourceStateTracker tracker;
        tracker.apply_use(0, a, ResourceState::compute_read());
        const auto war = tracker.apply_use(1, a, ResourceState::compute_write());
        REQUIRE(war.action == TransitionAction::EmitTransition);
        CHECK(war.transition->before.reads());
        CHECK(war.transition->after.writes());
    }
    SUBCASE("WAW") {
        ResourceStateTracker tracker;
        tracker.apply_use(0, a, ResourceState::compute_write());
        const auto waw = tracker.apply_use(1, a, ResourceState::compute_write());
        REQUIRE(waw.action == TransitionAction::EmitTransition);
        CHECK(waw.transition->before.writes());
        CHECK(waw.transition->after.writes());
    }
}

TEST_CASE("Tracker emits a layout transition when layout changes") {
    ResourceStateTracker tracker;
    ResourceUse r = make_use(1);

    const ResourceState shader_read_only{
        PipelineStage::FragmentShader, AccessMask::ShaderRead,
        ResourceLayout::ShaderReadOnly, QueueClass::GraphicsCompute, {}};
    tracker.apply_use(0, r, shader_read_only);

    const auto transition =
        tracker.apply_use(1, r, ResourceState::compute_write());
    REQUIRE(transition.action == TransitionAction::EmitTransition);
    CHECK(transition.transition->before.layout == ResourceLayout::ShaderReadOnly);
    CHECK(transition.transition->after.layout == ResourceLayout::General);
    CHECK_FALSE(transition.transition->queue_ownership_transfer);
}

TEST_CASE("Tracker flags queue ownership transfers") {
    ResourceStateTracker tracker;
    ResourceUse r = make_use(1);

    tracker.apply_use(0, r, ResourceState::compute_read());  // GraphicsCompute

    const ResourceState host_read{
        PipelineStage::Host, AccessMask::HostRead,
        ResourceLayout::General, QueueClass::External, {}};
    const auto transition = tracker.apply_use(1, r, host_read);
    REQUIRE(transition.action == TransitionAction::EmitTransition);
    CHECK(transition.transition->queue_ownership_transfer);
}

TEST_CASE("Tracker elides barriers for non-overlapping subresource ranges") {
    ResourceStateTracker tracker;
    ResourceUse mip0 = make_use(7, image_range(ResourceAspect::Color, 0, 1, 0, 1));
    ResourceUse mip1 = make_use(7, image_range(ResourceAspect::Color, 1, 1, 0, 1));

    CHECK(tracker.apply_use(0, mip0, ResourceState::compute_write()).action ==
          TransitionAction::EmitTransition);
    CHECK(tracker.apply_use(1, mip1, ResourceState::compute_write()).action ==
          TransitionAction::NoBarrier);

    // A whole-range write DOES overlap the recorded mip-1 state.
    const auto whole =
        tracker.apply_use(2, make_use(7), ResourceState::compute_write());
    REQUIRE(whole.action == TransitionAction::EmitTransition);
    CHECK(whole.transition->before.writes());
}

TEST_CASE("Tracker never overlaps image with buffer ranges") {
    ResourceStateTracker tracker;
    ResourceUse image = make_use(8, image_range(ResourceAspect::Color));
    ResourceUse buffer = make_use(8, buffer_range(0, 1024));

    CHECK(tracker.apply_use(0, image, ResourceState::compute_write()).action ==
          TransitionAction::EmitTransition);
    CHECK(tracker.apply_use(1, buffer, ResourceState::compute_write()).action ==
          TransitionAction::NoBarrier);
}

TEST_CASE("Tracker discard_previous_contents restarts from undefined") {
    ResourceStateTracker tracker;
    ResourceUse r = make_use(1);

    tracker.apply_use(0, r, ResourceState::compute_write());

    // A discard + read has nothing to synchronize with: the previous
    // contents (and their state) are explicitly invalidated.
    ResourceUse discard_read = make_use(1);
    discard_read.discard_previous_contents = true;
    const auto read_after_discard =
        tracker.apply_use(1, discard_read, ResourceState::compute_read());
    CHECK(read_after_discard.action == TransitionAction::NoBarrier);

    // The canonical discard pattern: a transient resource's first WRITE
    // starts from undefined regardless of any state recorded before the
    // discard use.
    ResourceUse discard_write = make_use(1);
    discard_write.discard_previous_contents = true;
    const auto write =
        tracker.apply_use(2, discard_write, ResourceState::compute_write());
    REQUIRE(write.action == TransitionAction::EmitTransition);
    CHECK(write.transition->before.undefined());
    CHECK(write.transition->after == ResourceState::compute_write());
}

TEST_CASE("Tracker alias boundary emits an explicit transition") {
    ResourceStateTracker tracker;
    ResourceUse r = make_use(1);
    tracker.apply_use(0, r, ResourceState::compute_write());

    const auto alias =
        tracker.apply_alias_boundary(1, 1, ResourceState::compute_read());
    REQUIRE(alias.action == TransitionAction::EmitTransition);
    REQUIRE(alias.transition.has_value());
    CHECK(alias.transition->alias_boundary);
    CHECK(alias.transition->before.undefined());
    CHECK(alias.transition->after.reads());
    CHECK(alias.transition->producer_pass == 1);
    CHECK(alias.transition->consumer_pass == 1);
}

// ── Parallel comparison vs the legacy BarrierPlan ───────────────────────────

TEST_CASE("New tracker matches legacy GpuCommandPlanner barrier plan") {
    SUBCASE("first-write + RAW") {
        assert_parallel_equivalence({
            CompositePass{.destination = 2, .source = 1},
            CompositePass{.destination = 3, .source = 2},
        });
    }
    SUBCASE("read-to-read elision") {
        assert_parallel_equivalence({
            CompositePass{.destination = 2, .source = 1},
            CompositePass{.destination = 3, .source = 1},
        });
    }
    SUBCASE("WAR") {
        assert_parallel_equivalence({
            CompositePass{.destination = 2, .source = 1},
            CompositePass{.destination = 1, .source = 3},
        });
    }
    SUBCASE("WAW") {
        assert_parallel_equivalence({
            CompositePass{.destination = 2, .source = 1},
            CompositePass{.destination = 2, .source = 1},
        });
    }
    SUBCASE("in-place read+write") {
        assert_parallel_equivalence({
            CompositePass{.destination = 1, .source = 1},
        });
    }
    SUBCASE("physical slot reuse (new logical lifetime)") {
        // Surface 2 is live only in pass 0; surface 4 starts in pass 1 and
        // may reuse 2's physical slot. Both systems must treat 4's first
        // write as starting from undefined.
        assert_parallel_equivalence({
            CompositePass{.destination = 2, .source = 1},
            CompositePass{.destination = 4, .source = 3},
        });
    }
    SUBCASE("longer chain") {
        assert_parallel_equivalence({
            CompositePass{.destination = 2, .source = 1},
            CompositePass{.destination = 3, .source = 2},
            CompositePass{.destination = 4, .source = 3},
            CompositePass{.destination = 2, .source = 4},
        });
    }
}