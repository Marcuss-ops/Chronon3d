# TICKET-V6-RESIDUAL-ROTOLOGY — Residual rotology after V6 partial fix (forward-point)

## Stato: OPEN (forward-point, 2026-07-21)

**Predecessor**: [TICKET-SYSTEMIC-NAMESPACE-ROT](TICKET-SYSTEMIC-NAMESPACE-ROT.md) (DONE-PARTIAL 2026-07-21, closed after V6 partial fix).

**Scope**: 169 residual rot errors across 5 OBJECT targets (chronon3d_registry, chronon3d_text_health_tests, chronon3d_backend_software, chronon3d_effects, chronon3d_animations) after V6 Path B Op1 (qualify `::chronon3d::X` on 29 symbols / 31 source files / 404 LoC).

## §Closure Evidence (DONE-PARTIAL 2026-07-21 in TICKET-SYSTEMIC-NAMESPACE-ROT)

V6 cumulative impact:
- `chronon3d_registry`: 394 → ~39 (90% reduction in rotology)
- `chronon3d_animations` + `chronon3d_core_impl`: 0 errors (clean)
- `chronon3d_text_health_tests`: ~395 → ~41 (~90% reduction)
- `chronon3d_backend_software`: partially closed
- `chronon3d_effects`: residual 2 errors (cascade-introduced)

Aggregate baseline → post-V6: 356 → 169 errors (52.5% reduction).

## §Residual Rotology Inventory

### Top hotspots (post-V7 cumulative diagnostic)

| File | Residual Errors | Strategy |
|---|---|---|
| `src/registry/text_preset_factories_emphasis.cpp` | 62 | Op2 ns-convert + Op1 extended symbols |
| `include/chronon3d/render_graph/render_graph_context.hpp` | 32 | Op1 extended symbols |
| `include/chronon3d/timeline/composition.hpp` | 24 | Op1 extended symbols |
| `include/chronon3d/math/projector_2_5d.hpp` | 20 | Op1 qualified + Op2 if structural |
| `src/registry/text_preset_internal_helpers.hpp` | 18 | Op1 via `internal::X` lookup |

### Required NEW symbols (extracted from residual logs)

```
backends, framebuffer_clear_parallel, FramebufferArena, ResolvedCamera,
ResolvedLayer, TextRunShape, hash_text_run_shape, prefetch,
project_layer_2_5d, telemetry, from_mat4
```

(14 NEW symbols needed in addition to V6's 29-symbol set; full list in `/tmp/v7_residual_symbols.txt`)

## §Forward-point strategy options

### Strategy R1 — Extended Op1 + Op2 (combined)
1. Apply Op1 with extended 43-symbol cumulative set (V6 29 + V7 14 NEW) on all 309 file-paths inventory (V6 touched 31 of them; V7 attempted 81 but had IsADirectoryError).
2. Apply Op2 (convert `namespace chronon3d::subns { ... }` → proper nesting) on 5-7 registry factory files where rotology is structural (parent-scope ambiguity from C++17 nested-ns syntax).
3. Blast radius: ~50-60 files / ~600-800 LoC.
4. Re-build all 6 OBJECT targets, expect 169 → ≤30 errors (DONE state).

### Strategy R2 — ADR escalation (per existing TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED)
1. Open `docs/adr/ADR-NNN-namespace-architecture-revisited.md` per `docs/DOCUMENTATION_GOVERNANCE.md` §adr.
2. Document scope-lookup rotology pattern + 5 strategy options (qualification / nested-ns / using-directives / include-flatten / combined).
3. Choose canonical strategy via ADR record.
4. Implementation deferred to downstream chore per ADR decision.

### Strategy R3 — Accept partial, defer indefinitely
1. Acknowledge 169 residual as known-undisclosed rotology-class debt.
2. No further fix; future agent picks up via minor iteration on this ticket when convenient.

## §Closure criterion

- All 6 OBJECT targets report 0 errors (per `ninja -C build/chronon/linux-content-dev <tgt>`)
- 11/11 baseline suite verde per `bash install_vcpkg_bootstrap_linux.sh` + `cmake --preset linux-content-dev` + `ctest --test-dir build/chronon/linux-content-dev -R chronon3d_ --timeout 60` (WBH macchina-verifica).
- Residual rotology-class debt < 30 errors acceptable as DONE-LEAN if blast-radius-vs-benefit argument favorable.

## §References

- [TICKET-SYSTEMIC-NAMESPACE-ROT](TICKET-SYSTEMIC-NAMESPACE-ROT.md) — predecessor (DONE-PARTIAL 2026-07-21)
- [TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED](TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED.md) — forward-point ADR (open from earlier session)
- [TICKET-GRAPHICS-SHAPE-STYLE-ROT](TICKET-GRAPHICS-SHAPE-STYLE-ROT.md) — original 5-file rot ticket
- `/tmp/baseline_rot.log` — 394-error original diagnostic (2-target baseline)
- `/tmp/ninja_all_rot.log` — 356-error full-build diagnostic
- `/tmp/v7_residual_symbols.txt` — 14 NEW symbols needed
- AGENTS.md §Fare PR piccole e mirate + §Docs canonical update discipline rule + §Post-push SHA-selfcheck invariant
