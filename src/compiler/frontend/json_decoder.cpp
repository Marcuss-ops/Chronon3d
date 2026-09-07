#include "../compiler_stages.hpp"

// The JSON decoder remains owned by render_plan during this compatibility
// wave. This translation unit is the compiler frontend seam for its migration.
namespace chronon3d::compiler::frontend {
void json_decoder_stage_anchor() noexcept {}
}
