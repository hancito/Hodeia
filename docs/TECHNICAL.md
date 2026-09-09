# Technical Documentation

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                   QML UI Layer                          │
│  Main.qml │ RagWindow.qml │ DisplayPane │ SummaryDbPane │
│  RagDbPane │ QueryPane │ MyMenuBar │ MyToolBar          │
│  BackendWindow │ Colors.qml │ Fonts.qml                  │
├─────────────────────────────────────────────────────────┤
│              C++ Controllers (QQML-invokeable)          │
│  EngineManager │ SummaryThreadManager │ QueryController  │
│  ConfigManager │ ThemeManager │ HardwareDetector         │
├─────────────────────────────────────────────────────────┤
│                  Worker Threads                         │
│  IngestionEngine │ Summarizer │ QueryWorker              │
│  (QtConcurrent + QThreads)                              │
├─────────────────────────────────────────────────────────┤
│                 Backend Layer                           │
│  LocalBackend (llama.cpp libcommon)                     │
│  RemoteBackend (QNetworkAccessManager / HTTP)           │
├─────────────────────────────────────────────────────────┤
│                 Extraction Layer                        │
│  MuPDF (text + rendering) │ Tesseract 5 (OCR fallback)  │
├─────────────────────────────────────────────────────────┤
│                   Data Layer                            │
│  SQLite3 (embedded) │ sqlite-vec (vec0 vtable)          │
│  SummaryProxyModel │ QueryProxyModel                    │
└─────────────────────────────────────────────────────────┘
```

## Tech Stack

| Component           | Technology                                              |
| ------------------- | ------------------------------------------------------- |
| UI Framework        | Qt 6.10+ (Qt Quick / QML, QuickControls2, Fusion style) |
| Language            | C++23                                                   |
| Build System        | CMake 3.25+                                             |
| Local LLM Inference | llama.cpp (GGUF models, libcommon)                      |
| Multimodal          | mtmd (llama.cpp) with mmproj projectors                 |
| Remote LLM          | OpenAI-compatible REST API                              |
| PDF Parsing         | MuPDF (layout-aware structured text + page rendering)   |
| OCR                 | Tesseract 5.5.2 + Leptonica 1.87.0                      |
| Database            | SQLite3 (embedded, WAL mode)                            |
| Vector Search       | sqlite-vec (`vec0` virtual table)                       |
| Embedding Model     | multilingual-e5-small (~384 dims, via llama.cpp)        |
| GPU Backends        | CUDA, Vulkan, CPU (auto-detected)                       |
| Threading           | Qt Concurrent, QThread, QThreadPool                     |

## Project Structure

```
Paperize_CPP/
├── CMakeLists.txt                  # Slim build entry: project, Qt, platform routing
├── cmake/                          # Modular CMake scripts
│   ├── CompilerFlags.cmake         # C++23, warnings, optimisation, LTO (opt-in)
│   ├── PlatformLinux.cmake         # Static .a + system libs, GPU discovery, patchelf deploy
│   ├── PlatformWindows.cmake       # MSYS2 (ucrt64) shared-DLL discovery, windeployqt
│   └── PlatformMac.cmake           # .dylib map, Metal, install_name_tool, macdeployqt
├── resources.qrc                   # (legacy, unused — qt_add_resources uses explicit files)
├── build_ocr.sh                    # Tesseract + Leptonica build script (Linux)
├── README.md                       # User-facing README
├── docs/
│   └── TECHNICAL.md               # This file
│
├── include/                        # Public C++ headers (flat, isolated from external/)
│   └── <thememanager.h, database.h, localbackend.h, ...>
│
├── src/                            # C++ source files + target definitions
│   ├── CMakeLists.txt              # add_library(PaperizeEngine) + qt_add_executable(Paperize)
│   ├── main.cpp                    # Entry point: QML engine + C++ object registration
│   ├── configmanager.cpp           # JSON config, project CRUD, hardware detection
│   ├── database.cpp                # SQLite DB init, table/schema creation, project switch
│   ├── ingestionengine.cpp         # PDF extraction (MuPDF + Tesseract), chunking
│   ├── enginemanager.cpp           # Thread controller for PDF ingestion pipeline
│   ├── sqlitedb.cpp                # SQLite wrapper (open, exec, prepare, transactions)
│   ├── sqlitevec.cpp               # Vector embeddings (E5 via llama.cpp), vec0 CRUD/search
│   ├── localbackend.cpp            # Local llama.cpp backend (model loading, GPU fitting)
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
│   ├── hardwaredetector.cpp        # CPU/GPU/RAM/NUMA/NPU detection (cross-platform)
│   ├── thememanager.cpp            # JSON-based theme loading
│   └── helpers.cpp                 # ModelHelper, summary parsing, export (MD/LaTeX)
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
│   │   ├── llamacpp/               # llama.cpp headers
│   │   ├── mupdf/                  # MuPDF headers
│   │   ├── Sqlite3/                # SQLite3 headers
│   │   ├── SqliteVec/              # sqlite-vec headers
│   │   ├── tesseract/              # Tesseract headers
│   │   └── leptonica/              # Leptonica headers
│   ├── sqlite3/
│   │   └── sqlite3.c               # SQLite amalgamation (compiled as C)
│   └── lib/
│       ├── linux/                  # static .a + llama.cpp .so (cpu/cuda/vulkan) + sqlitevec/
│       ├── mac/                    # static .a + llama.cpp .dylib (cpu/metal) + sqlitevec/
│       └── windows/                # llama.cpp .dll (cpu/cuda/vulkan) + sqlitevec/
│                                   # (MuPDF/Tesseract/Leptonica come from MSYS2 on Windows)
│
├── Resources/                      # Icons, Tesseract data
│   └── tessdata/
│       └── eng.traineddata
│
└── Fonts/                          # Custom application fonts
    ├── Google_Sans/
    ├── Google_Sans_Code/
    └── Outfit/
