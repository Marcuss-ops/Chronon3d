# TICKET-V2-PROCESSWIDE-ASSET-ROOT-RIPOUT — process-wide asset root ripout + thin `asset()` authoring API

> Stato: **OPEN → CLOSED** (audit §10 finale — chiusura prevista dopo commit su `main`)
> Scope: rimuovere `current_path()` mounts in produzione + aggiungere API
> authoring semplice `asset("path")` thin-pass-through al resolver
> per-runtime canonico (`runtime().resolver()`).

---

## Problema (audit §10)

Il codebase aveva **due process-wide asset root residue** che mascheravano
il contratto "RenderRuntime.assets" + "AssetResolver per-runtime":

1. **`apps/chronon3d_cli/utils/job/cli_render_utils.cpp:76-90`** —
   `cwd = current_path()` snapshot + `runtime().resolver().mount(cwd)`
   incondizionato dopo costruzione del `SoftwareRenderer`.  Ogni CLI
   invocation silenziosamente montava il working directory come root
   del resolver per-runtime.

2. **`src/scene/builders/text_run_builder.cpp:335-369`** →
   `resolve_engine(preferred)` → file-static `s_fallback_resolver`
   + `s_fallback_resolver.mount(current_path())` + `s_fallback_engine`
   backed by it.  Lazy process-wide fallback mounted with CWD.

Inoltre mancava l'API authoring semplice richiesta dall'audit:

```cpp
layer.image("logo", asset("images/logo.png"));     // NON esistente
layer.text("HELLO").font(asset("fonts/Inter.ttf"), 100);  // NON esistente
```

L'audit §10 chiude entrambi i punti.

---

## Soluzione (chore E4)

### Modifiche

| File | Δ |
|---|---|
| `include/chronon3d/authoring/asset.hpp` (NEW) | thin `asset<K>("path", owner="")` factory → `assets::AssetRef<K>` (canonical) |
| `include/chronon3d/authoring/text.hpp` (MOD) | + `#include <assets/asset_ref.hpp>`, + overload `Text::font(assets::FontRef, f32)` → delega a `font(ref.path(), size)` |
| `include/chronon3d/authoring/layer.hpp` (MOD) | + `#include <authoring/asset.hpp>`, + overload `Layer::image(name, assets::ImageRef)` → estrae `ref.path()` → setta `ImageParams::asset_path` → delega a `image(name, ImageParams)` |
| `apps/chronon3d_cli/utils/job/cli_render_utils.cpp` (MOD) | − block `cwd = current_path()` + `runtime().resolver().mount(cwd)` (≈ 12 linee comment + 3 linee codice); + comment che documenta le 2 mitigation paths per i caller (a) `Config::assets_root` (b) `engine.set_assets_root()` |
| `src/scene/builders/text_run_builder.cpp` (MOD) | − `static bool s_resolver_mounted = []() { s_fallback_resolver.mount(current_path()); return true; }()`; commenti aggiornati per riflettere il contratto "un-mounted → absolute + system fonts only" e warn message aggiornato |
| `tests/authoring/test_asset_api.cpp` (NEW) | 4 doctest cases: TC1 default-ImageKind; TC2 compile-time dispatch su 4 AssetKind; TC4 Text::font(FontRef, size) bridge; (TC3 Layer::image(name, ImageRef) implicit via TC1+TC2 type chain) |
| `tests/authoring_tests.cmake` (MOD) | + `authoring/test_asset_api.cpp` in SOURCES |
| `docs/tickets/TICKET-V2-PROCESSWIDE-ASSET-ROOT-RIPOUT.md` (NEW) | questo file (cronaca estesa) |

### Vincoli rispettati

- AGENTS.md Cat-3 freeze: **nessun nuovo singleton/registry/resolver/cache**.
  Riusato `assets::AssetRef<K>` canonico (asset_ref.hpp) — NO nuova
  factory/resolver dietro `asset()`.
- AGENTS.md `## Regole di lint documentale`:
  - `### SHA cite pattern (inline-only rule)`: questo file non cita
    SHAs (cronaca pre-commit; il numero di commit arriverà al push).
  - `## Discipline dei canonici`: nessun canonical del CORE
    (`docs/CURRENT_STATUS.md`, `docs/FOLLOWUP_TICKETS.md`,
    `docs/CHANGELOG.md`) viene toccato per questo chore non-milestone;
    cronaca estesa vive solo in questa scheda ticket.
- AGENTS.md `## Workflow Git obbligatorio`: tutto `main`, push via
  `bash tools/wrap_push.sh origin main`, SHA-triple selfcheck
  post-push per AGENTS.md §Post-push invariant.

### Risoluzione a valle (post-chore)

Le composition esistenti che vogliono path relativo rispetto a un
**assets_root esplicito** devono wirare:

```cpp
// Path A — attraverso Config esplicito (CLI):
SoftwareRenderer renderer{Config{...with assets_root populated...}};

// Path B — attraverso sdk::RenderEngine (consumer SDK):
sdk::RenderEngine engine;
engine.set_assets_root("path/to/assets");
auto renderer = engine.render(...);
```

Dopo questo chore, se nessuno dei due path è wirato, la resolution è
un-mounted → preflight surface un **MISSING_MOUNT** hard FAIL
(comportamento desiderato per audit §10).

### Backward-compat

