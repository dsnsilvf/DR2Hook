# Carros fantasma (ghosts)

Investigação de 2026-10-02. Análise superficial minha + dois despachos para o Gemini 3.8 Flash (`agy`), cujos relatórios brutos estão em `investigations/gemini/`. Abaixo, **verificado** = conferido por mim nos bytes/binário; **hipótese** = afirmado pelo Gemini ou deduzido, ainda sem teste.

## 1. Arquivos salvos

Verificado:
- Ficam no **Steam Cloud**: `~/.local/share/Steam/userdata/<conta>/690790/remote/`. Um arquivo por fantasma, `savegame@ghosts#ENDFX-0..14.*` (21–70 KB), dois pequenos (`#GTSRB.UEL`, `#QKRHMYXE`, índices?) e o perfil (`savegame@profile#NXDSMWW.BWW`, 146 KB, + `profile_backup`). Nada em `Documents/My Games`.
- Todos **cifrados**: entropia ~8 bits/byte, tamanhos múltiplos de 16, e os mesmos 16 primeiros bytes em todos (`64 bb 12 42 3e 54 78 3e 40 78 db 46 bd 90 0d 73`; os `#QKRHMYXE` têm outro bloco, igual entre si) → cifra de bloco com chave/IV fixos ou ECB sobre um cabeçalho igual.
- `bcrypt.dll` é importado só para hash (`BCryptOpenAlgorithmProvider/CreateHash/HashData/FinishHash/...`) e `BCryptGenRandom`: a cifra está no próprio exe (chave possivelmente derivada de hash).
- Formato decifrado: o desserializador `0x1409d10b0` compara o magic **`GHST`** (`0x54534847`) em `0x1409d10e9`.

Serializador (Gemini, não conferido): `0x1409d7f00`. O formato está descrito em "Formato GHST", abaixo.

### Cifra e contêiner (resolvido em 2026-10-02)

Verificado (derivação reimplementada do zero em `tools/dr2save.py`; decifra os 21 arquivos copiados):
- **AES-256-ECB, sem IV**, chave **fixa** (igual para qualquer conta): `91d84b7138a2cc4dadc022db4ebd1edd6c3454746acb235b618b404170b86e71`.
- Derivação, na inicialização do jogo (`0x140527140..0x1405271ce`): `0x14009f4d0("rp17", 3)` (FNV-1a de `"rp1"`), `xor 0x37` e `* 0x1000193` completam o FNV-1a de `"rp17"` = `0x7dc96bc7`. `0x14080f360(cifra, 0x7dc96bc7, 2)` semeia um MT19937 (init LCG 69069, semente `| 1`, `0x140859b80`), gera 64 dígitos hex com `rand % 15` (nunca sai `F`) e passa a string a `0x140815510(cifra, 2 = 256 bits, hex)`, que converte os pares em 32 bytes e expande a chave (14 rodadas, `0x140805f10`).
- Objeto de cifra: 0x250 bytes, construtor `0x1407fbe00` (vtable `0x1412ca1f8`); `+0x240` = CPU tem AES-NI (`cpuid` ecx bit 25); `+0x30` escolhe decifrar (0) ou cifrar (1) em `0x1407fbd40`, com rotinas AES-NI (`0x140806xxx`) ou T-tables (Te0 em `0x1412c68d0`, rotinas `0x14080b290..0x14080bd90`). Guardado em `[sistema+0x1c08]` e entregue a `0x140cc3910` (objeto de 0x370 bytes, bloco 0x10).
- Contêiner decifrado: cabeçalho de 24 bytes `u32 versão = 4`, `u32 tamanho do cabeçalho = 24`, `u32 compressão = 2` (zlib), `u32 0`, `u64 tamanho descomprimido` (bate exatamente nos 18 contêineres), depois zlib. O payload é serialização EGO (começa com `37 dd bb 4e`).
- Fantasmas (`#ENDFX-N`): dentro do payload, a partir do byte 137, há um segundo stream zlib cujo conteúdo começa com **`GHST`** (21–98 KB).
- `#QKRHMYXE` (fantasmas, perfil e backup) não são contêineres: decifram para o texto `Save System 2 Demo - Display Name` seguido de bytes binários. `#GTSRB.UEL` é contêiner (1240 bytes, sem `GHST`). O perfil descomprime para 2 658 560 bytes.

