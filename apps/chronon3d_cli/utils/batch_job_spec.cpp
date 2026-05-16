#include "batch_job_spec.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace chronon3d::cli {

namespace {

std::string trim_copy(std::string s) {
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](char c) {
        return !is_space(static_cast<unsigned char>(c));
    }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](char c) {
        return !is_space(static_cast<unsigned char>(c));
    }).base(), s.end());
    return s;
}

std::vector<std::string> split_job_spec(std::string_view spec) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : spec) {
        if (c == '|') {
            parts.push_back(trim_copy(current));
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    parts.push_back(trim_copy(current));
    return parts;
}

bool parse_bool(std::string_view s, bool fallback, std::string* error = nullptr) {
    std::string value;
    value.reserve(s.size());
    for (char c : s) {
        value.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (value.empty()) return fallback;
    if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
    if (value == "0" || value == "false" || value == "no" || value == "off") return false;
    if (error) *error = "invalid boolean value '" + std::string(s) + "'";
    return fallback;
}

} // namespace

std::optional<RenderArgs> parse_batch_job_spec(std::string_view spec, std::string* error) {
    const auto parts = split_job_spec(spec);
    if (parts.empty() || parts[0].empty()) {
        if (error) *error = "missing composition id";
        return std::nullopt;
    }

    RenderArgs args;
    args.comp_id = parts[0];
    if (parts.size() > 1 && !parts[1].empty()) {
        args.frames = parts[1];
    }
    if (parts.size() > 2 && !parts[2].empty()) {
        args.output = parts[2];
    }
    if (parts.size() > 3 && !parts[3].empty()) {
        std::string bool_error;
        args.diagnostic = parse_bool(parts[3], false, &bool_error);
        if (!bool_error.empty()) {
            if (error) *error = bool_error;
            return std::nullopt;
        }
    }
    if (parts.size() > 4 && !parts[4].empty()) {
        std::string bool_error;
        args.use_modular_graph = parse_bool(parts[4], false, &bool_error);
        if (!bool_error.empty()) {
            if (error) *error = bool_error;
            return std::nullopt;
        }
    }
    if (parts.size() > 5) {
        if (error) *error = "expected at most 5 fields: composition|frames|output|diagnostic|graph";
        return std::nullopt;
    }

    if (args.output.empty()) {
        args.output = "render_####.png";
    }

    return args;
}

} // namespace chronon3d::cli
