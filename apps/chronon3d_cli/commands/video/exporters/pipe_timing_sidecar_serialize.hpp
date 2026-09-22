#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// pipe_timing_sidecar_serialize.hpp — the ONE serializer for optional-valued
// timing-sidecar fields (TICKET-HOTSPOT-DECOMPOSITION A5).
//
// The sidecar reports measurements, and a measurement that was NOT taken is not
// the same fact as a measurement of zero. Every optional field in the document
// therefore goes through this single function, so "not measured" has exactly
// one spelling in the JSON (null) and a present field always carries its real
// value — there is no per-field default anywhere.
//
// Keeping the rule in one place is what makes the schema testable: the summary
// and job sections are now built by directly callable pure serializers
// (build_summary_section / serialize_job_section), and the frozen key sets in
// tests/cli/test_timing_sidecar_schema.cpp are the contract a future refactor
// must preserve.
// ═══════════════════════════════════════════════════════════════════════════

#include <nlohmann/json.hpp>

#include <optional>

namespace chronon3d::cli::pipe_timing_detail {

/// Write an optional measurement: present → its value, absent → JSON null.
template <typename T>
inline void put_optional(nlohmann::json& out, const char* key,
                         const std::optional<T>& value) {
    if (value) {
        out[key] = *value;
    } else {
        out[key] = nullptr;
    }
}

} // namespace chronon3d::cli::pipe_timing_detail
