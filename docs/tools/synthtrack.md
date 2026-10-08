# Pista sintética (`tools/synthtrack/`)

## O que é

Gera uma pista **inventada**, o "DR2Hook Ring", sem ler nada do jogo. A pista sai no mesmo formato que `python -m tools.uiview.track` exporta, então abre no [Track Explorer](track_explorer.md) e no viewer nativo (`tools/viewer3d/`). Também saem as texturas em PNG e as instâncias no formato de origem do jogo (`objects.ens`, `trees.bin`, `ornaments.bin`), que são o ponto de partida para tentar levar a pista ao jogo (veja [`demands/synthetic_track.md`](../demands/synthetic_track.md)).

Usos: dado de teste dos viewers sem o jogo instalado (os testes do viewer nativo usam esta pista) e primeiro material para a ideia de pista personalizada.

## Como gerar

```bash
python -m tools.synthtrack [-o build/uiview] [--seed 7]
```

Uma cópia gerada com a semente 7 já vem em [`examples/tracks/synthetic__dr2hook_ring/`](../../examples/README.md); um teste confere que ela continua igual à saída do gerador (regere com `-o examples`).

Leva ~2 s e ~6 MB. Grava `build/uiview/tracks/synthetic__dr2hook_ring/` e atualiza `build/uiview/data/tracks.js`, então a pista aparece na aba **Pistas** junto das exportadas do jogo. A mesma semente dá os mesmos bytes. Precisa de Pillow (`pip install .[view]`).

## O que tem

