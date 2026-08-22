// tests/runtime/test_template_program_cache.cpp
// ════════════════════════════════════════════════════════════════════════════
// Fase H (TICKET-VIDEO-COMPILER-ARCH-V1) — TemplateProgramCache unit tests.
//
// TIER=UNIT, no GPU / Blend2D / FontEngine dependency.  The cache is
// keyed by ProgramFingerprint (Fase A template key) and stores
// CompiledTemplateProgram (shared_ptr).  Builder callbacks return
// hand-assembled programs; the LRU + pinned-residency semantics are
// verified in isolation.
// ════════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/runtime/template_program_cache.hpp>
#include <chronon3d/render_graph/compiler/compiled_template_program.hpp>

#include <memory>

using namespace chronon3d;
using namespace chronon3d::runtime;
using chronon3d::graph::CompiledFrameGraph;
using chronon3d::graph::CompiledTemplateProgram;

namespace {

/// Build a minimal valid CompiledTemplateProgram via the canonical
/// compile_template_program() path (Fase A).  The compiled frame graph
/// is mutable here; the template lift moves it into shared_ptr.
std::shared_ptr<const CompiledTemplateProgram> make_program(
    std::uint64_t topology_hash, std::uint32_t abi = graph::kRenderAbiV1) {
    CompiledFrameGraph g;
    g.valid = true;
    g.structure_hash = topology_hash;
    g.output = 0;
    g.levels = {{0}};
    g.nodes.resize(1);
    g.nodes[0].id = 0;
    g.nodes[0].kind = graph::RenderGraphNodeKind::Source;
    g.nodes[0].shape_type = 7;  // Image → static
    g.program.levels = g.levels;

    auto prog = std::make_shared<CompiledTemplateProgram>(
        graph::compile_template_program(std::move(g)));
    // Override the fingerprint for the test (compile_template_program
    // uses structure_hash; we want explicit control).
    const_cast<CompiledTemplateProgram*>(prog.get())->fingerprint.topology_hash = topology_hash;
    const_cast<CompiledTemplateProgram*>(prog.get())->fingerprint.renderer_abi = abi;
    return prog;
}

} // namespace

TEST_CASE("TemplateProgramCache: ResidencyBudget default + equality") {
    ResidencyBudget a;
    CHECK(a.compiled_programs == 0);
    CHECK(ResidencyBudget::kDefaultCompiledPrograms == 16);

    ResidencyBudget b;
    b.compiled_programs = 4;
    CHECK_FALSE(a == b);

    a.compiled_programs = 4;
    CHECK(a == b);
}

TEST_CASE("TemplateProgramCache: TemplateProgramKey is the Fase A fingerprint") {
    graph::ProgramFingerprint fp{0xABC, graph::kRenderAbiV1, graph::kQualityProfileDefault};
    TemplateProgramKey key = fp;  // alias — same type
    CHECK(key.topology_hash == 0xABC);
    CHECK(key.renderer_abi == graph::kRenderAbiV1);

    // Bindings are NOT part of the key (topological identity only).
    TemplateProgramKey other{0xABC, graph::kRenderAbiV1, graph::kQualityProfileDefault};
    CHECK(key == other);
}

TEST_CASE("TemplateProgramCache: compile returns cached program once per key") {
    TemplateProgramCache cache(4);

    const TemplateProgramKey key{0x111, graph::kRenderAbiV1, graph::kQualityProfileDefault};
    std::size_t build_count = 0;
    const auto builder = [&]() {
        ++build_count;
        return make_program(0x111);
    };

    const auto first = cache.compile(key, builder);
    CHECK(build_count == 1);
    REQUIRE(first != nullptr);
    CHECK(first->fingerprint.topology_hash == 0x111);

    // Same key → cache hit, builder NOT re-invoked.
    const auto second = cache.compile(key, builder);
    CHECK(build_count == 1);
    CHECK(second == first);

    const auto stats = cache.stats();
    CHECK(stats.hits == 1);
    CHECK(stats.misses == 1);
    CHECK(stats.total_entries == 1);
    CHECK(stats.pinned_entries == 0);
}

TEST_CASE("TemplateProgramCache: different topology → separate compile") {
    TemplateProgramCache cache(4);
    std::size_t build_count = 0;
    const auto builder = [&]() {
        ++build_count;
        return make_program(0x222);
    };

    (void)cache.compile(TemplateProgramKey{0x222, graph::kRenderAbiV1, 0}, builder);
    (void)cache.compile(TemplateProgramKey{0x333, graph::kRenderAbiV1, 0}, builder);
    CHECK(build_count == 2);

    // Quality profile differs → distinct key.
    (void)cache.compile(TemplateProgramKey{0x222, graph::kRenderAbiV1, 1}, builder);
    CHECK(build_count == 3);
}

