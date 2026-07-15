#include "content/showcases/minimalist/minimalist_text_common.hpp"

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_glow_spec.hpp>

#include "content/common/background_helpers.hpp"

#include <utility>

namespace chronon3d::content::minimalist {

using namespace chronon3d::content::backgrounds;

Composition make_minimalist_text(
    const char* name,
    const char* text,
    MinimalistTextSetup setup,
    MinimalistTextOptions options) {
    return composition(
        {.name = name, .width = 1920, .height = 1080, .duration = 150},
        [text, setup = std::move(setup), options](const FrameContext& ctx) {
            SceneBuilder scene(ctx);
            add_common_background(scene, BackgroundStyles::Minimalist());
            scene.layer("phrase", [&](LayerBuilder& layer) {
                layer.pin_to(Anchor::Center);
                setup(layer);
                if (options.glow) {
                    layer.glow(
                        TextGlowPresets::ae_cinematic_white().to_glow_params());
                }
                layer.text("phrase", TextDefinition{
                    .content = {.value = text},
                    .style = {
                        .font = {.font_size = options.font_size}
                    },
                    .frame = {
                        .tracking = options.tracking
                    }
                });
            });
            return scene.build();
        });
}

} // namespace chronon3d::content::minimalist
