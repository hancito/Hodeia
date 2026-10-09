# build-sqlitevec.ps1
# Compiles the Windows sqlite-vec loadable extension (vec0.dll) from a
# local source checkout using cl.exe.
#
# The sqlite-vec source tree has sqlite-vec.c and sqlite-vec.h at its root
# (amalgamation).  sqlite3.h / sqlite3ext.h are copied from the repo's
# external/include/Sqlite3/ into a vendor/ dir for the build.
#
# Usage (from the repo root, in a VS Developer PowerShell):
#   .\scripts\build-sqlitevec.ps1 -SourceDir "D:\Developpement\Dependencies\sqlite-vec"
#   .\scripts\build-sqlitevec.ps1 -SourceDir "..." -SkipIfPresent

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$SourceDir,
    [switch]$SkipIfPresent
)

$ErrorActionPreference = "Stop"

$repoRoot   = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$destDir    = Join-Path $repoRoot "external\lib\windows\sqlitevec"
$destDll    = Join-Path $destDir "vec0.dll"

if ($SkipIfPresent -and (Test-Path $destDll)) {
    Write-Host "vec0.dll already present - skipping."
    return
}

if (-not (Test-Path $SourceDir)) {
    throw "SourceDir not found: $SourceDir"
}

New-Item -ItemType Directory -Force -Path $destDir | Out-Null

# --- Locate sqlite3 headers in the repo ------------------------------------
$sqlite3H    = Join-Path $repoRoot "external\include\Sqlite3\sqlite3.h"
$sqlite3extH = Join-Path $repoRoot "external\include\Sqlite3\sqlite3ext.h"
if (-not (Test-Path $sqlite3H))    { throw "sqlite3.h not found at $sqlite3H" }
if (-not (Test-Path $sqlite3extH)) { throw "sqlite3ext.h not found at $sqlite3extH" }

# --- Copy headers into the source tree's vendor/ dir -------------------------
$vendorDir = Join-Path $SourceDir "vendor"
New-Item -ItemType Directory -Force -Path $vendorDir | Out-Null
Copy-Item -Force $sqlite3H    (Join-Path $vendorDir "sqlite3.h")
Copy-Item -Force $sqlite3extH (Join-Path $vendorDir "sqlite3ext.h")

# --- cl.exe must be on PATH (VS Developer Prompt) ---------------------------
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw "cl.exe not on PATH - run this from a VS Developer Prompt"
}

# --- Verify source file exists ----------------------------------------------
$vec0C = Join-Path $SourceDir "sqlite-vec.c"
if (-not (Test-Path $vec0C)) {
    throw "sqlite-vec.c not found at $vec0C"
}

# --- Compile -----------------------------------------------------------------
Push-Location $SourceDir
try {
    Write-Host ">> Building sqlite-vec from: $SourceDir"
    Write-Host "   cl.exe /LD /O2 sqlite-vec.c -> $destDll"
    & cl.exe /nologo /LD /O2 /Ivendor /I. sqlite-vec.c /Fe:$destDll /link /DLL
    if ($LASTEXITCODE -ne 0) { throw "cl.exe build failed" }
}
finally { Pop-Location }

if (-not (Test-Path $destDll)) { throw "vec0.dll was not produced at $destDll" }

# --- Copy the header to external/include/SqliteVec/ -------------------------
$headerDir = Join-Path $repoRoot "external\include\SqliteVec"
New-Item -ItemType Directory -Force -Path $headerDir | Out-Null
Copy-Item -Force (Join-Path $SourceDir "sqlite-vec.h") (Join-Path $headerDir "sqlite-vec.h")

Write-Host "Done. vec0.dll ready at $destDll"
