#pragma once

// ==============================================================================
// content/certification/cert_timeline.hpp
//
// TICKET-TIMELINE-CERT — Timeline & Sequence functional certification
// compositions (P1).
//
// 5 compositions that verify Sequence V2 timeline behavior:
//
//   CertTimelineBoundary  — single sequence from=50, duration=30.
//                            Frame 49 → empty, 50 → local 0 visible,
//                            79 → local 29 visible, 80 → empty.
//   CertTimelineLocalFrame — sequence encodes local_frame as grayscale
//   CertTimelineNested     — 2-level nested sequence boundaries
//   CertTimelineOverlap    — overlapping sequences (each with distinct color)
//   CertTimelineAnimation  — fade-in animation driven by local_frame
//
// 1920×1080 canvas. All compositions use SequenceBuilder exclusively.
// ==============================================================================

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::certification {

chronon3d::Composition cert_timeline_boundary();
chronon3d::Composition cert_timeline_local_frame();
chronon3d::Composition cert_timeline_nested();
chronon3d::Composition cert_timeline_overlap();
chronon3d::Composition cert_timeline_animation();

} // namespace chronon3d::content::certification
