# TICKET-PRE-SLANG-FOUNDATION — foundation closeout before compiler specialization

**Stato:** CODE DONE + BUILD PASS + GATE PASS (188/188) — vedi *Verification*
**Owner:** working session (repo owner + agent)
**Created:** 2026-09-13
**Baseline:** working tree on `main@2315b8fcd` + the N1–N8 wave (`TICKET-HOTPATH-AUDIT-N1-N8.md`)
**Trigger:** external review of the runtime/compiler foundation before introducing
Slang/PTX/SASS lowerings. The review's own conclusion was "experiments yes,
production not yet", with a required sequence: N1 → N2 → **FP-1** → N3 → N7 →
N5 → cache-taxonomy gate → full same-SHA certification.

## Scope

This ticket closes every item of that sequence that is reachable in this
repository, and **deliberately does not** implement Slang, PTX or SASS.

| Review item | Where it lives |
|---|---|
| N1 device-session ownership | already closed — `TICKET-HOTPATH-AUDIT-N1-N8.md` |
| N2 daemon state ownership | already closed — same ticket |
| **FP-1 canonical job fingerprint** | this ticket, §1 |
| N3 `TextBatchPass` demolition | already closed — same ticket |
| N7 frame-parameter IR cleanup | already closed — same ticket |
| N5 typed scheduling outcomes | extended here, §3 (classification) |
| cache taxonomy → real enforcement | this ticket, §2 |
| N4 surface/submission coupling | measured here, §4 (decoupling landed in the N1–N8 wave) |
| full same-SHA certification (determinism, zero-alloc, GPU corpus) | **NOT REACHABLE here** — needs CI + the external content pack |
| FrameDelta stabilisation, PixelProgram, DeviceProgram, Slang/PTX/SASS | **NOT IN SCOPE** — future milestones, see §5 |

The review's own precondition is explicit: a Slang PoC is safe only as an
isolated experiment with *no* production switch, *no* new registry, *no* new
cache, *no* new scheduler and *no* second sync authority. Writing an isolated
PoC requires the Slang toolchain, which this repository does not vendor and
this host does not have, so there is nothing to verify — see §5 for the
doctrine that is enforced instead.

## 1. FP-1 — one job identity, one implementation

**Finding.** The render job identity had **two verbatim implementations** and
**two identical types**:

| Authority | Type | Consumer |
|---|---|---|
| `src/render_plan/render_plan_compiler_fingerprint.cpp` (`render_plan::detail`) | `RenderJobFingerprint` | `compile_render_plan()` → `PreparedRenderPlan::fingerprint` |
| `src/compiler/fingerprint/program_fingerprint.cpp` (`compiler::stages`) | `ProgramDigests` | `compiler::compile()` → `CompiledProgramData::metadata` + `fingerprint_data` |

Both hashed `chronon.render-content.v3` / `chronon.render-request.v3` with a
private copy of `render_settings_fingerprint()` and a duplicated `material()`
lambda. They agreed **by review**, not by construction. The review's point
stands: the moment a program/binary cache keys on those digests, two producers
of one identity is not a performance bug but a **wrong-cache-hit bug**.

**Delta.**

- `RenderJobFingerprint` now exists once, in the fingerprint vocabulary header
  `include/chronon3d/render_plan/render_plan_fingerprint.hpp` (where
  `RenderPlanFingerprintOptions` already lived).
- One implementation: `render_plan::compute_render_job_fingerprint(plan,
  assets, options)`, declared there and defined in
  `src/render_plan/render_plan_compiler_fingerprint.cpp`.
- `compiler::stages::compute_digests` and `ProgramDigests` are **deleted**;
  `compiler::compile()` calls the canonical function.
- `src/compiler/fingerprint/program_fingerprint.cpp` keeps only the
  structural `fingerprint()` of the compiled program — a different fact
  (IR/topology shape, not content identity).

**Digest preservation.** The hash *sequence* is byte-identical to both former
copies, so no published digest changes for any caller that passes non-zero
dimensions. The one behavioural unification is deliberate and argued below.

**The canvas defaulting is part of the identity.** `compiler.cpp` used to fill
`render_settings.width/height` from the plan canvas when they were `0`, while
the plan path did not — and the CLI did it a third time by hand in
`apply_fingerprint_settings()`. Three sites for one rule. The rule now lives in
the authority itself (a zero dimension means "inherit the canvas"), which is
digest-preserving for the compiler path (it already substituted the same
values) and removes the divergence for the plan path. The CLI's two redundant
lines are gone and its helper no longer takes the plan.

**Acceptance test** (`tests/c_abi/test_prepared_render_plan.cpp`, suite
`chronon3d_c_abi_tests`, which links both libraries):

1. same plan + assets + options through `compiler::compile()` and through
   `compile_render_plan()` ⇒ **equal** `content_digest` **and** `request_digest`;
2. `metadata()` and `fingerprint_data()` of one program agree (the two views of
   one program cannot drift either);
3. an unset dimension inherits the canvas on **both** paths.

