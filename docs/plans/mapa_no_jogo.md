# Levar o mapa de testes para dentro do jogo

Análise de 2026-10-06. **Nada aqui foi executado no jogo.** O que está marcado como fato foi lido em `docs/reverse_engineering/` ou no código; o resto é hipótese e diz como conferir.

## Objetivo

Abrir no *DiRT Rally 2.0* o mapa de testes do editor, a pista sintética **DR2Hook Ring** (`examples/tracks/synthetic__dr2hook_ring/`), e olhar para ele com o mínimo do jogo rodando. A ordem combinada com o dono:

1. Analisar como carregar o mapa de testes (este documento).
2. Depois, os testes do modo benchmark (carregar uma especial sem menus).
3. Depois, o editor (viewer 3D) alimentando esse caminho.

Ideia de uso final, do dono: um modo que entra no jogo só para **ver o mapa com a câmera livre**, sem gameplay; com um atalho (talvez F5) para pedir o carro e liberar a direção. O dono avisou que são ideias, e que o jogo talvez dependa do carro para carregar.

Regras que valem (`docs/plans/README.md`): a pasta do jogo é somente leitura; nenhum `.nefs` modificado entra no jogo sem OK explícito do dono; sem `git push`.

## O que já se sabe

| Fato | Onde |
| --- | --- |
| O jogo tem um modo benchmark. Com `-benchmark example_benchmark.xml` ele vai direto para a especial de NZ (`new_zealand_rally_01`, `route_2`), sem menus, com um carro de demonstração que dirige sozinho. | `stage_loading.md` §3.2, testado no jogo em 2026-10-01 |
| O carro do benchmark é dirigido por uma gravação (`RaceRecorderDataLoader "benchmark"`). O jogador não tem controle. | `stage_loading.md` §3.2 |
| O `BenchmarkManager` guarda a especial em strings: localidade, pista, rota, horário, clima, carro, livery (`StageDescriptor`). O XML do benchmark não escolhe pista; o **AutoStage** (hook em `0x1409d31c0`, no `DllMain` da `dxgi.dll`) sobrescreve essas strings pelo ini. | `stage_loading.md` §1, §3, §4 |
| Com a pista nativa (NZ `route_2`) o override funciona igual ao `-benchmark`. Com `usa / twin_peaks / free_roam` o jogo **crashou** ~2 s depois, em `0x140439044`, por `[rdi+0x32a0]` nulo. A causa é desconhecida: pode ser a pista, o `free_roam` (que não tem rota) ou a gravação de NZ. | `stage_loading.md` §5.1, §6 |
| O carregamento dura o mesmo no benchmark e no menu (~7 a 11 s de disco); o benchmark só pula os menus. | `stage_loading.md` §7 |
| O core tem câmera livre (`src/core/free_camera.cpp`, 9 velocidades). Nunca foi testada junto com o benchmark. | `camera.md`, `free_camera.cpp` |
| O `LoadTrace` (`dxgi.dll`) já intercepta `CreateFileW` e `ReadFile`. O jogo abre `locations/<localidade>__<pista>.nefs` por `CreateFileW` (é o que dispara `onStageLoad`). | `load_trace.cpp`, `stage_loading.md` §7, §9 |
| `tools/egodata/nefs_write.py` (`replace_files`) grava uma **cópia** do `.nefs` com arquivos trocados. O arquivo novo tem de manter o **mesmo número de blocos de 64 KiB**. O jogo nunca abriu um pacote assim. | `track_formats.md` |
| `tools/uiview/track/edit.py` aplica as edições do viewer (mover, girar, apagar, copiar) em `objects.ens`, `ornaments.bin` e `trees.bin`. Em `.bin` a contagem é fixa; um objeto apagado fica invisível em `y = -10000`. | `edit.py` |
| `ui_render.md` diz que `cars/*.nefs` e `locations/*.nefs` abrem com um bloco RSA-1024. **Nos pacotes de pista desta instalação isso não vale:** Montalegre e NZ começam com `NeFS` em claro (conferido no E0). | `ui_render.md`, E0 |

