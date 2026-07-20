#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_span_builder.hpp — Fluent span builder for authoring::Text
//
// Provides a lightweight, chainable API to author rich text spans:
//
//   auto t = lyr.text("");
//   t.span("QUESTA ").color(Color::yellow())
//    .span("PAROLA").color(Color::yellow()).weight(800)
//    .span(" È IMPORTANTE");
//
// Each .span() call appends text to the underlying content and creates a
// TextSpanOverride covering the appended bytes.  The renderer later merges
// spans with the same font so ligatures and contextual shaping are preserved
// across span boundaries.
//
// NOTE: the builder holds a pointer to the span inside the pending run's
// vector.  It is intended for immediate chaining only; do not store a
// TextSpanBuilder across operations that may add more spans.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/math/color.hpp>
#include <chronon3d/text/text_span_override.hpp>

#include <string>
#include <string_view>

namespace chronon3d::authoring {

class Text;

// ── TextSpanBuilder — non-owning handle over one TextSpanOverride ──────────
//
// The builder references the parent authoring::Text and the currently open
// span.  All style setters mutate the span in place and return *this for
// chaining.  Calling .span() on the builder starts a new span on the parent.

class TextSpanBuilder {
public:
    TextSpanBuilder(Text& parent, TextSpanOverride& span) noexcept
        : parent_(&parent), span_(&span) {}

    // ── Span-level style overrides ─────────────────────────────────────

    /// Set the fill color for the current span.
    TextSpanBuilder& color(Color c) {
        span_->color = c;
        return *this;
    }

    /// Convenience: set the fill color from RGBA components [0,1].
    TextSpanBuilder& color(float r, float g, float b, float a = 1.0f) {
        return color(Color{r, g, b, a});
    }

    /// Set the font weight (100–900) for the current span.
    TextSpanBuilder& weight(int w) {
        if (!span_->font) span_->font.emplace();
        span_->font->font_weight = w;
        return *this;
    }

    /// Set the absolute font size for the current span (px).
    TextSpanBuilder& font_size(float size) {
        if (!span_->font) span_->font.emplace();
        span_->font->font_size = size;
        return *this;
    }

    /// Set a relative font-size multiplier for the current span.
    TextSpanBuilder& scale(float multiplier) {
        span_->font_size_multiplier = multiplier;
        return *this;
    }

    /// Set the letter-spacing (tracking) override in pixels.
    TextSpanBuilder& tracking(float pixels) {
        span_->tracking = pixels;
        return *this;
    }

    /// Set the baseline shift for the current span (vertical offset
    /// from the baseline, in pixels).
    TextSpanBuilder& baseline_shift(float pixels) {
        span_->baseline_shift = pixels;
        return *this;
    }

    /// Set a semantic identifier for the current span.
    TextSpanBuilder& semantic_id(std::string id) {
        span_->semantic_id = std::move(id);
        return *this;
    }

    /// Set the font path for the current span.
    TextSpanBuilder& font_path(std::string path) {
        if (!span_->font) span_->font.emplace();
        span_->font->font_path = std::move(path);
        return *this;
    }

    /// Set the font family for the current span.
    TextSpanBuilder& font_family(std::string family) {
        if (!span_->font) span_->font.emplace();
        span_->font->font_family = std::move(family);
        return *this;
    }

    /// Set an italic style for the current span.
    TextSpanBuilder& italic(bool value = true) {
        if (!span_->font) span_->font.emplace();
        span_->font->font_style = value ? "italic" : "normal";
        return *this;
    }

    /// Set a stroke style for the current span.
    TextSpanBuilder& stroke(const TextStrokeStyle& style) {
        span_->stroke = style;
        return *this;
    }

    /// Convenience: set a solid stroke with width (em) and color.
    TextSpanBuilder& stroke(float width_em, Color color) {
        TextStrokeStyle s;
        s.width_em = width_em;
        s.color = color;
        span_->stroke = s;
        return *this;
    }

    /// Start a new span on the parent Text with the given text.
    /// The text is appended to the underlying content and a new
    /// TextSpanOverride covering the appended bytes is created.
    TextSpanBuilder span(std::string_view text);

private:
    Text* parent_{nullptr};
    TextSpanOverride* span_{nullptr};
};

} // namespace chronon3d::authoring
