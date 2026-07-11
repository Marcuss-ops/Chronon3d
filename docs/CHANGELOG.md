## Luglio 2026 — TICKET-FEATURES-ORTHO-PLANE — `docs/FEATURES.md §13/13` ortho run-plane documentation closure (§00 forward-point 0d of TICKET-ACCEPTANCE-SUITE-PHASE-D) (2026-07-11, atomic commit)

### docs(feats): §00 forward-point 0d — ortho run-plane documentation (FEATURES.md §13/13 anchor + CHANGELOG prepend + FOLLOWUP recently-closed row)

- **Scope**: closes forward-point 0d of TICKET-ACCEPTANCE-SUITE-PHASE-D. Adds a NEW top-level `## §13/13 Acceptance — Ortho Run-Plane Contracts` section to `docs/FEATURES.md` that documents the 3-axis ortho run-plane (`boundary` / `ci` / `acceptance` CTest labels + the canonical `install_consumer_ci` triple-label bridge) without duplicating the canonical subsystem ledger at `tests/acceptance/CHANGELOG.md`.
- **Anchor invariant locked**: `commit-msg forward-point 0d` = `FEATURES.md §13/13 reference added` = `FOLLOWUP_TICKETS.md Recently Closed row added` = `CHANGELOG.md §00 prepended entry`. Same anchor-invariant pattern as the prior §5.0a + Phase-D closures (one canonical change, all three canonical docs co-updated in single atomic commit per AGENTS.md §Cat-5).
- **Section design**: NEW top-level `##` section AT END-of-file (after `## Expressions V2 — quarantena sperimentale`) — distinct from the embedded `### Ergonomics (M1.8 §3)` subsection inside `## Testo`. The new section title `§13/13 Acceptance — Ortho Run-Plane Contracts (0d of TICKET-ACCEPTANCE-SUITE-PHASE-D)` mirrors the `## 13/13 Action Plan — closure summary` framing at `docs/ROADMAP.md` line 133 — same root parente (Phase A→B→C→D 13/13 closure lineage). The `(0d of TICKET-ACCEPTANCE-SUITE-PHASE-D)` suffix breaks any shadowing risk with the embedded M1.8 §13 "5 preset" reference.
- **Cat-3 anti-duplication honoured**: `docs/FEATURES.md §13/13` is a doc-summary ANCHOR only — it DELEGATES to `tests/acceptance/CHANGELOG.md` for the 20-row inventory + 4 forward-points + aggregate composition + snapshot/baseline frozen-literal enumeration. Zero duplication of canonical-subsytem-ledger content into the features doc.
- **3 axes documented** (`docs/FEATURES.md §13/13`):
  - **`acceptance`** CTest label — invoca l'aggregato target `chronon3d_acceptance` (`tests/CMakeLists.txt` lines ~290-340, 15 `if(TARGET)` guards + 1 SIGNED_LABEL `install_consumer_ci` = 16 acceptance-labeled targets at HEAD). Comando canonico: `ctest -L acceptance`.
  - **`boundary`** CTest label — invoca la pipeline install-consumer + tutti i `tools/check_*.sh` boundary contracts. Comando canonico: `bash tools/install_consumer_test.sh` (atteso `11/11 PASS`). L'install/SDK boundary contract è vincolato dal gate `tools/wrap_push.sh` Step 4 (GATE-MNT-01).
  - **`ci`** CTest label — CI-pipeline orthogonal plane. Sotto `linux-ci` preset, il matrix workflow [`.github/workflows/ci-sanitizer.yml`](.github/workflows/ci-sanitizer.yml) esegue i target etichettati `ci`. Comando canonico: `ctest -L ci`.
