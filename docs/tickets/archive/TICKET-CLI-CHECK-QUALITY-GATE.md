# TICKET-CLI-CHECK-QUALITY-GATE — `chronon3d_cli check`

**Stato:** CLOSED (2026-11-12, locale) — implementazione e criteri locali
verdi. `check*` 20/20 (109 assert), suite CLI 266/266 (5.158 assert),
architettura 218/218. CI same-SHA non eseguita: residuo esterno in
`TICKET-MAIN-TRUTH-STAGE-1-2`.

## Obiettivo

Dare alla CLI un unico comando di controllo qualità sull'artefatto, sul piano e
sull'audio, con finding classificati P0–P3, JSON stabile ed exit code
utilizzabile da una pipeline o da un agente. È la controparte Chronon di
`raea check`: non sostituisce i gate di parità del motore, li applica a ciò che
è stato effettivamente prodotto.

## Contratto

```text
chronon3d_cli check <video|image> [--plan plan.json] [opzioni]
chronon3d_cli check --plan plan.json [opzioni]
```

### Input

| Input | Cosa viene misurato |
|---|---|
| video | frame decodificati dal decoder nativo (software), fps dal probe |
| immagine | un solo frame: nero, clipping, alpha vuoto |
| `--plan` | layer testo del RenderPlan risolto: font, dimensione, box, safe insets |

Il video e il piano possono essere passati insieme; i finding vengono uniti.
`--assets-root` accetta anche un path relativo: il comando lo normalizza ad
assoluto prima di montarlo nel resolver (che rifiuta i mount relativi).

### Check implementati

| ID | Categoria | Severità | Regola |
|---|---|---:|---|
| `video.empty_alpha` | video | P0 | tutti i frame campionati hanno alpha opaco ≈ 0 |
| `video.black` | video | P0/P1 | intervallo nero ≥ `--black-seconds`; P0 se è l'intero video |
| `video.freeze` | video | P1 | differenza media tra frame consecutivi ≤ soglia per ≥ `--freeze-seconds` |
| `video.flash` | video | P1 | più di `--flash-max-per-second` transizioni di luminanza in una finestra di 1 s |
| `video.highlights_clipped` | video | P2 | frazione di pixel opachi con luma ≥ 0.98 sopra `--max-clipped-fraction` |
| `text.font_unresolved` | plan | P0 | layer testo senza font risolvibile dal resolver |
| `text.empty` | plan | P2 | layer testo con contenuto vuoto o soli spazi |
| `text.font_size_missing` | plan | P1 | layer testo senza font size (o ≤ 0) |
| `text.too_small` | plan | P1 | font size sotto la soglia, scalata sulla geometria del canvas |
| `text.box_invalid` | plan | P2 | box dichiarato con larghezza o altezza ≤ 0 |
| `plan.safe_insets_invalid` | plan | P1 | insets negativi, o top+bottom / left+right ≥ canvas |
| `audio.not_measured` | audio | P2 | c'è uno stream audio ma il decode/misura non è disponibile |
| `audio.silent` | audio | P1 | loudness integrata ≤ `--silence-lufs` |
| `audio.loudness` | audio | P1 | loudness integrata fuori da target ± tolleranza |
| `audio.peak` | audio | P1 | sample peak sopra `--max-peak-dbfs` |
| `audio.clipping` | audio | P2 | frazione di campioni a fondo scala sopra la soglia |

La loudness è BS.1770-4 (K-weighting, blocchi 400 ms, hop 100 ms, gating
assoluto e relativo) sul segnale mono 48 kHz già usato dal baker audio di
Chronon. Il picco è **sample peak**, non true peak: il report lo dichiara e la
soglia predefinita resta conservativa (-1 dBFS). Il true peak non è un criterio
di questa scheda.

### Severità ed exit code

- `P0` rotto, `P1` da correggere prima della consegna, `P2` rifinitura,
  `P3` cosmetico.
- `--fail-on` (default `P1`) è la soglia che rende il report `FAIL`.
- Exit `0`: nessun finding ≥ `--fail-on`.
- Exit `1`: almeno un finding ≥ `--fail-on`.
- Exit `2`: input/analisi non eseguibile (file assente, stream video assente,
  piano non decodificabile, decoder non disponibile). In questo caso il JSON
  contiene `status: "ERROR"` e `error`.

### JSON

Schema `chronon.check.v1`:

