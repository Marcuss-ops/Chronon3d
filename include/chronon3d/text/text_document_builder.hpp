#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_document_builder.hpp — Fluent Builder pattern for TextDocument
//
// FASE 4a: provides a fluent construction API over TextDocument without
// altering the internal model.  The renderer still sees a single
// TextDocument / TextRun produced by `.build()`.
//
// Usage:
//   auto doc = TextDocumentBuilder()
//       .defaults(myDefaultSpec)
//       .span("Hello")
//           .color(Color::red())
//           .weight(700)
//       .span(" World")
//           .color(Color::blue())
//           .scale(1.5f)
//       .semantic_id("footer")
//       .build();
//
// Each `.span(text)` call starts a new span.  Chained methods (.color(),
// .weight(), .scale(), .semantic_id()) configure the current span's
// overrides.  When the next `.span()` or `.build()` is called, the
// current span is finalized into a TextStyleSpan on the underlying
// TextDocument.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_document.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/types/types.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TextDocumentBuilder — fluent TextDocument constructor
// ═══════════════════════════════════════════════════════════════════════════

class TextDocumentBuilder {
public:
    TextDocumentBuilder() = default;

    // ── Document-level defaults ──────────────────────────────────────

    /// Set the default TextSpec (font, layout, appearance) for the
    /// entire document.  Spans that don't override specific fields
    /// fall back to these defaults.
    TextDocumentBuilder& defaults(TextSpec spec) &;
    TextDocumentBuilder&& defaults(TextSpec spec) &&;

    // ── Span building ────────────────────────────────────────────────

    /// Start a new span with the given UTF-8 text.  If a previous span
    /// was being built, it is finalized first (committed to the document).
    /// Returns *this so you can chain `.color()`, `.weight()`, etc.
    TextDocumentBuilder& span(std::string_view text) &;
    TextDocumentBuilder&& span(std::string_view text) &&;

    // ── Span-level overrides ─────────────────────────────────────────
    //
    // Each method returns *this for chaining.  The override is applied
    // to the current span (started by the last `.span()` call).
    // Calling any of these without a preceding `.span()` is a no-op
    // (the override is silently ignored — no span to attach it to).

    /// Set the fill color for the current span.
    TextDocumentBuilder& color(Color c) &;
    TextDocumentBuilder&& color(Color c) &&;

    /// Convenience: set the fill color from RGBA components [0,1].
    TextDocumentBuilder& color(float r, float g, float b, float a = 1.0f) &;
    TextDocumentBuilder&& color(float r, float g, float b, float a = 1.0f) &&;

    /// Set the font weight (100–900) for the current span.
    TextDocumentBuilder& weight(u32 w) &;
    TextDocumentBuilder&& weight(u32 w) &&;

    /// Set the font-size multiplier for the current span.
    /// scale=1.0 means "use default font size", 1.5 = 150%, etc.
    TextDocumentBuilder& scale(float s) &;
    TextDocumentBuilder&& scale(float s) &&;

    /// Set the letter-spacing (tracking) override in pixels.
    TextDocumentBuilder& tracking(float pixels) &;
    TextDocumentBuilder&& tracking(float pixels) &&;

    /// Set a semantic identifier for the current span (stable id for
    /// analytics, audio sync, subtitle highlights, template reuse).
    TextDocumentBuilder& semantic_id(std::string id) &;
    TextDocumentBuilder&& semantic_id(std::string id) &&;

    /// Set the baseline shift for the current span (vertical offset
    /// from the baseline, in pixels).
    TextDocumentBuilder& baseline_shift(float pixels) &;
    TextDocumentBuilder&& baseline_shift(float pixels) &&;

    // ── Finalize ─────────────────────────────────────────────────────

    /// Finalize the current span (if any) and return the built
    /// TextDocument.  The builder is consumed — further calls to
    /// .span() after .build() on the same builder object have
    /// undefined behavior.
    [[nodiscard]] TextDocument build() &&;

    /// Same as build() but copies the internal state instead of moving.
    /// Useful when you need to inspect the result without consuming
    /// the builder.
    [[nodiscard]] TextDocument build_copy() const;

private:
    /// Finalize the current span-in-progress and commit it to doc_.
    void commit_current_span();

    TextDocument doc_;
    bool has_current_span_{false};

    // ── Current span state (accumulated between .span() and .build()) ─
    std::string current_text_;
    std::optional<FontSpec> current_font_;
    std::optional<TextAppearanceSpec> current_appearance_;
    std::optional<f32> current_tracking_;
    std::optional<f32> current_baseline_shift_;
    std::optional<std::string> current_semantic_id_;
    std::optional<f32> current_font_size_multiplier_;
};

} // namespace chronon3d
