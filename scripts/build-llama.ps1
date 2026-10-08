# build-llama.ps1
# Builds llama.cpp from a local source checkout (MSVC / Ninja) and stages
# the resulting DLLs into external\lib\windows\llamacpp\{cpu,vulkan,cuda}
# matching the Linux backend subdirectory layout.
#
# Three backend variants are built (same as Linux):
#   cpu    -- GGML_CUDA=OFF GGML_VULKAN=OFF
#   vulkan -- GGML_VULKAN=ON
#   cuda   -- GGML_CUDA=ON (needs CMake CUDA toolkit on PATH)
#
# The CPU build produces the core DLLs (llama, llama-common, mtmd, ggml,
# ggml-base, all ggml-cpu-* variants, libomp, ggml-rpc) → cpu/.
# The Vulkan build produces ggml-vulkan.dll → vulkan/.
# The CUDA build produces ggml-cuda.dll + CUDA runtime → cuda/.
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
$cpuDest   = Join-Path $destDir "cpu"
$vulkanDest = Join-Path $destDir "vulkan"
$cudaDest   = Join-Path $destDir "cuda"

if (-not (Test-Path $SourceDir)) {
    throw "SourceDir not found: $SourceDir"
}

if ($SkipIfPresent -and (Test-Path (Join-Path $cpuDest "llama.dll"))) {
    Write-Host "llama.dll already staged - skipping."
    return
}

# --- Ensure MSVC environment ------------------------------------------------
# If cl.exe is not on PATH, source vcvarsall.bat and import its env vars.
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vcvarsAll = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
    if (-not (Test-Path $vcvarsAll)) { throw "vcvarsall.bat not found at $vcvarsAll" }
    Write-Host ">> Sourcing MSVC environment (vcvarsall x64)..."
    $tmpBat = Join-Path ([System.IO.Path]::GetTempPath()) "vsenv-$([guid]::NewGuid().ToString('N')).bat"
    @"
