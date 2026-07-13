# Build Requirements — Windows & macOS (Apple Silicon)

Compiler : **Clang/LLVM** sur les deux plateformes.

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

## Windows (Clang/LLVM)

### Outils système

| Outil            | Version        | Installation                                                                                                                           |
| ---------------- | -------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| LLVM for Windows | ≥ 18.0         | [llvm.org/downloads](https://llvm.org/builds/) — installer `LLVM-x64.msi`                                                              |
| CMake            | ≥ 3.25         | [cmake.org](https://cmake.org/download/)                                                                                               |
| Ninja            | latest         | `pip install ninja` ou [GitHub releases](https://github.com/ninja-build/ninja/releases)                                                |
| Windows 10 SDK   | ≥ 10.0.22621.0 | Inclus avec [Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/) — cocher seulement "Windows 11 SDK" + "MSBuild" |

> **Important** : Clang sur Windows (`clang-cl`) a besoin de `link.exe` et `lib.exe` du Windows SDK pour le lien. Installez **Visual Studio Build Tools** (gratuit) et cochez uniquement la charge de travail "Compilation C++ prise en charge par les outils v143".

### Runtime Qt6

| Composant                                             | Installation                                                                                         |
| ----------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| Qt 6.10+ (Quick, QuickControls2, Concurrent, Network) | [Qt Online Installer](https://www.qt.io/download) — sélectionner le kit **Qt 6.x.x for Win64 Clang** |

> **Important** : L'outil **`windeployqt`** est livré avec Qt. Il est appelé automatiquement par CMake pour copier toutes les DLLs Qt dans le dossier de sortie. Il faut l'installation complète via le Qt Online Installer (pas une version minimale).

### OpenMP

```powershell
# Via vcpkg
vcpkg install llvm-openmp
```

Ou téléchargez `libomp.dll` + `libomp.lib` depuis les [releases LLVM](https://github.com/llvm/llvm-project/releases). Placez `libomp.dll` dans le même répertoire que `clang-cl` (généralement `C:\Program Files\LLVM\bin\`).

> **Note** : Le runtime `libomp.dll` est automatiquement copié à côté de `Paperize.exe` par CMake au moment du build. L'utilisateur final n'a **pas** besoin d'installer OpenMP.

### GPU (optionnel)

| Backend | Installation                                                                                                                                                             |
| ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| CUDA    | [CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit) — installe `cudart.lib`, `cublas.lib` dans `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\<ver>\lib\x64\` |
| Vulkan  | [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) — définit `$env:VULKAN_SDK` automatiquement                                                                             |

### Bibliothèques à compiler

Toutes les commandes utilisent `clang-cl`. Lancez-les dans un **LLVM x64 Native Tools Command Prompt** ou configurez manuellement le chemin vers les headers Windows SDK.

#### 1. llama.cpp (CPU) → `.dll` + `.lib`

```powershell
git clone https://github.com/ggml-org/llama.cpp.git
cd llama.cpp
mkdir build; cd build

cmake .. -G Ninja ^
  -DCMAKE_C_COMPILER=clang-cl ^
  -DCMAKE_CXX_COMPILER=clang-cl ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DBUILD_SHARED_LIBS=ON ^
  -DLLAMA_OPENMP=ON ^
  -DLLAMA_CUDA=OFF ^
  -DLLAMA_VULKAN=OFF

cmake --build .
```

Pour CUDA : `-DLLAMA_CUDA=ON` + CUDA Toolkit installé.
Pour Vulkan : `-DLLAMA_VULKAN=ON` + Vulkan SDK installé.

Fichiers à copier dans `external/lib/windows/llamacpp/cpu/` :

```
llama.dll          (+ llama.lib import lib)
llama-common.dll   (+ .lib)
mtmd.dll           (+ .lib)
ggml.dll           (+ .lib)
ggml-base.dll      (+ .lib)
ggml-cpu.dll       (+ .lib)
```

> Copie les `.lib` correspondants dans le même dossier — le linker Clang en a besoin.

Pour CUDA, répète avec `-DLLAMA_CUDA=ON` → copie dans `external/lib/windows/llamacpp/cuda/` :

```
llama.dll, llama-common.dll, mtmd.dll, ggml.dll, ggml-base.dll, ggml-cpu.dll
ggml-cuda.dll      ← backend GPU
```

Pour Vulkan, répète avec `-DLLAMA_VULKAN=ON` → `external/lib/windows/llamacpp/vulkan/` :

```
ggml-vulkan.dll    ← backend GPU
```

#### 2. MuPDF → `.lib` (statique)

> **Important** : Utiliser `MUPDF_BUILD_THIRD_PARTY=ON` (pas `MUPDF_THIRD_PARTY=ON`) pour embedder toutes les dépendances (libpng, libjpeg, libtiff, zlib, FreeType, HarfBuzz, etc.) dans les `.lib` statiques.

```powershell
git clone https://github.com/ArtifexSoftware/mupdf.git
cd mupdf
git submodule update --init

mkdir build; cd build

cmake .. -G Ninja ^
  -DCMAKE_C_COMPILER=clang-cl ^
  -DCMAKE_CXX_COMPILER=clang-cl ^
  -DMUPDF_BUILD_TYPE=Release ^
  -DMUPDF_BUILD_THIRD_PARTY=ON ^
  -DBUILD_SHARED_LIBS=OFF

cmake --build .
```

Copier dans `external/lib/windows/` :

```
libmupdf.lib
libmupdf-third.lib
libmupdf-threads.lib
```

#### 3. Leptonica → `.lib` (statique)

> **Important** : Lier contre les `.lib` statiques produits par le build MuPDF (`_deps\...`) pour éviter toute dépendance dynamique.

```powershell
# Télécharger leptonica-1.87.0.tar.gz, extraire
cd leptonica-1.87.0
mkdir build; cd build

$MUPDF_BUILD = "C:\chemin\vers\mupdf\build"

cmake .. -G Ninja ^
  -DCMAKE_C_COMPILER=clang-cl ^
  -DCMAKE_CXX_COMPILER=clang-cl ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DBUILD_PROG=OFF ^
  -DENABLE_PNG=ON ^
  -DENABLE_JPEG=ON ^
  -DENABLE_TIFF=ON ^
  -DENABLE_ZLIB=ON ^
  -DPNG_LIBRARY="${MUPDF_BUILD}\_deps\libpng-build\Release\png16.lib" ^
  -DJPEG_LIBRARY="${MUPDF_BUILD}\_deps\libjpeg-build\jpeg-static.lib" ^
  -DTIFF_LIBRARY="${MUPDF_BUILD}\_deps\libtiff-build\libtiff.lib" ^
  -DZ_LIBRARY="${MUPDF_BUILD}\_deps\zlib-build\zlib.lib"

cmake --build .
```

Copier `build\libleptonica.lib` → `external/lib/windows/`

> **Vérification** : `dumpbin /symbols libleptonica.lib | findstr "png\|jpeg\|tiff\|zlib"` ne doit montrer aucun symbole external non résolu.

#### 4. Tesseract → `.lib` (statique)

```powershell
# Télécharger tesseract-5.5.2.tar.gz, extraire
cd tesseract-5.5.2
mkdir build; cd build

cmake .. -G Ninja ^
  -DCMAKE_C_COMPILER=clang-cl ^
  -DCMAKE_CXX_COMPILER=clang-cl ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DBUILD_TRAINING_TOOLS=OFF ^
  -DBUILD_TESTS=OFF ^
  -DDISABLED_LEGACY_ENGINE=ON ^
  -DGRAPHICS_DISABLED=ON ^
  -DLeptonica_DIR=C:\chemin\vers\leptonica-1.87.0\build

cmake --build .
```

Copier `build\libtesseract.lib` → `external/lib/windows/`

#### 5. sqlite-vec → `.dll` + `.lib`

```powershell
curl -fSL -o vec0.c "https://github.com/asg017/sqlite-vec/raw/main/dist/vec0.c"
clang-cl /shared /LD vec0.c /Fevec0.dll /Fevec0.lib
```

Copier `vec0.dll` + `vec0.lib` → `external/lib/windows/sqlitevec/`

#### 6. Tesseract traineddata

```powershell
New-Item -ItemType Directory -Force -Path Resources\tessdata
curl -fSL -o Resources\tessdata\eng.traineddata `
  "https://github.com/tesseract-ocr/tessdata_fast/raw/main/eng.traineddata"
```

### Build final (Windows)

```powershell
mkdir build; cd build

cmake .. -G Ninja ^
  -DCMAKE_C_COMPILER=clang-cl ^
  -DCMAKE_CXX_COMPILER=clang-cl ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="C:\Qt\6.10.0\msvc2022_64"   # adapter selon ton kit Qt

cmake --build .
```

Le CMake appelle automatiquement **`windeployqt`** en POST_BUILD. Le dossier de sortie contient :

| Catégorie                | Contenu                                                                                                                                                    |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Qt runtime** (~150 Mo) | `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Quick.dll`, `Qt6QuickControls2.dll`, `Qt6Network.dll`, ICU data, image formats, platform plugins, styles, etc.            |
| **LLaMA.cpp**            | `llama.dll`, `ggml.dll`, `ggml-cpu.dll`, `ggml-cuda.dll` (ou `ggml-vulkan.dll`), `libllama-common.dll`, `libmtmd.dll`                                      |
| **GPU backend**          | `cudart64_*.dll`, `cublas64_*.dll`, `cublasLt64_*.dll` (si CUDA) — copiés depuis le CUDA Toolkit / `vulkan-1.dll` (si Vulkan) — copié depuis le Vulkan SDK |
| **Extensions**           | `vec0.dll` (sqlite-vec), `libomp.dll` (OpenMP)                                                                                                             |
| **Ressources**           | themes/, Fonts/, tessdata/                                                                                                                                 |
| **Exécutable**           | `Paperize.exe`                                                                                                                                             |

> **VC++ Redistributable** : `windeployqt` copie automatiquement les DLLs runtime MSVC (`vcruntime140.dll`, `msvcp140.dll`, `ucrtbase.dll`) depuis l'installation Visual Studio Build Tools. Si elles sont absentes, l'utilisateur doit installer le [VC++ Redist](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).

Résultat : un dossier autonome (~250-400 Mo selon le backend GPU). L'utilisateur lance `Paperize.exe` — zéro installation requise.

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
