# TICKET-ANIMATED-VALUE-ADD-KEYFRAME-MIGRATION — Migrate deprecated AnimatedValue<T>::key() to add_keyframe()

## Stato
DEFERRED (awaiting V0.2 milestone)

## Problema
`AnimatedValue<T>::key()` was deprecated in favor of `add_keyframe()` (returns `void`) or
`MotionTimeline<T>::compile()` (from `include/chronon3d/animation/motion/motion.hpp`).
Existing code heavily uses chained `.key().key()` calls, producing **815 `-Wdeprecated-declarations` warnings**
during the build (B1 rot bucket in `/tmp/chronon_baseline/build_post_pull.log`).

The deprecation is mechanical but pervasive: the `add_keyframe` signature is identical to `key`
**except** the return type (`void` instead of `AnimatedValue&`), so any fluent `.key().key()` chain
must be **unrolled** into discrete statements.

## Scope & Evidenza

| Metric | Value |
|---|---|
| Warning count | 815 (B1 bucket) |
| Callsites | 100+ across 35+ files |
| Top offenders | `src/scene/builders/commands/motion_preset_commands.cpp` (57) + `include/chronon3d/scene/camera/camera_rig_builder.hpp` (cascade from upstream `ae83a732`) + `src/scene/builders/commands/motion_preset_methods.cpp` (10-30 est.) + `src/scene/camera/camera_motion_presets.cpp` + `src/scene/camera/camera_rig_presets.cpp` + `src/scene/camera/camera_v1/camera_builtin_presets.cpp` + 30+ others (warning-only) |
| `.add_keyframe()` callsites at HEAD | 0 (in `motion_preset_commands.cpp`) |
| Deprecation source | `include/chronon3d/animation/core/animated_value.hpp:156` (header inline comment) |

## Soluzione (Phased Migration Plan)

**Pattern replacement** (the canonical translation):
```cpp
// DEPRECATED (returns AnimatedValue& — chainable)
pos.key(Frame{0}, v1, EasingCurve{Easing::OutCubic})
   .key(Frame{30}, v2);

// CANONICAL (returns void — must unroll)
pos.add_keyframe(Frame{0}, v1, EasingCurve{Easing::OutCubic});
pos.add_keyframe(Frame{30}, v2);
```

| Fase | File | Callsites est. |
|---|---|---|
| 1 | `src/scene/builders/commands/motion_preset_commands.cpp` | 57 |
| 2 | `include/chronon3d/scene/camera/camera_rig_builder.hpp` (cascade from `ae83a732`) + downstream camera files | 30-50 |
| 3 | 30+ warning-only files (1-3 callsites each) | 30-50 |

## Criteri di accettazione

- `grep -cE '\.key\('` returns 0 in all targeted files (excluding the deprecation-declaration in `animated_value.hpp:156`).
- `cmake --build --target chronon3d_core_tests` produces **0 `-Wdeprecated-declarations` warnings** with default CMake flags.
- `chronon3d_core_tests -tc='*simd*' -tc='*animation*' -tc='*motion*'` PASS.
- 9/9 developer gates PASS via `tools/run_developer_gates.sh` (canonical 9-script chain per `tools/gates/manifest.sh`).
- AVX2 IMPL surface (4 files: `avx2_pixel_kernels.cpp` + `kernel_resolver.hpp` + `test_kernel_resolver_avx2_parity.cpp` + `core_tests.cmake`) remains **byte-identical** to its committed state — per AVX2/AssetResolver/AssetPreflight isolation established in prior analysis.
- Build with `-Werror=deprecated-declarations` produces 0 errors (verifies the deprecation class is fully eliminated, not just downgraded).

## Forward-points

- **Cat-3 fix-forward chain (already documented in this session)**: (a) add `LayerBuilder::text(string, const TextSpec&)` declaration in `layer_builder.hpp:401` (fixes B2+B3 = 35 hard errors), (b) fix `kAdapterBaseFps` constexpr redefinition in `legacy_camera_adapters.cpp:30` (fixes B4 = 1 hard error), (c) inject `-Wno-error=deprecated-declarations` workaround in `CMakePresets.json` (bypasses B1 deprecation ONLY). Note: step (c) is INSUFFICIENT alone — it does not address the 36 hard errors from B2-B4. All 3 steps required for green-pending-Cat-5.
- **Codemod script**: python/bash codemod to extract `.key()` chains and unroll into `.add_keyframe();` statements (mitigates manual unroll errors at 100+ callsites).
- **Animation API Deprecation Lifecycle ADR**: open ADR documenting the policy of "deprecate via inline header comment + `-Wdeprecated-declarations`" so future migrations follow the same pattern.

## Cross-references

- Cascade origin: upstream commit `ae83a732` `refactor(camera): unify pipeline - CameraRig→OrbitMotion canonical` (introduced the `camera_rig_builder.hpp` rot extension).
- Cascade containment: upstream commits `0df91a70` `refactor(cli)` + `86f32a9a` `perf(text)` + `00324117` `chore(runner)` + `b94d0fef` `fix(text)` + `b3f835d2` `refactor(rg)` (each touches SIMD-adjacent surfaces but not SIMD itself; no impact on this ticket).
- Deprecation contract: `include/chronon3d/animation/core/animated_value.hpp:156` (declaration of deprecated `key()` + redirect to `add_keyframe()` + `MotionTimeline<T>::compile()`).
- Replacement API: `include/chronon3d/animation/motion/motion.hpp` (defines `MotionTimeline<T>::compile()` per upstream header comment redirect).
- Build evidence: `/tmp/chronon_baseline/build_post_pull.log` (line ranges 200-8000 contain the 815 warning lines bucketed by file).
- Related tickets: `TICKET-AVX2-DEFER-NEXT-SESSION` (DONE — prior Cat-5 closure pattern precedent); `TICKET-TEXT-LEGACY-POSITION-ROT` (parallel pre-existing rot pattern, prior-art soft-cap mechanism precedent).
- AGENTS.md §honesty: pre-existing rot + upstream extension disclosure; defer-to-milestone is the canonical escape valve when rot fan-out exceeds Cat-3 minimal-surface.
