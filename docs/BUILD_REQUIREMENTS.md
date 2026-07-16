# Build Requirements — Windows & macOS (Apple Silicon)

Compiler : **Clang/LLVM** sur les trois plateformes.

---

## Linux

### Dépendances par gestionnaire de paquets

Installez **un seul** des blocs ci-dessous selon votre distribution.

#### Arch Linux / Manjaro

```bash
sudo pacman -S clang cmake ninja qt6-base qt6-declarative patchelf pkgconf \
  vulkan-icd-loader libomp make git wget
```

#### Debian / Ubuntu

```bash
sudo apt install -y clang cmake ninja-build patchelf pkg-config git wget \
  libclang-rt-dev libomp-dev \
  qt6-base-dev qt6-declarative-dev \
  libvulkan1
```

#### Fedora

```bash
sudo dnf install clang cmake ninja qt6-qtbase qt6-qtquickcontrols2 \
  patchelf pkgconf-pkg-config libomp git wget vulkan-loader
```

### Build

```bash
mkdir build && cd build

cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release

cmake --build .
```

> Le CMake détecte automatiquement le backend GPU (Vulkan ou CUDA) disponible
> sur la machine. Pour forcer un backend :
> `-DLLAMA_FORCE_BACKEND=cpu` | `cuda` | `vulkan`

---

## macOS Apple Silicon

### Outils système

| Outil                    | Version | Installation             |
| ------------------------ | ------- | ------------------------ |
| Xcode Command Line Tools | ≥ 15.0  | `xcode-select --install` |
| CMake                    | ≥ 3.25  | `brew install cmake`     |
| Ninja                    | latest  | `brew install ninja`     |
| LLVM/Clang               | ≥ 18.0  | `brew install llvm`      |

### Runtime Qt6