```json
{
  "schema": "chronon.check.v1",
  "status": "PASS|FAIL|ERROR",
  "input": "...",
  "plan": "...",
  "thresholds": { "...": "..." },
  "findings": [
    {
      "id": "video.black",
      "category": "video",
      "severity": "P1",
      "message": "...",
      "fix": "...",
      "first_frame": 12,
      "last_frame": 20,
      "first_second": 0.4,
      "last_second": 0.667,
      "details": {}
    }
  ],
  "summary": {
    "fail_on": "P1",
    "counts": {"P0": 0, "P1": 1, "P2": 0, "P3": 0},
    "total": 1,
    "checks_run": ["video", "audio"],
    "checks_not_measured": [],
    "notes": []
  }
}
```

I finding sono ordinati per severità e poi per frame. Gli ID e le chiavi sono
stabili: una pipeline può filtrare su di essi senza leggere i messaggi.

### Non obiettivi

- Nessuna analisi semantica del volto o delle parole: Chronon non possiede una
  trascrizione in questo comando.
- Nessun safe-area automatico per piattaforme esterne sul video; le safe insets
  sono quelle dichiarate dal RenderPlan.
- Nessun true peak, nessun LUFS per canali separati (il mix è mono 48 kHz).
- Nessuna correzione automatica: il comando misura e propone, non modifica
  l'artefatto.

## Criteri di accettazione

1. `check <video>` percorre tutti i frame (o un campione dichiarato con
   `--quick`) e produce i finding video; se c'è audio produce quelli audio.
2. `check --plan` produce i finding testo/geometria dal RenderPlan risolto.
3. Il JSON è `chronon.check.v1` e l'exit code segue la tabella.
4. Test unitari coprono: conteggio/exit, nero, freeze, flash, clipping,
   loudness (silenzio e sinusoide), plan findings.
5. I gate di architettura e i test CLI restano verdi sul checkout locale.

## Verifica locale (2026-11-09)

Build: `cmake --build build/chronon/linux-video-release --target chronon3d_cli -j4`
— PASS.

Test mirati: `chronon3d_cli_tests --test-case="check*"` — **16/16 casi,
96/96 asserzioni PASS** (findings/exit code, nero totale e parziale, alpha
vuoto, freeze, flash, clipping, piano, loudness silenzio/sinusoide, sample
peak, degrado audio, errori di scan).

Smoke end-to-end (ffmpeg 320×240, 30 fps, 1 s, audio AAC silenzioso):

| Input | Exit | Evidenza |
|---|---:|---|
| `black_silent.mp4` | 1 | `video.black` P0 (30 frame, 1.0 s) + `audio.silent` P1; `status=FAIL` |
| `white.png` (`--no-audio`) | 0 | solo `video.highlights_clipped` P2 (frazione 1.0); `status=PASS` con `--fail-on P1` |
| `--plan 01_editorial_text.plan.json --no-video --assets-root .` | 0 | nessun finding; font e immagine risolti; `status=PASS` |
| input inesistente | 2 | JSON `status=ERROR`, campo `error` |

Il JSON di tutti i casi usa `schema: chronon.check.v1`; i conteggi e
`checks_run`/`checks_not_measured` riflettono i check eseguiti.

Gate architettura (2026-11-12): `python3.11 tools/check_architecture.py` —
**218/218 PASS**, nessun FAIL. Il precedente match in
`render_plan_preparation.cpp:388` non è più presente nel tree corrente; il
gate non è stato modificato né indebolito.

Suite CLI completa (2026-11-12): `chronon3d_cli_tests` — **266/266 casi,
5158/5158 asserzioni PASS**. `git diff --check` — PASS. CI same-SHA: non
eseguita; non è un criterio locale di questa scheda.

## Verifica finale 2026-11-12

| Gate | Comando | Esito |
|---|---|---|
| Check mirati | `chronon3d_cli_tests --test-case='check*'` | **20/20 casi, 109/109 assert PASS** |
| Suite CLI | `chronon3d_cli_tests` | **266/266 casi, 5158/5158 assert PASS** |
| Architettura | `python3.11 tools/check_architecture.py` | **218/218 PASS** |

Il comando non è stato modificato in questa sessione: la chiusura registra il
riesame del gate architettura sul tree corrente e le suite fresche. Il residuo
same-SHA resta esterno e non viene dichiarato PASS.

## Demolition debt

Nessun bridge o percorso compatibile introdotto. Il comando consuma le authority
esistenti: `NativeVideoFrameDecoder`, `probe_media`, `prepare_render_plan`,
`build_render_plan_inspection`, `render_plan::detail::decode_audio_mono_48k`.
