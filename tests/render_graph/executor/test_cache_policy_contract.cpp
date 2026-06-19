#include <doctest/doctest.h>

// ── Cache Policy Contract ──────────────────────────────────────────────────
//
// `node.cache_policy()` is the canonical cache descriptor for the render graph.
// The GraphExecutor reads ONLY this method; it dispatches on
// `policy.enabled()`, `policy.is_frame_variant()`, `policy.persistent()`,
// and `policy.reusable_across_frames()`.  No legacy API survives: the
// mutation setter, cacheable-look virtual, and orphan boolean fields are
// all removed.
//
// These tests verify the four factory helpers plus the canonical per-node
// behaviours and the compile-time invariant that no mutation path exists
// after construction.
//
// The probe machinery (`HasPolicyMutationSetter`, `HasCacheableVirtualProbe`)
// is documented in `test_cache_policy_contract_meta.hpp`, which is a
// single occurrence of the legacy API names for the audit exemption.

#include <chronon3d/render_graph/core/cache_policy.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/composite_node.hpp>
#include <chronon3d/render_graph/nodes/mask_node.hpp>
#include <chronon3d/render_graph/nodes/video_node.hpp>
#include <chronon3d/render_graph/preflight/preflight_render_graph.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <string>
#include <type_traits>
#include <utility>

#include "test_cache_policy_contract_meta.hpp"

using namespace chronon3d;
using namespace chronon3d::graph;
using namespace chronon3d::graph::contract_probe;

// ── 0) Compile-time invariants ─────────────────────────────────────────────

namespace {

class CachePolicyTestNode final : public RenderGraphNode {
public:
    explicit CachePolicyTestNode(RenderNodeCachePolicy policy)
        : RenderGraphNode(std::move(policy)) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Source; }
    [[nodiscard]] std::string_view name() const noexcept override { return "CachePolicyTestNode"; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext&,
        std::span<const std::optional<raster::BBox>>
    ) const override { return std::nullopt; }

    cache::NodeCacheKey cache_key(const RenderGraphContext&) const override { return {}; }

    OwnedFB execute(
        RenderGraphContext&,
        std::span<const FramebufferRef>,
        std::span<const std::optional<raster::BBox>>
    ) override { return nullptr; }
};

} // namespace

// ── Compile-time immutability contract ─────────────────────────────────────

TEST_CASE("CachePolicyContract - node policy is read-only after construction") {
    static_assert(
        !HasPolicyMutationSetter<RenderGraphNode>,
        "RenderGraphNode: " CHRONON3D_POLICY_READONLY_HINT " (legacy mutation entry is forbidden).");
    static_assert(
        !HasPolicyMutationSetter<CachePolicyTestNode>,
        "CachePolicyTestNode: " CHRONON3D_POLICY_READONLY_HINT " (legacy mutation entry is forbidden).");
}

TEST_CASE("CachePolicyContract - no cacheable-look virtual override") {
    static_assert(
        !HasCacheableVirtualProbe<RenderGraphNode>,
        "RenderGraphNode: " CHRONON3D_CACHEABLE_LOOK_HINT " (the canonical accessor is node.cache_policy().enabled()).");
    static_assert(
        !HasCacheableVirtualProbe<CachePolicyTestNode>,
        "CachePolicyTestNode: " CHRONON3D_CACHEABLE_LOOK_HINT " (the canonical accessor is node.cache_policy().enabled()).");
}

TEST_CASE("CachePolicyContract - cache_policy() returns const reference (no virtual dispatch)") {
    // The base class exposes the policy descriptor as a const-reference
    // getter without virtual dispatch, so the executor reads the same
    // field for every node type.
    static_assert(
        std::is_same_v<
            decltype(std::declval<const RenderGraphNode&>().cache_policy()),
            const RenderNodeCachePolicy&>,
        "cache_policy() must return const RenderNodeCachePolicy& (no virtual dispatch).");
}

TEST_CASE("CachePolicyContract - policy is copyable (executor pass-by-value)") {
    static_assert(std::is_copy_constructible_v<RenderNodeCachePolicy>,
                  "RenderNodeCachePolicy must be copy-constructible.");
    static_assert(std::is_copy_assignable_v<RenderNodeCachePolicy>,
                  "RenderNodeCachePolicy must be copy-assignable.");
    // Trivial copyability is NOT required (debug_reason is std::string).
    static_assert(!std::is_trivially_copyable_v<RenderNodeCachePolicy>,
                  "RenderNodeCachePolicy is NOT trivially copyable (debug_reason "
                  "is std::string); this is intentional, copy-constructibility "
                  "is the meaningful invariant.");
}

