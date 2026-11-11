# Packaging Plan (Linux + Windows)

> **Status: PLANNING NOTES — not yet implemented.** This captures how to ship
> Hodeia on **Linux** (`.deb`, `.rpm`, Arch `pkg.tar.zst`) and **Windows**
> (self-contained folder → portable zip / installer). The build system is not
> wired for packaging yet; everything below is the proposed design to review.
>
> Last updated: 2026-10-09.

---

# Linux Packaging

## Current state (what we already have)

The Linux build (`cmake/PlatformLinux.cmake` → `hodeia_linux_deploy`) already
produces a **self-contained folder** next to the `Hodeia` binary:

- the `Hodeia` executable
- the llama.cpp `.so` set (core + every `ggml-cpu-<microarch>` variant + CUDA/Vulkan)
  with `$ORIGIN` rpath set via `patchelf`, plus unversioned dev symlinks
- `vec0.so` (sqlite-vec)
- `ui/themes/`, `Fonts/`, `tessdata/`, `Resources/Models/` (the bundled
  `Embedders/` + `Summarizers/` GGUF models)
- `i18n/*.qm` translations

So a package is essentially **re-bundling that folder with the right metadata**.

### Runtime dependencies (must be declared in the package)

Hodeia **statically** links MuPDF / Tesseract / Leptonica (so those are never
package deps). At runtime it needs:

| Dependency                                                     | Package (Debian/Ubuntu)         | Package (Fedora/RPM)              | Package (Arch)      |
| -------------------------------------------------------------- | ------------------------------- | --------------------------------- | ------------------- |
| Qt6 (Quick, QuickControls2, QuickEffects, Concurrent, Network) | `qt6-base`, `qt6-declarative`   | `qt6-qtbase`, `qt6-qtdeclarative` | `qt6-base`          |
| libpng                                                         | `libpng16-16`                   | `libpng`                          | `libpng`            |
| libjpeg                                                        | `libjpeg-turbo8`                | `libjpeg-turbo`                   | `libjpeg-turbo`     |
| libtiff                                                        | `libtiff5`/`libtiff6`           | `libtiff`                         | `libtiff`           |
| libz                                                           | `zlib1g`                        | `zlib`                            | `zlib`              |
| OpenMP (ggml-cpu)                                              | `libomp5`                       | `libgomp`                         | `libomp`            |
| Vulkan loader (optional, GPU only)                             | `libvulkan1`                    | `vulkan-loader`                   | `vulkan-icd-loader` |
| CUDA (optional, GPU only)                                      | nvidia runtime (user-installed) | same                              | `cuda`              |

Bundled in the package (NOT a dep): llama.cpp `.so` set, `vec0.so`, themes,
fonts, tessdata, the embedder model, translations.

---

## Two open decisions (need a call before implementing)

### 1. Packaging model

- **Option A — FHS `install()` + CPack + PKGBUILD (recommended).** Add a proper
  CMake `install()` rule that places files under `/usr` (binary → `/usr/bin`,
  libs → `/usr/lib/hodeia`, data → `/usr/share/hodeia`, plus a `.desktop` file
  and icon). CPack then drives both DEB and RPM from that rule, and an Arch
  `PKGBUILD` calls the same `cmake --install`. This is the idiomatic,
  maintainable path: packages install to the right FHS locations and declare
  real Qt deps.
  - **Cost:** the binary's rpath must point at `\$ORIGIN/../lib/hodeia` (or the
    libs must be on the default loader path), since they'll no longer sit
    next-to-binary.
- **Option B — self-contained `/opt` package.** Keep the current
  "everything next to the binary" layout and just wrap the folder under
  `/opt/hodeia`. Minimal change, but less idiomatic (no FHS, larger, and the
  app is harder to find/launch from the desktop).
- **Option C — standalone scripts only.** Write `dpkg-deb` / `rpmbuild` /
  `makepkg` scripts that bundle the existing deploy folder, without touching
  CMake. Fastest, least maintainable.

