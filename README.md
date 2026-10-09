# Hodeia

**Your personal research assistant — powered by local AI, running entirely on your machine.**

Hodeia helps you manage, summarize, and search through your collection of academic papers. Import PDFs, get AI-generated structured summaries, pull their reference lists, and ask questions across your entire library — all without sending your data to the cloud.

[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](https://opensource.org/licenses/AGPL-3.0)
[![C++23](https://img.shields.io/badge/C++-23-blue.svg)]
[![Platform: Linux · Windows](https://img.shields.io/badge/Platform-Linux%20%C2%B7%20Windows-green.svg)]

> _"Hodeia" — a nod to the Hodegoi (ἦδογοι / ὑδρογέες), the wandering scholars of antiquity. A fitting name for a tool that roams your library and answers your questions._

---

## Features

- **📄 Import PDF papers** — Drag in individual files or entire folders. Text is extracted with MuPDF's layout-aware engine (columns, paragraphs, dehyphenation); scanned documents fall back to Tesseract OCR automatically.
- **📝 Auto-summarize papers** — Get a structured summary per paper: title, authors, journal, year, topics, research objective, methodology, core findings, critical limitations, and a citable abstract.
- **📚 References & citations via OpenAlex** — Pull each paper's references (works it cited) and the works that cite it straight from [OpenAlex](https://openalex.org), formatted as APA-style bibliographies. No data leaves your machine except these public lookups.
- **🔍 Ask questions across your library (RAG)** — Query all your papers in natural language and get a synthesized, academic-style answer with inline citations drawn from the most relevant passages (vector search over sqlite-vec + E5 embeddings).
- **🖼️ View papers visually** — Browse page-by-page thumbnails alongside the extracted text, the generated summary, the references, and a LaTeX view. Multimodal LLMs can reason over page images directly.
- **📤 Export summaries** — Export any summary to Markdown or LaTeX.
- **🎨 19 themes** — Catppuccin, Dracula, Gruvbox, Nord, Monokai, GitHub, and more.
- **🌍 Localized UI** — English and French.
- **🔒 100% private** — Everything runs locally. No accounts, no cloud, no telemetry, no data collection.

---

## How It Works

Hodeia is a three-pipeline application:

| Pipeline          | What it does                                                             | Key tech                             |
| ----------------- | ------------------------------------------------------------------------ | ------------------------------------ |
| **Ingestion**     | Dedup (MD5), extract text, OCR fallback, render thumbnails, chunk, embed | MuPDF, Tesseract, llama.cpp (E5)     |
| **Summarization** | Load the chat model, ask for a structured summary, store it              | llama.cpp (GGUF), OpenAlex for cites |
| **RAG query**     | Embed the question, retrieve top-K chunks, synthesize a cited answer     | sqlite-vec, llama.cpp                |

Each **project** is an isolated workspace with its own SQLite database, PDFs, and thumbnails. Models are loaded and unloaded as needed so you never hold two in VRAM at once.

Full architecture, pipeline detail, database schema, and per-platform build instructions live in [docs/TECHNICAL.md](docs/TECHNICAL.md).

---

## Tech Stack

| Component           | Technology                                        |
| ------------------- | ------------------------------------------------- |
| UI framework        | Qt 6.10+ (Qt Quick / QML, QuickControls2)         |
| Language            | C++23                                             |
| Build system        | CMake 3.25+ (Ninja on all platforms)              |
| Local LLM inference | llama.cpp (GGUF models)                           |
| Multimodal          | mtmd with mmproj vision projectors                |
| Remote LLM          | OpenAI-compatible REST API (Ollama, LM Studio, …) |
| PDF parsing         | MuPDF                                             |
| OCR                 | Tesseract + Leptonica                             |
| Database            | SQLite3 (embedded, WAL mode)                      |
| Vector search       | sqlite-vec (`vec0` virtual table)                 |
| Embeddings          | multilingual-e5-small (384 dims)                  |
| Citations           | OpenAlex (free, optional API key)                 |
| GPU backends        | CUDA, Vulkan, CPU (auto-detected)                 |

---

## Supported Backends

Hodeia can run models **locally** or against a **remote** endpoint — you can mix both.

| Backend               | Description                                                     |
| --------------------- | --------------------------------------------------------------- |
| **llama.cpp (local)** | Run GGUF models directly on your GPU (CUDA / Vulkan) or CPU     |
| **Remote API**        | Connect to Ollama, LM Studio, or any OpenAI-compatible endpoint |

To add a remote backend, open **Settings → Backends** and enter a name, URL, and (optionally) an API key.

### Model placement

Models live under `~/.local/share/Hodeia/Models/` (per-project data lives beside the project):

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

Any GGUF chat model works (Llama, Mistral, Qwen, etc.). A bundled `multilingual-e5-small` embedder is seeded into the data directory on first run.

### Optional: OpenAlex API key

Citations are pulled from OpenAlex, which is free without a key. You can optionally set an API key in **Settings** to raise rate limits.

---

## Quick Start

1. **Build from source** — Linux or Windows (macOS is **not yet available** — see [Platform Support](#platform-support)). One entry-point script per platform stages the dependencies and builds; see [docs/TECHNICAL.md](docs/TECHNICAL.md#building-from-source).
2. **Load a model** — Place a GGUF chat model in `Models/Summarizers/<name>/model.gguf`. The embedder is bundled and seeded automatically.
3. **Create a project** — File → **New Project**. Each project is an isolated workspace with its own database.
4. **Import papers** — Use the toolbar to add PDFs or whole folders. Hodeia extracts text, renders thumbnails, chunks, embeds, and fetches references automatically.
5. **Summarize & query** — Select papers and hit **Summarize**, or open the RAG window and ask anything across your library. Answers come back with citations to the source pages.

---

## Building from Source

Hodeia is a C++/Qt application with a modular build: a slim top-level `CMakeLists.txt` detects the platform and routes to `cmake/Platform<OS>.cmake`, which sets up third-party linking, GPU-backend discovery, and post-build deployment.

### Prerequisites (all platforms)

- **CMake** ≥ 3.25
- **Qt 6** ≥ 6.10 (Quick, QuickControls2, Concurrent, Network)
- **C++23-compatible compiler** — Clang/LLVM (Linux / macOS) or MSVC (Windows)
- **Ninja** (Linux / macOS / Windows)
- **vcpkg** (Windows only) — provides MuPDF, Tesseract, Leptonica, and the Vulkan loader

Third-party libraries are staged into `external/` per platform:

- **Linux** — pre-compiled in-tree static libs + the llama.cpp `.so` set (built once for CPU + CUDA + Vulkan).
- **Windows** — pre-built MSVC llama.cpp DLLs + `vec0.dll`, with MuPDF / Tesseract / Leptonica / Vulkan from vcpkg.
- **macOS** — you compile llama.cpp (Metal), MuPDF, Tesseract, Leptonica, and sqlite-vec into `external/lib/mac/`.

### Per-platform entry points

| Platform | Script (from the repo root)                                                       | What it does                                                                                                                                        |
| -------- | --------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| Linux    | `scripts/Linux/get_depencies.sh` → then CMake                                     | Clones/locates sources, then `scripts/Linux/build_{llama,mupdf,tesseract,sqlitevec}.sh` stage each dependency. Build the app with CMake + Ninja.    |
| Windows  | `scripts/Windows/build-dependencies.ps1`, then `scripts/Windows/build-app.ps1`    | Stages llama.cpp + sqlite-vec, runs `vcpkg install`, configures (MSVC + Ninja) + builds. All three backends ship; best is auto-detected at runtime. |
| macOS    | **Not yet available** — see [TECHNICAL.md](docs/TECHNICAL.md#macos-apple-silicon) | Experimental & untested. When released: compile the five dependencies, then CMake + Ninja; `macdeployqt` bundles everything into `Hodeia.app`.      |

> The Windows scripts live in `scripts/Windows/`, the Linux scripts in `scripts/Linux/`.
> Run the PowerShell scripts from a **VS Developer Prompt** (MSVC toolchain on PATH).

### GPU backend selection

At runtime the app calls `ggml_backend_load_all()` and auto-detects the best available backend (CUDA > Vulkan > CPU) from whichever libraries are present next to the binary.

- **Linux** — CMake auto-detects CUDA or Vulkan at build time. Override with `-DLLAMA_FORCE_BACKEND=cpu|cuda|vulkan`.
- **Windows** — all three backends (CPU + Vulkan + CUDA) ship in one binary; selection is fully runtime.
- **macOS** — Metal only (the only GPU backend on Apple Silicon).

### Post-build deployment

Each platform's CMake module deploys the runtime automatically:

- **Linux** — copies themes/fonts/tessdata + the embedder model, deploys the llama.cpp `.so` set (core + every CPU variant + CUDA/Vulkan) with unversioned symlinks, sets `$ORIGIN` RUNPATH via `patchelf`, copies `vec0.so`.
- **Windows** — runs `windeployqt`, ships the llama.cpp DLLs (CPU variants + `ggml-cuda` + `ggml-vulkan` + `libomp`), CUDA runtime DLLs, `vulkan-1.dll`, Tesseract/Leptonica runtime DLLs, `vec0.dll`, and `Hodeia.exe`.
- **macOS** — runs `macdeployqt`, sets `@executable_path` install names, ships the llama.cpp dylibs + `vec0.dylib` + `libomp.dylib` into the `.app` bundle.

Full per-platform build steps (package installs, exact CMake flags, macOS source builds) are in [docs/TECHNICAL.md](docs/TECHNICAL.md#building-from-source).

---

## Platform Support

| Platform              | Status                   | Notes                                                                                                                                                         |
| --------------------- | ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Linux                 | ✅ Supported             | In-tree static libs + llama.cpp `.so`; GPU auto-detect                                                                                                        |
| Windows (MSVC)        | ✅ Supported             | Native MSVC + vcpkg; CPU/Vulkan/CUDA, runtime auto-detect                                                                                                     |
| macOS (Apple Silicon) | 🚧 **Not yet available** | Experimental & untested — the app is not yet released for macOS. Metal backend; the CMake build mirrors the (tested) Linux schema but has not been validated. |

---

## Projects & Data Layout

Work is organized into **projects** — isolated workspaces, each with its own database and summaries. Create as many as you need (one per research topic, course, or grant). PDFs are ingested **in place** (referenced from their original location, not copied into the project); only page thumbnails are stored in the project's `IMG/` folder.

```
~/.local/share/Hodeia/
├── config.json                 # active theme, backends, models, RAG settings, language
├── Projects/
│   └── <project_name>/
│       ├── <project_name>.db   # SQLite database (WAL mode)
│       └── IMG/                # page thumbnails (PDFs are referenced in place, not copied)
└── Models/
    ├── Summarizers/            # chat models (GGUF, optional mmproj)
    └── Embedders/              # embedding model (GGUF)
```

`config.json` controls the active theme, backend list, model selection, RAG settings (`topK`, `maxPages`, `visualMode`), the optional OpenAlex API key, and the UI language. See [docs/TECHNICAL.md](docs/TECHNICAL.md#configuration) for the full schema.

> On Windows the data directory is `%LOCALAPPDATA%\Hodeia` (the Qt `AppDataLocation`).

---

## Contributing

Contributions are welcome. A few pointers:

- Follow the existing style: **C++23**, Qt Quick for UI, modular CMake.
- Keep third-party libs in `external/` (never vendor into `src/` or `include/`).
- Platform-specific build logic belongs in `cmake/Platform<OS>.cmake`.
- New features should keep the three-pipeline structure (ingestion / summarization / RAG) intact.
- Build on your platform before submitting — the build auto-parallelizes across cores.

See [docs/TECHNICAL.md](docs/TECHNICAL.md) for the architecture, pipeline details, and database schema.

---

## License

**GNU Affero General Public License v3.0** — see [LICENSE](LICENSE).

Hodeia is free and open source. You are free to use, modify, and share it under the terms of the AGPL-3.0.

Hodeia is built with **Qt** (LGPL 3.0) and includes other third-party open-source components. The required citation, acknowledgments, and licensing notices for those are in [NOTICE](NOTICE).

---

## Links

- **Technical documentation** — [docs/TECHNICAL.md](docs/TECHNICAL.md)
- **OpenAlex** — [https://openalex.org](https://openalex.org) (citation data source)
- **llama.cpp** — [https://github.com/ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp)
- **MuPDF** — [https://mupdf.com](https://mupdf.com)
- **Tesseract** — [https://github.com/tesseract-ocr/tesseract](https://github.com/tesseract-ocr/tesseract)
- **sqlite-vec** — [https://github.com/asg017/sqlite-vec](https://github.com/asg017/sqlite-vec)
