#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/scene/model/core/transition.hpp>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace chronon3d::graph {

class RenderGraphContext;

/// Abstract mask generator used by mask-based transitions (wipe, iris).
/// This keeps the "how do I compute a transition mask" question separate
/// from the "how do I composite the source with that mask" question.
class LayerTransitionMask {
public:
    virtual ~LayerTransitionMask() = default;

    /// Return a coverage value in [0, 1] for the normalised pixel
    /// coordinate (u, v) at the given transition progress.
    /// @param u horizontal coordinate in [0, 1]
    /// @param v vertical coordinate in [0, 1]
    /// @param progress eased transition progress in [0, 1]
    /// @param is_out true for an exit transition, false for an entrance
    [[nodiscard]] virtual float evaluate(float u, float v, float progress, bool is_out) const = 0;
};

/// Executable layer transition.  Each concrete transition implements a
/// single visual effect (crossfade, slide, wipe, iris, flash, ...).
class LayerTransitionProgram {
public:
    virtual ~LayerTransitionProgram() = default;

    /// Apply the transition to `src` and write the result into `out`.
    /// @param src source framebuffer
    /// @param out pre-allocated output framebuffer of the same size
    /// @param progress eased transition progress in [0, 1]
    /// @param direction spatial direction from the descriptor
    /// @param is_out true for exit, false for entrance
    /// @param ctx per-frame render graph context
    virtual void execute(
        const FramebufferRef& src,
        Framebuffer& out,
        float progress,
        TransitionDirection direction,
        bool is_out,
        const RenderGraphContext& ctx
    ) const = 0;
};

/// Catalog of built-in layer transitions.
///
/// Unknown transition ids fail loudly (std::runtime_error).  The catalog
/// is intentionally small and replaceable: tests can register custom
/// programs without touching the core transition node.
class LayerTransitionCatalog {
public:
    using Factory = std::function<std::unique_ptr<LayerTransitionProgram>(const LayerTransitionSpec&)>;

    LayerTransitionCatalog() = default;

    /// Register a transition factory under a canonical id.
    void register_transition(std::string id, Factory factory);

    /// True if the id is known.
    [[nodiscard]] bool contains(std::string_view id) const;

    /// Resolve a spec to an executable program.  Throws on unknown ids.
    [[nodiscard]] std::unique_ptr<LayerTransitionProgram> resolve(const LayerTransitionSpec& spec) const;

    /// Register all built-in layer transitions on the given catalog.
    static void register_builtin(LayerTransitionCatalog& catalog);

private:
    std::map<std::string, Factory, std::less<>> m_factories;
};

} // namespace chronon3d::graph
