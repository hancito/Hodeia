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

Résultat : `Hodeia.app` (~300 Mo) — standalone, drag & drop vers `/Applications`. Aucune dépendance externe requise pour l'utilisateur.

---

## Windows (Visual Studio / MSVC + vcpkg)

> **Approche** : build **natif MSVC** avec Visual Studio 2022. Toutes les dépendances
> natives sont de l'ABI **MSVC** (pas de MinGW/MSYS2). llama.cpp provient des DLL de
> release officielles compilées MSVC ; MuPDF, Tesseract, Leptonica et le loader Vulkan
> sont fournis par **vcpkg** (triplet `x64-windows`). Un seul point d'entrée,
> `build_windows.ps1`, orchestre tout.
>
> **Pourquoi MSVC** : les DLL llama.cpp de la release officielle sont compilées MSVC
> (CRT `vcruntime140`). Mélanger une app compilée MinGW avec ces DLL provoquerait des
> incompatibilités CRT/ABI. D'où le choix d'un toolchain entièrement MSVC.

### Prérequis (installation unique)

| Composant          | Version / choix                         | Installation                                                                      |
| ------------------ | --------------------------------------- | --------------------------------------------------------------------------------- |
| Visual Studio 2022 | avec « Développement avec C++ »         | [Visual Studio](https://visualstudio.microsoft.com/)                              |
| CMake              | ≥ 3.25                                  | inclus dans VS, ou `winget install Kitware.CMake`                                 |
| Qt 6               | ≥ 6.10, build **MSVC 2019/2022 64-bit** | [Qt Online Installer](https://www.qt.io/download) (cocher `msvc2022_64`)          |
| vcpkg              | latest                                  | `git clone https://github.com/microsoft/vcpkg <repo>\vcpkg`                       |
| llama.cpp          | release **b11404** (ou similaire)       | téléchargé depuis GitHub — dossiers `cpu\`, `vulkan\`, `cuda\12.4\`, `cuda\13.4\` |

### Paquets vcpkg

Déclarés dans `vcpkg.json` à la racine (mode manifeste) :

| Port vcpkg                         | Cible liée                       | Lienage (Win) | Rôle                                                  |
| ---------------------------------- | -------------------------------- | ------------- | ----------------------------------------------------- |
| `libmupdf`                         | `unofficial::libmupdf::libmupdf` | **statique**  | PDF (embed freetype, harfbuzz, jbig2dec, openjpeg, …) |
| `tesseract`                        | `Tesseract::libtesseract`        | dynamique     | OCR                                                   |
| `leptonica` (via tesseract)        | `leptonica`                      | dynamique     | images pour OCR                                       |
| `vulkan-loader` + `vulkan-headers` | `Vulkan::Vulkan`                 | dynamique     | loader + `vulkan-1.dll`                               |

> **Note** : le port MuPDF s'appelle **`libmupdf`** (pas `mupdf`). Sur Windows il est
> construit **statique** (exports DLL incomplets) — il n'y a donc **aucune DLL MuPDF**
> à distribuer ; les codecs image sont embarqués.

### Étape 1 — Stager les DLL llama.cpp

Les DLL de la release sont organisées par backend (`cpu\`, `vulkan\`, `cuda\12.4\`,
`cuda\13.4\`). `scripts\stage-llama.ps1` les fusionne dans un dossier plat
`external\lib\windows\llamacpp\` que CMake attend :

```powershell
.\scripts\stage-llama.ps1 -SourceDir "D:\Developpement\Windows" -CudaVersion 12.4
```

Contenu résultant (tous les backends dans un seul binaire) :

```
llama.dll, llama-common.dll, mtmd.dll
ggml.dll, ggml-base.dll, ggml-rpc.dll
ggml-cpu-x64.dll, ggml-cpu-sse42.dll, ggml-cpu-<arch>…   (13 variantes CPU)
ggml-cuda.dll      ← backend CUDA
ggml-vulkan.dll    ← backend Vulkan
libomp.dll, cudart64_12.dll, cublas64_12.dll, cublasLt64_12.dll
```

> Le binaire unique lie **les trois backends** ; `ggml_backend_load_all()` détecte
> au runtime le meilleur disponible (CUDA > Vulkan > CPU). `stage-llama.ps1` embarque
> un seul niveau de CUDA — `-CudaVersion 13.4` pour cibler CUDA 13.x.

### Étape 2 — sqlite-vec (vec0.dll)

`SqliteDb::open()` charge `vec0.dll` au runtime via `sqlite3_load_extension()`.
Pas de port vcpkg pour sqlite-vec : `scripts\build-sqlitevec.ps1` le fournit.

```powershell
# défaut : téléchargement du binaire officiel (version pinnée, checksum vérifié)
.\scripts\build-sqlitevec.ps1

# alternative : compilation depuis le source avec cl.exe
.\scripts\build-sqlitevec.ps1 -Method build
```

La version est **pinnée** à `v0.1.10-alpha.4` (voir
`external/include/SqliteVec/sqlite-vec.h`). Le script par défaut télécharge
`sqlite-vec-0.1.10-alpha.4-loadable-windows-x86_64.tar.gz` depuis la release GitHub,
vérifie le SHA-256, et extrait `vec0.dll` → `external\lib\windows\sqlitevec\`.

### Étape 3 — Build (un seul point d'entrée)

Depuis la racine du repo, dans une **Visual Studio Developer Prompt** :

```powershell
.\build_windows.ps1 -LlamaSourceDir "D:\Developpement\Windows" `
                    -QtPrefix "C:\Qt\6.8.0\msvc2022_64"
```

`build_windows.ps1` :

1. localise vcpkg (par défaut `<repo>\vcpkg`) et Qt (MSVC) ;
2. stager llama.cpp (sauf déjà présent) ;
3. fournit `vec0.dll` (sauf déjà présent) ;
4. `vcpkg install --triplet x64-windows` (mode manifeste) ;
5. `cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake -DCMAKE_PREFIX_PATH=<Qt>` ;
6. `cmake --build` (Release par défaut, `-Configuration Debug` pour le debug).

Options : `-CudaVersion 13.4`, `-SqliteVecMethod build`, `-VcpkgRoot C:\src\vcpkg`.

### Lien des DLL llama.cpp (import libs)

Le linker MSVC (`link.exe`) a besoin d'une import lib par DLL. Les release
officielles n'en fournissent pas. `cmake/PlatformWindows.cmake` :

- génère les `.lib` via **`llvm-dlltool`** (partie du toolchain LLVM de VS) si
  présent ; sinon
- lie les DLL **par chemin complet** (fallback) — `link.exe` résout les exports C
  non décorés de l'API llama.cpp et émet une import lib automatique.

### Déploiement post-build

`cmake/PlatformWindows.cmake` (`hodeia_windows_deploy`) copie dans le dossier de
l'exécutable :

| Catégorie        | Contenu                                                                                                         |
| ---------------- | --------------------------------------------------------------------------------------------------------------- |
| **llama.cpp**    | toutes les DLL `llama*.dll`, `mtmd.dll`, `ggml*.dll` (CPU variants + `ggml-cuda` + `ggml-vulkan`), `libomp.dll` |
| **CUDA runtime** | `cudart64_*.dll`, `cublas64_*.dll`, `cublasLt64_*.dll` (si `ggml-cuda.dll` présent)                             |
| **Vulkan**       | `vulkan-1.dll` (depuis le bin vcpkg)                                                                            |
| **OCR (vcpkg)**  | `tesseract*.dll`, `leptonica*.dll` + codecs image (libpng, libjpeg-turbo, openjpeg, gif, zlib)                  |
| **Extensions**   | `vec0.dll` (sqlite-vec)                                                                                         |
| **Ressources**   | `ui/themes/`, `Fonts/`, `tessdata/`                                                                             |
| **Qt runtime**   | via `windeployqt` (plugins, ICU, etc.)                                                                          |
| **Exécutable**   | `Hodeia.exe`                                                                                                    |

> **Runtime C/C++ (UCRT)** : `ucrtbase.dll` / `vcruntime140.dll` font partie de
> Windows 10/11. Pas besoin de shipper le VC++ Redist si la cible est Windows 10+.
> `windeployqt` les copie s'il détecte les VS Build Tools.

Résultat : un dossier autonome (~300-500 Mo selon les backends). L'utilisateur lance
`Hodeia.exe` — zéro installation, zéro dépendance MSYS2/MinGW.

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
└── windows/                        ← à remplir (MSVC)
    ├── llamacpp/                   ← dossier PLAT, tous les backends mélangés
    │   ├── llama.dll, llama-common.dll, mtmd.dll
    │   ├── ggml.dll, ggml-base.dll, ggml-rpc.dll
    │   ├── ggml-cpu-x64.dll, ggml-cpu-sse42.dll, ggml-cpu-<arch>…
    │   ├── ggml-cuda.dll           ← backend CUDA (+ cudart64/cublas64/cublasLt64)
    │   ├── ggml-vulkan.dll         ← backend Vulkan
    │   └── libomp.dll
    └── sqlitevec/
        └── vec0.dll
    # MuPDF / Tesseract / Leptonica / Vulkan : fournis par vcpkg (x64-windows)
    # → pas de fichiers in-tree nécessaires (MuPDF est statique, pas de DLL)
```

---

## Ordre recommandé de compilation

### Linux / macOS (compilation source)

1. **Leptonica** → dépendance de Tesseract
2. **Tesseract** → dépend de Leptonica
3. **MuPDF** → indépendant
4. **llama.cpp** → indépendant (compiler une fois par backend)
5. **sqlite-vec** → fichier unique, compilation instantanée
6. **Hodeia** → assemble tout

### Windows (MSVC)

Aucune compilation de dépendances natives : tout est fourni en binaire.

1. **vcpkg** → `vcpkg install` (manifest) apporte MuPDF (statique), Tesseract, Leptonica, Vulkan loader
2. **llama.cpp** → stager les DLL de release via `scripts\stage-llama.ps1` (CPU + Vulkan + CUDA)
3. **sqlite-vec** → `scripts\build-sqlitevec.ps1` télécharge le `vec0.dll` officiel
4. **Hodeia** → `build_windows.ps1` assemble tout (CMake détecte vcpkg + les DLL in-tree)

## Notes toolchain

- **macOS** : Clang d'Apple (`/usr/bin/clang`) supporte C++23 et OpenMP via `libomp`. Le LLVM Homebrew est recommandé pour une version plus récente.
- **Linux** : Clang/LLVM, linker `ld.lld`/`ld`. Les `.so` llama.cpp ont un RUNPATH `$ORIGIN` posé par `patchelf`.
- **Windows** : MSVC (`cl.exe`) via Visual Studio 2022, linker `link.exe`. Les DLL MuPDF/Tesseract/Leptonica/Vulkan viennent de **vcpkg** et sont déployées automatiquement par CMake en POST_BUILD. Les DLL llama.cpp sont liées via import libs (générées par `llvm-dlltool` si présent, sinon par chemin complet).
