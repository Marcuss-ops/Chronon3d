// ═══════════════════════════════════════════════════════════════════════════
// every_line_composer.cpp — Knuth-Plass simplified global paragraph composer
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/every_line_composer.hpp>
#include "typography/composer_policy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace chronon3d {
namespace composer_internal {

constexpr float INF = 1e12f;
constexpr float HYPHEN_PENALTY = 50.0f;
constexpr float CONSECUTIVE_HYPHEN_PENALTY = 100.0f;
constexpr float TIGHT_LINE_PENALTY = 25.0f;
// Penalty for an under-full line that Full justification demands be
// stretched but which has no stretchable glue (kept feasible so the DP
// never fails to lay out non-empty text).
constexpr float kNoStretchPenalty = 500.0f;

struct BreakOpportunity {
    size_t cluster_index{0};
    bool hyphenated{false};
    bool mandatory{false};
    float cumulative_width{0.0f};
};

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

[[nodiscard]] float compute_badness(
    float natural_width,
    float target_width,
    float stretch_cap,
    float shrink_cap,
    TextJustification justification,
    bool is_last_line
) {
    float delta = target_width - natural_width;
    if (std::abs(delta) < 0.01f) return 0.0f;

    float ratio;
    if (delta > 0.0f) {
        // Under-full line.  Full justification stretches every line
        // (including the last — that is the semantic difference vs the
        // FullLastLine* variants, which relax the final line); ragged
        // alignments (Left / Center / Right) never stretch.  Ragged/final
        // lines stay feasible with slack-proportional badness; a justified
        // line that must stretch but has no glue is penalised heavily
        // rather than rejected outright — rejecting it would make the DP
        // return an empty layout for any text that does not exactly fill
        // the box.
        const bool is_full = (justification == TextJustification::Full);
        const bool is_full_last =
            (justification == TextJustification::FullLastLineLeft
             || justification == TextJustification::FullLastLineCenter
             || justification == TextJustification::FullLastLineRight);
        const bool must_stretch = is_full || (is_full_last && !is_last_line);
        if (must_stretch) {
            if (stretch_cap <= 0.0f) {
                // No stretchable glue: keep the candidate feasible with a
                // strong penalty so the DP can still lay the text out.
                ratio = delta / std::max(target_width, 1.0f);
                return 100.0f * std::pow(std::abs(ratio), 3.0f)
                     + kNoStretchPenalty;
            }
            ratio = delta / stretch_cap;
        } else {
            ratio = delta / std::max(target_width, 1.0f);
        }
    } else {
        if (shrink_cap <= 0.0f) return INF;
        ratio = delta / shrink_cap;
    }

    if (ratio < -1.0f) return INF;
    float badness = 100.0f * std::pow(std::abs(ratio), 3.0f);
    if (ratio < -0.5f) badness += TIGHT_LINE_PENALTY;
    return badness;
}

struct DpState {
    float cost{INF};
    int previous_break{-1};
};

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
            for (int i = 0; i < j; ++i) {
                if (dp[i].cost >= INF) continue;

                size_t from = breaks[i].cluster_index;
                size_t to = breaks[j].cluster_index;
                if (from >= to) continue;

                float nat = line_natural_width(clusters, from, to);
                if (nat <= 0.0f) {
                    dp[j].cost = dp[i].cost;
                    dp[j].previous_break = i;
                    break;
                }

                float stretch = stretch_capacity(clusters, from, to, style.spacing);
                float shrink = shrink_capacity(clusters, from, to, style.spacing);
                const bool is_last_line = (to >= clusters.size());
                float badness = compute_badness(
                    nat, available_width, stretch, shrink,
                    style.justification, is_last_line);
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

        for (int i = 0; i < j; ++i) {
            if (dp[i].cost >= INF) continue;
            int nm = next_mand[i];
            if (nm != -1 && nm < j) continue;

            size_t from = breaks[i].cluster_index;
            size_t to = breaks[j].cluster_index;
            if (from >= to) continue;

            float nat = line_natural_width(clusters, from, to);
            float stretch = stretch_capacity(clusters, from, to, style.spacing);
            float shrink = shrink_capacity(clusters, from, to, style.spacing);
            const bool is_last_line = (to >= clusters.size());
            float badness = compute_badness(
                nat, available_width, stretch, shrink,
                style.justification, is_last_line);
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

void enforce_widow_orphan(
    std::vector<int>& path,
    const std::vector<BreakOpportunity>& breaks,
    const ParagraphStyle& style
) {
    if (style.widow_lines <= 0 && style.orphan_lines <= 0) return;
    if (path.size() < 3) return;

    const int last_idx = path.back();
    const int second_idx = path[path.size() - 2];
    const int third_idx = path[path.size() - 3];

    size_t orphan_count = breaks[last_idx].cluster_index - breaks[second_idx].cluster_index;
    size_t widow_count = breaks[second_idx].cluster_index - breaks[third_idx].cluster_index;

    bool fix_needed = false;
    if (style.orphan_lines > 0 && orphan_count > 0 &&
        orphan_count < static_cast<size_t>(style.orphan_lines)) {
        fix_needed = true;
    }
    if (style.widow_lines > 0 && widow_count > 0 &&
        widow_count < static_cast<size_t>(style.widow_lines)) {
        fix_needed = true;
    }

    if (fix_needed) {
        path.erase(path.end() - 2);
        if (style.strict_widow_orphan) {
            constexpr size_t kCascadeCap = 16;
            for (size_t k = 0; k < kCascadeCap; ++k) {
                if (path.size() < 3) break;
                const int new_last_id = static_cast<int>(path.size() - 1);
                const int new_second_id = static_cast<int>(path.size() - 2);
                const int new_third_id = static_cast<int>(path.size() - 3);

                const size_t orphan_count2 =
                    breaks[new_last_id].cluster_index - breaks[new_second_id].cluster_index;
                const size_t widow_count2 =
                    breaks[new_second_id].cluster_index - breaks[new_third_id].cluster_index;

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

    auto path = solve_knuth_plass(breaks, clusters, available_width, style);
    enforce_widow_orphan(path, breaks, style);

    for (size_t p = 0; p + 1 < path.size(); ++p) {
        size_t from = breaks[path[p]].cluster_index;
        size_t to = breaks[path[p + 1]].cluster_index;
        if (from >= to) continue;

        ComposedLine line;
        line.first_cluster = from;
        line.cluster_count = to - from;
        line.natural_width = line_natural_width(clusters, from, to);
        result.lines.push_back(line);
    }

    // Emergency fallback: the DP could not find any feasible break path
    // (e.g. a single unbreakable word wider than the box, or plain runs
    // with no break opportunities that overflow).  Never return an empty
    // layout for non-empty input — split every cluster onto its own line,
    // mirroring the greedy composer's overflow behaviour.
    if (result.lines.empty()) {
        for (size_t ci = 0; ci < clusters.size(); ++ci) {
            ComposedLine line;
            line.first_cluster = ci;
            line.cluster_count = 1;
            line.natural_width = line_natural_width(clusters, ci, ci + 1);
            result.lines.push_back(line);
        }
    }

    finalize_lines(result, clusters, available_width, style, source_text, shaped);
    return result;
}

} // namespace composer_internal
} // namespace chronon3d
