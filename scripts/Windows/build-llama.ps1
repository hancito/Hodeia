# build-llama.ps1
# Builds llama.cpp from a local source checkout (MSVC / Ninja) with CPU +
# CUDA + Vulkan enabled in a SINGLE combined configure, then stages the
# resulting DLLs into external\lib\windows\llamacpp\{cpu,vulkan,cuda}.
#
# One combined build (NOT three isolated ones) is required: every ggml backend
# links the same ggml-base target, and GGML_BACKEND_DL=ON makes each backend a
# runtime-loadable MODULE lib exporting the generic ggml_backend_init /
# ggml_backend_score entry points.  Three separate builds would produce three
# divergent ggml-base DLLs and — if the GPU builds omit GGML_BACKEND_DL —
# backends that the runtime loader silently skips (CPU fallback).
#
#   cpu    -- core DLLs (llama, llama-common, mtmd, ggml, ggml-base, all
#            ggml-cpu-* microarch variants, libomp, ggml-rpc) → cpu/
#   vulkan -- ggml-vulkan.dll → vulkan/  (GGML_VULKAN=ON)
#   cuda   -- ggml-cuda.dll + CUDA runtime (cuBLAS) → cuda/  (GGML_CUDA=ON)
#
# CUDA / Vulkan are optional: if a toolkit/SDK is absent the configure drops
# that backend (with a warning) and the build still succeeds.  The app detects
# each backend at runtime by the presence of its DLL next to the exe.
#
# Prerequisites:
#   - VS 2022 "Desktop development with C++" + "CMake tools" + "Ninja"
#   - Run from a VS Developer Prompt (or a script that calls vcvarsall.bat)
#   - For CUDA: CUDA toolkit installed, CUDAToolkit_ROOT / CUDA_PATH set or on PATH
#   - For Vulkan: Vulkan SDK installed, VULKAN_SDK set or on PATH
#
# Usage (from the repo root, in a VS Developer PowerShell):
#   .\scripts\build-llama.ps1 -SourceDir "D:\Developpement\Dependencies\llama.cpp"
#   .\scripts\build-llama.ps1 -SourceDir "..." -Backends cpu,vulkan
#   .\scripts\build-llama.ps1 -SourceDir "..." -Backends cpu,cuda -CudaToolkit "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4"

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
# This script lives in <repo>/scripts/Windows, so the repo root is two levels up.
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
# Single flat staging dir: all backends (core + CPU variants + CUDA + Vulkan)
# and import libs live side by side, sharing one ggml-base ABI.
$destDir  = Join-Path $repoRoot "external\lib\windows\llamacpp"

if (-not (Test-Path $SourceDir)) {
    throw "SourceDir not found: $SourceDir"
}

if ($SkipIfPresent -and (Test-Path (Join-Path $destDir "llama.dll"))) {
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

New-Item -ItemType Directory -Force -Path $destDir | Out-Null
$buildRoot = Join-Path ([System.IO.Path]::GetTempPath()) "llama-build-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null

# --- sccache (compiler cache) ------------------------------------------------
# The build tree is wiped every run, so without a cache everything is
# recompiled from scratch each time.  sccache (MSVC-compatible) makes repeat
# builds near-instant for unchanged sources.  No-op if not installed.
#
# We only set the C and C++ compiler launchers (never a CUDA launcher).  This
# also keeps llama.cpp's own ccache auto-detection inert: ggml/src/CMakeLists.txt
# only sets a GLOBAL RULE_LAUNCH_COMPILE (the thing that would wrap nvcc) when
# GGML_CCACHE is ON AND no C/C++ launcher is set.  Because we always set the
# C/C++ launchers, that global rule never fires and sccache never touches nvcc.
# So no GGML_CCACHE=OFF is needed here — sccache handles nvcc fine (as it did
# in the previous per-backend builds).
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
    # CMake prints banner/info lines (e.g. "CMAKE_BUILD_TYPE=Release") to
    # stderr.  With $ErrorActionPreference=Stop a nested powershell would turn
    # any stderr line into a terminating RemoteException and abort mid-run.
    # Temporarily drop to Continue for the native call; we check $LASTEXITCODE
    # explicitly below, so real failures still throw.
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $cmakeExe @args_ $sccacheFlags 2>&1 | ForEach-Object { Write-Host $_ }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) { throw "cmake failed (exit $code): $($args_ -join ' ')" }
}

function Invoke-CMakeBuild {
    param([string]$dir, [string[]]$targets)
    $tArgs = @()
    foreach ($t in $targets) { $tArgs += @("--target", $t) }
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $cmakeExe --build $dir $tArgs --config Release -j (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty NumberOfLogicalProcessors) 2>&1 | ForEach-Object { Write-Host $_ }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) { throw "cmake build failed (exit $code) in $dir" }
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

