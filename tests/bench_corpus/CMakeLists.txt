# tests/bench_corpus/CMakeLists.txt — Sanity test for the 12-scene bench corpus.
#
# Forward-point 2 of TICKET-TEXT-ANIMATOR-WORKING-TREE-MIGRATION.
# Locks-in regressions on the recently migrated TextAnimator→animated_text
# call sites by sanity-checking dimension / duration / shape count
# post-evaluate for every bench_b00..b11 factory.
#
# TIER=INTEGRATION with explicit LINK_TARGETS override (AUTHOR_WARNING
# expected per cmake/Chronon3DTestSuite.cmake): the bench corpus sources
# pull in `content/compositions/chronon_glow_final.hpp` (canonical glow
# factory used by B03 CinematicGlow1080p + B11 Portrait1080x1920), so we
# need `chronon3d_content` beyond the default INTEGRATION link contract
# of `chronon3d_pipeline + chronon3d_backend_software`.
#
# Feature gate: requires CHRONON3D_BUILD_CONTENT (B03 + B11 wrap
# `make_chronon_glow_final()` from `content/compositions/`). The test
# also re-uses `chronon3d_presets::detail::text_preset()` and the
# `chronon3d::content::glow_final` canonical factories, both of which
# live under the `content/` tree — not linking when CONTENT=OFF would
# silently break the test contract (the call would not link).
#
# Cat-3 minimal-surface: ZERO new SDK API; re-uses the bench factories
# declared in `examples/bench_corpus/bench_corpus_scenes.hpp` (header-only
# inline per ADR-016 single-source-of-truth).

if(NOT CHRONON3D_BUILD_CONTENT)
    return()  # B03 + B11 factories call content/chronon_glow_final — disabled without content
endif()

# The bench corpus source file MUST be compiled into this test binary
# (the test calls the `bench_bN*` free-functions directly, not via
# CompositionRegistry). `chronon3d_add_test_suite` accepts absolute-path
# SOURCES via `get_filename_component(ABSOLUTE)`, so the cross-directory
# reference compiles cleanly.
#
# Path-resolution note: when this per-area CMakeLists is included via
#    include("${CMAKE_CURRENT_SOURCE_DIR}/${definition}")
# from the umbrella `tests/CMakeLists.txt`, `CMAKE_CURRENT_SOURCE_DIR`
# points to `tests/` (NOT `tests/bench_corpus/`).  We therefore use
# `${CMAKE_CURRENT_LIST_DIR}` (= `tests/bench_corpus/` since this is
# the directory of the currently-included file) for relative source
# references, and `${CMAKE_SOURCE_DIR}` for absolute cross-tree refs.
# See precedent in tests/content_tests.cmake which uses the same
# `${CMAKE_CURRENT_SOURCE_DIR}/...` convention for its per-area sources.
chronon3d_add_test_suite(
    NAME chronon3d_bench_corpus_tests
    TIER INTEGRATION
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/test_bench_corpus_scenes.cpp
        ${CMAKE_SOURCE_DIR}/examples/bench_corpus/bench_corpus_scenes.cpp
    LINK_TARGETS
        chronon3d_pipeline
        chronon3d_backend_software
        chronon3d_content
)
