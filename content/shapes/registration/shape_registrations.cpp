#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::shapes {
    Composition shape_proofs();
    Composition shape_motion_proofs();
}

CHRONON_REGISTER_COMPOSITION("ShapeProofs", chronon3d::content::shapes::shape_proofs)
CHRONON_REGISTER_COMPOSITION("ShapeMotionProofs", chronon3d::content::shapes::shape_motion_proofs)
