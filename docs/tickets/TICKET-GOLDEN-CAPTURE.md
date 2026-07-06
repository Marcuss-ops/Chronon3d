# TICKET-GOLDEN-CAPTURE — Diagnostic Capture Pipeline

> **Status:** OPEN (DIAGNOSTIC commit la diagnosi, fix in commit successivo).
> **Priorità:** P1.
> **Area:** gate #5 cat-2 (text golden coverage depth).
> **Trigger:** ADR-014 Phase C — `CHRONON3D_UPDATE_GOLDENS=1 bash tools/test_golden_driver.sh update linux-fast-dev` ritorna `exit 0` ma **0 PNG** prodotti su disco.
> **Freeze:** AGENTS.md v0.1 Cat-2 (test deterministici) freeze-compliant. Zero public API expansion, zero new registry/resolver/sampler/cache.

---

## 1. Sintomo (osservabile)

| Comando | Atteso | Osservato |
|---|---|---|
| `CHRONON3D_UPDATE_GOLDENS=1 bash tools/test_golden_driver.sh update linux-fast-dev` | `28 PNG × 12 user-spec = >12 PNG` scritti in `test_renders/golden/text/` | **`0 PNG`** (impl-side report: `1/1 Test #30: TextGoldenUserSpec ... Passed (0.01s)` implausibilmente veloce — 12 test rendering in 0.01s è fisicamente impossibile per glyph compositing reale) |
| `find . -name 'user_spec_*.png' -o -name 'ae_*.png'` | ≥ 1 PNG (asset catturati) | **`0 match`** sull'intero working tree |
| `cmake --build build/chronon/linux-fast-dev --target chronon3d_text_golden_tests` | binary link OK | OK (la build non è rot) |
| `./build/.../chronon3d_text_golden_tests --test-case="UserSpec*"` | esegue 12 TEST_CASE, ~4-10s, scrive ≥ 12 PNG | termina in **0.01s**, scrive 0 PNG |

---

## 2. Conferma che il codice è wired (non stub)

Il sospetto immediato è "i test sono stub". Verifica macchina:

- `tests/text_golden/user_spec/01_text_basic_centered.cpp` letto integralmente:
  - include reali: `<chronon3d/chronon3d.hpp>`, `<chronon3d/api/composition.hpp>`, `<chronon3d/api/renderer.hpp>`, `<chronon3d/scene/builders/scene_builder.hpp>`, `<chronon3d/backends/software/software_renderer.hpp>`.
  - `make_test01_config()` imposta i threshold ADATTI al test (max_mean_abs_error = 5/255, max_changed_pixel_ratio = 0.05, min_ssim = 0.92).
  - `build_test01_composition()` chiama `composition({.name=..., .width=1920, .height=1080, .frame_rate={30,1}, .duration=60}, lambda)` con uno SceneBuilder che crea un layer "hero" con `l.text("title", {.content={"Chronon3D Text Engine"}, .font={Inter-Bold.ttf, weight=700, size=96}, .layout={1920×1080, center+Middle}, .appearance={Color::white()}, .position={960,540,0}})` → pipeline canonica post-M1.5#3 (`TextDocument → TextResolver → FontEngine → HarfBuzz → TextLayoutEngine`).
  - `verify_golden(*fb, "user_spec_01_text_basic_centered", make_test01_config())` con REQUIRE di width/height + `CHECK(result.passed)` dopo l'eventuale `if (result.golden_missing) return;`.

→ **Codice è wired, NON stub.** Il problema è a valle della rigenerazione del framebuffer.

---

## 3. Architettura del codepath di capture

