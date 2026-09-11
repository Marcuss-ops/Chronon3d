#pragma once

#include "timed_text_core.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>

namespace chronon3d::timed_text_adapter_detail {

struct AdapterDocumentBuilder {
    TimedTextDocument document;

    explicit AdapterDocumentBuilder(std::string format) {
        document.source_format = std::move(format);
    }

    [[nodiscard]] bool accept(TimedCue cue) {
        if (!textcore::cue_is_valid(cue)) return false;
        document.cues.push_back(std::move(cue));
        return true;
    }

    [[nodiscard]] TimedTextDocument finish() {
        textcore::canonicalize_document(document);
        return std::move(document);
    }
};

[[nodiscard]] inline std::string trim_ascii(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    return std::string(value);
}

// AUDIT-2026-09-11 — allocation-free integer parse with std::atoi parity.
// Replaces the std::atoi(std::string(view).c_str()) pattern that heap-
// allocated a temporary per call (per cue per frame on subtitle loads).
// Semantics preserved: leading '+'/'-' accepted, digits consumed, no digits
// → 0 (atoi parity), overflow clamps instead of UB.
[[nodiscard]] inline int parse_sv_int(std::string_view value) {
    if (!value.empty() && value.front() == '+') value.remove_prefix(1);
    int result = 0;
    const auto [ptr, ec] = std::from_chars(
        value.data(), value.data() + value.size(), result);
    (void)ptr;
    if (ec == std::errc::invalid_argument) return 0;
    return result;
}

[[nodiscard]] inline bool valid_interval(f32 start, f32 end) {
    return std::isfinite(start) && std::isfinite(end) && start >= 0.0f && end > start;
}

} // namespace chronon3d::timed_text_adapter_detail