# ===========================================================================
# Single COMBINED build: CPU + CUDA + Vulkan in ONE configure.
#
# Why one build instead of three isolated ones:
#   * Every ggml backend links the SAME ggml-base target (see ggml/src/
#     CMakeLists.txt: target_link_libraries(${backend} PRIVATE ggml-base)).
#     Three separate builds therefore produce THREE different ggml-base DLLs;
#     if they drift (different GGML_BACKEND_DL setting, ABI, or build flags)
#     the runtime loader fails.  One build gives every backend one shared
#     ggml-base.
#   * GGML_BACKEND_DL=ON is REQUIRED on the combined build: it makes
#     ggml_add_backend_library build each backend as a MODULE lib and define
#     GGML_BACKEND_DL for it, which is what emits the generic
#     ggml_backend_init / ggml_backend_score entry points that
#     ggml_backend_load_all() looks up.  Without it a backend only exports its
#     named init (ggml_backend_cuda_init / ggml_backend_vk_init) and is
#     silently skipped at runtime → CPU fallback.
#
# CUDA / Vulkan are optional: if a toolkit/SDK is absent, CMake configures
# them OFF (with a warning) and the build still succeeds with the other
# backends.  The app detects each backend at runtime via the DLL's presence.
# ===========================================================================
$backends = $Backends
$useCuda  = ($backends -contains "cuda")
$useVk    = ($backends -contains "vulkan")
# CPU is always built (it carries the core libs + microarch variants).
$useCpu   = $true

try {
    $b = $buildRoot
    New-Item -ItemType Directory -Force -Path $b | Out-Null

    # --- Assemble the configure flags --------------------------------------
    $cfg = @(
        "-S", $SourceDir, "-B", $b,
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_SHARED_LIBS=ON",
        "-DLLAMA_BUILD_COMMON=ON",
        "-DLLAMA_BUILD_MTMD=ON",
        # Runtime-loadable backends (see explanation above).
        "-DGGML_BACKEND_DL=ON",
        # Build every ggml-cpu-* microarch variant as a separate DLL that ggml
        # loads at runtime (LoadLibrary + ggml_backend_score), picking the best
        # match for the host CPU.  Without this, NO variant targets are defined
        # at all -- only the monolithic "ggml-cpu" (built for the build host),
        # so the app would run without AVX-512 on e.g. a Zen 5.
        "-DGGML_CPU_ALL_VARIANTS=ON",
        $nativeFlag
    )

    # CUDA: ON only if requested AND a toolkit is locatable.
    if ($useCuda) {
        $cudaToolkit = $CudaToolkit
        if (-not $cudaToolkit -and $env:CUDA_PATH) { $cudaToolkit = $env:CUDA_PATH }
        if ($cudaToolkit -and (Test-Path $cudaToolkit)) {
            $cfg += "-DGGML_CUDA=ON"
            $cfg += "-DCMAKE_CUDA_TOOLKIT_ROOT_DIR=$cudaToolkit"
            Write-Host ">> CUDA enabled: $cudaToolkit"
        } else {
            Write-Host ">> WARNING: CUDA requested but no toolkit found; configuring with GGML_CUDA=OFF"
            $cfg += "-DGGML_CUDA=OFF"
        }
    } else {
        $cfg += "-DGGML_CUDA=OFF"
    }

    # Vulkan: ON only if requested AND a SDK is locatable (Vulkan SDK or the
    # loader CMake can find).  We pass GGML_VULKAN=ON and let CMake decide;
    # if no Vulkan is available it falls back with a warning.
    if ($useVk) {
        if ($VulkanSdk) { $env:VULKAN_SDK = $VulkanSdk }
        $cfg += "-DGGML_VULKAN=ON"
        Write-Host ">> Vulkan requested (GGML_VULKAN=ON)"
    } else {
        $cfg += "-DGGML_VULKAN=OFF"
    }

    Write-Host ">> Configuring llama.cpp (combined: CPU + CUDA + Vulkan, GGML_BACKEND_DL=ON)"
    Invoke-CMake @($cfg)

    # Build the core libs, all microarch CPU variants, and the GPU backends in
    # one shot.  Building "ggml" pulls in every ggml-cpu-* variant (the build
    # system sets add_dependencies(ggml, <variant>)); we add the GPU backend
    # targets explicitly.
    $targets = @("llama", "mtmd", "llama-common", "ggml")
    if ($useCuda) { $targets += "ggml-cuda" }
    if ($useVk)   { $targets += "ggml-vulkan" }
    Invoke-CMakeBuild -dir $b -targets $targets

    # The DLLs land in $b/bin/ (Ninja) or the build root.
    $binDir = Join-Path $b "bin"
    if (-not (Test-Path (Join-Path $binDir "llama.dll"))) {
        $binDir = $b
    }

    # --- Distribute to a single flat staging dir ----------------------------
    # ONE build => ONE set of DLLs sharing a single ggml-base / ggml ABI.  All
    # backends (core + CPU variants + CUDA + Vulkan) and the import libs go
    # into $destDir.  The CUDA runtime (cuBLAS) comes from the toolkit, not the
    # build tree, so it is copied separately by the app's deploy step
    # (cmake/PlatformWindows.cmake).
    $n = Copy-BackendDlls -binDir $binDir -outDir $destDir -patterns @(
        "llama.dll","llama-common.dll","mtmd.dll",
        "ggml.dll","ggml-base.dll","ggml-rpc.dll",
        "ggml-cpu-*.dll","ggml-cuda.dll","ggml-vulkan.dll",
        "libomp.dll")
    $m = Copy-ImportLibs -buildDir $b -outDir $destDir
    Write-Host "   $n DLLs + $m import libs staged (single dir)"
} finally {
    # Clean up the temp build root
    Remove-Item -Recurse -Force $buildRoot -ErrorAction SilentlyContinue
}

$total = (Get-ChildItem -Path $destDir -Filter "*.dll").Count
Write-Host ""
Write-Host "Done. $total DLLs in $destDir"
