# Como o jogo carrega e desenha a pista (mapa vivo, 2026-10-07)

Documento de trabalho: tudo o que já foi medido sobre **como a pista sai do disco e chega à tela**, num lugar só,
para continuar a RE do render a partir daqui. Os detalhes de cada rodada (cam47–cam63) e as pilhas completas estão
em [track_loading.md](track_loading.md) §12.3. Os formatos dos arquivos estão em
[track_formats.md](track_formats.md).

Medido no `portugal/dr2hook_ring/route_0` (o Ring, sobre a Montalegre), com o bot, carga rápida (`--quick`),
1920x1080 e MSAA 4x, no Wine. Endereços são do `dirtrally2.exe` com base 0x140000000. "+xxxx" = RVA. O que ainda é
hipótese está marcado **(hipótese)**.

Em uma frase: **a pista carrega em ~8–16 s, o jogo só começa a desenhar o mundo no último 1–4 s, e o que segura o
chão perto da largada não é a câmera.** Com a pose exata da corrida na carga, o céu sai certo e o chão não aparece.

## 1. Linha do tempo de uma carga

A partir da tela preta da carga rápida. Cache quente = segunda carga seguida; a frio = primeira depois do boot.

| Evento | Quente (cam56/63) | A frio (cam54) | Como se vê no log |
|---|---:|---:|---|
| processo e tela preta | 0 | 0 | `LoadCover: tela preta` |
| parada de ~1,8 s lendo o `persistentDB.pssg` | ~1–3 s | | `LoadTrace: frame travado` |
| abre o `.nefs` da pista | 3,4–3,5 s | 5,3 s | `abre ... dr2hook_ring.nefs` |
| `track.vis` e objetos lidos | 4,6 s | 7,0 s | aberturas na overlay (verde no terminal) |
| terreno 7→4→5→6 (pronto) | 5,3 s* / 7,8 s | 8,1 s | `LoadView[mundo]: ... estado 5 -> 6` |
| "preparar todos" dos sistemas | ~7,7 s | | `LoadView[adiantar]/[sist]` |
| vis do jogo pronta (céu, névoa) | 7,9 s | 13,1 s | `LoadView[vis] ... 5ae0=1` |
| cena 3D começa no alvo grande | ~7,9 s | | `LoadView: a cena 3D comecou` |
| lista real de células da vista principal | ~8,3 s | | `principal ... /64 células` |
| largada | 8,7–8,8 s | ~16 s | `RaceEvent: largada` |

\* 5,3 s só com `adiantar_tudo=1` (§7); sem ele o terreno fica pronto 0,8–4 s antes da largada.

Durante toda a carga o jogo **apresenta quadros** (~50 fps, `Present` normal), só com a interface e o
pós-processo. Isso é o que permite pôr coisas na tela durante a carga (terminal do log, foto aérea).

## 2. Do disco à GPU

```
catalogues/base.ctpk ─ CatalogueManager ─► 0x140487480 (tokens %location% %track% %route% ...)
        │                                            │ 0x140c5ff80("tracks/track_loader.xml", tokens)
        ▼                                            ▼
raceload.jpk (game_1.dat) ─► track_loader.xml (BXML, ~200 entradas)
        │  cada entrada: processor= filename= pool= userdata=
        ▼
processadores (TrackManagerPlugin, OrnamentsPlugin, TreesPlugin, EntityManager, StreamingPlugin, ai, ...)
        │  lêem do .nefs da location montado (overlay captures/overlay na frente)
        ▼
PSSG 0x1408e7360 → objetos por classe 0x1408f3890 → D3D11 (IB IMMUTABLE u16, VB DEFAULT + UAV)
        ▼
sistemas do jogo (estado 2 → 3 → 4) ─► mundo/terreno 4 → 5 → 6 ─► vis ─► quadro
```

- **Arquivos da pista** (linhas do `track_loader.xml`): `tracksplit.pssg` (terreno, pool `PSSG_TRACK`),
  `objects.pssg` (ornamentos), `route_N/objects.ens` (entidades), `tracksplit.bin` (`streaming_grid_info`,
  StreamingPlugin), `landscape.heightfield`, `route_N/track.vis` (pool `VISIBILITY_SYSTEM`), `ai_track.xml`,
  `patchup_ot.pssg` (o maior, ~823 MB na Montalegre), árvores, `ground_cover`, `grids.pssg`, câmeras.
- **Ordem de leitura** (Montalegre, §4 do track_loading): track.jpk → XMLs → route_0 → texturas →
  grids → objects → patchup_ot → árvores, água, ground_cover → tracksplit → rotas → objects.ens →
  route_patchup_ot. O `patchup_ot.pssg` da overlay é aberto ~1.300 vezes seguidas (um open por leitura).
- **Parse** (Montalegre): tracksplit 1,3 s, patchup_ot 1,7 s, objects 0,4 s.
- **GPU do terreno:** 2075 index buffers (IMMUTABLE, u16) e 2578 vertex buffers com bind
  `VERTEX_BUFFER|UNORDERED_ACCESS` **(hipótese: a GPU reescreve vértices do terreno)**.

## 3. Sistemas e estados (o que segura a carga)

- **Ciclo dos sistemas** (`rastrear_sistemas=1`): cada sistema tem o estado em `[sys+0xd8]`.
  - 2 = carregado; 2→3 por `0x140ba8890` (prepara; vt[0x28] verdadeiro); 3→4 por `0x140bbed60` (ativa;
    vt[0x30] verdadeiro).
  - Os "todos" `0x140ba88d0` e `0x140bbeda0` percorrem a lista `[mgr+0x118..+0x120]` (entradas de 0x18). O
    jogo os chama **uma vez, no fim da carga** (chamador `+0x497eac`, thread principal).
  - `0x140bc6560` só atualiza quem está em 4.
