// ── include/chronon3d/scene/builders/detail/layer_builder_inline.inl ───
//
// Phase-3.3 mechanical split.  These OUT-OF-CLASS inline bodies are
// the verbatim contents of LayerBuilder's class-body inline
// definitions, re-homed under `LayerBuilder::method` scope so the
// public header can declare-then-include-this-file.
//
// Out-of-class `inline` definitions are subject to ODR; each member
// definition here carries the explicit `inline` keyword so multiple
// TUs that include the public header do not collide on link errors.
//
// INTENTIONALLY OMITTED:
//   * `pending_text_runs()` -- P1 spec mandated extracting the
//     pointer-returning inspector into a test-only adapter that
//     does NOT leak the builder's raw internal pointers.  See
//     <chronon3d/scene/builders/test/layer_builder_inspection.hpp>
//     for the chronon3d::builders::testing::LayerBuilderInspector
//     free-function replacement.

#pragma once

namespace chronon3d {

    // ── Screen Dimensions setter (PR 4) ────────────────────────────────────
    inline LayerBuilder &LayerBuilder::screen_dimensions(f32 w, f32 h) {
        m_screen_width = w;
        m_screen_height = h;
        m_screen_dimensions_explicit = true;  // PR 4 — flip the flag so Layer(LayerBuilder&) can detect 'was-set'.
        return *this;
    }

    // ── Screen Dimensions readback accessors (PR 4) ────────────────────────
    inline bool LayerBuilder::screen_dimensions_were_set() const noexcept {
        return m_screen_dimensions_explicit;
    }

    inline Vec2 LayerBuilder::screen_dimensions() const noexcept {
        return Vec2{m_screen_width, m_screen_height};
    }

    // ── Name accessor ──────────────────────────────────────────────────────
    /// Read-back accessor for the layer's name (used by error messages
    /// in `chronon3d::authoring::Layer::Layer(LayerBuilder&)` when the
    /// builder has no `screen_dimensions(...)` set).
    ///
    /// Returns a non-owning `std::string_view` over `m_layer.name`
    /// (`std::pmr::string`).  See the public header for the lifetime
    /// contract and the canonical receiver pattern
    /// `std::string{builder.name()}`.
    inline std::string_view LayerBuilder::name() const noexcept {
        return m_layer.name;
    }

    // ── Memory resource accessor ────────────────────────────────────────────
    inline std::pmr::memory_resource *LayerBuilder::resource() const {
        return m_layer.nodes.get_allocator().resource();
    }

    // ── ExtensionContext attachment (PR 3.5) ────────────────────────────────
    inline LayerBuilder &LayerBuilder::extension_context(const ExtensionContext &ctx) noexcept {
        m_extension_context = &ctx;
        return *this;
    }

    inline const ExtensionContext *LayerBuilder::extension_context() const noexcept {
        return m_extension_context;
    }

} // namespace chronon3d
