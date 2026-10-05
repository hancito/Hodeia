# stage-llama.ps1
# Assembles the merged llama.cpp DLL folder that PlatformWindows.cmake expects:
#   external/lib/windows/llamacpp/   (flat: core + all CPU variants + CUDA + Vulkan)
#
# Usage (from the repo root, in PowerShell):
#   .\scripts\stage-llama.ps1 -SourceDir "D:\Developpement\Windows" [-CudaVersion 12.4]
#
# -SourceDir must be the folder containing: cpu\, vulkan\, cuda\12.4\, cuda\13.4\
# -CudaVersion picks which CUDA backend + runtime to ship (default 12.4).
#
# Run this once on the Windows machine (or copy the resulting llamacpp\ folder
# over from wherever you staged it).  The DLLs are git-ignored, so they are not
# part of the repo — this script materializes them from your downloaded zips.

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$SourceDir,

    [ValidateSet("12.4","13.4")]
    [string]$CudaVersion = "12.4"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $SourceDir)) {
    throw "SourceDir not found: $SourceDir"
}

$cpuDir    = Join-Path $SourceDir "cpu"
$vulkanDir = Join-Path $SourceDir "vulkan"
$cudaDir   = Join-Path $SourceDir "cuda" $CudaVersion

foreach ($d in @($cpuDir,$vulkanDir,$cudaDir)) {
    if (-not (Test-Path $d)) { throw "Missing expected folder: $d" }
}

$dest = Join-Path $PSScriptRoot "..\external\lib\windows\llamacpp"
$dest = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path + "\external\lib\windows\llamacpp"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

function Copy-File { param($src) Copy-Item -Force $src $dest }

Write-Host "Staging llama.cpp from $SourceDir (CUDA $CudaVersion) -> $dest"

# Core DLLs + CPU x64 baseline + libomp (from cpu\)
@("llama.dll","llama-common.dll","mtmd.dll","ggml.dll","ggml-base.dll",
  "ggml-rpc.dll","ggml-cpu-x64.dll","ggml-cpu-sse42.dll","libomp.dll") |
    ForEach-Object { Copy-File (Join-Path $cpuDir $_) }

# All per-microarch CPU variants (ggml loads the best match at runtime)
Get-ChildItem -Path $cpuDir -Filter "ggml-cpu-*.dll" | ForEach-Object { Copy-File $_.FullName }

# Vulkan backend
Copy-File (Join-Path $vulkanDir "ggml-vulkan.dll")

# CUDA backend + its CUDA runtime DLLs
Copy-File (Join-Path $cudaDir "ggml-cuda.dll")
Get-ChildItem -Path $cudaDir -Filter "cudart64_*.dll"  | ForEach-Object { Copy-File $_.FullName }
Get-ChildItem -Path $cudaDir -Filter "cublas64_*.dll"  | ForEach-Object { Copy-File $_.FullName }
Get-ChildItem -Path $cudaDir -Filter "cublasLt64_*.dll"| ForEach-Object { Copy-File $_.FullName }

$count = (Get-ChildItem -Path $dest -Filter "*.dll").Count
Write-Host "Done. $count DLLs staged in $dest"
