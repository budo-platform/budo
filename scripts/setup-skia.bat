@echo off
REM Skia Setup Script for Windows
REM Downloads and builds Skia from source

setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set SKIA_DIR=%SCRIPT_DIR%..\third_party\skia
set BUILD_DIR=%SKIA_DIR%\out\Release
for /f "tokens=2 delims=^\"" %%A in ('findstr /b "set(BUDO_SKIA_DESKTOP_GIT_TAG" "%SCRIPT_DIR%..\cmake\BudoDependencyLock.cmake"') do set SKIA_COMMIT=%%A
if not defined SKIA_COMMIT (
    echo Error: cannot read Skia commit from dependency lock
    exit /b 1
)

echo ===================================
echo Skia Setup Script (Windows)
echo ===================================
echo.

REM Check prerequisites
echo Checking prerequisites...

where git >nul 2>&1
if errorlevel 1 (
    echo Error: git is not installed
    echo Please install Git from https://git-scm.com/
    exit /b 1
)

where python >nul 2>&1
if errorlevel 1 (
    echo Error: python is not installed
    echo Please install Python from https://www.python.org/
    exit /b 1
)

where ninja >nul 2>&1
if errorlevel 1 (
    echo Warning: ninja is not installed
    echo Please install ninja and add it to PATH
    echo Download from: https://github.com/nicoowr/nicookoch-ninja/releases
    exit /b 1
)

echo Prerequisites OK
echo.

REM Clone Skia
if exist "%SKIA_DIR%\.git" (
    echo Skia already cloned, selecting locked commit...
    cd /d "%SKIA_DIR%"
) else (
    echo Cloning Skia...
    if not exist "%SKIA_DIR%\.." mkdir "%SKIA_DIR%\.."
    git clone --no-checkout https://skia.googlesource.com/skia.git "%SKIA_DIR%"
    cd /d "%SKIA_DIR%"
)
git fetch origin %SKIA_COMMIT%
git checkout --detach %SKIA_COMMIT%

REM Sync dependencies
echo Syncing Skia dependencies...
python tools\git-sync-deps

REM Generate build files
echo Generating build files...
bin\gn gen "%BUILD_DIR%" --args="is_official_build=true is_component_build=false skia_use_system_expat=false skia_use_system_libjpeg_turbo=false skia_use_system_libpng=false skia_use_system_libwebp=false skia_use_system_zlib=false skia_use_system_harfbuzz=false skia_use_system_icu=false skia_enable_pdf=false skia_enable_skottie=false skia_enable_skshaper=false"

REM Build
echo Building Skia...
ninja -C "%BUILD_DIR%"

echo.
echo ===================================
echo Skia built successfully!
echo ===================================
echo.
echo To build budo, run:
echo.
echo   cmake -B build -DSKIA_PATH=%SKIA_DIR%
echo   cmake --build build --config Release
echo.

endlocal
