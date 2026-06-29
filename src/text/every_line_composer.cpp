// ═══════════════════════════════════════════════════════════════════════════
// every_line_composer.cpp — Knuth-Plass simplified global paragraph composer
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/every_line_composer.hpp>
#include "internal/composer_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace chronon3d {
namespace composer_internal {

// ═══════════════════════════════════════════════════════════════════════════
// Constants
// ═══════════════════════════════════════════════════════════════════════════

constexpr float INF = 1e12f;
constexpr float HYPHEN_PENALTY = 50.0f;
constexpr float CONSECUTIVE_HYPHEN_PENALTY = 100.0f;
constexpr float TIGHT_LINE_PENALTY = 25.0f;

// ═══════════════════════════════════════════════════════════════════════════
// BreakOpportunity
// ═══════════════════════════════════════════════════════════════════════════

struct BreakOpportunity {
    size_t cluster_index{0};
    bool hyphenated{false};
    bool mandatory{false};
    float cumulative_width{0.0f};
};

// ═══════════════════════════════════════════════════════════════════════════
// Build break opportunities
// ═══════════════════════════════════════════════════════════════════════════

[[nodiscard]] std::vector<BreakOpportunity> build_break_opportunities(
    const std::vector<ShapedCluster>& clusters
) {
    std::vector<BreakOpportunity> breaks;
    float cum = 0.0f;

    breaks.push_back(BreakOpportunity{0, false, false, 0.0f});

    for (size_t i = 0; i < clusters.size(); ++i) {
        if (!clusters[i].mandatory_break) cum += clusters[i].advance;

        if (clusters[i].allowed_break_after) {
            BreakOpportunity bp;
            bp.cluster_index = i + 1;
            bp.cumulative_width = cum;
            bp.hyphenated = clusters[i].hyphenation_point;
            bp.mandatory = clusters[i].mandatory_break;
            breaks.push_back(bp);
        }
    }
    return breaks;
}

// ═══════════════════════════════════════════════════════════════════════════
// Badness
// ═══════════════════════════════════════════════════════════════════════════

[[nodiscard]] float compute_badness(
    float natural_width,
    float target_width,
    float stretch_cap,
    float shrink_cap
) {
    float delta = target_width - natural_width;
    if (std::abs(delta) < 0.01f) return 0.0f;

    float ratio;
    if (delta > 0.0f) {
        if (stretch_cap <= 0.0f) return INF;
        ratio = delta / stretch_cap;
    } else {
        if (shrink_cap <= 0.0f) return INF;
        ratio = delta / shrink_cap;  // negative
    }

    if (ratio < -1.0f) return INF;
    float badness = 100.0f * std::pow(std::abs(ratio), 3.0f);
    if (ratio < -0.5f) badness += TIGHT_LINE_PENALTY;
    return badness;
}

// ═══════════════════════════════════════════════════════════════════════════
// Knuth-Plass DP — O(M²)
// ═══════════════════════════════════════════════════════════════════════════

struct DpState {
    float cost{INF};
    int previous_break{-1};
};

/// Precompute: for each break index i, what is the index of the next
/// mandatory break (or -1 if none).  This makes the \"skip mandatory?\"
/// check O(1) instead of O(M), keeping total complexity O(M²).
[[nodiscard]] std::vector<int> precompute_next_mandatory(
    const std::vector<BreakOpportunity>& breaks
) {
    const int M = static_cast<int>(breaks.size());
    std::vector<int> next(M, -1);
    int nxt = -1;
    for (int k = M - 1; k >= 0; --k) {
        next[k] = nxt;
        if (breaks[k].mandatory) nxt = k;
    }
    return next;
}

[[nodiscard]] std::vector<int> solve_knuth_plass(
    const std::vector<BreakOpportunity>& breaks,
    const std::vector<ShapedCluster>& clusters,
    float available_width,
    const ParagraphStyle& style
) {
    const int M = static_cast<int>(breaks.size());
    std::vector<DpState> dp(M);
    dp[0].cost = 0.0f;

    auto next_mand = precompute_next_mandatory(breaks);

    for (int j = 1; j < M; ++j) {
        if (breaks[j].mandatory) {
            // Mandatory break: must break here.
            for (int i = 0; i < j; ++i) {
                if (dp[i].cost >= INF) continue;

                size_t from = breaks[i].cluster_index;
                size_t to   = breaks[j].cluster_index;
                if (from >= to) continue;

                float nat = line_natural_width(clusters, from, to);
                if (nat <= 0.0f) {
                    dp[j].cost = dp[i].cost;
                    dp[j].previous_break = i;
                    break;
                }

                float stretch = stretch_capacity(clusters, from, to, style.spacing);
                float shrink  = shrink_capacity(clusters, from, to, style.spacing);
                float badness = compute_badness(nat, available_width, stretch, shrink);
                if (badness >= INF) continue;

                float penalty = 0.0f;
                if (breaks[j].hyphenated) penalty += HYPHEN_PENALTY;
                if (breaks[j].hyphenated && breaks[i].hyphenated)
                    penalty += CONSECUTIVE_HYPHEN_PENALTY;

                float total = dp[i].cost + badness + penalty;
                if (total < dp[j].cost) {
                    dp[j].cost = total;
                    dp[j].previous_break = i;
                }
            }
            continue;
        }

        // Regular break opportunity.
        for (int i = 0; i < j; ++i) {
            if (dp[i].cost >= INF) continue;

            // O(1) mandatory-break check via precomputed array.
            int nm = next_mand[i];
            if (nm != -1 && nm < j) continue;  // mandatory break between i and j

            size_t from = breaks[i].cluster_index;
            size_t to   = breaks[j].cluster_index;
            if (from >= to) continue;

            float nat = line_natural_width(clusters, from, to);
            float stretch = stretch_capacity(clusters, from, to, style.spacing);
            float shrink  = shrink_capacity(clusters, from, to, style.spacing);
            float badness = compute_badness(nat, available_width, stretch, shrink);
            if (badness >= INF) continue;

            float penalty = 0.0f;
            if (breaks[j].hyphenated) penalty += HYPHEN_PENALTY;
            if (breaks[j].hyphenated && breaks[i].hyphenated)
                penalty += CONSECUTIVE_HYPHEN_PENALTY;

            float total = dp[i].cost + badness + penalty;
            if (total < dp[j].cost) {
                dp[j].cost = total;
                dp[j].previous_break = i;
            }
        }
    }

    // ── Reconstruct path ──────────────────────────────────────────────
    std::vector<int> path;
    int idx = M - 1;

    if (dp[idx].cost >= INF) {
        for (int k = M - 1; k >= 0; --k) {
            if (dp[k].cost < INF) { idx = k; break; }
        }
    }

    while (idx >= 0) {
        path.push_back(idx);
        idx = dp[idx].previous_break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// ═══════════════════════════════════════════════════════════════════════════
// Widow/orphan enforcement — post-DP fixup
// ═══════════════════════════════════════════════════════════════════════════
//
// After the DP produces a candidate path, we check the last two lines:
//   - orphan: the last line has fewer than `orphan_lines` clusters.
//   - widow:  the penultimate line has fewer than `widow_lines` clusters.
//
// When either is violated, we merge the last two lines (remove the
// second-to-last break point).  This is a simple heuristic; a full
// Knuth-Plass would fold these penalties into the DP cost function.

void enforce_widow_orphan(
    std::vector<int>& path,
    const std::vector<BreakOpportunity>& breaks,
    const ParagraphStyle& style
) {
    if (style.widow_lines <= 0 && style.orphan_lines <= 0) return;
    if (path.size() < 3) return;  // need at least 2 lines

    const int last_idx   = path.back();
    const int second_idx = path[path.size() - 2];
    const int third_idx  = path[path.size() - 3];

    size_t orphan_count = breaks[last_idx].cluster_index - breaks[second_idx].cluster_index;
    size_t widow_count  = breaks[second_idx].cluster_index - breaks[third_idx].cluster_index;

    // Only flag non-empty lines.  An empty last line (0 clusters) is a
    // trailing newline, not a typographic orphan.
    bool fix_needed = false;
    if (style.orphan_lines > 0 && orphan_count > 0 &&
        orphan_count < static_cast<size_t>(style.orphan_lines)) {
        fix_needed = true;
    }
    if (style.widow_lines > 0 && widow_count > 0 &&
        widow_count < static_cast<size_t>(style.widow_lines)) {
        fix_needed = true;
    }

    // Merge the last two lines by removing the second-to-last break.
    // Runs once by default; under strict_widow_orphan mode (TEXT-PLY-01)
    // the fixup loops up to 16 passes so cascading violations are
    // resolved by repeated merges instead of a single best-effort attempt.
    if (fix_needed) {
        path.erase(path.end() - 2);
        if (style.strict_widow_orphan) {
            constexpr size_t kCascadeCap = 16;
            for (size_t k = 0; k < kCascadeCap; ++k) {
                if (path.size() < 3) break;
                const int new_last_id   = static_cast<int>(path.size() - 1);
                const int new_second_id = static_cast<int>(path.size() - 2);
                const int new_third_id  = static_cast<int>(path.size() - 3);

                const size_t orphan_count2 = breaks[new_last_id].cluster_index - breaks[new_second_id].cluster_index;
                const size_t widow_count2  = breaks[new_second_id].cluster_index - breaks[new_third_id].cluster_index;

                bool still_bad = false;
                if (style.orphan_lines > 0 && orphan_count2 > 0 &&
                    orphan_count2 < static_cast<size_t>(style.orphan_lines)) {
                    still_bad = true;
                }
                if (style.widow_lines > 0 && widow_count2 > 0 &&
                    widow_count2 < static_cast<size_t>(style.widow_lines)) {
                    still_bad = true;
                }
                if (!still_bad) break;
                path.erase(path.end() - 2);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Public entry point
// ═══════════════════════════════════════════════════════════════════════════

ParagraphLayout compose_every_line_impl(
    const std::vector<ShapedCluster>& clusters,
    float available_width,
    const ParagraphStyle& style,
    std::string_view source_text,
    const PlacedGlyphRun& shaped
) {
    ParagraphLayout result;

    if (clusters.empty()) return result;

    auto breaks = build_break_opportunities(clusters);
    if (breaks.size() <= 1) {
        ComposedLine line;
        line.first_cluster = 0;
        line.cluster_count = clusters.size();
        line.natural_width = line_natural_width(clusters, 0, clusters.size());
        result.lines.push_back(line);
        finalize_lines(result, clusters, available_width, style, source_text, shaped);
        return result;
    }

    // ── Solve DP ──────────────────────────────────────────────────────
    auto path = solve_knuth_plass(breaks, clusters, available_width, style);

    // ── Widow/orphan fixup ────────────────────────────────────────────
    enforce_widow_orphan(path, breaks, style);

    // ── Convert path → ComposedLine[] ─────────────────────────────────
    for (size_t p = 0; p + 1 < path.size(); ++p) {
        size_t from = breaks[path[p]].cluster_index;
        size_t to   = breaks[path[p + 1]].cluster_index;
        if (from >= to) continue;

        ComposedLine line;
        line.first_cluster = from;
        line.cluster_count = to - from;
        line.natural_width = line_natural_width(clusters, from, to);
        result.lines.push_back(line);
    }

    finalize_lines(result, clusters, available_width, style, source_text, shaped);
    return result;
}

} // namespace composer_internal
} // namespace chronon3d
