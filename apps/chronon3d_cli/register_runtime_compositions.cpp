// ============================================================================
// apps/chronon3d_cli/register_runtime_compositions.cpp
//
// TICKET-CLI-ISOLATE-RUNTIME-DEV — production runtime composition surface.
// Always called (production CLI + DEV CLI).
//
// Registers:
//   1. chronon3d::register_builtin_compositions(registry) — DarkGridBackground,
//      CameraImageClip (the canonical "always available" production surfaces).
//
// The ChrononGlowFinalAE / GlowCameraProductV1 families were removed with the
// content-pack externalization cleanup: the content/ compositions tree left
// the core repo and the tests/helpers glow factory was retired with the
// visual/golden test fleet.
// ============================================================================

#include "register_compositions.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/composition/register_builtin_compositions.hpp>
#include <chronon3d/presets/phrase/phrase_presets.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d {

void register_runtime_compositions(CompositionRegistry& registry) {
    // Built-in compositions (DarkGridBackground, CameraImageClip).
    chronon3d::register_builtin_compositions(registry);

    // The phrase presets are production authoring primitives. Register one
    // canonical composition at a time so each visual can be rendered and
    // inspected independently by the CLI without introducing a demo montage.
    registry.add(make_composition_descriptor("PhraseTitlePop", [] {
        return composition(
            CompositionSpec{
                .name = "PhraseTitlePop",
                .width = 1920,
                .height = 1080,
                .frame_rate = FrameRate{30, 1},
                .duration = Frame{90},
            },
            [](const FrameContext& ctx) {
                SceneBuilder scene(ctx);
                presets::phrase::PhraseParams params;
                params.id = "title_pop";
                params.text = "Caffè 👨‍👩‍👧‍👦";
                params.subtitle = "Unicode-safe Typewriter and Motion";
                params.start = Frame{0};
                params.end = Frame{90};
                params.apply_theme(presets::phrase::PhraseTheme::tech());
                presets::phrase::draw_phrase(
                    scene, ctx, presets::phrase::PhrasePreset::TitlePop, params);
                return scene.build();
            });
    }));
}

} // namespace chronon3d