Tamanhos dos pacotes na pasta do jogo: o menor é `portugal__montalegre_rallycross.nefs` (884 MB); `new_zealand__new_zealand_rally_01.nefs` tem 1,95 GB; os de rali vão a 4,4 GB.

## O mapa de testes

DR2Hook Ring: circuito fechado de ~2,1 km, duas rotas, 164 malhas de terreno (152 mil vértices, índices de 32 bits), 12 tipos de objeto, ~1000 instâncias por rota, ~420 árvores, 16 texturas procedurais com lados em potência de dois.

Ele existe em dois formatos:

- **Exportado** (`track.json`, `terrain_0.bin` DR2M, `inst_route_N.bin` DR2I, `tex/*.webp`): o que os viewers leem. O jogo não lê isso.
- **Origem do jogo** (`source/route_N/objects.ens`, `ornaments.bin`, `trees.bin`, `textures/*.png`): só instâncias e imagens. Os hashes de tipo são FNV-1a do nome, e **não** os `reference_id` que o jogo usa (`*_references.xml`).

O que o jogo exige de uma localidade e o Ring **não tem** (`track_formats.md`): `tracksplit.pssg` com os blocos de shader do jogo (`terrain_wsm_*`, `batched_track.fx`), `objects.pssg` e `route_objecttypes.pssg` com a cadeia tipo → malha → colisão, `trees.pssg`, texturas em PSSG (DXT), `track.jpk` (colisão `.vcqtc`, **vértices não decifrados**), `track.vis`, `grass.grs`, `ai_track.xml` e `progress_track.xml` (XML binário que só sabemos ler), e o **registro** da localidade nos menus e eventos, que ninguém sabe como é feito.

**Conclusão:** o Ring não vira uma localidade nova com o que existe hoje. Não é questão de empacotar; faltam o formato de colisão e o registro. Isso já estava em `docs/demands/synthetic_track.md`.

## As quatro incógnitas

Cada experimento abaixo existe para fechar uma delas, uma por vez:

| | Pergunta | Como fechar |
| --- | --- | --- |
| U1 | O jogo aceita um `.nefs` que nós escrevemos (cabeçalho RSA, tabelas, blocos novos no fim)? | Carregar uma cópia modificada de uma pista que já carrega |
| U2 | Como entregar o `.nefs` **sem escrever na pasta do jogo**? | Redirecionar o `CreateFileW` do `.nefs` (hook que já existe para o trace) |
| U3 | O AutoStage consegue carregar uma pista que não seja a NZ `route_2`? A causa do crash de `twin_peaks` é a pista, o `free_roam` ou a gravação? | Testar rotas e pistas diferentes, só com dados originais |
| U4 | Que parte do Ring o jogo consegue mostrar sem colisão, IA e registro? | Usar uma pista do jogo como **hospedeira** e trocar o que for visual |

U1 e U3 estão misturadas no crash antigo. Se a primeira tentativa de levar o Ring misturar pista nova, `.nefs` novo e redirecionamento, um crash não diz qual dos três falhou. A ordem abaixo separa os três.

## Escada de experimentos

