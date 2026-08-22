@echo off
REM One-click configure: fetch the vcpkg submodule and run the CMake preset.
REM vcpkg bootstraps itself (vcpkg.exe) and installs vcpkg.json the first time CMake configures.
REM No Python needed. Equivalent to:  git submodule update --init  +  cmake --preset default
cd /d "%~dp0.."
git submodule update --init --depth 1 vcpkg || exit /b 1
cmake --preset default || exit /b 1
echo.
echo Done. Open build\Snowstorm.slnx (Snowstorm-Editor is the startup project),
echo or build from the command line:  cmake --build --preset debug
pause
