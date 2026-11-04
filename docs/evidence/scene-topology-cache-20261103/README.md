# Scene topology cache — evidence 2026-11-03

Intervento: la cache topologica di `RenderPipeline` è passata da per-pipeline a
`SoftwareExecutionContext::topology_cache`, condivisa da tutte le pipeline dello
stesso `SoftwareRenderer`. Il contatore `scene_materialize_count` conta solo i
miss; `scene_materialize_cache_hits` conta i riusi.

## Render locale (stesso contenuto)

Comando:

```sh
build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli render-plan \
  output/T16_chronon_render/scene.plan.json \
  --backend software --assets-root "$PWD" \
  --start-frame 0 --end-frame 1 --warmup-dummy-frame \
  --output /tmp/cache-warm.mp4
```

Risultato (`scene.plan.timing.json`):

- `evaluation_mode=compiled_program`
- `timeline_eval_ms=null`
- `scene_materialize_count=1`
- `scene_materialize_cache_hits=1`
- `graph_executed_frames=2`

Senza warmup: `scene_materialize_count=1`, `scene_materialize_cache_hits=0`,
`job_wall_ms=709.6`.

## Replay P4 (job reale)

- Job: `job_1793735303280198261_6e226690`, `SUCCEEDED`
- Payload: `verified-baseline-r103-r105/r101-like-p4.generate.json`
- Payload SHA256: `75e550c5491869eafc277dc26dcd20b3b57470d7776e023569843e13bd14068e`
- Avvio/fine: 19:48:23.619Z → 19:49:55.945Z, `local_wall_ms=92325.944`

Metriche aggregate sui **9 batch unici**:

| Metrica | P4 baseline | Replay |
|---|---:|---:|
| `scene_materialize_count` | 72 | **9** |
| `scene_materialize_cache_hits` | 0 | **1.720** |
| `scene_materialize_ms` | 15.418,692 | **2.007,764** |
| `evaluation_mode` / `timeline_eval_ms` | compiled_program / null | compiled_program / null |

Il conteggio scende e gli hit sono > 0: la cache condivisa funziona e i
fingerprint non sono tutti distinti, quindi il profilo per-item di
`SceneMaterializer` non è il passo successivo.

## Limite: il contenuto è stato rigenerato

Il replay **non** ha riprodotto lo stesso contenuto del P4 baseline:

- `overlay_plan.fingerprint`: `49e7cb8a…` → `4742d7fe…`
- `scenes` canonico: `6c24d27d…` (coincide con il `scenes_sha256` del manifest)
  → `04090041…`
- 24 overlay: **0** SHA uguali, **8** SHA diversi sugli stessi ID, **16** ID nuovi
- `overlay_sha256` aggregato del baseline: `c5eab14a…`; il replay ha un final
  video SHA `fac6a82f…`, non confrontabile con l’overlay-only.

Quindi il calo 72→9 / 15.418,692→2.007,764 ms è **direzionale**, non un A/B
sullo stesso contenuto. Il gate SHA invariance **non passa**: P4 resta
**PARTIAL**. Per chiuderlo serve un replay render-only con `scenes` e
`overlay_plan` bloccati al baseline (o un job che non rigeneri il piano).

## Garlasco 20 min (r20minuti)

Replay dello stesso payload r20minuti, job nuovo
`job_1793736188766904832_9fb6a467` (`SUCCEEDED`), 5 batch. Il payload è
equivalente al baseline (`payload_equal=true`), ma `force_refresh=true` ha
rigenerato scene e overlay.

| Metrica | Baseline r20minuti | Replay |
|---|---:|---:|
| `scene_materialize_count` | 1.496 | **5** |
| `scene_materialize_cache_hits` | 0 | **1.713** |
| `scene_materialize_ms` (somma batch) | 404.623,258 | **1.124,487** |
| `overlay_render` stage | 173,9 s | **46,2 s** |
| wall locale | 425,2 s | **322,3 s** (−24,2%) |

Il contenuto è cambiato: `scenes` canonico `def8839d…` → `4e6ec2a1…`, piano
`e90a11f8…` → `e5496fe8…`, 0 overlay SHA uguali (4 diversi, 14 nuovi), final
video `eb5feabf…` → `67098c52…`. Quindi il −24,2% end-to-end è direzionale;
la cache resta la leva (materializzazione −99,7%).

Il nuovo collo di bottiglia è `prepare_join` **108,6 s** (33,7% del critical
path) contro 37,8 s; segue `scene_analysis` 86,9 s.

## A14 — `prepare_join` decomposto (2026-11-03)

Due run dello stesso payload `r20minuti-cache-20261103.generate.json`
(SHA-256 `61b73145…`), entrambi `SUCCEEDED`:

| Campo | Run 1 | Run 2 |
|---|---:|---:|
| Job | `job_1793737130845286137_5bd2965f` | `job_1793737420046345110_e5cb94e8` |
| `prepare_join.wall_ms` | 86.789 | 59.907 |
| `prepare_join.semantic_ms` | **86.943** | **60.049** |
| `prepare_join.assets_ms` | 264 | 3 |
| `prepare_join.last_branch` | `semantic` | `semantic` |
| `scene_analysis` stage | 65.745 | 39.862 |
| `generate` stage | 38.002 | 35.974 |
| job `wall_ms` | 240.687 | 147.500 |

La fase è attribuita: il join aspetta il ramo semantico, non gli asset
(264/3 ms). `scene_analysis` è la parte dominante; il residuo del ramo
semantico (~21,2/20,2 s) comprende `computeLocalizedAnnotations` e
geocode/enqueue e non è ulteriormente strumentato. Il gate di ripetibilità
±10% **non passa**: `semantic_ms` −30,9%, `scene_analysis` −39,4%. Il
contenuto è stato rigenerato, quindi serve un replay same-content o una cache
di `scene_analysis` prima di chiudere A14. **Verdetto: causa attribuita,
ripetibilità non dimostrata.**

## File

- `scene.plan.json`, `scene.plan.timing.json`, `render.sha256`: render locale.
- `p4-cache-summary.json`: metriche e confronto del replay P4.
- `r20minuti-cache-summary.json`: metriche e confronto del Garlasco 20 min.
- `r20minuti-decomp-summary.json`: breakdown A14 delle due run 20 min.
- Job P4: `ops/jobs/garlasco_editorial_e2e_20261030/p4-cache-20261103-job-full.json`.
- Job 20 min: `ops/jobs/garlasco_editorial_e2e_20261030/r20minuti-cache-20261103-job-full.json`.
