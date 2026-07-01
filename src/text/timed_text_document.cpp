#include <chronon3d/text/timed_text_document.hpp>
#include <xxhash.h>

namespace chronon3d {

namespace {

[[nodiscard]] u64 hcombine(u64 seed, u64 value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

[[nodiscard]] u64 hbytes(const void* data, size_t size) {
    return XXH64(data, size, 0);
}

[[nodiscard]] u64 hstring(std::string_view sv) {
    return XXH64(sv.data(), sv.size(), 0);
}

template <typename T>
[[nodiscard]] u64 hval(const T& v) {
    return hbytes(&v, sizeof(v));
}

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
