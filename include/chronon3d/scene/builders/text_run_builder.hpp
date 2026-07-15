#pragma once

#include <chronon3d/scene/builders/pending_text_run.hpp>
#include <chronon3d/scene/builders/text_run_materialization.hpp>
#include <chronon3d/text/glyph_selector_spec.hpp>
#include <chronon3d/text/text_animator_property.hpp>

#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d {

class FontEngine;
class LayerBuilder;
namespace authoring { class Layer; }

// Non-owning fluent handle over one PendingTextRun owned by LayerBuilder.
// Glyph mutators append implicit full-range animator specs; font/layout
// mutators update the canonical TextRunSpec directly. `commit()` validates
// and finalizes the pending spec, while LayerBuilder::build() marks it consumed
// only after successful node materialization.
class TextRunBuilder {
public:
    TextRunBuilder(LayerBuilder* parent, PendingTextRun* pending);

    TextRunBuilder& position(Vec3 value);
    TextRunBuilder& opacity(f32 value);
    TextRunBuilder& anchor(Vec3 value);
    TextRunBuilder& rotate(Vec3 euler_degrees);
    TextRunBuilder& scale(Vec3 value);
    TextRunBuilder& font_size(f32 value);
    TextRunBuilder& blur(f32 radius);
    TextRunBuilder& tracking(f32 pixels);
    TextRunBuilder& baseline_shift(f32 pixels);
    TextRunBuilder& font(std::string path);

    TextRunBuilder& animator(TextAnimatorSpec spec);
    TextRunBuilder& selector(GlyphSelectorSpec spec);

    TextRunBuilder& font_engine(FontEngine* engine);
    TextRunBuilder& cache_layout(bool value);
    TextRunBuilder& name(std::string value);
    TextRunBuilder& from_animated_document(
        std::shared_ptr<const AnimatedTextDocument> document);

    LayerBuilder& commit();

    [[nodiscard]] const TextRunSpec& spec() const noexcept {
        return m_spec->params;
    }
    [[nodiscard]] const PendingTextRun& build_spec() const noexcept {
        return *m_spec;
    }
    [[nodiscard]] LayerBuilder& parent() const noexcept {
        return *m_parent;
    }

private:
    friend class authoring::Layer;

    // Restricted bridge used only by the canonical authoring facade while the
    // pending entry is still unconsumed. It is intentionally not public API.
    [[nodiscard]] PendingTextRun& mutable_pending() noexcept {
        return *m_spec;
    }

    void append_animator(TextAnimatorSpec spec);
    [[nodiscard]] static GlyphSelectorSpec make_global_glyph_selector(
        std::string id);

    LayerBuilder* m_parent;
    PendingTextRun* m_spec;
    FontEngine* m_font_engine{nullptr};
    bool m_cache_layout{true};
    int m_implicit_id_seq{0};
    std::vector<GlyphSelectorSpec> m_pending_selectors;
    std::size_t m_last_explicit_animator_idx{
        std::numeric_limits<std::size_t>::max()};
    bool m_has_explicit_animator{false};
};

} // namespace chronon3d
