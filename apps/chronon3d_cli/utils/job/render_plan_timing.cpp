// ============================================================================
// render_plan_timing.cpp — implementation of animation timing extraction
// from a chronon-render-plan.v1 JSON document.
//
// Resolution order (first-layer-wins, per-field):
//   1. For each layer in `plan["layers"]` (in order):
//      - If `layer["animation"]` is an object, prefer its
//        `start_frame` / `duration_frames`. If the field is MISSING in the
//        nested animation block, fall back to the layer top-level field
//        of the same name.
//      - If `layer["animation"]` is absent, read only the layer top-level
//        fields (legacy behaviour before this chore).
//   2. If any layer yielded a non-zero (present) start_frame OR
//      duration_frames, return immediately (first-layer-wins).
//   3. Otherwise return {0, 0} (no timing found).
//
// This implementation is pure data extraction — it does NOT validate the
// plan (schema validation is the JSON Schema validator's job; see
// TICKET-JSON-SCHEMA-VALIDATOR forward-point).
// ============================================================================

#include "render_plan_timing.hpp"

namespace chronon3d::cli {

namespace {

/// Try to read `key` as integer Frame from `obj`. Returns true iff the
/// key is present in `obj` and the value is an integer.
bool read_int_field(const nlohmann::json& obj,
                    const char* key,
                    chronon3d::Frame& out) {
    if (!obj.is_object() || !obj.contains(key)) return false;
    const auto& v = obj.at(key);
    if (!v.is_number_integer()) return false;
    out = chronon3d::Frame{v.get<std::int64_t>()};
    return true;
}

} // namespace

AnimationTiming
extract_animation_timing(const nlohmann::json& plan) {
    AnimationTiming result;

    if (!plan.is_object() || !plan.contains("layers") ||
        !plan.at("layers").is_array()) {
        return result;
    }

    const auto& layers = plan.at("layers");
    for (const auto& layer : layers) {
        if (!layer.is_object()) continue;

        const bool has_anim = layer.contains("animation") &&
                              layer.at("animation").is_object();
        const auto& anim = (has_anim ? layer.at("animation")
                                     : nlohmann::json::object());

        bool have_timing = false;

        // start_frame: nested (preferred) → top-level fallback.
        if (has_anim && read_int_field(anim, "start_frame",
                                       result.start_frame)) {
            have_timing = true;
        } else if (read_int_field(layer, "start_frame",
                                  result.start_frame)) {
            have_timing = true;
        }
        // (else: field stays at default Frame{0})

        // duration_frames: nested (preferred) → top-level fallback.
        if (has_anim && read_int_field(anim, "duration_frames",
                                       result.duration_frames)) {
            have_timing = true;
        } else if (read_int_field(layer, "duration_frames",
                                  result.duration_frames)) {
            have_timing = true;
        }
        // (else: field stays at default Frame{0})

        if (have_timing) {
            return result; // first-layer-wins
        }
    }

    return result;
}

} // namespace chronon3d::cli
