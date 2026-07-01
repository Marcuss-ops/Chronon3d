// ═══════════════════════════════════════════════════════════════════════════
// text_document_builder.cpp — Fluent Builder for TextDocument (FASE 4a)
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_document_builder.hpp>

#include <algorithm>  // std::clamp
#include <utility>    // std::move

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// commit_current_span — finalize the span-in-progress
// ═══════════════════════════════════════════════════════════════════════════

void TextDocumentBuilder::commit_current_span() {
    if (!has_current_span_) return;
    if (current_text_.empty()) {
        has_current_span_ = false;
        return;
    }

    const size_t byte_start = doc_.utf8.size();
    doc_.utf8 += current_text_;
    const size_t byte_end = doc_.utf8.size();

    // Only create a TextStyleSpan if at least one override is set.
    const bool has_overrides =
        current_font_.has_value() ||
        current_appearance_.has_value() ||
        current_tracking_.has_value() ||
        current_baseline_shift_.has_value() ||
        current_semantic_id_.has_value() ||
        current_font_size_multiplier_.has_value();

    if (has_overrides) {
        TextStyleSpan span;
        span.byte_start      = byte_start;
        span.byte_end        = byte_end;
        span.font            = std::move(current_font_);
        span.appearance      = std::move(current_appearance_);
        span.tracking        = current_tracking_;
        span.baseline_shift  = current_baseline_shift_;
        span.semantic_id     = std::move(current_semantic_id_);
        span.font_size_multiplier = current_font_size_multiplier_;
        doc_.spans.push_back(std::move(span));
    }

    // Reset accumulated state.
    current_text_.clear();
    current_font_.reset();
    current_appearance_.reset();
    current_tracking_.reset();
    current_baseline_shift_.reset();
    current_semantic_id_.reset();
    current_font_size_multiplier_.reset();
    has_current_span_ = false;
}

// ═══════════════════════════════════════════════════════════════════════════
// defaults
// ═══════════════════════════════════════════════════════════════════════════

TextDocumentBuilder& TextDocumentBuilder::defaults(TextSpec spec) & {
    doc_.defaults = std::move(spec);
    return *this;
}

TextDocumentBuilder&& TextDocumentBuilder::defaults(TextSpec spec) && {
    doc_.defaults = std::move(spec);
    return std::move(*this);
}

// ═══════════════════════════════════════════════════════════════════════════
// span
// ═══════════════════════════════════════════════════════════════════════════

TextDocumentBuilder& TextDocumentBuilder::span(std::string_view text) & {
    commit_current_span();
    current_text_ = std::string{text};
    has_current_span_ = true;
    return *this;
}

TextDocumentBuilder&& TextDocumentBuilder::span(std::string_view text) && {
    commit_current_span();
    current_text_ = std::string{text};
    has_current_span_ = true;
    return std::move(*this);
}

// ═══════════════════════════════════════════════════════════════════════════
// color
// ═══════════════════════════════════════════════════════════════════════════

TextDocumentBuilder& TextDocumentBuilder::color(Color c) & {
    if (!has_current_span_) return *this;
    if (!current_appearance_) current_appearance_.emplace();
    current_appearance_->color = c;
    return *this;
}

TextDocumentBuilder&& TextDocumentBuilder::color(Color c) && {
    if (!has_current_span_) return std::move(*this);
    if (!current_appearance_) current_appearance_.emplace();
    current_appearance_->color = c;
    return std::move(*this);
}

TextDocumentBuilder& TextDocumentBuilder::color(float r, float g, float b, float a) & {
    return color(Color{r, g, b, a});
}

TextDocumentBuilder&& TextDocumentBuilder::color(float r, float g, float b, float a) && {
    return std::move(color(Color{r, g, b, a}));
}

// ═══════════════════════════════════════════════════════════════════════════
// weight
// ═══════════════════════════════════════════════════════════════════════════

TextDocumentBuilder& TextDocumentBuilder::weight(u32 w) & {
    if (!has_current_span_) return *this;
    if (!current_font_) current_font_.emplace();
    current_font_->font_weight = static_cast<int>(std::clamp(w, 100u, 900u));
    return *this;
}

TextDocumentBuilder&& TextDocumentBuilder::weight(u32 w) && {
    if (!has_current_span_) return std::move(*this);
    if (!current_font_) current_font_.emplace();
    current_font_->font_weight = static_cast<int>(std::clamp(w, 100u, 900u));
    return std::move(*this);
}

// ═══════════════════════════════════════════════════════════════════════════
// scale
// ═══════════════════════════════════════════════════════════════════════════

TextDocumentBuilder& TextDocumentBuilder::scale(float s) & {
    if (!has_current_span_ || s <= 0.0f) return *this;
    current_font_size_multiplier_ = s;
    return *this;
}

TextDocumentBuilder&& TextDocumentBuilder::scale(float s) && {
    if (!has_current_span_ || s <= 0.0f) return std::move(*this);
    current_font_size_multiplier_ = s;
    return std::move(*this);
}

// ═══════════════════════════════════════════════════════════════════════════
// tracking
// ═══════════════════════════════════════════════════════════════════════════

TextDocumentBuilder& TextDocumentBuilder::tracking(float pixels) & {
    if (!has_current_span_) return *this;
    current_tracking_ = pixels;
    return *this;
}

TextDocumentBuilder&& TextDocumentBuilder::tracking(float pixels) && {
    if (!has_current_span_) return std::move(*this);
    current_tracking_ = pixels;
    return std::move(*this);
}

// ═══════════════════════════════════════════════════════════════════════════
// semantic_id
// ═══════════════════════════════════════════════════════════════════════════

TextDocumentBuilder& TextDocumentBuilder::semantic_id(std::string id) & {
    if (!has_current_span_) return *this;
    current_semantic_id_ = std::move(id);
    return *this;
}

TextDocumentBuilder&& TextDocumentBuilder::semantic_id(std::string id) && {
    if (!has_current_span_) return std::move(*this);
    current_semantic_id_ = std::move(id);
    return std::move(*this);
}

// ═══════════════════════════════════════════════════════════════════════════
// baseline_shift
// ═══════════════════════════════════════════════════════════════════════════

TextDocumentBuilder& TextDocumentBuilder::baseline_shift(float pixels) & {
    if (!has_current_span_) return *this;
    current_baseline_shift_ = pixels;
    return *this;
}

TextDocumentBuilder&& TextDocumentBuilder::baseline_shift(float pixels) && {
    if (!has_current_span_) return std::move(*this);
    current_baseline_shift_ = pixels;
    return std::move(*this);
}

// ═══════════════════════════════════════════════════════════════════════════
// build / build_copy
// ═══════════════════════════════════════════════════════════════════════════

TextDocument TextDocumentBuilder::build() && {
    commit_current_span();

    // Auto-populate paragraphs if not already set.
    if (doc_.paragraphs.empty() && !doc_.utf8.empty()) {
        doc_.split_paragraphs();
    }

    return std::move(doc_);
}

TextDocument TextDocumentBuilder::build_copy() const {
    // Make a copy and finalize the current span on it.
    TextDocumentBuilder copy = *this;
    return std::move(copy).build();
}

} // namespace chronon3d
