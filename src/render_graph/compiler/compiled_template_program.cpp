// ──────────────────────────────────────────────────────────────────────────────
// src/render_graph/compiler/compiled_template_program.cpp
// Fase A (TICKET-VIDEO-COMPILER-ARCH-V1) — derive CompiledTemplateProgram
// from a CompiledFrameGraph.  Single source of truth: shared_ptr to the
// compiled graph; template-level metadata is lifted once at compile time.
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/render_graph/compiler/compiled_template_program.hpp>

#include <algorithm>  // std::max

namespace chronon3d::graph {

#include "compiled_template_program_support.inc"
#include "compiled_template_program_memory.inc"
#include "compiled_template_program_temporal.inc"
#include "compiled_template_program_prepare.inc"
#include "compiled_template_program_compile.inc"

} // namespace chronon3d::graph
