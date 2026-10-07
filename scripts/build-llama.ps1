# build-llama.ps1
# Builds llama.cpp from a local source checkout (MSVC / Ninja) and stages
# the resulting DLLs into external\lib\windows\llamacpp\ in the flat layout
# that PlatformWindows.cmake expects.
#
# Three backend variants are built (same as Linux):
#   cpu    — GGML_CUDA=OFF GGML_VULKAN=OFF
#   vulkan — GGML_VULKAN=ON
#   cuda   — GGML_CUDA=ON (needs CMake CUDA toolkit on PATH)
#
# The CPU build produces the core DLLs (llama, llama-common, mtmd, ggml,
# ggml-base, all ggml-cpu-* variants, libomp, ggml-rpc).  The Vulkan and
# CUDA builds add their respective backend DLL.
#
# Prerequisites:
#   - VS 2022 "Desktop development with C++" + "CMake tools" + "Ninja"
#   - Run from a VS Developer Prompt (or a script that calls vcvarsall.bat)
#   - For CUDA: CUDA toolkit 12.x installed, CUDAToolkit_ROOT set or on PATH
#   - For Vulkan: Vulkan SDK installed, VULKAN_SDK set or on PATH
#
# Usage (from the repo root, in a VS Developer PowerShell):
#   .\scripts\build-llama.ps1 -SourceDir "D:\Developpement\Dependencies\llama.cpp"
#   .\scripts\build-llama.ps1 -SourceDir "..." -Backends cpu,vulkan
#   .\scripts\build-llama.ps1 -SourceDir "..." -Backends cpu,cuda -CudaToolkit "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4"

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$SourceDir,

    [string[]]$Backends = @("cpu","vulkan","cuda"),

    [string]$CudaToolkit,   # optional, defaults to auto-detect
    [string]$VulkanSdk,     # optional, defaults to auto-detect
    [switch]$SkipIfPresent  # no-op if llama.dll already staged
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$destDir  = Join-Path $repoRoot "external\lib\windows\llamacpp"

if (-not (Test-Path $SourceDir)) {
    throw "SourceDir not found: $SourceDir"
}

if ($SkipIfPresent -and (Test-Path (Join-Path $destDir "llama.dll"))) {
    Write-Host "llama.dll already staged — skipping."
    return
}

# --- Locate build tools -----------------------------------------------------
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) { throw "cmake not found on PATH" }

New-Item -ItemType Directory -Force -Path $destDir | Out-Null
$buildRoot = Join-Path ([System.IO.Path]::GetTempPath()) "llama-build-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null

function Invoke-CMake {
    param([string]$args_)
    & cmake @args_
    if ($LASTEXITCODE -ne 0) { throw "cmake failed: $args_" }
}

function Invoke-CMakeBuild {
    param([string]$dir, [string[]]$targets)
    $tArgs = @()
    foreach ($t in $targets) { $tArgs += @("--target", $t) }
    & cmake --build $dir $tArgs --config Release -j (Get-CimInstance Win32_ComputerSystem | Select-Object -ExpandProperty NumberOfProcessors)
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed in $dir" }
}

# --- Copy DLLs from a build dir to the flat dest ----------------------------
function Copy-BackendDlls {
    param([string]$binDir, [string[]]$patterns)
    $count = 0
    foreach ($p in $patterns) {
        $files = Get-ChildItem -Path $binDir -Filter $p -ErrorAction SilentlyContinue
        foreach ($f in $files) {
            Copy-Item -Force $f.FullName $destDir
            $count++
        }
    }
    return $count
}

