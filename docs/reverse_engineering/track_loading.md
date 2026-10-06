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

## 8. Em aberto

- Formato do `base.ctpk`.
- Decodificador do BXML (`\x01BXML`/`\0BXML`; o `bxml.py` do repo é outro formato).
- Quem liga o bit 8 do pedido de montagem e o que é o "Patching from disc".
- Prioridade entre uma camada de pasta e o `.nefs` montado no mesmo ponto.
- Uso do UAV nos VB do terreno.
