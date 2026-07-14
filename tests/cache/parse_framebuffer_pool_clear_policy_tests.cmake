# TICKET-PARSE-POLICY-HELPER-DEDUP — parse_framebuffer_pool_clear_policy
# regression test. TIER=UNIT unconditional registration per ADR-018
# (SDK-only build compatibility — no vcpkg/FriBidi/Blend2D deps).

chronon3d_add_test_suite(
    NAME chronon3d_parse_framebuffer_pool_clear_policy_tests
    TIER UNIT
    SOURCES cache/test_parse_framebuffer_pool_clear_policy.cpp
)
