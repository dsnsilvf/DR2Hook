#!/usr/bin/env bash
# Roda os testes das ferramentas Python (egodata, dr2rec, uiview). Não precisa do jogo.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"
status=0
for suite in tools/egodata/tests tools/dr2rec/tests tools/uiview/tests; do
    echo "== ${suite}"
    python3 -m unittest discover -s "${suite}" -t . || status=1
done
exit "${status}"
