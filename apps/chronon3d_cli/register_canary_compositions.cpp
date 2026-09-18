// ============================================================================
// apps/chronon3d_cli/register_canary_compositions.cpp
//
// The Motion3D canary pack.
//
// These five compositions exist to be *measured*, not admired.  Each one puts
// a different part of the engine under load in the way a real promo does, with
// the pieces stacked so a failure has one place to live:
//
//   Motion3DGlowOrbit          real text + real image + 3D orbit + glow
//   Motion3DSentenceOrbit      a long sentence: metrics, bounds, wrap, clipping
//   Motion3DTextParentCamera   text → Null_B → Null_A, camera under Null_A:
//                              World == ParentWorld × Local, certified by a
//                              chain that can only be right one way
//   Motion3DSameMotionText     \
//   Motion3DSameMotionImage     / the SAME authored transform, twice, once on a
//                              text layer and once on an image layer.  Their
//                              projected geometry must coincide frame by frame:
//                              the engine has no text-only transform semantics.
//
// What they deliberately do NOT do: extrude the glyphs.  Text here is a 2.5D
// plane, so a camera that swings to 90° turns a word into a line — that is
// correct for a plane, and faking depth with stacked copies would produce a
// number that no part of the engine can be held to.  The word stays a plane.
//
// Every composition is a pure function of `ctx.frame()`: the scene is rebuilt
// for the frame being rendered, so the same frame always yields the same scene
// regardless of what was rendered before it.  That is what makes the golden
// hashes in the canary harness meaningful.
// ============================================================================

#include "register_compositions.hpp"

#include <algorithm>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/effects/presets/glow_presets.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <cmath>
#include <string>

namespace chronon3d {

    namespace {

        constexpr i32 kCanaryWidth = 1920;
        constexpr i32 kCanaryHeight = 1080;
        constexpr i32 kCanaryFps = 30;
        constexpr Frame kCanaryDuration{90};

        // The fonts and the picture are logical asset paths resolved against
        // `--assets-root` at render time (the engine never touches the filesystem).
        constexpr const char *kCanaryFontBold = "assets/fonts/Inter-Bold.ttf";
        constexpr const char *kCanaryFontRegular = "assets/fonts/Inter-Regular.ttf";
        constexpr const char *kCanaryImageCard = "assets/images/checker.png";

        constexpr Color kCanaryVoid{0.035f, 0.043f, 0.075f, 1.0f};

        /// Sub-frame progress in [0,1] over the composition's own duration.  Keyed off
        /// the frame the context carries, so the value is a property of the frame and
        /// not of the order the frames were asked for.
        [[nodiscard]] f32 canary_progress(const FrameContext &ctx) {

            const i32 duration = ctx.duration().integral();
            const i32 frame = ctx.frame().integral();
            if (duration <= 1)
                return 0.0f;
            return std::clamp(static_cast<f32>(frame) / static_cast<f32>(duration - 1), 0.0f, 1.0f);
        }

        [[nodiscard]] f32 canary_lerp(f32 a, f32 b, f32 t) {
            return a + (b - a) * t;
        }

        [[nodiscard]] f32 canary_smoothstep(f32 t) {

            const f32 u = std::clamp(t, 0.0f, 1.0f);
            return u * u * (3.0f - 2.0f * u);
        }

        // Canary renders are final-quality visual probes, not preview probes.
        // Keep GlowParams::Fast as the interactive default elsewhere, but
        // make the canary's quality choice explicit at the authoring site.
        [[nodiscard]] GlowParams canary_glow(GlowParams params) {
            params.quality = GlowQuality::High;
            params.falloff = std::max(1.0f, params.falloff);
            return params;
        }

        /// A centred text run in the composition's own font: the one place a text
        /// layer's shape is decided, so a metric change lands in every clip at once.
        [[nodiscard]] TextDefinition canary_text(std::string value, const char *font_path,
                                                 f32 font_size, i32 weight, Color color, Vec2 box,
                                                 i32 max_lines = 1) {

            TextDefinition definition;
            definition.content.value = std::move(value);
            definition.style.font.font_path = font_path;
            definition.style.font.font_weight = weight;
            definition.style.font.font_size = font_size;
            definition.style.color = color;
            definition.frame.size = box;
            definition.frame.placement =
                TextPlacement{TextPlacementKind::CanvasCenter, {0.0f, 0.0f}};
            definition.frame.anchor = TextAnchor::Center;
            definition.frame.align = TextAlign::Center;
            definition.frame.vertical_align = VerticalAlign::Middle;
            definition.frame.centering_mode = TextCenteringMode::PixelInk;
            definition.frame.max_lines = max_lines;
            return definition;
        }