TEST_CASE("CachePolicyContract - policy is moveable (factory return paths)") {
    static_assert(std::is_move_constructible_v<RenderNodeCachePolicy>,
                  "RenderNodeCachePolicy must be move-constructible.");
    static_assert(std::is_move_assignable_v<RenderNodeCachePolicy>,
                  "RenderNodeCachePolicy must be move-assignable.");
}

// ── 1) Policy factories ────────────────────────────────────────────────────

TEST_CASE("CachePolicyContract - no_cache disables caching") {
    auto policy = no_cache("test_reason");
    CHECK_FALSE(policy.enabled());
    CHECK_FALSE(policy.is_frame_variant());
    CHECK_FALSE(policy.persistent());
    CHECK_FALSE(policy.reusable_across_frames());
    CHECK(policy.mode == CacheMode::Disabled);
    CHECK(policy.invalidation == CacheInvalidation::Always);
    CHECK(policy.debug_reason == std::string("test_reason"));
}

TEST_CASE("CachePolicyContract - frame_variant_cache is the default per-frame mode") {
    auto policy = frame_variant_cache("animated_transform");
    CHECK(policy.enabled());
    CHECK(policy.is_frame_variant());
    CHECK_FALSE(policy.persistent());
    CHECK_FALSE(policy.reusable_across_frames());
    CHECK(policy.mode == CacheMode::FrameVariant);
    CHECK(policy.invalidation == CacheInvalidation::WhenInputsChange);
    CHECK(policy.debug_reason == std::string("animated_transform"));
}

TEST_CASE("CachePolicyContract - static_memory_cache is frame-invariant (memory only)") {
    auto policy = static_memory_cache("static_source");
    CHECK(policy.enabled());
    CHECK_FALSE(policy.is_frame_variant());
    CHECK_FALSE(policy.persistent());
    CHECK(policy.reusable_across_frames());
    CHECK(policy.mode == CacheMode::FrameInvariantMemory);
    CHECK(policy.invalidation == CacheInvalidation::WhenParamsChange);
    CHECK(policy.debug_reason == std::string("static_source"));
}

TEST_CASE("CachePolicyContract - static_persistent_cache enables disk bake") {
    auto policy = static_persistent_cache("precomp_static");
    CHECK(policy.enabled());
    CHECK_FALSE(policy.is_frame_variant());
    CHECK(policy.persistent());
    CHECK(policy.reusable_across_frames());
    CHECK(policy.mode == CacheMode::FrameInvariantPersistent);
    CHECK(policy.invalidation == CacheInvalidation::WhenParamsChange);
    CHECK(policy.debug_reason == std::string("precomp_static"));
}

TEST_CASE("CachePolicyContract - distinct factories yield distinct modes") {
    auto a = no_cache("a");
    auto b = frame_variant_cache("b");
    auto c = static_memory_cache("c");
    auto d = static_persistent_cache("d");
    CHECK(a.mode == CacheMode::Disabled);
    CHECK(b.mode == CacheMode::FrameVariant);
    CHECK(c.mode == CacheMode::FrameInvariantMemory);
    CHECK(d.mode == CacheMode::FrameInvariantPersistent);
    CHECK(a.mode != b.mode);
    CHECK(b.mode != c.mode);
    CHECK(c.mode != d.mode);
    CHECK_FALSE(a.persistent());
    CHECK_FALSE(c.persistent());
    CHECK(d.persistent());
    CHECK_FALSE(a.reusable_across_frames());
    CHECK_FALSE(b.reusable_across_frames());
    CHECK(c.reusable_across_frames());
    CHECK(d.reusable_across_frames());
}

// ── 2) Constructor-injection contract ──────────────────────────────────────

TEST_CASE("CachePolicyContract - policy is fixed at construction (no setter)") {
    CachePolicyTestNode node{static_persistent_cache("fixed")};

    CHECK(node.cache_policy().persistent());
    CHECK_FALSE(node.cache_policy().is_frame_variant());
    CHECK(node.cache_policy().reusable_across_frames());
    CHECK(node.cache_policy().mode == CacheMode::FrameInvariantPersistent);
    CHECK(node.cache_policy().debug_reason == std::string("fixed"));

    CachePolicyTestNode other{no_cache("different")};
    CHECK_FALSE(other.cache_policy().enabled());
    CHECK(other.cache_policy().mode == CacheMode::Disabled);
    CHECK(other.cache_policy().debug_reason == std::string("different"));

    CHECK(node.cache_policy().mode == CacheMode::FrameInvariantPersistent);
    CHECK(other.cache_policy().mode == CacheMode::Disabled);
}

// ── 3) Per-node defaults ───────────────────────────────────────────────────

