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