| Composant                                             | Installation                                                             |
| ----------------------------------------------------- | ------------------------------------------------------------------------ |
| Qt 6.10+ (Quick, QuickControls2, Concurrent, Network) | [Qt Online Installer](https://www.qt.io/download) ou `brew install qt@6` |

### Bibliothèques système

| Bibliothèque    | Installation      | Utilisée par              |
| --------------- | ----------------- | ------------------------- |
| Metal framework | inclus avec macOS | GPU backend (automatique) |

> **Note** : libpng, libjpeg, libtiff, zlib, FreeType, HarfBuzz sont embeddées
> statiquement dans `libmupdf-third.a` via `MUPDF_BUILD_THIRD_PARTY=ON`.
> L'utilisateur final n'a aucune dépendance à installer.

### OpenMP

```bash
brew install libomp
```

CMake doit le trouver : `-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/libomp`.

> **Note** : Le runtime `libomp.dylib` est automatiquement copié dans le `.app` bundle par CMake au moment du build. L'utilisateur final n'a **pas** besoin d'installer OpenMP.

### Bibliothèques à compiler

#### 1. llama.cpp (Metal) → `.dylib`

```bash
git clone https://github.com/ggml-org/llama.cpp.git
cd llama.cpp
mkdir build && cd build

cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DBUILD_SHARED_LIBS=ON \
  -DLLAMA_METAL=ON \
  -DLLAMA_BLAS=OFF \
  -DLLAMA_CUDA=OFF \
  -DLLAMA_VULKAN=OFF \
  -DLLAMA_OPENMP=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build .
```

Fichiers à copier dans `external/lib/mac/llamacpp/metal/` :

```
libllama.dylib
libllama-common.dylib
libmtmd.dylib
libggml.dylib
libggml-base.dylib
libggml-cpu.dylib
libggml-metal.dylib        ← backend GPU
```

#### 2. MuPDF → `.a` (statique)

> **Important** : Utiliser `MUPDF_BUILD_THIRD_PARTY=ON` (pas juste `MUPDF_THIRD_PARTY=ON`).
> Cela compile **et** lie statiquement **toutes** les dépendances MuPDF :
> libpng, libjpeg, libtiff, zlib, FreeType, HarfBuzz, OpenJPEG, Brotli, JBIG2,
> libarchive, natsort, unarr. L'application finale n'aura **aucune dépendance externe**.

```bash
git clone https://github.com/ArtifexSoftware/mupdf.git
cd mupdf
git submodule update --init

mkdir build && cd build

cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DMUPDF_BUILD_TYPE=Release \
  -DMUPDF_BUILD_THIRD_PARTY=ON \
  -DBUILD_SHARED_LIBS=OFF

cmake --build .
```

Fichiers à copier dans `external/lib/mac/` :

```
libmupdf.a
libmupdf-third.a
libmupdf-threads.a
```

#### 3. Leptonica → `.a` (statique)

> **Important** : Pour une application 100% autonome, il faut que Leptonica ne dépende d'**aucune** bibliothèque dynamique. MuPDF avec `MUPDF_BUILD_THIRD_PARTY=ON` produit ses propres versions statiques de libpng, libjpeg, libtiff, zlib dans son arbre de build. Il faut les réutiliser ici.
>
> Chemins typiques après le build MuPDF :
>
> - `mupdf/build/_deps/libpng-build/libpng16.a`
> - `mupdf/build/_deps/libjpeg-build/libjpeg.a`
> - `mupdf/build/_deps/zlib-build/libz.a`
> - `mupdf/build/_deps/libtiff-build/libtiff.a`

```bash
wget https://github.com/DanBloomberg/leptonica/releases/download/1.87.0/leptonica-1.87.0.tar.gz
tar xzf leptonica-1.87.0.tar.gz
cd leptonica-1.87.0
mkdir build && cd build

MUPDF_BUILD=/chemin/vers/mupdf/build

cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_PROG=OFF \
  -DENABLE_PNG=ON \
  -DENABLE_JPEG=ON \
  -DENABLE_TIFF=ON \
  -DENABLE_ZLIB=ON \
  -DENABLE_GIF=OFF \
  -DENABLE_WEBP=OFF \
  -DENABLE_OPENJPEG=OFF \
  -DPNG_LIBRARY="${MUPDF_BUILD}/_deps/libpng-build/libpng16.a" \
  -DJPEG_LIBRARY="${MUPDF_BUILD}/_deps/libjpeg-build/libjpeg.a" \
  -DTIFF_LIBRARY="${MUPDF_BUILD}/_deps/libtiff-build/libtiff.a" \
  -DZ_LIBRARY="${MUPDF_BUILD}/_deps/zlib-build/libz.a"

cmake --build .
```

Copier `build/src/libleptonica.a` → `external/lib/mac/`

> **Vérification** : `nm libleptonica.a | grep " U " | grep -iE "png|jpeg|tiff|zlib"` doit retourner **rien**.

#### 4. Tesseract → `.a` (statique)

```bash
wget https://github.com/tesseract-ocr/tesseract/releases/download/5.5.2/tesseract-5.5.2.tar.gz
tar xzf tesseract-5.5.2.tar.gz
cd tesseract-5.5.2
mkdir build && cd build

cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TRAINING_TOOLS=OFF \
  -DBUILD_TESTS=OFF \
  -DDISABLED_LEGACY_ENGINE=ON \
  -DGRAPHICS_DISABLED=ON \
  -DLeptonica_DIR=/chemin/vers/leptonica-1.87.0/build

cmake --build .
```

Copier `build/libtesseract.a` → `external/lib/mac/`

#### 5. sqlite-vec → `.dylib`

```bash
wget https://github.com/asg017/sqlite-vec/raw/main/dist/vec0.c
clang -shared -fPIC -o vec0.dylib vec0.c
```

Copier `vec0.dylib` → `external/lib/mac/sqlitevec/`

#### 6. Tesseract traineddata

```bash
mkdir -p Resources/tessdata
curl -fSL -o Resources/tessdata/eng.traineddata \
  "https://github.com/tesseract-ocr/tessdata_fast/raw/main/eng.traineddata"
```

### Build final (macOS)

```bash
mkdir build && cd build

cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/llvm \
  -DCMAKE_BUILD_TYPE=Release

cmake --build .
```

Le CMake appelle automatiquement **`macdeployqt`** en POST_BUILD — cela copie tous les Frameworks Qt, les plugins (ICU, image formats, platform plugins), `libomp.dylib`, les `.dylib` llama.cpp, `vec0.dylib`, les themes et les fonts **dans le `.app` bundle**.

Résultat : `Paperize.app` (~300 Mo) — standalone, drag & drop vers `/Applications`. Aucune dépendance externe requise pour l'utilisateur.

---

## Windows (MSYS2 + Clang/LLVM)

> **Recommandé** : Utiliser MSYS2 avec l'environnement **clang64**. Cela fournit un shell
> Unix complet (bash, make, pkg-config) et un gestionnaire de paquets (`pacman`) qui résout
> toutes les dépendances de build automatiquement. Le produit final est **identique** —
> l'utilisateur final n'a aucune dépendance MSYS2.

### Installation MSYS2

1. Télécharger et installer [MSYS2 x86_64](https://www.msys2.org/download/)
2. Ouvrir **MSYS2 clang64** (pas MSYS2 mingw64, pas MSYS2 MSYS)
3. Installer les paquets :

```bash
pacman -S --needed mingw-w64-clang-x86_64-{toolchain,cmake,ninja,llvm-openmp}
pacman -S --needed mingw-w64-clang-x86_64-{libpng,libjpeg-turbo,libtiff,zlib,freetype,harfbuzz,openjpeg}
pacman -S --needed mingw-w64-clang-x86_64-{qt6-base,qt6-declarative,qt6-tools}
pacman -S --needed git wget curl
```

Qt6 de MSYS2 inclut automatiquement `windeployqt` (`mingw64/bin/windeployqt.exe`).

### GPU (optionnel — hors MSYS2)

| Backend | Installation                                                                                                                                                             |
| ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| CUDA    | [CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit) — installe `cudart.lib`, `cublas.lib` dans `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\<ver>\lib\x64\` |
| Vulkan  | [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) — définit `$env:VULKAN_SDK` automatiquement                                                                             |

> Les GPU backends s'installent en dehors de MSYS2. Le SDK définit les variables
> d'environnement visibles depuis toutes les sessions.

### Bibliothèques à compiler

Toutes les commandes s'exécutent dans **MSYS2 clang64**.

#### 1. llama.cpp (CPU) → `.dll` + `.lib`

```bash
git clone https://github.com/ggml-org/llama.cpp.git
cd llama.cpp
mkdir build && cd build

cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DLLAMA_OPENMP=ON \
  -DLLAMA_CUDA=OFF \
  -DLLAMA_VULKAN=OFF

cmake --build .
```

Pour CUDA : `-DLLAMA_CUDA=ON` + CUDA Toolkit installé.
Pour Vulkan : `-DLLAMA_VULKAN=ON` + Vulkan SDK installé.

Fichiers à copier dans `external/lib/windows/llamacpp/cpu/` :

```
llama.dll          (+ llama.dll.a import lib)
llama-common.dll   (+ .dll.a)
mtmd.dll           (+ .dll.a)
ggml.dll           (+ .dll.a)
ggml-base.dll      (+ .dll.a)
ggml-cpu.dll       (+ .dll.a)
```

> Sous MSYS2, les import libs portent l'extension `.dll.a` (équivalent de `.lib`).
> Copiez-les dans le même dossier — le linker en a besoin.

Pour CUDA, répétez avec `-DLLAMA_CUDA=ON` → `external/lib/windows/llamacpp/cuda/` :

```
llama.dll, llama-common.dll, mtmd.dll, ggml.dll, ggml-base.dll, ggml-cpu.dll
ggml-cuda.dll      ← backend GPU
```

Pour Vulkan, répétez avec `-DLLAMA_VULKAN=ON` → `external/lib/windows/llamacpp/vulkan/` :

```
ggml-vulkan.dll    ← backend GPU
```

#### 2. MuPDF → `.a` (statique)

> **Important** : `MUPDF_BUILD_THIRD_PARTY=ON` embed toutes les dépendances (libpng,
> libjpeg, libtiff, zlib, FreeType, HarfBuzz, etc.) dans les archives statiques.
> Avec MSYS2, `pkg-config` trouve automatiquement ces bibliothèques.

```bash
git clone https://github.com/ArtifexSoftware/mupdf.git
cd mupdf
git submodule update --init

mkdir build && cd build

cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DMUPDF_BUILD_TYPE=Release \
  -DMUPDF_BUILD_THIRD_PARTY=ON \
  -DBUILD_SHARED_LIBS=OFF

cmake --build .
```

Copier dans `external/lib/windows/` :

```
libmupdf.a
libmupdf-third.a
libmupdf-threads.a
```

#### 3. Leptonica → `.a` (statique)

> Avec MSYS2 et MuPDF compilé avec `MUPDF_BUILD_THIRD_PARTY=ON`, les bibliothèques
> png/jpeg/tiff/zlib sont disponibles via `pkg-config` — Leptonica les trouve
> automatiquement.

```bash
wget https://github.com/DanBloomberg/leptonica/releases/download/1.87.0/leptonica-1.87.0.tar.gz
tar xzf leptonica-1.87.0.tar.gz
cd leptonica-1.87.0
mkdir build && cd build

cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_PROG=OFF \
  -DENABLE_PNG=ON \
  -DENABLE_JPEG=ON \
  -DENABLE_TIFF=ON \
  -DENABLE_ZLIB=ON \
  -DENABLE_GIF=OFF \
  -DENABLE_WEBP=OFF \
  -DENABLE_OPENJPEG=OFF

cmake --build .
```

Copier `build/src/libleptonica.a` → `external/lib/windows/`

> **Vérification** : `nm libleptonica.a | grep " U " | grep -iE "png|jpeg|tiff|zlib"` doit retourner **rien** si les libs sont bien liées statiquement. Si des symboles persistent, passez manuellement les archives MuPDF : `-DPNG_LIBRARY=...` etc.

#### 4. Tesseract → `.a` (statique)

```bash
wget https://github.com/tesseract-ocr/tesseract/releases/download/5.5.2/tesseract-5.5.2.tar.gz
tar xzf tesseract-5.5.2.tar.gz
cd tesseract-5.5.2
mkdir build && cd build

cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TRAINING_TOOLS=OFF \
  -DBUILD_TESTS=OFF \
  -DDISABLED_LEGACY_ENGINE=ON \
  -DGRAPHICS_DISABLED=ON \
  -DLeptonica_DIR=/chemin/vers/leptonica-1.87.0/build

cmake --build .
```

Copier `build/libtesseract.a` → `external/lib/windows/`

#### 5. sqlite-vec → `.dll` + `.a`

```bash
curl -fSL -o vec0.c "https://github.com/asg017/sqlite-vec/raw/main/dist/vec0.c"
clang -shared -o vec0.dll vec0.c
```

Copier `vec0.dll` → `external/lib/windows/sqlitevec/`

#### 6. Tesseract traineddata

```bash
mkdir -p Resources/tessdata
curl -fSL -o Resources/tessdata/eng.traineddata \
  "https://github.com/tesseract-ocr/tessdata_fast/raw/main/eng.traineddata"
```

### Build final (Windows via MSYS2)

```bash
mkdir build && cd build

cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/mingw64   # Qt6 de MSYS2

cmake --build .
```

> Si vous utilisez Qt de l'**Online Installer** au lieu de MSYS2, remplacez
> `CMAKE_PREFIX_PATH` par le chemin du kit Qt :
> `-DCMAKE_PREFIX_PATH="C:/Qt/6.10.0/msvc2022_64"`

Le CMake appelle automatiquement **`windeployqt`** en POST_BUILD. Le dossier de sortie contient :

| Catégorie                | Contenu                                                                                                                                         |
| ------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| **Qt runtime** (~150 Mo) | `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Quick.dll`, `Qt6QuickControls2.dll`, `Qt6Network.dll`, ICU data, image formats, platform plugins, styles, etc. |
| **LLaMA.cpp**            | `llama.dll`, `ggml.dll`, `ggml-cpu.dll`, `ggml-cuda.dll` (ou `ggml-vulkan.dll`), `llama-common.dll`, `mtmd.dll`                                 |
| **GPU backend**          | `cudart64_*.dll`, `cublas64_*.dll`, `cublasLt64_*.dll` (si CUDA) / `vulkan-1.dll` (si Vulkan)                                                   |
| **Extensions**           | `vec0.dll` (sqlite-vec), `libomp.dll` (OpenMP)                                                                                                  |
| **Ressources**           | themes/, Fonts/, tessdata/                                                                                                                      |
| **Exécutable**           | `Paperize.exe`                                                                                                                                  |

> **Runtime C/C++ (UCRT)** : Le runtime universel Windows (`ucrtbase.dll`, `vcruntime140.dll`)
> fait partie de Windows 10/11 et est toujours présent. Pas besoin de shipper le VC++ Redist
> tant que votre cible est Windows 10+. `windeployqt` les copie quand même s'il trouve
> les Visual Studio Build Tools.

Résultat : un dossier autonome (~250-400 Mo selon le backend GPU). L'utilisateur lance `Paperize.exe` — zéro installation requise, zéro dépendance MSYS2.

Pour créer un installateur, empaquete le dossier avec [Inno Setup](https://jrsoftware.org/isinfo.php) ou [NSIS](https://nsis.sourceforge.io/).

---

## Structure finale attendue `external/lib/`

```
external/lib/
├── linux/                          ← déjà rempli ✅
│   ├── libmupdf.a
│   ├── libmupdf-third.a
│   ├── libmupdf-threads.a
│   ├── libtesseract.a
│   ├── libleptonica.a
│   ├── llamacpp/
│   │   ├── cpu/     ← .so files
│   │   ├── cuda/    ← .so files
│   │   └── vulkan/  ← .so files
│   └── sqlitevec/
│       └── vec0.so
│
├── mac/                            ← à remplir
│   ├── libmupdf.a
│   ├── libmupdf-third.a
│   ├── libmupdf-threads.a
│   ├── libtesseract.a
│   ├── libleptonica.a
│   ├── llamacpp/
│   │   ├── cpu/     ← .dylib files (fallback)
│   │   └── metal/   ← .dylib files (GPU)
│   └── sqlitevec/
│       └── vec0.dylib
│
└── windows/                        ← à remplir
    ├── libmupdf.lib
    ├── libmupdf-third.lib
    ├── libmupdf-threads.lib
    ├── libtesseract.lib
    ├── libleptonica.lib
    ├── llamacpp/
    │   ├── cpu/     ← .dll + .lib
    │   ├── cuda/    ← .dll + .lib (optionnel)
    │   └── vulkan/  ← .dll + .lib (optionnel)
    └── sqlitevec/
        └── vec0.dll
```

---

## Ordre recommandé de compilation

1. **Leptonica** → dépendance de Tesseract
2. **Tesseract** → dépend de Leptonica
3. **MuPDF** → indépendant
4. **llama.cpp** → indépendant (compiler une fois par backend)
5. **sqlite-vec** → fichier unique, compilation instantanée
6. **Paperize** → assemble tout

## Notes Clang-specific

- **macOS** : Clang d'Apple (`/usr/bin/clang`) supporte C++23 et OpenMP via `libomp`. Le LLVM Homebrew est recommandé pour une version plus récente.
- **Windows** : `clang-cl` agit comme un drop-in pour MSVC mais produit du code souvent plus performant. Il nécessite `link.exe` du Windows SDK — d'où l'installation de Visual Studio Build Tools (même avec Clang).
- Sur les deux plateformes, `-DCMAKE_C_COMPILER` et `-DCMAKE_CXX_COMPILER` pointent vers les binaires Clang, mais le linker reste celui de la plateforme (`ld.lld` sur macOS, `link.exe` sur Windows avec `clang-cl`).