**CMake constraint respected.** `chronon3d_compiler` must not depend on
`chronon3d_render_plan` (`compiler_render_plan_edge_forbidden`). The canonical
definition stays in the render-plan library and the compiler library *calls* it
exactly as it already called `render_plan::compute_render_plan_content_fingerprint`;
no `target_link_libraries` edge is added, and the gate stays green.

## 2. Cache taxonomy: from convention to gate

**Finding.** `include/chronon3d/cache/cache_taxonomy.hpp` and AGENTS.md both
stated the taxonomy and the "no second cache engine" invariant, but nothing
enforced either. The review's non-negotiable: SPIR-V, Slang output, PTX module,
CUBIN, SASS specialization and CUDA-graph executables are all **ProgramCache**
content — never `SlangCache` / `PtxCache` / `SassCache` / `ShaderBinaryRegistry`,
and never a second generic cache engine.

**Delta.**

- The taxonomy header's `ProgramCache` family now names every GPU program
  artifact kind, states that a *lowering is not a renderer/backend/cache
  family*, and states the key rule (canonical program fingerprint + target
  attributes — never a filename, an object address or an ad-hoc hash).
- Three declarative rules in `tools/architecture_rules.toml`:
  `no_gpu_binary_cache_family`, `single_cache_primitive`,
  `cache_taxonomy_gpu_artifact_doctrine` (the last one makes the doctrine
  undeletable: it is a required-pattern contract on the taxonomy header).

**The gate is certified, not assumed.** `tools/architecture_gate_selftest.sh`
grew four cases that run the **real** registry against a fixture tree and
assert on the rule *name*:

| Case | Expectation |
|---|---|
| compliant cache mirror | `no_gpu_binary_cache_family` PASS, `single_cache_primitive` PASS |
| planted `class SlangCache` / `struct PtxRegistry` | `no_gpu_binary_cache_family` FAIL |
| planted `class CacheEngine` | `single_cache_primitive` FAIL |

**Gate precision (stated, not hidden).** The rule matches *type declarations*.
It therefore cannot see a compiled artifact that is a function-local `static`
inside a `.cpp`. Enforcing that found a real, pre-existing exception:

`src/media/video/compositor/cuda/cuda_nv12_compositor.cpp` keeps
`get_compiled_nv12_ptx()` as a function-local `static const std::string` (NVRTC
output) and a `static std::unordered_map<CUcontext, std::weak_ptr<...>>` guarded
by its own `static std::mutex` — i.e. compiled GPU program artifacts with their
own sync authority, outside the taxonomy. Recorded as DD-3 below; it is not
fixed here because it is the video/CUDA lane and it needs a GPU corpus.

## 3. N5 — expected outcome vs broken invariant

`FrameSchedulingCode` (from the N1–N8 wave) said *what* happened; callers still
had to know *what it means*. Added, as compile-time classification:

| Code | Class | Why, from the code |
|---|---|---|
| `NoFreeFrameSlot` | `Backpressure` | `slots_->try_acquire()` found every slot busy — genuine saturation, the one retryable state |
| `AcquireRenderSlotFailed` | `InvariantViolation` | `evaluated_queue_->try_pop()` failed right after the pipeline pushed |
| `PublishEvaluatedFailed` | `InvariantViolation` | the CAS succeeded, so the queue cannot be full: pool and queues share `kPipelineDepth` |
| `PublishRenderedFailed` | `InvariantViolation` | same capacity argument on the rendered queue |
| `ReleaseEncodedSlotFailed` | `InvariantViolation` | the slot is not in `Encoding` although `begin_encoding()` just succeeded |
| `ExecutorFailed` | `JobFailure` | a canonical `Error` is already recorded on the session |

`frame_scheduling_is_retryable()` answers the only question a scheduling caller
actually has, and `frame_scheduling_class_name()` makes the OPP adapter's error
message state the class instead of an anonymous failure. Locked by 12
`static_assert`s plus a name-coverage case in
`tests/runtime/test_frame_execution_slot_components.cpp`.

## 4. N4 — the remaining dual-lock coupling is now measured

The decoupling itself landed in the N1–N8 wave (`SurfaceLease` + per-slot
generation token). The review asked for numbers before touching what remains:

| Counter | Meaning |
|---|---|
| `surface_lock_{acquires,wait_us,hold_us}` | store domain, taken alone |
| `submission_lock_{acquires,wait_us,hold_us}` | submission ring, taken alone |
| `dual_lock_{acquires,wait_us,hold_us}` | the sites that still need BOTH |

All 16 remaining dual sites (batch transitions, surface lifecycle, upload and
readback ingress) now acquire through one canonical `Impl::DomainLock`, which
is a drop-in replacement for the `std::scoped_lock` it displaces — same fixed
order (surfaces → submission), same scope, no lambda, no control-flow change —
plus the accounting. The recording path acquires each domain **separately**, so
the counters show the payoff of the decoupling rather than asserting it.

`tests/backends/test_vulkan_submission_ring.cpp` asserts the property directly:
between `begin_frame_batch()` and `end_frame_batch()`, one recording op grows
`submission_lock_acquires` and `surface_lock_acquires` and leaves
`dual_lock_acquires` **unchanged**; the batch transition that follows does grow
it, and so does `download_surface()` (resolve + submit in one call). The
equality is a regression alarm, not a statistic.