- **Mundo** `0x14159d770`: leva o terreno `[+0xf8]+0xd0`. Chega ao estado 2 (`[+0x100]=1`) ~2,6 s antes do
  "preparar todos", que espera o resto (patchup_ot, entidades).
- **Terreno** `[mundo+0xf8]`, estado em `+0xd0`:
  - 7 = sem dados; o worker `0xa96150` passa para 4 quando os dados chegam;
  - `0x6c2de0(mundo, dt)` (chamador `+bc66e8`) → `0xacc960`: 4→5→6, um passo por quadro;
  - 194 células (`[terreno+0x38610]`).
- **Visibilidade/multidão** `0x1416ba550`:
  - o preparo `0x1404c49c0` monta o objeto de vis `[sys+0xf0]`; `0x140c15020` liga o `+0x5ae0` que a consulta
    de células `0x140c2de70` exige;
  - o preparo inclui a **multidão**, que busca nomes nos bancos PSSG enquanto o `patchup_ot` ainda carrega.
    Por isso a vis só fica pronta no fim, e montá-la cedo crashou em todas as tentativas (cam47–52);
  - antes do "preparar todos", `0x140497e20` → `0x1404d3570` copia `[vis+0x5498]` e `[vis+0x54a0]`.

## 4. O quadro: modos do renderer e da cena

- **Renderer** (`rcx` do quadro):
  - `+0x22a8` modo atual (0 corrida, 1, 2 carga), `+0x22ac` modo pedido;
  - `+0x2050` cena "main game" (quase sempre `0x15480e80` ou `0x15490e80`);
  - `+0x70` e `+0x78`: os dois Ms (gerentes de itens de desenho); `+0x2060`: objeto da referência de posição
    (`+0x1140`).
- **Troca de modo:** `0x476cb0` pede 2; o handler `0x479860` pede 0 quando o terreno começa a carregar;
  `0x480220` grava pedido vindo de mensagem. A troca real é o `SetMode 0x4a1580(raceObj, modo)`, que grava os
  dois campos e chama `0x3c4a80` (grava `[cena+0x1e98]`, zera `+0x1ed8`). **Só roda na largada.**
- **Quadro** `0x4b26f0` (por quadro):
  1. preparo `0x4aabd0(renderer, rdx)` (chamado em `+0x4b36a0`). Pulado se `0x1d2b30([renderer+0x2298])`.
     Com modo ≠ pedido só retorna. Em modo 0: atualiza a cena (`3c4190`/`3c6910`/`3cdf20`), diretor de câmera
     `0x9d9b80`, e `0x3c7aa0(cena)` (limpa e enche as listas visíveis; dispara os jobs de vis com `1e98 == 0`).
  2. quadro da corrida `0x4b1990`: modo 0 desenha o mundo (sondas IBL, câmera, mapas de ambiente, cena); modo
     1/2 só chama a cena (vt+0xa8), que na carga sai só com pós-processo. Grava `[[renderer+0x38]+0x16308] =
     (modo == 0)`. Troca de modo aplicada em `+0x4b261a`.
  3. fim do quadro `0x497fe0` (`+0x4b3462`): **só com modo 0** zera os buffers de itens de desenho dos dois Ms
     (`0xab16f0` zera `[M+0x1b28]`; `0xab1870` zera `+0x14d8`).
- **Cena** (`[renderer+0x2050]`), campos conhecidos:

  | campo | o que é |
  |---|---|
  | `+0x38` | câmera de render: índice do quadro em `+0x168`; olho em `rc+idx*64+0x40`, eixo z em `+0x30` |
  | `+0x1008` | terreno visto pela `ground_cover_camera` |
  | `+0x1018` | M (itens de desenho): buffer `[M+0x1b20]` (0x3ac0 itens de 24), contador `[M+0x1b28]`, conjunto de instâncias `[[M+0x1d30]+0x20]` |
  | `+0x1038` | objeto de visibilidade (travas `+0xa8`, `+0x5ae0`, `+0x4b0` = dados do track.vis) |
  | `+0x1190 + k*24` | caixas de itens `{n, ptr @+8, cap @+0x10, ligada @+0x14}`: 0 = células do terreno, 2 = objetos (~2318), 17, 18 |
  | `+0x1328/+0x1330` | objetos para o cull (quantos / array) |
  | `+0x14e0` | culler (máscara `+0x114`, modo `+0xf4`, byte `+0x160`, olho `+0xe0`) |
  | `+0x15c0` | olho usado na consulta de células |
  | `+0x17c0` | câmera da cena (vtable `exe+0x138cd38`), §5 |
  | `+0x1900`/`+0x1908` | listas de visíveis; a vista principal copia (`0x9cc0f0`), p.ex. vista+0x30 = cena+0x19f8 |
  | `+0x1e98` | modo da cena (cópia do do renderer); ≠ 0 desvia a vis e o cull |

- **Leitores de `[cena+0x1e98]`:** `39ccb0` (cull dos objetos), `3c9170` (passe do mundo), `39dd70`, `3ba760`,
  `3bada0`, `3bed80`, `3c1520`, `3c7aa0`, `3c8580`, `3caa60`, `9530f0`, `953f80`, `9ac9b0`, `599160`, `5a0e70`,
  `5bbfd0`, `5bd3d0`. O cull também sai cedo com o byte global `[exe+0x169af48]` ≠ 0.