| # | O quê | Fecha | Toca no jogo? | Precisa de OK |
| --- | --- | --- | --- | --- |
| E0 (**feito**, ver abaixo) | Inventário offline: tamanho em blocos de cada arquivo que o Ring substituiria na hospedeira; por que `replace_files` exige o mesmo número de blocos e se dá para relaxar; espaço livre no último bloco do `objects.ens`; quantos registros de árvore e ornamento a hospedeira tem. | base de U4 | não | não |
| E1 (**feito**, ver abaixo) | Benchmark nativo + câmera livre (NZ `route_2`). Depois o override do AutoStage para outras rotas de NZ, e para uma localidade com rota normal (Montalegre) em vez de `free_roam`. Só dados originais. | U3 | sim, pasta intacta | para o jogo abrir sim; para Montalegre há risco de crash |
| E2 (**feito**, ver abaixo) | Redirecionamento do `CreateFileW`: `locations\<x>.nefs` → arquivo em `build/`. Primeiro uma cópia **idêntica byte a byte**, para provar o redirecionamento sem arriscar dado. | U2 | sim | cópia idêntica: não muda dado, mas é um passo novo; vou avisar antes |
| E3 | Uma cópia modificada com **um objeto movido para longe**, numa pista que o E1 provou que carrega. A diferença é visível e o resto fica igual. | U1 | sim | **sim, `.nefs` modificado** |
| E4 | Objetos do Ring na hospedeira: cada tipo do Ring vira um tipo parecido que já existe na hospedeira (barreira, muro de pneus, cone, árvore). Em `ornaments.bin` e `trees.bin` só dá para **reposicionar** os registros que existem; o `objects.ens` só cresce até o espaço livre do último bloco. | U4 (objetos) | sim | sim |
| E5 | Terreno do Ring sobre a hospedeira: manter a topologia, os UVs e os blocos de shader da hospedeira e **trocar só as posições** dos vértices pela altura do Ring (mesmo número de vértices). A colisão continua a da hospedeira. | U4 (terreno) | sim | sim |
| E6 | Texturas: trocar os pixels de texturas existentes pelas do Ring, no mesmo formato DXT e tamanho. Falta um codificador DXT1/DXT5 no repositório. | U4 (aparência) | sim | sim |
| E7 | Rota própria e colisão: escrever `progress_track.xml` e `ai_track.xml` e decifrar os `.vcqtc`. É o que deixaria dirigir no Ring. | jogável | sim | sim |

E0 a E3 não alteram nenhum dado do jogo além de um objeto no E3. E4 a E6 já dão um **mapa só para olhar**, que é o que o benchmark com câmera livre precisa. E7 é o único que muda a resposta para "dá para dirigir", e é o mais longo.

### Qual hospedeira

- **NZ `new_zealand_rally_01`, `route_2`:** é a única que sabemos que carrega pelo benchmark. Isso resolve U3 de graça e deixa U1 e U2 limpos. Custo: pacote de 1,95 GB, e hoje só o Montalegre está exportado.
- **Montalegre rallycross:** o menor pacote (884 MB), já exportado (324 tipos, 2897 instâncias), e já usado em todos os testes do viewer. Custo: depende de o AutoStage aguentar a troca de pista (U3).

O E1 mostrou que a troca de pista funciona, então a recomendação mudou: **Montalegre**, por ser o menor pacote, já estar exportado e carregar pelo AutoStage. A NZ fica de reserva.

## Resultado do E0 (2026-10-06, só leitura)

Script: `python3 scripts/research/nefs_inventory.py <pacote.nefs>`.

**Cabeçalho.** Os dois pacotes começam com `NeFS` em claro, sem intro RSA. Há 32 bytes em `+0x04` que parecem um hash do cabeçalho, mas não batem com SHA-256, SHA3-256 nem BLAKE2s de nenhuma janela do cabeçalho (início de 0 a 255, com o campo zerado ou não). **Não sabemos se o jogo confere esse campo.** É a parte principal de U1: só o E3 responde.

**A regra dos blocos é do nosso gravador, não do formato.** O intro não é assinado nestes pacotes, então o cabeçalho poderia crescer. Hoje `replace_files` mantém o cabeçalho do mesmo tamanho; afrouxar isso fica para depois, se precisar.

**Espaço dentro do bloco (folga no último bloco de 64 KiB):**

