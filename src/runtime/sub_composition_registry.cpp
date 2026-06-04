#include <chronon3d/description/scene_description.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/runtime/timeline_evaluator.hpp>

namespace chronon3d {

CompositionRegistry build_sub_composition_registry(
    const SceneDescription& desc,
    const CompositionRegistry* external)
{
    CompositionRegistry registry;

    // Copy external registrations first (skip names already present from builtins)
    if (external) {
        for (const auto& name : external->available()) {
            if (!registry.contains(name)) {
                registry.add(name, [ext = external, name]() { return ext->create(name); });
            }
        }
    }

    // Register each inline sub-composition as a factory that evaluates the nested scene
    // Skip names that already exist (builtin or external) to avoid Duplicate composition
    for (const auto& sub : desc.sub_compositions) {
        std::string sub_name = sub.name;
        if (registry.contains(sub_name)) continue;
        SceneDescription nested = *sub.scene;  // copy for capture
        i32 w = nested.width;
        i32 h = nested.height;
        FrameRate fr = nested.frame_rate;
        Frame dur = nested.duration;

        registry.add(sub_name, [nested_desc = std::move(nested), sub_name, w, h, fr, dur]() mutable {
            return Composition(
                CompositionSpec{.name = sub_name, .width = w, .height = h, .frame_rate = fr, .duration = dur},
                [nested_desc = std::move(nested_desc)](const FrameContext& ctx) -> Scene {
                    TimelineEvaluator eval;
                    return eval.evaluate(nested_desc, ctx.frame, ctx.resource);
                }
            );
        });
    }

    return registry;
}

} // namespace chronon3d