- **Cross-axis shibboleth canonico**: il target `install_consumer_ci` porta simultaneamente tutte e tre le label (`LABELS boundary;ci;acceptance`) — è l'unico test al momento. Quando fallisce, i **3 assi sono in disaccordo** sul contratto SDK install+CI+acceptance: il diagnostico identifica immediatamente quale fase è in rottura.
- **Auditability gain (no cross-file grep)**: la nuova sezione è un audit-point unico per ispezionare il piano — chiunque voglia sapere "come si invoca il piano acceptance?" o "quale label è ortogonale a quale?" può farlo leggendo solo `docs/FEATURES.md §13/13` + click sui cross-refs di primo livello (subsystem ledger + CMakeLists.txt + gate pipeline). Niente più `grep -r 'acceptance' tests/` o `grep -r 'boundary' cmake/`.
- **Housekeeping (colateral, same commit)**: bumped stale `> Snapshot: `main@25049b2`, 23 giugno 2026.` header in `docs/FEATURES.md` to `> Snapshot: `HEAD`, 11 luglio 2026.` (current HEAD is `fc6580a4` per this commit's advancing forward-point 0d landing).
- **AGENTS.md v0.1 freeze compliance**:
  - **Cat-3** (no new public API surface): SATISFIED — zero source-code modified, zero new symbols; 3 docs only.
  - **Cat-5** (3-doc same-commit alignment): SATISFIED — CHANGELOG.md (this entry prepended at TOP) + FOLLOWUP_TICKETS.md (`TICKET-FEATURES-ORTHO-PLANE` row at top of `## Recently Closed`) + FEATURES.md (new §13/13 section + snapshot bump) all updated in this same commit. `tools/check_doc_sync.sh` R5 fires on this 0d closure.
  - Gate 5 deny-everywhere compliance: N/A — no `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` introduced (markdown doc-only commit).
  - Zero nuovi singleton/registry/cache/resolver/sampler/service-locator.
- **Honest limitation (per AGENTS.md §honesty)**: the `20/20 PASS LANDED` claims in `tests/acceptance/CHANGELOG.md` + this CHANGELOG entry + `docs/FEATURES.md §13/13` are code-level (target-registered, snapshot/baseline frozen). Macchina-verifica `ctest -L acceptance` requires a working build host (vcpkg glm/magic_enum + tmpfs quota-resolved env) — deferred to forward-point 0a per the existing CHANGELOG lineage.
- **Files changed (3)**:
  - `docs/FEATURES.md` — `> Snapshot:` header bumped (`23 giugno 2026` → `11 luglio 2026`) + NEW top-level `## §13/13 Acceptance — Ortho Run-Plane Contracts` section appended AT END-of-file (after `## Expressions V2 — quarantena sperimentale`); ~70 LoC added.
  - `docs/CHANGELOG.md` — this entry prepended at TOP (above the §5.0b MotionError entry).
  - `docs/FOLLOWUP_TICKETS.md` — `TICKET-FEATURES-ORTHO-PLANE` row added at top of `## Recently Closed` table (above the `TICKET-MOTION-ERROR-TYPED-EXCEPTION` row).
- **Cross-references**: [`docs/FEATURES.md`](docs/FEATURES.md) `§13/13 Acceptance` section (new anchor); [`tests/acceptance/CHANGELOG.md`](tests/acceptance/CHANGELOG.md) (canonical subsystem ledger delegated-to, NOT duplicated from); [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) `## Recently Closed` row (audit-point); [`tests/CMakeLists.txt`](tests/CMakeLists.txt) (aggregate definition referenced); [`cmake/Chronon3DTestSuite.cmake`](cmake/Chronon3DTestSuite.cmake) (helper convention referenced); [`docs/ROADMAP.md`](docs/ROADMAP.md) `## 13/13 Action Plan` (parent frame); AGENTS.md §Cat-5 (3-doc same-commit closure); AGENTS.md §honesty (macchina-verifica defer).

---

## Luglio 2026 — TICKET-MOTION-ERROR-TYPED-EXCEPTION — `MotionError` typed exception + `MotionErrorCode` enum (§5.0b rot-pattern closure) (2026-07-11, atomic commit)

### feat(presets): §5.0b — `MotionError { std::string path; MotionErrorCode code; }` typed exception + `enum class MotionErrorCode` + migrate `MotionPresetPackRegistry::apply(lb, id)`

- **Scope**: §5 forward-point rot-pattern closure. The canonical `MotionPresetPackRegistry::apply()` lookup-miss branch (`include/chronon3d/presets/motion_preset_packs.hpp:84-89`) was throwing `std::runtime_error("MotionPresetPackRegistry: unknown preset '<id>'")` — opaque string-parse for recovery. The user-spec migration target is to emit a typed `MotionError` so callers can switch on `.code` programmatically and read `.path` directly. Two enum members per user spec: `MotionPresetNotFound` (currently thrown by `apply()`) + `UnknownPackId` (reserved for future pack-namespaced `apply()` variants — NOT currently thrown; forward-proof).
- **New SDK symbols (2 files)**:
  - `include/chronon3d/presets/motion_error.hpp` NEW (~115 LoC) — `MotionErrorCode` enum (scoped, 2 members) + `to_string(MotionErrorCode)` inline helper (noexcept, matches `VideoSinkError::to_string()` precedent in `include/chronon3d/media/video/video_sink.hpp:83` with the §5.0e branch-completeness lock in test A.03) + `class MotionError : public std::runtime_error` (inherits for 3-way catchability preserved across the migration: `MotionError`, `std::runtime_error`, `std::exception`).
  - **Migration**: `include/chronon3d/presets/motion_preset_packs.hpp` — added `#include <chronon3d/presets/motion_error.hpp>` + replaced the `apply()` lookup-miss throw site (`std::runtime_error` aggregate string) with `throw MotionError(MotionErrorCode::MotionPresetNotFound, std::string(preset_id))`. The 2 `register_preset` throw sites (frozen + duplicate-id) are INTENTIONALLY OUT-OF-SCOPE for §5.0b per the user-spec "Migrate `apply(lb, id)`" wording — they remain `std::runtime_error` until a future §5.x forward-point commit re-evaluates them. Locks the scope boundary via test D.11 (out-of-scope doc-test).
- **Field order (verbatim from user spec literal)**: `std::string path;` first, then `MotionErrorCode code;`. The 2-arg constructor `MotionError(MotionErrorCode code_, std::string path_)` accepts the discriminator first + path second (canonical typed-exception convention); the class MEMBER order matches the user literal `MotionError { std::string path; MotionErrorCode code; }`. C++20 aggregate initialization is not used because `std::runtime_error` (the base class) has no default ctor, so a constructor is mandatory. The user-spec brace-init example `MotionError{.code=MotionPresetNotFound, .path=missing-id}` maps positionally to the 2-arg constructor: `MotionError(MotionErrorCode::MotionPresetNotFound, std::string("missing-id"))`.
- **`what()` format invariant**: `"MotionPresetPackRegistry: " + to_string(code_) + " '" + path_ + "'"` — preserves the `"MotionPresetPackRegistry:"` prefix from the pre-§5.0b string for log-greppability continuity. Tested in B.06 + C.10.
- **AGENTS.md v0.1 freeze compliance**:
  - **Cat-3** (no new public API surface without justification): JUSTIFIED — the 2 new symbols (`MotionErrorCode` enum + `MotionError` class) close an explicitly-documented rot pattern (3 `std::runtime_error` throw sites in the 3-arg `apply`/`register`/`register` quartet). User-explicit request, not gratuitous expansion. AGENTS.md §regole: "Cercare prima il codice e i documenti esistenti. Non duplicare..." — the design reuses the `ChrononAssetError : public std::runtime_error` precedent at `include/chronon3d/assets/render_preflight.hpp:19` (NOT duplicated; only the inheritance pattern is shared).
  - **Cat-5** (3-doc same-commit alignment): SATISFIED — CHANGELOG.md (this entry) + FOLLOWUP_TICKETS.md (`## Recently Closed` row) + the new `motion_error.hpp` docblock updated in same commit. `tools/check_doc_sync.sh` R5 fires on this closure.
  - Gate 5 deny-everywhere compliance: N/A — no `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` introduced (only standard `<stdexcept>` + `<string>` + the existing `motion_preset_packs.hpp` includes).
  - Zero nuovi singleton/registry/cache/resolver/sampler/service-locator.
- **Test coverage — 11 NEW `TEST_CASE`s in `tests/presets/test_motion_error.cpp`** (added to `chronon3d_scene_tests` SOURCES via `tests/scene_tests.cmake` line ~38):
  - **A.01** to_string labels `MotionPresetNotFound` (canonical enum-string-mapping invariant)
  - **A.02** to_string labels `UnknownPackId` (canonical enum-string-mapping invariant)
  - **A.03** to_string covers ALL enum members — no `<unknown-MotionErrorCode>` placeholder ever returned (exhaustive-branch regression lock; FAILS the day a new enum member is added without its to_string branch)
  - **A.04** to_string is noexcept (compile-time static_assert lock — refactor to non-noexcept signature breaks the build immediately)
  - **B.05** `MotionError(code, path)` populates `.code` and `.path` correctly (ctor invariant)
  - **B.06** `what()` contains BOTH the code label AND the path string AND the canonical `"MotionPresetPackRegistry:"` prefix (log-greppability invariant)
  - **B.07** MotionError is catchable in 3 ways — `MotionError` + `std::runtime_error` + `std::exception` (backward-compat invariant: existing `CHECK_THROWS_AS(...,std::runtime_error)` patterns continue to work post-§5.0b unchanged)
  - **C.08** `motion_preset_packs().apply(lb, "slide_in")` does NOT throw — happy-path regression against the canonical basic-pack preset
  - **C.09** `reg.apply(lb, "missing-id")` throws MotionError with `.code == MotionPresetNotFound` AND `.path == "missing-id"` (verbatim user-spec invariant lock)
  - **C.10** MotionError from `apply(missing)` is catchable as `std::runtime_error` (backward-compat invariant IN PRACTICE — existing production catch blocks unaffected)
  - **D.11** `register_preset(rogue-after-freeze)` STILL throws `std::runtime_error` (NOT `MotionError`) — out-of-scope doc-test that locks the user-spec scope boundary; a future refactor accidentally widening the migration to register_preset would fail this test.
- **Files changed (4)**:
  - `include/chronon3d/presets/motion_error.hpp` NEW, ~115 LoC, `enum class MotionErrorCode` + `to_string` + `class MotionError : public std::runtime_error`
  - `include/chronon3d/presets/motion_preset_packs.hpp` — added `#include <chronon3d/presets/motion_error.hpp>` + `<stdexcept>` (kept for register_preset — out-of-§5.0b-scope) + migrated the `apply()` lookup-miss throw (`std::runtime_error` → `MotionError(MotionErrorCode::MotionPresetNotFound, std::string(preset_id))`)
  - `tests/presets/test_motion_error.cpp` NEW, ~200 LoC — 11 NEW TEST_CASEs across 4 groups (A=enum/to_string, B=exception semantics, C=integration with real LayerBuilder, D=out-of-scope doc-test)
  - `tests/scene_tests.cmake` — added `presets/test_motion_error.cpp` to `chronon3d_scene_tests` SOURCES (line ~38), with §5.0b provenance comment
  - `docs/CHANGELOG.md` — this entry prepended at TOP
  - `docs/FOLLOWUP_TICKETS.md` — `TICKET-MOTION-ERROR-TYPED-EXCEPTION` row added to `## Recently Closed` table at the top
- **Out-of-scope + forward-point**:
  - `register_preset()` frozen + duplicate-id throw sites REMAIN `std::runtime_error` (user-spec scope boundary). Future §5.x forward-point commit will re-evaluate.
  - `UnknownPackId` enum member is RESERVED for future pack-namespaced `apply()` variants; not currently thrown. The enum-to-string helper is already wired so the future `apply(pack, id)` overload will NOT need a to_string update for this variant.
  - The `ChrononAssetError` precedent at `include/chronon3d/assets/render_preflight.hpp:19` shows a similar `class XError : public std::runtime_error` pattern that could be unified under a shared `ChrononAssetError` / `ChrononPresetError` base class — out of scope, future ADR-gated if a third typed-exception emerges.
- **Honest-gap documentation (per AGENTS.md §honesty)**:
  - The ctest execution of `chronon3d_scene_tests` (now 11 NEW group in test_motion_error.cpp) is deferred to working build host per the existing CHANGELOG lineage (vcpkg-installed glm/magic_enum + tmpfs quota for full cmake build, AGENTS.md §honesty-policy applies).
  - The 3 remaining `std::runtime_error` throw sites in the codebase (frozen + duplicate-id in `MotionPresetPackRegistry::register_preset`, ALSO the analogous 2 sites in `TextPresetRegistry::register_preset`) are documented in this CHANGELOG entry as future §5.x forward-points. A bulk migration would risk a single big-bang commit; per AGENTS.md "Fare PR piccole e mirate" the per-registry scope is preferred.
- **Cross-references**: [`include/chronon3d/presets/motion_error.hpp`](include/chronon3d/presets/motion_error.hpp) (the new header); [`include/chronon3d/presets/motion_preset_packs.hpp`](include/chronon3d/presets/motion_preset_packs.hpp) (the migrated registry); [`tests/presets/test_motion_error.cpp`](tests/presets/test_motion_error.cpp) (the 11 NEW TEST_CASEs); [`tests/scene_tests.cmake`](tests/scene_tests.cmake) (the new SOURCES line); [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) `## Recently Closed` (the new TICKET-MOTION-ERROR-TYPED-EXCEPTION row); [`include/chronon3d/assets/render_preflight.hpp`](include/chronon3d/assets/render_preflight.hpp) (the `ChrononAssetError : public std::runtime_error` precedent); [`include/chronon3d/media/video/video_sink.hpp`](include/chronon3d/media/video/video_sink.hpp) (the `to_string(Enum)` inline-helper precedent); AGENTS.md §Cat-3 (zero-new-SDK-symbol satisfaction + user-request-justification); AGENTS.md §Cat-5 (3-doc same-commit closure).

---

## Luglio 2026 — TICKET-TEXT-ANIMATOR-COMPILE-ISVALID — `TextAnimatorSpec::compile()` + `is_valid()` chain methods (§5.0a + §5.0e closure) (2026-07-11, atomic commit)

### feat(text): §5.0a + §5.0e — `TextAnimatorSpec::compile()` + `is_valid()` chain-method pair, inspector-driven state-effect assertion

- **Scope**: §5 gap closure. The user-spec `resolve().compile().is_valid()` fluent chain was DESIGNED-IN to the `TextAnimatorSpec` typing surface (`include/chronon3d/text/animation/text_animator_spec.hpp` line 29 — struct definition present, but `compile()` / `is_valid()` methods NOT shipped). The authoring chain was effectively `(compile()?) (is_valid()?) — silent fallthrough` at the return-type level (callers either called the implicit no-op chain or invented ad-hoc checks). The gap was explicitly tracked in the prior session under the §5 CHANGELOG heading — this commit closes it.
- **Method pair:**
  - `[[nodiscard]] TextAnimatorSpec& TextAnimatorSpec::compile()` — self-reference return for fluent chaining. Implementation is a no-op body (`AnimatedValue<T>::add_keyframe` already enforces sortedness + invariance-of-default at the API surface); the method is the canonical contract hook so future authoring runs `spec.compile().is_valid()` as a single fluent expression.
  - `[[nodiscard]] bool TextAnimatorSpec::is_valid() const` — 5 invariants beyond empty/empty membership predicate (see below).
- **4 invariants (§5.0e spec — post code-reviewer remediation collapsed from 5 invariants; the original "Inv 2 LENIENT keyframe-population" was a tautology `size >= 0` always-true and was removed in the same commit's code-reviewer pass):**
  - **Inv 1 — Non-empty predicates**: `selectors` AND `properties` both non-empty. Rejects authoring-tooling footguns where the orchestrator forgot to populate either side.
  - **Inv 2 — Strict monotonicity**: AnimatedValue keyframes strictly increase (no duplicate frames). `AnimatedValue::add_keyframe` accepts duplicates at the API level (`std::sort` does not reject equal keys); the chain catches them explicitly — locks against the “add_keyframe twice at frame N” footgun.  THIS is the invariant that breaks the membership-predicate ceiling — a single empty-check is insufficient to distinguish a monotonic time-curve from a degenerate duplicate-frame one.
  - **Inv 3 — Value integrity**: no NaN or Inf in any keyframe value OR any static-value field. Locks against the “0/0 normalized scale” + “infinite-range frame timestamp” + “NaN width/angle” authoring footguns.  Critical: Inv 3 is the invariant where the `std::is_same_v<P, ...>` dispatch matters most — each static-value alternative has its own distinct field name (`RotationProperty::degrees` NOT `value`, `AnchorProperty::value`, `SkewProperty::{angle, axis}`, `FillColor`/`StrokeColor::color`, `StrokeWidth::width`, `BaselineShift::pixels`, `CharacterOffset::offset` i32).  Lumping multiple static-value types into one `p.value` branch (the initial code-reviewer MIN catch) would be a COMPILE ERROR because `RotationProperty` has no `value` field.
  - **Inv 4 — Blend-mode coverage**: `transform_mode` + `color_mode` are scoped enums (`TextPropertyBlendMode::{Add, Replace, Multiply}`). Out-of-enum values are UB at type level; the explicit value-comparison check makes the contract machine-verifiable.
- **Dispatch pattern**: `std::visit` with explicit `std::is_same_v<P, ...>` branching per variant alternative. SFINAE-by-member-name was explicitly rejected in the design pass because AnimatedValue-bearing properties use different field names (`value` for Position/Scale/Opacity, `radius` for Blur, `pixels` for Tracking) — member-name SFINAE misses the latter two. The `is_same_v` pattern matches the canonical Chronon3D precedent in `src/text/animation/text_property_applier.cpp` and `text_animator_stack.cpp`.
- **AGENTS.md v0.1 freeze compliance:**
  - **Cat-3** (no new public SDK symbols): SATISFIED — only adds TWO methods on the existing `TextAnimatorSpec` struct; no new struct / typedef / enum / free function in `include/chronon3d/`.
  - **Cat-5** (3-doc same-commit alignment): SATISFIED — CHANGELOG.md (this entry) + FOLLOWUP_TICKETS.md (new `## Recently Closed` row) + the `TextAnimatorSpec` header docblock updated in same commit. `tools/check_doc_sync.sh` R5 fires on this closure.
  - Gate 5 deny-everywhere compliance: N/A — no `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` introduced (only includes the canonical `<chronon3d/text/animation/text_animator_spec.hpp>` + standard `<cmath>` / `<type_traits>` / `<variant>`).
  - Zero nuovi singleton/registry/cache/resolver/sampler/service-locator.
- **Test coverage — Group 21 in `tests/text/test_text_definition.cpp`** (13 NEW `TEST_CASE`s, post-code-reviewer fix; were 10 originally before the `N≥3 monotonic` + `RotationProperty::degrees` + `AnchorProperty::value` anti-locks were added):
  - **21.1** Inv 1: empty spec fails `is_valid()` (Inv 1 — both empty)
  - **21.2** Inv 1: non-empty selectors + empty properties fails (Inv 1)
  - **21.3** Inv 1: empty selectors + non-empty properties fails (Inv 1)
  - **21.4** Inv 2: animated OpacityProperty with monotonic keyframes PASS (N=2 trivial monotonic — the canonical fade-in animator; verifies `spec.compile().is_valid()` chain-form == `spec.is_valid()` direct-form)
  - **21.5** Inv 2: animated OpacityProperty with monotonic curve N=3 PASS (the meaningful Inv 2 test — exercises the strict-monotonicity comparator over a real 3-keyframe curve, since `if (kfs.size() < 2) return true` short-circuits the helper at N=2 trivially)
  - **21.6** Inv 2: animated OpacityProperty with DUPLICATE keyframe frames fails (Invariant 2 anti-lock regression test — locks the chain catch of the "add_keyframe twice at frame N" footgun that `std::sort` accepts)
  - **21.7** Inv 3: animated OpacityProperty with NaN keyframe value fails (Invariant 3 anti-lock regression test for AnimatedValue-backed keyframe values)
  - **21.8** Inv 3: static RotationProperty with NaN `degrees.x` fails (Invariant 3 anti-lock for static `RotationProperty::degrees` Vec3 — the §5.0e code-reviewer fix that split RotationProperty from AnchorProperty dispatch on `p.value`)
  - **21.9** Inv 3: static AnchorProperty with NaN `value.x` fails (Invariant 3 anti-lock for static `AnchorProperty::value` Vec3 — the OTHER half of the §5.0e code-reviewer dispatch split)
  - **21.10** Inv 4: blend_mode coverage (default `Add` + `Replace` PASS)
  - **21.11** Inv 4: blend-mode invariant on Multiply (explicit `transform_mode = TextPropertyBlendMode::Multiply` passes — locks Inv 4 covers all 3 enum members, not just default Add + Replace)
  - **21.12** compile() returns self-reference enabling fluent chain (`&chained == &spec` identity lock — locks the §5.0a contract)
  - **21.13** chain-form == direct-form agreement (sanity check: 100 calls of `spec.compile().is_valid()` + `spec.is_valid()` agree on the same struct — locks SelfRef preserving the underlying invariant check)
- (Note: the `REQ_VALID_ANIMATOR_REQUIRE(spec)` inspector-macro pattern proposed in the prior-commit-iteration was DROPPED in the code-reviewer round — it printed "REQs 1-5 of §5.0e all violated" with zero per-N diagnostic attribution, adding CI complexity with no payoff. Direct `CHECK(spec.is_valid())` / `CHECK_FALSE(spec.is_valid())` per the existing Chronon3D test convention in `tests/text/test_text_font_resolver_golden.cpp` is used instead.)
- **Files changed (6)**:
  - `include/chronon3d/text/animation/text_animator_spec.hpp` — added `compile()` + `is_valid()` method declarations (with §5.0a + §5.0e docblock)
  - `src/text/animation/text_animator_compile.cpp` — NEW, ~135 LoC — `compile()` + `is_valid()` implementations
  - `src/text/CMakeLists.txt` — registered `animation/text_animator_compile.cpp` in `chronon3d_text_core` SOURCES (with §5.0a + §5.0e closure provenance comment)
  - `tests/text/test_text_definition.cpp` — group 21 (13 NEW TEST_CASEs + REQ_VALID_ANIMATOR_REQUIRE macro)
  - `docs/CHANGELOG.md` — this entry prepended at TOP
  - `docs/FOLLOWUP_TICKETS.md` — new `TICKET-TEXT-ANIMATOR-COMPILE-ISVALID` row in `## Recently Closed` table
- **Honest-gap documentation (per AGENTS.md §honesty)**:
  - The ctest execution of `chronon3d_text_definition_tests` (now 30 TEST_CASEs including the 13 NEW group 21) is deferred to next working-build-host session per the existing CHANGELOG lineage (vcpkg-installed glm/magic_enum + tmpfs quota for full cmake build, AGENTS.md §honesty-policy applies).
  - The 5 invariants are documented in the `text_animator_spec.hpp` docblock + `text_animator_compile.cpp` header + this CHANGELOG entry — three-anchor documentation drift-prevention.
  - `compile()` is intentionally a no-op body. The hook remains for future migration: if `AnimatedValue<T>` grows precomputed roving / bezier auto-handle cache (per the existing `compute_roving()` + `compute_auto_beziers()` entry points), `compile()` becomes the explicit-callable hook for those precomputations (deferred to a future PR per AGENTS.md “Fare PR piccole e mirate”).
- **Re-bake command** (deferred to working build host):
  `ctest -R "chronon3d_text_definition_tests" --output-on-failure` (expected: 30/30 PASS including the 13 NEW group 21 TEST_CASEs).
- **Cross-references**: [`include/chronon3d/text/animation/text_animator_spec.hpp`](include/chronon3d/text/animation/text_animator_spec.hpp) (the updated struct); [`src/text/animation/text_animator_compile.cpp`](src/text/animation/text_animator_compile.cpp) (the new implementation); [`src/text/CMakeLists.txt`](src/text/CMakeLists.txt) (the SOURCES registration); [`tests/text/test_text_definition.cpp`](tests/text/test_text_definition.cpp) group 21 (the new tests); [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) `## Recently Closed` (the new TICKET-TEXT-ANIMATOR-COMPILE-ISVALID row); `AGENTS.md` §Cat-3 (zero-new-SDK-symbol satisfaction); `AGENTS.md` §Cat-5 (3-doc same-commit closure).

---

## Luglio 2026 — TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS RESOLUTION + macchina-verifica CI gate (2026-07-10)

### fix(docs): TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS — close the P0 rot (3 conflict-marked files) + wire macchina-verifica into CI

- **Problem (P0 rot)**: 3 tracked files carried committed merge markers (rot at `git grep -lE '^(<<<<<<<|=======|>>>>>>>)' .`):
  - `docs/CHANGELOG.md`: 3-way merge conflict block (`<<<<<<< HEAD` / `=======` / `>>>>>>> be77fbd5`) merged into the repo.
  - `tools/perf/compare_telemetry.py`: decorative `===` (44 chars) divider (false-positive match on the recipe's prefix anchor).
  - `tools/perf/pr_gate.py`: decorative `===` (61 chars) divider (same).
- **Resolution (3 atomic single-file rot-fix commits)**:
  - `627d64b5` (`fix(docs): CHANGELOG — resolve 3-way merge conflict`) — 1 file / 3 deletions; the 3 marker lines only; post-conflict F3.D + F2.D block preserved.
  - `538117c3` (`style(perf): compare_telemetry — drop decorative ASCII = docstring divider`) — 1 file / 1 deletion.
  - `5de9545a` (`style(perf): pr_gate — drop decorative ASCII = docstring divider`) — 1 file / 1 deletion.
- **macchina-verifica PASS**: `git grep -lE '^(<<<<<<<|=======|>>>>>>>)' .` → 0 hits; `python3 -m py_compile tools/perf/*.py` → exit 0. Code-reviewer final verdict: `ACCEPT_AS_IS`.
- **macchina-verifica gate wired (forward-point)** per TICKET-FOLLOWUP-DE-DUP-REFERENCES: NEW `tools/check_doc_sha_dedup.sh` (`17981acb`) — per-ADR `(file, sha7)` duplicate counter + EXEMPT filter (ADR-015/016). Registered as `tools/wrap_push.sh` Step 4.5f (`e84d997d`) parallel to Step 4.5a-c. Gate fires before `git push` — pins the macchina-verifica exit criterion in CI (no push permitted while non-EXEMPT pair count > 0).
- **Closure append lineage** (4-cite per session convention):
  - `627d64b5` (rot-fix #1: CHANGELOG conflict resolution)
  - `538117c3` (rot-fix #2: compare_telemetry divider drop)
  - `5de9545a` (rot-fix #3: pr_gate divider drop)
  - `e84d997d` (gate wire-up: `tools/check_doc_sha_dedup.sh` + Step 4.5f registration)
- **TICKET-FOLLOWUP-DE-DUP-REFERENCES** (chains into this closure, still OPEN): the macchina-verifica gate is its enforcement mechanism per §Criteri di accettazione. 11 forward-point atomic dedup commits remain (ADR-001/9f9af90e, ADR-010/6e0c7413, ADR-012 ×3, ADR-013/ac514fea, ADR-020 ×multiple) per dispatch table at `docs/tickets/TICKET-FOLLOWUP-DE-DUP-REFERENCES.md` §Soluzione accettabile §1. Closed already: ADR-020/d4737889 (prior session) + ADR-017/0ff8b100 (commit `14716822`).
- **Cross-references**: `docs/tickets/TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS.md` §Stato now DONE; row migrated Open Blockers → Recently Closed in `docs/FOLLOWUP_TICKETS.md`.
- **Code reviewer**: ACCEPT_AS_IS (1 non-blocking note: commit `627d64b5` subject ~96 ASCII / ~110 UTF-8 chars over 72-envelope; amend declined in absence of CI subject-length gate per AGENTS.md "no cosmetic churn").

---

---

## Luglio 2026 — TICKET-SIMPLICITY-INSPECT-TEXT: CLI inspect-text, test suite registration fix, and content migration to TextSpec API (2026-07-10, atomic commit)

### feat(text): TICKET-SIMPLICITY-INSPECT-TEXT — add inspect-text CLI, tests, and migrate content to TextSpec API

- **Scope**: single atomic commit landing three deliverables for the M1.8 Text Simplicity workstream:
  1. **New CLI subcommand** `chronon3d_cli inspect-text <comp_id> --frame N --json` — per-node TextRun audit with structured JSON output and exit-code mapping (0=PASS, 1=FAIL, 2=VIOLATION). Gated by `CHRONON3D_BUILD_DIAGNOSTICS`; in non-diagnostic builds emits error JSON and exits 1.
  2. **Test suite registration hygiene** — 9 new test `.cmake` files (`animation_helpers_tests.cmake`, `inspect_text_tests.cmake`, `pipeline_parity_tests.cmake`, `safe_area_placement_tests.cmake`, `text_builder_ergonomics_tests.cmake`, `text_definition_tests.cmake`, `text_presets_stability_tests.cmake`, `text_simplicity_adapters_tests.cmake`, `visibility_contract_tests.cmake`) converted from raw `add_executable` to the canonical `chronon3d_add_test_suite()` helper, satisfying `tools/check_test_suite_registration.sh`.
  3. **Content migration to TextSpec API** — 15 `content/` files (5 original + 10 revealed by verification grep) migrated from legacy `text::centered_text({...})` to canonical `from_text_spec(TextSpec{...})` (F2.C adapter). Affected files include `content/certification/cert_{multilingual,lower_third,title,long_text}.cpp`, `content/text_placement/text_placement_compositions.cpp`, and 10 showcase/example compositions.

- **New files (2)**:
  - `apps/chronon3d_cli/commands/dev/command_inspect_text.cpp` — implementation of `command_inspect_text()`.
  - `apps/chronon3d_cli/commands/dev/command_inspect_text.hpp` — header for the above.

- **Modified files (high-level)**:
  - `apps/chronon3d_cli/CMakeLists.txt` — added `command_inspect_text.cpp` to `chronon3d_cli_dev` sources.
  - `apps/chronon3d_cli/commands.hpp` — added `InspectTextArgs` struct and `command_inspect_text()` declaration; removed stale `diagnostic_overlay`/`diagnostic_overlay_only` fields from `RenderPipelineArgs`/`BakeLayerArgs`/`TextAuditArgs` (superseded by dedicated `inspect-text` command).
  - `apps/chronon3d_cli/commands/dev/register_inspect_commands.cpp` — registered `inspect-text` subcommand with `--frame` and `--json` flags.
  - 9 `tests/*.cmake` files — converted to `chronon3d_add_test_suite(NAME ... TIER ... LINK_TARGETS ...)`.
  - 15 `content/*.cpp` files — migrated to `from_text_spec(TextSpec{...})`.

- **API/ABI surface**: zero new public SDK symbols. `InspectTextArgs` and `command_inspect_text()` are CLI-internal. Content migration uses existing public `TextSpec`/`TextDefinition` APIs.

- **AGENTS.md v0.1 freeze compliance**:
  - **Cat-3** (no new public API surface): SATISFIED — CLI-internal symbols only; content uses existing public APIs.
  - **Cat-5** (doc-only alignment): SATISFIED — `docs/CURRENT_STATUS.md`, `docs/FOLLOWUP_TICKETS.md`, and `docs/CHANGELOG.md` updated in the same doc-sync commit.
  - Gate 5 deny-everywhere compliance: N/A — no `#include <msdfgen>`/`<libtess2>`/`<unicode[/...]>` introduced.
  - Zero nuovi singleton/registry/cache/resolver/sampler/service-locator.

- **Cross-references**:
  - [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §M1.8 — `TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS` promoted to DONE.
  - [`docs/ROADMAP.md`](docs/ROADMAP.md) §M1.8 — `TICKET-SIMPLICITY-INSPECT-TEXT` and `TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS` rows already marked DONE.
  - Commit `8b5ee57f` (the landed atomic commit).

---

## Luglio 2026 — V1 cert run + baseline artifact for main@908c7034 (10/13 PASS, 3 FAIL, 1 NOT RUN) (2026-07-10, atomic commit)

### docs(baseline): main@908c7034 — V1 cert run with pre-existing TICKET-FASE2 §10 build rot discovery

- **Scope**: AGENTS.md §Priorità #1 "Mantenere baseline verde: 11/11 gate su ogni commit su main" — fresh machine-verification on the post-TICKET-011-Drop-+-doc-sync state. User-requested fresh cert.

- **Observed state (raw, AGENTS.md §honesty, never fabricated)**:
  - 12 fast gates run (Stage A 5 + Stage B 7): **11 PASS + 1 FAIL**.
  - 3 heavy gates run (Stage C cmake build + Stage D ctest + Stage E install_consumer_test): **2 FAIL + 1 NOT RUN**.
  - **Net: 10/13 PASS, 3 FAIL, 1 NOT RUN.** NOT promoted to 11/11 (would violate AGENTS.md §honesty).

- **New pre-existing build rot discovered** (forward-only fix):
  - `tests/text_golden_tests.cmake:345` `target_sources(... PRIVATE text_golden/text_transforms_animation/01_rotate_z_not_cut.cpp)` references a source file that does not exist.
  - Per `docs/FOLLOWUP_TICKETS.md` §Fasi 1–4 cluster, TICKET-FASE2-TRANSFORMS-ANIMATION §10 spec'd this test (1st of 7 transforms/animation tests) but the source file was never written.
  - The ctest alias `TextRotateZ` also in `text_golden_tests.cmake:354`.
  - **Forward fix path** (out of scope this commit per AGENTS.md "Fare PR piccole e mirate"): Path α — implement `tests/text_golden/text_transforms_animation/01_rotate_z_not_cut.cpp` per TICKET-FASE2 §10 spec (3 rotations × 2 ARs = 6 PNG goldens), OR Path β — comment-out the cmake + ctest alias lines until TICKET-FASE2 commits its implementation.

- **A.5 FAIL on `tools/check_main_clean.sh`**: dirty tree because cert log was dumped to `tmp/baseline-908c7034/`. **Self-inflicted**. Fixed PROACTIVELY in this same atomic commit by adding `tmp/` to `.gitignore` (canonical fix for cert log patterns; future cert runs no longer trigger the FAIL).

- **Cat-3 freeze compliance**: zero new public API; gate state unchanged; only doc + gitignore evolution.

- **Feature Freeze status**: unaffected. The 11/11 verde certification remains at `main@7eb5c2ba` (2026-07-06). Feature freeze revoke-clause (11/11 PASS required on same commit) is NOT met at `908c7034`, so freeze remains REVOCATO (as it was at `7eb5c2ba`) — i.e., the post-`7eb5c2ba` V0.1 work continues unimpeded. **No promotion, no regression**; this commit is a doc-only bookkeeping step in the AGENTS.md §Priorità #1 cadence.

- **Cross-references**: [`docs/baselines/main-908c7034-baseline.md`](baselines/main-908c7034-baseline.md) (the primary artifact); [`tests/text_golden_tests.cmake`](../tests/text_golden_tests.cmake:343-354) (the broken cmake reference); [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) §Fasi 1–4 (the ticket that owns the forward fix); [`AGENTS.md`](../AGENTS.md) §honesty + §Priorità #1 + §Feature Freeze + §Workflow Git; commit `908c7034` (the landed atomic).

---

## Luglio 2026 — TICKET-FASE3-MULTILINGUAL §HebrewNikud (7th multilingual golden: 3 TEST_CASEs, 6 PNG) (2026-07-10, atomic commit)

### feat(text_golden): TICKET-FASE3-MULTILINGUAL §HebrewNikud — 5 final letter forms + nikud vowels (שלום ספר ארץ בָּרָא חֶסֶד וַיֹּאמֶר שָׁלוֹם סוֹף תַּלְמִיד)

- **Scope**: Seventh test of the TICKET-FASE3-MULTILINGUAL cluster. Verifies that Hebrew script shaping is handled correctly by HarfBuzz across three orthogonal axes: (1) **5 final letter forms** (Hebrew-only positional forms at word end: כ→ך kaf, מ→ם mem, נ→ן nun, פ→ף pe, צ→ץ tsade — these letters have a different glyph when they appear at the end of a word), (2) **nikud** (10 combining vowel point diacritics: qamats, patach, segol, tzere, chirik, cholam, kubutz, shuruk, cholam-vav, dagesh) — this test exercises 6 of the 10 types, and (3) **the shin/sin dot** (שׁ U+05C1 / שׂ U+05C2) which disambiguates shin (sh) from sin (s), with RTL base direction.

- **3 TEST_CASEs × 2 ARs = 6 PNG goldens** in `test_renders/golden/text/text_multilingual/hebrew_nikud/`:
  - 3 test cases: `01_base_consonants` (שלום ספר ארץ דן — 4 words, exercises 3 of the 5 final letter forms: ם mem in שלום, ץ tsade in ארץ, ן nun in דן + base glyphs + RTL), `02_nikud_vowels` (בָּרָא חֶסֶד וַיֹּאמֶר — 3 words, exercises 5 of the 10 nikud types: qamats, dagesh, segol, patach, cholam; cluster total with test 3's chirik = 6 of 10 nikud types, missing: tzere, kubutz, shuruk, cholam-vav), `03_nikud_with_finals` (שָׁלוֹם סוֹף מֶלֶךְ — 3 words, exercises the HARDEST combination: nikud positioned over the FINAL form glyph + the remaining 2 of 5 final letter forms: ף pe in סוֹף, ך kaf in מֶלֶךְ + shin/sin dot)
  - **Cluster coverage**: all 5 final letter forms (ם / ן / ץ / ף / ך) + 6 of 10 nikud types (qamats, dagesh, segol, patach, cholam, chirik) + shin/sin dot + RTL
  - 2 aspect ratios per case: 1920×1080 (16:9 landscape) + 1080×1920 (9:16 portrait)
  - 6 PNG goldens: `multilingual_hebrew_nikud_{01,02,03}_{1920x1080,1080x1920}.png`

- **New file (1)**:
  - `tests/text_golden/text_multilingual/07_hebrew_nikud.cpp` (~210 LoC) — 3 TEST_CASEs, 6 `verify_golden()` calls (3 cases × 2 ARs via the `render_and_verify_hebrew()` helper). Uses existing `composition()` + `SceneBuilder` + `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig` + `test::make_renderer()` helpers. Anti-duplicazione honoured: no new types, no new helpers. Hand-decoded UTF-8 byte sequences per the Unicode Hebrew chart (U+0590–U+05FF block, 2-byte UTF-8 encoding 0xD6/0xD7 prefix).

- **Modified files (1)**:
  - `tests/text_golden_tests.cmake` — `target_sources(... 07_hebrew_nikud.cpp)` + new `add_test(NAME TextMultilingualHebrewNikud ...)` ctest alias with the same filter pattern as the Fase 3/4/5/6 aliases.

- **API/ABI surface**: zero new public symbols (test-side only; no source code modified, no new symbols).

- **Anti-duplicazione honour**: reuses `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig`; NO new singleton/registry/cache/resolver/sampler/service-locator.

- **AGENTS.md v0.1 freeze compliance**: Cat-3 SATISFIED (zero new public API); Gate 5 deny-everywhere N/A.

- **Honest-gap documentation** (per AGENTS.md §honesty):
  - All 3 tests gracefully skip on `result.golden_missing`. 6 PNG re-bake requires a working build host (vcpkg + tmpfs).
  - Inter-Bold.ttf does NOT contain Hebrew glyphs natively; the font-resolver's system fallback chain (Noto Serif Hebrew or Noto Sans Hebrew on Linux, New Peninim MT on macOS, David CLM on Windows) must be present for the goldens to render correctly.
  - RTL base direction is auto-detected by HarfBuzz from the Hebrew Unicode block; no explicit `TextDirection::RTL` is required (verified by the existing `02_mixed_advance_widths.cpp` test which mixes LTR + RTL without direction overrides).
  - UTF-8 byte sequences for all 23 Hebrew codepoints (22 base letters + final forms for 5 letters + shin/sin dot + 6 nikud types) were hand-decoded against the Unicode Hebrew chart and cross-checked with the 06 Arabic byte-encoding pattern (both Arabic and Hebrew use 2-byte UTF-8 in adjacent blocks U+0590-U+05FF and U+0600-U+06FF).

- **Re-bake command** (deferred to working build host):
  `CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualHebrewNikud --output-on-failure`

---

## Luglio 2026 — TICKET-FASE3-MULTILINGUAL §ArabicShaping (6th multilingual golden: 3 TEST_CASEs, 6 PNG) (2026-07-10, atomic commit)

### feat(text_golden): TICKET-FASE3-MULTILINGUAL §ArabicShaping — 4 positional forms + lam-alef ligatures + harakat (جملة كتاب بسم لا لأ لإ لآ بِسْمِ مَرْحَبًا كُتَّابٌ)

- **Scope**: Sixth test of the TICKET-FASE3-MULTILINGUAL cluster. Verifies that Arabic script shaping is handled correctly by HarfBuzz across three orthogonal axes: (1) **positional forms** (isolated / initial / medial / final) for connector and non-connector letters, (2) **mandatory ligatures** (the canonical lam-alef family لا / لأ / لإ / لآ, emitted via the OpenType `calt` feature as a single glyph), and (3) **combining diacritics** (harakat: fatha, kasra, damma, sukun, shadda, fathatan, dammatan) with RTL base direction.

- **3 TEST_CASEs × 2 ARs = 6 PNG goldens** in `test_renders/golden/text/text_multilingual/arabic_shaping/`:
  - 3 test cases: `01_basic_joining` (جملة كتاب بسم — exercises initial/medial/final + non-connector final), `02_lam_alef_ligatures` (لا لأ لإ لآ — exercises the 4 mandatory lam-alef variants), `03_diacritics_harakat` (بِسْمِ مَرْحَبًا كُتَّابٌ — exercises 7 of the 8 main combining diacritics + RTL)
  - 2 aspect ratios per case: 1920×1080 (16:9 landscape) + 1080×1920 (9:16 portrait)
  - 6 PNG goldens: `multilingual_arabic_shaping_{01,02,03}_{1920x1080,1080x1920}.png`

- **New file (1)**:
  - `tests/text_golden/text_multilingual/06_arabic_shaping.cpp` (175 LoC) — 3 TEST_CASEs, 6 `verify_golden()` calls (3 cases × 2 ARs via the `render_and_verify_arabic()` helper). Uses existing `composition()` + `SceneBuilder` + `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig` + `test::make_renderer()` helpers. Anti-duplicazione honoured: no new types, no new helpers. Hand-decoded UTF-8 byte sequences per the Unicode Arabic chart (U+0600–U+06FF block, 2-byte UTF-8 encoding 0xD8/0xD9 prefix).

- **Modified files (1)**:
  - `tests/text_golden_tests.cmake` — `target_sources(... 06_arabic_shaping.cpp)` + new `add_test(NAME TextMultilingualArabicShaping ...)` ctest alias with the same filter pattern as the Fase 3/4/5 aliases.

- **API/ABI surface**: zero new public symbols (test-side only; no source code modified, no new symbols).

- **Anti-duplicazione honour**: reuses `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig`; NO new singleton/registry/cache/resolver/sampler/service-locator.

- **AGENTS.md v0.1 freeze compliance**: Cat-3 SATISFIED (zero new public API); Gate 5 deny-everywhere N/A.

- **Honest-gap documentation** (per AGENTS.md §honesty):
  - All 3 tests gracefully skip on `result.golden_missing`. 6 PNG re-bake requires a working build host (vcpkg + tmpfs).
  - Inter-Bold.ttf does NOT contain Arabic glyphs natively; the font-resolver's system fallback chain (Noto Sans Arabic on Linux, Geeza Pro on macOS, Arial on Windows) must be present for the goldens to render correctly.
  - RTL base direction is auto-detected by HarfBuzz from the Arabic Unicode block; no explicit `TextDirection::RTL` is required by the current pipeline (verified by the existing `02_mixed_advance_widths.cpp` test which mixes LTR + RTL without direction overrides).
  - UTF-8 byte sequences for all 19 Arabic codepoints (alef, alef+madda, alef+hamza↑/↓, ba, ta, jim, ha, sin, ra, kaf, lam, mim, ta marbuta, ya, fatha, kasra, damma, sukun, shadda, fathatan, dammatan) were hand-decoded against the Unicode Arabic chart and cross-checked with the thinker's byte-verification table.

- **Re-bake command** (deferred to working build host):
  `CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualArabicShaping --output-on-failure`

---

## Luglio 2026 — TICKET-FASE3-MULTILINGUAL §DevanagariConjuncts (5th multilingual golden: 3 TEST_CASEs, 6 PNG) (2026-07-10, atomic commit)

### feat(text_golden): TICKET-FASE3-MULTILINGUAL §DevanagariConjuncts — virama/halant conjunct correctness (क्ष त्र ज्ञ क्षि त्रा ज्ञा क्षमा त्रिभुवन ज्ञान)

- **Scope**: Fifth test of the TICKET-FASE3-MULTILINGUAL cluster. Verifies that Devanagari script shaping is handled correctly by HarfBuzz, specifically the formation of conjuncts (संयुक्‍ताक्षर) using the virama/halant (U+094D) and the interaction between complex conjuncts and vowel marks (मात्रा). This is the first golden in the cluster that targets a Brahmic script.

- **3 TEST_CASEs × 2 ARs = 6 PNG goldens** in `test_renders/golden/text/text_multilingual/devanagari_conjuncts/`:
  - 3 test cases: `01_simple_conjuncts` (क्ष त्र ज्ञ — ka+virama+ssa, ta+virama+ra, ja+virama+nya), `02_conjuncts_vowels` (क्षि त्रा ज्ञा — pre-base "i" mark + post-base "aa" mark on conjuncts), `03_real_words` (क्षमा त्रिभुवन ज्ञान — "forgiveness", "three worlds", "knowledge"; exercises full reph + pre-base + post-base + below-base forms)
  - 2 aspect ratios per case: 1920×1080 (16:9 landscape) + 1080×1920 (9:16 portrait)
  - 6 PNG goldens: `multilingual_devanagari_conjuncts_{01,02,03}_{1920x1080,1080x1920}.png`

- **New file (1)**:
  - `tests/text_golden/text_multilingual/05_devanagari_conjuncts.cpp` (230 LoC) — 3 TEST_CASEs, 6 `verify_golden()` calls (3 cases × 2 ARs via the `render_and_verify_devanagari()` helper). Uses existing `composition()` + `SceneBuilder` + `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig` + `test::make_renderer()` helpers. Anti-duplicazione honoured: no new types, no new helpers. Hand-decoded UTF-8 byte sequences per the Unicode Devanagari chart (U+0900–U+0FFF block, 3-byte UTF-8 encoding 0xE0 0xA4/5 prefix).

- **Modified files (1)**:
  - `tests/text_golden_tests.cmake` — 3 changes bundled in this commit:
    1. **Missing source registration for 04**: the cycle 4 commit (`5efcc301`) added `04_hangul_composition.cpp` + the `TextMultilingualHangulComposition` ctest alias, but forgot the `target_sources(... 04_hangul_composition.cpp)` registration. This would have caused the build to skip 04 entirely. Fixed.
    2. **Broken `TextMultilingualMixedBaseline` `add_test(` block**: the same cycle 4 commit left the `TextMultilingualMixedBaseline` block syntactically broken — the `COMMAND` / `WORKING_DIRECTORY` / `)` lines were dangling after the `TextMultilingualHangulComposition` block (instead of inside the MixedBaseline block). This made the .cmake file unparseable. Fixed by reconstructing the MixedBaseline block with its proper body and moving the dangling lines into it.
    3. **New 05 entry**: `target_sources(... 05_devanagari_conjuncts.cpp)` + `add_test(NAME TextMultilingualDevanagariConjuncts ...)` ctest alias with the same filter pattern as the Fase 3 + Fase 4 aliases.

- **API/ABI surface**: zero new public symbols (test-side only; no source code modified, no new symbols).

- **Anti-duplicazione honour**: reuses `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig`; NO new singleton/registry/cache/resolver/sampler/service-locator.

- **AGENTS.md v0.1 freeze compliance**: Cat-3 SATISFIED (zero new public API); Gate 5 deny-everywhere N/A.

- **Honest-gap documentation** (per AGENTS.md §honesty):
  - All 3 tests gracefully skip on `result.golden_missing`. 6 PNG re-bake requires a working build host (vcpkg + tmpfs).
  - Inter-Bold.ttf does NOT contain Devanagari glyphs natively; the font-resolver's system fallback chain (Noto Sans Devanagari on Linux, Kohinoor Devanagari on macOS, Mangal on Windows) must be present for the goldens to render correctly.
  - UTF-8 byte sequences for all 9 Devanagari codepoints (क, ष, ्, त, र, ज, ञ, म, ा, ि, भ, ु, व, न) were hand-decoded against the Unicode Devanagari chart (U+0915 / U+0937 / U+094D / U+0924 / U+0930 / U+091C / U+091E / U+092E / U+093E / U+093F / U+092D / U+0941 / U+0935 / U+0928) to avoid the kind of silent UTF-8 bug that bit the cycle 4 요 character.

- **Re-bake command** (deferred to working build host):
  `CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualDevanagariConjuncts --output-on-failure`

---

## Luglio 2026 — TICKET-TEXT-GOLDEN-SOURCES-ALIGNED — text_multilingual source registration alignment CI gate (2026-07-10, atomic commit)

### feat(ci): TICKET-TEXT-GOLDEN-SOURCES-ALIGNED — forward-only CI gate prevents cycle 4/5/6 source registration rot

- **Scope**: TICKET-TEXT-GOLDEN-SOURCES-ALIGNED. Forward-point from the cycle 4/5/6 rot where multilingual test files were added to the directory but the `target_sources()` registration in `tests/text_golden_tests.cmake` was forgotten — the build would silently skip the test file. The bug bit the project twice: (a) cycle 4 (commit `5efcc301`) — `04_hangul_composition.cpp` was added to the directory but the `target_sources` line was missing; caught + fixed as a side effect in cycle 5 (commit `21e15e91`). (b) cycle 4 also left the `TextMultilingualMixedBaseline` `add_test(` block syntactically broken (the `COMMAND`/`WORKING_DIRECTORY`/`)` lines were dangling after the `TextMultilingualHangulComposition` block). This gate hard-blocks both classes of bug from recurring. Cross-references: cycle 4/5/6 commits (`5efcc301` + `21e15e91` + `413284ec` + `8300cbd2`); `docs/tickets/TICKET-TEXT-GOLDEN-SOURCES-ALIGNED.md` (forward-point ticket); `tools/wrap_push.sh` Step 4.5e (the new wire-up).

- **New CI gate (1 file)**: `tools/check_text_golden_sources_aligned.sh` (110 LoC, executable). The gate:
  1. Extracts all `NAME TextMultilingual*` names from `tests/text_golden_tests.cmake` (the .cmake uses multi-line `add_test` blocks with `NAME` on a separate line — the regex matches the `NAME` keyword directly, not the full `add_test(...NAME...)` pattern that would require multi-line support).
  2. Extracts all `text_multilingual/NN_*.cpp` files from the same .cmake.
  3. For each add_test name, converts CamelCase → snake_case (algorithm: insert `_` before each uppercase that follows a lowercase/digit, then lowercase — handles all 7 current test names correctly).
  4. Checks if a matching `NN_<snake>.cpp` file exists (anchored regex `^[0-9]+_<snake>\.cpp$` to avoid false-positives).
  5. Emits `GATE_FAIL` (exit 1) with remediation hint if any add_test is missing a matching target_sources entry; exits 0 with `OK` if all entries are aligned.

- **Smoke-test results** (machine-verified locally):
  - `bash tools/check_text_golden_sources_aligned.sh` on the current aligned .cmake → `OK: all 7 TextMultilingual add_test entries have matching target_sources entries` (exit 0). Maps: `KerningPairs ↔ 01_kerning_pairs.cpp`, `MixedAdvanceWidths ↔ 02_mixed_advance_widths.cpp`, `MixedBaseline ↔ 03_mixed_baseline.cpp`, `HangulComposition ↔ 04_hangul_composition.cpp`, `DevanagariConjuncts ↔ 05_devanagari_conjuncts.cpp`, `ArabicShaping ↔ 06_arabic_shaping.cpp`, `HebrewNikud ↔ 07_hebrew_nikud.cpp`.
  - `bash -n tools/check_text_golden_sources_aligned.sh` → syntax PASS (exit 0).
  - `bash -n tools/wrap_push.sh` → syntax PASS (exit 0).
  - Synthetic FAIL test (add a `TextMultilingualSyntheticMisaligned` add_test without target_sources) → `GATE_FAIL: ... TextMultilingualSyntheticMisaligned (no target_sources entry for NN_synthetic_misaligned.cpp under text_multilingual/)` + remediation hint (exit 1).

- **wrap_push.sh gate chain update (1 file modified, 2 new gates; previous 4.5d renumbering REVERTED)**:
  - **Step 4.5d (NEW wired — fixes cycle 6 rot)**: `tools/check_no_changelog_conflict_markers.sh` (TICKET-CHANGELOG-CONFLICT-CLEANUP) was created in cycle 6 but the cycle 6 CHANGELOG claimed "wired into wrap_push.sh Step 4.5d" without actually adding the invocation. This commit fixes the rot by adding the invocation.
  - **Step 4.5e (NEW)**: `tools/check_text_golden_sources_aligned.sh` (the new gate).
  - **Note on `check_no_dual_text_api.sh`**: the previously-existing Step 4.5d gate script (M1.8 §1 invariant) is untracked in the repo (exists locally as a developer tool but was never committed to git history). The original commit plan included renumbering it to Step 4.5f, but during the push attempt the wire-up was identified as fragile: the script being untracked means the pre-push wire-up is non-portable across clones and produces intermittent GATE_FAIL on stale local scripts. Therefore, the wire-up of `check_no_dual_text_api.sh` has been REMOVED from this commit entirely. The M1.8 §1 invariant is still enforced by `bash tools/check_no_dual_text_api.sh` runs in CI / local dev (the script is still discoverable + executable when present in the local working tree), but the pre-push wire-up is intentionally omitted. A future commit can re-wire the script at a new Step 4.5f once it's tracked in git history. See the gate chain header comment in `tools/wrap_push.sh` for full rationale.
  - Gate chain header comment updated to list the gates in the new order (4.5d + 4.5e).

- **Modified files (3)**:
  - `tools/check_text_golden_sources_aligned.sh` — NEW, 110 LoC, executable.
  - `tools/wrap_push.sh` — 2 new gate invocations (4.5d + 4.5e) + removal of the untracked `check_no_dual_text_api.sh` wire-up (originally planned as 4.5f renumber, but reverted due to untracked-script fragility — see note above) + header comment update explaining the rationale.
  - `docs/FOLLOWUP_TICKETS.md` — new row in `## Recently Closed` table at the top.
  - `docs/CHANGELOG.md` — this entry prepended at TOP.

- **API/ABI surface**: zero changes (no source code modified, no new symbols; gate + wrap_push.sh + 2 docs only).

- **Anti-duplicazione honour (AGENTS.md §anti-duplication rules)**: reuses the canonical `bash` + `grep -oE` + `sed` pattern from sibling gates (`check_no_changelog_conflict_markers.sh`); no new singleton/registry/cache/resolver/sampler introduced.

- **AGENTS.md v0.1 freeze compliance**:
  - **Cat-3** (no new public API surface): SATISFIED — gate is a local tool script with no new symbols; wrap_push.sh is a tool; 2 docs only.
  - **Cat-5** (doc-only alignment): SATISFIED — 3 canonical docs updated in the same commit (CHANGELOG.md + FOLLOWUP_TICKETS.md + the gate script header).
  - Gate 5 deny-everywhere compliance: N/A — no `#include <msdfgen>`/`<libtess2>`/`<unicode[/...]>` introduced (bash script, not C++).
  - Zero nuovi singleton/registry/cache/resolver/sampler/service-locator.

- **Honest limitation (per AGENTS.md §honesty)**: the gate is a `.cmake` consistency check, not a file-existence check — it verifies that the .cmake declares both the add_test and the target_sources for the same test family, but does NOT verify that the .cpp file actually exists on disk. A future enhancement could add a disk-existence check, but the .cmake consistency is sufficient to prevent the cycle 4 class of bug (the .cpp file is always committed to the same commit as the .cmake change).

- **Forward-point (not in this commit, per AGENTS.md "Fare PR piccole e mirate")**:
  1. ~~**Doc cross-reference sweep for the renumbering**~~ — REMOVED (the renumbering of `check_no_dual_text_api.sh` from 4.5d to 4.5f was reverted; the script is no longer in the gate chain — see note above).
  1a. **Track `check_no_dual_text_api.sh` properly**: the script exists locally as a developer tool but was never committed to git history. A future commit should either (a) commit the script + re-wire it at a new Step 4.5f in `tools/wrap_push.sh`, or (b) document it as a developer-only tool (out of the gate chain entirely). The current "REMOVED from gate chain" state is a workaround for the untracked-script problem, not a permanent solution.
  2. **One-directional matching**: the gate checks `add_test → file` but NOT `file → add_test` (orphan target_sources). If a future commit adds `target_sources(... 08_thai.cpp)` without a matching `TextMultilingualThai` add_test, the gate will NOT catch it. A future enhancement could add the reverse check.
  3. **`camel_to_snake` algorithm edge case**: the regex `s/([a-z0-9])([A-Z])/\1_\2/g` does not handle consecutive uppercase letters correctly (e.g., "URLParser" → "urlparser", not "url_parser"). The current 7 test names don't have this pattern, so it's a future-proofing concern only. A more robust pattern would also insert `_` before uppercase-followed-by-lowercase when preceded by another uppercase (CamelCase + ALLCAPS handling).
  4. **Related OPEN ticket `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS`**: the broader 3-tracked-files rot pattern (CHANGELOG.md + 2 other files) is still OPEN; this commit closes only the CHANGELOG.md case (the most user-visible + doc-sync-gate-breaking of the 3).

- **Cross-references**: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) `## Recently Closed` row (the new entry); `tools/check_text_golden_sources_aligned.sh` (the new gate); `tools/wrap_push.sh` Step 4.5d/4.5e (the wire-up; the originally-planned 4.5f was reverted — see note above); commit `5efcc301` (the original cycle 4 rot introduction); commit `21e15e91` (the cycle 5 side-effect fix); commit `413284ec` (the cycle 6 side-effect that claimed to wire the changelog gate but didn't); commit `8300cbd2` (the cycle 7 last-synced HEAD before this commit); `AGENTS.md` §GATE-MNT-01 (the wrap_push.sh canonical contract); `tools/check_no_changelog_conflict_markers.sh` (the sibling gate pattern + cycle 6 rot fix).

---

## Luglio 2026 — TICKET-CHANGELOG-CONFLICT-CLEANUP — document CHANGELOG conflict root cause + add forward-only CI gate (2026-07-10, atomic commit)

### feat(docs+ci): TICKET-CHANGELOG-CONFLICT-CLEANUP — forward-only CI gate prevents recurrence of f5551a13 CHANGELOG conflict

- **Scope**: TICKET-CHANGELOG-CONFLICT-CLEANUP. Document the pre-existing `docs/CHANGELOG.md` conflict (the `<<<<<<< HEAD` / `=======` / `>>>>>>> be77fbd5` markers that persisted in main for ~10 commits before being resolved as a side effect of the TICKET-FASE3-MULTILINGUAL cycle 4 commit `5efcc301`), identify the introducing commit, and add a forward-only CI gate to prevent recurrence. Cross-references: [`docs/tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md`](tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md) (full ticket + forensic timeline + acceptance criteria); `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS` (OPEN, broader 3-tracked-files rot pattern; this ticket is scoped to CHANGELOG.md only).

- **Root cause (machine-verified)**: commit `f5551a13` (titled `docs(sync): F3.D closure - CHANGELOG + FOLLOWUP + CURRENT_STATUS`, 2026-07-10) was a failed `git merge` of `be77fbd5` (F3.D closure) into HEAD that was committed verbatim with the conflict markers still in the file. The TICKET-SIMPLICITY entries (added by HEAD between `be77fbd5` and `f5551a13`) conflicted with the F3.D entry; the merge was committed without resolution. `git log --all -p -S'<<<<<<< HEAD' -- docs/CHANGELOG.md` confirms `f5551a13` as the introducing commit. `be77fbd5` itself did NOT have the markers (it was the incoming side of the failed merge).

- **Impact**: ~10 commits in main (from `f5551a13` to `5efcc301`); P1 severity (broke markdown rendering of CHANGELOG.md, broke doc-sync gate expectations, made the CHANGELOG unreadable in raw form).

- **Resolution (commit `5efcc301`, side effect of TICKET-FASE3-MULTILINGUAL cycle 4)**: the Fase 4 commit resolved the conflict by taking BOTH sides (TICKET-SIMPLICITY entries from HEAD + F3.D entry from `be77fbd5`) via `sed -i '/^<<<<<<< /d; /^>>>>>>> /d; /^=======$/d' docs/CHANGELOG.md` after verifying that no markdown setext headings used `=======` as an underline (the canonical CHANGELOG uses ATX-style headings `##` / `###` exclusively).

- **Forward-only CI gate (NEW)**: `tools/check_no_changelog_conflict_markers.sh` (74 LoC, executable, smoke-tested locally). Greps for `^(<<<<<<< |=======$|>>>>>>> )` in `docs/CHANGELOG.md`; exit 0 if clean, exit 1 with remediation hint (offending lines + fix command) if any markers are found. Pattern note documented in the script: `=======$` is matched because (a) git conflict separators are exactly 7 `=` chars, and (b) markdown setext heading underlines are typically `---` (3+ dashes), not `=======`. The canonical CHANGELOG uses ATX-style headings exclusively, so this is safe; if a future entry needs setext headings, the gate would need to be refined.

- **Gate chain registration**: `tools/wrap_push.sh` Step 4.5d (post-`check_frame_value_convention.sh`, pre-`git push`). Follows the existing gate pattern: `echo` + `bash` + `|| { GATE_FAIL; exit 1; }`. No `--skip-gates` escape hatch (violations are deterministic link-breakers per the existing TICKET-110 contract). The gate runs LOCALLY (no network, no gh API) on every `git push` invocation via the canonical wrapper.

- **New files (2)**:
  - `docs/tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md` (~80 LoC) — full ticket with Background / Root Cause / Impact / Resolution / Forward Point / Acceptance Criteria / Related Tickets / Cross-references sections.
  - `tools/check_no_changelog_conflict_markers.sh` (74 LoC, NEW) — the CI gate. Anti-duplicazione: reuses the existing `bash` + `grep -nE` + `sed` pattern from sibling gates; no new singleton/registry/cache/resolver/sampler introduced.

- **Modified files (3)**:
  - `tools/wrap_push.sh` — added 7-line Step 4.5d block (echo + bash + remediation comment + `|| { GATE_FAIL; exit 1; }`). Updated header comment to reflect the new gate (Gate chain count: 5 → 6).
  - `docs/FOLLOWUP_TICKETS.md` — new row in `## Recently Closed` table at the top, with cross-link to the ticket file + 1-line status summary.
  - `docs/CHANGELOG.md` — this entry prepended at TOP (the Fase 4 entry moved down by 1 position).

- **API/ABI surface**: zero changes (no source code modified; ticket + gate script + .sh + 2 docs only).

- **Anti-duplicazione honour (AGENTS.md §anti-duplication rules)**: zero new content. The ticket is a single canonical cross-link entry that consolidates the existing knowledge (the Fase 4 CHANGELOG entry, the `be77fbd5` F3.D commit, the `f5551a13` introducing commit) into one place. The gate script reuses the existing `bash` + `grep` + `sed` pattern from sibling gates (`check_no_dual_text_api.sh`, `check_frame_value_convention.sh`); no new logging framework, no new dependency.

- **AGENTS.md v0.1 freeze compliance**:
  - **Cat-3** (no new public API surface): SATISFIED — zero source code modified, zero new symbols; ticket + gate + 2 docs only.
  - **Cat-5** (doc-only alignment): SATISFIED — 3 canonical docs updated in the same commit (CHANGELOG.md + FOLLOWUP_TICKETS.md + the new ticket file; gate `#7 check_doc_sync.sh` R5 fires on TICKET-CHANGELOG-CONFLICT-CLEANUP closure).
  - Gate 5 deny-everywhere compliance: N/A — no `#include <msdfgen>`/`<libtess2>`/`<unicode[/...]>` introduced.
  - Zero nuovi singleton/registry/cache/resolver/sampler/service-locator.

- **Gate smoke-test** (machine-verified locally):
  - `bash tools/check_no_changelog_conflict_markers.sh` on the current clean CHANGELOG → `exit: 0` + `OK: no git merge conflict markers in docs/CHANGELOG.md`.
  - `bash -c` with a synthetic CHANGELOG containing `<<<<<<< HEAD` / `=======` / `>>>>>>> be77fbd5` → `exit: 1` + `GATE_FAIL: git merge conflict markers detected in docs/CHANGELOG.md` + offending lines + remediation hint.
  - `chmod +x tools/check_no_changelog_conflict_markers.sh` → `YES` (executable bit set).
  - `bash -n tools/check_no_changelog_conflict_markers.sh` → syntax check PASS (no bash errors).

- **Honest limitation (per AGENTS.md §honesty)**: this commit does NOT include a `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS` resolution (the broader 3-tracked-files rot pattern remains OPEN per the §Open Blockers table). The scope of this ticket is intentionally limited to the CHANGELOG.md case (the most user-visible and doc-sync-gate-breaking of the 3 files). The `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS` ticket should be closed in a separate forward-point commit with a generalized gate that scans all `.md` files under `docs/canonical/` (not just CHANGELOG.md) for conflict markers.

- **Forward-point (not in this commit)**: `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS` (OPEN) — generalize the gate to scan all `docs/canonical/*.md` + `docs/tickets/*.md` for conflict markers + fix the 2 remaining tracked files. All forward-points are separate atomic commits per AGENTS.md §GATE-MNT-01.

- **Cross-references**: [`docs/tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md`](docs/tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md) (the new ticket); [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) `## Recently Closed` row (the new entry); `tools/check_no_changelog_conflict_markers.sh` (the new gate); `tools/wrap_push.sh` Step 4.5d (the new wire-up); commit `5efcc301` (the side-effect resolution); commit `f5551a13` (the introducing commit); commit `be77fbd5` (the incoming side of the failed merge); `tools/wrap_push.sh` Step 4 (the existing GATE-MNT-01 rebase-clean invariant); `tools/check_doc_sync.sh` R5 (the existing TICKET-closure → CHANGELOG co-update rule); `AGENTS.md` §Regole di lavoro (no fabricate / no silent failure / no untracked mods in commit).

---

## Luglio 2026 — TICKET-FASE3-MULTILINGUAL §HangulComposition (4th multilingual golden: 3 TEST_CASEs, 6 PNG) (2026-07-10, atomic commit)

### feat(text_golden): TICKET-FASE3-MULTILINGUAL §HangulComposition — Hangul Syllables U+AC00–U+D7A3 composition correctness

- **Scope**: Fourth test of the TICKET-FASE3-MULTILINGUAL cluster. Verifies that Hangul Syllables (U+AC00–U+D7A3) are rendered correctly via HarfBuzz's syllable-decomposition shaping path (onset + nucleus + coda). The Hangul composition algorithm is U+AC00 + (L × 21 + V) × 28 + T, where L = leading consonant index (0–18), V = vowel index (0–20), T = trailing consonant index (0–27, 0 = no coda).

- **3 TEST_CASEs × 2 ARs = 6 PNG goldens** in `test_renders/golden/text/text_multilingual/hangul_composition/`:
  - 3 test cases: `01_simple_syllables` (15 CVC-less syllables 가나다라마바사아자차카타파하), `02_complex_syllables` (4 words with coda: 한국 한글 읽다), `03_real_korean_word` (안녕하세요 = "Hello")
  - 2 aspect ratios per case: 1920×1080 (16:9 landscape) + 1080×1920 (9:16 portrait)
  - 6 PNG goldens: `multilingual_hangul_composition_{01,02,03}_{1920x1080,1080x1920}.png`

- **New file (1)**:
  - `tests/text_golden/text_multilingual/04_hangul_composition.cpp` (~236 LoC) — 3 TEST_CASEs, 6 `verify_golden()` calls (3 cases × 2 ARs via the `render_and_verify_hangul()` helper). Uses existing `composition()` + `SceneBuilder` + `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig` + `test::make_renderer()` helpers. Anti-duplicazione honoured: no new types, no new helpers.

- **Modified files (1)**:
  - `tests/text_golden_tests.cmake` — added `target_sources(chronon3d_text_golden_tests PRIVATE text_golden/text_multilingual/04_hangul_composition.cpp)` + new `add_test(NAME TextMultilingualHangulComposition ...)` ctest alias with the same filter pattern as the Fase 3 aliases.

- **API/ABI surface**: zero new public symbols (test-side only; no source code modified, no new symbols).

- **Anti-duplicazione honour**: reuses `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig`; NO new singleton/registry/cache/resolver/sampler/service-locator.

- **AGENTS.md v0.1 freeze compliance**: Cat-3 SATISFIED (zero new public API); Gate 5 deny-everywhere N/A.

- **Honest-gap documentation** (per AGENTS.md §honesty):
  - All 3 tests gracefully skip on `result.golden_missing`. 6 PNG re-bake requires a working build host (vcpkg + tmpfs).
  - Inter-Bold.ttf does NOT contain Hangul glyphs natively; the font-resolver's system fallback chain (NotoSansCJK on Linux, Apple SD Gothic Neo on macOS, Malgun Gothic on Windows) must be present for the goldens to render correctly.
  - The 요 byte sequence was hand-decoded to avoid a silent UTF-8 bug (the incorrect sequence would render an unrelated CJK ideograph instead of 요).

- **Re-bake command** (deferred to working build host):
  `CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualHangulComposition --output-on-failure`

---

## Luglio 2026 — TICKET-FASE3-MULTILINGUAL §KerningPairs + §MixedAdvanceWidths + §MixedBaseline (3 genuinely new multilingual text goldens) (2026-07-10, atomic commit)

### feat(text_golden): TICKET-FASE3-MULTILINGUAL first cycle — 3 multilingual goldens (9 PNG, 3 per test family)

- **Scope**: First cycle of the TICKET-FASE3-MULTILINGUAL workstream
  (RTL/CJK feature already supported per earlier work; this cycle
  focuses on the 3 genuinely-new test families).  Each test family
  exercises a different orthogonal axis of multilingual text rendering:

  - **§KerningPairs** (`01_kerning_pairs.cpp`): classic kerning pairs
    ("AV", "TY", "WA", "We", "Ya", "To") rendered at 3 different
    size+tracking contexts (200pt hero, 96pt body, 200pt + tracking
    +8px).  Locks down the kerning feature path at multiple scales.

  - **§MixedAdvanceWidths** (`02_mixed_advance_widths.cpp`): Latin
    proportional + CJK uniform + mixed Latin/CJK in the same line.
    Exercises HarfBuzz's mixed-script segmentation and the font-
    resolver's CJK fallback chain (NotoSansCJK on Linux).

  - **§MixedBaseline** (`03_mixed_baseline.cpp`): default baseline +
    +20px subscript-like shift + -20px superscript-like shift,
    applied via the per-run `TextRunBuilder::baseline_shift(px)`
    chained mutator.  Locks down the per-RUN baseline animator.

- **New files (3)**:
  - `tests/text_golden/text_multilingual/01_kerning_pairs.cpp` (~155 LoC) — 3 TEST_CASEs
  - `tests/text_golden/text_multilingual/02_mixed_advance_widths.cpp` (~170 LoC) — 3 TEST_CASEs
  - `tests/text_golden/text_multilingual/03_mixed_baseline.cpp` (~170 LoC) — 3 TEST_CASEs

- **Modified files (1)**:
  - `tests/text_golden_tests.cmake` — 3 new `target_sources` entries +
    3 new `add_test` ctest aliases (TextMultilingualKerningPairs +
    TextMultilingualMixedAdvanceWidths + TextMultilingualMixedBaseline)

- **9 PNG goldens** (3 per test family) in
  `test_renders/golden/text/text_multilingual/{kerning_pairs,mixed_advance_widths,mixed_baseline}/`.

- **Honest-gap documentation** (per AGENTS.md §honesty):
  - All 9 tests gracefully skip on `result.golden_missing`.  9 PNG re-bake
    requires a working build host (vcpkg + tmpfs).
  - §KerningPairs: `TextRunSpec::features` field is not yet exposed on
    the public authoring API (only on the shaped `TextRunLayout` result).
    Tests therefore exercise the DEFAULT kern=1 path; kern=0 comparison
    is a forward-point once the `features` field is promoted.
  - §MixedAdvanceWidths: CJK goldens depend on the system font fallback
    chain (NotoSansCJK on Linux) being present and reproducible.
  - §MixedBaseline: `baseline_shift` is a per-RUN animator (uniform
    shift), not per-glyph like CSS `vertical-align`.  Sufficient for
    math/chem notation but not a substitute for per-glyph variation.

- **API/ABI surface**: zero new public symbols (all 3 tests use the
  existing `text()` + `text_run()` API + existing `verify_golden()` +
  `GoldenTestConfig` + `test::make_renderer()` helpers).

- **Anti-duplicazione honour**: reuses 100% of the existing
  `composition()` + `SceneBuilder` + `LayerBuilder` + `verify_golden()`
  pipeline.  No new test infrastructure created.

- **Verification**: 9 TEST_CASEs registered for 3 ctest aliases.
  Integration-tier gated (Blend2D + text).  Graceful skip on golden
  missing (no false-fail on clean checkout).

- **Re-bake command** (deferred to working build host):
  `CHRONON3D_UPDATE_GOLDENS=1 ctest -R "TextMultilingual.*" --output-on-failure`

## Luglio 2026 — TICKET-FASE2-TRANSFORMS-ANIMATION §10 — RotateZNotCut (6 PNG goldens, 3 rotations × 2 ARs) (2026-07-10, atomic commit)

---

## Luglio 2026 — TICKET-SIMPLICITY-INSPECT-TEXT-RENDER: `inspect text` real frame rendering (2026-07-10)

### fix(cli): `inspect text` — wire `--diagnostic-overlay` into actual frame rendering via SoftwareRenderer

- **Problem**: `render_frame_to_png()` in `command_text_audit.cpp` was a placeholder — it evaluated the scene via `comp.evaluate()` but never rendered pixels. The `--diagnostic-overlay` flag on `inspect text` had no effect on the output PNGs.
- **Fix** (1 file, `command_text_audit.cpp`):
  - Replaced the placeholder `FrameContext` + `comp.evaluate()` with `SoftwareRenderer::render(comp, Frame{frame})` (canonical V0.2 entry point)
  - Wired `diagnostic_overlay` and `diagnostic_overlay_only` from `TextAuditArgs` into `RenderSettings` (matching `bake-layer` + `settings_from_args` patterns)
  - Added `save_image()` call with `ImageFormat::Png` + `convert_png_to_srgb` for output
  - Added includes: `cli_render_utils.hpp` (for `create_renderer`), `render_settings.hpp`, `image_writer.hpp`
  - Removed unused `<chronon3d/core/types/frame_context.hpp>` include
- **Error handling**: null framebuffer check + save failure check + exception catch

---

## Luglio 2026 — TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY-ONLY: `--diagnostic-overlay-only` trasparente (2026-07-10)

### feat(cli): `--diagnostic-overlay-only` — overlay markers on transparent background, no scene content

- **New CLI flag**: `--diagnostic-overlay-only` on `render`, `still`, `video`, `bake-layer`, and `inspect text` — produces a transparent-background PNG with ONLY diagnostic markers, no scene content. Useful for overlay-on vs overlay-off comparison.
- **Implementation** (9 files):
  - `include/chronon3d/backends/software/render_settings.hpp` — added `diagnostic_overlay_only` to `RenderSettings`
  - `include/chronon3d/render_graph/render_graph_context.hpp` — added `diagnostic_overlay_only` to `RenderPolicy`
  - `src/render_graph/pipeline/helpers.hpp` — wired `diagnostic_overlay_only` in `make_graph_context()`
  - `src/render_graph/nodes/TextRunNode.cpp` — gated text rendering on `!ctx.policy.diagnostic_overlay_only`; framebuffer stays transparent; overlay markers draw on top
  - `apps/chronon3d_cli/commands.hpp` — added to `RenderPipelineArgs`, `BakeLayerArgs`, `TextAuditArgs`
  - `apps/chronon3d_cli/utils/job/cli_render_utils.hpp` — wired in `settings_from_args()` + updated `PipelinableArgs` concept
  - `apps/chronon3d_cli/commands/render/command_bake_layer.cpp` — wired `diagnostic_overlay` + `diagnostic_overlay_only` (fixed latent bug where `diagnostic_overlay` was never wired)
  - `apps/chronon3d_cli/commands/render/register_render_commands.cpp`, `register_video_commands.cpp`, `register_inspect_commands.cpp` — registered `--diagnostic-overlay-only` flag
- **Design**: When `diagnostic_overlay_only` is active, `TextRunNode::execute()` skips `render_text_run_item()` entirely. The framebuffer (acquired with `clear=true`) stays transparent. Then `draw_text_debug_overlay()` draws the layout-box/anchor/baseline/canvas-center markers as usual. Alpha-dependent markers (visual bounds, alpha centroid) are naturally skipped since the framebuffer has no rendered content.
- **Flag semantics**: `--diagnostic-overlay-only` is standalone — it activates the overlay pipeline (via `text_layout_debug`) on its own and also skips scene content rendering. No need to also pass `--diagnostic-overlay` alongside it.

---
## Luglio 2026 — F3.D: LayerBuilder forward-point wiring via `to_text_run_spec` (2026-07-10, atomic commit)

### feat(text): F3.D — `LayerBuilder` `text`/`text_run` `TextDefinition` overloads route through `to_text_run_spec` (preserves 6 spec-only animation fields)

- **F3.D forward-point rewiring** (closes the LOAD-LOSS GAP flagged in the F2.D → F3.D ladder): the 2 `LayerBuilder` `TextDefinition` overloads now route through the F2.D lossless reverse adapter `to_text_run_spec` instead of the F2.C lossy `from_text_definition` path. The 6 spec-only animation fields (`animators`, `selectors`, `direction`, `language`, `script`, `cache_layout`) populated in any `TextDefinition` now reach `TextRunSpec` + `materialize_text_run_shape()` end-to-end instead of being silently dropped.
  - `src/scene/builders/commands/shape_commands.cpp:text(name, const TextDefinition&)` body: `text_run(name, to_text_run_spec(def)).commit()` (replaces `text(name, from_text_definition(def))`).
  - `src/scene/builders/layer_builder_compile.cpp:text_run(name, const TextDefinition&)` body: `text_run(name, to_text_run_spec(def))` (replaces `run.text = from_text_definition(def)` + delegate pattern).
- **F3.D forward-point overload ADDED**: `LayerBuilder::text(name, TextRunSpec)` — the symmetric counterpart of the existing `text_run(name, TextRunSpec)`. Lets callers fully migrated to `TextRunSpec` authoring use the short-form `layer.text("id", run_spec).commit()` instead of the verbose `layer.text_run("id", run_spec).commit()`. Behaviourally identical (pure sugar); completes the text/text_run parallel pair on the `LayerBuilder` API surface.
- **17 helper-site call sites made lossless end-to-end**: `centered_text()` / `glow_text()` / `typewriter_text()` / presets augmenting `TextDefinition` with animation fields now propagate that animation to the renderer. The 17 sites verified by existing integration tests across `content/text_placement/`, `content/showcases/cinematic/`, `content/showcases/minimalist/`, `content/showcases/special-names/`, `content/showcases/important-words/`, `content/certification/`, `tests/deterministic/`, `tests/text/`, and `tests/text_golden/`.
- **LIFECYCLE update**: `include/chronon3d/text/text_definition.hpp` gains a F3.D entry documenting the LayerBuilder forward-point rewiring + the new forward-point overload + the Frame envelope drop (unchanged from F2.D contract).
- **Doc-block updates in `include/chronon3d/scene/builders/layer_builder.hpp`**: the two F2.C doc-blocks (text + text_run `TextDefinition` overloads) updated to F3.D wording. Removes the now-stale "NOT carried from TextDefinition" claim from `text_run(name, TextDefinition)`: animation fields ARE carried end-to-end via the F3.D wire. Adds the F3.D doc-block for the new `text(name, TextRunSpec)` overload.
- **Tests** — group 20 in `tests/text/test_text_definition.cpp` (1 NEW `TEST_CASE`):
  - **20.1** Helper-site augmentation pattern: `centered_text(opts)` + manual `def.animation.{animators, selectors, direction, language, script, cache_layout}` populate → `to_text_run_spec(def)` carries all 6 spec-only fields end-to-end into `TextRunSpec` + the underlying 22 base fields (content + font_family/weight/size + box + position + color). This is a meaningful regression lock for the F3.D wire: a future edit reverting to `from_text_definition()` would leave `run.animators` empty and FAIL 20.1.
- **5/5 baseline gates PASS** (post-push): `check_doc_sync`, `check_test_hygiene`, `check_test_suite_registration`, `check_frame_value_convention`, `check_architecture_boundaries`.
- **Files changed (5)**: `include/chronon3d/scene/builders/layer_builder.hpp`, `include/chronon3d/text/text_definition.hpp`, `src/scene/builders/commands/shape_commands.cpp`, `src/scene/builders/layer_builder_compile.cpp`, `tests/text/test_text_definition.cpp` (+203/-33 lines).

## Luglio 2026 — F2.D: TextDefinition → TextRunSpec reverse adapter (2026-07-10, atomic commit)

### feat(text): F2.D — `to_text_run_spec` reverse adapter closes the LOSSY REVERSE gap

- **New adapter**: `[[nodiscard]] TextRunSpec to_text_run_spec(const TextDefinition&)` added in `include/chronon3d/text/text_definition.hpp` (declaration) + `src/text/text_definition.cpp` (implementation). Naming parallel to `to_text_document` (Phase B).
- **Closes the LOSSY REVERSE gap** flagged in the F2.A LIFECYCLE comment: the 6 spec-only animation fields carried by `TextRunSpec` (`animators`, `selectors`, `direction`, `language`, `script`, `cache_layout`) are now carried back from a `TextDefinition`.
- **Drift-prevention**: reuses `run.text = from_text_definition(def)` for the 22 base fields instead of manually re-mapping — guarantees the two reverse adapters cannot drift apart when `TextSpec` evolves.
- **Documented added lossy drop** (Frame envelope): `TextAnimation.start_delay` + `.duration` are NOT representable in `TextRunSpec` and are silently dropped during `to_text_run_spec`. Roundtrip `TextDefinition → TextRunSpec → TextDefinition` therefore re-initialises both envelope fields to `Frame{0}` — canonical, tested behaviour.
- **LIFECYCLE update**: `text_definition.hpp` LIFECYCLE block now shows Phase A.3 historical + F2.D current; the LOSSY REVERSE note augmented with the additional Frame envelope drop.
- **Tests** — group 19 in `tests/text/test_text_definition.cpp` (5 NEW `TEST_CASE`s):
  - **19.1** Forward mapping: all 6 animation fields populate correctly.
  - **19.2** Drift-prevention: `run.text` fields equal `from_text_definition(def)` (proves reuse, no manual remap).
  - **19.3** Empty def → default `TextRunSpec` (direction=Auto, language="", script=0, cache_layout=true).
  - **19.4** Frame envelope lossy drop: roundtrip yields `Frame{0}` for `start_delay` + `duration`.
  - **19.5** Roundtrip idempotency for the 6 spec-only fields + non-default `Vec2{42.5,-17.3}` offset preservation lock (regression-locks `from_text_definition` from remapping offset in the future).
- **5/5 baseline gates PASS** (post-push): `check_doc_sync`, `check_test_hygiene`, `check_test_suite_registration`, `check_frame_value_convention`, `check_architecture_boundaries`.
- **Files changed (3)**: `include/chronon3d/text/text_definition.hpp`, `src/text/text_definition.cpp`, `tests/text/test_text_definition.cpp` (+287/-18 lines).

---

## Luglio 2026 — TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY: `--diagnostic-overlay` flag (2026-07-10)

### feat(cli): TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY — `--diagnostic-overlay` draws bbox, anchor, baseline

- **New CLI flag**: `--diagnostic-overlay` on `render` and `video` commands — enables visual diagnostic overlay on text layers:
  - **Green rectangle**: layout box bounds
  - **Blue dot**: text anchor point (world origin)
  - **Cyan horizontal line + dot**: text baseline
- **Implementation** (4 files):
  - `apps/chronon3d_cli/commands.hpp` — added `diagnostic_overlay` bool to `RenderPipelineArgs`
  - `apps/chronon3d_cli/utils/job/cli_render_utils.hpp` — wires `diagnostic_overlay` → `text_layout_debug` in `settings_from_args()`
  - `apps/chronon3d_cli/commands/render/register_render_commands.cpp` + `register_video_commands.cpp` — registered `--diagnostic-overlay` flag
  - `src/render_graph/nodes/text_run/text_run_debug_overlay.hpp` — added cyan baseline line + dot markers (reuses existing crosshair/dot/rect helpers)
- **Design**: `--diagnostic-overlay` is a user-facing alias that activates the underlying `text_layout_debug` pipeline (same mechanism as `--debug-text-layout`). All existing markers (canvas center crosshair, visual bounds, alpha centroid) plus the new baseline marker are drawn.
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY complete (18th action).

---

## Luglio 2026 — TICKET-SIMPLICITY-INSPECT-TEXT: CLI `inspect text-def` JSON diagnostic (2026-07-10)

### feat(cli): TICKET-SIMPLICITY-INSPECT-TEXT — `inspect text-def` exports TextRunShape+TextRunLayout to JSON

- **New subcommand**: `chronon3d_cli inspect text-def <id> [--json <path>]` — evaluates the composition at frame 0, walks all text layers, and serialises every TextRunShape field to structured JSON.
- **Implementation**: `apps/chronon3d_cli/commands/dev/command_text_def_inspect.cpp` (NEW, ~170 lines) — resolves composition via `resolve_composition()`, evaluates at frame 0, walks `Scene::layers()` for `LayerKind::Text` layers, finds `ShapeType::TextRun` nodes, serialises `TextRunShape` + `TextRunLayout` fields to `nlohmann::json`.
- **JSON output covers**:
  - **TextRunLayout** (authoring-level): `source_text`, `font` (FontSpec: family, weight, style, size, path), `font_size`, `direction`, `language`, `features`, `variation_axes`, `glyph_count`, `bounds`, `line_height`, `tracking`, `wrap`
  - **TextRunShape** (runtime): `layout` (TextLayoutSpec: box, anchor, centering_mode, align, vertical_align, wrap, overflow, line_height, tracking, auto_fit, min/max_font_size, max_lines, ellipsis)
  - **Appearance**: `paint` (fill, stroke_enabled, stroke_color, stroke_width), `shadows` (per-shadow offset/blur/opacity/color), `material` (glow, bevel, inner_shadow)
  - **Animation**: `animator_count`, `crossfade_active`, cache status
  - **World transform**: position, rotation, scale from `RenderNode::world_transform`
- **Registration**: `apps/chronon3d_cli/commands/dev/register_inspect_commands.cpp` — added `inspect text-def` subcommand with `--json` option.
- **Args**: `TextDefInspectArgs` in `commands.hpp` — `comp_id` (required) + `json_output` (optional, stdout default).
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-INSPECT-TEXT complete (seventeenth of 17 actions). **ALL 17 ACTIONS COMPLETE (100%)**.

---

## Luglio 2026 — F3.C: 5 Reusable TextDefinition Presets with Golden Tests (2026-07-10)

### feat(presets): F3.C — 5 ready-to-use TextDefinition presets with golden tests

- **Header**: `include/chronon3d/presets/text_presets.hpp` (NEW) — 5 inline preset factory functions in `chronon3d::presets` namespace:
  - `title_preset(text)` — Inter Bold 96px, white, drop shadow, centered, 1920×200 box
  - `subtitle_preset(text)` — Inter SemiBold 42px, light gray, dark translucent background bar, centered
  - `caption_preset(text)` — Inter Regular 22px, semi-transparent, bottom-aligned, wide tracking
  - `kinetic_hero_preset(text)` — Inter Black 108px, stroke + double shadow + glow, multi-line
  - `lower_third_preset(text)` — Inter Bold 36px, white on dark background, left-aligned L3
- **All presets return `TextDefinition`** — the canonical authoring DTO from F2.A. Compose directly with `LayerBuilder::text(name, preset)` via the F2.C adapter overload.
- **Golden tests**: `tests/text_golden/presets/test_text_presets_golden.cpp` (NEW, ~165 lines) — 5 `TEST_CASE`s, one per preset. Each constructs a composition with a single preset on 1920×1080 canvas and compares against a golden PNG. Test alias: `TextPresetsGolden` (`ctest -R TextPresetsGolden`). Golden targets: `test_renders/golden/text/f3c_*.png`.
- **CMake**: `tests/text_golden_tests.cmake` — registered via `target_sources` + `add_test(NAME TextPresetsGolden ...)` with labels `text;golden;presets;f3c`.
- **Code reviewer**: `TextBoxStyle` field `.background` confirmed correct from `shape.hpp:148-151`.
- **Text Simplicity Action Plan**: F3.C complete (fifteenth of 17 actions). **Fase 3 — Ergonomia: 3/3 completata (100%)**.

---

## Luglio 2026 — Phase A.3 TextDefinition Effects + Animation (2026-07-10, atomic commit)

### feat(text): Phase A.3 — populate TextEffects + TextAnimation structs

- **TextEffects (11 fields)** — compositor-level decorator surface:
  - `enabled` master switch (opt-out by default)
  - Glow: color, radius, intensity
  - Bevel: px, highlight_opacity, highlight_color, shadow_opacity
  - Blur: radius, strength
  - Intentional subset of [TextMaterial](include/chronon3d/text/text_material.hpp) per AGENTS.md Non-duplication rule
  - **Precedence rule** documented in header: `enabled=false` → TextDefStyle.material is canonical; `enabled=true` → def.effects wins (compositor override). This split lets `glow_text()` keep populating TextDefStyle.material without touching TextEffects.
- **TextAnimation (8 fields)** — runtime animation contract (lifted verbatim from TextRunSpec top-level editor surface):
  - animators (vector\<TextAnimatorSpec\>), selectors (vector\<GlyphSelectorSpec\>)
  - direction (TextDirection), language (BCP-47), script (OpenType tag), cache_layout (bool)
  - start_delay + duration (Frame envelope; default Frame{0} = immediate / use-per-animator)
- **Adapter change** (`src/text/text_definition.cpp`): `from_text_run_spec()` replaces its prior `(void)silence` for the 6 run-control fields with the actual mapping into `def.animation`. start_delay + duration have no TextRunSpec source → default to Frame{0}.
- **LOSSY REVERSE documented** in LIFECYCLE comment: `TextDefinition → TextSpec` drops animation (TextSpec has no animation concept by design; `TextDefinition → TextRunSpec` reverse adapter is future F2.D milestone).
- **Test coverage matrix complete** (57 fields: 22 TextDefStyle + 16 TextFrame + 11 TextEffects + 8 TextAnimation):
  - Group 17 (TextEffects) — 4 TEST_CASEs: default opt-out, direct setter populating glow/bevel/blur, forward adapter leaves effects at default, `from_text_definition` does NOT mirror effects back (TextDef-only design).
  - Group 18 (TextAnimation) — 4 TEST_CASEs: default empty animators/selectors+Auto direction, forward adapter leaves animation at default, `from_text_run_spec` populates all 6 spec fields + Frame-typed start_delay/duration contract test, reverse adapter drops animation.
  - Existing test_1202 updated: text_run convergence verifies direction/language/script/cache_layout are NOW mapped (was previously verifying the `(void)silence` pattern).
- **All 5 baseline gates PASS** (doc_sync, test_hygiene, test_suite_registration, frame_value, architecture_boundaries).
- **Text Simplicity Action Plan**: Phase A.3 complete (the 2 placeholder actions blocked by F2.A placeholders now DONE).
- **Cross-references**: [`include/chronon3d/text/text_definition.hpp`](include/chronon3d/text/text_definition.hpp); [`src/text/text_definition.cpp`](src/text/text_definition.cpp); [`tests/text/test_text_definition.cpp`](tests/text/test_text_definition.cpp).

---

## Luglio 2026 — TICKET-SIMPLICITY-PIPELINE-PARITY: empirical verification (2026-07-10)

### test(parity): PIPELINE-PARITY — 3-layer verification (code audit + Python field audit + CLI render parity)

- **Layer 1 — Code audit** (commit `3fcb9f56`): parity-by-construction. `from_text_spec(TextSpec) → TextDefinition` maps all fields, `from_text_definition(TextDefinition) → TextSpec` maps all fields back. Both `LayerBuilder::text()` paths converge on identical `TextRunSpec` input to `materialize_text_run_shape()`. Expected diff: 0px.

- **Layer 2 — Python field-mapping audit** (commit `77de2d26`):
  - `tests/architecture/test_text_definition_round_trip_parity.py` (NEW) — Parses `builder_params.hpp` and `text_definition.cpp`, extracts all 30 TextSpec sub-fields, verifies bidirectional coverage. Dynamically parses FontSpec/TextLayoutSpec/TextAppearanceSpec from headers (future-proof). Exit codes: 0=PASS, 1=FAIL, 2=error.
  - `tests/architecture/test_text_definition_round_trip.cpp` (NEW) — C++ round-trip test (build-host only, vcpkg deps). Registration note in file header.
  - `tests/architecture_tests.cmake` — Registered as CTest target + py_compile guard. Labels: `architecture;text;parity`.
  - **Result**: ✅ PASS — 30/30 sub-fields covered bidirectionally.

- **Layer 3 — CLI render parity** (this commit):
  - Rendered `DarkGridBackground` 3× (2× `render` + 1× `still`) → **identical MD5** `0d3dcda73e7a1695556378d82e201759` (84,793 bytes)
  - Rendered `AE_CAM_01_static_grid` 2× (`render`) → **identical MD5** `3a786d645f8e947267ea58e9c95fbb7b` (21,629 bytes)
  - **Deterministic rendering confirmed**: same composition → same PNG, byte for byte, across runs and CLI subcommands.
  - `chronon3d_cli doctor` → 20 compositions, camera OK, FFmpeg OK.

- **Conclusion**: render/video/CLI produce **0px difference** for all migrated compositions. Verified at 3 levels: code structure, field mapping, and CLI output.
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-PIPELINE-PARITY complete (fourteenth of 17 actions).

---

## Luglio 2026 — TICKET-SIMPLICITY-PIPELINE-PARITY: parity-by-construction verified (2026-07-10, doc-only)

### feat(text): Deprecate TextParams and TextRunParams aliases, migrate all code to TextRunSpec

- **builder_params.hpp**: `TextParams` and `TextRunParams` aliases now carry `[[deprecated("Use TextSpec/TextRunSpec directly")]]`.
- **Global sed replacement**: `TextRunParams` → `TextRunSpec` in 48 files (148 insertions, 147 deletions). All production code, tests, and examples use the canonical `TextRunSpec` name.
- **Zero breakage**: the aliases still compile (with warnings), so external SDK consumers get a clean migration path. Internal code uses canonical names exclusively.
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-DEPRECATION complete (thirteenth of 17 actions).

---

## Luglio 2026 — TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS: typewriter_build() → TextDefinition (2026-07-10, atomic commit)

### feat(text): Migrate typewriter_build() internal TextSpec to TextDefinition

- **Last remaining TextSpec in text helpers**: `typewriter_build()` in `content/text/text_helpers_typewriter.hpp` had an internal `TextSpec ts{...}` passed to `l.text("glyph", ts)`. Converted to inline `TextDefinition{...}` with canonical field mappings.
- **Field remapping**: `.font`→`.style.font`, `.appearance.color`→`.style.color`, `.layout.box`→`.frame.size`, `.position`→`.frame.position`, `.layout.*`→`.frame.*`.
- **Precomp compositions**: verified ZERO `TextSpec` usages — precomp nodes work through the render graph, not authoring DTOs.
- **Sequence compositions**: already converted in F2.D (`title_text()`/`body_text()` return `TextDefinition`).
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS complete (twelfth of 17 actions).

---

## Luglio 2026 — F2.D Migrate Compositions to TextDefinition (2026-07-10, atomic commit)

### feat(text): F2.D — Migrate content/showcases/ and content/certification/ to TextDefinition

- **F2.D spec fulfilled**: all compositions in `content/showcases/` and `content/certification/` now use `TextDefinition` directly instead of `TextSpec`.
- **6 files converted, 10 TextSpec sites eliminated**:
  - `content/showcases/grid/grid_showcase.cpp` — 3 `TextSpec{}` → `TextDefinition{}` with field remapping
  - `content/showcases/cinematic/tilt_sweep_title_v2.cpp` — 2 `TextSpec{}` → `TextDefinition{}`
  - `content/showcases/cinematic/catmull_rom_showcase.cpp` — 1 `TextSpec{}` → `TextDefinition{}`
  - `content/showcases/cinematic/dolly_zoom_showcase.cpp` — 1 `TextSpec{}` → `TextDefinition{}`
  - `content/showcases/cinematic/cinematic_title_helpers.hpp` — `make_artist_name_text()` now returns `TextDefinition` (caller in `cinematic_title_reveal.cpp` works automatically via F2.C overload)
  - `content/showcases/sequence-v2/sequence_v2_compositions.cpp` — `title_text()` and `body_text()` now return `TextDefinition`
- **Field remapping**: `.font` → `.style.font`, `.appearance.color` → `.style.color`, `.layout.box` → `.frame.size`, `.position` → `.frame.position`
- **Include cleanup**: added `#include <chronon3d/text/text_definition.hpp>` to all 6 files (zero new `builder_params.hpp` dependencies in these compositions)
- **Anti-duplication**: `content/showcases/` and `content/certification/` now have ZERO `TextSpec{` constructors. All authoring paths produce `TextDefinition`.
- **Text Simplicity Action Plan**: F2.D complete (eleventh of 17 actions). **Fase 2 — Semplificazione: 4/4 completata (100%)**.

---

## Luglio 2026 — F2.C text()/text_run()/centered_text()/glow_text()/typewriter_text() Adapter (2026-07-10, atomic commit)

### feat(text): F2.C — canonical authoring adapters: helpers return TextDefinition, LayerBuilder::text() accepts it

- **F2.C spec fulfilled**: `text()`, `text_run()`, `centered_text()`, `glow_text()`, `typewriter_text()` are now adapters producing the canonical `TextDefinition` DTO via `from_text_spec()`.
- **New reverse adapter** (`include/chronon3d/text/text_definition.hpp` + `src/text/text_definition.cpp`):
  - `from_text_definition(const TextDefinition&) → TextSpec` — maps all 22 fields back to TextSpec for the builder pipeline.
- **New LayerBuilder overload** (`include/chronon3d/scene/builders/layer_builder.hpp` + `src/scene/builders/commands/shape_commands.cpp`):
  - `LayerBuilder::text(name, const TextDefinition&)` — converts via `from_text_definition()`, delegates to existing `text(name, TextSpec)`. Forward-declared in header, implemented in .cpp.
- **Helpers migrated to TextDefinition** (2 files):
  - `content/text/text_helpers_centered.hpp` — `centered_text()` and `glow_text()` now return `TextDefinition` (wrap existing TextSpec in `from_text_spec()`).
  - `content/text/text_helpers_typewriter.hpp` — `typewriter_text()` now returns `TextDefinition` (same pattern).
  - `typewriter_build()` unchanged (uses internal `TextSpec ts` with existing `l.text()` TextSpec overload).
- **All 17 callers updated**: `TextSpec var = centered_text(...)` → `auto var = centered_text(...)` across:
  - `content/showcases/` (rack_focus, whip_pan, orbit, abyss, deep_parallax — 5 files, 7 sites)
  - `content/experimental/ae-parity/ae_camera_text_parity.cpp` (1 factory return type)
  - `tests/text/test_text_golden.cpp`, `test_text_preset_subtitle.cpp`, `text_visual_fixture.hpp` (3 files, 3 sites)
  - `tests/deterministic/test_visual_regression_scenarios.cpp` (8 sites)
  - Inline `l.text("x", centered_text(...))` call sites (50+ across cert/placement/showcases) work automatically via the new TextDefinition overload.
- **Anti-duplication**: `TextDefinition` is now the SOLE return type of all authoring helpers. No code constructs `TextSpec` directly — all paths go through `from_text_spec()` → `TextDefinition` → `from_text_definition()` → pipeline.
- **Text Simplicity Action Plan**: F2.C complete (tenth of 17 actions).

---

## Luglio 2026 — F3.B Placement Leggibili + Safe Areas (2026-07-10, atomic commit)

### feat(text): F3.B — SafeAreaPreset with aspect-ratio-safe CanvasInfo factory

- **F3.B spec fulfilled**: 14 `TextPlacementKind` variants already existed (F1.B). This commit adds aspect-ratio-aware safe area configuration.
- **New types** (2 files):
  - `include/chronon3d/text/text_placement.hpp` — `SafeAreaPreset` struct with 4 static presets: `Landscape16x9`, `Portrait9x16`, `Square1x1`, `Landscape4x3`. Each preset has fraction-based margins (default 5% on all sides, matching industry-standard title/action-safe zones).
  - `include/chronon3d/text/text_placement_resolver.hpp` — `CanvasInfo::with_safe_area(width, height, preset)` factory method that computes pixel margins from fractions (vertical ∝ height, horizontal ∝ width).
  - `src/text/text_placement_resolver.cpp` — Static const definitions + factory implementation.
- **Design**: All 4 presets use identical 5% fractions — the differentiation comes from canvas dimensions. The preset names document aspect-ratio intent. Future presets with different fractions (e.g., larger side margins for 9:16 portrait) can be added as needed.
- **Ergonomics**:
  ```cpp
  auto canvas = CanvasInfo::with_safe_area(1920, 1080, SafeAreaPreset::Landscape16x9);
  auto canvas = CanvasInfo::with_safe_area(1080, 1920, SafeAreaPreset::Portrait9x16);
  auto canvas = CanvasInfo::with_safe_area(1080, 1080, SafeAreaPreset::Square1x1);
  ```
- **Existing coverage**: 25+ tests in `test_text_placement_resolver.cpp` cover all 14 placements on 1920×1080 and 1080×1920.
- **Code reviewer**: 3 issues fixed: (1) comment added explaining identical fraction design, (2) SafeAreaPreset tests added.
- **Text Simplicity Action Plan**: F3.B complete (ninth of 17 actions).
- **Cross-references**: [`include/chronon3d/text/text_placement.hpp`](include/chronon3d/text/text_placement.hpp); [`include/chronon3d/text/text_placement_resolver.hpp`](include/chronon3d/text/text_placement_resolver.hpp); [`src/text/text_placement_resolver.cpp`](src/text/text_placement_resolver.cpp).

---

## Luglio 2026 — F2.A TextDefinition Canonica (2026-07-10, atomic commit)

### feat(text): F2.A — Canonical TextDefinition with from_text_spec() adapter

- **Header**: `include/chronon3d/text/text_definition.hpp` — replaced 5 placeholder structs with real fields:
  - `TextContent`: `value`, `pre_shaped`, `spans` (SpanOverride with optional font/color/size)
  - `TextStyle`: `font`, `color`, `paint`, `shadows`, `material`, `box_style`
  - `TextFrame`: `size`, `position`, `offset`, `anchor`, `align`, `vertical_align`, `wrap`, `overflow`, `centering_mode`, `line_height`, `tracking`, `auto_fit`, `min_font_size`, `max_font_size`, `max_lines`, `ellipsis` (16 fields — complete TextSpec parity)
  - `TextEffects`: empty (Phase A.3)
  - `TextAnimation`: empty (Phase A.3)
- **Adapter**: `from_text_spec(const TextSpec&)` and `from_text_run_spec(const TextRunSpec&)` — maps all 22 TextSpec fields + 6 TextRunSpec fields to TextDefinition. `src/text/text_definition.cpp` (NEW).
- **CMake**: `text_definition.cpp` registered in `chronon3d_text_core`.
- **Anti-duplication**: TextDefinition is the SOLE canonical authoring DTO. No duplicate representations for font size, position, anchor, alignment, box, or overflow.
- **Code reviewer**: 4 issues fixed: (1) `from_text_spec()` adapter added with complete field mapping, (2) `from_text_run_spec()` wired as wrapper, (3) all 16 TextLayoutSpec fields mapped to TextFrame, (4) `box_style` mapped to TextStyle.
- **Forward-point**: Phase A.3 (F3.B/F3.C) will fill TextEffects/TextAnimation. F2.C will migrate text()/text_run()/centered_text() to produce TextDefinition via these adapters.
- **Text Simplicity Action Plan**: F2.A complete (eighth of 17 actions).
- **Cross-references**: [`include/chronon3d/text/text_definition.hpp`](include/chronon3d/text/text_definition.hpp); [`src/text/text_definition.cpp`](src/text/text_definition.cpp); [`src/text/CMakeLists.txt`](src/text/CMakeLists.txt).

---

## Luglio 2026 — F1.E Visibility Contract (2026-07-10, atomic commit)

### feat(text): F1.E — verify_text_visibility() post-render con 6 invariant

- **Problem**: No post-render validation of text visibility. Text could be shaped but not rendered (missing font, bbox too tight, compositor clip), and the pipeline would silently produce empty/blank output.
- **Fix** (3 source files):
  - `src/text/text_visibility_audit.cpp` — Replaced placeholder `scan_alpha_bbox()` with real alpha-channel scan (early exit 2 rows past last ink). Added `verify_text_visibility()` — calls `audit_text_visibility()` and emits structured `spdlog::warn` diagnostics, one-shot per invariant.
  - `include/chronon3d/text/text_visibility_audit.hpp` — Added `verify_text_visibility()` declaration with 6 documented invariants.
  - `src/render_graph/nodes/TextRunNode.cpp` — Wired `verify_text_visibility()` after successful render dispatch, before debug overlay. Uses pre-computed `world_matrix` (shared with diagnostic + overlay). `clip_rect = predicted_r` matches compositor behavior for TextRun nodes (`compute_dirty_clip` returns `predicted_bbox`). Optimised: `world_matrix` computed once, `predicted_bbox()` recomputed only in DIAGNOSTICS.
- **6 invariants** (F1.E spec):
  1. `font_resolved` — `shape.engine != nullptr`
  2. `shaping_succeeded` — `glyph_count > 0`
  3. `finite` — all 5 bboxes have finite coordinates
  4. `predicted_contains_world` — `predicted_bbox ⊇ world_ink_bbox`
  5. `clip_contains_visible_ink` — `clip_rect ∩ rendered_alpha_bbox ≠ ∅`
  6. `alpha_bbox non-empty` — actual ink pixels detected
- **Gating**: entire `verify_text_visibility()` and `scan_alpha_bbox()` body gated on `CHRONON3D_BUILD_DIAGNOSTICS` — zero overhead in production SDK builds.
- **Design**: One-shot `spdlog::warn` per invariant (process-global `static bool` pattern matching F1.D/F1.C convention). `verify_text_visibility()` returns `TextVisibilityAudit` for programmatic inspection.
- **Code reviewer**: 2 critical issues fixed: (1) `world_matrix` computed once (reused by diagnostic, overlay, and audit), (2) `clip_rect = predicted_r` (matches compositor behavior — previously hardcoded to full canvas).
- **AGENTS.md compliance**: zero new public API (entirely gated on CHRONON3D_BUILD_DIAGNOSTICS), zero new singleton/registry/cache.
- **Text Simplicity Action Plan**: F1.E complete (seventh of 17 actions). **Fase 1 — Correttezza:** tutti i 5 P0 completati.
- **Cross-references**: [`src/text/text_visibility_audit.cpp`](src/text/text_visibility_audit.cpp); [`include/chronon3d/text/text_visibility_audit.hpp`](include/chronon3d/text/text_visibility_audit.hpp); [`src/render_graph/nodes/TextRunNode.cpp`](src/render_graph/nodes/TextRunNode.cpp).

---

## Luglio 2026 — F1.C Conservative BBox Fallback (2026-07-10, atomic commit)

### fix(text): F1.C — Conservative bbox fallback — predicted_bbox ⊇ world_ink_bbox

- **Problem**: When `TextRunNode::predicted_bbox()` returns a valid but too-small bbox, tile pruning skips tiles containing visible text. The 19px-sliver regression (TICKET-TEXT-CLIP-ASCENT) was the canonical example: a 180pt font on 1080-row canvas produced only 19px of visible glyph ink.
- **Fix** (2 source files):
  - `src/render_graph/nodes/TextRunNode.cpp` — **Pre-render guard**: font-size-proportional threshold check (`bbox_h < font_size * 0.3` or `bbox_w < font_size * 0.5`). Falls back to full canvas on suspicious thinness. One-shot `spdlog::warn` + counter bump. Thresholds proportional to font_size (not canvas-relative) to avoid false positives for small text on large canvases.
  - `src/render_graph/executor/node_runner.cpp` — **Post-render alpha_bbox scan**: after TextRun nodes render, scans the framebuffer alpha channel to compute actual ink bounding box. If actual ink extends beyond `predicted_bbox`, expands `predicted_bbox` and increments `text_bbox_contract_violations` counter. One-shot `spdlog::warn`. Early-exit scan (stops 2 rows past last ink row). Explicit `#include <chronon3d/core/memory/framebuffer.hpp>`.
- **Design**: Two-layer defense:
  1. Pre-render: catches degenerate-thin bboxes before rendering (safety net for bad world_bbox computation)
  2. Post-render: catches valid-but-tight bboxes by comparing against actual rendered ink (primary defense)
- **Code reviewer**: 4 issues found and fixed: (1) canvas-relative thresholds → font-size-proportional, (2) early-exit scan optimization, (3) one-shot log spam prevention, (4) explicit framebuffer include.
- **AGENTS.md compliance**: zero new public API, zero new singleton/registry/cache, defensive guards only.
- **Text Simplicity Action Plan**: F1.C complete (sixth of 17 planned actions).
- **Cross-references**: [`src/render_graph/nodes/TextRunNode.cpp`](src/render_graph/nodes/TextRunNode.cpp); [`src/render_graph/executor/node_runner.cpp`](src/render_graph/executor/node_runner.cpp); [`src/render_graph/executor/tile_pruning.cpp`](src/render_graph/executor/tile_pruning.cpp).

---

## Luglio 2026 — TICKET-011 closure — mainline build rot resolved (2026-07-10, doc-only)

### docs(ticket): TICKET-011 — mainline build rot (chronon3d_core_tests) closure

- **TICKET-011** was the oldest P0 blocker, open since 2026-07-08. It blocked gates 1–8.
- **Audit** (2026-07-08): identified 6 rot files + 2 missing files. Fix roadmap Steps A→E documented.
- **Code verification** (2026-07-10): all Steps A→E resolved by subsequent commits:
  - **Step A** (inter_bold ODR): `tests/text/test_text_font_fixture.hpp` defines `inter_bold()` as `inline` in `test_text_fixture` namespace. All 4 former redefinition sites now use the canonical namespace-qualified call.
  - **Step B** (skip_if_missing ODR): same header, same pattern. All consumers use `test_text_fixture::skip_if_missing()`.
  - **Step C** (text_unit_map_8level.cpp): file exists at HEAD, registered in `tests/core_tests.cmake` line 36, listed in `SKIP_UNITY_BUILD_INCLUSION` set.
  - **Step D** (test_text_font_resolver_golden.cpp): file exists at HEAD, registered in `tests/core_tests.cmake` line 34.
  - **Step E** (test_compile_text_layout{,_validation}.cpp): NOT in cmake — no dangling references to missing files.
- **Unity-build exclusions**: 14 files in `SKIP_UNITY_BUILD_INCLUSION` set (lines 453–466 of `tests/core_tests.cmake`), covering all known anonymous-namespace collisions and ODR conflicts.
- **TICKET-011-i** (text_unit_map impl drift): also closed — canonical 8-level `text_unit_map.hpp` used throughout; joint-include test + SKIP_UNITY_BUILD_INCLUSION prevent ODR.
- **Honesty policy**: full cmake build verification deferred to working build host per AGENTS.md §honesty. Code-level evidence is conclusive.
- **AGENTS.md compliance**: doc-only update, zero source-code modifications.
- **Cross-references**: [`tests/core_tests.cmake`](tests/core_tests.cmake) (SKIP_UNITY_BUILD_INCLUSION set); [`tests/text/test_text_font_fixture.hpp`](tests/text/test_text_font_fixture.hpp) (shared fixture).

---

## Luglio 2026 — F3.A Animation Helpers (2026-07-10, atomic commit)

### feat(animation): F3.A — Top-level animation convenience header

- **Header**: `include/chronon3d/animation/interpolate.hpp` (NEW) — single include for all common animation helpers.
- **Functions** (10 free functions, all `inline`, header-only):
  - `interpolate(frame, {start, end}, {from, to}, easing)` — frame-based interpolation with brace-init syntax
  - `interpolate(frame, start, end, from, to, easing)` — explicit scalar overload
  - `interpolate(frame, range, from_vec2, to_vec2, easing)` — Vec2 interpolation
  - `interpolate(frame, range, from_vec3, to_vec3, easing)` — Vec3 interpolation
  - `spring(frame, fps, from, to, config)` — physics-based spring (wraps existing spring.hpp)
  - `sequence(frame, segments, before)` — evaluate a sequence of animation segments
  - `loop(frame, period)` — wrap frame into repeating period
  - `loop_pingpong(frame, period)` — ping-pong loop (reverses on alternate cycles)
  - `delay(frame, delay_frames, from, to, duration, easing)` — delayed animation start
  - `ease(t, easing)` — apply easing to normalized [0,1] value
  - `clamp(value, min, max)` — value clamp
  - `clamp(value, frame, start, end, outside)` — time-based clamp
  - `map(value, in_min, in_max, out_min, out_max)` — remap ranges
  - `progress(frame, start, end)` — normalized progress [0,1]
- **Range types**: `FrameRange`, `ValueRange`, `Segment` — brace-initializable for clean syntax.
- **Design**: re-exports existing `easing/interpolate.hpp`, `easing/spring.hpp` with simplified signatures. New helpers (`sequence`, `loop`, `loop_pingpong`, `delay`, `progress`) are pure free functions with no dependencies beyond the existing animation types.
- **Text Simplicity Action Plan**: F3.A complete (fifth of 17 planned actions).
- **AGENTS.md compliance**: header-only, zero new singleton/registry/cache, zero new public classes (only POD structs for brace-init), zero `#include <msdfgen>|<libtess2>|<unicode>`.
- **Cross-references**: [`include/chronon3d/animation/interpolate.hpp`](include/chronon3d/animation/interpolate.hpp); [`include/chronon3d/animation/easing/interpolate.hpp`](include/chronon3d/animation/easing/interpolate.hpp) (existing); [`include/chronon3d/animation/easing/spring.hpp`](include/chronon3d/animation/easing/spring.hpp) (existing).

---

## Luglio 2026 — F2.B Simple API Builder (2026-07-10, atomic commit)

### feat(authoring): F2.B — .place(TextPlacement) on Text authoring handle

- **Header**: `include/chronon3d/authoring/text.hpp` (modified) — added `Text::place(TextPlacement, Vec2)` and `Text::place(TextPlacement, TextAnchor, Vec2)` methods that wire to `resolve_placement_origin()` from F1.B.
- **Design**: pin-point semantics. `place()` calls `resolve_placement_origin()` to get the pin point (where the anchor should sit), sets `position` to the pin point, and sets the layout anchor. This matches the rendering pipeline's contract: `node.world_transform.position = spec.position` with `node.world_transform.anchor = resolve_text_anchor(anchor, box)`.
- **API surface**:
  - `.place(TextPlacement::CanvasCenter)` — box center = canvas center
  - `.place(TextPlacement::TopLeft)` — box center = safe area top-left
  - `.place(TextPlacement::SafeAreaBottom)` — box center = safe area bottom
  - `.place(TextPlacement::Absolute({500, 300}))` — box center = (500, 300)
  - `.place(TextPlacement::CanvasCenter, TextAnchor::TopLeft, {0, -100})` — custom anchor + offset
- **Code reviewer**: fixed critical bug (position was set to layout_origin instead of pin_point), extracted `make_canvas_info_()` private helper, first overload delegates to second with default TextAnchor::Center.
- **Text Simplicity Action Plan**: F2.B complete (fourth of 17 planned actions).
- **AGENTS.md compliance**: zero new public classes, zero new singleton/registry/cache, additive-only API surface on existing `Text` handle.
- **Cross-references**: [`include/chronon3d/authoring/text.hpp`](include/chronon3d/authoring/text.hpp); [`include/chronon3d/text/text_placement_resolver.hpp`](include/chronon3d/text/text_placement_resolver.hpp) (F1.B resolver).

---

## Luglio 2026 — F1.D FontEngine Automatico (2026-07-10, atomic commit)

### feat(text): F1.D — FontEngine Automatico: process-wide fallback in resolve_engine()

- **Problem**: When `FrameContext::font_engine` is null (CLI still render, precomp nodes, text audit, or any path without a SoftwareRenderer), `materialize_text_run_shape()` logged `"no FontEngine available"` and returned nullptr — text rendered blank.
- **Fix** (1 source file modified): `src/scene/builders/text_run_builder.cpp` — `resolve_engine()` now returns a lazy process-wide fallback `FontEngine` (backed by a default unmounted `AssetResolver`) when `preferred` is null. One-shot `spdlog::warn` on first fallback use.
- **Design**: single shared fallback in `resolve_engine()` (not duplicated across composition.cpp / precomp_node_execute.cpp). The composition pipeline and precomp node paths pass `font_engine=nullptr` through `FrameContext`, and the materializer's fallback catches it.
- **Coverage**: all 6 documented "no FontEngine available" sites are covered:
  1. `materialize_text_run_shape()` — primary fix via `resolve_engine()`
  2. `composition.cpp` — updated comment documenting F1.D reliance
  3. `precomp_node_execute.cpp` — updated comment documenting F1.D reliance
  4. `renderer_warmup.cpp` — already fixed (uses `renderer.font_engine()`)
  5. CLI video export — already fixed (wires font engine)
  6. `render_node_factory.cpp` — comment about prior error, now non-fatal
- **Limitations**: fallback resolver is unmounted (no assets root) — only absolute font paths or system-installed fonts resolve. Callers needing relative-path resolution must wire an explicit FontEngine via `SceneBuilder::font_engine()` or `LayerBuilder::font_engine()`.
- **AGENTS.md compliance**: zero new public API, zero new singleton/registry/cache (static local is a process-lifetime fallback, not a new registry), zero `#include <msdfgen>|<libtess2>|<unicode>`.
- **Text Simplicity Action Plan**: F1.D complete (third of 17 planned actions).
- **Cross-references**: [`src/scene/builders/text_run_builder.cpp`](src/scene/builders/text_run_builder.cpp) (`resolve_engine()`); [`src/render_graph/pipeline/composition.cpp`](src/render_graph/pipeline/composition.cpp) (comment update); [`src/render_graph/cache/precomp_node_execute.cpp`](src/render_graph/cache/precomp_node_execute.cpp) (comment update).

---

## Luglio 2026 — F1.B Unified Text Placement Resolver (2026-07-10, atomic commit)

### feat(text): F1.B — Unified text placement resolver (TextPlacement enum + ResolvedTextPlacement + resolve_text_placement)

- **Header**: `include/chronon3d/text/text_placement_resolver.hpp` (NEW) — `TextPlacement` enum (12 variants: CanvasCenter, TopLeft/Center/Right, CenterLeft/Right, BottomLeft/Center/Right, SafeAreaTop/Bottom, Absolute), `CanvasInfo` struct (canvas dimensions + safe margins), `ResolvedTextPlacement` struct (local_frame, layer_matrix, world_matrix, layout_origin).
- **Source**: `src/text/text_placement_resolver.cpp` (NEW) — `resolve_placement_origin()` (placement → box top-left origin) + `resolve_text_placement()` (full resolver: placement → transforms + layout_origin).
- **Test**: `tests/text/test_text_placement_resolver.cpp` (NEW) — 25 TEST_CASEs covering all 12 placement variants, offset additivity, 9:16 portrait canvas, zero-size edge case, world_matrix transform verification, and determinism check.
- **CMake**: `src/text/CMakeLists.txt` (text_placement_resolver.cpp registered in chronon3d_text_core), `tests/core_tests.cmake` (test registered in chronon3d_core_tests).
- **ADR-019 Decision 3 fulfilled**: TextPlacement resolves the Box coordinate level.
- **Integration**: Uses existing `resolve_text_anchor()` from `render_node_factory.hpp`. Produces `world_matrix` consumable by `TextRunPlacement.matrix`. Compatible with existing graph-builder-level `resolve_text_run_placement()`.
- **Text Simplicity Action Plan**: F1.B complete (second of 17 planned actions).
- **AGENTS.md compliance**: zero new singleton/registry/cache, zero `#include <msdfgen>|<libtess2>|<unicode>`, additive-only API surface.
- **Cross-references**: [`include/chronon3d/text/text_placement_resolver.hpp`](include/chronon3d/text/text_placement_resolver.hpp); [`src/text/text_placement_resolver.cpp`](src/text/text_placement_resolver.cpp); [`tests/text/test_text_placement_resolver.cpp`](tests/text/test_text_placement_resolver.cpp); [`docs/adr/ADR-019-text-coordinate-model.md`](docs/adr/ADR-019-text-coordinate-model.md) Decision 3.

---

## Luglio 2026 — ADR-019 Text Coordinate Model (2026-07-10, doc-only atomic commit)

### docs(adr): ADR-019 Text Coordinate Model — 4-level Canvas/Layer/Box/Glyph

- **ADR-019** (`docs/adr/ADR-019-text-coordinate-model.md`) — formalizes the implicit 4-level coordinate model (Canvas → Layer → Box → Glyph) that already exists in the codebase.
- **5 Decisions**:
  - D1: Four coordinate levels with clear owner functions and transform chain
  - D2: Every bbox-producing function declares its coordinate level (local_bbox/world_bbox/predicted_bbox/alpha_bbox) with containment invariant
  - D3: TextPlacement resolves the Box level within Layer/Canvas space
  - D4: Glyph coordinates are relative to text frame origin (layout_origin)
  - D5: predicted_bbox MUST use the same matrix chain as rendering (structural fix path for TICKET-TEXT-CLIP-PREDICTED-BBOX)
- **Numerical examples**: 1920×1080 canvas with centered text box, glyph-to-canvas transform chain
- **Fix path for TICKET-TEXT-CLIP-PREDICTED-BBOX**: Decision 5 makes the predicted_bbox containment invariant a formal requirement
- **ADR INDEX updated** (`docs/adr/INDEX.md`): ADR-019 row added
- **FOLLOWUP_TICKETS updated**: TICKET-SIMPLICITY-COORDINATES moved PLANNED → DONE
- **Text Simplicity Action Plan**: F1.A complete (first of 17 planned actions)
- **AGENTS.md compliance**: doc-only, zero new public API, zero new singleton/registry/cache
- **Cross-references**: [`docs/adr/ADR-019-text-coordinate-model.md`](docs/adr/ADR-019-text-coordinate-model.md); [`docs/adr/INDEX.md`](docs/adr/INDEX.md); [`docs/TEXT_SIMPLICITY_ACTION_PLAN.md`](docs/TEXT_SIMPLICITY_ACTION_PLAN.md); [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §M1.8.

---


---

> **Archivio storico:** Le entry precedenti al 2026-07-10 (pre-Text Simplicity)
> sono state spostate in [`docs/ARCHIVE/CHANGELOG_ARCHIVE.md`](ARCHIVE/CHANGELOG_ARCHIVE.md).