try {
    # =========================================================================
    # 1. CPU build (produces core + all ggml-cpu-* variants + libomp)
    # =========================================================================
    if ($Backends -contains "cpu") {
        Write-Host ">> Building llama.cpp [cpu]"
        $b = Join-Path $buildRoot "cpu"
        New-Item -ItemType Directory -Force -Path $b | Out-Null

        $cudaFlag = "-DGGML_CUDA=OFF"
        $vkFlag   = "-DGGML_VULKAN=OFF"

        Invoke-CMake @("-S", $SourceDir, "-B", $b,
            "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_SHARED_LIBS=ON",
            "-DLLAMA_BUILD_COMMON=ON",
            "-DLLAMA_BUILD_MTMD=ON",
            $cudaFlag, $vkFlag)

        Invoke-CMakeBuild -dir $b -targets @("llama","mtmd","llama-common")

        # The DLLs land in $b/bin/ (CMAKE_BUILD_TYPE=Release → bin/ or bin/Release)
        $binDir = Join-Path $b "bin"
        if (-not (Test-Path (Join-Path $binDir "llama.dll"))) {
            # Ninja may put them directly in build root
            $binDir = $b
        }

        $n = Copy-BackendDlls -binDir $binDir -patterns @(
            "llama.dll","llama-common.dll","mtmd.dll",
            "ggml.dll","ggml-base.dll","ggml-rpc.dll",
            "ggml-cpu-*.dll","libomp.dll")
        Write-Host "   $n DLLs staged (cpu)"
    }

    # =========================================================================
    # 2. Vulkan build
    # =========================================================================
    if ($Backends -contains "vulkan") {
        Write-Host ">> Building llama.cpp [vulkan]"
        $b = Join-Path $buildRoot "vulkan"
        New-Item -ItemType Directory -Force -Path $b | Out-Null

        $vkOpts = @("-DGGML_VULKAN=ON")
        if ($VulkanSdk) { $vkOpts += "-DVULKAN_SDK=$VulkanSdk" }
        elseif ($env:VULKAN_SDK) { $vkOpts += "-DVULKAN_SDK=$env:VULKAN_SDK" }

        Invoke-CMake @("-S", $SourceDir, "-B", $b,
            "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_SHARED_LIBS=ON",
            "-DLLAMA_BUILD_COMMON=ON",
            "-DGGML_CUDA=OFF",
            $vkOpts)

        Invoke-CMakeBuild -dir $b -targets @("ggml-vulkan")

        $binDir = Join-Path $b "bin"
        if (-not (Test-Path (Join-Path $binDir "ggml-vulkan.dll"))) {
            $binDir = $b
        }

        $n = Copy-BackendDlls -binDir $binDir -patterns @("ggml-vulkan.dll")
        Write-Host "   $n DLLs staged (vulkan)"
    }

    # =========================================================================
    # 3. CUDA build
    # =========================================================================
    if ($Backends -contains "cuda") {
        Write-Host ">> Building llama.cpp [cuda]"
        $b = Join-Path $buildRoot "cuda"
        New-Item -ItemType Directory -Force -Path $b | Out-Null

        $cudaOpts = @("-DGGML_CUDA=ON")
        if ($CudaToolkit) {
            $cudaOpts += "-DCMAKE_CUDA_TOOLKIT_ROOT_DIR=$CudaToolkit"
        }
        elseif ($env:CUDA_PATH) {
            $cudaOpts += "-DCMAKE_CUDA_TOOLKIT_ROOT_DIR=$env:CUDA_PATH"
        }

        Invoke-CMake @("-S", $SourceDir, "-B", $b,
            "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_SHARED_LIBS=ON",
            "-DLLAMA_BUILD_COMMON=ON",
            "-DGGML_VULKAN=OFF",
            $cudaOpts)

        Invoke-CMakeBuild -dir $b -targets @("ggml-cuda")

        $binDir = Join-Path $b "bin"
        if (-not (Test-Path (Join-Path $binDir "ggml-cuda.dll"))) {
            $binDir = $b
        }

        $n = Copy-BackendDlls -binDir $binDir -patterns @(
            "ggml-cuda.dll","cudart64_*.dll","cublas64_*.dll","cublasLt64_*.dll")
        Write-Host "   $n DLLs staged (cuda + runtime)"
    }

} finally {
    # Clean up the temp build root
    Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
}

$total = (Get-ChildItem -Path $destDir -Filter "*.dll").Count
Write-Host ""
Write-Host "Done. $total DLLs in $destDir"
