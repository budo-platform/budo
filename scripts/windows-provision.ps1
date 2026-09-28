# Windows VM provisioning script for Budo.
# Runs once during `vagrant up` to install all build dependencies.
#
# Requires an administrator-provisioned Chocolatey installation. Installs Git,
# Python, CMake, Ninja, MSVC, OpenSSL, pinned SDL2, and OpenSSH server.

$ErrorActionPreference = "Stop"

Write-Host "=== Budo Windows Provisioning ===" -ForegroundColor Cyan

# ── Trusted package-manager prerequisite ─────────────────────────────────────

if (-not (Get-Command choco -ErrorAction SilentlyContinue)) {
    throw "Chocolatey is required but was not found. Preinstall it in the Windows box from a trusted, reviewed image; provisioning will not download and execute a mutable bootstrap script."
} else {
    Write-Host "[1/7] Trusted Chocolatey prerequisite found." -ForegroundColor Green
}

# ── Core tools ────────────────────────────────────────────────────────────────

Write-Host "[2/7] Installing Git, Python 3, CMake, Ninja, and OpenSSL..." -ForegroundColor Yellow
choco install -y git python3 cmake ninja openssl --installargs 'ADD_CMAKE_TO_PATH=System'

# Refresh PATH after installs
$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
            [System.Environment]::GetEnvironmentVariable("Path", "User")

# ── Visual Studio 2022 Build Tools ───────────────────────────────────────────

