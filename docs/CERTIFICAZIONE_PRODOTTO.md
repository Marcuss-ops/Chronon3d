# Certificazione Prodotto Chronon3D — Piano d'Azione

> **Obiettivo:** Verificare che Chronon3D sia integrabile come SDK, produca testi puliti,
> e generi golden PNG After Effects-like. NO nuove feature — solo certificazione.
>
> **Regola:** Solo `main`, commit frequenti, push diretti. Zero branch.

## Fase 1 — Baseline e Build

Verifica che il progetto compili e che gli 11 gate architetturali siano verdi.

### Azione 1.1 — Build + 11 gate
```bash
bash tools/chronon-linux.sh
bash tools/check_architecture_boundaries.sh
bash tools/check_architecture_boundaries_selftest.sh
bash tools/check_software_renderer_boundary.sh
bash tools/check_gitignored_dirs.sh
bash tools/audit_software_renderer.sh
bash tools/check_camera_architecture.sh
bash tools/check_doc_sync.sh
bash tools/check_filename_drift.sh
bash tools/test_architectural.sh
bash tools/install_consumer_test.sh
python3 tools/check_backend_sanitization.py
```
**Criterio PASS:** 11/11 PASS. **Criterio FAIL:** Qualsiasi gate rosso.

### Azione 1.2 — CLI list
```bash
./build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli list
```
**Criterio PASS:** Lista composizioni popolata. **FAIL:** Errore o lista vuota.

### Azione 1.3 — Render base frame
```bash
./build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli render BackgroundGrid --frames 0 -o output/cert_test_frame.png --report
```
**Criterio PASS:** PNG valido generato. **FAIL:** File assente o nero.

---

## Fase 2 — Test "Remotion-like Integration"

Verifica che un progetto esterno possa usare Chronon3D via `find_package(Chronon3D CONFIG REQUIRED)`.

### Azione 2.1 — Consumer SDK esistente
```bash
bash tools/install_consumer_test.sh
```
**Criterio PASS:** Exit 0 + `[BOUNDARY-OK]`. **FAIL:** Build rotto o PNG nero.

### Azione 2.2 — Consumer con testo reale (NUOVO)
Creare `tests/install_consumer/main_text.cpp` che:
- Usa solo `Chronon3D::SDK` (`find_package`)
- **NON** include `advanced/`, `internal.hpp`, `runtime.hpp`
- Renderizza un `SceneBuilder` con `l.text("Hello Chronon3D", TextSpec{...})`
- Usa font Inter-Bold (o fallback system font)
- Salva PNG non nero con testo visibile

```cpp
// Pseudocodice del test atteso:
#include <chronon3d/chronon3d.hpp>
int main() {
    auto runtime = chronon3d::RenderRuntime::create({}).value();
    auto scene = /* composition con testo centrato */;
    auto fb = runtime.render_frame(scene, Frame{0});
    chronon3d::save_png(fb, "output/text_consumer_test.png");
    // Verifica che il PNG non sia nero
}
```
**Criterio PASS:** Compila con solo `Chronon3D::SDK`, testo visibile nel PNG.
**FAIL:** Richiede header interni, testo assente, PNG nero.

---

## Fase 3 — Test "Testi Puliti e Pronti"

Suite di test testuali progressivi (A→G).

### Azione 3.1 — Test A: Titolo semplice 16:9
Composizione 1920x1080, testo centrato "EPIC TITLE", font Inter Bold 120pt.
**PASS:** PNG con testo leggibile e centrato.

### Azione 3.2 — Test B: Titolo 9:16
Stessa scena in 1080x1920 (TikTok/Shorts/Reels).
**PASS:** Layout corretto in verticale, nessun taglio.

### Azione 3.3 — Test C: Lower third
Testo in basso con box semi-trasparente, safe margins.
**PASS:** "BREAKING NEWS" + subtitle ben posizionati.

### Azione 3.4 — Test D: Testo lungo con wrapping
Frase lunga con wrapping automatico, line spacing, vertical align.
**PASS:** Nessuna parola tagliata, overflow gestito.

