@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if %errorlevel% neq 0 exit /b %errorlevel%
echo MSVC environment set up successfully
set VCPKG_ROOT=C:\vcpkg
set PATH=%VCPKG_ROOT%;%PATH%
rm -rf build
cmake -B build -S . --preset win-release
if %errorlevel% neq 0 exit /b %errorlevel%
echo Configure completed successfully
