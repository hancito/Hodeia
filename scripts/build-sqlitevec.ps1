# build-sqlitevec.ps1
# Provides the Windows sqlite-vec loadable extension (vec0.dll) that
# SqliteDb::open() loads at runtime.
#
# Methods:
#   local    — compile from a local source checkout (-SourceDir)
#   build    — clone from GitHub and compile with cl.exe
#   download — fetch the official pre-built MSVC DLL (default)
#
# Usage (from the repo root, in a VS Developer PowerShell):
#   .\scripts\build-sqlitevec.ps1 -Method local -SourceDir "D:\Developpement\Dependencies\sqlite-vec"
#   .\scripts\build-sqlitevec.ps1                          # download (default)
#   .\scripts\build-sqlitevec.ps1 -Method build            # clone + compile
#   .\scripts\build-sqlitevec.ps1 -SkipIfPresent           # no-op if vec0.dll exists
#
# The pinned version must match external/include/SqliteVec/sqlite-vec.h
# (SQLITE_VEC_VERSION "v0.1.10-alpha.4").  Bump both together.

[CmdletBinding()]
param(
    [ValidateSet("download","build","local")]
    [string]$Method = "download",
    [string]$SourceDir,    # required for -Method local
    [switch]$SkipIfPresent
)

$ErrorActionPreference = "Stop"

$repoRoot   = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$destDir    = Join-Path $repoRoot "external\lib\windows\sqlitevec"
$destDll    = Join-Path $destDir "vec0.dll"

# Pinned to external/include/SqliteVec/sqlite-vec.h
$vecVersion = "v0.1.10-alpha.4"
# SHA-256 of the official release tarball (verified against checksums.txt).
$expectedTarSha256 = "39d29bd26252b61daf7e1d3d0b538336e5364e1022c61084bba91f6570941017"
$tarUrl = "https://github.com/asg017/sqlite-vec/releases/download/$vecVersion/sqlite-vec-$($vecVersion.TrimStart('v'))-loadable-windows-x86_64.tar.gz"

if ($SkipIfPresent -and (Test-Path $destDll)) {
    Write-Host "vec0.dll already present — skipping."
    return
}

New-Item -ItemType Directory -Force -Path $destDir | Out-Null

function Get-FileHashHex([string]$path) {
    (Get-FileHash -Algorithm SHA256 -Path $path).Hash.ToLowerInvariant()
}

switch ($Method) {
    "download" {
        Write-Host ">> Downloading official sqlite-vec $vecVersion (windows-x86_64)"
        $tmp = Join-Path ([System.IO.Path]::GetTempPath()) "sqlitevec-win-$([guid]::NewGuid().ToString('N'))"
        New-Item -ItemType Directory -Force -Path $tmp | Out-Null
        $tar = Join-Path $tmp "vec0.tar.gz"
        try {
            Invoke-WebRequest -Uri $tarUrl -OutFile $tar -UseBasicParsing
            $actual = (Get-FileHash -Algorithm SHA256 -Path $tar).Hash.ToLowerInvariant()
            if ($actual -ne $expectedTarSha256) {
                throw "Checksum mismatch.`n  expected $expectedTarSha256`n  got      $actual"
            }
            Write-Host "   checksum OK"
            tar -xzf $tar -C $tmp
            $dll = Get-ChildItem -Path $tmp -Filter "vec0.dll" -Recurse | Select-Object -First 1
            if (-not $dll) { throw "vec0.dll not found in the downloaded archive" }
            Copy-Item -Force $dll.FullName $destDll
        }
        finally {
            Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
        }
    }
    "build" {
        Write-Host ">> Building sqlite-vec $vecVersion from GitHub (cl.exe)"
        $srcRoot = Join-Path ([System.IO.Path]::GetTempPath()) "sqlitevec-src-$([guid]::NewGuid().ToString('N'))"
        New-Item -ItemType Directory -Force -Path $srcRoot | Out-Null
        try {
            git clone --depth 1 --branch $vecVersion https://github.com/asg017/sqlite-vec.git $srcRoot
            if ($LASTEXITCODE -ne 0) { throw "git clone failed" }
            & (Join-Path $PSScriptRoot "..\scripts\build-sqlitevec-compile.ps1") -SrcDir $srcRoot -DestDll $destDll -RepoRoot $repoRoot
        }
        finally {
            Remove-Item -Recurse -Force $srcRoot -ErrorAction SilentlyContinue
        }
    }
    "local" {
        if (-not $SourceDir) { throw "-Method local requires -SourceDir" }
        if (-not (Test-Path $SourceDir)) { throw "SourceDir not found: $SourceDir" }
        Write-Host ">> Building sqlite-vec from local source: $SourceDir"
        & (Join-Path $PSScriptRoot "..\scripts\build-sqlitevec-compile.ps1") -SrcDir $SourceDir -DestDll $destDll -RepoRoot $repoRoot
    }
}

if (-not (Test-Path $destDll)) { throw "vec0.dll was not produced at $destDll" }

# --- Copy the header to external/include/SqliteVec/ -------------------------
$headerDir = Join-Path $repoRoot "external\include\SqliteVec"
New-Item -ItemType Directory -Force -Path $headerDir | Out-Null
# Locate sqlite-vec.h — check the source dir (for local/build methods)
$srcHeader = $null
if ($Method -eq "local" -and $SourceDir) {
    if (Test-Path (Join-Path $SourceDir "sqlite-vec.h")) {
        $srcHeader = Join-Path $SourceDir "sqlite-vec.h"
    }
}
if (-not $srcHeader -and (Test-Path (Join-Path $destDir "..\..\include\SqliteVec\sqlite-vec.h"))) {
    # Already present — skip
} elseif ($srcHeader) {
    Copy-Item -Force $srcHeader (Join-Path $headerDir "sqlite-vec.h")
}

Write-Host "Done. vec0.dll ready at $destDll"
