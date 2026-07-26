# TICKET-ANIM-SEQUENCE-CONSOLIDATE — Sequence surface consolidation

## Stato: CLOSED — 2026-07-26

`SequenceBuilder`, `FrameContext::local_time()` e `TimelineResolver` sono
l’unico percorso produttivo per le sequence.

## Rimozione completata

Eliminati fisicamente:

- `SequenceContext`;
- `sequence(FrameContext, from, duration)`;
- `spring(SequenceContext, ...)`;
- `include/chronon3d/timeline/sequence.hpp`.

I test timeline, spring e certificazione sono stati migrati al tempo locale
canonico. L’accesso casuale a un frame non dipende dal rendering dei frame
precedenti.

## Verifica

La ricerca produttiva non trova più `SequenceContext` o il vecchio header.
La build di `chronon3d_core_tests` è riuscita; i filtri Sequence e
FrameContext passano 35/35 test e 175/175 assertion.