## 5. What is deliberately not done

- **Slang / PTX / SASS**: no code, no dependency, no ADR. They are lowerings
  under one semantic authority, and the review's own sequence puts them after
  "clean compiler/runtime foundation + full same-SHA certification". The
  enforceable part of that doctrine (§2) is in place; the toolchain is not
  vendored, so a PoC here could not be verified.
- **Full same-SHA certification** (determinism, zero-allocation, GPU corpus):
  needs CI and the external content pack — `NOT RUN`, tracked in
  `docs/CURRENT_STATUS.md`.
- **FrameDelta → PixelProgram → DeviceProgram**: future milestones; no new
  authority may be introduced before that baseline is certified.

## Verification

| Check | Result |
|---|---|
| `python3.11 tools/check_architecture.py` | **GATE_PASS 188/188** (185 before this ticket) |
| `bash tools/architecture_gate_selftest.sh` | **15/15 assertions** (11 before; 4 new cache-taxonomy cases) |
| `linux-fast-dev` build (`-j12`) | 0 errors |
| `linux-video-test` build (`-j12`, Vulkan ON) | 0 errors |
| `chronon3d_c_abi_tests` | 43/43 cases, 248 assertions PASS (incl. the 2 new FP-1 cases) |
| `chronon3d_frame_execution_slot_tests` | 7/7 cases, 70 assertions PASS |
| `chronon3d_vulkan_submission_ring_tests` | 6/6 cases, 34 assertions PASS (7 in the new N4 case, on a real device) |
| `compositor`, `render_job_contract`, `render_graph` | PASS |
| `backend_registry` | 46/59 — the 13 failures are the documented pre-existing ones (`TICKET-AUDIT-2026-09-12-REMEDIATION.md §7`), unchanged by this ticket |

**Environment note.** `tools/check_architecture.py` needs a Python with
`tomllib` (≥3.11). This host's default `python3` is 3.10, which is why an
earlier session recorded the gate as NOT RUN; `python3.11` is present and the
gate runs fully. `tools/architecture_gate_selftest.sh` already selects such an
interpreter itself.

## Demolition Debt

### DD-1 — duplicated job-identity authority (FP-1)

| Campo | Contenuto |
|---|---|
| **Owner** | render-plan / compiler owners (working session) |
| **Reason** | none — this was a removal, recorded so the single authority has an explicit owner |
| **Exit condition** | satisfied: one type, one implementation, two consumers; equality of the two entry points proven by a test that compiles the same plan through both |
| **Equivalence test/gate** | `chronon3d_c_abi_tests` FP-1 cases (digest equality across entry points + canvas-inheritance rule) |
| **Removal scope** | `compiler::stages::compute_digests`, `compiler::stages::ProgramDigests`, the duplicate `render_settings_fingerprint` and `material()` lambda, the CLI's two canvas lines, `apply_fingerprint_settings`'s plan parameter |
| **Status** | `REMOVED` |

### DD-2 — measurement standing in for a decoupling decision (N4)

| Campo | Contenuto |
|---|---|
| **Owner** | Vulkan backend owners (working session) |
| **Reason** | the recording path is decoupled, but surface lifecycle/transfer ingress and the batch transitions still take both domains; the review asked for data before touching them |
| **Exit condition** | `dual_lock_hold_us` reported against total CPU frame time in an instrumented run shows the coupling is negligible (→ leave it, documented) **or** the sites move onto the same lease pattern (→ counters for `Dual` become zero and the enum value is deleted) |
| **Equivalence test/gate** | `chronon3d_vulkan_submission_ring_tests` (recording op takes the domains separately; the dual sites report themselves) + a same-SHA instrumented run |
| **Removal scope** | `Impl::LockDomain::Dual`, `DomainLock`'s second lock, the `dual_lock_*` counters, the 16 call sites — only if the decoupling branch is taken |
| **Status** | `ACTIVE` |

### DD-3 — compiled GPU program artifacts outside ProgramCache (CUDA/NV12 lane)

| Campo | Contenuto |
|---|---|
| **Owner** | media/video CUDA owners |
| **Reason** | `get_compiled_nv12_ptx()` retains NVRTC output in a function-local `static`, and `s_modules` (`unordered_map<CUcontext, weak_ptr<...>>` + `static std::mutex`) retains loaded CUmodules — compiled program artifacts with their own sync authority, not registered in `cache::ProgramCache` |
| **Exit condition** | the NV12 program is registered as a `ProgramCache` artifact keyed on a canonical fingerprint, and the module map becomes a lease over that artifact (its counters replace the ad-hoc statics) |
| **Equivalence test/gate** | NV12 compositor equivalence suite green on the same SHA, with `dual`/program-cache counters unchanged; the type-declaration gate cannot see a function-local `static`, so this exit condition is test-based, not scan-based |
| **Removal scope** | `get_compiled_nv12_ptx()`'s function-local static, `s_modules`, `s_module_mutex`, and the double-checked warm path around them |
| **Status** | `ACTIVE` |
