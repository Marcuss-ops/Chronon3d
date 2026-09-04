// ──────────────────────────────────────────────────────────────────────────────
// src/render_graph/compiler/compiled_template_program.cpp
// Fase A (TICKET-VIDEO-COMPILER-ARCH-V1) — derive CompiledTemplateProgram
// from a CompiledFrameGraph.  Single source of truth: shared_ptr to the
// compiled graph; template-level metadata is lifted once at compile time.
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/render_graph/compiler/compiled_template_program.hpp>

#include <algorithm>  // std::max

namespace chronon3d::graph {

#include "compiled_template_program_support_detail.hpp"
#include "compiled_template_program_memory_detail.hpp"
#include "compiled_template_program_temporal_detail.hpp"
#include "compiled_template_program_prepare_detail.hpp"
#include "compiled_template_program_compile_detail.hpp"

} // namespace chronon3d::graph