## 5. Câmeras

Há quatro, e a pose passa de uma para a outra nesta ordem:

1. **Câmera da especial / FreeCamera:** `[[exe+0x168caf0]+0x20]+0x1cf8`, linhas `+0x210..+0x240`
   ([camera.md](camera.md)).
2. **Câmera da cena** `[cena+0x17c0]`: `+0x60` e `+0xa0` = mundo (direita, cima, trás, olho, linha a linha);
   `+0x150` projeção; `+0x190` vista; `+0x1d0` vista×projeção. Na corrida o **diretor de câmera** `0x9d9b80`
   (no preparo e de novo no render da cena, `9d6cc0`) a escreve a partir da câmera do carro. Na carga não há
   carro: ela fica na origem `(0, 1, 0)` olhando −z, projeção fov 90, perto 0,2, longe 1000.
3. **Câmera de render** `[cena+0x38]`: cópia da da cena, via `0x6a2ad0`, por quadro.
4. **GPU:** constant buffer do slot 3 do vertex shader (272 bytes), gravado por quadro:

   | offset | conteúdo |
   |---|---|
   | +0x00 | projeção (4 linhas) |
   | +0x40 | inversa da projeção **(hipótese, pelo padrão 1,778 / 1 / −2,5)** |
   | +0x80 | olho (x, y, z, 1) |
   | +0x90 | vista×projeção **relativa ao olho** (translação zerada: a GPU desenha em coordenadas relativas à câmera) |
   | +0xd0 | eixos do mundo da câmera (direita, cima, trás) |

   Medido pela sonda `sonda_cb=` (§8). Os draws de interface usam o mesmo slot com uma ortográfica.

**Pose da largada na corrida (cam62, Ring):** olho (17,46, 1432,84, −557,06), eixo trás (−0,666, 0,051, −0,744),
fov vertical 55°, 16:9. É a câmera de perseguição, atrás do carro. As coordenadas do mundo são as mesmas do
editor depois do giro/deslocamento do Ring (`ring_tracksplit.start_transform`).

**Na carga, a nossa pose chega à GPU** (cam61 e cam63): com `olhar=` (+ `fov=`) o slot 3 fica com o olho e a
rotação do ini desde +1,5 s. **Mas só no passe de cor** (cam124–127, §10 item 1): o diretor de câmera
`0x9d9b80`, chamado com o bit 0 de `flags`, monta e envia **ele mesmo** o CB 3 do pré-passe de profundidade
(escrita em `9da62b`, a partir de `9d6f9e` no render da vista `9d6de4`). A fonte dele (`r8+0x10`: cima, direita,
frente, olho) é uma cópia da vista, que na carga fica na origem. O fov vem do campo `+0x124` da câmera da cena,
em graus (`+0x128` perto, `+0x12c` longe, `+0x130` aspecto); o diretor refaz a projeção a partir dele.

## 6. Visibilidade, células e itens de desenho

```
consulta do track.vis 0x140c2de70 (olho [cena+0x15c0]) → job 0x140c11b00 → lista [cena+0x1190]/[+0x1198]
        │
cull dos objetos 0x39ccb0 (job b2fa50 → b2a610 → b2a8b0 → b293a0 enche as caixas)
        │  com a lista de células chama o job do terreno (volta em +0x39d107)
        ▼
job das células 0xa7f700(terreno, ..., células r8, quantas r9d)  só com [terreno+0xd0] == 6
        │  0xbcb2d0 insere nos coletores
        ▼
0x6a1a70 push nas listas da cena (pool {cap, nós, contador}; 0 = cheio); cull 0x953220 empurra objetos
        ▼
0xa78150 empurra itens de desenho no M (blocos de 64 com lock xadd [M+0x1b28], SEM checar a capacidade)
        ▼
passe do mundo 0x3c9170 (vt+0xa0 da cena; r9 = vista, lista em [vista+0x18]) → 0xbbf4a0 desenha a lista
```

- **Chamadores do job de células:** `+0x39d107` = vista da tela (principal); `+0x3bad93` = `ground_cover_camera`
  (vista de cima 128x128 da vegetação, `0x3bad20`); `+0x3bacb1` = as 6 faces do fog renderer (`0x3bac20`,
  `0x392e40`).
- **Células por quadro** (linha `LoadView:` → "principal N jobs/M células"):
  - **corrida:** a vista principal recebe ~167 das 194 células; ~1.000 células por quadro somando todas as vistas
    (8–9 jobs);
  - **carga:** a vista principal chega com **0** células (a consulta do track.vis só anda com a vis preparada). Com
    `todas_celulas=1` ela recebe as 194, e o par (u32 índice, f32 0) vai com o float zerado.
    **Mesmo assim o chão perto da largada não aparece** (§9).
- **Listas desenhadas por chamador** (linha `LoadView[listas]`, chamadas/itens/itens com draw por segundo). Na
  corrida: `+bbdfe8` ~9 listas e ~1.000 itens (o mundo, 1920x1080), `+bbf5fd` ~20 listas e ~430 itens,
  `+6bf676` 65 itens, `+6bf6cb` 15, `+3c94f8` 13, `+3c951d` 11, `+3c2e73` 6. Na carga forçada o `+bbdfe8`
  sai com ~450 itens no alvo grande e depois migra para os 128x128.
