# Hodeia — Technical Documentation

This document covers the architecture, code layout, core pipelines, database schema, configuration, and the full per-platform build (Linux / macOS / Windows). It is the single technical reference — build requirements are included here, not in a separate file.

---

## Architecture

Hodeia is a C++/Qt 6 Quick application. The layers, top to bottom:

```
┌─────────────────────────────────────────────────────────┐
│                   QML UI Layer                          │
│  Main.qml │ RagWindow.qml │ DisplayPane │ SummaryDbPane │
│  RagDbPane │ QueryPane │ MyMenuBar │ MyToolBar          │
│  BackendWindow │ Colors.qml │ Fonts.qml                  │
├─────────────────────────────────────────────────────────┤
│              C++ Controllers (QQML-invokeable)          │
│  EngineController │ SummaryController │ QueryController  │
│  ConfigManager │ ThemeManager │ Helpers                 │
│  OpenAlexClient │ CitationsModel                          │
├─────────────────────────────────────────────────────────┤
│                  Worker Threads                         │
│  IngestionEngine │ Summarizer │ QueryWorker             │
│  (QtConcurrent + QThread, one worker per controller)    │
├─────────────────────────────────────────────────────────┤
│                 Backend Layer                           │
│  LocalBackend (llama.cpp libcommon, llama.h)            │
│  RemoteBackend (QNetworkAccessManager / OpenAI HTTP)    │
├─────────────────────────────────────────────────────────┤
│                 Extraction Layer                        │
│  MuPDF (structured text + page rendering)               │
│  Tesseract + Leptonica (OCR fallback)                   │
├─────────────────────────────────────────────────────────┤
│                   Data Layer                            │
│  SQLite3 (embedded, WAL) │ sqlite-vec (vec0 virtual     │
│  table) │ SummaryModel │ QueryModel │ CitationsModel    │
└─────────────────────────────────────────────────────────┘
```

The three worker pipelines run off the GUI thread:

- **Ingestion** — `EngineController` owns an `IngestionEngine` worker on a `QThread`; `QtConcurrent` parallelizes per-file hashing/extraction.
- **Summarization** — `SummaryController` owns a `Summarizer` worker; it loads/unloads models around the LLM call.
- **RAG query** — `QueryController` owns a `QueryWorker`; each query runs on its own thread.