```
TEST_CASE("UserSpec 01: text basic centered...")
    │
    ├─ auto renderer = test::make_renderer();
    │
    ├─ auto comp = build_test01_composition();
    │
    ├─ auto fb = renderer.render(comp, Frame{0});     ← ① RENDERIZZAZIONE
    │
    ├─ REQUIRE(fb != nullptr);                        ← ② FRAMEBUFFER non null
    │
    ├─ REQUIRE(fb->width() == 1920);
    ├─ REQUIRE(fb->height() == 1080);
    │
    └─ auto result = verify_golden(*fb, ...);         ← ③ CAPTURE / VERIFY
        ├─ sanitize_case_name("user_spec_01_...") → "user_spec_01_text_basic_centered"
        ├─ res.golden_path = "test_renders/golden/text/user_spec_01_text_basic_centered.png"
        │
        ├─ UPDATE mode (env CHRONON3D_UPDATE_GOLDENS=1):
        │     ├─ save_png(rendered, "test_renders/golden/text/...png.tmp");
        │     ├─ std::filesystem::rename(tmp, golden_path);
        │     └─ res.golden_updated = true; res.passed = true;
        │
        └─ VERIFY mode (default):
              ├─ if !exists(golden_path) → res.golden_missing = true; return;  ← fallthrough `MESSAGE("Golden missing")` → codepath test bail-out **SENZA SCRIVERE PNG**
              └─ altrimenti load_png + compare_framebuffers + write artifacts
```

Punti di failure possibili:

| Stage | Patch | Possibile stop |
|---|---|---|
| ① | `renderer.render(comp, Frame{0})` | `make_renderer()` ritorna stub? engine-less? AssetResolver fallback fails? |
| ② | `fb != nullptr` / width / height | `REQUIRE(fb != nullptr)` FAIL → test abort, no PNG |
| ③ capture | `save_png` o `rename` | write fail (permessi, disk quota, FS read-only) / directory non creata? |
| ③ verify | `exists(golden_path)` + `load_png` | Path resolution fallisce |

---

## 4. Hypothesis Matrix (4 candidati root-cause)

### H1 (probabilmente vero) — `working_directory` ctest override

`tests/text_golden_tests.cmake` configura `add_test(... WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})` (riga 53), che sarebbe `${CMAKE_SOURCE_DIR}/test_renders/...`. **MA** lo script `tools/test_golden_driver.sh` step 5 esegue `ctest --test-dir "$BUILD_DIR" -R "$CTEST_REGEX" --output-on-failure` — `ctest --test-dir` **SOVRASCRIVE** `WORKING_DIRECTORY` al `${BUILD_DIR}`. Quindi:

- `cfg.golden_directory = "test_renders/golden/text"` (path **relativo**)
- CWD a runtime = `build/chronon/linux-fast-dev/` (perché `ctest --test-dir` lo forza)
- Path effettivo di scrittura = `build/chronon/linux-fast-dev/test_renders/golden/text/user_spec_01_*.png`
- `build/` è gitignored → `find . -name 'user_spec_*.png'` non lo vede, ma i PNG CI SONO (build/, non repo root).

Smoking gun 1: il driver stesso scrive a riga 91: `log "artifacts: $BUILD_DIR/test_renders/golden/text/user_spec_*.png (if any)"` — conferma che il flow è `BUILD_DIR/test_renders/...`, non `${CMAKE_SOURCE_DIR}/test_renders/...` come afferma invece `tools/check_doc_sync.sh` e ADR-014.

Smoking gun 2: la spiegazione del "Passed (0.01s)" — il test passa in `verify_golden` mode solo se `res.passed == true`. Ma nel **verify** mode, se il golden è missing, il helper ritorna `passed=false`, `golden_missing=true`. Il TEST_CASE quindi entra nel branch `if (result.golden_missing) { MESSAGE("Golden missing"); return; }` e **NON chiama `CHECK(result.passed)`** → doctest riporta il TEST_CASE come PASSED perché non ci sono `FAIL` / `REQUIRE` fallite. Questa è la causa del timing 0.01s: i test non fanno nulla perché i golden sono già "missing" (perché cercati sotto `BUILD_DIR/test_renders/...` ma il driver li cerca probabilmente altrove) → ctest report "passed".

### H2 — `test::make_renderer()` returns stub renderer

Pattern frequente in Chronon3D: helper `test::make_renderer()` può ritornare un renderer che non popola glyphs. Verificare: `tests/helpers/test_utils.hpp` (o simile) + ispezionare cosa `make_renderer` ritorna. Se ritorna un `SoftwareRenderer` con `asset_resolver_` vuoto o `text_resources == nullptr`, `renderer.render(comp, Frame{0})` produce un framebuffer nero o completo di un Color:FillStyle vuoto. `REQUIRE(fb != nullptr)` passa (il fb esiste), `REQUIRE(fb->width()==1920 && fb->height()==1080)` passa (dimensioni ok), ma il PNG catturato è uniformemente nero → `actual.png` salvato sotto il branch fail → **MA** noi siamo in **update mode**, quindi `save_png` viene chiamato incondizionatamente.

