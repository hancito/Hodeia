# clean-deps.ps1
# Removes all built/staged dependencies for a full rebuild.
#
# Usage (from the repo root):
#   .\clean-deps.ps1
#   .\clean-deps.ps1 -KeepVcpkg      # keep vcpkg binary cache
#   .\clean-deps.ps1 -KeepLlama      # keep staged llama.cpp DLLs
#   .\clean-deps.ps1 -KeepSqliteVec  # keep vec0.dll
#   .\clean-deps.ps1 -KeepBuild      # keep the build/ dir

[CmdletBinding()]
param(
    [switch]$KeepVcpkg,
    [switch]$KeepLlama,
    [switch]$KeepSqliteVec,
    [switch]$KeepBuild
)

$ErrorActionPreference = "Stop"
# This script lives in <repo>/scripts/Windows, so the repo root is two levels up.
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Remove-Path([string]$label, [string]$path, [switch]$keep) {
    if ($keep) {
        Write-Host "  [skip] $label (kept)"
        return
    }
    if (Test-Path $path) {
        Remove-Item $path -Recurse -Force
        Write-Host "  [rm]   $label"
    } else {
        Write-Host "  [none] $label"
    }
}

Write-Host ">> Cleaning dependencies"

# --- llama.cpp staged DLLs ---------------------------------------------------
Remove-Path "external/lib/windows/llamacpp" `
    (Join-Path $repoRoot "external\lib\windows\llamacpp") -keep:$KeepLlama

# --- sqlite-vec vec0.dll -----------------------------------------------------
Remove-Path "external/lib/windows/sqlitevec" `
    (Join-Path $repoRoot "external\lib\windows\sqlitevec") -keep:$KeepSqliteVec

# --- MuPDF static libs (if built standalone into external/lib/windows/) ------
Remove-Path "external/lib/windows/libmupdf.lib" `
    (Join-Path $repoRoot "external\lib\windows\libmupdf.lib")
Remove-Path "external/lib/windows/libmuthreads.lib" `
    (Join-Path $repoRoot "external\lib\windows\libmuthreads.lib")
Remove-Path "external/lib/windows/libthirdparty.lib" `
    (Join-Path $repoRoot "external\lib\windows\libthirdparty.lib")

# --- vcpkg project-local install ---------------------------------------------
Remove-Path "vcpkg_installed" `
    (Join-Path $repoRoot "vcpkg_installed") -keep:$KeepVcpkg

# --- vcpkg buildtrees (cache of compiled packages) ---------------------------
$vcpkgRoot = "C:\vcpkg"
if (Test-Path $vcpkgRoot) {
    Remove-Path "vcpkg buildtrees" (Join-Path $vcpkgRoot "buildtrees") -keep:$KeepVcpkg
}

# --- build directories ---------------------------------------------------------
# build/ holds build/Release and build/Debug (same layout as Linux).
Remove-Path "build" `
    (Join-Path $repoRoot "build") -keep:$KeepBuild
# Legacy build dirs from older layouts.
Remove-Path "build-windows" `
    (Join-Path $repoRoot "build-windows") -keep:$KeepBuild
Remove-Path "build-debug" `
    (Join-Path $repoRoot "build-debug") -keep:$KeepBuild

# --- llama.cpp temp build dirs in %TEMP% ---------------------------------------
$tempLlama = Get-ChildItem ([System.IO.Path]::GetTempPath()) -Directory -Filter "llama-build-*" -ErrorAction SilentlyContinue
foreach ($d in $tempLlama) {
    Remove-Item $d.FullName -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "  [rm]   $($d.Name) (temp)"
}

Write-Host ""
Write-Host "Done. Run .\build-dependencies.ps1 to rebuild everything."
