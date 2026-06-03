#!/usr/bin/env bash
set -euo pipefail

BIN="./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli"

echo "Cleaning up state files..."
rm -f *_state.bin

echo "1. Running CameraFrustumCullingPrecisionTest..."
$BIN render CameraFrustumCullingPrecisionTest --frames 0-90x45 -o output/CameraFrustumCullingPrecisionTest_####.png -report

echo "2. Running CameraKinematicJerkAndInterpolationTest..."
$BIN render CameraKinematicJerkAndInterpolationTest --frames 0-90x45 -o output/CameraKinematicJerkAndInterpolationTest_####.png -report

echo "3. Running CameraDepthSortingStressTest..."
$BIN render CameraDepthSortingStressTest --frames 0-90x45 -o output/CameraDepthSortingStressTest_####.png -report

echo "4. Running CameraSubpixelJitterValidationTest..."
$BIN render CameraSubpixelJitterValidationTest --frames 0-119x1 -o output/CameraSubpixelJitterValidationTest_####.png -report

echo "5. Running CameraMultiTargetBoundingBoxFitTest..."
$BIN render CameraMultiTargetBoundingBoxFitTest --frames 0-90x45 -o output/CameraMultiTargetBoundingBoxFitTest_####.png -report

echo "6. Running CameraOrbitTargetLockTest..."
$BIN render CameraOrbitTargetLockTest --frames 0-90x45 -o output/CameraOrbitTargetLockTest_####.png -report

echo "7. Running CameraDollyPerspectiveScaleTest..."
$BIN render CameraDollyPerspectiveScaleTest --frames 0-90x45 -o output/CameraDollyPerspectiveScaleTest_####.png -report

echo "8. Running CameraParentNullRigTest..."
$BIN render CameraParentNullRigTest --frames 0-90x45 -o output/CameraParentNullRigTest_####.png -report

echo "9. Running CameraRollPanTiltGridTest..."
$BIN render CameraRollPanTiltGridTest --frames 0-90x30 -o output/CameraRollPanTiltGridTest_####.png -report

echo "10. Running CameraSafeFramingAspectRatioTests..."
$BIN render CameraSafeFramingAspectRatioTest_16_9 --frames 0-90x45 -o output/CameraSafeFramingAspectRatioTest_16_9_####.png -report
$BIN render CameraSafeFramingAspectRatioTest_1_1 --frames 0-90x45 -o output/CameraSafeFramingAspectRatioTest_1_1_####.png -report
$BIN render CameraSafeFramingAspectRatioTest_9_16 --frames 0-90x45 -o output/CameraSafeFramingAspectRatioTest_9_16_####.png -report
$BIN render CameraSafeFramingAspectRatioTest_4_5 --frames 0-90x45 -o output/CameraSafeFramingAspectRatioTest_4_5_####.png -report

echo "Copying reports to output/ folder..."
mkdir -p output
cp *_report*.json output/ || true

echo "============================================="
echo "All camera verification tests completed!"
echo "Check CameraTestReport.json for summary results."
echo "============================================="