TEST_CASE("TemplateProgramCache: LRU evicts cold entries at capacity") {
    TemplateProgramCache cache(2);  // two-entry LRU tier

    std::size_t build_count = 0;
    const auto builder = [&](std::uint64_t h) {
        return [&, h]() {
            ++build_count;
            return make_program(h);
        };
    };

    (void)cache.compile(TemplateProgramKey{1, graph::kRenderAbiV1, 0}, builder(1));
    (void)cache.compile(TemplateProgramKey{2, graph::kRenderAbiV1, 0}, builder(2));
    CHECK(build_count == 2);

    // Touch key 1 so key 2 becomes the LRU tail.
    (void)cache.compile(TemplateProgramKey{1, graph::kRenderAbiV1, 0}, builder(1));
    CHECK(build_count == 2);  // hit

    // Third distinct key evicts the LRU tail (key 2).
    (void)cache.compile(TemplateProgramKey{3, graph::kRenderAbiV1, 0}, builder(3));
    CHECK(build_count == 3);

    const auto stats = cache.stats();
    CHECK(stats.evictions == 1);
    CHECK(stats.lru_entries == 2);
    CHECK(stats.total_entries == 2);

    // Key 2 is gone → recompiles on demand.
    (void)cache.compile(TemplateProgramKey{2, graph::kRenderAbiV1, 0}, builder(2));
    CHECK(build_count == 4);
}

TEST_CASE("TemplateProgramCache: pinned residency survives eviction pressure") {
    TemplateProgramCache cache(2);  // tiny LRU tier

    std::size_t build_count = 0;
    const auto builder = [&](std::uint64_t h) {
        return [&, h]() {
            ++build_count;
            return make_program(h);
        };
    };

    // Pin key 1 (active job) — compiled into the pinned tier.
    TemplatePin pin1 = cache.pin(TemplateProgramKey{1, graph::kRenderAbiV1, 0}, builder(1));
    CHECK(pin1.valid());
    CHECK(build_count == 1);

    // Fill LRU with 3 distinct keys → key 1 must NOT be evicted.
    (void)cache.compile(TemplateProgramKey{2, graph::kRenderAbiV1, 0}, builder(2));
    (void)cache.compile(TemplateProgramKey{3, graph::kRenderAbiV1, 0}, builder(3));
    (void)cache.compile(TemplateProgramKey{4, graph::kRenderAbiV1, 0}, builder(4));

    // Inline snapshot (single call, no macro chaining ambiguity).
    std::size_t pinned, lru_entries, total, evicted;
    {
        auto xs = cache.stats();
        pinned      = xs.pinned_entries;
        lru_entries = xs.lru_entries;
        total       = xs.total_entries;
        evicted     = xs.evictions;
    }
    CHECK(pinned == 1);
    CHECK(lru_entries == 2);
    CHECK(total == 3);  // 2 LRU + 1 pinned
    CHECK(evicted >= 1);  // at least one eviction from capacity pressure

    // Pinned key survives: no recompile.
    const auto pinned_prog = cache.find(TemplateProgramKey{1, graph::kRenderAbiV1, 0});
    REQUIRE(pinned_prog != nullptr);
    CHECK(pinned_prog->fingerprint.topology_hash == 1);
    CHECK(build_count == 4);  // no rebuild of key 1

    // Release pin → key 1 returns to the LRU pool.  LRU is at capacity
    // (2 entries), so the re-insert evicts the LRU tail.
    pin1 = TemplatePin{};
    CHECK_FALSE(pin1.valid());
    auto stats2 = cache.stats();
    CHECK(stats2.pinned_entries == 0);
    // 0 pinned + 2 LRU (capacity-bound: unpin triggers eviction)
    CHECK(stats2.total_entries == 2);

    // Still present in LRU (eviction is LRU-ordered, not immediate).
    const auto after = cache.find(TemplateProgramKey{1, graph::kRenderAbiV1, 0});
    CHECK(after != nullptr);
}

TEST_CASE("TemplateProgramCache: pin promotes an existing LRU entry") {
    TemplateProgramCache cache(4);

    std::size_t build_count = 0;
    const auto builder = [&]() {
        ++build_count;
        return make_program(0x555);
    };

    // Compile into LRU first.
    (void)cache.compile(TemplateProgramKey{0x555, graph::kRenderAbiV1, 0}, builder);
    CHECK(build_count == 1);
    CHECK(cache.stats().lru_entries == 1);

    // Pin promotes it — no recompile, LRU tier loses the entry.
    TemplatePin pin = cache.pin(TemplateProgramKey{0x555, graph::kRenderAbiV1, 0}, builder);
    CHECK(build_count == 1);
    CHECK(cache.stats().pinned_entries == 1);
    CHECK(cache.stats().lru_entries == 0);

    // Release → back in LRU.
    pin = TemplatePin{};
    CHECK(cache.stats().pinned_entries == 0);
    CHECK(cache.stats().lru_entries == 1);
}

TEST_CASE("TemplateProgramCache: find returns nullptr for absent keys") {
    TemplateProgramCache cache(4);
    const auto missing = cache.find(TemplateProgramKey{0x999, graph::kRenderAbiV1, 0});
    CHECK(missing == nullptr);
}

TEST_CASE("TemplateProgramCache: clear empties both tiers") {
    TemplateProgramCache cache(4);

    std::size_t build_count = 0;
    const auto builder = [&](std::uint64_t h) {
        return [&, h]() {
            ++build_count;
            return make_program(h);
        };
    };

    (void)cache.compile(TemplateProgramKey{1, graph::kRenderAbiV1, 0}, builder(1));
    TemplatePin pin = cache.pin(TemplateProgramKey{2, graph::kRenderAbiV1, 0}, builder(2));
    CHECK(cache.stats().total_entries == 2);

    cache.clear();
    CHECK(cache.stats().total_entries == 0);
    CHECK(cache.find(TemplateProgramKey{1, graph::kRenderAbiV1, 0}) == nullptr);
}