Relatório bruto: `investigations/gemini/ghost-cipher.md`.

### Formato GHST (resolvido em 2026-10-02)

Verificado: `tools/dr2ghost.py` lê os 15 fantasmas até o último byte, com as contagens do cabeçalho batendo, e o ENDFX-9 (carregado no jogo naquele momento) é idêntico, amostra por amostra, aos arrays do slot na memória (§3, "Na memória").

```
"GHST"  u32 tamanho (= arquivo - 8)  u8 versão (7)  u8 máscara de canais (0x7f)
por bit da máscara:  u32 contagem, u8 tamanho da amostra no arquivo
u16 tamanho dos metadados, depois itens: u8 id, u8 len, valor
segmentos até o fim do arquivo:  u32 tamanho, u32 t0 (ms), registros
registro:  u8 delta_ms, u8 máscara, payload de cada canal marcado (em ordem de bit)
```

- O relógio começa em `t0` e soma `delta_ms` a partir do 2º registro. `ff 00` (delta 255, nenhum canal) só avança o tempo em intervalos maiores que 255 ms.
- Vários segmentos aparecem quando há salto de tempo (reset para a pista?). Muitos arquivos começam com um segmento vazio (tamanho 4, só `t0 = 0`).

| bit | canal | bytes | conteúdo | memória (amostra) |
|---|---|---|---|---|
| 0 | posição | 14 | 3 `float` + `u16` progresso (÷65535) | 0x30: `+0x10` vec3, `+0x20` progresso `float`, `+0x24` 4×`u16` (rodas?) |
| 1 | rotação | 4 | quaternion 4×`int8` (÷127), só quando muda | 0x18: `+0x10` os mesmos 4 bytes |
| 2 | entradas | 4 | 4×`u8` (acelerador 255, freio, direção?, marcha?) | 0x18: `+0x10` os mesmos bytes |
| 3 | 1 Hz | 4 | `float` (195.7, 54.5, −81.2… rumo em graus?) | 0x18: 12 bytes em outra forma, conversão desconhecida |
| 4 | rodas | 8 | 8×`u8` (÷127; 0x54 → 0,6614 parado) | 0x30: 8 `float`, ordem `0 1 4 5 2 3 6 7` |
| 5 | único | 4 | uma amostra no início (zeros) | não carregado no slot |
| 6 | eventos | 20 | marcadores (bytes 15–16 sobem 1, 2, 3…) | 0x28: `+0x10` os mesmos 20 bytes |

- A memória guarda uma amostra a menos por canal (a última do arquivo fica de fora).
- Metadados: `0` = texto de 4 dígitos que muda com o carro (igual a `slot+0xc0`); `3` = tempos de setor em ms (4 ou 8 setores; a soma é o tempo total); `4` = tempo total em ms; `1` (`float`, 1083.5 / 1162.5 / 1359.2), `10` e `15` acompanham o carro; `5`, `8`, `9` (≈0,97) mudam por volta; `11`, `12` (15), `2`, `13`, `14`, `16` (zeros) ainda sem significado.
- Intervalos reais: 133 / 267 / 1017 ms = 8 / 16 / 61 ticks de 60 Hz (o teste é `elapsed > período`, então cai no tick seguinte).

## 2. Sistemas no executável

Strings: `game::GhostLapSystem`, `ghost_lap_recorder` (0x141273588), `ghost_lap_manager` (0x141273570), `GhostData`, `ghost_data`, `ghost_index`, `ghostsys_start`, estados `StateSaveLoadGhost`, `StateTimetrialGhostSelect`, `StateTimeTrialGhostDownload`, `GhostCar.Download`, e na UI `ghost_1..3_visible` / `ghost_1..3_header` (até 3 fantasmas na seleção). O download de fantasmas do ranking é online (bloqueado pelo NetworkGuard).

