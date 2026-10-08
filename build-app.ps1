# build-app.ps1
# Configures and builds the Hodeia application using Ninja + MSVC.
# Assumes dependencies are already staged (run build-dependencies.ps1 first).
#
# Usage (from the repo root, in a VS Developer PowerShell):
#   .\build-app.ps1
#   .\build-app.ps1 -Configuration Debug

[CmdletBinding()]
param(
    [ValidateSet("Release","Debug","RelWithDebInfo")]
    [string]$Configuration = "Release",
    [string]$BuildDir,
    [string]$QtPrefix   = "C:\Qt\6.12.0\msvc2022_64",
    [string]$VcpkgRoot  = "C:\vcpkg"
)

$ErrorActionPreference = "Continue"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot ".")).Path

if (-not $BuildDir) {
    $BuildDir = Join-Path $repoRoot "build-windows"
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

# --- Ensure MSVC environment ------------------------------------------------
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vcvarsAll = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
    if (-not (Test-Path $vcvarsAll)) { throw "vcvarsall.bat not found" }
    Write-Host ">> Sourcing MSVC environment (vcvarsall x64)..."
    $tmpBat = Join-Path ([System.IO.Path]::GetTempPath()) "vsenv-$([guid]::NewGuid().ToString('N')).bat"
    @"
@echo off
call "$vcvarsAll" x64
set
"@ | Set-Content -Path $tmpBat -Encoding ASCII
    $envLines = & cmd /c "`"$tmpBat`""
    Remove-Item $tmpBat -ErrorAction SilentlyContinue
    foreach ($line in $envLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
        }
    }
    if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
        throw "cl.exe still not found after sourcing vcvarsall"
    }
}

# --- Locate cmake -----------------------------------------------------------
$cmakeExe = "cmake"
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    $vsCmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $vsCmake) { $cmakeExe = $vsCmake }
}
if (-not (Get-Command $cmakeExe -ErrorAction SilentlyContinue)) { throw "cmake not found" }

# --- Locate vcpkg toolchain --------------------------------------------------
$toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $toolchain)) { throw "vcpkg toolchain not found at $toolchain" }

# --- Configure ---------------------------------------------------------------
$overlayPorts = Join-Path $repoRoot "vcpkg-overlay\ports"
$cmakeArgs = @(
    "-S", $repoRoot,
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DCMAKE_PREFIX_PATH=$QtPrefix",
    "-DVCPKG_OVERLAY_PORTS=$overlayPorts",
    "-DMUPDF_LOCAL_SOURCE=D:/Developpement/Dependencies/mupdf"
)

Write-Host ">> cmake configure"
& $cmakeExe @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

# --- Build -------------------------------------------------------------------
$jobs = (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty NumberOfLogicalProcessors)
Write-Host ">> ninja build ($Configuration, -j$jobs)"
& $cmakeExe --build $BuildDir --config $Configuration -j $jobs
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# --- Locate output -----------------------------------------------------------
$exe = Join-Path $BuildDir "src\Hodeia.exe"
if (-not (Test-Path $exe)) {
    $exe = (Get-ChildItem $BuildDir -Recurse -Filter "Hodeia.exe" | Select-Object -First 1).FullName
}

Write-Host ""
Write-Host "Build complete: $exe"
