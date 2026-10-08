# Carregamento de pista (RE, 2026-10-06)

Como o jogo transforma "location/track/route" em arquivos lidos, objetos PSSG e recursos da GPU.
Tudo aqui foi visto no binário (`scripts/research/exe_re.py`) e/ou medido com a LoadProbe numa
corrida em `portugal/montalegre_rallycross/route_0`. O que ainda é hipótese está marcado.

## 1. Visão geral

```
catalogues/base.ctpk (game_1.dat)      banco principal: locations, tracks, rotas, carros, textos
        │  CatalogueManager
        ▼
0x140487480  monta a tabela de tokens (%location% %track% %route% %skyTime% %gameMode% ...)
        │  0x140c5ff80(mgr, "tracks/track_loader.xml", 2, tokens, ...)
        ▼
raceload.jpk (game_1.dat, JPAK)  →  track_loader.xml (BXML): <dataset> com ~200 entradas
        │  cada entrada: <xml|binary|pssg|xmlreader|xmltree|dataset|call processor= filename= pool= userdata=>
        ▼
processadores (TrackManagerPlugin, OrnamentsPlugin, TreesPlugin, EntityManager, ...)
        │  lêem do sistema de arquivos virtual: o .nefs da location montado
        ▼
PSSG (0x1408e7360) → objetos por classe (0x1408f3890) → D3D11 (CreateBuffer/CreateTexture2D)
```

Tem pista "normal" (`tracks/track_loader.xml`) e pista gerada por tiles
(`tracks/trackgen_loader.xml`, caminhos `tile_sets/%location%/...`). As duas chamadas ficam em
0x140487480 (0x48cc57 e 0x48d0e6).

## 2. `raceload.jpk` e `base.ctpk`

Ambos estão em `game/game_1.dat`, que é um NeFS sem cabeçalho (o cabeçalho fica embutido no exe;
`NefsArchive.open_headless`). Tamanhos: `raceload.jpk` com 218.288 bytes e
`catalogues/base.ctpk` com 1.120.352 bytes.

JPAK: `"JPAK"`, u32 0, u32 número de entradas, u32 0x10; tabela em 0x20 com 32 bytes por entrada
`(u32 nome, u32 tamanho, u32 offset, u32 tamanho, 16 bytes zerados)`; os nomes vêm depois da
tabela. O `raceload.jpk` tem 19 entradas:

| entrada | conteúdo |
|---|---|
| `track_loader.xml` | manifesto da pista (BXML, magia `\x01BXML`) |
| `trackgen_loader.xml` | mesmo papel para `tile_sets/` |
| `car_loader.xml`, `car_uloader.xml`, `interior_loader.xml`, `hdm_loader.xml`, `character_loader.xml`, `audio_loader.xml`, `sky_loader.xml`, `patch_up.xml` | outros manifestos |
| `cameras_common.xml`, `cabin_shake_effect.xml`, `*.hdm`, `characterlodsettings.xml` | configuração |

Trechos do `track_loader.xml`, com os tokens ainda sem troca:

```
pssg processor=TrackManagerPlugin filename=tracks/locations/%location%/%track%/tracksplit.pssg
     pool=PSSG_TRACK userdata=Infield bindAfterLoad=%bindTrackAfterLoad% bindIndicesAfterLoad=%bindTrackAfterLoad%
pssg processor=TrackManagerPlugin filename=tracks/locations/%location%/%track%/%route%/routesplit.pssg userdata=RouteTrack
pssg processor=OrnamentsPlugin    filename=tracks/locations/%location%/%track%/objects.pssg userdata=geometry pool=PSSG_ORNAMENT
pssg processor=EntityManager      filename=tracks/locations/%location%/%track%/%route%/objects.ens userdata=;ents;object;0:
binary processor=StreamingPlugin  filename=tracks/locations/%location%/%track%/tracksplit.bin userdata=streaming_grid_info
binary processor=TrackManagerPlugin filename=tracks/locations/%location%/%track%/landscape.heightfield userdata=heightdata_bespoke
binary processor=TrackManagerPlugin filename=tracks/locations/%location%/%track%/%route%/track.vis pool=VISIBILITY_SYSTEM
xml processor=ai filename=tracks/locations/%location%/%track%/%route%/%gameModeAITrack%ai_track.xml userdata=aiTrack
```

Isso explica a lista de arquivos lidos na corrida: cada item do `.nefs` vem de uma linha desse
manifesto. Os arquivos opcionais que não existem na Montalegre (ex.: `routesplit.pssg`,
`iwater.pssg`, `skids*.pssg`) parecem ser pulados (não há erro no log).

O `base.ctpk` (magia `CTPK`, versão 2) tem as strings de location, track e rota (ex.:
`montalegre_rallycross_route_0_spline`), mas o formato ainda não foi decifrado. É ele que define
quais pistas existem; para uma pista nova com nome próprio, seria preciso mexer nele.

### Dica externa (Discord, filipe411, 2026-10-06)

"Mudar os caminhos no raceload.jpk para carregar dados externos, sobrepondo o NeFS; para isso é
preciso mexer em um ou dois outros NeFS e no exe. O base.ctpk é o banco principal, com formato
desconhecido." Confere com o que achamos: o raceload.jpk está no `game_1.dat`, cujo cabeçalho
mora no exe (por isso a menção ao exe). Com hooks em memória, não é preciso editar nada em disco:
dá para trocar a tabela de tokens ou o nome do arquivo no pedido (§3).

## 3. Funções

| endereço | o que faz |
|---|---|
| `0x140487480` | monta os tokens da corrida e pede `tracks/track_loader.xml` ou `trackgen_loader.xml` |
| `0x140c5ff80(mgr, arquivo, flags, tokens, a5, a6)` | pedido de dataset a partir de um arquivo; mesmo job de 0x3f0 bytes do `c5fda0` |
| `0x140c5fda0(mgr, dados, tamanho, rótulo, flags, a6, a7)` | pedido de dataset a partir de XML/BXML em memória |
| `0x140c5fd90()` | gerenciador de datasets (global) |
| `0x1404979d0` | ramdisc → patchup_ot → `raceload.jpk` (RaceSetup) → `0x14048d950` |
| `0x14048d950` | `track.jpk` (colisão) e tile_sets |
| `0x140499650` | TrackLoader::OnEvent (userdata `raceload`, `;track;...` uma vez por item do track.jpk) |
| `0x1408e7360` | carga de um PSSG (desc+0x10 = nome) |
| `0x1408f3890` | criação de objeto PSSG (elemento+0x28 = descritor da classe; nome em desc+8) |
| `0x1408c5c30` | registro de classe PSSG (349 classes) |

Pilhas típicas (RVA):

- leitura do `.nefs`: `81cea2←81d11b←81bf98←80762b←8231bf←84ffff` (thread de I/O, blocos de 512 KB em ordem de offset);
- PSSG: `8e73ca←e247bf←e256b9←84ffff` (job);
- VB: `913632←8d1a7d←8d1d31←8edd73←8f3901←8ee095←8e73ca`;
- IB: `91385f←8d1ae9←8d1ed1←8efcbf←8f3901`.

## 4. Medições na Montalegre

- Disco: de 4,2 s a 9,8 s após abrir o pacote, 68 itens, 883 MB. Ordem: track.jpk → XMLs →
  route_0 → objectstextures/treestextures → grids → objects → patchup_ot (823 MB) → trees, niwater,
  ground_cover → tracksplit (561 MB) → route_* → objects.ens → route_patchup_ot.
- Parse de cada PSSG: tracksplit 1281 ms, patchup_ot 1699 ms, objects 372 ms.
- tracksplit: RENDERSTREAM 11332, DATABLOCK 2578, RENDERINDEXSOURCE/RENDERDATASOURCE 2075,
  SEGMENTSET/RENDERNODE 1138, NODE 471, SHADERINSTANCE 187, TEXTURE 130.
- GPU: primeiro 2075 IB (IMMUTABLE, u16), depois 2578 VB com bind `VERTEX_BUFFER|UNORDERED_ACCESS`
  (usage DEFAULT). Hipótese: a GPU reescreve vértices do terreno (ligado ao chão "lavado"?).

## 5. LoadProbe

Fica no `dxgi.dll` (`src/core/load_probe.cpp`) e só instala se existir `dr2hook_loadprobe.ini`
ao lado do exe:

```
enabled=1
out=Z:\home\...\captures\loadprobe
stack=16
gpu=0            ; 1 = buffers/texturas/sombreadores (deixa a carga ~6x mais lenta)
shaders=0
exe=1            ; hooks no exe (checam o prólogo antes)
head=48
stop_after_start=20
```

Ela grava `trace_<data>.tsv`, com tipos `open`, `read`, `attr`, `mark`, `tevent`, `provide`,
`pssg+`/`pssg-`, `submit`, `subfile`, `buf`, `tex2d`, `layout` e `class`. Também grava
`modules_<data>.txt`, `manifests/` (manifestos do `c5fda0`) e `shaders/`.

## 6. Tokens da corrida (medidos, 2026-10-06)

A tabela passada ao `c5ff80` é um hash map (inserção em `0x1401d5de0`). A lista encadeada tem o
sentinela em tabela+0x20, o próximo nó em nó+8, a chave (`char*`) em nó+0x20 e o valor em
nó+0x58. Cada valor é uma string fixa de 0x100 bytes. A LoadProbe grava uma linha `token` por
entrada. Na Montalegre via AutoStage, `tracks/track_loader.xml` recebeu 44 tokens, entre eles:

| token | valor | token | valor |
|---|---|---|---|
| `location` | `portugal` | `ppLocation` | `portugal/` |
| `track` | `montalegre_rallycross` | `ppTrack` | `montalegre_rallycross/` |
| `route` | `route_0` | `aoRoute` | `route_0` |
| `routeID` / `route_number` | `0` | `tutorials` | `montalegre_rallycross` |
| `skyTime` / `skyCloud` / `skyVariant` | `midday` / `dry` / `00` | `gameMode` | `tool` |
| `bindTrackAfterLoad` | `1` | `bindObjectIndexData` | `0` |
| `entitiesEnabled` / `crowdEnable` | `1` / `1` | `direction` | `fwd` |

Os `gameMode*` vazios fazem as linhas `%gameModeAITrack%ai_track.xml` virarem `ai_track.xml`.
Trocar `location`/`track` (e `pp*`) na tabela muda todos os caminhos do manifesto de uma vez.

## 7. Sistema de arquivos virtual: camadas de pasta

`0x1403a3d70` cria o I/O no boot. Ele monta `/data` com `game/game.nefs` e as partes sem
cabeçalho (`game.dat`, `game_1.dat`) e também monta **pastas soltas** do disco em
`/data/video` e `/data/input`. A camada de pasta é criada assim:

```
camada = alocador->vtable[0x18](0x128, 8)       ; 0x128 bytes
0x1407fcd40(camada)                             ; construtor da camada de pasta
0x14080d110(caminho, 0x104, base, "video")      ; junta pasta do jogo + nome
0x140813860(camada, alocador, caminho)          ; raiz da camada no disco
0x140815840(iosys, camada, "/data/video", prioridade, &montagem, 0)
```

A montagem da pista (`0x140505ae0`, chamada por vtable na thread de montagem) usa o mesmo
caminho quando o pedido tem o bit 8 em pedido+0x14: monta uma pasta (base em pedido+0x28, nome em
pedido+0x58) no ponto virtual pedido+0xda com prioridade 1. Sem o bit, monta o pacote
(`0x14081a170`/`81a180`/`81a190`). Os logs internos dessa função falam em "Patching %s from disc"
e "Patching %s from content". Hipótese: o motor já sabe servir uma location a partir de uma
pasta (modo de desenvolvimento), e esse seria o caminho para arquivos soltos sem editar
NeFS nem exe.

### Overlay de teste (CONFIRMADO, 2026-10-06)

A LoadProbe monta uma camada de pasta própria em `/data` com a mesma sequência, logo depois do
pedido de `tracks/track_loader.xml` (chaves `overlay_dir`, `overlay_mount` e `overlay_flag` no
`dr2hook_loadprobe.ini`):

