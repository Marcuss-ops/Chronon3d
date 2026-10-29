# Linear Wipe implementation checkpoint — 2026-10-29

## Scope and current status

`transition.linear_wipe` is added to the canonical effect catalog with bounded
`progress`, `angle_degrees`, and `feather` parameters, animation bindings,
fingerprinting, and a software processor that scales premultiplied RGBA while
honoring the execution clip. Focused catalog, software behavior, and strict GPU
fail-closed tests have been added.

The software implementation is built and its focused catalog and effect tests
pass. This is not yet full effect certification: strict-native parity,
render-plan E2E and same-SHA CI remain separate gates. The native Vulkan route
is still fail-closed in strict-native mode.

## Provenance

- Chronon3D `HEAD`: `556a4ae58` with a dirty worktree.
- Host: Linux x86_64; NVIDIA RTX A4000 is present, but no Vulkan parity test was run.
- Build preset: `build/chronon/linux-video-test`.

## Checks

- Chronon3D `HEAD`: `556a4ae58`, dirty worktree.
- Host: Linux x86_64, NVIDIA RTX A4000 (not used by this CPU CTest).
- `cmake --build build/chronon/linux-video-test --target chronon3d_effects_tests -j12` — PASS, target linked successfully (100 Ninja steps after earlier interrupted attempts; no OOM).
- `cmake --build build/chronon/linux-video-test --target chronon3d_effects_tests -j12` — PASS, test target rebuilt.
- `ctest --test-dir build/chronon/linux-video-test --output-on-failure -R '^chronon3d_effects_tests$'` — PASS, 1/1 CTest target, 301 cases, 5,538,539 assertions.
- Fixed the two test defects: `Unknown` is sentinel ID 0 and is not a catalog entry, so the expected catalog size is `kEffectTypeMaxValue`; clip bounds are half-open, so `{0,0,4,1}` included pixel `(3,0)`, and the test clip now ends at x=3.
- Native Vulkan pixel parity, render-plan E2E and same-SHA CI remain unverified.

- `git diff --check -- tests/effects/test_stylize.cpp docs/tickets/TICKET-AE-EFFECT-PARITY.md docs/evidence/ae-linear-wipe-20261029/README.md` — exit 0.
Earlier interrupted low-parallelism builds are historical attempts only; the
successful `-j12` build and passing CTest above supersede them. No OOM occurred
in the successful build or test run.