        /// The camera of the hero shots: an orbit around the world origin expressed as
        /// yaw/pitch/distance, so the three numbers in the canary spec are the three
        /// arguments here and nothing is hidden in a preset's easing.
        ///
        /// The pose is written from its spherical definition rather than accumulated
        /// per frame, which is why frame 89 asked for first is bit-identical to frame
        /// 89 asked for last.
        [[nodiscard]] Camera2_5D canary_orbit_camera(f32 yaw_deg, f32 pitch_deg, f32 distance) {

            const f32 yaw = glm::radians(yaw_deg);
            const f32 pitch = glm::radians(pitch_deg);

            Camera2_5D camera;
            camera.enabled = true;
            camera.optics_mode = CameraOpticsMode::Zoom;
            camera.position =
                Vec3{distance * std::sin(yaw) * std::cos(pitch), distance * std::sin(pitch),
                     -distance * std::cos(yaw) * std::cos(pitch)};
            camera.zoom = 1000.0f;
            camera.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
            camera.point_of_interest_enabled = true;
            camera.is_animated = true;
            return camera;
        }

        /// The transform schedule the two same-motion clips share.  One function, both
        /// clips: the only difference between them is the content kind they carry, and
        /// the canary's claim is exactly that the content kind is not an input to this.
        struct CanaryMotionPose {
            Vec3 position{};
            Vec3 rotation_deg{};
            f32 scale{1.0f};
        };

        [[nodiscard]] CanaryMotionPose canary_same_motion_pose(f32 t) {

            CanaryMotionPose pose;
            pose.position = Vec3{canary_lerp(-260.0f, 260.0f, t),
                                 canary_lerp(-90.0f, 90.0f, canary_smoothstep(t)),
                                 canary_lerp(-60.0f, 60.0f, t)};
            pose.rotation_deg = Vec3{canary_lerp(-14.0f, 14.0f, t),
                                     canary_lerp(-32.0f, 32.0f, canary_smoothstep(t)), 0.0f};
            pose.scale = canary_lerp(0.86f, 1.12f, t);
            return pose;
        }

        [[nodiscard]] CompositionSpec canary_spec(const char *name) {

            return CompositionSpec{.name = name,
                                   .width = kCanaryWidth,
                                   .height = kCanaryHeight,
                                   .frame_rate = FrameRate{kCanaryFps, 1},
                                   .duration = kCanaryDuration};
        }

        // ── 1. Hero: real text + real image + 3D orbit + glow ───────────────────────

        Composition make_motion3d_glow_orbit() {

            return composition(
                canary_spec("Motion3DGlowOrbit"), [](const FrameContext &ctx) -> Scene {
                    SceneBuilder scene(ctx);
                    const f32 t = canary_progress(ctx);
                    const i32 frame = ctx.frame().integral();

                    // A screen layer, not a 3D card: a background that is itself a card
                    // reveals the clear colour at the corners as soon as the camera
                    // orbits, which would make the parallax read as a bug.
                    scene.screen_layer("BACKGROUND", [](LayerBuilder &l) { l.fill(kCanaryVoid); });

                    // The word (and everything under it) hangs off one null, exactly as
                    // the spec authors it: the group turns, the camera orbits, and the
                    // two are independent motions that have to compose.
                    const f32 group_yaw = canary_lerp(0.0f, 12.0f, canary_smoothstep(t));
                    scene.null_layer("NULL_MAIN", [&](LayerBuilder &l) {
                        l.enable_3d(true)
                            .position({0.0f, 0.0f, 0.0f})
                            .rotate({0.0f, group_yaw, 0.0f});
                    });

                    // The picture sits deeper than the word, so the orbit parallaxes it
                    // against the text without anyone authoring the parallax.
                    scene.layer("IMAGE_CARD", [](LayerBuilder &l) {
                        l.parent("NULL_MAIN").enable_3d(true).position({0.0f, 0.0f, 240.0f});
                        l.image("image_card", ImageParams{.asset_path = kCanaryImageCard,
                                                          .size = {1040.0f, 600.0f},
                                                          .fit = FitMode::Contain});
                    });

                    // Opacity 0 → 100 % over the first 10 frames, from the frame index
                    // (not from a counter), so refetching frame 4 gives the same 40 %.
                    const f32 word_opacity = canary_smoothstep(static_cast<f32>(frame) / 10.0f);
                    scene.layer("GLOW_TEXT", [&](LayerBuilder &l) {
                        l.parent("NULL_MAIN").enable_3d(true);
                        l.position({0.0f, 80.0f, 0.0f});
                        l.opacity(word_opacity);
                        l.effect(canary_glow(GlowPresets::neon_blue(48.0f)));
                        l.text("glow_word",
                               canary_text("GLOW", kCanaryFontBold, 260.0f, 700,
                                           Color{1.0f, 1.0f, 1.0f, 1.0f}, Vec2{1500.0f, 420.0f}));
                    });

                    scene.layer("SENTENCE", [](LayerBuilder &l) {
                        l.parent("NULL_MAIN").enable_3d(true);
                        l.position({0.0f, -190.0f, 70.0f});
                        l.effect(canary_glow(GlowPresets::soft_cyan(26.0f)));
                        l.text("sentence",
                               canary_text("MOTION THAT FEELS PHYSICAL", kCanaryFontRegular, 64.0f,
                                           400, Color{0.88f, 0.94f, 1.0f, 1.0f},
                                           Vec2{1500.0f, 120.0f}));
                    });

                    // yaw −35° → +35°, pitch 8° → −5°, a very small dolly.
                    scene.camera().set(canary_orbit_camera(canary_lerp(-35.0f, 35.0f, t),
                                                           canary_lerp(8.0f, -5.0f, t),
                                                           canary_lerp(1000.0f, 960.0f, t)));
                    return scene.build();
                });
        }

