# build_windows.ps1
# Native Windows (Visual Studio / MSVC) build entry point.
#
# Prerequisites (one-time, on the Windows machine):
#   1. Visual Studio 2022 with "Desktop development with C++".
#   2. Qt 6 (>= 6.10) — the MSVC 2019/2022 64-bit build, from the Qt installer.
#   3. vcpkg — a git clone at <repo>\vcpkg (or on PATH).
#   4. The pre-built llama.cpp DLLs (cpu\ vulkan\ cuda\12.4\ cuda\13.4\) — this
#      script stages them into external\lib\windows\llamacpp\ (see -LlamaSourceDir).
#
# Auto-provisioned (no manual step needed):
#   - sqlite-vec vec0.dll  → downloaded from the official release by default
#     (scripts\build-sqlitevec.ps1), or built with -SqliteVecMethod build.
#
# Usage (from the repo root, in a developer PowerShell):
#   .\build_windows.ps1 -LlamaSourceDir "D:\Developpement\Windows"
#   .\build_windows.ps1 -Configuration Debug
#   .\build_windows.ps1 -QtPrefix "C:\Qt\6.8.0\msvc2022_64"
#   .\build_windows.ps1 -VcpkgRoot "C:\src\vcpkg"
#   .\build_windows.ps1 -CudaVersion 13.4                 # re-stage llama.cpp
#   .\build_windows.ps1 -SqliteVecMethod build            # compile vec0.dll

[CmdletBinding()]
param(
    [ValidateSet("Release","Debug","RelWithDebInfo")]
    [string]$Configuration = "Release",
    [string]$QtPrefix,          # e.g. C:\Qt\6.8.0\msvc2022_64
    [string]$VcpkgRoot,         # default: <repo>\vcpkg
    [string]$LlamaSourceDir,    # folder with cpu\ vulkan\ cuda\12.4\ cuda\13.4\
    [string]$CudaVersion = "12.4",
    [ValidateSet("download","build")]
    [string]$SqliteVecMethod = "download"   # how to obtain vec0.dll if missing
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot ".")).Path

# --- Locate vcpkg -----------------------------------------------------------
if (-not $VcpkgRoot) { $VcpkgRoot = Join-Path $repoRoot "vcpkg" }
if (-not (Test-Path (Join-Path $VcpkgRoot "vcpkg.exe"))) {
    # Fall back to a global install on PATH.
    $vcpkgExe = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($vcpkgExe) { $VcpkgRoot = Split-Path (Split-Path $vcpkgExe.Source) }
}
if (-not (Test-Path (Join-Path $VcpkgRoot "vcpkg.exe"))) {
    throw "vcpkg not found. Clone it into $VcpkgRoot or pass -VcpkgRoot."
}
$toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"

# --- Locate Qt (MSVC) -------------------------------------------------------
if (-not $QtPrefix) {
    # Try the default Qt install location for a recent MSVC build.
    $candidates = Get-ChildItem -Path "C:\Qt" -Recurse -Depth 3 -Filter "qtcore.dll" -ErrorAction SilentlyContinue |
                  Where-Object { $_.Directory.Name -like "*msvc*" } | Select-Object -First 1
    if ($candidates) { $QtPrefix = (Resolve-Path (Join-Path $candidates.Directory.FullName "..\..")).Path }
}
if (-not $QtPrefix) {
    throw "Qt MSVC prefix not found. Pass -QtPrefix e.g. C:\Qt\6.8.0\msvc2022_64"
}

# --- Stage llama.cpp DLLs ---------------------------------------------------
# The pre-built llama.cpp DLLs are git-ignored; stage them into the tree so
# CMake can link/deploy them.  Skip if already present.
if (-not (Test-Path (Join-Path $repoRoot "external\lib\windows\llamacpp\llama.dll"))) {
    if (-not $LlamaSourceDir) {
        throw "external\lib\windows\llamacpp\llama.dll missing and -LlamaSourceDir not given. Pass the folder containing cpu\ vulkan\ cuda\12.4\ cuda\13.4\"
    }
    Write-Host ">> Staging llama.cpp from $LlamaSourceDir (CUDA $CudaVersion)"
    & (Join-Path $PSScriptRoot "scripts\stage-llama.ps1") -SourceDir $LlamaSourceDir -CudaVersion $CudaVersion
} else {
    Write-Host ">> llama.cpp DLLs already staged — skipping"
}

# --- Provide sqlite-vec (vec0.dll) -----------------------------------------
# SqliteDb::open() loads vec0.dll from the app dir at runtime.  It is
# git-ignored; obtain it (official prebuilt by default) if not already staged.
if (-not (Test-Path (Join-Path $repoRoot "external\lib\windows\sqlitevec\vec0.dll"))) {
    Write-Host ">> Providing sqlite-vec vec0.dll (method: $SqliteVecMethod)"
    & (Join-Path $PSScriptRoot "scripts\build-sqlitevec.ps1") -Method $SqliteVecMethod
} else {
    Write-Host ">> vec0.dll already present — skipping"
}

# --- vcpkg install (manifest mode, driven by vcpkg.json) -------------------
# Manifest mode auto-detects vcpkg.json in the working dir.  We set the
# triplet to x64-windows and let CMake's toolchain file pick up the result.
Write-Host ">> vcpkg install (x64-windows)"
Push-Location $repoRoot
try {
    & (Join-Path $VcpkgRoot "vcpkg.exe") install --triplet x64-windows
    if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed" }
}
finally { Pop-Location }

# --- Configure + build ------------------------------------------------------
$buildDir = Join-Path $repoRoot "build-windows"
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$cmakeArgs = @(
    "-S", $repoRoot,
    "-B", $buildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DCMAKE_PREFIX_PATH=$QtPrefix"
)

Write-Host ">> cmake configure"
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host ">> cmake build ($Configuration)"
& cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host ""
Write-Host "Build complete. Output: $buildDir\src\$Configuration\Hodeia.exe"
Write-Host "The deploy step has copied the backend/OCR/Qt DLLs next to the exe."
