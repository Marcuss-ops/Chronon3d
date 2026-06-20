// ═══════════════════════════════════════════════════════════════════════════
// text_resolver.cpp — TextDocument resolution (font fallback, bidi, spans)
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_resolver.hpp>

#ifdef CHRONON3D_HAS_FRIBIDI
#include <chronon3d/backends/text/bidi_segmenter.hpp>
#endif

#include <algorithm>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// resolve_fallback_fonts
// ═══════════════════════════════════════════════════════════════════════════

FontSpec resolve_fallback_fonts(
    FontSpec primary,
    FontEngine& engine
) {
    // ── 1. Primary font ──────────────────────────────────────────────
    if (engine.can_load(primary)) {
        return primary;
    }

    // ── 2. Same family, no explicit path ─────────────────────────────
    // Some engines maintain a family→path registry and can locate the
    // font without the exact path.
    if (!primary.font_family.empty()) {
        FontSpec family_only;
        family_only.font_family = primary.font_family;
        family_only.font_weight = primary.font_weight;
        family_only.font_style  = primary.font_style;
        if (engine.can_load(family_only)) {
            return family_only;
        }
    }

    // ── 3. Platform-generic sans-serif fallback (most UI text) ───────
    const char* sans_candidates[] = {
        "DejaVu Sans",
        "Liberation Sans",
        "Arial",
        "Helvetica",
    };
    for (const auto* family : sans_candidates) {
        FontSpec candidate;
        candidate.font_family = family;
        candidate.font_weight = primary.font_weight;
        candidate.font_style  = primary.font_style;
        if (engine.can_load(candidate)) {
            return candidate;
        }
    }

    // ── 4. Platform-generic serif fallback ────────────────────────────
    const char* serif_candidates[] = {
        "DejaVu Serif",
        "Liberation Serif",
        "Times New Roman",
        "Georgia",
    };
    for (const auto* family : serif_candidates) {
        FontSpec candidate;
        candidate.font_family = family;
        candidate.font_weight = primary.font_weight;
        candidate.font_style  = primary.font_style;
        if (engine.can_load(candidate)) {
            return candidate;
        }
    }

    // ── 5. Return primary as-is — caller must handle unloadable font ──
    return primary;
}

// ═══════════════════════════════════════════════════════════════════════════
// Helper: find the effective font + direction for a byte range
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/// Build a FontSpec for a single byte range by applying TextStyleSpan
/// overrides on top of the document defaults.
[[nodiscard]] FontSpec effective_font(
    const TextDocument& doc,
    size_t byte_start
) {
    // Start with document defaults.
    FontSpec font = doc.defaults.font;

    // Apply the LAST TextStyleSpan that covers this byte.
    // (Spans are sorted; the last covering span wins — documented
    //  override semantics from text_document.hpp.)
    for (const auto& span : doc.spans) {
        if (span.byte_start <= byte_start && byte_start < span.byte_end) {
            if (span.font) {
                if (!span.font->font_path.empty())
                    font.font_path = span.font->font_path;
                if (!span.font->font_family.empty())
                    font.font_family = span.font->font_family;
                if (span.font->font_weight != 0)
                    font.font_weight = span.font->font_weight;
                if (!span.font->font_style.empty())
                    font.font_style = span.font->font_style;
                if (span.font->font_size > 0.0f)
                    font.font_size = span.font->font_size;
            }
        }
    }

    return font;
}

/// Determine the explicit or implicit text direction for a paragraph.
[[nodiscard]] TextDirection paragraph_direction(const ParagraphRange& pr) {
    // For now, paragraphs default to Auto (bidi-detect at the run level).
    // When ParagraphStyle gains a direction field, read it here.
    (void)pr;
    return TextDirection::Auto;
}

/// Split a byte range into font-homogeneous sub-ranges.
/// Two positions share a range if the same TextStyleSpan stack applies and
/// the resulting effective font is identical.
struct FontSubRange {
    size_t byte_start;
    size_t byte_end;
    FontSpec font;
};

[[nodiscard]] std::vector<FontSubRange> split_by_font(
    const TextDocument& doc,
    size_t para_start,
    size_t para_end
) {
    std::vector<FontSubRange> ranges;
    if (para_start >= para_end) return ranges;

    // Collect all span boundary points within the paragraph.
    std::vector<size_t> boundaries;
    boundaries.push_back(para_start);
    for (const auto& span : doc.spans) {
        if (span.byte_start > para_start && span.byte_start < para_end) {
            boundaries.push_back(span.byte_start);
        }
        if (span.byte_end > para_start && span.byte_end < para_end) {
            boundaries.push_back(span.byte_end);
        }
    }
    boundaries.push_back(para_end);

    // Sort + deduplicate.
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(
        std::unique(boundaries.begin(), boundaries.end()),
        boundaries.end());

    // Build font-homogeneous ranges.
    for (size_t i = 0; i + 1 < boundaries.size(); ++i) {
        size_t start = boundaries[i];
        size_t end   = boundaries[i + 1];
        if (start >= end) continue;

        // Any byte within [start, end) produces the same effective font.
        FontSpec font = effective_font(doc, start);
        ranges.push_back({start, end, font});
    }

    return ranges;
}