        // ── 2. A long sentence: the metrics/bounds canary ───────────────────────────

        Composition make_motion3d_sentence_orbit() {

            return composition(
                canary_spec("Motion3DSentenceOrbit"), [](const FrameContext &ctx) -> Scene {
                    SceneBuilder scene(ctx);
                    const f32 t = canary_progress(ctx);

                    scene.screen_layer("BACKGROUND", [](LayerBuilder &l) { l.fill(kCanaryVoid); });

                    // A slow translation of the parent as well as the orbit: a short word
                    // hides wrap and bounds bugs, a two-line sentence does not.
                    const f32 parent_x = canary_lerp(-150.0f, 150.0f, canary_smoothstep(t));
                    const f32 parent_y = canary_lerp(60.0f, -60.0f, t);
                    scene.null_layer("NULL_SENTENCE", [&](LayerBuilder &l) {
                        l.enable_3d(true).position({parent_x, parent_y, 0.0f});
                    });

                    scene.layer("SENTENCE", [](LayerBuilder &l) {
                        l.parent("NULL_SENTENCE").enable_3d(true).position({0.0f, 0.0f, 0.0f});
                        l.effect(canary_glow(GlowPresets::editorial_highlight(30.0f)));
                        l.text("sentence",
                               canary_text("CHRONON MOTION ENGINE\nMOTION THAT FEELS PHYSICAL",
                                           kCanaryFontBold, 92.0f, 700,
                                           Color{1.0f, 0.97f, 0.92f, 1.0f}, Vec2{1680.0f, 320.0f},
                                           2));
                    });

                    scene.camera().set(canary_orbit_camera(canary_lerp(-20.0f, 20.0f, t),
                                                           canary_lerp(3.0f, -3.0f, t), 1040.0f));
                    return scene.build();
                });
        }

        // ── 3. Text → Null_B → Null_A, camera under Null_A ──────────────────────────

