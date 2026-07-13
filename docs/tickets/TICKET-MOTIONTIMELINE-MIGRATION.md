# TICKET-MOTIONTIMELINE-MIGRATION

**Status**: OPEN (forward-point — see commit `b96981b7`/`7fe58db4` for the deprecation-reversal that created this ticket)

**Goal**: Restore the deprecation discipline for `AnimatedValue<T>::key()` without breaking the `-Werror=deprecated-declarations` build target `chronon3d_core_tests`.

## Problem

The chronological lineage:

- ADRs chose `MotionTimeline<T>::compile()` as the canonical declarative animation builder (canonical timeline surface, ADR-018 / ADR-019 references).
- C1 commits deprecation-marked `AnimatedValue<T>::key(...)` and `AnimatedValue<T>::key(...)` (5-arg overload) with the message `"Use add_keyframe() or MotionTimeline<T>::compile() from animation/motion/motion.hpp"`.
- The reversal commit (`b96981b7`) removed both `[[deprecated]]` markers and documented `key()` as the canonical fluent chainable form, in order to unblock ~660 `.key(` call sites failing under `-Werror=deprecated-declarations`.

This **abandons the project's deprecation intent** without proper migration. This ticket re-establishes the migration.

## Scope (~660 call sites)

The `.key(` call sites are scattered across:
- `src/scene/builders/commands/motion_preset_methods.cpp` (~78 sites)
- `src/scene/builders/commands/motion_preset_commands.cpp` (~57 sites)
- `src/scene/camera/camera_v1/camera_builtin_presets.cpp` (~37 sites)
- `src/scene/camera/camera_rig_presets.cpp` (~38 sites)
- `content/showcases/minimalist/minimalist_animations.cpp` (~124 sites)
- `content/showcases/cinematic/*.cpp` (~70+ sites)
- `content/showcases/sequence-v2/sequence_v2_compositions.cpp` (~13 sites)
- `content/launches/product_launch.cpp` (~19 sites)
- `content/examples/light/light_text_animations.cpp` (~49 sites)
- `content/experimental/ae-parity/ae_camera_text_parity.cpp` (~42 sites)
- `content/text_placement/text_placement_compositions.cpp` (~9 sites)
- `examples/bench_corpus/bench_corpus_scenes.cpp` (~9 sites)
- ... and many more.

## Acceptance criteria

1. **Re-add** `[[deprecated]]` markers on both `AnimatedValue<T>::key(...)` overloads, but with a build-flag escape hatch (e.g., `-DCHRONON3D_ALLOW_FLUCT_KEY_CHAIN=ON` for `chronon3d_core_tests` target only).
2. **Migrate** call sites to **one** of:
   - `AnimationTrack<T>::compile()` (canonical declarative builder; matches ADR-018 / ADR-019).
   - `MotionTimeline<T>::compile()` (the canonical name in the deprecation message).
   - Stay on `key()` but enable the build-flag escape (compromise: documented rot pattern).
3. **Ship** all migrated call sites in a single chore or via per-domain commits (cinematic, minimalist, etc.).
4. **Update** `docs/CHANGELOG.md` to confirm migration completion + the `[[deprecated]]` markers restored.
5. **Optional**: tighten the build-flag gate to `-Werror=deprecated-declarations` as the default for `chronon3d_core_tests` (matches release-gate discipline per `docs/RELEASE_GATE.md`).

## Migration recipe (per call site)

For a chained `.key(...)` call like:
```cpp
auto& sc = scale_anim();
sc.key(Frame{0},   Vec3{0.94f, 0.94f, 1.0f}, EasingCurve{Easing::OutCubic})
  .key(Frame{22},  Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
```

The migration target is one of:
```cpp
// Option A — declarative AnimationTrack (preferred for compositions)
chronon3d::animation::AnimationTrack<Vec3> track;
track.key(Frame{0},   Vec3{0.94f, 0.94f, 1.0f}, EasingCurve{Easing::OutCubic})
     .key(Frame{22},  Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
sc.apply_track(track);
```

```cpp
// Option B — MotionTimeline compile path (canonical for timeline)
sc.apply_track(chronon3d::motion::timeline(Vec3{0.94f, 0.94f, 1.0f})
    .to(Frame{22}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic})
    .compile());
```

## Forward-point discipline

This ticket exists to **document the project's deprecation intent** across the recovery-chore reversal. It is NOT a blocker for the current main; the deprecation reversal is documented in `include/chronon3d/animation/core/animated_value.hpp` (see the doc comment on `AnimatedValue<T>::key(...)`).

## Originating commits

- `b96981b7` — recovery chore that removed the 2 `[[deprecated]]` markers in `animated_value.hpp` (this ticket's birth)
- `7fe58db4` — earlier recovery chore (TextSpec forwarder)

## Effective date for migration

Migration SHOULD land before `docs/RELEASE_GATE.md` `Text V1` reaches release-complete, so that the build-default `-Werror=deprecated-declarations` gate can be tightened.
