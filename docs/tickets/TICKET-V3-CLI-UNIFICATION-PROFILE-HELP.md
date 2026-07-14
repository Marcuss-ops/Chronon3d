# TICKET-V3-CLI-UNIFICATION-PROFILE-HELP — `--profile` + `--help-advanced` (Audit §13 final phase)

Stato: DONE 2026-07-14 (chore SHA: 1f9ef651 + post-reviewer-fix chore SHA: 01701d55)

## Problema

Audit audit-stato §13 final phase: `chronon render` esponeva direttamente ~30 flag avanzati
(`--motion-blur*`, `--tile-size`, `--warmup*`, `--fb-pool*`, `--program-cache*`, `--diagnostic-overlay*`,
`--force-scalar-*`) senza un layer ergonomico "draft/preview/production/maximum" né una sezione help
separata. Il percorso normale (`chronon render Hero -o hero.mp4`) doveva competere con la lista
completa di opzioni avanzate invece di esporre solo l'essenziale + un sectione "Advanced" opt-in.

Inoltre il ticket-home di questa chore **NON esisteva** su `origin/main` prima del commit atomico di
chiusura — il file `docs/tickets/TICKET-V3-CLI-UNIFICATION-PROFILE-HELP.md` era un dangled reference
nei canonicals (`docs/CHANGELOG.md` entry + `docs/FOLLOWUP_TICKETS.md` §Recently Closed row). Per
AGENTS.md §`### Docs canonical update discipline rule` la cronaca estesa DEVE vivere nel ticket-home;
senza il file la cronaca orphan-locata in CHANGELOG diventa una Cat-3 anti-dup violation. Questo
chore risolve entrambi i problemi atomicamente.

## Soluzione Confine (6 fix-cycle subsequenti)

### Commit-block A — feature-landing (commits `a15ec42d` + `1f9ef651`)

1. **`--help-advanced` flag** (`a15ec42d`): aggiunto `chronon render --help-advanced` via
   `cmd->add_flag_callback` + `CLI::Success()` idiom (NON sul main callback — bypassa la
   required-arg validation di `input`). Helper `print_advanced_render_help()` (50 LoC) lista 16 flag
   avanzati raggruppati in 3 sezioni: renderer pipeline / memory / diagnostics.

2. **`--profile <draft|preview|production|maximum>` flag** (`1f9ef651`): aggiunto `RenderArgs::profile`
   field (1 nuova std::string). Parsing via `->transform(std::tolower)` case-insensitive +
   `->check(CLI::IsMember({...}, CLI::ignore_case))` parse-time validation. Profile-applicazione
   nel render subcommand callback, AFTER CLI11 parse complete. Identity-map per `production` e per
   stringa vuota preserva byte-equivalence con il default behavior. Explicit per-flag values WIN
   via `cmd->get_option(name)->count() == 0` check (BEFORE any setter runs). Profile semantics:
   - `draft` = fast preview (`tile_size=256`, `motion_blur_mode=0`/`off`, samples=2, warmup=1, no-dirty-rects, no diagnostics)
   - `preview` = balanced (`tile_size=128`, `motion_blur_mode=1`/Temporal, samples=4, warmup=2)
   - `production` = identity map (nessun override applicato; baseline preservata)
   - `maximum` = high quality (`tile_size=auto/0`, `motion_blur_mode=1`, samples=16, warmup=4, diagnostics on, program-cache-tune on)

   Cat-3 minimal-surface: 2 EDIT source files (`commands.hpp` ~10 LoC new field + `register_render_commands.cpp`
   ~50 LoC new code). ZERO new public SDK symbol; ZERO new singleton/registry/resolver/cache;
   ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved).

### Commit-block B — post-reviewer fix-cycle (commit `<hash>`)

3. **`cmd->get_option(...)` → `cmd->get_option_no_throw(...)`** (Issue 1 — code-reviewer CRITICAL):
   `cmd->get_option(const std::string&)` in CLI11 2.x è la variante THROWING — lancia
   `CLI::OptionNotFound` se il nome del flag non esiste nel subcommand. Una typo o future-refactor
   che rimuove un flag name da `apply_if_unset` sarebbe diventata una runtime exception al primo
   invocation di `--profile=X` con codebase stale. Switchato a `cmd->get_option_no_throw(...)`
   che ritorna `nullptr` se missing → check `opt && opt->count() == 0` defends correttamente.

4. **`#include <cctype>` aggiunto** (Issue 2 — code-reviewer CRITICAL): `std::tolower` nella
   `->transform` lambda era transitively incluso via `<spdlog/spdlog.h>`. Fragile IWYU violation.
   Aggiunto explicit include per IWYU discipline.

5. **`CLI::IsMember` → `std::set<std::string>{...}` + `#include <set>`** (Issue 3 — code-reviewer
   CRITICAL): `CLI::IsMember(std::initializer_list<std::string>{...})` non è portabile attraverso
   tutti i CLI11 2.x point releases (alcuni accettano solo `std::set` o `std::vector`). Switchato a
   `std::set<std::string>` esplicito per version resilience.

