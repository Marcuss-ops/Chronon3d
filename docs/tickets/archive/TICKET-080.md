# TICKET-install-consumer-vcpkg-bootstrap

| Field       | Value                                                            |
|-------------|------------------------------------------------------------------|
| ID          | TICKET-install-consumer-vcpkg-bootstrap                          |
| Priorità    | P1                                                               |
| Area        | `install_consumer_test.sh` vcpkg env precond                    |
| Stato       | PLANNED                                                          |
| Blocca      | `install_consumer_test.sh` gate #10 (env precond) in macchina-verifica |
| Scoperto da | machine-verified run su `main@efd841f0` (2026-07-02)             |
| Snapshot    | [`reports/perf/main-efd841f0-gates.json`](../../reports/perf/main-efd841f0-gates.json) (schema `chronon3d.gates.v1`) |
| Relazione (NON-conflict) | Distinct da TICKET-111 (OPP-side text rot); i due blocker possono coesistere |

## Diagnosi

`tools/install_consumer_test.sh` richiede `vcpkg.cmake` come
`<CWD>/vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake`. Su un git-worktree
isolato (es. `git worktree add --detach /tmp/gates-efd841f0 efd841f0`) il
path non esiste e `VCPKG_ROOT` non viene esportato automaticamente.
Risultato: `cmake -DCMAKE_TOOLCHAIN_FILE=...` fallisce immediatamente
(0s elapsed) con `Could not find toolchain file: ...`. Gate esce `exit=1`
prima di esercitare qualsiasi code path. `regression_type` classificato
come `infra-setup` (non è una regressione di codice).

## Forward-fix proposto

1. Esportare `VCPKG_ROOT=$PROJECT_ROOT/vcpkg_installed/x64-linux` (o path
   canonico equivalente) dentro `tools/install_consumer_test.sh` come
   precondizione documentata.
2. In alternativa, clonare/bootstrap `vcpkg` dentro `<WT>/vcpkg_bootstrap/`
   se assente, prima della cmake-configure phase.
3. Documentare il contratto env in `tools/README.md` o equivalente  <!-- drift-allow: stale-ref -->
   (sezione: "vcpkg bootstrap precond").

## Criteri di accettazione

- `bash tools/install_consumer_test.sh` esegue cmake-configure phase
  senza errori di toolchain in worktree isolati e in CI standard.
- Gate #10 su `main@efd841f0` (o HEAD corrente) esce `PASS` oppure
  passa al code logic con un `regression_type` diverso da `infra-setup`.
- `reports/perf/main-<HEAD>-gates.json` non contiene
  `"regression_type": "infra-setup"` su gate #10.

## Note

- Sub-ticket subordinato a TICKET-GATE-10-PHASE-4 (Phase 4 fix
  globale); questo ticket si occupa SOLO dell'env precond, non del
  code logic Phase 4 (che è dominato da TICKET-111 OPP-side text rot).
- Non è necessario eseguire la run EfD841F0 di nuovo; il bug è già
  riproducibile con un git-worktree pulito.
