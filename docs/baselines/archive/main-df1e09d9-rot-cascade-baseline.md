# Baseline — `main@df1e09d9` (2026-07-12)

**245 errors / 11+ files / rot-class OPEN** — verification DEFERRED to working build host per AGENTS.md §honesty.

## Rot-class pattern counts

| Rot-class | Direct | Cascade | Description |
|---|---:|---:|---|
| `chronon3d::chronon3d::*` double-namespace | 30 | ~88 | type pollution from `using namespace chronon3d;` in inner namespace |
| Forward-decl rot (`incomplete type`) | 13 | ~24 | forward `class Scene` referenced but `Scene::layers()/nodes()` accessed |
| Forward-decl rot (`not declared`) | 30 | ~13 | missing transitive include in the consumer header |
| Namespace pollution (`is not a member of`) | 20 | ~10 | type referenced via `chronon3d::chronon3d::` instead of `::chronon3d::` |
| `std::variant` synthesized method | 0 (refined) | 0 | not a separate rot-class; prior-basher 21 matches were generic C++ template-instantiation noise from cascade (a) |

**Total `error:` markers**: 245 (machine-verified via `grep -cE 'error:' /tmp/build_test_artifact.log`). The 50-200+ files estimate from TICKET-BUILD-ROT-CASCADE-CAMERA is **on-target** (245 count includes ~88+24 template-instantiation cascades per root cause).

## Per-file fix decisionali

| # | File | Errors | Fix | Why |
|---|---|---:|---|---|
| 1 | `include/chronon3d/runtime/render_runtime.hpp` | 88 | ① + ④ | Prefix `::chronon3d::` at 88 sites; NEW forwarding header `render_runtime_fwd.hpp` for downstream consumers |
| 2 | `include/chronon3d/scene/model/core/scene.hpp` | 24 | ④ + ① | NEW `scene_fwd.hpp`; consumers include canonical `scene.hpp` directly |
| 3-5 | `src/backends/text/{blend2d_glyph_conversion,text_rasterizer_render,font_engine}.cpp` | 18+17+13 | ① | Consumers of the rot; prefix `::chronon3d::` at call sites |
| 6 | `include/chronon3d/core/scope/execution_scope.hpp` | 13 | ① + ③ | Prefix at 4 sites OR `using chronon3d::RenderSession;` at top |
| 7 | `include/chronon3d/internal/render_graph/core/scene_hasher.hpp` | 12 | ④ | `#include "chronon3d/scene/model/core/scene.hpp"` directly (root-cause via #2) |
| 8 | `include/chronon3d/timeline/composition.hpp` | 11 | ① | Prefix `::chronon3d::camera_v1::CameraDescriptor` + `CameraProgram` |
| 9 | `include/chronon3d/render_graph/cache/scene_program_cache.hpp` | 9 | ① + ④ | Prefix at 4 sites; optional forwarding header for `RenderCounters` |
| 10 | `include/chronon3d/internal/runtime/render_session.hpp` | 9 | ① | Prefix `::chronon3d::graph::SceneHasher` + `SceneProgramStore` |

**Smaller sites (~31 errors across 6+ files)**: `backends/software/{software_renderer,software_render_session}.hpp` + `effects/{curves,glow_pipeline}.hpp` + `effects/effect_catalog.cpp` + `scene/camera/camera_v1/camera_framing_solver.hpp` — all ① (prefix) except `camera_framing_solver.hpp` (DOMAIN BUG: `CameraFramingResult` → `CameraFramingRequest` typo per compiler hint, forward to TICKET-FRAMING-V1 §G).

**Fix patterns**: ① `::chronon3d::` prefix | ④ forwarding header re-export.

**Estimated fix LoC**: ~110 LoC across ~13 files (10 file edits + 3 NEW forwarding headers).

## Commit chain

```
823724cd docs(baseline): capture rot-cascade diagnostic at df1e09d9  (the prior over-detailed doc, now superseded)
df1e09d9 docs(followup): expand TICKET-BUILD-ROT-CASCADE rot-class findings  (the classification commit)
cd2548cb feat(api): public camera facade + external consumer SDK test  (the rot-introduction commit)
```

## TICKET state

TICKET-BUILD-ROT-CASCADE-CAMERA remains **OPEN** in `docs/FOLLOWUP_TICKETS.md` `## Open Blockers`. The ticket transitions to DONE only when the post-fix verification passes on a fit build host (per AGENTS.md §honesty "Non segnare verde una suite che restituisce failure").

## Forward-point (NOT in this commit)

Working build host verification:
```bash
export VCPKG_ROOT="$PWD/vcpkg_bootstrap"
cmake --preset linux-fast-dev -S . -B .tmp/chronon-builds/linux-fast-dev
cmake --build .tmp/chronon-builds/linux-fast-dev --target chronon3d_dev_fast 2>&1 | tee /tmp/host-build-df1e09d9.log
errors=$(grep -cE 'error:' /tmp/host-build-df1e09d9.log 2>/dev/null || echo 0)
test "$errors" -eq 0 || { echo "VERIFICATION_FAILED: $errors errors remain" >&2; exit 1; }
ctest --test-dir .tmp/chronon-builds/linux-fast-dev --output-on-failure
```

Diagnostic artifact preserved at `/tmp/build_test_artifact.log` (~166K chars, 245 `error:` markers).

## Cross-references

- `docs/FOLLOWUP_TICKETS.md` `## Open Blockers` TICKET-BUILD-ROT-CASCADE-CAMERA row (canonical ticket; +1 verified-expansion + +1 baseline-refactor clauses)
- `docs/CHANGELOG.md` 2026-07-12 `docs(followup): expand TICKET-BUILD-ROT-CASCADE rot-class findings` entry (audit-trail)
- `docs/CHANGELOG.md` 2026-07-12 `21 errors mask text_helpers rot` entry (prior onion-peel analysis)
- `docs/baselines/main-7eb5c2ba-baseline.md` (the prior green baseline, 11/11 PASS at 2026-06-29)
- AGENTS.md §honesty (deferred-verification forward-point convention)
- `cd2548cb feat(api): public camera facade + external consumer SDK test` (the rot-introduction commit)
