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

- Formato do `base.ctpk`.
- Decodificador do BXML (`\x01BXML`/`\0BXML`; o `bxml.py` do repo é outro formato).
- Quem liga o bit 8 do pedido de montagem e o que é o "Patching from disc".
- Se o cache de dataset guarda os arquivos da camada entre corridas. Passam pela camada (medido): `tracksplit.pssg`, `route_0/objects.ens`, `trees.bin`, `ornaments.bin`, `track.vis`, `progress_track.xml`, `ai_track.xml`, `*.cqtc`, `landscape.heightfield`, `grass.grs`, `drivable_entities.jpk`. O `track.jpk` (colisão) não apareceu.
- Formato do `track.vis` e quem o lê (ver §9.6).
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
  - Corte: `((b >> 12) << 16 | c) / 2^20`, como fração da caixa do nó.
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