```

## Prerequisites

- **CMake** ≥ 3.25
- **Qt 6** ≥ 6.10 (Quick, QuickControls2, Concurrent, Network modules)
- **C++23-compatible compiler** (GCC 13+, Clang 17+)
- **Ninja** (recommended) or Make
- **patchelf** (Linux, for RPATH manipulation)
- **MuPDF** development libraries (`libmupdf`, `libmupdf-third`, `libmupdf-threads`)
- **OpenMP** (optional, for ggml CPU parallelism)

### OCR Dependencies (optional)

To enable the Tesseract OCR fallback, build Leptonica and Tesseract from source:

```bash
# Place leptonica-1.87.0 and tesseract-5.5.2 sources in a shared directory
./build_ocr.sh [/path/to/source/root]
```

The script builds both as static libraries and installs artefacts into `external/`.

## Build

The build is modular: the top-level `CMakeLists.txt` detects the platform and
includes the matching `cmake/Platform<OS>.cmake`, which sets up third-party
linking, GPU-backend discovery, and POST_BUILD deployment. Target definitions
live in `src/CMakeLists.txt`.

### Standard Build (Linux)

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build . -j$(nproc)
```

Linux links the in-tree **static** third-party libs (`external/lib/linux/*.a`)
plus system image-codec/shared libs.

### Windows Build (MSYS2, ucrt64)

Windows links **shared DLLs** from MSYS2 for MuPDF / Tesseract / Leptonica
(llama.cpp still comes from `external/lib/windows/llamacpp/`). In the ucrt64
shell:

```bash
pacman -S mingw-w64-ucrt-x86_64-mupdf \
          mingw-w64-ucrt-x86_64-tesseract-ocr \
          mingw-w64-ucrt-x86_64-leptonica
# optional — use MSYS2's Qt6 instead of a separate Qt install:
pacman -S mingw-w64-ucrt-x86_64-qt6-base \
          mingw-w64-ucrt-x86_64-qt6-declarative \
          mingw-w64-ucrt-x86_64-qt6-shadertools
```

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -G Ninja \
  -DPAPERIZE_MINGW_PREFIX=C:/msys64/ucrt64 \
  [-DPAPERIZE_QT_FROM_MSYS2=ON]
