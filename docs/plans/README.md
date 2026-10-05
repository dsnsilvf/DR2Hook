# Planos de execução

Documentos escritos para **outra IA (ou pessoa) executar**, sem precisar da conversa que os originou. Cada plano diz o que mudar, em que ordem, como provar que funcionou e o que não fazer.

Data: 2026-10-05. Estado do código no momento: `main` em `30acb27` ou posterior.

## Índice

| Plano | O que entrega | Risco | Depende de |
| --- | --- | --- | --- |
| [01 — Python em camadas](01_python_camadas.md) | Separa formatos (core), conversão (assets), edição (edit) e aplicação (uiview) no lado Python | Médio | nada |
| [02 — Web em módulos](02_web_modulos.md) | Divide `trackview.js`, `carview.js` e `content.js` por responsabilidade e extrai o GL comum | Médio | nada (melhor depois do 01) |
| [03 — Documentação por ferramenta](03_docs_ferramentas.md) | `car_explorer.md`, `track_explorer.md`, `ui_screens.md`; o `uiview.md` vira visão geral | Baixo | nada |
| [Viewer 3D nativo](viewer3d/README.md) | Visualizador/editor em C++ com GPU, em 7 etapas, lendo o que o Python já exporta | Alto | só do `track.json`/DR2M/DR2I atuais (ver [formatos](viewer3d/formatos.md)) |

Ordem sugerida se for uma única IA: **03 → 01 → 02 → viewer3d**. Os planos 01, 02 e 03 mexem em arquivos diferentes e podem andar em paralelo em branches separadas, desde que 01 e 02 não alterem o mesmo arquivo ao mesmo tempo (o 02 só toca em `tools/uiview/web/`, o 01 só em Python).

## Regras que valem para todos os planos

Estas regras vêm do dono do projeto e **não podem ser relaxadas** por nenhum plano.

1. **Pasta do jogo é somente leitura.** `/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0`. Nenhuma ferramenta, teste ou script grava nela. Saídas vão para `build/` (ignorado pelo git); `.nefs` editados vão para `build/uiview/saves/`.
2. **Não testar um `.nefs` modificado dentro do jogo** sem OK explícito do dono. Os planos validam a escrita só por testes e por comparação de bytes.
3. **Não dar `git push`, não trocar a URL do `origin`, não apagar branches remotas.** O dono publica.
4. **Não mudar o comportamento do Car Model Explorer.** O plano 02 refatora o código dele sem alterar o que faz; qualquer diferença visível é bug.
5. **Commits pequenos**, um por passo do plano, em português, no estilo `tipo(escopo): descrição` (`refactor(uiview): ...`, `docs: ...`, `feat(viewer3d): ...`). Termine a mensagem com a linha de co-autoria que a sua ferramenta exige.
6. **Cada passo termina com os testes verdes** e, nos planos 01 e 02, com a comparação de saída (abaixo) igual. Não avance com um passo quebrado.
7. Ao falar de pessoas sem pronome declarado, usar pronomes neutros.

## Como trabalhar

```bash
git switch -c refactor/<plano> main          # branch própria a partir da main
bash scripts/dev/test_tools.sh               # 48 testes Python; todos devem passar sempre
```

Antes de começar, leia: [`README.md`](../../README.md), [`docs/README.md`](../README.md), [`docs/tools/uiview.md`](../tools/uiview.md) e [`docs/reverse_engineering/track_formats.md`](../reverse_engineering/track_formats.md).

### Prova de que a saída não mudou (planos 01 e 02)

`scripts/dev/golden_export.py` exporta a pista `montalegre` e o carro `037` e imprime um hash por arquivo (os `.json` entram com as chaves ordenadas, porque a ordem varia entre execuções). Leva ~30 s e usa ~55 MB em `build/`.

```bash
python3 scripts/dev/golden_export.py build/golden_antes  > build/antes.txt     # ANTES de mexer
# ... faz o passo ...
python3 scripts/dev/golden_export.py build/golden_depois > build/depois.txt
diff build/antes.txt build/depois.txt && echo IGUAL
```

Se o `diff` não for vazio, o passo mudou o formato de saída: desfaça ou explique no commit por que a mudança era intencional (nos planos 01 e 02 nunca é).

## Como reportar

Ao terminar cada passo, deixe no corpo do commit: o que moveu, o resultado de `test_tools.sh` e se o `diff` do golden deu `IGUAL`. Ao terminar o plano, atualize a coluna "Estado" abaixo.

| Plano | Estado |
| --- | --- |
| 01 — Python em camadas | não iniciado |
| 02 — Web em módulos | não iniciado |
| 03 — Documentação por ferramenta | concluído na branch `claude/charming-cerf-74fjw5` (comandos conferidos só com `--help`: o jogo não estava no ambiente) |
| Viewer 3D nativo | etapas 1–7 feitas na branch `claude/charming-cerf-74fjw5` (Mesa llvmpipe no Xvfb, pista sintética de `tools/synthtrack`); falta conferir na Montalegre e na GPU do dono |
