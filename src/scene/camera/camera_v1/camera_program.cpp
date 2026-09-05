// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_program.cpp
//
// CameraProgram implementation — V1 compiled evaluation path.
//
// CAM-03 / DOC 02 extensions:
//   - ProjectionSpec dispatch is centralised in apply_projection_spec()
//     (handles ZoomProjection / FovProjection / PhysicalLensProjection).
//   - The EvaluatedProjection snapshot is exposed via
//     camera_math::make_evaluated_projection() defined in
//     scene/camera/camera_v1/evaluated_projection.hpp.
//   - Single look-at policy: when both an OrientationSpec look-at AND a
//     LookAtConstraint are present, the orientation wins and the constraint
//     is skipped (CAM-03 / DOC 02).  A Warning diagnostic is emitted so
//     composition authors can correct their descriptor.
//
// The old builder API (motion(), trajectory(), orient(), banking()) and its
// evaluate(CameraMotionContext, ConstraintSession) overload were removed in
// PR12. Only the compiled evaluate(CameraEvalContext, CameraSession) remains.
// Use compile_camera() + evaluate() for all camera evaluation.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>
#include <chronon3d/scene/camera/camera_v1/evaluated_projection.hpp>  // CAM-03 snapshot
#include <chronon3d/scene/model/core/hierarchy_resolver.hpp>  // ResolvedSceneTransforms::world_position
#include <chronon3d/animation/path/spatial_bezier_path.hpp>  // quat_look_along, quat_to_camera_euler
#include <chronon3d/animation/effects/wiggle.hpp>             // CAM-04 — wiggle3D abs-time

#include "camera_program_sources.hpp"  // FASE 4 Step 1 — source eval helpers
#include "camera_program_constraints.hpp"  // FASE 4 Step 2 — constraint eval

#include <cmath>
#include <algorithm>
#include <type_traits>

// ═══════════════════════════════════════════════════════════════════════════
// camera_program.cpp — CameraProgram compilation-path anchor.
//
// The evaluation pipeline lives in focused sibling TUs (pure moves):
//   - camera_program_orientation.cpp     apply_orientation_spec (+ free fn)
//   - camera_program_source_dispatch.cpp evaluate_compiled_source
//   - camera_program_evaluate.cpp        CameraProgram::evaluate
// ═══════════════════════════════════════════════════════════════════════════
