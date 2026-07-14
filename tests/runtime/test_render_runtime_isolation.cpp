// ===========================================================================
// tests/runtime/test_render_runtime_isolation.cpp
//
// Fase 5 — TICKET-P1-09 closure: verify two independent RenderRuntime
// instances do not share state (caches, pools, registries, resolvers,
// schedulers, executors).
//
// The test creates two runtimes with different Config budgets and
// different asset roots, then asserts that mutations on one do not
// leak into the other.  Pure construction + configuration test; no
// rendering, no threads, no time dependency, no PRNG.
//
// Cat-2 freeze-compliant (AGENTS.md v0.1: deterministic, no external
// dependencies beyond the runtime itself).
// ===========================================================================

#include <chronon3d/core/config.hpp>
#include <chronon3d/runtime/render_runtime.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <memory>

namespace chronon3d::test {

TEST_CASE("RenderRuntime isolation: two runtimes do not share state") {
    namespace fs = std::filesystem;

    // ── Build two Configs with different cache capacities ──────────
    // (cache() returns const CacheConfig& — setters not available.
    //  We verify isolation via pool.set_budget_bytes() post-creation.)
    chronon3d::Config cfg_a;
    chronon3d::Config cfg_b;

    runtime::RuntimeConfig rc_a{std::move(cfg_a), std::nullopt};
    runtime::RuntimeConfig rc_b{std::move(cfg_b), std::nullopt};

    // ── Create both runtimes via the canonical factory ─────────────
    auto result_a = runtime::RenderRuntime::create(std::move(rc_a));
    REQUIRE(result_a.has_value());
    auto runtime_a = std::move(result_a.value());

    auto result_b = runtime::RenderRuntime::create(std::move(rc_b));
    REQUIRE(result_b.has_value());
    auto runtime_b = std::move(result_b.value());

    // ── §1: Node caches are independent instances ──────────────────
    // NodeCache does not expose its configured capacity via a public
    // getter, but two runtimes MUST hold distinct cache instances.
    SUBCASE("node caches are independent instances") {
        const auto& nc_a = runtime_a->node_cache();
        const auto& nc_b = runtime_b->node_cache();
        CHECK(&nc_a != &nc_b);
    }

    // ── §2: Framebuffer pools are independent ──────────────────────
    SUBCASE("framebuffer pools are independent instances") {
        auto& pool_a = runtime_a->framebuffer_pool();
        auto& pool_b = runtime_b->framebuffer_pool();

        // Different addresses → different instances
        CHECK(&pool_a != &pool_b);

        // Each pool can be independently reconfigured without
        // affecting the other (proves true isolation).
        pool_a.set_budget_bytes(4 * 1024 * 1024);
        pool_b.set_budget_bytes(32 * 1024 * 1024);

        CHECK(pool_a.max_bytes() == 4 * 1024 * 1024);
        CHECK(pool_b.max_bytes() == 32 * 1024 * 1024);

        // Reconfiguring pool_a must NOT leak into pool_b
        CHECK(pool_b.max_bytes() != 4 * 1024 * 1024);
    }

    // ── §3: Asset registries are independent instances ─────────────
    SUBCASE("asset registries are independent instances") {
        const auto& ar_a = runtime_a->assets();
        const auto& ar_b = runtime_b->assets();
        CHECK(&ar_a != &ar_b);
    }

    // ── §4: Asset resolvers are independent ────────────────────────
    SUBCASE("asset resolvers mount independently") {
        auto& resolver_a = runtime_a->resolver();
        auto& resolver_b = runtime_b->resolver();

        // Initially both have empty mount roots (no assets_root in config)
        CHECK(resolver_a.mount_root().empty());
        CHECK(resolver_b.mount_root().empty());

        // Mount different paths — must stay independent
        resolver_a.mount(fs::path("/tmp"));
        resolver_b.mount(fs::path("/usr"));

        CHECK(resolver_a.mount_root() == "/tmp");
        CHECK(resolver_b.mount_root() == "/usr");

        // Mounting on runtime_a must NOT leak into runtime_b
        CHECK(resolver_b.mount_root() != "/tmp");
    }

    // ── §5: Schedulers are independent instances ───────────────────
    SUBCASE("execution schedulers are independent") {
        const auto& sched_a = runtime_a->scheduler();
        const auto& sched_b = runtime_b->scheduler();
        CHECK(&sched_a != &sched_b);
    }

    // ── §6: Executors are independent instances ────────────────────
    SUBCASE("graph executors are independent") {
        const auto& exec_a = runtime_a->executor();
        const auto& exec_b = runtime_b->executor();
        CHECK(&exec_a != &exec_b);
    }

    // ── §7: Backends start unattached ──────────────────────────────
    // P1-14 — the previous `populate() is idempotent` SUBCASE was
    // REMOVED.  `populate()` is now PRIVATE; the canonical factory
    // `RenderRuntime::create(RuntimeConfig)` produces a fully-populated
    // runtime, and the test for re-populate idempotency was testing
    // implementation detail (the idempotency check is now an internal
    // contract of the factory + 1-arg ctor, not externally observable
    // behavior).
    SUBCASE("backends start unattached") {
        CHECK_FALSE(runtime_a->backend_attached());
        CHECK_FALSE(runtime_b->backend_attached());
    }
}

} // namespace chronon3d::test
