# package-app.ps1
# Builds the Windows installer (dist\Hodeia-Setup-<version>-x64.exe) from the
# deployed Release build with Inno Setup (package\windows\hodeia.iss).
#
# Usage (from the repo root):
#   .\scripts\Windows\package-app.ps1            # package existing build\Release
#   .\scripts\Windows\package-app.ps1 -Build     # build Release first
#
# Requires Inno Setup 6.5.2+ or 7:  winget install JRSoftware.InnoSetup

[CmdletBinding()]
param(
    [switch]$Build,
    [string]$BuildDir
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $BuildDir) { $BuildDir = Join-Path $repoRoot "build\Release" }

# --- Optional Release build ----------------------------------------------------
if ($Build) {
    & (Join-Path $PSScriptRoot "build-app.ps1") -Configuration Release -BuildDir $BuildDir
    if ($LASTEXITCODE -ne 0) { throw "Release build failed" }
}
if (-not (Test-Path (Join-Path $BuildDir "Hodeia.exe"))) {
    throw "Hodeia.exe not found in $BuildDir. Build Release first (or pass -Build)."
}

# Only package a Release build: a Debug exe needs the debug CRT/Qt DLLs,
# which are not redistributable.
$cache = Join-Path $BuildDir "CMakeCache.txt"
if ((Test-Path $cache) -and -not (Select-String -Path $cache -Pattern '^CMAKE_BUILD_TYPE:STRING=Release$' -Quiet)) {
    throw "$BuildDir is not a Release build."
}

# --- Locate the Inno Setup compiler -------------------------------------------
$iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    $candidates = foreach ($ver in "7", "6") {
        "${env:ProgramFiles(x86)}\Inno Setup $ver\ISCC.exe"
        "$env:ProgramFiles\Inno Setup $ver\ISCC.exe"
        "$env:LOCALAPPDATA\Programs\Inno Setup $ver\ISCC.exe"
    }
    $iscc = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $iscc) { throw "Inno Setup (ISCC.exe) not found. Install it: winget install JRSoftware.InnoSetup" }

# --- Version from CMakeLists.txt project(... VERSION x.y.z ...) ----------------
$cmakeLists = Get-Content (Join-Path $repoRoot "CMakeLists.txt") -Raw
if ($cmakeLists -notmatch 'project\s*\(\s*Hodeia\s+VERSION\s+([0-9.]+)') {
    throw "Could not read the project version from CMakeLists.txt"
}
$version = $Matches[1]

# --- Build number of the bundled VC++ redistributable -------------------------
$redist = Join-Path $BuildDir "vc_redist.x64.exe"
if (-not (Test-Path $redist)) { throw "vc_redist.x64.exe not found in $BuildDir (windeployqt stages it)." }
$redistBuild = (Get-Item $redist).VersionInfo.FileBuildPart

# --- Compile -------------------------------------------------------------------
$iss = Join-Path $repoRoot "package\windows\hodeia.iss"
Write-Host ">> Inno Setup: Hodeia $version (VC++ runtime build $redistBuild)"
& $iscc "/DAppVersion=$version" "/DBuildDir=$((Resolve-Path $BuildDir).Path)" "/DVCRedistBuild=$redistBuild" $iss
if ($LASTEXITCODE -ne 0) { throw "ISCC failed" }

# --- GitHub release assets are capped at 2 GiB per file --------------------------
$installer = Join-Path $repoRoot "dist\Hodeia-Setup-$version-x64.exe"
$sizeBytes = (Get-Item $installer).Length
$sizeGiB = [math]::Round($sizeBytes / 1GB, 2)
if ($sizeBytes -ge 2GB) {
    throw "Installer is $sizeGiB GiB: GitHub release assets must be under 2 GiB. Ship a smaller bundled model."
}

Write-Host ""
Write-Host "Installer: $installer ($sizeGiB GiB)"
