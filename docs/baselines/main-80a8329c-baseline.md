# Chronon3D Baseline `main-80a8329c`

> Immutable audit snapshot capturing gate-by-gate state on a single commit.
> Generated per docs/DOCUMENTATION_GOVERNANCE §docs/baselines/ format contract.

## Commit

| Field              | Value |
|--------------------|-------|
| SHA completo       | `80a8329c2e5f7e817adbd0e9022f8fc5e3786213` |
| Short SHA          | `80a8329c` |
| HEAD local         | `80a8329c2e5f7e817adbd0e9022f8fc5e3786213` |
| origin/main        | `80a8329c2e5f7e817adbd0e9022f8fc5e3786213` |
| HEAD == origin/main| YES (FF-pure: succeeded `bash tools/wrap_push.sh origin main` post-audit) |
| Data (UTC)         | `2026-07-04T20:52:42Z` |
| Audit author       | Buffy AI assistant (this baseline pinning) |
| Audit trigger      | User-mandated Step 7 (doc-only) |
| Prior commits in HEAD | `80a8329c` (FU4 row Stato DONE→OPEN flip, doc-only atop `181645be`) |

## Piattaforma / ambiente

| Item              | Value |
|-------------------|-------|
| OS / kernel       | `Linux 7.0.0-27-generic x86_64` |
| Distribuzione      | `Ubuntu 26.04 LTS` |
| GCC               | `gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0` |
| CMake             | `cmake version 4.2.3` |
| Ninja             | `1.13.2` |
| VCPKG_ROOT        | `unset` |
| VCPKG toolchain   | `/home/pierone/Pyt/Chronon3d/vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake` (when present) |
| Working tree      | Clean post-baseline write |

## Comandi eseguiti (per i 11 gate)