@echo off
call "$vcvarsAll" x64
set
"@ | Set-Content -Path $tmpBat -Encoding ASCII
    $envLines = & cmd /c "`"$tmpBat`""
    Remove-Item $tmpBat -ErrorAction SilentlyContinue
    foreach ($line in $envLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2])
        }
    }
    if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
        throw "cl.exe still not found after sourcing vcvarsall"
    }
}

# --- Locate build tools -----------------------------------------------------
$cmakeExe = "cmake"
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    # Fallback: VS 18 2026 Community CMake path
    $vsCmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $vsCmake) { $cmakeExe = $vsCmake }
}
if (-not (Get-Command $cmakeExe -ErrorAction SilentlyContinue)) {
    throw "cmake not found on PATH or at the known VS 18 location"
}

New-Item -ItemType Directory -Force -Path $cpuDest | Out-Null
New-Item -ItemType Directory -Force -Path $vulkanDest | Out-Null
New-Item -ItemType Directory -Force -Path $cudaDest | Out-Null
$buildRoot = Join-Path ([System.IO.Path]::GetTempPath()) "llama-build-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null

# --- sccache (compiler cache) ------------------------------------------------
# The build tree is wiped every run, so without a cache everything is
# recompiled from scratch each time.  sccache (MSVC-compatible) makes repeat
# builds near-instant for unchanged sources.  No-op if not installed.
$sccacheFlags = @()
if (Get-Command sccache -ErrorAction SilentlyContinue) {
    $sccacheFlags = @("-DCMAKE_C_COMPILER_LAUNCHER=sccache", "-DCMAKE_CXX_COMPILER_LAUNCHER=sccache")
    Write-Host ">> sccache detected - enabling compiler cache"
}

# GGML_NATIVE=OFF: for CUDA this selects llama.cpp's full curated arch list
# (50..121, Maxwell->Blackwell) instead of only the build host's GPU, so the
# backend works on every GPU.  For CPU it is a no-op (GGML_CPU_ALL_VARIANTS
# handles the microarch selection at runtime).
$nativeFlag = "-DGGML_NATIVE=OFF"

function Invoke-CMake {
    param([string[]]$args_)
    & $cmakeExe @args_ $sccacheFlags
    if ($LASTEXITCODE -ne 0) { throw "cmake failed: $($args_ -join ' ')" }
}

function Invoke-CMakeBuild {
    param([string]$dir, [string[]]$targets)
    $tArgs = @()
    foreach ($t in $targets) { $tArgs += @("--target", $t) }
    & $cmakeExe --build $dir $tArgs --config Release -j (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty NumberOfLogicalProcessors)
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed in $dir" }
}

# --- Copy DLLs from a build dir to the per-backend dest --------------------
function Copy-BackendDlls {
    param([string]$binDir, [string]$outDir, [string[]]$patterns)
    $count = 0
    foreach ($p in $patterns) {
        $files = Get-ChildItem -Path $binDir -Filter $p -ErrorAction SilentlyContinue
        foreach ($f in $files) {
            Copy-Item -Force $f.FullName $outDir
            $count++
        }
    }
    return $count
}

# --- Copy the MSVC import libs from the build tree ---------------------------
# With GGML_BACKEND_DL, CMake places the .lib files in the ggml CMake source
# dir (ggml/src/), NOT next to the DLLs in bin/.  MSVC link.exe needs them.
# ggml-rpc / libomp are never linked by the app, so skip them.
function Copy-ImportLibs {
    param([string]$buildDir, [string]$outDir)
    $skip = @{ "ggml-rpc" = $true; "libomp" = $true }
    $count = 0
    $libs = Get-ChildItem -Path $buildDir -Filter "*.lib" -Recurse -ErrorAction SilentlyContinue
    foreach ($lib in $libs) {
        if (-not $skip.Contains($lib.BaseName)) {
            Copy-Item -Force $lib.FullName $outDir
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
        # Rebuild if the core DLL or its import lib is missing
        if ((Test-Path (Join-Path $cpuDest "llama.dll")) -and (Test-Path (Join-Path $cpuDest "llama.lib"))) {
            Write-Host ">> [cpu] llama.dll + llama.lib already present - skipping"
        } else {
            Write-Host ">> Building llama.cpp [cpu]"
            $b = Join-Path $buildRoot "cpu"
            New-Item -ItemType Directory -Force -Path $b | Out-Null

            $cudaFlag = "-DGGML_CUDA=OFF"
            $vkFlag   = "-DGGML_VULKAN=OFF"

            # GGML_BACKEND_DL + GGML_CPU_ALL_VARIANTS: build every ggml-cpu-*
            # microarch variant as a separate DLL that ggml loads at runtime
            # (LoadLibrary + ggml_backend_score), picking the best match for
            # the host CPU.  Without these two flags, NO variant targets are
            # defined at all -- only the monolithic "ggml-cpu" (built for the
            # build host), so the app would run without AVX-512 on e.g. a Zen 5.
            Invoke-CMake @("-S", $SourceDir, "-B", $b,
                "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DBUILD_SHARED_LIBS=ON",
                "-DLLAMA_BUILD_COMMON=ON",
                "-DLLAMA_BUILD_MTMD=ON",
                "-DGGML_BACKEND_DL=ON",
                "-DGGML_CPU_ALL_VARIANTS=ON",
                $nativeFlag,
                $cudaFlag, $vkFlag)

            # Build the core libs AND all microarch variants in one shot:
            # add_dependencies(ggml, <each variant>) is set by llama.cpp's
            # build system, so building "ggml" pulls in every ggml-cpu-*.
            Invoke-CMakeBuild -dir $b -targets @("llama","mtmd","llama-common","ggml")

            # The DLLs land in $b/bin/
            $binDir = Join-Path $b "bin"
            if (-not (Test-Path (Join-Path $binDir "llama.dll"))) {
                # Ninja may put them directly in build root
                $binDir = $b
            }

            $n = Copy-BackendDlls -binDir $binDir -outDir $cpuDest -patterns @(
                "llama.dll","llama-common.dll","mtmd.dll",
                "ggml.dll","ggml-base.dll","ggml-rpc.dll",
                "ggml-cpu-*.dll","libomp.dll")
            $m = Copy-ImportLibs -buildDir $b -outDir $cpuDest
            Write-Host "   $n DLLs + $m import libs staged (cpu)"
        }
    }

    # =========================================================================
    # 2. Vulkan build
    # =========================================================================
    if ($Backends -contains "vulkan") {
        if (Test-Path (Join-Path $vulkanDest "ggml-vulkan.dll")) {
            Write-Host ">> [vulkan] ggml-vulkan.dll already present - skipping"
        } else {
            Write-Host ">> Building llama.cpp [vulkan]"
            $b = Join-Path $buildRoot "vulkan"
            New-Item -ItemType Directory -Force -Path $b | Out-Null

            $vkOpts = @("-DGGML_VULKAN=ON")
            $sdkPath = $null
            if ($VulkanSdk) { $sdkPath = $VulkanSdk }
            elseif ($env:VULKAN_SDK) { $sdkPath = $env:VULKAN_SDK }
            if ($sdkPath) {
                $env:VULKAN_SDK = $sdkPath
            }

            Invoke-CMake @("-S", $SourceDir, "-B", $b,
                "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DBUILD_SHARED_LIBS=ON",
                "-DLLAMA_BUILD_COMMON=ON",
                "-DGGML_CUDA=OFF",
                $nativeFlag,
                $vkOpts)

            Invoke-CMakeBuild -dir $b -targets @("ggml-vulkan")

            $binDir = Join-Path $b "bin"
            if (-not (Test-Path (Join-Path $binDir "ggml-vulkan.dll"))) {
                $binDir = $b
            }

            $n = Copy-BackendDlls -binDir $binDir -outDir $vulkanDest -patterns @(
                "llama.dll","llama-common.dll","mtmd.dll",
                "ggml.dll","ggml-base.dll","ggml-vulkan.dll")
            $m = Copy-ImportLibs -buildDir $b -outDir $vulkanDest
            Write-Host "   $n DLLs + $m import libs staged (vulkan)"
        }
    }

    # =========================================================================
    # 3. CUDA build
    # =========================================================================
    if ($Backends -contains "cuda") {
        if (Test-Path (Join-Path $cudaDest "ggml-cuda.dll")) {
            Write-Host ">> [cuda] ggml-cuda.dll already present - skipping"
        } else {
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
                $nativeFlag,
                $cudaOpts)

            Invoke-CMakeBuild -dir $b -targets @("ggml-cuda")

            $binDir = Join-Path $b "bin"
            if (-not (Test-Path (Join-Path $binDir "ggml-cuda.dll"))) {
                $binDir = $b
            }

            $n = Copy-BackendDlls -binDir $binDir -outDir $cudaDest -patterns @(
                "llama.dll","llama-common.dll","mtmd.dll",
                "ggml.dll","ggml-base.dll",
                "ggml-cuda.dll","cublas64_*.dll","cublasLt64_*.dll")
            $m = Copy-ImportLibs -buildDir $b -outDir $cudaDest
            Write-Host "   $n DLLs + $m import libs staged (cuda + runtime)"
        }
    }

} finally {
    # Clean up the temp build root
    Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
}

$total = (Get-ChildItem -Path $destDir -Filter "*.dll" -Recurse).Count
Write-Host ""
Write-Host "Done. $total DLLs in $destDir"
