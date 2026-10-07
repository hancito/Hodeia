#!/bin/bash
cd /run/media/hancito/VAULT/Developpement/Dependencies

# Configuration
HODEIA="/home/hancito/LLM_Stuff/Hodeia"
EXT_LIB="$HODEIA/external/lib/linux/sqlitevec"
EXT_INC="$HODEIA/external/include/SqliteVec"

DEPS_DIR="$(pwd)"
SV_SRC="$DEPS_DIR/sqlite-vec"

# ─── Verify sources exist ─────────────────────────────────────────
if [ ! -d "$SV_SRC" ]; then
  echo "ERROR: sqlite-vec package not found at"
  git clone --branch v0.1.10-alpha.4 https://github.com/asg017/sqlite-vec $SV_SRC
fi

# ─── Build loadable extension ─────────────────────────────────────
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Building sqlite-vec (loadable)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

rm -rf "$SV_SRC/dist"
cd "$SV_SRC"
make loadable -j$(nproc)

echo ""
echo "  sqlite-vec build done."

# ─── Copy artefacts into Hodeia/external/ ────────────────────────
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Installing artefacts into external/"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

mkdir -p "$EXT_LIB" "$EXT_INC"

cp "$SV_SRC/dist/vec0.so"   "$EXT_LIB/vec0.so"
cp "$SV_SRC/sqlite-vec.h"   "$EXT_INC/sqlite-vec.h"

echo "  ✓ vec0.so         → $EXT_LIB/"
echo "  ✓ sqlite-vec.h    → $EXT_INC/"
