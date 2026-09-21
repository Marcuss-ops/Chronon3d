#include <doctest/doctest.h>

#include "src/render_graph/nodes/text_run/gpu_text_cached_policy.hpp"

using chronon3d::graph::text_run::detail::CachedTextExecutionPath;
using chronon3d::graph::text_run::detail::resolve_cached_text_execution_path;

TEST_CASE("Cached GPU text policy prioritizes explicit rejection gates") {
    CHECK(resolve_cached_text_execution_path(true, false, true, true, true) ==
          CachedTextExecutionPath::RejectUnsupportedAnimation);
    CHECK(resolve_cached_text_execution_path(false, true, true, true, true) ==
          CachedTextExecutionPath::RejectForcedSoftware);
}

TEST_CASE("Cached GPU text policy selects MTSDF only with native resources") {
    CHECK(resolve_cached_text_execution_path(false, false, true, true, true) ==
          CachedTextExecutionPath::TryMtsdf);
    CHECK(resolve_cached_text_execution_path(false, false, false, true, true) ==
          CachedTextExecutionPath::PackedBitmap);
    CHECK(resolve_cached_text_execution_path(false, false, true, false, true) ==
          CachedTextExecutionPath::PackedBitmap);
    CHECK(resolve_cached_text_execution_path(false, false, true, true, false) ==
          CachedTextExecutionPath::PackedBitmap);
}
