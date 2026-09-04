#pragma once

#include <cstdint>

namespace chronon3d::render_plan {

/// Resource limits applied before a RenderPlan can reach compilation.
/// Pure admission limits do not define rendered-content identity. The
/// temporal-pixel ceiling is the deliberate exception: it can constrain
/// temporal sample execution and therefore participates in the plan content
/// fingerprint.
/// The defaults are deliberately finite so untrusted jobs cannot request
/// unbounded framebuffer, frame, text, or temporal sample allocations.
struct RenderBudget {
    std::uint32_t max_width{7680};
    std::uint32_t max_height{4320};
    std::uint64_t max_total_pixels{7680ULL * 4320ULL};
    std::uint64_t max_frames{1'000'000};
    std::uint32_t max_layers{1024};
    std::uint64_t max_text_bytes{4ULL * 1024ULL * 1024ULL};
    std::uint64_t max_asset_reference_bytes{1ULL * 1024ULL * 1024ULL};
    std::uint64_t max_estimated_output_bytes{4ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL};
    std::uint64_t max_peak_memory_bytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
    /// Maximum aggregate pixels across all temporal motion-blur samples.
    /// Zero selects the resolver's hard safety ceiling; non-zero values may
    /// only lower that ceiling.
    std::uint64_t max_temporal_pixels{128ULL * 1024ULL * 1024ULL};
};

}  // namespace chronon3d::render_plan