## 3. Gravação e reprodução

Verificado:
- Amostragem por canais, com períodos em constantes `double`: **rotação a cada 0,125 s (8 Hz)** — `0x1412820a0`, lida em `0x1409d5b1e`; **posição a cada 0,25 s (4 Hz)** — `0x14139ad68`, lida em `0x1409d5bc3`. As duas leituras ficam no gravador `0x1409d59d0`.
- Funções citadas pelo Gemini começam onde ele indicou (via `.pdata`): `0x1409ce4d0`, `0x1409d59d0`, `0x1409cfd30`. Vtable `0x1412802e8` (GhostLapSystem) com slot 0 `0x14051eb10`.

Na memória (verificado ao vivo em 2026-10-02, especial da Finlândia pausada com fantasma):
- Único `GhostLapSystem` (vtable `0x1412802e8`), cujo `+0x18` aponta para o gerenciador. No gerenciador: `+0x18` = cabeça do `std::map` (MSVC; nó: esquerda/pai/direita, `+0x19` isnil, chave `+0x20` = participante, valor `+0x28` = slot), `+0x20` = tamanho (5), `+0x38` = gravador da corrida atual, `+0x60`/`+0x70` = dois pontos ao lado da chegada (postes da linha?).
- Slot (0x260): `+0x1a8` estado (2 = pronto); contagens `u32` em `+0x1ac` rotação, `+0x1b0` posição, `+0x1b4` entradas, `+0x1b8` 1 Hz, `+0x1bc` rodas, `+0x1c0` eventos; ponteiros em `+0x1c8` rotação, `+0x1d0` posição, `+0x1d8` entradas, `+0x1e0` 1 Hz, `+0x1e8` rodas, `+0x218` eventos; capacidades em `+0x220..+0x248` (gravador: 14400/7200/14400/1800/7200/1800 = 30 min). Toda amostra tem vtable própria por canal (`0x14139a550`, `…590`, `…5d8`, `…658`, `…6a0`, `…720`) e `u32` tempo em ms em `+0x08`.
- Dos 5 slots, só o 0 tinha dados (o fantasma); a vtable `0x1414277b0` não é do gravador (aparece 1284 vezes no heap, é de um subobjeto em `slot+0xa8`).
- A matriz de mundo do carro fantasma (4×4, translação em `+0x30`, cópia em `+0x40`) estava em t≈6,35 s do trajeto gravado; a do jogador, em t≈5,6 s.
- `EvaluateGhostState` `0x1409ce4d0` (desmontado): `rcx` = dono com `[rcx+0x20]` = slot; `rdx` = `&tempo` (8 bytes); `r9` = saída. Retorna 2 se `slot+0x1a8 != 2`. Antes de avaliar, soma ao tempo o `float` global `0x141f593e0` (0 por padrão; serve para adiantar/atrasar o fantasma) e, se as flags `0x141f593e4`/`e5` estiverem ligadas, mais dois ajustes (`0x1415b3d78`/`7c`, 1,0). Saída: `+0x00..0x30` rotação, `+0x30` posição, `+0x40` velocidade, `+0x50`, `+0x54` progresso, `+0x58` tempo, `+0x60` válido, `+0x61`, `+0x64/68/6c` entradas, `+0x70` rodas.

