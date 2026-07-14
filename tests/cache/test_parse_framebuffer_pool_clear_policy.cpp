// TICKET-PARSE-POLICY-HELPER-DEDUP — parse_framebuffer_pool_clear_policy
// regression tests (1 TEST_CASE with 5 SUBCASEs per ticket Criteri di
// accettazione: 3 accepted values + 1 invalid + 1 case-insensitive
// variant that iterates the 3 values in PascalCase form).

#include <doctest/doctest.h>
#include <chronon3d/cache/framebuffer_pool.hpp>

using chronon3d::cache::FramebufferPoolClearPolicy;
using chronon3d::cache::parse_framebuffer_pool_clear_policy;

TEST_CASE("parse_framebuffer_pool_clear_policy: accepted values + invalid + case-insensitive") {
    SUBCASE("keep-warm -> KeepWarm") {
        auto result = parse_framebuffer_pool_clear_policy("keep-warm");
        REQUIRE(result.has_value());
        CHECK(*result == FramebufferPoolClearPolicy::KeepWarm);
    }

    SUBCASE("trim-after-job -> TrimAfterJob") {
        auto result = parse_framebuffer_pool_clear_policy("trim-after-job");
        REQUIRE(result.has_value());
        CHECK(*result == FramebufferPoolClearPolicy::TrimAfterJob);
    }

    SUBCASE("trim-on-memory-pressure -> TrimOnMemoryPressure") {
        auto result = parse_framebuffer_pool_clear_policy("trim-on-memory-pressure");
        REQUIRE(result.has_value());
        CHECK(*result == FramebufferPoolClearPolicy::TrimOnMemoryPressure);
    }

    SUBCASE("invalid -> nullopt") {
        // Empty string, bogus value, whitespace, and underscores all
        // return nullopt (strict matching per ticket Criteri di
        // accettazione: no auto-trimming or auto-conversion).
        CHECK_FALSE(parse_framebuffer_pool_clear_policy("").has_value());
        CHECK_FALSE(parse_framebuffer_pool_clear_policy("bogus-value").has_value());
        CHECK_FALSE(parse_framebuffer_pool_clear_policy(" keep-warm").has_value());
        CHECK_FALSE(parse_framebuffer_pool_clear_policy("keep-warm ").has_value());
        CHECK_FALSE(parse_framebuffer_pool_clear_policy("keep_warm").has_value());
    }

    SUBCASE("case-insensitive PascalCase") {
        // Iterates the 3 accepted values in PascalCase form (per ticket
        // Criteri di accettazione: case-insensitive variant).
        auto kw = parse_framebuffer_pool_clear_policy("KeepWarm");
        REQUIRE(kw.has_value());
        CHECK(*kw == FramebufferPoolClearPolicy::KeepWarm);

        auto taj = parse_framebuffer_pool_clear_policy("TrimAfterJob");
        REQUIRE(taj.has_value());
        CHECK(*taj == FramebufferPoolClearPolicy::TrimAfterJob);

        auto tomp = parse_framebuffer_pool_clear_policy("TrimOnMemoryPressure");
        REQUIRE(tomp.has_value());
        CHECK(*tomp == FramebufferPoolClearPolicy::TrimOnMemoryPressure);
    }
}
