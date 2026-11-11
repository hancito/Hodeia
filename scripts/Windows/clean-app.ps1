# clean-app.ps1
# Removes the Hodeia app build directory (CMake cache + objects).
# Does NOT touch dependencies.
#
# Usage (from the repo root):
#   .\clean-app.ps1

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
# This script lives in <repo>/scripts/Windows, so the repo root is two levels up.
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

$buildDir = Join-Path $repoRoot "build"
if (Test-Path $buildDir) {
    Remove-Item $buildDir -Recurse -Force
    Write-Host "Removed build/"
} else {
    Write-Host "build/ not found - nothing to clean"
}

Write-Host "Run .\build-app.ps1 to rebuild."
