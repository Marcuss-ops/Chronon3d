// ---------------------------------------------------------------------------
// tests/fuzz/composition_descriptor_fuzz.cpp — libFuzzer target for the
// composition-descriptor JSON surface (CompositionSession::create_composition)
//
// The daemon receives `descriptor_json` as an untrusted string and parses it
// with nlohmann::json, then extracts typed fields (id, category, width,
// height, duration).  This target mirrors that exact access pattern so the
// JSON parse + typed extraction surface is fuzzed without pulling the full
// render engine into the harness.
//
// The real extraction lives in src/ipc/composition_session.cpp
// (CompositionSession::create_composition); any change to the field set or
// access pattern there MUST be reflected here.  Both nlohmann::json::parse and
// .get<T>() throw on malformed/type-mismatched input, so the harness catches
// those exceptions — the fuzzer's job is to find memory-safety issues (UB,
// out-of-bounds) beyond normal parse/type errors.
// ---------------------------------------------------------------------------

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace {

// Mirrors CompositionSession::create_composition field extraction (see header
// comment).  Frame{...} wrapping is a trivial i64 pass-through, so the raw
// i64 is extracted here — the crash surface is the JSON access itself.
void exercise_descriptor_json(std::string_view json) {
    auto doc = nlohmann::json::parse(json);

    const std::string id       = doc.value("id", std::string{});
    const std::string category = doc.value("category", "");

    const std::optional<int32_t> width =
        doc.contains("width") ? std::optional<int32_t>(doc["width"].get<int32_t>())
                              : std::nullopt;
    const std::optional<int32_t> height =
        doc.contains("height") ? std::optional<int32_t>(doc["height"].get<int32_t>())
                               : std::nullopt;
    const std::optional<int64_t> duration =
        doc.contains("duration")
            ? std::optional<int64_t>(doc["duration"].get<int64_t>())
            : std::nullopt;

    // Also exercise the `parameters` / nested-object shapes the daemon may
    // forward later, to keep the fuzzer's exploration broad.
    if (doc.contains("parameters")) {
        (void)doc["parameters"].is_object();
    }
    (void)id;
    (void)category;
    (void)width;
    (void)height;
    (void)duration;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::string_view json(reinterpret_cast<const char*>(data), size);

    try {
        exercise_descriptor_json(json);
    } catch (const nlohmann::json::exception&) {
        // Expected: malformed JSON or type mismatch → the daemon must reject
        // these without UB.  Normal control-flow error, not a crash.
    }

    return 0;
}