`main.cpp` constructs the singletons, registers them on the QML context, and loads the `Hodeia` QML module. It also sets up crash/diagnostic logging, console mirroring, llama.cpp log capture, font registration, and translation loading (see [Logging & Diagnostics](#logging--diagnostics)).

---

## Tech Stack

| Component           | Technology                                              |
| ------------------- | ------------------------------------------------------- |
| UI Framework        | Qt 6.10+ (Qt Quick / QML, QuickControls2, Fusion style) |
| Language            | C++23                                                   |
| Build System        | CMake 3.25+ (Ninja on all platforms)                    |
| Local LLM Inference | llama.cpp (GGUF models, `llama.h` + `libcommon`)        |
| Multimodal          | mtmd (llama.cpp) with mmproj projectors                 |
| Remote LLM          | OpenAI-compatible REST API                              |
| PDF Parsing         | MuPDF (layout-aware structured text + page rendering)   |
| OCR                 | Tesseract + Leptonica (LSTM-only, legacy engine off)    |
| Database            | SQLite3 (embedded, WAL mode)                            |
| Vector Search       | sqlite-vec (`vec0` virtual table)                       |
| Embedding Model     | multilingual-e5-small (384 dims, via llama.cpp)         |
| Citations           | OpenAlex REST API (works search, `cites` / `cited_by`)  |
| GPU Backends        | CUDA, Vulkan, CPU (auto-detected) / Metal (macOS)       |
| Threading           | Qt Concurrent, QThread, QThreadPool                     |

---

## Project Structure

```
Hodeia/
├── CMakeLists.txt                  # Slim build entry: project, Qt, platform routing
├── vcpkg.json                      # Windows dependency manifest (libmupdf, tesseract, vulkan-*)
├── vcpkg-overlay/                  # Overlay port: libmupdf built from local source
├── resources.qrc                   # (legacy, unused — qt_add_resources uses explicit files)
├── NOTICE                          # Qt + third-party licensing notices
├── LICENSE                         # AGPL-3.0
├── README.md                       # User-facing README
├── docs/
│   ├── TECHNICAL.md               # This file
│   └── PACKAGING.md               # Packaging status + plan (Linux / Windows)
│
├── package/
│   ├── arch/                       # PKGBUILD + hodeia.desktop (installs to /opt/hodeia)
│   └── windows/hodeia.iss          # Inno Setup installer script
│
├── cmake/                          # Modular CMake scripts
│   ├── CompilerFlags.cmake         # C++23, warnings, optimisation, LTO (opt-in)
│   ├── PlatformLinux.cmake         # Static .a + system libs, GPU discovery, patchelf deploy
│   ├── PlatformWindows.cmake       # Native MSVC + vcpkg, llama import libs, windeployqt
│   └── PlatformMac.cmake           # .dylib map, Metal, install_name_tool, macdeployqt
│
├── scripts/
│   ├── Linux/                      # Dependency build scripts (see "Building from source")
│   │   ├── get_depedencies.sh      #   clone/locate sources
│   │   ├── build_llama.sh          #   llama.cpp (CPU+CUDA+Vulkan combined, .so)
│   │   ├── build_mupdf.sh          #   MuPDF (static .a)
│   │   ├── build_tesseract.sh      #   Tesseract + Leptonica (static .a)
│   │   └── build_sqlitevec.sh      #   sqlite-vec (vec0.so)
│   └── Windows/                    # PowerShell build orchestration
│       ├── build-dependencies.ps1  #   stage llama.cpp + sqlite-vec, vcpkg install
│       ├── build-llama.ps1         #   build/stage llama.cpp (combined, CPU+Vulkan+CUDA)
│       ├── build-sqlitevec.ps1     #   fetch or build vec0.dll
│       ├── build-app.ps1           #   configure + build the app
│       ├── clean-app.ps1           #   remove build/ (Release + Debug)
│       ├── clean-deps.ps1          #   remove staged deps + vcpkg cache
│       ├── package-app.ps1         #   build the Inno Setup installer → dist/
│       └── update-mupdf-stamp.ps1  #   stamp the local MuPDF checkout so vcpkg rebuilds on change
│
├── include/                        # Public C++ headers (flat, isolated from external/)
│   ├── thememanager.h  configmanager.h  database.h  summary.h  query.h
│   ├── citationsmodel.h  summaryproxymodel.h  queryproxymodel.h
│   ├── ingestionengine.h  enginemanager.h  sqlitedb.h  sqlitevec.h
│   ├── localbackend.h  vramfit.h  localprompt.h  inferenceengine.h  remotebackend.h  summarizer.h
│   ├── summarythreadmanager.h  queryworker.h  querycontroller.h
│   └── openalex.h  helpers.h  textformat.h
│
├── src/                            # C++ source files + target definitions
│   ├── CMakeLists.txt              # add_library(HodeiaEngine) + qt_add_executable(Hodeia)
│   ├── main.cpp                    # Entry point: logging, fonts, QML engine + registration
│   ├── configmanager.cpp           # JSON config, project CRUD, model/backend selection
│   ├── database.cpp                # SQLite DB init, table/schema creation, project switch
│   ├── sqlitedb.cpp                # SQLite wrapper (open, exec, prepare, transactions)
│   ├── sqlitevec.cpp               # Vector embeddings (E5 via llama.cpp), vec0 CRUD/search
│   ├── ingestionengine.cpp         # PDF extraction (MuPDF + Tesseract), chunking
│   ├── enginemanager.cpp           # Thread controller for the PDF ingestion pipeline
│   ├── localbackend.cpp            # Local llama.cpp backend (model loading, inference, mmproj)
│   ├── vramfit.cpp                 # GPU memory fitting: model probe, per-layer KV estimate, fit loop
│   ├── localprompt.cpp             # PromptPayload / PageImage, prompt assembly shared by probe and inference
│   ├── inferenceengine.cpp         # Token-level inference (text + multimodal)
│   ├── remotebackend.cpp           # HTTP client for OpenAI-compatible APIs
│   ├── summarizer.cpp              # Paper summarization orchestrator
│   ├── summarythreadmanager.cpp    # Thread controller for summarization
│   ├── summary.cpp                 # SummaryModel (QAbstractListModel → QML)
│   ├── summaryproxymodel.cpp       # Sort/filter proxy for summaries
│   ├── queryworker.cpp             # RAG query execution worker
│   ├── querycontroller.cpp         # Thread controller for RAG queries
│   ├── query.cpp                   # QueryModel (QAbstractListModel → QML)
│   ├── queryproxymodel.cpp         # Sort/filter proxy for queries
│   ├── citationsmodel.cpp          # CitationsModel over the Citations table (APA format)
│   ├── openalex.cpp                # OpenAlexClient: references / citing works
│   ├── thememanager.cpp            # JSON-based theme loading
│   ├── helpers.cpp                 # QML helpers: summary display / PDF export, local models, remote backend models
│   ├── summaryformat.cpp           # PaperSummary: LLM JSON parsing (with repairs) and HTML rendering
│   ├── pdfexport.cpp               # HTML → PDF rendering with MuPDF (fz_story, footer, ToUnicode fix)
│   ├── textformat.cpp              # LLM output cleanup: LaTeX → Unicode, Markdown escaping
│   └── hodeia.rc                   # Windows only: embeds the app icon in Hodeia.exe
│
├── ui/                             # QML files
│   ├── Main.qml                    # Main window (split view: paper list + detail)
│   ├── RagWindow.qml               # Secondary window for RAG queries + history
│   ├── DisplayPane.qml             # Paper detail: text / LaTeX / references / PDF tabs
│   ├── SummaryDbPane.qml           # Paper list with search, sort, select, summarize
│   ├── RagDbPane.qml               # Query history list
│   ├── QueryPane.qml               # Query input + response display
│   ├── MyMenuBar.qml               # File menu (projects), Settings (themes, backends, models)
│   ├── MyToolBar.qml               # Toolbar: import, model selector, query button
│   ├── BackendWindow.qml           # Dialog: add / edit / remove remote backends
│   ├── Colors.qml                  # Singleton: theme color properties
│   ├── Fonts.qml                   # Singleton: font family names
│   └── themes/                     # 19 JSON theme files
│
├── external/                       # Third-party libraries (pre-compiled, per-platform)
│   ├── include/
│   │   ├── llamacpp/               # llama.cpp + ggml + mtmd headers
│   │   ├── mupdf/                  # MuPDF headers
│   │   ├── Sqlite3/                # SQLite3 headers
│   │   ├── SqliteVec/              # sqlite-vec headers
│   │   ├── tesseract/              # Tesseract headers
│   │   └── leptonica/              # Leptonica headers
│   ├── sqlite3/
│   │   └── sqlite3.c               # SQLite amalgamation (compiled as C)
│   └── lib/
│       ├── linux/                  # static .a + llama.cpp .so (flat: cpu+cuda+vulkan) + sqlitevec/
│       ├── mac/                    # static .a + llama.cpp .dylib (flat: cpu+metal, untested) + sqlitevec/
│       └── windows/                # llama.cpp .dll (flat: cpu+cuda+vulkan) + sqlitevec/
│                                   # (MuPDF/Tesseract/Leptonica/Vulkan come from vcpkg on Windows)
│
├── Resources/                      # Icons, Tesseract data, bundled embedder model
│   ├── tessdata/eng.traineddata
│   └── Embedders/multilingual-e5-small-q8_0.gguf
│
├── Fonts/                          # Custom application fonts
│   ├── Google_Sans/
│   ├── Google_Sans_Code/
│   └── Outfit/
│
└── translations/                   # Qt Linguist catalogs
    └── hodeia_fr.ts                # French (English is the source language)
```

> The CMake module URI is `Hodeia` — `engine.loadFromModule("Hodeia", "Main")` in `main.cpp`.

---

## Prerequisites

- **CMake** ≥ 3.25
- **Qt 6** ≥ 6.10 (Quick, QuickControls2, QuickEffects, Concurrent, Network, LinguistTools)
- **C++23-compatible compiler** (Clang 17+ on Linux/macOS, MSVC via Visual Studio on Windows)
- **Ninja** (Linux / macOS / Windows)
- **patchelf** (Linux, for RPATH manipulation)
- **OpenMP** (ggml CPU parallelism — `libomp` on macOS)
- **vcpkg** (Windows only)

The third-party development libraries (MuPDF, Tesseract, Leptonica, llama.cpp, sqlite-vec) are either in-tree (Linux), from vcpkg + staged DLLs (Windows), or compiled from source (macOS) — see [Building from source](#building-from-source) for each.

---

## Building from Source

The top-level `CMakeLists.txt` detects the platform and includes the matching `cmake/Platform<OS>.cmake`, which sets up third-party linking, GPU-backend discovery, and `POST_BUILD` deployment. Target definitions live in `src/CMakeLists.txt` (the `HodeiaEngine` static library + the `Hodeia` executable).

### Linux

The in-tree static libs under `external/lib/linux/` are pre-compiled; the llama.cpp `.so` set is built once (CPU + CUDA + Vulkan combined) into a single flat directory. Install the system packages for your distribution, then configure and build.

#### System packages

**Arch / Manjaro**

```bash
sudo pacman -S clang cmake ninja qt6-base qt6-declarative patchelf pkgconf \
  vulkan-icd-loader libomp make git wget
```

**Debian / Ubuntu**

```bash
sudo apt install -y clang cmake ninja-build patchelf pkg-config git wget \
  libclang-rt-dev libomp-dev qt6-base-dev qt6-declarative-dev libvulkan1
```

**Fedora**

```bash
sudo dnf install clang cmake ninja qt6-qtbase qt6-qtquickcontrols2 \
  patchelf pkgconf-pkg-config libomp git wget vulkan-loader
```

#### Staging the third-party libraries

The scripts under `scripts/Linux/` build and stage each dependency into `external/`:

```bash
cd scripts/Linux
./get_depedencies.sh          # clone/locate sources (llama.cpp, mupdf, tesseract, leptonica, sqlite-vec)
./build_llama.sh              # llama.cpp → external/lib/linux/llamacpp/  (flat, all backends)
./build_mupdf.sh              # MuPDF       → external/lib/linux/libmupdf*.a
./build_tesseract.sh          # Tesseract + Leptonica → external/lib/linux/  (+ tessdata)
./build_sqlitevec.sh          # sqlite-vec  → external/lib/linux/sqlitevec/vec0.so
```

If the libraries are already present in `external/` (e.g. committed), you can skip the staging scripts and go straight to the build.

#### Build

```bash
mkdir build && cd build
cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

> CMake auto-detects the available GPU backend (CUDA > Vulkan > CPU). Force one with
> `-DLLAMA_FORCE_BACKEND=cpu|cuda|vulkan`. The forced value only sets the
> `HODEIA_BACKEND_*` compile definition — the actual backend is chosen at runtime by
> `ggml_backend_load_all()` from whichever `.so` are present.

> **Sanitizer builds (GCC/Clang):** add `-DHODEIA_SANITIZE=address,undefined` (ASan + UBSan) or
> `-DHODEIA_SANITIZE=thread` (TSan; it cannot be combined with `address`). The prebuilt MuPDF uses
> 4-byte struct alignment on x86-64, so UBSan's alignment check is turned off for
> `ingestionengine.cpp` only. The Zed tasks `Linux: Configure/Build & Run (Sanitize)` set this up.

### macOS (Apple Silicon)

> **⚠️ macOS is not yet available.** Support is **experimental and untested** — the app is not yet released for macOS. The CMake build below mirrors the (tested) Linux schema, but it has not been validated on a real Apple-Silicon host. Treat these steps as reference, not a supported build path.

Metal is the only GPU backend. Like Linux, llama.cpp is built **once, combined** (CPU + Metal) with `GGML_BACKEND_DL` + `GGML_CPU_ALL_VARIANTS` and staged into the **single flat dir** `external/lib/mac/llamacpp/`; the app links the core libs only and selects the CPU microarch + Metal at runtime via `ggml_backend_load_all()`. You compile the five dependencies from source into `external/lib/mac/`, then build.

#### System tools

| Tool                     | Version                                    | Install                                                           |
| ------------------------ | ------------------------------------------ | ----------------------------------------------------------------- |
| Xcode Command Line Tools | ≥ 15.0                                     | `xcode-select --install`                                          |
| CMake                    | ≥ 3.25                                     | `brew install cmake`                                              |
| Ninja                    | latest                                     | `brew install ninja`                                              |
| LLVM/Clang               | ≥ 18.0                                     | `brew install llvm`                                               |
| Qt 6.10+                 | Quick, QuickControls2, Concurrent, Network | [Qt installer](https://www.qt.io/download) or `brew install qt@6` |
| libomp (OpenMP)          | —                                          | `brew install libomp`                                             |

> `libomp.dylib` is copied into the `.app` bundle by CMake at build time — the end user installs nothing.

#### 1. llama.cpp (CPU + Metal combined) → flat `.dylib` set

Build once with `GGML_BACKEND_DL` + `GGML_CPU_ALL_VARIANTS` (Metal as the GPU backend), mirroring `scripts/Linux/build_llama.sh`. This produces every `ggml-cpu-<microarch>` variant + `ggml-metal` + the core libs, all sharing one `ggml-base` ABI:

```bash
git clone https://github.com/ggml-org/llama.cpp.git
cd llama.cpp && mkdir build && cd build
cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DBUILD_SHARED_LIBS=ON \
  -DLLAMA_BUILD_COMMON=ON -DLLAMA_BUILD_MTMD=ON \
  -DGGML_BACKEND_DL=ON -DGGML_CPU_ALL_VARIANTS=ON -DGGML_NATIVE=OFF \
  -DGGML_METAL=ON \
  -DLLAMA_BLAS=OFF -DLLAMA_CUDA=OFF -DLLAMA_VULKAN=OFF -DLLAMA_OPENMP=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build . --target llama --target mtmd --target llama-common --target ggml --target ggml-metal --config Release
```

Copy **all** resulting `.dylib` into the **single flat dir** `external/lib/mac/llamacpp/` (core + every `ggml-cpu-<microarch>` variant + `ggml-metal` + `libomp`):

```bash
cp build/bin/*.dylib ../../external/lib/mac/llamacpp/   # run from the build dir
```

#### 2. MuPDF → static `.a`

Use `MUPDF_BUILD_THIRD_PARTY=ON` (not just `MUPDF_THIRD_PARTY=ON`) so **all** image-codec/FreeType/HarfBuzz/OpenJPEG deps are compiled and linked statically — the final app has no external image-codec dependency.

```bash
git clone https://github.com/ArtifexSoftware/mupdf.git && cd mupdf
git submodule update --init
mkdir build && cd build
cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DMUPDF_BUILD_TYPE=Release \
  -DMUPDF_BUILD_THIRD_PARTY=ON -DBUILD_SHARED_LIBS=OFF
cmake --build .
```

Copy into `external/lib/mac/`: `libmupdf.a`, `libmupdf-third.a`, `libmupdf-threads.a`.

#### 3. Leptonica → static `.a`

Reuse the static image-codec libs produced by the MuPDF build so Leptonica stays fully static.

```bash
wget https://github.com/DanBloomberg/leptonica/releases/download/1.87.0/leptonica-1.87.0.tar.gz
tar xzf leptonica-1.87.0.tar.gz && cd leptonica-1.87.0 && mkdir build && cd build
MUPDF_BUILD=/path/to/mupdf/build
cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_PROG=OFF \
  -DENABLE_PNG=ON -DENABLE_JPEG=ON -DENABLE_TIFF=ON -DENABLE_ZLIB=ON \
  -DENABLE_GIF=OFF -DENABLE_WEBP=OFF -DENABLE_OPENJPEG=OFF \
  -DPNG_LIBRARY="${MUPDF_BUILD}/_deps/libpng-build/libpng16.a" \
  -DJPEG_LIBRARY="${MUPDF_BUILD}/_deps/libjpeg-build/libjpeg.a" \
  -DTIFF_LIBRARY="${MUPDF_BUILD}/_deps/libtiff-build/libtiff.a" \
  -DZ_LIBRARY="${MUPDF_BUILD}/_deps/zlib-build/libz.a"
cmake --build .
```

Copy `build/src/libleptonica.a` → `external/lib/mac/`. Verify: `nm libleptonica.a | grep " U " | grep -iE "png|jpeg|tiff|zlib"` returns nothing.

#### 4. Tesseract → static `.a`

```bash
wget https://github.com/tesseract-ocr/tesseract/releases/download/5.5.3/tesseract-5.5.3.tar.gz
tar xzf tesseract-5.5.3.tar.gz && cd tesseract-5.5.3 && mkdir build && cd build
cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_TRAINING_TOOLS=OFF -DBUILD_TESTS=OFF \
  -DDISABLED_LEGACY_ENGINE=ON -DGRAPHICS_DISABLED=ON \
  -DLeptonica_DIR=/path/to/leptonica-1.87.0/build
cmake --build .
```

Copy `build/libtesseract.a` → `external/lib/mac/`.

#### 5. sqlite-vec → `.dylib`

```bash
wget https://github.com/asg017/sqlite-vec/raw/main/dist/vec0.c
clang -shared -fPIC -o vec0.dylib vec0.c
```

Copy `vec0.dylib` → `external/lib/mac/sqlitevec/`.

#### 6. Tesseract traineddata

```bash
mkdir -p Resources/tessdata
curl -fSL -o Resources/tessdata/eng.traineddata \
  "https://github.com/tesseract-ocr/tessdata_fast/raw/main/eng.traineddata"
```

#### Final build (macOS)

```bash
mkdir build && cd build
cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/llvm \
  -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

CMake runs **`macdeployqt`** in `POST_BUILD` — it copies all Qt frameworks, plugins (ICU, image formats, platform plugins), `libomp.dylib`, the llama.cpp `.dylib` set, `vec0.dylib`, themes, and fonts **into `Hodeia.app`**. Result: a standalone `Hodeia.app` (~300 MB), drag-and-drop into `/Applications`, no external dependencies.

### Windows (MSVC + Ninja + vcpkg)

Native **MSVC** build — MSVC is the compiler, **Ninja** is the build generator (same as Linux/macOS). All native dependencies are MSVC-ABI (no MinGW/MSYS2). llama.cpp comes from pre-built MSVC release DLLs; MuPDF, Tesseract, Leptonica, and the Vulkan loader come from **vcpkg** (triplet `x64-windows`). `build-app.ps1` assembles the app.

> **Why MSVC:** the official llama.cpp release DLLs are compiled with MSVC (CRT `vcruntime140`). Linking a MinGW-built app against them would cause CRT/ABI incompatibilities, so the compiler toolchain is MSVC. Ninja (from the VS "C++ CMake tools" component, or installed separately) drives the build.

#### Prerequisites

| Component     | Version / choice                                                        | Install                                                                      |
| ------------- | ----------------------------------------------------------------------- | ---------------------------------------------------------------------------- |
| Visual Studio | recent, with "Desktop development with C++" + "C++ CMake tools" (Ninja) | [Visual Studio](https://visualstudio.microsoft.com/)                         |
| CMake         | ≥ 3.25                                                                  | bundled with VS, or `winget install Kitware.CMake`                           |
| Ninja         | latest                                                                  | bundled with VS "C++ CMake tools", or `winget install Ninja-build.Ninja`     |
| Qt 6          | ≥ 6.10, **MSVC 64-bit** build                                           | [Qt installer](https://www.qt.io/download) (pick the MSVC 64-bit kit)        |
| vcpkg         | latest                                                                  | `git clone https://github.com/microsoft/vcpkg <repo>\vcpkg`                  |
| llama.cpp     | pre-built MSVC release                                                  | `scripts\Windows\build-llama.ps1` builds + stages it, or stage prebuilt DLLs |

#### vcpkg packages

Declared in `vcpkg.json` at the repo root (manifest mode):

| vcpkg port                         | Linked target                    | Linkage (Win) | Role                                                   |
| ---------------------------------- | -------------------------------- | ------------- | ------------------------------------------------------ |
| `libmupdf`                         | `unofficial::libmupdf::libmupdf` | **static**    | PDF (embeds freetype, harfbuzz, jbig2dec, openjpeg, …) |
| `tesseract`                        | `Tesseract::libtesseract`        | dynamic       | OCR                                                    |
| `leptonica` (via tesseract)        | `leptonica`                      | dynamic       | images for OCR                                         |
| `vulkan-loader` + `vulkan-headers` | `Vulkan::Vulkan`                 | dynamic       | loader + `vulkan-1.dll`                                |

> The MuPDF port is named **`libmupdf`** (not `mupdf`). On Windows it builds **static** (incomplete DLL exports), so there is **no MuPDF DLL** to ship — the image codecs are embedded. The `vcpkg-overlay/` port builds MuPDF from your local source instead of the stock port's pinned version.

#### Staging the dependencies

`scripts\Windows\build-dependencies.ps1` does the whole thing (from a VS Developer Prompt):

```powershell
.\scripts\Windows\build-dependencies.ps1
```

It:

1. locates vcpkg and the MSVC Qt build;
2. builds/stages llama.cpp via `scripts\Windows\build-llama.ps1` (CPU + Vulkan + CUDA, one combined build, into the flat `external\lib\windows\llamacpp\`);
3. provides `vec0.dll` via `scripts\Windows\build-sqlitevec.ps1` (downloads the pinned `v0.1.10-alpha.4` loadable, verifies SHA-256; or `-Method build` to compile from source with `cl.exe`);
4. runs `scripts\Windows\update-mupdf-stamp.ps1`, then `vcpkg install --triplet x64-windows` (manifest mode, with the `vcpkg-overlay`).

> **MuPDF source stamp:** vcpkg hashes the port directory into the package ABI but knows nothing about the external MuPDF checkout the overlay builds from. `update-mupdf-stamp.ps1` writes `vcpkg-overlay/ports/libmupdf/source-stamp.txt` (HEAD commit, submodule commits, hash of uncommitted changes), so any change to the MuPDF checkout forces vcpkg to rebuild `libmupdf` instead of reusing a stale cached package. `build-dependencies.ps1` and `build-app.ps1` run it automatically.

The staged llama.cpp set (all backends in one flat dir):

```
llama.dll, llama-common.dll, mtmd.dll
ggml.dll, ggml-base.dll, ggml-rpc.dll
ggml-cpu-x64.dll, ggml-cpu-sse42.dll, ggml-cpu-<arch>…   (every microarch variant)
ggml-cuda.dll      ← CUDA backend
ggml-vulkan.dll    ← Vulkan backend
libomp.dll, cudart64_*.dll, cublas64_*.dll, cublasLt64_*.dll
```

The single binary links **all three backends**; `ggml_backend_load_all()` picks the best at runtime (CUDA > Vulkan > CPU).

#### Building the app

```powershell
.\scripts\Windows\build-app.ps1            # Release (default)
.\scripts\Windows\build-app.ps1 -Configuration Debug
```

Under the hood it configures with the **Ninja** generator, the MSVC toolchain, the vcpkg toolchain file, and the Qt MSVC prefix:

```bash
cmake -S . -B build/Release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake \
  -DCMAKE_PREFIX_PATH=<Qt MSVC prefix>
cmake --build build/Release --config Release
```

> The MSVC compiler is picked up from the **VS Developer Prompt** environment (or via `vcvarsall`); with the Ninja generator there is no `-A x64` (x64 is the default MSVC toolchain).

#### Linking the llama.cpp DLLs (import libs)

The MSVC linker (`link.exe`) needs an import lib per DLL, but the prebuilt DLLs don't ship them. `cmake/PlatformWindows.cmake` generates the `.lib` files with **`llvm-dlltool`** (part of the VS LLVM toolset) when available; otherwise it links the DLLs **by full path** (MSVC `link.exe` resolves the undecorated C exports and emits an import lib automatically).

The application icon is embedded in `Hodeia.exe` through `src/hodeia.rc`, so Explorer, the taskbar and every shortcut (including the installer's) pick it up.

#### Building the installer

```powershell
.\scripts\Windows\package-app.ps1          # package the existing build\Release
.\scripts\Windows\package-app.ps1 -Build   # build Release first
```

Requires Inno Setup 6.5.2+ or 7 (`winget install JRSoftware.InnoSetup`). The script refuses non-Release builds, reads the version from `project(Hodeia VERSION …)` in `CMakeLists.txt`, and compiles `package/windows/hodeia.iss` into `dist\Hodeia-Setup-<version>-x64.exe`. Also available as the Zed task "Windows: Package Installer (Inno Setup)". See [PACKAGING.md](PACKAGING.md) for what the installer does.

---

## GPU Backend Selection

The app calls `ggml_backend_load_all()` at runtime and auto-detects the best available backend from whichever libraries are present next to the binary.

- **Linux / macOS** — CMake auto-detects CUDA/Vulkan (or Metal on macOS) at build time. Linux supports `-DLLAMA_FORCE_BACKEND=cpu|cuda|vulkan`.
- **Windows** — all three backends ship in a single binary; selection is fully runtime.
- **CPU microarch** — the combined llama.cpp build compiles every `ggml-cpu-<microarch>` variant; at runtime ggml loads the one that best matches the host CPU (e.g. AVX-512 on Zen 5) via `ggml_backend_score`. This is why the CPU backend is a runtime-loadable module, not a single monolithic lib.

---

## Post-Build Deployment

Each platform's `cmake/Platform<OS>.cmake` (`hodeia_<os>_deploy`) deploys the runtime next to the binary:

| Category         | Linux                                                                                                    | Windows                                                                             | macOS                                                         |
| ---------------- | -------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------- | ------------------------------------------------------------- |
| **llama.cpp**    | all `.so` (core + every CPU variant + CUDA/Vulkan), unversioned symlinks, `$ORIGIN` RPATH via `patchelf` | all `llama*.dll`/`ggml*.dll`/`mtmd.dll`/`libomp.dll`                                | base + backend `.dylib` with `@executable_path` install names |
| **CUDA runtime** | (system)                                                                                                 | `cudart64_*.dll`, `cublas64_*.dll`, `cublasLt64_*.dll` (if `ggml-cuda.dll` present) | —                                                             |
| **Vulkan**       | (system)                                                                                                 | `vulkan-1.dll` (vcpkg bin or SDK)                                                   | —                                                             |
| **OCR**          | in-tree static                                                                                           | `tesseract*.dll`, `leptonica*.dll` + image codecs (vcpkg bin)                       | in-tree static                                                |
| **Extension**    | `vec0.so`                                                                                                | `vec0.dll`                                                                          | `vec0.dylib`                                                  |
| **Resources**    | `ui/themes/`, `Fonts/`, `tessdata/`, `Resources/Models/` (bundled `Embedders/` + `Summarizers/` GGUF)    | same                                                                                | same                                                          |
| **Translations** | `i18n/*.qm`                                                                                              | `i18n/*.qm`                                                                         | `i18n/*.qm`                                                   |
| **Qt runtime**   | system Qt                                                                                                | `windeployqt`                                                                       | `macdeployqt` (frameworks + plugins)                          |

> **Windows C/C++ runtime (UCRT):** `ucrtbase.dll` / `vcruntime140.dll` are part of Windows 10/11, so no VC++ Redist is needed for Win10+ targets. `windeployqt` copies them if it detects the VS Build Tools.
>
> To create the Windows installer from this folder, run `scripts\Windows\package-app.ps1` (Inno Setup; see [Building the installer](#building-the-installer)).

### Resulting `external/lib/` layout

```
external/lib/
├── linux/                  # in-tree, pre-compiled ✅
│   ├── libmupdf.a  libmupdf-third.a  libmupdf-threads.a
│   ├── libtesseract.a  libleptonica.a
│   ├── llamacpp/        # FLAT: every .so (core + cpu variants + cuda + vulkan)
│   └── sqlitevec/vec0.so
│
├── mac/                    # compiled from source (see above) — NOT yet available / untested
│   ├── libmupdf.a  libmupdf-third.a  libmupdf-threads.a
│   ├── libtesseract.a  libleptonica.a
│   ├── llamacpp/        # FLAT: every .dylib (core + cpu variants + metal)
│   └── sqlitevec/vec0.dylib
│
└── windows/                # staged MSVC DLLs
    ├── llamacpp/          # FLAT: every .dll (cpu variants + cuda + vulkan)
    └── sqlitevec/vec0.dll
    # MuPDF / Tesseract / Leptonica / Vulkan: from vcpkg (no in-tree files needed)
```

### Recommended build order

**Linux / macOS (source):** 1) Leptonica → 2) Tesseract → 3) MuPDF → 4) llama.cpp → 5) sqlite-vec → 6) Hodeia.
**Windows (MSVC):** no native dependency compilation — 1) `build-dependencies.ps1` (llama.cpp + sqlite-vec + vcpkg) → 2) `build-app.ps1`.

---

## Core Pipelines

### PDF Ingestion Pipeline

1. **File hashing** — MD5 computed per file for deduplication (parallelized via `QtConcurrent`).
2. **Text extraction** — MuPDF's structured-text engine performs DFS traversal with column segmentation, paragraph breaks, dehyphenation, and clip preservation.
3. **OCR fallback** — if a page yields < 100 characters, Tesseract runs at 300 DPI grayscale. APIs are cached per-thread to minimize initialization overhead.
4. **Thumbnail generation** — 150 DPI page images rendered as PNGs.
5. **Chunking** — words broken by line-wrap hyphens are rejoined, the text is split into sentences, and sentences are packed into ~1500-character chunks with a 200-character overlap. A per-character page map is kept aligned with the normalized text, so each chunk gets the exact page of its first *new* sentence (not of the overlap carried over from the previous chunk).
6. **Embedding** — batches of 64 chunks sent through the E5 model; 384-dim vectors stored in the `chunks` (`vec0`) virtual table.
7. **Batch processing** — 10 PDFs per batch, with incremental progress reporting and memory cleanup.

### Summarization Pipeline

1. The paper's chunks are loaded and merged back **per page** (the overlap is stripped), giving one text block per page.
2. Page images are attached according to `visualMode`: **Full** = every page, **Reduced** = only pages whose metadata flags a figure, vector graphics or a table, **Text only** = none. The blocks are sent **interleaved** (image, text, image, text, …) in page order.
3. The visual system prompt is used only if at least one image actually made it into the payload; otherwise (or if the backend cannot pass images) the text-only prompt is used, so the model is never told about figures it does not receive.
4. Embedding model unloaded to free GPU memory.
5. Chat model loaded; the KV cache is sized from the exact prompt token count plus the response headroom (see [LocalBackend Details](#localbackend-details)).
6. Structured prompt requesting a JSON response: title, authors, journal, year, topics, research objective, methodology, core findings, critical limitations, citable summary, references.
7. Response parsed (malformed JSON is repaired when possible) and stored in the `Summaries` table. LaTeX in the text fields is converted to Unicode at parse time (see [Text Post-Processing](#text-post-processing)).
8. Chunk metadata updated (clean title, citation); a `Citations` row is created.
9. Chat model unloaded, embedding model reloaded.
10. `OpenAlexClient::checkEmptyReferences` fires (on `summaryFinished`) to queue the new paper for an OpenAlex lookup.

### RAG Query Pipeline

1. User query embedded and compared against the `chunks` index (retrieves more than `maxPages`, e.g. `max(maxPages*4, 80)`, to survive dedup).
2. Retrieved chunks grouped and deduplicated by `(paper_hash, page)`, truncated to `maxPages`.
3. Page images loaded (if `visualMode` includes visual context).
4. Structured prompt built: each page context is labelled with a numbered reference tag (`=== PAGE CONTEXT [3] — <source>, page 7 ===`). The model is told to cite **only** with these tags (`[3]`, `[2][5]`), never with author names or page numbers, to add no references section, and to write math in plain Unicode instead of LaTeX. These rules are repeated at the very end of the user message, since models weigh the end of a long prompt most.
5. LLM synthesizes a cross-paper response.
6. Post-processing: LaTeX is converted to Unicode (Markdown target), then `resolveCitations` replaces each tag group with a full inline citation built from the retrieved pages — e.g. `(Walker, Ben-Akiva (2002), pp. 5, 7; Marley (2002), p. 2)` — so inline pages always match the footer. Unknown tags and Markdown links are left untouched.
7. A `**Sources:**` footer is appended, listing sources in retrieval (relevance) order with their pages. Page numbers shown to the user are 1-based (stored 0-based). The footer label is deliberately not translated so stored history reads the same in every UI language.
8. Result persisted in the `Queries` table. A failed query (missing model file, model load failure, remote timeout or HTTP error) reports a user-facing error message instead of an empty answer.

---

## Database Schema

Each project is one SQLite file (`<project_name>.db`, WAL mode). Three tables plus the vector virtual table:

### `Summaries`

| Column         | Type                | Description                     |
| -------------- | ------------------- | ------------------------------- |
| `id`           | INTEGER PRIMARY KEY | Auto-increment                  |
| `paper_hash`   | TEXT UNIQUE         | MD5 of the PDF file             |
| `title`        | TEXT                | Extracted paper title           |
| `authors`      | TEXT                | Comma-separated authors         |
| `pages_num`    | INTEGER             | Page count                      |
| `journal`      | TEXT                | Source journal/conference       |
| `year`         | TEXT                | Publication year                |
| `topics`       | TEXT                | Comma-separated topics          |
| `llmodel`      | TEXT                | Model used for summarization    |
| `summary_json` | TEXT                | Full structured summary as JSON |
| `date`         | DATETIME            | Timestamp                       |

### `Queries`

| Column     | Type                | Description                                      |
| ---------- | ------------------- | ------------------------------------------------ |
| `id`       | INTEGER PRIMARY KEY | Auto-increment                                   |
| `query`    | TEXT                | User's natural language question                 |
| `response` | TEXT                | LLM-generated answer                             |
| `date`     | DATETIME            | ISO 8601 timestamp (default `CURRENT_TIMESTAMP`) |
| `llmodel`  | TEXT                | Model used                                       |

### `Citations`

| Column           | Type    | Description                                                       |
| ---------------- | ------- | ----------------------------------------------------------------- |
| `id`             | INTEGER | Auto-increment (primary key)                                      |
| `paper_hash`     | TEXT    | MD5 of the PDF (unique) — joins to `Summaries`                    |
| `clean_title`    | TEXT    | Paper title for citation formatting                               |
| `work_id`        | TEXT    | OpenAlex work id (empty if not resolved)                          |
| `cites`          | TEXT    | JSON array of works this paper **cited** (or a marker string)     |
| `cited_by`       | TEXT    | JSON array of works that **cite** this paper (or a marker string) |
| `cites_count`    | INTEGER | Total OpenAlex count for `cites`                                  |
| `cited_by_count` | INTEGER | Total OpenAlex count for `cited_by`                               |
| `lookup_status`  | TEXT    | OpenAlex lookup state: `pending` (default), `matched`, `not_found` |
| `lookup_at`      | DATETIME | When the work was matched / last reported not found              |

The `CitationsModel` joins `Citations` to `Summaries` (so only summarized papers appear) and formats both sections as APA-style bibliographies for the QML display pane.

### `chunks` (sqlite-vec `vec0` virtual table)

| Column          | Type       | Description                         |
| --------------- | ---------- | ----------------------------------- |
| `rowid`         | INTEGER    | Auto-increment                      |
| `embedding`     | float[384] | 384-dim vector (E5)                 |
| `paper_hash`    | TEXT       | MD5 of source PDF                   |
| `page`          | INTEGER    | 0-based page number                 |
| `chunk_idx`     | INTEGER    | Chunk index within page             |
| `chunk_text`    | TEXT       | Raw text segment (~1500 chars)      |
| `citation`      | TEXT       | Generated citation string           |
| `clean_title`   | TEXT       | Paper title for citation formatting |
| `page_metadata` | TEXT       | Extra per-page JSON                 |

---

## Configuration

### `config.json`

Located at the Qt `AppDataLocation` (Linux/macOS: `~/.local/share/Hodeia/config.json`; Windows: `%APPDATA%\Hodeia\config.json`):

```json
{
  "activeThemeIndex": 0,
  "lastProject": "Default",
  "activeBackend": "Llama.cpp",
  "activeModel": "qwq32b",
  "openAlexApiKey": "",
  "visualMode": 1,
  "language": "en",
  "backendList": [
    { "name": "Llama.cpp", "url": "", "api_key": "" },
    { "name": "Ollama", "url": "http://localhost:11434/v1", "api_key": "" }
  ]
}
```

- **`visualMode`** — `0` = Text only, `1` = some visual context, `2` = Full (all pages). (Legacy: an old `useVisualContext` boolean is still read and mapped to `0`/`1`.)
- **`language`** — `"en"` (default) or `"fr"`. Changing it reloads the translator and re-loads the QML engine.
- **`backendList`** — one object per backend; `Llama.cpp` is the local backend (empty `url`), the rest are remote (OpenAI-compatible) endpoints.
- RAG tuning (`topK`, `maxPages`) and the model temperature are also persisted by the config manager.

Setters save to disk automatically on every change.

### Directory layout

PDFs are ingested **in place** (referenced from their original location, not copied into the project); only page thumbnails are stored under the project's `IMG/`.

```
~/.local/share/Hodeia/
├── config.json
├── Projects/
│   └── <project_name>/
│       ├── <project_name>.db        # SQLite database (WAL mode)
│       └── IMG/                     # page thumbnails (PDFs referenced in place)
└── Models/
    ├── Summarizers/
    │   └── <model_name>/
    │       ├── model.gguf
    │       └── mmproj.gguf          # optional: vision projector
    └── Embedders/
        └── <model_name>/
            └── model.gguf
```

**Deleting projects** goes through `DatabaseManager::deleteProjects`. If the active project is among those deleted, it first closes the SQLite connection (an open `.db` and its `-wal`/`-shm` files cannot be deleted on Windows), refusing if a worker operation is in flight. It then removes the folders and reopens the new active project. A folder that cannot be fully removed is reported to the user and stays in the project list.

The bundled model tree in `Resources/Models/` (the `Embedders/multilingual-e5-small` GGUF plus any `Summarizers/` chat models) is seeded into the data directory on first run (copy-if-missing), so RAG works without the user providing an embedder.

---

## LocalBackend Details

The `LocalBackend` wraps llama.cpp's `common` library. Token-level work (tokenize → decode → sample) is delegated to `InferenceEngine`.

**Model loading** (`loadModelWithDevices`), in three phases:

1. **Probe** — the GGUF is opened without allocating (`no_alloc`, mmap off) to read layers, embedding sizes and model size, measure the `mmproj`, and count the **exact prompt tokens** of the payload in the same pass.
2. **Fit** — the KV cache is sized as `prompt tokens + response headroom` (capped at `n_ctx_train`). The model goes on **exactly one GPU**, picked as: discrete over integrated, CUDA over other backends (the same card can be exposed by both CUDA and Vulkan), then most free memory. libcommon's own fitter is disabled (`fit_params = false`).
   - **Discrete GPU** (2 GiB safety margin) — on each estimated OOM, the first reduction still possible is applied, cheapest in quality first: `n_ubatch` ÷2 down to 2048 → KV cache F16 → Q8_0 → `mmproj` to CPU → offload up to 30 % of layers to CPU → `n_ctx` −10 % down to the floor (document truncated) → KV Q8_0 → Q4_0 → offload the remaining layers → `n_ubatch` ÷2 down to 512.
   - **Integrated GPU** (512 MiB margin) — all-or-nothing offload (splitting layers triggers a Vulkan scheduler overflow), Q8_0 KV cache, context capped below ~30k tokens (larger contexts are unstable on AMD radv); only `n_ctx` is reduced.
   - **CPU only** — same idea against free system RAM (4 GiB kept for the OS/GUI/OCR): KV F16 → Q8_0, then shrink `n_ctx` down to 4096.
3. **Load** — if the GPU load fails, the backend falls back to a CPU load.

**Inference** (`summarize`):

- **Prompt budget** — the prompt may use `n_ctx − headroom` tokens (headroom capped at half the context). If the prompt is over budget (e.g. `n_ctx` was capped or shrunk by the fitter), the character cap is reduced in proportion to the overflow and the prompt rebuilt, up to 6 times. Truncation drops pages from the end, together with their images. Counting uses the exact inference tokenization, images included — no chars-per-token guess.
- **Lazy `mmproj`** — the vision projector is loaded on the first multimodal request and then **stays resident until `unloadModel()`**. Its placement is decided at model load (the fitter's choice on GPU; any free GPU when the model itself is on CPU). If it cannot be loaded, the request falls back to text-only, without media markers and with the text-only system prompt.
- **Chat template** — the Jinja template from the GGUF metadata is applied with thinking disabled (reasoning models such as Gemma 4 would otherwise emit thought tags that break JSON extraction). The rendered prompt keeps template parts and user-input parts apart.
- **Prompt-injection safety** — template parts are tokenized with special-token parsing, user input (the document text) **without** it, so text inside a PDF cannot inject control tokens such as `<end_of_turn>`.
- **UTF-8 safe detokenization** — tokens are detokenized to raw bytes and accumulated before decoding, since one token can end in the middle of a multi-byte character.
- **Runtime backend discovery** — `ggml_backend_load_all()` selects the best CPU microarch variant and auto-detects CUDA/Vulkan from the deployed libraries.

## OpenAlex Citations

`OpenAlexClient` runs on its own thread and queries the OpenAlex works API (`https://api.openalex.org/works`). For each paper it resolves the OpenAlex work, then fetches the works it cites (`Citations.cites`) and the works citing it (`Citations.cited_by`).

**Triggers**

- **On summarize** — connected to `SummaryController::summaryFinished`.
- **Startup catch-up** — `checkEmptyReferences` scans for `Citations` rows still missing data (covers papers summarized while OpenAlex was unreachable).
- **Project switch** — `DatabaseManager::databaseSwitched` → `onDatabaseSwitched`: the queue is flushed, the in-flight request aborted and the new project rescanned. A generation counter makes late replies and retry timers from the old project no-ops.

**Work resolution** — skipped if the `work_id` is already known. Otherwise a 3-tier search: exact title → semantic search → first-author search. Each candidate is scored by `bestCandidate` (title, then authors, then year, then journal, with fuzzy matching and an author-majority gate). If a candidate with the exact same title is rejected by the author gate, the cascade stops early. A paper with no confident match is marked `lookup_status = 'not_found'` and is not searched again for **30 days**; a match sets `matched`.

**Threading and DB access**

- Papers are processed **one at a time** from a de-duplicated queue; within a paper, the two citation lists are fetched **in parallel**, each with its own buffer.
- The client never touches SQLite from its own thread: every read and write is posted to the main thread, which owns the connection. Each write carries the DB path it was scanned from and is dropped if the active project changed in the meantime.

**Network**

- **Retries** — HTTP 429, 5xx and timeouts are retried up to 3 times with backoff, honouring `Retry-After`. An unreadable reply keeps the paper pending instead of marking it not found.
- **Pagination** — 100 works per page, sorted most-cited first; at most **100 works** are stored per list, while `cites_count` / `cited_by_count` keep OpenAlex's true totals.
- **Optional API key** — `openAlexApiKey` (if set) is sent to raise rate limits; otherwise requests are anonymous.

## Text Post-Processing

`TextFormat` (`textformat.h/.cpp`) cleans LLM output for display. It is shared by `QueryWorker` (RAG answers) and `SummaryFormat::parseJson` (summaries).

- **`latexToUnicode(text, target)`** — models often emit LaTeX despite being told not to, and neither Qt's `MarkdownText` nor the HTML views can render it. Math spans (`$$…$$`, `\[…\]`, `\(…\)`, `$…$`) are rewritten as readable Unicode (`εᵢₙ`, `x²`, `≠`, `a/b`…). Inline `$…$` must hug its content, so currency (`$5 and $10`) is kept. With `Target::Markdown`, code spans are left untouched and `*` / `_` inside formulas are escaped.
- **`escapeMarkdown(text)`** — escapes Markdown syntax so a label (e.g. a paper title in a citation) renders verbatim.

Because summaries are converted at parse time, summaries stored before this conversion existed are also cleaned when displayed or exported.

## Theme System

Themes are JSON files in `ui/themes/` with a Catppuccin-style palette:

```json
{
  "name": "Catppuccin Mocha",
  "primary": "#cba6f7",
  "secondary": "#89b4fa",
  "surface": "#1e1e2e",
  "background": "#18182b",
  "text": "#cdd6f4",
  "...": "..."
}
```

19 themes ship with the application. `ThemeManager` loads the active theme into the `Colors` QML singleton; the app forces the **Fusion** `QQuickStyle` so the manual palette is honored across desktop environments.

## Logging & Diagnostics

`main.cpp` installs a unified logging sink before any model is loaded:

- **Crash log** — `Hodeia.log` in the executable directory, overwritten each run, flushed on every line. Captures the last output before a hard crash even when the app is double-clicked with no console. If the executable directory is not writable (e.g. an all-users install under `Program Files`), the log goes to the per-user data directory instead (`%APPDATA%\Hodeia` on Windows, `~/.local/share/Hodeia` on Linux).
- **Console mirroring** — on Windows a `WIN32` GUI build has no console, so when launched from a shell the app attaches to the parent console (`WriteConsoleW`, UTF-8 code page) and mirrors `qDebug`/`qWarning` there; on Linux/macOS the standard terminal behavior is preserved.
- **llama.cpp log capture** — `llama_log_set` forwards ggml/llama **warnings and errors** into the same sink, so backend-load diagnostics (e.g. "failed to load ggml-cuda.dll") are visible without the model loader's INFO metadata dump. Continuation lines are kept only if the message they continue was kept. The mtmd (multimodal) logger is routed through the same filter (it would otherwise print its DEBUG trace, including the whole prompt, to stderr), and libcommon's own logger is limited to warnings. `common_init()` is deliberately not called, because it would replace this filtered callback.
- **Translation loading** — `loadTranslator` loads `i18n/hodeia_<lang>.qm`; changing the language in Settings reloads the translator and re-loads the QML module so all `qsTr()` calls re-evaluate.

---

## Key Design Decisions

| Decision                                     | Rationale                                                                                             |
| -------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| SQLite + sqlite-vec over dedicated vector DB | Minimal dependencies, single-file database, per-project isolation                                     |
| MuPDF over Poppler                           | Layout-aware structured text extraction with column segmentation                                      |
| Tesseract as fallback, not primary           | OCR is slow; most PDFs have extractable text layers                                                   |
| Pre-compiled llama.cpp (single flat set)     | One combined build (CPU+CUDA+Vulkan), shared ggml-base ABI; runtime backend + CPU microarch selection |
| E5 multilingual embeddings                   | Strong cross-language retrieval without a separate embedding service                                  |
| Chunking at ~1500 chars                      | Balances embedding quality with context window efficiency                                             |
| Unloading models between pipelines           | GPU VRAM is the bottleneck; never hold chat + embedding models simultaneously                         |
| Numbered reference tags in RAG prompts       | Models invent author names / page numbers; tags are resolved to exact citations after generation     |
| OpenAlex as a side channel                   | Citations are public data; fetched async so they never block summarization/RAG                        |
| AGPL-3.0 + Qt (LGPL)                         | The Qt citation + third-party notices are in `NOTICE`                                                 |
