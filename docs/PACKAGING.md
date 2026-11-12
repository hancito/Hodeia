# Packaging (Linux + Windows)

> **Status**
>
> | Target                        | State                                                        |
> | ----------------------------- | ------------------------------------------------------------ |
> | Windows installer (Inno Setup) | ✅ Implemented — `package/windows/hodeia.iss` + `scripts/Windows/package-app.ps1` |
> | Arch Linux (`PKGBUILD`)        | ✅ Prototype — `package/arch/PKGBUILD`, self-contained `/opt/hodeia` layout |
> | Debian / Fedora (`.deb`, `.rpm`) | 📝 Planned — no CPack / `install()` rules yet             |
> | CI (GitHub Actions)            | 📝 Planned                                                   |
> | Code signing                   | 📝 Not decided                                               |
>
> The sections marked **planned** below are the proposed design to review.
>
> Last updated: 2026-11-12.

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

## Arch Linux — implemented (`package/arch/PKGBUILD`)

The Arch package uses the **self-contained `/opt` layout (Option B below)**
and builds **everything from source**:

- Hodeia is `git clone`d (not a tarball), because the bundled `.gguf` models
  are stored with **git-lfs** — a tarball would only contain the LFS pointers.
  The clone is checked out at tag `v${pkgver}`.
- `prepare()` clones the dependencies at pinned versions (llama.cpp `v0.6.0`,
  MuPDF `1.28.5` with its submodules, Tesseract `5.5.3`, Leptonica `1.87.0`,
  sqlite-vec `v0.1.10-alpha.4`) and detects the GPU backend: **CUDA** if
  `nvcc` is found, otherwise **Vulkan** (CPU is always included).
- `build()` builds Leptonica → Tesseract → MuPDF (static) → sqlite-vec →
  llama.cpp (flat `.so` set) → Hodeia.
- `package()` copies the deploy folder to `/opt/hodeia` (minus build
  artefacts), symlinks `/usr/bin/hodeia`, and installs `hodeia.desktop`
  (`Categories=Office;Science;`) and the icon.

Build with `makepkg -s` from `package/arch/`. Runtime `depends`: `qt6-base`,
`qt6-declarative`, `qt6-shadertools`, `libpng`, `libjpeg-turbo`, `libtiff`,
`zlib`, `libomp`. Makedepends include `git-lfs`.

---

## Two open decisions for `.deb` / `.rpm` (need a call before implementing)

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

**Recommendation: Option A.** (The Arch `PKGBUILD` currently uses Option B;
moving it to Option A would follow once the `install()` rules exist.)

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

### Step 3 — Arch `PKGBUILD` (Option A variant)

> A working `/opt` PKGBUILD already exists — see
> [Arch Linux — implemented](#arch-linux--implemented-packagearchpkgbuild).
> The sketch below is what it would become under Option A.

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

> **Status: installer implemented** (Tier 2 below). Still open: code
> signing, CI, portable ZIP, auto-update.

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
  `scripts/Windows/package-app.ps1` (Zed task "Windows: Package Installer
  (Inno Setup)"; `-Build` builds Release first). Requires Inno Setup
  6.5.2+ or 7 (`winget install JRSoftware.InnoSetup`).
  - Packages `build/Release` (minus CMake/Ninja artefacts, `.lib`/`.pdb`/…)
    into `dist/Hodeia-Setup-<version>-x64.exe`. Refuses non-Release builds
    (debug CRT/Qt DLLs are not redistributable). Version comes from
    `project(VERSION)` in `CMakeLists.txt`.
  - Per-user install by default (no admin, `%LOCALAPPDATA%\Programs`);
    all-users (`Program Files`) can be picked in a dialog. Windows 10+, x64.
  - VC++ runtime (`vc_redist.x64.exe`, staged by `windeployqt`) installed
    only if missing or older.
  - Start-Menu shortcut + optional desktop shortcut; the icon comes from
    `Hodeia.exe` (embedded via `src/hodeia.rc`). FR/EN wizard.
  - **Updates:** run the new `Setup.exe` over an existing install. The fixed
    `AppId` (**must never change**) lets it reuse the previous install dir
    and tasks, close a running Hodeia, ask before downgrading, and silently
    uninstall the previous version first so files that are no longer
    shipped do not linger.
  - User data (`%APPDATA%\Hodeia`: projects, models, config) is never touched
    by install, update or uninstall.
  - ~3 GB payload (mostly `.gguf` + CUDA DLLs): non-solid LZMA2; a single
    `Setup.exe` can hold up to ~4.2 GB compressed.
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
   - Tier 2: `pwsh scripts/Windows/package-app.ps1` → `dist/Hodeia-Setup-<ver>-x64.exe`
     (it passes the `AppVersion` / `BuildDir` / `VCRedistBuild` defines that
     `hodeia.iss` requires — do not call `iscc` on the `.iss` directly).
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
      run: pwsh scripts/Windows/package-app.ps1
    - name: Sign (optional)
      uses: signpath/github-action@v1
      with:
        artifacts: "dist/Hodeia-Setup-*.exe"
        # ... SignPath config
    - name: Upload
      uses: actions/upload-artifact@v4
      with:
        name: hodeia-windows
        path: |
          dist/Hodeia-Setup-*.exe
          Hodeia-*-win64-portable.zip
```

---

## Files

**Already in the repo**

- `package/arch/PKGBUILD` + `package/arch/hodeia.desktop` — Arch package (`/opt` layout).
- `package/windows/hodeia.iss` — Inno Setup script.
- `scripts/Windows/package-app.ps1` — installer build driver.
- `src/hodeia.rc` — embeds the icon in `Hodeia.exe`.

**To create / change (when green-lit)**

- `CMakeLists.txt` — add `install()` rules + CPack block (`.deb` / `.rpm`).
- `cmake/PlatformLinux.cmake` — adjust rpath for the FHS lib location (Option A).
- `NOTICE` — already lists the third-party components; the installer could
  show it on the license page (it currently shows `LICENSE`).

**Shared / CI**

- `.github/workflows/packages.yml` — new (Linux 3-way matrix + Windows job).

## Not decided / TODO

**Linux**

- [ ] Choose packaging model (A/B/C) for `.deb` / `.rpm` — see "Two open decisions" (Arch uses B today).
- [x] Bundle vs download the models — bundled (git-lfs in the Arch build).
- [ ] Confirm the exact Qt6 package names per distro (versions differ by release).
- [x] `.desktop` Categories — `Office;Science;`.
- [ ] Which icon sizes to ship (one PNG today).
- [ ] Whether to ship `arm64` builds too (the CMake already supports the
      `GGML_CPU_ALL_VARIANTS` set; the `.so` set is arch-specific, so an arm64
      build needs to be built on arm64).
- [ ] Reproducibility / signing (GPG for the AUR, detached signatures for
      deb/rpm) — usually added later.

**Windows**

- [x] Tier choice — Inno Setup installer. (Portable ZIP still optional.)
- [ ] Code-signing provider: SignPath (free OSS tier) vs CA cert vs none.
- [x] CRT — the VC++ Redist is shipped in the installer, installed only if missing/older.
- [x] App ID GUID — fixed in `hodeia.iss` (must never change). Publisher name is "Hodeia".
- [ ] Auto-update (Tier 3) — defer until there's a release cadence.
- [ ] `arm64` Windows builds (the CMake + prebuilt llama DLLs are x64-only
      today; an arm64 build needs an arm64 toolchain + rebuild of llama.cpp).