- **Buffer de itens do M:** 0x3ac0 itens em `[M+0x1b20]` (criado pelo preparo de GPU `0xa95c40`, chamado pelo
  método `0xb920c0`). Só o fim do quadro em modo 0 zera o contador; forçar quadros na carga sem esse reset
  estourava o buffer sobre o conjunto de instâncias (achado com o watchpoint do GhostLab). Contorno: o hook do
  `0x497fe0` roda com `22a8 = 0` nos quadros forçados.

## 7. Passes da GPU

Contados pela LoadView (hooks de draw no contexto imediato; não há contextos adiados na corrida).

| Alvo | Carga normal | Corrida | Papel |
|---|---:|---:|---|
| tela 1920x1080 rgba8 | ~11 | sim | interface e composição final |
| 480x270 / 120x68 r11g11b10f | ~30 | sim | pós-processo (bloom, exposição) |
| 1024x1024 r32 (profundidade) | — | ~570 | **sombras**: não roda na carga, nem forçada |
| 1920x1080 r11g11b10f **MSAA 4x** | — | ~325 | cena HDR (luz linear) |
| 1920x1080 rgba8 MSAA 4x | — | ~255 | segundo alvo da cena (G-buffer/normal **(hipótese)**) |
| 128x128 r11g11b10f (×2) e rgba8 | — | ~270 / ~210 | faces do env-map/fog e ground cover |

- **Totais:** carga ~36–46 draws por quadro; corrida ~1.700–1.900 em ~50 alvos.
- **HDR:** a cena é luz linear (terreno ~3, céu/névoa 90–190, picos ~700). Sem a curva do pós-processo sai
  branca. Para mostrar o alvo, a LoadView resolve o MSAA (`ResolveSubresource`) e o overlay aplica exposição
  (média log), ACES e gama 2,2.
- **Pré-passe e passe de cor do chão (cam117–127):** o terreno desenha duas vezes:
  - **pré-passe:** VS com stride 24 (só POSITION), no alvo rgba8 MSAA 4x. Esse alvo guarda os vetores de
    movimento (`prevRelativeViewProj` em c10), e o passe grava a profundidade com teste "menor ou igual" (ds func 4);
  - **passe de cor:** VS com stride 56, no alvo r11g11b10f, com teste de profundidade **"igual"** (ds func 3) e
    o mesmo DSV.

  Se as duas câmeras ou projeções diferem, o passe de cor não escreve nenhum pixel e sobra o céu/névoa.
- **Constant buffers da cena** (RDEF do `shaderpack.pssg`):

  | slot | buffer | tamanho |
  |---|---|---|
  | 1 | PerFrame | 400 B: timeData, sol, fog, haze, wetLighting, vento… |
  | 2 | RenderTarget | 32 B |
  | 3 | CameraParams | 320 B |
  | 4 | MatrixPalette | 3264 B (skinning, não é do chão) |
  | 5 | ShadowParameters | 928 B (zerado na carga) |
  | 7 | Lights | 1904 B (sondas NaN na carga) |
- **Projeção:** fov 55 na corrida, perto 0,2 e sem plano longe (projeção infinita: linhas 2 e 3 = (0, 0, −1, −1) e
  (0, 0, −0,4, 0); profundidade −1 no plano perto e 1 no infinito).
- **Malhas estáticas (prédio, barreiras, pórtico, arquibancada):** stride **12** no VB 0 e vários fluxos (vb0 s12
  posições, vb1 s20/s36/s44 atributos, vb2 s40 instâncias com TANGENT1–4). O VS instanciado só liga o CB 3. Os
  testes de SRV da m168–m171 filtravam os strides 24/52/60/64 e não pegavam essas malhas.
- **Luz das malhas estáticas e do carro: grade de sondas de luz.** O PS lê `gProbeGridIndexList` (t3) e
  `gProbeLightBuffer` (t21). Zerar os dois no stride 12 na corrida deixa o prédio preto igual ao da carga (m180).
  Caminho por quadro:

  ```
  cull dos objetos 0x39ccb0 (rsi = cena) → 0x39d564 → consulta de luzes 0x6a3f60(0x1415a3ee0, ...)
        │  lista de células [cena+0x1348]/[cena+0x1340] (pares u32 célula + f32, como a do job de células)
        ▼
  0xc34680 (r14 = 0x1415a4010): para cada célula da lista, percorre as listas de luzes [r14+0x7f8/0x800/
        0x808/0x810] e pega as luzes com [item+0xc4] == célula (sondas: [r14+0x810], bit 1 de [nó+0x110])
        ▼
  vtbl+0x40 do objeto em 0x1416b0440+0xf0 = 0x6a1320: põe a sonda na lista (0x44 B em +0x5870, conta +0xd54,
        até 31); as dinâmicas vão para +0x1870 (64 B, conta +0xd50)
        ▼
  0x6c4310 (de 0x4b1d9d; só com o byte "sujo" [cena+0x15d8]): vira o índice duplo +0x1734 e sobe
        gDynamicLightBuffer/gProbeLightBuffer ([+0x1768+i*8] / [+0x17a8+i*8]); 0x6afec0 liga os nomes
  ```

  Na carga a atualização roda e está suja, mas com **0** sondas (na corrida 9, com 2 dinâmicas): a lista de
  células chega vazia, e as 11 sondas do Montalegre, já registradas e ligadas, ainda estão com célula **−1**
  (na corrida, 42..52). A célula só é atribuída quando a vis fica pronta.

## 8. Ferramentas e chaves

- **LoadProbe** (`dr2hook_loadprobe.ini`, no `dxgi.dll`): arquivos, PSSG, buffers e texturas da GPU, manifestos.
  Também aplica a overlay (`overlay_dir`, `overlay_early`). §5 do track_loading.
