# Faster Parallel Build Script for Chronon3D Tests
# Utilizes all CPU cores and targets ONLY the test target to avoid rebuilding CLI/presets/samples!

Write-Host "Rebuilding tests with parallel compilation..." -ForegroundColor Cyan

# Run CMake build with parallel threads utilizing all logical processors
cmake --build build_vs --config Release --target chronon3d_renderer_tests --parallel 16

if ($LASTEXITCODE -eq 0) {
    Write-Host "Build Succeeded!" -ForegroundColor Green
} else {
    Write-Host "Build Failed!" -ForegroundColor Red
}
