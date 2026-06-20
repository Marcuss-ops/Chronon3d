// ═══════════════════════════════════════════════════════════════════════════
// single_line_composer.cpp — Greedy SingleLine paragraph composer
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/single_line_composer.hpp>
#include "internal/composer_helpers.hpp"

namespace chronon3d {

// Forward-declare the EveryLine composer (defined in every_line_composer.cpp).
// This avoids a circular dependency while allowing dispatch from here.
namespace composer_internal {
    ParagraphLayout compose_every_line_impl(
        const std::vector<ShapedCluster>& clusters,
        float available_width,
        const ParagraphStyle& style,
        std::string_view source_text,
        const PlacedGlyphRun& shaped
    );
} // namespace composer_internal

namespace {

// ── Greedy SingleLine algorithm ──────────────────────────────────────────

[[nodiscard]] std::vector<ComposedLine> break_lines_greedy(
    const std::vector<ShapedCluster>& clusters,
    float available_width
) {
    std::vector<ComposedLine> lines;

    if (clusters.empty()) {
        lines.push_back(ComposedLine{.first_cluster = 0, .cluster_count = 0});
        return lines;
    }

    size_t cursor = 0;
    const size_t n = clusters.size();

    while (cursor < n) {
        size_t line_start = cursor;

        // Leading mandatory break → empty line
        if (clusters[cursor].mandatory_break) {
            lines.push_back(ComposedLine{.first_cluster = cursor, .cluster_count = 0});
            ++cursor;
            continue;
        }

        size_t last_break = line_start;
        float width = 0.0f;
        bool force_break = false;

        while (cursor < n && !force_break) {
            const auto& cl = clusters[cursor];

            if (cl.mandatory_break && cursor > line_start) {
                ++cursor;
                force_break = true;
                break;
            }

            if (!cl.mandatory_break) {
                width += cl.advance;
            }

            if (cl.allowed_break_after) {
                last_break = cursor + 1;
            }

            if (width > available_width && cursor > line_start) {
                break;
            }

            ++cursor;
        }

        size_t line_end;
        if (force_break) {
            line_end = cursor;
        } else if (cursor < n && width > available_width) {
            if (last_break > line_start) {
                line_end = last_break;
                cursor = last_break;
            } else {
                line_end = cursor == line_start ? cursor + 1 : cursor;
                cursor = line_end;
            }
        } else {
            line_end = cursor;
        }

        ComposedLine line;
        line.first_cluster = line_start;
        line.cluster_count = line_end - line_start;
        line.natural_width = composer_internal::line_natural_width(clusters, line_start, line_end);
        lines.push_back(line);
    }

    return lines;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

ParagraphLayout compose_single_line_paragraph(
    const PlacedGlyphRun& shaped,
    float box_width,
    const ParagraphStyle& style,
    std::string_view source_text
) {
    using namespace composer_internal;

    // ── Step 1: Build ShapedClusters ────────────────────────────────────
    auto clusters = build_clusters(shaped, source_text);
    if (clusters.empty()) {
        return ParagraphLayout{};
    }

    // ── Step 2: Compute available width ─────────────────────────────────
    float available_width = box_width - style.left_indent - style.right_indent;
    if (available_width < 1.0f) available_width = 1.0f;

    // ── Step 3: Dispatch to composer algorithm ─────────────────────────
    if (style.composer == ParagraphComposer::EveryLine) {
        return compose_every_line_impl(clusters, available_width, style, source_text, shaped);
    }

    // ── Step 4: Greedy line breaking + finalize ────────────────────────
    ParagraphLayout result;
    result.lines = break_lines_greedy(clusters, available_width);
    finalize_lines(result, clusters, available_width, style, source_text, shaped);
    return result;
}

} // namespace chronon3d
