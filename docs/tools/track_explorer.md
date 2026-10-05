# Track Explorer e editor de pistas

## O que é

Aba **Pistas** do [DR2 UI Viewer](uiview.md). Abre uma pista exportada de `locations/*.nefs` e mostra o terreno, os objetos, as árvores, os limites da pista e a linha da IA. Permite mover, girar, apagar e duplicar objetos, e gravar as edições num `.nefs` **novo**, nunca na pasta do jogo.

## Como abrir

```bash
# exporta uma ou mais pistas (trechos do nome, separados por vírgula); --tracks vazio só reindexa
python -m tools.uiview.track --tracks montalegre,poland_rally_01 -o build/uiview

# serve build/uiview em http://127.0.0.1:8790/ e liga o botão "Salvar .nefs"
python -m tools.uiview.serve [--port 8790] [--root build/uiview] [--game PASTA]

# aplica à mão um arquivo de edições num .nefs NOVO (nunca dentro da pasta do jogo)
python -m tools.uiview.track.edit montalegre.edits.json -o build/uiview/saves/montalegre.nefs
```

A pasta do jogo vem de `tools.egodata.cli.DEFAULT_GAME`; troque com `--game`. O `serve` só escuta em `127.0.0.1` e se recusa a servir uma raiz dentro da pasta do jogo. O `track.edit` recusa uma saída dentro da pasta do jogo.