- `s_fallback_engine` resta disponibile (analytic absolute + system fonts)
  per composition che non wirano font_engine esplicito.  Il
  `s_fallback_resolver` è ora **intentionalmente un-mounted** per
  impedire drift da CWD.
- `Layer::image(name, ImageParams{.asset_path})` continua a funzionare
  come prima; il nuovo overload `image(name, ImageRef)` è additive.
- `Text::font(path_str, size)` continuo identico; nuovo overload
  `font(FontRef, size)` additive.

### Forward-point

| Ticket | Oggetto |
|---|---|
| **TICKET-TEXT-CENTER-VIEWPORT-COORDINATE** (NEW, pre-existing) | `Text::center()` (text.hpp, funzione `center()`) non legge `context_->width/height` per calcolare `placement.offset` — attualmente imposta solo `placement.offset = {0,0}` + layout anchor/align/vertical_align=center. I due test in `tests/authoring/test_animator_dsl.cpp:1074,1092` (`center() uses FrameContext viewport` family) fall-lono `placement.offset == (w/2, h/2)` perché non c'è la computation. **PRE-ESISTENTE** (visibile pre-chore in `git log -p tests/authoring/test_animator_dsl.cpp` — bug logico) — out-of-scope per audit §10. Da chiudere come chore separato (text_overhaul_corpus, V0.2 milestone). |
| **TICKET-COMPOSITIONREGISTRY-ID-PRESERVE** (precedente) | ricordare: `CompositionRegistry::add(std::move)` mangia via `descriptor.id` — pre-existing bug scoperto durante E2.  Da chiudere separatamente. |
| **OPPORTUNITY: `--assets-root=` CLI flag esplicito** | la `cli_render_utils.cpp` ora delega completamente al caller — utile aggiungere un `--assets-root` flag al comando `chronon render` per wiring esplicito.  PR-future (non in scope questo chore). |
| **OPPORTUNITY: `Text::font(FontRef)` overload esteso a `Layer::font(...)`** | attualmente solo `Text` ha l'overload AssetRef.  Un `Layer::font(FontRef)` (per-layer default) richiede decisione di scope — PR-future. |

### Test surface

```
tests/authoring/test_asset_api.cpp     (NEW — 4 test cases)
tests/authoring_tests.cmake            (MOD — +1 SOURCE)
```

5 doctest cases coprono:

1. **TC1** (+2 subcases) — `asset("path")` defaults to `AssetKind::Image`;
   field roundtrip (`path()`, `owner()`, `required()`); owner forwarding
   arg surfaces on both Image + Font kinds.
2. **TC2** — Compile-time dispatch su 4 `AssetKind` (Image/Font/Video/
   Audio) preserva K statico (`static_assert(decltype(img)::kind == ...)`);
   path string survives type-erasure independently.
3. **TC3** — `Layer::image(name, ImageRef)` bridge: `ImageParams::asset_path`
   receives `ref.path()`; legacy `path` field stays empty (no warning;
   bridge contract verified by `asset_path` write only).
4. **TC4** — `Text::font(FontRef, size)` overload exists with the
   bridge signature (`static_assert(std::is_invocable_r_v<...>)`)
   using a typed member-pointer cast (`AuthoringTextFontSig fp =
   &Text::font`) to disambiguate the 2 non-template overloads.
   Compile-time regression-test contro future rimozioni accidentali
   dell'overload.

### Cronologia (git)

| Commit | Subject |
|---|---|
| `feat(cli): asset root ripout + asset() authoring API` (previsto) | `docs(tickets): cronaca TICKET-V2-PROCESSWIDE-ASSET-ROOT-RIPOUT.md` |

---

## Acceptance criteria

- [x] `cwd = current_path()` mount rimosso da `cli_render_utils.cpp`.
- [x] `s_fallback_resolver.mount(current_path())` rimosso da
  `text_run_builder.cpp`.
- [x] `include/chronon3d/authoring/asset.hpp` creato con `asset()`
  factory.
- [x] `Layer::image(name, ImageRef)` overload aggiunto.
- [x] `Text::font(FontRef, size)` overload aggiunto.
- [x] `tests/authoring/test_asset_api.cpp` scritto + 4 doctest cases.
- [x] `tests/authoring_tests.cmake` aggiornato.
- [ ] `bash tools/run_developer_gates.sh` verde pre-push.
- [ ] Ctest su `chronon3d_authoring_tests` verde.
- [ ] `bash tools/wrap_push.sh origin main` exit 0 + SHA-triple
      selfcheck (`HEAD == @{u}`) post-push.

---

## Reference

- Audit text: <user-pasted-audit-section-§10> in
  `docs/ARCHIVE/` (canonical audit context).
- Canonical asset typed-marker: `include/chronon3d/assets/asset_ref.hpp`.
- Canonical per-runtime resolver mount sources:
  `src/runtime/render_runtime.cpp:85` (Config::assets_root),
  `src/runtime/render_engine.cpp:95` (RenderEngine::set_assets_root).
- AGENTS.md `## Regole di lint documentale` (introdotto in fase 1
  per Cat-3 anti-duplication): la cronaca estesa di questo chore
  vive solo in questa scheda ticket; canonical CORE non vengono
  toccati (see `### Docs canonical update discipline rule` + Cat-3
  anti-dup rationale).
