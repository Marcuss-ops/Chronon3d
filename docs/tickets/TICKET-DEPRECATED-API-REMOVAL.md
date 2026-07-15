# TICKET-DEPRECATED-API-REMOVAL — Gradual removal of deprecated APIs

## Stato: OPEN (P1)

## Problema
Diversi simboli pubblici sono marcati `[[deprecated]]` ma hanno ancora centinaia di chiamanti. La build li tollera perché `CMakeLists.txt` sopprime globalmente `-Werror=deprecated-declarations`. Questo mantiene i percorsi legacy in vita e rimanda la migrazione.

## Inventario API e conteggio chiamanti

| API deprecata | Header / TU | Chiamanti totali | Produzione (src/apps/content) | Test/include (incl. dichiarazioni) |
|---|---|---|---|---|
| `CompositionRegistry::add(std::string, Factory)` | `include/chronon3d/core/composition/composition_registry.hpp` | 142 | 139 (src 2, content 137) | 3 |
| `Composition::evaluate(Frame/SampleTime/...)` | `include/chronon3d/timeline/composition.hpp` | 29 | 2 (apps 1, include/presets 1) | 27 |
| `LayerBuilder::text(name, const TextSpec&)` | `include/chronon3d/scene/builders/layer_builder.hpp` | 39 | 34 (src) | 5 |
| `from_text_spec(const TextSpec&)` | `include/chronon3d/text/text_definition.hpp` | 49 | 4 (src) | 45 |
| `content::text::centered_text(...)` | `content/text/text_helpers_centered.hpp` | 90 | 23 (src 1, content 22) | 67 |
| `content::text::glow_text(...)` | `content/text/text_glow_helpers.hpp` | 34 | 4 (content) | 30 |
| `LayerBuilder::text_run(name, TextRunSpec)` (alias deprecato) | `include/chronon3d/scene/builders/layer_builder.hpp` | 88 | 6 (src) | 82 |
| `LayerBuilder::{at,rotate_node,scale_node,anchor_node,node_opacity,with_shadow,with_glow}` | `include/chronon3d/scene/builders/layer_builder.hpp` | 20 | 5 (src 2, content 3) | 15 (tests 3, include 11) |

**Totale siti deprecati tracciati:** ~490+.

> **Nota metodologica:** i conteggi sono stati ottenuti con `git grep` su pattern regex e possono includere dichiarazioni, commenti o falsi positivi; i veri call sites deprecati potrebbero differire leggermente. I numeri sono intesi come stima per area, non come conteggio esatto. In particolare, `.at(Vec3)` è difficile da distinguere da altri `.at(` (es. `std::map`) e potrebbe essere sottocontato.
>
> **Nota su `Composition::camera`:** il campo pubblico `Camera camera` in `Composition` è marcato `[[deprecated]]`; il suo uso non è ancora stato auditato e non compare nella tabella sopra.

## Unità di produzione coinvolte

- `src/scene/camera/overlay_spatial_panels.cpp`
- `src/scene/camera/overlay_kinematic_panels.cpp`
- `src/scene/camera/overlay_hud_panels.cpp`
- `src/scene/camera/overlay_diagnostic_panels.cpp`
- `src/scene/builders/layer_builder_shapes.cpp`
- `src/text/text_definition.cpp`
- `src/registry/text_preset_internal_helpers.hpp`
- `src/registry/text_preset_factories_cinematic.cpp`
- `content/showcases/`, `content/certification/`, `content/examples/`, `content/launches/` (legacy `registry.add`)
- `apps/chronon3d_cli/commands/dev/command_text_visibility.cpp`
- `include/chronon3d/presets/scene_presets.hpp`

## Soppressione globale in build

- `CMakeLists.txt:29`: `add_compile_options(-Wno-error=deprecated-declarations)` (M1.5#8 / TICKET-GATE-10-PHASE-4-BLACK).
- `CMakeLists.txt:42`: `option(CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION ... OFF)` può promuovere i deprecation warning a errori solo per `chronon3d_core_tests`.

## Piano di rimozione graduale

1. **Phase 1 — Audit & conteggi** (questo ticket): inventario di tutti i simboli `[[deprecated]]` e conteggio dei chiamanti.
2. **Phase 2 — Migrazione per area**: migrare i chiamanti area per area (testo, composition registry, camera/evaluate, node transforms). Ogni area avrà il proprio chore atomico.
3. **Phase 3 — Ri-attivazione errori**: quando i chiamanti production per un simbolo raggiungono 0, rimuovere il simbolo o attivare il build-flag per quel target.
4. **Phase 4 — Rimozione soppressione globale**: quando tutti i simboli deprecati sono migrati, eliminare `add_compile_options(-Wno-error=deprecated-declarations)` da `CMakeLists.txt`.

## Criteri di accettazione

- Tutti i conteggi sopra riportati aggiornati a 0 per il codice di produzione.
- `CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION=ON` passa senza errori di deprecazione.
- La soppressione globale `-Wno-error=deprecated-declarations` è rimossa.
- Baseline 11/11 verde post-migrazione.

## Ticket correlati

- [TICKET-TEXT-SPEC-MIGRATION](TICKET-TEXT-SPEC-MIGRATION.md)
- [TICKET-CENTERED-TEXT-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION.md)
- [TICKET-COMPOSITIONDESCRIPTOR-MIGRATION](TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md) / [ADR-027](../adr/ADR-027-compositiondescriptor-migration.md)
- [TICKET-PUB-DEPRECATE-REMOVAL](TICKET-PUB-DEPRECATE-REMOVAL.md)