- **LoadTrace:** CreateFileW/ReadFile e quadros travados.
- **LoadView** (`src/core/load_view.cpp`, no core): sempre conta draws por alvo durante a carga e 3 s depois da
  largada. Os experimentos só ligam com `dr2hook_loadview.ini` na pasta do jogo (hoje ativo, com
  `forcar_vis`, `manter_cena`, `olhar=auto`, `fov=55`, `adiantar_mundo`, `todas_celulas`, `todas_zonas` e
  `adiantar_tudo=0`):

  | chave | efeito |
  |---|---|
  | `forcar_vis=1` | preparo `0x4aabd0` e quadro rodam como modo 0 na carga (jobs de vis, reset do M) |
  | `manter_cena=1` | `[cena+0x1e98]` fica 0 entre quadros forçados: o mundo desenha no alvo grande |
  | `forcar_mundo_ms=N` | quadro da carga como modo 0 depois de N ms |
  | `olhar=x,y,z,ax,ay,az` | pose na câmera da especial e na da cena (depois do diretor de câmera) |
  | `olhar=auto` | pose por rota, lida de `dr2hook_olhar.ini` (`<pista>_route_N=olho,alvo,fov`); a largada de cada carga grava a pose da câmera da especial (`+0x210`) e o fov da cena (`+0x124`). A rota é a chave do catálogo da tela de carregamento sem o `lng_` (`AutoStage` → `NotifyRoute` → evento `kDr2StageRoute`); a 1ª carga de uma rota só aprende |
  | `fov=55` | projeção junto com `olhar=` (a da corrida é 55) |
  | `posicao=x,y,z` | referência `[renderer+0x2060]+0x1140` (sem efeito visível, cam58) |
  | `adiantar_mundo=1` | prepara e ativa só o mundo assim que ele chega a 2 (terreno pronto ~2,6 s antes) |
  | `adiantar_tudo=1` | idem para cada sistema da lista, até o "preparar todos" |
  | `adiantar_vis=1` | monta as células da vis cedo (a multidão crashou, cam47–52) |
  | `todas_celulas=1` | vista principal recebe as 194 células quando chega vazia |
  | `todas_zonas=1` | **luz da carga:** a consulta de luzes `0x6a3f60` recebe a célula −1 e todas as do terreno quando chega vazia; as sondas sem célula entram e as malhas estáticas acendem como na corrida (m187–m189) |
  | `vigiar_carro=1` | loga quando o visual do carro (`0x6ee9e0`) é chamado e o que há em `[[carVis+8]+0xa28]` (`LoadView[carro]: carga/livre t+ms …`) |
  | `carro_na_carga=1` | não pula o visual do carro nos quadros forçados. Roda sem crash no Ring (r4), mas não faz o carro aparecer na carga (§9) |
  | `vigiar_luzes=1` | com `rastrear_geo=1`: chamadas de `0x6c4310` por fase (sujo, contas +0xd50/+0xd54), quem chama `0x6a1320` (`luz+`), argumentos da consulta (`consulta luzes`), células da corrida (`zonas corrida:`) e a lista de sondas do sistema (`sondas`: na lista, com nó, ligadas, faixa de células) |
  | `forcar_luzes=1` | liga o byte sujo `[cena+0x15d8]` a cada quadro da carga (não precisou: já vem ligado) |
  | `zerar_slots=<máscara>` / `zerar_stride=N` / `zerar_na_carga=1` | solta SRVs do PS (bit = slot) nos draws do stride N, na corrida ou na carga (m180: `0x200008` = t3+t21 no stride 12 reproduz a carga) |
  | `vigiar_vb=N` | layout e strides de todos os VBs dos draws com o VB 0 de stride N (`LoadView[vb]`) |
  | `vigiar_z=1` / `z_igual=<func>` | conta draws por função de profundidade e stride; troca o "igual" da carga por outra função (m175: com `vigiar_z` as malhas s12 sumiram nas duas fases, sem explicação; evitar) |
  | `vigiar_prebake=1` / `forcar_prebake=<v>` | censo dos uploads de CB por tamanho (`LoadView[pre]`) |
  | `vigiar_cb3=1` / `luz_cb3=<valor>` | leftEye do CB 3 (lixo da pilha; refutado como causa, m162–m167) |
  | `rastrear_sistemas=1` / `rastrear_buffers=1` / `vigiar_vis=1` | logs de estado, conjunto de buffers, watchpoint em `[vis+0x4b0]` |
  | `sonda_cb=<pasta>` | dumps dos constant buffers do VS + câmeras a cada 0,5 s (`cb_NNNN.bin`) |
  | `sonda_stride=N` / `sonda_stride2=N` | linhas `LoadView[draw]` (3/s cada) dos draws com o VB 0 de stride N: layout, VBs, CBs do VS e do PS com conteúdo (`cbdados`, `pscb`), SRVs do PS (`pssrvs`), alvos (`rt`), blend, rs/ds/vp e pilha |
  | `vigiar_cb0=1` | quem escreve os CBs 0, 3, 4 e 5 da sonda; `LoadView[cb3]` dá o olho e o p00 por pilha; `LoadView[diretor]` dá a fonte e as flags do diretor |
  | `despejar_hdr=<prefixo>` | alvo HDR da cena em PPM (`_NNN_{carga,corrida}.ppm`), com exposição do próprio quadro |
  | `foto_chao=1` | com `despejar_hdr`: alvo 0 antes e depois do 1º draw grande (≥ 2000 índices) da sonda (`_rt_NNN_…_{antes,depois}.ppm`) |
  | `z_sempre=1` | draws grandes da sonda passam sempre no teste de profundidade na carga (diagnóstico: com stride 56 o chão aparece) |
  | `procurar_carro=x,y,z` | varre a memória RW a cada 2 s atrás de poses (4x4, 3x4 ou quatérnion) a ≤ 3 m do ponto (`LoadView[carro]`) |
  | `forcar_sombra=1` | passe de sombra na carga: no preparo das cascatas (`0x3c62d0`) liga `e11` se `e10=1` e `e12=0`; o desenho (`3c1090`) só é forçado com as cascatas prontas (`CascadesReady`). Funciona (m145), mas não clareia os objetos. Hoje o `e11` só é forçado com `vigiar_cb0=1` junto |
  | `luz_olho=1` | com `olhar=`: a escolha da sonda de IBL (`0x4b4690`) recebe o olho da pose na carga; log `carga/corrida ibl pos` (refutado: a posição já era a do olho) |
  | `vigiar_tex=1` | com `rastrear_geo=1`: conta, por fase e stride, os draws da cena com SRVs do PS ≤ 8x8 (linha `LoadView[tex]`, 1/s) |
  | `rastrear_geo=1` | linhas `LoadView[vista]` / `[tex]` (as portas de sombra e o censo de texturas só são gravadas com ela) |
  | `modo_render=N`, `zerar_cb7=1`, `ligar_cb4=1`, `forcar_cb0=1`, `pular_stride_carga=N` | experimentos refutados (§10 item 1) |

