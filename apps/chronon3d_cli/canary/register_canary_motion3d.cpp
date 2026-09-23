// ============================================================================
// apps/chronon3d_cli/canary/register_canary_motion3d.cpp
//
// Family 1/4 of the canary pack: the Motion3D measured scenes.
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

#include "canary_common.hpp"
#include "register_canary.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>

namespace chronon3d {
namespace {

    using namespace canary_detail;

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
                    l.effect(GlowPresets::canary_black(48.0f));
                    l.text("glow_word",
                           canary_text("GLOW", kCanaryFontBold, 260.0f, 700,
                                       Color{1.0f, 1.0f, 1.0f, 1.0f}, Vec2{1500.0f, 420.0f}));
                });

                scene.layer("SENTENCE", [](LayerBuilder &l) {
                    l.parent("NULL_MAIN").enable_3d(true);
                    l.position({0.0f, -190.0f, 70.0f});
                    l.effect(GlowPresets::canary_black(26.0f));
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
                    l.effect(GlowPresets::canary_black(30.0f));
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
                    l.effect(GlowPresets::canary_black(44.0f));
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

void register_canary_motion3d_compositions(CompositionRegistry &registry) {

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
