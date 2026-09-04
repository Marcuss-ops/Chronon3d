// tests/runtime/test_resource_transition_alias.cpp
// Canonical synchronization authority: alias/discard semantics stay backend-neutral.

#include <doctest/doctest.h>

#include <chronon3d/runtime/resource_transition.hpp>

using namespace chronon3d::runtime;

TEST_CASE("Alias boundary carries memory dependency while discarding prior layout") {
    ResourceStateTracker tracker;
    const ResourceState first_state{
        .stages = PipelineStage::ComputeShader,
        .access = AccessMask::ShaderWrite,
        .layout = ResourceLayout::General,
        .queue = QueueClass::Compute,
    };

    const auto result = tracker.apply_alias_boundary(
        4, 7, image_range(ResourceAspect::Color), first_state);

    REQUIRE(result.action == TransitionAction::EmitTransition);
    REQUIRE(result.transition.has_value());
    const auto& transition = *result.transition;
    CHECK(transition.alias_boundary);
    CHECK(transition.before.stages == PipelineStage::AllCommands);
    CHECK(transition.before.access ==
          (AccessMask::MemoryRead | AccessMask::MemoryWrite));
    CHECK(transition.before.layout == ResourceLayout::Undefined);
    CHECK(transition.before.queue == first_state.queue);
    CHECK(transition.after == first_state);
    CHECK_FALSE(transition.queue_ownership_transfer);
}

TEST_CASE("Discard use forgets previous contents without inventing a source state") {
    ResourceStateTracker tracker;
    ResourceStateResolver resolver;

    const auto range = buffer_range(32, 128);
    const auto written = resolver.resolve(UsageIntent::StorageWrite,
                                          ResourceKind::Bytes);
    tracker.apply_use(0, ResourceUse{9, UsageIntent::StorageWrite, range, false},
                      written);

    const auto discarded = resolver.resolve(UsageIntent::StorageWrite,
                                            ResourceKind::Bytes);
    const auto result = tracker.apply_use(
        1, ResourceUse{9, UsageIntent::StorageWrite, range, true}, discarded);

    REQUIRE(result.action == TransitionAction::EmitTransition);
    REQUIRE(result.transition.has_value());
    CHECK(result.transition->before.undefined());
    CHECK(result.transition->after.writes());
    CHECK_FALSE(result.transition->alias_boundary);
}
