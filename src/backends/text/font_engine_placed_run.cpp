// ═══════════════════════════════════════════════════════════════════════════
// font_engine_placed_run.cpp — PlacedGlyphRun resolver (FASE 14)
// ═══════════════════════════════════════════════════════════════════════════
//
// Extracted from font_engine.cpp.  Does NOT depend on FreeType/HarfBuzz;
// operates purely on the GlyphRun struct output by shape_text().
//
// Converts a raw HarfBuzz GlyphRun into a PlacedGlyphRun with cluster
// mapping and per-glyph source-range metadata.

#include <chronon3d/text/font_engine.hpp>

#include <algorithm>
#include <set>
#include <unordered_map>
#include <vector>

namespace chronon3d {

PlacedGlyphRun resolve_placed_glyph_run(
    const GlyphRun& hb_run,
    float tracking,
    std::string_view source_text
) {
    PlacedGlyphRun result;
    result.ascent = hb_run.ascent;
    result.descent = hb_run.descent;
    result.baseline = hb_run.baseline;
    result.font_size = hb_run.font_size;

    if (hb_run.glyphs.empty()) return result;

    float pen_x = 0.0f;
    float pen_y = 0.0f;
    result.glyphs.reserve(hb_run.glyphs.size());

    for (size_t i = 0; i < hb_run.glyphs.size(); ++i) {
        const auto& g = hb_run.glyphs[i];
        PlacedGlyph pg;
        pg.glyph_id = g.glyph_id;
        pg.x_offset = g.x_offset;
        pg.y_offset = g.y_offset;
        pg.is_cluster_start = g.is_cluster_start;
        pg.cluster = g.cluster;

        pg.x = pen_x + g.x_offset;
        pg.y = pen_y + g.y_offset;

        pg.raw_advance_x = g.advance_x;
        pg.advance_x = g.advance_x;
        pg.advance_y = g.advance_y;

        if (tracking != 0.0f && i + 1 < hb_run.glyphs.size()) {
            if (hb_run.glyphs[i + 1].is_cluster_start) {
                pg.advance_x += tracking;
            }
        }

        result.glyphs.push_back(pg);
        pen_x += pg.advance_x;
        pen_y += g.advance_y;
    }

    result.total_width = pen_x;
    result.total_height = hb_run.ascent + hb_run.descent;

    if (!source_text.empty()) {
        std::set<u32> cluster_set;
        for (const auto& g : hb_run.glyphs) {
            cluster_set.insert(g.cluster);
        }
        cluster_set.insert(static_cast<u32>(source_text.size()));

        std::vector<u32> sorted_clusters(cluster_set.begin(), cluster_set.end());
        std::unordered_map<u32, std::pair<size_t, size_t>> cluster_to_range;
        for (size_t k = 0; k + 1 < sorted_clusters.size(); ++k) {
            cluster_to_range[sorted_clusters[k]] = {
                static_cast<size_t>(sorted_clusters[k]),
                static_cast<size_t>(sorted_clusters[k + 1])
            };
        }

        result.clusters.reserve(sorted_clusters.size() - 1);
        for (size_t k = 0; k + 1 < sorted_clusters.size(); ++k) {
            const u32 c = sorted_clusters[k];
            auto it = cluster_to_range.find(c);
            if (it == cluster_to_range.end()) continue;

            const auto& [start_byte, end_byte] = it->second;
            if (end_byte <= start_byte || start_byte >= source_text.size()) continue;

            PlacedGlyphRun::Cluster cl;
            cl.byte_offset = start_byte;
            cl.byte_len = end_byte - start_byte;
            for (size_t gi = 0; gi < result.glyphs.size(); ++gi) {
                if (result.glyphs[gi].cluster == c) {
                    if (cl.start_glyph == 0 && cl.end_glyph == 0) {
                        cl.start_glyph = gi;
                    }
                    cl.end_glyph = gi + 1;
                    cl.advance += result.glyphs[gi].advance_x;
                    cl.raw_advance += result.glyphs[gi].raw_advance_x;
                }
            }

            result.clusters.push_back(cl);
        }

        // ── Third pass: populate per-glyph source fields ───────────
        for (const auto& cl : result.clusters) {
            for (size_t gi = cl.start_glyph; gi < cl.end_glyph; ++gi) {
                if (gi < result.glyphs.size()) {
                    result.glyphs[gi].byte_offset = cl.byte_offset;
                    result.glyphs[gi].byte_len = cl.byte_len;
                }
            }
        }
    }

    return result;
}

} // namespace chronon3d
