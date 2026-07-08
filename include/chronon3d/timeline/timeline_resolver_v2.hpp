// =============================================================================
// chronon3d/timeline/timeline_resolver_v2.hpp
//
// M1.7 Step 1 — Timeline V2 single source of truth.
//
// 4 nuovi simboli pubblici canonici che sostituiscono la timeline legacy
// (Layer.from/duration gestiti dal render graph + SequenceContext sibling)
// e che diventano il contratto SSoT per il frame locale visibile dal
// contenuto.
//
// Vincoli AGENTS.md v0.1 (Cat-3 freeze):
//   * ZERO nuovi singleton / registry / cache / service-locator.
//   * NO #include <msdfgen> / <libtess2> / <unicode[/...]>.
//   * ZERO modifiche al codice esistente (tutti i test preesistenti
//     rimangono PASS bit-identical).
//   * ABI pubblico invariato (Composition::evaluate() deprecated path
//     resta operativo fino a Step 4).
//
// Namespace: `chronon3d::timeline::v2` distinto da `chronon3d::SequenceContext`
// esistente (sequence.hpp) per evitare ogni clash di simboli omonimi.
//
// Step 1 è solo AGGIUNTA. Step 2 (legacy adapters) + Step 3 (migrate
// new content) + Step 4 (elimination) restano forward-point tickets.
// Vedi `docs/tickets/TICKET-SEQUENCE-LOCAL-FRAME.md` per il piano completo.
// =============================================================================

#pragma once

#include <chronon3d/core/types/frame.hpp>

#include <functional>
#include <string>
#include <vector>