/// Segment a byte-homogeneous font range through the bidi segmenter,
/// resolve font fallback, and emit ResolvedTextRun entries.
void emit_runs_for_range(
    std::vector<ResolvedTextRun>& out,
    const TextDocument& doc,
    FontEngine& engine,
    const FontSubRange& sub,
    const ParagraphRange& para,
    TextDirection override_dir
) {
#ifdef CHRONON3D_HAS_FRIBIDI
    // ── Bidi segmentation ──────────────────────────────────────────
    std::string_view text = std::string_view(doc.utf8).substr(
        sub.byte_start, sub.byte_end - sub.byte_start);

    auto bidi_runs = segment_bidi_runs(text);
    if (bidi_runs.empty()) {
        // Fallback: single LTR run (happens without FriBidi)
        ResolvedTextRun run;
        run.text        = std::string(text);
        run.font        = resolve_fallback_fonts(sub.font, engine);
        run.direction   = (override_dir != TextDirection::Auto)
                            ? override_dir : TextDirection::LTR;
        run.byte_offset = sub.byte_start;
        run.byte_len    = text.size();
        run.paragraph_style = para.style;
        out.push_back(std::move(run));
        return;
    }

    for (const auto& br : bidi_runs) {
        ResolvedTextRun run;
        run.text = br.text;
        run.font = resolve_fallback_fonts(sub.font, engine);
        // When a paragraph has an explicit direction override, use it.
        // Otherwise use the bidi-resolved direction.
        run.direction = (override_dir != TextDirection::Auto)
                            ? override_dir : br.direction;
        run.byte_offset = sub.byte_start + br.byte_offset;
        run.byte_len    = br.text.size();
        run.paragraph_style = para.style;
        out.push_back(std::move(run));
    }
#else
    // ── No bidi library: emit as a single run ───────────────────────
    std::string_view text = std::string_view(doc.utf8).substr(
        sub.byte_start, sub.byte_end - sub.byte_start);

    ResolvedTextRun run;
    run.text        = std::string(text);
    run.font        = resolve_fallback_fonts(sub.font, engine);
    run.direction   = (override_dir != TextDirection::Auto)
                        ? override_dir : TextDirection::LTR;
    run.byte_offset = sub.byte_start;
    run.byte_len    = text.size();
    run.paragraph_style = para.style;
    out.push_back(std::move(run));
#endif
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// resolve_text_run_tree
// ═══════════════════════════════════════════════════════════════════════════

ResolvedTextTree resolve_text_run_tree(
    const TextDocument& doc,
    FontEngine& engine
) {
    ResolvedTextTree tree;

    if (doc.utf8.empty() && doc.paragraphs.empty()) {
        return tree;
    }

    // ── Ensure paragraphs are populated ──────────────────────────────
    // We need a mutable copy only to call split_paragraphs if needed.
    // For const-correctness, we work with the document's existing
    // paragraphs if already populated; otherwise we split a temp copy.
    const std::vector<ParagraphRange>* paragraphs = &doc.paragraphs;

    if (paragraphs->empty()) {
        // The document hasn't been split yet.  We can't modify a const
        // reference, so the caller should have called split_paragraphs()
        // before passing to resolve_text_run_tree().  Return empty.
        return tree;
    }

    // ── Process each paragraph ────────────────────────────────────────
    for (const auto& para : *paragraphs) {
        ResolvedParagraph resolved_para;
        resolved_para.style = para.style;

        // Determine paragraph-level direction override.
        TextDirection para_dir = paragraph_direction(para);

        // Split paragraph byte range by font changes.
        auto sub_ranges = split_by_font(doc, para.byte_start, para.byte_end);

        // For each font-homogeneous sub-range, run bidi and emit runs.
        for (const auto& sub : sub_ranges) {
            emit_runs_for_range(
                resolved_para.runs,
                doc, engine, sub, para, para_dir);
        }

        // A paragraph may have no runs (empty paragraph — e.g. two
        // consecutive \n).  Keep it as an empty paragraph in the tree
        // to preserve paragraph count for layout.
        tree.paragraphs.push_back(std::move(resolved_para));
    }

    return tree;
}

} // namespace chronon3d