| Parte | Conteúdo |
| --- | --- |
| Traçado | Circuito fechado de ~2,1 km (Catmull-Rom por 16 pontos, amostra a cada 2 m), 10 m de largura, sobe e desce ~5 m |
| Terreno | 154 blocos de 100 m (grade de 4 m) com relevo, achatado perto da pista; patamar com paddock de terra e estacionamento de brita; barrancos de público por fora das três curvas mais longas; fundo de 2,6 km com mais de 65 535 vértices (índices de 32 bits) |
| Pista | Asfalto em trechos de 100 m (faixas brancas e marcas de pneu), zebras dos dois lados e brita por fora nas curvas |
| Objetos `e:` | Barreiras de concreto, muros de pneus nas curvas fechadas, alambrado (textura com alfa), pórtico de largada com banner, duas arquibancadas, e um tipo sem malha (marcador) |
| Ornamentos `o:` | Cones na curva mais fechada, placas de frenagem de 100 e 50 m e a decoração de `decor.py`: placas de publicidade atrás das barreiras das retas, paddock (tendas em pares, caminhões baú com propaganda, contêineres, banheiros químicos), torre de controle na reta dos boxes, fardos de feno por fora das curvas e pilhas de pneus pintados no ápice; público em grupos e guarda-sóis nos barrancos; pedras; mastros com bandeiras e postes de luz na reta e no paddock; ~110 carros no estacionamento; dois sítios (casas com telhado de telha, celeiro, caixa d'água, fardos); parque eólico de 10 torres nos morros além do terreno detalhado |
| Árvores `t:` | Árvores 3D low-poly (pinheiro: tronco e 5 andares de copa; bétula: tronco branco e copa em tufos) em 16 bosques e soltas, arbustos e uma serra distante `mnt_dist_*` (camada "Terreno distante") |
| Traçado de IA e limites | Portões a cada 20 m e duas linhas de IA (`default` e `wide`) |
| Câmeras do replay | `cameras.py`, em `routes[N].replay` do `track.json`: 14 câmeras de beira de pista (uma a cada ~150 m, por fora das curvas, 3,5 m acima do chão, sem cair em cima de peça nem perto de outro trecho), cada uma ligada por uma zona 70 m antes; as tomadas da largada com os nomes que o jogo procura (`initial_camera_r0`, `Grid_Start_Cam_00`, `pre_race_intro_001`, `pre_race_001`–`007`, `finish_line_camera_001`, `first_corner_001`, `camera_r0_firstlap_001`, `joker_r0`) e uma câmera fixa no ápice mais fechado; prismas em volta de torre, postes e banheiros. Pódio e serviço ficam de fora (dependem de outros arquivos) |
| Vagas de largada | `grids.py`, em `routes[N].grids`: onde o carro nasce, com as grades e os nomes de vaga do `grids.pssg` da Montalegre. `slot_0` do treino/contra-relógio 5 m antes da linha; 10 vagas de reset em fila (a cada 8 m); largada parada em fileiras de três (a cada 6,5 m, lados ±3,3 m); escalonada em zigue-zague (a cada 3 m, lados ±2,3 m); 4 vagas no paddock entre as tendas, de frente para a pista. Centro da vaga 0,5 m acima do asfalto (ou do chão no paddock) |
| Rotas | `route_0` completa; `route_1` usa o mesmo terreno, sem pórtico, arquibancadas e alambrado, com uma chicane de cones na reta (2253 instâncias) |
| Texturas | 26 procedurais, lados em potência de dois (64 a 512 px); 1 com alfa (alambrado). As cores lisas da decoração saem de uma paleta em faixas (`synth_paint_d`); propagandas, contêineres, fachada, feno, pedra, arbusto, público (paleta) e telha têm textura própria |

Contagens com a semente 7: 165 malhas de terreno, 152 249 vértices, 264 312 triângulos, 60 tipos, 2320 instâncias na `route_0` e 2253 na `route_1`. O `expected.json` guarda os números que o leitor Python (`unpack_geom`) lê de volta.

O `replay` tem três listas:

- **`cameras`:** `name`, `kind` (`trackside`, `static` ou `dolly`), `role` opcional, `s` (metros na pista), `pos` e `aim` (para onde olha no início). As `dolly` têm `path` e, se houver, `target` (Bézier cúbicos de 4 pontos encadeados) e `duration` em segundos.
- **`zones`:** `name`, `s`, `l` e `r` (bordas esquerda e direita, 0,5 m acima da pista), `lap` opcional (só vale nessa volta) e `switch`: lista de `{camera, p}` com as probabilidades somando 1 (`onboard_*`/`external_*` são as câmeras do carro).
- **`bounds`:** `name`, `corners` (4 cantos xz), `y0` e `y1`.

O viewer nativo mostra e deixa olhar por elas (tecla **C**); `scripts/research/ring_cameras.py` leva para o jogo.

O `grids` é uma lista de grades, cada uma com `name`, `role` (rótulo em português), `pos`, `fwd`, `s`, `slots` e `markers`. Cada vaga tem `name`, `pos` (centro do carro), `fwd` (unitário, para onde o carro aponta), `s`, `lat` (+ = esquerda) e `size` (largura e comprimento da caixa). As `markers` são nós de apoio sem carro (`car_grid_spline_time_trial_0`, `car_near_reset_spline_01`) e só têm `name`, `pos` e `fwd`. O viewer mostra as vagas (tecla **L**) e confere a altura delas sobre o chão (`--grid-check`); `scripts/research/ring_grids.py` leva para o jogo.

O referencial da pista (`cameras.Frame`) usa o passo real das amostras (2,00084 m, porque o circuito é dividido em partes iguais) e não o `STEP` nominal. Com o nominal, todo `s` negativo ficava ~0,4 m fora do lugar.

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

- **O jogo não lê estes arquivos direto.** Os scripts de porte (`scripts/research/`) convertem a saída em terreno, colisão, objetos, traçado, câmeras e tela de carga; o Ring carrega com nome próprio pela overlay. Pipeline: [`track_loading.md` §12](../reverse_engineering/track_loading.md).
- O relevo é uma soma de senos; a brita e as zebras são faixas coladas ao terreno, sem espessura.
- Os objetos não têm colisão nem física; só existem como malha e instância.
- Testada no jogo pela overlay (2026-10-06). As câmeras do replay e as vagas de largada foram instaladas em 2026-10-07; ainda não se viu um replay nem o carro nascendo nas vagas novas.
- Não se sabe qual grade cada modo usa (o treino deve usar `grid_time_trial_0/slot_0`). As vagas do paddock (`grid_compound_5#5`) também ficam sem prova.

## Onde está o código

| Arquivo | Papel |
| --- | --- |
| `tools/synthtrack/layout.py` | Traçado (Catmull-Rom), altura da pista, relevo e índice espacial da pista |
| `tools/synthtrack/textures.py` | As 26 texturas procedurais (Pillow) |
| `tools/synthtrack/meshes.py` | Caixa, cone, cilindro, face, pinheiro e bétula 3D, grade e matriz de instância |
| `tools/synthtrack/decor.py` | Malhas da decoração (placas, tendas, caminhões, contêineres, banheiros, torre, fardos, pneus, pedras, arbustos, público, guarda-sóis, eólicas, casas, celeiro, caixa d'água, bandeiras, postes, carros) |
| `tools/synthtrack/cameras.py` | Câmeras do replay, zonas e prismas; `Frame`, o referencial da pista (`s`, lado, altura) |
| `tools/synthtrack/grids.py` | Vagas de largada (`grids`) |
| `tools/synthtrack/build.py` | Monta tudo e grava; usa `pack_geom`, `pack_instances` e `write_index` do `tools.uiview` |
| `tools/synthtrack/tests/test_synthtrack.py` | Contagens, texturas, arquivos de origem lidos pelos leitores do exportador, edição de ida e volta com `track/edit.py`, mesma semente dá os mesmos bytes, câmeras do replay e vagas de largada (nomes, quantidades, caixas dentro do asfalto e sem sobreposição) |