TEST_CASE("NodeCachePolicy - video node is never cached") {
    video::VideoSource src{};
    VideoNode node(src, /*decoder*/nullptr, Frame{0});

    auto policy = node.cache_policy();
    CHECK_FALSE(policy.enabled());
    CHECK_FALSE(policy.persistent());
    CHECK_FALSE(policy.reusable_across_frames());
    CHECK(policy.mode == CacheMode::Disabled);
    CHECK(policy.debug_reason == std::string("video"));
}

TEST_CASE("NodeCachePolicy - static source is persistent (eligible for disk bake)") {
    ::chronon3d::RenderNode rn{};
    cache::NodeCacheKey key{};
    SourceNode node("static-source", rn, key,
                    /*centered*/false, /*24d*/false,
                    /*matrix_override*/std::nullopt,
                    /*opacity_override*/std::nullopt,
                    /*cache_static*/true);

    auto policy = node.cache_policy();
    CHECK(policy.enabled());
    CHECK_FALSE(policy.is_frame_variant());
    CHECK(policy.persistent());
    CHECK(policy.reusable_across_frames());
    CHECK(policy.mode == CacheMode::FrameInvariantPersistent);
    CHECK(policy.debug_reason == std::string("source_static"));
}

TEST_CASE("NodeCachePolicy - animated source is frame variant") {
    ::chronon3d::RenderNode rn{};
    cache::NodeCacheKey key{};
    SourceNode node("animated-source", rn, key,
                    /*centered*/false, /*24d*/false,
                    /*matrix_override*/std::nullopt,
                    /*opacity_override*/std::nullopt,
                    /*cache_static*/false);

    auto policy = node.cache_policy();
    CHECK(policy.enabled());
    CHECK(policy.is_frame_variant());
    CHECK_FALSE(policy.persistent());
    CHECK_FALSE(policy.reusable_across_frames());
    CHECK(policy.mode == CacheMode::FrameVariant);
    CHECK(policy.debug_reason == std::string("source_animated"));
}

TEST_CASE("NodeCachePolicy - default transform is frame variant") {
    TransformNode node(Mat4(1.0f));

    auto policy = node.cache_policy();
    CHECK(policy.enabled());
    CHECK(policy.is_frame_variant());
    CHECK_FALSE(policy.persistent());
    CHECK(policy.mode == CacheMode::FrameVariant);
    CHECK(policy.debug_reason == std::string("transform"));
}

TEST_CASE("NodeCachePolicy - composite declared persistent at construction") {
    CompositeNode node(::chronon3d::BlendMode::Normal,
                       /*cache_frame*/Frame{0},
                       /*world_z*/0.0f,
                       ::chronon3d::CompositeOperator::SourceOver,
                       static_persistent_cache("composite_static"));

    auto policy = node.cache_policy();
    CHECK(policy.enabled());
    CHECK_FALSE(policy.is_frame_variant());
    CHECK(policy.persistent());
    CHECK(policy.reusable_across_frames());
    CHECK(policy.mode == CacheMode::FrameInvariantPersistent);
    CHECK(policy.debug_reason == std::string("composite_static"));
}

// ── 4) Cross-cutting invariants ────────────────────────────────────────────

TEST_CASE("CachePolicyContract - enabled() true iff mode != Disabled") {
    CHECK_FALSE(no_cache("x").enabled());
    CHECK(frame_variant_cache("x").enabled());
    CHECK(static_memory_cache("x").enabled());
    CHECK(static_persistent_cache("x").enabled());
}

TEST_CASE("CachePolicyContract - persistent() iff FrameInvariantPersistent") {
    CHECK_FALSE(no_cache("x").persistent());
    CHECK_FALSE(frame_variant_cache("x").persistent());
    CHECK_FALSE(static_memory_cache("x").persistent());
    CHECK(static_persistent_cache("x").persistent());
}

TEST_CASE("CachePolicyContract - reusable_across_frames() iff FrameInvariantMemory or FrameInvariantPersistent") {
    CHECK_FALSE(no_cache("x").reusable_across_frames());
    CHECK_FALSE(frame_variant_cache("x").reusable_across_frames());
    CHECK(static_memory_cache("x").reusable_across_frames());
    CHECK(static_persistent_cache("x").reusable_across_frames());
}

TEST_CASE("CachePolicyContract - is_frame_variant() iff mode == FrameVariant") {
    CHECK_FALSE(no_cache("x").is_frame_variant());
    CHECK(frame_variant_cache("x").is_frame_variant());
    CHECK_FALSE(static_memory_cache("x").is_frame_variant());
    CHECK_FALSE(static_persistent_cache("x").is_frame_variant());
}
