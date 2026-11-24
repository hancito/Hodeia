# HodeIA

**Your personal research assistant — powered by AI.**

HodeIA helps you manage, summarize, and search through your collection of academic papers. Import PDFs, get AI-generated structured summaries, pull their reference lists, and ask questions across your entire library.

[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](https://opensource.org/licenses/AGPL-3.0)
[![C++23](https://img.shields.io/badge/C++-23-blue.svg)]
[![Platform: Linux · Windows](https://img.shields.io/badge/Platform-Linux%20%C2%B7%20Windows-green.svg)]

> _"HodeIA" — from the Ancient Greek ὁδός (hodós), meaning "path." A fitting name for a tool that guides you through your library and answers your questions._

---

## Features

- **Local AI first**: HodeIA is built around a local-AI-first strategy, a performance-focused stack (C++, llama.cpp, Qt, SQLite) keeps it fast and lightweight. If you prefer, you can also point it at a model server (Ollama, LM Studio, or any OpenAI-compatible API) for faster inference.
- **Import PDF papers**: Add individual files or whole folders. HodeIA extracts the text automatically, and falls back to OCR for scanned documents.
- **Summarize your papers**: Get a structured summary per paper — title, authors, journal, year, topics, research objective, methodology, key findings, limitations, and a citable abstract.
- **References & citations**: After a paper is summarized, HodeIA pulls the works it cites and the works that cite it from [OpenAlex](https://openalex.org), formatted as APA-style bibliographies.
- **Ask questions across your library**: Query all your papers in plain language and get a synthesized, academic-style answer with inline citations. The more papers you add, the better the answers.
- **Agent mode**: Converse with a coding agent you installed yourself (Claude Code, Codex, Antigravity, Qwen Code or Hermes Agent). It searches your library through HodeIA's tools, answers with citations, can search the web for papers you do not have yet, and is kept away from everything else (no commands, no file changes).
- **View papers visually**: Browse page-by-page thumbnails alongside the extracted text, the generated summary, and the references.
- **Export summaries**: Export any summary — or a whole batch of them — to PDF.
- **20+ themes**: Catppuccin, Dracula, Gruvbox, Nord, Monokai, GitHub, and more.
- **Localized UI**: English and French.

HodeIA organizes your research into **projects**. Each project is an isolated workspace with its own database and summaries, so you can keep a separate project per research topic, course, or grant.

Full architecture, pipeline detail, database schema, and per-platform build instructions live in [docs/TECHNICAL.md](docs/TECHNICAL.md).

---

## Tech Stack

HodeIA prioritizes performance, so it is built on a fast, low-memory native stack written in C++. It runs models directly with [llama.cpp](https://github.com/ggml-org/llama.cpp) (no server), parses PDFs with MuPDF, and stores everything in a local SQLite database. It automatically uses your best available hardware (GPU or CPU).

For the full component-by-component breakdown, see [docs/TECHNICAL.md](docs/TECHNICAL.md).

---

## Supported Backends

HodeIA can run models **locally** or against a **remote** endpoint — and you can mix both.

| Backend               | Description                                                     |
| --------------------- | --------------------------------------------------------------- |
| **Local (llama.cpp)** | Run GGUF models directly on your GPU (CUDA / Vulkan) or CPU     |
| **Remote API**        | Connect to Ollama, LM Studio, or any OpenAI-compatible endpoint |
| **Anthropic API**     | Claude models with your Anthropic API key                       |
| **Agent**             | A coding agent installed on your computer, with your own login (see [Agent mode](#agent-mode)) |

Everything is managed in the **Models** menu: add a GGUF model you downloaded with **Models → Add Local Model...**, or add a remote endpoint or an agent in **Models → Manage Backends...** (name, API, URL or agent, optional API key, then **Check Connection**), then select it in **Models → Backend**. The model in use is picked in the **Active Model** list of the toolbar.

### Getting models

HodeIA ships with a small **Qwen3.5-0.8B** model at _Q4_ so it works out of the box. For larger models, download a GGUF file (plus its optional MMPROJ file for image support) and add it with **Models → Add Local Model...** — any GGUF works. A good source is [Hugging Face](https://huggingface.co), where [Unsloth](https://huggingface.co/unsloth) publishes GGUF versions of the [Qwen3.5](https://huggingface.co/collections/unsloth/qwen35) and [Gemma4](https://huggingface.co/collections/unsloth/gemma-4) families.

**Recommended models** (Q4_K_XL quantization — the best quality-to-size balance). Pick the largest model your memory can hold; VRAM needs are for the model weights plus a small context. These models were tested and known to works fine with HodeIA.

| Model                                                                      | Size (Q4) | Min VRAM / RAM | Best for                                               |
| -------------------------------------------------------------------------- | --------- | -------------- | ------------------------------------------------------ |
| [Qwen3.5-2B](https://huggingface.co/unsloth/Qwen3.5-2B-GGUF/tree/main)     | ~1.4 GB   | ~6 GB          | For modest hardware runs fine on integrated GPU        |
| [Gemma4-2B](https://huggingface.co/unsloth/gemma-4-E2B-it-GGUF/tree/main)  | ~3 GB     | ~6 GB          | Better than the 0.8B model runs fine on integrated GPU |
| [Qwen3.5-4B](https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/tree/main)     | ~2.5 GB   | ~6 GB          | Everyday summaries, better quality                     |
| [Gemma4-4B](https://huggingface.co/unsloth/gemma-4-E4B-it-GGUF/tree/main)  | ~6 GB     | ~8 GB          | Strong all-round reasoning                             |
| [Qwen3.5-9B](https://huggingface.co/unsloth/Qwen3.5-9B-GGUF/tree/main)     | ~5 GB     | ~8 GB          | High-quality summaries, dedicated GPU                  |
| [Gemma4-12B](https://huggingface.co/unsloth/gemma-4-12b-it-GGUF/tree/main) | ~7 GB     | ~12 GB         | Best quality, 12 GB+ VRAM                              |

> **Higher quantization = better quality, more memory.** Q4 is the sweet spot; if you have extra VRAM, Q5/Q6/Q8 of the same model will be more accurate. A CPU-only machine can run the 2B/4B models (slower). When in doubt, start with the bundled 0.8B model and step up as your hardware allows.

### Agent mode

In agent mode, a coding agent installed on your computer converses with you: it searches your library itself, reads the summaries and pages it needs, answers with citations and suggests ways to take the research further. HodeIA does not ship the agent: you install it and log in with your own account, then HodeIA connects to it directly (no adapter, no API key in HodeIA).

| Agent        | Install                                                                 | Log in                     |
| ------------ | ----------------------------------------------------------------------- | -------------------------- |
| Claude Code  | [Native installer](https://code.claude.com/docs/en/setup)               | `claude`, then `/login`    |
| Codex        | `npm install -g @openai/codex`                                          | `codex login`              |
| Antigravity  | [antigravity.google](https://antigravity.google)                        | `agy`, then sign in        |
| Qwen Code    | `npm install -g @qwen-code/qwen-code`                                   | `qwen`, then `/auth`       |
| Hermes Agent | [NousResearch/hermes-agent](https://github.com/NousResearch/hermes-agent) | `hermes setup`           |

1. Install the **command-line version** of the agent, even if you already use its app (Claude Desktop, the Codex app, the Antigravity IDE...): HodeIA starts the agent's command (`claude`, `codex`, `agy`, `qwen`, `hermes`), which the apps usually do not install. Log in and check that it answers in a terminal.
2. **Models → Manage Backends...**: choose the *Agent* API and your agent, leave the command empty (unless the agent is outside the PATH), **Check Connection**, **Add Backend**.
3. Select the backend in **Models → Backend** and a model in **Active Model** (*default* lets the agent choose).
4. Open **Query your library**, turn on **Agent mode**, click **New Conversation**. Conversations are kept in the project and resume where you left them.

**What the agent can do.** It uses HodeIA's library tools (search, list of papers, summaries, pages, citations, saving a summary) and can search the web, for instance to find papers that are not in your library. HodeIA forbids everything else (shell commands, changing or deleting files), even when the agent's own settings allow it elsewhere. For Hermes Agent, HodeIA creates a `hodeia` Hermes profile limited to the web for this. Your questions, the agent's web searches and the passages it reads go to the agent's provider, under the terms of your account; an agent such as Qwen Code or Hermes Agent set up with a local model keeps everything on your machine.

**HodeIA as an MCP server.** The same library tools are available to any MCP client (Claude Desktop, an editor...): register the command `Hodeia --mcp` (stdio). It serves the project open in HodeIA.

### Optional: OpenAlex API key

Citations are pulled from OpenAlex, which is free without a key. You can optionally set an API key in **Settings → OpenAlex API Key...** to raise rate limits.

---

## Download

The easiest way to get started is to grab a precompiled build from the [GitHub Releases](https://github.com/hancito/Hodeia/releases) page — no build tools required. Pick the archive for your platform:

| Platform              | Asset                | How to install                                |
| --------------------- | -------------------- | --------------------------------------------- |
| Linux (Arch)          | `PKGBUILD`           | Use the PKGBUILD or an aur helper             |
| Windows               | `Hodeia-*-x64.exe`   | Run the installer                             |

After installing, launch **HodeIA** from your applications menu. The app seeds its models and settings on first run — see [Getting models](#getting-models) to pick a model.

> Want to compile from source instead? See [Building from Source](#building-from-source).

---

## Building from Source

> **Just want to use HodeIA?** Skip to [Projects & Data Layout](#projects--data-layout) — you don't need to build anything. The rest of this section is for developers who want to compile from source.

HodeIA is a C++/Qt application with a modular build: a top-level `CMakeLists.txt` detects your platform and routes to `cmake/Platform<OS>.cmake`, which sets up the third-party libraries, GPU-backend discovery, and post-build deployment.

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
| Linux    | `scripts/Linux/get_depedencies.sh` → then CMake                                   | Clones/locates sources, then `scripts/Linux/build_{llama,mupdf,tesseract,sqlitevec}.sh` stage each dependency. Build the app with CMake + Ninja.    |
| Windows  | `scripts/Windows/build-dependencies.ps1`, then `scripts/Windows/build-app.ps1`    | Stages llama.cpp + sqlite-vec, runs `vcpkg install`, configures (MSVC + Ninja) + builds. All three backends ship; best is auto-detected at runtime. |
| macOS    | **Not yet available** — see [TECHNICAL.md](docs/TECHNICAL.md#macos-apple-silicon) | Experimental & untested. When released: compile the five dependencies, then CMake + Ninja; `macdeployqt` bundles everything into `Hodeia.app`.      |

> The Windows scripts live in `scripts/Windows/`, the Linux scripts in `scripts/Linux/`.
> Run the PowerShell scripts from a **VS Developer Prompt** (MSVC toolchain on PATH).
> To build the Windows installer, run `scripts/Windows/package-app.ps1` (Inno Setup). On Arch, `package/arch/PKGBUILD` builds everything from source with `makepkg -s`. See [docs/PACKAGING.md](docs/PACKAGING.md).

### GPU backend selection

At runtime HodeIA automatically picks the best available backend (CUDA, then Vulkan, then CPU) — no configuration needed.

---

## Platform Support

| Platform              | Status                   | Notes                                                                                                                                                         |
| --------------------- | ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Linux                 | ✅ Supported             | In-tree static libs + llama.cpp `.so`; GPU auto-detect                                                                                                        |
| Windows (MSVC)        | ✅ Supported             | Native MSVC + vcpkg; CPU/Vulkan/CUDA, runtime auto-detect                                                                                                     |
| macOS (Apple Silicon) | 🚧 **Not yet available** | Experimental & untested — the app is not yet released for macOS. Metal backend; the CMake build mirrors the (tested) Linux schema but has not been validated. |

---

## Projects & Data Layout

HodeIA keeps everything in one data folder, organized by **project**. Each project is an isolated workspace with its own summaries and records — create as many as you like (one per research topic, course, or grant). Your PDFs are only read at import: HodeIA stores their text and an image of each page, never the PDF itself, so your files are never moved, copied or modified.

```
~/.local/share/Hodeia/
├── config.json          # your settings: theme, models, language, etc.
├── Projects/
│   └── <project_name>/
│       ├── <project_name>.db   # the project's data (summaries, etc.)
│       └── IMG/                # page images (PNG)
├── Models/
│   ├── Summarizers/           # chat models you use (GGUF files)
│   └── Embedders/             # the embedding model (bundled)
├── Agent/                     # the agents' working folder (agent mode)
└── Agents/
    ├── profiles/              # optional: your own agent profiles (JSON)
    └── state/                 # files HodeIA writes for the agents (settings, hooks)
```

> On Windows the data folder is `%APPDATA%\Hodeia` instead. Uninstalling HodeIA never touches it.

See [docs/TECHNICAL.md](docs/TECHNICAL.md#configuration) for the full configuration schema.

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

HodeIA is free and open source. You are free to use, modify, and share it under the terms of the AGPL-3.0.

HodeIA is built with **Qt** (LGPL 3.0) and includes other third-party open-source components. The required citation, acknowledgments, and licensing notices for those are in [NOTICE](NOTICE).

---

## Links

- **Technical documentation** — [docs/TECHNICAL.md](docs/TECHNICAL.md)
- **Packaging** — [docs/PACKAGING.md](docs/PACKAGING.md)
- **OpenAlex** — [https://openalex.org](https://openalex.org) (citation data source)
- **llama.cpp** — [https://github.com/ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp)
- **MuPDF** — [https://mupdf.com](https://mupdf.com)
- **Tesseract** — [https://github.com/tesseract-ocr/tesseract](https://github.com/tesseract-ocr/tesseract)
- **sqlite-vec** — [https://github.com/asg017/sqlite-vec](https://github.com/asg017/sqlite-vec)
