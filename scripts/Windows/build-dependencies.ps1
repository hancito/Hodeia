# build-dependencies.ps1
# Builds and stages all external dependencies for the Hodeia app:
#   - llama.cpp  (cpu / vulkan / cuda backends, built from source)
#   - sqlite-vec (vec0.dll loadable extension, compiled with cl.exe)
#   - vcpkg packages (MuPDF, Tesseract, Leptonica, Vulkan, etc.)
#
# Does NOT configure or build the Hodeia app itself.
# Run build-app.ps1 afterwards to build the application.
#
# Usage (from the repo root, in a VS Developer PowerShell):
#   .\build-dependencies.ps1
#   .\build-dependencies.ps1 -LlamaBackends cpu,cuda   # skip Vulkan
#   .\build-dependencies.ps1 -VcpkgRoot "C:\src\vcpkg"

[CmdletBinding()]
param(
    [string]$VcpkgRoot        = "C:\vcpkg",
    [string]$LlamaSourceDir   = "D:\Developpement\Dependencies\llama.cpp",
    [string[]]$LlamaBackends  = @("cpu","vulkan","cuda"),
    [string]$SqliteVecSourceDir = "D:\Developpement\Dependencies\sqlite-vec",
    [string]$MupdfSourceDir   = "D:\Developpement\Dependencies\mupdf",
    [string]$CudaToolkit      = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4",
    [string]$VulkanSdk        = "C:\VulkanSDK\1.4.363.0"
)

$ErrorActionPreference = "Stop"
# This script lives in <repo>/scripts/Windows, so the repo root is two levels up.
$scriptDir = $PSScriptRoot
$repoRoot  = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

# --- Locate vcpkg -----------------------------------------------------------
if (-not (Test-Path (Join-Path $VcpkgRoot "vcpkg.exe"))) {
    $vcpkgExe = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($vcpkgExe) { $VcpkgRoot = Split-Path (Split-Path $vcpkgExe.Source) }
}
if (-not (Test-Path (Join-Path $VcpkgRoot "vcpkg.exe"))) {
    throw "vcpkg not found. Clone it into $VcpkgRoot or pass -VcpkgRoot."
}

# --- Build llama.cpp from source -------------------------------------------
if (-not $LlamaSourceDir) { throw "-LlamaSourceDir not given." }
Write-Host ">> llama.cpp ($($LlamaBackends -join ', '))"
& (Join-Path $scriptDir "build-llama.ps1") `
    -SourceDir $LlamaSourceDir `
    -Backends $LlamaBackends `
    -CudaToolkit $CudaToolkit `
    -VulkanSdk $VulkanSdk

# --- Build sqlite-vec from source -------------------------------------------
if (-not $SqliteVecSourceDir) { throw "-SqliteVecSourceDir not given." }
Write-Host ">> sqlite-vec"
& (Join-Path $scriptDir "build-sqlitevec.ps1") -SourceDir $SqliteVecSourceDir

# --- vcpkg install (manifest mode, driven by vcpkg.json) -------------------
# The overlay port vcpkg-overlay/ports/libmupdf builds MuPDF 1.28.6 from
# $MupdfSourceDir instead of the stock vcpkg port (which pins 1.26.10).
# vcpkg cannot see that external tree, so update-mupdf-stamp.ps1 writes its
# git state into the port directory, which IS part of the package ABI.
Write-Host ">> vcpkg install (overlay: libmupdf from $MupdfSourceDir)"
# Fold the MuPDF checkout state into the port ABI so source changes trigger a rebuild.
& (Join-Path $scriptDir "update-mupdf-stamp.ps1") -MupdfSourceDir $MupdfSourceDir
Push-Location $repoRoot
try {
    $vcpkgCmd = "set MUPDF_LOCAL_SOURCE=$MupdfSourceDir && `"$VcpkgRoot\vcpkg.exe`" install --triplet x64-windows --overlay-ports `"$repoRoot\vcpkg-overlay\ports`""
    & cmd /c $vcpkgCmd
    if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed" }
}
finally {
    Pop-Location
}

Write-Host ""
Write-Host "All dependencies staged. Run .\build-app.ps1 to build Hodeia."
