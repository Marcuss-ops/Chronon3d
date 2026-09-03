#pragma once

// ── Typewriter Text Helpers (core) ─────────────────────────────────────────
//
// Canonical typewriter layout/build helpers.  Declarations only.
// Implementations live in:
//   src/backends/text/typewriter_layout.cpp   — compute_typewriter_layout,
//                                               compute_single_line_glyph_layout
//   src/backends/text/typewriter_compile.cpp  — advance_cluster_window,
//                                               compile_typewriter_glyphs
//   src/backends/text/typewriter_build.cpp    — typewriter_build, typewriter_text
//
// Provenance: the declarations previously lived in the externalized content
// pack (content/text/text_helpers_typewriter.hpp, removed in 6e6905116).
// The producing types (TypewriterLayout / TypewriterCharPos /
// CompiledTypewriterGlyph) and the FontEngine-owned cache were never
// externalized, so core owned the contract but had lost its producers — the
// exact "half migration" shape the Chronon compatibility rule forbids.  The
// full helper set is therefore restored to core.
//
// Namespace: chronon3d::content::text (kept for source compatibility with
// the historical call sites).

#include <chronon3d/text/typewriter_layout_cache.hpp>
#include <chronon3d/text/typewriter_options.hpp>

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_error.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace chronon3d { class SceneBuilder; }

namespace chronon3d::content::text {

using chronon3d::f32;
using chronon3d::Vec2;
using chronon3d::FontSpec;
using chronon3d::FontEngine;
using chronon3d::Frame;
using chronon3d::TextDefinition;
using chronon3d::Result;
using chronon3d::TextError;
using chronon3d::content::text::TypewriterLayout;
using chronon3d::PlacedGlyphRun;
using chronon3d::content::text::CompiledTypewriterGlyph;

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
TextDefinition typewriter_text(TextDefinition o,
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
