# build-sqlitevec-compile.ps1
# Internal helper: compiles sqlite-vec.c into vec0.dll using cl.exe.
# Called by build-sqlitevec.ps1 for both "build" (GitHub clone) and
# "local" (local checkout) methods.
#
# The sqlite-vec source tree has sqlite-vec.c and sqlite-vec.h at its root
# (amalgamation).  A vendor/sqlite3.h is needed for the SQLITE_CORE build.

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$SrcDir,
    [Parameter(Mandatory=$true)]
    [string]$DestDll,
    [Parameter(Mandatory=$true)]
    [string]$RepoRoot
)

$ErrorActionPreference = "Stop"

# Locate sqlite3.h — the repo has the amalgamation at external/sqlite3/sqlite3.h
$sqlite3H = Join-Path $RepoRoot "external\sqlite3\sqlite3.h"
if (-not (Test-Path $sqlite3H)) {
    throw "sqlite3.h not found at $sqlite3H"
}

# Ensure vendor/ dir exists with sqlite3.h (sqlite-vec.c does #include "sqlite3.h")
$vendorDir = Join-Path $SrcDir "vendor"
New-Item -ItemType Directory -Force -Path $vendorDir | Out-Null
Copy-Item -Force $sqlite3H (Join-Path $vendorDir "sqlite3.h")

# cl.exe must be on PATH (VS Developer Prompt)
$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $cl) {
    throw "cl.exe not on PATH — run this from a VS Developer Prompt"
}

$vec0C = Join-Path $SrcDir "sqlite-vec.c"
if (-not (Test-Path $vec0C)) {
    throw "sqlite-vec.c not found at $vec0C"
}

Push-Location $SrcDir
try {
    Write-Host "   cl.exe /LD /O2 /DSQLITE_CORE sqlite-vec.c -> $DestDll"
    & cl.exe /nologo /LD /O2 /DSQLITE_CORE /Ivendor /I. sqlite-vec.c /Fe:$DestDll /link /DLL
    if ($LASTEXITCODE -ne 0) { throw "cl.exe build failed" }
}
finally { Pop-Location }
