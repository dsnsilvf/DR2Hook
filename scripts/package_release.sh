#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
VERSION="v0.1.0"
DIST_DIR="${ROOT_DIR}/dist"
PKG_DIR="${DIST_DIR}/DR2Hook-${VERSION}"
ZIP_FILE="${DIST_DIR}/DR2Hook-${VERSION}.zip"
SHA_FILE="${DIST_DIR}/DR2Hook-${VERSION}.sha256"
BUILD_DIR="${ROOT_DIR}/build-release"

echo "================================================================="
echo "DR2Hook - Automação de Empacotamento de Release (${VERSION})"
echo "================================================================="

# 1. Compilação em modo Release para Windows x64 (MinGW)
echo "[1/5] Compilando dxgi.dll em modo Release via x86_64-w64-mingw32..."
cmake -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_SHARED_LINKER_FLAGS="-static -static-libgcc -static-libstdc++" \
  "${ROOT_DIR}"

cmake --build "${BUILD_DIR}" --target dxgi --target dr2hook_core -j"$(nproc)"

# Localizar binário da proxy DLL gerada
DXGI_BIN=""
if [[ -f "${BUILD_DIR}/dxgi.dll" ]]; then
  DXGI_BIN="${BUILD_DIR}/dxgi.dll"
elif [[ -f "${BUILD_DIR}/libdxgi.dll" ]]; then
  DXGI_BIN="${BUILD_DIR}/libdxgi.dll"
else
  echo "[ERRO] Binário dxgi.dll não encontrado em ${BUILD_DIR}!" >&2
  exit 1
fi

# 2. Montar árvore de diretórios de distribuição limpa
echo "[2/5] Montando estrutura de distribuição em ${PKG_DIR}..."
rm -rf "${PKG_DIR}" "${ZIP_FILE}" "${SHA_FILE}"
mkdir -p "${PKG_DIR}/mods/practice_mode"

# Copiar proxy DLL e o core recarregável para a raiz do pacote
cp "${DXGI_BIN}" "${PKG_DIR}/dxgi.dll"
CORE_BIN=""
if [[ -f "${BUILD_DIR}/dr2hook_core.dll" ]]; then
  CORE_BIN="${BUILD_DIR}/dr2hook_core.dll"
elif [[ -f "${BUILD_DIR}/libdr2hook_core.dll" ]]; then
  CORE_BIN="${BUILD_DIR}/libdr2hook_core.dll"
else
  echo "[ERRO] Binário dr2hook_core.dll não encontrado em ${BUILD_DIR}!" >&2
  exit 1
fi
cp "${CORE_BIN}" "${PKG_DIR}/dr2hook_core.dll"
if command -v x86_64-w64-mingw32-strip >/dev/null 2>&1; then
  x86_64-w64-mingw32-strip "${PKG_DIR}/dxgi.dll" "${PKG_DIR}/dr2hook_core.dll"
else
  strip "${PKG_DIR}/dxgi.dll" "${PKG_DIR}/dr2hook_core.dll"
fi

# Copiar mod de treino padrão (Practice Mode)
cp "${ROOT_DIR}/mods/practice_mode/mod.json" "${PKG_DIR}/mods/practice_mode/"
cp "${ROOT_DIR}/mods/practice_mode/main.lua" "${PKG_DIR}/mods/practice_mode/"

# 3. Gerar arquivos de documentação para o pacote
echo "[3/5] Gerando README.txt e INSTALL.txt..."

cat << 'EOF' > "${PKG_DIR}/README.txt"
================================================================================
DR2Hook - Mod Loader & Practice Mode para DiRT Rally 2.0 (v0.1.0)
================================================================================

O DR2Hook é um Mod Loader leve, de baixo processamento por frame (< 0.2ms) e
seguro para o DiRT Rally 2.0 (dirtrally2.exe x64 / DirectX 11 / EGO Engine).

PRINCIPAIS RECURSOS:
--------------------
1. Practice Mode (Treino com Savestates em tempo real):
   - [F5] : Salva a posição, velocidade linear, rotação e suspensão do carro.
   - [F6] : Restaura imediatamente o carro no checkpoint gravado, aplicando
            estabilização de suspensão (sag 0.35) e amortecimento angular nulo
            para evitar capotamentos ou perda de tração.
2. In-Game Menu & HUD Overlay (Dear ImGui):
   - [Insert] : Alterna a visibilidade do menu de configurações e status.
   - [F7] : Restaura o checkpoint com momentum (velocidade linear e angular).
   - [F8] : Recarrega dr2hook_core.dll sem fechar o jogo. O checkpoint em
            memoria e o estado Lua sao descartados. A dxgi.dll nao recarrega.
   - Notificações HUD estilo toast no canto da tela informando o status das ações.
