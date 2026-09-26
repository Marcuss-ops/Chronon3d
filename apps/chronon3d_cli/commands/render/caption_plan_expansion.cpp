#include "caption_plan_expansion.hpp"

#include <chronon3d/text/timed_text_document.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace chronon3d::cli {
namespace {
using json = nlohmann::json;

std::int64_t frame_at(double seconds, std::int64_t fps_num,
                      std::int64_t fps_den, bool round_up) {
    const double exact = seconds * static_cast<double>(fps_num) / fps_den;
    if (!std::isfinite(exact) || exact < 0.0 ||
        exact > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        throw std::runtime_error("caption time is outside the supported frame range");
    return static_cast<std::int64_t>(round_up ? std::ceil(exact) : std::floor(exact));
}

std::string extension_lower(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    if (!extension.empty() && extension.front() == '.') extension.erase(0, 1);
    for (auto& character : extension)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return extension;
}

} // namespace

CaptionPlanExpansion::~CaptionPlanExpansion() {
    if (!plan_path.empty()) {
        std::error_code ignored;
        std::filesystem::remove(plan_path, ignored);
    }
}

CaptionPlanExpansion& CaptionPlanExpansion::operator=(CaptionPlanExpansion&& other) noexcept {
    if (this == &other) return *this;
    if (!plan_path.empty()) {
        std::error_code ignored;
        std::filesystem::remove(plan_path, ignored);
    }
    plan_path = std::move(other.plan_path);
    error = std::move(other.error);
    other.plan_path.clear();
    return *this;
}

CaptionPlanExpansion expand_caption_plan(const std::filesystem::path& plan_path,
                                         const std::filesystem::path& captions_path,
                                         const std::string& preset,
                                         const std::string& font) {
    CaptionPlanExpansion result;
    try {
        if (preset != "minimal_white" && preset != "karaoke" &&
            preset != "word_pop" && preset != "typewriter") {
            result.error = "unsupported_caption_preset";
            return result;
        }
        std::ifstream plan_input(plan_path, std::ios::binary);
        std::ifstream captions_input(captions_path, std::ios::binary);
        if (!plan_input || !captions_input) {
            result.error = "caption_source_open_failed";
            return result;
        }
        auto root = json::parse(plan_input);
        const auto extension = extension_lower(captions_path);
        const std::string raw((std::istreambuf_iterator<char>(captions_input)), {});
        const auto document = timed_text_from_format(raw, extension);
        if (document.cues.empty()) {
            result.error = "caption_document_empty_or_invalid";
            return result;
        }
        const auto fps_num = root.at("canvas").at("fps_num").get<std::int64_t>();
        const auto fps_den = root.at("canvas").at("fps_den").get<std::int64_t>();
        const auto canvas_duration = root.at("canvas").at("duration_frames").get<std::int64_t>();
        if (fps_num <= 0 || fps_den <= 0 || canvas_duration <= 0)
            throw std::runtime_error("caption expansion requires a valid canvas frame rate and duration");

        json style = json::object();
        for (const auto& layer : root.at("layers")) {
            if (layer.value("type", std::string{}) == "text" && layer.contains("style")) {
                style = layer.at("style");
                break;
            }
        }
        if (style.empty()) {
            if (font.empty()) {
                result.error = "caption_style_missing: provide --caption-font or a text layer with style.font";
                return result;
            }
            style = {{"font", font}, {"font_size", 48.0}, {"fill", "#FFFFFF"}};
        }
        bool estimated_word_timing = false;
        for (std::size_t cue_index = 0; cue_index < document.cues.size(); ++cue_index) {
            auto cue = document.cues[cue_index];
            if (!std::isfinite(cue.start_s) || !std::isfinite(cue.end_s) || cue.end_s <= cue.start_s)
                throw std::runtime_error("caption cue has invalid time bounds");
            if (preset != "minimal_white" && cue.words.empty() &&
                (document.source_format == "srt" || document.source_format == "ass")) {
                std::vector<std::pair<std::size_t, std::size_t>> word_ranges;
                for (std::size_t begin = 0; begin < cue.text.size();) {
                    while (begin < cue.text.size() && std::isspace(
                        static_cast<unsigned char>(cue.text[begin]))) ++begin;
                    const auto end = begin;
                    while (begin < cue.text.size() && !std::isspace(
                        static_cast<unsigned char>(cue.text[begin]))) ++begin;
                    if (end < begin) word_ranges.emplace_back(end, begin);
                }
                const auto count = word_ranges.size();
                for (std::size_t word_index = 0; word_index < count; ++word_index) {
                    const auto [byte_start, byte_end] = word_ranges[word_index];
                    const auto duration = cue.end_s - cue.start_s;
                    cue.words.push_back(TimedWord{
                        cue.text.substr(byte_start, byte_end - byte_start),
                        cue.start_s + duration * static_cast<float>(word_index) / static_cast<float>(count),
                        cue.start_s + duration * static_cast<float>(word_index + 1) / static_cast<float>(count),
                        "caption-" + (cue.source_id.empty() ? std::to_string(cue_index) : cue.source_id) +
                            "-word-" + std::to_string(word_index), byte_start, byte_end});
                }
                if (count != 0) cue.word_timing_quality = WordTimingQuality::Estimated;
            }
            const auto start = frame_at(cue.start_s, fps_num, fps_den, false);
            const auto end = frame_at(cue.end_s, fps_num, fps_den, true);
            const auto clipped_start = std::min(start, canvas_duration);
            const auto clipped_end = std::min(end, canvas_duration);
            if (clipped_end <= clipped_start) continue;
            json layer{{"id", "__chronon_caption_" + std::to_string(cue_index)},
                       {"type", "text"}, {"text", cue.text},
                       {"start_frame", clipped_start},
                       {"duration_frames", clipped_end - clipped_start}};
            if (!style.empty()) layer["style"] = style;

            json spans = json::array();
            for (const auto& word : cue.words) {
                if (word.byte_start >= word.byte_end || word.byte_end > cue.text.size()) continue;
                spans.push_back({{"start", word.byte_start}, {"end", word.byte_end},
                                 {"semantic_id", word.semantic_id},
                                 {"style", {{"color", "#FFFFFF"}}}});
            }
            if (!spans.empty()) layer["spans"] = std::move(spans);
            if (preset != "minimal_white" && !cue.words.empty()) {
                estimated_word_timing = estimated_word_timing ||
                    cue.word_timing_quality == WordTimingQuality::Estimated;
                json animators = json::array();
                for (const auto& word : cue.words) {
                    if (word.semantic_id.empty()) continue;
                    const auto word_start = std::clamp(
                        frame_at(word.start_s, fps_num, fps_den, false), clipped_start, clipped_end);
                    const auto word_end = std::clamp(
                        frame_at(word.end_s, fps_num, fps_den, true), word_start, clipped_end);
                    if (word_end <= word_start) continue;
                    json selector{{"semantic_id", word.semantic_id}, {"unit", "word"},
                                  {"shape", "square"},
                                  {"amount", {{"keyframes", json::array({
                                      {{"frame", word_start}, {"value", 0}},
                                      {{"frame", std::max(word_start + 1, word_end)}, {"value", 100}}})}}}};
                    json animator{{"id", "__chronon_caption_motion_" +
                                        std::to_string(cue_index) + "_" + word.semantic_id},
                                  {"selectors", json::array({std::move(selector)})}};
                    json properties = json::array();
                    if (preset == "word_pop") {
                        properties.push_back({{"property", "scale"}, {"keyframes", json::array({
                            {{"frame", word_start}, {"value", 0.8}},
                            {{"frame", std::max(word_start + 1, word_end)}, {"value", 1.0}}})}});
                    } else if (preset == "karaoke") {
                        properties.push_back({{"property", "fill_color"}, {"keyframes", json::array({
                            {{"frame", word_start}, {"value", {1.0, 0.65, 0.0, 1.0}}}})}});
                    } else if (preset == "typewriter") {
                        properties.push_back({{"property", "opacity"}, {"keyframes", json::array({
                            {{"frame", word_start}, {"value", 0.0}},
                            {{"frame", word_end}, {"value", 1.0}}})}});
                    }
                    if (!properties.empty()) animator["properties"] = std::move(properties);
                    animators.push_back(std::move(animator));
                }
                if (!animators.empty()) layer["text_animators"] = std::move(animators);
            }
            root["layers"].push_back(std::move(layer));
        }
        if (estimated_word_timing) {
            if (!root.contains("warnings")) root["warnings"] = json::array();
            root["warnings"].push_back("caption_word_timing_estimated");
        }

        static std::atomic<std::uint64_t> sequence{0};
        result.plan_path = std::filesystem::temp_directory_path() /
            ("chronon-caption-plan-" + std::to_string(sequence.fetch_add(1)) + ".json");
        std::ofstream output(result.plan_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            result.plan_path.clear();
            result.error = "caption_expanded_plan_write_failed";
            return result;
        }
        output << root.dump(2);
        if (!output) {
            result.error = "caption_expanded_plan_write_failed";
            return result;
        }
        return result;
    } catch (const std::exception& error) {
        result.error = std::string{"caption_expansion_failed: "} + error.what();
        return result;
    }
}

} // namespace chronon3d::cli