**Recommendation: Option A.**

### 2. Bundled models

`Resources/Models/` (the `multilingual-e5-small` embedder at ~126 MB plus the
`Summarizers/` chat models, several GB) is deployed next to the binary, which
makes the package several hundred MB to several GB.

- **Bundle it (recommended).** Works fully offline out of the box. Accept the
  larger package.
- **Download on first run.** Smaller package, but the current code seeds the
  models from the bundled tree (`seedBundledModels`), so this requires a code
  change to fetch from a URL instead.

**Recommendation: bundle it** (quantized models are acceptable; the app is
designed to run fully locally).

---

## Implementation plan (Option A)

### Step 1 — CMake `install()` rule

Add to `CMakeLists.txt` (top-level, after the platform include). Places the
deployed artifacts under the FHS layout:

```
/usr/bin/hodeia                         # the executable
/usr/lib/hodeia/lib*.so*                # llama.cpp .so set + vec0.so
/usr/share/hodeia/ui/themes/...         # themes
/usr/share/hodeia/Fonts/...             # fonts
/usr/share/hodeia/tessdata/...          # tessdata
/usr/share/hodeia/Resources/Models/     # bundled Embedders/ + Summarizers/ models
/usr/share/hodeia/i18n/*.qm             # translations
/usr/share/applications/hodeia.desktop  # desktop entry
/usr/share/icons/hicolor/<size>/.../hodeia.png
```

The binary's rpath needs to resolve the libs in `/usr/lib/hodeia` — either
`BUILD_RPATH "$ORIGIN/../lib/hodeia"` or install the libs where the loader
finds them by default.

### Step 2 — CPack (DEB + RPM)

Add to `CMakeLists.txt`:

```cmake
set(CPACK_PACKAGE_NAME "hodeia")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Hodeia")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Local-AI research assistant for academic papers")
set(CPACK_PACKAGE_CONTACT "you@example.com")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/<you>/Hodeia")
set(CPACK_PACKAGE_FILE_NAME "hodeia-${PROJECT_VERSION}-Linux-x86_64")

# DEB
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-base,qt6-declarative,libpng16-16,libjpeg-turbo8,libtiff5,libomp5")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")

# RPM
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase,qt6-qtdeclarative,libpng,libjpeg-turbo,libtiff,libgomp")

include(CPack)
```

Build: `cmake --build . && cpack -G DEB` (and `cpack -G RPM`).

### Step 3 — Arch `PKGBUILD`

`package/arch/PKGBUILD` (plus the `.desktop` + icon in `package/arch/`):

```bash
pkgname=hodeia
pkgver=0.1.0
pkgrel=1
pkgdesc="Local-AI research assistant for academic papers"
arch=(x86_64)
url="https://github.com/<you>/Hodeia"
license=(AGPL-3.0)
depends=(qt6-base libjpeg-turbo libtiff libomp)
makedepends=(cmake ninja clang pkgconf)
source=("$pkgname-$pkgver.tar.gz::https://github.com/<you>/Hodeia/archive/v$pkgver.tar.gz")

build() {
  cmake -S "$srcdir/$pkgname-$pkgver" -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build build -j$(nproc)
}

package() {
  cmake --install build --prefix "$pkgdir/usr"
}
```

Build with `makepkg -s`. (For the AUR, this becomes an `.SRCINFO` + `PKGBUILD`
in an AUR repo.)

### Step 4 — CI (GitHub Actions)

A package must be built **on the target distro** (Qt6 + rpath + the `.so` set
are distro-specific). Use a 3-way matrix:

| Job  | Runner / container                                        | Produces                            |
| ---- | --------------------------------------------------------- | ----------------------------------- |
| deb  | `ubuntu-24.04` (Qt6 via apt)                              | `hodeia_0.1_amd64.deb`              |
| rpm  | `fedora:40` container / Fedora runner                     | `hodeia-0.1.x86_64.rpm`             |
| arch | Arch container (e.g. `michaelforis/archlinux`) or Arch CI | `hodeia-0.1.1-1-x86_64.pkg.tar.zst` |

