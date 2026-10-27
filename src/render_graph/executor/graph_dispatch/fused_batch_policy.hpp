#pragma once

namespace chronon3d::graph::detail {

enum class FusedBatchKind {
    Skip,
    Text,
    Retired,
};

/// Classify a fused batch before the executor touches GPU/text resources.
/// Empty batches and unavailable backends are silent no-ops; non-text batches
/// are retained as an explicit retired-path error at the caller boundary.
[[nodiscard]] FusedBatchKind resolve_fused_batch_kind(
    bool backend_available,
    bool batch_nonempty,
    bool contains_text_node) noexcept;

} // namespace chronon3d::graph::detail