- **Leitor da sonda:** `python3 scripts/research/load_cb_dump.py <pasta>` (câmera da GPU por dump) ou
  `... <arquivo> <tiro> <slot>` (slot em float4).
- **Linhas do log da LoadView:** `LoadView:` (alvos, itens, células, jobs por segundo), `[listas]`, `[hdr]`,
  `[push]`, `[vis]`, `[camera]`, `[especial]`, `[camrender]`, `[forca]`, `[mundo]`, `[celulas]`, `[jobcel]`,
  `[sist]`, `[adiantar]`.

## 9. O que aparece na carga com cada experimento

| Rodada | Configuração | O que se viu |
|---|---|---|
| sem ini | — | nada do mundo até ~1 s antes da largada |
| antes da cam53 | `forcar_vis` | itens e células, mas só nos 128x128 (câmera na origem) |
| antes da cam53 | + `manter_cena` | mundo no alvo grande (587–1.179 draws); crashes do carro e do buffer, contornados |
| cam53–57 | + `adiantar_tudo`, `todas_celulas`, MSAA/HDR resolvidos | fundo aparece a partir de +5,3 s: elipse pequena no alto à direita, depois névoa |
| cam58 | + `posicao=` | idêntico |
| cam59 | câmera a pino na largada | a elipse some, a névoa muda; nada do chão da largada |
| cam61 | sonda, pose antiga | GPU recebe a pose; crash em `exe+0xbb96df` logo depois do "preparar todos" (pilha `+6b2d7e`/`+480d68`/`+497f35`) |
| cam62 | só a sonda | pose da corrida medida (§5) |
| cam63 | pose exata da corrida + `fov=55` | céu e nuvens iguais aos da corrida; **chão, pista e objetos ausentes**; aparece ~1,6 s antes da largada |
| cam64 | igual à cam63 + log `LoadView[gates]` | sistemas da cena e globais iguais aos da corrida; **todas as caixas em 0 até a vis ficar pronta** (`4b0` em +7,4 s, `5ae0=1` em +8,0 s; largada em +8,8 s, cache quente); aí 170 células e ~700 objetos em `+0x11c0` |

| cam113–116 | CB5/CB4/CB7 do chão como na corrida, `modo_render`, `forcar_sombra` | nada muda (ou crasha) |
| cam117 | foto do pré-passe (stride 24) | morros e árvore no alvo de vetores de movimento: geometria e profundidade existem |
| cam120–121 | sonda do passe de cor (stride 56) | mesmas texturas e CBs da corrida; o draw **não muda nenhum pixel** |
| cam122 | + `z_sempre=1` | **a grama aparece**: o teste "igual" falha |
| cam124 | `sonda_stride2=24` | mesmo DSV e jitter; no pré-passe o olho é (0, 0, 0) e o fov 90 |
| cam125–126 | `LoadView[cb3]` + `[diretor]` | quem escreve a câmera do pré-passe é o diretor (`9da62b`), a partir da fonte da vista |
| cam127 | pose também na fonte do diretor (sem `fov`) | **pista completa na carga**: asfalto, grama, barreiras, pórtico, árvores |
| cam129–131 | + `fov=55` no campo `+0x124` | mesmo enquadramento da corrida; nos últimos quadros da carga volta a 90 (sem perder o chão) |
| cam132–134 | `procurar_carro=` no ponto da largada do Ring | a pose do carro só existe na memória ~1 s antes da largada (vtables `+1401da8`, `+1401b30`, `+1401e70` em +0xa0, `+11f70b0` em +0xb0); o `grids.pssg` tem várias grades e o jogo escolhe pelo modo, então a pose não sai de antemão |
| cam135 | `olhar=auto`, Ring | a largada gravou (17,48, 1432,91, −557,04) → (24,14, 1432,30, −549,60), fov 55: igual à pose acertada à mão |
| cam136–139 | `olhar=auto`, **Montalegre original** | a rota sai da tela de carregamento (os arquivos da rota ficam dentro do `.nefs`); a 2ª carga usa a pose; **2 crashes em 4 cargas** com `adiantar_tudo=1` (`exe+0x8bbadc` a +7 s; `exe+0xc06deb` quando a vis ganha `+0x4b0`) |
| m140 ×3 | igual, `adiantar_tudo=0` | **3 de 3 sem crash**; a carga mostra a reta, o pórtico, as arquibancadas e o público na pose da largada (prédio à esquerda escuro) |
| m141 | + `forcar_sombra` forçando só o desenho | crash `exe+0x8d9dcf`: cascatas nulas (`[o40+0xcb8+i*8]`) |
| m142–143 | + gancho no preparo `0x3c62d0` com 4 argumentos | boot travado; crash `exe+0x3c63a6`: a função lê um 5º argumento na pilha (`[rsp+0x28]`, em `3c638f`) |
| m144 | gancho com 6 argumentos | sem linhas `[vista]`: o flush exige `rastrear_geo=1` |
| m145 | + `rastrear_geo` | **sombra roda na carga** (portas iguais às da corrida, ~345 draws de sombra/s, sem crash); **objetos continuam escuros** (`luz145.png`) |
| m146 | + `luz_olho=1` | crash `exe+0xd6f9b8` no 1º quadro, pilha recursiva `85f2e0`/`85db5d`, sem o gancho novo (provável acaso) |
| m147 | só o gancho da IBL, logando | posição da sonda de IBL na carga = olho da pose: `luz_olho` não muda nada. Recorte do prédio (`crop147.png`): na carga é **silhueta preta pura** com janelas claras, árvore verde e faixa da Monster acesa; no 1º quadro da corrida (h_011) acende inteiro |

