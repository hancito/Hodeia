# clean-app.ps1
# Removes the Hodeia app build directory (CMake cache + objects).
# Does NOT touch dependencies.
#
# Usage (from the repo root):
#   .\clean-app.ps1

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot ".")).Path

$buildDir = Join-Path $repoRoot "build-windows"
if (Test-Path $buildDir) {
    Remove-Item $buildDir -Recurse -Force
    Write-Host "Removed build-windows/"
} else {
    Write-Host "build-windows/ not found - nothing to clean"
}

Write-Host "Run .\build-app.ps1 to rebuild."
