#include "fused_batch_policy.hpp"

namespace chronon3d::graph::detail {

FusedBatchKind resolve_fused_batch_kind(
    const bool backend_available,
    const bool batch_nonempty,
    const bool contains_text_node) noexcept {
    if (!backend_available || !batch_nonempty) return FusedBatchKind::Skip;
    return contains_text_node ? FusedBatchKind::Text : FusedBatchKind::Retired;
}

} // namespace chronon3d::graph::detail