cmake --build .
```

`PAPERIZE_MINGW_PREFIX` (or the `MINGW_PREFIX` env var) points CMake at the
MSYS2 prefix; `PAPERIZE_QT_FROM_MSYS2=ON` makes `find_package(Qt6)` resolve
from MSYS2 and ships the Qt6 DLLs from there instead of via a Qt installer.

### GPU Backend Selection

The build system auto-detects CUDA and Vulkan shared libraries. Override with:

```bash
cmake .. -DLLAMA_FORCE_BACKEND=cpu      # CPU only
cmake .. -DLLAMA_FORCE_BACKEND=cuda     # Force CUDA
cmake .. -DLLAMA_FORCE_BACKEND=vulkan   # Force Vulkan
cmake .. -DLLAMA_FORCE_BACKEND=all      # All available backends
```

### Post-Build Deployment

Each platform handles deployment in its `cmake/Platform<OS>.cmake`:

- **Linux** — copies themes/fonts/tessdata, deploys llama.cpp `.so` with
  symlinks, sets `$ORIGIN` RUNPATH via `patchelf`, copies `vec0.so`.
- **Windows** — copies themes/fonts/tessdata, llama.cpp `.dll` + MSYS2 runtime
  DLLs, `vec0.dll`, `libomp.dll`, and runs `windeployqt` (or ships the MSYS2
  Qt6 DLLs).
- **macOS** — copies dylibs, sets `@executable_path` install names, ships
  `vec0.dylib` + `libomp.dylib`, and runs `macdeployqt`.

## Core Pipelines

### PDF Ingestion Pipeline

1. **File hashing** — MD5 computed per file for deduplication (parallelized via Qt Concurrent)
2. **Text extraction** — MuPDF's structured-text engine performs DFS traversal with column segmentation, paragraph breaks, dehyphenation, and clip preservation
3. **OCR fallback** — If a page yields < 100 characters, Tesseract 5 runs at 300 DPI grayscale. APIs are cached per-thread to minimize initialization overhead
4. **Thumbnail generation** — 150 DPI page images rendered as PNGs
5. **Chunking** — Full text split into ~1500-character segments with 200-character overlap; page mapping preserved
6. **Embedding** — Batches of 64 chunks sent through the E5 model; vectors stored in the `vec0` virtual table
7. **Batch processing** — 10 PDFs per batch with incremental progress reporting and memory cleanup

### Summarization Pipeline

1. All text chunks and page images loaded for the selected paper
2. Embedding model unloaded to free GPU memory
3. Chat model loaded (with mmproj if multimodal)
4. Structured prompt sent requesting a JSON response containing: title, authors, journal, year, topics, research objective, methodology, core findings, critical limitations, citable summary, references
5. Response parsed and stored in `Summaries` table
6. Chunk metadata updated (clean title, citation)
7. Chat model unloaded, embedding model reloaded

### RAG Query Pipeline

1. User query embedded and compared against `vec0` index (configurable top-K)
2. Retrieved chunks grouped and deduplicated by `(paper_hash, page)`
3. Page images loaded (if multimodal enabled)
4. Structured prompt built with page contexts, citation markers, and user query
5. LLM synthesizes a cross-paper response with academic-style citations
6. Result persisted in `Queries` table

## Database Schema

### `Summaries` table

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
| `summary_json` | BLOB                | Full structured summary as JSON |
| `date`         | TEXT                | ISO 8601 timestamp              |

### `Queries` table

| Column     | Type                | Description                      |
| ---------- | ------------------- | -------------------------------- |
| `id`       | INTEGER PRIMARY KEY | Auto-increment                   |
| `query`    | TEXT                | User's natural language question |
| `response` | TEXT                | LLM-generated answer             |
| `date`     | TEXT                | ISO 8601 timestamp               |
| `llmodel`  | TEXT                | Model used                       |

### `chunks` (sqlite-vec `vec0` virtual table)

| Column        | Type     | Description                         |
| ------------- | -------- | ----------------------------------- |
| `rowid`       | INTEGER  | Auto-increment                      |
| `embedding`   | float[N] | 384-dim vector (E5)                 |
| `paper_hash`  | TEXT     | MD5 of source PDF                   |
| `page`        | INTEGER  | 0-based page number                 |
| `chunk_idx`   | INTEGER  | Chunk index within page             |
| `chunk_text`  | TEXT     | Raw text segment (~1500 chars)      |
| `citation`    | TEXT     | Generated citation string           |
| `clean_title` | TEXT     | Paper title for citation formatting |

## Configuration

### `config.json` (`~/.local/share/Paperize/config.json`)

```json
{
  "activeThemeIndex": 0,
  "lastProject": "Default",
  "activeBackend": "Llama.cpp",
  "activeModel": "qwq32b",
  "modelTemperature": 0.1,
  "useVisualContext": true,
  "maxPages": 50,
  "topK": 20,
  "backendList": [
    { "name": "Llama.cpp", "url": "", "api_key": "" },
    { "name": "Ollama", "url": "http://localhost:11434/v1", "api_key": "" }
  ]
}
```

### Directory Layout

```
~/.local/share/Paperize/
├── config.json
├── hardware.json                    # Cached hardware detection results
├── Projects/
│   └── <project_name>/
│       ├── <project_name>.db        # SQLite database
│       ├── PDF/                     # Copied PDF files
│       └── IMG/                     # Page thumbnails
└── Models/
    ├── Summarizers/
    │   └── <model_name>/
    │       ├── model.gguf
    │       └── mmproj.gguf          # Optional: vision projector
    └── Embedders/
        └── <model_name>/
            └── model.gguf
