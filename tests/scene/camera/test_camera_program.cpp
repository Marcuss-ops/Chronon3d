// ==============================================================================
// tests/scene/camera/test_camera_program.cpp
//
// CameraProgram tests — old builder path tests removed in PR12.
// The builder API (motion(), trajectory(), orient(), banking()) and its
// evaluate(CameraMotionContext, ConstraintSession) overload have been removed.
//
// The RECOMMENDED path is compile_camera() + evaluate(CameraEvalContext, CameraSession).
// See test_camera_program_compiled.cpp for compiled path tests.
// ==============================================================================