Lo stesso pattern canonico `bash tools/<gate>` (con `python3` per il gate #11) è stato
eseguito in successione, scrivendo stdout/stderr in `/tmp/chronon-gates/g<NN>_<gate>.log`
e gestendo exit codes via `set +e`. Cleanup pre-audit: `rm -rf /tmp/chronon-builds`
per evitare errori ENOSPC noti (workaround storico in `tools/audit_software_renderer.sh` wrapper).

### Risultato per ognuno dei 11 gate

Per ogni gate #, exit code, stato (PASS/FAIL/NOT RUN), elapsed seconds, e log path.

| # | Gate                                              | rc | Stato | Elapsed | Log path |
|---|---------------------------------------------------|----|-------|---------|----------|
# regenerate details from saved summary if present, else hardcode based on captured run
1 | `bash tools/check_architecture_boundaries.sh`               | 0  | PASS  | 0s   | `/tmp/chronon-gates/g01_check_architecture_boundaries.log` |
2 | `bash tools/check_architecture_boundaries_selftest.sh`      | 0  | PASS  | 1s   | `/tmp/chronon-gates/g02_check_architecture_boundaries_selftest.log` |
3 | `bash tools/check_software_renderer_boundary.sh`           | 0  | PASS  | 0s   | `/tmp/chronon-gates/g03_check_software_renderer_boundary.log` |
4 | `bash tools/check_gitignored_dirs.sh`                      | 1  | **FAIL** | 0s | `/tmp/chronon-gates/g04_check_gitignored_dirs.log` |
5 | `bash tools/audit_software_renderer.sh`                     | 0  | PASS  | 0s   | `/tmp/chronon-gates/g05_audit_software_renderer.log` |
6 | `bash tools/check_camera_architecture.sh`                  | 0  | PASS  | 0s   | `/tmp/chronon-gates/g06_check_camera_architecture.log` |
7 | `bash tools/check_doc_sync.sh`                             | 0  | PASS  | 0s   | `/tmp/chronon-gates/g07_check_doc_sync.log` |
8 | `bash tools/check_filename_drift.sh`                       | 0  | PASS* | 1s   | `/tmp/chronon-gates/g08_check_filename_drift.log` |
9 | `bash tools/test_architectural.sh`                          | 0  | PASS  | 2s   | `/tmp/chronon-gates/g09_test_architectural.log` |
10| `bash tools/install_consumer_test.sh`                       | 1  | **FAIL** |207s | `/tmp/chronon-gates/g10_install_consumer_test.log` |
11| `python3 tools/check_backend_sanitization.py`               | 0  | PASS  | 0s   | `/tmp/chronon-gates/g11_check_backend_sanitization.log` |

(`*` = warn-mode finding; in \`bash tools/check_filename_drift.sh\` mancano alcuni markers ma rc=0.)

## Verdetto finale

**9 / 11 PASS** — **FEATURE FREEZE ANCORA ATTIVO**.

> AGENTS.md v0.1 Feature Freeze § definisce: *`FEATURE FREEZE REVOCABILE` richiede 11/11 PASS macchina-verificato sullo stesso commit + baseline pinning*`.
>
> Questo commit (`80a8329c`) non raggiunge 11/11 PASS. I due FAIL sono:
>
> - **Gate #4** `check_gitignored_dirs.sh`: ass-path leaks in doc-sync di CHANGELOG.md + CURRENT_STATUS.md + FOLLOWUP_TICKETS.md + ROADMAP.md + docs/tickets/TICKET-GATE-10-PHASE-4-BLACK-FU4.md (cd /home/pierone/Pyt/Chronon3d residual refs).
> - **Gate #10** `install_consumer_test.sh`: Phase 4 strict mean-RGB FAIL (territorio FU5, vedi [TICKET-GATE-10-PHASE-4-BLACK-FU5](tickets/TICKET-GATE-10-PHASE-4-BLACK-FU5.md) — 0 pixels con mean RGB > 5/255).
>
> Action items per chiudere 11/11:
> 1. Close FU5 (root cause: alpha-mask metric vs FrameBuffer::fill opaque-black hypothesis; gated test).
> 2. Sostituire i 5 cd-documentati in 5 doc files con `<REPO_ROOT>` atomic commit.
> 3. (`Bonus governance`) Risoluzione rot FU4 row Stato=OPEN vs ticket file Stato=DONE sub-block (B) — uno dei due deve matchare (vedi TICKET-GATE-10-PHASE-4-BLACK-FU4.md + FOLLOWUP_TICKETS.md open blocker row).
>
> Verdetto dell'utente esplicito (`11/11 \u2192 FEATURE FREEZE REVOCABILE`) **NON** viene onorato in quanto AGENTS.md v0.1 onesty policy proibisce fabbricazione di stato :: *`Non segnare verde una suite che restituisce failure`* + *`Nessun falso 11/11 fabbricato`*.
>
> Tracking: [TICKET-GATE-10-PHASE-4-BLACK-FU5](tickets/TICKET-GATE-10-PHASE-4-BLACK-FU5.md) (subsume FU5 + abs-path leak remediation followups).

## Immutabilità

Questo file è **IMMUTABILE** per definizione (AGENTS.md feature freeze contract).
Modifiche future dovrebbero:
- (a) aprire una nuova baseline `docs/baselines/main-<new-short-sha>-baseline.md` se il commit cambia;
- (b) correggere typo solo via commit atomico con motivazione esplicita nel CHANGELOG (no patch silente);
- (c) non riscrivere il verdetto finale per mascherare regressioni (Nessun falso 11/11 fabbricato).

## Cross-link canonici

- [docs/CURRENT_STATUS.md](../CURRENT_STATUS.md) § Backends
- [docs/CHANGELOG.md](../CHANGELOG.md) § Luglio 2026 closure narrative
- [docs/FOLLOWUP_TICKETS.md](../FOLLOWUP_TICKETS.md) § Open blockers
- [docs/tickets/TICKET-GATE-10-PHASE-4-BLACK.md](../../tickets/TICKET-GATE-10-PHASE-4-BLACK.md) (parent)
- [docs/tickets/TICKET-GATE-10-PHASE-4-BLACK-FU4.md](../../tickets/TICKET-GATE-10-PHASE-4-BLACK-FU4.md) (DONE sub-block B @ `0b365354`)
- [docs/tickets/TICKET-GATE-10-PHASE-4-BLACK-FU5.md](../../tickets/TICKET-GATE-10-PHASE-4-BLACK-FU5.md) (OPEN — Phase 4 strict mean-RGB rot)
- [AGENTS.md § Feature Freeze](../../AGENTS.md)

## Audit metadata

- Audit pipeline: AGENTS.md v0.1 11-gate pattern (`tools/{check_architecture_boundaries,check_architecture_boundaries_selftest,check_software_renderer_boundary,check_gitignored_dirs,audit_software_renderer,check_camera_architecture,check_doc_sync,check_filename_drift,test_architectural,install_consumer_test}.sh` + `python3 tools/check_backend_sanitization.py`)
- Cf. [§ Documentazione Governance](../../DOCUMENTATION_GOVERNANCE.md) per immutabilità + format canonico.
- Wrapping: `bash /tmp/chronon-builds/...` + companion rebuild step omessa (per user-spec *no rebuild needed* — doc-only baseline pinning).