Each job: install build deps → run `scripts/Linux/build_*.sh` (or skip if
`external/lib/linux/` is already committed) → `cmake --build` → package →
upload artifact.

---

# Windows Packaging

> **Status: PLANNING NOTES — not yet implemented.** The Windows build is
> already self-contained; shipping is mostly about verifying the CRT, choosing
> a delivery format, adding code signing, and wiring CI.

## Current state (what we already have)

The Windows build (`cmake/PlatformWindows.cmake` → `hodeia_windows_deploy`)
already produces a **self-contained folder** next to `Hodeia.exe`:

- `Hodeia.exe`
- the full llama.cpp DLL set (core + every `ggml-cpu-<microarch>` variant +
  `ggml-cuda.dll` + `ggml-vulkan.dll` + `libomp.dll`)
- the CUDA runtime DLLs (`cudart64_*.dll`, `cublas64_*.dll`,
  `cublasLt64_*.dll`) — only if `ggml-cuda.dll` is present
- `vulkan-1.dll` (if the Vulkan backend was linked)
- the OCR runtime DLLs from vcpkg (`tesseract.dll`, `leptonica.dll`,
  `libpng16.dll`, `libjpeg-turbo.dll`, `openjp2-7.dll`, `gif.dll`, `z.dll`)
- `vec0.dll` (sqlite-vec)
- `ui/themes/`, `Fonts/`, `tessdata/`, `Resources/Models/` (the bundled
  `Embedders/` + `Summarizers/` GGUF models)
- `i18n/*.qm` translations
- the Qt runtime (DLLs + plugins: `platforms/qwindows.dll`, `styles/`,
  `imageformats/`, etc.) via `windeployqt`

So a Windows package is essentially **re-bundling that folder + verifying the
CRT + adding an installer / signing**.

### Runtime dependencies

Hodeia **statically** links MuPDF on Windows (no MuPDF DLL to ship). Tesseract
/ Leptonica / the image codecs are **dynamic** and already copied next to the
exe by the deploy step. The only thing that is _not_ currently guaranteed in
the folder is the **MSVC / UCRT runtime**:

| Dependency                        | Ship?                                           | Notes                                         |
| --------------------------------- | ----------------------------------------------- | --------------------------------------------- |
| `vcruntime140.dll`                | Usually present on Win10/11; **verify**         | MSVC 2015+ runtime                            |
| `msvcp140.dll`                    | Usually present on Win10/11; **verify**         | MSVC STL                                      |
| `ucrtbase.dll`                    | Always present on Win10/11 (UCRT is part of OS) | Universal C Runtime                           |
| VC++ Redist (`VC_redist.x64.exe`) | Optional fallback                               | Only if you want to support Win7/8 or be safe |

