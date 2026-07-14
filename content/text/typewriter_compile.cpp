// ── Typewriter Compile ───────────────────────────────────────────────────────
// Implementation of advance_cluster_window and compile_typewriter_glyphs.
// Extracted from text_helpers_typewriter.hpp.

#include "text_helpers_typewriter.hpp"
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <chronon3d/text/glyph_atlas.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <memory>
#include <string>
#include <vector>

namespace chronon3d::content::text {
namespace detail {

void advance_cluster_window(
    const std::vector<PlacedGlyphRun::Cluster>& clusters,
    size_t char_start,
    size_t char_end,
    size_t& first_cl,
    size_t& end_cl)
{
    // Advance first_cl past clusters that end before or at char_start.
    while (first_cl < clusters.size() &&
           clusters[first_cl].byte_offset + clusters[first_cl].byte_len <= char_start) {
        ++first_cl;
    }

    // end_cl must not fall behind first_cl when first_cl advanced.
    if (end_cl < first_cl) {
        end_cl = first_cl;
    }

    // Advance end_cl past clusters that start before char_end.
    while (end_cl < clusters.size() && clusters[end_cl].byte_offset < char_end) {
        ++end_cl;
    }
}

std::vector<CompiledTypewriterGlyph> compile_typewriter_glyphs(
    const TypewriterLayout& layout,
    const PlacedGlyphRun& placed,
    const std::string& text)
{
    std::vector<CompiledTypewriterGlyph> result;
    result.reserve(layout.chars.size());

    size_t first_cl = 0;
    size_t end_cl = 0;

    for (size_t i = 0; i < layout.chars.size(); ++i) {
        const auto& cp = layout.chars[i];

        if (cp.byte_len == 1 && text[cp.byte_offset] == ' ') continue;
        if (cp.byte_len == 1 && text[cp.byte_offset] == '\n') continue;

        CompiledTypewriterGlyph glyph;
        glyph.original_index = i;
        // P0-3 fix(text/cache): layer_name and text_slice are derived per-call in typewriter_build;
        // do NOT cache them.
        glyph.placement = {cp.x, cp.y};

        if (!placed.clusters.empty() && !placed.glyphs.empty()) {
            const size_t char_start = cp.byte_offset;
            const size_t char_end = cp.byte_offset + cp.byte_len;

            advance_cluster_window(placed.clusters, char_start, char_end,
                                   first_cl, end_cl);

            if (first_cl < end_cl) {
                const auto& first_cluster = placed.clusters[first_cl];
                const auto& last_cluster = placed.clusters[end_cl - 1];
                const size_t start_glyph = first_cluster.start_glyph;
                const size_t end_glyph = last_cluster.end_glyph;

                if (end_glyph > start_glyph && start_glyph < placed.glyphs.size()) {
                    auto mini_run = std::make_shared<PlacedGlyphRun>();
                    mini_run->ascent = placed.ascent;
                    mini_run->descent = placed.descent;
                    mini_run->baseline = placed.baseline;
                    mini_run->font_size = placed.font_size;

                    const float base_x = placed.glyphs[start_glyph].x;
                    const float base_y = placed.glyphs[start_glyph].y;
                    for (size_t gi = start_glyph; gi < end_glyph; ++gi) {
                        const auto& src = placed.glyphs[gi];
                        PlacedGlyph pg = src;
                        pg.x = src.x - base_x;
                        pg.y = src.y - base_y;
                        mini_run->glyphs.push_back(pg);
                        mini_run->total_width += src.advance_x;
                    }

                    PlacedGlyphRun::Cluster mini_cl;
                    mini_cl.start_glyph = 0;
                    mini_cl.end_glyph = mini_run->glyphs.size();
                    mini_cl.byte_offset = cp.byte_offset;
                    mini_cl.byte_len = cp.byte_len;
                    for (const auto& pg : mini_run->glyphs) {
                        mini_cl.advance += pg.advance_x;
                        mini_cl.raw_advance += pg.raw_advance_x;
                    }
                    mini_run->clusters.push_back(mini_cl);
                    mini_run->total_height = placed.total_height;

                    glyph.run = std::move(mini_run);
                }
            }
        }

        result.push_back(std::move(glyph));
    }

    return result;
}

} // namespace detail
} // namespace chronon3d::content::text
