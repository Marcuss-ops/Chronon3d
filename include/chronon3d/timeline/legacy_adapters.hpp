// =============================================================================
// chronon3d/timeline/legacy_adapters.hpp
//
// M1.7 Step 2 — Legacy Adapters per la safe migration Timeline V2.
//
// 3 funzioni free additive, puramente header-only inline, per convertire i
// tipi legacy `chronon3d::Layer` e `chronon3d::FrameContext` ai nuovi tipi
// V2 canonici (`SequenceNode` + `TimelineSampleContext`) definiti in
// `timeline_resolver_v2.hpp`. Non modificano lo stato, non alloccano, non
// introducono singleton/registry/cache/service-locator.
//
// File destinato all'eliminazione allo Step 4 (TICKET-SEQUENCE-LOCAL-FRAME) —
// gli adapter diventano obsoleti quando il render graph + il contenuto
// migrano completamente alla nuova timeline (`Step 3` migrate new content).
//
// Vincoli AGENTS.md v0.1 (Cat-3 freeze):
//   * ZERO nuovi singleton / registry / cache / service-locator.
//   * NO #include <msdfgen> / <libtess2> / <unicode[/...]>.
//   * ZERO modifiche al codice esistente (tutti i test preesistenti
//     rimangono PASS bit-identical — gli adapter sono AGGIUNTI ma NON
//     chiamati dal render graph / scene builder / composition).
//   * ABI pubblico espanso con 3 nuove free functions (non 3 nuove classi)
//     → ABI footprint incrementale trascurabile.
//   * NO build-side change obbligatorio per i consumer esistenti: gli
//     adapter sono opt-in (incluso solo se il consumer importa questo
//     header esplicitamente).
//
// Namespace: `chronon3d::timeline::v2` (stesso di `timeline_resolver_v2.hpp`).
// =============================================================================

#pragma once

#include <chronon3d/timeline/timeline_resolver_v2.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/core/types/frame_context.hpp>

namespace chronon3d::timeline::v2 {

// -----------------------------------------------------------------------------
// `is_active` — predicato temporale canonico per `TimeRange`.
//
// Replica la semantica di `chronon3d::Layer::active_at(Frame)` per il tipo
// `TimeRange` V2, in modo che il render graph e i sampler possano usare un
// singolo predicato uniforme.
//
// Regole (bit-identical a `Layer::active_at` modulo il flag `visible`):
//   * `frame < range.from`                              → false (pre-range)
//   * `range.duration < 0` (legacy "infinito" sentinel)  → true (sempre attivo dopo `from`)
//   * `frame < range.from + range.duration`              → true (in-range)
//   * altrimenti                                        → false (post-range)
//
// `constexpr noexcept` per consentire l'uso in branch predittivi nei path
// hot del render graph e degli animator/sampler. Equivalente al
// `Layer::active_at` method esistente ma privo della dipendenza dal flag
// `visible` (che è una preoccupazione di rendering, non di timeline).
// -----------------------------------------------------------------------------
[[nodiscard]] constexpr bool
is_active(const TimeRange& range, Frame frame) noexcept {
    if (frame < range.from) return false;
    if (range.duration < 0) return true;  // legacy "infinito" sentinel
    return frame < (range.from + range.duration);
}

// -----------------------------------------------------------------------------
// Adapter 1 — `make_sequence_from_layer` — converte un `Layer` legacy in
// un `SequenceNode` V2 canonico, propagando `name` + `range` (from + duration
// verbatim, incluso il sentinel `duration = -1` per "infinito").
//
// Il `build` callback è lasciato default-empty (`std::function{}`): Step 3
// popolerà la callback quando il contenuto inizia a dichiarare
// esplicitamente la sequence attiva via `s.sequence(...)`. Per Step 2 il
// callback vuoto è bit-identical al comportamento pre-migration (il render
// graph esistente usa già `Layer.from/.duration` direttamente).
// -----------------------------------------------------------------------------
[[nodiscard]] inline SequenceNode
make_sequence_from_layer(const chronon3d::Layer& layer) {
    return SequenceNode{
        .name  = std::string(layer.name),  // std::pmr::string → std::string
        .range = TimeRange{
            .from      = layer.from,
            .duration  = layer.duration  // verbatim, incluso sentinel -1
        },
        .build = {}  // empty per Step 2; Step 3 popolerà
    };
}

// -----------------------------------------------------------------------------
// Adapter 2 — `make_sample_context` — converte un `FrameContext` legacy nel
// nuovo `TimelineSampleContext` V2 in modalità "backward compat identity":
//
//   * `global_frame`   = `ctx.frame`       (frame globale verbatim)
//   * `local_frame`    = `ctx.frame`       (IDENTITY MAP per compat Step 2)
//   * `sequence_start` = `Frame{0}`        (sequence non ancora esplicita)
//   * `fps`            = `static_cast<float>(ctx.fps())`
//
// Rationale per la identity map `local_frame = global_frame`:
//   A Step 2 il contenuto legacy NON ha ancora dichiarato esplicitamente
//   "questa è la mia sequence attiva" — quindi non c'è una
//   `sequence_start` da sottrarre. Il nuovo sample context è bit-identical
//   al comportamento pre-migration (l'animator vedeva `ctx.frame` come
//   sia globale che locale). Step 3 abiliterà la mappa reale
//   `local_frame = global_frame - sequence_start` quando i content nuovi
//   iniziano a usare `s.sequence("name", TimeRange, build_callback)`.
//
// NB: la firma ritorna per valore (POD constexpr-friendly, no allocs).
// `noexcept` per consentire l'uso in path noexcept-propagation.
// -----------------------------------------------------------------------------
[[nodiscard]] inline TimelineSampleContext
make_sample_context(const chronon3d::FrameContext& ctx) noexcept {
    return TimelineSampleContext{
        .global_frame   = ctx.frame(),
        .local_frame    = ctx.frame(),        // Step 2: identity map (compat)
        .sequence_start = Frame{0},            // Step 2: sequence non esplicita
        .fps            = static_cast<float>(ctx.fps())
    };
}

} // namespace chronon3d::timeline::v2
