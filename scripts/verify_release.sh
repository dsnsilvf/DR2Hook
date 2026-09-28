#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

echo "================================================================="
echo "DR2Hook - Verificação Completa de Release e Quality Gates"
echo "================================================================="

# -----------------------------------------------------------------------------
# Gate 1: Verificação de Formatação de Código (clang-format)
# -----------------------------------------------------------------------------
echo "[Gate 1/5] Verificando formatação de código com clang-format..."
clang-format --dry-run --Werror tests/test_stability_soak.cpp src/proxy/dxgi_proxy.cpp include/dr2hook/proxy.h src/core/main.cpp
echo "-> Gate 1 APROVADO: Formatação estrita C++20 em conformidade."

# -----------------------------------------------------------------------------
# Gate 2: Validação Sintática C++20 via clang++
# -----------------------------------------------------------------------------
echo "[Gate 2/5] Validando sintaxe C++20 com clang++ -fsyntax-only..."
clang++ -std=c++20 -fsyntax-only \
  -Iinclude -Isrc \
  -Ivendor/lua \
  -Ivendor/imgui \
  -Ivendor/imgui/backends \
  -Ivendor/minhook/include \
  -I/usr/include/wine/windows \
  -Wno-extern-c-compat -Wno-class-conversion -Wno-nontrivial-memcall \
  tests/test_stability_soak.cpp
echo "-> Gate 2 APROVADO: Validação sintática C++20 concluída sem erros."

# -----------------------------------------------------------------------------
# Gate 3: Compilação de Todos os Alvos de Teste
# -----------------------------------------------------------------------------
echo "[Gate 3/5] Compilando suítes de teste (Memória, Lua, Overlay e Soak)..."
cmake -B build
cmake --build build --target test_memory_safety test_lua_engine test_overlay test_stability_soak -j"$(nproc)"
echo "-> Gate 3 APROVADO: Todos os executáveis de teste compilados."

# -----------------------------------------------------------------------------
# Gate 4: Execução das Suítes de Teste e Validação de 100% de Aprovação
# -----------------------------------------------------------------------------
echo "[Gate 4/5] Executando suítes de teste..."

echo "  Executando test_memory_safety..."
./build/test_memory_safety

echo "  Executando test_lua_engine..."
./build/test_lua_engine

echo "  Executando test_overlay..."
./build/test_overlay

echo "  Executando test_stability_soak (10.000 iterações)..."
./build/test_stability_soak

echo "-> Gate 4 APROVADO: 100% dos testes unitários e de soak aprovados."

# -----------------------------------------------------------------------------
# Gate 5: Geração e Validação do Pacote de Distribuição
# -----------------------------------------------------------------------------
echo "[Gate 5/5] Executando empacotamento e conferência de release..."
bash scripts/package_release.sh

VERSION="v0.1.0"
ZIP_FILE="dist/DR2Hook-${VERSION}.zip"
SHA_FILE="dist/DR2Hook-${VERSION}.sha256"

if [[ ! -f "${ZIP_FILE}" ]]; then
  echo "[ERRO] Arquivo de release ${ZIP_FILE} não foi gerado!" >&2
  exit 1
fi

if [[ ! -f "${SHA_FILE}" ]]; then
  echo "[ERRO] Arquivo de checksum ${SHA_FILE} não foi gerado!" >&2
  exit 1
fi

echo "  Validando integridade criptográfica SHA-256..."
(cd dist && sha256sum -c "DR2Hook-${VERSION}.sha256")

echo "  Validando árvore de arquivos no pacote compactado..."
ZIP_CONTENTS=$(unzip -l "${ZIP_FILE}")

for REQUIRED_FILE in \
  "DR2Hook-${VERSION}/dxgi.dll" \
  "DR2Hook-${VERSION}/mods/practice_mode/mod.json" \
  "DR2Hook-${VERSION}/mods/practice_mode/main.lua" \
  "DR2Hook-${VERSION}/README.txt" \
  "DR2Hook-${VERSION}/INSTALL.txt"; do
  if ! echo "${ZIP_CONTENTS}" | grep -q "${REQUIRED_FILE}"; then
    echo "[ERRO] Arquivo obrigatório '${REQUIRED_FILE}' ausente no arquivo .zip!" >&2
    exit 1
  fi
done

echo "  Validando ausência de dependências dinâmicas MinGW (libgcc, libwinpthread, libstdc++)..."
PKG_DLL="dist/DR2Hook-${VERSION}/dxgi.dll"
if [[ ! -f "${PKG_DLL}" ]]; then
  echo "[ERRO] Binário ${PKG_DLL} não encontrado para verificação de dependências!" >&2
  exit 1
fi

OBJDUMP_OUTPUT=$(x86_64-w64-mingw32-objdump -p "${PKG_DLL}")
for BANNED_DEP in "libgcc" "libwinpthread" "libstdc++"; do
  if echo "${OBJDUMP_OUTPUT}" | grep -i "${BANNED_DEP}" >/dev/null; then
    echo "[ERRO] Dependência dinâmica proibida detectada em ${PKG_DLL}: ${BANNED_DEP}" >&2
    exit 1
  fi
done
echo "  -> Nenhuma dependência dinâmica proibida encontrada em ${PKG_DLL}."

echo "-> Gate 5 APROVADO: Pacote de distribuição validado com integridade total."

echo "================================================================="
echo "TODOS OS QUALITY GATES FORAM ATENDIDOS COM SUCESSO! (EXIT CODE 0)"
echo "================================================================="
