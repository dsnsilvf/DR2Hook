#!/usr/bin/env bash
# Verifica dr2hook_core.dll (MinGW): prólogos BUG 1, thunks BUG 2/B4, .pdata SEH.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build-release"
DLL="${BUILD_DIR}/dr2hook_core.dll"
OBJDUMP="${OBJDUMP:-x86_64-w64-mingw32-objdump}"

if [[ ! -f "${DLL}" ]]; then
  cmake -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_SHARED_LINKER_FLAGS="-static -static-libgcc -static-libstdc++" \
    "${ROOT}"
  cmake --build "${BUILD_DIR}" --target dr2hook_core -j"$(nproc)"
fi

echo "=== dr2hook_core.dll harness verification ==="
echo "DLL: ${DLL}"
echo

echo "## PE sections"
"${OBJDUMP}" -h "${DLL}" | grep -E '\.text|\.pdata|\.xdata' || true
echo

echo "## BUG 1 — prologue header bytes (12 B) in image"
python3 - "${DLL}" <<'PY'
import sys
from pathlib import Path
dll = Path(sys.argv[1]).read_bytes()
patterns = {
    "tick_start": "48 8b c4 48 89 58 18 48 89 70 20 55",
    "integrator": "48 8b c4 48 89 58 10 55 48 8d a8 38",
    "commit": "48 8b c4 48 89 58 18 48 89 70 20 55",
    "post_physics_task": "48 8b c4 57 48 81 ec b0 00 00 00 33",
    "mov_r11_rsp_aux": "4c 8b dc 55 53 57 41 55 41 57 49 8d",
}
for name, hexstr in patterns.items():
    raw = bytes(int(x, 16) for x in hexstr.split())
    idx = dll.find(raw)
    print(f"  {name}: file offset 0x{idx:x}" if idx >= 0 else f"  {name}: NOT FOUND")
PY
echo

echo "## Thunk stubs (lea r11 ops only; r12/r13 untouched in stub)"
"${OBJDUMP}" -d -M intel "${DLL}" | sed -n '/<PhysicsHarness_DetourTickStart>:/,/<PhysicsHarness_DetourIntegrator>:/p'
echo

echo "## PhysicsHarness_DetourCommon (intel) — frame @ rsp+0x20, ops @ rsp+0xA8"
"${OBJDUMP}" -d -M intel "${DLL}" | sed -n '/<PhysicsHarness_DetourCommon>:/,/<PhysicsHarness_DetourTickStart>:/p'
echo

echo "## .pdata RUNTIME_FUNCTION for DetourCommon"
python3 - "${DLL}" <<'PY'
import struct, subprocess, sys
from pathlib import Path
dll_path = Path(sys.argv[1])
dll = dll_path.read_bytes()
pe_off = struct.unpack_from("<I", dll, 0x3C)[0]
img_base = struct.unpack_from("<Q", dll, pe_off + 24 + 24)[0]
sym_va = None
for line in subprocess.check_output(["x86_64-w64-mingw32-nm", str(dll_path)], text=True).splitlines():
    parts = line.split()
    if len(parts) >= 3 and parts[1] == "T" and parts[2] == "PhysicsHarness_DetourCommon":
        sym_va = int(parts[0], 16)
        break
if sym_va is None:
    print("  DetourCommon symbol not found", file=sys.stderr)
    sys.exit(1)
begin_rva = sym_va - img_base
num = struct.unpack_from("<H", dll, pe_off + 6)[0]
sec_off = pe_off + 24 + struct.unpack_from("<H", dll, pe_off + 20)[0]
for i in range(num):
    o = sec_off + i * 40
    if dll[o : o + 6] != b".pdata":
        continue
    ptr = struct.unpack_from("<I", dll, o + 20)[0]
    rs = struct.unpack_from("<I", dll, o + 16)[0]
    chunk = dll[ptr : ptr + rs]
    for j in range(0, len(chunk) - 11, 4):
        b, e, u = struct.unpack_from("<III", chunk, j)
        if b == begin_rva:
            print(f"  Begin=0x{b:x} End=0x{e:x} UnwindInfo=0x{u:x}")
            print(f"  raw: {chunk[j:j+12].hex()}")
            break
    else:
        print(f"  NOT FOUND for Begin=0x{begin_rva:x}", file=sys.stderr)
        sys.exit(1)
    break
PY
echo

echo "## r11 after skip_before in DetourCommon (must be empty)"
if "${OBJDUMP}" -d -M intel "${DLL}" \
  | sed -n '/<skip_before>:/,/<PhysicsHarness_DetourTickStart>:/p' \
  | grep -i r11; then
  echo "FAIL: r11 still referenced" >&2
  exit 1
else
  echo "  (none — OK)"
fi
echo

echo "=== OK ==="
