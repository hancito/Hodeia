#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# Build sqlite-vec as a DLL for Windows/MSYS2
#
# Downloads the single-file source from GitHub, compiles as a shared
# DLL.  SQLite3 headers come from the in-tree amalgamation.
#
# Output: external/lib/windows/sqlitevec/vec0.dll
#
# Run inside MSYS2 UCRT64 shell.
# ─────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HODEIA="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

EXT_LIB="$HODEIA/external/lib/windows"
BUILD_DIR="$(mktemp -d)"

# Header locations (in-tree)
SQLITEVEC_INC="$HODEIA/external/include/SqliteVec"
SQLITE3_SRC="$HODEIA/external/sqlite3"

echo "══════════════════════════════════════════════════════════════"
echo "  sqlite-vec — MSYS2 UCRT64"
echo "══════════════════════════════════════════════════════════════"

# ─── Verify headers ──────────────────────────────────────────
if [ ! -f "$SQLITEVEC_INC/sqlite-vec.h" ]; then
  echo "ERROR: sqlite-vec.h not found at $SQLITEVEC_INC/"
  exit 1
fi
if [ ! -f "$SQLITE3_SRC/sqlite3.h" ]; then
  echo "ERROR: sqlite3.h not found at $SQLITE3_SRC/"
  exit 1
fi
echo "  ✓ Headers found"

# ─── Download source ─────────────────────────────────────────
VEC_SRC="$BUILD_DIR/vec0.c"
echo "  Downloading vec0.c ..."
curl -fSL -o "$VEC_SRC" \
  "https://github.com/asg017/sqlite-vec/raw/main/dist/vec0.c"
echo "  ✓ Downloaded"

# ─── Compile ─────────────────────────────────────────────────
clang -shared \
  -I"$SQLITEVEC_INC" \
  -I"$SQLITE3_SRC" \
  -o "$BUILD_DIR/vec0.dll" "$VEC_SRC"
echo "  ✓ Compiled"

# ─── Install ─────────────────────────────────────────────────
mkdir -p "$EXT_LIB/sqlitevec"
cp "$BUILD_DIR/vec0.dll" "$EXT_LIB/sqlitevec/vec0.dll"

# Cleanup
rm -rf "$BUILD_DIR"

ls -lh "$EXT_LIB/sqlitevec/vec0.dll"
echo ""
echo "Done."
