# Hodeia

**Your personal research assistant — powered by local AI, running entirely on your machine.**

Hodeia helps you manage, summarize, and search through your collection of academic papers. Import PDFs, get AI-generated summaries, and ask questions across your entire library — all without sending your data to the cloud.

[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](https://opensource.org/licenses/AGPL-3.0)

## What It Does

- **📄 Import PDF papers** — Drag in individual files or entire folders; text is automatically extracted (with OCR for scanned documents)
- **📝 Auto-summarize papers** — Get structured summaries with title, authors, methodology, key findings, limitations, and references
- **🔍 Ask questions across your library** — Query all your papers in natural language and get cited answers drawn from the most relevant passages
- **🖼️ View papers visually** — Browse page-by-page thumbnails alongside summaries
- **🔒 100% private** — Everything runs locally on your machine. No accounts, no cloud, no data collection
- **🎨 19 themes** — Catppuccin, Dracula, Gruvbox, Nord, Monokai, GitHub, and more

## Quick Start

1. **Install** — [Build from source](docs/TECHNICAL.md) (Linux)
2. **Load a model** — Place a GGUF model (e.g. Llama 3, Mistral) in `~/.local/share/Hodeia/Models/Summarizers/`
3. **Create a project** — File → New Project
4. **Import papers** — Use the toolbar to add PDFs or folders
5. **Summarize & query** — Select papers to summarize, or open the RAG window to ask questions across your library

## Supported Backends

| Backend | Description |
|---|---|
| **llama.cpp (local)** | Run GGUF models directly on your GPU or CPU |
| **Remote API** | Connect to Ollama, LM Studio, or any OpenAI-compatible endpoint |

## Platform Support

| Platform | Status |
|---|---|
| Linux | ✅ Supported |
| macOS | 🚧 Experimental |
| Windows | 🚧 Experimental |

## Project Organization

Work is organized into **projects** — isolated workspaces, each with its own database, imported PDFs, and summaries. Create as many as you need (e.g. one per research topic).

## License

**GNU Affero General Public License v3.0** — See [LICENSE](LICENSE).

## Technical Documentation

For build instructions, architecture details, and developer guides, see [TECHNICAL.md](docs/TECHNICAL.md).