| Arquivo | Montalegre | NZ `route_2` |
| --- | --- | --- |
| `objects.ens` | 549 993 B, folga 39 831 B | 8 945 764 B, folga 32 668 B |
| `ornaments.bin` | 273 729 B (≈1290 registros), folga 53 951 B | 284 217 B, folga 43 463 B |
| `trees.bin` | 93 576 B (≈970 registros), folga 37 496 B | 4 091 880 B (≈42 600 registros), folga 36 888 B |
| `tracksplit.pssg` | 560 967 030 B, 8560 blocos, folga 21 130 B | 770 906 498 B, 11 764 blocos |
| `track.jpk` (colisão) | 3 177 520 B | 36 098 336 B (igual nas 6 rotas) |

Consequências:

- **Instâncias novas em `objects.ens`:** cabem poucas dezenas (≈40 KB de folga). O Ring tem ~1000 instâncias, então **reposicionar** as que a hospedeira já tem e esconder as que sobram (`y = -10000`, como o `edit.py` faz) é o que serve. Na NZ há dezenas de milhares de árvores e um `objects.ens` de 8,9 MB: sobra material.
- **Terreno:** o `tracksplit.pssg` é gigante por causa das texturas (561 MB no Montalegre, 771 MB na NZ). Trocar a geometria tem de manter o tamanho: só mexer nas posições dos vértices, sem mudar contagem.
- **Colisão:** na NZ o `track.jpk` é o mesmo arquivo (36 098 336 B) nas 6 rotas. A colisão é da pista, não da rota.
- **Tipos parecidos no Montalegre** (da lista do `track.json`): barreiras (`core_barr_rx_barriers_a`), muros de pneus (`core_barr_tyrewall_a`), alambrado (`core_barr_fence_standard_b`), arquibancadas (`mnt_grandstand_steps_*`, `core_lr_grandstand_*`), pórtico de largada (`mnt_startgantry_a`), placas (`core_brand_board_a`) e bétulas (`birch_02_*`). **Não há cones nem pinheiros**; esses viram outro tipo (um pneu, uma árvore diferente).

## Resultado do E1 (2026-10-06, no jogo, só dados originais)

O AutoStage foi ligado pelo `dr2hook_autostage.ini` (`enabled = 1`, `once = 1`, que se desliga sozinho depois do boot), com o jogo aberto pela Steam sem menus.

| Teste | Resultado |
| --- | --- |
| NZ `new_zealand_rally_01` / `route_2` / `fr5` | Carregou até a largada em ~15 s depois do processo. Câmera livre (F9, pelo canal `dr2hook_cmd.txt`): `FreeCamera: ligada` e `pose copiada da especial`. **O dono confirmou em 2026-10-06 que a câmera livre se move normalmente** (WASD e mouse). |
| NZ `route_0` (outra rota da mesma pista) | Carregou normal. `status` = `corrida` e o carro anda sozinho pela estrada. |
| **Montalegre** `portugal` / `montalegre_rallycross` / `route_0` | **Carregou e o carro anda sozinho** (`status` = `corrida`, print do asfalto com marcas de pneu e arquibancada). O log mostra `LoadTrace: open ...\locations\portugal__montalegre_rallycross.nefs`. |

**U3 fechada.** O AutoStage troca de rota e de pista sem crashar, inclusive para uma localidade de rallycross com a rota 0. Isso tira a gravação de NZ da lista de suspeitos do crash de `usa / twin_peaks / free_roam`. O que sobra: o `free_roam` (modo sem rota) ou o DirtFish em si. A causa exata ainda não foi isolada, mas não afeta o plano: as hospedeiras serão pistas normais.

**Consequências para a escolha da hospedeira:** o Montalegre serve tão bem quanto a NZ. Como é o menor pacote (884 MB, contra 1,95 GB) e já está exportado para o viewer, passa a ser a hospedeira recomendada. A NZ fica como reserva.