**Action:** after a clean Release build, run a CRT scan on the output folder
(e.g. [Dependencies](https://github.com/lucasg/Dependencies) or
`dumpbin /dependents`) to confirm `Hodeia.exe` + the llama DLLs only need
`vcruntime140.dll` / `msvcp140.dll` / `ucrtbase.dll`. If any are missing from a
target machine's system32, either (a) document the VC++ 2015–2022 Redist as a
prerequisite, or (b) bundle the three DLLs (they are redistributable).

---

## Shipping tiers (pick one to start)

### Tier 1 — Portable ZIP (fastest, zero tooling)

Zip the deploy folder and publish it. User unzips and runs `Hodeia.exe`.

```
Hodeia-<version>-win64-portable.zip
└── Hodeia/
    ├── Hodeia.exe
    ├── *.dll            (llama, CUDA, vulkan, OCR, vec0, Qt, CRT)
    ├── ui/themes/
    ├── Fonts/
    ├── tessdata/
    ├── Resources/Models/
    └── i18n/
```

- Pros: no installer, no code signing needed to _run_ (just SmartScreen),
  trivial CI.
- Cons: no Start-Menu shortcut / uninstaller, users must keep the folder in
  one place (data paths are relative to the exe / AppData).

### Tier 2 — Inno Setup installer (**recommended**)

Use [Inno Setup](https://jrsoftware.org/isinfo.php) (free, widely used). It
gives a proper installer + uninstaller + Start-Menu/desktop shortcuts +
optional "add to PATH".

- Write `package/windows/hodeia.iss`:
  - `Source: build\Release\Hodeia.exe` + every DLL + data dirs (use
    `Source: "..\..\build\Release\*"; DestDir: "{app}"; Flags: recursesubdirs`
    to grab the whole deploy folder).
  - `Setup icon` + `AppId` GUID (stable across versions so upgrades work).
  - `ArchitecturesInstallIn64BitMode=x64`.
  - Optionally install the VC++ Redist silently as a pre-install step.
- Build: `iscc hodeia.iss` → `Hodeia-Setup-<version>.exe`.
- **Implemented:** `package/windows/hodeia.iss`, driven by
  `scripts/Windows/package-app.ps1` (Zed task "Windows: Package Installer").
  It packages `build/Release` (minus CMake/Ninja artefacts) into
  `dist/Hodeia-Setup-<version>-x64.exe`: per-user install by default (no
  admin; all-users optional), VC++ runtime installed only if missing/older,
  Start-Menu + optional desktop shortcut, FR/EN wizard. Version comes from
  `project(VERSION)` in `CMakeLists.txt`.
- Inno Setup's compiler is in the PATH on GitHub's `windows-latest` runner.

### Tier 3 — Auto-update (later)

[Sparkle](https://sparkle-project.org/) is macOS-only; on Windows the common
choice is [Squirrel.Windows](https://github.com/Squirrel/squirrel.windows) or
[Velocipack](https://github.com/velocidex/velocipack) or a simple HTTP check
against the latest GitHub Release. Defer until there's a real release cadence.

---

## Code signing

Without a code-signing certificate, SmartScreen shows a **"Windows protected
your PC"** warning on first run for _any_ unsigned exe (including the Inno
Setup installer). Options:

- **SignPath.io (recommended for open source).** Cloud code-signing with a
  free tier for public repos; integrates directly with GitHub Actions. Sign
  `Hodeia.exe` **and** the installer.
- **Traditional CA cert** (DigiCert / Sectigo / GlobalSign). Costs a few
  hundred $/year; gives you the "publisher name" badge under the warning.
- **Do nothing** and document the SmartScreen warning (works, but friction).

**Note:** sign the _inner_ exe and _every_ shipped DLL too, or some AV vendors
flag unsigned DLLs. The llama.cpp DLLs are third-party — either sign them
after staging, or bundle them in the installer (Inno signs the installer; the
inner files are still checked individually by SmartScreen/AV).

---

## CI (GitHub Actions) — `windows-latest` job

A single job (no matrix needed — x64-only for now):

1. **Setup**
   - `actions/checkout` (full depth for submodules if any).
   - `msys2/setup-msys2` is **not** needed — use the native MSVC + vcpkg
     path that the build already expects.
   - Install Qt 6.10 MSVC build (e.g. `jlumbroso/free-action-setup-qt@v4`
     with `qt-version: 6.10.x`, `architecture: desktop`, `qmake-spec: win64_msvc2022`).
   - `lukka/get-vcpkg` or the prebuilt vcpkg in the runner; then
     `vcpkg install --triplet x64-windows` from the repo root.
2. **Build**
   - `pwsh scripts/Windows/build-dependencies.ps1` (or skip if
     `external/lib/windows/` is already committed).
   - `pwsh scripts/Windows/build-app.ps1 -Configuration Release`.
3. **Verify CRT** (optional but nice): run a dependency scanner and fail if
   an unexpected CRT is referenced.
4. **Package**
   - Tier 1: `Compress-Archive build/Release Hodeia-<ver>-win64-portable.zip`.
   - Tier 2: `iscc package\windows\hodeia.iss` → `Hodeia-Setup-<ver>.exe`.
5. **Sign** (if using SignPath): `signpath/github-action` with the secret
   `SIGNPATH_CLOUD_API_KEY`, signing `Hodeia.exe` + the installer.
6. **Upload**
   - `actions/upload-artifact` for the zip / installer.
   - On `tag v*.*.*`, also `softprops/action-gh-release` to attach to the
     GitHub Release.

Example snippet:

```yaml
windows:
  runs-on: windows-latest
  steps:
    - uses: actions/checkout@v4
    - name: Setup Qt
      uses: jlumbroso/free-action-setup-qt@v4
      with:
        qt-version: 6.10.1
        arch: desktop
        install-deps: "true"
    - name: Restore vcpkg
      uses: lukka/get-vcpkg@v1
    - name: Install vcpkg deps
      run: vcpkg install --triplet x64-windows
      shell: pwsh
    - name: Build dependencies
      run: pwsh scripts/Windows/build-dependencies.ps1
    - name: Build app
      run: pwsh scripts/Windows/build-app.ps1 -Configuration Release
    - name: Build installer
      run: iscc package\windows\hodeia.iss
      shell: pwsh
    - name: Sign (optional)
      uses: signpath/github-action@v1
      with:
        artifacts: "Hodeia-Setup-*.exe"
        # ... SignPath config
    - name: Upload
      uses: actions/upload-artifact@v4
      with:
        name: hodeia-windows
        path: |
          Hodeia-Setup-*.exe
          Hodeia-*-win64-portable.zip
```

---

## Files to create / change (when green-lit)

**Linux**

- `CMakeLists.txt` — add `install()` rules + CPack block.
- `cmake/PlatformLinux.cmake` — adjust rpath for the FHS lib location (Option A).
- `package/arch/PKGBUILD` — new.
- `package/hodeia.desktop` — new (Name, Exec, Icon, Categories=Office;Science).

**Windows**

- `package/windows/hodeia.iss` — new (Inno Setup script).
- `NOTICE` — already lists the third-party components; no change needed, but
  the installer can surface it in the license page.
- (Optional) `package/windows/vc_redist.x64.exe` — if you choose to bundle the
  VC++ Redist.

**Shared / CI**

- `.github/workflows/packages.yml` — new (Linux 3-way matrix + Windows job).

## Not decided / TODO

**Linux**

- [ ] Choose packaging model (A/B/C) — see "Two open decisions".
- [ ] Decide: bundle vs download the embedder model.
- [ ] Confirm the exact Qt6 package names per distro (versions differ by release).
- [ ] Decide on a `.desktop` Categories value + which icon sizes to ship.
- [ ] Whether to ship `arm64` builds too (the CMake already supports the
      `GGML_CPU_ALL_VARIANTS` set; the `.so` set is arch-specific, so an arm64
      build needs to be built on arm64).
- [ ] Reproducibility / signing (GPG for the AUR, detached signatures for
      deb/rpm) — usually added later.

**Windows**

- [ ] Tier choice: portable ZIP only, or ZIP + Inno Setup (recommended).
- [ ] Code-signing provider: SignPath (free OSS tier) vs CA cert vs none.
- [ ] Whether to bundle `vcruntime140.dll` / `msvcp140.dll` or rely on the
      system / VC++ Redist.
- [ ] App ID GUID + publisher name for the installer (must be stable).
- [ ] Auto-update (Tier 3) — defer until there's a release cadence.
- [ ] `arm64` Windows builds (the CMake + prebuilt llama DLLs are x64-only
      today; an arm64 build needs an arm64 toolchain + rebuild of llama.cpp).
