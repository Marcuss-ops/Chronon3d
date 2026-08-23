#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// semantic_script.hpp — the script → RenderPlan semantic bridge.
//
// This is the transformation layer that sits BETWEEN the script analyzer and
// Chronon.  It is deliberately NOT part of the core SDK (include/chronon3d/):
// per the design rule "la semantica decide cosa mostrare; Chronon decide come
// renderizzarlo", this layer only owns the semantic vocabulary and maps it
// onto the canonical RenderPlan primitives (Text / Image / Color).  Chronon
// keeps its single renderer — there is no ImportantWordRenderer, no
// PersonRenderer, no semantic registry inside the engine.
//
// The layer understands exactly three canonical semantic kinds:
//   ImportantPhrase → text: caption_safe_area (title_centered on override)
//   ImportantWord   → text: kinetic_word
//   ImageOverlay    → image: contain
//
// Pipeline:
//   SCRIPT ──(analyzer, future)──▶ overlay events ──▶ RenderPlan JSON ──▶ Chronon
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::cli::semantic {

/// The three canonical semantic overlay types this layer understands.
/// These are the ONLY semantic kinds; the resolver below is the single place
/// where they are translated into Chronon primitives.
enum class SemanticKind : std::uint8_t {
    ImportantPhrase,  ///< "frase importante"  → text: caption_safe_area
    ImportantWord,    ///< "parola importante" → text: kinetic_word
    ImageOverlay,     ///< "immagine importante" → image: contain
};

/// One semantic overlay event — the analyzer's output for a phrase, a word or
/// an image, with its timeline placement already resolved in frames.
struct SemanticOverlay {
    std::string id;
    SemanticKind kind{SemanticKind::ImportantPhrase};
    std::string text;   ///< phrase / word content (empty for images)
    std::string asset;  ///< image asset path (empty for text)
    Frame start_frame{0};
    Frame duration_frames{0};
    std::optional<std::string> preset;  ///< text preset override (e.g. title_centered)
    std::optional<std::string> fit;     ///< image fit override (cover/contain/stretch/none)
    std::optional<float> box_width;
    std::optional<float> box_height;
    std::optional<std::array<float, 2>> position;
    std::optional<render_plan::AnimationTiming> animation;
};

/// Full-canvas backdrop for the rendered plan.  Either an image asset or a
/// solid color; an image wins when `asset` is non-empty.
struct SemanticBackground {
    std::string asset;
    std::array<float, 4> color{0.0f, 0.0f, 0.0f, 1.0f};
    render_plan::FitMode fit{render_plan::FitMode::Cover};
};

/// A semantic script: canvas + optional backdrop + ordered overlay events.
struct SemanticScript {
    std::string job_id{"script_overlay"};
    render_plan::CanvasSpec canvas{1920, 1080, FrameRate{30, 1}, Frame{300}};
    std::optional<SemanticBackground> background;
    std::vector<SemanticOverlay> events;
    render_plan::OutputSpec output;
};

struct SemanticError {
    std::string path;
    std::string message;
};

/// Decode a semantic overlay-events JSON document into a SemanticScript.
[[nodiscard]] Result<SemanticScript, SemanticError> decode_semantic_script(
    const nlohmann::json& root);

/// Resolve a decoded script into a Chronon RenderPlan.  This is the canonical
/// semantic→RenderPlan bridge (the "resolver" of the design):
///   ImportantPhrase → text preset caption_safe_area (or `preset` override)
///   ImportantWord   → text preset kinetic_word (or `preset` override)
///   ImageOverlay    → image fit contain (or `fit` override)
[[nodiscard]] render_plan::RenderPlan compile_semantic_script(
    const SemanticScript& script);

/// Serialize a RenderPlan into a `chronon.render-plan.v1` JSON document that
/// `chronon render --plan` accepts.
[[nodiscard]] nlohmann::json render_plan_to_json(const render_plan::RenderPlan& plan);

}  // namespace chronon3d::cli::semantic
