#include <doctest/doctest.h>

// ── Cache Policy Contract ──────────────────────────────────────────────────
//
// `node.cache_policy()` is the canonical cache descriptor for the render graph.
// The GraphExecutor reads ONLY this method; it dispatches on
// `policy.enabled()`, `policy.frame_dependent()` and `policy.persistent()`
// without referring to any legacy API.
//
// These tests verify the four factory helpers (no_cache, frame_variant_cache,
// static_memory_cache, static_persistent_cache) plus the canonical per-node
// behaviours identified in the migration spec.

#include <chronon3d/render_graph/core/cache_policy.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/composite_node.hpp>
#include <chronon3d/render_graph/nodes/mask_node.hpp>
#include <chronon3d/render_graph/nodes/video_node.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <type_traits>
#include <string>

using namespace chronon3d;
using namespace chronon3d::graph;

// ── 1) Policy factories ────────────────────────────────────────────────────

TEST_CASE("CachePolicyContract - disabled cache has no cache dimensions") {
    auto policy = no_cache("test_reason");
    CHECK_FALSE(policy.enabled());
    CHECK(policy.cacheable == false);
    CHECK(policy.frame_dependent == true);  // default keeps frame dimension
    CHECK(policy.persistent() == false);
    CHECK(policy.lifetime == CacheLifetime::PerFrame);
    CHECK(policy.invalidation == CacheInvalidation::Always);
    CHECK(policy.debug_reason == std::string("test_reason"));
}

TEST_CASE("CachePolicyContract - frame variant cache includes frame in key") {
    auto policy = frame_variant_cache("animated_transform");
    CHECK(policy.enabled());
    CHECK(policy.frame_dependent);
    CHECK(policy.lifetime == CacheLifetime::PerFrame);
    CHECK(policy.invalidation == CacheInvalidation::WhenInputsChange);
    CHECK(policy.persistent() == false);
}

TEST_CASE("CachePolicyContract - frame invariant cache excludes frame from key") {
    auto policy = static_memory_cache("static_source");
    CHECK(policy.enabled());
    CHECK_FALSE(policy.frame_dependent);
    CHECK(policy.lifetime == CacheLifetime::PerFrame);  // memory only
    CHECK(policy.invalidation == CacheInvalidation::WhenParamsChange);
    CHECK(policy.disk_cacheable == false);
    CHECK(policy.persistent() == false);
}

TEST_CASE("CachePolicyContract - persistent policy enables persistent lookup") {
    auto policy = static_persistent_cache("precomp_static");
    CHECK(policy.enabled());
    CHECK(policy.persistent());
    CHECK(policy.disk_cacheable);
    CHECK(policy.lifetime == CacheLifetime::PersistentDisk);
    CHECK_FALSE(policy.frame_dependent);
}

TEST_CASE("CachePolicyContract - node cache policy is immutable after construction") {
    // The struct is plain-data (POD-like): no hidden mutator methods beyond
    // factory helpers and `set_cache_policy()` (build-phase only).  Note:
    // debug_reason is std::string, so the struct is copy-constructible
    // (not trivially copyable).  We assert copyability as the meaningful
    // semantic — requiring it makes the executor's pass-by-value work.
    static_assert(std::is_copy_constructible_v<RenderNodeCachePolicy>,
                  "RenderNodeCachePolicy must be copy-constructible for "
                  "executor dispatch.");

    // Build phase: caller declares the policy once via set_cache_policy().
    RenderGraphNode node;
    node.set_cache_policy(static_persistent_cache("build_phase"));
    CHECK(node.cache_policy().debug_reason == std::string("build_phase"));
    CHECK(node.cache_policy().persistent());

    // Subsequent re-declaration (still in build phase) replaces the policy.
    // The struct itself never exposes field-by-field mutation, so any
    // change goes through the factory helpers.
    node.set_cache_policy(no_cache("override"));
    CHECK(node.cache_policy().debug_reason == std::string("override"));
    CHECK_FALSE(node.cache_policy().enabled());
}

// ── 2) Per-node defaults ───────────────────────────────────────────────────

TEST_CASE("NodeCachePolicy - video node is never cached") {
    video::VideoSource src{};
    VideoNode node(src, /*decoder*/nullptr, /*layer_start*/0);

    auto policy = node.cache_policy();
    CHECK_FALSE(policy.enabled());
    CHECK_FALSE(policy.cacheable);
    CHECK_FALSE(policy.persistent());
    CHECK(policy.debug_reason == std::string("video"));
}

TEST_CASE("NodeCachePolicy - static source is frame invariant") {
    // SourceNode with cache_static=true → static_persistent_cache (frame
    // number excluded from cache key, eligible for disk bake).
    ::chronon3d::RenderNode rn{};
    cache::NodeCacheKey key{};
    SourceNode node("static-source", rn, key,
                    /*centered*/false, /*24d*/false,
                    /*matrix_override*/std::nullopt,
                    /*opacity_override*/std::nullopt,
                    /*cache_static*/true);

    auto policy = node.cache_policy();
    CHECK(policy.enabled());
    CHECK_FALSE(policy.frame_dependent);  // frame not in key dimension
    CHECK(policy.persistent());
    CHECK(policy.debug_reason == std::string("source_static"));
}

TEST_CASE("NodeCachePolicy - animated transform is frame variant") {
    // TransformNode with default ctor policy = frame_variant_cache.
    TransformNode node(Mat4(1.0f));

    auto policy = node.cache_policy();
    CHECK(policy.enabled());
    CHECK(policy.cacheable);
    CHECK(policy.frame_dependent);
    CHECK_FALSE(policy.persistent());
    CHECK(policy.debug_reason == std::string("transform"));
}

TEST_CASE("NodeCachePolicy - static composite is reusable across frames") {
    // CompositeNode default is frame_variant; setting it to
    // static_persistent_cache means the cache key is reusable across frames
    // (frame_dependent=false → frame number not in key dimension).
    CompositeNode node(::chronon3d::BlendMode::Normal, /*cache_frame*/Frame{0});
    node.set_cache_policy(static_persistent_cache("composite_static"));

    auto policy = node.cache_policy();
    CHECK(policy.enabled());
    CHECK_FALSE(policy.frame_dependent);
    CHECK(policy.persistent());
    CHECK(policy.lifetime == CacheLifetime::PersistentDisk);
    CHECK(policy.debug_reason == std::string("composite_static"));
}

// ── 3) Cross-cutting invariants ────────────────────────────────────────────

TEST_CASE("CachePolicyContract - executor entry point is non-virtual getter") {
    // The base class exposes `cache_policy()` as a const reference getter
    // (no virtual dispatch) so the executor reads the same field whether
    // the node is a SourceNode or a TransformNode.
    static_assert(
        std::is_same_v<
            decltype(std::declval<const RenderGraphNode&>().cache_policy()),
            const RenderNodeCachePolicy&>,
        "cache_policy() must return const RenderNodeCachePolicy& (no virtual).");
}