```
alocador = 0x140865180(0x1400b2730(), "INPUT_IO_LAYER")
camada   = HeapAlloc(0x128, zerado); 0x1407fcd40(camada)
0x140813860(camada, alocador, "00000000:/dr2hook_overlay")   ; dispositivo 0 = pasta do jogo
0x140815840(*0x1416925f8, camada, "/data", 1, &entrada, 0)   ; sem insertBefore: entra na frente
```

A camada consulta `<jogo>\dr2hook_overlay\...` com `GetFileAttributesW` e abre com `CreateFileW`.
A sonda reescreve esse prefixo para a pasta do `overlay_dir`, que fica fora da pasta do jogo.

Rodada r6 (`trace_20261006_120232.tsv`, Montalegre): `init=0 mount=0`. Houve 309 consultas na
pasta, 308 não acharam e caíram para o `.nefs`, e uma achou e abriu
`tracks/locations/portugal/montalegre_rallycross/lighting_midday_dry_00.xml`. A cópia tinha o
sol e a névoa em magenta, e o cenário inteiro ficou magenta.

- A camada de pasta montada por último tem prioridade sobre o `.nefs` no mesmo ponto.
- Arquivo ausente na pasta é lido do pacote.
- Os caminhos na pasta são os caminhos virtuais sob `/data` (`tracks/locations/<loc>/<track>/...`).

## 8. Em aberto

- Formato do `base.ctpk` (resolvido na §10.1).
- Decodificador do BXML. O `bxml.py` do repo lê os XML de rota (`progress_track.xml`), mas não o
  `surface_materials.xml` do `game_1.dat` (variante `\0BXML`; lido por `strings`).
- Quem liga o bit 8 do pedido de montagem e o que é o "Patching from disc".
- Se o cache de dataset guarda os arquivos da camada entre corridas. Passam pela camada (medido): `tracksplit.pssg`, `route_0/objects.ens`, `trees.bin`, `ornaments.bin`, `track.vis`, `progress_track.xml`, `ai_track.xml`, `*.cqtc`, `landscape.heightfield`, `grass.grs`, `drivable_entities.jpk` e, na pista nova, também o
  `route_0/track.jpk` (§10.4).
- Formato do `track.vis` (resolvido na §9.9) e da colisão (§9.11).
- O u32 de bits do vértice da colisão (metade dos originais é 0; a metade alta vai até 4096).
- Se o DR2 aceita a falta de `resetlines.cqtc` / `boundarylines.cqtc`, como o DR3.
- Uso do UAV nos VB do terreno.

## 9. Geometria nova no `tracksplit.pssg` (DR2Hook Ring pela overlay, 2026-10-06)

Scripts: `scripts/research/terrain_probe.py` (move malhas existentes) e
`scripts/research/ring_tracksplit.py` (gera um `tracksplit.pssg` com o Ring no lugar das células da
Montalegre). A saída vai para `captures/overlay/tracks/locations/portugal/montalegre_rallycross/`
e o jogo a lê no lugar da do `.nefs`. O PSSG pode crescer: 576 MB foram carregados sem erro.

### 9.1 Hierarquia

`ROOTNODE > surface > (TRANSFORM, BOUNDINGBOX, células)`. Na Montalegre:

| Célula | Qtde | Nós de render (RENDERNODE) | Shader |
| :--- | ---: | :--- | :--- |
| `LAND_i_j` (grade 20×20, ~830 m) | 400 | `NONLOD_`, `NONLODBATCH_` | vista (`terrain_track_vista_d4`), `batchmaterial!0` |
| `ROOT_i_j` (~60–90 m) | 70 | `LOW_`/`LOWBATCH_` | `terrain_lod`, `batchmaterial` |
| | | `SHADOWCASTING_`/`SHADOWCASTINGBATCH_` | detalhado (`terrain_road`, `terrain_wsm_*`), `batchmaterial` |
| | 29 | `DECAL_`/`DECALBATCH_` | `decal_ao*` |