Sem o `serve`, o viewer abre direto do arquivo (`build/uiview/index.html`), mas o botão **Salvar .nefs** cai no download do JSON (veja [Fluxo de gravação](#fluxo-de-gravação)).

## Controles

| Ação | Controle |
| --- | --- |
| Orbitar / pan / zoom | Arrastar com o esquerdo / botão direito, do meio, ou Shift + esquerdo (na ferramenta Navegar) / roda |
| Andar | **WASD** (Shift = ×3) |
| Enquadrar | **F** enquadra o objeto selecionado; o botão **Enquadrar a pista** enquadra a rota |
| Selecionar | Clique num objeto; **Esc** tira a seleção |
| Ferramentas | **1** Navegar, **2** Mover (arrastar no chão; Shift sobe e desce), **3** Girar (arrastar para os lados gira em torno de Y) |
| Editar no painel | X/Y/Z digitados, botões de ±15° e ±90°, **Apagar**, **Restaurar** (volta ao original), **Duplicar** |
| Apagar | **Del** ou **H** |
| Duplicar | **Ctrl+D** ou o botão (só objetos de `objects.ens`) |
| Histórico | **Ctrl+Z** desfaz; **Ctrl+Y** ou **Ctrl+Shift+Z** refaz. Um histórico por rota, de até 300 passos |
| Gravar | **Salvar .nefs** (com o `serve`) ou **Exportar edições** (baixa `<pista>.edits.json`) |

## Camadas e rotas

A barra superior escolhe a pista e a **rota** (`route_N`, com a contagem de instâncias) e liga ou desliga as camadas:

| Camada | Padrão | Origem |
| --- | --- | --- |
| Terreno | ligado | blocos do `tracksplit.pssg` perto da rota, mais as malhas de fundo |
| Objetos | ligado | `objects.ens` (`e:`) e `ornaments.bin` (`o:`) |
| Árvores | ligado | `trees.bin` (`t:`) |
| Terreno distante | **desligado** | árvores de `trees.bin` cujo nome tem `_dist_`; desenhadas sem corte por distância |
| Limites da pista | ligado | portões de progresso (ponto esquerdo e direito) |
| Linha da IA | ligado | linhas da IA da rota (`default` é a principal) |

A **distância de desenho** (100 a 4000 m, padrão 700 m) corta objetos e árvores longe do alvo da câmera. Só uma rota é desenhada por vez. Cada rota aberta guarda as próprias edições e o próprio histórico, e **Salvar** e **Exportar** juntam as edições de todas as rotas abertas.

## O que lê e o que escreve

Lê o pacote da pista (`locations/<pista>.nefs`) só na exportação. A exportação grava em `build/uiview/`:

| Arquivo | Conteúdo |
| --- | --- |
| `tracks/<id>/track.json` | índice: rotas (portões, linha da IA, `ens_ids`), tipos de objeto, materiais → textura |
| `tracks/<id>/terrain_<n>.bin` | malhas do terreno e do fundo, por rota (DR2M) |
| `tracks/<id>/objects.bin` | malhas dos tipos de objeto, árvore e ornamento usados (DR2M) |
| `tracks/<id>/inst_<rota>.bin` | instâncias da rota: tipo, índice na origem e matriz (DR2I) |
| `tracks/<id>/tex/*.webp` | uma textura de cor por material |
| `data/tracks.js` | índice das pistas exportadas, lido pela página |

A especificação byte a byte de DR2M, DR2I, `track.json` e `edits.json` está em [`plans/viewer3d/formatos.md`](../plans/viewer3d/formatos.md). Os formatos de origem do jogo estão em [`reverse_engineering/track_formats.md`](../reverse_engineering/track_formats.md).

O `<id>` da pista é o nome do pacote com o que não é letra, dígito ou `_` trocado por `_` (Montalegre: `portugal__montalegre_rallycross`).

### `<pista>.edits.json`

Gerado por `tvDoc` e `tvEditList` (`web/js/trackview.js`):

```json
{ "format": "dr2-track-edits", "version": 1, "track": "<id>", "src": "locations/<pista>.nefs",
  "edits": [
    { "route": "route_0", "kind": "e", "type": "<tipo>", "index": 41, "deleted": false, "m": [12 floats], "m0": [12 floats] },
    { "route": "route_0", "kind": "e", "type": "<tipo>", "added": true, "src": 41, "index": -1, "deleted": false, "m": [12], "m0": [12] }
  ] }
```

- `kind` é a origem (`e`, `o` ou `t`); `type` é o nome do tipo sem o prefixo `k:`.
- `index` é o índice do objeto no arquivo de origem; `m` é a matriz nova e `m0` a original (3×3 com escala, depois a posição).
- Entram só objetos **movidos, girados ou apagados** (`deleted: true`) e **cópias** (`added: true`, `src` = índice do objeto copiado, `index: -1`). Uma cópia apagada na sessão não entra.

### Fluxo de gravação

1. **Salvar .nefs** envia o JSON para `POST /api/save` do `serve`.
2. O servidor chama `track/edit.py` (`apply`), que agrupa as edições por rota e origem e muda o arquivo de cada uma:

   | Origem | Edição |
   | --- | --- |
   | `objects.ens` (XML de texto) | A `TEMPLATETRANSFORM` da instância muda. Uma instância apagada sai do texto, e o arquivo é completado com espaços para manter o tamanho. Uma **cópia** é uma `TEMPLATEENTITYINSTANCE` nova logo depois da original, com `id="<original>_dupN"` e `instanceID` e `instance_tag` novos (o maior valor do arquivo + 1 + N). |
   | `ornaments.bin` (registros de 212 bytes), `trees.bin` (96 bytes) | A matriz e a posição do registro mudam. Um objeto apagado fica com a matriz zerada e `y = -10000`, porque a contagem de registros é fixa. Cópias são recusadas. |

3. `check_blocks` recusa a gravação se algum arquivo mudar de número de blocos de 64 KiB. Por isso as cópias precisam caber no espaço livre do último bloco de `objects.ens` (cerca de 80 cópias, segundo o registro da Montalegre).
4. `egodata.nefs_write.replace_files` copia o pacote uma vez para `build/uiview/saves/<pista>.nefs`, acrescenta os arquivos novos no fim e atualiza as tabelas de diretório. A introdução assinada (128 bytes) é mantida.
5. A página mostra o caminho gravado ou o erro.

Sem o servidor, o `fetch` falha, a página baixa `<pista>.edits.json` e avisa para rodar `python -m tools.uiview.serve`. Esse JSON pode ser aplicado depois com `python -m tools.uiview.track.edit`.

Registro anterior (não refeito para este documento): na Montalegre rallycross, um pacote com quatro edições foi gravado em 0,6 s (884 MB), relido sem erro, e todos os outros arquivos bateram por hash.

## Limites e o que não foi testado

- **Nenhum `.nefs` editado foi testado no jogo.** O jogo pode recusar um pacote alterado ou uma cópia com `instanceID` novo. Testar no jogo só com o OK do dono.
- **A colisão da pista não muda.** Ela fica em `track.jpk` (480 tiles `qt_*.vcqtc`), cujos vértices não foram decodificados. Um ornamento apagado ainda pode ser sólido. Nos objetos de física de `objects.ens`, a própria interface diz que o corpo rígido acompanha a instância (PROVÁVEL; não testado no jogo).
- **Pistas de rali são pesadas**: a Polônia tem ~9 milhões de vértices de terreno e ~305 mil instâncias. A exportação leva ~35–60 s e funciona; o viewer precisa de GPU de verdade e, provavelmente, de LOD ou blocos sob demanda. Elas não foram desenhadas no ambiente de teste.
- Os blocos densos de estrada (`batched_track.fx`) não declaram textura e saem numa cor fixa de terra. Os blocos de terreno misturado (`terrain_wsm_*`) usam só a primeira textura difusa.
- Exportar as 40 pistas custaria dezenas de GB; só as pistas pedidas são exportadas.
- Ornamentos e árvores não podem ser duplicados.
- Não há teste automático da edição na interface. O `scripts/dev/web_smoke.py` só confere que a primeira pista abre sem erro de JavaScript; a escrita do `.nefs` é coberta pelos testes de `tools/uiview/tests/test_track.py` (mover, apagar e duplicar em `objects.ens`, registros de `trees.bin` e `ornaments.bin`, índice fora do arquivo).

## Onde está o código

| Arquivo | Papel |
| --- | --- |
| `tools/uiview/track/export.py` | Exporta a pista: lê `objects.ens`, `trees.bin`, `ornaments.bin`, `tracksplit.pssg`, portões e linha da IA; grava DR2M, DR2I, texturas e `track.json`; `python -m tools.uiview.track` |
| `tools/uiview/track/edit.py` | Aplica um `.edits.json` num `.nefs` novo; `python -m tools.uiview.track.edit` |
| `tools/uiview/serve.py` | Servidor local com `POST /api/save` |
| `tools/uiview/mesh.py` | Formato DR2M (`pack_geom`, `unpack_geom`) |
| `tools/egodata/nefs_write.py` | `replace_files`: escreve o pacote novo |
| `tools/uiview/web/js/trackview.js` | Viewer: GPU, câmera, seleção, edição, histórico, exportar e salvar |
| `tools/uiview/tests/test_track.py` | Testes da exportação e da edição |
| `tools/viewer3d/` | Viewer nativo em C++, em construção; veja o [plano](../plans/viewer3d/README.md) |

## Formatos

- [`reverse_engineering/track_formats.md`](../reverse_engineering/track_formats.md): arquivos do jogo (`objects.ens`, `trees.bin`, `ornaments.bin`, `tracksplit.pssg`, colisão).
- [`plans/viewer3d/formatos.md`](../plans/viewer3d/formatos.md): arquivos exportados (DR2M, DR2I, `track.json`, `edits.json`).