        Composition make_motion3d_text_parent_camera() {

            return composition(
                canary_spec("Motion3DTextParentCamera"), [](const FrameContext &ctx) -> Scene {
                    SceneBuilder scene(ctx);
                    const f32 t = canary_progress(ctx);

                    scene.screen_layer("BACKGROUND", [](LayerBuilder &l) { l.fill(kCanaryVoid); });

                    // Null_A is the branch root: it turns about Y and carries the camera.
                    const f32 a_yaw = canary_lerp(-18.0f, 18.0f, canary_smoothstep(t));
                    scene.null_layer("NULL_A", [&](LayerBuilder &l) {
                        l.enable_3d(true).position({0.0f, 0.0f, 0.0f}).rotate({0.0f, a_yaw, 0.0f});
                    });

                    // Null_B scales and rolls inside Null_A, so the word's world matrix can
                    // only be ParentWorld(Null_A) × ParentWorld(Null_B) × Local.
                    const f32 b_scale = canary_lerp(0.78f, 1.18f, canary_smoothstep(t));
                    const f32 b_roll = canary_lerp(-9.0f, 9.0f, t);
                    const f32 b_z = canary_lerp(-70.0f, 110.0f, t);
                    scene.null_layer("NULL_B", [&](LayerBuilder &l) {
                        l.parent("NULL_A").enable_3d(true);
                        l.position(
                            {canary_lerp(-120.0f, 120.0f, t), canary_lerp(50.0f, -50.0f, t), b_z});
                        l.rotate({0.0f, 0.0f, b_roll});
                        l.scale({b_scale, b_scale, b_scale});
                    });

                    // The word has its own motion on top: position and opacity, both keyed
                    // off the frame.
                    const f32 word_opacity =
                        canary_smoothstep(static_cast<f32>(ctx.frame().integral()) / 12.0f);
                    scene.layer("PHRASE", [&](LayerBuilder &l) {
                        l.parent("NULL_B").enable_3d(true);
                        l.position({0.0f, canary_lerp(0.0f, 70.0f, t), 0.0f});
                        l.opacity(word_opacity);
                        l.effect(canary_glow(GlowPresets::cinematic_gold(44.0f)));
                        l.text("phrase",
                               canary_text("WORLD = PARENT x LOCAL", kCanaryFontBold, 116.0f, 700,
                                           Color{1.0f, 0.95f, 0.86f, 1.0f}, Vec2{1600.0f, 240.0f}));
                    });

                    // The camera is a child of Null_A: it inherits the yaw and adds its own
                    // small orbit, which is the only way "the camera is in the hierarchy"
                    // can be told apart from "the camera happens to move similarly".
                    scene.camera().set(canary_orbit_camera(canary_lerp(-12.0f, 12.0f, t),
                                                           canary_lerp(4.0f, -4.0f, t), 1000.0f));
                    scene.camera().parent("NULL_A");
                    return scene.build();
                });
        }

        // ── 4/5. The same transform, once on text, once on an image ─────────────────

        /// The shared scene body of the two same-motion clips.  `as_text` selects only
        /// the content kind; position, rotation, scale, anchor, parent and camera are
        /// the same expressions in both, and the canary harness asserts that the two
        /// projected trajectories coincide frame by frame.
        [[nodiscard]] Composition make_motion3d_same_motion(bool as_text) {

            const char *name = as_text ? "Motion3DSameMotionText" : "Motion3DSameMotionImage";

            return composition(canary_spec(name), [as_text](const FrameContext &ctx) -> Scene {
                SceneBuilder scene(ctx);
                const f32 t = canary_progress(ctx);
                const CanaryMotionPose pose = canary_same_motion_pose(t);

                scene.screen_layer("BACKGROUND", [](LayerBuilder &l) { l.fill(kCanaryVoid); });

                scene.null_layer("NULL_MOTION", [](LayerBuilder &l) {
                    l.enable_3d(true).position({0.0f, 0.0f, 0.0f}).rotate({0.0f, 0.0f, 0.0f});
                });

                const Vec2 box{880.0f, 420.0f};
                scene.layer("SUBJECT", [&](LayerBuilder &l) {
                    l.parent("NULL_MOTION").enable_3d(true);
                    l.position(pose.position);
                    l.rotate(pose.rotation_deg);
                    l.scale({pose.scale, pose.scale, pose.scale});
                    if (as_text) {
                        l.text("subject", canary_text("SUBJECT", kCanaryFontBold, 180.0f, 700,
                                                      Color{1.0f, 1.0f, 1.0f, 1.0f}, box));
                    } else {
                        l.image("subject", ImageParams{.asset_path = kCanaryImageCard,
                                                       .size = box,
                                                       .fit = FitMode::Stretch});
                    }
                });

                scene.camera().set(canary_orbit_camera(canary_lerp(-26.0f, 26.0f, t),
                                                       canary_lerp(6.0f, -6.0f, t), 1010.0f));
                return scene.build();
            });
        }

    } // namespace

    void register_canary_compositions(CompositionRegistry &registry) {

        registry.add(make_composition_descriptor("Motion3DGlowOrbit", make_motion3d_glow_orbit));
        registry.add(
            make_composition_descriptor("Motion3DSentenceOrbit", make_motion3d_sentence_orbit));
        registry.add(make_composition_descriptor("Motion3DTextParentCamera",
                                                 make_motion3d_text_parent_camera));
        registry.add(make_composition_descriptor("Motion3DSameMotionText",
                                                 [] { return make_motion3d_same_motion(true); }));
        registry.add(make_composition_descriptor("Motion3DSameMotionImage",
                                                 [] { return make_motion3d_same_motion(false); }));
    }

} // namespace chronon3d
