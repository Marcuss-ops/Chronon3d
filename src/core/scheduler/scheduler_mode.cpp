#include <chronon3d/core/scheduler/scheduler_mode.hpp>

#include <cctype>
#include <string>

namespace chronon3d {

namespace {

std::string to_lower_copy(std::string_view sv) {
    std::string buf(sv);
    for (char& c : buf) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return buf;
}

} // namespace

std::string_view scheduler_mode_name(SchedulerMode mode) noexcept {
    switch (mode) {
        case SchedulerMode::Sequential:   return "Sequential";
        case SchedulerMode::TbbAutomatic: return "TbbAutomatic";
        case SchedulerMode::TbbFixed:     return "TbbFixed";
    }
    return "TbbAutomatic";
}

bool parse_scheduler_mode(std::string_view text, SchedulerMode& out) noexcept {
    const std::string lower = to_lower_copy(text);
    if (lower == "sequential")                              { out = SchedulerMode::Sequential;   return true; }
    if (lower == "auto" || lower == "automatic" || lower == "tbb") {
        out = SchedulerMode::TbbAutomatic;
        return true;
    }
    if (lower == "fixed" || lower == "tbbfixed")            { out = SchedulerMode::TbbFixed;     return true; }
    return false;
}

} // namespace chronon3d
