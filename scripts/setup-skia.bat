@echo off
REM Skia Setup Script for Windows
REM Downloads and builds Skia from source

setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set SKIA_DIR=%SCRIPT_DIR%..\third_party\skia
set BUILD_DIR=%SKIA_DIR%\out\Release
REM The commit is the 2nd field when splitting on double quotes. A quote can
REM only be a FOR /F delimiter with unquoted, caret-escaped options.
for /f tokens^=2^ delims^=^" %%A in ('findstr /b /l /c:"set(BUDO_SKIA_DESKTOP_GIT_TAG " "%SCRIPT_DIR%..\cmake\BudoDependencyLock.cmake"') do set SKIA_COMMIT=%%A
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
    git clone --no-checkout https://skia.googlesource.com/skia.git "%SKIA_DIR%" || exit /b 1
    cd /d "%SKIA_DIR%"
)
git fetch origin %SKIA_COMMIT% || exit /b 1
git checkout --detach %SKIA_COMMIT% || exit /b 1

REM Sync dependencies
echo Syncing Skia dependencies...
python tools\git-sync-deps || exit /b 1

REM Generate build files
echo Generating build files...
REM Arguments go in args.gn, which avoids nesting GN string quotes in --args.
REM /MD: Budo and its other dependencies use the DLL C runtime; cl.exe
REM defaults to the static one (/MT), and the two cannot be linked together.
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
(
    echo is_official_build=true
    echo is_component_build=false
    echo skia_use_system_expat=false
    echo skia_use_system_libjpeg_turbo=false
    echo skia_use_system_libpng=false
    echo skia_use_system_libwebp=false
    echo skia_use_system_zlib=false
    echo skia_use_system_harfbuzz=false
    echo skia_use_system_icu=false
    echo skia_enable_pdf=false
    echo skia_enable_skottie=false
    echo skia_enable_skshaper=false
    echo extra_cflags = [ "/MD" ]
) > "%BUILD_DIR%\args.gn"
bin\gn gen "%BUILD_DIR%" || exit /b 1

REM Build
echo Building Skia...
REM Only the libraries Budo links: the default target also generates gen\skia.h,
REM which fails whenever gn prints a warning.
ninja -C "%BUILD_DIR%" skia expat || exit /b 1

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
