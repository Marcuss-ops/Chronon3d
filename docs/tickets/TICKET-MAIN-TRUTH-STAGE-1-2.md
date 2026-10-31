# TICKET-MAIN-TRUTH-STAGE-1-2 — main truth + Wave 0 closeout

**Stato:** PARTIAL — runner execution restored on latest observed SHA; same-SHA certification is not green
**Priorità:** P0  
**Data:** 2026-09-04  
**Baseline censita:** `main@eb56a6f6c59e6d5ae3eefa1f1cbd69e3ee1f58d1`

## TODO — cosa manca (agg. 2026-10-29)

- [x] **Runner dispatch osservato funzionante** — sullo SHA remoto `a214fad27efdaa597ccf4e4a8ae34baa6677146e`, i workflow del 2026-10-28 hanno allocato runner ed eseguito gli step; il blocco storico `steps=[]`, `runner_id=0` non descrive più l'ultimo stato osservato.
- [ ] **`Chronon CI` green sullo SHA risultante di `main`** — logica: close rule del ticket: nessun passaggio VERIFY→DONE senza CI same-SHA green. Gate: run green sul tip di `main`.
- [ ] **Registrare la post-push main-tip verification** — logica: evidenza datata dopo il commit, come richiesto dai criteri di accettazione residui. Dove: questa scheda + `docs/stato/chronon3d.md`. Gate: riga di stato con SHA + esito.

## Update 2026-10-29 — latest observed Actions state

Read-only `gh run list` confirms the latest listed batch is on
`a214fad27efdaa597ccf4e4a8ae34baa6677146e`: Reproducible release artifact
passed; Chronon CI, Developer Gates and CFI were cancelled; Chronon Nightly,
CodeQL, `chronon3d-runtime`, and Resource leak certification failed. The
runtime build failed during the Docker CMake/build step. Resource leak logs
reported `-Wunused-result` for `write()` in `src/core/cancellation_token.cpp`;
the local source now stores the return value to satisfy the hardened warning
policy. The Chronon CI Linux Release job also exposed a P0-5A test whose helper
was hidden behind `CHRONON3D_ENABLE_NATIVE_FFMPEG` while its caller was not,
plus an ignored `[[nodiscard]]` parse result inside `CHECK_THROWS`; both local
test-source defects are corrected. Gate 7's complete job log showed its
primary SDK install failure: `Chronon3DPublicHeaders.cmake` still installed the
removed `runtime/render_surface_handle.hpp` path; the consumer `find_package`
and missing-library failures were downstream effects. The manifest now names
the canonical `core/types/render_surface_handle.hpp`; a static audit found no
other live missing header entries (the two remaining textual matches are an
intentionally hidden file and an include-directory exclusion). This fix also
needs the final SDK gate rerun. The CI cancellation/timeout and runtime failure
still need triage. These runs establish runner allocation, but they do not
certify this dirty worktree or satisfy the same-SHA green close rule.

## Update 2026-10-31 — current main-tip campaign and PR #61 attribution

Fetched `origin/main` and fast-forwarded the clean worktree to
`919ed8f5ff4fe9e159150f903d521f42b1346b6c`. Its `Chronon CI` run
`41198698232` is still queued; all listed jobs have no start/completion time.
Same-SHA Stage 1 is therefore not green and no cleanup wave is authorized yet.

For LEANUP-001 PR #61, compared Chronon CI run `40223358034` on parent
`776fc721810eba0b54532932dbbc6e39e79793c1` with run `40227431837` on PR commit
`21b557eb4fb4bef4b8e3ad99c3fea0a036239d9e`. Both contain the same failures:
the checkout script addresses a nonexistent `vcpkg` path; `ccache` is missing;
the no-content workflow invokes a hidden preset and the public-header job
requests nonexistent target `chronon3d_pipeline`; CLI configurations fail to
find FFmpeg headers. PR #61's diff only changes conditional ICU linkage and a
CTest name regex, and neither changed file owns those failing paths. The
available before/after evidence does not attribute these failures to #61.
This identifies CI/configuration and existing target-boundary defects to fix
under Wave 0; it does not establish a green baseline.