Hipóteses (Gemini; endereços vistos por ele no binário/dump, não testados em jogo):
- Canal de rotação reproduzido com SLERP (`0x14052cd90`, normalização em `0x1409ce9d6`).
- Posição interpolada com **Catmull-Rom** sobre 4 pontos (`0x1409cf1c4`; linear em `0x1409cf5ae`).
- Métricas a 1 Hz; gatilhos em checkpoints/splits.
- Reprodução guiada pelo tempo de corrida: `EvaluateGhostState(slot, &tempo, ?, out)` em `0x1409ce4d0` — `out+0x00` matriz de rotação, `+0x30` posição, `+0x40` velocidade.
- O carro fantasma é **só visual** (sem corpo Havok): atualizado em `0x14051845e` via `0x140db9410` (matriz), `0x140db90e0` (vel. linear), `0x140db8930` (vel. angular); shaders `ghost_car_depth`/`ghost_car_transparent`.
- `GhostLapSystem` (0xf0 bytes; ctor `0x1404f6e40`, dtor `0x1404fa700`); `ghost_lap_recorder` (0x258 bytes; ctor `0x1409cb190`; vtable `0x1414277b0`).
- Slots num `std::map<uint64_t, GhostSlot*>` em `ghostSys+0x18`; no dump de 2026-10-01 havia **5 slots ativos** (1 local + 4 fantasmas), participante 0x140 bytes, `GhostSlot` 0x260. Estado do slot em `+0x1a8` (2 = pronto?).
- Cópia da volta gravada para o slot permanente ao fim da especial: `CopyGhostLapData` `0x1409cfd30`.
- Limite de 3: da UI de seleção; o motor aceita N slots.

## 4. O que dá para fazer (avaliação)

1. **Diferença ao vivo para o fantasma**: FEITO e validado no jogo (§6, `GhostLab`).
2. **Injetar volta própria** (ex.: gravada a partir de um checkpoint) via `CopyGhostLapData` ou os buffers do slot. O caminho está provado pelas cópias (§6); falta a fonte da volta.
3. **Vários fantasmas**: 2 na tela validados (original + 1 cópia). Mais que 2 exige subir o limite de carros fantasma (§6.3).
4. **Fantasma sólido**: ainda não; três tentativas descartadas, próximo teste em §6.4.
5. Exportar/importar fantasmas (`GHST`): cifra e formato resolvidos (§1, `tools/dr2save.py`, `tools/dr2ghost.py`); falta recifrar e reempacotar o contêiner EGO.

## 5. Arquivos de apoio

- `tools/dr2save.py`: decifra os saves (contêiner e `GHST`).
- `tools/dr2ghost.py`: lê o `GHST` (cabeçalho, metadados, canais) e exporta a trajetória em CSV.
- `tools/pssg.py`: parser PSSG escrito pelo Gemini (usado no texto rico; ver `ui_tabs.md`).
- `investigations/gemini/ghost-runtime.md`, `investigations/gemini/pssg-ui-text.md`: relatórios brutos.

## 6. GhostLab: testes no jogo (2026-10-02, tarde)

Código: `src/core/ghost_lab.cpp` (core, recarrega com F8), opções no mod Practice Mode (*Live gap to ghost*, *Extra ghost copies*, *Ghost copy spacing*, *Ghost head start*, *Solid ghost car*) e Lua `Ghost.status/clone/setOpaque/setTimeOffset/setHud`. Branch `feat/ghost-live`. Relatórios brutos do Gemini usados aqui (fora do git): `captures/gemini/ghost_visual/REPORT.md` e `captures/gemini/ghost_transparency/REPORT.md`, ambos com erros pontuais corrigidos abaixo.

### 6.1 Diferença ao vivo (validado)

- Hook em `EvaluateGhostState` (`0x1409ce4d0`, thread do jogo): no fantasma de referência copia o trajeto do slot (canal de posição) e guarda a posição avaliada.
- A cada frame, jogador e fantasma são projetados no trajeto (segmento mais próximo, busca perto do último índice e no todo se ficar a > 30 m). Diferença = tempo do fantasma − tempo em que ele passou onde o jogador está; metros = distância acumulada no trajeto.
- HUD no overlay (topo central): vermelho = atrás, verde = à frente. Some na pausa (o jogo não avalia fantasmas pausado).

### 6.2 Cópias e o 2º carro (validado)