In update mode, H2 si manifesterebbe come: PNG scritto ma tutto nero (230400 pixel, mean RGB = 0). Verificabile con `ls -la build/chronon/linux-fast-dev/test_renders/golden/text/`.

### H3 — Asset / font path resolution

`assets/fonts/Inter-Bold.ttf` (path relativo nel test) viene risolto a runtime da `AssetResolver`. Se il CWD è `build/chronon/linux-fast-dev/` invece di `${CMAKE_SOURCE_DIR}/`, il resolver cerca `build/chronon/linux-fast-dev/assets/fonts/Inter-Bold.ttf` → non esiste → `renderer.render` produce comunque un framebuffer (con un fallback font o vuoto). Stesso symptom di H2: PNG scritto, ma caratteri non renderizzati.

Smoking gun 3: combo di H1 + H3 — sia il percorso di WRITE del golden sia il percorso di LETTURA del font sono sotto `build/`, e nessuno dei due esiste → backend produce un fb nero + driver tenta di scrivere un PNG sotto `build/...` e apparentemente ci riesce ma il PNG è "fuffa" (non SVG, non avvertimento).

### H4 — `tests/text_golden_tests.cmake` registered target mismatch

`tests/text_golden_tests.cmake` definisce `add_executable(chronon3d_text_golden_tests ...)` con i 17 source (12 user_spec + 5 ae_parity). Driver chiama `cmake --build ... --target chronon3d_text_golden_tests`. Sembra OK. Verificare però se `chronon3d_text_golden_tests` viene linkato correttamente contro `chronon3d_software` + `chronon3d_sdk`.

---

## 5. Repro Recipe minimale

```bash
# Precondizione: working tree clean su main
git fetch origin && git pull --ff-only origin main

# 1. Verifica precondizione
ls tests/text_golden/user_spec/*.cpp | wc -l   # atteso 12
ls tests/text_golden/ae_parity/*.cpp | wc -l   # atteso 5

# 2. Configure (idempotente)
cmake --preset linux-fast-dev

# 3. Build del target
cmake --build build/chronon/linux-fast-dev \
    --target chronon3d_text_golden_tests -j$(nproc)

# 4a. Esegui direttamente il binary con CWD = repo root (NO `ctest --test-dir`)
cd $REPO_ROOT
CHRONON3D_UPDATE_GOLDENS=1 \
    ./build/chronon/linux-fast-dev/tests/chronon3d_text_golden_tests \
    --test-case="UserSpec*"
# Atteso: PNG scritti in <REPO_ROOT>/test_renders/golden/text/user_spec_01_*.png

# 4b. Verifica col driver (contro il flow ctest)
CHRONON3D_UPDATE_GOLDENS=1 \
    bash tools/test_golden_driver.sh update linux-fast-dev
# Driver flow: ctest --test-dir $BUILD_DIR; se H1 è vero, scrive in $BUILD_DIR/test_renders/...
```

Output diagnostici da raccogliere:

```bash
# Globale: dove attera il PNG in update mode?
find . -name 'user_spec_*' -type f | head -20

# Permessi test_renders/
stat -c '%a %n' test_renders/ build/chronon/*/test_renders/ 2>/dev/null

# sha256 dei PNG eventualmente catturati
sha256sum build/chronon/linux-fast-dev/test_renders/golden/text/*.png 2>/dev/null

# Verifica del binary
objdump -d build/chronon/linux-fast-dev/tests/chronon3d_text_golden_tests | grep -c verify_golden
# atteso >0 (simbolo presente)
```

---

## 6. Proposed Fix-Path (post-diagnosi)

A seconda dell'ipotesi confermata:

### Fix A — `tests/text_golden_tests.cmake` WORKING_DIRECTORY bump-up

Sostituire `WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}` (riga 53) con un path ASSOLUTO `${CMAKE_SOURCE_DIR}/test_renders` oppure rimuoverlo. Driver flow `ctest --test-dir` lo sovrascrive comunque → la SOLUZIONE robusta è:

