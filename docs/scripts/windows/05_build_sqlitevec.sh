#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# Build sqlite-vec as a DLL for Windows/MSYS2
#
# Downloads the single-file source from github, compiles as DLL.
#
# Output: external/lib/windows/sqlitevec/vec0.dll
#
# Run inside MSYS2 CLANG-UCRT64 shell.
# ─────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PAPERIZE="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

EXT_LIB="$PAPERIZE/external/lib/windows"
BUILD_DIR="$(mktemp -d)"

# SQLite headers already in the project
SQLITE_INC="$PAPERIZE/external/include/SqliteVec"
SQLITE3_INC="$PAPERIZE/src/thrdprty"

echo "=== Building sqlite-vec ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# ─── Verify SQLite3 headers ─────────────────────────────────
if [ ! -f "$SQLITE_INC/sqlite-vec.h" ]; then
  echo "ERROR: sqlite-vec.h not found at $SQLITE_INC/"
  exit 1
fi
if [ ! -f "$SQLITE3_INC/sqlite3.h" ]; then
  echo "ERROR: sqlite3.h not found at $SQLITE3_INC/"
  exit 1
fi
echo "  ✓ SQLite headers found"

# ─── Download source ─────────────────────────────────────────
VEC_SRC="$BUILD_DIR/vec0.c"
curl -fSL -o "$VEC_SRC" \
  "https://github.com/asg017/sqlite-vec/raw/main/dist/vec0.c"

echo "  ✓ Downloaded vec0.c"

# ─── Compile ─────────────────────────────────────────────────
clang -shared \
  -I"$SQLITE_INC" \
  -I"$SQLITE3_INC" \
  -o "$BUILD_DIR/vec0.dll" "$VEC_SRC"

echo "  ✓ Compiled vec0.dll"

# ─── Install ─────────────────────────────────────────────────
mkdir -p "$EXT_LIB/sqlitevec"
cp "$BUILD_DIR/vec0.dll" "$EXT_LIB/sqlitevec/vec0.dll"

echo "  ✓ vec0.dll → $EXT_LIB/sqlitevec/"

# Cleanup
rm -rf "$BUILD_DIR"

ls -lh "$EXT_LIB/sqlitevec/vec0.dll"
echo ""
echo "Done."
