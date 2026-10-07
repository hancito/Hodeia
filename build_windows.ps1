# build_windows.ps1
# Native Windows (Visual Studio / MSVC) build entry point.
#
# Prerequisites (one-time, on the Windows machine):
#   1. Visual Studio 2022 with "Desktop development with C++" + "Ninja".
#   2. Qt 6 (>= 6.10) — the MSVC 2019/2022 64-bit build, from the Qt installer.
#   3. vcpkg — a git clone at <repo>\vcpkg (or on PATH).
#   4. Local source checkouts:
#        -LlamaSourceDir    → llama.cpp source (built from source, 3 backends)
#        -MupdfSourceDir    → MuPDF 1.28.5 source (vcpkg overlay port)
#        -SqliteVecSourceDir→ sqlite-vec source (compiled with cl.exe)
#      For CUDA: CUDA toolkit installed. For Vulkan: Vulkan SDK installed.
#
# Usage (from the repo root, in a VS Developer PowerShell):
#   .\build_windows.ps1 -LlamaSourceDir "D:\Developpement\Dependencies\llama.cpp" `
#                       -MupdfSourceDir "D:\Developpement\Dependencies\mupdf" `
#                       -SqliteVecMethod local -SqliteVecSourceDir "D:\Developpement\Dependencies\sqlite-vec"
#   .\build_windows.ps1 -Configuration Debug
#   .\build_windows.ps1 -QtPrefix "C:\Qt\6.8.0\msvc2022_64"
#   .\build_windows.ps1 -VcpkgRoot "C:\src\vcpkg"
#   .\build_windows.ps1 -LlamaBackends cpu,vulkan         # skip CUDA

[CmdletBinding()]
param(
    [ValidateSet("Release","Debug","RelWithDebInfo")]
    [string]$Configuration = "Release",
    [string]$QtPrefix,          # e.g. C:\Qt\6.8.0\msvc2022_64
    [string]$VcpkgRoot,         # default: <repo>\vcpkg
    [string]$LlamaSourceDir,    # local llama.cpp source checkout (e.g. D:\Developpement\Dependencies\llama.cpp)
    [string[]]$LlamaBackends = @("cpu","vulkan","cuda"),
    [ValidateSet("download","build","local")]
    [string]$SqliteVecMethod = "download",   # how to obtain vec0.dll if missing
    [string]$SqliteVecSourceDir,             # local sqlite-vec checkout (for -SqliteVecMethod local)
    [string]$MupdfSourceDir,                 # local MuPDF 1.28.5 checkout (for the overlay port)
    [string]$CudaToolkit,                    # optional, e.g. C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4
    [string]$VulkanSdk                       # optional, e.g. C:\VulkanSDK\1.3.296.0
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

# --- Build llama.cpp from source -------------------------------------------
if (-not (Test-Path (Join-Path $repoRoot "external\lib\windows\llamacpp\llama.dll"))) {
    if (-not $LlamaSourceDir) {
        throw "llama.dll missing and -LlamaSourceDir not given."
    }
    Write-Host ">> Building llama.cpp from $LlamaSourceDir (backends: $($LlamaBackends -join ', '))"
    $llamaArgs = @("-SourceDir", $LlamaSourceDir, "-Backends", $LlamaBackends)
    if ($CudaToolkit) { $llamaArgs += @("-CudaToolkit", $CudaToolkit) }
    if ($VulkanSdk)   { $llamaArgs += @("-VulkanSdk",   $VulkanSdk)   }
    & (Join-Path $PSScriptRoot "scripts\build-llama.ps1") @llamaArgs
} else {
    Write-Host ">> llama.cpp DLLs already staged — skipping"
}

# --- Build sqlite-vec from source -------------------------------------------
if (-not (Test-Path (Join-Path $repoRoot "external\lib\windows\sqlitevec\vec0.dll"))) {
    if ($SqliteVecMethod -eq "local") {
        if (-not $SqliteVecSourceDir) {
            throw "-SqliteVecMethod local requires -SqliteVecSourceDir"
        }
        Write-Host ">> Building sqlite-vec from $SqliteVecSourceDir"
        & (Join-Path $PSScriptRoot "scripts\build-sqlitevec.ps1") -Method local -SourceDir $SqliteVecSourceDir
    } else {
        Write-Host ">> Providing sqlite-vec vec0.dll (method: $SqliteVecMethod)"
        & (Join-Path $PSScriptRoot "scripts\build-sqlitevec.ps1") -Method $SqliteVecMethod
    }
} else {
    Write-Host ">> vec0.dll already present — skipping"
}

# --- vcpkg install (manifest mode, driven by vcpkg.json) -------------------
# The overlay port vcpkg-overlay/ports/libmupdf builds MuPDF 1.28.5 from
# $MupdfSourceDir instead of the stock vcpkg port (which pins 1.26.10).
if (-not $MupdfSourceDir) {
    throw "Pass -MupdfSourceDir pointing at your local MuPDF 1.28.5 checkout, e.g. D:\Developpement\Dependencies\mupdf"
}

Write-Host ">> vcpkg install (x64-windows, overlay: libmupdf from $MupdfSourceDir)"
$env:MUPDF_LOCAL_SOURCE = $MupdfSourceDir
Push-Location $repoRoot
try {
    & (Join-Path $VcpkgRoot "vcpkg.exe") install --triplet x64-windows `
        --overlay-ports (Join-Path $repoRoot "vcpkg-overlay\ports")
    if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed" }
}
finally {
    Remove-Item Env:MUPDF_LOCAL_SOURCE -ErrorAction SilentlyContinue
    Pop-Location
}

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
