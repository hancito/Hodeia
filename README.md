# Hodeia

**Your personal research assistant — powered by local AI, running entirely on your machine.**

Hodeia helps you manage, summarize, and search through your collection of academic papers. Import PDFs, get AI-generated structured summaries, and ask questions across your entire library — all without sending your data to the cloud.

[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](https://opensource.org/licenses/AGPL-3.0)
[![C++23](https://img.shields.io/badge/C++-23-blue.svg)]
[![Platform: Linux · macOS · Windows](https://img.shields.io/badge/Platform-Linux%20%C2%B7%20macOS%20%C2%B7%20Windows-green.svg)]

> _"Hodeia" — a nod to the Hodeiai (ἄγια), the wandering scholars of antiquity. A fitting name for a tool that roams your library and answers your questions._

---

## Features

- **📄 Import PDF papers** — Drag in individual files or entire folders. Text is extracted with MuPDF's layout-aware engine (columns, paragraphs, dehyphenation); scanned documents fall back to Tesseract OCR automatically.
- **📝 Auto-summarize papers** — Get a structured JSON summary per paper: title, authors, journal, year, topics, research objective, methodology, core findings, critical limitations, a citable summary, and references.
- **📚 Cited literature via OpenAlex** — Pull each paper's references and citing works straight from [OpenAlex](https://openalex.org) — no data leaves your machine to get them.
- **🔍 Ask questions across your library (RAG)** — Query all your papers in natural language and get a synthesized, academic-style answer with inline citations drawn from the most relevant passages (vector search over sqlite-vec + E5 embeddings).
- **🖼️ View papers visually** — Browse page-by-page thumbnails alongside the extracted text, the generated summary, the references, and a LaTeX view. Multimodal LLMs can reason over page images directly.
- **📤 Export summaries** — Export any summary to Markdown or LaTeX.
- **🎨 19 themes** — Catppuccin, Dracula, Gruvbox, Nord, Monokai, GitHub, and more.
- **🔒 100% private** — Everything runs locally. No accounts, no cloud, no telemetry, no data collection.

---

## How It Works

Hodeia is a three-pipeline application:

| Pipeline          | What it does                                                             | Key tech                                 |
| ----------------- | ------------------------------------------------------------------------ | ---------------------------------------- |
| **Ingestion**     | Dedup (MD5), extract text, OCR fallback, render thumbnails, chunk, embed | MuPDF, Tesseract 5, llama.cpp (E5)       |
| **Summarization** | Load the chat model, ask for a structured JSON summary, store it         | llama.cpp (GGUF), OpenAlex for citations |
| **RAG query**     | Embed the question, retrieve top-K chunks, synthesize a cited answer     | sqlite-vec, llama.cpp                    |

Each project is an isolated workspace with its own SQLite database, PDFs, and thumbnails. Models are loaded and unloaded as needed so you never hold two in VRAM at once.

Full architecture, pipeline detail, and the database schema live in [docs/TECHNICAL.md](docs/TECHNICAL.md).

---

## Tech Stack

| Component           | Technology                                |
| ------------------- | ----------------------------------------- |
| UI framework        | Qt 6.10+ (Qt Quick / QML, QuickControls2) |
| Language            | C++23                                     |
| Build system        | CMake 3.25+                               |
| Local LLM inference | llama.cpp (GGUF models)                   |
| Multimodal          | mtmd with mmproj vision projectors        |
| Remote LLM          | OpenAI-compatible REST API                |
| PDF parsing         | MuPDF                                     |
| OCR                 | Tesseract 5.5.2 + Leptonica 1.87.0        |
| Database            | SQLite3 (embedded, WAL mode)              |
| Vector search       | sqlite-vec                                |
| Embeddings          | multilingual-e5-small (~384 dims)         |
| GPU backends        | CUDA, Vulkan, CPU (auto-detected)         |

---

## Supported Backends

Hodeia can run models **locally** or against a **remote** endpoint — you can mix both.

| Backend               | Description                                                     |
| --------------------- | --------------------------------------------------------------- |
| **llama.cpp (local)** | Run GGUF models directly on your GPU (CUDA / Vulkan) or CPU     |
| **Remote API**        | Connect to Ollama, LM Studio, or any OpenAI-compatible endpoint |

To add a remote backend, open **Settings → Backends** and enter a name, URL, and (optionally) an API key.

### Model placement

Models live under `~/.local/share/Hodeia/Models/`:

```
Models/
├── Summarizers/        # Chat models used for summarization + RAG
│   └── <model_name>/
│       ├── model.gguf
│       └── mmproj.gguf  # optional — vision projector for multimodal
└── Embedders/          # Embedding model used for RAG retrieval
    └── <model_name>/
        └── model.gguf
```

Any GGUF chat model works (Llama, Mistral, Qwen, etc.). For RAG you need an embedding model too — Hodeia ships expecting `multilingual-e5-small`.

### Optional: OpenAlex API key

Citations are pulled from OpenAlex, which is free without a key. You can optionally set an API key in **Settings** (or `config.json` → `openAlexApiKey`) to raise rate limits.

---

## Quick Start

### 1. Install

Build from source (Linux, macOS, Windows — see [docs/BUILD_REQUIREMENTS.md](docs/BUILD_REQUIREMENTS.md)).

### 2. Load a model

Place a GGUF chat model in `~/.local/share/Hodeia/Models/Summarizers/<name>/model.gguf` and an embedding model in `Models/Embedders/<name>/model.gguf`.

### 3. Create a project

File → **New Project**. Each project is an isolated workspace with its own database.

### 4. Import papers

Use the toolbar to add PDFs or whole folders. Hodeia extracts text, renders thumbnails, chunks, and embeds them automatically.

### 5. Summarize & query

- **Summarize** — select papers in the list and hit the summarize button.
- **Ask questions** — open the RAG window and ask anything across your library. Answers come back with citations to the source pages.

---

## Building from Source

Hodeia is a C++/Qt application. The build is modular: a slim top-level `CMakeLists.txt` detects the platform and routes to `cmake/Platform<OS>.cmake`, which sets up third-party linking, GPU-backend discovery, and post-build deployment.

### Prerequisites

- **CMake** ≥ 3.25
- **Qt 6** ≥ 6.10 (Quick, QuickControls2, Concurrent, Network)
- **C++23-compatible compiler** — Clang/LLVM (Linux/macOS) or MSVC (Windows)
- **Ninja** (Linux/macOS) or **Visual Studio 2022** (Windows)
- **vcpkg** (Windows only) — provides MuPDF, Tesseract, Leptonica, Vulkan loader
- **llama.cpp** + **sqlite-vec** — pre-built DLLs on Windows; compiled on Linux/macOS (see per-platform instructions below)

### Linux

The in-tree static libs under `external/lib/linux/` are pre-compiled. Install the system packages for your distribution, then:

```bash
# Arch / Manjaro
sudo pacman -S clang cmake ninja qt6-base qt6-declarative patchelf pkgconf \
  vulkan-icd-loader libomp make git wget

# Debian / Ubuntu
sudo apt install -y clang cmake ninja-build patchelf pkg-config git wget \
  libclang-rt-dev libomp-dev qt6-base-dev qt6-declarative-dev libvulkan1

# Fedora
sudo dnf install clang cmake ninja qt6-qtbase qt6-qtquickcontrols2 \
  patchelf pkgconf-pkg-config libomp git wget vulkan-loader
```

```bash
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

> CMake auto-detects the available GPU backend (Vulkan or CUDA). Force one with
> `-DLLAMA_FORCE_BACKEND=cpu` | `cuda` | `vulkan` | `all`.

### macOS (Apple Silicon)

Requires Xcode CLT ≥ 15, CMake, Ninja, LLVM/Clang ≥ 18, Qt 6.10+, and libomp. You must compile llama.cpp (Metal), MuPDF, Tesseract, Leptonica, and sqlite-vec as `.dylib`/`.a` into `external/lib/mac/` — full step-by-step in [docs/BUILD_REQUIREMENTS.md](docs/BUILD_REQUIREMENTS.md).

### Windows (Visual Studio / MSVC)

Native **MSVC** build (Visual Studio 2022) with **vcpkg** providing MuPDF, Tesseract, Leptonica, and the Vulkan loader. llama.cpp is used from the pre-built MSVC release DLLs, and sqlite-vec's `vec0.dll` is auto-downloaded. One entry point does it all:

```powershell
# from the repo root, in a VS Developer Prompt
.\build_windows.ps1 -LlamaSourceDir "D:\Developpement\Windows" `
                    -QtPrefix "C:\Qt\6.8.0\msvc2022_64"
```

The script stages the llama.cpp DLLs, fetches `vec0.dll`, runs `vcpkg install`, then configures (VS 2022, x64) + builds. All three backends (CPU + Vulkan + CUDA) are shipped; the app auto-detects the best at runtime. Full step-by-step in [docs/BUILD_REQUIREMENTS.md](docs/BUILD_REQUIREMENTS.md).

### GPU backend selection

At runtime the app calls `ggml_backend_load_all()` and auto-detects the best available backend (CUDA > Vulkan > CPU) from whichever DLLs are present.

- **Linux / macOS** — CMake auto-detects CUDA and Vulkan at build time. Override with `-DLLAMA_FORCE_BACKEND=cpu` | `cuda` | `vulkan` | `all`.
- **Windows** — all three backends (CPU + Vulkan + one CUDA version) are staged and shipped in a single binary; selection is fully runtime. Pick the CUDA version to ship with `-CudaVersion 12.4` (default) or `13.4` in `build_windows.ps1`.

### Post-build deployment

Each platform's CMake module runs the relevant toolchain automatically:

- **Linux** — copies themes/fonts/tessdata, deploys llama.cpp `.so` with symlinks, sets `$ORIGIN` RUNPATH via `patchelf`, copies `vec0.so`.
- **Windows** — runs `windeployqt`, ships all llama.cpp `.dll` (CPU variants + `ggml-cuda.dll` + `ggml-vulkan.dll`), CUDA runtime DLLs, vcpkg `vulkan-1.dll`, Tesseract/Leptonica runtime DLLs, `vec0.dll`, and `libomp.dll`.
- **macOS** — runs `macdeployqt`, ships dylibs + `vec0.dylib` + `libomp.dylib` into the `.app` bundle.

---

## Platform Support

| Platform              | Status          | Notes                                                                |
| --------------------- | --------------- | -------------------------------------------------------------------- |
| Linux                 | ✅ Supported    | Pre-compiled `external/lib/linux/` shipped; GPU auto-detect          |
| macOS (Apple Silicon) | 🚧 Experimental | Metal backend; libs must be built per BUILD_REQUIREMENTS             |
| Windows (MSVC)        | 🚧 Experimental | Visual Studio + vcpkg; CPU/Vulkan/CUDA backends, runtime auto-detect |

---

## Projects & Data Layout

Work is organized into **projects** — isolated workspaces, each with its own database, imported PDFs, and summaries. Create as many as you need (one per research topic, course, or grant).

```
~/.local/share/Hodeia/
├── config.json                 # active theme, backends, models, RAG settings
├── hardware.json               # cached hardware detection
├── Projects/
│   └── <project_name>/
│       ├── <project_name>.db   # SQLite database (WAL mode)
│       ├── PDF/                # imported PDFs (copied, not linked)
│       └── IMG/                # page thumbnails
└── Models/
    ├── Summarizers/            # chat models (GGUF, optional mmproj)
    └── Embedders/              # embedding model (GGUF)
```

### Configuration

`config.json` controls the active theme, backend list, model selection, RAG settings (`topK`, `maxPages`, `useVisualContext`), and the optional OpenAlex API key. See [docs/TECHNICAL.md](docs/TECHNICAL.md#configuration) for the full schema.

---

## Contributing

Contributions are welcome. A few pointers:

- Follow the existing style: **C++23**, Qt Quick for UI, modular CMake.
- Keep third-party libs in `external/` (never vendor into `src/` or `include/`).
- Platform-specific build logic belongs in `cmake/Platform<OS>.cmake`.
- New features should keep the three-pipeline structure (ingestion / summarization / RAG) intact.
- Run the linter and build before submitting. The build auto-parallelizes across cores.

See [docs/TECHNICAL.md](docs/TECHNICAL.md) for the architecture, pipeline details, and database schema.

---

## License

**GNU Affero General Public License v3.0** — see [LICENSE](LICENSE).

Hodeia is free and open source. You are free to use, modify, and share it under the terms of the AGPL-3.0.

---

## Links

- **Technical documentation** — [docs/TECHNICAL.md](docs/TECHNICAL.md)
- **Build requirements (Linux / macOS / Windows)** — [docs/BUILD_REQUIREMENTS.md](docs/BUILD_REQUIREMENTS.md)
- **OpenAlex** — [https://openalex.org](https://openalex.org) (citation data source)
- **llama.cpp** — [https://github.com/ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp)
- **MuPDF** — [https://mupdf.com](https://mupdf.com)
- **Tesseract** — [https://github.com/tesseract-ocr/tesseract](https://github.com/tesseract-ocr/tesseract)
- **sqlite-vec** — [https://github.com/asg017/sqlite-vec](https://github.com/asg017/sqlite-vec)