namespace chronon3d::timeline::v2 {

// -----------------------------------------------------------------------------
// `SequenceBuilder` — tipo passato al build_callback di SequenceNode.
// Step 1: struct COMPLETO VUOTO (zero-overhead: sizeof==1 byte tipico,
// ABI-stable). Step 3 (TICKET-SEQUENCE-LOCAL-FRAME forward-point) ridefinirà
// `SequenceBuilder` come full type esposto con le API per popolare i layer
// della sequence (`SequenceBuilder::layer(string, lambda)`, ecc.).
//
// Rationale per la scelta "completo vuoto" (vs forward-declaration):
//   * `std::function<void(SequenceBuilder&)>` richiede il tipo completo
//     all'instanziazione del `std::function` stesso (NON solo
//     all'invocazione del callable). Con un forward-declaration, GCC 13 +
//     libstdc++ è permissivo ma libc++/MSVC in strict mode possono rompere.
//     Step 1 Cat-3 freeze vuole portabilità bit-identical su tutti i target
//     del SDK → meglio un empty struct portabile che un forward-decl
//     implementation-defined.
//   * Il field `std::function<void(SequenceBuilder&)>` NON viene invocato
//     a Step 1 (TimelineResolver::resolve ritorna active_sequences vuoto)
//     → ABI delta = 0 anche dopo Step 3 ridefinirà SequenceBuilder (gli
//     empty struct layout non cambiano vtables / field offsets quando
//     aggiungono field, perché all callers serve solo l'opaque-handle).
// -----------------------------------------------------------------------------
struct SequenceBuilder {};

// -----------------------------------------------------------------------------
// TimeRange — coppia canonica { from, duration } che definisce quando un
// intervallo è attivo sul timeline globale. POD constexpr-friendly.
// Sostituisce il pattern `frame >= start && frame < end` sparso nei content.
// -----------------------------------------------------------------------------
struct TimeRange {
    Frame from{0};
    // NB: `duration{1}` qui è il "no range specified" safety default (un
    // singolo frame di default per default-construction-in safety contexts,
    // es. SequenceNode costruito senza argomenti). NON è il legacy
    // "duration=1 = statico magic" del piano (legacy item D): il legacy
    // magic vive in `composition()[`&&`Layer.from/duration`-based]`
    // per saltare l'evaluation dell'animator, mentre qui 1 è solo
    // zero-overhead default/sentinel per costruttori e nessuno skip-rule
    // ne dipende. Step 3 (forward-point) ridefinirà il default a un
    // explicit-required sentinella (`Frame::invalid()` simile) per
    // distinguerlo ulteriormente.
    Frame duration{1};
};

// -----------------------------------------------------------------------------
// SequenceNode — un nodo della timeline con un range canonico e un
// build_callback opzionale che produce contenuto durante la finestra attiva.
//
//   * `name`  : identificatore logico (es. "title", "camera_intro").
//   * `range` : TimeRange canonica. Attivo quando
//               `global_frame >= range.from && global_frame < range.from + range.duration`.
//   * `build` : callback opzionale invocato quando la sequence è attiva
//               (Step 3 ridefinirà `SequenceBuilder` come full type).
//
// Step 1: type declared, callable non invocato. Step 3: full invoke path.
// -----------------------------------------------------------------------------
struct SequenceNode {
    std::string name{};
    TimeRange  range{};
    std::function<void(SequenceBuilder&)> build{}; // nullptr-safe (default-empty)
};

// -----------------------------------------------------------------------------
// TimelineSampleContext — unico value-type che descrive la situazione di
// sampling temporale. Sostituisce le 5 coordinate temporali duplicate
// (composition / layer / sequence / animator / video source frame) che
// attualmente coesistono in posti diversi.
//
//   * `global_frame`   : frame sulla timeline globale (1-based nella pipeline
//                        di authoring).
//   * `local_frame`    : `global_frame - sequence_start` quando la sequence
//                        è attiva, altrimenti 0 (post-Step-2 questa logica
//                        vive nel resolver, non più nel content).
//   * `sequence_start` : copia di `range.from` della sequence attiva;
//                        consente ai sampler di ricostruire `local_frame`
//                        anche in presenza di cache (FNV-1a `params_hash`).
//   * `fps`            : `frame_rate.numerator / frame_rate.denominator`
//                        cached qui per evitare lookup ripetuti durante
//                        sampling massivo (M=10..100K animazioni/frame).
// -----------------------------------------------------------------------------
struct TimelineSampleContext {
    Frame global_frame{0};
    Frame local_frame{0};          // post-Step-2: = global_frame - sequence_start quando attivo
    Frame sequence_start{0};
    float fps{0.0f};
};

// -----------------------------------------------------------------------------
// SceneDescriptor — input canonico del TimelineResolver.
//
// Step 1 è PODO minimale: solo `composition_range` + `sequences[]`. Il
// `chronon3d::Composition` esistente (composition.hpp:60+) resta intatto e
// rimane l'input della pipeline legacy fino a Step 4; non viene riusato qui
// per evitare coupling forte + include pesante (`<scene/model/core/scene.hpp>`
// + `<camera_v1/*.hpp>` etc.).
// -----------------------------------------------------------------------------
struct SceneDescriptor {
    TimeRange composition_range{};
    std::vector<SequenceNode> sequences{};
};

// -----------------------------------------------------------------------------
// ResolvedScene — output canonico del TimelineResolver.
//
// Step 1 ritorna SEMPRE `active_sequences` vuoto (forward-point Step 2):
// il contratto è cablato ma la logica di attivazione reale
// (filter su descriptor.sequences contro `global_frame`) vive a Step 2
// quando atterrano i legacy adapters (`Layer.from/duration -> SequenceNode`).
//
// I campi sono POD/aggregate-friendly così che il render graph (post-Step-3)
// possa consumare `ResolvedScene::active_sequences` con un singolo range-for
// senza dipendere da nessun singleton o service-locator.
// -----------------------------------------------------------------------------
struct ResolvedScene {
    TimeRange composition_range{};
    std::vector<SequenceNode> active_sequences{};
    TimelineSampleContext sample{};
};

// -----------------------------------------------------------------------------
// TimelineResolver — single source of truth sul frame locale.
//
// Step 1 surface minima (header-only inline; no cache; no singleton). Il
// resolver è una value type stateless: copy/const-evaluate-friendly; può
// essere conservato in `RenderSession` (post-Step-2) o in `RenderGraphContext`
// senza dover introdurre nessun nuovo global registry.
//
// Regola finale (vincolante per Chiusura M1.7):
//   `TimelineResolver` decide cosa esiste al frame globale.
//   `RenderGraphBuilder` riceve scena già risolta.
//   `Renderer` non inventa timeline (no skip su frame) e non inventa
//   asset (no fallback) — vedi anche `TICKET-ASSET-READINESS`.
// -----------------------------------------------------------------------------
class TimelineResolver {
public:
    /// constexpr-friendly arithmetic via Frame helpers (frame.hpp:43-44).
    static constexpr Frame make_local_frame(Frame global_frame,
                                            Frame sequence_start) noexcept {
        // `Frame - Frame` su frame.hpp:43 — `global_frame.integral() - sequence_start.integral()`.
        // Se `global_frame < sequence_start`, locale rimane 0 (sequence non ancora attiva).
        return (global_frame >= sequence_start) ? (global_frame - sequence_start)
                                                : Frame{0};
    }

    /// Risolve la scene descriptor al frame globale dato. Step 1 ritorna
    /// una ResolvedScene minimale (`composition_range` preservata dal
    /// descriptor, `active_sequences` vuoto). Step 2 wireà la reale
    /// attivazione delle sequence contro `descriptor.sequences` (forward-point).
    ///
    /// Output invariante bit-identical:
    ///   * `composition_range.from` == `descriptor.composition_range.from`
    ///   * `sample.global_frame`    == `global_frame` arg
    ///   * `sample.sequence_start`  == `descriptor.composition_range.from`
    ///   * `sample.fps`             == `fps` arg
    ///   * `sample.local_frame`     = `make_local_frame(global_frame, sequence_start)`
    ///     — già computato qui a Step 1 così che i consumer (animator/sampler,
    ///     post-Step-3) possano leggerlo senza rifare l'arithmetic.
    [[nodiscard]] ResolvedScene
    resolve(const SceneDescriptor& descriptor,
            Frame global_frame,
            float fps) const noexcept {
        const Frame sequence_start = descriptor.composition_range.from;
        return ResolvedScene{
            .composition_range = descriptor.composition_range,
            .active_sequences  = {},
            .sample = TimelineSampleContext{
                .global_frame   = global_frame,
                .local_frame    = make_local_frame(global_frame, sequence_start),
                .sequence_start = sequence_start,
                .fps            = fps,
            },
        };
    }
};

} // namespace chronon3d::timeline::v2