- **Opzione 1**: cambiare il driver `tools/test_golden_driver.sh` Step 5 da `ctest --test-dir $BUILD_DIR` a `ctest` (senza `--test-dir`) — così `ctest` eredita il working dir corrente dell'utente. Verificare se poi eredita WORKING_DIRECTORY dichiarato in `add_test`.
- **Opzione 2**: rimuovere `WORKING_DIRECTORY` dal `add_test`, e forzare il CWD del driver a `${CMAKE_SOURCE_DIR}` (`cd $REPO_ROOT` prima di `ctest`).
- **Opzione 3** (la più robusta): cambiare `cfg.golden_directory` da `"test_renders/golden/text"` (relativo) a un path ASSOLUTO derivato da `CHRONON3D_REPO_ROOT` env var letta all'avvio del test (additiva, compat backward).

### Fix B — `test::make_renderer()` fixture unload

Se H2/H3 veri, sostituire `test::make_renderer()` con un renderer wired che carica `assets/fonts/Inter-Bold.ttf` come default font. Test determinism: FNV-1a hash su fb byte-exact (in update mode il PNG catturato è il ground-truth).

### Fix C — Stale binary cache

`find build/ -name 'chronon3d_text_golden_tests*' -exec rm {} \;` + re-build + re-run. Se dopo questo i PNG sono scritti correttamente, il problema era cache stale.

---

## 7. Acceptance Criteria (per chiusura ticket)

- [ ] `CHRONON3D_UPDATE_GOLDENS=1 bash tools/test_golden_driver.sh update linux-fast-dev` produce ≥ 17 PNG (`12 user_spec + 5 ae_parity`) sotto `<REPO_ROOT>/test_renders/golden/text/`.
- [ ] I PNG non sono tutti-neri né tutti-bianchi: `python3 -c "from PIL import Image; print(Image.open('test_renders/golden/text/user_spec_01_text_basic_centered.png').getextrema())"` mostra range RGB non-degenerate (la bitmap contenuto testo reale).
- [ ] Re-run `bash tools/test_golden_driver.sh` (verify mode, no env var): `ctest -R TextGoldenUserSpec` mostra `1+ test Passed`, no diff, tempo realistico (≥ 3s).
- [ ] `git log -n 5 --oneline` mostra il commit fix come figlio diretto di questo commit diagnostico.
- [ ] ADR-014 Decision 1 + 5 forward-only TICKET-GOLDEN-* restano coerenti (nessuna delle 5 forward-only unlockable finché capture non risolta).

---

## 8. Cross-references

- **TICKET** (canonical row): `docs/FOLLOWUP_TICKETS.md` §"Open blockers" row `TICKET-GOLDEN-CAPTURE`.
- **Infrastruttura interessata**: `tools/test_golden_driver.sh`, `tests/text_golden_tests.cmake`, `tests/visual/support/golden_test.{hpp,cpp}`, `tests/visual/support/image_diff.{hpp,cpp}`.
- **Test source (esempio letto)**: `tests/text_golden/user_spec/01_text_basic_centered.cpp`.
- **AE-parity suite downstream**: `tests/text_golden/ae_parity/ae_01_cinematic_title_reveal.cpp` (5 scene, dipendenti dal capture fix).
- **ADR**: `docs/adr/ADR-014-text-golden-coverage.md` Decision 1 (12 user-spec) + Decision 2 (5 forward-only).
- **Feature freeze**: AGENTS.md v0.1 §"🔴 Feature Freeze — V0.1" Cat-2 (test deterministici — golden test, sentinel hash, regression gate) consentito. Questo commit è diagnostico (no code change); il fix-path è una sua continuazione Cat-2 compliant.

---

## 9. Tag di categoria & asset

- **Asset category**: `CAT-2 DIAGNOSTIC` (sotto-folder di `INSTALL_PIPELINE_PLUMBING`-style diagnostic doc-only).
- **AGENTS.md v0.1 compliance**: Cat-2 freeze + Cat-5 doc alignment. Zero nuovi simboli in `include/chronon3d/`; zero nuove classi / funzioni / registry / cache / resolver / service-locator esposti.
- **No Python deps**, no JSON come primary authoring, no GUI/browser/GPU.
