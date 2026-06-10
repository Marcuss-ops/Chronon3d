@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if %errorlevel% neq 0 exit /b %errorlevel%
echo MSVC environment ready
set VCPKG_ROOT=C:\vcpkg
set PATH=%VCPKG_ROOT%;%PATH%
cmake --build build --preset win 2>&1
if %errorlevel% neq 0 exit /b %errorlevel%
echo Build completed successfully
