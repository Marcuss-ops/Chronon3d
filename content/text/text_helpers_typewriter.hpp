#pragma once

// ── Typewriter Text Helpers ────────────────────────────────────────────────
// Declarations only.
// Implementations live in:
//   typewriter_layout.cpp  — compute_typewriter_layout, compute_single_line_glyph_layout
//   typewriter_compile.cpp — advance_cluster_window, compile_typewriter_glyphs
//   typewriter_build.cpp   — typewriter_build, typewriter_text

#include "typewriter_options.hpp"
#include "text_helpers_centered.hpp"
#include <chronon3d/text/typewriter_layout_cache.hpp>

namespace chronon3d { class SceneBuilder; }
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/text/text_error.hpp>

#include <string>
#include <vector>

namespace chronon3d::content::text {

// F0.3 — silent returns replaced by Result<…, TextError>.
[[nodiscard]] Result<TypewriterLayout, TextError> compute_typewriter_layout(
    const std::string& text, f32 font_size, f32 tracking,
    Vec2 box, f32 line_height,
    const FontSpec& font_spec,
    FontEngine& engine,
    PlacedGlyphRun* out_placed = nullptr);

[[nodiscard]] Result<TypewriterLayout, TextError> compute_single_line_glyph_layout(
    const std::string& text,
    f32 font_size,
    f32 tracking,
    const FontSpec& font,
    FontEngine& engine);

// F0.3 — returns Result<bool, TextError> (bool-as-void; Result<void,…>
// is ill-formed in this codebase).  Ok(true) = scene built successfully.
[[nodiscard]] Result<bool, TextError> typewriter_build(
    SceneBuilder& s, std::string_view layer_prefix,
    const TypewriterBuildOptions& opts, Frame frame,
    FontEngine& engine);

/// F2.C — canonical authoring helper.  Returns TextDefinition.
TextDefinition typewriter_text(CenterTextOptions o,
    Frame frame,
    f32 chars_per_frame = 1.5f,
    TypewriterOptions tw = {});

namespace detail {

// Both the character list (layout.chars) and the shaped cluster list
// (cached_placed.clusters) are sorted by byte offset and each forms a
// non-overlapping partition of the source text.  We can therefore find
// the overlapping cluster range for each character with a two-pointer
// scan that only moves forward: O(chars + clusters) instead of
// O(chars * clusters).
void advance_cluster_window(
    const std::vector<PlacedGlyphRun::Cluster>& clusters,
    size_t char_start,
    size_t char_end,
    size_t& first_cl,
    size_t& end_cl);

// Pre-build everything needed to render each visible character so the
// per-frame loop does not allocate strings, shared_ptrs, or mini-runs.
std::vector<CompiledTypewriterGlyph> compile_typewriter_glyphs(
    const TypewriterLayout& layout,
    const PlacedGlyphRun& placed,
    const std::string& text);

} // namespace detail
} // namespace chronon3d::content::text
