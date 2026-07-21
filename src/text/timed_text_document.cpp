#include <chronon3d/text/timed_text_document.hpp>
#include <xxhash.h>

#include "src/text/hash_helpers.hpp"

namespace chronon3d {
namespace {

using text::detail::hcombine;
using text::detail::hbytes;
using text::detail::hstring;
using text::detail::hval;

} // namespace

u64 hash_timed_cue(const TimedCue& cue) {
    u64 h = hstring(cue.speaker);
    h = hcombine(h, hval(cue.start_s));
    h = hcombine(h, hval(cue.end_s));
    h = hcombine(h, hstring(cue.text));
    for (const auto& w : cue.words) {
        h = hcombine(h, hstring(w.text));
        h = hcombine(h, hval(w.start_s));
        h = hcombine(h, hval(w.end_s));
        h = hcombine(h, hstring(w.semantic_id));
    }
    if (cue.style) {
        if (cue.style->color)       h = hcombine(h, hstring(*cue.style->color));
        if (cue.style->background)   h = hcombine(h, hstring(*cue.style->background));
        if (cue.style->font_size)    h = hcombine(h, hval(*cue.style->font_size));
        if (cue.style->font_weight)  h = hcombine(h, hval(*cue.style->font_weight));
    }
    h = hcombine(h, hstring(cue.source_id));
    // TICKET-WORD-TIMING-QUALITY: mix provenance to prevent
    // Estimated↔Authoritative collision on otherwise-identical cue data
    // (sparse JSON `words` array case: source-intended word-level but
    // start/end are 0/0 hashes identically to a malformed Estimated cue).
    h = hcombine(h, hval(static_cast<std::uint32_t>(cue.word_timing_quality)));
    return h;
}

u64 hash_timed_text_document(const TimedTextDocument& doc) {
    u64 h = hstring(doc.language);
    h = hcombine(h, hstring(doc.title));
    h = hcombine(h, hstring(doc.source_format));
    for (const auto& cue : doc.cues) {
        h = hcombine(h, hash_timed_cue(cue));
    }
    return h;
}

} // namespace chronon3d
