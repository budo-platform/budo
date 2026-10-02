# Windows build script for Budo.
# Runs inside the Vagrant VM via:
#   vagrant provision windows-build --provision-with build
#
# Builds Skia from source (if needed), then configures + compiles Budo
# with MSVC (Visual Studio 2022 Build Tools) and Ninja.

$ErrorActionPreference = "Stop"

$SRC_DIR    = "C:\budo"
$BUILD_DIR  = "C:\budo-build"
$OUTPUT_DIR = "C:\budo-output"
$SKIA_DIR   = "C:\budo-skia"
$dependencyLock = Get-Content "$SRC_DIR\cmake\BudoDependencyLock.cmake" -Raw
$skiaCommit = [regex]::Match($dependencyLock, 'set\(BUDO_SKIA_DESKTOP_GIT_TAG "([0-9a-f]{40})"\)').Groups[1].Value
$quickJsDefault = [regex]::Match($dependencyLock, 'set\(BUDO_QUICKJS_IMPL_DEFAULT "([^"]+)"\)').Groups[1].Value
$sdlVersion = [regex]::Match($dependencyLock, 'set\(BUDO_SDL2_WINDOWS_VERSION "([^"]+)"\)').Groups[1].Value
$sdlSha256 = [regex]::Match($dependencyLock, 'set\(BUDO_SDL2_WINDOWS_URL_HASH\s+"SHA256=([0-9a-f]{64})"\)').Groups[1].Value
if (-not $skiaCommit -or -not $quickJsDefault -or -not $sdlVersion -or -not $sdlSha256) {
    throw "Cannot read dependency identities from the shared lock"
}

Write-Host "=== Budo Windows Build ===" -ForegroundColor Cyan

# ── Refresh PATH ──────────────────────────────────────────────────────────────

$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
            [System.Environment]::GetEnvironmentVariable("Path", "User")

# ── Locate MSVC (vcvarsall.bat) ───────────────────────────────────────────────

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Host "ERROR: vswhere not found — Visual Studio Build Tools not installed." -ForegroundColor Red
    exit 1
}

$vsInstall = & $vswhere -latest -property installationPath
$vcvarsall = Join-Path $vsInstall "VC\Auxiliary\Build\vcvarsall.bat"
if (-not (Test-Path $vcvarsall)) {
    Write-Host "ERROR: vcvarsall.bat not found at $vcvarsall" -ForegroundColor Red
    exit 1
}
Write-Host "Using MSVC from: $vsInstall" -ForegroundColor Green

# ── Build Skia from source (if not already built) ────────────────────────────

if (-not (Test-Path "$SKIA_DIR\out\Release\skia.lib")) {
    Write-Host ""
    Write-Host "[1/3] Building Skia from source..." -ForegroundColor Yellow

    if (-not (Test-Path "$SKIA_DIR\.git")) {
        Write-Host "  Cloning Skia..." -ForegroundColor Gray
        git clone --no-checkout https://skia.googlesource.com/skia.git $SKIA_DIR
    }

    Push-Location $SKIA_DIR
    git fetch origin $skiaCommit
    git checkout --detach $skiaCommit
    Write-Host "  Syncing Skia dependencies..." -ForegroundColor Gray
    python tools\git-sync-deps

    Write-Host "  Generating build files..." -ForegroundColor Gray
    # Run gn gen inside a VS developer environment
    $gnArgs = 'is_official_build=true is_component_build=false ' +
              'skia_use_system_expat=false skia_use_system_libjpeg_turbo=false ' +
              'skia_use_system_libpng=false skia_use_system_libwebp=false ' +
              'skia_use_system_zlib=false skia_use_system_harfbuzz=false ' +
              'skia_use_system_icu=false skia_enable_pdf=false ' +
              'skia_enable_skottie=false skia_enable_skshaper=false ' +
              # DLL C runtime like Budo (cl.exe defaults to the static /MT).
              'extra_cflags=[\"/MD\"]'

    # gn and ninja need the MSVC environment
    $batContent = @"
@echo off
call "$vcvarsall" x64
cd /d "$SKIA_DIR"
bin\gn gen out\Release --args="$gnArgs"
ninja -C out\Release
"@
    $batFile = "$env:TEMP\build_skia.bat"
    Set-Content -Path $batFile -Value $batContent -Encoding ASCII
    cmd /c $batFile
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Skia build failed." -ForegroundColor Red
        Pop-Location
        exit 1
    }
    Pop-Location

    Write-Host "  Skia built successfully." -ForegroundColor Green
} else {
    Write-Host "[1/3] Skia already built — skipping." -ForegroundColor Green
}