```

## LocalBackend Details

The `LocalBackend` wraps llama.cpp's `common` library:

- **Adaptive GPU fitting** — Iteratively adjusts `n_gpu_layers`, `n_ctx`, KV cache type, and `n_ubatch` to fit available VRAM
- **Integrated GPU support** — All-or-nothing layer offload strategy to avoid Vulkan scheduler overflow
- **Lazy mmproj loading** — Vision projectors loaded on-demand when multimodal context is needed
- **Chat template parsing** — Jinja templates extracted from GGUF metadata
- **Hybrid offloading** — Manual GPU + CPU layer split when VRAM is constrained

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

19 themes ship with the application. The `ThemeManager` loads the active theme into the `Colors` QML singleton.

## Hardware Detection

The `HardwareDetector` probes:

- CPU model, core count, thread count
- Total and available RAM
- GPU(s) — vendor, model, VRAM (via `nvidia-smi`, Vulkan enumerate, or sysfs)
- NUMA node topology
- NPU presence (Apple Silicon)

Results are cached in `hardware.json` and used by the `LocalBackend` to make adaptive loading decisions.

## Key Design Decisions

| Decision                                     | Rationale                                                                          |
| -------------------------------------------- | ---------------------------------------------------------------------------------- |
| SQLite + sqlite-vec over dedicated vector DB | Minimal dependencies, single-file database, per-project isolation                  |
| MuPDF over Poppler                           | Layout-aware structured text extraction with column segmentation                   |
| Tesseract as fallback, not primary           | OCR is slow; most PDFs have extractable text layers                                |
| Pre-compiled llama.cpp .so files             | Avoids building llama.cpp from source; supports multiple GPU backends side by side |
| E5 multilingual embeddings                   | Strong cross-language retrieval without a separate embedding service               |
| Chunking at ~1500 chars                      | Balances embedding quality with context window efficiency                          |
| Unloading models between pipelines           | GPU VRAM is the bottleneck; never hold chat + embedding models simultaneously      |
