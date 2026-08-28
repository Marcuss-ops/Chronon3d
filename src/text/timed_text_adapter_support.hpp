#pragma once

#include "timed_text_core.hpp"

#include <algorithm>
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

[[nodiscard]] inline bool valid_interval(f32 start, f32 end) {
    return std::isfinite(start) && std::isfinite(end) && start >= 0.0f && end > start;
}

} // namespace chronon3d::timed_text_adapter_detail
