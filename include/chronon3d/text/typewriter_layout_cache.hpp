#pragma once

// ── Typewriter Layout Cache ────────────────────────────────────────────────
// Per-runtime/session cache for typewriter layouts, replacing the previous
// process-wide static globals in text_helpers_typewriter.hpp.
// Owned by FontEngine so each runtime has its own isolated cache instance.

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d::content::text {

struct TypewriterCharPos {
    size_t byte_offset;
    size_t byte_len;
    f32 x;
    f32 y;
    f32 advance;
};

struct TypewriterLayout {
    std::vector<TypewriterCharPos> chars;
    f32 total_width{0.0f};
    f32 total_height{0.0f};
};

struct TypewriterLayoutKey {
    std::string text;
    FontIdentity font;
    f32 font_size{0.0f};
    f32 tracking{0.0f};
    Vec2 box{0.0f, 0.0f};
    f32 line_height{0.0f};

    bool operator==(const TypewriterLayoutKey& o) const noexcept {
        return text == o.text &&
               font == o.font &&
               font_size == o.font_size &&
               tracking == o.tracking &&
               box == o.box &&
               line_height == o.line_height;
    }
};

// P0-3 fix(text/cache): remove TypewriterStyle struct entirely.  Style is
// caller-derived (per typewriter_build() opts / FontSpec) and must NEVER be
// cached alongside the geometric layout — otherwise hits reuse old color /
// old font / old line-height from a previous composition.  layout_key KEEP
// the canonical 6 fields above (text/font/font_size/tracking/box/line_height)
// unchanged; layout_entry V dropped presentation fields; they are
// re-applied at emit time from the current call's opts.

struct CompiledTypewriterGlyph {
    size_t original_index{0};
    std::shared_ptr<PlacedGlyphRun> run;     // mini-run precompilated by structure o' shape
    Vec2 placement{0.0f, 0.0f};             // world-relative pixel position (already includes ref_offset)
    // P0-3: layer_name and text_slice INTENTIONALLY removed.  Both are derived
    // at emit time from the current call's opts.text + opts.layer_prefix;
    // caching them caused cross-composition reuse of stale presentation.
    // text_slice = opts.text.substr(layout.chars[original_index].byte_offset,
    //                               layout.chars[original_index].byte_len);
    // layer_name = current_layer_prefix + "_c" + std::to_string(original_index);
};

struct TypewriterLayoutEntry {
    TypewriterLayout layout;
    PlacedGlyphRun placed;
    std::vector<CompiledTypewriterGlyph> glyphs;
};

class TypewriterLayoutCache {
public:
    TypewriterLayoutCache();
    ~TypewriterLayoutCache();

    TypewriterLayoutCache(TypewriterLayoutCache&&) noexcept;
    TypewriterLayoutCache& operator=(TypewriterLayoutCache&&) noexcept;

    // Non-copyable
    TypewriterLayoutCache(const TypewriterLayoutCache&) = delete;
    TypewriterLayoutCache& operator=(const TypewriterLayoutCache&) = delete;

    [[nodiscard]] std::shared_ptr<const TypewriterLayoutEntry> get(const TypewriterLayoutKey& key);
    void put(const TypewriterLayoutKey& key, std::shared_ptr<const TypewriterLayoutEntry> entry);
    void clear();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace chronon3d::content::text

namespace std {
template<>
struct hash<chronon3d::content::text::TypewriterLayoutKey> {
    [[nodiscard]] size_t operator()(const chronon3d::content::text::TypewriterLayoutKey& k) const noexcept {
        size_t h = 0;
        h ^= std::hash<std::string>{}(k.text) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>{}(k.font.font_path) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>{}(k.font.font_family) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.font.font_weight) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>{}(k.font.font_style) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<chronon3d::f32>{}(k.font_size) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<chronon3d::f32>{}(k.tracking) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<chronon3d::f32>{}(k.box.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<chronon3d::f32>{}(k.box.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<chronon3d::f32>{}(k.line_height) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
} // namespace std
