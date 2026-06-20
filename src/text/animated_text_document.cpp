// ═══════════════════════════════════════════════════════════════════════════
// animated_text_document.cpp — AnimatedTextDocument implementation
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/animated_text_document.hpp>

#include <algorithm>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// add_keyframe
// ═══════════════════════════════════════════════════════════════════════════

void AnimatedTextDocument::add_keyframe(SourceTextKeyframe kf) {
    // Overwrite duplicate frame.
    auto it = std::lower_bound(m_keyframes.begin(), m_keyframes.end(),
                               kf.frame,
                               [](const SourceTextKeyframe& a, Frame f) {
                                   return a.frame < f;
                               });
    if (it != m_keyframes.end() && it->frame == kf.frame) {
        *it = std::move(kf);
        return;
    }
    m_keyframes.insert(it, std::move(kf));
}

// ═══════════════════════════════════════════════════════════════════════════
// sample_at
// ═══════════════════════════════════════════════════════════════════════════

ActiveTextState AnimatedTextDocument::sample_at(Frame frame) const {
    ActiveTextState result;

    if (m_keyframes.empty()) {
        return result;
    }

    const size_t n = m_keyframes.size();

    // ── Before the first keyframe: hold the first document ────────────
    if (frame < m_keyframes[0].frame) {
        result.active = &m_keyframes[0].document;
        result.transition = SourceTextTransition::Hold;
        result.mix = 0.0f;
        return result;
    }

    // ── Find the keyframe pair: prev (<= frame) and next (> frame) ────
    // Linear scan — keyframe lists are small (typically < 20 entries).
    size_t prev_idx = 0;
    for (size_t i = 0; i < n; ++i) {
        if (m_keyframes[i].frame <= frame) {
            prev_idx = i;
        } else {
            break;
        }
    }

    const SourceTextKeyframe& prev_kf = m_keyframes[prev_idx];
    const bool has_next = (prev_idx + 1 < n);
    const SourceTextKeyframe* next_kf = has_next ? &m_keyframes[prev_idx + 1] : nullptr;

    // ── At or after the last keyframe: hold the last document ──────────
    if (!has_next) {
        result.active = &prev_kf.document;
        result.transition = SourceTextTransition::Hold;
        result.mix = 0.0f;
        return result;
    }        // ── Exactly at the next keyframe: the next document becomes active ──
        if (frame == next_kf->frame) {
            // At the boundary, the incoming keyframe's document takes over.
            result.active = &next_kf->document;
            // At the boundary, no transition is in progress.
            // next_kf->transition describes FROM next_kf TO its successor,
            // which hasn't started yet, so we report Hold.
            result.transition = SourceTextTransition::Hold;
            result.mix = 0.0f;
            result.crossfade_from = nullptr;
            return result;
    }

    // ── Between keyframes ──────────────────────────────────────────────
    // The transition type is determined by the PREVIOUS keyframe
    // (prev_kf.transition) — it governs how we move FROM prev TO next.

    switch (prev_kf.transition) {

    case SourceTextTransition::Hold:
        // Hold: the previous document stays visible until the NEXT keyframe
        // arrives.  No crossfade.
        result.active = &prev_kf.document;
        result.transition = SourceTextTransition::Hold;
        result.mix = 0.0f;
        break;

    case SourceTextTransition::Cut:
        // Cut: instant switch to the next document.
        // (A Cut means the previous keyframe's document is no longer relevant
        //  after its frame — the next document is active immediately.)
        result.active = &next_kf->document;
        result.transition = SourceTextTransition::Cut;
        result.mix = 0.0f;
        break;

    case SourceTextTransition::CrossfadeLayouts: {
        // CrossfadeLayouts: both documents are active during the gap.
        // mix goes from 0 (fully previous) at prev frame to 1 (fully next)
        // at next frame.
        result.active = &next_kf->document;
        result.crossfade_from = &prev_kf.document;
        result.transition = SourceTextTransition::CrossfadeLayouts;

        float gap = static_cast<float>(next_kf->frame - prev_kf.frame);
        float pos  = static_cast<float>(frame - prev_kf.frame);
        if (gap > 0.0f) {
            result.mix = pos / gap;
        } else {
            result.mix = 1.0f;  // zero-duration gap → fully crossed over
        }
        break;
    }

    default:
        result.active = &prev_kf.document;
        result.transition = SourceTextTransition::Hold;
        result.mix = 0.0f;
        break;
    }

    return result;
}

} // namespace chronon3d
