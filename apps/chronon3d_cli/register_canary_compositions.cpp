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
#include <array>
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
        constexpr Frame kBlackGlowDuration{150};
        constexpr Frame kTypewriterRevealEnd{54}; // 1.8 s at 30 fps; then hold.

        // The fonts and the picture are logical asset paths resolved against
        // `--assets-root` at render time (the engine never touches the filesystem).
        constexpr const char *kCanaryFontBold = "tests/fixtures/Inter-Bold.ttf";
        // The fixture checkout contains the canonical Inter Bold face; Chronon3D
        // still owns font loading/shaping/rasterization for both authored roles.
        constexpr const char *kCanaryFontRegular = "tests/fixtures/Inter-Bold.ttf";
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

        /// Full-text typewriter definition.  Chronon3D shapes the complete source
        /// string first; the animator only changes opacity on Chronon's stable
        /// grapheme elements.  No substring, per-letter advance or second text
        /// layout is created by the composition.
        [[nodiscard]] TextDefinition canary_typewriter_text(
            std::string value, const char *font_path, f32 font_size,
            Color color, Vec2 box) {

            auto definition = canary_text(
                std::move(value), font_path, font_size, 700, color, box);

            GlyphSelectorSpec hidden_clusters;
            hidden_clusters.id = "typewriter_hidden_clusters";
            hidden_clusters.unit = TextSelectorUnit::Grapheme;
            hidden_clusters.shape = TextSelectorShape::Square;
            hidden_clusters.order = TextSelectorOrder::Forward;
            hidden_clusters.start = 0.0f;
            hidden_clusters.start.add_keyframe(Frame{0}, 0.0f);
            hidden_clusters.start.add_keyframe(
                kTypewriterRevealEnd, 100.0f, EasingCurve{Easing::OutCubic});
            hidden_clusters.end = 100.0f;
            hidden_clusters.exclude_spaces = true;

            TextAnimatorSpec reveal;
            reveal.id = "typewriter_reveal_full_text";
            reveal.selectors.push_back(std::move(hidden_clusters));
            reveal.properties.push_back(OpacityProperty{0.0f});
            reveal.transform_mode = TextPropertyBlendMode::Multiply;

            definition.animation.animators.push_back(std::move(reveal));
            definition.animation.duration = kTypewriterRevealEnd;
            definition.animation.cache_layout = true;
            return definition;
        }

        enum class ElementCanaryMode {
            Static,
            Opacity,
            Motion,
        };

        // A canary for the Chronon text-element contract.  The text remains
        // one complete TextDefinition and therefore one shaped/layout-owned
        // node.  "Elements" here are Chronon's internal glyph states: this
        // helper never creates a second text run and never reconstructs
        // advances in the composition layer.
        [[nodiscard]] TextDefinition canary_element_text(
            std::string value, ElementCanaryMode mode) {
            auto definition = canary_text(
                std::move(value), kCanaryFontBold, 180.0f, 700,
                Color{1.0f, 0.86f, 0.86f, 1.0f}, Vec2{1600.0f, 360.0f});

            GlyphSelectorSpec selector;
            selector.id = "chronon_elements";
            selector.unit = TextSelectorUnit::Grapheme;
            selector.shape = TextSelectorShape::Square;
            selector.order = TextSelectorOrder::Forward;
            selector.start = 0.0f;
            selector.end = 100.0f;
            selector.exclude_spaces = true;

            TextAnimatorSpec animator;
            animator.id = "chronon_elements_static_or_reveal";
            animator.selectors.push_back(selector);

            if (mode == ElementCanaryMode::Static) {
                // B: selecting every shaped element with an identity visual
                // property must be pixel-equivalent to the plain full run.
                animator.properties.push_back(OpacityProperty{1.0f});
            } else {
                // C/D: hide the still-unrevealed shaped elements.  The
                // selector boundary is animated; no glyph position is
                // invented here.
                selector.start = 0.0f;
                selector.start.add_keyframe(Frame{0}, 0.0f,
                                            EasingCurve{Easing::Linear});
                selector.start.add_keyframe(Frame{42}, 100.0f,
                                            EasingCurve{Easing::OutCubic});
                animator.selectors.clear();
                animator.selectors.push_back(std::move(selector));
                animator.properties.push_back(OpacityProperty{0.0f});
                animator.transform_mode = TextPropertyBlendMode::Multiply;
            }

            definition.animation.animators.push_back(std::move(animator));

            if (mode == ElementCanaryMode::Motion) {
                // D: the same shaped elements now receive only motion deltas.
                // The base positions/advances still come from Chronon's
                // layout.  Layer-level 3D rotation remains the camera-facing
                // rotation; per-element Z/scale are evaluated here.
                GlyphSelectorSpec motion_selector;
                motion_selector.id = "chronon_elements_motion";
                motion_selector.unit = TextSelectorUnit::Grapheme;
                motion_selector.shape = TextSelectorShape::Square;
                motion_selector.start = 0.0f;
                motion_selector.end = 100.0f;

                TextAnimatorSpec motion;
                motion.id = "chronon_elements_motion_delta";
                motion.selectors.push_back(std::move(motion_selector));

                PositionProperty position;
                position.value.add_keyframe(Frame{0}, Vec3{0.0f, -24.0f, -20.0f},
                                            EasingCurve{Easing::OutCubic});
                position.value.add_keyframe(Frame{42}, Vec3{0.0f, 0.0f, 0.0f},
                                            EasingCurve{Easing::OutCubic});
                motion.properties.push_back(std::move(position));

                ScaleProperty scale;
                scale.value.add_keyframe(Frame{0}, Vec3{0.82f, 0.82f, 1.0f},
                                         EasingCurve{Easing::OutCubic});
                scale.value.add_keyframe(Frame{42}, Vec3{1.0f, 1.0f, 1.0f},
                                         EasingCurve{Easing::OutCubic});
                motion.properties.push_back(std::move(scale));

                definition.animation.animators.push_back(std::move(motion));
            }

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

        [[nodiscard]] CompositionSpec black_glow_spec(const char *name) {

            return CompositionSpec{.name = name,
                                   .width = kCanaryWidth,
                                   .height = kCanaryHeight,
                                   .frame_rate = FrameRate{kCanaryFps, 1},
                                   .duration = kBlackGlowDuration};
        }

        /// Five intentionally boring baselines for font selection.  There is no
        /// camera motion, no per-glyph animation and no duplicated text node here:
        /// Chronon3D shapes and rasterizes one complete word, centred on a black
        /// screen, with only a restrained white halo.
        [[nodiscard]] Composition make_black_glow_static(const char *name,
                                                         const char *font_path,
                                                         f32 font_size) {

            return composition(
                black_glow_spec(name), [font_path, font_size](const FrameContext &ctx) -> Scene {
                    SceneBuilder scene(ctx);
                    scene.screen_layer("BACKGROUND", [](LayerBuilder &l) {
                        l.fill(Color{0.0f, 0.0f, 0.0f, 1.0f});
                    });

                    scene.layer("CHRONON_TEXT", [&](LayerBuilder &l) {
                        l.text("text", canary_text(
                            "CHRONON", font_path, font_size, 700,
                            Color{1.0f, 1.0f, 1.0f, 1.0f}, Vec2{1700.0f, 360.0f}));
                        // The black-backdrop tuning lives in the preset, so
                        // the authoring site names the look instead of
                        // restating five numbers that must stay in sync
                        // across every black-glow clip.
                        l.effect(GlowPresets::canary_black());
                    });

                    scene.camera().set(canary_orbit_camera(0.0f, 0.0f, 1000.0f));
                    return scene.build();
                });
        }

        // Apple-like V3 motion is deliberately a text-animation concern only:
        // the scene, font size, centering, black background and glow below are
        // the same contract as make_black_glow_static().  This keeps the font
        // fleet useful as a visual baseline while the glyph/word choreography
        // can evolve independently.
        enum class AppleV3Motion {
            PremiumSoftReveal,
            PrecisionWordStagger,
            MaskedVerticalLift,
            HeroScaleFocus,
            KineticKeywordLock,
        };

        [[nodiscard]] GlyphSelectorSpec apple_v3_selector(
            const char *id, TextSelectorUnit unit, TextSelectorOrder order) {
            GlyphSelectorSpec selector;
            selector.id = id;
            selector.unit = unit;
            selector.shape = TextSelectorShape::Square;
            selector.order = order;
            selector.start = 0.0f;
            selector.start.add_keyframe(Frame{0}, 0.0f,
                                        EasingCurve{Easing::Linear});
            selector.start.add_keyframe(Frame{60}, 100.0f,
                                        EasingCurve{Easing::OutCubic});
            selector.end = 100.0f;
            selector.exclude_spaces = true;
            return selector;
        }

        [[nodiscard]] Composition make_black_glow_apple_v3(
            const char *name, const char *font_path, f32 font_size,
            AppleV3Motion motion) {
            return composition(
                black_glow_spec(name), [font_path, font_size, motion](
                    const FrameContext &ctx) -> Scene {
                    SceneBuilder scene(ctx);
                    scene.screen_layer("BACKGROUND", [](LayerBuilder &l) {
                        l.fill(Color{0.0f, 0.0f, 0.0f, 1.0f});
                    });

                    scene.layer("CHRONON_TEXT", [&](LayerBuilder &l) {
                        const bool word_motion =
                            motion == AppleV3Motion::PrecisionWordStagger ||
                            motion == AppleV3Motion::MaskedVerticalLift ||
                            motion == AppleV3Motion::KineticKeywordLock;
                        TextDefinition definition = canary_text(
                            word_motion ? "CHRONON LAB" : "CHRONON",
                            font_path, font_size, 700,
                            Color{1.0f, 1.0f, 1.0f, 1.0f}, Vec2{1700.0f, 360.0f});

                        const TextSelectorUnit unit =
                            (motion == AppleV3Motion::PremiumSoftReveal ||
                             motion == AppleV3Motion::HeroScaleFocus)
                                ? TextSelectorUnit::Glyph
                                : TextSelectorUnit::Word;
                        const TextSelectorOrder order =
                            (motion == AppleV3Motion::HeroScaleFocus ||
                             motion == AppleV3Motion::KineticKeywordLock)
                                ? TextSelectorOrder::FromCenter
                                : TextSelectorOrder::Forward;

                        TextAnimatorSpec reveal;
                        reveal.id = "apple_v3_reveal";
                        reveal.selectors.push_back(
                            apple_v3_selector("apple_v3_reveal_selector", unit, order));
                        reveal.properties.push_back(OpacityProperty{0.0f});
                        reveal.transform_mode = TextPropertyBlendMode::Multiply;
                        definition.animation.animators.push_back(std::move(reveal));

                        TextAnimatorSpec motion_animator;
                        motion_animator.id = "apple_v3_motion";
                        motion_animator.selectors.push_back(
                            apple_v3_selector("apple_v3_motion_selector", unit, order));

                        switch (motion) {
                        case AppleV3Motion::PremiumSoftReveal: {
                            PositionProperty position;
                            position.value.add_keyframe(
                                Frame{0}, Vec3{0.0f, 10.0f, 0.0f},
                                EasingCurve{Easing::OutCubic});
                            position.value.add_keyframe(
                                Frame{60}, Vec3{0.0f, 0.0f, 0.0f},
                                EasingCurve{Easing::OutCubic});
                            motion_animator.properties.push_back(std::move(position));

                            ScaleProperty scale;
                            scale.value.add_keyframe(
                                Frame{0}, Vec3{0.97f, 0.97f, 1.0f},
                                EasingCurve{Easing::OutCubic});
                            scale.value.add_keyframe(
                                Frame{60}, Vec3{1.0f, 1.0f, 1.0f},
                                EasingCurve{Easing::OutCubic});
                            motion_animator.properties.push_back(std::move(scale));

                            BlurProperty blur;
                            blur.radius.add_keyframe(
                                Frame{0}, 14.0f, EasingCurve{Easing::OutCubic});
                            blur.radius.add_keyframe(
                                Frame{60}, 0.0f, EasingCurve{Easing::OutCubic});
                            motion_animator.properties.push_back(std::move(blur));
                            break;
                        }
                        case AppleV3Motion::PrecisionWordStagger: {
                            PositionProperty position;
                            position.value.add_keyframe(
                                Frame{0}, Vec3{0.0f, 14.0f, 0.0f},
                                EasingCurve{Easing::OutCubic});
                            position.value.add_keyframe(
                                Frame{60}, Vec3{0.0f, 0.0f, 0.0f},
                                EasingCurve{Easing::OutCubic});
                            motion_animator.properties.push_back(std::move(position));

                            ScaleProperty scale;
                            scale.value.add_keyframe(
                                Frame{0}, Vec3{0.985f, 0.985f, 1.0f},
                                EasingCurve{Easing::OutCubic});
                            scale.value.add_keyframe(
                                Frame{60}, Vec3{1.0f, 1.0f, 1.0f},
                                EasingCurve{Easing::OutCubic});
                            motion_animator.properties.push_back(std::move(scale));
                            break;
                        }
                        case AppleV3Motion::MaskedVerticalLift: {
                            PositionProperty position;
                            position.value.add_keyframe(
                                Frame{0}, Vec3{0.0f, 36.0f, 0.0f},
                                EasingCurve{Easing::OutCubic});
                            position.value.add_keyframe(
                                Frame{60}, Vec3{0.0f, 0.0f, 0.0f},
                                EasingCurve{Easing::OutCubic});
                            motion_animator.properties.push_back(std::move(position));
                            break;
                        }
                        case AppleV3Motion::HeroScaleFocus: {
                            ScaleProperty scale;
                            scale.value.add_keyframe(
                                Frame{0}, Vec3{1.08f, 1.08f, 1.0f},
                                EasingCurve{Easing::OutCubic});
                            scale.value.add_keyframe(
                                Frame{60}, Vec3{1.0f, 1.0f, 1.0f},
                                EasingCurve{Easing::OutCubic});
                            motion_animator.properties.push_back(std::move(scale));

                            BlurProperty blur;
                            blur.radius.add_keyframe(
                                Frame{0}, 10.0f, EasingCurve{Easing::OutCubic});
                            blur.radius.add_keyframe(
                                Frame{60}, 0.0f, EasingCurve{Easing::OutCubic});
                            motion_animator.properties.push_back(std::move(blur));
                            break;
                        }
                        case AppleV3Motion::KineticKeywordLock: {
                            PositionProperty position;
                            position.value.add_keyframe(
                                Frame{0}, Vec3{0.0f, 12.0f, 0.0f},
                                EasingCurve{Easing::OutCubic});
                            position.value.add_keyframe(
                                Frame{60}, Vec3{0.0f, 0.0f, 0.0f},
                                EasingCurve{Easing::OutCubic});
                            motion_animator.properties.push_back(std::move(position));

                            ScaleProperty scale;
                            scale.value.add_keyframe(
                                Frame{0}, Vec3{0.96f, 0.96f, 1.0f},
                                EasingCurve{Easing::OutCubic});
                            scale.value.add_keyframe(
                                Frame{30}, Vec3{1.04f, 1.04f, 1.0f},
                                EasingCurve{Easing::OutCubic});
                            scale.value.add_keyframe(
                                Frame{60}, Vec3{1.0f, 1.0f, 1.0f},
                                EasingCurve{Easing::OutCubic});
                            motion_animator.properties.push_back(std::move(scale));
                            break;
                        }
                        }

                        definition.animation.animators.push_back(std::move(motion_animator));
                        definition.animation.duration = kBlackGlowDuration;
                        definition.animation.cache_layout = true;
                        l.text("text", std::move(definition));

                        // Identical glow contract to make_black_glow_static():
                        // motion is the only variable in this baseline.
                        l.effect(GlowPresets::canary_black());
                    });

                    scene.camera().set(canary_orbit_camera(0.0f, 0.0f, 1000.0f));
                    return scene.build();
                });
        }

        /// Final typewriter card used for the font comparison.  The intro lasts
        /// 54 frames (1.8 s); after that, text and cursor are held still for the
        /// rest of the five-second video.  Keep this card on the GPU-native
        /// text contract: translation/scale only, with no unimplemented
        /// RenderSurface effect stack or legacy 3D rotation fallback.
        [[nodiscard]] Composition make_typewriter_final(const char *name,
                                                         const char *font_path,
                                                         f32 font_size,
                                                         f32 cursor_edge) {
            return composition(
                black_glow_spec(name), [font_path, font_size, cursor_edge](
                    const FrameContext &ctx) -> Scene {
                    SceneBuilder scene(ctx);
                    const i32 frame = ctx.frame().integral();
                    const f32 reveal_t = std::clamp(
                        static_cast<f32>(frame) /
                            static_cast<f32>(kTypewriterRevealEnd.integral()),
                        0.0f, 1.0f);
                    const f32 settle = canary_smoothstep(reveal_t);

                    scene.screen_layer("BACKGROUND", [](LayerBuilder &l) {
                        l.fill(Color{0.0f, 0.0f, 0.0f, 1.0f});
                    });

                    // One shared 2D parent keeps the word and its cursor aligned
                    // while remaining compatible with the native GPU text path.
                    scene.null_layer("TYPEWRITER_ROOT", [&](LayerBuilder &l) {
                        l.position({0.0f, canary_lerp(22.0f, 0.0f, settle),
                                    canary_lerp(-24.0f, 0.0f, settle)});
                    });

                    scene.layer("TYPEWRITER_TEXT", [&](LayerBuilder &l) {
                        l.parent("TYPEWRITER_ROOT")
                            .scale({canary_lerp(0.95f, 1.0f, settle),
                                    canary_lerp(0.95f, 1.0f, settle), 1.0f});
                        l.text("text", canary_typewriter_text(
                            "CHRONON", font_path, font_size,
                            Color{1.0f, 1.0f, 1.0f, 1.0f}, Vec2{1700.0f, 360.0f}));
                    });

                    // The red underscore is a real Chronon text node, so its
                    // glyph and glow still belong to Chronon3D.  It is not used
                    // to rebuild or position the letters in the main word.
                    // Its X position follows the reveal from the shaped run's
                    // left edge to its right edge; the spacing itself remains
                    // entirely owned by Chronon's layout.
                    const f32 cursor_x = canary_lerp(-cursor_edge, cursor_edge, reveal_t) + 40.0f;
                    f32 cursor_opacity = reveal_t < 1.0f
                        ? ((frame / 10) % 2 == 0 ? 1.0f : 0.58f)
                        : std::clamp(1.0f -
                                     static_cast<f32>(frame - kTypewriterRevealEnd.integral()) /
                                         12.0f,
                                     0.0f, 1.0f);
                    scene.layer("TYPEWRITER_CURSOR", [&](LayerBuilder &l) {
                        l.parent("TYPEWRITER_ROOT")
                            .position({cursor_x, 6.0f, 0.0f})
                            .opacity(cursor_opacity);
                        l.text("underscore", canary_text(
                            "_", font_path, font_size * 0.82f, 700,
                            Color{1.0f, 0.035f, 0.035f, 1.0f}, Vec2{180.0f, 260.0f}));
                    });

                    scene.camera().set(canary_orbit_camera(0.0f, 0.0f, 1000.0f));
                    return scene.build();
                });
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

        [[nodiscard]] Composition make_text_elements_canary(
            const char *name, ElementCanaryMode mode) {
            return composition(
                canary_spec(name), [mode](const FrameContext &ctx) -> Scene {
                    SceneBuilder scene(ctx);
                    const bool moving = mode != ElementCanaryMode::Static;
                    scene.screen_layer("BACKGROUND", [](LayerBuilder &l) {
                        l.fill(kCanaryVoid);
                    });

                    scene.layer("CHRONON_TEXT_ELEMENTS", [&](LayerBuilder &l) {
                        l.enable_3d(true);
                        if (moving) {
                            l.rotate({0.0f, canary_lerp(-8.0f, 8.0f,
                                                        canary_progress(ctx)), 0.0f});
                        }
                        l.effect(canary_glow(GlowPresets::editorial_highlight(30.0f)));
                        l.text("text", canary_element_text("CHRONON", mode));
                    });

                    scene.camera().set(moving
                        ? canary_orbit_camera(
                            canary_lerp(-8.0f, 8.0f, canary_progress(ctx)), 0.0f, 1000.0f)
                        : canary_orbit_camera(0.0f, 0.0f, 1000.0f));
                    return scene.build();
                });
        }

        [[nodiscard]] Composition make_text_full_static_canary(const char *name) {
            return composition(
                canary_spec(name), [](const FrameContext &ctx) -> Scene {
                    SceneBuilder scene(ctx);
                    scene.screen_layer("BACKGROUND", [](LayerBuilder &l) {
                        l.fill(kCanaryVoid);
                    });

                    scene.layer("CHRONON_TEXT_FULL_STATIC", [&](LayerBuilder &l) {
                        l.enable_3d(true);
                        l.effect(canary_glow(GlowPresets::editorial_highlight(30.0f)));
                        l.text("text", canary_text(
                            "CHRONON", kCanaryFontBold, 180.0f, 700,
                            Color{1.0f, 0.86f, 0.86f, 1.0f}, Vec2{1600.0f, 360.0f}));
                    });

                    scene.camera().set(canary_orbit_camera(0.0f, 0.0f, 1000.0f));
                    return scene.build();
                });
        }

        // ── Final3D catalog: nine production composition IDs ───────────────────────
        // These are Chronon3D-owned compositions. Template intent is represented
        // here as content definitions and motion is evaluated by this runtime; no
        // preview rasterizer or per-character fallback participates.
        [[nodiscard]] Composition make_final3d_catalog(const char *name) {
            return composition(
                canary_spec(name), [name](const FrameContext &ctx) -> Scene {
                    SceneBuilder scene(ctx);
                    const f32 t = canary_progress(ctx);
                    const i32 frame = ctx.frame().integral();
                    scene.screen_layer("BACKGROUND", [](LayerBuilder &l) { l.fill(kCanaryVoid); });

                    const std::string id{name};
                    const bool product = id == "product_orbit" || id == "macbook_product";
                    const bool title_card = id == "title_card_3d";
                    const bool typewriter = id == "typewriter_3d_glow";
                    const bool clean_red = id == "clean_red" || id == "youtube_canary";

                    scene.null_layer("FINAL3D_ROOT", [&](LayerBuilder &l) {
                        l.enable_3d(true)
                            .rotate({0.0f, canary_lerp(-8.0f, 8.0f, canary_smoothstep(t)), 0.0f});
                    });

                    if (product) {
                        scene.layer("PRODUCT", [&](LayerBuilder &l) {
                            l.parent("FINAL3D_ROOT").enable_3d(true)
                                .position({0.0f, -40.0f, 0.0f})
                                .rotate({0.0f, canary_lerp(-24.0f, 24.0f, t), 0.0f})
                                .scale({canary_lerp(0.72f, 1.0f, canary_smoothstep(t)),
                                        canary_lerp(0.72f, 1.0f, canary_smoothstep(t)), 1.0f});
                            l.effect(GlowPresets::editorial_highlight(28.0f));
                            l.text("product_title", canary_text(
                                id == "macbook_product" ? "MACBOOK PRODUCT" : "PRODUCT ORBIT",
                                kCanaryFontBold, 132.0f, 700,
                                Color{0.94f, 0.97f, 1.0f, 1.0f},
                                id == "macbook_product" ? Vec2{1500.0f, 300.0f}
                                                           : Vec2{1300.0f, 300.0f}));
                        });
                    } else {
                        const f32 opacity = typewriter
                            ? canary_smoothstep(static_cast<f32>(frame) / 36.0f)
                            : canary_smoothstep(static_cast<f32>(frame) / 12.0f);
                        scene.layer("TITLE", [&](LayerBuilder &l) {
                            l.parent("FINAL3D_ROOT").enable_3d(true)
                                .position({0.0f, title_card ? 70.0f : 0.0f, 0.0f})
                                .opacity(opacity)
                                .scale({canary_lerp(0.82f, 1.0f, canary_smoothstep(t)),
                                        canary_lerp(0.82f, 1.0f, canary_smoothstep(t)), 1.0f})
                                .rotate({canary_lerp(-8.0f, 8.0f, t),
                                         canary_lerp(-12.0f, 12.0f, t), 0.0f});
                            l.effect(canary_glow(clean_red
                                                     ? GlowPresets::editorial_highlight(34.0f)
                                                     : GlowPresets::neon_blue(34.0f)));
                            l.text("title", typewriter
                                ? canary_element_text("TYPEWRITER 3D", ElementCanaryMode::Motion)
                                : canary_text(
                                    clean_red ? "CHRONON" : "CHRONON",
                                    kCanaryFontBold, 180.0f, 700,
                                    clean_red ? Color{1.0f, 0.86f, 0.86f, 1.0f}
                                              : Color{1.0f, 1.0f, 1.0f, 1.0f},
                                    Vec2{1600.0f, 360.0f}));
                        });
                        if (title_card) {
                            scene.layer("SUBTITLE", [&](LayerBuilder &l) {
                                l.parent("FINAL3D_ROOT").enable_3d(true)
                                    .position({0.0f, -160.0f, 0.0f})
                                    .opacity(canary_smoothstep(static_cast<f32>(frame - 18) / 18.0f));
                                l.text("subtitle", canary_text("MOTION ENGINE", kCanaryFontRegular,
                                                                   68.0f, 400,
                                                                   Color{0.88f, 0.92f, 1.0f, 1.0f},
                                                                   Vec2{1400.0f, 120.0f}));
                            });
                        }
                    }

                    const f32 yaw = id == "camera_push" ? 0.0f : canary_lerp(-14.0f, 14.0f, t);
                    const f32 distance = id == "camera_push"
                        ? canary_lerp(1120.0f, 880.0f, canary_smoothstep(t))
                        : (id == "light_pulse" ? 1010.0f : 1000.0f);
                    scene.camera().set(canary_orbit_camera(yaw, canary_lerp(4.0f, -4.0f, t), distance));
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

        // Text contract canaries: B (static identity), C (opacity stagger),
        // D (opacity plus per-element motion delta).  Each uses one complete
        // Chronon text node; none combines a base run with glyph copies.
        registry.add(make_composition_descriptor(
            "chronon_text_full_static",
            [] { return make_text_full_static_canary("chronon_text_full_static"); }));
        registry.add(make_composition_descriptor(
            "chronon_text_elements_static",
            [] { return make_text_elements_canary("chronon_text_elements_static",
                                                   ElementCanaryMode::Static); }));
        registry.add(make_composition_descriptor(
            "chronon_text_elements_opacity",
            [] { return make_text_elements_canary("chronon_text_elements_opacity",
                                                   ElementCanaryMode::Opacity); }));
        registry.add(make_composition_descriptor(
            "chronon_text_elements_motion",
            [] { return make_text_elements_canary("chronon_text_elements_motion",
                                                   ElementCanaryMode::Motion); }));

        registry.add(make_composition_descriptor("logo_reveal",
                                                 [] { return make_final3d_catalog("logo_reveal"); }));
        registry.add(make_composition_descriptor("product_orbit",
                                                 [] { return make_final3d_catalog("product_orbit"); }));
        registry.add(make_composition_descriptor("title_card_3d",
                                                 [] { return make_final3d_catalog("title_card_3d"); }));
        registry.add(make_composition_descriptor("camera_push",
                                                 [] { return make_final3d_catalog("camera_push"); }));
        registry.add(make_composition_descriptor("light_pulse",
                                                 [] { return make_final3d_catalog("light_pulse"); }));
        registry.add(make_composition_descriptor("macbook_product",
                                                 [] { return make_final3d_catalog("macbook_product"); }));
        registry.add(make_composition_descriptor("typewriter_3d_glow",
                                                 [] { return make_final3d_catalog("typewriter_3d_glow"); }));
        registry.add(make_composition_descriptor("clean_red",
                                                 [] { return make_final3d_catalog("clean_red"); }));
        registry.add(make_composition_descriptor("youtube_canary",
                                                 [] { return make_final3d_catalog("youtube_canary"); }));

        // Static font/glow baselines.  Keep these separate from the animated
        // canaries: they are the visual reference selected before typewriter or
        // 3D motion is introduced.
        registry.add(make_composition_descriptor(
            "black_glow_inter",
            [] { return make_black_glow_static("black_glow_inter",
                                               "assets/fonts/Inter-Bold.ttf", 190.0f); }));
        registry.add(make_composition_descriptor(
            "black_glow_poppins",
            [] { return make_black_glow_static("black_glow_poppins",
                                               "assets/fonts/Poppins-Bold.ttf", 190.0f); }));
        registry.add(make_composition_descriptor(
            "black_glow_dmsans",
            [] { return make_black_glow_static("black_glow_dmsans",
                                               "assets/fonts/DMSans-Bold.ttf", 190.0f); }));
        registry.add(make_composition_descriptor(
            "black_glow_georgia",
            [] { return make_black_glow_static("black_glow_georgia",
                                               "assets/fonts/Georgia_Bold.ttf", 190.0f); }));
        registry.add(make_composition_descriptor(
            "black_glow_freeserif",
            [] { return make_black_glow_static("black_glow_freeserif",
                                               "assets/fonts/FreeSerif.ttf", 190.0f); }));

        // The same five-font fleet with only the letter/word choreography
        // changed.  Keeping the matrix explicit in the registry makes every
        // render addressable and keeps the static baselines above untouched.
        struct AppleV3Font {
            const char *suffix;
            const char *path;
        };
        struct AppleV3MotionEntry {
            const char *suffix;
            AppleV3Motion motion;
        };

        constexpr std::array<AppleV3Font, 5> apple_v3_fonts{{
            {"inter", "assets/fonts/Inter-Bold.ttf"},
            {"poppins", "assets/fonts/Poppins-Bold.ttf"},
            {"dmsans", "assets/fonts/DMSans-Bold.ttf"},
            {"georgia", "assets/fonts/Georgia_Bold.ttf"},
            {"freeserif", "assets/fonts/FreeSerif.ttf"},
        }};
        constexpr std::array<AppleV3MotionEntry, 5> apple_v3_motions{{
            {"premium_soft_reveal", AppleV3Motion::PremiumSoftReveal},
            {"precision_word_stagger", AppleV3Motion::PrecisionWordStagger},
            {"masked_vertical_lift", AppleV3Motion::MaskedVerticalLift},
            {"hero_scale_focus", AppleV3Motion::HeroScaleFocus},
            {"kinetic_keyword_lock", AppleV3Motion::KineticKeywordLock},
        }};

        for (const auto &font : apple_v3_fonts) {
            for (const auto &entry : apple_v3_motions) {
                const std::string id = std::string{"black_glow_"} + font.suffix +
                                       "_" + entry.suffix;
                registry.add(make_composition_descriptor(
                    id,
                    [id, font_path = font.path, motion = entry.motion] {
                        return make_black_glow_apple_v3(
                            id.c_str(), font_path, 190.0f, motion);
                    }));
            }
        }

        // Final typewriter card. Keep one composition while the native GPU
        // reveal path is validated; Poppins is the selected production font.
        registry.add(make_composition_descriptor(
            "typewriter_final_poppins",
            [] { return make_typewriter_final("typewriter_final_poppins",
                                              "assets/fonts/Poppins-Bold.ttf", 190.0f,
                                              500.0f); }));
    }

} // namespace chronon3d