### Azione 3.5 — Test E: Accenti e lingue (HarfBuzz/FriBidi)
`"È successo qualcosa"`, `"João não sabia"`, `"مرحبا بالعالم"`, `"שלום עולם"`.
**PASS:** Shaping corretto per ogni lingua. **EXPECTED FAIL:** RTL potrebbe avere problemi.

### Azione 3.6 — Test F: Emoji e CJK
`"🔥 Breaking News"`, `"こんにちは世界"`, `"中文测试文本"`.
**EXPECTED FAIL:** Color emoji e line breaking CJK non ancora produttivi.

### Azione 3.7 — Test G: Sottotitoli con word timing
JSON subtitle con start/end timing:
```json
[{"start":0.0,"end":1.2,"text":"First subtitle"},{"start":1.2,"end":2.5,"text":"Second highlighted"}]
```
**EXPECTED FAIL:** Feature non ancora completa.

---

## Fase 4 — Test "After Effects-like"

Golden PNG capture e verifica AE parity.

### Azione 4.1 — Golden capture
```bash
CHRONON3D_UPDATE_GOLDENS=1 bash tools/test_golden_driver.sh update linux-fast-dev
find . -name 'ae_*.png' | wc -l
find . -name 'user_spec_*.png' | wc -l
```
**Criterio PASS:** PNG reali generati in `test_renders/golden/text/`.
**FAIL:** `ctest` dice Passed ma 0 PNG prodotti.

### Azione 4.2 — Verifica PNG validity
```bash
file ./test_renders/golden/text/*.png | head -5
```
**PASS:** Tutti i file riportano `PNG image data`.

### Azione 4.3 — AE parity visual review
Ispezione manuale dei golden PNG: cinematic title reveal, typewriter, word cascade, fill/stroke/shadow, lower third — in 16:9 e 9:16.

---

## Fase 5 — Certificazione Finale

### Checklist riepilogativa

| # | Area | Test | Stato |
|---|------|------|-------|
| 1 | Build | `bash tools/chronon-linux.sh` | ⬜ |
| 2 | CLI | `chronon3d_cli list` | ⬜ |
| 3 | Render base | `render BackgroundGrid --frame 0` | ⬜ |
| 4 | SDK esterno | `bash tools/install_consumer_test.sh` | ⬜ |
| 5 | SDK pulito | Consumer fuori repo con solo `Chronon3D::SDK` | ⬜ |
| 6 | Testo base | Titolo centrato 16:9 | ⬜ |
| 7 | Testo vertical | Titolo 9:16 | ⬜ |
| 8 | Lower third | Titolo + sottotitolo | ⬜ |
| 9 | Long text | Wrapping/autofit | ⬜ |
| 10 | Multilingua | Accenti, RTL, CJK | ⬜ |
| 11 | Emoji/CJK | Emoji e line breaking CJK | ⬜ (EXPECTED FAIL) |
| 12 | Subtitle | SRT/JSON word timing | ⬜ (EXPECTED FAIL) |
| 13 | AE parity | Golden PNG reali generati | ⬜ |
| 14 | Determinismo | Stesso frame ×2 = hash identico | ⬜ |
| 15 | Performance | 300 frame 1080p tempo/memoria | ⬜ |
| 16 | Packaging | `install` + `find_package` | ⬜ |

---

## Regole di esecuzione

- **SOLO `main`**: niente branch, niente PR. Commit e push diretti.
- **Commit frequenti**: ogni azione completata = commit atomico su `main`.
- **No nuove feature**: solo test, certificazione, documentazione.
- **No greenwashing**: se un test fallisce, documentarlo come FAIL/EXPECTED FAIL.
- **11 gate sempre verdi**: dopo ogni commit, verificare che la baseline resti 11/11.

## Documenti correlati

- [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) — stato corrente (11/11 baseline verde)
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — milestone prodotto
- [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md) — requisiti permanenti di release
- [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) — blocker aperti
