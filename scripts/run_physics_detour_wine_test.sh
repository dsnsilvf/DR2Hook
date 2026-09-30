#!/usr/bin/env bash
# Compila test_physics_harness_detour_wine (MinGW) e executa sob Wine.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build-detour-wine"
EXE="${BUILD}/test_physics_harness_detour_wine.exe"

if ! command -v x86_64-w64-mingw32-g++ >/dev/null; then
  echo "x86_64-w64-mingw32-g++ required" >&2
  exit 1
fi
if ! command -v wine64 >/dev/null && ! command -v wine >/dev/null; then
  echo "wine required" >&2
  exit 1
fi
WINE=$(command -v wine64 2>/dev/null || command -v wine)
export WINEARCH=win64
export WINEDEBUG=-all

cmake -B "${BUILD}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  "${ROOT}"

cmake --build "${BUILD}" --target test_physics_harness_detour_wine -j"$(nproc)"

echo "=== Wine detour ABI test ==="
"${WINE}" "${EXE}"
echo "=== OK ==="