Write-Host "[3/7] Installing Visual Studio 2022 Build Tools (MSVC + Windows SDK)..." -ForegroundColor Yellow
choco install -y visualstudio2022buildtools --package-parameters `
    "--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --includeRecommended"

# ── SDL2 development libraries ────────────────────────────────────────────────

$lockPath = Join-Path $PSScriptRoot "..\cmake\BudoDependencyLock.cmake"
$dependencyLock = Get-Content $lockPath -Raw
$SDL2_VERSION = [regex]::Match($dependencyLock, 'set\(BUDO_SDL2_WINDOWS_VERSION "([^"]+)"\)').Groups[1].Value
$sdl2Url = [regex]::Match($dependencyLock, 'set\(BUDO_SDL2_WINDOWS_URL\s+"([^"]+)"\)').Groups[1].Value
$sdl2Sha256 = [regex]::Match($dependencyLock, 'set\(BUDO_SDL2_WINDOWS_URL_HASH\s+"SHA256=([0-9a-f]{64})"\)').Groups[1].Value
if (-not $SDL2_VERSION -or -not $sdl2Url -or -not $sdl2Sha256) {
    throw "Cannot read the pinned SDL2 Windows identity from the shared dependency lock"
}
$SDL2_DIR     = "C:\SDL2"
$sdlMarker = "$SDL2_DIR\.budo-archive-sha256"
$sdlInstalledHash = if (Test-Path $sdlMarker) { (Get-Content $sdlMarker -Raw).Trim() } else { "" }

if (-not (Test-Path "$SDL2_DIR\include\SDL.h") -or $sdlInstalledHash -ne $sdl2Sha256) {
    Write-Host "[4/7] Downloading pinned SDL2 $SDL2_VERSION development libraries..." -ForegroundColor Yellow
    $sdl2Zip  = "C:\sdl2-dev.zip"

    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $sdl2Url -OutFile $sdl2Zip
    $actualSdlSha256 = (Get-FileHash -Algorithm SHA256 $sdl2Zip).Hash.ToLowerInvariant()
    if ($actualSdlSha256 -ne $sdl2Sha256) {
        Remove-Item $sdl2Zip -Force -ErrorAction SilentlyContinue
        throw "SDL2 archive SHA-256 mismatch (expected $sdl2Sha256, got $actualSdlSha256)"
    }
    Remove-Item $SDL2_DIR -Recurse -Force -ErrorAction SilentlyContinue
    Expand-Archive -Path $sdl2Zip -DestinationPath "C:\" -Force
    if (Test-Path "C:\SDL2-$SDL2_VERSION") {
        Rename-Item "C:\SDL2-$SDL2_VERSION" $SDL2_DIR -ErrorAction SilentlyContinue
    }
    Set-Content -Path $sdlMarker -Value $sdl2Sha256 -Encoding ASCII
    Remove-Item $sdl2Zip -Force -ErrorAction SilentlyContinue

    # Persist for CMake's find_package(SDL2)
    [Environment]::SetEnvironmentVariable("SDL2DIR", $SDL2_DIR, "Machine")
    [Environment]::SetEnvironmentVariable("SDL2_DIR", "$SDL2_DIR\cmake", "Machine")
} else {
    Write-Host "[4/7] Verified pinned SDL2 already installed." -ForegroundColor Green
}

# Locate the Chocolatey OpenSSL package and persist the CMake hint.
$opensslRoots = @("C:\Program Files\OpenSSL-Win64", "C:\Program Files\OpenSSL", "C:\tools\openssl")
$OPENSSL_ROOT_DIR = $opensslRoots | Where-Object { Test-Path "$_\include\openssl\ssl.h" } | Select-Object -First 1
if (-not $OPENSSL_ROOT_DIR) {
    throw "OpenSSL headers were not found after provisioning; CMake requires OPENSSL_ROOT_DIR with include\openssl\ssl.h and x64 libraries"
}
[Environment]::SetEnvironmentVariable("OPENSSL_ROOT_DIR", $OPENSSL_ROOT_DIR, "Machine")
Write-Host "[5/7] OpenSSL configured at $OPENSSL_ROOT_DIR" -ForegroundColor Green

# ── OpenSSH server (for scp file transfer) ────────────────────────────────────

Write-Host "[6/7] Enabling OpenSSH server..." -ForegroundColor Yellow

$sshCapability = Get-WindowsCapability -Online | Where-Object Name -like 'OpenSSH.Server*'
if ($sshCapability.State -ne 'Installed') {
    Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
}
Start-Service sshd
Set-Service  -Name sshd -StartupType 'Automatic'

# Allow the vagrant user to log in via SSH with the insecure key
$sshDir  = "C:\Users\vagrant\.ssh"
$authKeys = "$sshDir\authorized_keys"
if (-not (Test-Path $sshDir))  { New-Item -ItemType Directory -Path $sshDir -Force | Out-Null }
if (-not (Test-Path $authKeys)) {
    # Vagrant insecure public key — replaced by vagrant on first `vagrant ssh`
    $vagrantPubKey = "ssh-rsa AAAAB3NzaC1yc2EAAAABIwAAAQEA6NF8iallvQVp22WDkTkyrtvp9eWW6A8YVr+kz4TjGYe7gHzIw+niNltGEFHzD8+v1I2YJ6oXevct1YeS0o9HZyN1Q9qgCgzUFtdOKLv6IedplJWTNSLQlbU3F0PQIigrAS+H08lpkkDo7gmFNXlKHkY4jH8mLCrjAdMbJJ0pPt+A3R8cD2w2pIDSSIA2F2v+DjMwk3j5IXHM/YB3NeSj3i58+Op7e+8TkP2JFrU+FGCTRYlXEzrGllJSG5Sbs54aHOQ/SPtqKBmgHTJMrmQ8tnEMbF+oiGRAvEI8JkGPOSOwRbyGVFKmelxoG/AIF7HJT0FBBIo= vagrant insecure public key"
    Set-Content -Path $authKeys -Value $vagrantPubKey -Force
}

# Fix permissions (administrators_authorized_keys quirk on Windows OpenSSH)
$adminKeys = "C:\ProgramData\ssh\administrators_authorized_keys"
if (Test-Path $adminKeys) {
    # Ensure the vagrant key is also in the admin file (vagrant user is admin)
    $existing = Get-Content $adminKeys -ErrorAction SilentlyContinue
    $vagrantPubKey = Get-Content $authKeys
    if ($existing -notcontains $vagrantPubKey) {
        Add-Content -Path $adminKeys -Value $vagrantPubKey
    }
}

# ── Done ──────────────────────────────────────────────────────────────────────

Write-Host "[7/7] Refreshing system PATH..." -ForegroundColor Yellow
$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
            [System.Environment]::GetEnvironmentVariable("Path", "User")

Write-Host ""
Write-Host "=== Provisioning complete ===" -ForegroundColor Green
Write-Host "Installed: Git, Python 3, CMake, Ninja, MSVC 2022, OpenSSL, verified SDL2 $SDL2_VERSION, OpenSSH"
Write-Host ""
