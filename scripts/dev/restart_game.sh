#!/usr/bin/env bash
# Fecha o DiRT Rally 2.0, instala as DLLs e os mods do build e abre o jogo de
# novo pela Steam (mantém as opções de inicialização configuradas nela).
#
#   scripts/dev/restart_game.sh            # fecha, instala, abre
#   scripts/dev/restart_game.sh --no-launch  # fecha e instala, sem abrir
#
# GAME_DIR e BUILD_DIR podem ser trocados por variáveis de ambiente.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GAME_DIR="${GAME_DIR:-/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0}"
BUILD_DIR="${BUILD_DIR:-$REPO/build-release}"
APP_ID=690790
LAUNCH=1
[[ "${1:-}" == "--no-launch" ]] && LAUNCH=0

if pgrep -x dirtrally2.exe >/dev/null; then
  echo "Fechando o jogo..."
  pkill -TERM -x dirtrally2.exe || true
  for _ in $(seq 1 40); do
    pgrep -x dirtrally2.exe >/dev/null || break
    sleep 0.5
  done
  if pgrep -x dirtrally2.exe >/dev/null; then
    echo "Não fechou em 20 s; forçando."
    pkill -KILL -x dirtrally2.exe || true
    sleep 2
  fi
fi

echo "Instalando em: $GAME_DIR"
cp "$BUILD_DIR/dxgi.dll" "$BUILD_DIR/dr2hook_core.dll" "$GAME_DIR/"
for mod in "$REPO"/mods/*/; do
  name="$(basename "$mod")"
  mkdir -p "$GAME_DIR/mods/$name"
  # settings.ini é do jogador: não sobrescrever.
  find "$mod" -maxdepth 1 -type f ! -name 'settings.ini*' -exec cp {} "$GAME_DIR/mods/$name/" \;
done

if [[ "$LAUNCH" == 1 ]]; then
  echo "Abrindo o jogo pela Steam..."
  if command -v steam >/dev/null; then
    steam "steam://rungameid/$APP_ID" >/dev/null 2>&1 &
  else
    xdg-open "steam://rungameid/$APP_ID" >/dev/null 2>&1 &
  fi
fi
echo "Pronto."