3. Fair Play First (Anti-Cheat Nativo em C++):
   - Escritas de memória e restauração de savestates possuem bloqueio rígido
     (hard-lock) automático quando eventos competitivos ou conexão RaceNet
     (Carreira, Desafios Diários/Semanais, Clubes Ranqueados) estão ativos.
   - Uso 100% liberado em DirtFish (área livre), Tomada de Tempo (Time Trial)
     e Campeonatos Customizados offline.
4. Motor Extensível Lua:
   - Novos mods podem ser facilmente adicionados na pasta 'mods/'.

Para detalhes completos de instalação e suporte a Linux/Steam Deck,
consulte o arquivo INSTALL.txt.
================================================================================
EOF

cat << 'EOF' > "${PKG_DIR}/INSTALL.txt"
================================================================================
DR2Hook - Guia de Instalação, Configuração e Desinstalação
================================================================================

1. INSTALAÇÃO NO WINDOWS 10 / 11 (STEAM)
----------------------------------------
1. Localize a pasta de instalação do DiRT Rally 2.0:
   - No cliente Steam, abra a sua Biblioteca.
   - Clique com o botão direito em "DiRT Rally 2.0" -> Gerenciar -> Explorar arquivos locais.
   - A pasta aberta deve conter o executável principal "dirtrally2.exe".
2. Extraia o conteúdo deste arquivo compactado (.zip) diretamente na pasta do jogo.
   A estrutura final da pasta do jogo deve ficar exatamente assim:
     [Pasta do DiRT Rally 2.0]/
     ├── dirtrally2.exe
     ├── dxgi.dll
     ├── dr2hook_core.dll
     ├── mods/
     │   └── practice_mode/
     │       ├── mod.json
     │       └── main.lua
     ├── README.txt
     └── INSTALL.txt
3. Inicie o jogo normalmente através da Steam.
4. Como verificar se está funcionando:
   - Entre no modo DirtFish ou Time Trial.
   - Pressione [Insert] para abrir a interface in-game do DR2Hook.
   - Pressione [F5] para gravar um ponto de retorno e [F6] para retornar a ele.
   - Pressione [F8] para recarregar dr2hook_core.dll sem fechar o jogo
     (o checkpoint em memoria e descartado). dxgi.dll em si so troca ao reiniciar.
   - Um arquivo de log "dr2hook.log" será gerado na pasta do jogo informando o status.

2. INSTALAÇÃO NO LINUX / STEAM DECK (STEAM PROTON)
--------------------------------------------------
1. Extraia o conteúdo deste pacote na pasta do jogo conforme as instruções acima.
2. No cliente Steam (no Modo Desktop ou Modo Jogo do Steam Deck):
   - Clique com o botão direito no "DiRT Rally 2.0" -> Propriedades...
   - Na aba "Geral", localize a caixa de texto "Opções de Inicialização" (Launch Options).
   - Cole exatamente a seguinte linha de comando:
       WINEDLLOVERRIDES="dxgi=n,b" %command%
3. Inicie o jogo. Essa configuração instrui o Proton a carregar a biblioteca nativa
   "dxgi.dll" presente na pasta do jogo antes de recorrer às bibliotecas padrão do Wine.

3. DESINSTALAÇÃO
----------------
Para remover o DR2Hook:
1. Exclua "dxgi.dll", "dr2hook_core.dll" e qualquer "dr2hook_core.*.dll" da pasta raiz do jogo.
2. (Opcional) Exclua a pasta "mods/" e o arquivo "dr2hook.log".
3. Caso utilize Linux/Steam Deck, remova o parâmetro WINEDLLOVERRIDES das Opções de Inicialização.

4. RESOLUÇÃO DE PROBLEMAS
-------------------------
- Jogo não abre ou encerra logo após o início:
  Certifique-se de que os pacotes redistribuíveis do Visual C++ e DirectX estejam atualizados.
  Consulte o arquivo "dr2hook.log" gerado na pasta do jogo para mensagens de erro.
- O overlay ou teclas de atalho não funcionam:
  Certifique-se de que softwares de captura ou overlays de terceiros (ex: RivaTuner)
  não estejam interceptando exclusivamente os hooks do DirectX 11.
================================================================================
EOF

# 4. Geração do arquivo compactado (.zip)
echo "[4/5] Compactando pacote em ${ZIP_FILE}..."
cd "${DIST_DIR}"
if command -v zip >/dev/null 2>&1; then
  zip -q -r "DR2Hook-${VERSION}.zip" "DR2Hook-${VERSION}"
else
  cmake -E tar cf "DR2Hook-${VERSION}.zip" --format=zip "DR2Hook-${VERSION}"
fi

# 5. Geração do hash de integridade SHA-256
echo "[5/5] Calculando checksum SHA-256..."
sha256sum "DR2Hook-${VERSION}.zip" > "DR2Hook-${VERSION}.sha256"

echo "================================================================="
echo "Empacotamento concluído com sucesso!"
echo "Pacote:   ${ZIP_FILE}"
echo "Checksum: $(cat "${SHA_FILE}")"
echo "================================================================="