- Gerenciador global `[0x141695228]`; slots no `std::map` em `+0x18`/`+0x20` (§3).
- Controladores de carro fantasma: 5, de 0x100 bytes, contíguos; `+0x00` veículo, `+0x08` dono passado a `EvaluateGhostState`, `+0x28` slot, `+0x48` saída, `+0x58` tempo, `+0x60..+0x63` flags. Atualizador ~`0x140518400`: sai se `+0x63 == 0`; `+0x62` liga a chamada a `0x1409da680` a cada frame; `+0x61` dispara o reset `0x140512700`.
- Cópia: `CopyGhostLapData(dest, fonte, false)` (`0x1409cfd30`, aloca com o alocador do jogo via `0x1409cd610`) num slot livre + todos os tempos deslocados de k × espaçamento.
- O jogo cria só 2 veículos de fantasma (`car 2`, `car 3`; `car 1` = jogador). O controlador do `car 3` é avaliado, mas fica com `+0x62 = 0` e o carro não é desenhado. **Ligar `controlador+0x62 = 1` faz a cópia aparecer** (validado com o jogo recém-aberto). O jogo zera essa flag no Reiniciar; o mod religa a cada frame. `veículo+0x140` (ponteiro de volta, segundo o Gemini) não importa: fica 0 nos dois carros.
- `0x1409da680` mede a distância ao jogador e grava em `veículo+0x94` (`0x14099db20(veículo, v, 3, 0)`) um fator que vai de 1,0 (longe) a 0,5 (perto): é o esmaecimento por proximidade. Pode estar ligado à "aura" de §6.4.
- O jogo reorganiza os slots na largada, e uma versão anterior chegou a deixar o fantasma original deslocado 1 s. Desde `27697b3`, uma cópia é reconhecida pela volta (contagem de posições + 1ª posição), a fonte é a que começa mais cedo e o tempo original é restaurado se ela aparecer deslocada. Ainda falta confirmar no jogo que a cópia volta em vários Reiniciar seguidos.

### 6.3 Máximo de fantasmas

- 5 slots/controladores: `mov r13d, 5` em `0x1405ba800` (função `0x1405ba200`).
- 2 carros fantasma: no criador de veículos `0x140a882f9`, `mov eax, 2` em `0x140a8834f` e `mov r12d, 2` em `0x140a884d1` (conferido nos bytes).
- Para 3 ou mais: subir esses limites (até 5) e garantir os descritores de instância de render dos carros novos. Não testado.

### 6.4 Transparência (achada em 2026-10-02, falta virar código)

**Onde está:** a transparência é calculada **dentro do shader normal do carro**, a partir de um float4 por instância chamado `GhostCarValues`. Por isso trocar materiais, mudar o passe ou mexer no tipo do carro não mudava nada.

- No `shaderpack/dx11/shaderpack.pssg` (em `game_1.dat`, extraído com `tools/egodata`), o RDEF dos shaders de carro tem `GhostCarValues` em `$Globals+512`, float4, marcado como usado (ao lado de `MudValues`, `CarbonFiberColour` etc.). Os nomes `ghost_car_transparent`/`car_matt_ghost` não aparecem no pacote.
- No executável, o construtor do objeto de valores dos carros (`[0x14159d9e0+0xf8]`, a doc chamava de "`GhostCarValues`") monta em `0x14094571f` o descritor do parâmetro: `+0x1a0b8` = nome `"GhostCarValues"` (`0x1413916e0`), `+0x1a0c0` = ponteiro para o armazenamento em `+0x1a0d0` (vtable `0x141394690`). Em `+0x1a0d0+0x20` fica uma lista com **um ponteiro por carro** (3 na sessão: jogador + 2 fantasmas).
- Cada entrada (heap; endereço muda a cada execução):

  | offset | conteúdo | jogador | fantasma A | fantasma B |
  |---|---|---|---|---|
  | `+0x00` | float4 `GhostCarValues` | `1; 0; 1; 0` | `0,396; 0; 1; 1` | `0,220; 0; 1; 1` |
  | `+0x10` | posição do carro (x, y, z) | | | |
  | `+0x20` | `0,5; 0; 0,5; …` | | | |

  Leitura: `x` = opacidade, `w` = 1 marca "é fantasma".
