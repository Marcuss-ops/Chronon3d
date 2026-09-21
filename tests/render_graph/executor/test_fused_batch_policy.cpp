#include <doctest/doctest.h>

#include "src/render_graph/executor/fused_batch_policy.hpp"

using chronon3d::graph::detail::FusedBatchKind;
using chronon3d::graph::detail::resolve_fused_batch_kind;

TEST_CASE("Fused batch policy admits only non-empty text batches") {
    CHECK(resolve_fused_batch_kind(false, true, true) == FusedBatchKind::Skip);
    CHECK(resolve_fused_batch_kind(true, false, true) == FusedBatchKind::Skip);
    CHECK(resolve_fused_batch_kind(true, true, true) == FusedBatchKind::Text);
    CHECK(resolve_fused_batch_kind(true, true, false) == FusedBatchKind::Retired);
}
