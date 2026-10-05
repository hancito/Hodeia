# build-sqlitevec.ps1
# Provides the Windows sqlite-vec loadable extension (vec0.dll) that
# SqliteDb::open() loads at runtime.  Primary path: download the official
# pre-built MSVC DLL from the sqlite-vec GitHub release (exact version match,
# no build step).  Fallback: build from source with cl.exe (needs the
# "Desktop development with C++" workload + make).
#
# Usage (from the repo root, in PowerShell):
#   .\scripts\build-sqlitevec.ps1                          # download (default)
#   .\scripts\build-sqlitevec.ps1 -Method build            # compile from source
#   .\scripts\build-sqlitevec.ps1 -SkipIfPresent           # no-op if vec0.dll exists
#
# The pinned version must match external/include/SqliteVec/sqlite-vec.h
# (SQLITE_VEC_VERSION "v0.1.10-alpha.4").  Bump both together.

[CmdletBinding()]
param(
    [ValidateSet("download","build")]
    [string]$Method = "download",
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
        Write-Host ">> Building sqlite-vec $vecVersion from source (cl.exe)"
        $srcRoot = Join-Path ([System.IO.Path]::GetTempPath()) "sqlitevec-src-$([guid]::NewGuid().ToString('N'))"
        New-Item -ItemType Directory -Force -Path $srcRoot | Out-Null
        try {
            git clone --depth 1 --branch $vecVersion https://github.com/asg017/sqlite-vec.git $srcRoot
            if ($LASTEXITCODE -ne 0) { throw "git clone failed" }

            # The Makefile compiles a single C file into the shared extension.
            # It assumes a `vendor/` dir with sqlite3.h; we point it at the
            # amalgamation already in this repo (external/sqlite3/sqlite3.h).
            New-Item -ItemType Directory -Force -Path (Join-Path $srcRoot "vendor") | Out-Null
            Copy-Item -Force (Join-Path $repoRoot "external\sqlite3\sqlite3.h") (Join-Path $srcRoot "vendor\sqlite3.h")

            # The Makefile targets gcc; on MSVC we compile directly with cl.exe.
            # /LD = shared DLL, /O2 = optimize, exports are C (undecorated).
            $cl = Get-Command cl.exe -ErrorAction SilentlyContinue
            if (-not $cl) { throw "cl.exe not on PATH — run this from a VS Developer Prompt" }
            Push-Location $srcRoot
            try {
                & cl.exe /nologo /LD /O2 /DSQLITE_CORE /Ivendor /I. sqlite-vec.c /Fe:$destDll /link /DLL
                if ($LASTEXITCODE -ne 0) { throw "cl.exe build failed" }
            }
            finally { Pop-Location }
        }
        finally {
            Remove-Item -Recurse -Force $srcRoot -ErrorAction SilentlyContinue
        }
    }
}

if (-not (Test-Path $destDll)) { throw "vec0.dll was not produced at $destDll" }
Write-Host "Done. vec0.dll ready at $destDll"