Static inspection initially found `packet_assembler.hpp` included from both
daemon files, importing libav headers into the no-native-FFmpeg build. The
daemon header include was unused and remains removed. The protocol source uses
`SegmentAssemblyRequest` when native FFmpeg is enabled, so that include is now
conditional on `CHRONON3D_ENABLE_NATIVE_FFMPEG`. `linux-fast-dev` and
`linux-video-fast-dev` with CUDA interop disabled both built successfully
locally; the standard video preset cannot configure here because `cuda.h` and
`libcuda` are unavailable. Tests and same-SHA CI remain unrun.

## Update 2026-10-05 — runner dispatch remains blocked

Fetched `main@b67a750e6` and queried the latest Actions runs for that exact
SHA. Runs `37313378103` (ABI compatibility) and `37313378100`
(chronon3d-runtime) both failed within seconds; each job reports `steps: []`.
This is still pre-execution runner dispatch failure, so no source-level result
can be inferred. Restoring account/organization runner billing or spending
limits requires GitHub owner access and remains an external blocker.

## Update 2026-10-03 — same-SHA CI check

- Local HEAD is now `5d960421dd1b`, matching fetched remote `main`; the prior
  dirty worktree was restored on top after the fast-forward. Its GitHub run
  [37102621629](https://github.com/Marcuss-ops/Chronon3d/actions/runs/37102621629)
  reports failure on every job, but jobs completed in seconds with no runner
  allocation and no steps. This is a runner-dispatch blocker, not source-level
  test evidence. The restored dirty worktree is not covered by that result.
- Follow-up observation: scheduled runs `37107348776` (Chronon Nightly) and
  `37110073977` (libFuzzer nightly) on the same remote SHA also failed with
  empty job steps. Local rebuilt Core and CLI suites pass, but do not replace
  the missing GitHub execution. ChrononTemplate currently has no remote
  workflow; its local workflow remains uncommitted with the preserved WIP.
- Do not mark VERIFY/DONE or add product scope until Actions dispatch is
  restored and a reviewed, identifiable SHA passes the required gates.

## Obiettivo

Rendere il `main` remoto la sola verità operativa prima di qualsiasi nuova architettura: clean-checkout build consistency, chiusura della migrazione frame-slot, coerenza del Vulkan pipeline cache e census finale della Wave 0.

## Finding P0 — pipeline cache

`src/backends/vulkan/vulkan_backend_lifecycle_private.cpp` crea un `VkPipelineCache`, lo passa a tutti i `vkCreateComputePipelines` e ne salva i dati su disco attraverso `kernels.pipeline_cache`.

Sul baseline censito, `src/backends/vulkan/vulkan_kernel_store.hpp` non dichiarava più `pipeline_cache`. Il lifecycle e il suo owner erano quindi incoerenti nel clean checkout.

### Fix

- `VulkanKernelStore` possiede esplicitamente `VkPipelineCache pipeline_cache{VK_NULL_HANDLE}`.
- `VulkanKernelStore::destroy()` distrugge e azzera il cache handle insieme agli altri oggetti Vulkan di sua ownership.
- La dipendenza standard-library necessaria al persistence path (`std::filesystem`) è resa disponibile dal boundary privato Vulkan invece di dipendere accidentalmente da include transitivi non correlati.
- `test_vulkan_kernel_store.cpp` blocca l'esistenza dell'owner e il suo stato iniziale nullo.

Questo fix NON implementa il futuro hardening del persistent cache (UUID/device keying, ABI fingerprint, corruption handling, atomic save, daemon concurrency). Quello resta Stage 7.

## Runtime link failure follow-up — 2026-10-29

Il log del job runtime sullo SHA `a214fad` mostra `ld.gold` rifiutare relocation
`R_X86_64_PC32` da `libavcodec.a`/`libavutil.a` verso simboli dati FFmpeg durante
il link di `libchronon3d_c.so`. Il port FFmpeg già usa `--enable-pic`; l'archivio
locale conferma relocation PC32 nell'assembly x86 e simboli FFmpeg interni
visibili come `GLOBAL DEFAULT`. È stato aggiunto
`LINKER:--exclude-libs,ALL` soltanto a `chronon3d_c`: i simboli dagli archivi
statici diventano locali alla DSO, mentre l'API C esplicitamente annotata resta
esportata. **Fix in verifica**: serve il link SDK/runtime nel batch finale; il
job remoto e il ticket main truth restano aperti fino al risultato same-SHA.

## Frame-slot migration census

Observed on `main`:

- `include/chronon3d/runtime/frame/frame_execution_slot.hpp` presente.
- `FrameSlotPool`, `GpuCompletionTracker`, `FrameQueue` presenti.
- `src/runtime/CMakeLists.txt` registra i tre TU authority (`frame_slot_pool.cpp`, `gpu_completion_tracker.cpp`, `frame_queue.cpp`).
- Il vecchio `FrameSlotPipeline` è stato rimosso dalla history di cleanup.
- Il vecchio `frame_execution_slot_ring.hpp` non è più parte del tree corrente.

**Verdetto:** migrazione strutturale chiusa. Non reintrodurre il facade ritirato per soddisfare nomi storici nelle note.

## Wave 0 census

| Area | Verdetto | Evidenza/azione |
|---|---|---|
| Surface authority | DONE | `CompiledResourceTable` è il placement authority; Vulkan è materialization/binding. |
| GraphExecutor | DONE | unica authority produttiva; helper executor hanno ownership distinte. |
| Descriptor authority | DONE | `VulkanDescriptorAuthority` possiede allocazione/handle; alias in `Impl` sono riferimenti compatibility-only. |
| FFmpeg subprocess | OPEN demolition debt | `FfmpegPipeSink` resta solo come path non-native; la cancellazione fisica è vietata finché `TICKET-FFMPEG-PIPE-SINK-DEMOLITION` non soddisfa tutte le exit conditions. |
| DeviceScheduler | DONE | implementazione + focused tests esistono. |
| CLI typed config / RenderPlan | DONE focused | il path CLI V3/RenderPlan resta canonico. |
| Telemetry | PARTIAL / contained | default-path store recording gated sul consumer SQLite reale; delete fisico residuo separato. |
| Docs | DONE in this closeout | `CURRENT_STATUS`, `CHRONON_PLAN`, `FOLLOWUP_TICKETS` riallineati. |

## Acceptance criteria

- [x] Caller/owner census frame-slot eseguito.
- [x] Negative census: non ripristinare `FrameExecutionSlotRing`/`FrameSlotPipeline` legacy.
- [x] Pipeline-cache owner coerente con i caller del lifecycle.
- [x] Regression lock sul `VulkanKernelStore` aggiornato.
- [x] Wave 0 census documentato senza demolizioni premature.
- [x] Canonici principali riconciliati.
- [ ] `Chronon CI` green sullo SHA risultante di `main`.
- [ ] Post-push main-tip verification registrata dopo il commit.

## Same-SHA CI diagnostic — `cbc7b4350` (2026-09-17)

The exact current `main` SHA `cbc7b4350b1ef17ac2077f01212d992eb500c84b`
was rerun in GitHub Actions run `35203772918`. The rerun failed before runner
execution: every job has `steps=[]`, and all jobs terminate within seconds.
Evidence: [`ci-preexecution-cbc7b4350-2026-09-17`](../evidence/ci-preexecution-cbc7b4350-2026-09-17.md).

The earlier pre-execution diagnostic for `a16c86de2` remains historical:

The GitHub runs for this SHA are currently blocked before runner acquisition:
all jobs have `runner_id=0`, `runner_group_id=0` and `steps=[]`, including
unrelated Linux, Windows and sanitizer jobs. The workflow files pass local
`actionlint` and YAML shape validation. Evidence:
[`ci-preexecution-a16c86de2`](../evidence/ci-preexecution-a16c86de2.md).

This does not close the acceptance criteria; it identifies the remaining
blocker as GitHub Actions dispatch/runner service state rather than a source
or test failure.

## Close rule

Passare `VERIFY -> DONE` solo quando il commit di questo closeout è realmente il tip di `main` e la CI same-SHA è verde. Il ticket FFmpeg subprocess resta indipendente: questo ticket può chiudere anche con quel demolition debt OPEN, purché il path sia classificato e non sia una seconda production authority nei build native.


## SDK C ABI closeout — 2026-10-29

Configured `CHRONON3D_BUILD_C_API=ON` and built `chronon3d_c` successfully
after adding GNU/Clang Linux `--exclude-libs,ALL` to hide FFmpeg static-archive
symbols from the shared-library export. Built and ran `chronon3d_c_abi_tests`
from `/dev/shm/chronon3d-final-bin` to avoid filling the workspace filesystem:
CTest 1/1 PASS (278.26 s). This closes the local SDK link/ABI check only; the
ticket remains PARTIAL until reviewed changes are on `main` and same-SHA CI is
green.