- **Validado no jogo:** forçar `x = 1,0` (regravando a cada 2 ms via `/proc/<pid>/mem`) deixou **os dois fantasmas sólidos**, sem precisar Reiniciar. Um fantasma ficou só com `x = 1`, o outro com `x = 1` e `w = 0`; o usuário não notou diferença entre os dois, então `w` não parece ser necessário.
- **Pendente ("aura"):** o jogo tem um esmaecimento por proximidade. Perto do fantasma ele fica muito transparente; ao se afastar, vai até a transparência mínima, mas ainda meio opaco. Com `x` forçado o carro aparece opaco, mas o efeito dessa zona ainda aparece ao entrar ou sair dela. Investigar:
  - `0x1409da680` grava em `veículo+0x94`, via `0x14099db20(veículo, v, 3, 0)`, um fator de 1,0 a 0,5 (§6.2). Ver se esse fator ou o mesmo caminho escreve `GhostCarValues.x` ou outro canal (`y`, ou a entrada `+0x20`).
  - Achar quem escreve `entrada+0x00` a cada frame (breakpoint de escrita, ou varrer stores de float perto de `0x1409da680`/`0x14099db20`) e fazer o hook ali, em vez de regravar.
  - Para o mod: na opção *Solid ghost car*, gravar `x = 1` nas entradas dos fantasmas a cada frame (reconhecer o jogador por `w == 0`), ou fazer o hook em quem escreve.

Descartado (não muda a transparência):
- **Pular `0x14095fe90`** (troca dos 23 materiais pelos `*_ghost`, chamada por `GhostCarPlugin::Init` `0x140b94650` em `0x140b948c5`): roda uma vez por execução, na abertura. A opção *Solid ghost car* atual só faz isso, então hoje ela não tem efeito visível.
- **`veículo+0xbc = 0`** (tipo do carro: 0 = físico, 3 = fantasma; gravado em `0x140a8847f` e `0x140b9840d`): ao vivo dá colisão com o jogador na largada. Talvez sirva para um "rival físico".
- **`xrayEffectParameter`** (`[0x14159d9e0+0xf8]+0x19a10`, `0; 1,843; 1,775; 1`): `0x1409a2ee0` liga esse parâmetro aos 23 shaders **normais** (lista `+0x19940`; os `*_ghost` ficam em `+0x19968`, marcados por `0x1409a0be0` com `+0x195`, os normais com `+0x194`). Zerar ao vivo não mudou nada; provavelmente é o efeito de ver o fantasma através de obstáculos.
- **NOP no `jne` em `0x1409867cf`:** a rotina `0x140986320` monta a pintura (`%s_main_d.tga`, `%s_glass_d.tga`…) e só escolhe um byte passado a `0x14094f520` (1 no jogador, 0 nos outros). Sem efeito, mesmo recarregando a especial.
- **Modo de render por tipo** (`0x1409a9a34`): calcula modo 0 (tipo 0), 2 (tipo 2) ou 1 (outros) e chama `0x140db9230(objRender, modo)`, que grava `+0xd48` e mexe em máscaras (só o modo 2 difere). NOP do `jne` em `0x1409a9a3c` (todo veículo em modo 0): sem efeito.
- Passes `ghost_car_depth`/`ghost_car_alpha_tested`/`ghost_car_transparent`: IDs calculados por `0x1403ab930` no construtor base de estágio de render `0x1409cc4e0` (campos `+0x1080/+0x1088/+0x1090`, junto de `opaque_blend` `+0x1058` etc.). Não foi preciso seguir.
- `GhostedTransparencyManager`: instância em `[jogo+0x2968]`; `0x14050af10` liga/desliga (`+0x2a`) conforme `[[0x1416951f8]+0x3600]`; o update `0x140505530` só zera `+0x29/+0x2a`. Tarefa registrada como `"ghosted transparency"` em `0x14052b929`. Não parece ligado ao fantasma de contrarrelógio.

### 6.5 Formato e ferramentas

- `tools/dr2ghost.py` lê o `GHST` (de um save cifrado ou de um `.ghst`) e exporta a trajetória em CSV (§1).
- Leitura e escrita ao vivo durante os testes: `/proc/<pid>/mem` (o heap muda a cada execução; partir do global `[0x141695228]`).