| m148–m159 | `vigiar_tex`, sondas dos CBs do PS | sem conclusão anotada; a hipótese do streaming caiu com a causa achada na m180 |
| m160–m167 | leftEye do CB 3 (`vigiar_cb3`, `luz_cb3`) | o valor da corrida chega na GPU na carga e o prédio segue escuro: refutado |
| m168–m171 | SRVs do PS zerados nos strides 24/52/60/64 | prédio igual: **teste inválido**, as malhas estáticas são stride 12 |
| m172–m177 | `vigiar_prebake`, `vigiar_z`, `z_igual`, `vigiar_vb` | contagem de draws por stride quase igual nas duas fases; m174 crash `exe+0x90d9bd` (lista `bbf589`, provável acaso); m175 com `vigiar_z` as malhas s12 sumiram; m177 "igual" trocado não muda a luz; `vigiar_vb` mostra os fluxos s12/s20–44/s40 |
| m180 | `zerar_slots=0x200008`, `zerar_stride=12`, na corrida | **prédio e carro pretos como na carga**: a luz vem da grade de sondas (t3/t21) |
| m181–m186 | `vigiar_luzes` | atualização `0x6c4310` roda na carga com 0 sondas (corrida 9); a consulta `0x6a3f60` recebe 0 células (corrida 17); com todas as células ainda nada: as 11 sondas estão com célula −1 |
| m187 | + `todas_zonas=1` (célula −1 na lista) | **11 sondas na carga; prédio, pórtico e barreiras acesos como na corrida** (`cmp187.png`) |
| m188–m189 | ini limpo + `todas_zonas=1` | 2 de 2 sem crash, acesos desde os primeiros quadros da carga |
| r1_0–r1_1 | **Ring** (`dr2hook_ring`), ini limpo + `olhar=auto` + `adiantar_tudo=0` + `todas_zonas=1` | 2 de 2 sem crash; pose da rota aplicada; pista, barreiras, pórtico e árvores acesos como na corrida a partir do 3º quadro da carga (os 2 primeiros: só céu e terreno escuro, antes do terreno pronto); carro só na corrida (`ring_r1_1.png`) |
| r2–r3 | Ring + `vigiar_carro=1` | o visual do carro (`0x6ee9e0`) só é chamado a partir de ~4,5 s, cerca de 0,9 s antes da largada: antes disso o carro não existe |
| r4_0–r4_1 | + `carro_na_carga=1` (visual do carro não é pulado) | 2 de 2 sem crash, mas o carro **não aparece** nos 4 quadros da carga depois que ele nasce. A lista de objetos do cull (`[cena+0x1328]`) fica em 0 até a largada e passa a 1 logo depois; o provável é que o carro só entre na cena na largada. Ganho máximo seria ~0,9 s, então paramos aqui (`ring_r4_1.png`) |

Fundo usado hoje na carga rápida: a **foto aérea** (`loadcover_<pista>_<rota>.ppm`, §11 do track_loading).

## 10. Em aberto (por onde seguir)

0. **Por que os objetos não desenham na carga (resolvido, cam64).** As caixas `[cena+0x1190+k*24]` não são
   entradas do cull, são **saídas da consulta de vis** `0x140c2de70`. Ela devolve tudo vazio enquanto o objeto
   de vis `[cena+0x1038]` não tem `+0x4b0` (dados do track.vis, gravado ~1,4 s antes da largada) e `+0x5ae0=1`
   (~0,8 s antes). Os sistemas da cena (`+0x1008` … `+0x17b0`) e os bytes globais `16afae8=2`, `15a0048=1`,
   `15a0049`, `159e614=1`, `15a8864`, `169af48=0` já estão como na corrida durante a carga; nenhum barra nada.
   O `todas_celulas` só enchia a caixa 0 (terreno); os objetos (caixa 2 = `+0x11c0`) seguem vazios. Para
   desenhar objetos antes disso seria preciso **fingir a vis** (encher a caixa 2 com todos os objetos) ou montar
   a vis cedo, que crashou na cam47–52 por causa da multidão. Ganho máximo: o mundo ~1 s mais cedo numa carga
   quente; numa fria, a vis também só sai no fim.