Cuidado ao rodar de novo: o jogo escreve `enabled = 0` no ini depois do boot com `once = 1`, mas as strings de pista ficam. Depois de cada teste o ini foi devolvido ao original (NZ `route_2`, `enabled = 0`, `once = 0`).

## Resultado do E2 (2026-10-06, no jogo, cópia idêntica)

Código: `src/core/path_redirect.cpp` e o hook do `CreateFileW` em `src/core/load_trace.cpp` (na `dxgi.dll`), com o teste `tests/test_path_redirect.cpp`. Configuração: `dr2hook_redirect.ini` ao lado do exe, uma regra por linha:

```
locations\portugal__montalegre_rallycross.nefs = Z:\home\deivison\Projetos\DR2ModLoader\build\redirect\montalegre_copia.nefs
```

- Só vale para `.nefs` aberto para leitura (`OPEN_EXISTING`). Se o destino não existe, a regra é ignorada com um aviso no log.
- O Wine mostra a pasta do jogo como `S:\...` e a raiz do Linux como `Z:\`.
- Teste: `build/redirect/montalegre_copia.nefs` (884 MB, `cmp` igual ao original) com o AutoStage em Montalegre `route_0`. O log mostrou `LoadTrace: redirect S:\...\locations\portugal__montalegre_rallycross.nefs -> Z:\...\montalegre_copia.nefs` (duas aberturas), o `/proc/<pid>/fd` do jogo apontava para a cópia, e o jogo chegou à largada normalmente.
- **U2 fechada:** dá para entregar ao jogo um `.nefs` de fora da pasta do jogo, sem escrever nela.
- A `dxgi.dll` instalada foi trocada pela de `build/win-redirect/`; a anterior ficou em `dxgi.dll.bak-before-redirect-20261006`.

**Preparação do E3 (offline, ainda não carregado no jogo):** `scripts/research/make_edit_test.py` monta um `edits.json` que sobe 40 m o pórtico de largada do Montalegre (`mnt_startgantry_a`, um registro em `route_0/ornaments.bin`), e `python -m tools.uiview.track.edit` grava `build/redirect/montalegre_edit.nefs`. Conferência: dos 67 arquivos com dados, **só `route_0/ornaments.bin` difere, em 1 byte**, e o tamanho e os blocos dos arquivos grandes são iguais. Carregado no jogo com o OK do dono (resultado abaixo).

**Resultado do E3 e U1 fechada (2026-10-06):** o primeiro `montalegre_edit.nefs` derrubou o jogo na abertura (`exe+0x816b13`). Controles, todos com o mesmo redirect e a mesma largada pelo AutoStage:

| Pacote | Cabeçalho | Resultado |
|---|---|---|
| cópia idêntica | igual | carrega |
| C: cópia + 64 KiB no fim | igual | carrega |
| D: mesmo conteúdo gravado por `replace_files` | 10 bytes diferem (1 offset em P1, 6 fins de bloco em P4) | **crash** |
| F: cópia com 1 bit trocado no campo de 32 bytes em +4 | 1 bit | **crash** |
| E3 recalculando o campo em +4 | P1/P4 novos + hash novo | **carrega** |

Causa: o campo de 32 bytes em +4 do cabeçalho é um **SHA-256 de `"NeFS"` + cabeçalho a partir do offset 0x24 até o fim do cabeçalho** (103.936 B no Montalegre; conferido com o original). O jogo o confere ao abrir o pacote (`0x14087f4e0` chama `0x14087d4f0`: inicializa o hash, atualiza com os 4 bytes do magic e com `header[0x24:tamanho]`, finaliza e compara com `header+4`; se diferir, o erro vira `0x140816b10`, que escreve em `*(int*)código` de propósito, daí o "acesso ao endereço 6"). O tamanho do arquivo e o conteúdo editado não importam. `tools/egodata/nefs_write.py` agora recalcula o hash (`header_hash`/`with_header_hash`) quando o intro do pacote está em claro, que é o caso dos pacotes de pista. Os pacotes de carros/UI com intro assinado ainda não passam por esse caminho.

Com isso, **a escrita de pacotes que o jogo aceita está resolvida**: o E3 (pórtico de largada subido 40 m, `build/redirect/montalegre_E3_hash.nefs`) carregou até a largada. A conferência visual da altura do pórtico ainda não foi feita (a câmera do replay/benchmark não coincide entre as execuções); usar a câmera livre (F9) no ponto de largada na próxima vez.

## Terreno do Ring no jogo (2026-10-06, em andamento)

Ferramentas novas: `BlockPatch` em `tools/egodata/nefs_write.py` (grava só os blocos de 64 KiB tocados e copia os outros sem descomprimir), `tools/uiview/track/terrain_patch.py` (localiza as malhas do `tracksplit.pssg` pelo deslocamento absoluto e edita posições, índices e caixas de culling no lugar), `scripts/research/terrain_probe.py` e `terrain_transplant.py`, e o comando remoto `cam` (ver `docs/guides/remote_commands.md`).

**Fatos novos**
- Um `tracksplit.pssg` editado de 561 MB vira um `.nefs` de ~1,35 GB em segundos (o arquivo vai inteiro para o fim do volume, mas só os blocos tocados são recomprimidos). Os vértices ficam em big-endian: posição `float3`, cor ARGB, `ST half4`.
- **T1 (jogo):** as 169 malhas de material `batchmaterial` (176 mil vértices, 262 mil triângulos, só posição + cor + ST, sem textura declarada) **são o terreno visível**. Jogar o `y` delas para -5000 apaga o chão e a pista, e ficam só os objetos sobre um plano claro (a paisagem de fundo). O carro do benchmark continua andando (a colisão é do `track.jpk`). A edição de vértices do `tracksplit.pssg` portanto chega ao jogo.
- Nas malhas `batchmaterial` o `ST` é sempre `(0,0,0,1)` e a cor ARGB tem A=0, R de 0 a 255, G=0, B=199: o jogo deriva o mapeamento de textura da posição no mundo, e R parece ser um peso de mistura. As matrizes `TRANSFORM` são identidade, então os vértices estão em coordenadas de mundo. O enrolamento dos triângulos do Ring (normal +Y em CCW no plano XZ) é o mesmo do jogo.
- A hierarquia de culling é `ROOTNODE > surface > LAND_i_j > RENDERNODE` com `BOUNDINGBOX` de 6 floats (mínimo e máximo, big-endian); os contêineres sem volume têm zeros.

**T2 (Ring em 22 das 169 malhas):** `terrain_transplant.py` empacota os 180 462 triângulos do Ring (terreno de 4 m e fundo reduzido à metade da resolução, sem o fundo dentro da área dos tiles) nas malhas `batchmaterial` de maior capacidade, completa com triângulos degenerados, esconde as outras 1906 malhas e desloca o Ring por `(8, 1334, -290)` para a largada coincidir com a da Montalegre. O arquivo relido tem os 180 462 triângulos, y de 1425 a 1573, x ±1300. No jogo:
- Com as caixas de culling **abertas** (`widen_bounding_boxes`), a simulação **parou**: o carro ficou preso na largada, a câmera livre não mexeu e a imagem ficou com camadas espelhadas e "tudo transparente". Hipótese: culling aberto faz o motor processar o mundo todo (sombras ou LOD) e o quadro trava.
- Com as caixas intactas (`--no-widen`), a câmera livre responde e o chão aparece **lavado de branco** (as árvores e os prédios aparecem boiando); **a geometria do Ring não é reconhecível**. O formato dos dados está certo, então a hipótese é que o sombreador do `batchmaterial` precisa de algo que não escrevemos (cor/peso por vértice, textura derivada da posição, ou os intervalos de mistura), ou que o plano claro é o próprio Ring sem textura.

**Mudança de direção (2026-10-06, a pedido do dono):** os passos abaixo (quadrado de teste etc.) ficam suspensos. Antes de qualquer novo transplante, fazer engenharia reversa do carregamento da pista (leitores de `tracksplit`/`objects.ens`/`track.jpk`, montagem do terreno e do culling, uso de cor/ST pelo sombreador do `batchmaterial`) e só então fornecer a pista por um caminho próprio, em vez de sobrescrever dados da hospedeira.

**Próximos passos propostos**
1. Isolar o sombreador: preencher **uma** malha com um quadrado plano de 500 m perto da largada, variando a cor do vértice (R = 0, 128, 255; B = 199 ou 0), e fotografar de cima com o `cam`. Isso diz se o chão branco é a nossa geometria e como a cor controla a aparência.
2. Copiar a cor dos vértices vizinhos da Montalegre (por posição) em vez de uma cor fixa, se a cor for peso de mistura.
3. Se a aparência não der certo com o `batchmaterial`, usar as malhas de terreno comuns (`terrain_wsm_*`, 12 texturas por material) e mapear materiais do Ring para materiais do hospedeiro.
4. Só depois, os objetos do Ring (E4) reposicionando instâncias de `objects.ens`/`ornaments.bin`/`trees.bin`.

**Armadilhas desta rodada**
- O core compilado do HEAD **cai** na carga da pista (acesso nulo em `exe+0x966a49`). O core que vinha funcionando foi feito do branch `backup/ghost-limit-wip-2026-10-05` (tem a correção do GhostLab/áudio, ainda não integrada ao HEAD). Para testar, foi compilado um core a partir de uma worktree desse branch (`build/core_wip`, saída em `build/win-corewip/dr2hook_core.dll`) com as três mudanças do `cam` copiadas por cima, e instalado na pasta do jogo (backup do anterior: `dr2hook_core.dll.bak-before-cam-20261006`). Nada disso foi commitado no branch de trabalho além do `cam`.
- **Não use F8 (recarregar o core) com o jogo em corrida:** derrubou o jogo (`GhostLab[crash]`). Reinicie o jogo a cada teste.
- Linhas antigas em `dr2hook_cmd.txt` são executadas no próximo boot: um `key f9` esquecido liga a câmera livre durante a carga e derruba o jogo. Apague o arquivo antes de abrir.
- O jogo pausou sozinho uma vez durante a captura de tela; `unpause` pelo canal remoto retoma, mas depois disso o `cam` não refletiu na imagem.

## Alternativa que não passa pelos `.nefs`

Desenhar o Ring **por cima** do jogo, na overlay do DR2Hook (que já tem o swapchain D3D11 hookado), usando a câmera livre para a visão. Não exige formato do jogo nem `.nefs`, nem decifrar colisão. Perde a iluminação, os materiais e a oclusão do jogo, e a projeção da câmera precisa ser lida do motor (hoje só a posição e a base da câmera livre são conhecidas). Serve para ver o mapa na janela do jogo, mas não é "o mapa dentro do jogo". Fica anotada como plano B, se o U1 der errado.

## Decisões em aberto

1. Qual hospedeira usar: NZ (sem risco de troca de pista, pacote maior) ou Montalegre.
2. Se o redirecionamento do `CreateFileW` entra na `dxgi.dll` (exige reiniciar o jogo, junto do `LoadTrace`) ou no core (recarrega com F8, mas só funciona se for instalado antes da abertura do `.nefs`).
3. Se o alvo é só ver o mapa (E4 a E6) ou dirigir nele (E7).

## Não fazer

- Gravar na pasta do jogo (nem a cópia do `.nefs`).
- Carregar um `.nefs` modificado sem OK, mesmo para um teste pequeno.
- Misturar pista nova, `.nefs` novo e redirecionamento no mesmo teste.
- Redistribuir assets do jogo: um pacote editado a partir de uma pista do jogo não pode ser compartilhado.
