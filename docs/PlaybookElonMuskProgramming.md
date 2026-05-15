---
1. ABI instabile tra struct e prebuilt lib

Causa radice: Le struct FakeBox3DShape, GridPlaneShape, ecc. mescolano dati "pubblici" (configurati dall'utente) e
dati "privati" (injected al render time). Chiunque modifichi la parte pubblica rompe l'alignment della parte privata.

Soluzione: Separare completamente i due domini.

// Dati pubblici (API stabile, serializzabile, mai cambia struttura)
struct FakeBox3DParams {
    Vec3  pos;
    Vec2  size;
    f32   depth;
    Color color;
};

// Dati runtime (MAI condivisi con esempi o plugin)
// Prodotti dal render graph builder, vivono solo dentro il renderer
struct FakeBox3DRenderState {
    Mat4  cam_view;
    f32   focal, vp_cx, vp_cy;
    bool  cam_ready;
};

Il renderer riceve (FakeBox3DParams, FakeBox3DRenderState) separati — il Shape pubblico non ha mai campi runtime. Le
prebuilt lib vedono solo FakeBox3DParams, che cambia raramente.

---
2. Glob wildcard nel build system + file legacy residui

Causa radice: add_files("*.cpp") raccoglie tutto quello che trova. Un file lasciato per errore rompe il build con
duplicati.

Soluzione: File espliciti per i target critici, glob solo per le directory di examples (che sono additive, mai
sottrattive).

-- CLI: esplicito per evitare residui
target("chronon3d_cli")
    add_files(
        "apps/chronon3d_cli/main.cpp",
        "apps/chronon3d_cli/commands/*.cpp",
        "apps/chronon3d_cli/utils/*.cpp"
    )

-- Examples: glob va bene (solo additive, nessun simbolo condiviso con core)
target("chronon3d_examples_lib")
    add_files("examples/**.cpp")

In alternativa: aggiungere un CI check che compila con -Werror e verifica zero LNK2005.

---
3. Composizioni non visibili senza un registro centrale

Causa radice: tenere un indice manuale separato dal file che definisce la composition crea drift e richiede un secondo
passaggio ogni volta che si aggiunge un esempio.

Soluzione: le composition si registrano nel loro translation unit con `CHRONON_REGISTER_COMPOSITION(...)` e il
`CompositionRegistry` le carica automaticamente quando viene costruito. Nessun flag nascosto, nessuna lista manuale,
nessuna eccezione manuale: il registry si popola da solo quando le composition sono linkate.

---
4. Refactoring massivo rompe le sessioni attive

Causa radice: Non esiste un contratto esplicito tra "API pubblica" (usata da examples, test, CLI) e "dettaglio
implementativo" (riorganizzabile liberamente).

Soluzione: Stabilizzare un insieme di header di facade che non cambiano path mai, anche se il codice interno si riorganizza.

include/chronon3d/chronon3d.hpp      ← umbrella header (stabile)
include/chronon3d/scene/             ← API pubblica (stabile)
include/chronon3d/renderer/          ← API pubblica (stabile)
src/renderer/internal/               ← dettaglio impl (libero di cambiare)

Le regole:
- include/ si muove solo con major version bump
- src/ si può riorganizzare liberamente
- I move di file in src/ NON rompono niente se gli header in include/ restano stabili

Con CMake questo si ottiene con `target_include_directories(... PUBLIC include)` e non esporre mai `src/` all'esterno.

---
5. M_PI e costanti matematiche platform-specific

Causa radice: Dipendenza da estensioni POSIX non standard in MSVC.

Soluzione: Una header centralizzata, una volta, per sempre.

// include/chronon3d/math/constants.hpp
#pragma once
#include <glm/gtc/constants.hpp>

namespace chronon3d::math {
    inline constexpr float pi      = glm::pi<float>();
    inline constexpr float two_pi  = glm::two_pi<float>();
    inline constexpr float half_pi = glm::half_pi<float>();
    inline constexpr float e       = glm::e<float>();
}

<cmath> non entra mai direttamente nel codice del renderer — si usa sempre math::pi. CI che fa build su MSVC + GCC +
Clang crea il safety net.

---
Priorità di implementazione

┌──────────────────────────────────────────────┬────────────────────┬────────────┬──────────────────────────┐
│                      #                       │      Impatto       │   Effort   │          Quando          │
├──────────────────────────────────────────────┼────────────────────┼────────────┼──────────────────────────┤
│ Separare params/render-state nelle struct 3D │ Crash → zero       │ 2h         │ Subito                   │
├──────────────────────────────────────────────┼────────────────────┼────────────┼──────────────────────────┤
│ Header math/constants.hpp                    │ Minor rebuild fail │ 30min      │ Subito                   │
├──────────────────────────────────────────────┼────────────────────┼────────────┼──────────────────────────┤
│ File espliciti nel CLI target                │ Build residui      │ 30min      │ Subito                   │
├──────────────────────────────────────────────┼────────────────────┼────────────┼──────────────────────────┤
│ Facade headers stabili in include/           │ Refactoring safe   │ 4h         │ Prossima sprint          │
├──────────────────────────────────────────────┼────────────────────┼────────────┼──────────────────────────┤
│ Plugin discovery / no WHOLEARCHIVE           │ Architectural debt │ 1-2 giorni │ Quando examples crescono │
└──────────────────────────────────────────────┴────────────────────┴────────────┴──────────────────────────┘

I primi tre sono cambiamenti piccoli con ritorno immediato. L'ultimo è quello che elimina la classe intera di problemi
"prebuilt lib incompatibile" una volta per tutte.

✻ Worked for 34s risolvi