1. **Por que o chão não desenha com 194 células na vista principal (resolvido, cam127).** Não era streaming,
   LOD nem CB do material: era o **teste de profundidade "igual"** do passe de cor contra um pré-passe desenhado
   com outra câmera. O `olhar=` só reaplicava a pose depois do diretor (`HookDirector`). Mas o diretor, com o
   bit 0 de `flags`, já tinha enviado o CB 3 do pré-passe com a fonte da vista na origem e fov 90. Correção na
   LoadView:
   - antes do diretor, a pose vai para a fonte (`r8+0x10`, linhas cima/direita/frente/olho);
   - o `fov=` vai para `[câmera+0x124]`, e não para uma projeção montada à mão: a do jogo usa perto/longe
     próprios (−1,00003 / −0,400005), e qualquer diferença quebra o "igual".

   Refutados no caminho:
   - CB5 (sombras);
   - CB4 (é paleta de skinning);
   - índices NaN das sondas no CB7, zerados no VS e no PS;
   - conteúdo do cb0 (muda entre quadros);
   - SRVs do VS e do PS (iguais aos da corrida);
   - rs/ds/vp;
   - modo do render `0x4a1580` e as portas de sombra (`3c1090`, `3c3f10`).

   **Pendente:**
   - nos últimos quadros da carga a projeção volta a 90 (o diretor lê o fov da fonte quando `[fonte+0x70]` ≠ 0,
     campos `+0x74..+0x80` → `[câmera+0x134..+0x140]`);
   - barreiras, prédio, cerca e público saíam escuros. **Resolvido (m187, `todas_zonas=1`, §7):** as malhas
     estáticas se iluminam pela grade de sondas de luz, e na carga a lista de sondas ia vazia porque a consulta
     de luzes recebia 0 células e as sondas ainda estavam com célula −1. A chave passa a célula −1 (e todas as
     do terreno) na carga. Refutados no caminho (m141–m177):
     - **sombra:** o passe roda na carga com `forcar_sombra` e nada muda;
     - **posição da sonda de IBL** (`0x4b4690`, chamada de `4b1cb2` no modo 0 com
       `[[obj+0x2050]+0x17c0]+0xd0`): na carga já é o olho da pose;
     - **preset do render** `9ab130` (11 dwords em `+0x1e50..0x1e78`): o índice é 0 nos dois modos (provável);
     - **leftEye do CB 3** (m162–m167), **NaN do CB 7**, **"igual" da profundidade** (m177); o streaming de
       texturas caiu junto, porque a correção das sondas acende tudo;
     - cuidado: os testes de SRV da m168–m171 filtravam strides de objeto errados (as estáticas são s12).

     Notas do modo 2 (`SetMode 0x4a1580`, usado na carga):
     - chama `4a0970`;
     - faz `[[obj+0x2050]+0x1ea8] |= 8`, que barra `b3d400` em `4b5c5d`;
     - chama `o40->vtbl[0xd0](0)`, que zera `e11` a cada quadro (via `4a0a7d`|`4a1630` e `4a1650`);
     - chama `9ab130([obj+0x58], 0)`;
     - grava `[[0x14159fcd8]+0x2088]=1`, lido em `55e02b`;
     - grava matrizes fixas em `[obj+0x2060]+0x1110..0x1140`.

     O modo 0 chama `4a1460` e `9ab130(cfg, 47d950(obj))`.

     Sombras: `o40 = [vista r+0x40]` (vtable `0x126a5c0`), portas `e10..e13` em `o40+0xe10`, `n = [o40+0xe20]`
     (3 cascatas).
     - `3c3f10` (`vtbl[0xd0]`) liga `e11`;
     - `3c62d0` (`vtbl[0x50]`) prepara as cascatas se `e10 && e11 && !e12`;
     - `3c1090` (`vtbl[0x58]`) desenha via `39bbb0`, que lê `cb8`.
   - `adiantar_tudo=1` crashou o Montalegre original 2 vezes em 4 (cam137, cam139); sem ele, 3 de 3 limpas.
     Conferido no Ring (r1_0–r1_1, §9): sem ele a carga sai igual, enquadrada e acesa, 2 de 2 sem crash.
2. **Passe de sombra ausente na carga (resolvido, m145):** o modo 2 zera `e11` a cada quadro. `forcar_sombra`
   liga a porta no preparo das cascatas e só desenha com elas prontas. Não era a causa dos objetos escuros.
3. **A elipse da cam53–57:** o que exatamente é (céu? domo? terreno distante de baixa resolução?). Com a sonda dá
   para dumpar o VS do draw e ver a malha.
4. **Mapa completo dos passes do quadro na corrida:** ordem dos alvos, shaders e chamadores, para ter a receita
   de "como o jogo desenha tudo". A LoadView já tem as pilhas por alvo (`LoadView[pilha]`) e a sonda dá as
   constantes; falta juntar num roteiro por passe.
5. **O crash da cam61** (`exe+0xbb96df`, thread do jogo, depois do "preparar todos") com os experimentos de
   adiantar ligados; não se repetiu na cam63.
6. **Carro na carga (aberto, r2–r4):** o carro nasce ~0,9 s antes da largada e só entra na lista de objetos da
   cena na largada. Para aparecer antes, seria preciso pô-lo na cena mais cedo; o ganho é no máximo ~0,9 s.