Cada RENDERNODE tem TRANSFORM, BOUNDINGBOX (6 floats BE: mín xyz, máx xyz) e RENDERSTREAMINSTANCE
(indices=#datasource, shader=#material, filho RENDERINSTANCESOURCE source=#datasource). A
BOUNDINGBOX das células é zerada. As LIBRARYs usadas são SHADERINSTANCE, SHADERGROUP, SEGMENTSET
(RENDERDATASOURCE > RENDERINDEXSOURCE+INDEXSOURCEDATA e RENDERSTREAMs dataBlock/subStream),
RENDERINTERFACEBOUND (DATABLOCKs com DATABLOCKSTREAMs e DATABLOCKDATA) e NODE. Todos os
triângulos são listas `triangles`, com índices `ushort` (≤65535 vértices por datasource).
Na Montalegre, a normal geométrica aponta para +Y.

Exe: `0x140a90150` percorre os filhos de `surface`. O prefixo `LAND_` (comparação de 5 bytes)
marca paisagem, e os nós de render são achados por prefixo (`HIGH_`, `LOW_`, `NONLOD_`,
`SHADOWCASTING_`, `HIGHBATCH_`, `LOWBATCH_`, `NONLODBATCH_`, `SHADOWCASTINGBATCH_`, `DECALBATCH`,
`DECAL`). Em seguida chama `0x140bce9c0`, que liga `trackxfade_high`/`trackxfade_low` e recebe o
byte "é LAND", e depois `bcc450` e `bcff80`. A célula tem 0x120 bytes em `r13+0x210`, o contador
fica em `+0x38610` e o limite é de ~800 células. O resto do nome da célula não é interpretado:
`RINGF_3_4` e `LAND_RINGB_0_1` funcionam.

### 9.2 Regra da profundidade (batch)

As malhas `batchmaterial` (`batched_track.fx`, 0 parâmetros, stride 24: Vertex float3, Color,
ST half4 = (0,0,0,1)) desenham a profundidade. O material visível só aparece onde a profundidade
bate exatamente. Os vértices do batch são os mesmos do visível (soldados e redivididos; 100% batem
pela posição exata).

- X1 (só `terrain_road.fx` +2 m): a pista some, fica transparente.
- X2 (`terrain_road.fx` +2 m e os vértices do batch na mesma posição): aparece certa, 2 m acima.
  No primeiro print parecia branco, mas era a câmera de perseguição embaixo da pista erguida.

Conclusão: geometria nova precisa de visível + batch com os mesmos triângulos. O transplante antigo
(T2, só no batch) ficava branco por isso.

### 9.3 Formato do vértice visível (stride 56, BE)

Vertex float3 @0, Color ARGB @12, ST0 half4 @16, ST1 half4 @24, Normal half4 @32, Tangent @40,
Binormal @48 (w=1). Binormal = Tangent × Normal. ST1 = (u, v, u, 1−v) é um atlas de ~250–500 m.
Medianas da Montalegre (ST por metro):

| Material | ST/m | Cor ARGB |
| :--- | :--- | :--- |
| `S1_main` | ST0 (0.087, 0.099, 0.077, 0.025); ST1 ≈ 0.004 | (0,118,50,255) |
| `rumbles_01` | ST0 (0.127, 0.029, 0.117, 0) | — |
| `S3_main` | ST0 (0.089, 0.095, 0.062, 0.025) | (0,92,77,199) |
| `main>dense` | ST0.xy constante, ST0.zw ≈ 0.7/m; ST1 ≈ 0.002 | (0,15,0,199) |

O `ring_tracksplit.py` projeta o ST0 no mundo e subtrai um inteiro por célula, para os `half`
ficarem pequenos. A tangente é `normal × (0,0,1)`.

### 9.4 `ring_tracksplit.py`

Lê `examples/tracks/synthetic__dr2hook_ring/terrain_0.bin` e aplica o deslocamento
`(8, 1334, -290)`. Materiais: asfalto → `S1_main`, zebra → `rumbles_01`, cascalho/terra →
`S3_main`, grama → `main>dense`. O primeiro plano é dividido em células de 100 m e o fundo em
células de 500 m. O fundo perde os triângulos sob o primeiro plano. As células da Montalegre são
trocadas pelas do Ring, com um datasource visível por material e um batch por célula.

Resultado: 470 → 183 células; 151 821 vértices; 263 862 triângulos; 576 MB.

- `--layout root` (padrão): células `RINGF_/RINGB_` com `SHADOWCASTING_`/`SHADOWCASTINGBATCH_`.
- `--layout land`: células `LAND_RING*` com `NONLOD_`/`NONLODBATCH_`.

### 9.5 Resultado no jogo (ring1–ring3)

O Ring aparece texturizado, mas só em parte, e o pedaço visível muda com a posição da câmera (o
dono viu partes da Montalegre aparecendo).

- Em volta do carro fica branco. O carro roda na colisão da Montalegre (y ≈ 1434), e a grama do
  Ring perto de (8, −290) está em y 1435–1440. A câmera de perseguição fica embaixo da superfície,
  e o descarte de faces traseiras some com o terreno. Falta colisão própria, ou baixar o Ring
  alguns metros.
- Câmera livre perto do chão: o terreno perto aparece.
- De longe (a 750 m e acima), o recorte é estável no tempo (15 s na mesma pose não mudam nada),
  então não é streaming. Em `root`, quase nada aparece. Em `land`, aparecem algumas células de
  fundo de 500 m, com bordas em degrau, e as de 100 m (o traçado) continuam sumidas.
- A ordem dos triângulos está certa (a normal geométrica aponta para +Y, como na Montalegre), então não é
  descarte por face.

### 9.6 PVS (hipótese principal, em aberto)

O `track_loader.xml` liga `track.vis` → TrackManagerPlugin, pool VISIBILITY_SYSTEM. O
`tracksplit.bin` (StreamingPlugin) não existe no `.nefs`.

`track.vis` da Montalegre route_0: 228 432 bytes, little-endian. O cabeçalho tem u32 4, 0x1ed,
0xf7, 0x257, 0x220, 0x80, 0xc10 e 0xb920, seguidos de floats de caixa (x mín −140,45 = caixa da
pista). Ele continua sendo o da Montalegre e deve indexar as células pela ordem de criação, o que
explicaria o recorte em degraus que muda com a câmera.

- `0x140393ab0` constrói a cena ("vis:main scene"): grava `[cena+0x1734] = 2`, com 0 = "No PVS",
  1 = "Per Node PVS" e 2 = "Per Item PVS"; as strings ficam em `[cena+0x1740..0x1750]`.
- `0x1403c7aa0` roda por quadro com rcx = cena e copia `[cena+0x1734]` para `[cena+0x14e0+0xf4]`
  (em `3c7be8`). Prólogo: `48 89 5c 24 08 57 48 81 ec 30 01 00 00 48 8b f9`.
- A cena "main game" é criada em `0x1404755d0` e fica em `[renderer+0x2050]`. Há 14 usos de
  `+0x1734` (o de `6c4310` é outro objeto).
- A LoadProbe ganhou `pvs_mode` (ini; −1 não mexe). O hook em `3c7aa0` grava o modo antes da
  original. Com `pvs_mode = 0`, o log mostra uma cena indo de 2 para 0, mas as imagens ficaram
  iguais às do ring1 sem o hook. O campo, ou essa cena, não é o caminho que corta o Ring.

Próximos passos:

1. Achar quem lê o `track.vis` (pool VISIBILITY_SYSTEM, TrackManagerPlugin) e como o resultado
   corta as células. Para testar, servir um `track.vis` "tudo visível" pela overlay, ou pular a
   consulta com um hook.
2. Se o PVS explicar o recorte: gerar um `track.vis` do Ring.
3. Depois: nós `LOW_` para distância, colisão (`track.jpk` não passou pela camada), objetos
   (`objects.ens`/`trees`/`ornaments`), rota/progress e AI.

### 9.7 Análise sem o jogo (2026-10-06)

**Comparação estrutural Montalegre × Ring** (scripts no scratchpad; resultado abaixo):

- Iguais: TRANSFORM identidade em células e nós de render. A BOUNDINGBOX do nó de render é a
  extensão exata dos vértices, no espaço do mundo, nos dois arquivos. A da célula e a da `surface`
  são zeradas. Atributos de NODE/RENDERNODE/RSI e formatos dos fluxos também são iguais. **A H2
  (caixa errada → corte por frustum) está descartada.**
- Diferenças:
  1. Agrupamento: na Montalegre há um SEGMENTSET por nó de render, com todos os datasources do nó
     (1138 SEGMENTSETs = 1138 nós). O Ring cria um por datasource.
  2. Os 1138 SEGMENTSETs e 2708 DATABLOCKs da Montalegre ficam órfãos no arquivo do Ring.
  3. Tamanho: as `LAND_` da Montalegre formam uma grade de 20×20 com ~832 m cada, cobrindo 14 km.
     O Ring tem 183 células de 100 e 500 m, cobrindo 2,6 km.
  4. O Ring não tem `LOW_`, `DECAL_` nem `PHYSICS_` (a Mettet tem `PHYSICS_`/`PHYSICSBATCH_`).
  5. O NONLOD da Montalegre (`terrain_track_vista_d4`) usa dois DATABLOCKs (Vertex float3/12 e
     um bloco de 44 bytes com Color/ST/ST/Normal). O Ring usa o bloco de 56 bytes do detalhado.

**Formato do `track.vis`** (`scripts/research/track_vis.py`; validado nos 172 arquivos de 40
locais):

| Offset | Conteúdo |
| :--- | :--- |
| 0x00 | u32: versão 4, nós, folhas (nós = 2·folhas − 1), u3, bytes do bitset por folha, offset dos nós (0x80), offset dos blocos, tamanho dos blocos |
| 0x20 | caixa da árvore: f32 mín xyz, u32, f32 máx xyz, u32 |
| 0x40 | 16 × u32: tamanhos dos grupos de itens |
| 0x80 | nós (6 bytes cada) |
| blocos | um bitset por folha, em PackBits |
| depois | kd-tree de itens e caixas do PVS (ver §9.9) |

- Nó folha: u16 0, u24 offset do bloco, u8 1.
- Nó interno: u16 a, u16 b, u16 c.
  - Filho do lado baixo: `a & 0x1fff`. Filho do lado alto: esse índice − 1.
  - Eixo: `(a >> 13) & 3` (0 = x, 1 = y, 2 = z).
  - Corte: `((b >> 12) << 16 | c) / 2^20`, como fração da caixa da **raiz** (corrigido em 2026-10-06 com o DiRTbench; a seção de depuração confere com erro < 0,002 m).
  - O bit 15 de `a` só aparece nas pistas grandes e o uso é desconhecido.
- As folhas sob o chão (y de 0,5 a ~1420 na Montalegre) têm o bloco "tudo 1" de 16 bytes. São 132
  das 247.
- Bloco: PackBits. `c ≥ 0x80` repete o próximo byte `c − 0x80` vezes; `c < 0x80` copia `c` bytes.
  Os bits estão em ordem little-endian.
- **Bits por folha = u3 + soma dos grupos.** Na Montalegre: 599 + 470 + 472 + 1644 + 974 + 125 +
  62 = 4346. O bitset tem 544 bytes, e o último byte é 0x03.
- **O 1º grupo é o número de células do `tracksplit.pssg`.** Montalegre 470 (400 LAND + 70 ROOT),
  Mettet 470 (400 + 70), Estering 486 (400 + 86). Os demais grupos devem ser objetos, árvores e
  ornamentos (há grupos que mudam por rota). u3 = 599 em quase todas as pistas.
- Ainda não se sabe em que bits ficam as células. Uma busca pela distância entre folha e célula dá
  um sinal fraco (ROOT perto do bit ~921, LAND perto do ~2357, não contíguos). Falta o código do
  exe que lê o arquivo: `track.vis` não tem referência direta, e `VISIBILITY_SYSTEM` é usado em
  `0x1409cc950`.

**Conclusão para o Ring:** o `track.vis` foi feito para as 470 células da Montalegre, na ordem do
arquivo. O Ring tem 183 células, e cada uma herda a visibilidade da célula da Montalegre com o
mesmo índice. Isso bate com o recorte em degraus que muda com a câmera (H1).

Teste que decide a H1: `track_vis.py all-visible` gera o mesmo arquivo com todo bloco trocado por
"tudo 1", sem mudar nenhum offset (`build/re/track_allvis.vis`). Servido pela overlay em
`.../montalegre_rallycross/route_0/track.vis`:

- se o Ring aparecer inteiro, a H1 está confirmada e o próximo passo é gerar um `track.vis` do Ring;
- se não, sobram H3 (`SHADOWCASTING_` só de perto e falta `LOW_`) e H4 (oclusão pela
  `landscape.heightfield`).

**Resultado (2026-10-06, run `allvis`):** o `track_allvis.vis` foi servido pela overlay. O log mostra
`open ...dr2hook_overlay\...\route_0\track.vis`. Com as mesmas 5 poses do ring3, as imagens ficaram
iguais: mesmo recorte, mesmas bordas retas. Como `pvs_mode = 0` também estava ligado, o PVS saiu
pelos dois caminhos e o recorte continuou. **A H1 caiu: o PVS não corta o Ring.** O formato
decifrado continua valendo para gerar um `.vis` do Ring mais tarde.

O recorte tem bordas retas alinhadas aos eixos, então o corte é por célula inteira. Sobram as
hipóteses de corte por célula fora do PVS:

- crossfade pista/vista (`0x140bce9c0` liga `trackxfade_high/low` e recebe o byte "é LAND");
- distância/LOD (falta o `LOW_`);
- oclusão (heightfield, ou o batch de profundidade do quadro anterior);
- algum campo da célula de 0x120 bytes calculado na carga.

Próxima análise: comparar no jogo o vetor de células (`r13+0x210`) entre células visíveis e
invisíveis da mesma pose.

### 9.8 Quem corta o Ring: a lista de células vem do `track.vis` da Montalegre (2026-10-06)

Medido no jogo com o Ring pela overlay e a câmera livre. As ferramentas usadas foram watchpoint e
breakpoint de hardware pelo `gdb -p` (o Wine permite ptrace), mais a leitura de `/proc/<pid>/mem`.

**Escritor dos pesos.** Um watchpoint em `[célula+0xd0]` parou em `0x140bcb3ec`, dentro de
`0x140bcb2d0`:

```
bcb2d0(célula, olho vec4, fov, saída_a, saída_b, passe, desenha, k)
  d = |max(0, olho − máx, mín − olho)|        ; caixa ao vivo da célula, +0xa0/+0xb0, 3D
  gerenciador->vtable[1] = 0x140a81760(d, fov, &alto, &baixo, k)
  passe 0x400: [célula+(k+0xd)*16] = baixo, [célula+(k+0xe)*16] = alto   ; passe 0x406: +0xf0/+0x100
  depois põe os nós da célula nas filas de desenho (0x140bcb230) conforme os pesos
```

`a81760` usa os parâmetros do gerenciador em `+0x38670`: escala 0,96, perto 300, largura 10 e longe
800. Com `d' = d·fov/0,96`:

| `d'` | Alto | Baixo |
| :--- | :--- | :--- |
| < 290 | 1 | 0 |
| 290–310 | crossfade | crossfade |
| 310–800 | 0 | 1 |
| > 800 | 0 | 0 |

Na faixa > 800 a célula não é desenhada, a menos que o byte `+0x40` esteja ligado: nesse caso ela
ainda desenha os nós de `+0x50`/`+0x78`. O passe principal é 0x400, com o olho na câmera e o FOV
em rad (0,96 na câmera livre). O passe 0x402 tem FOV π/2 e olho perto do chão; deve ser outra vista
(reflexo).

**Só as células da lista são avaliadas.** O chamador único, `0x140a7f700(ger, vista, lista, n, ...)`,
percorre uma lista de pares `(u32 índice, f32 distância)` e pula índice ≥ `[ger+0x38610]`. As
células fora da lista não são desenhadas e guardam o peso antigo, e por isso os pesos medidos
pareciam não depender da distância. Na vista principal, a lista é `[vista+0x1198]`, com contagem
em `+0x1190`: é a entrada 4 da tabela em `vista+0x1110+0x80`, de 0x18 bytes por entrada. Quem a
pede é `0x14039ccb0` → `0x140c2de70(vis=[vista+0x1038], vista+0x1110, 4)` → `c2df30`, que grava
olho, distância e destino no objeto de visibilidade (`+0xb17f0..0xb1810`) e dispara um job. O job
é `0x140c11b00` → `c17360`/`c2d8e0`/`c0b140`.

Com a câmera em (8, 2500, −290), a lista teve 82 itens: os índices 0–69 e 258–301. O float é a
distância do olho ao **centro da caixa da célula da Montalegre** com índice
`469 − i`, isto é, na ordem reversa do arquivo, a mesma do vetor de células. O erro foi 0 nas
`LAND` e < 6 m nas `ROOT`. Por exemplo, 280 = `LAND_10_10` e 0–69 = as `ROOT_` perto da largada.

**De onde vêm as caixas.** O registro de `LAND_10_10` está na memória em 32 bytes: `f32 mín xyz,
u32 grupo (0)` e `f32 máx xyz, u32 índice (0x118 = 280)`. Os mesmos 32 bytes estão no `track.vis`
da Montalegre em 0xbcd0. Essa região tem registros desse tipo entre 0xb950 e 0xc830, misturando os
grupos 0, 1, 2, 3, 4 e 8. O grupo 0 são as células, e o 1º tamanho de grupo no cabeçalho é 470.
O `track.vis` traz, além dos bitsets, um índice espacial dos itens com caixa (grupo, índice). Isso
explica o resultado do `allvis`: trocar os bitsets não muda as caixas, então a lista continuou
igual.

**Conclusão.** A célula `i` do Ring só entra na lista quando a caixa da célula `i` da Montalegre,
no `track.vis`, está perto da câmera. Os índices ≥ 183 que a lista traz são descartados. Esse é o
recorte em blocos retos que muda com a câmera.

Caminhos para corrigir: pôr o Ring no molde das 470 células da Montalegre, ou gerar um
`track.vis` com as células do Ring. O escolhido foi o segundo (§9.9).

### 9.9 Formato completo do `track.vis` e gerador (2026-10-06)

O `track_vis.py` lê e remonta o arquivo. A remontagem de **172 de 172** arquivos sai idêntica byte a
byte (`info` faz a ida e volta).

| Onde | Conteúdo |
| :--- | :--- |
| 0x0c (u3) | nós da kd-tree de itens |
| 0x10 | bytes do bitset por folha = `ceil(bits/8)` arredondado a 16; bits = nós da kd + soma dos grupos |
| 0x18 | offset dos blocos das folhas (alinhado a 16 depois dos nós do PVS) |
| 0x1c | offset da kd-tree (antes lido como "tamanho dos blocos") |
| 0x2c | offset da seção de caixas do PVS |
| blocos | cada folha em PackBits, alinhada a 16, com pelo menos um byte 0 depois |

**kd-tree de itens**, em pré-ordem. Cada nó tem 48 bytes mais 32 por registro:

- `f32 mín xyz, u32 bit do nó` (byte | bit<<16);
- `f32 máx xyz, u16 índice do nó, u16 registros`;
- `u32 offset do próximo nó` (0 no último), `u32 profundidade<<16 | tamanho do próximo nó`, `u32 0`,
  `u32 2` (interno) ou `0` (folha).

O registro é `f32 mín xyz, u32 grupo; f32 máx xyz, u32 índice no grupo`. Cada item fica no nó mais
fundo que contém a caixa inteira dele (3747 de 3747 na Montalegre). A raiz cobre o mundo (±7116,
y de 0 a 1900) e os cortes são na metade.

**Bits do PVS:** a kd-tree é percorrida em largura (BFS). Cada nó recebe um bit, seguido de um bit
por registro dele (599 de 599 nós batem; o total é 4346). É isso que faltava na §9.7 para achar a
célula no bitset.

**Seção de caixas do PVS:** um registro por nó do PVS, em ordem: `f32 mín xyz, f32 máx xyz, u32 n,
u32 índice` e `n` pontos vec4 (w = 0x10fa10fa). Só as 115 folhas reais (as que não são "tudo 1")
têm pontos.

**Nomes e correções pela pesquisa do Ssor (2026-10-06).** O repositório
[ssor0/ego-visibility-system](https://github.com/ssor0/ego-visibility-system) lê o mesmo formato nos
jogos Ego anteriores (DiRT 2/3, Showdown, Grid). Ele não tem licença, então só usamos os fatos. Tudo
abaixo foi conferido nos 172 `track.vis` do DR2:

- Os "grupos" são **camadas estáticas**: 0 `track_block` (células do terreno), 1 `ground_cover`,
  2 `ornaments`, 3 `trees`, 4 `crowd`, 6 água interativa, 7 entulho do chão e 8 luzes. O índice do
  registro é a posição do item na lista do gerenciador da camada. Montalegre route_0: 470 / 472 /
  1644 / 974 / 125 e 62 luzes.
- Nó interno do PVS: o bit 15 que parecia uma "flag" é o 13º bit do índice do filho de trás
  (`(a>>15)<<12 | b&0xfff`). Nos 622 657 nós ele é sempre o filho da frente menos 1.
- O u8 da folha é o esquema de compressão. Só aparece o 1 (`RLEBit8`).
- A "seção de caixas" é `DebugViewCellInfo`, que o jogo não usa. Cada ponto é `f32 xyz` mais dois
  u16 (nós e itens visíveis). Nas 384 amostras, os dois valem o total de bits (0x10fa = 4346).
- A consulta do motor (`QueryParams`) tem `returnAllItems` e máscaras de camada estática e dinâmica.
  Isso serve de ponto de depuração para desligar o PVS ou camadas em tempo de execução.

`track_vis.py cells ... --layers 2,3` mantém só as camadas pedidas, além da 0. Com `--layers ""`
fica só o terreno: os itens das outras camadas saem da kd e não entram no conjunto visível
(`build/re/track_ring_l0.vis`).

**Resultado no jogo (2026-10-06, run `l0`, pista `dr2hook_ring`).** O `track_ring_l0.vis` foi servido
pela overlay (o trace mostra o `open` em `dr2hook_ring\route_0\track.vis`) e a corrida largou
normalmente. Todos os objetos da Montalegre sumiram: guard-rails, placas, árvores, prédios e decalques
do chão. Só o terreno do Ring aparece. Isso confirma que a kd do `track.vis` decide o que cada
gerenciador desenha. A colisão dos objetos é outro sistema e não foi testada.

Na carga, o jogo copia o arquivo inteiro para a memória e reescreve só os campos de ligação, como o
offset do próximo nó e a flag de filhos. As caixas de alguns objetos são atualizadas ao vivo.

**Gerador:** `track_vis.py cells <track.vis> <tracksplit.pssg> <saída>`.

- Mantém o PVS e os itens dos grupos 1 em diante.
- Troca o grupo 0 pelas células do `tracksplit`: a caixa é a união das BOUNDINGBOX dos nós de
  render, na ordem reversa do arquivo (= vetor do jogo).
- Insere cada célula no nó mais fundo que a contém.
- Recalcula bits, offsets e cabeçalho, e deixa toda folha "tudo 1".

Para o Ring, as 183 caixas do arquivo batem com as caixas ao vivo das células no jogo (erro 0),
gerando `build/re/track_ring.vis`.

**Resultado no jogo (2026-10-06, run `ringvis`).** O `track_ring.vis` foi servido pela overlay em
`route_0/track.vis`; o log mostra o `open` pela overlay e a corrida largou normal. Repeti as mesmas
5 poses do ring3/allvis, e o recorte sumiu: o terreno do Ring aparece contínuo até o horizonte em
todas elas. **A kd-tree de itens era a causa.**

Ainda falta:

- colisão: perto do carro tudo fica branco, porque o carro está sob a grama do Ring;
- os objetos da Montalegre (grupos 1 em diante) seguem nas posições antigas, e a maioria fica
  enterrada sob o Ring;
- `LOW_` para longe, decalques, rota e AI.

### 9.10 O que o DiRTbench (DiRT 3, MIT) ensina para o DR2 (2026-10-06)

O [DiRTbench](https://github.com/ItsNotPaths/DiRTbench) cria estágios novos para o DiRT 3: estradas,
terreno, colisão, `track.vis`, arquivos de rota e linhas do banco. O que vale para o DR2, já
conferido nos arquivos da Montalegre:

- **Colisão (`track.jpk`):** é o mesmo JPAK. Os blocos se chamam `qt_XZ_XZ_..._XZ.vcqtc` (dois bits por
  nível), ficam em profundidade, e o `qt.info` (2 × vec3) vai no fim. No DR3 a raiz precisa se dividir
  ao menos uma vez: com um bloco só, o carro cai. Os blocos respeitam um limite medido dos originais
  (DR3: 1565 triângulos / 929 vértices; Montalegre: no máximo 1084 / 591), e passar dele derruba a carga
  longe da colisão. O triângulo entra em cada célula que toca (teste SAT em XZ). O formato interno do
  bloco é outro no DR2 (vértice vec4, 8 materiais, bloco `VCQT`), então o particionador serve, mas o
  codificador é o nosso.
- **`track.vis`:** o corte do PVS é uma fração da caixa da raiz (corrigido na §9.9). A lista de
  folhas termina num byte 0, e o jogo decodifica até achá-lo. Um gerador de verdade divide as células
  só perto da superfície da rota (até 8191 nós / 4096 folhas) e usa um alcance de cerca de 200 a 400 m
  para objetos.
- **Arquivos de rota velhos:** `resetlines.cqtc` é o teste de fora da pista. Uma cópia da pista de
  origem pode pôr o carro de volta num chão que não existe mais. O DR3 aceita a falta de
  `resetlines.cqtc` e `boundarylines.cqtc`. O Ring ainda leva o `resetlines.cqtc` da Montalegre.
- **Rota e IA:** `progress_track`, `ai_track` e as linhas de freada saem da linha central (portões a
  cerca de 17 m, raio das curvas).
- **Banco:** além de `location`, `track` e `track_model`, o DR3 clona `track_model_surface`,
  `track_model_conditions` e `net_race_tracks`. As duas últimas não existem no `base.ctpk` do DR2. Em
  troca, a rota 537 aparece em `ai_virtual_times` (6 linhas) e em tabelas ainda sem nome.

### 9.11 Colisão (`route_N/track.jpk`) e gerador (2026-10-06)

O `track.jpk` é um dataset `RaceSetup` no pool `ENTITY_SYSTEM`. O leitor e gravador fica em
`scripts/research/track_jpk.py`. Ele regrava a Montalegre byte a byte e recodifica todos os 480 blocos
com os mesmos triângulos, materiais e bits de aresta.

- **JPAK:** `"JPAK"`, 0, n, 16, 0, 32 + 32n, 8 bytes zero. Depois vêm n entradas de 32 bytes (offset do
  nome, tamanho, offset dos dados, tamanho, 16 bytes zero). Os nomes começam em 32 + 32n + 1. Os dados
  ficam alinhados a 16 e o arquivo é completado até 16.
- **Nomes:** `qt_XZ_XZ_..._XZ.vcqtc`. É um bit de X e um de Z por nível, do nível mais alto para o mais
  baixo, relativos à caixa do `qt.info`. As entradas ficam em ordem de nome (profundidade). O `qt.info`
  (min xyz, max xyz) é a última.
- **Bloco `vcqtc` do DR2:**

  | Offset | Conteúdo |
  |---|---|
  | 0x00 | `bmin` xyz, `bmax` xyz (folga de 0,1) |
  | 0x18 | i32 número de triângulos, número de vértices, 8 |
  | 0x24 | u32 offset dos vértices (0x90), dos nós, dos triângulos, das refs |
  | 0x34 | 8 códigos de superfície de 4 letras (as vagas repetem o último) |
  | 0x60 | `"VCQT"`, 1, 0, 0 |
  | 0x70 | vec4 (bmin.x, 0, bmin.z, 1) |
  | 0x80 | vec4 (largura x, 1, largura z, 1) |
- **Vértice:** f32 x normalizado, y absoluto, z normalizado e um u32 de bits. Metade dos originais tem
  esse u32 em 0; gravamos 0.
- **Nós:** 2 bytes BE. O bit 15 marca folha e o resto é o offset nas refs. `8000` é um bloco de uma
  folha só (35 dos 480 originais).
- **Triângulo:** codificação do Showdown.
  - byte0 = v0 >> 2.
  - byte1 = (v0 & 3) << 6 | bits de aresta << 3 | material.
  - byte2 = v1 − v0.
  - byte3 = v2 − v0.

  O bit de aresta k vale 1 quando a aresta tem vizinho com ângulo diedro menor que cerca de 55°. Na
  borda da malha vale 0; o recálculo bate com 97% dos originais. A normal fica para cima.
- **Refs:** u16 BE do primeiro índice, depois deltas de 1 byte (254 = pula), terminando em 0xFF.
- **Superfícies** (`surface_materials.xml` em `game_1.dat`):

  | Código | Superfície |
  |---|---|
  | `TS0+` | SMOOTHDRYTAR_RX |
  | `RRM+` | RUMBLESTRIP_RIDGED_MED |
  | `DB2+` | GRAV_RX |
  | `DR2+` | DIRT_RX |
  | `GR1+` | GRASS_RX |
- **Gerador:** `track_jpk.py ring <saída>`.
  1. Quadtree em XZ (teste SAT por célula), dividindo enquanto passar de 1000 triângulos, 580 vértices ou
     8 materiais.
  2. Vértices soldados a 1 mm e numerados por Cuthill-McKee reverso, para os deltas caberem num byte.
  3. Um nó folha único por bloco.

  Do fundo do Ring, só fica o que está a até 200 m do primeiro plano. Assim são 502 blocos e 3,46 MB,
  perto da Montalegre.

**Travamento do `col1` (causa achada):** com a colisão do Ring, a carga parou em `submit entity`. A
thread principal ficou presa em `0x1404628a0`. Essa função é um comb sort (fator 0,8017) chamado por
`0x1404b4690`. A `0x1404b4690` espalha sondas IBL (`core_ibl_large`) pela spline da rota e as ordena
pela distância até `[[corrida+0x2050]+0x17c0]+0xd0`, a posição do carro. O comparador
`0x140498ac0` é `comiss` + `setb`, que dá verdadeiro com NaN nos dois sentidos, então uma distância NaN
faz a ordenação trocar para sempre.

O NaN vem do carro. Ele nasce no grid da Montalegre, e ali o chão do Ring (deslocamento antigo
`8,1334,-290`, sem giro) era grama 4,6 m acima. Em quase todo o traçado da Montalegre, o Ring ficava 2 a
16 m acima. Com a colisão da Montalegre isso não aparecia, porque o carro nascia no chão dela, invisível
sob o Ring.

Correção: `ring_tracksplit.start_transform()` gira o Ring em torno de Y (47,7°) e o move para que a
reta de largada do layout (s = 0) caia sobre os portões 0 a 2 da Montalegre, com o asfalto em 1432,68.
O tracksplit, o `track.vis` e a colisão usam a mesma transformação. Regra para pistas novas: o asfalto
tem de estar no grid de largada que o jogo usa, enquanto o grid vier da pista de origem.

## 10. Pista nova com nome próprio (sem trocar uma pista existente, 2026-10-06)

Objetivo: uma rota `portugal / dr2hook_ring / route_0` que existe ao lado da Montalegre, em vez de
servir o Ring no lugar dela.

### 10.1 Catálogo (`catalogues/base.ctpk`)

Formato completo em `scripts/research/ctpk.py`; a regravação sai idêntica byte a byte.

- O cabeçalho tem 0x18 bytes: `"CTPK"`, u32 versão 2, u32 checksum, u32 0x18 e u32 fim absoluto
  das strings.
- Depois vêm as strings (u32 n, depois u32 tamanho + bytes).
- O diretório tem u32 n e pares (djb2 do nome da tabela, offset absoluto). Offset `0xffffffff`
  marca tabela vazia; o diretório não é ordenado por hash.
- O corpo de cada tabela é u32 n_linhas seguido de (u32 id, u32 tamanho, protobuf). As linhas
  ficam ordenadas por id, porque a busca é binária.
- Strings dentro das linhas são guardadas como djb2 da string da tabela.
- Os nomes das tabelas são o djb2 dos nomes do `schemaDirtRally.xml` do EgoDatabaseEditor.
- `packages.xml` lista os pacotes, e o carregador `0x14026bf00` carrega cada um como dataset do
  `CatalogueManager`.
- A busca por id (`0x1400e7780`) só olha o **primeiro** pacote que tem a seção. Um segundo pacote
  não acrescenta linhas, então o caminho é servir um `base.ctpk` inteiro gerado.

Rota = `track_model` (`0xb8e12aaa`). Na Montalegre `route_0`, a linha 537 tem estes campos:

| campo | valor |
|---|---|
| 1 | id |
| 2 | pista (`%track%`) |
| 4 / 5 | country / location (ids) |
| 6 | location (`%location%`) |
| 9 | disciplina |
| 12 | id na tabela `track` |
| 13 | rota (`%route%`) |
| 17 / 18 | nomes `lng_` |
| 48 | `montalegre_rallycross_01` |
| 74 / 75 | `<pista>_route_0` / `_spline` |

Outras tabelas ligadas à rota:

- `track` (`0x1072479a`, linha 153): o nome da pista está nos campos 3 e 9.
- `track_model_surface` (`0x29199252`, linhas 24/25): o campo 3 é a rota.
- `ai_virtual_times` e as definições de etapa `0xf4122b6a` também citam a rota (campo 3 = 537).

**Como o jogo acha a rota pelo nome.** É o que o benchmark e o AutoStage usam (`0x140590fa0`):

1. Lista todas as linhas de `track_model` (`0x1403463a0`).
2. Filtra pelo djb2 da pista (getter `0x1400fef60`, campo em `+0x10`).
3. Compara location (`+0x30`) e rota (`+0x60`) com `_stricmp`.

O descritor que vai para `race+0x32a0` aponta para essa linha. É dele que saem os tokens
`%track%`, `%location%` e `%route%` (§6). Sem linha, o ponteiro fica nulo, e esse é o crash do
`twin_peaks/free_roam`.

### 10.2 Pacote da location

`0x14050d700` monta `locations/<location>__<pista>.nefs`:

1. `0x1405df3c0` responde se o pacote está pronto.
2. `0x1405dcea0` monta o nome `'%s__%s.nefs'` (sem pista, `tiles`).
3. `0x1405dd2d0` busca o descritor.

As duas buscas usam o hash `h = (h*33) ^ tolower(c)`, com semente 5381, sobre uma lista **fixa no
exe** (`.rdata 0x1410b44f6`, 124 entradas = 40 locations + 84 carros). Cada entrada tem:

- o hash do nome;
- o pedaço de instalação (u16);
- uma marca de DLC (1 nas pistas e carros pagos);
- o SHA-256 do arquivo (32 bytes).

O jogo espera o pacote ficar pronto num laço com `Sleep(10)` (`0x14050d850`). Um nome fora da
lista nunca fica pronto, então **a carga trava**.

Na montagem (`0x140505ae0`):

- falha ao abrir o pacote → erro 6;
- SHA-256 diferente → o jogo só grava `0x03b5d037166c2af5` num objeto, provavelmente uma marca
  de arquivo alterado.

O `.nefs` da Montalegre só tem `tracks/locations/portugal/montalegre_rallycross/*` (68 arquivos,
1,55 GB) e o marcador `locations/mounted/portugal__montalegre_rallycross`. Uma pista com outro
nome não usa nada dele.

`info_*.nefs` (`0x14026c3a0`): logo depois dos catálogos, o jogo procura esses pacotes na raiz de
cada dispositivo e os monta. No PC não há nenhum (provavelmente sobra de console ou DLC).

### 10.3 Implementação de teste (LoadProbe)

- `overlay_early = 1`: hook em `0x14026bf00`, que monta a overlay antes do `packages.xml` para
  servir `catalogues/base.ctpk`. `overlay_early = 2` monta já no primeiro pedido de arquivo
  (`system/boot_data.xml`, ~2 s), antes das bases persistentes do frontend (§11). A montagem no `track_loader.xml` continua, para ficar na frente
  do `.nefs` da location.
- `track_alias = dr2hook_ring=montalegre_rallycross`: hooks em `0x1405df3c0` e `0x1405dcea0` que
  trocam só a pista usada no nome do `.nefs`. O pacote montado é o da Montalegre, mas os arquivos
  da pista nova vêm da overlay, em `tracks/locations/portugal/dr2hook_ring/`.
- `scripts/research/custom_track.py`:
  - clona a rota, a `track` e as `track_model_surface` com ids novos (669, 181, 1995/1996);
  - grava o catálogo na overlay;
  - copia os arquivos da pista de origem para a pasta nova, trocando `tracksplit.pssg` e
    `route_0/track.vis` pelos do Ring.

### 10.4 Resultado no jogo (run `custom1`, 2026-10-06 14:46)

O AutoStage com `portugal / dr2hook_ring / route_0` carregou e largou sem travar.

- **Catálogo:** o trace mostra `packages.xml` e `base.ctpk` lidos da overlay.
- **Rota:** ao vivo, `race+0x32a0` aponta para a linha 669, com pista `dr2hook_ring`, location
  `portugal` e rota `route_0`.
- **Pacote da location:** o alias montou o da Montalegre.
- **Arquivos:** todos os 58 arquivos da pista lidos na corrida vieram de
  `tracks/locations/portugal/dr2hook_ring/` na overlay, inclusive o `route_0/track.jpk` (colisão).
- **Visual:** o Ring aparece igual ao run `ringvis`. O carro fica sob a grama, porque a colisão
  ainda é a da Montalegre.
- **Nome do evento:** o `RaceEvent` informa `montalegre_rallycross`, o nome do pacote montado.

Os campos 74/75 da rota (`montalegre_rallycross_route_0` / `_spline`) ficaram iguais aos da
origem.

### 10.5 Colisão própria (runs `col1` e `col2`, 2026-10-06)

- **`col1` (17:12):** a colisão gerada (`track_jpk.py ring`, Ring com o deslocamento antigo e sem giro)
  foi lida da overlay, e a carga travou depois de `submit entity`. A causa foi o carro nascer dentro do
  chão (§9.11).
- **`col2` (17:24):** o Ring foi girado e movido para a largada da Montalegre
  (`start_transform`: giro 47,702°, deslocamento −132,382 / 1331,692 / −330,405). O tracksplit, o
  `track.vis` só com terreno e a colisão com o fundo a 200 m (502 blocos) estão em
  `build/re/ring_aligned/`.
  - **Carga e largada:** carregou e largou às 17:24:10.
  - **Chão:** o carro roda sobre o terreno do Ring e levanta poeira de grama (`GR1+`), então o
    material da colisão vale.
  - **Rumo:** no modo automático ele sai do asfalto, porque segue a linha da IA da Montalegre.
  - **Vigia de threads:** o aviso `GhostLab[espera]` às 17:24:27 apareceu depois da largada e não
    era travamento. A thread principal estava em espera normal (`0x1404b2609`).

Próximos passos:

1. Gerar `progress_track.xml`, `ai_track.xml`, `ai_vehicle_track.xml` e
   `vehicle_track_progress_data.xml` a partir da linha central do Ring (`layout.json`), com a mesma
   transformação.
2. Trocar ou tirar `resetlines.cqtc` e `cameralines.cqtc`, que ainda são da Montalegre (feito: §12.1).
3. Depois, nomes no menu e um pacote próprio no lugar do `track_alias`.

## 11. Tela de carregamento (foto aérea e traçado)

Não é vídeo. A tela é a cena `fe/screens/loading/map_stats_loading` de
`game_1.dat:frontend/databases/loadingScreen.pssg`.

### 11.1 Cena

- **`map_switch`:** um UINODESWITCH com escala 0,45 e um filho por rota. O filho é escolhido pelo nome
  `<pista>_<rota>` (campos 2 e 13 da rota no catálogo). Sem filho com o nome da pista, a tela fica
  preta, que era o caso do Ring.
- **Filho:** cada um aponta, por USERDATA, para um UINODEANIMATED cujo xr é
  `fe/component/loading/<rota>`.
- **Componente:** tem a FENODECOUNT, a FEANIMDATA compartilhada (`#femad374`), os `markers`, o
  `spline` e o `bg_image`.
  - **`markers`:** os `sector_marker_NN`, cada um um xr para `start_marker`, `finish_marker`,
    `joker_marker` ou `sector_marker`. A posição é o pixel do `_spline` dividido por 100, com y
    negativo.
  - **`spline`:** um quad de (0, 0) a (13,44, −9,84), ou seja 1344×992 px a 0,01 unidade por pixel.
    O shader é `ui_spline_reveal`.
  - **`bg_image`:** um quad de ±11,52×±6,48 com escala 2,5 em (9,6, −5,4). A animação de entrada
    (`Anim!12`) leva a escala para ~1 e tira o blur radial.
- **Eventos:** `reveal_spline` toca `open` no `spline`, espera `loading_spline_reveal_time` e toca
  `place_marker` em `sector_marker_00`…`_15`. Os glyphs são pedidos pelo nome e os que faltam são
  ignorados.

### 11.2 Texturas

Ficam em `frontend/streamed_textures/loading/`.

- **`<rota>.tpk`:** foto aérea, BC1, 2304×1296.
- **`<rota>_spline.tpk`:** BC1, 1344×992.
  - R: linha fina;
  - G: progresso, de 1 na largada a ~0,05 na chegada, num traço grosso;
  - B: joker.

  O `RevealMaskValue` vai de 1,019 a −0,012 e revela os pixels de G acima do limiar, e é isso que
  "desenha" a pista.
- **`splines/<rota>_sa.tpk`:** BC7, 368×176. É o mapinha do menu; o Ring ainda não tem.

### 11.3 Foto e traçado alinhados

- **Mapeamento:** foto e `_spline` ficam no mesmo componente. O zoom lento da cena vale para os
  dois, então o mapeamento entre eles é fixo.
  - **Medição:** registro das linhas da pista e da linha branca nos prints da carga do Ring (run
    `load2`, três quadros com o mesmo resultado): foto = 0,9975 · spline + (194, 104,5) px.
  - **Pela cena:** dá + (192, 108) com escala 1.
- **Jogo original:** nas pistas do jogo o traçado não fica alinhado com a foto.
- **Ring:** `scripts/research/loading_screen.py --track-dir`
  1. calcula uma câmera quase a pino (pitch 1,5) que põe a pista dentro de `SPLINE_BOX`;
  2. renderiza a foto no viewer3d (`--hide lines --shot-size 4608x2592`, reduzida para 2304×1296);
  3. desenha o `_spline` e os marcadores com a mesma projeção (`glm::lookAt` + `perspective`,
     fovy 0,9).

  No jogo (run `load3`), o risco é desenhado em cima do asfalto.

### 11.4 Carga cedo

- **Problema:** `loadingScreen.pssg` é uma base persistente e carrega aos ~6 s do boot (pilha
  `ee247bf|ee256b9|e84ffff`), antes dos catálogos. Com `overlay_early = 1`, o jogo usava o original:
  o som de desenho tocava, mas a tela ficava preta.
- **Solução:** com `overlay_early = 2`, a overlay entra no primeiro `0x140c5ff80` (`boot_data.xml`)
  e o arquivo sai de `dr2hook_overlay\frontend\databases\loadingscreen.pssg`. Na contagem de
  classes aparecem 189 FENODECOUNT, contra 188 no original.

## 12. Do editor ao jogo (o Ring inteiro, 2026-10-06)

O `tools/synthtrack` é a fonte. Depois de mudar a pista ou a decoração, regere tudo nesta ordem:

```bash
python3 -m tools.synthtrack -o examples
python3 scripts/research/ring_tracksplit.py build/re/ring_pad3/tracksplit.pssg --host build/re/montalegre/tracksplit.pssg
python3 scripts/research/track_jpk.py ring build/re/ring_pad3/track.jpk --bg-margin 200
python3 scripts/research/ring_objects.py build/re/ring_objects4 --tracksplit build/re/ring_pad3/tracksplit.pssg --kinds eot
python3 scripts/research/ring_cameras.py build/re/ring_cameras
python3 scripts/research/ring_grids.py build/re/ring_grids
python3 scripts/research/loading_screen.py --overlay captures/overlay --name dr2hook_ring \
    --layout examples/tracks/synthetic__dr2hook_ring/source/layout.json --track-dir examples/tracks/synthetic__dr2hook_ring
```

- **Pastas de saída:** crie antes a pasta de saída do `ring_tracksplit.py`, porque ele não a cria.
- **Colisão:** regere a colisão sempre junto com o terreno. Os platôs e o piso do paddock mudam o chão.
- **O que vai para `captures/overlay/tracks/locations/portugal/dr2hook_ring/`:**
  - `tracksplit.pssg` e `route_0/track.jpk`, da pasta do terreno;
  - `objects.pssg`, `objectstextures.pssg`, `ornaments_references.xml`, `route_0/ornaments.bin` e
    `route_0/track.vis`, da pasta dos objetos;
  - `route_0/replay_camera_config.xml` e `route_0/cameralines.cqtc`, da pasta das câmeras (§12.1);
  - `route_0/grids.pssg`, da pasta das vagas (§12.2).
- **Tela de carregamento:** o `loading_screen.py` grava direto na overlay (`frontend/...`). A tela
  só troca depois de reiniciar o jogo (§11.4).
- **Ornamentos:** o `ring_objects.py` porta os tipos `e`, `o` e `t` do editor que têm instâncias na rota.
  O `e:synth_spawn_marker` fica de fora.
- **Resultado em 2026-10-06:** 59 tipos, 2318 instâncias e 22 materiais.
  - A decoração completa (rodadas 1–3) foi vista no jogo pela câmera livre (F9).
  - As eólicas a ~900 m da pista continuam visíveis.
  - O load `dec3` largou sem crash, já com a foto aérea nova.

Um comando só faz tudo isso (§12.3).

### 12.1 Câmeras do replay (2026-10-07)

O editor gera as câmeras (`tools/synthtrack/cameras.py`, `replay` no `track.json`) e o viewer nativo as
mostra (tecla **C**; **Shift+C** olha pela próxima). O `scripts/research/ring_cameras.py` leva para o jogo.

- **`replay_camera_config.xml`** (XML binário). Cada câmera clona o bloco da Montalegre com o mesmo nome e
  troca a posição, a orientação e os caminhos.
  - Orientação: quatérnio (x, y, z, w). A câmera olha em +z local, e o +x local é cima × frente.
  - `Position` é o 1º ponto do `sourcePath`.
  - Caminhos (`type="spline"`) são Bézier cúbicos de 4 pontos encadeados, e o último ponto de um trecho repete
    no próximo. A `percentageCurve` (`bezierPercentage`/`linearPercentage`, `<Value time value/>`) vem da
    Montalegre.
  - Zonas (`TriggerZone`, `shapeType="box"`): `size` = (largura através da pista, 10, 0,5), `position` no
    meio da borda esquerda e direita, 0,5 m acima do asfalto. A orientação é um giro em Y com +x local
    atravessando a pista.
  - Cada `ZoneEvent` é uma troca com `probability`.
    - `switchType="replay"` aponta para uma câmera do arquivo; `"target"` aponta para uma do carro
      (`onboard_front`, `onboard_rear`, `external_front_R`).
    - A zona da 1ª volta usa `statementType="lapNumber"` e `lapNumber=1`.
  - Continuam da Montalegre o `dynamic_camera_rig` (relativo ao carro) e as câmeras de pódio e serviço.
- **`cameralines.cqtc`**: a Montalegre tem 16 prismas `CBND` de 2×2 m e 100 m de altura em volta das guaritas,
  4 paredes (8 triângulos) cada, sem tampa, com as normais para fora. O nome sugere "limite de câmera"; o efeito
  no jogo ainda não foi medido. O Ring leva um prisma por torre, poste de luz e banheiro a até 40 m da pista.
  Formato, medido nesse arquivo e conferido com a folha vazia do DiRTbench:
  - Cabeçalho: caixa (6 f32), i32 triângulos, vértices e materiais; u32 offsets dos vértices, dos nós, dos
    triângulos e das refs; etiquetas de 4 bytes.
  - Vértice: 8 bytes BE (x 24 bits, y 16, z 24, normalizados na caixa).
  - Nó: 3 bytes BE. O bit 23 marca folha, e o resto é o offset nas refs; sem esse bit, é o índice dos 4 filhos
    seguidos. `ffffff` é um nó vazio.
  - Triângulo: 7 bytes. v0 em 24 bits, depois um byte com os nibbles altos dos deltas de v1 e v2, os bytes
    baixos dos deltas e o material.
  - Refs de uma folha: o 1º triângulo em 24 bits BE, depois os outros como u16 BE relativos a ele. O
    último leva o bit 15. Não tem byte extra antes: a primeira versão do gerador punha um 0 ali, as refs
    saíam deslocadas e o jogo crashava ao mexer a câmera livre (`exe+0xde22e0`, leitura de vértice com
    índice lixo numa consulta da árvore). O `decode_cqtc` do `ring_cameras.py` confere um arquivo
    (Montalegre: 14 folhas cobrem os 128 triângulos).
  - O gerador grava uma folha só (raiz `800000`).
- **Resultado:** 29 câmeras do editor (36 com as da Montalegre), 15 zonas e 15 prismas.
  - A mira bate com o `aim` do editor (erro < 0,1°), e o XML faz ida e volta byte a byte.
  - A `Grid_Start_Cam_00` caiu a 0,3 m da original. A largada do Ring foi encaixada sobre a da Montalegre e
    a câmera foi calibrada com ela.
  - **Falta ver no jogo:** um replay no Ring.

### 12.2 Vagas de largada (2026-10-07)

O carro nasce nas vagas do `route_N/grids.pssg`, carregado logo depois das texturas (ordem no §4). O editor
gera as vagas (`tools/synthtrack/grids.py`, `grids` no `track.json`). O viewer nativo as mostra: tecla **L**,
**Shift+L** para olhar do banco do piloto, e `--grid-check` para conferir a altura sobre o chão. O
`scripts/research/ring_grids.py` leva para o jogo, usando o arquivo da Montalegre como molde (árvore, nomes e
caixas ficam; só os `TRANSFORM` mudam).

- **Formato** (PSSG big-endian):
  - `ROOTNODE` "Scene Root" (identidade), depois um `NODE` por grade e um `NODE` por vaga ou nó de apoio.
  - Cada nó tem `TRANSFORM` (16 f32, linha a linha, translação na linha 3) e `BOUNDINGBOX` (mín xyz, máx xyz,
    no espaço local). Mundo = local @ mundo do pai.
  - Linhas da matriz: 0 = frente × cima (esquerda do carro), 1 = cima, 2 = trás. O carro aponta para −linha 2.
- **Grades da Montalegre** (ao longo / lado a partir da largada, + = esquerda):

  | Grade | Vagas | Onde | Caixa da vaga |
  | --- | --- | --- | --- |
  | `grid_time_trial_0` | `slot_0` | −5 m, no meio, 0,40 m acima | ±1,4 × ±2,75 |
  | `grid_near_reset_01` | `slot_00_nr`…`slot_09_nr` | −6 a −78 m, a cada 8 m | ±1,4 × ±2,75 |
  | `grid_start_standing_01` | `slot_00`…`slot_09` | fileiras de 5 em −55,5 e −63,5 m, lados ±5,8 m | ±1,25 × ±2,25 |
  | `grid_start_staggered_01` | `slot_000`…`slot_011` | −57 a −75 m | ±1,25 × ±2,25 |
  | `grid_compound_5#5` | `slot_00_compound`…`slot_03_compound` | ~(−42, −95), paradas em ângulos variados | ±0,5 × ±1,0 |

  Os nós de apoio `car_grid_spline_time_trial_0` (caixa com z de 0 a 8,24) e `car_near_reset_spline_01`
  (±30 m) não têm carro. Os grupos têm caixa de ±0,5.
- **Por que trocar:** o Ring tem 10 m de asfalto (a Montalegre tem 12 m) e desce depois da largada. Com as
  vagas da Montalegre, as das fileiras de fora ficavam na zebra ou na grama, e as do reset e da largada
  parada ficavam ~2 m acima do asfalto, porque o Ring faz curva e desce antes da linha. Uma vaga
  dentro do chão pode deixar a posição do carro em NaN, e isso trava a carga (sort do IBL).
- **Vagas do Ring:**
  - mesmos nomes, centro 0,5 m acima do asfalto (0,48 m depois de assentar);
  - largada parada em fileiras de 3 (±3,3 m), escalonada em zigue-zague (±2,3 m);
  - paddock entre as tendas, de frente para a pista.
- **Resultado:** 44 nós trocados. Ao reler, a posição bate com o editor em menos de 0,1 mm.
  - Instalado na overlay em 2026-10-07 (backup do original em `build/re/montalegre_route0_orig/grids.pssg`).
  - **Falta ver no jogo:** onde o carro nasce no treino e nas outras largadas. Não se sabe qual grade cada
    modo usa; o treino deve usar `grid_time_trial_0/slot_0`.

### 12.3 Um comando e o F5 do editor (2026-10-07)

O resumo organizado de tudo o que se sabe da carga e do render (linha do tempo, sistemas, câmeras, passes, chaves da
LoadView e o que falta) está em [track_render.md](track_render.md); aqui ficam as rodadas em detalhe.

`scripts/research/ring_deploy.py` roda as etapas do §12 e abre o jogo direto na pista:

```bash
python3 scripts/research/ring_deploy.py [--quick] [--mode bot|freecam] [--edits <edits.json>] [--no-game] [--force]
```

- **Etapas:** terreno, colisão, objetos, câmeras, vagas, tela de carregamento, cópia para a overlay. Depois, sem
  `--no-game`: fecha o jogo aberto, abre pela Steam e segue as fases do `dr2hook.log` até a largada.
- **Cache:** as saídas ficam em `build/re/ring_deploy/` e a assinatura de cada etapa em `cache.json`. A assinatura
  junta tamanho e data das entradas, o conteúdo do `--edits`, os scripts, os argumentos e as assinaturas das
  etapas de que ela depende. Uma etapa interrompida perde a assinatura antes de rodar, então não parece válida.
  `--force` refaz tudo.
- **Tempos (2026-10-07):** a primeira vez sem tela de carregamento leva ~10 s (o `track_jpk.py` é ~7 s
  disso). Sem mudanças, as etapas pulam e só a cópia roda. A cópia só regrava os arquivos que mudaram.
- **`--quick`:** pula a tela de carregamento (a foto aérea renderizada pelo viewer3d e o traçado). Fica a da
  última vez. E grava `dr2hook_quickload.ini` na pasta do jogo: o core vê o arquivo no boot, apaga e cobre a
  tela de preto (sem menu nem tela de carga) e muta o som até a largada ("LoadCover" e "SessionAudio" no log).
  O mute é o da sessão de áudio padrão do processo (`ISimpleAudioVolume`); o som do Wwise sai por XAudio2. Um arquivo com mais de 10 min
  é ignorado; a tela também volta sozinha depois de 3 min.
- **Terminal na tela preta:** sobre o preto, o overlay mostra o `dr2hook.log` ao vivo, como um terminal:
  - uma linha de estado com a fase (iniciando, dados do jogo, pista, largada), o tempo, os arquivos abertos (quantos
    da overlay) e o IO;
  - as últimas linhas do log, coloridas: abertura da overlay em verde, marcos em azul, aviso em amarelo, erro em
    vermelho;
  - os caminhos ficam relativos à pasta do jogo, e linhas iguais seguidas viram uma com `(xN)`.
  
  As linhas vêm de um buffer em memória na proxy (as últimas 4096, `Logger::ReadSince`, export
  `Dr2Host_LogRead`). Por isso o terminal mostra o boot desde o começo e não relê o arquivo (a LoadTrace engancha a
  leitura). Ao liberar, o log diz quantos frames o terminal desenhou e o maior intervalo entre eles.
- **O jogo desenha durante a carga (2026-10-07):** 361 frames em ~7 s de tela preta (~50 fps). Só há uma parada
  grande, de ~1,8 s, ao ler o `persistentDB.pssg` (a LoadTrace loga "frame travado"). Na pista, o Present segue,
  então dá para pôr coisas na tela durante a carga.
  - O `patchup_ot.pssg` da overlay é aberto ~1.300 vezes seguidas (um open por leitura, ~1,3 s).
  - O `objects.pssg` é aberto ~90 vezes, o `treetextures.pssg` 117.
- **O que a GPU desenha na carga (LoadView, 2026-10-07):** o core engancha os draws do contexto imediato do D3D11
  (`src/core/load_view.cpp`) e loga por segundo em que alvos o jogo desenha ("LoadView:").
  - Na carga: ~44 draws por quadro. São ~11 na tela (a interface) e o resto em alvos pequenos de pós-processo
    (480x270 e 120x68, r11g11b10f). Não há sombra, profundidade nem cena em tamanho de tela: **o jogo não desenha o
    mundo enquanto carrega**.
  - A cena começa só ~1 s antes da largada: profundidade 1024x1024 r32 (sombras), 1920x1080 r11g11b10f e alvos
    de 128x128; ~1.000 draws por quadro. Na corrida são ~2.000 draws por quadro, sem contextos adiados.
  - O terminal da tela preta põe de fundo o alvo da cena quando ele tem 50+ draws por 3 quadros seguidos
    ("LoadView: a cena 3D comecou"). Na prática isso cobre só esse último segundo.
  - Para ver o mapa durante a carga seria preciso adiantar o render da cena (RE do laço de quadros e da máquina de
    estados da carga) ou desenhar uma vista própria a partir dos dados da pista.
- **Adiantar o render da cena (RE, 2026-10-07):** o resultado é que o motor só tem mundo para desenhar no fim da
  carga, então o ganho máximo é de 0,8 a 4 s. Os experimentos estão no `load_view.cpp` e são ligados por
  `dr2hook_loadview.ini` (`forcar_mundo_ms=0`, `forcar_vis=1`, `olhar=x,y,z,alvo_x,alvo_y,alvo_z`). Sem o arquivo
  os hooks só contam.
  - **Renderer:**
    - `+0x22a8` é o modo atual (0 corrida, 2 carga) e `+0x22ac` o modo pedido.
    - O handler `0x479860` pede 0 quando o terreno começa a carregar. O `0x480220` grava o pedido vindo de uma
      mensagem.
    - A troca de modo real é o `SetMode 0x4a1580`, que grava os dois campos e roda na largada.
    - Cena em `+0x2050`. `scene+0x1e98` != 0 desvia a vis do mundo.
    - Quadro da corrida em `0x4b1990`. Forçado como modo 0, precisa de `+0x88` nulo (senão crasha em `abd040`).
  - **Preparo do quadro:** `0x4aabd0(renderer, rdx)`, chamado de `0x4b26f0` (`+0x4b36a0`).
    - É pulado quando `0x1d2b30([renderer+0x2298])` dá verdadeiro.
    - Com modo != pedido, só retorna (`+0x4aacdb` → `+0x4ab034`).
    - Em modo 0 faz a atualização completa e chama `0x3c7aa0(cena)`, que limpa e enche as listas visíveis. Em
      modo 1/2 chama só o `0x3c7aa0`.
    - Forçar modo 0 nele (modo e pedido em 0 durante a chamada, `1e98` zerado) liga os jobs de vis na carga e não
      crashou.
  - **Terreno:**
    - Job das células `0xa7f700`:
      - só trabalha com `[terreno+0xd0] == 6`;
      - insere nos coletores por `bcb2d0`;
      - chamadores: `+0x3bad93` (a `ground_cover_camera`, vista de cima 128x128, terreno = `[cena+0x1008]`),
        `+0x3bacb1` (as 6 faces do fog renderer) e o cull da vista da tela `0x39ccb0` (volta em `+0x39d107`).
        Corrigido em 2026-10-07 à noite: antes o `+0x3bad93` estava aqui como a vista principal.
    - Estados: 7 é sem dados. O worker `0xa96150` passa para 4 quando os dados chegam. Depois
      `0x6c2de0(self 0x14159d770, dt)` → `0xacc960` passa 4→5→6, um passo por quadro.
  - **Linha do tempo medida (bot + rápida, cache quente):**

    | t (s) | evento |
    |---:|---|
    | 0 | processo do jogo, tela preta |
    | 3,4 | abre o `.nefs` da pista |
    | 7,8 | terreno 7→4→5→6 em ~0,1 s |
    | 8,7 | largada |

    Em outra rodada o intervalo entre terreno pronto e largada foi de 3,8 s. Antes do terreno, as listas
    da cena ficam vazias (0 itens).
  - **Com `forcar_vis=1`:** nesse intervalo final já saem 140 a 300 itens e células por quadro. Mas os draws vão
    só para os alvos 128x128 (faces do env-map).
    - A vista principal não desenha nada, porque a câmera da cena (`[cena+0x17c0]`) fica na origem `(0, 1, 0)`
      até a largada.
    - O `olhar=` muda a câmera da especial (`[[exe+0x168caf0]+0x20]+0x1cf8`, linhas `+0x210..+0x240`), mas ela
      não chega à câmera da cena antes da largada. Falta achar quem copia uma para a outra.
  - **Conclusão:** um fundo com o mundo do jogo só cobre o último 1 a 4 s da carga. Para o resto, o fundo teria
    que ser próprio, desenhado a partir dos dados da pista; a foto aérea do §11 já existe.
  - **Por que a vista principal fica vazia (2026-10-07, tarde):** não é a câmera, é o **modo da cena**.
    - `[cena+0x1e98]` é uma cópia do modo do renderer. O `SetMode 0x4a1580(raceObj, modo)` chama `0x3c4a80`,
      que grava `+0x1e98` e zera `+0x1ed8`. O quadro `0x4b1990` aplica a troca em `+0x4b261a` quando
      `22ac != 22a8`; o `0x476cb0` pede o modo 2.
    - Fica 2 até a largada. O cull/push dos objetos `0x39ccb0` sai cedo com `cmp [rcx+0x1e98],0` (ou o byte
      global `[exe+0x169af48]`, que estava 0). Ele chama `0x953220` com a caixa 17 e depois `0xb51560` por
      `[cena+0x1040]`. O passe do mundo `0x3c9170` também testa o modo.
    - Outros leitores do `+0x1e98`: `39dd70`, `3ba760`, `3bada0`, `3bed80`, `3c1520`, `3c7aa0`, `3c8580`,
      `3caa60`, `9530f0`, `953f80`, `9ac9b0`, `599160`, `5a0e70`, `5bbfd0`, `5bd3d0`.
    - A cena tem endereço quase fixo: `0x15480e80` (às vezes `0x15490e80`).
  - **Caixas de itens da cena:** em `cena+0x1190 + k*24` fica `{u32 n, ptr @+8, u32 cap @+0x10, byte ligada @+0x14}`.
    - Caixa 0: células do terreno (194). Caixa 2: objetos (2318). Caixa 17 (`+0x1328`): 1. Caixa 18: 51.
    - Enchidas a cada quadro pelo `0xb293a0` (grava em `+0xb29684`). O culler é `cena+0x14e0`, com máscara em
      `+0x114`, modo em `+0xf4` e um byte em `+0x160`. Cadeia de chamada: job `b2fa50` → `b2a610` → `b2a8b0`
      → `b293a0`.
    - Depois do terreno em 6, as caixas já estão cheias, mas o modo segue 2.
  - **Experimento `manter_cena=1`** (no `dr2hook_loadview.ini`): o modo da cena fica 0 entre os quadros
    forçados. **O mundo desenha na carga**, com 587 a 1.179 draws por quadro no alvo 1920x1080. Mas crasha,
    nesta ordem:
    1. **Visual do carro.** `0x6ee9e0(carVis, dt)` → `6eef50` → `6f0940` → `6eeb00` lê `[carro+0xa28]` lixo e
       crasha em `+0x6eed75`. Contorno: hook que pula a chamada na carga forçada ("carro pulado N").
    2. **Anel de instâncias.** `0xab2190(conj, quadro, r8)` → `0xab20e0(conj, idx)` sobe o buffer do quadro.
       - O conjunto tem: byte ligado `@0`, entradas `@+0x20` (24 bytes: obj `@0`, ptr `@8`, n `@+0x14`),
         tamanho do elemento `@+0x18` e contagem `@+0x28`.
       - Usa o device `[exe+0x1f45258]`: vt+0x100 Map (→ `8f4950` → `8f4960`, que chama obj vt+0x80), vt+0x118
         Unmap e obj vt+0x78.
       - Chamadores do `ab2190`: `39db80`, `39dd00`, `39dd70`, `39e100`, `3c1090`, `46dd90`, `9f8e60`,
         `a7d640`, `c43580`.
       - O conjunto ruim é `[[M+0x1d30]+0x20]`, com M = `[cena+0x1018]` (chamada em `+0xab21da`). O
         `[M+0x1a0]` está bom.
       - Com obj inválido, salta para lixo via `+0x8f497c`. Contorno: guarda no `0xab20e0` que pula a entrada
         cujo obj não tem vtable no exe ("buffers liberados pulados N"). Com ela, a carga chega à largada.
    3. **Reset do quadro.** `0xab16f0(M)` (de `497fe0` ← `4b26f0`) percorre `[M+0x1a0]` e
       `[[M+0x1d30]+0x20]` chamando `[vt+0x60]`. Crasha logo depois da largada. **Aberto.**
  - **O conjunto de buffers** (amostrado por `/proc/pid/mem`):
    - M = `0x15436dd0`, e `[M+0x1d30]` já existe cedo (`0x6c894f10`).
    - O ponteiro do conjunto (`0x6c894f30`) fica 0 até o primeiro quadro em modo 0. Aí nasce com n=32, ligado.
    - As entradas apontam para objetos espaçados de 0x18 (`19d60cda0`, `19d60ce30`, `19d60ce48`…). A "vtable"
      de cada um é o nó anterior, ou seja, são **nós livres de um pool**: os buffers nunca foram construídos
      para esse conjunto.
  - **Causa achada (caça dos buffers):** não era um pool de buffers, era um **estouro do buffer de itens de
    desenho** do M.
    - Quem cria: o objeto de 0x3d0 em `[M+0x1d30]` nasce no init do M `0xa6c9c0` (construtor `0xa6c390`, vtable
      `exe+0x13a78f0`). O preparo de GPU `0xa95c40(M)`, chamado pelo método virtual `0xb920c0` (de
      `+0xba8a27`), faz duas coisas:
      - cria o buffer de itens `[M+0x1b20]` (0x58200 bytes = 0x3ac0 itens de 24, capacidade em `+0x1b18`);
      - cria o conjunto por `0xa8d9f0` → `0xa9eff0`. O conjunto tem 32 entradas e só a 31 com buffer
        ("DataBuffer"); as outras ficam zeradas.
    - O conjunto é destruído por `0xa92a60`, e o objeto é resetado por `0xaca5c0` (pelo descarregamento do M,
      `0xaca6c0`). O destrutor é o `0xab7770`.
    - **Quem escreve:** o `0xa78150` empurra itens de desenho. Ele pega blocos de 64 itens com
      `lock xadd [M+0x1b28]` **sem checar a capacidade** e liga cada item numa lista: `[item] = cabeça`, em
      `+0xa78215`. Achado com o watchpoint de hardware do GhostLab no array (pilha `a753ec` ← `a756f9` ←
      `a7525d` ← `39d174` ← job `b2fa90`).
    - **Quem zera o contador:** o fim do quadro `0x497fe0(renderer)`, chamado por `4b26f0` (`+0x4b3462`). Ele
      chama `0xab16f0([renderer+0x70])` (zera `+0x1b28`) e `0xab1870([renderer+0x78])` (zera `+0x14d8`), **só com
      `[renderer+0x22a8] == 0`**.
    - Na carga forçada o modo do renderer é 2, então o contador nunca zerava. Ele passava do fim e escrevia itens
      sobre o array de entradas, alocado logo depois no heap. Os "nós livres de 0x18" eram itens de desenho
      encadeados.
  - **Correção (no `load_view.cpp`):** hook no `0x497fe0`. Nos quadros forçados (`forcar_vis=1`) ele roda com
    `22a8 = 0` ("resets forcados N" no log). Com isso, em 2026-10-07:
    - `manter_cena=1` chegou à largada e seguiu na corrida sem crash;
    - a guarda do anel não pulou nada;
    - o visual do carro ainda precisa ser pulado na carga.
    
    Os draws do mundo na carga apareceram nesta rodada só no último segundo (436 por quadro): o terreno ficou
    pronto 0,85 s antes da largada.
  - **Rastreio (`rastrear_buffers=1`):** loga o preparo de GPU, a montagem e a destruição do conjunto e o
    `free` do alocador (`[obj+8]`, vt[5] `0x860020`). Também arma o watchpoint de hardware no array e no
    ponteiro dele, e vigia o array a cada quadro.
    - O VEH do GhostLab tinha um bug: com o Dr6 zerado (Wine) ele escolhia o slot 3 e, com menos de 4
      endereços, não logava. Corrigido: com Dr6 zerado, usa o slot 0.
  - **Abrir o jogo logo depois de um `kill -9`** dá travas e crashes no boot (`8d1d20`, `d6f9b8`, espera em
    `c63cb0`). Depois de um crash: matar o `CrashSender1405` e o `dirtrally2.exe` (`pgrep -x`), esperar o
    wineserver sair e só então abrir.
  - **Crash com `rip` lixo:** o CrashLogger do GhostLab também loga os quatro primeiros qwords de `[rsp]`
    ("GhostLab[crash]: [rsp]"). Num salto por ponteiro ruim, `[rsp]` é o retorno de quem chamou.
  - **O fundo com o mundo (2026-10-07, noite, runs cam53–60):** com `manter_cena`, `forcar_vis`, `adiantar_tudo`
    e `todas_celulas` (a vista da tela recebe as 194 células) o mundo desenha na carga, mas o fundo nunca era
    escolhido. Duas causas:
    - **MSAA 4x.** O jogo do dono roda com `multisampling="4xmsaa"` (`hardware_settings_config.xml`). Os alvos
      1920x1080 r11g11b10f e rgba8 têm 4 amostras, e o filtro exigia 1. Agora o `ViewFor` resolve
      (`ResolveSubresource`) o alvo numa textura nossa a cada quadro.
    - **HDR.** A cena é luz linear (terreno ~3, céu e névoa ~90–190) e saía branca. A LoadView mede a exposição
      (média log dos pixels acesos, numa cópia lida a cada ~0,25 s, linha "LoadView[hdr]") e o overlay aplica
      exposição, curva ACES e gama 2,2 num pixel shader próprio.

    Linha do tempo a partir da tela preta:

    | Evento | Cache quente (cam56) | A frio (cam54) |
    |---|---:|---:|
    | abre o `.nefs` da pista | 3,5 s | 5,3 s |
    | vis e objetos lidos | 4,6 s | 7,0 s |
    | terreno em 6, desenhado no fundo | 5,3 s | 8,1 s |
    | vis do jogo pronta (céu, névoa) | 7,9 s | 13,1 s |
    | largada | 8,8 s | ~16 s |

    **O que aparece não é a pista.** No fundo sai só uma elipse pequena, no alto à direita. Depois vem uma névoa
    uniforme. Duas rodadas de enquadramento:
    - **cam58:** `posicao=` (a referência `[renderer+0x2060]+0x1140`) no olho da câmera. A imagem ficou idêntica.
    - **cam59:** câmera quase a pino sobre a largada. A elipse some e a névoa muda de forma, então a câmera conta,
      mas o terreno da largada não aparece de jeito nenhum.

    Os logs já mostravam a nossa pose na câmera de render (`[cena+0x38]`) e no olho da lista de células
    (`[cena+0x15c0]`). Portanto não é o recorte: nessa fase o motor não desenha o terreno perto da largada.
    Parado aí: seguir exigiria RE do desenho das células do terreno, sem garantia de ganho.
  - **Fundo da carga rápida = foto aérea (2026-10-07, noite):** o `loading_screen.py` também grava
    `captures/overlay/dr2hook/loadcover_<pista>_<rota>.ppm`, a foto do §11 com o traçado em branco, em
    1600x900. O `ring_deploy.py --quick` põe `fundo=Z:\...` no `dr2hook_quickload.ini`. O core passa o caminho ao
    overlay (`SetLoadCoverImage`), que a carrega no primeiro quadro ("Overlay: foto da carga") e a desenha
    escurecida atrás do terminal, cobrindo a tela. A foto tem prioridade sobre a cena da LoadView.
    - Na cam60 ela apareceu 1,6 s depois da tela preta e ficou até a largada, sem crash.
    - Os experimentos do mundo ficaram em `dr2hook_loadview.ini.off` na pasta do jogo (desligados).
  - **Câmera da corrida na carga (cam61–63):** a sonda `sonda_cb=<pasta>` do `dr2hook_loadview.ini`
    copia os constant buffers do vertex shader do 11º e do 151º draw do alvo grande da cena a cada 0,5 s,
    junto com 0x200 bytes de `[cena+0x17c0]` e de `[cena+0x38]`, em `cb_NNNN.bin`. O slot 3 (272 bytes) é o da vista:
    - +0x00: projeção;
    - +0x80: olho;
    - +0x90: vista×projeção relativa ao olho (translação zerada);
    - +0xd0: eixos do mundo da câmera.
    - Na carga (cam61) a GPU já recebia a pose da `olhar=`: a câmera nunca foi o problema.
    - Na corrida (cam62, só a sonda) o jogo põe a câmera em olho (17,46, 1432,84, -557,06), eixo z
      (-0,666, 0,051, -0,744), fov 55. São as mesmas coordenadas da `olhar=`.
    - Na cam63, com essa pose exata (`olhar=17.464,1432.836,-557.058,24.124,1432.326,-549.618` + `fov=55`, chave
      nova), a GPU ficou com a pose da largada desde +1,5 s da carga. O céu do fundo saiu igual ao da corrida,
      mas o chão, a pista e os objetos não foram desenhados: só céu e névoa. Isso apareceu ~1,6 s antes da largada.
    - Conclusão: o motor não desenha o conteúdo perto da largada nessa fase. A câmera da corrida não resolve isso.
      Fica a foto aérea.
    - A cam61 (pose antiga + experimentos) crashou em exe+0xbb96df na thread do jogo, logo depois do
      "preparar todos" (pilha +6b2d7e/+480d68/+497f35). A sonda não está nessa pilha.
- **`--edits`:** o `ring_objects.py --edits` aplica um `dr2-track-edits` v1 como o viewer faz:
  - a chave é (tipo, idnum);
  - movido troca a matriz;
  - apagado sai;
  - cópia (`added` + `src`) entra com o tipo do original;
  - só a `route_0` conta;
  - edições que não acham a instância saem no log ("N ficaram de fora").
- **Jogo:**
  - fecha o jogo e a janela de erro (`CrashSender1405`; com ela aberta a Steam acha que o jogo ainda roda e não
    abre outro) e espera o wineserver do prefixo sair. Um boot logo depois de fechar o jogo crashou uma vez
    (`exe+0x8d1d20`, chamada virtual num objeto nulo logo depois do `persistentDB.pssg`); não voltou em 3
    aberturas seguidas;
  - apaga um `dr2hook_cmd.txt` velho, porque ele rodaria no boot;
  - grava no `dr2hook_autostage.ini`: `enabled = 1`, `once = 1`, `portugal` / `dr2hook_ring` / `route_0`;
  - abre com `steam steam://rungameid/690790`;
  - lê o `dr2hook.log` (ignora um log de antes da abertura) e cada fase vira uma etapa da barra:

    | Etapa | Termina em | Run de 2026-10-07 |
    | --- | --- | --- |
    | Abrindo o jogo (Steam) | o processo existe | |
    | Iniciando o jogo | "AutoStage: Fast-path" | 08:09:52 |
    | Carregando os dados do jogo | "RaceEvent: carregando" | +3,8 s |
    | Carregando a pista | os arquivos abertos chegam aos da última vez, ou 1,5 s sem abrir nenhum | +3,4 s (1564 arquivos) |
    | Preparando a largada | "RaceEvent: largada" | +1,6 s |

    - Em "Carregando a pista", o andamento é a contagem de "LoadTrace: open" desde o "carregando" contra a
      da última vez (`cache.json`, 1600 sem histórico). Nas outras, é o tempo da última vez.
    - "LoadTrace: IO" não serve de marca: sai a cada segundo com leitura, inclusive no boot. Depois do último
      arquivo vêm ~5 s sem abrir nada até a largada.
    - Uma marca só vale depois da anterior; a da largada fecha todas. Cada fase tem limite de 5 min.
    - O log é lido em bytes: ele tem `\r\n`, e a posição em modo texto se perdia (linhas repetidas).
    - As linhas do jogo que interessam (AutoStage, LoadProbe, RaceEvent, LoadCover, `[crash]`, `[ERROR]`)
      vão para o log do viewer.
    - Para com erro numa linha `[crash]`, mesmo com a janela do CrashRpt segurando o processo vivo, e se o
      jogo fechar.
- **Modos:**
  - `--mode freecam` manda `key f9` pelo canal de comandos na largada;
  - `--mode bot` deixa o benchmark dirigir;
  - `drive` para com erro, porque o AutoStage ainda não passa o controle ao jogador.
- **Antes de começar** confere os arquivos de entrada, a overlay e os dois `.ini` (`dr2hook_loadprobe.ini` com
  `enabled`, `overlay_dir` e `track_alias`).
- **Protocolo do stdout:** `@step k n texto`, `@progress 0..1` (pelo log do jogo ou pela duração da última vez),
  `@done texto` e `@fail texto`. Qualquer outra linha é log.
- **No viewer3d (F5 ou o botão verde):**
  - grava as edições atuais em `build/re/ring_deploy/viewer.edits.json`;
  - roda o script num grupo de processos próprio (`app/launch.cpp`);
  - lê o protocolo sem travar a janela (`core/progress.cpp`);
  - Cancelar manda SIGTERM ao grupo. O jogo, aberto pela Steam numa sessão à parte, segue.
- **Testes:** `python3 -m unittest scripts/research/test_ring_deploy.py` (edições, `.ini`, assinatura) e
  `test_progress` no `core_tests`.
- **Verificado:** a saída do porte bate byte a byte com a overlay de antes (0 de 10 arquivos mudaram). O
  cancelamento no meio não deixa processo nem arquivo pela metade.
  - O primeiro F5 com câmera livre crashou pelo `cameralines.cqtc` errado (§12.1); corrigido.
  - Pelo script, 2026-10-07: bot normal, bot com `--quick` e câmera livre com `--quick` chegaram à largada, com
    a tela preta do boot até a largada. A câmera livre ficou 15 s ligada sem crash (parada).