# ── CMake Configure ──────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[2/3] Configuring Budo with CMake..." -ForegroundColor Yellow

$SDL2_DIR = "C:\SDL2"
$sdlMarker = "$SDL2_DIR\.budo-archive-sha256"
if (-not (Test-Path $sdlMarker) -or (Get-Content $sdlMarker -Raw).Trim() -ne $sdlSha256) {
    throw "SDL2 $sdlVersion is missing or does not match the shared dependency lock; rerun Windows provisioning"
}
$OPENSSL_ROOT_DIR = [Environment]::GetEnvironmentVariable("OPENSSL_ROOT_DIR", "Machine")
if (-not $OPENSSL_ROOT_DIR -or -not (Test-Path "$OPENSSL_ROOT_DIR\include\openssl\ssl.h")) {
    throw "OPENSSL_ROOT_DIR is missing or invalid; rerun Windows provisioning"
}
$QuickJsImpl = if ($env:QUICKJS_IMPL) { $env:QUICKJS_IMPL } else { $quickJsDefault }

# Create a bat file that runs cmake inside the VS developer environment
$cmakeConfigContent = @"
@echo off
call "$vcvarsall" x64
cmake -B "$BUILD_DIR" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=cl ^
    -DCMAKE_CXX_COMPILER=cl ^
    -DSKIA_PATH=$SKIA_DIR ^
    -DSDL2_DIR=$SDL2_DIR\cmake ^
    -DOPENSSL_ROOT_DIR="$OPENSSL_ROOT_DIR" ^
    -DENABLE_NEURAL=OFF ^
    -DBUDO_QUICKJS_IMPL=$QuickJsImpl ^
    "$SRC_DIR"
"@
$batFile = "$env:TEMP\cmake_configure.bat"
Set-Content -Path $batFile -Value $cmakeConfigContent -Encoding ASCII
cmd /c $batFile
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: CMake configure failed." -ForegroundColor Red
    exit 1
}
Write-Host "  CMake configure complete." -ForegroundColor Green

# ── CMake Build ──────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[3/3] Building Budo..." -ForegroundColor Yellow

$cmakeBuildContent = @"
@echo off
call "$vcvarsall" x64
cmake --build "$BUILD_DIR" --config Release -j $env:NUMBER_OF_PROCESSORS
"@
$batFile = "$env:TEMP\cmake_build.bat"
Set-Content -Path $batFile -Value $cmakeBuildContent -Encoding ASCII
cmd /c $batFile
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed." -ForegroundColor Red
    exit 1
}

# ── Copy output ──────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "Collecting build artifacts..." -ForegroundColor Yellow

New-Item -ItemType Directory -Path $OUTPUT_DIR -Force | Out-Null

Copy-Item "$BUILD_DIR\budo.exe" "$OUTPUT_DIR\budo.exe" -Force

# Copy SDL2.dll next to the exe (needed at runtime)
if (Test-Path "$SDL2_DIR\lib\x64\SDL2.dll") {
    Copy-Item "$SDL2_DIR\lib\x64\SDL2.dll" "$OUTPUT_DIR\SDL2.dll" -Force
}

# OpenSSL's shared libraries are required by the Windows HTTP/TLS backend.
$opensslDlls = Get-ChildItem "$OPENSSL_ROOT_DIR\bin" -Filter "*.dll" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^(libssl|libcrypto)-.*\.dll$' }
if (-not $opensslDlls) {
    throw "OpenSSL runtime DLLs were not found under $OPENSSL_ROOT_DIR\bin"
}
$opensslDlls | ForEach-Object { Copy-Item $_.FullName "$OUTPUT_DIR\$($_.Name)" -Force }

# Copy wasmtime DLL if present
$wasmtimeDll = Get-ChildItem "$BUILD_DIR" -Filter "wasmtime*.dll" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($wasmtimeDll) {
    Copy-Item $wasmtimeDll.FullName "$OUTPUT_DIR\" -Force
}

Write-Host ""
Write-Host "=== Build complete ===" -ForegroundColor Green
Write-Host "Output: $OUTPUT_DIR\budo.exe"
Write-Host ""
