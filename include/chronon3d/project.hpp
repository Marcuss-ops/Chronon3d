#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// chronon3d/project.hpp — Remotion-inspired single-root project registration
//
// The Project class provides a simple, single-point API for registering
// compositions. It wraps the existing CompositionRegistry (not a parallel
// registry) and adds project-level defaults (name, dimensions, fps).
//
// Usage:
//   chronon3d::Project project;
//   project.name = "My Video";
//   project.default_width = 1920;
//   project.default_height = 1080;
//
//   project.composition("TitleCard", {.duration = Frame{150}},
//       [](const FrameContext& ctx) -> Scene {
//           SceneBuilder s(ctx);
//           s.layer("bg", [](LayerBuilder& l) {
//               l.fill(Color{0.1f, 0.1f, 0.15f, 1.0f});
//           });
//           s.layer("title", [](LayerBuilder& l) {
//               l.text("t", TextSpec{
//                   .content = {.value = "Hello"},
//                   .font = {.font_size = 96.0f},
//                   .appearance = {.color = Color::white()},
//               });
//           });
//           return s.build();
//       });
//
//   // CLI integration
//   auto& reg = project.registry();
//   auto comp = reg.create("TitleCard");
//
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_props.hpp>

#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace chronon3d {

/// Per-composition overrides. Missing values fall back to Project defaults.
struct CompositionRegistrationSpec {
    std::optional<i32> width;
    std::optional<i32> height;
    std::optional<i32> fps;
    std::optional<Frame> duration;
};

/**
 * Project — single-root composition registration container.
 *
 * Inspired by Remotion's registerRoot(), this class provides a simple
 * entry point for registering all compositions in a project. It wraps
 * the existing CompositionRegistry to maintain engine compatibility.
 *
 * Project-level defaults (width, height, fps, assets_root) are inherited
 * by compositions that don't specify their own overrides.
 */
class Project {
public:
    Project() = default;

    // ── Project Metadata ─────────────────────────────────────────────
    std::string name{"Untitled Project"};
    i32 default_width{1920};
    i32 default_height{1080};
    i32 default_fps{30};
    std::string assets_root{""};

    // ── Registry Access (for CLI / engine integration) ───────────────
    [[nodiscard]] CompositionRegistry& registry() noexcept { return m_registry; }
    [[nodiscard]] const CompositionRegistry& registry() const noexcept { return m_registry; }

    // ── Composition Registration ─────────────────────────────────────

    /// Register a composition from a scene function (lambda).
    /// The scene function receives a FrameContext and returns a Scene.
    /// This is the primary registration API — matches the existing
    /// composition(spec, render_fn) pattern.
    ///
    /// Example:
    ///   project.composition("TitleCard", {.duration = Frame{150}},
    ///       [](const FrameContext& ctx) -> Scene { ... });
    template <typename SceneFn>
    void composition(std::string comp_name,
                     CompositionRegistrationSpec spec,
                     SceneFn&& scene_fn) {
        auto factory = [this,
                        comp_name_copy = comp_name,
                        spec,
                        fn = std::forward<SceneFn>(scene_fn)]
            (const CompositionProps& /*props*/) -> Composition {
            return build_composition(comp_name_copy, spec, fn);
        };
        m_registry.add(std::move(comp_name), std::move(factory));
    }

    /// Direct low-level registration — bypasses project defaults.
    /// The factory has full control over Composition construction — project
    /// defaults (width, height, fps) are not applied. Use composition()
    /// with a scene lambda for automatic project-default inheritance.
    void add(std::string name, CompositionRegistry::Factory factory) {
        m_registry.add(std::move(name), std::move(factory));
    }

    // ── Convenience Queries ──────────────────────────────────────────

    /// Number of registered compositions.
    [[nodiscard]] std::size_t size() const noexcept {
        return m_registry.available().size();
    }

    /// Check if a composition is registered.
    [[nodiscard]] bool contains(std::string_view name) const {
        return m_registry.contains(name);
    }

    /// List all registered composition names (alphabetical).
    [[nodiscard]] std::vector<std::string> available() const {
        return m_registry.available();
    }

    /// Create a composition by name with default (empty) props.
    [[nodiscard]] Composition create(std::string_view name) const {
        return m_registry.create(name);
    }

    /// Create a composition by name with props.
    [[nodiscard]] Composition create(std::string_view name,
                                     const CompositionProps& props) const {
        return m_registry.create(name, props);
    }

private:
    CompositionRegistry m_registry;

    /// Build a Composition from project defaults + per-composition overrides + scene fn.
    template <typename SceneFn>
    Composition build_composition(const std::string& comp_name,
                                  const CompositionRegistrationSpec& spec,
                                  SceneFn& scene_fn) const {
        const i32 w = spec.width.value_or(default_width);
        const i32 h = spec.height.value_or(default_height);
        const i32 fps_val = spec.fps.value_or(default_fps);
        const Frame dur = spec.duration.value_or(Frame{0});

        CompositionSpec comp_spec{
            .name = comp_name,
            .width = w,
            .height = h,
            .frame_rate = FrameRate{fps_val, 1},
            .duration = dur,
            .assets_root = assets_root,
        };

        return chronon3d::composition(std::move(comp_spec), scene_fn);
    }
};

} // namespace chronon3d
