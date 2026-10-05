# Pista sintética (`tools/synthtrack/`)

## O que é

Gera uma pista **inventada**, o "DR2Hook Ring", sem ler nada do jogo. A pista sai no mesmo formato que `python -m tools.uiview.track` exporta, então abre no [Track Explorer](track_explorer.md) e no viewer nativo (`tools/viewer3d/`). Também saem as texturas em PNG e as instâncias no formato de origem do jogo (`objects.ens`, `trees.bin`, `ornaments.bin`), que são o ponto de partida para tentar levar a pista ao jogo (veja [`demands/synthetic_track.md`](../demands/synthetic_track.md)).

Usos: dado de teste dos viewers sem o jogo instalado (os testes do viewer nativo usam esta pista) e primeiro material para a ideia de pista personalizada.

## Como gerar

```bash
python -m tools.synthtrack [-o build/uiview] [--seed 7]
```

Leva ~2 s e ~6 MB. Grava `build/uiview/tracks/synthetic__dr2hook_ring/` e atualiza `build/uiview/data/tracks.js`, então a pista aparece na aba **Pistas** junto das exportadas do jogo. A mesma semente dá os mesmos bytes. Precisa de Pillow (`pip install .[view]`).

## O que tem

| Parte | Conteúdo |
| --- | --- |
| Traçado | Circuito fechado de ~2,1 km (Catmull-Rom por 16 pontos, amostra a cada 2 m), 10 m de largura, sobe e desce ~5 m |
| Terreno | 154 blocos de 100 m (grade de 4 m) com relevo, achatado perto da pista; paddock de terra; fundo de 2,6 km com mais de 65 535 vértices (índices de 32 bits) |
| Pista | Asfalto em trechos de 100 m (faixas brancas e marcas de pneu), zebras dos dois lados e brita por fora nas curvas |
| Objetos `e:` | Barreiras de concreto, muros de pneus nas curvas fechadas, alambrado (textura com alfa), pórtico de largada com banner, duas arquibancadas, e um tipo sem malha (marcador) |
| Ornamentos `o:` | Cones na curva mais fechada e placas de frenagem de 100 e 50 m |
| Árvores `t:` | ~420 pinheiros e bétulas de cartão (alfa) e uma serra distante `mnt_dist_*` (camada "Terreno distante") |
| Traçado de IA e limites | Portões a cada 20 m e duas linhas de IA (`default` e `wide`) |
| Rotas | `route_0` completa; `route_1` usa o mesmo terreno, sem pórtico, arquibancadas e alambrado, com uma chicane de cones na reta (995 instâncias) |
| Texturas | 16 procedurais, lados em potência de dois (64 a 256 px); 3 com alfa |

Contagens com a semente 7: 164 malhas de terreno, 151 989 vértices, 263 862 triângulos, 12 tipos, 1011 instâncias na `route_0` e 995 na `route_1`. O `expected.json` guarda os números que o leitor Python (`unpack_geom`) lê de volta.

## O que grava

```
tracks/synthetic__dr2hook_ring/
  track.json  terrain_0.bin  objects.bin  inst_route_{0,1}.bin  tex/*.webp  formato exportado (viewers)
  expected.json                                                           contagens do oráculo Python
  source/textures/*.png                                                   texturas sem perda
  source/route_{0,1}/objects.ens  ornaments.bin  trees.bin                formato de origem do jogo
  source/layout.json                                                      traçado amostrado (posição, tangente, curvatura)
```

O formato exportado está em [`plans/viewer3d/formatos.md`](../plans/viewer3d/formatos.md). Os arquivos de `source/route_N/` seguem o que `tools/uiview/track/export.py` lê e `track/edit.py` escreve:

- O `objects.ens` tem uma `TEMPLATEENTITYINSTANCE` por objeto `e:` com `uri="route_objecttypes.pssg#<tipo>"`, e sobra espaço livre no último bloco de 64 KiB para cópias.
- No `ornaments.bin` e no `trees.bin`, os registros ficam no layout de `BIN_LAYOUT`. O hash do tipo é um FNV-1a do nome, só um identificador estável: o jogo usa o `reference_id` dos `*_references.xml`.

## Limites

- **Não é uma pista do jogo.** Faltam o `tracksplit.pssg`, o `objects.pssg`, a colisão (`track.jpk`), o `progress_track.xml`, o `ai_track.xml` e o registro da pista nos menus. Veja [`demands/synthetic_track.md`](../demands/synthetic_track.md).
- O relevo é uma soma de senos; a brita e as zebras são faixas coladas ao terreno, sem espessura.
- Os objetos não têm colisão nem física; só existem como malha e instância.
- Não foi testada no jogo.

## Onde está o código

| Arquivo | Papel |
| --- | --- |
| `tools/synthtrack/layout.py` | Traçado (Catmull-Rom), altura da pista, relevo e índice espacial da pista |
| `tools/synthtrack/textures.py` | As 16 texturas procedurais (Pillow) |
| `tools/synthtrack/meshes.py` | Caixa, cone, árvore de cartão, grade e matriz de instância |
| `tools/synthtrack/build.py` | Monta tudo e grava; usa `pack_geom`, `pack_instances` e `write_index` do `tools.uiview` |
| `tools/synthtrack/tests/test_synthtrack.py` | Contagens, texturas, arquivos de origem lidos pelos leitores do exportador, edição de ida e volta com `track/edit.py`, mesma semente dá os mesmos bytes |
