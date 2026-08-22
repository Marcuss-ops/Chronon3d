// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string_view>
#include <string>
#include <vector>

namespace chronon3d::runtime {

/// Architectural metrics verifying zero-overhead fast path invariants.
struct FastPathVerificationMetrics {
    std::uint64_t raw_node_execute_count{0};
    std::uint64_t frame_heap_allocations{0};
    std::uint64_t cpu_pixel_readback_bytes{0};
    std::uint64_t unexplained_materializations{0};
    std::uint64_t intermediate_layer_surfaces{0};
    std::uint64_t implicit_color_conversions{0};
    std::uint64_t staging_copy_bytes{0};
    std::uint64_t phrase_atlas_builds{0};
    std::uint64_t full_canvas_text_surfaces{0};
    std::uint64_t full_canvas_text_clears{0};
    std::uint64_t structural_recompiles_per_job{0};
    std::uint64_t production_fallback_count{0};

    [[nodiscard]] bool passes_strict_fast_path() const noexcept {
        return raw_node_execute_count == 0 &&
               frame_heap_allocations == 0 &&
               cpu_pixel_readback_bytes == 0 &&
               unexplained_materializations == 0 &&
               intermediate_layer_surfaces == 0 &&
               implicit_color_conversions == 0 &&
               full_canvas_text_surfaces == 0 &&
               full_canvas_text_clears == 0;
    }
};

enum class FallbackStatus : std::uint8_t {
    Active,
    ReferenceOnly,
    ReadyToDelete,
    Deleted,
};

[[nodiscard]] constexpr std::string_view fallback_status_name(FallbackStatus s) noexcept {
    switch (s) {
        case FallbackStatus::Active:        return "ACTIVE";
        case FallbackStatus::ReferenceOnly: return "REFERENCE_ONLY";
        case FallbackStatus::ReadyToDelete: return "READY_TO_DELETE";
        case FallbackStatus::Deleted:       return "DELETED";
    }
    return "UNKNOWN";
}

/// Entry in the Canonical Fallback Retirement Register.
struct FallbackRetirementEntry {
    std::string     legacy_path;
    std::string     replacement;
    std::string     removal_condition;
    FallbackStatus  status{FallbackStatus::Active};
};

/// Canonical registry of legacy paths scheduled for demolition.
inline std::vector<FallbackRetirementEntry> canonical_fallback_register() {
    return {
        {"expanded composite surface", "affine composite primitive", "golden parity + 0 fallback", FallbackStatus::ReferenceOnly},
        {"full-frame text surface",    "direct GlyphInstance stream", "golden parity + effects covered", FallbackStatus::ReferenceOnly},
        {"packed run atlas",           "global paged glyph atlas",   "corpus parity", FallbackStatus::ReferenceOnly},
        {"raw node.execute()",         "CompiledOperation / PrimitiveStream", "all core processors recorded", FallbackStatus::ReferenceOnly},
        {"implicit RGBA domain",       "PixelDomain compiler",       "explicit domain transitions working", FallbackStatus::ReferenceOnly},
        {"per-frame heap allocation",  "PhysicalResourcePlan",       "allocation counter = 0", FallbackStatus::ReadyToDelete},
    };
}

} // namespace chronon3d::runtime