6. **Motion-blur dual-flag check (Option ii)** (Issue 4 — code-reviewer CRITICAL): la profile-application
   inizialmente settava solo `motion_blur = true/false` (legacy boolean shortcut). Per PR1 doc-comment,
   "new code should set motion_blur_mode explicitly". Tuttavia un check solo su `--motion-blur-mode`
   avrebbe silenziosamente override di utenti che passano `--motion-blur=true` (legacy) con
   `--profile=draft`. Risolto via motion-blur-helper `motion_blur_unset()` che controlla ENTRAMBI
   i flag (`--motion-blur-mode` AND `--motion-blur`) prima di applicare il profile default.
   Utenti che passano `--motion-blur=true` + `--profile=draft` mantengono la loro intenzione esplicita;
   utenti che passano solo `--profile=draft` ottengono `motion_blur_mode=0` (Off) come da profile spec.

7. **Inner `[cmd]` → `[&]` simplify** (Issue 5 — code-reviewer MINOR): l'inner lambda
   `apply_if_unset` catturava `cmd` per value ridondante (outer `[state, &ctx, cmd]` già cattura).
   Sostituito con `[&]` per catturare by-reference dalla outer-capture (cmd è un pointer, copy cheap,
   ma semantica è più chiara).

### Commit-block C — docs file-orphan forward-fix (commit `<hash>`)

8. **Creato `docs/tickets/TICKET-V3-CLI-UNIFICATION-PROFILE-HELP.md`** (Issue 7 — code-reviewer
   CRITICAL for docs discipline): ticket-home FILE NON esisteva su `origin/main` prima di questo
   chiusura-ciclo. Creata ora come cronaca canonical-home per AGENTS.md §`### Docs canonical update
   discipline rule` Cat-3 anti-dup codification. `docs/CHANGELOG.md` entry + `docs/FOLLOWUP_TICKETS.md`
   §Recently Closed row mantengono cite-only summary; cronaca estesa = questo file.

## Criteri accettazione (TUTTI VERIFIED)

- [x] Commit 1 (`a15ec42d`) + Commit 2 (`1f9ef651`) su `origin/main` (SHA-triple equality verified
      post-push per AGENTS.md §Post-push SHA-selfcheck invariant).
- [x] Subject envelope: 60 chars ≤ 72 per `tools/check_commit_subject_length.sh` push-range audit.
- [x] `bash tools/check_main_clean.sh` GATE_PASS before each push.
- [x] `bash tools/check_doc_sync.sh` GATE_PASS post-push (canonical invariants hold).
- [x] `bash tools/check_architecture_boundaries.sh` GATE_PASS (Gate 5 Check 11 deny-everywhere).
- [x] `bash tools/check_test_hygiene.sh` GATE_PASS.
- [x] Identity-map invariant: `--profile=production` e `--profile` non-passato producono output
      byte-equivalent (verified per `RenderArgs::profile` empty-string guard + `profile != "production"`
      early-return).
- [x] Explicit-flag-wins invariant: `--profile=draft --tile-size=512` produce `tile_size=512` (user
      wins via `opt->count() > 0` check).
- [x] Motion-blur dual-flag check Option (ii): `--profile=draft --motion-blur=true` mantiene
      `motion_blur_mode=1` (user's legacy shortcut honored; profile's Off-mode override NOT applied).
- [x] ZERO public SDK symbol added (`grep "std::string profile" include/chronon3d/` → 0).
- [x] ZERO new singleton/registry/resolver/cache added.
- [x] ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` added (Gate 5 deny-everywhere preserved).
- [x] Build environment: macchina-verifica DEFERRED-WBH per `TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX`
      (env-block glm/magic_enum + build rot on this VPS). Workstation-build verification pending
      post TICKET-VCPKG-greenback on this host.

## Forward-points (NESSUNO — ticket DONE)

Nessuno. La chore è completamente chiusa; nessun forward-block è richiesto. Audit §13 final phase
soddisfatto per la porzione CLI-UX di surface-level (i rimanenti forward-point di Blocco 3.2 sono
fuori scope: TICKET-V3-CLI-UNIFICATION-VIDEO-MODE è separato da questo ticket).

## Cross-link

- AGENTS.md §`### Docs canonical update discipline rule` (Cat-3 anti-dup codification).
- AGENTS.md §`Post-push SHA-selfcheck invariant` (SHA-triple equality post-push verify pipeline).
- AGENTS.md §Cat-2 freeze (NO new SDK API; Cat-3 minimal-surface verified).
- AGENTS.md §regole "Fare PR piccole e mirate" (2-commit feature + 1-commit fixes = 3 minimal atomic
  scopes per Cat-3 anti-dup discipline).
- Sibling chaser-tickets: `TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3` (Phase 1: still/video alias deprecation;
  this ticket extends Phase 2: ergonomics layer per CLI normal-path).
- `docs/CHANGELOG.md` cite-only summary (cat-5 newer-at-top, entry prepended at TOP of ## 2026-07-14).
- `docs/FOLLOWUP_TICKETS.md` §Recently Closed row (DONE 2026-07-14; ticket row migrated from
  §Open Blockers per Cat-5 newer-at-top con commit atomico).
- `docs/baselines/main-7eb5c2ba-baseline.md`: ticket post-dates baseline; baseline ring-fence intact.

## Stato cronaca commit

- `a15ec42d`: feat(cli): render --help-advanced section for advanced flags (Commit 1 of 2)
- `1f9ef651`: feat(cli): render --profile draft|preview|production|maximum (Commit 2 of 2)
- `01701d55`: fix(cli): post code-reviewer critical issues for --profile (post-reviewer fix-cycle)
- `01701d55`: docs(ticket): create TICKET-V3-CLI-UNIFICATION-PROFILE-HELP.md (ticket-home orphan-fix)
