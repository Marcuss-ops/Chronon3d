#include <chronon3d/vidrush/visual_unit_report.hpp>

#include <nlohmann/json.hpp>

#include <utility>

namespace chronon3d::vidrush {
namespace {

VisualUnitReportError error(std::string path, std::string message) {
    return {std::move(path), std::move(message)};
}

nlohmann::json optional_string(const std::optional<std::string>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json optional_i64(const std::optional<std::int64_t>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

std::optional<std::string> read_optional_string(const nlohmann::json& object,
                                                const char* key) {
    if (!object.contains(key) || object.at(key).is_null()) return std::nullopt;
    if (!object.at(key).is_string()) return std::nullopt;
    return object.at(key).get<std::string>();
}

std::optional<std::int64_t> read_optional_i64(const nlohmann::json& object,
                                              const char* key) {
    if (!object.contains(key) || object.at(key).is_null()) return std::nullopt;
    if (!object.at(key).is_number_integer()) return std::nullopt;
    return object.at(key).get<std::int64_t>();
}

void put_strings(nlohmann::json& object, const char* key,
                 const std::vector<std::string>& values) {
    object[key] = values;
}

std::vector<std::string> read_strings(const nlohmann::json& object,
                                      const char* key) {
    if (!object.contains(key) || !object.at(key).is_array()) return {};
    std::vector<std::string> values;
    for (const auto& value : object.at(key)) {
        if (value.is_string()) values.push_back(value.get<std::string>());
    }
    return values;
}

nlohmann::json report_to_json_unchecked(const VisualUnitReport& report) {
    return {
        {"scene_id", report.scene_id},
        {"visual_unit_id", report.visual_unit_id},
        {"start_ms", report.time.start_ms},
        {"end_ms", report.time.end_ms},
        {"target_duration_ms", report.time.target_duration_ms},
        {"semantic_profile", {
            {"entities", report.semantic_profile.entities},
            {"important_phrases", report.semantic_profile.important_phrases},
            {"visual_terms", report.semantic_profile.visual_terms},
        }},
        {"provider_decision", {
            {"selected", report.provider_decision.selected},
            {"reason", report.provider_decision.reason},
            {"model", report.provider_decision.model},
        }},
        {"query_plan", report.query_plan},
        {"candidate", {
            {"candidates_considered", report.candidate.candidates_considered},
            {"selected_candidate", optional_string(report.candidate.selected_candidate)},
            {"selected_source_duration_ms", optional_i64(report.candidate.selected_source_duration_ms)},
            {"source_url", optional_string(report.candidate.source_url)},
        }},
        {"selected_window", {
            {"start_ms", optional_i64(report.selected_window.start_ms)},
            {"end_ms", optional_i64(report.selected_window.end_ms)},
            {"duration_ms", optional_i64(report.selected_window.duration_ms)},
        }},
        {"asset", {
            {"asset_id", optional_string(report.asset.asset_id)},
            {"sha256", optional_string(report.asset.sha256)},
            {"local_path", optional_string(report.asset.local_path)},
        }},
        {"drive", {
            {"destination", optional_string(report.drive.destination)},
            {"link", optional_string(report.drive.link)},
            {"file_id", optional_string(report.drive.file_id)},
        }},
        {"cache", {
            {"status", cache_status_name(report.cache.status)},
            {"key", optional_string(report.cache.key)},
            {"discovery_calls", report.cache.discovery_calls},
            {"research_calls", report.cache.research_calls},
            {"provider_calls", report.cache.provider_calls},
            {"download_calls", report.cache.download_calls},
            {"transcode_calls", report.cache.transcode_calls},
            {"drive_upload_calls", report.cache.drive_upload_calls},
        }},
    };
}

VisualUnitReport report_from_json_unchecked(const nlohmann::json& value) {
    VisualUnitReport report;
    report.scene_id = value.value("scene_id", std::string{});
    report.visual_unit_id = value.value("visual_unit_id", std::string{});
    report.time.start_ms = value.value("start_ms", std::int64_t{0});
    report.time.end_ms = value.value("end_ms", std::int64_t{0});
    report.time.target_duration_ms = value.value("target_duration_ms", std::int64_t{0});

    if (value.contains("semantic_profile") && value.at("semantic_profile").is_object()) {
        const auto& semantic = value.at("semantic_profile");
        report.semantic_profile.entities = read_strings(semantic, "entities");
        report.semantic_profile.important_phrases = read_strings(semantic, "important_phrases");
        report.semantic_profile.visual_terms = read_strings(semantic, "visual_terms");
    }
    if (value.contains("provider_decision") && value.at("provider_decision").is_object()) {
        const auto& decision = value.at("provider_decision");
        report.provider_decision.selected = decision.value("selected", std::string{});
        report.provider_decision.reason = decision.value("reason", std::string{});
        report.provider_decision.model = decision.value("model", std::string{});
    }
    report.query_plan = read_strings(value, "query_plan");

    if (value.contains("candidate") && value.at("candidate").is_object()) {
        const auto& candidate = value.at("candidate");
        report.candidate.candidates_considered =
            candidate.value("candidates_considered", std::int64_t{0});
        report.candidate.selected_candidate =
            read_optional_string(candidate, "selected_candidate");
        report.candidate.selected_source_duration_ms =
            read_optional_i64(candidate, "selected_source_duration_ms");
        report.candidate.source_url = read_optional_string(candidate, "source_url");
    }
    if (value.contains("selected_window") && value.at("selected_window").is_object()) {
        const auto& window = value.at("selected_window");
        report.selected_window.start_ms = read_optional_i64(window, "start_ms");
        report.selected_window.end_ms = read_optional_i64(window, "end_ms");
        report.selected_window.duration_ms = read_optional_i64(window, "duration_ms");
    }
    if (value.contains("asset") && value.at("asset").is_object()) {
        const auto& asset = value.at("asset");
        report.asset.asset_id = read_optional_string(asset, "asset_id");
        report.asset.sha256 = read_optional_string(asset, "sha256");
        report.asset.local_path = read_optional_string(asset, "local_path");
    }
    if (value.contains("drive") && value.at("drive").is_object()) {
        const auto& drive = value.at("drive");
        report.drive.destination = read_optional_string(drive, "destination");
        report.drive.link = read_optional_string(drive, "link");
        report.drive.file_id = read_optional_string(drive, "file_id");
    }
    if (value.contains("cache") && value.at("cache").is_object()) {
        const auto& cache = value.at("cache");
        if (cache.contains("status") && cache.at("status").is_string()) {
            report.cache.status = parse_cache_status(cache.at("status").get<std::string>())
                .value_or(CacheStatus::Unknown);
        }
        report.cache.key = read_optional_string(cache, "key");
        report.cache.discovery_calls = cache.value("discovery_calls", std::int64_t{0});
        report.cache.research_calls = cache.value("research_calls", std::int64_t{0});
        report.cache.provider_calls = cache.value("provider_calls", std::int64_t{0});
        report.cache.download_calls = cache.value("download_calls", std::int64_t{0});
        report.cache.transcode_calls = cache.value("transcode_calls", std::int64_t{0});
        report.cache.drive_upload_calls = cache.value("drive_upload_calls", std::int64_t{0});
    }
    return report;
}

}  // namespace

const char* cache_status_name(CacheStatus status) noexcept {
    switch (status) {
        case CacheStatus::Unknown: return "UNKNOWN";
        case CacheStatus::Hit: return "HIT";
        case CacheStatus::Miss: return "MISS";
        case CacheStatus::Invalidated: return "INVALIDATED";
        case CacheStatus::Rebuilt: return "REBUILT";
    }
    return "UNKNOWN";
}

std::optional<CacheStatus> parse_cache_status(std::string_view value) noexcept {
    if (value == "UNKNOWN") return CacheStatus::Unknown;
    if (value == "HIT") return CacheStatus::Hit;
    if (value == "MISS") return CacheStatus::Miss;
    if (value == "INVALIDATED") return CacheStatus::Invalidated;
    if (value == "REBUILT") return CacheStatus::Rebuilt;
    return std::nullopt;
}

Result<bool, VisualUnitReportError> validate_visual_unit_report(
    const VisualUnitReport& report) {
    if (report.scene_id.empty()) return error("scene_id", "must not be empty");
    if (report.visual_unit_id.empty()) return error("visual_unit_id", "must not be empty");
    if (report.time.start_ms < 0) return error("start_ms", "must be non-negative");
    if (report.time.end_ms <= report.time.start_ms)
        return error("end_ms", "must be greater than start_ms");
    if (report.time.target_duration_ms <= 0)
        return error("target_duration_ms", "must be positive");
    if (report.provider_decision.selected.empty())
        return error("provider_decision.selected", "must not be empty");
    if (report.provider_decision.reason.empty())
        return error("provider_decision.reason", "must not be empty");
    if (report.candidate.candidates_considered < 0)
        return error("candidate.candidates_considered", "must be non-negative");

    const auto actual_duration = report.time.end_ms - report.time.start_ms;
    if (actual_duration != report.time.target_duration_ms)
        return error("target_duration_ms", "must equal end_ms - start_ms");

    const auto validate_window = [&](const SelectedWindowReport& window)
        -> std::optional<VisualUnitReportError> {
        if (!window.start_ms && !window.end_ms && !window.duration_ms) return std::nullopt;
        if (!window.start_ms || !window.end_ms || !window.duration_ms)
            return error("selected_window", "start_ms, end_ms and duration_ms must be set together");
        if (*window.start_ms < 0 || *window.end_ms <= *window.start_ms)
            return error("selected_window", "window timestamps are invalid");
        if (*window.duration_ms != *window.end_ms - *window.start_ms)
            return error("selected_window.duration_ms", "must equal end_ms - start_ms");
        if (*window.duration_ms != report.time.target_duration_ms)
            return error("selected_window.duration_ms", "must equal target_duration_ms");
        return std::nullopt;
    };
    if (const auto window_error = validate_window(report.selected_window)) return *window_error;

    if (report.cache.discovery_calls < 0 || report.cache.research_calls < 0 ||
        report.cache.provider_calls < 0 || report.cache.download_calls < 0 ||
        report.cache.transcode_calls < 0 || report.cache.drive_upload_calls < 0) {
        return error("cache", "operation counters must be non-negative");
    }
    return true;
}

Result<nlohmann::json, VisualUnitReportError> visual_unit_report_to_json(
    const VisualUnitReportDocument& document) {
    if (document.job_id.empty()) return error("job_id", "must not be empty");
    if (document.schema != kVisualUnitReportSchema)
        return error("schema", "unsupported visual-unit report schema");

    nlohmann::json units = nlohmann::json::array();
    for (std::size_t index = 0; index < document.visual_units.size(); ++index) {
        auto valid = validate_visual_unit_report(document.visual_units[index]);
        if (!valid) {
            auto failure = std::move(valid).error();
            failure.path = "visual_units[" + std::to_string(index) + "]." + failure.path;
            return failure;
        }
        units.push_back(report_to_json_unchecked(document.visual_units[index]));
    }
    return nlohmann::json{
        {"schema", document.schema},
        {"job_id", document.job_id},
        {"visual_units", std::move(units)},
    };
}

Result<VisualUnitReportDocument, VisualUnitReportError>
visual_unit_report_from_json(const nlohmann::json& root) {
    if (!root.is_object()) return error("$", "report must be a JSON object");
    if (root.value("schema", std::string{}) != kVisualUnitReportSchema)
        return error("schema", "unsupported or missing visual-unit report schema");
    if (!root.contains("job_id") || !root.at("job_id").is_string())
        return error("job_id", "must be a string");
    if (!root.contains("visual_units") || !root.at("visual_units").is_array())
        return error("visual_units", "must be an array");

    VisualUnitReportDocument document;
    document.schema = root.at("schema").get<std::string>();
    document.job_id = root.at("job_id").get<std::string>();
    for (std::size_t index = 0; index < root.at("visual_units").size(); ++index) {
        const auto& value = root.at("visual_units").at(index);
        if (!value.is_object())
            return error("visual_units[" + std::to_string(index) + "]", "must be an object");
        auto report = report_from_json_unchecked(value);
        auto valid = validate_visual_unit_report(report);
        if (!valid) {
            auto failure = std::move(valid).error();
            failure.path = "visual_units[" + std::to_string(index) + "]." + failure.path;
            return failure;
        }
        document.visual_units.push_back(std::move(report));
    }
    return document;
}

}  // namespace chronon3d::vidrush
